/*
 * naming-convention-fix.cpp - Refactoring frontend that emits
 * Replacements (in clang-apply-replacements YAML format) for the
 * auto-fixable subset of naming-convention violations.
 *
 * Workflow (two ClangTool passes):
 *   Pass 1 — walk every TU with the shared lib visitor; for each
 *            violation that has a non-empty `suggested_name` and a
 *            renameable kind, record a USR -> new-name entry in a
 *            global plan. Skip typedef-banned (Rule 4) — human
 *            judgment required per case.
 *   Pass 2 — walk every TU with our own visitor; for every decl and
 *            reference (DeclRefExpr, TagTypeLoc) whose canonical-decl
 *            USR appears in the plan, emit a Replacement at the
 *            identifier source location.
 *
 * Output: one YAML file per source file with replacements, suitable
 * for `clang-apply-replacements <out-dir>`.
 *
 * Auto-fix scope: Rules 1, 2, 3 (decl + use sites). Rule 4 (typedefs)
 * is reported by the analysis tool but never auto-fixed.
 */

#include "core.h"

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/Expr.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/TypeLoc.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Index/USRGeneration.h>
#include <clang/Lex/Lexer.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Core/Replacement.h>
#include <clang/Tooling/ReplacementsYaml.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/YAMLTraits.h>
#include <llvm/Support/raw_ostream.h>

#include <map>
#include <set>
#include <string>
#include <vector>

using namespace clang;
using namespace clang::tooling;
using yetty::naming::checker_visitor;
using yetty::naming::violation;
using yetty::naming::violation_kind;

static llvm::cl::OptionCategory ToolCategory("naming-convention-fix options");

static llvm::cl::opt<std::string> OutDir("out",
	llvm::cl::desc("Output directory for per-file YAML replacement files. "
		       "If empty, runs as dry-run (only prints the plan)."),
	llvm::cl::cat(ToolCategory));

static llvm::cl::opt<bool> Verbose("verbose",
	llvm::cl::desc("Print detailed progress"),
	llvm::cl::cat(ToolCategory));

static llvm::cl::list<std::string> OnlyKinds("only-kind",
	llvm::cl::desc("Restrict fixes to specific kinds (repeatable). "
		       "Values: function-prefix, struct-bound, struct, union, "
		       "enum, enum-const, variable."),
	llvm::cl::cat(ToolCategory));

struct Rename {
	std::string old_name;
	std::string new_name;
	violation_kind kind;
	std::string module;
};

/* USR -> rename plan, populated by pass 1, consumed by pass 2. */
static std::map<std::string, Rename> g_plan;

/*
 * Typedef substitution plan: typedef T;  underlying type `struct yetty_X`
 * (or one renamed to `struct yetty_X` by pass 1) becomes the substitution
 * `struct yetty_X`. Every TypedefTypeLoc referencing T gets rewritten;
 * the typedef declaration itself is deleted.
 */
struct TypedefSub {
	std::string typedef_name;   /* e.g. "yetty_yterm_terminal_t" */
	std::string replacement;    /* e.g. "struct yetty_yterm_terminal" */
};
static std::map<std::string, TypedefSub> g_typedef_subs; /* typedef USR -> sub */

/*
 * Pending typedef info captured during pass 1 (cannot resolve immediately
 * because the underlying struct may itself be in g_plan; we need the
 * final rename to compute the replacement string). Resolved after pass 1
 * completes via resolve_pending_typedefs().
 */
struct PendingTypedef {
	std::string typedef_usr;
	std::string typedef_name;
	std::string struct_usr;       /* canonical struct USR */
	std::string struct_orig_name; /* struct's name pre-rename */
};
static std::vector<PendingTypedef> g_pending_typedefs;
static int g_typedef_subs_skipped = 0;

/* Suggested-name collision detection: two distinct decls cannot both
 * rename to the same string. */
static std::set<std::string> g_taken_names;

/* file -> replacements list, populated by pass 2. */
static std::map<std::string, std::vector<Replacement>> g_replacements;

static int g_unfixable = 0;
static int g_collisions = 0;
static int g_fixes_emitted = 0;

static std::string usr_of(const Decl *d)
{
	if (!d)
		return "";
	llvm::SmallString<128> buf;
	if (clang::index::generateUSRForDecl(d, buf))
		return ""; /* generator returned true on failure */
	return buf.str().str();
}

