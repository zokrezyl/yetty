/*
 * yexpr expression-parser contract test (#416) — pure, headless.
 *
 * Covers the grammar's ACCEPT/REJECT surface: well-formed expressions parse,
 * and malformed ones are rejected with an error Result. This deliberately does
 * NOT inspect the returned AST: `yetty_yexpr_parse` returns its node arena by
 * value while `root` (and every node child-link) is a pointer into that arena,
 * so after the by-value return those pointers reference the callee's popped
 * stack frame — dereferencing `.value.root` is a use-after-return (ASAN
 * confirms it; it only "works" in release under NRVO). That ownership bug is
 * reported separately; here we exercise the parser purely through the Result
 * ok/err flag, which is safe regardless.
 */

#include <yetty/yexpr/yexpr.h>

#include "ytest.h"

#include <stddef.h>
#include <string.h>

static int parses_ok(const char *source)
{
    struct yetty_yexpr_parse_result r = yetty_yexpr_parse(source, strlen(source));
    return YETTY_IS_OK(r);
}

/*---------------------------------------------------------------------------
 * Well-formed expressions across every grammar production parse successfully.
 *-------------------------------------------------------------------------*/
static void test_accepts_valid(struct ytest *test)
{
    YTEST_CHECK(test, parses_ok("42"));            /* number */
    YTEST_CHECK(test, parses_ok("3.14"));          /* decimal */
    YTEST_CHECK(test, parses_ok("x"));             /* identifier */
    YTEST_CHECK(test, parses_ok("1 + 2 * 3"));     /* precedence mix */
    YTEST_CHECK(test, parses_ok("2 ^ 3"));         /* power */
    YTEST_CHECK(test, parses_ok("-5"));            /* unary negation */
    YTEST_CHECK(test, parses_ok("-x * 2"));        /* unary in a term */
    YTEST_CHECK(test, parses_ok("(1 + 2) * 3"));   /* parentheses */
    YTEST_CHECK(test, parses_ok("((1 + 2))"));     /* nested parentheses */
    YTEST_CHECK(test, parses_ok("sin(x)"));        /* 1-arg call */
    YTEST_CHECK(test, parses_ok("max(1, 2)"));     /* 2-arg call */
    YTEST_CHECK(test, parses_ok("f(a, b, c, d)")); /* 4 args (the limit) */
    YTEST_CHECK(test, parses_ok("sin(x) + cos(y)")); /* two calls */
    YTEST_CHECK(test, parses_ok("@buffer3"));      /* buffer reference */
}

/*---------------------------------------------------------------------------
 * Malformed input is rejected with an error Result (no crash).
 *-------------------------------------------------------------------------*/
static void test_rejects_malformed(struct ytest *test)
{
    YTEST_CHECK(test, !parses_ok(""));              /* empty */
    YTEST_CHECK(test, !parses_ok("1 +"));           /* dangling operator */
    YTEST_CHECK(test, !parses_ok(")"));             /* unmatched close paren */
    YTEST_CHECK(test, !parses_ok("(1 + 2"));        /* unclosed paren */
    YTEST_CHECK(test, !parses_ok("* 5"));           /* leading binary op */
    YTEST_CHECK(test, !parses_ok("f(a, b, c, d, e)")); /* one past the 4-arg limit */
    YTEST_CHECK(test, !parses_ok("@"));             /* '@' with no identifier */
}

int main(void)
{
    struct ytest test = ytest_begin("yexpr_parse");
    YTEST_RUN(&test, test_accepts_valid);
    YTEST_RUN(&test, test_rejects_malformed);
    return ytest_end(&test);
}
