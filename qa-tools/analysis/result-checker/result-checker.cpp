/*
 * result-checker.cpp - Static analysis tool for Result type enforcement
 *
 * Two independent checks, selected by flags:
 *
 *   --check-propagation   (default: on)
 *       Checks that functions calling other functions that return Result
 *       types also return Result types themselves.
 *
 *   --check-double-eval   (default: off)
 *       Checks that the result argument passed to YETTY_RETURN_IF_ERR is a
 *       plain variable, not a call expression. The macro expands `res` in two
 *       places (YETTY_IS_ERR(res) and YETTY_ERR(..., res)), so a call passed
 *       there fires TWICE — a second side effect on the error path, and the
 *       chained error comes from the second call, not the first. The fix is to
 *       assign the call to a local first, then pass the local.
 *
 * Pass both flags to run both checks in a single pass. To run only the
 * double-eval check, add `--check-propagation=false`.
 *
 * Usage:
 *   result-checker <source-files> -- [compiler-flags]
 *   result-checker --compile-commands=build/compile_commands.json <source-files>
 */

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/Attr.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Basic/TokenKinds.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Lex/Lexer.h>
#include <clang/Lex/MacroArgs.h>
#include <clang/Lex/PPCallbacks.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/Lex/Token.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Support/CommandLine.h>

#include <string>
#include <vector>

using namespace clang;
using namespace clang::tooling;

static llvm::cl::OptionCategory ToolCategory("result-checker options");

static llvm::cl::opt<bool> Verbose("verbose",
	llvm::cl::desc("Print verbose output"),
	llvm::cl::cat(ToolCategory));

static llvm::cl::opt<bool> CheckPropagation("check-propagation",
	llvm::cl::desc("Check that functions calling result-returning functions "
		       "also return a Result type (default: on)"),
	llvm::cl::init(true), llvm::cl::cat(ToolCategory));

static llvm::cl::opt<bool> CheckDoubleEval("check-double-eval",
	llvm::cl::desc("Check that YETTY_RETURN_IF_ERR's result argument is a "
		       "plain variable, not a double-evaluated call expression "
		       "(default: off)"),
	llvm::cl::init(false), llvm::cl::cat(ToolCategory));

/*
 * Running totals, owned by main() and threaded through the action factory so
 * no file-scope mutable state is needed.
 */