static bool kind_renameable(violation_kind k)
{
	switch (k) {
	case violation_kind::function_prefix:
	case violation_kind::function_struct_bound:
	case violation_kind::struct_prefix:
	case violation_kind::union_prefix:
	case violation_kind::enum_prefix:
	case violation_kind::enum_constant_prefix:
	case violation_kind::variable_prefix:
		return true;
	case violation_kind::typedef_banned:
		return false;
	}
	return false;
}

static const char *kind_filter_name(violation_kind k)
{
	switch (k) {
	case violation_kind::function_prefix:        return "function-prefix";
	case violation_kind::function_struct_bound:  return "struct-bound";
	case violation_kind::struct_prefix:          return "struct";
	case violation_kind::union_prefix:           return "union";
	case violation_kind::enum_prefix:            return "enum";
	case violation_kind::enum_constant_prefix:   return "enum-const";
	case violation_kind::variable_prefix:        return "variable";
	case violation_kind::typedef_banned:         return "typedef";
	}
	return "?";
}

static bool kind_allowed_by_filter(violation_kind k)
{
	if (OnlyKinds.empty())
		return true;
	const char *name = kind_filter_name(k);
	for (const auto &s : OnlyKinds)
		if (s == name)
			return true;
	return false;
}

/* True if `name` ends with `suffix`. */
static bool ends_with(const std::string &name, const std::string &suffix)
{
	return name.size() >= suffix.size() &&
	       name.compare(name.size() - suffix.size(), suffix.size(),
			    suffix) == 0;
}

/*
 * If a typedef-banned violation's underlying type is a record, queue it
 * for later substitution. We can't compute the replacement now because
 * the struct may itself be in g_plan (renamed in this pass) — we need
 * the final post-rename name. Resolution happens after pass 1 finishes
 * via resolve_pending_typedefs().
 *
 * We also can't hold AST pointers across TU boundaries (they get
 * invalidated when the next TU is processed), so we capture stable
 * USR strings + names now.
 */
static void try_queue_typedef(const violation &v)
{
	if (v.kind != violation_kind::typedef_banned)
		return;
	const auto *td = dyn_cast<TypedefNameDecl>(v.decl);
	if (!td)
		return;
	QualType under = td->getUnderlyingType()
				 .getCanonicalType()
				 .getUnqualifiedType();
	if (!under->isRecordType())
		return;
	const RecordDecl *rd = under->getAsRecordDecl();
	if (!rd || !rd->getIdentifier())
		return;
	std::string struct_name = rd->getNameAsString();
	if (struct_name.empty())
		return;

	std::string td_usr = usr_of(td->getCanonicalDecl());
	if (td_usr.empty())
		return;
	std::string st_usr = usr_of(rd->getCanonicalDecl());
	g_pending_typedefs.push_back(PendingTypedef{
		td_usr, td->getNameAsString(), st_usr, struct_name,
	});
}

