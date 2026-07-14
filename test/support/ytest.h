/*
 * ytest.h — the shared assertion harness for yetty C tests.
 *
 * A single assertion vocabulary for every C test in the tree. Two families:
 *
 *   YTEST_CHECK*    record a failure and CONTINUE, so one run surfaces every
 *                   failure at once.
 *   YTEST_REQUIRE*  record a failure and ABORT the current test path, for the
 *                   cases where continuing would be unsafe (a NULL about to be
 *                   dereferenced, a fixture that failed to build).
 *
 * Every failure prints `file:line: FAIL: ...`. Each executable prints a final
 * `<suite>: N checks, M failed` line and returns:
 *
 *     0   all checks passed
 *     1   at least one check failed
 *    77   the test skipped itself (unsupported environment)
 *
 * The 77 skip code is honoured by ctest through SKIP_RETURN_CODE, wired by the
 * yetty_add_c_test() CMake helper.
 *
 * assert() is BANNED in tests: CMAKE_BUILD_TYPE=Release defines NDEBUG, which
 * compiles assert() out, turning an assert-only test into a false-green run
 * that checks nothing. Every macro here stays live regardless of NDEBUG.
 *
 * Usage
 * -----
 *   #include "ytest.h"   // test/support is on the include path via the helper
 *
 *   static void test_something(struct ytest *test)
 *   {
 *       struct yetty_ycore_int_result res = compute();
 *       YTEST_REQUIRE_OK(test, res);          // aborts this test on failure
 *       YTEST_CHECK_EQ_INT(test, res.value, 42);
 *   }
 *
 *   int main(void)
 *   {
 *       struct ytest test = ytest_begin("mymodule_something");
 *       YTEST_RUN(&test, test_something);       // arms the REQUIRE abort frame
 *       return ytest_end(&test);                // prints summary, returns 0/1/77
 *   }
 *
 * Run each test function through YTEST_RUN(): it arms the setjmp frame a
 * failing REQUIRE longjmps back to, so one aborted test does not stop the
 * others. A REQUIRE that fires outside a YTEST_RUN frame falls back to exiting
 * the process with the failure code, after printing the summary.
 *
 * Argument evaluation: the CHECK/REQUIRE macros may evaluate their arguments
 * more than once (to compare, then to print). Pass plain variables, not
 * side-effecting call expressions — assign a call to a variable first, then
 * assert on the variable. This mirrors the YETTY_RETURN_IF_ERR contract.
 */

#ifndef YETTY_TEST_YTEST_H
#define YETTY_TEST_YTEST_H

#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/ycore/result.h>

/* Per-executable test state. Lives as a local in main() — never a file-scope
 * object — and is threaded to every test function by pointer. */
struct ytest {
    const char *suite;
    unsigned int checks;
    unsigned int failures;
    int skipped;       /* set by YTEST_SKIP before exit(77) */
    int abort_armed;   /* 1 while a YTEST_RUN frame can catch a REQUIRE abort */
    jmp_buf abort_env; /* longjmp target for a failing REQUIRE */
};

/* Print the summary line and map the run to its process exit code. */
static inline int ytest_end(struct ytest *test)
{
    fprintf(stdout, "%s: %u checks, %u failed\n", test->suite, test->checks, test->failures);
    fflush(stdout);
    if (test->skipped) {
        return 77;
    }
    return test->failures ? 1 : 0;
}

/* Abort the current test path. Inside a YTEST_RUN frame this longjmps back to
 * the runner so sibling tests still run; outside one it surfaces the summary
 * and exits with the failure code. */
static inline void ytest__abort(struct ytest *test)
{
    if (test->abort_armed) {
        longjmp(test->abort_env, 1);
    }
    exit(ytest_end(test));
}

/* Record one boolean check. Always counts; on failure prints the location and
 * message, and — when fatal — aborts the current test path. Returns the
 * pass/fail value so REQUIRE-style callers can branch further if they wish. */
