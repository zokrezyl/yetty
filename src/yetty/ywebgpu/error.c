#include <yetty/webgpu/error.h>
#include <yetty/ytrace/ytrace.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
/* Coroutine query for the pop-scope wait guard — emscripten fibers
 * cannot Asyncify-sleep, so a pop inside a coroutine must not block. */
#include <yetty/yplatform/ycoroutine.h>
#endif

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
    /* Set when the pop call returned before the callback fired (browser
     * backend: the scope resolves on a later event-loop tick). The callback
     * then owns the heap capture: it logs any error and frees it. */
    int detached;
    int has_error;
    char message[512];
};

static void error_scope_pop_callback(WGPUPopErrorScopeStatus status, WGPUErrorType type,
                                     WGPUStringView message, void *userdata1, void *userdata2)
{
    (void)userdata2;
    struct yetty_ywebgpu_error_scope_capture *capture = userdata1;
    if (!capture) {
        return;
    }
    capture->completed = 1;
    if (status == WGPUPopErrorScopeStatus_Success && type != WGPUErrorType_NoError) {
        capture->has_error = 1;
        size_t len = message.length < sizeof(capture->message) - 1 ? message.length
                                                                   : sizeof(capture->message) - 1;
        if (message.data && len > 0) {
            memcpy(capture->message, message.data, len);
        }
        capture->message[len] = '\0';
    }
    if (capture->detached) {
        /* The caller has long returned — the error can no longer surface
         * as a Result. Log it here so it is visible instead of lost. */
        if (capture->has_error) {
            yerror("WebGPU captured validation error (async, after pop returned): %s",
                   capture->message);
        }
        free(capture);
    }
}

void yetty_ywebgpu_error_scope_push(WGPUDevice device)
{
    wgpuDevicePushErrorScope(device, WGPUErrorFilter_Validation);
}

struct yetty_ycore_void_result yetty_ywebgpu_error_scope_pop(WGPUInstance instance,
                                                             WGPUDevice device)
{
    /* Heap-allocated: on the browser backend the callback fires from a later
     * event-loop tick, after this frame is gone — a stack capture would be a
     * dangling write. */
    struct yetty_ywebgpu_error_scope_capture *capture =
        calloc(1, sizeof(struct yetty_ywebgpu_error_scope_capture));
    WGPUPopErrorScopeCallbackInfo info = {0};
    info.mode = WGPUCallbackMode_AllowSpontaneous;
    info.callback = error_scope_pop_callback;
    info.userdata1 = capture;
    WGPUFuture future = wgpuDevicePopErrorScope(device, info);
    if (!capture) {
        /* Allocation failed; the scope is still balanced, the error (if
         * any) is dropped by the NULL-guard in the callback. The
         * validation outcome is unknowable — report that instead of
         * claiming success. */
        return YETTY_ERR(yetty_ycore_void, "PopErrorScope capture allocation failed");
    }
#ifdef __EMSCRIPTEN__
    /* The browser backend resolves the scope only from a later event-loop
     * tick — block on the future via wgpuInstanceWaitAny, which the
     * emdawnwebgpu port implements as an Asyncify suspend (this build is
     * asyncify-instrumented). Two guards: the instance must be available,
     * and we must not be inside a stackful coroutine — emscripten fibers
     * cannot Asyncify-sleep. */
    if (!capture->completed && instance && !yetty_yplatform_coro_current()) {
        enum { POP_SCOPE_WAIT_SECONDS = 5 };
        WGPUFutureWaitInfo wait = {.future = future};
        WGPUWaitStatus wait_status = wgpuInstanceWaitAny(
            instance, 1, &wait, (uint64_t)POP_SCOPE_WAIT_SECONDS * 1000000000ull);
        if (wait_status != WGPUWaitStatus_Success) {
            ywarn("ywebgpu: PopErrorScope WaitAny failed (status %d) — was the "
                  "instance created with WGPUInstanceFeatureName_TimedWaitAny?",
                  (int)wait_status);
        }
    }
#else
    (void)instance;
    (void)future;
#endif
    if (!capture->completed) {
        /* Dawn native resolves synchronously-validated scopes inline and
         * the browser backend was waited on above — reaching this is
         * exceptional (WaitAny unavailable, timed out, or a pop inside a
         * coroutine). Detach: the callback will log any error when it
         * eventually fires, and frees the capture. */
        capture->detached = 1;
        ywarn("ywebgpu: PopErrorScope did not complete inline; "
              "captured error (if any) will be logged asynchronously");
        return YETTY_OK_VOID();
    }
    int has_error = capture->has_error;
    if (has_error) {
        /* Full driver message to the log; the Result carries a static
         * summary (Result messages are pointers, never copies — a stack
         * or freed buffer would dangle). */
        yerror("WebGPU captured validation error: %s", capture->message);
    }
    free(capture);
    if (has_error) {
        return YETTY_ERR(yetty_ycore_void, "WebGPU validation error (full message in log)");
    }
    return YETTY_OK_VOID();
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
