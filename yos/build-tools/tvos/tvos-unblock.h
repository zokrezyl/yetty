/* tvos-unblock.h — preamble injected with `-include` when building
 * yos against the AppleTVOS SDK.
 *
 * tvOS marks more libc functions `unavailable` than iOS Simulator:
 * everything iOS hides PLUS `daemon` and `execvP`. The symbols still
 * ship in libSystem on the device; only the headers refuse to declare
 * them. This header redirects each codegen-bridge call site to a
 * dlsym-backed forwarder defined in tvos-unblock.c.
 *
 * Single TU rule: only `-include`'d into the codegen-bridge TU. Hand-
 * written sources must not reach these names on tvOS.
 */
#ifndef YOS_TVOS_UNBLOCK_H
#define YOS_TVOS_UNBLOCK_H

/* Pre-load SDK headers that declare the unavailable symbols with
 * their original names. Subsequent re-inclusions hit the multi-include
 * guards and become no-ops, so the macros below don't get applied to
 * the SDK declarations. */
#include <math.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

int    __yos_tvos_unblock_clock_settime(clockid_t, const struct timespec *);
double __yos_tvos_unblock_drem(double, double);
int    __yos_tvos_unblock_finite(double);
double __yos_tvos_unblock_gamma(double);
double __yos_tvos_unblock_significand(double);
int    __yos_tvos_unblock_system(const char *);
int    __yos_tvos_unblock_daemon(int, int);
int    __yos_tvos_unblock_execvP(const char *, const char *, char *const []);
void   __yos_tvos_unblock_longjmperror(void);

#define clock_settime __yos_tvos_unblock_clock_settime
#define drem          __yos_tvos_unblock_drem
#define finite        __yos_tvos_unblock_finite
#define gamma         __yos_tvos_unblock_gamma
#define significand   __yos_tvos_unblock_significand
#define system        __yos_tvos_unblock_system
#define daemon        __yos_tvos_unblock_daemon
#define execvP        __yos_tvos_unblock_execvP
#define longjmperror  __yos_tvos_unblock_longjmperror

#ifdef __cplusplus
}
#endif

#endif /* YOS_TVOS_UNBLOCK_H */
