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
#include <string.h>

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
    /* Browser/driver failure reason. Captured so the WARN on failure can
     * carry the actual message — with tracing off, a debug-only log of
     * the reason means it is effectively dropped. */
    char error_msg[256];
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

static void await_state_capture_message(struct yetty_yplatform_ywgpu_await_state *st,
                                        WGPUStringView msg)
{
    if (msg.data && msg.length > 0) {
        size_t len =
            msg.length < sizeof(st->error_msg) - 1 ? msg.length : sizeof(st->error_msg) - 1;
        memcpy(st->error_msg, msg.data, len);
        st->error_msg[len] = '\0';
    }
}

static void map_callback(WGPUMapAsyncStatus status, WGPUStringView msg, void *userdata1,
                         void *userdata2)
{
    (void)userdata2;
    struct yetty_yplatform_ywgpu_await_state *st = userdata1;
    ydebug("ywebgpu: map_callback status=%d msg=\"%.*s\"", (int)status, (int)msg.length,
           msg.data ? msg.data : "");
    await_state_capture_message(st, msg);
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
        ywarn("buffer_map_await: status=%d msg=%s", st.status,
              st.error_msg[0] ? st.error_msg : "(none)");
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
    await_state_capture_message(st, msg);
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
        ywarn("queue_done_await: status=%d msg=%s", st.status,
              st.error_msg[0] ? st.error_msg : "(none)");
        return YETTY_ERR(yetty_ycore_void, "queue work done failed");
    }
    return YETTY_OK_VOID();
}
