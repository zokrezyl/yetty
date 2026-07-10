/* pthread_win32.c — Win32 implementations of the pthread surface
 * declared in compat/pthread.h. Built into yos_exe; symbols satisfy the
 * wasm3 + yos host TUs that #include <pthread.h> through the compat
 * include path. */

#include <process.h>   /* _beginthreadex */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>  /* struct timeval */
#include <windows.h>

#include "pthread.h"

/* ── self-tracking via FLS ─────────────────────────────────────────── */

static DWORD       g_self_fls         = FLS_OUT_OF_INDEXES;
static INIT_ONCE   g_self_fls_once    = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK init_self_fls(PINIT_ONCE once, PVOID param, PVOID *ctx)
{
    (void)once; (void)param; (void)ctx;
    /* No FLS destructor for the self-slot: yos_pthread_data is freed by
     * pthread_join (or on the start-thunk's exit path when detached). */
    g_self_fls = FlsAlloc(NULL);
    return g_self_fls != FLS_OUT_OF_INDEXES;
}

static void ensure_self_fls(void)
{
    InitOnceExecuteOnce(&g_self_fls_once, init_self_fls, NULL, NULL);
}

/* Lazy-attach pthread_self for threads we did NOT create (typically the
 * process main thread). The struct is heap-owned and freed on process
 * exit — no destructor on the FLS slot because we'd otherwise free the
 * main thread's data structure while it's still in use during DLL
 * unload. */
static struct yos_pthread_data *attach_self(void)
{
    ensure_self_fls();
    struct yos_pthread_data *p = (struct yos_pthread_data *)FlsGetValue(g_self_fls);
    if (p) return p;

    p = (struct yos_pthread_data *)calloc(1, sizeof *p);
    if (!p) return NULL;
    /* DUPLICATE_SAME_ACCESS — handle owned by p; we close it never (the
     * main thread's pseudo-handle GetCurrentThread() doesn't own anything
     * but the real handle is duplicated so HANDLE-using code stays valid). */
    HANDLE h = NULL;
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(),
                    GetCurrentProcess(), &h,
                    0, FALSE, DUPLICATE_SAME_ACCESS);
    p->handle = h;
    p->tid    = GetCurrentThreadId();
    FlsSetValue(g_self_fls, p);
    return p;
}

/* ── attributes ────────────────────────────────────────────────────── */

int pthread_attr_init(pthread_attr_t *a)
{
    if (!a) return EINVAL;
    a->stacksize   = 0;            /* 0 = let CreateThread/_beginthreadex choose */
    a->detachstate = PTHREAD_CREATE_JOINABLE;
    return 0;
}
int pthread_attr_destroy(pthread_attr_t *a) { (void)a; return 0; }

int pthread_attr_setstacksize(pthread_attr_t *a, size_t stk) {
    if (!a) return EINVAL;
    a->stacksize = stk;
    return 0;
}
int pthread_attr_getstacksize(const pthread_attr_t *a, size_t *out) {
    if (!a || !out) return EINVAL;
    *out = a->stacksize;
    return 0;
}
int pthread_attr_setdetachstate(pthread_attr_t *a, int state) {
    if (!a) return EINVAL;
    a->detachstate = state;
    return 0;
}
int pthread_attr_getdetachstate(const pthread_attr_t *a, int *out) {
    if (!a || !out) return EINVAL;
    *out = a->detachstate;
    return 0;
}
int pthread_attr_setguardsize(pthread_attr_t *a, size_t g) {
    (void)a; (void)g;
    return 0;
}

/* ── thread create / join / detach / self / exit / equal ───────────── */

static unsigned __stdcall yos_pthread_thunk(void *raw)
{
    struct yos_pthread_data *p = (struct yos_pthread_data *)raw;
    ensure_self_fls();
    FlsSetValue(g_self_fls, p);
    p->ret = p->start(p->arg);
    /* On detached threads, free the struct here — no joiner will. */
    if (_InterlockedCompareExchange(&p->detached, 1, 1) == 1) {
        if (p->handle) CloseHandle(p->handle);
        free(p);
    }
    return 0;
}

