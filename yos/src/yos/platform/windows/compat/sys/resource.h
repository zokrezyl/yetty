/* Minimal <sys/resource.h> compat. */
#ifndef YOS_WIN_COMPAT_SYS_RESOURCE_H
#define YOS_WIN_COMPAT_SYS_RESOURCE_H

#include <sys/types.h>
#include <sys/time.h>
#include <stdint.h>

#ifndef RLIM_INFINITY
#define RLIM_INFINITY (~(uint64_t)0)
#endif

typedef uint64_t rlim_t;

struct rlimit {
    rlim_t rlim_cur;
    rlim_t rlim_max;
};

struct rusage {
    struct timeval ru_utime;
    struct timeval ru_stime;
    long ru_maxrss, ru_ixrss, ru_idrss, ru_isrss, ru_minflt, ru_majflt;
    long ru_nswap, ru_inblock, ru_oublock, ru_msgsnd, ru_msgrcv;
    long ru_nsignals, ru_nvcsw, ru_nivcsw;
};

#define RUSAGE_SELF      0
#define RUSAGE_CHILDREN -1
#define RUSAGE_THREAD    1

#define RLIMIT_CPU        0
#define RLIMIT_FSIZE      1
#define RLIMIT_DATA       2
#define RLIMIT_STACK      3
#define RLIMIT_CORE       4
#define RLIMIT_RSS        5
#define RLIMIT_NPROC      6
#define RLIMIT_NOFILE     7
#define RLIMIT_MEMLOCK    8
#define RLIMIT_AS         9

/* Real bodies in compat_libc.c so the codegen extract sees external
 * linkage (an inline-only body produces no exported symbol for the
 * bridge to call). */
extern int getrlimit(int r, struct rlimit *rl);
extern int setrlimit(int r, const struct rlimit *rl);
/* Real impl in compat_libc.c — fills ru_utime/ru_stime from
 * GetProcessTimes / GetThreadTimes so callers measuring CPU advance
 * see nonzero deltas. */
extern int getrusage(int who, struct rusage *ru);

#endif
