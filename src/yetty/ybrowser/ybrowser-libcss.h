#ifndef YETTY_YBROWSER_LIBCSS_H
#define YETTY_YBROWSER_LIBCSS_H

/* libcss bridge for ybrowser.
 *
 * Owns the per-document css_select_ctx, the select handler vtable, and
 * the list of stylesheets we created so we can destroy them on doc
 * teardown. The box pass calls yetty_ybrowser_libcss_select per element
 * to get a typed css_computed_style; helpers below convert libcss's
 * (css_fixed,css_unit) pairs and (css_color) into ybrowser's float
 * pixels and struct yetty_ylexbor_color. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <libcss/libcss.h>
#include <lexbor/dom/interfaces/element.h>

struct yetty_ylexbor;
struct yetty_ylexbor_color;

struct yetty_ybrowser_libcss {
    css_select_ctx *select_ctx;
    css_select_handler handler;
    css_unit_ctx unit_ctx;
    css_media media;

    /* Stylesheets we created via yetty_ybrowser_libcss_add_sheet; we own
     * them and destroy them in libcss_destroy. select_ctx holds borrowed
     * pointers into this list. */
    css_stylesheet **sheets;
    size_t sheet_count, sheet_cap;

    /* Back-pointer for callbacks that need viewport metrics / lookups. */
    struct yetty_ylexbor *r;
};

/* Lifecycle. */
int yetty_ybrowser_libcss_init(struct yetty_ylexbor *r);
void yetty_ybrowser_libcss_destroy(struct yetty_ylexbor *r);

/* Inject the MediaWiki float-helper stylesheet (UA_WIKIPEDIA_CSS). Idempotent
 * within a document. Call only for MediaWiki pages — these helpers float
 * generic class names (`.thumb`, `.infobox`) and would wreck other sites.
 * Returns 0 on success. */
int yetty_ybrowser_libcss_apply_wikipedia_quirks(struct yetty_ylexbor *r);

/* Parse one CSS source string through libcss and append to the select
 * ctx. origin is CSS_ORIGIN_AUTHOR for everything we see today. Returns
 * 0 on success, -1 on error (caller may still continue — the cascade
 * just won't include this sheet). */
int yetty_ybrowser_libcss_add_sheet(struct yetty_ylexbor *r, const char *css, size_t len,
                                    css_origin origin);

/* Run libcss's selector matching against the cascade for `el` and
 * return the cascaded computed style for the unstyled-pseudo (i.e.
 * CSS_PSEUDO_ELEMENT_NONE). Caller frees with
 * yetty_ybrowser_libcss_release. inline_css/inline_css_len may be NULL/0
 * to mean "no inline style". */
css_computed_style *yetty_ybrowser_libcss_select(struct yetty_ylexbor *r, lxb_dom_element_t *el,
                                                 const char *inline_css, size_t inline_css_len);

void yetty_ybrowser_libcss_release(css_computed_style *style);

/* ===========================================================================
 * Computed-property bridges. Each returns 1 when the property was set
 * (caller should use the value), 0 when the cascade left it
 * inherit/auto/none/not-set (caller falls back to its default). Length
 * conversions resolve em/rem/%/vh/vw using the runtime's unit_ctx.
 * ===========================================================================*/

int yetty_ybrowser_libcss_color(const css_computed_style *style, struct yetty_ylexbor_color *out);
int yetty_ybrowser_libcss_bg_color(const css_computed_style *style,
                                   struct yetty_ylexbor_color *out);

/* Each of these returns 1 on a real numeric value (px-resolved), 0 on
 * auto/inherit/none. pct_basis is the parent's content width in CSS px
 * — used to resolve % values. font_size is the element's computed font
 * size in CSS px (used for em).
 *
 * For width / height / max_width / min_width / flex_basis the box pass
 * doesn't know the parent's content area yet (that's only resolved at
 * layout time), so it calls these with `pct_basis = 0`. When the CSS
 * value is a percentage, the function then stores it as a NEGATIVE
 * ratio (e.g. -1.0 for 100%, -0.5 for 50%) in *out_px; the layout pass
 * detects negative values and resolves them against the actual parent
 * content width. Anything else (px / em / rem / ...) round-trips as a
 * positive pixel value. */
int yetty_ybrowser_libcss_width(struct yetty_ylexbor *r, const css_computed_style *style,
                                float font_size, float pct_basis, float *out_px);
int yetty_ybrowser_libcss_height(struct yetty_ylexbor *r, const css_computed_style *style,
                                 float font_size, float pct_basis, float *out_px);
int yetty_ybrowser_libcss_max_width(struct yetty_ylexbor *r, const css_computed_style *style,
                                    float font_size, float pct_basis, float *out_px);
int yetty_ybrowser_libcss_min_width(struct yetty_ylexbor *r, const css_computed_style *style,
                                    float font_size, float pct_basis, float *out_px);
/* Margin / padding: a percentage value is returned as a ratio (e.g. 0.10
 * for 10%) with *out_pct = true, because percent margins and paddings
 * both resolve against the containing block's content width — which is
 * only known at layout time. Non-percent values are resolved px with
 * *out_pct = false. */
int yetty_ybrowser_libcss_margin(struct yetty_ylexbor *r, const css_computed_style *style, int side,
                                 float font_size, float pct_basis, float *out_px, bool *out_auto,
                                 bool *out_pct);
