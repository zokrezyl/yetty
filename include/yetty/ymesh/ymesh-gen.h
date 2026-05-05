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
#include <yetty/ypaint-core/complex-prim-types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define YETTY_YMESH_TYPE_ID 0x80000005u

/* Public-facing config-style uniforms (the GPU-side has more — model/view/
 * proj matrices are derived from the mesh's bbox at create_instance time). */
struct yetty_ymesh_uniforms {
    float bounds_x;
    float bounds_y;
    float bounds_w;
    float bounds_h;
    float bbox_min[3];
    float bbox_max[3];
};

/* Factory create / destroy — registered with the abstract complex-prim
 * factory in ypaint-canvas.c. */
struct yetty_ypaint_core_concrete_factory *yetty_ymesh_factory_create(void);
void yetty_ymesh_factory_destroy(struct yetty_ypaint_core_concrete_factory *factory);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YMESH_YMESH_GEN_H */
