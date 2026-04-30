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

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPLATFORM_THREAD_H */