int yetty_ybrowser_libcss_padding(struct yetty_ylexbor *r, const css_computed_style *style,
                                  int side, float font_size, float pct_basis, float *out_px,
                                  bool *out_pct);

/* box-sizing: 1 = border-box, 0 = content-box (initial) / inherit. */
int yetty_ybrowser_libcss_box_sizing(const css_computed_style *style);

/* line-height: 1 on a concrete value, 0 on `normal` / inherit. A unitless
 * number returns the factor in *out_factor (resolve per element against
 * its own font-size); a length/percentage returns a used px in *out_px.
 * Exactly one of the two outputs is non-zero on a hit. */
int yetty_ybrowser_libcss_line_height(struct yetty_ylexbor *r, const css_computed_style *style,
                                      float font_size, float *out_px, float *out_factor);
int yetty_ybrowser_libcss_border_width(struct yetty_ylexbor *r, const css_computed_style *style,
                                       int side, float font_size, float *out_px);
int yetty_ybrowser_libcss_border_color(const css_computed_style *style, int side,
                                       const struct yetty_ylexbor_color *current_color,
                                       struct yetty_ylexbor_color *out);
int yetty_ybrowser_libcss_font_size(struct yetty_ylexbor *r, const css_computed_style *style,
                                    float parent_font_size, float *out_px);
int yetty_ybrowser_libcss_font_weight(const css_computed_style *style, int *out);
int yetty_ybrowser_libcss_font_italic(const css_computed_style *style, bool *out);

/* Display: returns CSS_DISPLAY_* (see libcss/properties.h). Always
 * yields a concrete value (libcss resolves to BLOCK/INLINE/NONE/etc.). */
int yetty_ybrowser_libcss_display(const css_computed_style *style, bool root);

/* Text-align: returns CSS_TEXT_ALIGN_* (LEFT/RIGHT/CENTER/JUSTIFY). */
int yetty_ybrowser_libcss_text_align(const css_computed_style *style);

/* Flex direction: returns CSS_FLEX_DIRECTION_* (ROW/COLUMN/...). */
int yetty_ybrowser_libcss_flex_direction(const css_computed_style *style);

/* Flex item / container properties. */
int yetty_ybrowser_libcss_flex_grow(const css_computed_style *style, float *out);
/* Returns 1 + writes *out_px if basis is a length; out_auto=true if basis
 * is `auto` (or `content` — we collapse them). 0 = not set / inherit. */
int yetty_ybrowser_libcss_flex_basis(struct yetty_ylexbor *r, const css_computed_style *style,
                                     float font_size, float pct_basis, float *out_px,
                                     bool *out_auto);
int yetty_ybrowser_libcss_justify_content(const css_computed_style *style);
int yetty_ybrowser_libcss_align_items(const css_computed_style *style);

/* Float / clear. Returns the CSS_FLOAT_* / CSS_CLEAR_* enum. */
int yetty_ybrowser_libcss_float(const css_computed_style *style);
int yetty_ybrowser_libcss_clear(const css_computed_style *style);

/* Position: returns the CSS_POSITION_* enum (STATIC / RELATIVE /
 * ABSOLUTE / FIXED / STICKY). Default is CSS_POSITION_STATIC. */
int yetty_ybrowser_libcss_position(const css_computed_style *style);

/* Inset (top/right/bottom/left). `side`: 0=top,1=right,2=bottom,3=left.
 * Returns 0 when the side is `auto`/unset, 1 when it is a resolved length
 * (px written to *out_value; may be negative), or 2 when it is a percentage
 * (the ratio, e.g. 0.10 for 10%, written to *out_value — resolve against the
 * containing block at layout time). `pct_basis` only feeds em/viewport
 * resolution for the length case. */
int yetty_ybrowser_libcss_inset(struct yetty_ylexbor *r, const css_computed_style *style, int side,
                                float font_size, float pct_basis, float *out_value);

/* White-space: returns the CSS_WHITE_SPACE_* enum (NORMAL / PRE /
 * NOWRAP / PRE_WRAP / PRE_LINE). Default is CSS_WHITE_SPACE_NORMAL. */
int yetty_ybrowser_libcss_white_space(const css_computed_style *style);

/* Text-decoration: returns the CSS_TEXT_DECORATION_* bitmask
 * (UNDERLINE | OVERLINE | LINE_THROUGH | BLINK), with the NONE bit
 * (0x10) ORed when the property is explicitly `none`. The caller masks
 * out the bits it cares about; CSS_TEXT_DECORATION_NONE forces a clear. */
int yetty_ybrowser_libcss_text_decoration(const css_computed_style *style);

/* Background-image: when the computed value is an absolute URL,
 * fills *out_url with a freshly malloc'd NUL-terminated copy that the
 * caller must free(). Returns 1 on success, 0 on `none` / `inherit` /
 * any non-url() value. */
int yetty_ybrowser_libcss_bg_image_url(const css_computed_style *style, char **out_url);

/* List-style-type: returns the CSS_LIST_STYLE_TYPE_* enum (DISC /
 * CIRCLE / SQUARE / DECIMAL / NONE / …). Used by box-build to pick
 * the marker character (or suppress it) per <li>. */
int yetty_ybrowser_libcss_list_style_type(const css_computed_style *style);

#endif
