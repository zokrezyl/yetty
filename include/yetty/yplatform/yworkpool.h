/*
 * yplatform/yworkpool.h - Lazy worker thread pool.
 *
 * Generic primitive for moving heavy or blocking work off the loop/render
 * thread. A job is a (run, done, ctx) triple: `run` executes on a worker
 * thread and may block; `done` (optional) is dispatched back onto the
 * event-loop thread via post_to_loop once run returns. Same shape covers
 * blocking GPU completion (wgpuInstanceWaitAny), image decoding, hashing,
 * compression, sync filesystem operations, etc.
 *
 * Threads are started lazily on the first submit and reused. Pool grows
 * up to max_threads as concurrent demand exceeds available workers; once
 * grown, threads stay alive for the lifetime of the pool. Jobs submitted
 * while all workers are busy are queued FIFO and picked up as workers
 * become free.
 *
 * Scope: this header is desktop-only. The yplatform-side of cross-platform
 * features handles its webasm path differently (browser-native async, no
 * threads), so webasm code never includes this header. Build systems must
 * compile yworkpool/default.c only on desktop platforms.
 *
 * Thread safety:
 *   - yworkpool_submit is safe to call from any thread.
 *   - The pool must be created and destroyed on the loop thread.
 *   - job.done always runs on the loop thread.
 */

#ifndef YETTY_YPLATFORM_YWORKPOOL_H
#define YETTY_YPLATFORM_YWORKPOOL_H

#include <stdint.h>
#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yevent_event_loop;
struct yetty_yplatform_yworkpool;

YETTY_YRESULT_DECLARE(yetty_yplatform_yworkpool_ptr, struct yetty_yplatform_yworkpool *);

/* A unit of work.
 *
 *   run(ctx)  : runs on a worker thread; may block. Required.
 *   done(ctx) : runs on the event-loop thread after run() returns. Optional
 *               (may be NULL). The pool is responsible for posting it to the
 *               loop; the caller does not touch post_to_loop directly.
 *   ctx       : opaque caller-owned pointer. Lifetime is the caller's
 *               responsibility; typically freed inside done().
 */
struct yetty_yplatform_yworkpool_job {
    void (*run)(void *ctx);
    void (*done)(void *ctx);
    void *ctx;
};

/* Create a pool. Threads are not started yet (lazy).
 *   loop         : used for done() dispatch via loop->ops->post_to_loop.
 *                  Must have a non-NULL post_to_loop op.
 *   name         : short tag used in trace output; copied internally. May
 *                  be NULL.
 *   max_threads  : hard upper bound on concurrent workers. Must be >= 1. */
struct yetty_yplatform_yworkpool_ptr_result yetty_yplatform_yworkpool_create(
    struct yetty_yevent_event_loop *loop, const char *name, uint32_t max_threads);

/* Submit a job. Hands off to an idle worker if one exists; spawns a new
 * worker (up to max_threads) otherwise; else enqueues FIFO. Thread-safe. */
struct yetty_ycore_void_result yetty_yplatform_yworkpool_submit(
    struct yetty_yplatform_yworkpool *pool, struct yetty_yplatform_yworkpool_job job);

/* Destroy. Signals all workers to exit after they finish their current
 * job, joins them, drains pending jobs (their done() is NOT called for
 * jobs whose run() never executed). Handles NULL. */
void yetty_yplatform_yworkpool_destroy(struct yetty_yplatform_yworkpool *pool);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPLATFORM_YWORKPOOL_H */