/* Pass-1 violation handler: build the rename plan. */
static void pass1_handler(const violation &v)
{
	/* Typedefs are not renamed — they're substituted-and-deleted via
	 * a separate plan, but only when their underlying type is a record
	 * we can refer to as `struct yetty_X` directly. */
	if (v.kind == violation_kind::typedef_banned) {
		try_queue_typedef(v);
		return;
	}
	if (!kind_renameable(v.kind))
		return;
	if (!kind_allowed_by_filter(v.kind))
		return;
	if (v.suggested_name.empty()) {
		g_unfixable++;
		return;
	}
	/* Externally-linked symbols (e.g. tree-sitter grammar entry points
	 * declared in our headers but defined in linked 3rdparty libs)
	 * cannot be auto-renamed — we don't own the definition site. */
	if (auto *fd = dyn_cast<FunctionDecl>(v.decl)) {
		if (!fd->getDefinition()) {
			g_unfixable++;
			return;
		}
		/* Inverse case: the function may be defined in our code but
		 * declared (as a callback contract) in a 3rdparty header
		 * — e.g. libslirp's `slirp_output`. If any redeclaration
		 * lives outside our tree, the rename would break the
		 * contract; skip. */
		if (v.context) {
			SourceManager &sm = v.context->getSourceManager();
			bool external_decl = false;
			for (const FunctionDecl *rd : fd->redecls()) {
				SourceLocation rl = sm.getExpansionLoc(
					rd->getLocation());
				if (rl.isInvalid())
					continue;
				if (sm.isInSystemHeader(rl))
					continue;
				std::string rfn = sm.getFilename(rl).str();
				if (rfn.empty())
					continue;
				if (!yetty::naming::module_for_path(rfn)
					     .has_value()) {
					external_decl = true;
					break;
				}
			}
			if (external_decl) {
				g_unfixable++;
				return;
			}
		}
	}
	if (auto *vd = dyn_cast<VarDecl>(v.decl)) {
		if (vd->hasExternalStorage() && !vd->getDefinition()) {
			g_unfixable++;
			return;
		}
	}
	/* Macro-generated `<X>_result` structs cannot be auto-renamed:
	 * every YETTY_OK(<X>, ...) / YETTY_ERR(<X>, ...) / YETTY_YRESULT_DECLARE
	 * invocation reconstructs the type via token-pasting on `<X>`, and
	 * macro arguments are not decl references — the AST visitor cannot
	 * see them. Renaming just the struct decl breaks every caller.
	 * Leave these for manual fix at the YETTY_YRESULT_DECLARE call site. */
	if ((v.kind == violation_kind::struct_prefix ||
	     v.kind == violation_kind::union_prefix) &&
	    ends_with(v.current_name, "_result")) {
		g_unfixable++;
		return;
	}
	std::string usr = usr_of(v.decl);
	if (usr.empty())
		return;
	auto it = g_plan.find(usr);
	if (it != g_plan.end())
		return; /* already planned */
	if (g_taken_names.count(v.suggested_name)) {
		if (Verbose) {
			llvm::errs() << "collision: '" << v.current_name
				     << "' -> '" << v.suggested_name
				     << "' already claimed; skipping\n";
		}
		g_collisions++;
		return;
	}
	g_plan[usr] = Rename{ v.current_name, v.suggested_name, v.kind,
			      v.module };
	g_taken_names.insert(v.suggested_name);
}

class Pass1Consumer : public ASTConsumer {
public:
	explicit Pass1Consumer(ASTContext &ctx)
		: visitor(ctx, &pass1_handler)
	{
	}

	void HandleTranslationUnit(ASTContext &ctx) override
	{
		visitor.TraverseDecl(ctx.getTranslationUnitDecl());
	}

private:
	checker_visitor visitor;
};

class Pass1Action : public ASTFrontendAction {
public:
	std::unique_ptr<ASTConsumer>
	CreateASTConsumer(CompilerInstance &ci, StringRef) override
	{
		return std::make_unique<Pass1Consumer>(ci.getASTContext());
	}
};

/*
 * Pass 2: walks every TU and emits a Replacement at every spot that
 * mentions a planned-rename decl by name — both the decl site and
 * every reference.
 */
class Pass2Visitor : public RecursiveASTVisitor<Pass2Visitor> {
public:
	explicit Pass2Visitor(ASTContext &ctx) : context(ctx)
	{
	}

	bool VisitFunctionDecl(FunctionDecl *fd)
	{
		/* C++ constructor names carry the parent class identifier
		 * but have no embedded TypeSourceInfo — VisitTagTypeLoc
		 * never sees them. Rewrite explicitly when the parent
		 * class is in the rename plan. Destructors are handled
		 * implicitly: their name `~Foo` embeds a TypeSourceInfo
		 * for `Foo` which VisitTagTypeLoc visits at the correct
		 * offset, so no special-case here. */
		if (auto *ctor = dyn_cast<CXXConstructorDecl>(fd)) {
			if (CXXRecordDecl *rec = ctor->getParent())
				rewrite_for(rec,
					    ctor->getNameInfo().getLoc(),
					    rec->getNameAsString());
			return true;
		}
		if (isa<CXXDestructorDecl>(fd))
			return true;
		if (fd->getIdentifier())
			rewrite_for(fd, fd->getLocation(),
				    fd->getNameAsString());
		return true;
	}

	bool VisitRecordDecl(RecordDecl *rd)
	{
		if (rd->getIdentifier())
			rewrite_for(rd, rd->getLocation(),
				    rd->getNameAsString());
		return true;
	}

