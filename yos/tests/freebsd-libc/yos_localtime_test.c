/* yos_localtime_test.c — yos-authored regression for the
 * time/getenv/strncmp/localtime_r sequence that nvim runs at
 * startup and that smashes its stack canary today.
 *
 * Intent: walk the same bridges in the same order, sandwich each
 * critical local between cookie variables, and verify the cookies
 * are intact at the end. Each cookie is a different magic so a
 * spillover tells us which slot was clobbered.
 */
#include <atf-c.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

ATF_TC(localtime_r_basic);
ATF_TC_HEAD(localtime_r_basic, tc) {
    atf_tc_set_md_var(tc, "descr",
        "localtime_r writes a sane struct tm and doesn't trash the canary");
}
ATF_TC_BODY(localtime_r_basic, tc) {
    volatile uint32_t guard_lo = 0xA5A5A5A5u;
    time_t t = 1700000000;  /* 2023-11-14 22:13:20 UTC */
    struct tm tm;
    volatile uint32_t guard_hi = 0x5A5A5A5Au;

    memset(&tm, 0, sizeof tm);
    struct tm *r = localtime_r(&t, &tm);

    ATF_REQUIRE(r == &tm);
    ATF_REQUIRE(tm.tm_year == 123);   /* 2023 - 1900 */
    ATF_REQUIRE(tm.tm_mon  == 10);    /* November = 10 */
    ATF_REQUIRE(guard_lo == 0xA5A5A5A5u);
    ATF_REQUIRE(guard_hi == 0x5A5A5A5Au);
}

ATF_TC(nvim_startup_seq);
ATF_TC_HEAD(nvim_startup_seq, tc) {
    atf_tc_set_md_var(tc, "descr",
        "time/getenv/strncmp/localtime_r in the order nvim runs them");
}
ATF_TC_BODY(nvim_startup_seq, tc) {
    volatile uint32_t guard_a = 0xDEADBEEFu;
    time_t t = 0;
    volatile uint32_t guard_b = 0xCAFEBABEu;
    char *tz = (char *)0;
    volatile uint32_t guard_c = 0xFEEDFACEu;
    struct tm tm;
    volatile uint32_t guard_d = 0xBAADF00Du;

    memset(&tm, 0, sizeof tm);

    (void)time(&t);
    tz = getenv("TZ");

    /* nvim's /etc/localtime path probe goes through a strncmp on
     * the TZ env before falling back. Mirror it. */
    int cmp = (tz != (void *)0) ? strncmp(tz, ":", 1) : -1;
    (void)cmp;

    localtime_r(&t, &tm);

    ATF_REQUIRE(t > 0);
    ATF_REQUIRE(tm.tm_year > 100);  /* > year 2000 */
    ATF_REQUIRE(guard_a == 0xDEADBEEFu);
    ATF_REQUIRE(guard_b == 0xCAFEBABEu);
    ATF_REQUIRE(guard_c == 0xFEEDFACEu);
    ATF_REQUIRE(guard_d == 0xBAADF00Du);
}

ATF_TC(tm_size_is_44);
ATF_TC_HEAD(tm_size_is_44, tc) {
    atf_tc_set_md_var(tc, "descr",
        "wasm struct tm is 44 bytes (matches cv_tm_h2w write size)");
}
ATF_TC_BODY(tm_size_is_44, tc) {
    /* If clang's wasm32 ABI started padding the tm_zone pointer
     * field or aligning the struct to 8 bytes, our 44-byte writer
     * would underwrite (= leak stale stack into the next field) or
     * the guest would expect more bytes than we deliver. Pin both. */
    ATF_REQUIRE(sizeof(struct tm) == 44);
    ATF_REQUIRE(_Alignof(struct tm) == 4);
}

ATF_TC(log_write_path);
ATF_TC_HEAD(log_write_path, tc) {
    atf_tc_set_md_var(tc, "descr",
        "exact nvim do_log_to_file() shape — tm is in caller's stack");
}
ATF_TC_BODY(log_write_path, tc) {
    /* nvim's do_log_to_file does roughly:
     *   FILE *f = fopen(...);
     *   time_t now = time(NULL);
     *   getenv("TZ");
     *   ... strncmp on a level name ...
     *   struct tm tm;
     *   localtime_r(&now, &tm);
     *   strftime(...);
     *   fclose(f);
     * When the function returns the compiler checks the stack canary.
     * Walk the same shape with guards either side of every local. */
    volatile uint32_t g0 = 0xAA00u;
    FILE *f = fopen("/tmp/yos_log_test", "a");
    volatile uint32_t g1 = 0xBB00u;
    time_t now = 0;
    volatile uint32_t g2 = 0xCC00u;
    char *tz = getenv("TZ");
    volatile uint32_t g3 = 0xDD00u;
    char buf[8] = "INFO";
    volatile uint32_t g4 = 0xEE00u;
    int cmp = strncmp(buf, "INFO", 4);
    volatile uint32_t g5 = 0xFF00u;
    struct tm tm;
    volatile uint32_t g6 = 0x1100u;

    memset(&tm, 0, sizeof tm);
    now = time(0);
    localtime_r(&now, &tm);

    if (f) fclose(f);
    (void)tz; (void)cmp;

    ATF_REQUIRE(g0 == 0xAA00u);
    ATF_REQUIRE(g1 == 0xBB00u);
    ATF_REQUIRE(g2 == 0xCC00u);
    ATF_REQUIRE(g3 == 0xDD00u);
    ATF_REQUIRE(g4 == 0xEE00u);
    ATF_REQUIRE(g5 == 0xFF00u);
    ATF_REQUIRE(g6 == 0x1100u);
    ATF_REQUIRE(tm.tm_year > 100);
}

ATF_TP_ADD_TCS(tp) {
    ATF_TP_ADD_TC(tp, localtime_r_basic);
    ATF_TP_ADD_TC(tp, nvim_startup_seq);
    ATF_TP_ADD_TC(tp, tm_size_is_44);
    ATF_TP_ADD_TC(tp, log_write_path);
    return atf_no_error();
}
