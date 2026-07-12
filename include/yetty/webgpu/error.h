#ifndef YETTY_YWEBGPU_ERROR_H
#define YETTY_YWEBGPU_ERROR_H

#include <webgpu/webgpu.h>
#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Error state - cleared before WebGPU call, checked after */
struct yetty_ywebgpu_error_state {
    int has_error;
    WGPUErrorType type;
    char message[512];
};

/* Global error state - use this with the callback */
extern struct yetty_ywebgpu_error_state yetty_ywebgpu_error;

/* Clear error state before a WebGPU call */
void yetty_ywebgpu_error_clear(void);

/* Check if error occurred - returns 1 if error, 0 if ok */
int yetty_ywebgpu_error_check(void);

/* Uncaptured error callback - stores error in yetty_ywebgpu_error */
void yetty_ywebgpu_uncaptured_error_callback(WGPUDevice const *device, WGPUErrorType type,
                                             WGPUStringView message, void *userdata1,
                                             void *userdata2);

/* Get callback info struct ready to use in device descriptor */
WGPUUncapturedErrorCallbackInfo yetty_ywebgpu_get_error_callback_info(void);

/*
 * Captured-error scope — for WebGPU calls fed by UNTRUSTED input (e.g. a
 * user-supplied WGSL shader arriving over the wire). Errors raised inside
 * the scope are captured and reported to the caller as data instead of
 * reaching the uncaptured-error callback, whose fail-fast _Exit would let
 * a malformed shader kill the whole terminal.
 *
 * Usage:
 *   yetty_ywebgpu_error_scope_push(device);
 *   ... wgpu calls validating untrusted input ...
 *   struct yetty_ycore_void_result pop_res =
 *       yetty_ywebgpu_error_scope_pop(instance, device);
 *   YETTY_RETURN_IF_ERR(yetty_ycore_void, pop_res, "shader validation");
 *
 * Captures validation errors (filter = Validation). On Dawn native the
 * pop callback runs spontaneously inside wgpuDevicePopErrorScope — the
 * scope result for synchronously-validated calls is already resolved by
 * then. On the browser backend resolution needs the event loop, so pop
 * blocks on the scope future via wgpuInstanceWaitAny (Asyncify-backed;
 * the instance must be created with WGPUInstanceFeatureName_TimedWaitAny)
 * — a real validation failure surfaces as a Result on every platform
 * instead of being demoted to an async log line. The full validation
 * message is logged via yerror; the Result carries a static summary.
 */
void yetty_ywebgpu_error_scope_push(WGPUDevice device);

struct yetty_ycore_void_result yetty_ywebgpu_error_scope_pop(WGPUInstance instance,
                                                             WGPUDevice device);

/* Assert that no uncaptured wgpu error fired during the preceding call.
 * Logs a short tag identifying the call site, exits to stderr on first
 * hit. Use immediately after every wgpu API call we care about — the
 * point is to localise a Dawn validation / OOM / device-lost to the
 * exact wgpu call that triggered it, not the next call that happens to
 * touch the bad state. */
#include <stdio.h>
#include <stdlib.h>
#define YETTY_WGPU_CHECK(tag)                                                                      \
    do {                                                                                           \
        if (yetty_ywebgpu_error.has_error) {                                                       \
            fprintf(stderr, "\n[FATAL] wgpu error at " tag ": %s\n", yetty_ywebgpu_error.message); \
            fflush(stderr);                                                                        \
            _Exit(2);                                                                              \
        }                                                                                          \
    } while (0)

/* Device-lost callback — fires when Dawn invalidates the device (e.g. GPU
 * hang, validation failure that escalated to device-loss). Plumbed in so the
 * Wayland-direct-surface + multi-instance freeze does not just look like a
 * silent stall. */
void yetty_ywebgpu_device_lost_callback(WGPUDevice const *device, WGPUDeviceLostReason reason,
                                        WGPUStringView message, void *userdata1, void *userdata2);

WGPUDeviceLostCallbackInfo yetty_ywebgpu_get_device_lost_callback_info(void);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YWEBGPU_ERROR_H */
