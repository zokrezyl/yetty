/*
 * result-checker.cpp - Static analysis tool for Result type enforcement
 *
 * Checks that functions calling other functions that return Result types
 * also return Result types themselves.
 *
 * Usage:
 *   result-checker <source-files> -- [compiler-flags]
 *   result-checker --compile-commands=build/compile_commands.json <source-files>
 */

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/Attr.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Support/CommandLine.h>

#include <set>
#include <string>
#include <vector>

using namespace clang;
using namespace clang::tooling;

static llvm::cl::OptionCategory ToolCategory("result-checker options");

static llvm::cl::opt<bool> Verbose("verbose",
	llvm::cl::desc("Print verbose output"),
	llvm::cl::cat(ToolCategory));

/*
 * Check if a type name ends with "_result" indicating it's a Result type.
 */
static bool is_result_type(const std::string &type_name)
{
	const std::string suffix = "_result";
	if (type_name.length() < suffix.length())
		return false;
	return type_name.compare(type_name.length() - suffix.length(),
				 suffix.length(), suffix) == 0;
}

/*
 * Extract the canonical type name, handling struct prefixes.
 */
static std::string get_type_name(QualType type)
{
	/* Get the desugared type without qualifiers */
	QualType canonical = type.getCanonicalType();
	std::string name = canonical.getAsString();

	/* Remove "struct " prefix if present */
	const std::string prefix = "struct ";
	if (name.substr(0, prefix.length()) == prefix)
		name = name.substr(prefix.length());

	return name;
}

/*
 * Check if a function's return type is a Result type.
 */
static bool returns_result_type(const FunctionDecl *func)
{
	if (!func)
		return false;

	QualType ret_type = func->getReturnType();
	std::string type_name = get_type_name(ret_type);
	return is_result_type(type_name);
}

/*
 * Visitor that collects all function calls within a function body.
 */
class CallCollector : public RecursiveASTVisitor<CallCollector> {
public:
	explicit CallCollector(ASTContext &ctx) : context(ctx)
	{
	}

	bool VisitCallExpr(CallExpr *call)
	{
		const FunctionDecl *callee = call->getDirectCallee();
		if (callee && returns_result_type(callee)) {
			result_calls.push_back(call);
		}
		return true;
	}

	const std::vector<CallExpr *> &get_result_calls() const
	{
		return result_calls;
	}

	void clear()
	{
		result_calls.clear();
	}

private:
	ASTContext &context;
	std::vector<CallExpr *> result_calls;
};

/*
 * Main AST visitor that checks each function definition.
 */
class ResultCheckerVisitor
	: public RecursiveASTVisitor<ResultCheckerVisitor> {
public:
	explicit ResultCheckerVisitor(ASTContext &ctx)
		: context(ctx), collector(ctx), violations(0)
	{
	}

	bool VisitFunctionDecl(FunctionDecl *func)
	{
		/* Only check function definitions, not declarations */
		if (!func->hasBody())
			return true;

		/* Skip functions in system headers */
		SourceManager &sm = context.getSourceManager();
		SourceLocation loc = func->getLocation();
		if (sm.isInSystemHeader(loc))
			return true;

		/* Skip static inline functions in headers (they may be helpers) */
		/* Skip main function */
		std::string func_name = func->getNameAsString();
		if (func_name == "main")
			return true;

		/* Skip functions explicitly tagged as void-callback boundaries.
		 * Use:
		 *   __attribute__((annotate("yetty_external_callback")))
		 * on functions whose signature is dictated by an external library
		 * (libuv uv_*_cb, pthread start routine, emscripten loop, Android
		 * NDK app callbacks, pdfio paint callbacks, etc.). The Result-
		 * returning calls inside such a callback have nowhere to propagate
		 * to; the annotation documents the boundary and silences the
		 * checker. Use sparingly. */
		for (const auto *attr : func->specific_attrs<AnnotateAttr>()) {
			if (attr->getAnnotation() == "yetty_external_callback")
				return true;
		}

		/* Check if this function already returns a Result type */
		bool func_returns_result = returns_result_type(func);

		/* Collect all calls to result-returning functions */
		collector.clear();
		collector.TraverseStmt(func->getBody());

		const auto &result_calls = collector.get_result_calls();
		if (result_calls.empty())
			return true;

		/* If this function calls result-returning functions but doesn't
		 * return a result itself, report a violation */
		if (!func_returns_result) {
			report_violation(func, result_calls);
		}

		return true;
	}

	int get_violation_count() const
	{
		return violations;
	}

private:
	void report_violation(FunctionDecl *func,
			      const std::vector<CallExpr *> &calls)
	{
		SourceManager &sm = context.getSourceManager();
		SourceLocation func_loc = func->getLocation();

		std::string filename = sm.getFilename(func_loc).str();
		unsigned line = sm.getSpellingLineNumber(func_loc);

		llvm::errs() << filename << ":" << line << ": warning: "
			     << "function '" << func->getNameAsString()
			     << "' calls result-returning function(s) but "
			     << "does not return a Result type\n";

		/* Show which calls triggered this */
		for (const CallExpr *call : calls) {
			const FunctionDecl *callee = call->getDirectCallee();
			if (!callee)
				continue;

			SourceLocation call_loc = call->getBeginLoc();
			unsigned call_line = sm.getSpellingLineNumber(call_loc);

			std::string ret_type =
				get_type_name(callee->getReturnType());

			llvm::errs() << "    " << filename << ":" << call_line
				     << ": note: calls '"
				     << callee->getNameAsString()
				     << "' which returns '" << ret_type
				     << "'\n";
		}

		llvm::errs() << "\n";
		violations++;
	}

	ASTContext &context;
	CallCollector collector;
	int violations;
};

/*
 * AST consumer that runs our visitor.
 */
class ResultCheckerConsumer : public ASTConsumer {
public:
	explicit ResultCheckerConsumer(ASTContext &ctx) : visitor(ctx)
	{
	}

	void HandleTranslationUnit(ASTContext &ctx) override
	{
		visitor.TraverseDecl(ctx.getTranslationUnitDecl());
		total_violations += visitor.get_violation_count();
	}

	static int get_total_violations()
	{
		return total_violations;
	}

	static void reset_violations()
	{
		total_violations = 0;
	}

private:
	ResultCheckerVisitor visitor;
	static int total_violations;
};

int ResultCheckerConsumer::total_violations = 0;

/*
 * Frontend action that creates our consumer.
 */
class ResultCheckerAction : public ASTFrontendAction {
public:
	std::unique_ptr<ASTConsumer>
	CreateASTConsumer(CompilerInstance &ci, StringRef file) override
	{
		if (Verbose)
			llvm::errs() << "Analyzing: " << file << "\n";
		return std::make_unique<ResultCheckerConsumer>(ci.getASTContext());
	}
};

/*
 * Factory for creating frontend actions.
 */
class ResultCheckerActionFactory : public FrontendActionFactory {
public:
	std::unique_ptr<FrontendAction> create() override
	{
		return std::make_unique<ResultCheckerAction>();
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

	ResultCheckerConsumer::reset_violations();
	int result = tool.run(std::make_unique<ResultCheckerActionFactory>().get());

	int violations = ResultCheckerConsumer::get_total_violations();
	if (violations > 0) {
		llvm::errs() << "\nTotal violations: " << violations << "\n";
		return 1;
	}

	if (result == 0 && Verbose)
		llvm::errs() << "No result type violations found.\n";

	return result;
}
