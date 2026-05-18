/*
 * yplatform/ywebgpu.h - WebGPU await wrappers.
 *
 * struct yplatform_wgpu owns the per-instance state. Created once at
 * startup and threaded explicitly to anyone that needs to call an _await
 * wrapper — there is no global state.
 *
 * Two backends behind the same API:
 *   - desktop (yplatform/shared/ywebgpu.c): yields a coroutine, drives
 *     wgpuInstanceProcessEvents from a libuv timer on the loop thread,
 *     bounces the wgpu callback back via event_loop->ops->post_to_loop.
 *     Both _await wrappers MUST be called from within a coroutine.
 *   - webasm (yplatform/webasm/ywebgpu.c): suspends the C call stack via
 *     Asyncify (emscripten_sleep) until the wgpu callback fires. No
 *     coroutine, no ProcessEvents tick — the JS event loop pumps wgpu
 *     callbacks for free. Caller need not be inside a coroutine; the
 *     coroutine API on webasm is a degenerate stub (see
 *     yplatform/webasm/ycoroutine.c).
 */

#ifndef YETTY_YPLATFORM_YWEBGPU_H
#define YETTY_YPLATFORM_YWEBGPU_H

#include <stddef.h>
#include <yetty/ycore/result.h>

#include <webgpu/webgpu.h>

struct yetty_yevent_event_loop;
struct yetty_yplatform_wgpu;

YETTY_YRESULT_DECLARE(yplatform_wgpu_ptr, struct yetty_yplatform_wgpu *);

#ifdef __cplusplus
extern "C" {
#endif

/* Create the wgpu await machinery. Desktop registers a periodic
 * ProcessEvents tick on the loop thread; webasm has nothing to set up
 * (loop is kept in the signature only to match desktop). */
struct yplatform_wgpu_ptr_result yetty_yplatform_wgpu_create(WGPUInstance instance,
                                                             struct yetty_yevent_event_loop *loop);

/* Destroy. Stops the desktop tick. Handles NULL. */
void yetty_yplatform_wgpu_destroy(struct yetty_yplatform_wgpu *wgpu);

/* Block until the buffer map completes. Desktop yields the calling
 * coroutine; webasm asyncify-suspends the C stack. Caller must
 * subsequently use wgpuBufferGetConstMappedRange / wgpuBufferUnmap. */
struct yetty_ycore_void_result yetty_yplatform_wgpu_buffer_map_await(
    struct yetty_yplatform_wgpu *wgpu, WGPUBuffer buffer, WGPUMapMode mode, size_t offset,
    size_t size);

/* Block until all currently-submitted work on the queue has finished. */
struct yetty_ycore_void_result yetty_yplatform_wgpu_queue_done_await(
    struct yetty_yplatform_wgpu *wgpu, WGPUQueue queue);

/* Run wgpuSurfacePresent on the wgpu-wait pool. wgpuSurfacePresent
 * blocks under Wayland (and others) when the previous frame's GPU work
 * hasn't completed — keeping it on the loop thread freezes the entire
 * event loop (input, RPC, anim ticks). This variant pushes the call to
 * a worker, yields the caller coroutine, and resumes it on the loop
 * thread after Present returns. Must be called from inside a coroutine. */
struct yetty_ycore_void_result yetty_yplatform_wgpu_surface_present_await(
    struct yetty_yplatform_wgpu *wgpu, WGPUSurface surface);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPLATFORM_YWEBGPU_H */
