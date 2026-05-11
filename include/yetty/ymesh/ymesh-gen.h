#ifndef YETTY_YMESH_YMESH_GEN_H
#define YETTY_YMESH_YMESH_GEN_H

/*
 * ymesh internal types — type_id, uniforms struct, factory create/destroy.
 *
 * Mirrors the role of yimage-gen.h. Unlike yimage, the WGSL pipeline here
 * uses raw WebGPU (custom vertex layout + depth) so there is no schema-
 * driven serializer; serialization is hand-written in ymesh.c.
 */

#include <stddef.h>
#include <stdint.h>
#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward-declared so this header stays GPU-less and can be included by
 * client-side wire emitters that don't link Dawn. The full type lives in
 * yetty/ypaint-factory/complex-prim-factory.h (server side). */
struct yetty_ypaint_core_concrete_factory;

#define YETTY_YMESH_TYPE_ID 0x80000005u

/* Public-facing config-style uniforms. The GPU-side derives MVP from the
 * bbox + camera fields at instance create time (and re-derives them when
 * the wire bytes change in the interactive viewer). */
struct yetty_ymesh_uniforms {
    float bounds_x;
    float bounds_y;
    float bounds_w;
    float bounds_h;
    float bbox_min[3];
    float bbox_max[3];

    /* Camera + render mode. See ymesh.h for semantics + defaults. */
    float azimuth;
    float elevation;
    float dist_factor;
    float pan_x;
    float pan_y;
    uint32_t mode;
};

/* Render mode values for yetty_ymesh_uniforms.mode. */
#define YETTY_YMESH_MODE_SOLID 0u
#define YETTY_YMESH_MODE_WIREFRAME 1u

/* Factory create / destroy — registered with the abstract complex-prim
 * factory in ypaint-canvas.c. */
struct yetty_ypaint_core_concrete_factory *yetty_ymesh_factory_create(void);
void yetty_ymesh_factory_destroy(struct yetty_ypaint_core_concrete_factory *factory);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YMESH_YMESH_GEN_H */
