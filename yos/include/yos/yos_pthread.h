// Minimal POSIX-style pthread surface for code compiled with clang
// --target=wasm32 against the wasm3mt host runtime.
//
// All sync state lives in shared linear memory unless noted otherwise.
//   yos_pthread_t / yos_pthread_mutex_t / yos_pthread_cond_t / yos_pthread_key_t : u32 cells
//   yos_pthread_rwlock_t / yos_pthread_barrier_t / yos_pthread_once_t        : opaque
//                                                                  multi-u32
//                                                                  structs
//
// Some POSIX bits are simplified for POC reasons:
//   * yos_pthread_cond_timedwait takes an i64 *absolute* deadline in nanoseconds
//     (CLOCK_MONOTONIC). Use `wasm3mt_now_ns()` to read the clock.
//   * yos_pthread_mutex_t default behavior is "fast/non-recursive"; attr type is
//     accepted but not honoured.
//   * yos_pthread_key destructors are accepted but never invoked.

#ifndef WASM3MT_PTHREAD_H
#define WASM3MT_PTHREAD_H

#include <stdint.h>

/* L1 guest types — u32 cells / opaque structs in shared linear memory.
 * Defined unconditionally so that the host code that implements the L1
 * imports (runtime/yos_pthread_host.c) can use them in handler signatures
 * without including a duplicate definition. The guest sees exactly the same
 * layouts. */

/* Layouts mirror musl's pthread types for a 32-bit (sizeof(long)==4) target,
 * i.e. the i386/wasm32 musl layout. The first 32-bit word of each struct is
 * used by yos-pthread.c as the futex/state cell; remaining words are padding
 * (they exist so user code's `pthread_*_t` storage matches musl ABI). */

typedef uint32_t yos_pthread_t;          // handle (slot/tid in host table)
typedef int      yos_pthread_once_t;     // 4 B — single-cell state machine
typedef unsigned yos_pthread_key_t;
typedef int      yos_pthread_spinlock_t;

typedef struct { unsigned __attr;     } yos_pthread_mutexattr_t;
typedef struct { unsigned __attr;     } yos_pthread_condattr_t;
typedef struct { unsigned __attr;     } yos_pthread_barrierattr_t;
typedef struct { unsigned __attr[2];  } yos_pthread_rwlockattr_t;

typedef struct { union { int __i[9];  volatile int __vi[9];  unsigned long __s[9]; } __u; } yos_pthread_attr_t;     // 36 B
typedef struct { union { int __i[6];  volatile int __vi[6];  volatile void *volatile __p[6]; } __u; } yos_pthread_mutex_t;   // 24 B
typedef struct { union { int __i[12]; volatile int __vi[12]; void *__p[12]; } __u; } yos_pthread_cond_t;            // 48 B
typedef struct { union { int __i[8];  volatile int __vi[8];  void *__p[8];  } __u; } yos_pthread_rwlock_t;          // 32 B
typedef struct { union { int __i[5];  volatile int __vi[5];  void *__p[5];  } __u; } yos_pthread_barrier_t;         // 20 B

#ifdef __wasm__
/* Guest-only: static initializers, errno values, L1 import declarations. */

/* musl-style {0}: zero-initialise the whole struct (first int is the L1 cell,
 * 0 ⇒ "free / not-yet-init", which matches the runtime's expectations). */
#define PTHREAD_MUTEX_INITIALIZER  { { { 0 } } }
#define PTHREAD_COND_INITIALIZER   { { { 0 } } }
#define PTHREAD_ONCE_INIT          0

#define PTHREAD_BARRIER_SERIAL_THREAD  (-1)

// errno values we propagate through return codes
#define EBUSY     16
#define EINVAL    22
#define ETIMEDOUT 110

