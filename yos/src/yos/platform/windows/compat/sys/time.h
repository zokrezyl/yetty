/* <sys/time.h> compat. MSVC's <time.h> doesn't define `struct timeval`
 * (winsock2.h does). Pull <winsock2.h> for the struct definition, then
 * declare the POSIX functions whose bodies live in pthread_win32.c
 * (gettimeofday) / compat_stubs.c. */
#ifndef YOS_WIN_COMPAT_SYS_TIME_H
#define YOS_WIN_COMPAT_SYS_TIME_H

#include <stddef.h>
#include <sys/types.h>
#include <time.h>           /* struct timespec, time_t */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>       /* struct timeval */

#ifdef __cplusplus
extern "C" {
#endif

extern int gettimeofday(struct timeval *tv, void *tz);

/* itimerval / setitimer — Windows has no per-process interval timer
 * with SIGALRM-style delivery. Provide the struct so callers compile;
 * setitimer returns ENOSYS in compat_stubs.c. */
struct itimerval {
    struct timeval it_interval;
    struct timeval it_value;
};
#define ITIMER_REAL    0
#define ITIMER_VIRTUAL 1
#define ITIMER_PROF    2

extern int setitimer(int which, const struct itimerval *new_val,
                     struct itimerval *old_val);
extern int getitimer(int which, struct itimerval *cur_val);

/* timeradd / timersub — POSIX macros that may not be present. */
#ifndef timeradd
#define timeradd(a, b, r) do {                                          \
    (r)->tv_sec  = (a)->tv_sec  + (b)->tv_sec;                          \
    (r)->tv_usec = (a)->tv_usec + (b)->tv_usec;                         \
    if ((r)->tv_usec >= 1000000) { (r)->tv_sec++; (r)->tv_usec -= 1000000; } \
} while (0)
#endif
#ifndef timersub
#define timersub(a, b, r) do {                                          \
    (r)->tv_sec  = (a)->tv_sec  - (b)->tv_sec;                          \
    (r)->tv_usec = (a)->tv_usec - (b)->tv_usec;                         \
    if ((r)->tv_usec < 0) { (r)->tv_sec--; (r)->tv_usec += 1000000; }   \
} while (0)
#endif
#ifndef timerclear
#define timerclear(t) ((t)->tv_sec = (t)->tv_usec = 0)
#endif
#ifndef timerisset
#define timerisset(t) ((t)->tv_sec || (t)->tv_usec)
#endif
#ifndef timercmp
#define timercmp(a, b, CMP) \
    (((a)->tv_sec == (b)->tv_sec) ? ((a)->tv_usec CMP (b)->tv_usec) \
                                  : ((a)->tv_sec  CMP (b)->tv_sec))
#endif

#ifdef __cplusplus
}
#endif

#endif /* YOS_WIN_COMPAT_SYS_TIME_H */
