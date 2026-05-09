/*
 * ygui_css.c — minimal CSS-like parser for ygui layout.
 *
 * Parses a one-shot string of "property: value;" pairs and applies them
 * to a widget's `struct yetty_ygui_layout`. Designed for ergonomic
 * configuration of flex containers; not a real CSS engine.
 *
 * Tokenization is simple: scan to ':' for property name, then to ';' or
 * end-of-string for value. Whitespace is trimmed from both sides.
 *
 * Number parsing accepts integer or decimal, optional sign, optional
 * suffix `px` (default) or `%` (percent). `auto` is recognized for
 * flex-basis and the alignment family.
 *
 * Unknown properties or values are reported via the returned Result's
 * cause chain, but parsing of remaining declarations continues — the
 * library prefers "best effort" to "all-or-nothing" so a typo in one
 * property doesn't blow up the whole stylesheet.
 *
 * See ygui.h doc comment on yetty_ygui_widget_apply_css for the full
 * recognized property list.
 */

#include "ygui_internal.h"

#include <stdio.h>
#include <string.h>

/*=============================================================================
 * Tiny string helpers (operate on (ptr, len) pairs to avoid alloc).
 *===========================================================================*/

static int css_isspace(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

/* Trim leading/trailing whitespace from [*p, *p + *len). */
static void css_trim(const char **p, size_t *len)
{
    while (*len > 0 && css_isspace((*p)[0])) {
        (*p)++;
        (*len)--;
    }
    while (*len > 0 && css_isspace((*p)[*len - 1])) {
        (*len)--;
    }
}

static int css_eqi(const char *a, size_t alen, const char *lit)
{
    size_t llen = strlen(lit);
    if (alen != llen) {
        return 0;
    }
    for (size_t i = 0; i < alen; i++) {
        char ca = a[i];
        char cb = lit[i];
        if (ca >= 'A' && ca <= 'Z') {
            ca = (char)(ca - 'A' + 'a');
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (char)(cb - 'A' + 'a');
        }
        if (ca != cb) {
            return 0;
        }
    }
    return 1;
}

/*=============================================================================
 * Token = parsed value: a number with an optional unit, or a keyword.
 *===========================================================================*/

enum css_unit {
    CSS_UNIT_NONE,
    CSS_UNIT_PX,
    CSS_UNIT_PCT,
    CSS_UNIT_AUTO,
};

struct css_num {
    float value;
    enum css_unit unit;
    int ok; /* 1 if parsed, 0 otherwise */
};

/* Parse a single number with optional sign and decimal, plus optional
 * unit suffix (px or %). 'auto' returns ok=1 with unit=AUTO. Advances
 * *p / *len past the consumed bytes. */
static struct css_num css_parse_num(const char **p, size_t *len)
{
    struct css_num r = {0};
    css_trim(p, len);
    if (*len == 0) {
        return r;
    }

    if (*len >= 4 && css_eqi(*p, 4, "auto")) {
        r.unit = CSS_UNIT_AUTO;
        r.ok = 1;
        *p += 4;
        *len -= 4;
        return r;
    }

    const char *s = *p;
    size_t i = 0;
    int sign = 1;
    if (i < *len && (s[i] == '+' || s[i] == '-')) {
        if (s[i] == '-') {
            sign = -1;
        }
        i++;
    }
    int saw_digit = 0;
    float v = 0.0f;
    while (i < *len && s[i] >= '0' && s[i] <= '9') {
        v = v * 10.0f + (float)(s[i] - '0');
        saw_digit = 1;
        i++;
    }
    if (i < *len && s[i] == '.') {
        i++;
        float frac = 0.1f;
        while (i < *len && s[i] >= '0' && s[i] <= '9') {
            v += (float)(s[i] - '0') * frac;
            frac *= 0.1f;
            saw_digit = 1;
            i++;
        }
    }
    if (!saw_digit) {
        return r;
    }
    r.value = sign * v;
    /* Unit suffix */
    r.unit = CSS_UNIT_PX;
    if (i < *len && s[i] == '%') {
        r.unit = CSS_UNIT_PCT;
        i++;
    } else if (i + 2 <= *len && (s[i] == 'p' || s[i] == 'P') &&
               (s[i + 1] == 'x' || s[i + 1] == 'X')) {
        r.unit = CSS_UNIT_PX;
        i += 2;
    }
    r.ok = 1;
    *p += i;
    *len -= i;
    return r;
}

/* Parse up to 4 numbers separated by whitespace (the CSS shorthand for
 * padding / margin). Returns count actually parsed (1..4). */
static int css_parse_num_list(const char *p, size_t len, struct css_num out[4])
{
    int n = 0;
    while (n < 4) {
        css_trim(&p, &len);
        if (len == 0) {
            break;
        }
        out[n] = css_parse_num(&p, &len);
        if (!out[n].ok) {
            break;
        }
        n++;
    }
    return n;
}

/* Apply a 1..4-value padding/margin shorthand to (top, right, bottom, left). */
static void css_expand_4(const struct css_num *vals, int n, float ref_w, float ref_h, float *t,
                         float *r, float *b, float *l)
{
    /* All values use ref_w for percent; padding/margin spec uses parent
     * width even for vertical sides. */
    (void)ref_h;
    float v[4] = {0, 0, 0, 0};
    for (int i = 0; i < n && i < 4; i++) {
        float x = vals[i].value;
        if (vals[i].unit == CSS_UNIT_PCT) {
            x = ref_w * (vals[i].value / 100.0f);
        }
        v[i] = x;
    }
    switch (n) {
    case 1:
        *t = *r = *b = *l = v[0];
        break;
    case 2:
        *t = *b = v[0];
        *r = *l = v[1];
        break;
    case 3:
        *t = v[0];
        *r = *l = v[1];
        *b = v[2];
        break;
    case 4:
    default:
        *t = v[0];
        *r = v[1];
        *b = v[2];
        *l = v[3];
        break;
    }
}

/*=============================================================================
 * Property dispatcher
 *===========================================================================*/

struct css_diag {
    char buf[256];
    int has_msg;
};

static void diag_add(struct css_diag *d, const char *msg, const char *prop, size_t prop_len,
                     const char *val, size_t val_len)
{
    if (d->has_msg) {
        return; /* report only the first issue */
    }
    int n = snprintf(d->buf, sizeof d->buf, "%s [%.*s: %.*s]", msg, (int)prop_len, prop,
                     (int)val_len, val);
    (void)n;
    d->has_msg = 1;
}

struct align_kw {
    const char *name;
    ygui_align_t value;
};

static const struct align_kw align_keywords[] = {
    {"auto", YETTY_YGUI_ALIGN_AUTO},        {"start", YETTY_YGUI_ALIGN_START},
    {"flex-start", YETTY_YGUI_ALIGN_START}, {"center", YETTY_YGUI_ALIGN_CENTER},
    {"end", YETTY_YGUI_ALIGN_END},          {"flex-end", YETTY_YGUI_ALIGN_END},
    {"stretch", YETTY_YGUI_ALIGN_STRETCH},  {"baseline", YETTY_YGUI_ALIGN_BASELINE},
};

static int parse_align(const char *v, size_t vlen, ygui_align_t *out)
{
    for (size_t i = 0; i < sizeof align_keywords / sizeof align_keywords[0]; i++) {
        if (css_eqi(v, vlen, align_keywords[i].name)) {
            *out = align_keywords[i].value;
            return 1;
        }
    }
    return 0;
}

struct justify_kw {
    const char *name;
    ygui_justify_t value;
};

static const struct justify_kw justify_keywords[] = {
    {"start", YETTY_YGUI_JUSTIFY_START},
    {"flex-start", YETTY_YGUI_JUSTIFY_START},
    {"center", YETTY_YGUI_JUSTIFY_CENTER},
    {"end", YETTY_YGUI_JUSTIFY_END},
    {"flex-end", YETTY_YGUI_JUSTIFY_END},
    {"space-between", YETTY_YGUI_JUSTIFY_SPACE_BETWEEN},
    {"space-around", YETTY_YGUI_JUSTIFY_SPACE_AROUND},
    {"space-evenly", YETTY_YGUI_JUSTIFY_SPACE_EVENLY},
};

static int parse_justify(const char *v, size_t vlen, ygui_justify_t *out)
{
    for (size_t i = 0; i < sizeof justify_keywords / sizeof justify_keywords[0]; i++) {
        if (css_eqi(v, vlen, justify_keywords[i].name)) {
            *out = justify_keywords[i].value;
            return 1;
        }
    }
    return 0;
}

static void apply_one(struct yetty_ygui_widget *w, const char *prop, size_t plen, const char *val,
                      size_t vlen, struct css_diag *diag)
{
    css_trim(&prop, &plen);
    css_trim(&val, &vlen);
    if (plen == 0 || vlen == 0) {
        return;
    }

    /* display: flex | manual */
    if (css_eqi(prop, plen, "display")) {
        if (css_eqi(val, vlen, "flex")) {
            w->layout.mode = YETTY_YGUI_LAYOUT_FLEX;
        } else if (css_eqi(val, vlen, "manual") || css_eqi(val, vlen, "block")) {
            w->layout.mode = YETTY_YGUI_LAYOUT_MANUAL;
        } else {
            diag_add(diag, "unknown display value", prop, plen, val, vlen);
        }
        return;
    }

    if (css_eqi(prop, plen, "flex-direction")) {
        if (css_eqi(val, vlen, "row")) {
            w->layout.direction = YETTY_YGUI_FLEX_ROW;
        } else if (css_eqi(val, vlen, "column")) {
            w->layout.direction = YETTY_YGUI_FLEX_COLUMN;
        } else {
            diag_add(diag, "unknown flex-direction", prop, plen, val, vlen);
        }
        return;
    }

    if (css_eqi(prop, plen, "flex-wrap")) {
        if (css_eqi(val, vlen, "nowrap")) {
            w->layout.wrap = YETTY_YGUI_FLEX_NOWRAP;
        } else if (css_eqi(val, vlen, "wrap")) {
            w->layout.wrap = YETTY_YGUI_FLEX_WRAP;
        } else {
            diag_add(diag, "unknown flex-wrap", prop, plen, val, vlen);
        }
        return;
    }

    if (css_eqi(prop, plen, "justify-content")) {
        ygui_justify_t j;
        if (parse_justify(val, vlen, &j)) {
            w->layout.justify_content = j;
        } else {
            diag_add(diag, "unknown justify-content", prop, plen, val, vlen);
        }
        return;
    }

    if (css_eqi(prop, plen, "align-items") || css_eqi(prop, plen, "align-self") ||
        css_eqi(prop, plen, "align-content")) {
        ygui_align_t a;
        if (parse_align(val, vlen, &a)) {
            if (css_eqi(prop, plen, "align-items")) {
                w->layout.align_items = a;
            }
            if (css_eqi(prop, plen, "align-self")) {
                w->layout.align_self = a;
            }
            if (css_eqi(prop, plen, "align-content")) {
                w->layout.align_content = a;
            }
        } else {
            diag_add(diag, "unknown align value", prop, plen, val, vlen);
        }
        return;
    }

    if (css_eqi(prop, plen, "position")) {
        if (css_eqi(val, vlen, "relative") || css_eqi(val, vlen, "static")) {
            w->layout.position = YETTY_YGUI_POSITION_RELATIVE;
        } else if (css_eqi(val, vlen, "absolute")) {
            w->layout.position = YETTY_YGUI_POSITION_ABSOLUTE;
        } else {
            diag_add(diag, "unknown position", prop, plen, val, vlen);
        }
        return;
    }

    if (css_eqi(prop, plen, "flex")) {
        /* "<grow> [<shrink> [<basis>]]". A single number means grow=N. */
        struct css_num nums[4];
        int n = css_parse_num_list(val, vlen, nums);
        if (n >= 1) {
            w->layout.flex_grow = nums[0].value;
        }
        if (n >= 2) {
            w->layout.flex_shrink = nums[1].value;
        }
        if (n >= 3) {
            if (nums[2].unit == CSS_UNIT_PCT) {
                w->layout.flex_basis = 0.0f;
                w->layout.flex_basis_percent = nums[2].value;
            } else if (nums[2].unit == CSS_UNIT_AUTO) {
                w->layout.flex_basis = 0.0f;
                w->layout.flex_basis_percent = 0.0f;
            } else {
                w->layout.flex_basis = nums[2].value;
                w->layout.flex_basis_percent = 0.0f;
            }
        }
        if (n == 0) {
            diag_add(diag, "could not parse flex", prop, plen, val, vlen);
        }
        return;
    }

    if (css_eqi(prop, plen, "flex-grow") || css_eqi(prop, plen, "flex-shrink")) {
        struct css_num nv = css_parse_num(&val, &vlen);
        if (!nv.ok) {
            diag_add(diag, "could not parse number", prop, plen, val, vlen);
            return;
        }
        if (css_eqi(prop, plen, "flex-grow")) {
            w->layout.flex_grow = nv.value;
        }
        if (css_eqi(prop, plen, "flex-shrink")) {
            w->layout.flex_shrink = nv.value;
        }
        return;
    }

    if (css_eqi(prop, plen, "flex-basis")) {
        struct css_num nv = css_parse_num(&val, &vlen);
        if (!nv.ok) {
            diag_add(diag, "could not parse flex-basis", prop, plen, val, vlen);
            return;
        }
        if (nv.unit == CSS_UNIT_AUTO) {
            w->layout.flex_basis = 0.0f;
            w->layout.flex_basis_percent = 0.0f;
        } else if (nv.unit == CSS_UNIT_PCT) {
            w->layout.flex_basis = 0.0f;
            w->layout.flex_basis_percent = nv.value;
        } else {
            w->layout.flex_basis = nv.value;
            w->layout.flex_basis_percent = 0.0f;
        }
        return;
    }

    if (css_eqi(prop, plen, "gap")) {
        struct css_num nv = css_parse_num(&val, &vlen);
        if (nv.ok) {
            w->layout.gap = nv.value;
        } else {
            diag_add(diag, "could not parse gap", prop, plen, val, vlen);
        }
        return;
    }

    if (css_eqi(prop, plen, "padding") || css_eqi(prop, plen, "margin")) {
        struct css_num nums[4];
        int n = css_parse_num_list(val, vlen, nums);
        if (n == 0) {
            diag_add(diag, "could not parse shorthand", prop, plen, val, vlen);
            return;
        }
        float t = 0, r = 0, b = 0, l = 0;
        /* Use authored_w as percent reference (CSS uses parent width for both). */
        css_expand_4(nums, n, w->authored_w, w->authored_h, &t, &r, &b, &l);
        if (css_eqi(prop, plen, "padding")) {
            w->layout.padding_top = t;
            w->layout.padding_right = r;
            w->layout.padding_bottom = b;
            w->layout.padding_left = l;
        } else {
            w->layout.margin_top = t;
            w->layout.margin_right = r;
            w->layout.margin_bottom = b;
            w->layout.margin_left = l;
        }
        return;
    }

    /* width / height as percent only (px is "authored" — use set_size). */
    if (css_eqi(prop, plen, "width")) {
        struct css_num nv = css_parse_num(&val, &vlen);
        if (nv.ok && nv.unit == CSS_UNIT_PCT) {
            w->layout.width_percent = nv.value;
        } else if (nv.ok) {
            w->authored_w = nv.value;
            w->w = nv.value;
        } else {
            diag_add(diag, "could not parse width", prop, plen, val, vlen);
        }
        return;
    }
    if (css_eqi(prop, plen, "height")) {
        struct css_num nv = css_parse_num(&val, &vlen);
        if (nv.ok && nv.unit == CSS_UNIT_PCT) {
            w->layout.height_percent = nv.value;
        } else if (nv.ok) {
            w->authored_h = nv.value;
            w->h = nv.value;
        } else {
            diag_add(diag, "could not parse height", prop, plen, val, vlen);
        }
        return;
    }

    /* min/max width/height — px or percent. */
    struct {
        const char *name;
        float *px;
        float *pct;
    } size_props[] = {
        {"min-width", &w->layout.min_w, &w->layout.min_w_percent},
        {"min-height", &w->layout.min_h, &w->layout.min_h_percent},
        {"max-width", &w->layout.max_w, &w->layout.max_w_percent},
        {"max-height", &w->layout.max_h, &w->layout.max_h_percent},
    };
    for (size_t i = 0; i < sizeof size_props / sizeof size_props[0]; i++) {
        if (css_eqi(prop, plen, size_props[i].name)) {
            struct css_num nv = css_parse_num(&val, &vlen);
            if (!nv.ok) {
                diag_add(diag, "could not parse min/max size", prop, plen, val, vlen);
                return;
            }
            if (nv.unit == CSS_UNIT_PCT) {
                *size_props[i].pct = nv.value;
                *size_props[i].px = 0.0f;
            } else {
                *size_props[i].px = nv.value;
                *size_props[i].pct = 0.0f;
            }
            return;
        }
    }

    /* Unknown property — record but don't fail the whole string. */
    diag_add(diag, "unknown property", prop, plen, val, vlen);
}

/*=============================================================================
 * Public entry
 *===========================================================================*/

struct yetty_ycore_void_result yetty_ygui_widget_apply_css(struct yetty_ygui_widget *widget,
                                                           const char *css)
{
    if (!widget) {
        return YETTY_ERR(yetty_ycore_void, "apply_css: NULL widget");
    }
    if (!css) {
        return YETTY_OK_VOID();
    }

    struct css_diag diag = {0};
    const char *p = css;
    size_t total = strlen(css);
    const char *end = p + total;

    while (p < end) {
        /* Skip leading whitespace and stray semicolons. */
        while (p < end && (css_isspace(*p) || *p == ';')) {
            p++;
        }
        if (p >= end) {
            break;
        }
        const char *prop_start = p;
        while (p < end && *p != ':' && *p != ';') {
            p++;
        }
        if (p >= end || *p != ':') {
            /* Malformed declaration (no colon) — skip to next ';'. */
            while (p < end && *p != ';') {
                p++;
            }
            continue;
        }
        size_t plen = (size_t)(p - prop_start);
        p++; /* consume ':' */
        const char *val_start = p;
        while (p < end && *p != ';') {
            p++;
        }
        size_t vlen = (size_t)(p - val_start);
        apply_one(widget, prop_start, plen, val_start, vlen, &diag);
        if (p < end && *p == ';') {
            p++;
        }
    }

    if (widget->engine) {
        widget->engine->dirty = 1;
    }

    if (diag.has_msg) {
        return YETTY_ERR(yetty_ycore_void, diag.buf);
    }
    return YETTY_OK_VOID();
}