#if defined(__clang__) || defined(__GNUC__)
__attribute__((format(printf, 6, 7)))
#endif
static inline int
ytest__check(struct ytest *test, int passed, int fatal, const char *file, int line, const char *fmt,
             ...)
{
    test->checks++;
    if (passed) {
        return 1;
    }
    test->failures++;
    fprintf(stderr, "%s:%d: FAIL: ", file, line);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    fflush(stderr);
    if (fatal) {
        ytest__abort(test);
    }
    return 0;
}

/* Result-ok check: on failure prints the whole error cause chain, then frees
 * it (the test is the end consumer of the error). */
static inline int ytest__check_ok(struct ytest *test, int ok, struct yetty_ycore_error err,
                                  int fatal, const char *file, int line, const char *expr)
{
    test->checks++;
    if (ok) {
        return 1;
    }
    test->failures++;
    fprintf(stderr, "%s:%d: FAIL: expected OK: %s\n", file, line, expr);
    yetty_ycore_error_print(stderr, "  cause", err);
    yetty_ycore_error_destroy(err);
    fflush(stderr);
    if (fatal) {
        ytest__abort(test);
    }
    return 0;
}

/* Result-error check: the expected-error case owns and frees the cause chain. */
static inline int ytest__check_err(struct ytest *test, int is_err, struct yetty_ycore_error err,
                                   int fatal, const char *file, int line, const char *expr)
{
    test->checks++;
    if (is_err) {
        yetty_ycore_error_destroy(err);
        return 1;
    }
    test->failures++;
    fprintf(stderr, "%s:%d: FAIL: expected error: %s\n", file, line, expr);
    fflush(stderr);
    if (fatal) {
        ytest__abort(test);
    }
    return 0;
}

/* NULL-safe string equality: both NULL compare equal; exactly one NULL differs. */
static inline int ytest__str_eq(const char *left, const char *right)
{
    if (left == right) {
        return 1;
    }
    if (!left || !right) {
        return 0;
    }
    return strcmp(left, right) == 0;
}

static inline const char *ytest__str_or_null(const char *value)
{
    return value ? value : "(null)";
}

static inline int ytest__near(double left, double right, double tolerance)
{
    double diff = left - right;
    if (diff < 0) {
        diff = -diff;
    }
    return diff <= tolerance;
}

static inline struct ytest ytest_begin(const char *suite)
{
    struct ytest test = {0};
    test.suite = suite;
    return test;
}

/* Run one test function under an armed abort frame. A failing REQUIRE inside
 * `fn` longjmps back here, skipping the rest of `fn` but not the whole run. */
#define YTEST_RUN(test_ptr, fn)                                                                    \
    do {                                                                                           \
        (test_ptr)->abort_armed = 1;                                                               \
        if (setjmp((test_ptr)->abort_env) == 0) {                                                  \
            fn(test_ptr);                                                                          \
        }                                                                                          \
        (test_ptr)->abort_armed = 0;                                                               \
    } while (0)

/* Skip the whole executable: print the reason and exit with the ctest skip
 * code. Use at the top of main() when the environment cannot support the
 * test at all. */
#define YTEST_SKIP(test_ptr, ...)                                                                  \
    do {                                                                                           \
        fprintf(stderr, "SKIP: ");                                                                 \
        fprintf(stderr, __VA_ARGS__);                                                              \
        fputc('\n', stderr);                                                                       \
        (test_ptr)->skipped = 1;                                                                   \
        exit(77);                                                                                  \
    } while (0)

/*---------------------------------------------------------------------------
 * Boolean.
 *-------------------------------------------------------------------------*/
