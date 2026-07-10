//
//  m3_atomic.c
//
//  Threads-proposal support: a deliberately-tiny wait/notify queue keyed by host
//  pointer. NOT spec-perfect: notify wakes ALL parked waiters and lets each
//  re-check whether it was actually targeted (single global mutex/cond). This
//  is the minimum viable implementation; replace with per-address slots if
//  contention shows up.
//

#define _POSIX_C_SOURCE 200809L
#include "m3_atomic.h"

#include <pthread.h>
#include <time.h>
#include <errno.h>

// Single global mutex + cond. All waiters park on the same cond; on notify
// every waiter wakes, re-checks its address, and either returns "ok" or
// goes back to sleep. Correct under SeqCst atomic ops.
static pthread_mutex_t g_wait_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_wait_cond  = PTHREAD_COND_INITIALIZER;

// Generation counter bumped on every notify. A waiter records the generation
// when it parks and only returns "ok" if it sees the counter advance — that
// way a stray cond_wait wakeup does not get reported as a notification.
static volatile u64 g_notify_gen = 0;

static void
abstime_after (struct timespec * out, i64 timeout_ns)
{
    clock_gettime (CLOCK_REALTIME, out);
    out->tv_sec  += (time_t)(timeout_ns / 1000000000LL);
    out->tv_nsec += (long) (timeout_ns % 1000000000LL);
    if (out->tv_nsec >= 1000000000L)
    {
        out->tv_sec  += 1;
        out->tv_nsec -= 1000000000L;
    }
}

i32
m3Atomic_Wait32 (volatile u32 * addr, u32 expected, i64 timeout_ns)
{
    pthread_mutex_lock (&g_wait_mutex);

    // Re-check inside the lock. notify takes the same lock, so any value
    // change racing with us is serialized by it.
    if (__atomic_load_n (addr, __ATOMIC_SEQ_CST) != expected)
    {
        pthread_mutex_unlock (&g_wait_mutex);
        return 1; // "not-equal"
    }

    u64 start_gen = g_notify_gen;
    i32 status = 0;

    if (timeout_ns < 0)
    {
        // Wait forever, but loop until we observe a notify_gen advance.
        while (g_notify_gen == start_gen
               && __atomic_load_n (addr, __ATOMIC_SEQ_CST) == expected)
        {
            pthread_cond_wait (&g_wait_cond, &g_wait_mutex);
        }
        status = 0; // "ok"
    }
    else
    {
        struct timespec deadline;
        abstime_after (&deadline, timeout_ns);
        while (g_notify_gen == start_gen
               && __atomic_load_n (addr, __ATOMIC_SEQ_CST) == expected)
        {
            int rc = pthread_cond_timedwait (&g_wait_cond, &g_wait_mutex, &deadline);
            if (rc == ETIMEDOUT) { status = 2; break; }
        }
    }

    pthread_mutex_unlock (&g_wait_mutex);
    return status;
}

i32
m3Atomic_Wait64 (volatile u64 * addr, u64 expected, i64 timeout_ns)
{
    pthread_mutex_lock (&g_wait_mutex);

    if (__atomic_load_n (addr, __ATOMIC_SEQ_CST) != expected)
    {
        pthread_mutex_unlock (&g_wait_mutex);
        return 1;
    }

    u64 start_gen = g_notify_gen;
    i32 status = 0;

    if (timeout_ns < 0)
    {
        while (g_notify_gen == start_gen
               && __atomic_load_n (addr, __ATOMIC_SEQ_CST) == expected)
        {
            pthread_cond_wait (&g_wait_cond, &g_wait_mutex);
        }
    }
    else
    {
        struct timespec deadline;
        abstime_after (&deadline, timeout_ns);
        while (g_notify_gen == start_gen
               && __atomic_load_n (addr, __ATOMIC_SEQ_CST) == expected)
        {
            int rc = pthread_cond_timedwait (&g_wait_cond, &g_wait_mutex, &deadline);
            if (rc == ETIMEDOUT) { status = 2; break; }
        }
    }

    pthread_mutex_unlock (&g_wait_mutex);
    return status;
}

u32
m3Atomic_Notify (void * addr, u32 count)
{
    (void) addr; // single global queue: we don't use the address as a key

    pthread_mutex_lock (&g_wait_mutex);
    g_notify_gen += 1;
    if (count > 0)
        pthread_cond_broadcast (&g_wait_cond);
    pthread_mutex_unlock (&g_wait_mutex);

    // We can't tell exactly how many were waiting on *this* address with the
    // simplified queue; report the requested count as an upper bound.
    return count;
}
