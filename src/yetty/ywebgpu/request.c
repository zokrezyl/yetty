#include <yetty/ywebgpu/request.h>

#include <string.h>

void yetty_ywebgpu_adapter_request_callback(WGPURequestAdapterStatus status, WGPUAdapter adapter,
                                            WGPUStringView message, void *userdata1,
                                            void *userdata2)
{
    (void)message;
    if (status == WGPURequestAdapterStatus_Success) {
        *((WGPUAdapter *)userdata1) = adapter;
    }
    *((int *)userdata2) = 1;
}

void yetty_ywebgpu_device_request_callback(WGPURequestDeviceStatus status, WGPUDevice device,
                                           WGPUStringView message, void *userdata1, void *userdata2)
{
    struct yetty_ywebgpu_device_request_state *state = userdata2;

    if (status == WGPURequestDeviceStatus_Success) {
        *((WGPUDevice *)userdata1) = device;
    } else if (state) {
        if (message.data && message.length > 0) {
            size_t len = message.length < 255 ? message.length : 255;
            memcpy(state->error_msg, message.data, len);
            state->error_msg[len] = '\0';
        }
    }
    if (state) {
        state->ready = 1;
    }
}
