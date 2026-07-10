/* tests/freebsd-libc/atf-c.h — minimal ATF-compatible shim.
 *
 * FreeBSD's libc test files use the ATF (Automated Testing Framework)
 * macros. We rewrite the macros so an ATF test compiles as a plain
 * C program: each ATF_TC_BODY becomes a normal function, the
 * ATF_TP_ADD_TCS table is ignored (a separate gen_driver.py
 * pre-generates the runtime table), and ATF_REQUIRE turns into a
 * write()-based assertion that bumps a global failure counter.
 *
 * We deliberately avoid fprintf in the failure path: yos's wasm
 * variadic-printf bridge has been finicky in the past and we want
 * the test framework itself to be uncorrupted by libc bugs we're
 * trying to test for. So failure messages go through write(2) +
 * tiny manual formatting helpers.
 */
#ifndef YOS_ATF_C_H
#define YOS_ATF_C_H

#include <unistd.h>
#include <string.h>

/* Globals defined in run.c. */
extern int yos_atf_failed;
extern const char *yos_atf_current_test;

/* Tiny write-based diagnostic — concatenates up to 6 strings + a
 * decimal int, terminates with newline. Avoids varargs entirely so
 * the test harness keeps working even when the libc-under-test is
 * broken in printf. */
static inline void yos_atf_fail_msg(const char *file, int line,
                                    const char *expr)
{
    char buf[256];
    size_t n = 0;
    const char *prefix = "  FAIL ";
    while (*prefix && n < sizeof(buf)-1) buf[n++] = *prefix++;
    while (*file && n < sizeof(buf)-1) buf[n++] = *file++;
    if (n < sizeof(buf)-1) buf[n++] = ':';
    /* itoa for line */
    char num[16];
    int  ni = 0;
    int  lineval = line;
    if (lineval == 0) num[ni++] = '0';
    else {
        char tmp[16]; int ti = 0;
        if (lineval < 0) { buf[n++] = '-'; lineval = -lineval; }
        while (lineval > 0 && ti < 15) { tmp[ti++] = '0' + (lineval % 10); lineval /= 10; }
        while (ti > 0) num[ni++] = tmp[--ti];
    }
    for (int i = 0; i < ni && n < sizeof(buf)-1; i++) buf[n++] = num[i];
    const char *mid = ": ATF_REQUIRE(";
    while (*mid && n < sizeof(buf)-1) buf[n++] = *mid++;
    while (*expr && n < sizeof(buf)-1) buf[n++] = *expr++;
    if (n < sizeof(buf)-2) { buf[n++] = ')'; buf[n++] = '\n'; }
    write(2, buf, n);
}

#define ATF_TC(name)
#define ATF_TC_WITH_CLEANUP(name)
#define ATF_TC_WITHOUT_HEAD(name)
#define ATF_TC_HEAD(name, tc)       static void __unused_head_##name(void *tc)
#define ATF_TC_BODY(name, tc)       void atf_tc_body_##name(void *tc)
#define ATF_TC_CLEANUP(name, tc)    static void __unused_cleanup_##name(void *tc)

#define atf_tc_set_md_var(tc, key, val)        ((void)0)
#define atf_tc_get_md_var(tc, key)             ""
#define atf_tc_skip(...)                       do { write(2, "  [SKIP]\n", 9); return; } while (0)
#define atf_tc_expect_fail(...)                ((void)0)
#define atf_tc_expect_pass()                   ((void)0)
#define atf_tc_expect_signal(...)              ((void)0)
#define atf_tc_expect_exit(...)                ((void)0)
#define atf_tc_expect_timeout(...)             ((void)0)
#define atf_tc_expect_death(...)               ((void)0)

#define ATF_REQUIRE(cond) \
    do { \
        if (!(cond)) { \
            yos_atf_fail_msg(__FILE__, __LINE__, #cond); \
            yos_atf_failed++; \
        } \
    } while (0)

#define ATF_REQUIRE_MSG(cond, fmt, ...)        ATF_REQUIRE(cond)
#define ATF_REQUIRE_EQ(want, got)              ATF_REQUIRE((want) == (got))
#define ATF_REQUIRE_EQ_MSG(want, got, ...)     ATF_REQUIRE_EQ(want, got)
#define ATF_REQUIRE_STREQ(want, got)           ATF_REQUIRE(strcmp((want), (got)) == 0)
#define ATF_REQUIRE_INTEQ(want, got)           ATF_REQUIRE_EQ((want), (got))
#define ATF_REQUIRE_ERRNO(eno, expr) \
    do { errno = 0; ATF_REQUIRE(expr); ATF_REQUIRE(errno == (eno)); } while (0)

#define ATF_CHECK(cond)                        ATF_REQUIRE(cond)
#define ATF_CHECK_MSG(cond, ...)               ATF_REQUIRE(cond)
#define ATF_CHECK_EQ(want, got)                ATF_REQUIRE_EQ(want, got)
#define ATF_CHECK_EQ_MSG(want, got, ...)       ATF_REQUIRE_EQ(want, got)
#define ATF_CHECK_STREQ(want, got)             ATF_REQUIRE_STREQ(want, got)
#define ATF_CHECK_STREQ_MSG(want, got, ...)    ATF_REQUIRE_STREQ(want, got)
#define ATF_CHECK_ERRNO(eno, expr)             ATF_REQUIRE_ERRNO(eno, expr)
#define ATF_CHECK_INTEQ(want, got)             ATF_REQUIRE_EQ(want, got)
#define ATF_CHECK_MATCH(re, str)               ATF_REQUIRE((str) != (void *)0)

/* ATF_TP_ADD_TCS is the test-registration body. We ignore it —
 * gen_driver.py walks the source for ATF_TC_BODY and writes its
 * own yos_atf_table[]. The macro must still produce something the
 * compiler accepts, so it expands to a static unused function. */
#define ATF_TP_ADD_TCS(tp)                static int __unused_atf_add(void *tp)
#define ATF_TP_ADD_TC(tp, name)           ((void)0)
typedef int atf_no_error_t;
typedef int atf_error_t;
#undef atf_no_error
#define atf_no_error()                    0

#endif /* YOS_ATF_C_H */
