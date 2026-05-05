#ifndef YETTY_YMESH_GLB_H
#define YETTY_YMESH_GLB_H

/*
 * ymesh-glb — minimal cgltf wrapper.
 *
 * Parses a .glb (or .gltf) byte buffer and extracts the FIRST mesh's FIRST
 * primitive's POSITION + NORMAL attributes plus its index buffer. The
 * returned host-side mesh holds owned heap-allocated arrays — caller must
 * call yetty_ymesh_glb_destroy() to release them.
 *
 * MVP scope: positions + normals + indices only. Materials, textures, UVs,
 * skinning, animations, and multi-primitive scenes are ignored.
 */

#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ymesh_glb_data {
    /* Vertex attribute streams. positions and normals are interleaved
     * by ATTRIBUTE (positions[i*3 + {0,1,2}] = x,y,z), one float triple
     * per vertex. */
    float *positions;       /* vertex_count × 3 floats, owned */
    float *normals;         /* vertex_count × 3 floats, owned */
    size_t vertex_count;

    /* Index buffer. index_size is 2 (uint16) or 4 (uint32). */
    void *indices;          /* index_count × index_size bytes, owned */
    size_t index_count;
    uint32_t index_size;    /* 2 or 4 */

    /* Axis-aligned bounding box (world / model-space, post-cgltf). */
    float bbox_min[3];
    float bbox_max[3];
};

YETTY_YRESULT_DECLARE(yetty_ymesh_glb_data, struct yetty_ymesh_glb_data);

/* Parse the .glb byte buffer. The bytes do not need to outlive the call —
 * all output is heap-copied into the returned struct. Caller frees with
 * yetty_ymesh_glb_destroy(). */
struct yetty_ymesh_glb_data_result yetty_ymesh_glb_parse(
    const uint8_t *bytes, size_t len);

void yetty_ymesh_glb_destroy(struct yetty_ymesh_glb_data *data);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YMESH_GLB_H */
