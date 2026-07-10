/* pthread.h — POSIX threads on Win32, MSVC-native, no MinGW.
 *
 * Implementation strategy:
 *
 *   pthread_t          → heap-allocated yos_pthread_data * (FLS-tracked)
 *   pthread_mutex_t    → SRWLOCK (exclusive)         + zero init
 *   pthread_cond_t     → CONDITION_VARIABLE          + zero init
 *   pthread_rwlock_t   → SRWLOCK + volatile owner tid for unlock-mode dispatch
 *   pthread_spinlock_t → volatile long busy-spin via _InterlockedExchange
 *   pthread_barrier_t  → SYNCHRONIZATION_BARRIER (Win8+)
 *   pthread_key_t      → FLS index (FlsAlloc; destructors run on thread exit)
 *   pthread_once_t     → INIT_ONCE                    + INIT_ONCE_STATIC_INIT
 *
 * What's faked vs missing:
 *   pthread_kill, pthread_cancel        — no Win32 analogue. ENOSYS.
 *   pthread_sigmask                     — Win32 signals are async + per-process.
 *                                          Accept the call, no-op.
 *   PTHREAD_MUTEX_RECURSIVE             — SRWLOCK is non-recursive. yos's
 *                                          host pthread consumers never lock
 *                                          recursively (checked via grep —
 *                                          only the env.pthread_mutexattr_*
 *                                          bridge stub names it).
 *
 * All inline-able primitives are static inline here. The thread/key/once
 * machinery that needs process-wide state lives in pthread_win32.c.
 */
#ifndef YOS_WIN_COMPAT_PTHREAD_H
#define YOS_WIN_COMPAT_PTHREAD_H

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#include <windows.h>
#include <synchapi.h>
#include <intrin.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── thread handle ─────────────────────────────────────────────────── */

struct yos_pthread_data {
    HANDLE       handle;
    unsigned     tid;       /* GetCurrentThreadId() */
    void *     (*start)(void *);
    void        *arg;
    void        *ret;
    /* Detached flag — closed handle on exit instead of waiting for join. */
    long         detached;
};

typedef struct yos_pthread_data * pthread_t;

/* ── attributes ────────────────────────────────────────────────────── */

typedef struct {
    size_t stacksize;
    int    detachstate;
} pthread_attr_t;

#define PTHREAD_CREATE_JOINABLE  0
#define PTHREAD_CREATE_DETACHED  1

extern int pthread_attr_init(pthread_attr_t *a);
extern int pthread_attr_destroy(pthread_attr_t *a);
extern int pthread_attr_setstacksize(pthread_attr_t *a, size_t stk);
extern int pthread_attr_getstacksize(const pthread_attr_t *a, size_t *out);
extern int pthread_attr_setdetachstate(pthread_attr_t *a, int state);
extern int pthread_attr_getdetachstate(const pthread_attr_t *a, int *out);
extern int pthread_attr_setguardsize(pthread_attr_t *a, size_t g);

/* ── core thread ops ───────────────────────────────────────────────── */

extern int       pthread_create(pthread_t *t, const pthread_attr_t *attr,
                                void *(*start)(void *), void *arg);
extern int       pthread_join(pthread_t t, void **retval);
extern int       pthread_detach(pthread_t t);
extern pthread_t pthread_self(void);
extern void      pthread_exit(void *ret);
extern int       pthread_equal(pthread_t a, pthread_t b);
extern int       pthread_cancel(pthread_t t);
extern int       pthread_kill(pthread_t t, int sig);
extern int       pthread_sigmask(int how, const void *set, void *oldset);

/* ── mutex ─────────────────────────────────────────────────────────── */

typedef SRWLOCK pthread_mutex_t;
#define PTHREAD_MUTEX_INITIALIZER  SRWLOCK_INIT

typedef int pthread_mutexattr_t;
#define PTHREAD_MUTEX_NORMAL       0
#define PTHREAD_MUTEX_RECURSIVE    1
#define PTHREAD_MUTEX_ERRORCHECK   2
#define PTHREAD_MUTEX_DEFAULT      PTHREAD_MUTEX_NORMAL