struct CheckCounters {
	int propagation = 0;
	int double_eval = 0;
};

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
 * A source location is "foreign" — outside our own first-party code — when it
 * sits in a system header or under a bundled third-party tree. We enforce the
 * Result convention on yetty code only: third-party libraries have their own
 * error idioms (e.g. miniaudio's ma_result), and flagging their functions — or
 * flagging our code for not propagating *their* result types — is pure noise.
 */
static bool is_foreign_location(SourceLocation loc, const SourceManager &sm)
{
	if (loc.isInvalid())
		return false;
	if (sm.isInSystemHeader(loc))
		return true;
	StringRef filename = sm.getFilename(sm.getSpellingLoc(loc));
	return filename.contains("/3rdparty/") || filename.contains("/_deps/");
}

/*
 * Check if a type is a Result type whose propagation we enforce. It must end in
 * "_result" AND be declared in first-party code. The location test (not a name
 * prefix) is what keeps legitimate non-"yetty_"-prefixed result types such as
 * `rectangle_result` and `yplatform_coro_ptr_result` in scope while excluding
 * third-party look-alikes like `ma_result`.
 */
static bool is_enforced_result_type(QualType type, const ASTContext &ctx)
{
	if (!is_result_type(get_type_name(type)))
		return false;

	/* Our result types are always structs (YETTY_YRESULT_DECLARE emits a
	 * struct); third-party offenders resolve to a tag decl too (ma_result is
	 * a typedef'd enum). Locate that decl and reject it when it is foreign. */
	const TagDecl *tag = type.getCanonicalType()->getAsTagDecl();
	if (!tag)
		return true;
	return !is_foreign_location(tag->getLocation(), ctx.getSourceManager());
}

/*
 * Check if a function's return type is a Result type we enforce.
 */
static bool returns_result_type(const FunctionDecl *func)
{
	if (!func)
		return false;

	return is_enforced_result_type(func->getReturnType(),
				       func->getASTContext());
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
 * Main AST visitor that checks each function definition (propagation mode).
 */
class ResultCheckerVisitor
	: public RecursiveASTVisitor<ResultCheckerVisitor> {
public:
	ResultCheckerVisitor(ASTContext &ctx, CheckCounters &counters)
		: context(ctx), collector(ctx), counters(counters)
	{
	}

	bool VisitFunctionDecl(FunctionDecl *func)
	{
		/* Only check function definitions, not declarations */
		if (!func->hasBody())
			return true;

		/* Skip functions in system headers or bundled third-party code —
		 * we only enforce the convention on first-party yetty sources. */
		SourceManager &sm = context.getSourceManager();
		SourceLocation loc = func->getLocation();
		if (is_foreign_location(loc, sm))
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

		/* The rule: a function that can fail returns a Result type, and
		 * ANY function that calls a Result-returning function must itself
		 * return a Result type so the error propagates up the call chain.
		 * The only sanctioned non-propagating boundaries are 3rdparty /
		 * main / functions tagged YETTY_EXTERNAL_CALLBACK (handled above).
		 * Absorbing, (void)-casting, or otherwise consuming the result in
		 * a non-Result-returning function is NOT allowed — it breaks the
		 * propagation chain. */
		bool func_returns_result = returns_result_type(func);

		collector.clear();
		collector.TraverseStmt(func->getBody());

		const auto &result_calls = collector.get_result_calls();
		if (result_calls.empty())
			return true;

		if (!func_returns_result)
			report_violation(func, result_calls);

		return true;
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
		counters.propagation++;
	}

	ASTContext &context;
	CallCollector collector;
	CheckCounters &counters;
};

/*
 * Preprocessor callback that flags YETTY_RETURN_IF_ERR invocations whose
 * result argument is a call expression (double-eval mode).
 *
 * This must run at the preprocessor stage: by the time the AST exists the
 * macro has already been expanded, so the call appears twice with no trace of
 * the original single-argument intent. The unexpanded macro argument tokens
 * are only available here.
 */
class MacroArgChecker : public PPCallbacks {
public:
	MacroArgChecker(Preprocessor &pp, CheckCounters &counters)
		: preprocessor(pp), counters(counters)
	{
	}

	void MacroExpands(const Token &macro_name_tok, const MacroDefinition &,
			  SourceRange, const MacroArgs *args) override
	{
		if (!args)
			return;

		const IdentifierInfo *ident = macro_name_tok.getIdentifierInfo();
		if (!ident || ident->getName() != "YETTY_RETURN_IF_ERR")
			return;

		/* Skip expansions inside system headers — our macro is never
		 * used there, but stay defensive. */
		SourceManager &sm = preprocessor.getSourceManager();
		SourceLocation loc = macro_name_tok.getLocation();
		if (sm.isInSystemHeader(loc))
			return;

		/* Parameters are (type, res, msg); the result is argument 1. */
		if (args->getNumMacroArguments() < 2)
			return;

		const Token *res_tokens = args->getUnexpArgument(1);
		if (!res_tokens)
			return;
		unsigned res_len = MacroArgs::getArgLength(res_tokens);
		if (res_len == 0)
			return;

		if (!arg_is_call_expression(res_tokens, res_len))
			return;

		report_violation(loc, res_tokens, res_len);
	}

private:
	/*
	 * Heuristic over the unexpanded argument tokens: the argument is a call
	 * expression if any '(' is immediately preceded by something that can
	 * end a postfix-expression — an identifier (plain or method call:
	 * `foo(...)`, `obj->method(...)`), a ')' (function-pointer or
	 * cast-then-call: `(*fp)(...)`), or a ']' (`table[i](...)`). A plain
	 * variable, member access, array subscript, or dereference has no such
	 * '(' and is left alone.
	 */
	static bool arg_is_call_expression(const Token *tokens, unsigned len)
	{
		for (unsigned i = 1; i < len; ++i) {
			if (!tokens[i].is(tok::l_paren))
				continue;
			tok::TokenKind prev = tokens[i - 1].getKind();
			if (prev == tok::identifier ||
			    prev == tok::r_paren ||
			    prev == tok::r_square)
				return true;
		}
		return false;
	}

	/* Collapse any run of whitespace (including the newlines and source
	 * indentation of a multi-line call argument) into a single space, so
	 * each diagnostic stays on one line. */
	static std::string collapse_whitespace(const std::string &in)
	{
		std::string out;
		out.reserve(in.size());
		bool in_space = false;
		for (char ch : in) {
			if (ch == ' ' || ch == '\t' || ch == '\n' ||
			    ch == '\r' || ch == '\f' || ch == '\v') {
				in_space = true;
				continue;
			}
			if (in_space && !out.empty())
				out += ' ';
			in_space = false;
			out += ch;
		}
		return out;
	}

	std::string arg_spelling(const Token *tokens, unsigned len)
	{
		SourceManager &sm = preprocessor.getSourceManager();
		SourceLocation begin = tokens[0].getLocation();
		SourceLocation end = tokens[len - 1].getLocation();

		if (begin.isValid() && end.isValid() && !begin.isMacroID() &&
		    !end.isMacroID()) {
			std::string text =
				Lexer::getSourceText(
					CharSourceRange::getTokenRange(begin, end),
					sm, preprocessor.getLangOpts())
					.str();
			if (!text.empty())
				return collapse_whitespace(text);
		}

		/* Fallback: join individual token spellings. */
		std::string text;
		for (unsigned i = 0; i < len; ++i) {
			if (i)
				text += ' ';
			text += preprocessor.getSpelling(tokens[i]);
		}
		return text;
	}

	void report_violation(SourceLocation loc, const Token *tokens,
			      unsigned len)
	{
		SourceManager &sm = preprocessor.getSourceManager();
		SourceLocation spelling = sm.getSpellingLoc(loc);

		std::string filename = sm.getFilename(spelling).str();
		unsigned line = sm.getSpellingLineNumber(spelling);

		llvm::errs() << filename << ":" << line << ": warning: "
			     << "YETTY_RETURN_IF_ERR result argument '"
			     << arg_spelling(tokens, len)
			     << "' is a call expression — it is evaluated "
			     << "TWICE (the call fires again on the error "
			     << "path). Assign it to a local first, then pass "
			     << "the local.\n\n";
		counters.double_eval++;
	}

	Preprocessor &preprocessor;
	CheckCounters &counters;
};

/*
 * AST consumer that runs the propagation visitor.
 */
class ResultCheckerConsumer : public ASTConsumer {
public:
	ResultCheckerConsumer(ASTContext &ctx, CheckCounters &counters)
		: visitor(ctx, counters)
	{
	}

	void HandleTranslationUnit(ASTContext &ctx) override
	{
		if (!CheckPropagation)
			return;
		visitor.TraverseDecl(ctx.getTranslationUnitDecl());
	}

private:
	ResultCheckerVisitor visitor;
};

/*
 * Frontend action that wires up the preprocessor callback and the AST consumer.
 */
class ResultCheckerAction : public ASTFrontendAction {
public:
	explicit ResultCheckerAction(CheckCounters &counters)
		: counters(counters)
	{
	}

	bool BeginSourceFileAction(CompilerInstance &ci) override
	{
		if (CheckDoubleEval) {
			Preprocessor &pp = ci.getPreprocessor();
			pp.addPPCallbacks(
				std::make_unique<MacroArgChecker>(pp, counters));
		}
		return true;
	}

	std::unique_ptr<ASTConsumer>
	CreateASTConsumer(CompilerInstance &ci, StringRef file) override
	{
		if (Verbose)
			llvm::errs() << "Analyzing: " << file << "\n";
		return std::make_unique<ResultCheckerConsumer>(
			ci.getASTContext(), counters);
	}

private:
	CheckCounters &counters;
};

/*
 * Factory for creating frontend actions.
 */
class ResultCheckerActionFactory : public FrontendActionFactory {
public:
	explicit ResultCheckerActionFactory(CheckCounters &counters)
		: counters(counters)
	{
	}

	std::unique_ptr<FrontendAction> create() override
	{
		return std::make_unique<ResultCheckerAction>(counters);
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
	ResultCheckerActionFactory factory(counters);
	int result = tool.run(&factory);

	int total = counters.propagation + counters.double_eval;
	if (total > 0) {
		if (CheckPropagation)
			llvm::errs() << "Propagation violations: "
				     << counters.propagation << "\n";
		if (CheckDoubleEval)
			llvm::errs() << "Double-eval violations: "
				     << counters.double_eval << "\n";
		llvm::errs() << "Total violations: " << total << "\n";
		return 1;
	}

	if (result == 0 && Verbose)
		llvm::errs() << "No result type violations found.\n";

	return result;
}
