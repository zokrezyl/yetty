/*
 * out-param-checker.cpp - Static analysis for decomposed-value out-parameters.
 *
 * The smell this flags: a function that hands a small, naturally-whole value
 * back to the caller by decomposing it into several scalar pointer
 * out-parameters, instead of returning the value itself. In a codebase where
 * every fallible function already returns a Result type, the correct shape is
 * to return the small aggregate by value (wrapped in a `*_result`), not to
 * scatter its components across `int *` / `float *` out-parameters:
 *
 *     // BAD - caller must receive the pieces through pointers
 *     void yetty_foo_get_origin(const struct foo *foo, int *out_x, int *out_y);
 *     void yetty_foo_get_size(const struct foo *foo, int *out_w, int *out_h);
 *
 *     // GOOD - return the whole small value (as a Result)
 *     struct point_result yetty_foo_get_origin(const struct foo *foo);
 *     struct size_result  yetty_foo_get_size(const struct foo *foo);
 *
 * The simple, low-false-positive signal is: TWO OR MORE non-const pointer
 * out-parameters of the SAME scalar arithmetic type (two `int *`, three
 * `float *`, ...). Two scalars handed back through pointers is already a
 * smell — `x`/`y`, `width`/`height`, `rows`/`cols`, `r`/`g`/`b` are the
 * canonical cases.
 *
 *   --check-out-param-cluster   (default: on)
 *       Flag any first-party function with >= MinCount same-typed non-const
 *       pointer-to-scalar out-parameters.
 *
 *   --min-count <N>   (default: 2)
 *       Minimum number of same-typed scalar out-parameters that trips the
 *       check.
 *
 * Deliberately NOT flagged (kept out of the simple form to stay quiet):
 *   - mixed-type scalar out-parameters (e.g. one `int *` and one `float *`),
 *   - `const T *` inputs (read-only, not out-parameters),
 *   - `char *` / `unsigned char *` (strings and byte buffers),
 *   - `bool *` flags, enum pointers, struct/array/`void *`/double pointers.
 * A genuine in/out pair such as `swap(int *a, int *b)` will trip the check;
 * that is a rare, accepted false positive.
 *
 * Usage:
 *   out-param-checker <source-files> -- [compiler-flags]
 *   out-param-checker -p build/ <source-files>
 */

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/Type.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Support/CommandLine.h>

#include <map>
#include <set>
#include <string>
#include <vector>

using namespace clang;
using namespace clang::tooling;

static llvm::cl::OptionCategory ToolCategory("out-param-checker options");

static llvm::cl::opt<bool> Verbose("verbose",
	llvm::cl::desc("Print verbose output"), llvm::cl::cat(ToolCategory));

static llvm::cl::opt<bool> CheckOutParamCluster("check-out-param-cluster",
	llvm::cl::desc("Flag functions with >= min-count same-typed non-const "
		       "pointer-to-scalar out-parameters (default: on)"),
	llvm::cl::init(true), llvm::cl::cat(ToolCategory));

static llvm::cl::opt<unsigned> MinCount("min-count",
	llvm::cl::desc("Minimum number of same-typed scalar out-parameters that "
		       "trips the check (default: 2)"),
	llvm::cl::init(2), llvm::cl::cat(ToolCategory));

/*
 * Running totals, owned by main() and threaded through the action factory so no
 * file-scope mutable state is needed. The `reported` set dedupes across
 * translation units: an offending function declared in a header is seen once
 * per .c that includes it, but must be reported only once.
 */
struct CheckCounters {
	int cluster = 0;
	std::set<std::string> reported;
};

/*
 * A source location is "foreign" - outside our own first-party code - when it
 * sits in a system header or under a bundled third-party tree. We enforce the
 * convention on yetty code only; third-party libraries have their own API
 * idioms and flagging them is pure noise.
 */
