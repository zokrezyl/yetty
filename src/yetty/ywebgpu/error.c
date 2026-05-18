#include <yetty/webgpu/error.h>
#include <yetty/ytrace/ytrace.h>
#include <string.h>

/* Global error state */
struct yetty_ywebgpu_error_state yetty_ywebgpu_error = {0};

void yetty_ywebgpu_error_clear(void)
{
    yetty_ywebgpu_error.has_error = 0;
    yetty_ywebgpu_error.type = WGPUErrorType_NoError;
    yetty_ywebgpu_error.message[0] = '\0';
}

int yetty_ywebgpu_error_check(void)
{
    return yetty_ywebgpu_error.has_error;
}

void yetty_ywebgpu_uncaptured_error_callback(WGPUDevice const *device, WGPUErrorType type,
                                             WGPUStringView message, void *userdata1,
                                             void *userdata2)
{
    (void)device;
    (void)userdata1;
    (void)userdata2;

    /* Store error in global state */
    yetty_ywebgpu_error.has_error = 1;
    yetty_ywebgpu_error.type = type;
    size_t len = message.length < sizeof(yetty_ywebgpu_error.message) - 1
                     ? message.length
                     : sizeof(yetty_ywebgpu_error.message) - 1;
    memcpy(yetty_ywebgpu_error.message, message.data, len);
    yetty_ywebgpu_error.message[len] = '\0';

    /* Also log it */
    const char *type_str = "Unknown";
    switch (type) {
    case WGPUErrorType_Validation:
        type_str = "Validation";
        break;
    case WGPUErrorType_OutOfMemory:
        type_str = "OutOfMemory";
        break;
    case WGPUErrorType_Internal:
        type_str = "Internal";
        break;
    case WGPUErrorType_Unknown:
        type_str = "Unknown";
        break;
    default:
        break;
    }
    yerror("WebGPU %s error: %s", type_str, yetty_ywebgpu_error.message);
}

WGPUUncapturedErrorCallbackInfo yetty_ywebgpu_get_error_callback_info(void)
{
    WGPUUncapturedErrorCallbackInfo info = {0};
    info.callback = yetty_ywebgpu_uncaptured_error_callback;
    return info;
}

void yetty_ywebgpu_device_lost_callback(WGPUDevice const *device, WGPUDeviceLostReason reason,
                                        WGPUStringView message, void *userdata1, void *userdata2)
{
    (void)device;
    (void)userdata1;
    (void)userdata2;
    const char *reason_str = "Unknown";
    switch (reason) {
    case WGPUDeviceLostReason_Unknown:
        reason_str = "Unknown";
        break;
    case WGPUDeviceLostReason_Destroyed:
        reason_str = "Destroyed";
        break;
    case WGPUDeviceLostReason_CallbackCancelled:
        reason_str = "CallbackCancelled";
        break;
    case WGPUDeviceLostReason_FailedCreation:
        reason_str = "FailedCreation";
        break;
    default:
        break;
    }
    char buf[512];
    size_t len = message.length < sizeof(buf) - 1 ? message.length : sizeof(buf) - 1;
    if (message.data && len > 0) {
        memcpy(buf, message.data, len);
    }
    buf[len] = '\0';
    yerror("WebGPU device lost (%s): %s", reason_str, buf);
}

WGPUDeviceLostCallbackInfo yetty_ywebgpu_get_device_lost_callback_info(void)
{
    WGPUDeviceLostCallbackInfo info = {0};
    info.mode = WGPUCallbackMode_AllowSpontaneous;
    info.callback = yetty_ywebgpu_device_lost_callback;
    return info;
}
