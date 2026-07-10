/* tvos-unblock.c — dlsym-backed forwarders for libc symbols Apple
 * marks `unavailable` in the AppleTVOS SDK headers. They still ship
 * in libSystem at runtime; only the headers refuse to declare them.
 *
 * NOT `-include`'d with tvos-unblock.h itself: that would rewrite
 * our own definitions through the macro redirects. We declare the
 * libm helpers we use as plain externs to stay out of <math.h>.
 */

#include <dlfcn.h>
#include <errno.h>
#include <stddef.h>
#include <time.h>

extern double frexp(double, int *);
extern double remainder(double, double);
extern double lgamma(double);

#define DLSYM_ONCE(slot, name) do { \
    if (!(slot)) { (slot) = dlsym(RTLD_DEFAULT, (name)); } \
} while (0)

int __yos_tvos_unblock_clock_settime(clockid_t id, const struct timespec *ts)
{
    static int (*real)(clockid_t, const struct timespec *) = NULL;
    DLSYM_ONCE(real, "clock_settime");
    if (!real) { errno = ENOSYS; return -1; }
    return real(id, ts);
}

double __yos_tvos_unblock_drem(double x, double y)
{
    static double (*real)(double, double) = NULL;
    DLSYM_ONCE(real, "drem");
    if (real) return real(x, y);
    return remainder(x, y);
}

int __yos_tvos_unblock_finite(double x)
{
    static int (*real)(double) = NULL;
    DLSYM_ONCE(real, "finite");
    if (real) return real(x);
    return __builtin_isfinite(x);
}

double __yos_tvos_unblock_gamma(double x)
{
    static double (*real)(double) = NULL;
    DLSYM_ONCE(real, "gamma");
    if (real) return real(x);
    return lgamma(x);
}

double __yos_tvos_unblock_significand(double x)
{
    static double (*real)(double) = NULL;
    DLSYM_ONCE(real, "significand");
    if (real) return real(x);
    int e;
    double m = frexp(x, &e);
    return m * 2.0;
}

int __yos_tvos_unblock_system(const char *cmd)
{
    static int (*real)(const char *) = NULL;
    DLSYM_ONCE(real, "system");
    if (!real) { errno = ENOSYS; return -1; }
    return real(cmd);
}

int __yos_tvos_unblock_daemon(int nochdir, int noclose)
{
    static int (*real)(int, int) = NULL;
    DLSYM_ONCE(real, "daemon");
    if (!real) { errno = ENOSYS; return -1; }
    return real(nochdir, noclose);
}

int __yos_tvos_unblock_execvP(const char *file, const char *path,
                              char *const argv[])
{
    static int (*real)(const char *, const char *, char *const []) = NULL;
    DLSYM_ONCE(real, "execvP");
    if (!real) { errno = ENOSYS; return -1; }
    return real(file, path, argv);
}

/* longjmperror: FreeBSD libc-only diagnostic helper for longjmp(3),
 * called when the jmp_buf is corrupted. Apple's libSystem doesn't
 * export it from the tvOS slice. Provide a no-op fallback. */
void __yos_tvos_unblock_longjmperror(void)
{
    static void (*real)(void) = NULL;
    DLSYM_ONCE(real, "longjmperror");
    if (real) { real(); return; }
    /* No-op: a corrupted jmp_buf is fatal anyway. */
}