#define WASM3MT_IMPORT(name)                                                \
    __attribute__((import_module("env"), import_name(#name)))


// --- threads --------------------------------------------------------------

WASM3MT_IMPORT(yos_pthread_create)
int   yos_pthread_create (yos_pthread_t * t,
                      const yos_pthread_attr_t * attr,
                      void * (*start)(void *),
                      void * arg);

WASM3MT_IMPORT(yos_pthread_join)
int   yos_pthread_join   (yos_pthread_t t, void ** retval);

WASM3MT_IMPORT(yos_pthread_self)
yos_pthread_t yos_pthread_self (void);


// --- mutex ----------------------------------------------------------------

WASM3MT_IMPORT(yos_pthread_mutex_init)
int   yos_pthread_mutex_init    (yos_pthread_mutex_t * m, const yos_pthread_mutexattr_t * attr);

WASM3MT_IMPORT(yos_pthread_mutex_destroy)
int   yos_pthread_mutex_destroy (yos_pthread_mutex_t * m);

WASM3MT_IMPORT(yos_pthread_mutex_lock)
int   yos_pthread_mutex_lock    (yos_pthread_mutex_t * m);

WASM3MT_IMPORT(yos_pthread_mutex_trylock)
int   yos_pthread_mutex_trylock (yos_pthread_mutex_t * m);   // 0 on lock, EBUSY otherwise

WASM3MT_IMPORT(yos_pthread_mutex_unlock)
int   yos_pthread_mutex_unlock  (yos_pthread_mutex_t * m);

WASM3MT_IMPORT(yos_pthread_mutexattr_init)
int   yos_pthread_mutexattr_init    (yos_pthread_mutexattr_t * a);

WASM3MT_IMPORT(yos_pthread_mutexattr_destroy)
int   yos_pthread_mutexattr_destroy (yos_pthread_mutexattr_t * a);

WASM3MT_IMPORT(yos_pthread_mutexattr_settype)
int   yos_pthread_mutexattr_settype (yos_pthread_mutexattr_t * a, int kind);


// --- cond -----------------------------------------------------------------

WASM3MT_IMPORT(yos_pthread_cond_init)
int   yos_pthread_cond_init      (yos_pthread_cond_t * c, const yos_pthread_condattr_t * attr);

WASM3MT_IMPORT(yos_pthread_cond_destroy)
int   yos_pthread_cond_destroy   (yos_pthread_cond_t * c);

WASM3MT_IMPORT(yos_pthread_cond_wait)
int   yos_pthread_cond_wait      (yos_pthread_cond_t * c, yos_pthread_mutex_t * m);

// abstime_ns: absolute deadline against `wasm3mt_now_ns()` (CLOCK_MONOTONIC).
WASM3MT_IMPORT(yos_pthread_cond_timedwait)
int   yos_pthread_cond_timedwait (yos_pthread_cond_t * c,
                              yos_pthread_mutex_t * m,
                              int64_t abstime_ns);

WASM3MT_IMPORT(yos_pthread_cond_signal)
int   yos_pthread_cond_signal    (yos_pthread_cond_t * c);

WASM3MT_IMPORT(yos_pthread_cond_broadcast)
int   yos_pthread_cond_broadcast (yos_pthread_cond_t * c);


// --- TLS (POSIX yos_pthread_key_t) -------------------------------------------

WASM3MT_IMPORT(yos_pthread_key_create)
int    yos_pthread_key_create  (yos_pthread_key_t * key, void (*destructor)(void *));

WASM3MT_IMPORT(yos_pthread_key_delete)
int    yos_pthread_key_delete  (yos_pthread_key_t key);

WASM3MT_IMPORT(yos_pthread_setspecific)
int    yos_pthread_setspecific (yos_pthread_key_t key, const void * value);

WASM3MT_IMPORT(yos_pthread_getspecific)
void * yos_pthread_getspecific (yos_pthread_key_t key);


// --- rwlock ---------------------------------------------------------------
// No static initialiser — must call yos_pthread_rwlock_init.

WASM3MT_IMPORT(yos_pthread_rwlock_init)
int   yos_pthread_rwlock_init      (yos_pthread_rwlock_t * rw, const yos_pthread_rwlockattr_t * attr);

WASM3MT_IMPORT(yos_pthread_rwlock_destroy)
int   yos_pthread_rwlock_destroy   (yos_pthread_rwlock_t * rw);

WASM3MT_IMPORT(yos_pthread_rwlock_rdlock)
int   yos_pthread_rwlock_rdlock    (yos_pthread_rwlock_t * rw);

WASM3MT_IMPORT(yos_pthread_rwlock_wrlock)
int   yos_pthread_rwlock_wrlock    (yos_pthread_rwlock_t * rw);

WASM3MT_IMPORT(yos_pthread_rwlock_tryrdlock)
int   yos_pthread_rwlock_tryrdlock (yos_pthread_rwlock_t * rw);

WASM3MT_IMPORT(yos_pthread_rwlock_trywrlock)
int   yos_pthread_rwlock_trywrlock (yos_pthread_rwlock_t * rw);

WASM3MT_IMPORT(yos_pthread_rwlock_unlock)
int   yos_pthread_rwlock_unlock    (yos_pthread_rwlock_t * rw);


// --- barrier --------------------------------------------------------------

WASM3MT_IMPORT(yos_pthread_barrier_init)
int   yos_pthread_barrier_init    (yos_pthread_barrier_t * b,
                               const yos_pthread_barrierattr_t * attr,
                               unsigned count);

WASM3MT_IMPORT(yos_pthread_barrier_destroy)
int   yos_pthread_barrier_destroy (yos_pthread_barrier_t * b);

WASM3MT_IMPORT(yos_pthread_barrier_wait)
int   yos_pthread_barrier_wait    (yos_pthread_barrier_t * b);


// --- once -----------------------------------------------------------------

WASM3MT_IMPORT(yos_pthread_once)
int   yos_pthread_once (yos_pthread_once_t * once, void (*init_routine)(void));


// --- helpers (non-POSIX, useful for tests) -------------------------------

WASM3MT_IMPORT(wasm3mt_now_ns)
int64_t wasm3mt_now_ns (void);     // CLOCK_MONOTONIC nanoseconds

WASM3MT_IMPORT(wasm3mt_yield)
void    wasm3mt_yield  (void);     // sched_yield()

#endif /* __wasm__ */

#endif /* WASM3MT_PTHREAD_H */
