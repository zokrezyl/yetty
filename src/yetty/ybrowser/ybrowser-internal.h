#ifndef YETTY_YLEXBOR_INTERNAL_H
#define YETTY_YLEXBOR_INTERNAL_H

/* Shared between ylexbor's .c files. Not installed. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/types.h>
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
    YL_LAYOUT_GRID,        /* display:grid with a parsed grid-template-columns
	                        * — children auto-flow into the columns. Only the
	                        * track-sized column layout is modelled (no spans);
	                        * activated for small explicit-track grids (story
	                        * cards), the dominant real-world idiom. */
};

/* One grid-template-columns track. `is_fr` selects the unit: an `fr` track
 * shares leftover space weighted by `value`; otherwise `value` is a fixed px
 * width. `auto`/`min-content`/`max-content` are approximated as `1fr`. */
struct yl_grid_track {
    float value;
    uint8_t is_fr;
};

#define YL_GRID_MAX_TRACKS 8

/* A class-scoped grid template parsed from the author CSS
 * (`.cls{display:grid;grid-template-columns:…;column-gap:…}`). Looked up by
 * class name at box-build for grid containers. */
struct yl_grid_class {
    char *cls; /* owned; freed on document replace */
    struct yl_grid_track tracks[YL_GRID_MAX_TRACKS];
    uint8_t ntracks;
    float col_gap;
    float row_gap;
};

/* CSS `position` values. Mirrors the subset of CSS_POSITION_* the layout
 * pass acts on; STICKY collapses to RELATIVE until proper sticky tracking
 * exists. */
enum yetty_ylexbor_position {
    YL_POS_STATIC = 0,
    YL_POS_RELATIVE,
    YL_POS_ABSOLUTE,
    YL_POS_FIXED,
};

struct yetty_ylexbor_color {
    uint8_t r, g, b, a;
};

/* Bits set in yetty_ylexbor_box.pct_mask when the matching margin/padding
 * field holds a *percentage ratio* (e.g. 0.10 for 10%) rather than a
 * resolved pixel value. The layout pass multiplies the ratio by the
 * containing block's content width (the only correct basis for percent
 * margins AND paddings per CSS) and clears the bit. A separate flag is
 * needed — unlike width, margins may be legitimately negative px, so the
 * sign cannot double as a "this is a percent" marker. */
enum {
    YL_PCT_MARGIN_TOP = 1u << 0,
    YL_PCT_MARGIN_RIGHT = 1u << 1,
    YL_PCT_MARGIN_BOTTOM = 1u << 2,
    YL_PCT_MARGIN_LEFT = 1u << 3,
    YL_PCT_PADDING_TOP = 1u << 4,
    YL_PCT_PADDING_RIGHT = 1u << 5,
    YL_PCT_PADDING_BOTTOM = 1u << 6,
    YL_PCT_PADDING_LEFT = 1u << 7,
};

/* One styled sub-run inside an inline-text box's character stream.
 * Produced by the box pass as it walks <a>/<strong>/<em>/etc. nested
 * inside a block's inline children. wrap_inline_box reads `segs[]`
 * to emit one painted box per visible styled fragment per line.
 *
 * `element` is the deepest inline ancestor element this run came from
 * (typically an <a>, sometimes a <button> / <span data-x>). The wrap
 * pass stamps it onto the resulting INLINE_TEXT box so click hit-test
 * can walk up to find the click target — without this, links rendered
 * inside text never fire JS handlers. NULL for runs not nested under
 * any inline element (i.e. plain text directly inside a block). */
struct yetty_ylexbor_inline_seg {
    size_t start; /* byte offset into box->text where this seg begins */
    struct yetty_ylexbor_color fg;
    int font_weight;
    bool font_italic;
    bool underline;
    bool line_through;
    bool overline;
    lxb_dom_element_t *element; /* borrowed, may be NULL */
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

    /* Computed CSS `line-height` in px. 0 = unset / `normal` — the
     * inline-wrap pass falls back to font_size * 1.25 in that case. A
     * NUMBER value (e.g. line-height: 1.5) is pre-multiplied by font_size
     * at box-build; a DIMENSION (e.g. 24px) is the resolved length. */
    float line_height;

