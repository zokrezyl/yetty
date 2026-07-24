#ifndef YETTY_YLEXBOR_INTERNAL_H
#define YETTY_YLEXBOR_INTERNAL_H

/* Shared between ylexbor's .c files. Not installed. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <pthread.h>
#include <time.h>
#include <yetty/ycore/types.h>
#include <yetty/ysvg/ysvg.h>
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
    uint8_t is_pct;  /* value is a percent (0..100) of the grid content width,
		              * resolved to px at layout time. */
    uint8_t is_auto; /* `auto` / min-content / max-content — sized to the
		              * max-content of the items placed in this column. */
    float offset;    /* px added AFTER the percentage resolves — the length term
		              * of `calc(<pct> ± <len>)` track sizes (already sign-applied
		              * and unit-normalised to px). 0 for every non-calc track.
		              * Only consulted when is_pct is set. */
};

/* 16 covers the 12-column grids real design systems use (github's Primer
 * Brand: `grid-template-columns: repeat(12, minmax(0,1fr))` with items placed
 * via `grid-column: span N`). */
#define YL_GRID_MAX_TRACKS 16

/* Ancestor-context class requirements shared by the class-keyed grid
 * tables: a selector's non-target classes, matched loosely against the
 * element and its DOM ancestors at lookup time. */
enum { YL_GRID_SPAN_CONTEXT_MAX = 4 };

/* A class-scoped grid template parsed from the author CSS
 * (`.cls{display:grid;grid-template-columns:…;column-gap:…}`,
 * `.card.wide{grid-template-columns:1fr 16px auto}`). Looked up by class
 * (+ ancestor context) at box-build for grid containers. */
/* A stylesheet `width: max-content|min-content|fit-content` declaration.
 * libcss 0.9 drops intrinsic-sizing keywords as invalid, so the cascade
 * never sees them; this side table re-applies the keyword at box-build
 * (github's NavDropdown panels: position:absolute + width:max-content —
 * without it the inset fallback pinned them to the tiny container). */
struct yl_width_keyword_rule {
    char *selector;          /* owned */
    void *compiled_selector; /* owned */
    uint8_t selector_state;
    uint8_t keyword; /* 1 = max-content, 2 = min-content, 3 = fit-content */
};

/* A stylesheet `width|min-width|max-width: calc(<pct> ± <len>)` declaration.
 * libcss 0.9 can't parse calc() and drops the declaration, so percentage grid
 * columns (`width:calc(33.33% - 16px)`) lose their width and stack. This side
 * table captures the calc terms and resolves them PRECISELY at box-build —
 * `pct/100 * containing-block-width ± length` — so the column lands on the exact
 * Chrome pixel instead of the coarse `calc → %` textual approximation. */
struct yl_calc_length_rule {
    char *selector;          /* owned */
    void *compiled_selector; /* owned */
    uint8_t selector_state;
    uint8_t prop;        /* 0 = width, 1 = min-width, 2 = max-width */
    uint8_t offset_unit; /* 0 = px, 1 = rem, 2 = em */
    int8_t sign;         /* +1 or -1 applied to the length term */
    float pct;           /* percentage numerator (e.g. 33.33) */
    float offset;        /* length-term magnitude in offset_unit */
};

/* An aspect-ratio height source libcss can't parse: either the modern
 * `aspect-ratio: W / H` property, or the classic pre-`aspect-ratio` idiom of a
 * `::after { padding-bottom: P% }` pseudo on a `height:0` container (an image
 * frame). Both make an element's height a fixed multiple of its width. libcss
 * drops both, so an image inside falls back to its intrinsic height and blows
 * the frame up ~3x (CNN's card thumbnails). `ratio` is height/width. */
struct yl_aspect_rule {
    char *selector;          /* owned — the ELEMENT selector (pseudo stripped) */
    void *compiled_selector; /* owned */
    uint8_t selector_state;
    float ratio; /* height / width */
};

/* A `display: none` declaration whose selector uses a Selectors-Level-4
 * construct libcss 0.9.2 cannot parse (`:is()`, `:where()`, `:has()`) — libcss
 * drops the whole rule, so an element the page means to hide renders anyway.
 * CNN's lead-plus-headlines secondary cards hide their media wrapper via
 * `.item:not(:first-child) :is(...__item-media-wrapper, ...){display:none}`;
 * without this the phantom 16:9 thumbnails add ~1000px of blank card height.
 * The selector is matched with lexbor's native engine (which does support the
 * L4 pseudos), so no de-sugaring / variant explosion is needed. */
struct yl_display_none_rule {
    char *selector;          /* owned — one comma-alternative of the rule */
    void *compiled_selector; /* owned */
    uint8_t selector_state;
};

/* A stylesheet `-webkit-line-clamp: N` declaration. Non-standard, so
 * libcss drops it; this side table re-applies the line cap at layout.
 * The clamp-to-N-lines idiom (display:-webkit-box; -webkit-box-orient:
 * vertical; -webkit-line-clamp:N; overflow:hidden) truncates multi-line
 * text to N lines — every news-card title/snippet uses it. Without the
 * cap the card grows to the full untruncated title height and every
 * card below it drifts down. */
struct yl_line_clamp_rule {
    char *selector;          /* owned */
    void *compiled_selector; /* owned */
    uint8_t selector_state;
    uint8_t lines; /* clamp line count, 1..255 */
};

/* A class-based `transform: translate*(...)` declaration. libcss drops the
 * whole `transform` property, so JS-driven carousels/tab-panels that push the
 * inactive slide off an overflow-clipped viewport (Google News weather widget:
 * `transform: translateX(calc(11.25rem + 28px))`) render stacked. This side
 * table re-applies the translate at box-build. Only the translate component is
 * modelled (scale/rotate/matrix are ignored); percent offsets keep the raw
 * number with the matching *_pct flag, resolved against the box's own size. */
struct yl_transform_rule {
    char *selector;          /* owned */
    void *compiled_selector; /* owned */
    uint8_t selector_state;
    float tx, ty;
    bool tx_pct, ty_pct;
};

/* A stylesheet `height: var(...)` declaration that may depend on
 * ELEMENT-scoPED custom properties (style="--Spacer-size:114px" on the
 * element or an ancestor). Sheet-level var() substitution runs once
 * against the GLOBAL table, so these are re-resolved per matching
 * element at box-build (github's .lp-Spacer vertical-rhythm pattern:
 * five media buckets of nested var() fallbacks, values set inline). */
struct yl_var_height_rule {
    char *selector;          /* owned */
    void *compiled_selector; /* lxb_css_selector_list_t*; owned */
    uint8_t selector_state;  /* 0 uncompiled, 1 compiled, 2 failed */
    char *raw_value;         /* owned; the declaration value as written */
};

