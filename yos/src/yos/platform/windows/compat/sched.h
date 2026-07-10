/* <sched.h> compat — POSIX scheduling. Windows has its own
 * thread-priority scheme; expose just enough struct/constant shape to
 * compile, with no real behaviour wired up. */
#ifndef YOS_WIN_COMPAT_SCHED_H
#define YOS_WIN_COMPAT_SCHED_H

#include <stddef.h>
#include <sys/types.h>

#ifndef SCHED_OTHER
#define SCHED_OTHER     0
#define SCHED_FIFO      1
#define SCHED_RR        2
#define SCHED_BATCH     3
#define SCHED_IDLE      5
#endif

struct sched_param {
    int sched_priority;
};

static __inline int sched_yield(void) {
#ifdef _WIN32
    extern void __stdcall Sleep(unsigned long);
    Sleep(0);
#endif
    return 0;
}

static __inline int sched_getparam(int pid, struct sched_param *p) {
    (void)pid;
    if (p) p->sched_priority = 0;
    return 0;
}
static __inline int sched_setparam(int pid, const struct sched_param *p) {
    (void)pid; (void)p; return 0;
}
static __inline int sched_get_priority_min(int policy) { (void)policy; return 0; }
static __inline int sched_get_priority_max(int policy) { (void)policy; return 31; }
static __inline int sched_getscheduler(int pid)        { (void)pid; return SCHED_OTHER; }
static __inline int sched_setscheduler(int pid, int policy, const struct sched_param *p) {
    (void)pid; (void)policy; (void)p; return 0;
}

#ifndef YOS_WIN_HAS_CPU_SET_T
#define CPU_SETSIZE 1024
typedef struct {
    unsigned long bits[CPU_SETSIZE / (8 * sizeof(unsigned long))];
} cpu_set_t;

#define CPU_ZERO(set)        do { for (size_t _i = 0; _i < sizeof((set)->bits)/sizeof((set)->bits[0]); _i++) (set)->bits[_i] = 0; } while (0)
#define CPU_SET(cpu, set)    do { (set)->bits[(cpu) / (8 * sizeof(unsigned long))] |= 1ul << ((cpu) % (8 * sizeof(unsigned long))); } while (0)
#define CPU_CLR(cpu, set)    do { (set)->bits[(cpu) / (8 * sizeof(unsigned long))] &= ~(1ul << ((cpu) % (8 * sizeof(unsigned long)))); } while (0)
#define CPU_ISSET(cpu, set)  (((set)->bits[(cpu) / (8 * sizeof(unsigned long))] >> ((cpu) % (8 * sizeof(unsigned long)))) & 1ul)
#define CPU_COUNT(set)       0
#define YOS_WIN_HAS_CPU_SET_T 1
#endif

static __inline int sched_setaffinity(int pid, size_t sz, const cpu_set_t *m) {
    (void)pid; (void)sz; (void)m; return 0;
}
static __inline int sched_getaffinity(int pid, size_t sz, cpu_set_t *m) {
    (void)pid; (void)sz;
    if (m) CPU_ZERO(m);
    return 0;
}

#endif /* YOS_WIN_COMPAT_SCHED_H */
