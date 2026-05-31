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
 * When `surface` is non-NULL, also appends the surface capabilities
 * block — the list of supported texture formats (with the platform's
 * preferred-first ordering preserved) and present modes — so callers
 * can sanity-check what the platform actually offers. NULL surface is
 * fine; the section is omitted.
 *
 * Returns a newly allocated NUL-terminated string owned by the caller
 * (free() it). Returns NULL if adapter is NULL or on allocation failure.
 */
char *yetty_ywebgpu_get_webgpu_description(WGPUAdapter adapter, WGPUSurface surface);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YWEBGPU_UTILS_H */
