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
