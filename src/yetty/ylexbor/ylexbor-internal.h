#ifndef YETTY_YLEXBOR_INTERNAL_H
#define YETTY_YLEXBOR_INTERNAL_H

/* Shared between ylexbor's .c files. Not installed. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <yetty/ylexbor/ylexbor.h>

#include <lexbor/dom/interfaces/element.h>
#include <lexbor/html/html.h>
#include <lexbor/css/css.h>

/* ===========================================================================
 * Box tree — minimal layout representation.
 *
 * One box per laid-out fragment. For block-level elements we make exactly
 * one block box; for runs of inline content inside a block we make
 * inline-fragment boxes (one per laid-out line).
 *
 * Inline-only-children blocks contain a flat list of fragments; mixed-
 * children blocks generate "anonymous block" wrappers around inline runs.
 * This is the absolute minimum that produces correct vertical stacking
 * for the documentation-page subset we target.
 *
 * Coordinates are pane-local pixels with origin = top-left of viewport.
 * y grows downward. Computed by ylexbor-layout.c during the layout pass.
 * ===========================================================================*/

enum yetty_ylexbor_box_kind {
	YL_BOX_BLOCK = 0,     /* generated for display:block / list-item / heading */
	YL_BOX_INLINE_TEXT,   /* a single laid-out line of inline text */
	YL_BOX_INLINE_IMAGE,  /* <img> placeholder (no decoded pixels yet) */
};

struct yetty_ylexbor_color { uint8_t r, g, b, a; };

struct yetty_ylexbor_box {
	enum yetty_ylexbor_box_kind kind;

	/* Layout result — set by ylexbor-layout.c. */
	float x, y, w, h;

	/* Style snapshot used at layout + paint time. Resolved from the
	 * lexbor computed style during box generation. */
	struct yetty_ylexbor_color bg;
	struct yetty_ylexbor_color fg;
	float font_size;
	int  font_weight;     /* CSS weight; 400 = normal, 700 = bold */
	bool font_italic;

	/* Margins (block boxes only). Margin collapsing is done at layout
	 * time, not stored here. */
	float margin_top, margin_right, margin_bottom, margin_left;
	float padding_top, padding_right, padding_bottom, padding_left;

	/* Borrowed pointer to the originating element — keeps anchors for
	 * later (link-target hit-testing, hover, …). NULL for anonymous
	 * boxes. */
	lxb_dom_element_t *element;

	/* Inline-text payload (UTF-8, NOT NUL-terminated, len in text_len).
	 * Lives in r->text_arena, freed when the document is replaced. */
	const char *text;
	size_t text_len;

	/* Children (block boxes only). Stored as indices into the flat
	 * box vector to keep relocation cheap. */
	uint32_t first_child;
	uint32_t child_count;
};

/* Flat vector of boxes. Root is index 0. */
struct yetty_ylexbor_box_vec {
	struct yetty_ylexbor_box *data;
	uint32_t size, cap;
};

/* Forward decls for the optional JS runtime — concrete types live in
 * <quickjs.h>; keeping them opaque here avoids leaking the QuickJS
 * include into every .c that just touches the box / layout state. */
struct JSRuntime;
struct JSContext;

struct yetty_ylexbor {
	/* lexbor objects — owned. */
	lxb_html_document_t *document;
	lxb_css_parser_t    *css_parser;

	/* QuickJS — created lazily on the first <script> seen. NULL if
	 * QuickJS isn't compiled in (YETTY_HAVE_QUICKJS=0) or no scripts
	 * have been encountered yet. */
	struct JSRuntime *js_rt;
	struct JSContext *js_ctx;
	int js_error_count;        /* uncaught exceptions encountered */

	int viewport_w, viewport_h;
	float default_font_size;

	/* Layout output. Re-allocated on every load_html / set_viewport. */
	struct yetty_ylexbor_box_vec boxes;
	int content_height;

	/* Arena for text fragments referenced by box.text. Reset between
	 * documents. */
	char *text_arena;
	size_t text_arena_size, text_arena_cap;
};

/* ===========================================================================
 * box generation (ylexbor-box.c) — DOM + computed style → box vector.
 * ===========================================================================*/
struct yetty_ycore_void_result yetty_ylexbor_box_build(struct yetty_ylexbor *r);

/* ===========================================================================
 * layout (ylexbor-layout.c) — block-flow vertical stacking + line wrap.
 * ===========================================================================*/
struct yetty_ycore_void_result yetty_ylexbor_layout(struct yetty_ylexbor *r);

/* ===========================================================================
 * paint (ylexbor-paint.c) — emit ypaint prims into the caller's buffer.
 * ===========================================================================*/
struct yetty_ycore_void_result yetty_ylexbor_paint(
	struct yetty_ylexbor *r, struct yetty_ypaint_core_buffer *buf);

/* ===========================================================================
 * helpers
 * ===========================================================================*/

/* Append text bytes to the document's text arena and return a stable
 * pointer into it. Pointer is invalidated by load_html / destroy. */
const char *yetty_ylexbor_arena_dup(struct yetty_ylexbor *r,
				    const char *bytes, size_t len);

/* Naive text width: glyph_count(s) * font_size * 0.55. Same shortcut
 * ynetsurf uses; will be replaced by FreeType-driven metrics later. */
float yetty_ylexbor_naive_text_width(const char *s, size_t len, float font_size);

/* Internal box-vector growth — implemented in ylexbor.c. */
struct yetty_ycore_void_result _yetty_ylexbor_box_vec_reserve(
	struct yetty_ylexbor_box_vec *v, uint32_t want);

/* ===========================================================================
 * JavaScript (ylexbor-js.c) — optional, gated on YETTY_HAVE_QUICKJS.
 *
 * MVP wiring: walk the freshly-parsed DOM, find every <script> with no
 * `src` attribute (external script fetching is a follow-up), eval the
 * inline text. Console output via a minimal `console.log` /
 * `console.error` binding that prints to stderr. No DOM bindings, no
 * timers, no network — those are the long tail.
 *
 * When YETTY_HAVE_QUICKJS=0 these are no-op stubs so the rest of the
 * lib doesn't have to ifdef everywhere.
 * ===========================================================================*/
struct yetty_ycore_void_result yetty_ylexbor_js_init(struct yetty_ylexbor *r);
void yetty_ylexbor_js_destroy(struct yetty_ylexbor *r);
struct yetty_ycore_void_result yetty_ylexbor_js_run_inline_scripts(
	struct yetty_ylexbor *r);

#endif
