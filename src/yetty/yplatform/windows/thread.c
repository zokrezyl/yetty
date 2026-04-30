/* thread.c - Windows threading implementation */

#include <yetty/yplatform/thread.h>
#include <windows.h>
#include <process.h>
#include <stdlib.h>

/* Thread */

struct yetty_yplatform_ythread {
    HANDLE handle;
    ythread_func_t func;
    void *arg;
};

static unsigned __stdcall thread_wrapper(void *arg)
{
    struct yetty_yplatform_ythread *t = arg;
    t->func(t->arg);
    return 0;
}

struct yetty_yplatform_ythread *yetty_yplatform_ythread_create(ythread_func_t func, void *arg)
{
    struct yetty_yplatform_ythread *t = calloc(1, sizeof(*t));
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

int yetty_yplatform_ythread_join(struct yetty_yplatform_ythread *thread)
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

struct yetty_yplatform_ymutex {
    CRITICAL_SECTION cs;
};

struct yetty_yplatform_ymutex *yetty_yplatform_ymutex_create(void)
{
    struct yetty_yplatform_ymutex *m = calloc(1, sizeof(*m));
    if (!m) {
        return NULL;
    }
    InitializeCriticalSection(&m->cs);
    return m;
}

void yetty_yplatform_ymutex_destroy(struct yetty_yplatform_ymutex *m)
{
    if (!m) {
        return;
    }
    DeleteCriticalSection(&m->cs);
    free(m);
}

void yetty_yplatform_ymutex_lock(struct yetty_yplatform_ymutex *m)
{
    EnterCriticalSection(&m->cs);
}

void yetty_yplatform_ymutex_unlock(struct yetty_yplatform_ymutex *m)
{
    LeaveCriticalSection(&m->cs);
}
