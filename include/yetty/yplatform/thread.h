/*
 * yplatform/thread.h - Cross-platform threading abstraction
 */

#ifndef YETTY_YPLATFORM_THREAD_H
#define YETTY_YPLATFORM_THREAD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque thread handle */
struct yetty_yplatform_ythread;

/* Thread function signature: returns 0 on success */
typedef int (*ythread_func_t)(void *arg);

/* Thread */
struct yetty_yplatform_ythread *yetty_yplatform_ythread_create(ythread_func_t func, void *arg);
int yetty_yplatform_ythread_join(struct yetty_yplatform_ythread *thread);

/* Opaque mutex handle */
struct yetty_yplatform_ymutex;

/* Mutex */
struct yetty_yplatform_ymutex *yetty_yplatform_ymutex_create(void);
void yetty_yplatform_ymutex_destroy(struct yetty_yplatform_ymutex *m);
void yetty_yplatform_ymutex_lock(struct yetty_yplatform_ymutex *m);
void yetty_yplatform_ymutex_unlock(struct yetty_yplatform_ymutex *m);

/* Opaque condition variable handle */
struct yetty_yplatform_ycond;

/* Condition variable. Always used in the standard pattern:
 *   ymutex_lock(m);
 *   while (!predicate) ycond_wait(c, m);
 *   ...
 *   ymutex_unlock(m);
 *
 * ycond_signal wakes one waiter; ycond_broadcast wakes all. The mutex
 * passed to ycond_wait must be held by the caller; it is released while
 * waiting and re-acquired before returning. */
struct yetty_yplatform_ycond *yetty_yplatform_ycond_create(void);
void yetty_yplatform_ycond_destroy(struct yetty_yplatform_ycond *c);
void yetty_yplatform_ycond_wait(struct yetty_yplatform_ycond *c, struct yetty_yplatform_ymutex *m);
void yetty_yplatform_ycond_signal(struct yetty_yplatform_ycond *c);
void yetty_yplatform_ycond_broadcast(struct yetty_yplatform_ycond *c);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPLATFORM_THREAD_H */
