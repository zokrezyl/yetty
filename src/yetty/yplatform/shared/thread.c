/* thread.c - POSIX threading implementation */

#include <yetty/yplatform/thread.h>
#include <pthread.h>
#include <stdlib.h>

/* Thread */

struct ythread {
    pthread_t handle;
    ythread_func_t func;
    void *arg;
};

static void *thread_wrapper(void *arg)
{
    struct ythread *t = arg;
    t->func(t->arg);
    return NULL;
}

ythread_t *ythread_create(ythread_func_t func, void *arg)
{
    struct ythread *t = calloc(1, sizeof(*t));
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

int ythread_join(ythread_t *thread)
{
    if (!thread) {
        return -1;
    }
    int ret = pthread_join(thread->handle, NULL);
    free(thread);
    return ret;
}

/* Mutex */

struct ymutex {
    pthread_mutex_t handle;
};

ymutex_t *ymutex_create(void)
{
    struct ymutex *m = calloc(1, sizeof(*m));
    if (!m) {
        return NULL;
    }
    pthread_mutex_init(&m->handle, NULL);
    return m;
}

void ymutex_destroy(ymutex_t *m)
{
    if (!m) {
        return;
    }
    pthread_mutex_destroy(&m->handle);
    free(m);
}

void ymutex_lock(ymutex_t *m)
{
    pthread_mutex_lock(&m->handle);
}

void ymutex_unlock(ymutex_t *m)
{
    pthread_mutex_unlock(&m->handle);
}