struct yl_grid_class {
    char *cls;                               /* target class — owned; freed on document replace */
    char *context[YL_GRID_SPAN_CONTEXT_MAX]; /* owned */
    int context_count;
    struct yl_grid_track tracks[YL_GRID_MAX_TRACKS];
    uint8_t ntracks;
    /* `grid-template-columns: inherit` (gnews subgrid idiom
	 * `.Oc0wGc{display:inherit;grid-template-columns:inherit}`): ntracks
	 * is 0 here — box-build copies the tracks from the parent box. */
    uint8_t inherit_template;
    /* `repeat(auto-fit|auto-fill, <track>)`: tracks[0] holds the repeated
	 * track; box-build replicates it once per element child (with
	 * `grid-auto-flow: column` semantics — one implicit row). */
    uint8_t repeat_auto;
    float col_gap;
    float row_gap;
    /* Full selector alternative as written. Compiled lazily through the
	 * lexbor selector engine so lookups do REAL matching — combinators,
	 * attribute selectors, pseudo-classes — instead of the reduced
	 * class-key approximation (kept as the fallback when compilation
	 * fails). selector_state: 0 uncompiled, 1 compiled, 2 failed. */
    char *selector;          /* owned */
    void *compiled_selector; /* lxb_css_selector_list_t*; owned */
    uint8_t selector_state;
};

/* A class-scoped `grid-column` / `grid-row` placement parsed from the
 * author CSS (`.tile.feature { grid-column: span 2 }`,
 * `.card.wide .thumb { grid-column: 3/3 }`,
 * `.card .thumb { grid-row: 1/span 5 }`). Inline-style placement and
 * span-in-class-name (Primer) are handled at box-build; this table
 * covers the stylesheet-rule case libcss doesn't surface (no grid
 * support). The selector's final compound names the TARGET class; every
 * other class in the selector (ancestor compounds, extra classes on the
 * final compound) is kept as a loose CONTEXT requirement checked against
 * the element and its DOM ancestors at lookup time — combinators are not
 * modelled. Entries keep stylesheet order; the last matching entry wins
 * per axis (cascade approximation). */
struct yl_grid_span_class {
    char *cls;                               /* target class — owned; freed on document replace */
    char *context[YL_GRID_SPAN_CONTEXT_MAX]; /* owned */
    int context_count;
    uint8_t axis;  /* 0 = grid-column, 1 = grid-row */
    uint8_t start; /* 1-based explicit start line; 0 = auto */
    uint8_t span;
    /* Full selector alternative as written. Compiled lazily through the
	 * lexbor selector engine so lookups do REAL matching — combinators,
	 * attribute selectors, pseudo-classes — instead of the reduced
	 * class-key approximation (kept as the fallback when compilation
	 * fails). selector_state: 0 uncompiled, 1 compiled, 2 failed. */
    char *selector;          /* owned */
    void *compiled_selector; /* lxb_css_selector_list_t*; owned */
    uint8_t selector_state;
};

/* Resolved grid placement for one element, both axes (0 = auto). */
struct yl_grid_placement {
    uint8_t col_start;
    uint8_t col_span;
    uint8_t row_start;
    uint8_t row_span;
};

/* A `gap`/`column-gap` on a flex container from the author CSS
 * (`.cards { display:flex; gap:16px }`, `header nav { display:flex;
 * gap:24px }`). libcss in this tree models only the legacy multicol
 * column-gap, so these are text-scanned like the grid tables. Keyed by
 * the LAST simple selector: a class (match_tag=0) or a bare tag name
 * (match_tag=1). */
struct yl_flex_gap_class {
    char *key; /* owned; freed on document replace */
    uint8_t match_tag;
    float col_gap;
    /* Full selector alternative as written. Compiled lazily through the
	 * lexbor selector engine so lookups do REAL matching — combinators,
	 * attribute selectors, pseudo-classes — instead of the reduced
	 * class-key approximation (kept as the fallback when compilation
	 * fails). selector_state: 0 uncompiled, 1 compiled, 2 failed. */
    char *selector;          /* owned */
    void *compiled_selector; /* lxb_css_selector_list_t*; owned */
    uint8_t selector_state;
};

/* Maps a DOM element to a stable ydraw primitive-GROUP id, so paint can wrap
 * that element's prims in a CMD_GROUP the receiver (ygrid) can replace in
 * place. The id is assigned monotonically on first request and never reused
 * within the engine's life, so a re-layout that renumbers box indices does
 * NOT reassign an element's group — matching the yjungle producer model. The
 * table is keyed by the borrowed element pointer (never dereferenced) and is
 * cleared on document replace, when the old parse's element pointers die. */
struct yl_group_id_entry {
    const void *element; /* lxb element pointer — key only, never deref'd */
    uint32_t gid;
    /* FNV-1a of this group's page-coord prim payload as of the last time it
	 * was emitted (full render or delta). The incremental path repaints a
	 * candidate image group at offset 0, hashes it, and only ships a delta
	 * when the hash differs — so an unchanged image never re-ships. */
    uint64_t content_hash;
    int content_hash_valid;
};

/* Which engine decision produced a box's used width/height — stamped by
 * the layout pass at every final size assignment, emitted by
 * `--dump-boxes` (`ws=`/`hs=` columns) and clustered on by the anchor
 * comparator. The point: a geometry delta then NAMES the deciding branch
 * (`54x width src=abs-fit`) instead of requiring a manual reduction to
 * find it. Coarse on purpose — one value per mechanism, not per line of
 * code. */
enum yl_size_source {
    YL_SRC_NONE = 0,      /* never laid out */
    YL_SRC_VIEWPORT,      /* root: viewport dimensions */
    YL_SRC_CSS,           /* explicit css width/height (incl. resolved %) */
    YL_SRC_AVAIL,         /* block flow: parent's available width */
    YL_SRC_SHRINK_TO_FIT, /* promoted inline-block widget: max-content cap */
    YL_SRC_CONTENT,       /* measured max-content / laid-out content height */
    YL_SRC_FLEX_BASIS,    /* flex-basis px / % */
    YL_SRC_FLEX_EVEN,     /* all-auto even split */
    YL_SRC_FLEX_SHARE,    /* mixed-auto leftover share (content-capped) */
    YL_SRC_FLEX_GROW,     /* grew into positive leftover */
    YL_SRC_FLEX_SHRINK,   /* shrunk by main-axis overflow */
    YL_SRC_FLEX_MIN,      /* min-width floor reclaim */
    YL_SRC_FLEX_STRETCH,  /* cross-axis align-items:stretch */
    YL_SRC_GRID_TRACKS,   /* grid cell width from tracks (+span) */
    YL_SRC_GRID_STRETCH,  /* grid row-height stretch */
    YL_SRC_TABLE_COLS,    /* table column distribution */
    YL_SRC_TABLE_STRETCH, /* table cell equalized to its row's height */
    YL_SRC_ABS_INSET,     /* absolute: both insets pinned the size */
    YL_SRC_ABS_FIT,       /* absolute: shrink-to-fit under one/no inset */
    YL_SRC_ABS_STRETCH,   /* absolute: top+bottom stretched an auto-height box */
    YL_SRC_IMG_INTRINSIC, /* replaced intrinsic size (+aspect caps) */
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

    /* Static position — where this box's border box WOULD land in normal
	 * flow. Recorded (page coordinates) by the flow pass when it skips an
	 * absolutely-positioned / fixed child; layout_absolute_child falls
	 * back to it when neither inset of an axis is set (the spec's
	 * static-position rectangle, instead of the containing-block origin). */
    float static_x, static_y;
    bool static_pos_set;

