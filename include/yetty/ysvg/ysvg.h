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
 *   4. Text: <text>/<tspan> emits MSDF TEXT_DRAWABLE_LIST drawable-list entries via
 *      yetty_ydraw_drawable_list_add_text.
 *
 * The viewBox attribute on <svg> determines the scene bounds passed to the
 * ydraw buffer at creation time; absent a viewBox, the width/height
 * attributes are used; absent those, the config's pixel dimensions.
 *
 * The output buffer is owned by the caller (free with
 * yetty_ydraw_drawable_list_destroy).
 */

#include <stddef.h>
#include <stdint.h>
#include <yetty/ycore/result.h>
#include <yetty/ydraw-core/drawable-list.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Resolver for <image href="..."> content — supplied by the embedder
 * (ysvg itself has no network or codec access; the browser resolves the
 * href against its document base, fetches through its loader and decodes).
 * Returns 1 and fills *out_pixels (RGBA8 row-major, BORROWED — must stay
 * valid for the duration of the render call) plus the pixel dimensions;
 * returns 0 when the reference cannot be served (the element is skipped). */
struct yetty_ysvg_image_resolver {
    int (*resolve)(void *userdata, const char *href, const uint32_t **out_pixels, int *out_width,
                   int *out_height);
    void *userdata;
};

struct yetty_ysvg_render_config {
    /* Display cell size in pixels — used to derive a default font size when
     * no font-size is given on the SVG. */
    uint32_t cell_width;
    uint32_t cell_height;
    /* Surface dimensions in cells — only used as the scene-bounds fallback
     * when the SVG carries neither viewBox nor width/height. */
    uint32_t width_cells;
    uint32_t height_cells;
    /* Optional <image> resolver; resolve == NULL skips <image> elements. */
    struct yetty_ysvg_image_resolver image_resolver;
    /* When set, <a> subtrees record clickable regions into the render
	 * output (scene pixel space). Off by default — callers that don't
	 * consume links then have nothing to free. */
    int collect_link_regions;
};

/* Clickable region of one SVG <a> subtree: the union of its rendered
 * children's extents, in scene pixel space (same space the drawable
 * list's coordinates use). Nested <a> regions belong to the INNERMOST
 * anchor, matching browser click dispatch. */
struct yetty_ysvg_link_region {
    char *href; /* owned by the output; free via yetty_ysvg_links_free */
    float min_x, min_y, max_x, max_y;
};

void yetty_ysvg_links_free(struct yetty_ysvg_link_region *links, size_t count);

struct yetty_ysvg_render_output {
    struct yetty_ydraw_drawable_list *buffer;
    float scene_width;
    float scene_height;
    /* <a> click regions — non-NULL only with config.collect_link_regions;
	 * caller owns, frees via yetty_ysvg_links_free. */
    struct yetty_ysvg_link_region *links;
    size_t link_count;
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
