/*
 * yaml-parser.c — a tolerant YAML *subset* → chart IR.
 *
 * Charts are simple, so rather than pull in libyaml (which ydiagram, our
 * model, deliberately avoids) this parses the small subset chart documents
 * actually use. It is indentation/line based and understands:
 *
 *   chart: radar                 top-level scalars
 *   title: My title
 *   legend: true
 *   categories: [speed, power]   inline flow sequence
 *   categories:                  — or a block sequence —
 *     - speed
 *     - power
 *   data:                        a block map → category: value
 *     Chrome: 65
 *     Safari: 19
 *   series:                      a block sequence of maps
 *     - name: A
 *       color: "#5B8FF9"
 *       values: [3, 5, 2]        (values use an inline flow list)
 *   links:                       sankey flows
 *     - { source: A, target: B, value: 5 }
 *
 * Numeric value lists use the inline `[ ... ]` form. Block scalar lists under a
 * series `values:` are intentionally not supported (it keeps dash handling
 * unambiguous); everything common in chart YAML fits the rules above.
 */

#include <yetty/ychart/data-parser.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#else
#include <strings.h>
#endif

/*=============================================================================
 * Lines
 *===========================================================================*/

struct yaml_line {
    int indent;
    const char *text; /* trimmed (no indent, no trailing ws / CR) */
    size_t len;
};

static char *dup_n(const char *src, size_t len)
{
    char *out = malloc(len + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, src, len);
    out[len] = '\0';
    return out;
}

/* Strip one layer of surrounding quotes and ASCII whitespace; copies into buf
 * (NUL-terminated, truncated to buf_size). */
static void scalar_into(const char *ptr, size_t len, char *buf, size_t buf_size)
{
    while (len > 0 && (*ptr == ' ' || *ptr == '\t')) {
        ptr++;
        len--;
    }
    while (len > 0 && (ptr[len - 1] == ' ' || ptr[len - 1] == '\t')) {
        len--;
    }
    if (len >= 2 &&
        ((ptr[0] == '"' && ptr[len - 1] == '"') || (ptr[0] == '\'' && ptr[len - 1] == '\''))) {
        ptr++;
        len -= 2;
    }
    size_t n = len < buf_size - 1 ? len : buf_size - 1;
    memcpy(buf, ptr, n);
    buf[n] = '\0';
}

/* Split "key: value". Returns false if there is no colon. */
static bool split_kv(const char *text, size_t len, char *key, size_t key_size, const char **val,
                     size_t *val_len)
{
    const char *colon = memchr(text, ':', len);
    if (!colon) {
        return false;
    }
    scalar_into(text, (size_t)(colon - text), key, key_size);
    const char *v = colon + 1;
    size_t vl = len - (size_t)(colon + 1 - text);
    while (vl > 0 && (*v == ' ' || *v == '\t')) {
        v++;
        vl--;
    }
    *val = v;
    *val_len = vl;
    return true;
}

static bool parse_num(const char *ptr, size_t len, double *out)
{
    char buf[64];
    size_t w = 0;
    for (size_t i = 0; i < len && w < sizeof(buf) - 1; i++) {
        char c = ptr[i];
        if (c == ',' || c == '%' || c == ' ' || c == '"' || c == '\'') {
            continue;
        }
        buf[w++] = c;
    }
    buf[w] = '\0';
    if (w == 0) {
        return false;
    }
    char *e = NULL;
    double v = strtod(buf, &e);
    if (e == buf || *e != '\0') {
        return false;
    }
    *out = v;
    return true;
}

/*=============================================================================
 * Inline flow helpers
 *===========================================================================*/

/* Iterate the comma-separated items of "[ ... ]" (brackets optional). Calls
 * `fn(item_ptr, item_len, userdata)` for each; returns the first error. */
typedef struct yetty_ycore_void_result (*flow_item_fn)(const char *ptr, size_t len, void *userdata);

