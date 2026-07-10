/*
 * yplatform/thread.h - Cross-platform threading abstraction
 */

#ifndef YOS_YPLATFORM_THREAD_H
#define YOS_YPLATFORM_THREAD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque thread handle */
struct yos_yplatform_ythread;

/* Thread function signature: returns 0 on success */
typedef int (*ythread_func_t)(void *arg);

/* Thread */
struct yos_yplatform_ythread *yos_yplatform_ythread_create(ythread_func_t func, void *arg);
int yos_yplatform_ythread_join(struct yos_yplatform_ythread *thread);

/* Opaque mutex handle */
struct yos_yplatform_ymutex;

/* Mutex */
struct yos_yplatform_ymutex *yos_yplatform_ymutex_create(void);
void yos_yplatform_ymutex_destroy(struct yos_yplatform_ymutex *m);
void yos_yplatform_ymutex_lock(struct yos_yplatform_ymutex *m);
void yos_yplatform_ymutex_unlock(struct yos_yplatform_ymutex *m);

#ifdef __cplusplus
}
#endif

#endif /* YOS_YPLATFORM_THREAD_H */
