/* ios-unblock.h — preamble injected with `-include` when building
 * yos against the iPhoneSimulator SDK.
 *
 * The iOS headers mark six libc functions as `__attribute__((
 * unavailable("not available on iOS")))` even on the simulator. The
 * symbols still ship in libSystem at runtime (the simulator runs on
 * the host kernel) but clang refuses to compile direct calls.
 *
 * This header redirects each call site to a tiny dlsym-backed
 * forwarder defined in ios-unblock.c, so the codegen-emitted bridges
 * in src/yos/codegen/yos_bridge.c compile and link.
 *
 * Single TU rule: any file that needs the real names must NOT
 * `-include` this header — only the auto-bridge code does, and that
 * code only calls the renamed forwarders. Hand-written sources should
 * stay clear of these six names anyway on iOS.
 */
#ifndef YOS_IOS_UNBLOCK_H
#define YOS_IOS_UNBLOCK_H

/* Pre-load the SDK headers that declare the six unavailable symbols
 * with their original names. We do this BEFORE the macro redirects so
 * the SDK decls are evaluated once, untouched by macro substitution.
 * Subsequent `#include <stdlib.h>` etc. from the TU body hit the
 * standard multi-include guards and become no-ops, so the redirected
 * macros never get applied to the SDK declarations. */
#include <math.h>
#include <stdlib.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

int    __yos_ios_unblock_clock_settime(clockid_t, const struct timespec *);
double __yos_ios_unblock_drem(double, double);
int    __yos_ios_unblock_finite(double);
double __yos_ios_unblock_gamma(double);
double __yos_ios_unblock_significand(double);
int    __yos_ios_unblock_system(const char *);

#define clock_settime __yos_ios_unblock_clock_settime
#define drem          __yos_ios_unblock_drem
#define finite        __yos_ios_unblock_finite
#define gamma         __yos_ios_unblock_gamma
#define significand   __yos_ios_unblock_significand
#define system        __yos_ios_unblock_system

#ifdef __cplusplus
}
#endif

#endif /* YOS_IOS_UNBLOCK_H */
