#include <yetty/ywebgpu/request.h>

#include <string.h>

/* Copies the request failure message (if any) into the caller's state so
 * the actual browser/driver reason survives to the log — dropping it here
 * is what used to reduce "WebGPU is blocklisted on this GPU" to a generic
 * "adapter request failed". */
static void request_state_capture_failure(struct yetty_ywebgpu_request_state *state,
                                          WGPUStringView message)
{
    if (!state) {
        return;
    }
    if (message.data && message.length > 0) {
        size_t len = message.length < sizeof(state->error_msg) - 1 ? message.length
                                                                   : sizeof(state->error_msg) - 1;
        memcpy(state->error_msg, message.data, len);
        state->error_msg[len] = '\0';
    }
}

void yetty_ywebgpu_adapter_request_callback(WGPURequestAdapterStatus status, WGPUAdapter adapter,
                                            WGPUStringView message, void *userdata1,
                                            void *userdata2)
{
    struct yetty_ywebgpu_request_state *state = userdata2;

    if (status == WGPURequestAdapterStatus_Success) {
        *((WGPUAdapter *)userdata1) = adapter;
    } else {
        request_state_capture_failure(state, message);
    }
    if (state) {
        state->ready = 1;
    }
}

void yetty_ywebgpu_device_request_callback(WGPURequestDeviceStatus status, WGPUDevice device,
                                           WGPUStringView message, void *userdata1, void *userdata2)
{
    struct yetty_ywebgpu_request_state *state = userdata2;

    if (status == WGPURequestDeviceStatus_Success) {
        *((WGPUDevice *)userdata1) = device;
    } else {
        request_state_capture_failure(state, message);
    }
    if (state) {
        state->ready = 1;
    }
}
