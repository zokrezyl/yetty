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

/* Layout mode for blocks. Reading display:flex from the inline style
 * attribute is enough to unblock the common SPA shell layouts (header
 * bar, side-rail). We don't model flex-grow / flex-basis / wrap yet —
 * children get an equal slice of the parent's content width. */
enum yetty_ylexbor_layout_mode {
	YL_LAYOUT_BLOCK = 0,  /* default: vertical stacking */
	YL_LAYOUT_FLEX_ROW,   /* display:flex; flex-direction:row */
	YL_LAYOUT_FLEX_COLUMN,/* display:flex; flex-direction:column */
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

	/* Layout mode for this block's children — vertical stacking by
	 * default; flex row/column when display:flex is set. */
	enum yetty_ylexbor_layout_mode layout_mode;

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

	/* Children — explicit linked list. `first_child` is the index of
	 * the first child (0 if none — root is never a child), and each
	 * child carries `next_sibling` forming a forward-linked chain
	 * (0-terminated). We can't use a contiguous `[first .. first+n)`
	 * range because text-nodes inside a block sibling create boxes
	 * mid-walk, breaking contiguity. `child_count` is kept for
	 * sanity / debugging only — iteration MUST go through the
	 * sibling chain. */
	uint32_t first_child;
	uint32_t next_sibling;
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

/* Opaque to non-web TUs. Definition + management lives in
 * ylexbor-js-web.c. */
struct yetty_ylexbor_timer;

/* CSS custom-property entry — populated from `:root { --foo: bar; }`
 * (and equivalent) declarations in every stylesheet we load. lexbor's
 * cascade only assigns IDs to custom-property names; the *values* are
 * not substituted into `var(--foo)` references during cascade. We
 * collect them ourselves and resolve var() inside our color/length
 * readers (see ylexbor-css-vars.c). */
struct yetty_ylexbor_custom_prop {
	char *name;       /* e.g. "--bgColor-default" — without trailing : */
	char *value;      /* serialized value, e.g. "#0d1117" or "var(--x)" */
};

struct yetty_ylexbor_customs {
	struct yetty_ylexbor_custom_prop *data;
	int size, cap;
};

struct yetty_ylexbor {
	/* lexbor objects — owned. */
	lxb_html_document_t *document;
	lxb_css_parser_t    *css_parser;

	/* CSS custom-property table populated by yetty_ylexbor_css_vars_*
	 * as stylesheets get added. Read by read_inline_color / similar
	 * to substitute `var(--foo)` references. */
	struct yetty_ylexbor_customs customs;

	/* Base URL of the loaded document — used to resolve relative
	 * src= for external <script>, fetch(), XHR. NULL for HTML loaded
	 * from a string with no associated URL. Owned, freed on destroy. */
	char *base_url;

	/* Timer queue: array of timer*, sorted by deadline_ms ascending. */
	struct yetty_ylexbor_timer **timers;
	int timer_count, timer_cap;
	int next_timer_id;

