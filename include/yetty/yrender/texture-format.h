#ifndef YETTY_YRENDER_TEXTURE_FORMAT_H
#define YETTY_YRENDER_TEXTURE_FORMAT_H

/*
 * Pure-integer mirrors of selected WGPUTextureFormat / WGPUFilterMode values.
 *
 * Why this header exists:
 *   yetty_yrender_texture.format and .sampler_filter are uint32_t (see
 *   yetty/yrender/types.h). Authors of CPU-side, GPU-less code (yfont's
 *   FreeType rasterization, generate.py output, riscv64 client tools, ...)
 *   need to populate these fields without dragging in <webgpu/webgpu.h>.
 *
 * Contract:
 *   The numeric values MUST match the WGPU enum exactly. Server-side code
 *   that uploads to the GPU casts these straight back to WGPUTextureFormat /
 *   WGPUFilterMode at upload time, so any drift would silently mis-format
 *   atlas textures.
 *
 *   Source: Dawn's webgpu/webgpu.h — verify with `grep
 *   'R8Unorm\|RGBA8Unorm\|FilterMode_' build-tools/...` if you suspect drift.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum yetty_yrender_texture_format {
    YETTY_YRENDER_TEXTURE_FORMAT_R8_UNORM = 0x01u,    /* WGPUTextureFormat_R8Unorm */
    YETTY_YRENDER_TEXTURE_FORMAT_RGBA8_UNORM = 0x12u, /* WGPUTextureFormat_RGBA8Unorm */
};

enum yetty_yrender_filter_mode {
    YETTY_YRENDER_FILTER_NEAREST = 0u, /* WGPUFilterMode_Nearest */
    YETTY_YRENDER_FILTER_LINEAR = 1u,  /* WGPUFilterMode_Linear */
};

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YRENDER_TEXTURE_FORMAT_H */
