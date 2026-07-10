/* yos_logpath_test.c — yos-authored regression for the bridge calls
 * nvim's v_do_log_to_file makes between function entry and exit.
 * Built with -fstack-protector-strong so the wasm guest gets the
 * SAME canary instrumentation nvim has — meaning a corrupted slot
 * here trips __stack_chk_fail at function exit, exactly like nvim.
 */
#include <atf-c.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdarg.h>

/* Bisect step 1: just time + localtime_r. */
static int seg_time_localtime(void)
{
    volatile uint32_t g = 0xAAAA1111u;
    struct tm local_time;
    memset(&local_time, 0, sizeof local_time);
    time_t now = time(NULL);
    localtime_r(&now, &local_time);
    if (g != 0xAAAA1111u) return 1;
    return 0;
}

/* Bisect step 2: + gettimeofday. */
static int seg_with_gtod(void)
{
    volatile uint32_t g = 0xAAAA1111u;
    struct tm local_time;
    memset(&local_time, 0, sizeof local_time);
    time_t now = time(NULL);
    localtime_r(&now, &local_time);
    struct timeval tv;
    gettimeofday(&tv, NULL);
    if (g != 0xAAAA1111u) return 1;
    return 0;
}

/* Bisect step 3: + snprintf. */
static int seg_with_snprintf(void)
{
    volatile uint32_t g = 0xAAAA1111u;
    struct tm local_time;
    memset(&local_time, 0, sizeof local_time);
    time_t now = time(NULL);
    localtime_r(&now, &local_time);
    struct timeval tv;
    gettimeofday(&tv, NULL);
    char name[32] = {0};
    int64_t pid = getpid();
    snprintf(name, sizeof(name), "?.%-5lld", (long long)pid);
    if (g != 0xAAAA1111u) return 1;
    return 0;
}

/* Bisect step 4: + fopen + fclose (no fprintf). */
static int seg_with_fopen(void)
{
    volatile uint32_t g = 0xAAAA1111u;
    struct tm local_time;
    memset(&local_time, 0, sizeof local_time);
    time_t now = time(NULL);
    localtime_r(&now, &local_time);
    struct timeval tv;
    gettimeofday(&tv, NULL);
    char name[32] = {0};
    int64_t pid = getpid();
    snprintf(name, sizeof(name), "?.%-5lld", (long long)pid);
    FILE *f = fopen("/tmp/yos_logpath_seg.log", "a");
    if (f) fclose(f);
    if (g != 0xAAAA1111u) return 1;
    return 0;
}

/* Bisect step 5: + fprintf. */
static int seg_with_fprintf(void)
{
    volatile uint32_t g = 0xAAAA1111u;
    struct tm local_time;
    memset(&local_time, 0, sizeof local_time);
    time_t now = time(NULL);
    localtime_r(&now, &local_time);
    struct timeval tv;
    gettimeofday(&tv, NULL);
    char name[32] = {0};
    int64_t pid = getpid();
    snprintf(name, sizeof(name), "?.%-5lld", (long long)pid);
    FILE *f = fopen("/tmp/yos_logpath_seg.log", "a");
    if (f) {
        fprintf(f, "%s %d\n", name, 42);
        fclose(f);
    }
    if (g != 0xAAAA1111u) return 1;
    return 0;
}

ATF_TC(seg1_time_localtime);
ATF_TC_HEAD(seg1_time_localtime, tc) { atf_tc_set_md_var(tc, "descr", "time + localtime_r"); }
ATF_TC_BODY(seg1_time_localtime, tc) { ATF_REQUIRE(seg_time_localtime() == 0); }

ATF_TC(seg2_with_gtod);
ATF_TC_HEAD(seg2_with_gtod, tc) { atf_tc_set_md_var(tc, "descr", "+ gettimeofday"); }
ATF_TC_BODY(seg2_with_gtod, tc) { ATF_REQUIRE(seg_with_gtod() == 0); }

ATF_TC(seg3_with_snprintf);
ATF_TC_HEAD(seg3_with_snprintf, tc) { atf_tc_set_md_var(tc, "descr", "+ snprintf"); }
ATF_TC_BODY(seg3_with_snprintf, tc) { ATF_REQUIRE(seg_with_snprintf() == 0); }

ATF_TC(seg4_with_fopen);
ATF_TC_HEAD(seg4_with_fopen, tc) { atf_tc_set_md_var(tc, "descr", "+ fopen+fclose"); }
ATF_TC_BODY(seg4_with_fopen, tc) { ATF_REQUIRE(seg_with_fopen() == 0); }

ATF_TC(seg5_with_fprintf);
ATF_TC_HEAD(seg5_with_fprintf, tc) { atf_tc_set_md_var(tc, "descr", "+ fprintf"); }
ATF_TC_BODY(seg5_with_fprintf, tc) { ATF_REQUIRE(seg_with_fprintf() == 0); }