int pthread_create(pthread_t *t, const pthread_attr_t *attr,
                   void *(*start)(void *), void *arg)
{
    if (!t || !start) return EINVAL;

    struct yos_pthread_data *p = (struct yos_pthread_data *)calloc(1, sizeof *p);
    if (!p) return ENOMEM;
    p->start    = start;
    p->arg      = arg;
    p->detached = (attr && attr->detachstate == PTHREAD_CREATE_DETACHED) ? 1 : 0;

    unsigned stacksize = 0;
    if (attr && attr->stacksize) {
        if (attr->stacksize > 0xFFFFFFFFu) stacksize = 0xFFFFFFFFu;
        else                               stacksize = (unsigned)attr->stacksize;
    }

    unsigned tid = 0;
    uintptr_t h = _beginthreadex(NULL, stacksize, yos_pthread_thunk, p,
                                 CREATE_SUSPENDED, &tid);
    if (h == 0) {
        free(p);
        return EAGAIN;
    }
    p->handle = (HANDLE)h;
    p->tid    = tid;
    if (ResumeThread((HANDLE)h) == (DWORD)-1) {
        CloseHandle((HANDLE)h);
        free(p);
        return EAGAIN;
    }
    *t = p;
    return 0;
}

int pthread_join(pthread_t t, void **retval)
{
    if (!t) return EINVAL;
    if (t->detached) return EINVAL;
    if (t->handle) {
        WaitForSingleObject(t->handle, INFINITE);
        CloseHandle(t->handle);
        t->handle = NULL;
    }
    if (retval) *retval = t->ret;
    free(t);
    return 0;
}

int pthread_detach(pthread_t t)
{
    if (!t) return EINVAL;
    /* If the thread has already exited (handle still open but thread gone)
     * the thunk's detached check would race with us; the InterlockedExchange
     * ensures only one side frees. */
    long was = _InterlockedExchange(&t->detached, 1);
    if (was == 0) {
        DWORD code = STILL_ACTIVE;
        if (t->handle && GetExitCodeThread(t->handle, &code) && code != STILL_ACTIVE) {
            /* Already done — free here. */
            CloseHandle(t->handle);
            free(t);
        }
    }
    return 0;
}

pthread_t pthread_self(void)
{
    ensure_self_fls();
    struct yos_pthread_data *p = (struct yos_pthread_data *)FlsGetValue(g_self_fls);
    if (!p) p = attach_self();
    return p;
}

void pthread_exit(void *ret)
{
    ensure_self_fls();
    struct yos_pthread_data *p = (struct yos_pthread_data *)FlsGetValue(g_self_fls);
    if (p) {
        p->ret = ret;
        if (_InterlockedCompareExchange(&p->detached, 1, 1) == 1) {
            if (p->handle) CloseHandle(p->handle);
            free(p);
        }
    }
    _endthreadex(0);
}

int pthread_equal(pthread_t a, pthread_t b)
{
    return a == b;
}

int pthread_cancel(pthread_t t)
{
    /* POSIX cancellation requires cooperation at cancellation points;
     * Win32 has no analogue. Returning ENOSYS lets the caller fall back
     * to its own shutdown path. */
    (void)t;
    errno = ENOSYS;
    return ENOSYS;
}

int pthread_kill(pthread_t t, int sig)
{
    /* No per-thread async signals on Win32. yos's signal subsystem
     * sets the target ctx's `sig_pending` bit BEFORE calling
     * pthread_kill; the bit, not the host syscall, is the
     * authoritative delivery. Return 0 here so deliver_to_proc
     * propagates a "success" code to its caller — the next
     * yos_signal_pump on the target thread fires the handler. */
    (void)t; (void)sig;
    return 0;
}

int pthread_sigmask(int how, const void *set, void *oldset)
{
    /* Win32 has no per-thread signal mask. Silently accept. */
    (void)how; (void)set;
    if (oldset) memset(oldset, 0, sizeof(unsigned long));
    return 0;
}