    /* box-sizing. false = content-box (CSS initial): the explicit
     * `css_width` is the content width, padding + border expand the box.
     * true = border-box: `css_width` already includes padding + border,
     * so the content area is the remainder. The layout pass branches on
     * this when an explicit width is set. */
    bool border_box;

    /* Percent margin/padding marker — see YL_PCT_* above. */
    uint8_t pct_mask;

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

    /* Grid-container tracks (meaningful when layout_mode == YL_LAYOUT_GRID).
	 * Parsed from the author CSS's grid-template-columns (libcss exposes none
	 * of it). grid_ntracks == 0 means "not a resolved grid". */
    struct yl_grid_track grid_tracks[YL_GRID_MAX_TRACKS];
    uint8_t grid_ntracks;
    float grid_col_gap;
    float grid_row_gap;

    /* Float / clear. float_side: 0=none, 1=left, 2=right.
	 * clear_side: 0=none, 1=left, 2=right, 3=both. Boxes with
	 * float_side != 0 are removed from the parent's normal flow at
	 * layout time; subsequent in-flow content flows around them. */
    uint8_t float_side;
    uint8_t clear_side;

    /* CSS `position` (YL_POS_*). STATIC is normal flow. RELATIVE lays
		 * out in flow then shifts visually by the insets. ABSOLUTE / FIXED
		 * are removed from flow and placed against a containing block using
		 * the insets. STICKY is treated as RELATIVE for now. */
    uint8_t position;

    /* Inset offsets (top/right/bottom/left). A side counts only when its
		 * bit is set in `pos_set_mask` (0px is a valid offset, distinct from
		 * `auto`). When the side's bit is also set in `pos_pct_mask`, the
		 * value is a percent ratio (0.10 = 10%) resolved at layout against
		 * the containing block; otherwise it is resolved px (may be negative
		 * — `top:-8px` is legal). Bit order: 0=top,1=right,2=bottom,3=left. */
    float pos_top, pos_right, pos_bottom, pos_left;
    uint8_t pos_set_mask;
    uint8_t pos_pct_mask;

    /* CSS `transform: translate()` — a visual shift that moves the box and
		 * its whole subtree without affecting flow (siblings ignore it). Only
		 * the translate component is modelled; scale/rotate/matrix are not.
		 * Percent translates resolve against the box's OWN border-box size
		 * (the `translate(-50%,-50%)` centering idiom), not the containing
		 * block. Parsed from the inline style at box-build. */
    bool has_transform;
    float tf_tx, tf_ty;
    bool tf_tx_pct, tf_ty_pct;

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
    /* `text-decoration: line-through / overline` — paint a thin SDF
	 * rect at the text mid-line / above the cap-line. Same per-segment
	 * propagation as underline. */
    bool line_through;
    bool overline;

    /* Word-spacing slack added between runs of spaces in painted text.
	 * Filled by wrap_inline_box when `text-align: justify` is in
	 * effect: extra pixels per space so the line fills content_w. The
	 * paint pass routes through yetty_ydraw_drawable_list_add_text_full
	 * (TEXT_DRAWABLE_LIST v2) when this is non-zero. */
    float word_spacing;

    /* List marker — non-empty when the BLOCK box is an <li>. Painted
	 * once at the top-left of the block by the paint pass into the
	 * parent's padding gutter, so wrapping no longer strands "Line 2"
	 * at the marker column. NUL-terminated, lives in r->text_arena
	 * (so it's freed with the document). NULL when this isn't a list
	 * item. */
    const char *marker_text;
    size_t marker_text_len;

    /* Background image URL (resolved absolute), if any. Owned, freed
	 * on document destroy. The paint pass uses
	 * yetty_ylexbor_img_cache_get_or_load to fetch + decode, then
	 * emits a yimage prim sized to the box BEFORE the bg_color (so
	 * authors can layer a tint on top). */
    char *bg_image_url;

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
struct yetty_yplatform_yworkpool;

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

