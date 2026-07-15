#ifndef YETTY_YMESH_YMESH_LOAD_H
#define YETTY_YMESH_YMESH_LOAD_H

/*
 * ymesh-load — multi-format mesh loading front door. Sniffs the byte
 * content (no path needed) and produces the same triangle-soup structure
 * the GLB loader emits, so every downstream path (wire serialize, tool,
 * ycat) is format-agnostic.
 *
 * Formats:
 *   .glb  glTF 2.0 binary        (cgltf — see ymesh-glb.c)
 *   .ply  ascii + binary_little_endian; positions, optional normals,
 *         optional faces. A vertex-only PLY renders as a POINT CLOUD:
 *         each point becomes a small octahedron marker sized from the
 *         cloud's bounding box.
 *   .stl  binary + ascii (flat-shaded, vertices duplicated per facet)
 *   .obj  v / vn / f (fan triangulation; v, v//vn, v/vt/vn index forms;
 *         negative indices)
 *
 * Missing normals are computed (smooth, area-weighted). Per-vertex PLY
 * colors are parsed past but not yet carried to the GPU (the ymesh wire
 * has no color attribute yet).
 */

#include <yetty/ymesh/ymesh-glb.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ymesh_glb_data_result yetty_ymesh_load(const uint8_t *bytes, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YMESH_YMESH_LOAD_H */
