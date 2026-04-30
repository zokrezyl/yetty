/*
 * naming-convention.cpp - Analysis frontend for yetty naming-convention
 * checks. Walks every TU via libtooling, prints one warning per
 * violation, and exits non-zero if any were found.
 *
 * Rules and detection logic live in qa-tools/lib/naming-convention/.
 */

#include "core.h"

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/ASTContext.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Support/CommandLine.h>

#include <cctype>

using namespace clang;
using namespace clang::tooling;
using yetty::naming::checker_visitor;
using yetty::naming::kind_label;
using yetty::naming::violation;
using yetty::naming::violation_kind;

static llvm::cl::OptionCategory ToolCategory("naming-convention options");

static llvm::cl::opt<bool> Verbose("verbose",
	llvm::cl::desc("Print verbose output"),
	llvm::cl::cat(ToolCategory));

static int g_violations = 0;

static std::string expected_for(const violation &v)
{
	const std::string mp = "yetty_" + v.module + "_";
	switch (v.kind) {
	case violation_kind::function_prefix:
	case violation_kind::struct_prefix:
	case violation_kind::union_prefix:
	case violation_kind::enum_prefix:
	case violation_kind::variable_prefix:
		return "name to start with '" + mp + "<...>'";
	case violation_kind::function_struct_bound: {
		std::string n = v.suggested_name.empty()
					? std::string(mp + "<S>_<...>")
					: v.suggested_name;
		return "name to start with the bound-struct prefix (e.g. '" +
		       n + "')";
	}
	case violation_kind::enum_constant_prefix: {
		std::string up = "YETTY_";
		for (char c : v.module)
			up.push_back(static_cast<char>(
				std::toupper(static_cast<unsigned char>(c))));
		up.push_back('_');
		return "name to start with '" + up + "<...>'";
	}
	case violation_kind::typedef_banned:
		return "no typedefs allowed (use 'struct X' directly)";
	}
	return "";
}

static void print_violation(const violation &v)
{
	SourceManager &sm = v.context->getSourceManager();
	std::string filename = sm.getFilename(v.loc).str();
	unsigned line = sm.getSpellingLineNumber(v.loc);
	llvm::errs() << filename << ":" << line << ": warning: "
		     << kind_label(v.kind) << " '" << v.current_name
		     << "' violates naming convention; expected "
		     << expected_for(v) << "\n";
	g_violations++;
}

class Consumer : public ASTConsumer {
public:
	explicit Consumer(ASTContext &ctx) : visitor(ctx, &print_violation)
	{
	}

	void HandleTranslationUnit(ASTContext &ctx) override
	{
		visitor.TraverseDecl(ctx.getTranslationUnitDecl());
	}

private:
	checker_visitor visitor;
};

class Action : public ASTFrontendAction {
public:
	std::unique_ptr<ASTConsumer>
	CreateASTConsumer(CompilerInstance &ci, StringRef file) override
	{
		if (Verbose)
			llvm::errs() << "Analyzing: " << file << "\n";
		return std::make_unique<Consumer>(ci.getASTContext());
	}
};

class ActionFactory : public FrontendActionFactory {
public:
	std::unique_ptr<FrontendAction> create() override
	{
		return std::make_unique<Action>();
	}
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

	g_violations = 0;
	int result = tool.run(std::make_unique<ActionFactory>().get());

	if (g_violations > 0) {
		llvm::errs() << "\nTotal violations: " << g_violations << "\n";
		return 1;
	}

	if (result == 0 && Verbose)
		llvm::errs() << "No naming convention violations found.\n";

	return result;
}