	bool VisitEnumDecl(EnumDecl *ed)
	{
		if (ed->getIdentifier())
			rewrite_for(ed, ed->getLocation(),
				    ed->getNameAsString());
		return true;
	}

	bool VisitEnumConstantDecl(EnumConstantDecl *ec)
	{
		rewrite_for(ec, ec->getLocation(), ec->getNameAsString());
		return true;
	}

	bool VisitVarDecl(VarDecl *vd)
	{
		if (isa<ParmVarDecl>(vd))
			return true;
		if (!vd->hasGlobalStorage())
			return true;
		if (!vd->getDeclContext()->isFileContext())
			return true;
		rewrite_for(vd, vd->getLocation(), vd->getNameAsString());
		return true;
	}

	bool VisitDeclRefExpr(DeclRefExpr *e)
	{
		if (NamedDecl *nd = e->getDecl())
			rewrite_for(nd, e->getLocation(),
				    nd->getNameAsString());
		return true;
	}

	bool VisitTagTypeLoc(TagTypeLoc loc)
	{
		TagDecl *td = loc.getDecl();
		if (td && td->getIdentifier())
			rewrite_for(td, loc.getNameLoc(),
				    td->getNameAsString());
		return true;
	}

	/*
	 * Use site of a typedef (e.g. `foo_t x;`). When the typedef is
	 * in our substitution plan, replace `foo_t` with `struct yetty_X`.
	 */
	bool VisitTypedefTypeLoc(TypedefTypeLoc loc)
	{
		const TypedefNameDecl *td = loc.getTypedefNameDecl();
		if (!td)
			return true;
		std::string usr = usr_of(td->getCanonicalDecl());
		if (usr.empty())
			return true;
		auto it = g_typedef_subs.find(usr);
		if (it == g_typedef_subs.end())
			return true;
		emit(loc.getNameLoc(),
		     it->second.typedef_name.size(),
		     it->second.replacement);
		return true;
	}

	/*
	 * Replace the typedef declaration with a bare struct forward
	 * declaration. Removing the line entirely is unsafe: the original
	 * `typedef struct yetty_X foo_t;` line ALSO declares `struct
	 * yetty_X` at file scope (the typedef declarator implicitly
	 * forward-declares the struct in the enclosing scope). Erasing it
	 * causes any subsequent `struct yetty_X *` use in the same TU's
	 * declarations to forward-declare the struct in function-prototype
	 * scope only, leading to "conflicting types" errors.
	 *
	 * `struct yetty_X;` keeps the forward declaration intact while
	 * removing the offending typedef name.
	 */
	bool VisitTypedefDecl(TypedefDecl *td)
	{
		std::string usr = usr_of(td->getCanonicalDecl());
		if (usr.empty())
			return true;
		auto it = g_typedef_subs.find(usr);
		if (it == g_typedef_subs.end())
			return true;
		replace_typedef_with_fwd_decl(td, it->second.replacement);
		return true;
	}

private:
	void replace_typedef_with_fwd_decl(TypedefDecl *td,
					   const std::string &struct_form)
	{
		SourceManager &sm = context.getSourceManager();
		const LangOptions &lo = context.getLangOpts();
		SourceLocation begin = sm.getSpellingLoc(td->getBeginLoc());
		SourceLocation end = sm.getSpellingLoc(td->getEndLoc());
		if (begin.isInvalid() || end.isInvalid())
			return;
		auto next = Lexer::findNextToken(end, sm, lo);
		SourceLocation final_end =
			Lexer::getLocForEndOfToken(end, 0, sm, lo);
		if (next && next->is(tok::semi)) {
			final_end = Lexer::getLocForEndOfToken(
				next->getLocation(), 0, sm, lo);
		}
		if (final_end.isInvalid())
			return;
		StringRef fr = sm.getFilename(begin);
		if (fr.empty())
			return;
		std::string fname = fr.str();
		if (!path_in_scope(fname))
			return;
		unsigned begin_off = sm.getFileOffset(begin);
		unsigned end_off = sm.getFileOffset(final_end);
		if (end_off <= begin_off)
			return;
		Replacement rep(fname, begin_off, end_off - begin_off,
				struct_form + ";");
		g_replacements[fname].push_back(std::move(rep));
		g_fixes_emitted++;
	}

