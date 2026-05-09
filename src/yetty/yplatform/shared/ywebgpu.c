/*
 * yplatform/shared/ywebgpu.c - Desktop wgpu await wrappers.
 *
 * ProcessEvents must be driven somewhere or Dawn never delivers buffer-map
 * callbacks. We drive it from a periodic libuv timer that fires on the loop
 * thread — the same thread that issues wgpuQueueSubmit. Same-thread use is
 * safe; an earlier version drove ProcessEvents from a dedicated poll thread
 * and Dawn's Vulkan backend crashed on the first concurrent Submit.
 *
 * Callback mode is AllowSpontaneous so Dawn can also deliver callbacks from
 * its own internal threads. Either way the callback ends up calling
 * event_loop->ops->post_to_loop, which is thread-safe and routes the
 * coroutine resume back to the loop thread.
 */

#include <yetty/yplatform/ywebgpu.h>
#include <yetty/yplatform/ycoroutine.h>
#include <yetty/yevent/event-loop.h>
#include <yetty/yevent/event.h>
#include <yetty/ytrace/ytrace.h>

#include <webgpu/webgpu.h>

#include <stdlib.h>

#define YWEBGPU_TICK_MS 1

struct yetty_yplatform_wgpu {
    WGPUInstance instance;
    struct yetty_yevent_event_loop *loop;
    yetty_yevent_timer_id tick_timer_id;
    struct yetty_yevent_event_listener tick_listener;
    /* Number of in-flight _await calls. The ProcessEvents tick only runs
     * while this is > 0, so Dawn isn't pumped during regular rendering. */
    int pending_awaits;
    int tick_running;
};

/* Per-await context; lives on the awaiting coroutine's stack while it's
 * yielded. Carries the wgpu pointer so the callback can find the loop. */
struct yetty_yplatform_ywgpu_await_ctx {
    struct yetty_yplatform_coro *coro;
    struct yetty_yplatform_wgpu *wgpu;
};

/* The tick listener struct is embedded in struct yplatform_wgpu. We need to
 * recover the wgpu pointer in the handler — container_of pattern. */
static inline struct yetty_yplatform_wgpu *wgpu_from_listener(struct yetty_yevent_event_listener *l)
{
    return (struct yetty_yplatform_wgpu *)((char *)l -
                                           offsetof(struct yetty_yplatform_wgpu, tick_listener));
}

/* Runs on the loop thread every YWEBGPU_TICK_MS milliseconds. */
static struct yetty_ycore_int_result on_wgpu_tick(struct yetty_yevent_event_listener *listener,
                                                  const struct yetty_yui_event *event)
{
    (void)event;
    struct yetty_yplatform_wgpu *wgpu = wgpu_from_listener(listener);
    wgpuInstanceProcessEvents(wgpu->instance);
    return YETTY_OK(yetty_ycore_int, 1);
}

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
    wgpu->tick_listener.handler = on_wgpu_tick;

    struct yetty_yevent_timer_id_result tres = loop->ops->create_timer(loop);
    if (!YETTY_IS_OK(tres)) {
        free(wgpu);
        return YETTY_ERR(yplatform_wgpu_ptr, "create_timer failed");
    }
    wgpu->tick_timer_id = tres.value;

    struct yetty_ycore_void_result vres =
        loop->ops->config_timer(loop, wgpu->tick_timer_id, YWEBGPU_TICK_MS);
    if (!YETTY_IS_OK(vres)) {
        loop->ops->destroy_timer(loop, wgpu->tick_timer_id);
        free(wgpu);
        return YETTY_ERR(yplatform_wgpu_ptr, "config_timer failed");
    }

    vres = loop->ops->register_timer_listener(loop, wgpu->tick_timer_id, &wgpu->tick_listener);
    if (!YETTY_IS_OK(vres)) {
        loop->ops->destroy_timer(loop, wgpu->tick_timer_id);
        free(wgpu);
        return YETTY_ERR(yplatform_wgpu_ptr, "register_timer_listener failed");
    }

    /* Timer is NOT started here. It runs only while there is at least one
     * in-flight _await, so Dawn's ProcessEvents isn't pumped during normal
     * rendering (which broke ypaint). */

    yinfo("ywebgpu: created (%dms tick, on-demand)", YWEBGPU_TICK_MS);
    return YETTY_OK(yplatform_wgpu_ptr, wgpu);
}

/* Ref-count the in-flight awaits; start/stop the ProcessEvents tick at the
 * 0→1 and 1→0 transitions. Called from the loop thread only (all awaits
 * originate in coroutines that run on the loop thread). */
static void await_begin(struct yetty_yplatform_wgpu *wgpu)
{
    wgpu->pending_awaits++;
    if (wgpu->pending_awaits == 1 && !wgpu->tick_running) {
        wgpu->loop->ops->start_timer(wgpu->loop, wgpu->tick_timer_id);
        wgpu->tick_running = 1;
        ydebug("ywebgpu: tick started (pending=1)");
    }
}