    /* Style snapshot used at layout + paint time. Resolved from the
	 * lexbor computed style during box generation. */
    struct yetty_ylexbor_color bg;
    struct yetty_ylexbor_color fg;
    float font_size;
    int font_weight; /* CSS weight; 400 = normal, 700 = bold */
    bool font_italic;

    /* Per-box glyph advance as a fraction of font-size, overriding the engine
		 * default. Set to 1.0 for `font-family: Ahem` (the WPT test font where
		 * every glyph is exactly 1em wide) so text-dependent geometry is exact.
		 * 0 = use the engine-global ratio. */
    float glyph_advance;

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

    /* Deferred precise calc(<pct> ± <len>) width. css_width's negative-ratio
	 * convention can carry a percentage OR a fixed px, but not both, so a
	 * calc that mixes them is stored here and finalized in resolve_pct_metrics
	 * as calc_width_pct/100 * cb_width + calc_width_offset once the containing
	 * block width is known. calc_width_set gates it (0 = none). */
    float calc_width_pct;
    float calc_width_offset;
    uint8_t calc_width_set;

    /* Promoted inline-level box (inline-block / inline-flex widget with
	 * no surrounding text run): block layout sizes it to its content
	 * (max-content, capped at the available width) instead of the full
	 * parent content area. */
    bool shrink_to_fit;

    /* INLINE_TEXT run containing `white-space: nowrap` content — the
	 * wrap pass emits it as a single line (explicit '\n' still breaks). */
    bool nowrap;

    /* Size provenance (enum yl_size_source) — which layout decision
	 * produced the final w/h. Diagnostic only; never read back by
	 * layout. */
    uint8_t width_source;
    uint8_t height_source;
    float css_height; /* explicit height — placeholder for tables/img */
    /* Height was explicitly specified in CSS (including `height:0`), as opposed
	 * to auto. Distinguishes a real `height:0` — which with a percentage
	 * padding-bottom is the aspect-ratio-box idiom (`height:0;padding-bottom:
	 * 56.25%`) whose box height is padding-only and must NOT grow to fit an
	 * overflowing image — from the "unset" default that css_height==0 also
	 * represents. */
    uint8_t css_height_set;
    /* Aspect-ratio height/width from the side table (0 = none). When the box
	 * has no explicit height, layout sets height = width * aspect_ratio (the
	 * `aspect-ratio` property or a `::after{padding-bottom:%}` image frame). */
    float aspect_ratio;
    /* Replaced element (image) sized `height: <pct>` (e.g. 100%). At box-build
	 * there is no containing-block height, so the image falls back to its
	 * intrinsic/attr size; layout binds it to the nearest ancestor with a
	 * definite height (an aspect frame, an explicit height) so a portrait image
	 * inside a 16:9 frame fills the frame instead of overflowing to ~3x. Stored
	 * as the fraction (1.0 = 100%); 0 = not a percentage height. */
    float img_pct_h;
    /* Set once layout has pinned this image's height to a definite frame (see
	 * img_pct_h). object-fit:cover fills the frame in BOTH axes, so the flex
	 * responsive-downscale (which preserves aspect ratio) must cap the width
	 * without re-scaling the pinned height. */
    uint8_t img_frame_filled;
    uint8_t line_clamp; /* -webkit-line-clamp cap (0 = none), from side table */

    /* Flex item properties (only meaningful when this box is the
	 * child of a flex container). flex_basis_px < 0 = auto; >= 0 =
	 * explicit px (resolved from libcss). flex_grow = 0 means the
	 * item doesn't take leftover space. */
    float flex_grow;
    /* flex-shrink factor. -1 = unset (treated as the CSS initial 1.0 by the
		 * flex solver); 0 = never shrink (Tailwind `shrink-0`, fixed sidebars);
		 * >0 = shrink weight when items overflow the main axis. */
    float flex_shrink;
    float flex_basis_px;
    /* Intrinsic width keyword from the side table (yl_width_keyword_rule
	 * values); 0 = none. Layout sizes the box from its content measure. */
    uint8_t width_keyword;
    /* visibility:hidden|collapse — box keeps its layout space, paints
	 * nothing, and is transparent to hit-testing. Computed per element
	 * (inheritance + visibility:visible re-show fall out of the cascade). */
    uint8_t vis_hidden;

    /* Flex-container `flex-wrap`. 0 = nowrap (default, single line);
	 * 1 = wrap (items overflowing the main axis move to the next flex
	 * line). Only acted on for FLEX_ROW. */
    uint8_t flex_wrap;
    uint8_t main_reverse; /* container: flex-direction row/column-REVERSE */
    uint8_t wrap_reverse; /* container: flex-wrap: wrap-reverse */
    int32_t flex_order;   /* item: CSS `order` (0 default) */
    /* item: CSS align-self as a CSS_ALIGN_ITEMS_-compatible value
	 * (the libcss enums alias); 0 / AUTO = defer to the container. */
    int8_t align_self;

    /* Flex-container properties (meaningful when this box has
	 * layout_mode == YL_LAYOUT_FLEX_ROW/COLUMN). Values are the
	 * libcss CSS_JUSTIFY_CONTENT_* / CSS_ALIGN_ITEMS_* enums (zero =
	 * inherit / default, treated as flex-start for justify and
	 * stretch for align). */
    int justify_content;
    int align_items;
    /* CSS_ALIGN_CONTENT_* — distributes/sizes flex LINES on the cross axis when
		 * the container wraps. 0 = default (stretch). */
    int align_content;

    /* Grid-container tracks (meaningful when layout_mode == YL_LAYOUT_GRID).
	 * Parsed from the author CSS's grid-template-columns (libcss exposes none
	 * of it). grid_ntracks == 0 means "not a resolved grid". */
    struct yl_grid_track grid_tracks[YL_GRID_MAX_TRACKS];
    uint8_t grid_ntracks;
    float grid_col_gap;
    float grid_row_gap;

    /* `table-layout: fixed` on a TABLE box. When set and the table has a
		 * definite width, columns are sized equally (content-blind) instead of
		 * the default content-aware auto layout. */
    uint8_t table_fixed;

    /* Named-line grid placement. On a GRID container, `grid_line_spec` points
		 * (into the text arena) at the raw grid-template-columns value when it
		 * carries `[line-name]`s and every child uses `grid-column:<name>` — the
		 * trigger for explicit named placement (vs. the default row-major
		 * auto-flow). On a grid ITEM, `grid_col_name` is the arena-stored
		 * `grid-column` start line name. Both NULL otherwise; arena-owned, so
		 * freed with the document. */
    const char *grid_line_spec;
    const char *grid_col_name;

    /* `grid-column: span N` (or `auto / span N`) on a grid ITEM — the number of
		 * columns it occupies when auto-flowed into a grid container. 0/1 = a
		 * single column (default). github's 12-col layout places every card with
		 * one of these spans. */
    uint8_t grid_col_span;

    /* Explicit numeric grid lines on a grid ITEM (`grid-column: 3/3`,
		 * `grid-row: 1/span 5` — the gnews card pattern: thumbnail pinned to
		 * the right track spanning the text rows). 0 = auto (flow-placed).
		 * grid_row_span pairs with grid_row_start the way grid_col_span pairs
		 * with grid_col_start. */
    uint8_t grid_col_start;
    uint8_t grid_row_start;
    uint8_t grid_row_span;