static struct yetty_ycore_void_result for_each_flow_item(const char *text, size_t len,
                                                         flow_item_fn fn, void *userdata)
{
    /* Trim and strip a single [...] / {...} wrapper. */
    while (len > 0 && (*text == ' ' || *text == '\t')) {
        text++;
        len--;
    }
    while (len > 0 && (text[len - 1] == ' ' || text[len - 1] == '\t')) {
        len--;
    }
    if (len >= 2 &&
        ((text[0] == '[' && text[len - 1] == ']') || (text[0] == '{' && text[len - 1] == '}'))) {
        text++;
        len -= 2;
    }
    size_t i = 0;
    while (i < len) {
        size_t start = i;
        while (i < len && text[i] != ',') {
            i++;
        }
        size_t item_len = i - start;
        if (item_len > 0) {
            struct yetty_ycore_void_result r = fn(text + start, item_len, userdata);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "yaml: flow item");
        }
        if (i < len) {
            i++; /* comma */
        }
    }
    return YETTY_OK_VOID();
}

struct push_ctx {
    struct yetty_ychart_chart *chart;
    size_t series_index;
};

static struct yetty_ycore_void_result push_value_item(const char *ptr, size_t len, void *userdata)
{
    struct push_ctx *ctx = userdata;
    double v = 0.0;
    parse_num(ptr, len, &v);
    return yetty_ychart_series_push(ctx->chart, ctx->series_index, v);
}

static struct yetty_ycore_void_result push_category_item(const char *ptr, size_t len,
                                                         void *userdata)
{
    struct yetty_ychart_chart *chart = userdata;
    char label[256];
    scalar_into(ptr, len, label, sizeof(label));
    struct yetty_ycore_int_result cr = yetty_ychart_add_category(chart, label);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, cr, "yaml: inline category");
    return YETTY_OK_VOID();
}

/* Parse "#RRGGBB"/"0xAARRGGBB"/"RRGGBB" → ARGB. */
static uint32_t parse_color(const char *s)
{
    if (!s || !*s) {
        return 0;
    }
    if (s[0] == '#') {
        s++;
    } else if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }
    size_t len = strlen(s);
    char *e = NULL;
    unsigned long v = strtoul(s, &e, 16);
    if (e == s) {
        return 0;
    }
    return len <= 6 ? (0xFF000000u | (uint32_t)(v & 0xFFFFFFu)) : (uint32_t)v;
}

/*=============================================================================
 * Series / links / data blocks
 *===========================================================================*/

