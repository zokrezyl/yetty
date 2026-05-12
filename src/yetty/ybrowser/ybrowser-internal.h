#ifndef YETTY_YLEXBOR_INTERNAL_H
#define YETTY_YLEXBOR_INTERNAL_H

/* Shared between ylexbor's .c files. Not installed. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <yetty/ybrowser/ybrowser.h>

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
    YL_BOX_BLOCK = 0,    /* generated for display:block / list-item / heading */
    YL_BOX_INLINE_TEXT,  /* a single laid-out line of inline text */
    YL_BOX_INLINE_IMAGE, /* <img> placeholder (no decoded pixels yet) */
};

/* Layout mode for blocks. Reading display:flex from the inline style
 * attribute is enough to unblock the common SPA shell layouts (header
 * bar, side-rail). We don't model flex-grow / flex-basis / wrap yet —
 * children get an equal slice of the parent's content width. */
enum yetty_ylexbor_layout_mode {
    YL_LAYOUT_BLOCK = 0,   /* default: vertical stacking */
    YL_LAYOUT_FLEX_ROW,    /* display:flex; flex-direction:row */
    YL_LAYOUT_FLEX_COLUMN, /* display:flex; flex-direction:column */
    YL_LAYOUT_TABLE,       /* display:table — rows laid out vertically,
	                        * cells horizontally with equal column width */
};

struct yetty_ylexbor_color {
    uint8_t r, g, b, a;
};

/* One styled sub-run inside an inline-text box's character stream.
 * Produced by the box pass as it walks <a>/<strong>/<em>/etc. nested
 * inside a block's inline children. wrap_inline_box reads `segs[]`
 * to emit one painted box per visible styled fragment per line. */
struct yetty_ylexbor_inline_seg {
    size_t start;  /* byte offset into box->text where this seg begins */
    struct yetty_ylexbor_color fg;
    int font_weight;
    bool font_italic;
    bool underline;
};

struct yetty_ylexbor_box {
    enum yetty_ylexbor_box_kind kind;

    /* Layout result — set by ylexbor-layout.c. */
    float x, y, w, h;

    /* Style snapshot used at layout + paint time. Resolved from the
	 * lexbor computed style during box generation. */
    struct yetty_ylexbor_color bg;
    struct yetty_ylexbor_color fg;
    float font_size;
    int font_weight; /* CSS weight; 400 = normal, 700 = bold */
    bool font_italic;

    /* Layout mode for this block's children — vertical stacking by
	 * default; flex row/column when display:flex is set. */
    enum yetty_ylexbor_layout_mode layout_mode;

    /* Margins (block boxes only). Margin collapsing is done at layout
	 * time, not stored here. The `_auto` flags carry CSS `margin: auto`
	 * so the layout pass can implement horizontal centering when both
	 * left+right are auto and a fixed width is set. */
    float margin_top, margin_right, margin_bottom, margin_left;
    bool margin_left_auto, margin_right_auto;
    float padding_top, padding_right, padding_bottom, padding_left;

    /* Optional explicit width / max-width / min-width from CSS. Zero
	 * means "not specified" (use the parent's content area). The
	 * layout pass clamps `child_w` to [min_width, max_width] and pins
	 * to width when set. */
    float css_width, css_max_width, css_min_width;
    float css_height; /* explicit height — placeholder for tables/img */

    /* Flex item properties (only meaningful when this box is the
	 * child of a flex container). flex_basis_px < 0 = auto; >= 0 =
	 * explicit px (resolved from libcss). flex_grow = 0 means the
	 * item doesn't take leftover space. */
    float flex_grow;
    float flex_basis_px;

    /* Flex-container properties (meaningful when this box has
	 * layout_mode == YL_LAYOUT_FLEX_ROW/COLUMN). Values are the
	 * libcss CSS_JUSTIFY_CONTENT_* / CSS_ALIGN_ITEMS_* enums (zero =
	 * inherit / default, treated as flex-start for justify and
	 * stretch for align). */
    int justify_content;
    int align_items;