static void await_end(struct yetty_yplatform_wgpu *wgpu)
{
    if (wgpu->pending_awaits > 0) {
        wgpu->pending_awaits--;
    }
    if (wgpu->pending_awaits == 0 && wgpu->tick_running) {
        wgpu->loop->ops->stop_timer(wgpu->loop, wgpu->tick_timer_id);
        wgpu->tick_running = 0;
        ydebug("ywebgpu: tick stopped (pending=0)");
    }
}

void yetty_yplatform_wgpu_destroy(struct yetty_yplatform_wgpu *wgpu)
{
    if (!wgpu) {
        return;
    }
    yinfo("ywebgpu: destroying (pending_awaits=%d)", wgpu->pending_awaits);
    if (wgpu->loop && wgpu->loop->ops) {
        if (wgpu->tick_running && wgpu->loop->ops->stop_timer) {
            wgpu->loop->ops->stop_timer(wgpu->loop, wgpu->tick_timer_id);
        }
        if (wgpu->loop->ops->destroy_timer) {
            wgpu->loop->ops->destroy_timer(wgpu->loop, wgpu->tick_timer_id);
        }
    }
    free(wgpu);
}

/* Runs on the loop thread (posted by the wgpu callback via post_to_loop).
 * If the coroutine ran to completion this round, destroy it — fire-and-forget
 * coroutines own themselves once they yield into the GPU pipeline. */
static void resume_coro_on_loop(void *arg)
{
    struct yetty_yplatform_coro *coro = arg;
    ydebug("ywebgpu: resuming coro %u", yetty_yplatform_coro_id(coro));
    yetty_yplatform_coro_resume(coro);
    if (yetty_yplatform_coro_is_finished(coro)) {
        ydebug("ywebgpu: coro %u finished, destroying", yetty_yplatform_coro_id(coro));
        yetty_yplatform_coro_destroy(coro);
    }
}

/* Fires from inside ProcessEvents on the loop thread, or — depending on
 * Dawn's internal choices — from a Dawn worker thread. Either way we route
 * through post_to_loop so resume always happens on the loop thread. */
static void map_callback(WGPUMapAsyncStatus status, WGPUStringView msg, void *userdata1,
                         void *userdata2)
{
    (void)userdata2;
    struct yetty_yplatform_ywgpu_await_ctx *ctx = userdata1;
    ydebug("ywebgpu: map_callback status=%d coro=%u msg=\"%.*s\"", (int)status,
           yetty_yplatform_coro_id(ctx->coro), (int)msg.length, msg.data ? msg.data : "");
    yetty_yplatform_coro_set_status(ctx->coro, (int)status);
    ctx->wgpu->loop->ops->post_to_loop(ctx->wgpu->loop, resume_coro_on_loop, ctx->coro);
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

    struct yetty_yplatform_ywgpu_await_ctx ctx = {.coro = self, .wgpu = wgpu};

    WGPUBufferMapCallbackInfo cb = {0};
    cb.mode = WGPUCallbackMode_AllowSpontaneous;
    cb.callback = map_callback;
    cb.userdata1 = &ctx;

    ydebug("ywebgpu: buffer_map_await coro=%u buffer=%p offset=%zu size=%zu",
           yetty_yplatform_coro_id(self), (void *)buffer, offset, size);
    await_begin(wgpu);
    wgpuBufferMapAsync(buffer, mode, offset, size, cb);
    yetty_yplatform_coro_yield();
    await_end(wgpu);

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
    struct yetty_yplatform_ywgpu_await_ctx *ctx = userdata1;
    ydebug("ywebgpu: queue_done_callback status=%d coro=%u msg=\"%.*s\"", (int)status,
           yetty_yplatform_coro_id(ctx->coro), (int)msg.length, msg.data ? msg.data : "");
    yetty_yplatform_coro_set_status(ctx->coro, (int)status);
    ctx->wgpu->loop->ops->post_to_loop(ctx->wgpu->loop, resume_coro_on_loop, ctx->coro);
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

    struct yetty_yplatform_ywgpu_await_ctx ctx = {.coro = self, .wgpu = wgpu};

    WGPUQueueWorkDoneCallbackInfo cb = {0};
    cb.mode = WGPUCallbackMode_AllowSpontaneous;
    cb.callback = queue_done_callback;
    cb.userdata1 = &ctx;

    ydebug("ywebgpu: queue_done_await coro=%u queue=%p", yetty_yplatform_coro_id(self),
           (void *)queue);
    await_begin(wgpu);
    wgpuQueueOnSubmittedWorkDone(queue, cb);
    yetty_yplatform_coro_yield();
    await_end(wgpu);

    int status = yetty_yplatform_coro_get_status(self);
    if (status != WGPUQueueWorkDoneStatus_Success) {
        ywarn("queue_done_await: status=%d", status);
        return YETTY_ERR(yetty_ycore_void, "queue work done failed");
    }
    return YETTY_OK_VOID();
}