	bool path_in_scope(const std::string &fname)
	{
		if (fname.find("/3rdparty/") != std::string::npos)
			return false;
		if (fname.find("/src/libvterm") != std::string::npos)
			return false;
		if (fname.find("/src/tinyemu") != std::string::npos)
			return false;
		if (fname.find("/build-") != std::string::npos)
			return false;
		if (fname.find("/qa-tools/") != std::string::npos)
			return false;
		for (const char *root : {"/src/", "/include/", "/tools/", "/test/",
					 "/demo/"}) {
			if (fname.find(root) != std::string::npos)
				return true;
		}
		return false;
	}

	void rewrite_for(const Decl *d, SourceLocation loc,
			 const std::string &cur_name)
	{
		if (cur_name.empty())
			return;
		std::string usr = usr_of(d->getCanonicalDecl());
		if (usr.empty())
			return;
		auto it = g_plan.find(usr);
		if (it == g_plan.end())
			return;
		/* Defensive: only rewrite when the source token actually
		 * matches the name we expect (skip macro-generated places
		 * where the spelling differs from the AST name). */
		if (cur_name != it->second.old_name)
			return;
		emit(loc, it->second.old_name.size(), it->second.new_name);
	}

	void emit(SourceLocation loc, size_t length,
		  const std::string &new_text)
	{
		SourceManager &sm = context.getSourceManager();
		SourceLocation sloc = sm.getSpellingLoc(loc);
		if (sloc.isInvalid())
			return;
		if (sm.isInSystemHeader(sloc))
			return;
		StringRef fr = sm.getFilename(sloc);
		if (fr.empty())
			return;
		std::string fname = fr.str();
		/* Skip vendored / generated / build dirs. */
		if (fname.find("/3rdparty/") != std::string::npos)
			return;
		if (fname.find("/src/libvterm") != std::string::npos)
			return;
		if (fname.find("/src/tinyemu") != std::string::npos)
			return;
		if (fname.find("/build-") != std::string::npos)
			return;
		if (fname.find("/qa-tools/") != std::string::npos)
			return;
		/* Only rewrite under our project source roots. */
		bool ok = false;
		for (const char *root : {"/src/", "/include/", "/tools/", "/test/",
					 "/demo/"}) {
			if (fname.find(root) != std::string::npos) {
				ok = true;
				break;
			}
		}
		if (!ok)
			return;
		Replacement rep(sm, sloc,
				static_cast<unsigned>(length), new_text);
		g_replacements[fname].push_back(std::move(rep));
		g_fixes_emitted++;
	}

	ASTContext &context;
};

class Pass2Consumer : public ASTConsumer {
public:
	explicit Pass2Consumer(ASTContext &ctx) : visitor(ctx)
	{
	}

	void HandleTranslationUnit(ASTContext &ctx) override
	{
		visitor.TraverseDecl(ctx.getTranslationUnitDecl());
	}

private:
	Pass2Visitor visitor;
};

class Pass2Action : public ASTFrontendAction {
public:
	std::unique_ptr<ASTConsumer>
	CreateASTConsumer(CompilerInstance &ci, StringRef) override
	{
		return std::make_unique<Pass2Consumer>(ci.getASTContext());
	}
};

/*
 * Walk the queue of typedef-banned violations collected during pass 1
 * and decide which to substitute. We can only substitute when the
 * typedef's underlying struct ends up with a name we can refer to as
 * `struct yetty_X` (either it was already so, or it's been renamed to
 * one in g_plan). Otherwise the typedef stays — manual review.
 */
static void resolve_pending_typedefs()
{
	for (const auto &pt : g_pending_typedefs) {
		if (g_typedef_subs.count(pt.typedef_usr))
			continue;
		std::string struct_final_name = pt.struct_orig_name;
		auto plan_it = g_plan.find(pt.struct_usr);
		if (plan_it != g_plan.end())
			struct_final_name = plan_it->second.new_name;
		/* Only substitute when the resulting `struct X` is yetty-
		 * prefixed — anything else is a 3rdparty bridge or an
		 * orphan that needs human review. */
		const std::string yetty_prefix = "yetty_";
		if (struct_final_name.size() < yetty_prefix.size() ||
		    struct_final_name.compare(0, yetty_prefix.size(),
					      yetty_prefix) != 0) {
			g_typedef_subs_skipped++;
			continue;
		}
		g_typedef_subs[pt.typedef_usr] = TypedefSub{
			pt.typedef_name,
			"struct " + struct_final_name,
		};
	}
}