    /* Float / clear. float_side: 0=none, 1=left, 2=right.
	 * clear_side: 0=none, 1=left, 2=right, 3=both. Boxes with
	 * float_side != 0 are removed from the parent's normal flow at
	 * layout time; subsequent in-flow content flows around them. */
    uint8_t float_side;
    uint8_t clear_side;

    /* `overflow-x`/`overflow-y` resolve to a clip (hidden/clip/scroll/auto) on
	 * at least one axis — descendants are clipped to this box's padding box.
	 * How a collapsed carousel slide (Google News weather widget) hides its
	 * out-of-flow content instead of spilling it over the active slide. */
    bool clip_overflow;

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

    /* CSS `z-index`. Only meaningful on a positioned box. `z_index_set` = 0
	 * means `auto` (paints with the z=0 group, above normal flow). A set,
	 * negative z-index paints BELOW normal flow. Drives the stacking-order
	 * paint pass so overlays (dialogs, modals, dropdowns, fixed headers) paint
	 * on top of in-flow content regardless of DOM order. */
    int32_t z_index;
    uint8_t z_index_set;

    /* CSS `transform: translate()` — a visual shift that moves the box and
		 * its whole subtree without affecting flow (siblings ignore it). Only
		 * the translate component is modelled; scale/rotate/matrix are not.
		 * Percent translates resolve against the box's OWN border-box size
		 * (the `translate(-50%,-50%)` centering idiom), not the containing
		 * block. Parsed from the inline style at box-build. */
    bool has_transform;
    float tf_tx, tf_ty;
    bool tf_tx_pct, tf_ty_pct;

    /* Effective CSS `opacity` in [0,1]: the box's own opacity multiplied by
	 * every ancestor's (opacity is a group effect that is NOT inherited, so
	 * it is folded down the subtree at box-build). A near-zero value means
	 * the box and its subtree paint nothing — how JS carousels/tab-panels
	 * hide the inactive slide (Google News weather widget) without pulling it
	 * out of flow. 1.0 = fully opaque (the default). */
    float opacity;

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
    /* Index of the box that owns this one (set by link_child). 0 for the root
	 * and for any unlinked box; since boxes are allocated in tree order the
	 * root is always index 0, so a `parent == 0` on a non-root box means the
	 * root — the paint/hit clip walk stops when it reaches index 0. Used to
	 * find overflow-clipping ancestors from the flat paint loop. */
    uint32_t parent;

    /* When this box is an <iframe> whose src resolved to a document, points at
	 * the child engine that rendered it (a nested browsing context). Paint
	 * composites the child's drawables into this box's content area. Borrowed —
	 * the child is OWNED by the engine's iframe_children list and destroyed on
	 * document replace / engine destroy. NULL for every non-iframe box. */
    struct yetty_ylexbor *iframe_doc;
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
    /* Coarse selector specificity of the defining rule (classes/pseudo
	 * x10 + tags x1; `*` = 0). A later definition only overrides when
	 * its specificity is >= the stored one — `html { --x }` no longer
	 * beats an earlier `:root.theme-light { --x }`. */
    uint16_t specificity;
};

struct yetty_ylexbor_customs {
    struct yetty_ylexbor_custom_prop *data;
    int size, cap;
};

/* Forward-declare libcss bridge state — definition in
 * ybrowser-libcss.h. Kept opaque here so TUs that don't touch libcss
 * (paint / layout / js) don't need its headers. */
struct yetty_ybrowser_libcss;

/* String→string map backing the JS localStorage / sessionStorage
 * bindings. Grows on demand; owned by the engine and freed with it, so
 * two engines (tabs) never see each other's keys. */
struct yetty_ylexbor_kv_entry {
    char *key;
    char *value;
};

struct yetty_ylexbor_kv_store {
    struct yetty_ylexbor_kv_entry *items;
    int count, cap;
};

/* DevTools console ring. Every page console.* call and every REPL
 * evaluation is recorded here so a UI (the standalone ybrowser DevTools
 * panel) can display a live JavaScript console. Independent of QuickJS:
 * the ring is plain storage; only the producers (console.* capture,
 * eval) live behind the YETTY_HAVE_QUICKJS guard. */
#define YETTY_YLEXBOR_CONSOLE_CAP 2000

struct yetty_ylexbor_console_entry {
    int level;  /* enum yetty_ylexbor_console_level */
    char *text; /* owned */
};

struct yetty_ylexbor {
    /* Network loader (share handle, Alt-Svc cache). Either borrowed from
	 * the host via config (owns_loader = 0) or created privately at
	 * engine create (owns_loader = 1, destroyed with the engine). Never
	 * NULL after a successful create when libcurl is compiled in. */
    struct yetty_ybrowser_loader *loader;
    int owns_loader;

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

    /* JS-initiated navigation request (location.assign/replace/reload or
	 * location.href = …). Recorded here for the host to pick up and drive a
	 * real navigate() next frame; owned, cleared by the take-getter. NULL when
	 * no navigation is pending. */
    char *pending_navigation;

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

    /* DevTools console ring (lazily allocated on first push, CAP slots).
     * console_head is the next write slot; console_count is the number of
     * valid entries (<= CAP); console_total is the monotonic lifetime count
     * of pushes, which a UI compares against to detect newly-arrived lines. */
    struct yetty_ylexbor_console_entry *console_ring;
    int console_head;
    int console_count;
    uint64_t console_total;

    /* JS web-storage + document.cookie backing. Engine-owned so each
	 * document/tab gets its own map — these were process-wide once and
	 * leaked keys and cookies across tabs. Freed on destroy. Persistence
	 * across navigations (real localStorage semantics) arrives with the
	 * loader/profile object. */
    struct yetty_ylexbor_kv_store web_local_storage;
    struct yetty_ylexbor_kv_store web_session_storage;
    char *web_cookie_string;