    /* Sorted, de-duplicated set of every class token present in the current
	 * document, built lazily from the DOM the first time the custom-property
	 * scanner needs it. Lets the scanner reject design tokens scoped under a
	 * class that isn't on the page (an inactive theme, e.g. Google News parks
	 * its dark palette under `.dm7YTc` / `body.dm7YTc`) instead of capturing
	 * them globally and, say, painting the fixed header dark. Owned; freed and
	 * rebuilt on document replace. */
    char **doc_classes;
    int doc_class_count;
    int doc_classes_built;

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
    /* When set, paint does NOT fetch <img> URLs over the network — it
     * draws a placeholder for any not-yet-cached image and leaves the URL
     * pending. Keeps paint (which runs on the host render / event-loop
     * thread) from blocking for seconds on HTTP. The host then pulls one
     * image at a time off the critical path via
     * yetty_ylexbor_fetch_one_pending_image and re-renders, so the page
     * fills in progressively while staying responsive. `data:` URIs still
     * decode inline. Default 0 — one-shot callers keep synchronous fetch. */
    int defer_image_fetch;
    int dom_dirty;      /* JS mutated the DOM — host should
	                            * relayout. */

    int viewport_w, viewport_h;
    float default_font_size;

    /* Content-column width (px) scanned out of the author CSS's grid
     * track lists — the modern `grid-template-columns: ... minmax(0,
     * <Nrem>) ...` idiom that caps the readable column. libcss reports
     * display:grid but not the track sizes, and we don't lay out grid
     * tracks; box-build applies this as a max-width on grid containers so
     * the content lays out at a readable width instead of filling the
     * whole container. 0 = none found. See ybrowser-css-vars.c. */
    float grid_content_max_px;

    /* Class-scoped grid templates parsed from author CSS (see
     * yetty_ylexbor_css_scan_grid_templates). Looked up at box-build for
     * grid containers; the array + each `cls` are freed on document replace. */
    struct yl_grid_class *grid_classes;
    int grid_class_count;
    int grid_class_cap;

    /* Set once the MediaWiki float-helper stylesheet has been injected for a
     * MediaWiki page (see yetty_ybrowser_libcss_apply_wikipedia_quirks). Those
     * helpers use generic class names and so must NOT apply to other sites. */
    int wiki_quirks_applied;

    /* Per-glyph advance as a fraction of font-size, used by the naive
     * text-width estimate that drives line wrap AND the x-advance between
     * styled inline fragments on a line. It MUST match the advance of the
     * font the host actually renders with, or per-fragment positions drift
     * (scattered glyphs). 0 = the historical 0.55 default (kept for the
     * unit tests, which pin positions against it); an interactive host
     * renders with a monospace MSDF font and sets this to that font's real
     * advance (~0.602) so per-segment link/bold/italic styling lines up. */
    float glyph_advance_ratio;

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

    /* Async image fetching. When `img_pool` is set, yetty_ylexbor_start_image_fetch
	 * submits each pending <img> to the worker pool (parallel fetch+decode on
	 * background threads) instead of blocking the caller. `on_resource_ready`
	 * is invoked on the loop thread when a fetch lands so the host repaints.
	 * `img_jobs_in_flight`, `destroy_pending`, `fetch_generation` are touched
	 * ONLY on the loop thread (submit + done + load_html + destroy), so no
	 * locking is needed. `fetch_generation` is bumped on document replace so a
	 * late job from a previous page is discarded. `destroy_pending` defers the
	 * real teardown until the last in-flight job's done() runs — a job's
	 * done() must never touch a freed engine. */
    struct yetty_yplatform_yworkpool *img_pool;
    void (*on_resource_ready)(void *user);
    void *resource_ready_user;
    int img_jobs_in_flight;
    int destroy_pending;
    uint64_t fetch_generation;
};

