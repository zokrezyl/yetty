/* buffer — module-internal declarations (ydraw-core only).
 *
 * These functions are not part of the inter-module public API. The public
 * surface lives in include/yetty/ydraw-core/buffer.h.
 *
 * Most of these are the base64 round-trip pair (used internally to fuse
 * compression+encoding without double-buffering) and the matched min-side
 * scene-bounds accessors that nothing outside ydraw-core currently needs.
 */
#ifndef YETTY_YDRAW_CORE_BUFFER_INTERNAL_H
#define YETTY_YDRAW_CORE_BUFFER_INTERNAL_H

#include <stddef.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw-core/buffer.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ydraw_core_buffer_result yetty_ydraw_core_buffer_create_from_base64(
    const struct yetty_ycore_buffer *base64_buf);

/* Base64-encode the buffer's raw primitive bytes. Allocates the output —
 * caller owns result.value.data and must free() it. Symmetric inverse of
 * yetty_ydraw_core_buffer_create_from_base64. */
struct yetty_ycore_buffer_result yetty_ydraw_core_buffer_to_base64(
    const struct yetty_ydraw_core_buffer *buf);

float yetty_ydraw_core_buffer_scene_min_x(const struct yetty_ydraw_core_buffer *buf);
float yetty_ydraw_core_buffer_scene_min_y(const struct yetty_ydraw_core_buffer *buf);

/* Read-only view into the primitives payload. NULL on invalid buf. */
const struct yetty_ycore_buffer *yetty_ydraw_core_buffer_primitives(
    const struct yetty_ydraw_core_buffer *buf);

/* yetty_ydraw_core_buffer_data / _size are now part of the public surface
 * (see include/yetty/ydraw-core/buffer.h). They are intentionally not
 * re-declared here. */

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YDRAW_CORE_BUFFER_INTERNAL_H */
