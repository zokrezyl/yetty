#ifndef YETTY_YSVG_YSVG_H
#define YETTY_YSVG_YSVG_H

/*
 * ysvg - render an SVG document into a ydraw buffer.
 *
 * Scope: SVG Tiny 1.2 (https://www.w3.org/TR/SVGMobile12/), focused on the
 * static-graphics subset that maps cleanly onto ydraw MSDF primitives and
 * MSDF text spans. We do NOT cover scripting, SMIL animation, the SVG DOM
 * timing model, or audio/video elements.
 *
 * Pipeline:
 *   1. XML parse: source string → ysvg_node tree (elements + attributes).
 *   2. Style cascade: presentation attributes + inline `style` →
 *      resolved struct ysvg_style per element, with inheritance.
 *   3. Geometry flatten: shape elements + <path d="..."> → ydraw SDF
 *      primitives (circle, ellipse, box, rounded_box, segment, capsule).
 *      Path data is flattened to segments after applying the inherited
 *      transform stack.
 *   4. Text: <text>/<tspan> emits MSDF TEXT_SPAN flyweights via
 *      yetty_ydraw_draw_list_add_text.
 *
 * The viewBox attribute on <svg> determines the scene bounds passed to the
 * ydraw buffer at creation time; absent a viewBox, the width/height
 * attributes are used; absent those, the config's pixel dimensions.
 *
 * The output buffer is owned by the caller (free with
 * yetty_ydraw_draw_list_destroy).
 */

#include <stddef.h>
#include <stdint.h>
#include <yetty/ycore/result.h>
#include <yetty/ydraw-core/draw-list.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ysvg_render_config {
    /* Display cell size in pixels — used to derive a default font size when
     * no font-size is given on the SVG. */
    uint32_t cell_width;
    uint32_t cell_height;
    /* Surface dimensions in cells — only used as the scene-bounds fallback
     * when the SVG carries neither viewBox nor width/height. */
    uint32_t width_cells;
    uint32_t height_cells;
};

struct yetty_ysvg_render_output {
    struct yetty_ydraw_draw_list *buffer;
    float scene_width;
    float scene_height;
};

YETTY_YRESULT_DECLARE(yetty_ysvg_render, struct yetty_ysvg_render_output);

/* Render SVG source into a fresh ydraw buffer. `content` need not be NUL
 * terminated; `content_len` is authoritative. `args` is a flag string of
 * the same shape ymarkdown uses, currently:
 *   --font-size=<float>      override default font size (default cell_height)
 *   --line-spacing=<float>   text line spacing multiplier (default 1.2)
 *   --bg=<#RRGGBB|#RRGGBBAA> background fill (default transparent)
 * Pass NULL/0 for defaults. */
struct yetty_ysvg_render_result yetty_ysvg_render(const char *content, size_t content_len,
                                                  const char *args, size_t args_len,
                                                  const struct yetty_ysvg_render_config *config);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YSVG_YSVG_H */
