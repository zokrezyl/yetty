#ifndef YETTY_YIMAGE_YIMAGE_H
#define YETTY_YIMAGE_YIMAGE_H

/*
 * yimage — high-level API for producing a yimage complex from
 * an image source.
 *
 * Pipeline:
 *   stb_image decode (path or in-memory PNG/JPG/...)  → RGBA8 pixels
 *   yetty_yimage_uniforms_serialize(uniforms, pixels) → wire bytes
 *   ydraw_core_buffer_add_prim(buffer)               → attach to ydraw
 *
 * The frontend tool (tools/yimage) wraps the path-based variant with a
 * CLI; yecho's `{image: ...}` block (planned) will use the bytes variant
 * so DCS payloads decode at the receiving terminal.
 *
 * Compute-shader resampling (scale-image.wgsl) is NOT wired today —
 * pixels go to the atlas at source resolution and the GPU samples with
 * hardware bilinear at the displayed size. When the compute pass is
 * added back, this layer will pre-scale before serialization.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <yetty/ycore/result.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/yimage/yimage-gen.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Geometry config. NULL fields fall back to defaults. */
struct yetty_yimage_render_config {
    float bounds_x; /* 0   — overridden by canvas at render time */
    float bounds_y; /* 0   — overridden by canvas at render time */
    float bounds_w; /* 0   — when 0, defaults to source width  */
    float bounds_h; /* 0   — when 0, defaults to source height */
};

/* Probe the source pixel dimensions of `image_bytes` (PNG/JPG/...) without
 * a full decode (stb_image header parse). Returns 0 and writes *out_w /
 * *out_h on success, -1 on failure. Useful for aspect-correct layout
 * before deciding the display bounds. */
int yetty_yimage_probe_size(const uint8_t *image_bytes, size_t len, int *out_w, int *out_h);

/* Decode `image_bytes` (PNG/JPG/...) and produce a fresh ydraw-core
 * buffer holding ONE yimage complex prim. Caller frees the buffer with
 * yetty_ydraw_drawable_list_destroy. */
struct yetty_ydraw_drawable_list_result yetty_yimage_render(
    const uint8_t *image_bytes, size_t len, const struct yetty_yimage_render_config *config);

/* Decode `image_bytes` and append ONE yimage complex prim into an existing
 * `buf` at the config's bounds — the in-place counterpart of yetty_yimage_render
 * for producers that composite the image into a larger drawable_list (e.g. a
 * ydoc inline image drawn into the document buffer). */
struct yetty_ycore_void_result yetty_yimage_emit_into(
    struct yetty_ydraw_drawable_list *buf, const uint8_t *image_bytes, size_t len,
    const struct yetty_yimage_render_config *config);

/* Convenience: read the file at `path` and call yetty_yimage_render. */
struct yetty_ydraw_drawable_list_result yetty_yimage_render_path(
    const char *path, const struct yetty_yimage_render_config *config);

/* DCS envelope (YETTY_DCS_YDRAW_BIN, same wire format as ycat / yecho).
 * Returns bytes written; ERR on failure. */
struct yetty_ycore_size_result yetty_yimage_dcs_bin_emit(
    const struct yetty_ydraw_drawable_list *buffer, FILE *out);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YIMAGE_YIMAGE_H */