    /* Stylesheet-load tallies for the post-load debug line: external
	 * sheets applied / failed, inline <style> blocks applied. Reset at
	 * each load_html. */
    int css_sheets_loaded;
    int css_sheets_failed;
    int css_sheets_inline;
    /* When set, paint does NOT fetch <img> URLs over the network — it
     * draws a placeholder for any not-yet-cached image and leaves the URL
     * pending. Keeps paint (which runs on the host render / event-loop
     * thread) from blocking for seconds on HTTP. The host then pulls one
     * image at a time off the critical path via
     * yetty_ylexbor_fetch_one_pending_image and re-renders, so the page
     * fills in progressively while staying responsive. `data:` URIs still
     * decode inline. Default 0 — one-shot callers keep synchronous fetch. */
    int defer_image_fetch;
    /* When set, yetty_ylexbor_load_html does NOT run <script> blocks — it
	 * parses + applies CSS + lays out only, so the host can paint the initial
	 * HTML/CSS content immediately. The host then calls
	 * yetty_ylexbor_run_deferred_scripts() once after that first paint. Drives
	 * progressive rendering in the interactive UI. */
    int defer_scripts;
    int dom_dirty; /* JS mutated the DOM — host should
	                            * relayout. */
    /* A GPU repaint is owed. Set alongside dom_dirty on every DOM/style
     * mutation, but — unlike dom_dirty — NOT cleared by a geometry getter's
     * layout flush (getBoundingClientRect/clientWidth). A getter flush makes the
     * layout current (clears dom_dirty) yet the framebuffer is still stale, so
     * the render loop must consult this to know a paint is still pending. Without
     * it, code that mutates then measures in the same JS turn (iron-overlay
     * positioning its dialog then reading getBoundingClientRect) clears dom_dirty
     * before the render loop looks, and the modal is never repainted. Cleared by
     * the host once it ships the frame (yetty_ylexbor_mark_painted). */
    int needs_paint;
    /* Set while a box-build + layout pass is running (see
     * yetty_ylexbor_relayout_boxes_and_layout). The layout-dependent geometry
     * getters (clientWidth/offsetWidth/getBoundingClientRect) check this before
     * forcing a synchronous flush and skip the flush when a layout is already
     * in progress, so a geometry read reached from inside box-build/layout
     * returns the in-progress layout instead of recursing. This is a shared
     * layout-state signal, not a lock: relayout_boxes_and_layout sets/clears it
     * but does not itself reject a reentrant non-geometry caller. */
    int layout_in_progress;

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
    /* Element-var-dependent height rules (see yl_var_height_rule). */
    struct yl_var_height_rule *var_height_rules;
    int var_height_count, var_height_cap;

    /* Intrinsic width keywords (see yl_width_keyword_rule). */
    struct yl_width_keyword_rule *width_keyword_rules;
    int width_keyword_count, width_keyword_cap;

    /* Precise calc(<pct> ± <len>) widths (see yl_calc_length_rule). */
    struct yl_calc_length_rule *calc_length_rules;
    int calc_length_count, calc_length_cap;

    /* Aspect-ratio height sources (see yl_aspect_rule). */
    struct yl_aspect_rule *aspect_rules;
    int aspect_count, aspect_cap;

    /* `display:none` rules libcss dropped for L4 selectors (see
	 * yl_display_none_rule). */
    struct yl_display_none_rule *display_none_rules;
    int display_none_count, display_none_cap;

    /* -webkit-line-clamp line caps (see yl_line_clamp_rule). */
    struct yl_line_clamp_rule *line_clamp_rules;
    int line_clamp_count, line_clamp_cap;

    struct yl_transform_rule *transform_rules;
    int transform_count, transform_cap;

    struct yl_grid_class *grid_classes;
    int grid_class_count;
    int grid_class_cap;

    /* Class-scoped `grid-column: span N` rules from author CSS (see
     * yetty_ylexbor_css_scan_grid_spans). Freed with grid_classes. */
    struct yl_grid_span_class *grid_span_classes;
    int grid_span_class_count;
    int grid_span_class_cap;

    /* Flex-container gaps from author CSS (see
     * yetty_ylexbor_css_scan_flex_gaps). Freed with grid_classes. */
    struct yl_flex_gap_class *flex_gap_classes;
    int flex_gap_class_count;
    int flex_gap_class_cap;

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

    /* Precomputed @media-active map for the stylesheet source currently
     * being scanned. Built once by css_media_map_begin() at the top of
     * add_css_from and cleared by css_media_map_end() after the scanners
     * run, so the per-declaration scanners consult it in O(log n) instead
     * of each re-walking the source prefix (which was O(n^2) per sheet). */
    struct media_inactive_map *css_media_map;

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

    /* Shared lexbor selector-match engine for the supplementary
	 * cascade lookups (grid templates / spans / flex gaps). Lazy;
	 * destroyed with the engine. */
    void *supp_selector_matcher; /* lxb_selectors_t* */

    /* Rendered inline-<svg> scenes, keyed by element (see
	 * yetty_ylexbor_svg_inline_entry). */
    struct yetty_ylexbor_svg_inline_entry *svg_inline_cache;
    int svg_inline_count, svg_inline_cap;

    /* Element→group-id table (see struct yl_group_id_entry). Paint uses it to
	 * give each <img> box a stable CMD_GROUP id so a landed image ships as a
	 * per-group delta instead of a full-page reship. `next_group_id` is a
	 * monotonic counter (0 is reserved for the receiver's root entity); it is
	 * NOT reset on document replace so an id is never reused, while the map
	 * itself is cleared because the old element pointers die with the parse. */
    struct yl_group_id_entry *group_ids;
    int group_id_count, group_id_cap;
    uint32_t next_group_id;

    /* Incremental-render gate. `last_layout_hash` is an FNV-1a over every
	 * box's (kind, x, y, w, h) as of the last FULL render; `have_full_render`
	 * is set once a full render has established the page's groups on the
	 * receiver. The image-delta path only fires when a fresh relayout
	 * reproduces the same layout hash — i.e. images landed but nothing
	 * shifted — otherwise it defers to a full render. */
    uint64_t last_layout_hash;
    int have_full_render;

    /* Async image fetching. When `img_pool` is set, yetty_ylexbor_start_image_fetch
	 * submits each pending <img> to the worker pool (parallel fetch+decode on
	 * background threads) instead of blocking the caller. `on_resource_ready`
	 * is invoked on the loop thread when a fetch lands so the host repaints.
	 * `img_jobs_in_flight` and `destroy_pending` are touched ONLY on the loop
	 * thread (submit + done + load_html + destroy), so no locking is needed.
	 * `fetch_generation` is bumped on document replace so a late job from a
	 * previous page is discarded — it is ATOMIC because worker-thread transfer
	 * callbacks poll it (via request->cancel_generation) while the loop thread
	 * increments. `destroy_pending` defers the real teardown until the last
	 * in-flight job's done() runs — a job's done() must never touch a freed
	 * engine. */
    struct yetty_yplatform_yworkpool *img_pool;
    void (*on_resource_ready)(void *user);
    void *resource_ready_user;
    int img_jobs_in_flight;
    int destroy_pending;
    _Atomic uint64_t fetch_generation;

    /* Nested browsing contexts. Each <iframe> with a fetchable src gets a child
	 * engine that renders its document; paint composites the child's drawables
	 * into the iframe box. OWNED — destroyed on document replace and at engine
	 * destroy. `iframe_depth` guards against unbounded nesting (an iframe whose
	 * document iframes onward); the top-level document is 0. */
    struct yetty_ylexbor **iframe_children;
    int iframe_child_count, iframe_child_cap;
    int iframe_depth;
};

