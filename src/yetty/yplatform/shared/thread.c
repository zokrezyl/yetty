/* thread.c - POSIX threading implementation */

#include <yetty/yplatform/thread.h>
#include <pthread.h>
#include <stdlib.h>

/* Thread */

struct yetty_yplatform_ythread {
    pthread_t handle;
    ythread_func_t func;
    void *arg;
};

static void *thread_wrapper(void *arg)
{
    struct yetty_yplatform_ythread *t = arg;
    t->func(t->arg);
    return NULL;
}

struct yetty_yplatform_ythread *yetty_yplatform_ythread_create(ythread_func_t func, void *arg)
{
    struct yetty_yplatform_ythread *t = calloc(1, sizeof(*t));
    if (!t) {
        return NULL;
    }

    t->func = func;
    t->arg = arg;

    if (pthread_create(&t->handle, NULL, thread_wrapper, t) != 0) {
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
    int ret = pthread_join(thread->handle, NULL);
    free(thread);
    return ret;
}

/* Mutex */

struct yetty_yplatform_ymutex {
    pthread_mutex_t handle;
};

struct yetty_yplatform_ymutex *yetty_yplatform_ymutex_create(void)
{
    struct yetty_yplatform_ymutex *m = calloc(1, sizeof(*m));
    if (!m) {
        return NULL;
    }
    pthread_mutex_init(&m->handle, NULL);
    return m;
}

void yetty_yplatform_ymutex_destroy(struct yetty_yplatform_ymutex *m)
{
    if (!m) {
        return;
    }
    pthread_mutex_destroy(&m->handle);
    free(m);
}

void yetty_yplatform_ymutex_lock(struct yetty_yplatform_ymutex *m)
{
    pthread_mutex_lock(&m->handle);
}

void yetty_yplatform_ymutex_unlock(struct yetty_yplatform_ymutex *m)
{
    pthread_mutex_unlock(&m->handle);
}
