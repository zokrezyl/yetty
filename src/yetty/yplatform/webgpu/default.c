/*
 * yplatform/webgpu/default.c - Desktop wgpu await wrappers.
 *
 * One WGPU async call ↔ one yworkpool job. The worker thread blocks in
 * wgpuInstanceWaitAny(instance, 1, &fwi, UINT64_MAX) until the future
 * completes; the pool then posts done() to the loop thread, which resumes
 * the awaiting coroutine. No ProcessEvents timer, no polling, no CPU
 * spinning while idle.
 *
 * Wait threads are owned by a yworkpool created in yplatform_wgpu_create.
 * Threads are spawned lazily on first concurrent await and reused for the
 * lifetime of the instance. Concurrent _await calls grow the pool up to
 * its max; once at max, additional submits queue FIFO (this matches the
 * current callers — tile-diff and screenshot serialize their awaits within
 * one coroutine and run at most one such coroutine at a time).
 *
 * Requirements: the WGPU instance must have been created with the
 * WGPUInstanceFeatureName_TimedWaitAny feature enabled. WaitAny with
 * timeoutNS > 0 fails without it.
 */

#include <yetty/yplatform/ywebgpu.h>
#include <yetty/yplatform/ycoroutine.h>
#include <yetty/yplatform/yworkpool.h>
#include <yetty/yevent/event-loop.h>
#include <yetty/ytrace/ytrace.h>

#include <webgpu/webgpu.h>

#include <stdint.h>
#include <stdlib.h>

/* Pool sized for the worst case any single caller is expected to hit. The
 * tile-diff coroutine awaits sequentially (one in flight at a time), and
 * screenshot is also serialized. 4 leaves headroom for future concurrent
 * use without thread explosion. */
#define YWEBGPU_WAIT_POOL_MAX 4

struct yetty_yplatform_wgpu {
    WGPUInstance instance;
    struct yetty_yevent_event_loop *loop;
    struct yetty_yplatform_yworkpool *wait_pool;
};

/* Per-await context. Allocated by the _await wrapper, freed by done() on
 * the loop thread after the coroutine has been resumed. */
struct ywgpu_await_ctx {
    struct yetty_yplatform_wgpu *wgpu;
    struct yetty_yplatform_coro *coro;
    WGPUFuture future;
    /* Status set by the WGPU callback while it fires from inside
     * WaitAny on the worker thread. The coroutine reads it after resume. */
    int callback_status;
};

struct yplatform_wgpu_ptr_result yetty_yplatform_wgpu_create(WGPUInstance instance,
                                                             struct yetty_yevent_event_loop *loop)
{
    if (!instance || !loop) {
        return YETTY_ERR(yplatform_wgpu_ptr, "instance or loop is NULL");
    }
    if (!loop->ops || !loop->ops->post_to_loop) {
        return YETTY_ERR(yplatform_wgpu_ptr, "event loop has no post_to_loop op");
    }

    struct yetty_yplatform_wgpu *wgpu = calloc(1, sizeof(struct yetty_yplatform_wgpu));
    if (!wgpu) {
        return YETTY_ERR(yplatform_wgpu_ptr, "calloc failed");
    }
    wgpu->instance = instance;
    wgpu->loop = loop;

    struct yetty_yplatform_yworkpool_ptr_result pres =
        yetty_yplatform_yworkpool_create(loop, "wgpu-wait", YWEBGPU_WAIT_POOL_MAX);
    if (!YETTY_IS_OK(pres)) {
        free(wgpu);
        return YETTY_ERR(yplatform_wgpu_ptr, "yworkpool_create failed", pres);
    }
    wgpu->wait_pool = pres.value;

    yinfo("ywebgpu: created (WaitAny pool, max=%d)", YWEBGPU_WAIT_POOL_MAX);
    return YETTY_OK(yplatform_wgpu_ptr, wgpu);
}

void yetty_yplatform_wgpu_destroy(struct yetty_yplatform_wgpu *wgpu)
{
    if (!wgpu) {
        return;
    }
    yinfo("ywebgpu: destroying");
    if (wgpu->wait_pool) {
        yetty_yplatform_yworkpool_destroy(wgpu->wait_pool);
    }
    free(wgpu);
}

/* Worker-thread body: block until the future completes. WaitAny invokes
 * the registered callback (with WGPUCallbackMode_WaitAnyOnly mode) before
 * returning, so by the time we get here ctx->callback_status is set. */
static void ywgpu_wait_run(void *arg)
{
    struct ywgpu_await_ctx *ctx = arg;
    WGPUFutureWaitInfo fi = {0};
    fi.future = ctx->future;

    WGPUWaitStatus ws = wgpuInstanceWaitAny(ctx->wgpu->instance, 1, &fi, UINT64_MAX);
    if (ws != WGPUWaitStatus_Success) {
        /* Wait itself failed (timeout-with-no-feature, unsupported, etc.).
         * The callback may not have fired; synthesize an error status so
         * the awaiter doesn't read an uninitialised value. */
        ywarn("ywebgpu: wgpuInstanceWaitAny failed ws=%d coro=%u", (int)ws,
              yetty_yplatform_coro_id(ctx->coro));
        ctx->callback_status = -1;
    }
}

/* Loop-thread body: resume the coroutine. Mirrors the old
 * resume_coro_on_loop — fire-and-forget coroutines own themselves once
 * they yield into the GPU pipeline. */
