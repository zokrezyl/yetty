#ifndef YETTY_YLEXBOR_H
#define YETTY_YLEXBOR_H

/*
 * ylexbor — permissively-licensed in-process HTML/CSS renderer for
 * yetty. Sits on top of lexbor (Apache-2.0): HTML parser, CSS parser,
 * selectors, computed-style cascade. Layout + paint live here.
 *
 * Status: MVP. Implements:
 *   - HTML5 parsing (via lexbor)
 *   - CSS parsing + selector matching + computed style (via lexbor)
 *   - Naive block-flow layout: vertical stacking of block-level elements,
 *     line-by-line text wrapping inside inline content
 *   - Painting: emit ydraw primitives (boxes + TEXT_DRAWABLE_LIST) into a buffer
 *
 * Explicitly NOT implemented (TODO, separate work):
 *   - Float layout
 *   - Flexbox / Grid / Tables (treated as plain block)
 *   - Background images, gradients, borders with radii
 *   - z-index / stacking contexts
 *   - position: absolute/fixed/sticky
 *   - Form input rendering (rendered as plain text)
 *   - JavaScript / DOM events
 *   - Image fetching (external resources skipped)
 *   - Animations / transitions
 *
 * License note: this module is BSL-1.1 (yetty's main license) — unlike
 * src/yetty/ynetsurf which is GPL-2.0 due to NetSurf core. ylexbor uses
 * only Apache-2.0 lexbor and yetty's own code, so it can be linked
 * directly into the main yetty binary.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>

struct yetty_ydraw_drawable_list;

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ylexbor;
YETTY_YRESULT_DECLARE(yetty_ylexbor_ptr, struct yetty_ylexbor *);

struct yetty_ylexbor_config {
    int viewport_width;      /* px */
    int viewport_height;     /* px (unused by current layout — content height is computed) */
    float default_font_size; /* px; falls back to 16 if 0 */
};

struct yetty_ylexbor_ptr_result yetty_ylexbor_create(const struct yetty_ylexbor_config *cfg);
struct yetty_ycore_void_result yetty_ylexbor_destroy(struct yetty_ylexbor *r);

/* Set / replace the document. UTF-8 HTML; safe to call repeatedly to
 * replace previous content. */
struct yetty_ycore_void_result yetty_ylexbor_load_html(struct yetty_ylexbor *r, const char *html,
                                                       size_t html_len);

/* Append additional CSS that takes precedence over the document's own
 * <style>/<link> rules (origin = author). Handy for injecting a
 * theme. */
struct yetty_ycore_void_result yetty_ylexbor_add_css(struct yetty_ylexbor *r, const char *css,
                                                     size_t css_len);

/* Resize the viewport — invalidates the layout. */
struct yetty_ycore_void_result yetty_ylexbor_set_viewport(struct yetty_ylexbor *r, int width,
                                                          int height);

/* Drain the laid-out document into a ydraw buffer. Caller owns buf;
 * this function appends primitives. */
struct yetty_ycore_void_result yetty_ylexbor_render(struct yetty_ylexbor *r,
                                                    struct yetty_ydraw_drawable_list *buf);

/* Total content height after layout, in px. Useful for scrollbars. */
int yetty_ylexbor_content_height(const struct yetty_ylexbor *r);

/* Dispatch a synthetic 'click' event to JS handlers attached at (x,y).
 * Returns 1 if any handler fired. Caller should check
 * yetty_ylexbor_dom_dirty() afterwards and call _relayout() if set. */
int yetty_ylexbor_dispatch_click(struct yetty_ylexbor *r, float x, float y);

/* True iff JS mutated the DOM since the last relayout. Cleared by
 * yetty_ylexbor_relayout. */
int yetty_ylexbor_dom_dirty(const struct yetty_ylexbor *r);

/* Re-run box-build + layout. Cheap-ish (~ms for small docs). Called by
 * the host after a JS turn that mutated the DOM, or after a viewport
 * resize. Render() reads the same boxes. */
struct yetty_ycore_void_result yetty_ylexbor_relayout(struct yetty_ylexbor *r);

/* Set the base URL the loaded document was fetched from. Used to
 * resolve relative src= on external <script> and fetch() calls.
 * Optional — if not set, only absolute URLs work. */
struct yetty_ycore_void_result yetty_ylexbor_set_base_url(struct yetty_ylexbor *r, const char *url);

/* Run any pending timers whose deadline has elapsed and drain Promise
 * microtasks. Returns milliseconds until the next timer fires (-1 if
 * none). The host calls this from its event loop. */
int yetty_ylexbor_pump_timers(struct yetty_ylexbor *r);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YLEXBOR_H */
