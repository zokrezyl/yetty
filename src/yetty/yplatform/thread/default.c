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

/* Condition variable */

struct yetty_yplatform_ycond {
    pthread_cond_t handle;
};

struct yetty_yplatform_ycond *yetty_yplatform_ycond_create(void)
{
    struct yetty_yplatform_ycond *c = calloc(1, sizeof(*c));
    if (!c) {
        return NULL;
    }
    pthread_cond_init(&c->handle, NULL);
    return c;
}

void yetty_yplatform_ycond_destroy(struct yetty_yplatform_ycond *c)
{
    if (!c) {
        return;
    }
    pthread_cond_destroy(&c->handle);
    free(c);
}

void yetty_yplatform_ycond_wait(struct yetty_yplatform_ycond *c, struct yetty_yplatform_ymutex *m)
{
    pthread_cond_wait(&c->handle, &m->handle);
}

void yetty_yplatform_ycond_signal(struct yetty_yplatform_ycond *c)
{
    pthread_cond_signal(&c->handle);
}

void yetty_yplatform_ycond_broadcast(struct yetty_yplatform_ycond *c)
{
    pthread_cond_broadcast(&c->handle);
}
