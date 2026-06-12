#include <yetty/webgpu/error.h>
#include <yetty/ytrace/ytrace.h>
#include <stdio.h>
#include <stdlib.h>
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

    /* Fail fast on uncaptured wgpu errors. The user asked for an
     * immediate exit with a clear stderr message — the alternative
     * (carry on with the error state set) tends to mask the original
     * fault behind a chain of secondary failures that's painful to
     * unpick later. Comment out if you need to keep running through
     * validation hits while iterating. */
    fprintf(stderr,
            "\n[FATAL] WebGPU %s error: %s\n"
            "        See yetty trace log for the call sequence leading up.\n"
            "        Exiting.\n",
            type_str, yetty_ywebgpu_error.message);
    fflush(stderr);
    _Exit(2);
}

WGPUUncapturedErrorCallbackInfo yetty_ywebgpu_get_error_callback_info(void)
{
    WGPUUncapturedErrorCallbackInfo info = {0};
    info.callback = yetty_ywebgpu_uncaptured_error_callback;
    return info;
}

/*=============================================================================
 * Captured-error scope — see error.h. Validation errors raised between
 * push and pop never reach the fail-fast uncaptured callback above.
 *===========================================================================*/

struct yetty_ywebgpu_error_scope_capture {
    int completed;
    int has_error;
    char message[512];
};

static void error_scope_pop_callback(WGPUPopErrorScopeStatus status, WGPUErrorType type,
                                     WGPUStringView message, void *userdata1, void *userdata2)
{
    (void)userdata2;
    struct yetty_ywebgpu_error_scope_capture *capture = userdata1;
    capture->completed = 1;
    if (status != WGPUPopErrorScopeStatus_Success || type == WGPUErrorType_NoError) {
        return;
    }
    capture->has_error = 1;
    size_t len = message.length < sizeof(capture->message) - 1 ? message.length
                                                               : sizeof(capture->message) - 1;
    if (message.data && len > 0) {
        memcpy(capture->message, message.data, len);
    }
    capture->message[len] = '\0';
}

void yetty_ywebgpu_error_scope_push(WGPUDevice device)
{
    wgpuDevicePushErrorScope(device, WGPUErrorFilter_Validation);
}

int yetty_ywebgpu_error_scope_pop(WGPUDevice device, char *message_out, size_t message_capacity)
{
    struct yetty_ywebgpu_error_scope_capture capture = {0};
    WGPUPopErrorScopeCallbackInfo info = {0};
    info.mode = WGPUCallbackMode_AllowSpontaneous;
    info.callback = error_scope_pop_callback;
    info.userdata1 = &capture;
    (void)wgpuDevicePopErrorScope(device, info);
    if (!capture.completed) {
        /* Should not happen on Dawn native: synchronously-validated calls
         * have the scope result resolved before pop returns, and
         * AllowSpontaneous fires the callback inline. Log so a backend
         * where this assumption breaks is visible. */
        ywarn("ywebgpu: PopErrorScope did not complete inline; "
              "captured error (if any) is lost");
        return 0;
    }
    if (!capture.has_error) {
        return 0;
    }
    yerror("WebGPU captured validation error: %s", capture.message);
    if (message_out && message_capacity > 0) {
        size_t len = strlen(capture.message);
        if (len > message_capacity - 1) {
            len = message_capacity - 1;
        }
        memcpy(message_out, capture.message, len);
        message_out[len] = '\0';
    }
    return 1;
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

    /* Destroyed = normal teardown when we drop our device handle, not a
     * real fault — let the cleanup path complete. Anything else (GPU
     * hung at runtime, driver crash, hot-unplug, …) we exit immediately
     * so the failure mode is visible instead of cascading. */
    if (reason == WGPUDeviceLostReason_Destroyed) {
        ydebug("WebGPU device lost (%s): %s", reason_str, buf);
    } else {
        yerror("WebGPU device lost (%s): %s", reason_str, buf);
        fprintf(stderr,
                "\n[FATAL] WebGPU device lost (%s): %s\n"
                "        This usually means a GPU hang — check ytrace log for the\n"
                "        Submit/Present that preceded it.\n"
                "        Exiting.\n",
                reason_str, buf);
        fflush(stderr);
        _Exit(3);
    }
}

WGPUDeviceLostCallbackInfo yetty_ywebgpu_get_device_lost_callback_info(void)
{
    WGPUDeviceLostCallbackInfo info = {0};
    info.mode = WGPUCallbackMode_AllowSpontaneous;
    info.callback = yetty_ywebgpu_device_lost_callback;
    return info;
}
