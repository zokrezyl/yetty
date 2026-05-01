#ifndef YETTY_YWEBGPU_UTILS_H
#define YETTY_YWEBGPU_UTILS_H

#include <webgpu/webgpu.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Build a human-readable, multi-line description of the WebGPU adapter
 * (backend, vendor, device, architecture, description, key limits).
 *
 * Returns a newly allocated NUL-terminated string owned by the caller
 * (free() it). Returns NULL if adapter is NULL or on allocation failure.
 */
char *yetty_ywebgpu_get_webgpu_description(WGPUAdapter adapter);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YWEBGPU_UTILS_H */