int pthread_setname_np(pthread_t t, const char *name)
{
    /* SetThreadDescription is Win10 1607+; soft-link via GetProcAddress
     * so we still work on older targets. */
    typedef HRESULT (WINAPI *SetThreadDescription_t)(HANDLE, PCWSTR);
    static SetThreadDescription_t fn = NULL;
    static LONG probed = 0;
    if (_InterlockedCompareExchange(&probed, 1, 0) == 0) {
        HMODULE k = GetModuleHandleW(L"kernel32.dll");
        if (k) fn = (SetThreadDescription_t)(uintptr_t)GetProcAddress(k, "SetThreadDescription");
    }
    if (!fn || !t || !t->handle || !name) return 0;
    /* UTF-8 → UTF-16 (best-effort; we accept the rare mojibake). */
    wchar_t wbuf[64];
    int n = MultiByteToWideChar(CP_UTF8, 0, name, -1, wbuf,
                                (int)(sizeof wbuf / sizeof wbuf[0]));
    if (n <= 0) return 0;
    (void)fn(t->handle, wbuf);
    return 0;
}

int pthread_setschedparam(pthread_t t, int policy, const void *param)
{
    (void)t; (void)policy; (void)param;
    return 0;
}

/* ── cond_timedwait — absolute deadline → relative ms ──────────────── */

int pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *m,
                           const struct timespec *abstime)
{
    if (!abstime) return EINVAL;
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    int64_t ns = (int64_t)(abstime->tv_sec - now.tv_sec) * 1000000000LL
               + (int64_t)(abstime->tv_nsec - now.tv_nsec);
    DWORD ms;
    if (ns <= 0)                  ms = 0;
    else if (ns / 1000000LL > 0x7FFFFFFFLL) ms = INFINITE - 1;
    else                          ms = (DWORD)(ns / 1000000LL);

    if (SleepConditionVariableSRW(c, m, ms, 0))
        return 0;
    DWORD e = GetLastError();
    if (e == ERROR_TIMEOUT) return ETIMEDOUT;
    return EINVAL;
}

/* ── TLS keys (FLS-backed, supports destructors) ───────────────────── */

int pthread_key_create(pthread_key_t *k, void (*dtor)(void *))
{
    if (!k) return EINVAL;
    DWORD ix = FlsAlloc((PFLS_CALLBACK_FUNCTION)dtor);
    if (ix == FLS_OUT_OF_INDEXES) return EAGAIN;
    *k = ix;
    return 0;
}
int pthread_key_delete(pthread_key_t k)
{
    return FlsFree(k) ? 0 : EINVAL;
}
int pthread_setspecific(pthread_key_t k, const void *v)
{
    return FlsSetValue(k, (PVOID)v) ? 0 : EINVAL;
}
void *pthread_getspecific(pthread_key_t k)
{
    return FlsGetValue(k);
}

/* ── once ──────────────────────────────────────────────────────────── */

static BOOL CALLBACK once_thunk(PINIT_ONCE once, PVOID param, PVOID *ctx)
{
    (void)once; (void)ctx;
    void (*fn)(void) = (void (*)(void))param;
    if (fn) fn();
    return TRUE;
}

int pthread_once(pthread_once_t *o, void (*init)(void))
{
    if (!o || !init) return EINVAL;
    InitOnceExecuteOnce(o, once_thunk, (PVOID)(uintptr_t)init, NULL);
    return 0;
}

/* ── clock_gettime / nanosleep / gettimeofday ──────────────────────── */

/* QueryPerformanceFrequency value cached once; on Win7+ it's constant. */
static LARGE_INTEGER g_qpc_freq;
static INIT_ONCE     g_qpc_once = INIT_ONCE_STATIC_INIT;
static BOOL CALLBACK init_qpc(PINIT_ONCE once, PVOID param, PVOID *ctx)
{
    (void)once; (void)param; (void)ctx;
    QueryPerformanceFrequency(&g_qpc_freq);
    return TRUE;
}

