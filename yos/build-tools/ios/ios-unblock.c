/* ios-unblock.c — dlsym-backed forwarders for the six libc functions
 * Apple flags as `unavailable` in the iPhoneSimulator SDK headers.
 *
 * Each forwarder caches the resolved function pointer in a function-
 * scope static after the first dlsym. The symbols live in libSystem
 * which is already mapped by the time main() runs, so RTLD_DEFAULT
 * resolves them without us having to dlopen anything ourselves.
 *
 * If a symbol genuinely isn't there (e.g. a future iOS revision
 * actually drops it), the forwarder returns -ENOSYS / NaN / 0 — the
 * yos bridge then propagates that to the guest as an ordinary errno.
 *
 * NOT included in this TU's translation unit list: ios-unblock.h.
 * Including it here would rename our own function bodies via the
 * #define-redirects. The header is `-include`'d only into the
 * codegen-emitted yos_bridge.c.
 */

/* Deliberately NOT including <math.h> here: this TU is compiled with
 * `-include build-tools/ios/ios-unblock.h`, whose #define redirects
 * (drem -> __yos_ios_unblock_drem, …) would textually rewrite math.h's
 * own declarations of those symbols, propagating their `unavailable`
 * attribute onto OUR forwarders. Forward-declare the libm helpers we
 * fall back on instead. */
#include <dlfcn.h>
#include <errno.h>
#include <stddef.h>
#include <time.h>

extern double frexp(double, int *);    /* libm — not on the unavailable list */
extern double remainder(double, double);
extern double lgamma(double);
/* `isfinite` is a C99 macro that only exists once <math.h> is included;
 * we use the clang/gcc builtin instead so we can stay out of math.h. */

/* Helper macro: resolve once, fall back on -ENOSYS or NaN. */
#define DLSYM_ONCE(slot, name) do { \
    if (!(slot)) { (slot) = dlsym(RTLD_DEFAULT, (name)); } \
} while (0)

int __yos_ios_unblock_clock_settime(clockid_t id, const struct timespec *ts)
{
    static int (*real)(clockid_t, const struct timespec *) = NULL;
    DLSYM_ONCE(real, "clock_settime");
    if (!real) { errno = ENOSYS; return -1; }
    return real(id, ts);
}

double __yos_ios_unblock_drem(double x, double y)
{
    static double (*real)(double, double) = NULL;
    DLSYM_ONCE(real, "drem");
    if (real) return real(x, y);
    /* drem is just an old name for remainder; safe fallback. */
    return remainder(x, y);
}

int __yos_ios_unblock_finite(double x)
{
    static int (*real)(double) = NULL;
    DLSYM_ONCE(real, "finite");
    if (real) return real(x);
    return __builtin_isfinite(x);
}

double __yos_ios_unblock_gamma(double x)
{
    static double (*real)(double) = NULL;
    DLSYM_ONCE(real, "gamma");
    if (real) return real(x);
    /* Historical `gamma` was lgamma on many BSDs; tgamma is the true
     * gamma function — guess at the caller's expectation by deferring
     * to lgamma, matching FreeBSD's <math.h> declaration. */
    return lgamma(x);
}

double __yos_ios_unblock_significand(double x)
{
    static double (*real)(double) = NULL;
    DLSYM_ONCE(real, "significand");
    if (real) return real(x);
    /* significand(x) = x / 2^logb(x). Cheap synth fallback. */
    int e;
    double m = frexp(x, &e);
    return m * 2.0;
}

int __yos_ios_unblock_system(const char *cmd)
{
    static int (*real)(const char *) = NULL;
    DLSYM_ONCE(real, "system");
    if (!real) { errno = ENOSYS; return -1; }
    return real(cmd);
}