static void ywgpu_wait_done(void *arg)
{
    struct ywgpu_await_ctx *ctx = arg;
    struct yetty_yplatform_coro *coro = ctx->coro;
    yetty_yplatform_coro_set_status(coro, ctx->callback_status);
    free(ctx);

    ydebug("ywebgpu: resuming coro %u", yetty_yplatform_coro_id(coro));
    yetty_yplatform_coro_resume(coro);
    if (yetty_yplatform_coro_is_finished(coro)) {
        ydebug("ywebgpu: coro %u finished, destroying", yetty_yplatform_coro_id(coro));
        yetty_yplatform_coro_destroy(coro);
    }
}

static void map_callback(WGPUMapAsyncStatus status, WGPUStringView msg, void *userdata1,
                         void *userdata2)
{
    (void)userdata2;
    struct ywgpu_await_ctx *ctx = userdata1;
    ydebug("ywebgpu: map_callback status=%d coro=%u msg=\"%.*s\"", (int)status,
           yetty_yplatform_coro_id(ctx->coro), (int)msg.length, msg.data ? msg.data : "");
    ctx->callback_status = (int)status;
}

struct yetty_ycore_void_result yetty_yplatform_wgpu_buffer_map_await(
    struct yetty_yplatform_wgpu *wgpu, WGPUBuffer buffer, WGPUMapMode mode, size_t offset,
    size_t size)
{
    if (!wgpu) {
        return YETTY_ERR(yetty_ycore_void, "wgpu is NULL");
    }
    struct yetty_yplatform_coro *self = yetty_yplatform_coro_current();
    if (!self) {
        return YETTY_ERR(yetty_ycore_void, "buffer_map_await called outside coroutine");
    }

    struct ywgpu_await_ctx *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        return YETTY_ERR(yetty_ycore_void, "buffer_map_await: ctx alloc failed");
    }
    ctx->wgpu = wgpu;
    ctx->coro = self;

    WGPUBufferMapCallbackInfo cb = {0};
    cb.mode = WGPUCallbackMode_WaitAnyOnly;
    cb.callback = map_callback;
    cb.userdata1 = ctx;

    ydebug("ywebgpu: buffer_map_await coro=%u buffer=%p offset=%zu size=%zu",
           yetty_yplatform_coro_id(self), (void *)buffer, offset, size);
    ctx->future = wgpuBufferMapAsync(buffer, mode, offset, size, cb);

    struct yetty_yplatform_yworkpool_job job = {
        .run = ywgpu_wait_run,
        .done = ywgpu_wait_done,
        .ctx = ctx,
    };
    struct yetty_ycore_void_result sres = yetty_yplatform_yworkpool_submit(wgpu->wait_pool, job);
    if (!YETTY_IS_OK(sres)) {
        free(ctx);
        return YETTY_ERR(yetty_ycore_void, "buffer_map_await: yworkpool_submit failed", sres);
    }

    yetty_yplatform_coro_yield();

    int status = yetty_yplatform_coro_get_status(self);
    if (status != WGPUMapAsyncStatus_Success) {
        ywarn("buffer_map_await: status=%d", status);
        return YETTY_ERR(yetty_ycore_void, "buffer map failed");
    }
    return YETTY_OK_VOID();
}

static void queue_done_callback(WGPUQueueWorkDoneStatus status, WGPUStringView msg, void *userdata1,
                                void *userdata2)
{
    (void)userdata2;
    struct ywgpu_await_ctx *ctx = userdata1;
    ydebug("ywebgpu: queue_done_callback status=%d coro=%u msg=\"%.*s\"", (int)status,
           yetty_yplatform_coro_id(ctx->coro), (int)msg.length, msg.data ? msg.data : "");
    ctx->callback_status = (int)status;
}

struct yetty_ycore_void_result yetty_yplatform_wgpu_queue_done_await(
    struct yetty_yplatform_wgpu *wgpu, WGPUQueue queue)
{
    if (!wgpu) {
        return YETTY_ERR(yetty_ycore_void, "wgpu is NULL");
    }
    struct yetty_yplatform_coro *self = yetty_yplatform_coro_current();
    if (!self) {
        return YETTY_ERR(yetty_ycore_void, "queue_done_await called outside coroutine");
    }

    struct ywgpu_await_ctx *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        return YETTY_ERR(yetty_ycore_void, "queue_done_await: ctx alloc failed");
    }
    ctx->wgpu = wgpu;
    ctx->coro = self;

    WGPUQueueWorkDoneCallbackInfo cb = {0};
    cb.mode = WGPUCallbackMode_WaitAnyOnly;
    cb.callback = queue_done_callback;
    cb.userdata1 = ctx;

    ydebug("ywebgpu: queue_done_await coro=%u queue=%p", yetty_yplatform_coro_id(self),
           (void *)queue);
    ctx->future = wgpuQueueOnSubmittedWorkDone(queue, cb);

    struct yetty_yplatform_yworkpool_job job = {
        .run = ywgpu_wait_run,
        .done = ywgpu_wait_done,
        .ctx = ctx,
    };
    struct yetty_ycore_void_result sres = yetty_yplatform_yworkpool_submit(wgpu->wait_pool, job);
    if (!YETTY_IS_OK(sres)) {
        free(ctx);
        return YETTY_ERR(yetty_ycore_void, "queue_done_await: yworkpool_submit failed", sres);
    }

    yetty_yplatform_coro_yield();

    int status = yetty_yplatform_coro_get_status(self);
    if (status != WGPUQueueWorkDoneStatus_Success) {
        ywarn("queue_done_await: status=%d", status);
        return YETTY_ERR(yetty_ycore_void, "queue work done failed");
    }
    return YETTY_OK_VOID();
}
