#ifndef YETTY_YRENDER_BLENDER_H
#define YETTY_YRENDER_BLENDER_H

#include <stddef.h>
#include <yetty/ycore/result.h>
#include <yetty/yrender/render-target.h>
#include <yetty/yrender/rendered-layer.h>
#include <webgpu/webgpu.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yrender_blender;

/* Result type */
YETTY_YRESULT_DECLARE(yetty_yrender_blender, struct yetty_yrender_blender *);

/* Blender ops */
struct yetty_yrender_blender_ops {
    void (*destroy)(struct yetty_yrender_blender *self);

    /* Blend rendered layers to target */
    struct yetty_ycore_void_result (*blend)(struct yetty_yrender_blender *self,
                                            struct yetty_yrender_rendered_layer **layers,
                                            size_t layer_count);

    /* Set/replace render target (takes ownership, destroys old target) */
    void (*set_target)(struct yetty_yrender_blender *self, struct yetty_yrender_target *target);

    /* Get current target (not owned by caller) */
    struct yetty_yrender_target *(*get_target)(const struct yetty_yrender_blender *self);
};

/* Blender base */
struct yetty_yrender_blender {
    const struct yetty_yrender_blender_ops *ops;
};

/* Create blender with initial target (takes ownership of target) */
struct yetty_yrender_blender_result yetty_yrender_blender_create(
    WGPUDevice device, WGPUQueue queue, struct yetty_yrender_target *target,
    const char *shader_path);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YRENDER_BLENDER_H */
