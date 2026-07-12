#ifndef YETTY_YWEBGPU_REQUEST_H
#define YETTY_YWEBGPU_REQUEST_H

#include <webgpu/webgpu.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Generic WebGPU adapter/device request callbacks suitable for use with
 * WGPURequestAdapterCallbackInfo / WGPURequestDeviceCallbackInfo.
 *
 * Conventions (matching the WebGPU C API):
 *   - userdata1: pointer to the output handle (WGPUAdapter * / WGPUDevice *)
 *   - userdata2: pointer to a struct yetty_ywebgpu_request_state — the
 *     browser/driver failure message is copied into error_msg so the
 *     caller can log the actual reason ("adapter blocklisted", …), not
 *     just the fact of the failure.
 */

struct yetty_ywebgpu_request_state {
    char error_msg[256]; /* populated on failure (NUL-terminated) */
    int ready;           /* set to 1 on completion */
};

void yetty_ywebgpu_adapter_request_callback(WGPURequestAdapterStatus status, WGPUAdapter adapter,
                                            WGPUStringView message, void *userdata1,
                                            void *userdata2);

void yetty_ywebgpu_device_request_callback(WGPURequestDeviceStatus status, WGPUDevice device,
                                           WGPUStringView message, void *userdata1,
                                           void *userdata2);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YWEBGPU_REQUEST_H */