static bool is_foreign_location(SourceLocation loc, const SourceManager &sm)
{
	if (loc.isInvalid())
		return false;
	if (sm.isInSystemHeader(loc))
		return true;
	StringRef filename = sm.getFilename(sm.getSpellingLoc(loc));
	/* Bundled third-party trees inside the repo. The vendored libvterm and
	 * tinyemu live directly under src/ (not /3rdparty/ or /_deps/), so name
	 * them explicitly - their headers declare APIs we do not own. */
	if (filename.contains("/3rdparty/") || filename.contains("/_deps/") ||
	    filename.contains("/libvterm-") || filename.contains("/tinyemu/"))
		return true;
	/* External libraries pulled in via plain `-I` (not `-isystem`) are NOT
	 * flagged by isInSystemHeader - e.g. freetype under /usr/include, or a
	 * Nix-store dependency. Their headers live outside our source tree and
	 * their APIs are not ours to reshape, so treat the standard external
	 * include roots as foreign too. */
	return filename.starts_with("/usr/") ||
	       filename.starts_with("/nix/store/") ||
	       filename.starts_with("/opt/");
}

/*
 * A "decomposable scalar" is a plain arithmetic component a small value would
 * be built from: an integer (any width, signed or unsigned) or a floating-point
 * number. Deliberately excludes bool (flags), characters (strings / byte
 * buffers) and enums (discrete states, not geometric components).
 */
static bool is_decomposable_scalar(QualType type)
{
	const Type *canonical = type.getCanonicalType().getTypePtr();
	if (!canonical)
		return false;
	if (canonical->isRealFloatingType())
		return true;
	if (!canonical->isIntegerType())
		return false;
	return !canonical->isBooleanType() && !canonical->isAnyCharacterType() &&
	       !canonical->isEnumeralType();
}

/*
 * If `param` is a non-const pointer to a decomposable scalar, return the
 * canonical, unqualified pointee type name used to group like-typed
 * out-parameters together (so two `uint32_t *` cluster, but a `uint32_t *` and
 * an `int *` do not). Otherwise return the empty string.
 */
static std::string scalar_out_param_group(const ParmVarDecl *param)
{
	const PointerType *pointer =
		param->getType().getCanonicalType()->getAs<PointerType>();
	if (!pointer)
		return std::string();

	QualType pointee = pointer->getPointeeType();
	if (pointee.isConstQualified())
		return std::string();
	if (!is_decomposable_scalar(pointee))
		return std::string();

	return pointee.getCanonicalType().getUnqualifiedType().getAsString();
}

/*
 * Visitor that flags each function whose parameter list carries a cluster of
 * same-typed scalar out-parameters.
 */
class OutParamVisitor : public RecursiveASTVisitor<OutParamVisitor> {
public:
	OutParamVisitor(ASTContext &ctx, CheckCounters &counters)
		: context(ctx), counters(counters)
	{
	}