struct yetty_ylexbor_img_cache_entry {
    char *url;        /* owned */
    uint32_t *pixels; /* RGBA8 row-major; owned — NULL for vector entries */
    int w, h;         /* source pixel dims (scene dims for vectors) */
    int failed;       /* fetch/decode failed — don't retry */
    int loading;      /* an async fetch job is in flight for this url */
    /* Vector images: an SVG source renders through ysvg into a drawable
	 * list that paint MERGES into the page's output under the image box's
	 * transform — vector primitives survive, nothing is rasterized. */
    struct yetty_ydraw_drawable_list *svg_scene; /* owned; NULL for rasters */
    float svg_min_x, svg_min_y;                  /* scene-space origin */
    float svg_w, svg_h;                          /* scene-space extent */
    /* Root preserveAspectRatio — see yetty_ylexbor_svg_inline_entry. */
    float svg_par_align_x, svg_par_align_y;
    uint8_t svg_par_mode;
    /* Display-size downscale of `pixels`, produced at paint when the
	 * source is much larger than its layout box. Shipping full-res
	 * pixels ballooned page envelopes past the 16 MiB RPC body cap —
	 * every styled repaint got dropped and the client froze on the
	 * first unstyled frame. Keyed by the box size that produced it. */
    uint32_t *scaled_pixels; /* owned; NULL until first needed */
    int scaled_w, scaled_h;
};

/* Inline <svg> elements have no URL — their rendered scenes cache per
 * DOM element. Cleared on document replace (element pointers die with
 * the parse) and at destroy. */
struct yetty_ylexbor_svg_inline_entry {
    const void *element; /* lxb element pointer — cache key only, never deref'd */
    struct yetty_ydraw_drawable_list *scene; /* owned; NULL when failed */
    float min_x, min_y, w, h;
    /* preserveAspectRatio of the root element: align factors are 0 / 0.5 /
	 * 1 for Min / Mid / Max; mode 0 = meet, 1 = slice, 2 = none. */
    float par_align_x, par_align_y;
    uint8_t par_mode;
    /* SVG-internal <a> click regions (scene pixel space); owned, freed
	 * with the entry via yetty_ysvg_links_free. */
    struct yetty_ysvg_link_region *links;
    size_t link_count;
};

/* Render `bytes` through ysvg. On success fills scene + scene-space frame
 * and returns 1; returns 0 when ysvg cannot parse/render the source.
 * Defined in ybrowser-paint.c. */
int yetty_ylexbor_svg_scene_render(struct yetty_ylexbor *r, const char *bytes, size_t len,
                                   float default_font_px,
                                   struct yetty_ydraw_drawable_list **out_scene, float *out_min_x,
                                   float *out_min_y, float *out_w, float *out_h,
                                   struct yetty_ysvg_link_region **out_links,
                                   size_t *out_link_count);

/* ===========================================================================
 * Disk tier of the loader's HTTP cache (RFC 9111 private cache). The
 * loader decides freshness / revalidation / Vary; this layer persists
 * entries across restarts. Implemented in ybrowser-cache-disk.c.
 * ===========================================================================*/

struct yetty_ybrowser_disk_cache {
    char dir[1024]; /* empty + enabled==0 → memory-only operation */
    int enabled;
    pthread_mutex_t mutex;
};

struct yetty_ybrowser_disk_cache_meta {
    long status;
    time_t expires_at; /* absolute; in the past = stale-but-revalidatable */
    time_t stored_at;
    char etag[256];
    char last_modified[128];
    char content_type[160];
    char effective_url[1024];
};

struct yetty_ycore_void_result yetty_ybrowser_disk_cache_init(
    struct yetty_ybrowser_disk_cache *cache);
void yetty_ybrowser_disk_cache_shutdown(struct yetty_ybrowser_disk_cache *cache);
int yetty_ybrowser_disk_cache_load(struct yetty_ybrowser_disk_cache *cache, const char *url,
                                   int kind, struct yetty_ybrowser_disk_cache_meta *meta,
                                   char **out_body, size_t *out_body_len);
void yetty_ybrowser_disk_cache_store(struct yetty_ybrowser_disk_cache *cache, const char *url,
                                     int kind, const struct yetty_ybrowser_disk_cache_meta *meta,
                                     const char *body, size_t body_len);

/* The transform svg_scene_merge applies (scene → page px), exposed so the
 * click hit-test can invert it. Defined in ybrowser-paint.c. */
void yetty_ylexbor_svg_merge_transform(float min_x, float min_y, float scene_w, float scene_h,
                                       float par_align_x, float par_align_y, uint8_t par_mode,
                                       float box_x, float box_y, float box_w, float box_h,
                                       float *out_scale_x, float *out_scale_y, float *out_offset_x,
                                       float *out_offset_y);

/* Inline-svg cache lookup WITHOUT creating (hit-test path). */
struct yetty_ylexbor_svg_inline_entry *yetty_ylexbor_svg_inline_find(struct yetty_ylexbor *r,
                                                                     const void *element);

/* Parse the ROOT <svg> tag's preserveAspectRatio into align factors
 * (0 / 0.5 / 1 for Min / Mid / Max) and mode (0 = meet, 1 = slice,
 * 2 = none). Missing / malformed → the xMidYMid meet defaults. */
void yetty_ylexbor_svg_parse_preserve_aspect(const char *bytes, size_t len, float *out_align_x,
                                             float *out_align_y, uint8_t *out_mode);

/* Destroy every cached inline-<svg> scene and reset the cache. Called on
 * document replace (the element keys die with the old parse) and from the
 * engine teardown. Defined in ybrowser-paint.c. */
void yetty_ylexbor_svg_inline_cache_clear(struct yetty_ylexbor *r);

/* ===========================================================================
 * iron-iconset icons. Polymer sites (YouTube, Guardian, …) render vector
 * icons as <yt-icon icon="set:name"> / <iron-icon icon="set:name"> whose
 * glyph geometry lives in a document-level
 *   <iron-iconset-svg name="set"> … <svg><defs><g id="name"><path …></g> …
 * The web component copies that <g> into an <svg> at runtime; our JS engine
 * doesn't run that step, so the icon paints empty. These two helpers let the
 * box builder treat such an element as a replaced box and the painter
 * resolve + render its glyph. Defined in ybrowser-box.c.
 * ===========================================================================*/

/* True when `el` is a <yt-icon>/<iron-icon> carrying an `icon` attribute.
 * Splits the value into set:name (bare "name" implies the default set
 * "icons"). The out pointers alias into the live attribute buffer — valid
 * only while the element is alive; copy if you need to outlive it. */
bool yetty_ylexbor_icon_element_ref(lxb_dom_element_t *el, const char **out_set,
                                    size_t *out_set_len, const char **out_name,
                                    size_t *out_name_len);

/* Resolve an icon element to the document <g id="name"> (or <svg id="name">)
 * inside its matching <iron-iconset-svg name="set">. Returns NULL when the
 * element is not an icon or the iconset/glyph is absent. `*out_view_size`
 * receives the iconset `size` attribute (viewBox side length; default 24). */
lxb_dom_element_t *yetty_ylexbor_icon_resolve(struct yetty_ylexbor *r, lxb_dom_element_t *icon_el,
                                              float *out_view_size);

/* Return the stable ydraw primitive-GROUP id for `element`, assigning a fresh
 * monotonic id on first request (see struct yl_group_id_entry). `element` must
 * be non-NULL. Defined in ybrowser-paint.c. */
uint32_t yetty_ylexbor_group_id_for(struct yetty_ylexbor *r, const void *element);

/* Free the element→group-id map. Called on document replace and engine
 * teardown. Leaves next_group_id untouched so ids are never reused. */
void yetty_ylexbor_group_ids_clear(struct yetty_ylexbor *r);

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

