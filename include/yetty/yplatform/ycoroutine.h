/*
 * yplatform/ycoroutine.h - Coroutine primitive.
 *
 * Two backends, identical API but different mechanics:
 *
 *   - desktop (yplatform/shared/ycoroutine.c):
 *       Real stackful coroutines via libco. spawn allocates a stack;
 *       resume switches into it; yield switches back to whoever resumed.
 *       Resume must be called on the event-loop thread. Cross-thread
 *       wakeups (e.g. the GPU poll thread) post a request via the event
 *       loop and the loop thread invokes resume.
 *
 *   - webasm (yplatform/webasm/ycoroutine.c):
 *       Degenerate stub. spawn allocates a control struct, resume calls
 *       the entry function inline (no stack switch). The webasm wgpu
 *       _await wrappers (yplatform/webasm/ywebgpu.c) suspend the C call
 *       stack via Asyncify (emscripten_sleep) instead of yield, so a
 *       coroutine isn't actually needed — the stub exists only so
 *       desktop callers compile unchanged. yplatform_coro_yield is a
 *       warn-only no-op on this backend.
 */

#ifndef YETTY_YPLATFORM_YCOROUTINE_H
#define YETTY_YPLATFORM_YCOROUTINE_H

#include <stddef.h>
#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yplatform_coro;

YETTY_YRESULT_DECLARE(yplatform_coro_ptr, struct yetty_yplatform_coro *);

typedef void (*yplatform_coro_entry)(void *arg);

/* Spawn a coroutine. Does not start it; call yplatform_coro_resume to run.
 * stack_hint of 0 means use the platform default. name is copied internally
 * and used in trace output; may be NULL. */
struct yplatform_coro_ptr_result yetty_yplatform_coro_spawn(yplatform_coro_entry entry, void *arg,
                                                      size_t stack_hint, const char *name);

/* Suspend the currently-running coroutine. Returns when somebody resumes it.
 * Must be called from inside a coroutine (not the main stack). */
void yetty_yplatform_coro_yield(void);

/* Resume a suspended coroutine. Must be called on the event-loop thread.
 * Returns when the coroutine yields again or finishes. */
void yetty_yplatform_coro_resume(struct yetty_yplatform_coro *coro);

/* Free the coroutine. Must have finished (entry returned). */
void yetty_yplatform_coro_destroy(struct yetty_yplatform_coro *coro);

/* The currently-running coroutine, or NULL if called from the main stack. */
struct yetty_yplatform_coro *yetty_yplatform_coro_current(void);

/* True once the coroutine's entry function has returned. */
int yetty_yplatform_coro_is_finished(const struct yetty_yplatform_coro *coro);

/* Coroutine identity / debugging. Returns a stable id assigned at spawn. */
unsigned int yetty_yplatform_coro_id(const struct yetty_yplatform_coro *coro);
const char *yetty_yplatform_coro_name(const struct yetty_yplatform_coro *coro);

/* Small status word the coroutine itself can read after a resume. Used by
 * the wgpu await wrappers to deliver a completion code (success / cancelled
 * / error) without a separate per-await allocation. */
void yetty_yplatform_coro_set_status(struct yetty_yplatform_coro *coro, int status);
int yetty_yplatform_coro_get_status(const struct yetty_yplatform_coro *coro);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPLATFORM_YCOROUTINE_H */
