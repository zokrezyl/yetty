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
 *   - Painting: emit ypaint primitives (boxes + TEXT_SPAN) into a buffer
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

struct yetty_ypaint_core_buffer;

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ylexbor;
YETTY_YRESULT_DECLARE(yetty_ylexbor_ptr, struct yetty_ylexbor *);

struct yetty_ylexbor_config {
	int viewport_width;   /* px */
	int viewport_height;  /* px (unused by current layout — content height is computed) */
	float default_font_size;  /* px; falls back to 16 if 0 */
};

struct yetty_ylexbor_ptr_result yetty_ylexbor_create(
	const struct yetty_ylexbor_config *cfg);
struct yetty_ycore_void_result yetty_ylexbor_destroy(struct yetty_ylexbor *r);

/* Set / replace the document. UTF-8 HTML; safe to call repeatedly to
 * replace previous content. */
struct yetty_ycore_void_result yetty_ylexbor_load_html(
	struct yetty_ylexbor *r, const char *html, size_t html_len);

/* Append additional CSS that takes precedence over the document's own
 * <style>/<link> rules (origin = author). Handy for injecting a
 * theme. */
struct yetty_ycore_void_result yetty_ylexbor_add_css(
	struct yetty_ylexbor *r, const char *css, size_t css_len);

/* Resize the viewport — invalidates the layout. */
struct yetty_ycore_void_result yetty_ylexbor_set_viewport(
	struct yetty_ylexbor *r, int width, int height);

/* Drain the laid-out document into a ypaint buffer. Caller owns buf;
 * this function appends primitives. */
struct yetty_ycore_void_result yetty_ylexbor_render(
	struct yetty_ylexbor *r, struct yetty_ypaint_core_buffer *buf);

/* Total content height after layout, in px. Useful for scrollbars. */
int yetty_ylexbor_content_height(const struct yetty_ylexbor *r);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YLEXBOR_H */
