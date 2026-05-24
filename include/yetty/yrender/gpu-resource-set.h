#ifndef YETTY_YRENDER_GPU_RESOURCE_SET_H
#define YETTY_YRENDER_GPU_RESOURCE_SET_H

#include <yetty/yrender/types.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define YETTY_YRENDER_RS_MAX_TEXTURES 4
#define YETTY_YRENDER_RS_MAX_BUFFERS 4
#define YETTY_YRENDER_RS_MAX_UNIFORMS 32
/* Bumped from 4 to 64: ydraw-layer attaches one child per canvas-owned
 * font (default + every PDF-embedded font), and a single PDF can carry
 * a couple dozen subset fonts. 64 is also well within MAX_FLAT_TEXTURES
 * (64) in the binder/pipeline. */
#define YETTY_YRENDER_RS_MAX_CHILDREN 64

/* GPU resource set - collection of resources a provider needs */
struct yetty_ydraw_gpu_resource_set {
    char namespace[YETTY_YRENDER_NAME_MAX];
    struct yetty_ycore_pixel_size pixel_size;

    struct yetty_yrender_texture textures[YETTY_YRENDER_RS_MAX_TEXTURES];
    size_t texture_count;

    struct yetty_yrender_buffer buffers[YETTY_YRENDER_RS_MAX_BUFFERS];
    size_t buffer_count;

    struct yetty_yrender_uniform uniforms[YETTY_YRENDER_RS_MAX_UNIFORMS];
    size_t uniform_count;

    struct yetty_yrender_shader_code shader;

    struct yetty_ydraw_gpu_resource_set *children[YETTY_YRENDER_RS_MAX_CHILDREN];
    size_t children_count;

    /* Per-frame draw call shape. 6 vertices = full-pane quad. instance_count
     * defaults to 1 (single full-pane draw); set higher for instanced draws
     * such as shader-glyph's per-cell rendering, where each instance covers
     * one cell read from a storage buffer indexed by @builtin(instance_index).
     * Zero means "do not draw" — useful when a layer has its cell list empty
     * for one frame. */
    uint32_t instance_count;
};

YETTY_YRESULT_DECLARE(yetty_yrender_gpu_resource_set, const struct yetty_ydraw_gpu_resource_set *);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YRENDER_GPU_RESOURCE_SET_H */
