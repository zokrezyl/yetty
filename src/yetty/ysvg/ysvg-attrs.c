/*
 * ysvg-attrs.c — parsers for SVG attribute mini-languages.
 *
 * All parsers consume slices (pointer + length) — none of the inputs are
 * required to be NUL-terminated. SVG follows CSS lexical conventions:
 * numbers may carry a unit suffix, lists are separated by whitespace or
 * a single comma (the "wsp comma wsp" production).
 */

#include "ysvg-internal.h"

/* MSVC's <math.h> only exposes M_PI when _USE_MATH_DEFINES is set before
 * the include; glibc/macOS ship it unconditionally. Defining the macro
 * before <math.h> is the portable way to keep MSVC compiling. */
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif

#include <ctype.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*=============================================================================
 * Whitespace / number primitives
 *===========================================================================*/

static int ysvg_is_wsp(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

static size_t ysvg_skip_wsp(const char *s, size_t len, size_t pos)
{
    while (pos < len && ysvg_is_wsp((unsigned char)s[pos])) {
        pos++;
    }
    return pos;
}

static size_t ysvg_skip_wsp_comma(const char *s, size_t len, size_t pos)
{
    pos = ysvg_skip_wsp(s, len, pos);
    if (pos < len && s[pos] == ',') {
        pos++;
        pos = ysvg_skip_wsp(s, len, pos);
    }
    return pos;
}

int yetty_ysvg_parse_number(const char *s, size_t len, size_t *pos, float *out)
{
    *pos = ysvg_skip_wsp_comma(s, len, *pos);
    if (*pos >= len) {
        return 0;
    }

    size_t start = *pos;
    if (*pos < len && (s[*pos] == '+' || s[*pos] == '-')) {
        (*pos)++;
    }
    int saw_digit = 0;
    while (*pos < len && s[*pos] >= '0' && s[*pos] <= '9') {
        saw_digit = 1;
        (*pos)++;
    }
    if (*pos < len && s[*pos] == '.') {
        (*pos)++;
        while (*pos < len && s[*pos] >= '0' && s[*pos] <= '9') {
            saw_digit = 1;
            (*pos)++;
        }
    }
    if (!saw_digit) {
        *pos = start;
        return 0;
    }
    if (*pos < len && (s[*pos] == 'e' || s[*pos] == 'E')) {
        size_t save = *pos;
        (*pos)++;
        if (*pos < len && (s[*pos] == '+' || s[*pos] == '-')) {
            (*pos)++;
        }
        int saw_exp_digit = 0;
        while (*pos < len && s[*pos] >= '0' && s[*pos] <= '9') {
            saw_exp_digit = 1;
            (*pos)++;
        }
        if (!saw_exp_digit) {
            *pos = save;
        }
    }

    /* Copy to a small NUL-terminated buffer for strtof — most SVG numbers
     * are <32 chars. We tolerate any length by capping. */
    char buf[64];
    size_t span = *pos - start;
    if (span >= sizeof(buf)) {
        span = sizeof(buf) - 1;
    }
    memcpy(buf, s + start, span);
    buf[span] = '\0';
    *out = (float)strtod(buf, NULL);
    return 1;
}

float yetty_ysvg_parse_length(const char *s, size_t len, float pct_base, float fallback)
{
    if (!s || len == 0) {
        return fallback;
    }
    size_t pos = ysvg_skip_wsp(s, len, 0);
    float v = 0.0f;
    if (!yetty_ysvg_parse_number(s, len, &pos, &v)) {
        return fallback;
    }
    if (pos >= len) {
        return v;
    }

    /* Identify a 1-2 char unit. % is a common case. */
    if (s[pos] == '%') {
        return v * pct_base * 0.01f;
    }
    if (pos + 1 < len) {
        char a = s[pos], b = s[pos + 1];
        if (a == 'p' && b == 'x') {
            return v;
        }
        if (a == 'p' && b == 't') {
            return v * 1.3333333f; /* 1pt = 4/3 px */
        }
        if (a == 'p' && b == 'c') {
            return v * 16.0f; /* 1pc = 12pt = 16px */
        }
        if (a == 'm' && b == 'm') {
            return v * 3.7795276f; /* 96dpi */
        }
        if (a == 'c' && b == 'm') {
            return v * 37.795276f;
        }
        if (a == 'i' && b == 'n') {
            return v * 96.0f;
        }
        if (a == 'e' && b == 'm') {
            return v * pct_base; /* relative to font_size */
        }
        if (a == 'e' && b == 'x') {
            return v * pct_base * 0.5f;
        }
    }
    return v;
}

/*=============================================================================
 * Color
 *
 * Covers: #RGB / #RGBA / #RRGGBB / #RRGGBBAA, rgb()/rgba() (decimal or %),
 * `none`, `currentColor`, and the SVG 1.1 named-color list.
 *===========================================================================*/

struct named_color {
    const char *name;
    uint32_t rgba;
};

/* Subset of the SVG named-color list. Pack as 0xRRGGBBAA. */
static const struct named_color k_named_colors[] = {
    {"aliceblue", 0xF0F8FFFFu},
    {"antiquewhite", 0xFAEBD7FFu},
    {"aqua", 0x00FFFFFFu},
    {"aquamarine", 0x7FFFD4FFu},
    {"azure", 0xF0FFFFFFu},
    {"beige", 0xF5F5DCFFu},
    {"bisque", 0xFFE4C4FFu},
    {"black", 0x000000FFu},
    {"blanchedalmond", 0xFFEBCDFFu},
    {"blue", 0x0000FFFFu},
    {"blueviolet", 0x8A2BE2FFu},
    {"brown", 0xA52A2AFFu},
    {"burlywood", 0xDEB887FFu},
    {"cadetblue", 0x5F9EA0FFu},
    {"chartreuse", 0x7FFF00FFu},
    {"chocolate", 0xD2691EFFu},
    {"coral", 0xFF7F50FFu},
    {"cornflowerblue", 0x6495EDFFu},
    {"cornsilk", 0xFFF8DCFFu},
    {"crimson", 0xDC143CFFu},
    {"cyan", 0x00FFFFFFu},
    {"darkblue", 0x00008BFFu},
    {"darkcyan", 0x008B8BFFu},
    {"darkgoldenrod", 0xB8860BFFu},
    {"darkgray", 0xA9A9A9FFu},
    {"darkgreen", 0x006400FFu},
    {"darkgrey", 0xA9A9A9FFu},
    {"darkkhaki", 0xBDB76BFFu},
    {"darkmagenta", 0x8B008BFFu},
    {"darkolivegreen", 0x556B2FFFu},
    {"darkorange", 0xFF8C00FFu},
    {"darkorchid", 0x9932CCFFu},
    {"darkred", 0x8B0000FFu},
    {"darksalmon", 0xE9967AFFu},
    {"darkseagreen", 0x8FBC8FFFu},
    {"darkslateblue", 0x483D8BFFu},
    {"darkslategray", 0x2F4F4FFFu},
    {"darkslategrey", 0x2F4F4FFFu},
    {"darkturquoise", 0x00CED1FFu},
    {"darkviolet", 0x9400D3FFu},
    {"deeppink", 0xFF1493FFu},
    {"deepskyblue", 0x00BFFFFFu},
    {"dimgray", 0x696969FFu},
    {"dimgrey", 0x696969FFu},
    {"dodgerblue", 0x1E90FFFFu},
    {"firebrick", 0xB22222FFu},
    {"floralwhite", 0xFFFAF0FFu},
    {"forestgreen", 0x228B22FFu},
    {"fuchsia", 0xFF00FFFFu},
    {"gainsboro", 0xDCDCDCFFu},
    {"ghostwhite", 0xF8F8FFFFu},
    {"gold", 0xFFD700FFu},
    {"goldenrod", 0xDAA520FFu},
    {"gray", 0x808080FFu},
    {"green", 0x008000FFu},
    {"greenyellow", 0xADFF2FFFu},
    {"grey", 0x808080FFu},
    {"honeydew", 0xF0FFF0FFu},
    {"hotpink", 0xFF69B4FFu},
    {"indianred", 0xCD5C5CFFu},
    {"indigo", 0x4B0082FFu},
    {"ivory", 0xFFFFF0FFu},
    {"khaki", 0xF0E68CFFu},
    {"lavender", 0xE6E6FAFFu},
    {"lavenderblush", 0xFFF0F5FFu},
    {"lawngreen", 0x7CFC00FFu},
    {"lemonchiffon", 0xFFFACDFFu},
    {"lightblue", 0xADD8E6FFu},
    {"lightcoral", 0xF08080FFu},
    {"lightcyan", 0xE0FFFFFFu},
    {"lightgoldenrodyellow", 0xFAFAD2FFu},
    {"lightgray", 0xD3D3D3FFu},
    {"lightgreen", 0x90EE90FFu},
    {"lightgrey", 0xD3D3D3FFu},
    {"lightpink", 0xFFB6C1FFu},
    {"lightsalmon", 0xFFA07AFFu},
    {"lightseagreen", 0x20B2AAFFu},
    {"lightskyblue", 0x87CEFAFFu},
    {"lightslategray", 0x778899FFu},
    {"lightslategrey", 0x778899FFu},
    {"lightsteelblue", 0xB0C4DEFFu},
    {"lightyellow", 0xFFFFE0FFu},
    {"lime", 0x00FF00FFu},
    {"limegreen", 0x32CD32FFu},
    {"linen", 0xFAF0E6FFu},
    {"magenta", 0xFF00FFFFu},
    {"maroon", 0x800000FFu},
    {"mediumaquamarine", 0x66CDAAFFu},
    {"mediumblue", 0x0000CDFFu},
    {"mediumorchid", 0xBA55D3FFu},
    {"mediumpurple", 0x9370DBFFu},
    {"mediumseagreen", 0x3CB371FFu},
    {"mediumslateblue", 0x7B68EEFFu},
    {"mediumspringgreen", 0x00FA9AFFu},
    {"mediumturquoise", 0x48D1CCFFu},
    {"mediumvioletred", 0xC71585FFu},
    {"midnightblue", 0x191970FFu},
    {"mintcream", 0xF5FFFAFFu},
    {"mistyrose", 0xFFE4E1FFu},
    {"moccasin", 0xFFE4B5FFu},
    {"navajowhite", 0xFFDEADFFu},
    {"navy", 0x000080FFu},
    {"oldlace", 0xFDF5E6FFu},
    {"olive", 0x808000FFu},
    {"olivedrab", 0x6B8E23FFu},
    {"orange", 0xFFA500FFu},
    {"orangered", 0xFF4500FFu},
    {"orchid", 0xDA70D6FFu},
    {"palegoldenrod", 0xEEE8AAFFu},
    {"palegreen", 0x98FB98FFu},
    {"paleturquoise", 0xAFEEEEFFu},
    {"palevioletred", 0xDB7093FFu},
    {"papayawhip", 0xFFEFD5FFu},
    {"peachpuff", 0xFFDAB9FFu},
    {"peru", 0xCD853FFFu},
    {"pink", 0xFFC0CBFFu},
    {"plum", 0xDDA0DDFFu},
    {"powderblue", 0xB0E0E6FFu},
    {"purple", 0x800080FFu},
    {"red", 0xFF0000FFu},
    {"rosybrown", 0xBC8F8FFFu},
    {"royalblue", 0x4169E1FFu},
    {"saddlebrown", 0x8B4513FFu},
    {"salmon", 0xFA8072FFu},
    {"sandybrown", 0xF4A460FFu},
    {"seagreen", 0x2E8B57FFu},
    {"seashell", 0xFFF5EEFFu},
    {"sienna", 0xA0522DFFu},
    {"silver", 0xC0C0C0FFu},
    {"skyblue", 0x87CEEBFFu},
    {"slateblue", 0x6A5ACDFFu},
    {"slategray", 0x708090FFu},
    {"slategrey", 0x708090FFu},
    {"snow", 0xFFFAFAFFu},
    {"springgreen", 0x00FF7FFFu},
    {"steelblue", 0x4682B4FFu},
    {"tan", 0xD2B48CFFu},
    {"teal", 0x008080FFu},
    {"thistle", 0xD8BFD8FFu},
    {"tomato", 0xFF6347FFu},
    {"turquoise", 0x40E0D0FFu},
    {"violet", 0xEE82EEFFu},
    {"wheat", 0xF5DEB3FFu},
    {"white", 0xFFFFFFFFu},
    {"whitesmoke", 0xF5F5F5FFu},
    {"yellow", 0xFFFF00FFu},
    {"yellowgreen", 0x9ACD32FFu},
    {"transparent", 0x00000000u},
};

static int hexd(int c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static int slice_ieq(const char *s, size_t len, const char *lit)
{
    size_t ll = strlen(lit);
    if (ll != len) {
        return 0;
    }
    for (size_t i = 0; i < len; i++) {
        int a = (unsigned char)s[i];
        int b = (unsigned char)lit[i];
        if (a >= 'A' && a <= 'Z') {
            a += 'a' - 'A';
        }
        if (b >= 'A' && b <= 'Z') {
            b += 'a' - 'A';
        }
        if (a != b) {
            return 0;
        }
    }
    return 1;
}

/* Parse an rgb()/rgba() argument: number with optional %, separated by
 * commas or whitespace. */
static int parse_rgb_component(const char *s, size_t len, size_t *pos, float *out, int *was_pct)
{
    *pos = ysvg_skip_wsp_comma(s, len, *pos);
    if (*pos >= len) {
        return 0;
    }
    if (!yetty_ysvg_parse_number(s, len, pos, out)) {
        return 0;
    }
    *was_pct = 0;
    if (*pos < len && s[*pos] == '%') {
        *was_pct = 1;
        (*pos)++;
    }
    return 1;
}

int yetty_ysvg_parse_color(const char *s, size_t len, uint32_t *out_rgba)
{
    if (!s || len == 0) {
        return 0;
    }
    size_t pos = ysvg_skip_wsp(s, len, 0);
    while (len > pos && ysvg_is_wsp((unsigned char)s[len - 1])) {
        len--;
    }
    if (pos >= len) {
        return 0;
    }
    size_t span = len - pos;
    const char *p = s + pos;

    if (slice_ieq(p, span, "none") || slice_ieq(p, span, "transparent")) {
        *out_rgba = 0x00000000u;
        return 1;
    }
    if (slice_ieq(p, span, "currentColor")) {
        /* Caller resolves via paint kind — encode as black for now. */
        *out_rgba = 0x000000FFu;
        return 1;
    }

    if (p[0] == '#') {
        size_t hex_len = span - 1;
        const char *h = p + 1;
        uint32_t v = 0;
        if (hex_len == 3 || hex_len == 4) {
            for (size_t i = 0; i < hex_len; i++) {
                int d = hexd((unsigned char)h[i]);
                if (d < 0) {
                    return 0;
                }
                v = (v << 4) | (uint32_t)d;
                v = (v << 4) | (uint32_t)d;
            }
            if (hex_len == 3) {
                v = (v << 8) | 0xFF;
            }
            *out_rgba = v;
            return 1;
        }
        if (hex_len == 6 || hex_len == 8) {
            for (size_t i = 0; i < hex_len; i++) {
                int d = hexd((unsigned char)h[i]);
                if (d < 0) {
                    return 0;
                }
                v = (v << 4) | (uint32_t)d;
            }
            if (hex_len == 6) {
                v = (v << 8) | 0xFF;
            }
            *out_rgba = v;
            return 1;
        }
        return 0;
    }

    if (span >= 4 && (memcmp(p, "rgb", 3) == 0 || memcmp(p, "RGB", 3) == 0)) {
        int has_alpha = (p[3] == 'a' || p[3] == 'A');
        const char *paren = memchr(p, '(', span);
        if (!paren) {
            return 0;
        }
        size_t ppos = (size_t)(paren - s) + 1;
        float r = 0, g = 0, b = 0, a = 1.0f;
        int wr = 0, wg = 0, wb = 0, wa = 0;
        if (!parse_rgb_component(s, len, &ppos, &r, &wr)) {
            return 0;
        }
        if (!parse_rgb_component(s, len, &ppos, &g, &wg)) {
            return 0;
        }
        if (!parse_rgb_component(s, len, &ppos, &b, &wb)) {
            return 0;
        }
        if (has_alpha) {
            if (!parse_rgb_component(s, len, &ppos, &a, &wa)) {
                return 0;
            }
            if (wa) {
                a /= 100.0f;
            }
        }
        if (wr) {
            r = r * 255.0f / 100.0f;
        }
        if (wg) {
            g = g * 255.0f / 100.0f;
        }
        if (wb) {
            b = b * 255.0f / 100.0f;
        }
        if (r < 0) {
            r = 0;
        }
        if (g < 0) {
            g = 0;
        }
        if (b < 0) {
            b = 0;
        }
        if (a < 0) {
            a = 0;
        }
        if (r > 255) {
            r = 255;
        }
        if (g > 255) {
            g = 255;
        }
        if (b > 255) {
            b = 255;
        }
        if (a > 1) {
            a = 1;
        }
        uint32_t R = (uint32_t)(r + 0.5f);
        uint32_t G = (uint32_t)(g + 0.5f);
        uint32_t B = (uint32_t)(b + 0.5f);
        uint32_t A = (uint32_t)(a * 255.0f + 0.5f);
        *out_rgba = (R << 24) | (G << 16) | (B << 8) | A;
        return 1;
    }

    /* Named color */
    for (size_t i = 0; i < sizeof(k_named_colors) / sizeof(k_named_colors[0]); i++) {
        if (slice_ieq(p, span, k_named_colors[i].name)) {
            *out_rgba = k_named_colors[i].rgba;
            return 1;
        }
    }
    return 0;
}

/*=============================================================================
 * Transforms
 *===========================================================================*/

void yetty_ysvg_xform_identity(struct yetty_ysvg_xform *m)
{
    m->a = 1.0f;
    m->b = 0.0f;
    m->c = 0.0f;
    m->d = 1.0f;
    m->e = 0.0f;
    m->f = 0.0f;
}

void yetty_ysvg_xform_multiply(struct yetty_ysvg_xform *out, const struct yetty_ysvg_xform *l,
                               const struct yetty_ysvg_xform *r)
{
    struct yetty_ysvg_xform t;
    t.a = l->a * r->a + l->c * r->b;
    t.b = l->b * r->a + l->d * r->b;
    t.c = l->a * r->c + l->c * r->d;
    t.d = l->b * r->c + l->d * r->d;
    t.e = l->a * r->e + l->c * r->f + l->e;
    t.f = l->b * r->e + l->d * r->f + l->f;
    *out = t;
}

void yetty_ysvg_xform_point(const struct yetty_ysvg_xform *m, float x, float y, float *ox,
                            float *oy)
{
    *ox = m->a * x + m->c * y + m->e;
    *oy = m->b * x + m->d * y + m->f;
}

static int read_keyword(const char *s, size_t len, size_t *pos, char *buf, size_t cap)
{
    *pos = ysvg_skip_wsp(s, len, *pos);
    size_t start = *pos;
    while (*pos < len &&
           ((s[*pos] >= 'a' && s[*pos] <= 'z') || (s[*pos] >= 'A' && s[*pos] <= 'Z'))) {
        (*pos)++;
    }
    size_t n = *pos - start;
    if (n == 0 || n >= cap) {
        return 0;
    }
    memcpy(buf, s + start, n);
    buf[n] = '\0';
    return 1;
}

int yetty_ysvg_parse_transform(const char *s, size_t len, struct yetty_ysvg_xform *out)
{
    yetty_ysvg_xform_identity(out);
    if (!s || len == 0) {
        return 1;
    }

    size_t pos = 0;
    while (pos < len) {
        pos = ysvg_skip_wsp_comma(s, len, pos);
        if (pos >= len) {
            break;
        }
        char kw[16];
        if (!read_keyword(s, len, &pos, kw, sizeof(kw))) {
            return 0;
        }
        pos = ysvg_skip_wsp(s, len, pos);
        if (pos >= len || s[pos] != '(') {
            return 0;
        }
        pos++;
        float args[6] = {0};
        int n = 0;
        while (n < 6) {
            pos = ysvg_skip_wsp_comma(s, len, pos);
            if (pos < len && s[pos] == ')') {
                break;
            }
            if (!yetty_ysvg_parse_number(s, len, &pos, &args[n])) {
                return 0;
            }
            n++;
        }
        pos = ysvg_skip_wsp(s, len, pos);
        if (pos >= len || s[pos] != ')') {
            return 0;
        }
        pos++;

        struct yetty_ysvg_xform t;
        yetty_ysvg_xform_identity(&t);
        if (strcmp(kw, "matrix") == 0 && n == 6) {
            t.a = args[0];
            t.b = args[1];
            t.c = args[2];
            t.d = args[3];
            t.e = args[4];
            t.f = args[5];
        } else if (strcmp(kw, "translate") == 0 && (n == 1 || n == 2)) {
            t.e = args[0];
            t.f = (n == 2) ? args[1] : 0.0f;
        } else if (strcmp(kw, "scale") == 0 && (n == 1 || n == 2)) {
            t.a = args[0];
            t.d = (n == 2) ? args[1] : args[0];
        } else if (strcmp(kw, "rotate") == 0 && (n == 1 || n == 3)) {
            float rad = args[0] * (float)(M_PI / 180.0);
            float cs = cosf(rad), sn = sinf(rad);
            if (n == 1) {
                t.a = cs;
                t.b = sn;
                t.c = -sn;
                t.d = cs;
            } else {
                /* rotate(a, cx, cy) = T(cx,cy) R(a) T(-cx,-cy) */
                struct yetty_ysvg_xform t1, t2, t3;
                yetty_ysvg_xform_identity(&t1);
                t1.e = args[1];
                t1.f = args[2];
                yetty_ysvg_xform_identity(&t2);
                t2.a = cs;
                t2.b = sn;
                t2.c = -sn;
                t2.d = cs;
                yetty_ysvg_xform_identity(&t3);
                t3.e = -args[1];
                t3.f = -args[2];
                struct yetty_ysvg_xform tmp;
                yetty_ysvg_xform_multiply(&tmp, &t1, &t2);
                yetty_ysvg_xform_multiply(&t, &tmp, &t3);
            }
        } else if (strcmp(kw, "skewX") == 0 && n == 1) {
            t.c = tanf(args[0] * (float)(M_PI / 180.0));
        } else if (strcmp(kw, "skewY") == 0 && n == 1) {
            t.b = tanf(args[0] * (float)(M_PI / 180.0));
        } else {
            /* Unknown transform — ignore quietly. */
            continue;
        }
        struct yetty_ysvg_xform composed;
        yetty_ysvg_xform_multiply(&composed, out, &t);
        *out = composed;
    }
    return 1;
}

/*=============================================================================
 * Color helpers (used by paint pass)
 *===========================================================================*/

uint32_t yetty_ysvg_rgba_to_abgr(uint32_t rgba)
{
    /* SVG packs as 0xRRGGBBAA; ydraw expects 0xAABBGGRR. */
    uint32_t r = (rgba >> 24) & 0xFFu;
    uint32_t g = (rgba >> 16) & 0xFFu;
    uint32_t b = (rgba >> 8) & 0xFFu;
    uint32_t a = rgba & 0xFFu;
    return (a << 24) | (b << 16) | (g << 8) | r;
}

uint32_t yetty_ysvg_rgba_mul_alpha(uint32_t rgba, float k)
{
    if (k <= 0.0f) {
        return rgba & 0xFFFFFF00u;
    }
    if (k >= 1.0f) {
        return rgba;
    }
    uint32_t a = rgba & 0xFFu;
    uint32_t na = (uint32_t)((float)a * k + 0.5f);
    if (na > 255) {
        na = 255;
    }
    return (rgba & 0xFFFFFF00u) | na;
}
