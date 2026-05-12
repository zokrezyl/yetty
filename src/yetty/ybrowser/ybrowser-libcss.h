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
 * size in CSS px (used for em). */
int yetty_ybrowser_libcss_width(struct yetty_ylexbor *r, const css_computed_style *style,
                                float font_size, float pct_basis, float *out_px);
int yetty_ybrowser_libcss_height(struct yetty_ylexbor *r, const css_computed_style *style,
                                 float font_size, float pct_basis, float *out_px);
int yetty_ybrowser_libcss_max_width(struct yetty_ylexbor *r, const css_computed_style *style,
                                    float font_size, float pct_basis, float *out_px);
int yetty_ybrowser_libcss_min_width(struct yetty_ylexbor *r, const css_computed_style *style,
                                    float font_size, float pct_basis, float *out_px);
int yetty_ybrowser_libcss_margin(struct yetty_ylexbor *r, const css_computed_style *style,
                                 int side, float font_size, float pct_basis, float *out_px,
                                 bool *out_auto);
int yetty_ybrowser_libcss_padding(struct yetty_ylexbor *r, const css_computed_style *style,
                                  int side, float font_size, float pct_basis, float *out_px);
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

#endif
