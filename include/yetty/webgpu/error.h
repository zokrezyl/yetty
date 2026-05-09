#ifndef YETTY_YWEBGPU_ERROR_H
#define YETTY_YWEBGPU_ERROR_H

#include <webgpu/webgpu.h>

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

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YWEBGPU_ERROR_H */