/* Apply a "key: value" field to the series at `series_index`. */
static struct yetty_ycore_void_result series_field(struct yetty_ychart_chart *chart,
                                                   size_t series_index, const char *text,
                                                   size_t len)
{
    char key[64];
    const char *val;
    size_t val_len;
    if (!split_kv(text, len, key, sizeof(key), &val, &val_len)) {
        return YETTY_OK_VOID();
    }
    if (strcasecmp(key, "name") == 0 || strcasecmp(key, "label") == 0) {
        char buf[256];
        scalar_into(val, val_len, buf, sizeof(buf));
        free(chart->series[series_index].name);
        chart->series[series_index].name = dup_n(buf, strlen(buf));
        if (!chart->series[series_index].name) {
            return YETTY_ERR(yetty_ycore_void, "yaml: series name alloc");
        }
    } else if (strcasecmp(key, "color") == 0) {
        char buf[64];
        scalar_into(val, val_len, buf, sizeof(buf));
        chart->series[series_index].color = parse_color(buf);
    } else if (strcasecmp(key, "values") == 0 || strcasecmp(key, "data") == 0) {
        struct push_ctx ctx = {.chart = chart, .series_index = series_index};
        return for_each_flow_item(val, val_len, push_value_item, &ctx);
    }
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Parser
 *===========================================================================*/

struct yetty_ycore_void_result yetty_ychart_parse_yaml(const char *input, size_t len,
                                                       struct yetty_ychart_chart *chart)
{
    if (!input || !chart) {
        return YETTY_ERR(yetty_ycore_void, "parse_yaml: NULL input or chart");
    }

    /* 1. Split into trimmed logical lines (skip blank / # comment lines). */
    struct yaml_line *lines = NULL;
    size_t line_count = 0, line_cap = 0;
    size_t pos = 0;
    while (pos < len) {
        size_t line_start = pos;
        while (pos < len && input[pos] != '\n') {
            pos++;
        }
        size_t raw_len = pos - line_start;
        if (pos < len) {
            pos++;
        }
        const char *raw = input + line_start;
        int indent = 0;
        size_t i = 0;
        while (i < raw_len && (raw[i] == ' ' || raw[i] == '\t')) {
            indent++;
            i++;
        }
        const char *content = raw + i;
        size_t clen = raw_len - i;
        while (clen > 0 && (content[clen - 1] == '\r' || content[clen - 1] == ' ' ||
                            content[clen - 1] == '\t')) {
            clen--;
        }
        if (clen == 0 || content[0] == '#') {
            continue;
        }
        /* a lone "---" document marker is ignored */
        if (clen == 3 && memcmp(content, "---", 3) == 0) {
            continue;
        }
        if (line_count + 1 > line_cap) {
            size_t new_cap = line_cap ? line_cap * 2 : 16;
            struct yaml_line *grown = realloc(lines, new_cap * sizeof(*lines));
            if (!grown) {
                free(lines);
                return YETTY_ERR(yetty_ycore_void, "parse_yaml: out of memory");
            }
            lines = grown;
            line_cap = new_cap;
        }
        lines[line_count].indent = indent;
        lines[line_count].text = content;
        lines[line_count].len = clen;
        line_count++;
    }

    struct yetty_ycore_void_result result = YETTY_OK_VOID();
    size_t li = 0;
    while (li < line_count && YETTY_IS_OK(result)) {
        struct yaml_line line = lines[li];
        char key[64];
        const char *val;
        size_t val_len;
        if (!split_kv(line.text, line.len, key, sizeof(key), &val, &val_len)) {
            li++;
            continue;
        }
        int block_indent = line.indent;

        if (strcasecmp(key, "chart") == 0 || strcasecmp(key, "type") == 0) {
            char buf[64];
            scalar_into(val, val_len, buf, sizeof(buf));
            chart->kind = yetty_ychart_kind_from_name(buf);
            li++;
        } else if (strcasecmp(key, "title") == 0) {
            char buf[256];
            scalar_into(val, val_len, buf, sizeof(buf));
            result = yetty_ychart_set_title(chart, buf);
            li++;
        } else if (strcasecmp(key, "x") == 0 || strcasecmp(key, "xlabel") == 0) {
            char buf[256];
            scalar_into(val, val_len, buf, sizeof(buf));
            result = yetty_ychart_set_axis_labels(chart, buf, NULL);
            li++;
        } else if (strcasecmp(key, "y") == 0 || strcasecmp(key, "ylabel") == 0) {
            char buf[256];
            scalar_into(val, val_len, buf, sizeof(buf));
            result = yetty_ychart_set_axis_labels(chart, NULL, buf);
            li++;
        } else if (strcasecmp(key, "legend") == 0) {
            char buf[16];
            scalar_into(val, val_len, buf, sizeof(buf));
            chart->show_legend = (strcasecmp(buf, "true") == 0 || strcasecmp(buf, "on") == 0 ||
                                  strcasecmp(buf, "yes") == 0 || strcmp(buf, "1") == 0);
            li++;
        } else if (strcasecmp(key, "stacked") == 0) {
            char buf[16];
            scalar_into(val, val_len, buf, sizeof(buf));
            chart->stacked = (strcasecmp(buf, "true") == 0 || strcasecmp(buf, "on") == 0 ||
                              strcasecmp(buf, "yes") == 0 || strcmp(buf, "1") == 0);
            li++;
        } else if (strcasecmp(key, "values") == 0 && val_len == 0) {
            li++; /* stray */
        } else if (strcasecmp(key, "categories") == 0 || strcasecmp(key, "labels") == 0) {
            if (val_len > 0) {
                result = for_each_flow_item(val, val_len, push_category_item, chart);
                li++;
            } else {
                /* block sequence of "- item" lines */
                li++;
                while (li < line_count && lines[li].indent > block_indent && YETTY_IS_OK(result)) {
                    const char *t = lines[li].text;
                    size_t tl = lines[li].len;
                    if (t[0] == '-') {
                        t++;
                        tl--;
                    }
                    char label[256];
                    scalar_into(t, tl, label, sizeof(label));
                    struct yetty_ycore_int_result cr = yetty_ychart_add_category(chart, label);
                    if (YETTY_IS_ERR(cr)) {
                        result = YETTY_ERR(yetty_ycore_void, "yaml: block category", cr);
                    }
                    li++;
                }
            }
        } else if (strcasecmp(key, "data") == 0 && val_len == 0) {
            /* block map → category: value into a single implicit series */
            struct yetty_ycore_int_result sr = chart->series_count > 0
                                                   ? YETTY_OK(yetty_ycore_int, 0)
                                                   : yetty_ychart_add_series(chart, NULL, 0);
            if (YETTY_IS_ERR(sr)) {
                result = YETTY_ERR(yetty_ycore_void, "yaml: data series", sr);
            }
            size_t series_index = (size_t)sr.value;
            li++;
            while (li < line_count && lines[li].indent > block_indent && YETTY_IS_OK(result)) {
                char dkey[256];
                const char *dval;
                size_t dval_len;
                if (split_kv(lines[li].text, lines[li].len, dkey, sizeof(dkey), &dval, &dval_len)) {
                    struct yetty_ycore_int_result cr = yetty_ychart_add_category(chart, dkey);
                    if (YETTY_IS_ERR(cr)) {
                        result = YETTY_ERR(yetty_ycore_void, "yaml: data category", cr);
                    } else {
                        double v = 0.0;
                        parse_num(dval, dval_len, &v);
                        result = yetty_ychart_series_push(chart, series_index, v);
                    }
                }
                li++;
            }
        } else if (strcasecmp(key, "series") == 0 || strcasecmp(key, "datasets") == 0) {
            li++;
            int current_series = -1;
            while (li < line_count && lines[li].indent > block_indent && YETTY_IS_OK(result)) {
                const char *t = lines[li].text;
                size_t tl = lines[li].len;
                bool is_item = (t[0] == '-' && (tl == 1 || t[1] == ' '));
                if (is_item) {
                    struct yetty_ycore_int_result sr = yetty_ychart_add_series(chart, NULL, 0);
                    if (YETTY_IS_ERR(sr)) {
                        result = YETTY_ERR(yetty_ycore_void, "yaml: add series", sr);
                        break;
                    }
                    current_series = sr.value;
                    /* fields may follow the dash on the same line */
                    const char *rest = t + 1;
                    size_t rest_len = tl - 1;
                    while (rest_len > 0 && (*rest == ' ' || *rest == '\t')) {
                        rest++;
                        rest_len--;
                    }
                    if (rest_len > 0) {
                        result = series_field(chart, (size_t)current_series, rest, rest_len);
                    }
                } else if (current_series >= 0) {
                    result = series_field(chart, (size_t)current_series, t, tl);
                }
                li++;
            }
        } else if (strcasecmp(key, "links") == 0 || strcasecmp(key, "flows") == 0) {
            if (chart->kind == YETTY_YCHART_KIND_AUTO) {
                chart->kind = YETTY_YCHART_KIND_SANKEY;
            }
            li++;
            char src[256] = {0}, dst[256] = {0};
            double weight = 1.0;
            bool have_src = false, have_dst = false;
            while (li < line_count && lines[li].indent > block_indent && YETTY_IS_OK(result)) {
                const char *t = lines[li].text;
                size_t tl = lines[li].len;
                bool is_item = (t[0] == '-' && (tl == 1 || t[1] == ' '));
                const char *scan = t;
                size_t scan_len = tl;
                if (is_item) {
                    /* flush previous flow */
                    if (have_src && have_dst) {
                        result = yetty_ychart_add_flow(chart, src, dst, weight);
                    }
                    have_src = have_dst = false;
                    weight = 1.0;
                    scan = t + 1;
                    scan_len = tl - 1;
                    while (scan_len > 0 && (*scan == ' ' || *scan == '\t')) {
                        scan++;
                        scan_len--;
                    }
                    /* inline-map form: - { source: A, target: B, value: 5 } */
                    if (scan_len > 0 && scan[0] == '{') {
                        char inner[512];
                        scalar_into(scan + 1, scan_len >= 2 ? scan_len - 2 : 0, inner,
                                    sizeof(inner));
                        /* split on commas, each "k: v" */
                        size_t inner_len = strlen(inner);
                        size_t ti = 0;
                        while (ti < inner_len) {
                            size_t ts = ti;
                            while (ti < inner_len && inner[ti] != ',') {
                                ti++;
                            }
                            size_t tlen = ti - ts;
                            if (ti < inner_len) {
                                ti++; /* comma */
                            }
                            char fkey[64];
                            const char *fval;
                            size_t fval_len;
                            if (split_kv(inner + ts, tlen, fkey, sizeof(fkey), &fval, &fval_len)) {
                                char fbuf[256];
                                scalar_into(fval, fval_len, fbuf, sizeof(fbuf));
                                if (strcasecmp(fkey, "source") == 0) {
                                    snprintf(src, sizeof(src), "%s", fbuf);
                                    have_src = true;
                                } else if (strcasecmp(fkey, "target") == 0) {
                                    snprintf(dst, sizeof(dst), "%s", fbuf);
                                    have_dst = true;
                                } else if (strcasecmp(fkey, "value") == 0 ||
                                           strcasecmp(fkey, "weight") == 0) {
                                    parse_num(fval, fval_len, &weight);
                                }
                            }
                        }
                        li++;
                        continue;
                    }
                }
                /* block field form */
                char fkey[64];
                const char *fval;
                size_t fval_len;
                if (split_kv(scan, scan_len, fkey, sizeof(fkey), &fval, &fval_len)) {
                    char fbuf[256];
                    scalar_into(fval, fval_len, fbuf, sizeof(fbuf));
                    if (strcasecmp(fkey, "source") == 0 || strcasecmp(fkey, "from") == 0) {
                        snprintf(src, sizeof(src), "%s", fbuf);
                        have_src = true;
                    } else if (strcasecmp(fkey, "target") == 0 || strcasecmp(fkey, "to") == 0) {
                        snprintf(dst, sizeof(dst), "%s", fbuf);
                        have_dst = true;
                    } else if (strcasecmp(fkey, "value") == 0 || strcasecmp(fkey, "weight") == 0) {
                        parse_num(fval, fval_len, &weight);
                    }
                }
                li++;
            }
            if (YETTY_IS_OK(result) && have_src && have_dst) {
                result = yetty_ychart_add_flow(chart, src, dst, weight);
            }
        } else {
            li++;
        }
    }

    free(lines);
    if (YETTY_IS_ERR(result)) {
        return result;
    }

    /* Auto categories if series provided values but no categories. */
    if (chart->category_count == 0 && chart->series_count > 0) {
        size_t longest = 0;
        for (size_t s = 0; s < chart->series_count; s++) {
            if (chart->series[s].value_count > longest) {
                longest = chart->series[s].value_count;
            }
        }
        for (size_t i = 0; i < longest; i++) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%zu", i + 1);
            struct yetty_ycore_int_result cr = yetty_ychart_add_category(chart, buf);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, cr, "parse_yaml: auto category");
        }
    }
    return YETTY_OK_VOID();
}