static __forceinline int pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *a) {
    (void)a;
    InitializeSRWLock(m);
    return 0;
}
static __forceinline int pthread_mutex_destroy(pthread_mutex_t *m) {
    (void)m; return 0;
}
static __forceinline int pthread_mutex_lock(pthread_mutex_t *m) {
    AcquireSRWLockExclusive(m); return 0;
}
static __forceinline int pthread_mutex_unlock(pthread_mutex_t *m) {
    ReleaseSRWLockExclusive(m); return 0;
}
static __forceinline int pthread_mutex_trylock(pthread_mutex_t *m) {
    return TryAcquireSRWLockExclusive(m) ? 0 : EBUSY;
}

static __forceinline int pthread_mutexattr_init(pthread_mutexattr_t *a) {
    if (a) *a = PTHREAD_MUTEX_DEFAULT;
    return 0;
}
static __forceinline int pthread_mutexattr_destroy(pthread_mutexattr_t *a) {
    (void)a; return 0;
}
static __forceinline int pthread_mutexattr_settype(pthread_mutexattr_t *a, int type) {
    if (a) *a = type;
    return 0;
}

/* ── condition variable ────────────────────────────────────────────── */

typedef CONDITION_VARIABLE pthread_cond_t;
#define PTHREAD_COND_INITIALIZER  CONDITION_VARIABLE_INIT

typedef int pthread_condattr_t;

static __forceinline int pthread_cond_init(pthread_cond_t *c, const pthread_condattr_t *a) {
    (void)a;
    InitializeConditionVariable(c);
    return 0;
}
static __forceinline int pthread_cond_destroy(pthread_cond_t *c) {
    (void)c; return 0;
}
static __forceinline int pthread_cond_signal(pthread_cond_t *c) {
    WakeConditionVariable(c); return 0;
}
static __forceinline int pthread_cond_broadcast(pthread_cond_t *c) {
    WakeAllConditionVariable(c); return 0;
}
static __forceinline int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m) {
    return SleepConditionVariableSRW(c, m, INFINITE, 0) ? 0 : EINVAL;
}

/* pthread_cond_timedwait: POSIX gives the absolute deadline; Win32 wants a
 * relative ms timeout. clock_gettime(CLOCK_REALTIME) gives us the current
 * wall time, subtract to get the delta. */
extern int pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *m,
                                  const struct timespec *abstime);

static __forceinline int pthread_condattr_init(pthread_condattr_t *a) {
    if (a) *a = 0; return 0;
}
static __forceinline int pthread_condattr_destroy(pthread_condattr_t *a) {
    (void)a; return 0;
}
static __forceinline int pthread_condattr_setclock(pthread_condattr_t *a, int clk) {
    (void)a; (void)clk; return 0;
}

/* ── rwlock ────────────────────────────────────────────────────────── */

typedef struct {
    SRWLOCK lock;
    /* GetCurrentThreadId() of the exclusive holder, or 0 when shared/free.
     * Used by pthread_rwlock_unlock to pick the right SRWLOCK release. */
    volatile long exclusive_owner;
} pthread_rwlock_t;
#define PTHREAD_RWLOCK_INITIALIZER  {SRWLOCK_INIT, 0}

typedef int pthread_rwlockattr_t;

static __forceinline int pthread_rwlock_init(pthread_rwlock_t *r, const pthread_rwlockattr_t *a) {
    (void)a;
    InitializeSRWLock(&r->lock);
    r->exclusive_owner = 0;
    return 0;
}
static __forceinline int pthread_rwlock_destroy(pthread_rwlock_t *r) {
    (void)r; return 0;
}
static __forceinline int pthread_rwlock_rdlock(pthread_rwlock_t *r) {
    AcquireSRWLockShared(&r->lock); return 0;
}
static __forceinline int pthread_rwlock_tryrdlock(pthread_rwlock_t *r) {
    return TryAcquireSRWLockShared(&r->lock) ? 0 : EBUSY;
}
static __forceinline int pthread_rwlock_wrlock(pthread_rwlock_t *r) {
    AcquireSRWLockExclusive(&r->lock);
    _InterlockedExchange(&r->exclusive_owner, (long)GetCurrentThreadId());
    return 0;
}
static __forceinline int pthread_rwlock_trywrlock(pthread_rwlock_t *r) {
    if (!TryAcquireSRWLockExclusive(&r->lock)) return EBUSY;
    _InterlockedExchange(&r->exclusive_owner, (long)GetCurrentThreadId());
    return 0;
}
static __forceinline int pthread_rwlock_unlock(pthread_rwlock_t *r) {
    long me = (long)GetCurrentThreadId();
    long owner = _InterlockedCompareExchange(&r->exclusive_owner, 0, me);
    if (owner == me) {
        ReleaseSRWLockExclusive(&r->lock);
    } else {
        ReleaseSRWLockShared(&r->lock);
    }
    return 0;
}

