/* thread.c - POSIX threading implementation */

#include <yos/yplatform/thread.h>
#include <pthread.h>
#include <stdlib.h>

/* Thread */

struct yos_yplatform_ythread {
    pthread_t handle;
    ythread_func_t func;
    void *arg;
};

static void *thread_wrapper(void *arg)
{
    struct yos_yplatform_ythread *t = arg;
    t->func(t->arg);
    return NULL;
}

struct yos_yplatform_ythread *yos_yplatform_ythread_create(ythread_func_t func, void *arg)
{
    struct yos_yplatform_ythread *t = calloc(1, sizeof(*t));
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

int yos_yplatform_ythread_join(struct yos_yplatform_ythread *thread)
{
    if (!thread) {
        return -1;
    }
    int ret = pthread_join(thread->handle, NULL);
    free(thread);
    return ret;
}

/* Mutex */

struct yos_yplatform_ymutex {
    pthread_mutex_t handle;
};

struct yos_yplatform_ymutex *yos_yplatform_ymutex_create(void)
{
    struct yos_yplatform_ymutex *m = calloc(1, sizeof(*m));
    if (!m) {
        return NULL;
    }
    pthread_mutex_init(&m->handle, NULL);
    return m;
}

void yos_yplatform_ymutex_destroy(struct yos_yplatform_ymutex *m)
{
    if (!m) {
        return;
    }
    pthread_mutex_destroy(&m->handle);
    free(m);
}

void yos_yplatform_ymutex_lock(struct yos_yplatform_ymutex *m)
{
    pthread_mutex_lock(&m->handle);
}

void yos_yplatform_ymutex_unlock(struct yos_yplatform_ymutex *m)
{
    pthread_mutex_unlock(&m->handle);
}
