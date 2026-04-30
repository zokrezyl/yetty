/*
 * core.h - Shared core for the yetty naming-convention QA tools.
 *
 * Provides the AST visitor that detects naming-convention violations,
 * the Violation type, and small helpers shared by the analysis and
 * refactoring frontends.
 */

#pragma once

#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>

#include <functional>
#include <optional>
#include <set>
#include <string>

namespace yetty {
namespace naming {

enum class violation_kind {
	function_prefix,         /* Rule 1 on a function */
	function_struct_bound,   /* Rule 2 */
	struct_prefix,
	union_prefix,
	enum_prefix,
	enum_constant_prefix,
	variable_prefix,
	typedef_banned,
};

const char *kind_label(violation_kind k);

struct violation {
	violation_kind kind;
	const clang::Decl *decl;       /* canonical decl */
	std::string current_name;
	std::string suggested_name;    /* empty if not auto-fixable */
	std::string module;            /* derived from source path */
	clang::SourceLocation loc;     /* expansion loc */
	clang::ASTContext *context;
};

using violation_handler =
	std::function<void(const violation &)>;

/*
 * Module name for a given source file path; std::nullopt if file isn't
 * under src/yetty/<X>/ or include/yetty/<X>/. Hyphens in <X> are
 * converted to underscores.
 */
std::optional<std::string> module_for_path(const std::string &path);

/*
 * Expansion loc (identity for non-macro locations). Anchors diagnostics
 * at the macro call site rather than the macro definition.
 */
clang::SourceLocation effective_loc(clang::SourceManager &sm,
				    clang::SourceLocation loc);

/*
 * Visitor that walks every external decl, applies the four rules, and
 * invokes `handler` once per violation.
 */
class checker_visitor
	: public clang::RecursiveASTVisitor<checker_visitor> {
public:
	checker_visitor(clang::ASTContext &ctx, violation_handler h);

	bool VisitFunctionDecl(clang::FunctionDecl *func);
	bool VisitRecordDecl(clang::RecordDecl *rec);
	bool VisitEnumDecl(clang::EnumDecl *en);
	bool VisitEnumConstantDecl(clang::EnumConstantDecl *ec);
	bool VisitVarDecl(clang::VarDecl *var);
	bool VisitTypedefNameDecl(clang::TypedefNameDecl *td);

private:
	std::optional<std::string> decl_module(const clang::Decl *d);
	bool already_seen(const clang::Decl *d);

	clang::ASTContext &context;
	violation_handler handler;
	std::set<const clang::Decl *> seen;
};

} /* namespace naming */
} /* namespace yetty */