/*
 * Sort + de-dup replacements per file (same offset/length/text from
 * different TUs is normal when headers are shared). Then write one
 * YAML per file in clang-apply-replacements format.
 */
static int write_yaml(const std::string &out_dir)
{
	std::error_code ec;
	llvm::sys::fs::create_directories(out_dir);

	int idx = 0;
	int written = 0;
	for (auto &kv : g_replacements) {
		const std::string &file = kv.first;
		std::vector<Replacement> &reps = kv.second;
		if (reps.empty())
			continue;

		std::sort(reps.begin(), reps.end(),
			  [](const Replacement &a, const Replacement &b) {
				  if (a.getOffset() != b.getOffset())
					  return a.getOffset() < b.getOffset();
				  if (a.getLength() != b.getLength())
					  return a.getLength() < b.getLength();
				  return a.getReplacementText().str() <
					 b.getReplacementText().str();
			  });
		reps.erase(std::unique(reps.begin(), reps.end(),
				       [](const Replacement &a,
					  const Replacement &b) {
					       return a.getOffset() ==
							      b.getOffset() &&
						      a.getLength() ==
							      b.getLength() &&
						      a.getReplacementText() ==
							      b.getReplacementText();
				       }),
			   reps.end());

		TranslationUnitReplacements tu;
		tu.MainSourceFile = file;
		tu.Replacements = reps;

		std::string out_path = out_dir + "/naming-" +
				       std::to_string(idx++) + ".yaml";
		llvm::raw_fd_ostream os(out_path, ec);
		if (ec) {
			llvm::errs() << "cannot write " << out_path << ": "
				     << ec.message() << "\n";
			return 1;
		}
		llvm::yaml::Output yamlOut(os);
		yamlOut << tu;
		written++;
	}
	llvm::errs() << "wrote " << written << " YAML file(s) to " << out_dir
		     << "\n";
	return 0;
}

static void print_plan_summary()
{
	std::map<std::string, int> by_kind;
	for (auto &kv : g_plan)
		by_kind[kind_filter_name(kv.second.kind)]++;
	llvm::errs() << "rename plan: " << g_plan.size() << " total\n";
	for (auto &kv : by_kind)
		llvm::errs() << "  " << kv.first << ": " << kv.second << "\n";
	llvm::errs() << "  collisions skipped: " << g_collisions << "\n";
	llvm::errs() << "  unfixable (no suggested name): " << g_unfixable
		     << "\n";
	llvm::errs() << "typedef substitutions: " << g_typedef_subs.size()
		     << " (skipped: " << g_typedef_subs_skipped << ")\n";
}

int main(int argc, const char **argv)
{
	auto ep = CommonOptionsParser::create(argc, argv, ToolCategory);
	if (!ep) {
		llvm::errs() << ep.takeError();
		return 1;
	}
	auto &options = ep.get();
	ClangTool tool(options.getCompilations(),
		       options.getSourcePathList());

	llvm::errs() << "[pass 1] collecting rename plan\n";
	int rc = tool.run(newFrontendActionFactory<Pass1Action>().get());
	resolve_pending_typedefs();
	print_plan_summary();
	if (g_plan.empty() && g_typedef_subs.empty()) {
		llvm::errs() << "no fixes to emit\n";
		return rc;
	}

	llvm::errs() << "[pass 2] rewriting decls + use sites\n";
	rc |= tool.run(newFrontendActionFactory<Pass2Action>().get());
	llvm::errs() << "fixes emitted: " << g_fixes_emitted
		     << " across " << g_replacements.size() << " file(s)\n";

	if (OutDir.empty()) {
		llvm::errs() << "(no --out specified, dry-run; "
				"re-run with --out=<dir> to write YAML)\n";
		return rc;
	}
	rc |= write_yaml(OutDir);
	llvm::errs() << "next step: clang-apply-replacements-18 " << OutDir
		     << "\n";
	return rc;
}