/* ── spinlock ──────────────────────────────────────────────────────── */

typedef volatile long pthread_spinlock_t;
#define PTHREAD_PROCESS_PRIVATE  0
#define PTHREAD_PROCESS_SHARED   1

static __forceinline int pthread_spin_init(pthread_spinlock_t *s, int pshared) {
    (void)pshared;
    *s = 0;
    return 0;
}
static __forceinline int pthread_spin_destroy(pthread_spinlock_t *s) {
    (void)s; return 0;
}
static __forceinline int pthread_spin_lock(pthread_spinlock_t *s) {
    while (_InterlockedExchange(s, 1) != 0) {
        YieldProcessor();
    }
    return 0;
}
static __forceinline int pthread_spin_trylock(pthread_spinlock_t *s) {
    return _InterlockedExchange(s, 1) == 0 ? 0 : EBUSY;
}
static __forceinline int pthread_spin_unlock(pthread_spinlock_t *s) {
    _InterlockedExchange(s, 0);
    return 0;
}

/* ── barrier ───────────────────────────────────────────────────────── */

typedef SYNCHRONIZATION_BARRIER pthread_barrier_t;
typedef int                     pthread_barrierattr_t;

#define PTHREAD_BARRIER_SERIAL_THREAD  (-1)

static __forceinline int pthread_barrier_init(pthread_barrier_t *b,
                                              const pthread_barrierattr_t *a,
                                              unsigned count) {
    (void)a;
    return InitializeSynchronizationBarrier(b, (LONG)count, -1) ? 0 : EINVAL;
}
static __forceinline int pthread_barrier_destroy(pthread_barrier_t *b) {
    DeleteSynchronizationBarrier(b);
    return 0;
}
static __forceinline int pthread_barrier_wait(pthread_barrier_t *b) {
    return EnterSynchronizationBarrier(b, 0)
        ? PTHREAD_BARRIER_SERIAL_THREAD : 0;
}

/* ── TLS keys (with destructors) ───────────────────────────────────── */

typedef DWORD pthread_key_t;

/* FlsAlloc takes a void(WINAPI *)(void *) destructor that runs on every
 * thread exit AND on FlsFree — matches POSIX semantics. */
extern int  pthread_key_create(pthread_key_t *k, void (*dtor)(void *));
extern int  pthread_key_delete(pthread_key_t k);
extern int  pthread_setspecific(pthread_key_t k, const void *v);
extern void *pthread_getspecific(pthread_key_t k);

/* ── once ──────────────────────────────────────────────────────────── */

typedef INIT_ONCE pthread_once_t;
#define PTHREAD_ONCE_INIT  INIT_ONCE_STATIC_INIT

extern int pthread_once(pthread_once_t *o, void (*init)(void));

/* ── thread naming / scheduling — best-effort no-ops ───────────────── */

extern int pthread_setname_np(pthread_t t, const char *name);
extern int pthread_setschedparam(pthread_t t, int policy, const void *param);

#ifdef __cplusplus
}
#endif

/* ── clock_gettime / nanosleep / gettimeofday ──────────────────────── */
/* MSVC's <time.h> defines `struct timespec` and TIME_UTC under C11, but
 * not clock_gettime / nanosleep. They live in the same .c TU as the
 * pthread bodies so we declare them here. */

#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME             0
#define CLOCK_MONOTONIC            1
#define CLOCK_PROCESS_CPUTIME_ID   2
#define CLOCK_THREAD_CPUTIME_ID    3
#define CLOCK_MONOTONIC_RAW        4
#define CLOCK_REALTIME_COARSE      5
#define CLOCK_MONOTONIC_COARSE     6
#define CLOCK_BOOTTIME             7
typedef int clockid_t;
#endif

#ifdef __cplusplus
extern "C" {
#endif

extern int clock_gettime(clockid_t clk, struct timespec *ts);
extern int nanosleep(const struct timespec *req, struct timespec *rem);
extern int gettimeofday(struct timeval *tv, void *tz);

#ifdef __cplusplus
}
#endif

#endif /* YOS_WIN_COMPAT_PTHREAD_H */