	bool VisitFunctionDecl(FunctionDecl *func)
	{
		if (!CheckOutParamCluster)
			return true;

		/* Report once per function, at a stable site: the first
		 * declaration (usually the header prototype). Analysing the
		 * canonical decl makes the result independent of which
		 * redeclaration happened to be visited first. */
		const FunctionDecl *canonical = func->getCanonicalDecl();
		if (!canonical)
			canonical = func;
		if (canonical->isImplicit())
			return true;

		SourceManager &sm = context.getSourceManager();
		SourceLocation loc = canonical->getLocation();
		if (loc.isInvalid() || is_foreign_location(loc, sm))
			return true;

		if (canonical->getNameAsString() == "main")
			return true;

		/* Group the scalar out-parameters by pointee type. */
		std::map<std::string, std::vector<const ParmVarDecl *>> groups;
		for (const ParmVarDecl *param : canonical->parameters()) {
			std::string group = scalar_out_param_group(param);
			if (!group.empty())
				groups[group].push_back(param);
		}

		/* Collect, in declaration order, every parameter that belongs to
		 * a group large enough to trip the check. */
		std::vector<const ParmVarDecl *> offenders;
		for (const ParmVarDecl *param : canonical->parameters()) {
			std::string group = scalar_out_param_group(param);
			if (!group.empty() && groups[group].size() >= MinCount)
				offenders.push_back(param);
		}

		if (offenders.empty())
			return true;

		std::string filename = sm.getFilename(sm.getSpellingLoc(loc)).str();
		unsigned line = sm.getSpellingLineNumber(loc);
		std::string key = filename + ":" + std::to_string(line) + ":" +
				  canonical->getNameAsString();
		if (!counters.reported.insert(key).second)
			return true;

		report_violation(canonical, offenders);
		return true;
	}

private:
	void report_violation(const FunctionDecl *func,
			      const std::vector<const ParmVarDecl *> &offenders)
	{
		SourceManager &sm = context.getSourceManager();
		SourceLocation loc = func->getLocation();
		std::string filename = sm.getFilename(sm.getSpellingLoc(loc)).str();
		unsigned line = sm.getSpellingLineNumber(loc);

		std::string list;
		for (size_t i = 0; i < offenders.size(); ++i) {
			if (i)
				list += ", ";
			list += offenders[i]->getType().getAsString();
			std::string name = offenders[i]->getNameAsString();
			if (!name.empty()) {
				list += ' ';
				list += name;
			}
		}

		llvm::errs() << filename << ":" << line << ": warning: '"
			     << func->getNameAsString() << "' returns "
			     << offenders.size()
			     << " scalar values through pointer out-parameters ("
			     << list
			     << ") - return a small value struct by value (a "
			     << "*_result) instead of decomposing it into "
			     << "separate pointers\n";

		for (const ParmVarDecl *param : offenders) {
			SourceLocation param_loc = param->getLocation();
			std::string param_file =
				sm.getFilename(sm.getSpellingLoc(param_loc)).str();
			unsigned param_line =
				sm.getSpellingLineNumber(param_loc);
			llvm::errs() << "    " << param_file << ":" << param_line
				     << ": note: out-parameter '"
				     << param->getNameAsString() << "' has type '"
				     << param->getType().getAsString() << "'\n";
		}

		llvm::errs() << "\n";
		counters.cluster++;
	}

	ASTContext &context;
	CheckCounters &counters;
};

/*
 * AST consumer that runs the visitor over the translation unit.
 */
class OutParamConsumer : public ASTConsumer {
public:
	OutParamConsumer(ASTContext &ctx, CheckCounters &counters)
		: visitor(ctx, counters)
	{
	}

	void HandleTranslationUnit(ASTContext &ctx) override
	{
		visitor.TraverseDecl(ctx.getTranslationUnitDecl());
	}

private:
	OutParamVisitor visitor;
};

/*
 * Frontend action that builds the consumer.
 */
class OutParamAction : public ASTFrontendAction {
public:
	explicit OutParamAction(CheckCounters &counters) : counters(counters)
	{
	}

	std::unique_ptr<ASTConsumer>
	CreateASTConsumer(CompilerInstance &ci, StringRef file) override
	{
		if (Verbose)
			llvm::errs() << "Analyzing: " << file << "\n";
		return std::make_unique<OutParamConsumer>(ci.getASTContext(),
							  counters);
	}

private:
	CheckCounters &counters;
};

/*
 * Factory for creating frontend actions.
 */
class OutParamActionFactory : public FrontendActionFactory {
public:
	explicit OutParamActionFactory(CheckCounters &counters)
		: counters(counters)
	{
	}

	std::unique_ptr<FrontendAction> create() override
	{
		return std::make_unique<OutParamAction>(counters);
	}

private:
	CheckCounters &counters;
};

int main(int argc, const char **argv)
{
	auto expected_parser =
		CommonOptionsParser::create(argc, argv, ToolCategory);
	if (!expected_parser) {
		llvm::errs() << expected_parser.takeError();
		return 1;
	}

	CommonOptionsParser &options = expected_parser.get();
	ClangTool tool(options.getCompilations(), options.getSourcePathList());

	CheckCounters counters;
	OutParamActionFactory factory(counters);
	int result = tool.run(&factory);

	if (counters.cluster > 0) {
		llvm::errs() << "Decomposed-value out-parameter violations: "
			     << counters.cluster << "\n";
		return 1;
	}

	if (result == 0 && Verbose)
		llvm::errs() << "No decomposed-value out-parameters found.\n";

	return result;
}
