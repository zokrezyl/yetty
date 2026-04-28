/*
 * yplatform/ycoroutine.h - Cross-platform stackful coroutine primitive.
 *
 * Two backends, identical API:
 *   - desktop:  POSIX ucontext (makecontext/swapcontext)
 *   - webasm:   emscripten_fiber_t (Asyncify under the hood)
 *
 * A coroutine is created with yplatform_coro_spawn but does not run until
 * yplatform_coro_resume is called on it. Inside the coroutine entry, calling
 * yplatform_coro_yield suspends back to whoever resumed it.
 *
 * Resume must always be called on the event-loop thread. Cross-thread wakeups
 * (e.g. the GPU poll thread) post a request via the event loop and the loop
 * thread invokes resume.
 */

#ifndef YETTY_YPLATFORM_YCOROUTINE_H
#define YETTY_YPLATFORM_YCOROUTINE_H

#include <stddef.h>
#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yplatform_coro;

YETTY_YRESULT_DECLARE(yplatform_coro_ptr, struct yplatform_coro *);

typedef void (*yplatform_coro_entry)(void *arg);

/* Spawn a coroutine. Does not start it; call yplatform_coro_resume to run.
 * stack_hint of 0 means use the platform default. name is copied internally
 * and used in trace output; may be NULL. */
struct yplatform_coro_ptr_result yplatform_coro_spawn(yplatform_coro_entry entry, void *arg,
                                                      size_t stack_hint, const char *name);

/* Suspend the currently-running coroutine. Returns when somebody resumes it.
 * Must be called from inside a coroutine (not the main stack). */
void yplatform_coro_yield(void);

/* Resume a suspended coroutine. Must be called on the event-loop thread.
 * Returns when the coroutine yields again or finishes. */
void yplatform_coro_resume(struct yplatform_coro *coro);

/* Free the coroutine. Must have finished (entry returned). */
void yplatform_coro_destroy(struct yplatform_coro *coro);

/* The currently-running coroutine, or NULL if called from the main stack. */
struct yplatform_coro *yplatform_coro_current(void);

/* True once the coroutine's entry function has returned. */
int yplatform_coro_is_finished(const struct yplatform_coro *coro);

/* Coroutine identity / debugging. Returns a stable id assigned at spawn. */
unsigned int yplatform_coro_id(const struct yplatform_coro *coro);
const char *yplatform_coro_name(const struct yplatform_coro *coro);

/* Small status word the coroutine itself can read after a resume. Used by
 * the wgpu await wrappers to deliver a completion code (success / cancelled
 * / error) without a separate per-await allocation. */
void yplatform_coro_set_status(struct yplatform_coro *coro, int status);
int yplatform_coro_get_status(const struct yplatform_coro *coro);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPLATFORM_YCOROUTINE_H */
