/* yos_snprintf_test.c — exercise the variadic snprintf bridge.
 * The clearenv FreeBSD test failed because `snprintf("%s_%d", "x", 1)`
 * was producing "x_0" — i.e. the integer arg's wasm-side va_list slot
 * was being misread. Pin that down independently.
 */
#include <atf-c.h>
#include <stdio.h>
#include <string.h>

ATF_TC(snprintf_int);
ATF_TC_HEAD(snprintf_int, tc) { atf_tc_set_md_var(tc, "descr", "snprintf %d"); }
ATF_TC_BODY(snprintf_int, tc) {
    char buf[32];
    int n = snprintf(buf, sizeof buf, "%d", 42);
    ATF_REQUIRE(n == 2);
    ATF_REQUIRE_STREQ(buf, "42");
    n = snprintf(buf, sizeof buf, "%d", -7);
    ATF_REQUIRE(n == 2);
    ATF_REQUIRE_STREQ(buf, "-7");
}

ATF_TC(snprintf_str);
ATF_TC_HEAD(snprintf_str, tc) { atf_tc_set_md_var(tc, "descr", "snprintf %s"); }
ATF_TC_BODY(snprintf_str, tc) {
    char buf[32];
    int n = snprintf(buf, sizeof buf, "%s", "hello");
    ATF_REQUIRE(n == 5);
    ATF_REQUIRE_STREQ(buf, "hello");
}

ATF_TC(snprintf_str_int);
ATF_TC_HEAD(snprintf_str_int, tc) { atf_tc_set_md_var(tc, "descr", "snprintf %s_%d"); }
ATF_TC_BODY(snprintf_str_int, tc) {
    char buf[32];
    int n = snprintf(buf, sizeof buf, "%s_%d", "TEST", 5);
    ATF_REQUIRE_STREQ(buf, "TEST_5");
    ATF_REQUIRE(n == 6);
    /* The clearenv test calls this in a loop with growing i. */
    for (int i = 0; i < 4; i++) {
        snprintf(buf, sizeof buf, "%s_%d", "TEST", i);
        char want[16];
        snprintf(want, sizeof want, "TEST_%d", i);
        ATF_REQUIRE_STREQ(buf, want);
    }
}

ATF_TP_ADD_TCS(tp) {
    ATF_TP_ADD_TC(tp, snprintf_int);
    ATF_TP_ADD_TC(tp, snprintf_str);
    ATF_TP_ADD_TC(tp, snprintf_str_int);
    return atf_no_error();
}