	/* QuickJS — created lazily on the first <script> seen. NULL if
	 * QuickJS isn't compiled in (YETTY_HAVE_QUICKJS=0) or no scripts
	 * have been encountered yet. */
	struct JSRuntime *js_rt;
	struct JSContext *js_ctx;
	int js_error_count;        /* uncaught exceptions encountered */
	int dom_dirty;             /* JS mutated the DOM — host should
	                            * relayout. */

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

/* ===========================================================================
 * CSS custom-property resolver (ylexbor-css-vars.c) — fills the gap left
 * by lexbor's cascade for `var(--foo)` lookups. After every stylesheet
 * is added, scan its source for `--name: value;` declarations that live
 * in :root / html / * rules and store the name→value mapping. When
 * reading an attribute value, call resolve_vars to substitute.
 * ===========================================================================*/

/* Scan `css_source` for custom-property declarations rooted at :root /
 * html / *  and merge them into r->customs. Idempotent — later defs
 * overwrite earlier (closest to spec for our purposes). */
void yetty_ylexbor_css_vars_scan(struct yetty_ylexbor *r,
				  const char *css_source, size_t len);

/* Resolve every `var(--name [, fallback])` reference in `value`. Returns
 * a freshly malloc'd NUL-terminated string the caller must free.
 * Returns a copy of the input on no-vars / OOM. */
char *yetty_ylexbor_css_vars_resolve(struct yetty_ylexbor *r,
				      const char *value, size_t len);

/* Drop the customs table — called from destroy. */
void yetty_ylexbor_css_vars_destroy(struct yetty_ylexbor *r);

/* Internal box-vector growth — implemented in ylexbor.c. */
struct yetty_ycore_void_result _yetty_ylexbor_box_vec_reserve(
	struct yetty_ylexbor_box_vec *v, uint32_t want);

/* ===========================================================================
 * JavaScript (ylexbor-js.c + ylexbor-js-dom.c) — gated on YETTY_HAVE_QUICKJS.
 *
 * Layered:
 *   ylexbor-js.c       — JSRuntime/JSContext lifecycle, console.*, the
 *                        <script> walker that calls JS_Eval per inline
 *                        script.
 *   ylexbor-js-dom.c   — DOM bindings: document, Element, Node,
 *                        textContent / innerHTML / getAttribute /
 *                        setAttribute / appendChild / removeChild /
 *                        querySelector(All) / getElementById /
 *                        addEventListener / Element.style /
 *                        Element.classList. Mutations bump
 *                        r->dom_dirty so the host can re-run
 *                        box-build → layout → paint.
 *
 * When YETTY_HAVE_QUICKJS=0 the whole layer is no-op stubs.
 * ===========================================================================*/
struct yetty_ycore_void_result yetty_ylexbor_js_init(struct yetty_ylexbor *r);
void yetty_ylexbor_js_destroy(struct yetty_ylexbor *r);
struct yetty_ycore_void_result yetty_ylexbor_js_run_inline_scripts(
	struct yetty_ylexbor *r);

/* DOM-bindings install (called from js_init). */
void yetty_ylexbor_js_dom_install(struct yetty_ylexbor *r);

/* Reset DOM-side global state held across runtime lifecycles —
 * specifically the static `g_listeners` array, whose JSValues belong
 * to the runtime that's about to be destroyed. Called from
 * yetty_ylexbor_js_destroy. */
void yetty_ylexbor_js_dom_reset(struct yetty_ylexbor *r);

/* Web-API bindings (fetch/XHR/timers/window/navigator/location/history/
 * storage/getComputedStyle/matchMedia/requestAnimationFrame). Installed
 * right after the DOM bindings. */
void yetty_ylexbor_js_web_install(struct yetty_ylexbor *r);

/* Tear down web-layer state (pending timer handlers + array). Called
 * by js_destroy before freeing the JS runtime. */
void yetty_ylexbor_js_web_shutdown(struct yetty_ylexbor *r);

/* Drain QuickJS pending jobs (Promise microtasks) until empty. Called
 * after every JS turn. */
void yetty_ylexbor_js_drain_jobs(struct yetty_ylexbor *r);

/* Walk the DOM and load every external <script src=...> + run inline
 * scripts in document order. Synchronous: blocks on each fetch.
 * Replaces yetty_ylexbor_js_run_inline_scripts as the canonical
 * post-parse script driver. */
struct yetty_ycore_void_result yetty_ylexbor_js_run_all_scripts(
	struct yetty_ylexbor *r);

/* Tick: run any timers whose deadline has elapsed, drain promise jobs.
 * Returns ms until the next timer fires (-1 if none). */
int yetty_ylexbor_pump(struct yetty_ylexbor *r);

/* Dispatch a synthetic event of the given type to all addEventListener-
 * registered handlers. `target` may be NULL → fires on document. */
void yetty_ylexbor_js_dispatch_event_type(struct yetty_ylexbor *r,
	const char *type, void *target_element_ptr_or_null);

/* Resolve a possibly-relative URL against r->base_url. Caller frees
 * the returned string. Returns NULL on failure. */
char *yetty_ylexbor_resolve_url(struct yetty_ylexbor *r, const char *href);

/* Synchronous HTTP(S) fetch — used by the script loader and fetch()
 * binding. Returns body bytes (caller frees) and HTTP status. */
char *yetty_ylexbor_http_get(const char *url, size_t *out_len, long *out_status);

/* Dispatch a click event to the JS handlers attached to the element
 * whose box contains (x,y) in pane-local pixels. Returns 1 if a handler
 * fired, 0 otherwise. The handlers can mutate the DOM; the caller
 * should consult r->dom_dirty afterwards and re-run box-build → layout
 * → paint if set. */
int yetty_ylexbor_dispatch_click(struct yetty_ylexbor *r, float x, float y);

/* Re-resolve box tree + layout from the (possibly mutated) DOM. */
struct yetty_ycore_void_result yetty_ylexbor_relayout(struct yetty_ylexbor *r);

#endif
