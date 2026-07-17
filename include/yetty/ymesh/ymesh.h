#ifndef YETTY_YMESH_YMESH_H
#define YETTY_YMESH_YMESH_H

/*
 * ymesh — high-level API for producing a ymesh composite from a
 * glTF 2.0 (.glb) byte buffer or path.
 *
 * Pipeline:
 *   cgltf_parse + cgltf_load_buffers (path or in-memory .glb)
 *       → host-side mesh (positions, normals, indices, bbox)
 *   yetty_ymesh_serialize_prim(uniforms, mesh) → wire bytes
 *   yetty_ydraw_drawable_list_add_prim(buffer)  → attach to ydraw
 *
 * Wire format (all words u32 unless marked):
 *   [type_id u32][payload_size u32]
 *   [bounds_x f32][bounds_y f32][bounds_w f32][bounds_h f32]
 *   [bbox_min_x/y/z f32 ×3][bbox_max_x/y/z f32 ×3]
 *   [azimuth f32][elevation f32][dist_factor f32]    # camera orbit + dolly
 *   [pan_x f32][pan_y f32][mode u32]                 # screen-space pan + render-mode
 *   [vertex_count u32][index_count u32][index_size u32]
 *   [positions f32 × vertex_count×3]
 *   [normals   f32 × vertex_count×3]
 *   [indices   u32 × index_count]
 *
 * Camera semantics:
 *   azimuth, elevation : radians around bbox-centered orbit (Y-up).
 *   dist_factor        : camera distance = bbox_radius * dist_factor.
 *   pan_x, pan_y       : screen-space camera offset in pixels (post-projection).
 *   mode               : 0=solid (Lambert), 1=wireframe (line list).
 *
 * Mesh scope: positions + normals + indices, single mesh / first primitive.
 * No textures / UVs / materials yet.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <yetty/ycore/result.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/ymesh/ymesh-gen.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ymesh_render_config {
    float bounds_x; /* overridden by canvas at render time */
    float bounds_y; /* overridden by canvas at render time */
    float bounds_w; /* when 0, defaults to a fixed display size */
    float bounds_h;

    /* Camera state. Defaults applied when fields are zero (azimuth/elev/pan)
     * or non-positive (dist_factor): azimuth=0.7rad, elevation=0.5rad,
     * dist_factor=3.0, pan=(0,0). mode 0=solid, 1=wireframe. */
    float azimuth;
    float elevation;
    float dist_factor;
    float pan_x;
    float pan_y;
    uint32_t mode;
};

/* Decode `glb_bytes` and produce a fresh ydraw-core buffer holding ONE
 * ymesh complex prim. Caller frees with yetty_ydraw_drawable_list_destroy. */
struct yetty_ydraw_drawable_list_result yetty_ymesh_render(
    const uint8_t *glb_bytes, size_t len, const struct yetty_ymesh_render_config *config);

/* Convenience: read the file at `path` and call yetty_ymesh_render. */
struct yetty_ydraw_drawable_list_result yetty_ymesh_render_path(
    const char *path, const struct yetty_ymesh_render_config *config);

/* DCS envelope (YETTY_DCS_YDRAW_BIN, same wire format as yimage). */
struct yetty_ycore_size_result yetty_ymesh_dcs_bin_emit(
    const struct yetty_ydraw_drawable_list *buffer, FILE *out);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YMESH_YMESH_H */
