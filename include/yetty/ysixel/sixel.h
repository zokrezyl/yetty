#ifndef YETTY_YSIXEL_SIXEL_H
#define YETTY_YSIXEL_SIXEL_H

/*
 * ysixel — decode a DCS sixel payload into RGBA8 and package it as a yimage
 * complex prim, so legacy tools (img2sixel, chafa, lsix, gnuplot, timg, mpv)
 * present through the exact same drawable-list / figure path as a native
 * yimage. GPU-less leaf: no pipeline of its own — the yimage factory renders
 * the prim it produces.
 *
 * Pipeline:
 *   DCS <params> q <data> ST  →  (this module decodes the <data> bytes)  →
 *   RGBA8 pixels  →  yetty_yimage_uniforms_serialize  →  one yimage prim in a
 *   fresh ydraw drawable list.
 */

#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>
#include <yetty/ydraw-core/drawable-list.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Geometry config. `max_width_px` > 0 downscales (aspect-preserving) when the
 * decoded image is wider than the pane; 0 keeps native size. */
struct yetty_ysixel_render_config {
    float max_width_px;
};

/* Decoded RGBA8 image. `rgba` is width*height*4 bytes, row-major, heap-owned. */
struct yetty_ysixel_image {
    uint32_t width;
    uint32_t height;
    uint8_t *rgba;
};
YETTY_YRESULT_DECLARE(yetty_ysixel_image, struct yetty_ysixel_image);

/* Decode the sixel data payload — the bytes between the DCS `q` command and the
 * ST terminator (introducer, params and terminator excluded). Returns an owned
 * image; free it with yetty_ysixel_image_destroy. Empty output is an error;
 * pathological dimensions are capped so malformed input never exhausts memory. */
struct yetty_ysixel_image_result yetty_ysixel_decode(const char *data, size_t len);

/* Free the pixels owned by a decoded image. Safe on a zeroed image. */
void yetty_ysixel_image_destroy(struct yetty_ysixel_image *image);

/* Decode `data` and produce a fresh ydraw drawable list holding ONE yimage
 * prim, ready for the terminal's rich-content ingest. Caller frees the list
 * with yetty_ydraw_drawable_list_destroy. */
struct yetty_ydraw_drawable_list_result yetty_ysixel_render(
    const char *data, size_t len, const struct yetty_ysixel_render_config *config);

#ifdef __cplusplus
}
#endif

#endif