    /* Float / clear. float_side: 0=none, 1=left, 2=right.
	 * clear_side: 0=none, 1=left, 2=right, 3=both. Boxes with
	 * float_side != 0 are removed from the parent's normal flow at
	 * layout time; subsequent in-flow content flows around them. */
    uint8_t float_side;
    uint8_t clear_side;

    /* Text alignment for the block's inline children. Values:
	 *   0 = left (default), 1 = center, 2 = right, 3 = justify. */
    int text_align;

    /* Border. We render a solid rectangle outline as a ysdf box pair
	 * (outer fill + inner cutout) sized by `border_width`. Stored
	 * once per side because top/right/bottom/left can differ. */
    float border_top, border_right, border_bottom, border_left;
    float border_radius;
    struct yetty_ylexbor_color border_color;

    /* Borrowed pointer to the originating element — keeps anchors for
	 * later (link-target hit-testing, hover, …). NULL for anonymous
	 * boxes. */
    lxb_dom_element_t *element;

    /* Inline-text payload (UTF-8, NOT NUL-terminated, len in text_len).
	 * Lives in r->text_arena, freed when the document is replaced. */
    const char *text;
    size_t text_len;

    /* Underline flag — set on per-segment line fragments produced by
	 * wrap_inline_box when the source segment had `underline` set
	 * (typically from <a>). The paint pass renders a thin SDF rect
	 * below the text baseline when true. */
    bool underline;

    /* Style segments — only populated on the source INLINE_TEXT box
	 * emitted by flush_inline. wrap_inline_box reads `segs[]` to
	 * split each laid-out line into one painted sub-box per styled
	 * fragment, then frees the array. Wrap-produced fragments have
	 * segs=NULL and one style baked into the box's own font_weight
	 * / font_italic / fg / underline. */
    struct yetty_ylexbor_inline_seg *segs;
    size_t segs_count;

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
    char *name;  /* e.g. "--bgColor-default" — without trailing : */
    char *value; /* serialized value, e.g. "#0d1117" or "var(--x)" */
};

struct yetty_ylexbor_customs {
    struct yetty_ylexbor_custom_prop *data;
    int size, cap;
};

/* Forward-declare libcss bridge state — definition in
 * ybrowser-libcss.h. Kept opaque here so TUs that don't touch libcss
 * (paint / layout / js) don't need its headers. */
struct yetty_ybrowser_libcss;

struct yetty_ylexbor {
    /* lexbor objects — owned. */
    lxb_html_document_t *document;
    lxb_css_parser_t *css_parser;

    /* libcss bridge — owned. NULL when libcss isn't compiled in. */
    struct yetty_ybrowser_libcss *libcss;

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
    int js_error_count; /* uncaught exceptions encountered */
    int dom_dirty;      /* JS mutated the DOM — host should
	                            * relayout. */

    int viewport_w, viewport_h;
    float default_font_size;

    /* Layout output. Re-allocated on every load_html / set_viewport. */
    struct yetty_ylexbor_box_vec boxes;
    int content_height;

    /* Per-fragment heap allocations for box.text. Each call to
	 * yetty_ylexbor_arena_dup malloc's a fresh buffer (stable address)
	 * and pushes it onto this list. Freed in batches by arena_reset
	 * between documents and on destroy. The old "linear buffer +
	 * realloc" arena invalidated pointers handed out earlier in the
	 * walk — visible as garbage characters at paint time, especially
	 * on large pages like Wikipedia where the arena grew often. */
    char **text_chunks;
    size_t text_chunks_count, text_chunks_cap;

    /* Decoded image cache. Each entry holds the RGBA8 pixels for a
	 * resolved <img src=> URL so the paint pass can emit the
	 * pixels without re-fetching/re-decoding on every redraw.
	 * The pixel buffer is malloc'd and freed at destroy time. */
    struct yetty_ylexbor_img_cache_entry *img_cache;
    int img_cache_count, img_cache_cap;
};

struct yetty_ylexbor_img_cache_entry {
    char *url;        /* owned */
    uint32_t *pixels; /* RGBA8 row-major; owned */
    int w, h;         /* source pixel dims */
    int failed;       /* fetch/decode failed — don't retry */
};

