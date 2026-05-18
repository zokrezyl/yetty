/*
 * yplatform/webasm/ywebgpu.c - WebGPU await wrappers for emscripten.
 *
 * The desktop version (yplatform/shared/ywebgpu.c) yields a coroutine,
 * registers a wgpu callback, drives wgpuInstanceProcessEvents from a libuv
 * timer, and bounces the resume back to the loop thread. Webasm needs none
 * of that:
 *
 *   - Browser pumps wgpu callbacks for free from the JS event loop. No
 *     ProcessEvents, no timer, no ref-counted "tick" machinery.
 *   - Single-threaded by construction. Callbacks fire on the JS main
 *     thread; no post_to_loop hop.
 *   - Asyncify (-sASYNCIFY, set in build-tools/cmake/targets/webasm.cmake)
 *     suspends the entire C call stack at emscripten_sleep, so a "wait
 *     for callback" is a flag + sleep loop with no coroutine in sight.
 *
 * Trade-off: the C call to submit() is asyncify-suspended for the full
 * duration of the GPU readback rather than returning after the first
 * yield. The browser's JS event loop continues running during that time
 * (input, requestAnimationFrame, PTY callbacks all still fire), so this
 * is observably equivalent to the coroutine-yield model on desktop.
 */

#include <yetty/yplatform/ywebgpu.h>
#include <yetty/yevent/event-loop.h>
#include <yetty/ytrace/ytrace.h>

#include <emscripten/emscripten.h>
#include <webgpu/webgpu.h>

#include <stdlib.h>

struct yetty_yplatform_wgpu {
    WGPUInstance instance;
    /* Kept only to match the desktop signature; webasm has no per-loop
     * state (no timer, no post_to_loop). */
    struct yetty_yevent_event_loop *loop;
};

/* Local to a single _await call; lives on its caller's stack while
 * asyncify-suspended. The wgpu callback writes both fields. */
struct yetty_yplatform_ywgpu_await_state {
    volatile int done;
    int status;
};

struct yplatform_wgpu_ptr_result yetty_yplatform_wgpu_create(WGPUInstance instance,
                                                             struct yetty_yevent_event_loop *loop)
{
    if (!instance) {
        return YETTY_ERR(yplatform_wgpu_ptr, "instance is NULL");
    }

    struct yetty_yplatform_wgpu *wgpu = calloc(1, sizeof(struct yetty_yplatform_wgpu));
    if (!wgpu) {
        return YETTY_ERR(yplatform_wgpu_ptr, "calloc failed");
    }

    wgpu->instance = instance;
    wgpu->loop = loop;

    yinfo("ywebgpu: created (webasm asyncify backend)");
    return YETTY_OK(yplatform_wgpu_ptr, wgpu);
}

void yetty_yplatform_wgpu_destroy(struct yetty_yplatform_wgpu *wgpu)
{
    if (!wgpu) {
        return;
    }
    free(wgpu);
}

static void map_callback(WGPUMapAsyncStatus status, WGPUStringView msg, void *userdata1,
                         void *userdata2)
{
    (void)userdata2;
    struct yetty_yplatform_ywgpu_await_state *st = userdata1;
    ydebug("ywebgpu: map_callback status=%d msg=\"%.*s\"", (int)status, (int)msg.length,
           msg.data ? msg.data : "");
    st->status = (int)status;
    st->done = 1;
}

struct yetty_ycore_void_result yetty_yplatform_wgpu_buffer_map_await(
    struct yetty_yplatform_wgpu *wgpu, WGPUBuffer buffer, WGPUMapMode mode, size_t offset,
    size_t size)
{
    if (!wgpu) {
        return YETTY_ERR(yetty_ycore_void, "wgpu is NULL");
    }

    struct yetty_yplatform_ywgpu_await_state st = {0};

    WGPUBufferMapCallbackInfo cb = {0};
    cb.mode = WGPUCallbackMode_AllowSpontaneous;
    cb.callback = map_callback;
    cb.userdata1 = &st;

    ydebug("ywebgpu: buffer_map_await buffer=%p offset=%zu size=%zu", (void *)buffer, offset, size);
    wgpuBufferMapAsync(buffer, mode, offset, size, cb);
    /* emscripten_sleep(0) yields to JS once. Asyncify pauses this stack;
     * the browser delivers the wgpu Promise, callback flips done, asyncify
     * resumes here. */
    while (!st.done) {
        emscripten_sleep(0);
    }

    if (st.status != WGPUMapAsyncStatus_Success) {
        ywarn("buffer_map_await: status=%d", st.status);
        return YETTY_ERR(yetty_ycore_void, "buffer map failed");
    }
    return YETTY_OK_VOID();
}

static void queue_done_callback(WGPUQueueWorkDoneStatus status, WGPUStringView msg, void *userdata1,
                                void *userdata2)
{
    (void)userdata2;
    struct yetty_yplatform_ywgpu_await_state *st = userdata1;
    ydebug("ywebgpu: queue_done_callback status=%d msg=\"%.*s\"", (int)status, (int)msg.length,
           msg.data ? msg.data : "");
    st->status = (int)status;
    st->done = 1;
}

struct yetty_ycore_void_result yetty_yplatform_wgpu_queue_done_await(
    struct yetty_yplatform_wgpu *wgpu, WGPUQueue queue)
{
    if (!wgpu) {
        return YETTY_ERR(yetty_ycore_void, "wgpu is NULL");
    }

    struct yetty_yplatform_ywgpu_await_state st = {0};

    WGPUQueueWorkDoneCallbackInfo cb = {0};
    cb.mode = WGPUCallbackMode_AllowSpontaneous;
    cb.callback = queue_done_callback;
    cb.userdata1 = &st;

    ydebug("ywebgpu: queue_done_await queue=%p", (void *)queue);
    wgpuQueueOnSubmittedWorkDone(queue, cb);
    while (!st.done) {
        emscripten_sleep(0);
    }

    if (st.status != WGPUQueueWorkDoneStatus_Success) {
        ywarn("queue_done_await: status=%d", st.status);
        return YETTY_ERR(yetty_ycore_void, "queue work done failed");
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yplatform_wgpu_surface_present_await(
    struct yetty_yplatform_wgpu *wgpu, WGPUSurface surface)
{
    /* On webasm, wgpuSurfacePresent is a no-op (the browser RAF page-flips
     * automatically). We don't even need to call it. Keep the symbol so the
     * desktop caller compiles unchanged. */
    (void)wgpu;
    (void)surface;
    return YETTY_OK_VOID();
}