/* FILETIME 100ns-since-1601 → struct timespec relative to epoch 1970. */
static void filetime_to_timespec(const FILETIME *ft, struct timespec *ts)
{
    /* 11644473600 seconds between 1601-01-01 and 1970-01-01. */
    uint64_t hundreds = ((uint64_t)ft->dwHighDateTime << 32) | ft->dwLowDateTime;
    hundreds -= 116444736000000000ULL;
    ts->tv_sec  = (time_t)(hundreds / 10000000ULL);
    ts->tv_nsec = (long)  ((hundreds % 10000000ULL) * 100);
}

int clock_gettime(clockid_t clk, struct timespec *ts)
{
    if (!ts) { errno = EFAULT; return -1; }
    switch (clk) {
    case CLOCK_REALTIME:
    case CLOCK_REALTIME_COARSE: {
        FILETIME ft;
        /* GetSystemTimePreciseAsFileTime is Win8+ — fall back if missing. */
        typedef VOID (WINAPI *GSPF_t)(LPFILETIME);
        static GSPF_t fn = NULL;
        static LONG probed = 0;
        if (_InterlockedCompareExchange(&probed, 1, 0) == 0) {
            HMODULE k = GetModuleHandleW(L"kernel32.dll");
            if (k) fn = (GSPF_t)(uintptr_t)GetProcAddress(k, "GetSystemTimePreciseAsFileTime");
        }
        if (fn) fn(&ft);
        else    GetSystemTimeAsFileTime(&ft);
        filetime_to_timespec(&ft, ts);
        return 0;
    }
    case CLOCK_MONOTONIC:
    case CLOCK_MONOTONIC_RAW:
    case CLOCK_MONOTONIC_COARSE:
    case CLOCK_BOOTTIME: {
        InitOnceExecuteOnce(&g_qpc_once, init_qpc, NULL, NULL);
        LARGE_INTEGER c;
        QueryPerformanceCounter(&c);
        uint64_t freq = (uint64_t)g_qpc_freq.QuadPart;
        if (!freq) { errno = EINVAL; return -1; }
        ts->tv_sec  = (time_t)(c.QuadPart / freq);
        uint64_t rem = (uint64_t)c.QuadPart % freq;
        ts->tv_nsec = (long)((rem * 1000000000ULL) / freq);
        return 0;
    }
    case CLOCK_PROCESS_CPUTIME_ID: {
        FILETIME c, e, k, u;
        if (!GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) {
            errno = EINVAL; return -1;
        }
        uint64_t total = (((uint64_t)k.dwHighDateTime << 32) | k.dwLowDateTime)
                      +  (((uint64_t)u.dwHighDateTime << 32) | u.dwLowDateTime);
        ts->tv_sec  = (time_t)(total / 10000000ULL);
        ts->tv_nsec = (long)  ((total % 10000000ULL) * 100);
        return 0;
    }
    case CLOCK_THREAD_CPUTIME_ID: {
        FILETIME c, e, k, u;
        if (!GetThreadTimes(GetCurrentThread(), &c, &e, &k, &u)) {
            errno = EINVAL; return -1;
        }
        uint64_t total = (((uint64_t)k.dwHighDateTime << 32) | k.dwLowDateTime)
                      +  (((uint64_t)u.dwHighDateTime << 32) | u.dwLowDateTime);
        ts->tv_sec  = (time_t)(total / 10000000ULL);
        ts->tv_nsec = (long)  ((total % 10000000ULL) * 100);
        return 0;
    }
    default:
        errno = EINVAL;
        return -1;
    }
}

int nanosleep(const struct timespec *req, struct timespec *rem)
{
    if (!req || req->tv_nsec < 0 || req->tv_nsec >= 1000000000L) {
        errno = EINVAL; return -1;
    }
    int64_t ns = (int64_t)req->tv_sec * 1000000000LL + req->tv_nsec;
    DWORD ms = (ns + 999999) / 1000000;
    Sleep(ms);
    if (rem) { rem->tv_sec = 0; rem->tv_nsec = 0; }
    return 0;
}

int gettimeofday(struct timeval *tv, void *tz)
{
    (void)tz;
    if (!tv) { errno = EFAULT; return -1; }
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) return -1;
    tv->tv_sec  = (long)ts.tv_sec;
    tv->tv_usec = (long)(ts.tv_nsec / 1000);
    return 0;
}