#define YTEST_CHECK(test_ptr, cond)                                                                \
    ((void)ytest__check((test_ptr), (cond) ? 1 : 0, 0, __FILE__, __LINE__, "CHECK: %s", #cond))
#define YTEST_REQUIRE(test_ptr, cond)                                                              \
    ((void)ytest__check((test_ptr), (cond) ? 1 : 0, 1, __FILE__, __LINE__, "REQUIRE: %s", #cond))

/*---------------------------------------------------------------------------
 * Integer equality.
 *-------------------------------------------------------------------------*/
#define YTEST_CHECK_EQ_INT(test_ptr, actual, expected)                                             \
    ((void)ytest__check((test_ptr), (long)(actual) == (long)(expected), 0, __FILE__, __LINE__,     \
                        "EQ_INT: %s == %s (%ld vs %ld)", #actual, #expected, (long)(actual),       \
                        (long)(expected)))
#define YTEST_REQUIRE_EQ_INT(test_ptr, actual, expected)                                           \
    ((void)ytest__check((test_ptr), (long)(actual) == (long)(expected), 1, __FILE__, __LINE__,     \
                        "EQ_INT: %s == %s (%ld vs %ld)", #actual, #expected, (long)(actual),       \
                        (long)(expected)))

/*---------------------------------------------------------------------------
 * Size equality.
 *-------------------------------------------------------------------------*/
#define YTEST_CHECK_EQ_SIZE(test_ptr, actual, expected)                                            \
    ((void)ytest__check((test_ptr), (size_t)(actual) == (size_t)(expected), 0, __FILE__, __LINE__, \
                        "EQ_SIZE: %s == %s (%zu vs %zu)", #actual, #expected, (size_t)(actual),    \
                        (size_t)(expected)))
#define YTEST_REQUIRE_EQ_SIZE(test_ptr, actual, expected)                                          \
    ((void)ytest__check((test_ptr), (size_t)(actual) == (size_t)(expected), 1, __FILE__, __LINE__, \
                        "EQ_SIZE: %s == %s (%zu vs %zu)", #actual, #expected, (size_t)(actual),    \
                        (size_t)(expected)))

/*---------------------------------------------------------------------------
 * String equality (NULL-safe).
 *-------------------------------------------------------------------------*/
#define YTEST_CHECK_STR_EQ(test_ptr, actual, expected)                                             \
    ((void)ytest__check((test_ptr), ytest__str_eq((actual), (expected)), 0, __FILE__, __LINE__,    \
                        "STR_EQ: %s == %s (\"%s\" vs \"%s\")", #actual, #expected,                 \
                        ytest__str_or_null(actual), ytest__str_or_null(expected)))
#define YTEST_REQUIRE_STR_EQ(test_ptr, actual, expected)                                           \
    ((void)ytest__check((test_ptr), ytest__str_eq((actual), (expected)), 1, __FILE__, __LINE__,    \
                        "STR_EQ: %s == %s (\"%s\" vs \"%s\")", #actual, #expected,                 \
                        ytest__str_or_null(actual), ytest__str_or_null(expected)))

/*---------------------------------------------------------------------------
 * Memory equality.
 *-------------------------------------------------------------------------*/
#define YTEST_CHECK_MEM_EQ(test_ptr, actual, expected, len)                                        \
    ((void)ytest__check((test_ptr), memcmp((actual), (expected), (len)) == 0, 0, __FILE__,         \
                        __LINE__, "MEM_EQ: %s == %s (%zu bytes)", #actual, #expected,              \
                        (size_t)(len)))
#define YTEST_REQUIRE_MEM_EQ(test_ptr, actual, expected, len)                                      \
    ((void)ytest__check((test_ptr), memcmp((actual), (expected), (len)) == 0, 1, __FILE__,         \
                        __LINE__, "MEM_EQ: %s == %s (%zu bytes)", #actual, #expected,              \
                        (size_t)(len)))

/*---------------------------------------------------------------------------
 * Float equality within a tolerance.
 *-------------------------------------------------------------------------*/
#define YTEST_CHECK_NEAR(test_ptr, actual, expected, tolerance)                                    \
    ((void)ytest__check((test_ptr), ytest__near((actual), (expected), (tolerance)), 0, __FILE__,   \
                        __LINE__, "NEAR: %s ~= %s (%g vs %g, tol %g)", #actual, #expected,         \
                        (double)(actual), (double)(expected), (double)(tolerance)))
#define YTEST_REQUIRE_NEAR(test_ptr, actual, expected, tolerance)                                  \
    ((void)ytest__check((test_ptr), ytest__near((actual), (expected), (tolerance)), 1, __FILE__,   \
                        __LINE__, "NEAR: %s ~= %s (%g vs %g, tol %g)", #actual, #expected,         \
                        (double)(actual), (double)(expected), (double)(tolerance)))

/*---------------------------------------------------------------------------
 * NULL / non-NULL.
 *-------------------------------------------------------------------------*/
#define YTEST_CHECK_NULL(test_ptr, ptr)                                                            \
    ((void)ytest__check((test_ptr), (ptr) == NULL, 0, __FILE__, __LINE__, "NULL: %s", #ptr))
#define YTEST_REQUIRE_NULL(test_ptr, ptr)                                                          \
    ((void)ytest__check((test_ptr), (ptr) == NULL, 1, __FILE__, __LINE__, "NULL: %s", #ptr))
#define YTEST_CHECK_NOT_NULL(test_ptr, ptr)                                                        \
    ((void)ytest__check((test_ptr), (ptr) != NULL, 0, __FILE__, __LINE__, "NOT_NULL: %s", #ptr))
#define YTEST_REQUIRE_NOT_NULL(test_ptr, ptr)                                                      \
    ((void)ytest__check((test_ptr), (ptr) != NULL, 1, __FILE__, __LINE__, "NOT_NULL: %s", #ptr))

/*---------------------------------------------------------------------------
 * Yetty typed-result ok / error. Pass the WHOLE result struct (a variable),
 * not its .error field. The macros only read .error when the result is on
 * the error side of the tagged union.
 *-------------------------------------------------------------------------*/
#define YTEST_CHECK_OK(test_ptr, res)                                                              \
    do {                                                                                           \
        if (YETTY_IS_OK(res)) {                                                                    \
            (void)ytest__check((test_ptr), 1, 0, __FILE__, __LINE__, "OK: %s", #res);              \
        } else {                                                                                   \
            (void)ytest__check_ok((test_ptr), 0, (res).error, 0, __FILE__, __LINE__, #res);        \
        }                                                                                          \
    } while (0)
#define YTEST_REQUIRE_OK(test_ptr, res)                                                            \
    do {                                                                                           \
        if (YETTY_IS_OK(res)) {                                                                    \
            (void)ytest__check((test_ptr), 1, 1, __FILE__, __LINE__, "OK: %s", #res);              \
        } else {                                                                                   \
            (void)ytest__check_ok((test_ptr), 0, (res).error, 1, __FILE__, __LINE__, #res);        \
        }                                                                                          \
    } while (0)
#define YTEST_CHECK_ERR(test_ptr, res)                                                             \
    do {                                                                                           \
        if (YETTY_IS_ERR(res)) {                                                                   \
            (void)ytest__check_err((test_ptr), 1, (res).error, 0, __FILE__, __LINE__, #res);       \
        } else {                                                                                   \
            (void)ytest__check((test_ptr), 0, 0, __FILE__, __LINE__, "expected error: %s", #res);  \
        }                                                                                          \
    } while (0)
#define YTEST_REQUIRE_ERR(test_ptr, res)                                                           \
    do {                                                                                           \
        if (YETTY_IS_ERR(res)) {                                                                   \
            (void)ytest__check_err((test_ptr), 1, (res).error, 1, __FILE__, __LINE__, #res);       \
        } else {                                                                                   \
            (void)ytest__check((test_ptr), 0, 1, __FILE__, __LINE__, "expected error: %s", #res);  \
        }                                                                                          \
    } while (0)

#endif /* YETTY_TEST_YTEST_H */
