/*
 * ysvg-style.c — resolve the cascaded style for a single SVG element.
 *
 * SVG style comes from three places, in priority order:
 *   1. inline `style="prop:val;..."` attribute  (highest)
 *   2. presentation attributes (`fill="red"`, `stroke-width="2"`, ...)
 *   3. inherited from parent
 *
 * We do NOT support external/inline <style> CSS rules in this build —
 * that requires a selector engine. Inline style + presentation attrs
 * cover the bulk of SVG-in-the-wild content.
 */

#include "ysvg-internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*=============================================================================
 * Paint string parser
 *
 * `none`, `currentColor`, `url(#id) fallback?`, or a color.
 *===========================================================================*/

static int ysvg_str_eq_slice(const char *a, size_t alen, const char *lit)
{
    size_t l = strlen(lit);
    if (l != alen) {
        return 0;
    }
    return memcmp(a, lit, l) == 0;
}

static void parse_paint(struct yetty_ysvg_paint *out, const char *s, size_t len,
                        const struct yetty_ysvg_paint *parent)
{
    /* Trim leading/trailing whitespace. */
    size_t i = 0;
    while (i < len && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) {
        i++;
    }
    while (len > i &&
           (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\n' || s[len - 1] == '\r')) {
        len--;
    }
    if (i >= len) {
        if (parent) {
            *out = *parent;
        }
        return;
    }
    const char *p = s + i;
    size_t plen = len - i;

    if (ysvg_str_eq_slice(p, plen, "none")) {
        out->kind = YETTY_YSVG_PAINT_NONE;
        out->color = 0;
        return;
    }
    if (ysvg_str_eq_slice(p, plen, "inherit")) {
        if (parent) {
            *out = *parent;
        }
        return;
    }
    if (ysvg_str_eq_slice(p, plen, "currentColor")) {
        out->kind = YETTY_YSVG_PAINT_CURRENTCOLOR;
        out->color = 0x000000FFu;
        return;
    }
    if (plen > 4 && (memcmp(p, "url(", 4) == 0 || memcmp(p, "URL(", 4) == 0)) {
        const char *q = p + 4;
        size_t qlen = plen - 4;
        const char *end = memchr(q, ')', qlen);
        if (end) {
            /* Inside the parens: '#id' (we don't follow the fragment). */
            const char *id = q;
            size_t idlen = (size_t)(end - q);
            if (idlen > 0 && *id == '#') {
                id++;
                idlen--;
            }
            out->kind = YETTY_YSVG_PAINT_URL;
            out->url_id = id;
            out->url_id_len = idlen;
            /* Fallback color, if any, follows the ')' separated by space. */
            const char *rest = end + 1;
            size_t rl = plen - 4 - (size_t)(end - q) - 1;
            while (rl > 0 && (rest[0] == ' ' || rest[0] == '\t')) {
                rest++;
                rl--;
            }
            if (rl > 0) {
                uint32_t fallback = 0;
                if (yetty_ysvg_parse_color(rest, rl, &fallback)) {
                    out->color = fallback;
                }
            } else {
                /* Default fallback is black opaque — gradients we don't fully
                 * resolve render as their first stop's color (set by paint). */
                out->color = 0x000000FFu;
            }
            return;
        }
    }
    uint32_t c = 0;
    if (yetty_ysvg_parse_color(p, plen, &c)) {
        out->kind = YETTY_YSVG_PAINT_COLOR;
        out->color = c;
        return;
    }
    /* Unrecognized — inherit or leave as-is. */
    if (parent) {
        *out = *parent;
    }
}

/*=============================================================================
 * Inline style="..." parser
 *
 * Splits on ';', each part is "prop:value". Whitespace tolerated. Values
 * are not URL-encoded or quoted; we treat them as plain UTF-8 slices.
 *===========================================================================*/

void yetty_ysvg_style_apply_property(struct yetty_ysvg_style *out, const char *prop, size_t plen,
                                     const char *val, size_t vlen)
{
    /* Trim val whitespace */
    while (vlen > 0 && (val[0] == ' ' || val[0] == '\t' || val[0] == '\n' || val[0] == '\r')) {
        val++;
        vlen--;
    }
    while (vlen > 0 && (val[vlen - 1] == ' ' || val[vlen - 1] == '\t' || val[vlen - 1] == '\n' ||
                        val[vlen - 1] == '\r')) {
        vlen--;
    }

    if (ysvg_str_eq_slice(prop, plen, "fill")) {
        parse_paint(&out->fill, val, vlen, NULL);
    } else if (ysvg_str_eq_slice(prop, plen, "stroke")) {
        parse_paint(&out->stroke, val, vlen, NULL);
    } else if (ysvg_str_eq_slice(prop, plen, "fill-opacity")) {
        out->fill_opacity = yetty_ysvg_parse_length(val, vlen, 1.0f, out->fill_opacity);
    } else if (ysvg_str_eq_slice(prop, plen, "stroke-opacity")) {
        out->stroke_opacity = yetty_ysvg_parse_length(val, vlen, 1.0f, out->stroke_opacity);
    } else if (ysvg_str_eq_slice(prop, plen, "opacity")) {
        out->opacity = yetty_ysvg_parse_length(val, vlen, 1.0f, out->opacity);
    } else if (ysvg_str_eq_slice(prop, plen, "stroke-width")) {
        out->stroke_width = yetty_ysvg_parse_length(val, vlen, 100.0f, out->stroke_width);
    } else if (ysvg_str_eq_slice(prop, plen, "stroke-linecap")) {
        if (ysvg_str_eq_slice(val, vlen, "round")) {
            out->stroke_linecap = YETTY_YSVG_LINECAP_ROUND;
        } else if (ysvg_str_eq_slice(val, vlen, "square")) {
            out->stroke_linecap = YETTY_YSVG_LINECAP_SQUARE;
        } else {
            out->stroke_linecap = YETTY_YSVG_LINECAP_BUTT;
        }
    } else if (ysvg_str_eq_slice(prop, plen, "stroke-linejoin")) {
        if (ysvg_str_eq_slice(val, vlen, "round")) {
            out->stroke_linejoin = YETTY_YSVG_LINEJOIN_ROUND;
        } else if (ysvg_str_eq_slice(val, vlen, "bevel")) {
            out->stroke_linejoin = YETTY_YSVG_LINEJOIN_BEVEL;
        } else {
            out->stroke_linejoin = YETTY_YSVG_LINEJOIN_MITER;
        }
    } else if (ysvg_str_eq_slice(prop, plen, "font-size")) {
        out->font_size = yetty_ysvg_parse_length(val, vlen, out->font_size, out->font_size);
    } else if (ysvg_str_eq_slice(prop, plen, "text-anchor")) {
        if (ysvg_str_eq_slice(val, vlen, "middle")) {
            out->text_anchor = YETTY_YSVG_ANCHOR_MIDDLE;
        } else if (ysvg_str_eq_slice(val, vlen, "end")) {
            out->text_anchor = YETTY_YSVG_ANCHOR_END;
        } else {
            out->text_anchor = YETTY_YSVG_ANCHOR_START;
        }
    } else if (ysvg_str_eq_slice(prop, plen, "display")) {
        out->display = !ysvg_str_eq_slice(val, vlen, "none");
    } else if (ysvg_str_eq_slice(prop, plen, "visibility")) {
        out->visibility =
            !ysvg_str_eq_slice(val, vlen, "hidden") && !ysvg_str_eq_slice(val, vlen, "collapse");
    }
}

static void apply_style_attr(struct yetty_ysvg_style *out, const char *s, size_t len)
{
    size_t i = 0;
    while (i < len) {
        /* skip ws + leading ';' */
        while (i < len &&
               (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r' || s[i] == ';')) {
            i++;
        }
        if (i >= len) {
            break;
        }
        size_t pstart = i;
        while (i < len && s[i] != ':' && s[i] != ';') {
            i++;
        }
        if (i >= len || s[i] != ':') {
            /* malformed property — skip past next ';' */
            while (i < len && s[i] != ';') {
                i++;
            }
            continue;
        }
        size_t plen = i - pstart;
        /* Trim trailing whitespace from property name */
        while (plen > 0 && (s[pstart + plen - 1] == ' ' || s[pstart + plen - 1] == '\t')) {
            plen--;
        }
        i++; /* skip ':' */
        size_t vstart = i;
        while (i < len && s[i] != ';') {
            i++;
        }
        size_t vlen = i - vstart;
        yetty_ysvg_style_apply_property(out, s + pstart, plen, s + vstart, vlen);
    }
}

/*=============================================================================
 * Presentation attributes
 *===========================================================================*/

static void apply_presentation_attrs(struct yetty_ysvg_style *out, const struct yetty_ysvg_node *n,
                                     const struct yetty_ysvg_style *parent)
{
    const struct yetty_ysvg_attr *a;

    if ((a = yetty_ysvg_attr_find(n, YETTY_YSVG_ATTR_FILL))) {
        parse_paint(&out->fill, a->value, a->value_len, &parent->fill);
    }
    if ((a = yetty_ysvg_attr_find(n, YETTY_YSVG_ATTR_STROKE))) {
        parse_paint(&out->stroke, a->value, a->value_len, &parent->stroke);
    }
    if ((a = yetty_ysvg_attr_find(n, YETTY_YSVG_ATTR_FILL_OPACITY))) {
        out->fill_opacity =
            yetty_ysvg_parse_length(a->value, a->value_len, 1.0f, out->fill_opacity);
    }
    if ((a = yetty_ysvg_attr_find(n, YETTY_YSVG_ATTR_STROKE_OPACITY))) {
        out->stroke_opacity =
            yetty_ysvg_parse_length(a->value, a->value_len, 1.0f, out->stroke_opacity);
    }
    if ((a = yetty_ysvg_attr_find(n, YETTY_YSVG_ATTR_OPACITY))) {
        out->opacity = yetty_ysvg_parse_length(a->value, a->value_len, 1.0f, out->opacity);
    }
    if ((a = yetty_ysvg_attr_find(n, YETTY_YSVG_ATTR_STROKE_WIDTH))) {
        out->stroke_width =
            yetty_ysvg_parse_length(a->value, a->value_len, 100.0f, out->stroke_width);
    }
    if ((a = yetty_ysvg_attr_find(n, YETTY_YSVG_ATTR_STROKE_LINECAP))) {
        if (ysvg_str_eq_slice(a->value, a->value_len, "round")) {
            out->stroke_linecap = YETTY_YSVG_LINECAP_ROUND;
        } else if (ysvg_str_eq_slice(a->value, a->value_len, "square")) {
            out->stroke_linecap = YETTY_YSVG_LINECAP_SQUARE;
        } else {
            out->stroke_linecap = YETTY_YSVG_LINECAP_BUTT;
        }
    }
    if ((a = yetty_ysvg_attr_find(n, YETTY_YSVG_ATTR_STROKE_LINEJOIN))) {
        if (ysvg_str_eq_slice(a->value, a->value_len, "round")) {
            out->stroke_linejoin = YETTY_YSVG_LINEJOIN_ROUND;
        } else if (ysvg_str_eq_slice(a->value, a->value_len, "bevel")) {
            out->stroke_linejoin = YETTY_YSVG_LINEJOIN_BEVEL;
        } else {
            out->stroke_linejoin = YETTY_YSVG_LINEJOIN_MITER;
        }
    }
    if ((a = yetty_ysvg_attr_find(n, YETTY_YSVG_ATTR_FONT_SIZE))) {
        out->font_size =
            yetty_ysvg_parse_length(a->value, a->value_len, out->font_size, out->font_size);
    }
    if ((a = yetty_ysvg_attr_find(n, YETTY_YSVG_ATTR_TEXT_ANCHOR))) {
        if (ysvg_str_eq_slice(a->value, a->value_len, "middle")) {
            out->text_anchor = YETTY_YSVG_ANCHOR_MIDDLE;
        } else if (ysvg_str_eq_slice(a->value, a->value_len, "end")) {
            out->text_anchor = YETTY_YSVG_ANCHOR_END;
        } else {
            out->text_anchor = YETTY_YSVG_ANCHOR_START;
        }
    }
    if ((a = yetty_ysvg_attr_find(n, YETTY_YSVG_ATTR_DISPLAY))) {
        out->display = !ysvg_str_eq_slice(a->value, a->value_len, "none");
    }
    if ((a = yetty_ysvg_attr_find(n, YETTY_YSVG_ATTR_VISIBILITY))) {
        out->visibility = !ysvg_str_eq_slice(a->value, a->value_len, "hidden") &&
                          !ysvg_str_eq_slice(a->value, a->value_len, "collapse");
    }
}

/*=============================================================================
 * Public entry points
 *===========================================================================*/

void yetty_ysvg_style_init_root(struct yetty_ysvg_style *s, float default_font_size)
{
    s->fill.kind = YETTY_YSVG_PAINT_COLOR;
    s->fill.color = 0x000000FFu; /* SVG default fill is black opaque */
    s->fill.url_id = NULL;
    s->fill.url_id_len = 0;
    s->stroke.kind = YETTY_YSVG_PAINT_NONE;
    s->stroke.color = 0;
    s->stroke.url_id = NULL;
    s->stroke.url_id_len = 0;
    s->fill_opacity = 1.0f;
    s->stroke_opacity = 1.0f;
    s->opacity = 1.0f;
    s->stroke_width = 1.0f;
    s->stroke_linecap = YETTY_YSVG_LINECAP_BUTT;
    s->stroke_linejoin = YETTY_YSVG_LINEJOIN_MITER;
    s->font_size = default_font_size;
    s->text_anchor = YETTY_YSVG_ANCHOR_START;
    s->display = 1;
    s->visibility = 1;
}

void yetty_ysvg_style_resolve(struct yetty_ysvg_style *out,
                              const struct yetty_ysvg_style *parent_style,
                              const struct yetty_ysvg_doc *doc, const struct yetty_ysvg_node *node)
{
    /* Start from inherited parent style. */
    *out = *parent_style;
    /* `opacity` is NOT inherited — it's a presentation property that applies
     * only to the current element. Reset to 1.0 before reading. */
    out->opacity = 1.0f;

    apply_presentation_attrs(out, node, parent_style);
    yetty_ysvg_css_apply(out, doc, node);

    const struct yetty_ysvg_attr *style = yetty_ysvg_attr_find(node, YETTY_YSVG_ATTR_STYLE);
    if (style && style->value) {
        apply_style_attr(out, style->value, style->value_len);
    }
}