/* Build (begin) / tear down (end) the per-source @media-active map on
 * `r->css_media_map`. begin() does one linear pass over `css_source`,
 * recording the byte ranges inside non-matching @media blocks for the
 * current viewport; the scanners then test a position in O(log n) via
 * grid_media_active_at instead of re-walking the prefix each time. end()
 * frees it. `css_source` must stay alive between the two calls. */
void yetty_ylexbor_css_media_map_begin(struct yetty_ylexbor *r, const char *css_source, size_t len);
void yetty_ylexbor_css_media_map_end(struct yetty_ylexbor *r);

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
/* Scan author CSS for class-scoped `grid-column` / `grid-row` placement
 * declarations (`span N`, `A`, `A/B`, `A/span N`). Comma lists and
 * descendant/child chains are accepted: each alternative is keyed by the
 * last class of its final compound, with the selector's remaining classes
 * stored as loose ancestor-context requirements. Consumed at box-build
 * for grid items whose placement isn't set inline or encoded in a class
 * name. */
void yetty_ylexbor_css_scan_grid_spans(struct yetty_ylexbor *r, const char *css_source,
                                       size_t css_len);

/* Resolve both-axis grid placement for an element against the scanned
 * table: the element must carry an entry's target class and every context
 * class must appear on the element or one of its DOM ancestors. The last
 * matching entry per axis (stylesheet order) wins. Fields are 0 where no
 * entry matched. */
struct yl_grid_placement yetty_ylexbor_grid_span_class_lookup(struct yetty_ylexbor *r,
                                                              const lxb_dom_element_t *element);

/* Parse a `grid-column` / `grid-row` declaration value ([val_start,
 * val_end) in `src`) into a 1-based start line (0 = auto) and a span.
 * Accepted forms: `span N`, `A`, `A/B` (span B-A), `A/span N`,
 * `auto/span N`. Returns 0 for values it can't model (named lines,
 * negative lines). */
int yetty_ylexbor_grid_parse_placement(const char *src, size_t val_start, size_t val_end,
                                       int *out_start, int *out_span);

/* Scan author CSS for `gap`/`column-gap` on display:flex rules. */
/* Scan for `height: var(...)` declarations and record (selector, raw
 * value) so box-build can re-resolve them against element-scoped custom
 * properties. Media-gated rules are captured only when active; capture
 * order preserves the cascade (last matching rule wins at lookup). */
void yetty_ylexbor_css_scan_var_heights(struct yetty_ylexbor *r, const char *css_source,
                                        size_t css_source_len);

/* Last matching var-height rule for `element`, resolved element-scoped
 * into pixels. Returns 1 and sets *out_px on a definite result. */
int yetty_ylexbor_var_height_lookup(struct yetty_ylexbor *r, lxb_dom_element_t *element,
                                    float font_size, float *out_px);

/* Scan for intrinsic width keywords and record (selector, keyword). */
void yetty_ylexbor_css_scan_width_keywords(struct yetty_ylexbor *r, const char *css_source,
                                           size_t css_source_len);

/* Last matching width-keyword rule for `element`: 0 = none, else the
 * yl_width_keyword_rule keyword value. */
int yetty_ylexbor_width_keyword_lookup(struct yetty_ylexbor *r, lxb_dom_element_t *element);

/* Scan for `width|min-width|max-width: calc(<pct> ± <len>)` and record the
 * terms per selector (see yl_calc_length_rule). */
void yetty_ylexbor_css_scan_calc_lengths(struct yetty_ylexbor *r, const char *css_source,
                                         size_t css_source_len);

/* Last matching calc-length rule for `element` on `prop` (0 = width,
 * 1 = min-width, 2 = max-width). Returns 1 and sets *out_pct (percentage
 * numerator, e.g. 33.33) and *out_offset_px (sign-folded length term in px)
 * on a match; layout finalizes pct/100 * cb_width + offset. Else 0. */
int yetty_ylexbor_calc_length_lookup(struct yetty_ylexbor *r, lxb_dom_element_t *element,
                                     uint8_t prop, float font_size, float *out_pct,
                                     float *out_offset_px);

/* Scan for aspect-ratio height sources — `aspect-ratio:W/H` and the
 * `SEL::after{padding-bottom:P%}` frame idiom — and record (selector, ratio). */
void yetty_ylexbor_css_scan_aspect_ratios(struct yetty_ylexbor *r, const char *css_source,
                                          size_t css_source_len);

/* Last matching aspect-ratio rule for `element`: returns its height/width ratio,
 * or 0 if none. Layout sets height = width * ratio when height is auto. */
float yetty_ylexbor_aspect_ratio_lookup(struct yetty_ylexbor *r, lxb_dom_element_t *element);

/* Scan for `display:none` rules whose selector uses an L4 pseudo libcss drops
 * (`:is()`, `:where()`, `:has()`) and record each selector alternative. */
void yetty_ylexbor_css_scan_display_none(struct yetty_ylexbor *r, const char *css_source,
                                         size_t css_source_len);

/* 1 if `element` matches a scanned L4 `display:none` rule (should be hidden),
 * else 0. */
int yetty_ylexbor_display_none_lookup(struct yetty_ylexbor *r, lxb_dom_element_t *element);

/* Scan for `-webkit-line-clamp: N` and record (selector, line count). */
void yetty_ylexbor_css_scan_line_clamps(struct yetty_ylexbor *r, const char *css_source,
                                        size_t css_source_len);

/* Last matching line-clamp rule for `element`: 0 = none, else the line cap. */
int yetty_ylexbor_line_clamp_lookup(struct yetty_ylexbor *r, lxb_dom_element_t *element);

/* True if box `idx` lies entirely outside an overflow-clipping ancestor's
 * padding box (so it must not paint / hit-test). */
bool yetty_ylexbor_box_clipped_out(const struct yetty_ylexbor *r, uint32_t idx);

/* Paint/stacking order of boxes `a` vs `b`: <0 if `a` paints before (below)
 * `b`, >0 if after (above), 0 if same. Reproduces CSS stacking order (nesting-
 * aware). Shared by the paint pass and hit-testing so a click targets the box
 * actually painted on top. */
int yetty_ylexbor_paint_order_cmp(const struct yetty_ylexbor *r, uint32_t a, uint32_t b);

/* Scan for class-based `transform: translate*(...)` and record
 * (selector, tx, ty, pct flags). Complements the inline-transform parse. */
void yetty_ylexbor_css_scan_transforms(struct yetty_ylexbor *r, const char *css_source,
                                       size_t css_source_len);

/* Last matching class-based transform for `element`. Returns 1 and fills the
 * outputs on a match, 0 otherwise. Percent offsets come back as the raw
 * number with the *_pct flag set. */
int yetty_ylexbor_transform_lookup(struct yetty_ylexbor *r, lxb_dom_element_t *element,
                                   float *out_tx, float *out_ty, bool *out_tx_pct,
                                   bool *out_ty_pct);

void yetty_ylexbor_css_scan_flex_gaps(struct yetty_ylexbor *r, const char *css_source,
                                      size_t css_len);

/* Element-scoped var() resolution for inline styles — style="--x: v"
 * definitions on the element/ancestors beat the global table. Caller
 * frees. Defined in ybrowser-css-vars.c. */