/* Defined in ylexbor-paint.c so the fetch/decode plumbing lives next
 * to the prim-emission code. Box-build calls this eagerly to learn
 * each <img>'s natural pixel size for layout. */
struct yetty_ylexbor_img_cache_entry *yetty_ylexbor_img_cache_get_or_load(struct yetty_ylexbor *r,
                                                                          const char *url);

/* Pick the most likely "real" URL out of an <img>'s src/srcset/data-*
 * attributes — sites use lazy-loading patterns where `src` is a tiny
 * placeholder and the actual URL lives in `data-src`/`data-original`/
 * `srcset`. Caller frees the returned absolute URL with free(). */
char *yetty_ylexbor_img_pick_url(struct yetty_ylexbor *r, lxb_dom_element_t *el);

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
struct yetty_ycore_void_result yetty_ylexbor_paint(struct yetty_ylexbor *r,
                                                   struct yetty_ypaint_core_buffer *buf);

/* ===========================================================================
 * helpers
 * ===========================================================================*/

/* Append text bytes to the document's text arena and return a stable
 * pointer into it. Pointer is invalidated by load_html / destroy. */
const char *yetty_ylexbor_arena_dup(struct yetty_ylexbor *r, const char *bytes, size_t len);

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
void yetty_ylexbor_css_vars_scan(struct yetty_ylexbor *r, const char *css_source, size_t len);

/* Resolve every `var(--name [, fallback])` reference in `value`. Returns
 * a freshly malloc'd NUL-terminated string the caller must free.
 * Returns a copy of the input on no-vars / OOM. */
char *yetty_ylexbor_css_vars_resolve(struct yetty_ylexbor *r, const char *value, size_t len);

/* Drop the customs table — called from destroy. */
void yetty_ylexbor_css_vars_destroy(struct yetty_ylexbor *r);

/* Internal box-vector growth — implemented in ylexbor.c. */
struct yetty_ycore_void_result _yetty_ylexbor_box_vec_reserve(struct yetty_ylexbor_box_vec *v,
                                                              uint32_t want);

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
struct yetty_ycore_void_result yetty_ylexbor_js_run_inline_scripts(struct yetty_ylexbor *r);

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
struct yetty_ycore_void_result yetty_ylexbor_js_run_all_scripts(struct yetty_ylexbor *r);

/* Tick: run any timers whose deadline has elapsed, drain promise jobs.
 * Returns ms until the next timer fires (-1 if none). */
int yetty_ylexbor_pump(struct yetty_ylexbor *r);

/* Dispatch a synthetic event of the given type to all addEventListener-
 * registered handlers. `target` may be NULL → fires on document. */
void yetty_ylexbor_js_dispatch_event_type(struct yetty_ylexbor *r, const char *type,
                                          void *target_element_ptr_or_null);

/* Resolve a possibly-relative URL against r->base_url. Caller frees
 * the returned string. Returns NULL on failure. */
char *yetty_ylexbor_resolve_url(struct yetty_ylexbor *r, const char *href);

/* Synchronous HTTP(S) fetch — used by the script loader and fetch()
 * binding. Returns body bytes (caller frees) and HTTP status. */
char *yetty_ylexbor_http_get(const char *url, size_t *out_len, long *out_status);
/* Variant that sends a Referer header — needed for many CDN image
 * endpoints that 403/404 fetches without it (gstatic, cloudflare WAFs,
 * news-site image proxies). */
char *yetty_ylexbor_http_get_referer(const char *url, const char *referer, size_t *out_len,
                                     long *out_status);

/* Dispatch a click event to the JS handlers attached to the element
 * whose box contains (x,y) in pane-local pixels. Returns 1 if a handler
 * fired, 0 otherwise. The handlers can mutate the DOM; the caller
 * should consult r->dom_dirty afterwards and re-run box-build → layout
 * → paint if set. */
int yetty_ylexbor_dispatch_click(struct yetty_ylexbor *r, float x, float y);

/* Re-resolve box tree + layout from the (possibly mutated) DOM. */
struct yetty_ycore_void_result yetty_ylexbor_relayout(struct yetty_ylexbor *r);

#endif
