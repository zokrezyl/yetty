/*
 * yplatform/ywebgpu.h - Coroutine-aware WebGPU await wrappers.
 *
 * struct yplatform_wgpu owns the per-instance state (the WGPUInstance handle,
 * a back-pointer to the event loop, the periodic ProcessEvents tick). It is
 * created once at startup and threaded explicitly to anyone that needs to
 * call an _await wrapper — there is no global state.
 *
 * Both _await wrappers MUST be called from within a coroutine. They yield
 * the coroutine until the corresponding wgpu callback fires, then resume
 * it on the event-loop thread via event_loop->ops->post_to_loop.
 *
 * Desktop drives wgpuInstanceProcessEvents from a libuv timer on the loop
 * thread (same thread as wgpuQueueSubmit). Webasm version (TBD) will have
 * no tick — emscripten's WebGPU implementation drives callbacks from the
 * JS event loop directly.
 */

#ifndef YETTY_YPLATFORM_YWEBGPU_H
#define YETTY_YPLATFORM_YWEBGPU_H

#include <stddef.h>
#include <yetty/ycore/result.h>

#include <webgpu/webgpu.h>

struct yetty_ycore_event_loop;
struct yplatform_wgpu;

YETTY_YRESULT_DECLARE(yplatform_wgpu_ptr, struct yplatform_wgpu *);

#ifdef __cplusplus
extern "C" {
#endif

/* Create the wgpu await machinery. On desktop also starts the periodic
 * ProcessEvents tick on the loop thread. */
struct yplatform_wgpu_ptr_result yplatform_wgpu_create(WGPUInstance instance,
                                                       struct yetty_ycore_event_loop *loop);

/* Destroy. Stops the tick on desktop. Handles NULL. */
void yplatform_wgpu_destroy(struct yplatform_wgpu *wgpu);

/* Yield the calling coroutine until the buffer map completes. Caller must
 * subsequently use wgpuBufferGetConstMappedRange / wgpuBufferUnmap. */
struct yetty_ycore_void_result yplatform_wgpu_buffer_map_await(struct yplatform_wgpu *wgpu,
                                                               WGPUBuffer buffer, WGPUMapMode mode,
                                                               size_t offset, size_t size);

/* Yield until all currently-submitted work on the queue has finished. */
struct yetty_ycore_void_result yplatform_wgpu_queue_done_await(struct yplatform_wgpu *wgpu,
                                                               WGPUQueue queue);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPLATFORM_YWEBGPU_H */