char *yetty_ylexbor_css_vars_resolve_for_element(struct yetty_ylexbor *r,
                                                 const lxb_dom_element_t *element, const char *css,
                                                 size_t len);

/* Resolve a flex gap for an element: real selector matching against the
 * scanned table first, class-attribute / tag-name key fallback for
 * entries whose selector could not compile. Returns the gap in px, or 0. */
float yetty_ylexbor_flex_gap_lookup(struct yetty_ylexbor *r, const lxb_dom_element_t *element,
                                    const char *class_attr, size_t class_len, const char *tag_name,
                                    size_t tag_len);

/* Expand every `flex: …` shorthand declaration in `css_source` into its
 * longhands (flex-grow / flex-shrink / flex-basis). libcss in this tree
 * parses the longhands but not the shorthand, and real pages set flex
 * almost exclusively via the stylesheet shorthand. Returns a malloc'd
 * rewritten stylesheet (caller frees, *out_len set) or NULL when nothing
 * needed expanding. */
char *yetty_ylexbor_css_expand_flex(const char *css_source, size_t css_len, size_t *out_len);

void yetty_ylexbor_css_scan_grid_templates(struct yetty_ylexbor *r, const char *css_source,
                                           size_t len);

/* Parse `grid-template-columns` (+ gaps) from an inline `style` attribute
 * string into `out` (up to maxn tracks). Returns the track count, 0 if absent.
 * For grids declared directly on an element rather than via a stylesheet
 * class. */
int yetty_ylexbor_grid_parse_inline(const char *style, size_t len, struct yl_grid_track *out,
                                    int maxn, float *col_gap, float *row_gap);

/* Variant that ALSO accepts named-line templates (`[name] 200px [c-start] 1fr`)
 * and returns the grid-template-columns value substring (out_value/_len) for
 * later `grid-column:<name>` resolution. Used on the explicit named path. */
int yetty_ylexbor_grid_parse_inline_named(const char *style, size_t len, struct yl_grid_track *out,
                                          int maxn, float *col_gap, float *row_gap,
                                          const char **out_value, size_t *out_value_len);

/* Resolve a grid line name to its line index (== start track for a start-only
 * `grid-column:<name>`) within a grid-template-columns value. -1 if absent. */
int yetty_ylexbor_grid_resolve_line(const char *value, size_t n, const char *name, size_t name_len);

/* Column gap (px) from an inline `style` attribute (`gap`/`column-gap`), or -1
 * if absent. For flex/grid containers whose gap is set inline. */
float yetty_ylexbor_css_inline_gap(const char *style, size_t len);

/* Free r->grid_classes and each owned class name. Called on document replace. */
void yetty_ylexbor_grid_classes_free(struct yetty_ylexbor *r);

/* Look up a parsed grid template by an element's class attribute (space-
 * separated class list). Returns the matching entry or NULL. */
const struct yl_grid_class *yetty_ylexbor_grid_class_lookup(struct yetty_ylexbor *r,
                                                            const lxb_dom_element_t *element);

/* Resolve every `var(--name [, fallback])` reference in `value`. Returns
 * a freshly malloc'd NUL-terminated string the caller must free.
 * Returns a copy of the input on no-vars / OOM. */
char *yetty_ylexbor_css_vars_resolve(struct yetty_ylexbor *r, const char *value, size_t len);

/* Rewrite CSS Media Queries Level 4 range-syntax features — `(width>=960px)`,
 * `(width<=1023px)`, `(400px<=width<=700px)`, … — into the classic
 * `(min-width:960px)` / `(max-width:1023px)` forms that libcss 0.9.x parses.
 * Returns a freshly malloc'd string when a rewrite happened, else NULL (the
 * caller keeps the original buffer). */
char *yetty_ybrowser_css_rewrite_media_ranges(const char *css, size_t len);

/* De-sugar Selectors Level 4 pseudos that libcss 0.9.x rejects (dropping the
 * whole rule) into equivalent Level 3 selector lists: a compound `:not(.a.b)`
 * (De Morgan) or list `:not(.a, .b)` (chained nots), and `:is(.a, .b)` /
 * `:where(.a, .b)` (branch substitution). Returns a freshly malloc'd string
 * when a rewrite happened, else NULL (caller keeps the original buffer). */
char *yetty_ybrowser_css_desugar_selectors(const char *css, size_t len);

/* Rewrite `calc(<percentage> ± <length>)` to just `<percentage>`. libcss 0.9.x
 * cannot parse calc() and drops the declaration, collapsing percentage-based
 * flex/grid columns (`width:calc(33.33% - 16px)`) into single-column stacks.
 * Keeping the percentage preserves the column count. Returns a freshly malloc'd
 * string when a rewrite happened, else NULL (caller keeps the original). */
char *yetty_ybrowser_css_simplify_calc(const char *css, size_t len);

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

/* Recover the owning engine from a QuickJS context. The engine pointer is
 * stashed as the runtime opaque (js_dom_state.r) by the DOM install, so this
 * works for any callback firing inside a page's JS. Returns NULL before the
 * DOM bindings have been installed. Defined in ybrowser-js-dom.c. */
struct yetty_ylexbor *yetty_ylexbor_js_engine_from_ctx(struct JSContext *ctx);

/* Append one line to the DevTools console ring. `text` is copied. Safe to
 * call with a NULL engine (no-op). Defined in ybrowser-js.c, always compiled
 * regardless of YETTY_HAVE_QUICKJS. */
void yetty_ylexbor_console_push(struct yetty_ylexbor *r, int level, const char *text);

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

/* Same resolution against an explicit base — @import URLs resolve against
 * the IMPORTING stylesheet's URL, not the document base. */
char *yetty_ylexbor_resolve_url_against(const char *base_url, const char *href);

/* Publish decoded RGBA pixels onto the loader's resource cache entry for
 * `url` (copies), so the next navigation reuses the decode as well as
 * the bytes. No-op when the entry is gone or libcurl is compiled out. */
void yetty_ybrowser_loader_cache_put_pixels(struct yetty_ybrowser_loader *loader, const char *url,
                                            const uint32_t *pixels, int width, int height);

/* Dispatch a click event to the JS handlers attached to the element
 * whose box contains (x,y) in pane-local pixels. Returns 1 if a handler
 * fired, 0 otherwise. The handlers can mutate the DOM; the caller
 * should consult r->dom_dirty afterwards and re-run box-build → layout
 * → paint if set. */
int yetty_ylexbor_dispatch_click(struct yetty_ylexbor *r, float x, float y);

/* Re-resolve box tree + layout from the (possibly mutated) DOM, then resolve
 * iframes. Host relayout entry point. */
struct yetty_ycore_void_result yetty_ylexbor_relayout(struct yetty_ylexbor *r);

/* Box-build + layout only — the pure style/layout flush with no iframe
 * resolution or other side effects. Used as a CSSOM forced-layout point by
 * layout-dependent geometry getters. Guards r->layout_in_progress; restores
 * r->dom_dirty on failure. */
struct yetty_ycore_void_result yetty_ylexbor_relayout_boxes_and_layout(struct yetty_ylexbor *r);

#endif
