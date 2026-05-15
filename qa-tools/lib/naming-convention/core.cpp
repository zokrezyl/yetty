/*
 * core.cpp - Naming-convention checker implementation shared by the
 * analysis and refactoring frontends.
 */

#include "core.h"

#include <clang/AST/Attr.h>

#include <algorithm>
#include <cctype>
#include <vector>

using namespace clang;

namespace yetty {
namespace naming {

const char *kind_label(violation_kind k)
{
	switch (k) {
	case violation_kind::function_prefix:
	case violation_kind::function_struct_bound:
		return "function";
	case violation_kind::struct_prefix:
		return "struct";
	case violation_kind::union_prefix:
		return "union";
	case violation_kind::enum_prefix:
		return "enum";
	case violation_kind::enum_constant_prefix:
		return "enum constant";
	case violation_kind::variable_prefix:
		return "variable";
	case violation_kind::typedef_banned:
		return "typedef";
	}
	return "?";
}

std::optional<std::string> module_for_path(const std::string &path)
{
	static const std::vector<std::string> roots = {
		"/src/yetty/",
		"/include/yetty/",
	};
	for (const auto &root : roots) {
		size_t pos = path.find(root);
		if (pos == std::string::npos)
			continue;
		size_t start = pos + root.size();
		size_t end = path.find('/', start);
		if (end == std::string::npos)
			continue;
		std::string mod = path.substr(start, end - start);
		std::replace(mod.begin(), mod.end(), '-', '_');
		return mod;
	}
	return std::nullopt;
}

SourceLocation effective_loc(SourceManager &sm, SourceLocation loc)
{
	return sm.getExpansionLoc(loc);
}

static std::string to_upper(std::string s)
{
	for (char &c : s)
		c = static_cast<char>(
			std::toupper(static_cast<unsigned char>(c)));
	return s;
}

static bool starts_with(const std::string &name, const std::string &prefix)
{
	return name.size() >= prefix.size() &&
	       name.compare(0, prefix.size(), prefix) == 0;
}

/*
 * The prefix must match AND something must follow it (so that
 * "yetty_<module>_" alone is rejected).
 */
static bool has_prefix_with_tail(const std::string &name,
				 const std::string &prefix)
{
	return starts_with(name, prefix) && name.size() > prefix.size();
}

static std::string strip_struct_quals(std::string name)
{
	while (true) {
		if (starts_with(name, "const "))
			name = name.substr(6);
		else if (starts_with(name, "volatile "))
			name = name.substr(9);
		else
			break;
	}
	if (starts_with(name, "struct "))
		name = name.substr(7);
	return name;
}

/*
 * If `t` is `struct yetty_<module>_<S> *` (any cv-qualifiers), return S.
 * Otherwise return empty string.
 */
static std::string struct_arg_subname(QualType t, const std::string &module)
{
	if (!t->isPointerType())
		return "";
	QualType pointee = t->getPointeeType();
	std::string n = strip_struct_quals(pointee.getCanonicalType()
						   .getUnqualifiedType()
						   .getAsString());
	std::string prefix = "yetty_" + module + "_";
	if (!starts_with(n, prefix))
		return "";
	std::string tail = n.substr(prefix.size());
	if (tail.empty())
		return "";
	return tail;
}

/*
 * Suggest a name with the required prefix.
 *
 *   1. Already correctly prefixed → no fix.
 *   2. Already starts with "<module>_" (bare module token, no "yetty_")
 *      → just prepend "yetty_" → "yetty_<module>_<rest>" (avoids the
 *      ugly "yetty_<module>_<module>_<rest>" double-prefix).
 *   3. Starts with "yetty_<wrong>_" → strip the wrong module token and
 *      prepend the correct module → "yetty_<module>_<rest>".
 *   4. Starts with "yetty_" but with no second underscore (e.g.
 *      "yetty_main") → ambiguous, leave for human.
 *   5. Otherwise prepend the full module prefix.
 */
static std::string suggest_with_prefix(const std::string &name,
				       const std::string &mod_prefix,
				       const std::string &module)
{
	if (starts_with(name, mod_prefix))
		return "";
	std::string mod_token = module + "_";
	if (starts_with(name, mod_token))
		return "yetty_" + name;
	if (starts_with(name, "yetty_")) {
		std::string after_yetty = name.substr(6); /* skip "yetty_" */
		size_t us = after_yetty.find('_');
		if (us == std::string::npos)
			return "";
		std::string after_module = after_yetty.substr(us + 1);
		if (after_module.empty())
			return "";
		return mod_prefix + after_module;
	}
	return mod_prefix + name;
}

static std::string suggest_upper_with_prefix(const std::string &name,
					     const std::string &up_prefix,
					     const std::string &module)
{
	if (starts_with(name, up_prefix))
		return "";
	std::string up_module = to_upper(module) + "_";
	if (starts_with(name, up_module))
		return "YETTY_" + name;
	if (starts_with(name, "YETTY_")) {
		std::string after_yetty = name.substr(6); /* skip "YETTY_" */
		size_t us = after_yetty.find('_');
		if (us == std::string::npos)
			return "";
		std::string after_module = after_yetty.substr(us + 1);
		if (after_module.empty())
			return "";
		return up_prefix + after_module;
	}
	return up_prefix + name;
}

/*
 * For a function whose first arg is `struct yetty_<mod>_<S> *` and whose
 * current name is `yetty_<mod>_<rest>` (rest doesn't already start with
 * "<S>_"), suggest `yetty_<mod>_<S>_<rest>`.
 */
static std::string suggest_struct_bound(const std::string &name,
					const std::string &mod_prefix,
					const std::string &subname)
{
	if (!starts_with(name, mod_prefix))
		return "";
	std::string rest = name.substr(mod_prefix.size());
	std::string need_in_rest = subname + "_";
	if (starts_with(rest, need_in_rest))
		return "";
	return mod_prefix + subname + "_" + rest;
}

checker_visitor::checker_visitor(ASTContext &ctx, violation_handler h)
	: context(ctx), handler(std::move(h))
{
}

/* True if `d` is enclosed by any anonymous namespace — those decls have
 * internal linkage in C++, comparable to `static` in C, and should not
 * be subject to the external-symbol naming convention. */
static bool in_anonymous_namespace(const Decl *d)
{
	for (const DeclContext *dc = d->getDeclContext(); dc;
	     dc = dc->getParent()) {
		if (const auto *ns = llvm::dyn_cast<NamespaceDecl>(dc)) {
			if (ns->isAnonymousNamespace())
				return true;
		}
	}
	return false;
}

/* True if the path's extension marks it as a C++ source/header.
 * The yetty naming convention is C-specific — symbols declared in
 * C++ TUs follow C++ conventions (and may bridge to 3rdparty C++
 * APIs that we cannot rename). Skip them entirely. */
static bool is_cxx_path(const std::string &path)
{
	static const std::vector<std::string> exts = {
		".cpp", ".cc", ".cxx", ".hpp", ".hh", ".hxx", ".mm",
	};
	for (const auto &e : exts) {
		if (path.size() >= e.size() &&
		    path.compare(path.size() - e.size(), e.size(), e) == 0)
			return true;
	}
	return false;
}

std::optional<std::string> checker_visitor::decl_module(const Decl *d)
{
	SourceManager &sm = context.getSourceManager();
	SourceLocation loc = effective_loc(sm, d->getLocation());
	if (loc.isInvalid() || sm.isInSystemHeader(loc))
		return std::nullopt;
	if (in_anonymous_namespace(d))
		return std::nullopt;
	std::string fname = sm.getFilename(loc).str();
	if (fname.empty())
		return std::nullopt;
	if (is_cxx_path(fname))
		return std::nullopt;
	return module_for_path(fname);
}

bool checker_visitor::already_seen(const Decl *d)
{
	const Decl *c = d->getCanonicalDecl();
	return !seen.insert(c).second;
}

bool checker_visitor::VisitFunctionDecl(FunctionDecl *func)
{
	auto mod = decl_module(func);
	if (!mod)
		return true;
	if (already_seen(func))
		return true;
	if (func->getStorageClass() == SC_Static)
		return true;
	/* Only flag the definition, never plain declarations. An extern
	 * declaration of a function from another module (e.g. `extern void
	 * yetty_yplatform_get_cache_dir(void);` inside ydraw-canvas.c)
	 * would otherwise be flagged as "should be yetty_ydraw_..." even
	 * though the function rightly belongs to yplatform. The definition
	 * site (which is what we own and what affects ABI) is the right
	 * place to enforce the rule. */
	if (!func->isThisDeclarationADefinition())
		return true;

	std::string name = func->getNameAsString();
	if (name == "main")
		return true;

	bool is_external_cb = false;
	for (const auto *attr : func->specific_attrs<AnnotateAttr>()) {
		if (attr->getAnnotation() == "yetty_external_callback") {
			is_external_cb = true;
			break;
		}
	}

	std::string mod_prefix = "yetty_" + *mod + "_";
	SourceManager &sm = context.getSourceManager();

	if (!has_prefix_with_tail(name, mod_prefix)) {
		violation v{
			violation_kind::function_prefix,
			func->getCanonicalDecl(),
			name,
			suggest_with_prefix(name, mod_prefix, *mod),
			*mod,
			effective_loc(sm, func->getLocation()),
			&context,
		};
		handler(v);
		return true;
	}

	if (is_external_cb)
		return true;

	if (func->getNumParams() > 0) {
		ParmVarDecl *p0 = func->getParamDecl(0);
		std::string s = struct_arg_subname(p0->getType(), *mod);
		if (!s.empty()) {
			std::string need = mod_prefix + s + "_";
			if (!has_prefix_with_tail(name, need)) {
				violation v{
					violation_kind::function_struct_bound,
					func->getCanonicalDecl(),
					name,
					suggest_struct_bound(name, mod_prefix, s),
					*mod,
					effective_loc(sm, func->getLocation()),
					&context,
				};
				handler(v);
			}
		}
	}
	return true;
}

bool checker_visitor::VisitRecordDecl(RecordDecl *rec)
{
	if (!rec->getIdentifier())
		return true;
	if (!rec->getDeclContext()->isFileContext())
		return true;
	auto mod = decl_module(rec);
	if (!mod)
		return true;
	if (already_seen(rec))
		return true;

	/* Forward declarations of third-party types (e.g. libuv's
	 * `struct uv_loop_s`, pdfio's `struct _pdfio_file_s`) often appear
	 * in our headers as opaque-pointer wrappers. Renaming our forward
	 * decl breaks the typedef bridge to the 3rdparty definition. Skip
	 * any record whose definition is not in our tree (or doesn't
	 * exist anywhere we can see). */
	const RecordDecl *def = rec->getDefinition();
	if (!def) {
		return true;
	}
	/* Only check the rule once, at the definition site. A forward
	 * declaration in module A of a type defined in module B would
	 * otherwise produce a spurious "should be yetty_A_..." warning
	 * even when the definition is correctly named for module B. */
	if (def != rec)
		return true;
	{
		SourceManager &sm = context.getSourceManager();
		SourceLocation def_loc =
			effective_loc(sm, def->getLocation());
		std::string def_file = sm.getFilename(def_loc).str();
		if (!module_for_path(def_file).has_value())
			return true;
	}

	std::string name = rec->getNameAsString();
	std::string mod_prefix = "yetty_" + *mod + "_";
	if (has_prefix_with_tail(name, mod_prefix))
		return true;

	violation v{
		rec->isUnion() ? violation_kind::union_prefix
			       : violation_kind::struct_prefix,
		rec->getCanonicalDecl(),
		name,
		suggest_with_prefix(name, mod_prefix, *mod),
		*mod,
		effective_loc(context.getSourceManager(), rec->getLocation()),
		&context,
	};
	handler(v);
	return true;
}

bool checker_visitor::VisitEnumDecl(EnumDecl *en)
{
	if (!en->getIdentifier())
		return true;
	if (!en->getDeclContext()->isFileContext())
		return true;
	auto mod = decl_module(en);
	if (!mod)
		return true;
	if (already_seen(en))
		return true;

	std::string name = en->getNameAsString();
	std::string mod_prefix = "yetty_" + *mod + "_";
	if (has_prefix_with_tail(name, mod_prefix))
		return true;

	violation v{
		violation_kind::enum_prefix,
		en->getCanonicalDecl(),
		name,
		suggest_with_prefix(name, mod_prefix, *mod),
		*mod,
		effective_loc(context.getSourceManager(), en->getLocation()),
		&context,
	};
	handler(v);
	return true;
}

bool checker_visitor::VisitEnumConstantDecl(EnumConstantDecl *ec)
{
	auto mod = decl_module(ec);
	if (!mod)
		return true;
	if (already_seen(ec))
		return true;

	std::string name = ec->getNameAsString();
	std::string up_prefix = "YETTY_" + to_upper(*mod) + "_";
	if (has_prefix_with_tail(name, up_prefix))
		return true;

	violation v{
		violation_kind::enum_constant_prefix,
		ec->getCanonicalDecl(),
		name,
		suggest_upper_with_prefix(name, up_prefix, *mod),
		*mod,
		effective_loc(context.getSourceManager(), ec->getLocation()),
		&context,
	};
	handler(v);
	return true;
}

bool checker_visitor::VisitVarDecl(VarDecl *var)
{
	if (isa<ParmVarDecl>(var))
		return true;
	if (!var->hasGlobalStorage())
		return true;
	if (var->isStaticLocal())
		return true;
	if (var->getStorageClass() == SC_Static)
		return true;
	if (!var->getDeclContext()->isFileContext())
		return true;
	/* Only flag the definition, not extern declarations. An extern
	 * decl of a variable defined in another TU (e.g. an incbin-
	 * generated `extern const unsigned char gyplot_shaderData[];`)
	 * would otherwise be flagged at every file that needs to refer
	 * to it. The definition site is what we own. */
	if (!var->isThisDeclarationADefinition())
		return true;
	auto mod = decl_module(var);
	if (!mod)
		return true;
	if (already_seen(var))
		return true;

	std::string name = var->getNameAsString();
	if (name.empty())
		return true;
	std::string mod_prefix = "yetty_" + *mod + "_";
	if (has_prefix_with_tail(name, mod_prefix))
		return true;

	violation v{
		violation_kind::variable_prefix,
		var->getCanonicalDecl(),
		name,
		suggest_with_prefix(name, mod_prefix, *mod),
		*mod,
		effective_loc(context.getSourceManager(), var->getLocation()),
		&context,
	};
	handler(v);
	return true;
}

bool checker_visitor::VisitTypedefNameDecl(TypedefNameDecl *td)
{
	auto mod = decl_module(td);
	if (!mod)
		return true;
	if (already_seen(td))
		return true;

	/* The "no typedefs" rule applies to STRUCT typedefs and pointer-
	 * to-struct typedefs only. Those should become `struct yetty_X`
	 * (or `struct yetty_X *`) directly at use sites. Permitted:
	 *   - scalar/primitive aliases (`typedef uint64_t my_id;`)
	 *   - enum aliases
	 *   - function-pointer / function-type typedefs (callbacks,
	 *     vtable signatures — inlining them is unreadable)
	 *   - array typedefs */
	QualType under = td->getUnderlyingType()
				 .getCanonicalType()
				 .getUnqualifiedType();
	bool is_struct_typedef = under->isRecordType();
	bool is_struct_ptr_typedef = false;
	if (under->isPointerType()) {
		QualType pointee =
			under->getPointeeType().getUnqualifiedType();
		if (pointee->isRecordType())
			is_struct_ptr_typedef = true;
	}
	if (!is_struct_typedef && !is_struct_ptr_typedef)
		return true;

	violation v{
		violation_kind::typedef_banned,
		td->getCanonicalDecl(),
		td->getNameAsString(),
		"", /* not auto-fixable: human judgment per case */
		*mod,
		effective_loc(context.getSourceManager(), td->getLocation()),
		&context,
	};
	handler(v);
	return true;
}

} /* namespace naming */
} /* namespace yetty */
