/*
 * yclass-checker.cpp - Static analysis for yclass object/slice navigation.
 *
 * The yclass object model is a single allocation laid out as
 *
 *     [ struct yetty_yclass_object header (the klass pointer) ]
 *     [ parent-most data slice ] ... [ leaf class data slice ]
 *
 * The header and every data slice sit at FIXED offsets that codegen computes
 * from the class's ancestor chain. The handle you pass around is the
 * `struct yetty_yclass_object *`; you reach a class's typed slice with the
 * GENERATED accessor `yetty_<module>_<class>_data_get(obj)`, and you call a
 * polymorphic method with `yetty_<module>_<slot>(ctx, obj, ...)` directly.
 *
 * What you must NEVER do is reconstruct one from the other by hand with
 * pointer arithmetic:
 *
 *     (struct yetty_yclass_object *)(fig) - 1   // slice -> object, by hand
 *     obj + 1                                   // object -> first slice, by hand
 *     base = (struct yetty_yfigure_figure *)(obj + 1);
 *
 * Those `± N` tricks hard-code the layout (they assume a specific slice is
 * first / the header is one pointer wide). They break the instant a parent or
 * mixin is inserted, the header changes width, or the class is subclassed
 * further — and they are the seed of the `base`-pointer caches and the
 * hand-rolled dispatch shims that the class system exists to make unnecessary.
 *
 *   --check-object-arith   (default: on)
 *       Flag any `<expr> ± <integer>` where one operand is a
 *       `struct yetty_yclass_object *`.
 *
 * Usage:
 *   yclass-checker <source-files> -- [compiler-flags]
 *   yclass-checker -p build/ <source-files>
 */

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Lex/Lexer.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Support/CommandLine.h>

#include <string>

using namespace clang;
using namespace clang::tooling;

static llvm::cl::OptionCategory ToolCategory("yclass-checker options");

static llvm::cl::opt<bool> Verbose("verbose", llvm::cl::desc("Print verbose output"),
				   llvm::cl::cat(ToolCategory));

static llvm::cl::opt<bool> CheckObjectArith(
	"check-object-arith",
	llvm::cl::desc("Flag `object ± N` pointer arithmetic on a "
		       "struct yetty_yclass_object * (default: on)"),
	llvm::cl::init(true), llvm::cl::cat(ToolCategory));

struct CheckCounters {
	int object_arith = 0;
};

/*
 * A location is "foreign" — outside first-party code — when it sits in a system
 * header or under a bundled third-party tree. We enforce the convention on
 * yetty code only.
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
 * True when `type` is `struct yetty_yclass_object *` after canonicalisation.
 * Catches both a plain object handle (`obj`, type object*) and a C-style cast
 * to it (`(struct yetty_yclass_object *)(fig)`), since the cast expression's
 * type IS the object pointer.
 */
static bool is_yclass_object_ptr(QualType type)
{
	QualType canonical = type.getCanonicalType();
	if (!canonical->isPointerType())
		return false;
	const TagDecl *tag = canonical->getPointeeType()->getAsTagDecl();
	return tag && tag->getName() == "yetty_yclass_object";
}

/* Collapse runs of whitespace so a multi-line expression prints on one line. */
static std::string collapse_whitespace(const std::string &in)
{
	std::string out;
	out.reserve(in.size());
	bool in_space = false;
	for (char ch : in) {
		if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' ||
		    ch == '\v') {
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

class YclassVisitor : public RecursiveASTVisitor<YclassVisitor> {
public:
	YclassVisitor(ASTContext &ctx, CheckCounters &counters)
		: context(ctx), counters(counters)
	{
	}

	bool VisitBinaryOperator(BinaryOperator *op)
	{
		if (!CheckObjectArith)
			return true;

		const BinaryOperatorKind kind = op->getOpcode();
		if (kind != BO_Add && kind != BO_Sub)
			return true;

		SourceManager &sm = context.getSourceManager();
		if (is_foreign_location(op->getOperatorLoc(), sm))
			return true;

		Expr *lhs = op->getLHS();
		Expr *rhs = op->getRHS();

		/* One operand must be a yclass object pointer, the other an integer
		 * offset. `lhs->getType()` already reflects an explicit C-style cast,
		 * so `(struct yetty_yclass_object *)(fig) - 1` is caught. */
		Expr *object_side = nullptr;
		Expr *offset_side = nullptr;
		if (is_yclass_object_ptr(lhs->getType())) {
			object_side = lhs;
			offset_side = rhs;
		} else if (is_yclass_object_ptr(rhs->getType())) {
			object_side = rhs;
			offset_side = lhs;
		}
		if (!object_side)
			return true;
		if (!offset_side->getType()->isIntegerType())
			return true;

		report(op);
		return true;
	}

private:
	void report(BinaryOperator *op)
	{
		SourceManager &sm = context.getSourceManager();
		SourceLocation loc = sm.getSpellingLoc(op->getOperatorLoc());
		std::string filename = sm.getFilename(loc).str();
		unsigned line = sm.getSpellingLineNumber(loc);

		std::string text =
			Lexer::getSourceText(CharSourceRange::getTokenRange(op->getSourceRange()),
					     sm, context.getLangOpts())
				.str();
		if (text.empty())
			text = "<expr>";

		llvm::errs() << filename << ":" << line << ": warning: "
			     << "yclass object/slice pointer arithmetic '"
			     << collapse_whitespace(text)
			     << "' — reach a data slice with "
			     << "yetty_<module>_<class>_data_get(obj) and keep the "
			     << "struct yetty_yclass_object* as the handle (call "
			     << "yetty_<module>_<slot>(ctx, obj, ...) directly); never "
			     << "reconstruct object/slice with pointer math.\n\n";
		counters.object_arith++;
	}

	ASTContext &context;
	CheckCounters &counters;
};

class YclassConsumer : public ASTConsumer {
public:
	YclassConsumer(ASTContext &ctx, CheckCounters &counters) : visitor(ctx, counters)
	{
	}

	void HandleTranslationUnit(ASTContext &ctx) override
	{
		visitor.TraverseDecl(ctx.getTranslationUnitDecl());
	}

private:
	YclassVisitor visitor;
};

class YclassAction : public ASTFrontendAction {
public:
	explicit YclassAction(CheckCounters &counters) : counters(counters)
	{
	}

	std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &ci,
						       StringRef file) override
	{
		if (Verbose)
			llvm::errs() << "Analyzing: " << file << "\n";
		return std::make_unique<YclassConsumer>(ci.getASTContext(), counters);
	}

private:
	CheckCounters &counters;
};

class YclassActionFactory : public FrontendActionFactory {
public:
	explicit YclassActionFactory(CheckCounters &counters) : counters(counters)
	{
	}

	std::unique_ptr<FrontendAction> create() override
	{
		return std::make_unique<YclassAction>(counters);
	}

private:
	CheckCounters &counters;
};

int main(int argc, const char **argv)
{
	auto expected_parser = CommonOptionsParser::create(argc, argv, ToolCategory);
	if (!expected_parser) {
		llvm::errs() << expected_parser.takeError();
		return 1;
	}

	CommonOptionsParser &options = expected_parser.get();
	ClangTool tool(options.getCompilations(), options.getSourcePathList());

	CheckCounters counters;
	YclassActionFactory factory(counters);
	int result = tool.run(&factory);

	if (counters.object_arith > 0) {
		llvm::errs() << "Object-arithmetic violations: " << counters.object_arith
			     << "\n";
		return 1;
	}

	if (result == 0 && Verbose)
		llvm::errs() << "No yclass navigation violations found.\n";

	return result;
}