/* Mimic nvim's logmsg → v_do_log_to_file → fclose path the spawn-
 * failure ILOG triggers in channel_job_start. va_list args sit on
 * THIS function's stack; vfprintf walks them. The full sequence
 * also exercises fputc, fflush, and fclose at function exit, so
 * the canary check at return verifies none of those bridges
 * silently overran a slot. */
#include <stdarg.h>

static int seg_full_log_path(const char *fmt, ...)
{
    volatile uint32_t g0 = 0xA1A1A1A1u;
    struct tm local_time;
    volatile uint32_t g1 = 0xB2B2B2B2u;
    char date_time[20];
    volatile uint32_t g2 = 0xC3C3C3C3u;
    int millis = 0;
    struct timeval tv;
    volatile uint32_t g3 = 0xD4D4D4D4u;
    char name[32] = {0};
    volatile uint32_t g4 = 0xE5E5E5E5u;
    va_list args;
    volatile uint32_t g5 = 0xF6F6F6F6u;

    memset(&local_time, 0, sizeof local_time);
    time_t now = time(NULL);
    localtime_r(&now, &local_time);
    snprintf(date_time, sizeof(date_time), "%04d-%02d-%02d",
             1900 + local_time.tm_year, 1 + local_time.tm_mon, local_time.tm_mday);
    if (gettimeofday(&tv, NULL) == 0) millis = (int)tv.tv_usec / 1000;
    int64_t pid = (int64_t)getpid();
    snprintf(name, sizeof(name), "?.%-5lld", (long long)pid);

    FILE *f = fopen("/tmp/yos_full_log_path.log", "a");
    if (f) {
        fprintf(f, "%s %s.%03d %-10s %s:%d: ",
                "INF", date_time, millis, name, "fn", 42);
        va_start(args, fmt);
        vfprintf(f, fmt, args);
        va_end(args);
        fputc('\n', f);
        fflush(f);
        fclose(f);
    }

    if (g0 != 0xA1A1A1A1u) return 100;
    if (g1 != 0xB2B2B2B2u) return 200;
    if (g2 != 0xC3C3C3C3u) return 300;
    if (g3 != 0xD4D4D4D4u) return 400;
    if (g4 != 0xE5E5E5E5u) return 500;
    if (g5 != 0xF6F6F6F6u) return 600;
    return 0;
}

ATF_TC(seg6_full_log_with_va);
ATF_TC_HEAD(seg6_full_log_with_va, tc) {
    atf_tc_set_md_var(tc, "descr",
        "full v_do_log_to_file path with va_list args (matches nvim's logmsg)");
}
ATF_TC_BODY(seg6_full_log_with_va, tc) {
    int rc = seg_full_log_path("uv_spawn(%s) failed: %s", "/some/exe", "EBADF");
    ATF_REQUIRE_MSG(rc == 0, "full log path returned %d (guard or canary smash)", rc);
}

/* Match the EXACT shape of nvim's `ILOG("uv_spawn(%s) failed: %s",
 * uvproc->uvopts.file, uv_strerror(status))` in libuv_process_spawn:
 * a CALLER frame with a few locals + a stack canary, calling a log
 * macro that takes a variadic format string. The asyncify pass
 * may inline differently than our test's compile, but this is as
 * close as we can get without rebuilding nvim. */
__attribute__((noinline))
static int caller_with_canary(const char *path)
{
    volatile uint32_t guard_top = 0xAABBCCDDu;
    /* simulate libuv_process_spawn's local stack: file pointer,
     * status int, a few uvstdio descriptors, then the ILOG. */
    const char *file = path;
    int status = -22; /* EINVAL */
    char errbuf[64];
    snprintf(errbuf, sizeof(errbuf), "errno=%d", -status);

    int rc = seg_full_log_path("uv_spawn(%s) failed: %s", file, errbuf);
    if (rc) return rc;

    if (guard_top != 0xAABBCCDDu) return 999;
    return 0;
}

ATF_TC(seg7_caller_canary);
ATF_TC_HEAD(seg7_caller_canary, tc) {
    atf_tc_set_md_var(tc, "descr",
        "caller frame canary survives nested log via va_list");
}
ATF_TC_BODY(seg7_caller_canary, tc) {
    int rc = caller_with_canary("/path/to/exe");
    ATF_REQUIRE_MSG(rc == 0, "caller canary smashed; rc=%d", rc);
}

ATF_TP_ADD_TCS(tp) {
    ATF_TP_ADD_TC(tp, seg1_time_localtime);
    ATF_TP_ADD_TC(tp, seg2_with_gtod);
    ATF_TP_ADD_TC(tp, seg3_with_snprintf);
    ATF_TP_ADD_TC(tp, seg4_with_fopen);
    ATF_TP_ADD_TC(tp, seg5_with_fprintf);
    ATF_TP_ADD_TC(tp, seg6_full_log_with_va);
    ATF_TP_ADD_TC(tp, seg7_caller_canary);
    return atf_no_error();
}
