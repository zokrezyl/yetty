/* yos_time_test.c — yos-authored regression for the time(2) bridge.
 *
 * Why this exists:
 *   The auto-generated bridge for time() did:
 *       long *a0_h = (long *)(ctx->memory + a0);
 *       long _r = time(a0_h);
 *   Host glibc's time() writes a full 8-byte time_t through that
 *   pointer, but FreeBSD i386 (which is what our wasm32 sysroot
 *   models — see x86/_types.h: when !__LP64__, __time_t is
 *   __int32_t) makes wasm time_t a 4-byte int. The wasm app calls
 *   time(&t) where &t is a 4-byte slot on the wasm stack; the host
 *   then scribbles 4 extra bytes into the next stack slot, which is
 *   often the SSP guard cookie. nvim hit this on startup as
 *   `__stack_chk_fail`.
 *
 * The test sandwiches `time_t now` between two cookie variables on
 * the stack and verifies they're untouched after time(&now). Using
 * volatile + explicit reads keeps the optimiser from eliding the
 * cookies. We also check the return value matches the out-param.
 */
#include <atf-c.h>
#include <time.h>
#include <stdint.h>

ATF_TC(time_no_arg);
ATF_TC_HEAD(time_no_arg, tc) {
    atf_tc_set_md_var(tc, "descr", "time(NULL) returns a plausible value");
}
ATF_TC_BODY(time_no_arg, tc) {
    time_t r = time((time_t *)0);
    ATF_REQUIRE(r > 0);
    /* sanity floor: well after 2020-01-01. */
    ATF_REQUIRE(r > (time_t)1577836800);
}

ATF_TC(time_out_param);
ATF_TC_HEAD(time_out_param, tc) {
    atf_tc_set_md_var(tc, "descr", "time(&t) writes the same value it returns");
}
ATF_TC_BODY(time_out_param, tc) {
    time_t now = 0;
    time_t r = time(&now);
    ATF_REQUIRE(r > 0);
    ATF_REQUIRE(r == now);
}

ATF_TC(time_no_overflow);
ATF_TC_HEAD(time_no_overflow, tc) {
    atf_tc_set_md_var(tc, "descr",
        "time(&t) does not overflow into adjacent stack slots");
}
ATF_TC_BODY(time_no_overflow, tc) {
    /* Stack layout: [guard_lo] [now] [guard_hi]. If the host writes
     * 8 bytes into the 4-byte `now` slot it spills into one of the
     * guards. Volatile keeps the compiler from reordering or
     * eliding. */
    volatile uint32_t guard_lo = 0xDEADBEEFu;
    volatile time_t   now      = 0;
    volatile uint32_t guard_hi = 0xCAFEBABEu;

    (void)time((time_t *)&now);

    ATF_REQUIRE(guard_lo == 0xDEADBEEFu);
    ATF_REQUIRE(guard_hi == 0xCAFEBABEu);
    ATF_REQUIRE(now > 0);
}

ATF_TC(time_size_is_4);
ATF_TC_HEAD(time_size_is_4, tc) {
    atf_tc_set_md_var(tc, "descr",
        "wasm time_t is 4 bytes (FreeBSD i386 __int32_t)");
}
ATF_TC_BODY(time_size_is_4, tc) {
    /* If this fails, the FreeBSD sysroot has slipped to LP64 and the
     * narrow-pointer bridge fix would no longer be needed (and
     * conversely is wrong if still applied). Pin the assumption. */
    ATF_REQUIRE(sizeof(time_t) == 4);
}

ATF_TP_ADD_TCS(tp) {
    ATF_TP_ADD_TC(tp, time_no_arg);
    ATF_TP_ADD_TC(tp, time_out_param);
    ATF_TP_ADD_TC(tp, time_no_overflow);
    ATF_TP_ADD_TC(tp, time_size_is_4);
    return atf_no_error();
}
