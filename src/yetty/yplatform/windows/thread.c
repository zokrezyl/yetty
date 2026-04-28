/* thread.c - Windows threading implementation */

#include <yetty/yplatform/thread.h>
#include <windows.h>
#include <process.h>
#include <stdlib.h>

/* Thread */

struct ythread {
    HANDLE handle;
    ythread_func_t func;
    void *arg;
};

static unsigned __stdcall thread_wrapper(void *arg)
{
    struct ythread *t = arg;
    t->func(t->arg);
    return 0;
}

ythread_t *ythread_create(ythread_func_t func, void *arg)
{
    struct ythread *t = calloc(1, sizeof(*t));
    if (!t) {
        return NULL;
    }

    t->func = func;
    t->arg = arg;

    t->handle = (HANDLE)_beginthreadex(NULL, 0, thread_wrapper, t, 0, NULL);
    if (!t->handle) {
        free(t);
        return NULL;
    }
    return t;
}

int ythread_join(ythread_t *thread)
{
    if (!thread) {
        return -1;
    }
    WaitForSingleObject(thread->handle, INFINITE);
    CloseHandle(thread->handle);
    free(thread);
    return 0;
}

/* Mutex */

struct ymutex {
    CRITICAL_SECTION cs;
};

ymutex_t *ymutex_create(void)
{
    struct ymutex *m = calloc(1, sizeof(*m));
    if (!m) {
        return NULL;
    }
    InitializeCriticalSection(&m->cs);
    return m;
}

void ymutex_destroy(ymutex_t *m)
{
    if (!m) {
        return;
    }
    DeleteCriticalSection(&m->cs);
    free(m);
}

void ymutex_lock(ymutex_t *m)
{
    EnterCriticalSection(&m->cs);
}

void ymutex_unlock(ymutex_t *m)
{
    LeaveCriticalSection(&m->cs);
}