struct yetty_ylexbor_img_cache_entry {
    char *url;        /* owned */
    uint32_t *pixels; /* RGBA8 row-major; owned */
    int w, h;         /* source pixel dims */
    int failed;       /* fetch/decode failed — don't retry */
    int loading;      /* an async fetch job is in flight for this url */
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

/* yetty_ylexbor_prof / prof_now_ms are declared in the public header (used by
 * the host tool too). */

/* Real engine teardown (see yetty_ylexbor_destroy). Called either directly or,
 * when async fetches are outstanding, deferred to the last job's done(). */
struct yetty_ycore_void_result _yetty_ylexbor_destroy_now(struct yetty_ylexbor *r);

/* ===========================================================================
 * box generation (ylexbor-box.c) — DOM + computed style → box vector.
 * ===========================================================================*/
struct yetty_ycore_void_result yetty_ylexbor_box_build(struct yetty_ylexbor *r);

/* ===========================================================================
 * layout (ylexbor-layout.c) — block-flow vertical stacking + line wrap.
 * ===========================================================================*/
struct yetty_ycore_void_result yetty_ylexbor_layout(struct yetty_ylexbor *r);

/* ===========================================================================
 * paint (ylexbor-paint.c) — emit ydraw prims into the caller's buffer.
 * ===========================================================================*/
struct yetty_ycore_void_result yetty_ylexbor_paint(struct yetty_ylexbor *r,
                                                   struct yetty_ydraw_drawable_list *buf);

/* ===========================================================================
 * helpers
 * ===========================================================================*/

/* Append text bytes to the document's text arena and return a stable
 * pointer into it. Pointer is invalidated by load_html / destroy. */
const char *yetty_ylexbor_arena_dup(struct yetty_ylexbor *r, const char *bytes, size_t len);

/* Naive text width: glyph_count(s) * font_size * 0.55. Same shortcut
 * ynetsurf uses; will be replaced by FreeType-driven metrics later. */
float yetty_ylexbor_naive_text_width(const char *s, size_t len, float font_size,
                                     float advance_ratio);

/* The effective per-glyph advance ratio for `r` — the configured value or
 * the 0.55 default when unset. */
float yetty_ylexbor_glyph_advance_ratio(const struct yetty_ylexbor *r);

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

/* Drop the cached document class set (see r->doc_classes) so the next custom-
 * property scan rebuilds it from the current DOM. Call on document replace. */
void yetty_ylexbor_css_vars_reset_doc_classes(struct yetty_ylexbor *r);

/* Scan `css_source` for the `minmax(0, <len>)` grid content-column idiom
 * and record the widest track in the readable-column range into
 * r->grid_content_max_px. Cheap substring pass; safe to call per sheet. */
void yetty_ylexbor_css_scan_grid_content_width(struct yetty_ylexbor *r, const char *css_source,
                                               size_t len);

/* Scan `css_source` for class-scoped grid templates
 * (`.cls{display:grid;grid-template-columns:…;column-gap/gap/grid-gap:…}`) and
 * record them in r->grid_classes for box-build to look up by class name.
 * Cheap substring pass; safe to call per sheet. */
void yetty_ylexbor_css_scan_grid_templates(struct yetty_ylexbor *r, const char *css_source,
                                           size_t len);

/* Parse `grid-template-columns` (+ gaps) from an inline `style` attribute
 * string into `out` (up to maxn tracks). Returns the track count, 0 if absent.
 * For grids declared directly on an element rather than via a stylesheet
 * class. */
int yetty_ylexbor_grid_parse_inline(const char *style, size_t len, struct yl_grid_track *out,
                                    int maxn, float *col_gap, float *row_gap);

/* Column gap (px) from an inline `style` attribute (`gap`/`column-gap`), or -1
 * if absent. For flex/grid containers whose gap is set inline. */
float yetty_ylexbor_css_inline_gap(const char *style, size_t len);

/* Free r->grid_classes and each owned class name. Called on document replace. */
void yetty_ylexbor_grid_classes_free(struct yetty_ylexbor *r);

/* Look up a parsed grid template by an element's class attribute (space-
 * separated class list). Returns the matching entry or NULL. */
const struct yl_grid_class *yetty_ylexbor_grid_class_lookup(struct yetty_ylexbor *r,
                                                            const char *class_attr,
                                                            size_t class_len);

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

/* Parallel HTTP(S) fetch — runs up to `concurrency` requests at once
 * via curl_multi (HTTP/2 multiplexing reuses one connection per origin,
 * cutting total wall-time vs N sequential easy-handle calls). All
 * requests share the global referer (typically the document URL).
 *
 * For i in [0,n): on return, out_bodies[i] is malloc'd bytes (caller
 * frees) or NULL on failure; out_lens[i] is the body length; out_status[i]
 * is the HTTP status. Pre-allocate the three output arrays to length n.
 *
 * Used by ybrowser-paint to fetch every `<img>` URL on a page in one
 * batch before the synchronous decode/emit loop. */
void yetty_ylexbor_http_get_many(const char *const *urls, int n, const char *referer,
                                 int concurrency, char **out_bodies, size_t *out_lens,
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
