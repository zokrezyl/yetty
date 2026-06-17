/*
 * json-parser.c — JSON → chart IR.
 *
 * Self-contained recursive-descent JSON parser (no external dependency, in the
 * spirit of ylottie's own parser). It builds a small tagged tree, then maps
 * the recognised chart schema onto the IR and frees the tree.
 *
 * Accepted shapes (all keys optional unless noted):
 *
 *   { "chart": "pie", "title": "...", "x": "...", "y": "...",
 *     "legend": true, "stacked": false,
 *     "data": { "Chrome": 65, "Safari": 19 } }          object → category:value
 *
 *   { "chart": "column",
 *     "categories": ["Q1","Q2"],
 *     "series": [ { "name": "2021", "values": [10, 20] },
 *                 { "name": "2022", "color": "#5B8FF9", "values": [12, 18] } ] }
 *
 *   { "chart": "sankey",
 *     "links": [ { "source": "A", "target": "B", "value": 5 } ] }
 *
 *   [ { "label": "A", "value": 30 }, { "label": "B", "value": 20 } ]  bare array
 *   [ 1, 2, 3 ]                                                       bare values
 */

#include <yetty/ychart/data-parser.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

/*=============================================================================
 * JSON value tree
 *===========================================================================*/

enum json_type { JSON_NULL, JSON_BOOL, JSON_NUMBER, JSON_STRING, JSON_ARRAY, JSON_OBJECT };

struct json_value {
    enum json_type type;
    bool boolean;
    double number;
    char *string; /* unescaped, NUL-terminated (JSON_STRING) */
    struct json_value **items;
    char **keys; /* object keys, parallel to items (JSON_OBJECT) */
    size_t count;
    size_t capacity;
};

struct json_cursor {
    const char *p;
    const char *end;
    int depth;
    bool error;
};

#define JSON_MAX_DEPTH 128

static struct json_value *json_parse_value(struct json_cursor *cur);

static void json_free(struct json_value *value)
{
    if (!value) {
        return;
    }
    free(value->string);
    for (size_t i = 0; i < value->count; i++) {
        json_free(value->items[i]);
        if (value->keys) {
            free(value->keys[i]);
        }
    }
    free(value->items);
    free(value->keys);
    free(value);
}

static void json_skip_ws(struct json_cursor *cur)
{
    while (cur->p < cur->end) {
        char c = *cur->p;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            cur->p++;
        } else if (c == '/' && cur->p + 1 < cur->end && cur->p[1] == '/') {
            /* tolerate // line comments (JSON5-ish) */
            while (cur->p < cur->end && *cur->p != '\n') {
                cur->p++;
            }
        } else {
            break;
        }
    }
}

/* Append (key, child) to a container. key may be NULL for arrays. Returns
 * false on alloc failure. */
static bool json_push(struct json_value *parent, char *key, struct json_value *child)
{
    if (parent->count + 1 > parent->capacity) {
        size_t new_cap = parent->capacity ? parent->capacity * 2 : 8;
        struct json_value **items = realloc(parent->items, new_cap * sizeof(*items));
        if (!items) {
            return false;
        }
        parent->items = items;
        if (parent->type == JSON_OBJECT) {
            char **keys = realloc(parent->keys, new_cap * sizeof(*keys));
            if (!keys) {
                return false;
            }
            parent->keys = keys;
        }
        parent->capacity = new_cap;
    }
    if (parent->type == JSON_OBJECT) {
        parent->keys[parent->count] = key;
    }
    parent->items[parent->count] = child;
    parent->count++;
    return true;
}

/* Parse a JSON string literal (cursor at opening quote). Returns malloc'd
 * unescaped NUL-terminated bytes, or NULL on error. */
static char *json_parse_string_raw(struct json_cursor *cur)
{
    if (cur->p >= cur->end || *cur->p != '"') {
        cur->error = true;
        return NULL;
    }
    cur->p++;
    size_t cap = 32;
    size_t len = 0;
    char *out = malloc(cap);
    if (!out) {
        cur->error = true;
        return NULL;
    }
    while (cur->p < cur->end && *cur->p != '"') {
        char c = *cur->p++;
        if (c == '\\' && cur->p < cur->end) {
            char esc = *cur->p++;
            switch (esc) {
            case 'n':
                c = '\n';
                break;
            case 't':
                c = '\t';
                break;
            case 'r':
                c = '\r';
                break;
            case 'b':
                c = '\b';
                break;
            case 'f':
                c = '\f';
                break;
            case '/':
                c = '/';
                break;
            case '\\':
                c = '\\';
                break;
            case '"':
                c = '"';
                break;
            case 'u': {
                /* Minimal BMP \uXXXX → UTF-8. */
                if (cur->p + 4 > cur->end) {
                    free(out);
                    cur->error = true;
                    return NULL;
                }
                unsigned code = 0;
                for (int i = 0; i < 4; i++) {
                    char h = *cur->p++;
                    code <<= 4;
                    if (h >= '0' && h <= '9') {
                        code |= (unsigned)(h - '0');
                    } else if (h >= 'a' && h <= 'f') {
                        code |= (unsigned)(h - 'a' + 10);
                    } else if (h >= 'A' && h <= 'F') {
                        code |= (unsigned)(h - 'A' + 10);
                    }
                }
                /* Encode up to 3-byte UTF-8 (covers the BMP). */
                char utf8[3];
                size_t ulen = 0;
                if (code < 0x80u) {
                    utf8[ulen++] = (char)code;
                } else if (code < 0x800u) {
                    utf8[ulen++] = (char)(0xC0u | (code >> 6));
                    utf8[ulen++] = (char)(0x80u | (code & 0x3Fu));
                } else {
                    utf8[ulen++] = (char)(0xE0u | (code >> 12));
                    utf8[ulen++] = (char)(0x80u | ((code >> 6) & 0x3Fu));
                    utf8[ulen++] = (char)(0x80u | (code & 0x3Fu));
                }
                for (size_t k = 0; k < ulen; k++) {
                    if (len + 1 >= cap) {
                        cap *= 2;
                        char *grown = realloc(out, cap);
                        if (!grown) {
                            free(out);
                            cur->error = true;
                            return NULL;
                        }
                        out = grown;
                    }
                    out[len++] = utf8[k];
                }
                continue;
            }
            default:
                c = esc;
                break;
            }
        }
        if (len + 1 >= cap) {
            cap *= 2;
            char *grown = realloc(out, cap);
            if (!grown) {
                free(out);
                cur->error = true;
                return NULL;
            }
            out = grown;
        }
        out[len++] = c;
    }
    if (cur->p >= cur->end) {
        free(out);
        cur->error = true;
        return NULL;
    }
    cur->p++; /* closing quote */
    out[len] = '\0';
    return out;
}

static struct json_value *json_new(enum json_type type)
{
    struct json_value *value = calloc(1, sizeof(*value));
    if (value) {
        value->type = type;
    }
    return value;
}

static struct json_value *json_parse_value(struct json_cursor *cur)
{
    if (cur->error || cur->depth > JSON_MAX_DEPTH) {
        cur->error = true;
        return NULL;
    }
    json_skip_ws(cur);
    if (cur->p >= cur->end) {
        cur->error = true;
        return NULL;
    }
    char c = *cur->p;

    if (c == '"') {
        struct json_value *value = json_new(JSON_STRING);
        if (!value) {
            cur->error = true;
            return NULL;
        }
        value->string = json_parse_string_raw(cur);
        if (cur->error) {
            json_free(value);
            return NULL;
        }
        return value;
    }
    if (c == '{' || c == '[') {
        bool is_obj = (c == '{');
        char close = is_obj ? '}' : ']';
        struct json_value *value = json_new(is_obj ? JSON_OBJECT : JSON_ARRAY);
        if (!value) {
            cur->error = true;
            return NULL;
        }
        cur->p++;
        cur->depth++;
        json_skip_ws(cur);
        if (cur->p < cur->end && *cur->p == close) {
            cur->p++;
            cur->depth--;
            return value;
        }
        for (;;) {
            json_skip_ws(cur);
            char *key = NULL;
            if (is_obj) {
                key = json_parse_string_raw(cur);
                if (cur->error) {
                    json_free(value);
                    return NULL;
                }
                json_skip_ws(cur);
                if (cur->p >= cur->end || *cur->p != ':') {
                    free(key);
                    json_free(value);
                    cur->error = true;
                    return NULL;
                }
                cur->p++;
            }
            struct json_value *child = json_parse_value(cur);
            if (cur->error || !child) {
                free(key);
                json_free(child);
                json_free(value);
                cur->error = true;
                return NULL;
            }
            if (!json_push(value, key, child)) {
                free(key);
                json_free(child);
                json_free(value);
                cur->error = true;
                return NULL;
            }
            json_skip_ws(cur);
            if (cur->p < cur->end && *cur->p == ',') {
                cur->p++;
                continue;
            }
            if (cur->p < cur->end && *cur->p == close) {
                cur->p++;
                cur->depth--;
                break;
            }
            json_free(value);
            cur->error = true;
            return NULL;
        }
        return value;
    }
    if (c == 't' || c == 'f') {
        bool is_true = (c == 't');
        const char *lit = is_true ? "true" : "false";
        size_t lit_len = is_true ? 4 : 5;
        if ((size_t)(cur->end - cur->p) < lit_len || memcmp(cur->p, lit, lit_len) != 0) {
            cur->error = true;
            return NULL;
        }
        cur->p += lit_len;
        struct json_value *value = json_new(JSON_BOOL);
        if (!value) {
            cur->error = true;
            return NULL;
        }
        value->boolean = is_true;
        return value;
    }
    if (c == 'n') {
        if ((size_t)(cur->end - cur->p) < 4 || memcmp(cur->p, "null", 4) != 0) {
            cur->error = true;
            return NULL;
        }
        cur->p += 4;
        return json_new(JSON_NULL);
    }
    /* number */
    {
        char buf[64];
        size_t n = 0;
        while (cur->p < cur->end && n < sizeof(buf) - 1) {
            char d = *cur->p;
            if ((d >= '0' && d <= '9') || d == '-' || d == '+' || d == '.' || d == 'e' ||
                d == 'E') {
                buf[n++] = d;
                cur->p++;
            } else {
                break;
            }
        }
        if (n == 0) {
            cur->error = true;
            return NULL;
        }
        buf[n] = '\0';
        char *e = NULL;
        double value_num = strtod(buf, &e);
        if (e == buf) {
            cur->error = true;
            return NULL;
        }
        struct json_value *value = json_new(JSON_NUMBER);
        if (!value) {
            cur->error = true;
            return NULL;
        }
        value->number = value_num;
        return value;
    }
}

/*=============================================================================
 * Object lookups
 *===========================================================================*/

static struct json_value *obj_get(const struct json_value *obj, const char *key)
{
    if (!obj || obj->type != JSON_OBJECT) {
        return NULL;
    }
    for (size_t i = 0; i < obj->count; i++) {
        if (obj->keys[i] && strcasecmp(obj->keys[i], key) == 0) {
            return obj->items[i];
        }
    }
    return NULL;
}

/* First present key among a NULL-terminated list of alternatives. */
static struct json_value *obj_get_any(const struct json_value *obj, const char *const *keys)
{
    for (size_t i = 0; keys[i]; i++) {
        struct json_value *found = obj_get(obj, keys[i]);
        if (found) {
            return found;
        }
    }
    return NULL;
}

static bool value_as_number(const struct json_value *value, double *out)
{
    if (!value) {
        return false;
    }
    if (value->type == JSON_NUMBER) {
        *out = value->number;
        return true;
    }
    if (value->type == JSON_STRING && value->string) {
        char *e = NULL;
        double n = strtod(value->string, &e);
        if (e != value->string) {
            *out = n;
            return true;
        }
    }
    if (value->type == JSON_BOOL) {
        *out = value->boolean ? 1.0 : 0.0;
        return true;
    }
    return false;
}

static const char *value_as_string(const struct json_value *value)
{
    return (value && value->type == JSON_STRING) ? value->string : NULL;
}

/* Parse "#RRGGBB" / "0xAARRGGBB" / "RRGGBB" → ARGB. 0 if unrecognised. */
static uint32_t parse_color_string(const char *s)
{
    if (!s) {
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
    if (len <= 6) {
        return 0xFF000000u | (uint32_t)(v & 0xFFFFFFu);
    }
    return (uint32_t)v;
}

/*=============================================================================
 * Tree → chart
 *===========================================================================*/

/* Add (or reuse) a single implicit series, returning its index. */
static struct yetty_ycore_int_result ensure_default_series(struct yetty_ychart_chart *chart)
{
    if (chart->series_count > 0) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    return yetty_ychart_add_series(chart, NULL, 0);
}

/* Map a "data" value: object {label:num} or array of numbers / {label,value}. */
static struct yetty_ycore_void_result map_data(struct yetty_ychart_chart *chart,
                                               const struct json_value *data)
{
    struct yetty_ycore_int_result sr = ensure_default_series(chart);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "json: default series");
    size_t series_index = (size_t)sr.value;

    if (data->type == JSON_OBJECT) {
        for (size_t i = 0; i < data->count; i++) {
            double value = 0.0;
            value_as_number(data->items[i], &value);
            struct yetty_ycore_int_result cr =
                yetty_ychart_add_category(chart, data->keys[i] ? data->keys[i] : "");
            YETTY_RETURN_IF_ERR(yetty_ycore_void, cr, "json: data category");
            struct yetty_ycore_void_result pv =
                yetty_ychart_series_push(chart, series_index, value);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, pv, "json: data value");
        }
        return YETTY_OK_VOID();
    }
    if (data->type == JSON_ARRAY) {
        static const char *const label_keys[] = {"label", "name", "category", "x", NULL};
        static const char *const value_keys[] = {"value", "y", "count", "v", NULL};
        for (size_t i = 0; i < data->count; i++) {
            const struct json_value *item = data->items[i];
            double value = 0.0;
            char label_buf[32];
            const char *label = NULL;
            if (item->type == JSON_OBJECT) {
                value_as_number(obj_get_any(item, value_keys), &value);
                label = value_as_string(obj_get_any(item, label_keys));
            } else {
                value_as_number(item, &value);
            }
            if (!label) {
                snprintf(label_buf, sizeof(label_buf), "%zu", i + 1);
                label = label_buf;
            }
            struct yetty_ycore_int_result cr = yetty_ychart_add_category(chart, label);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, cr, "json: data category");
            struct yetty_ycore_void_result pv =
                yetty_ychart_series_push(chart, series_index, value);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, pv, "json: data value");
        }
        return YETTY_OK_VOID();
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result map_categories(struct yetty_ychart_chart *chart,
                                                     const struct json_value *cats)
{
    if (cats->type != JSON_ARRAY) {
        return YETTY_OK_VOID();
    }
    for (size_t i = 0; i < cats->count; i++) {
        const char *label = value_as_string(cats->items[i]);
        char buf[32];
        if (!label) {
            double n = 0.0;
            if (value_as_number(cats->items[i], &n)) {
                snprintf(buf, sizeof(buf), "%g", n);
                label = buf;
            } else {
                label = "";
            }
        }
        struct yetty_ycore_int_result cr = yetty_ychart_add_category(chart, label);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, cr, "json: category");
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result map_series(struct yetty_ychart_chart *chart,
                                                 const struct json_value *series_arr)
{
    if (series_arr->type != JSON_ARRAY) {
        return YETTY_OK_VOID();
    }
    static const char *const values_keys[] = {"values", "data", "y", NULL};
    for (size_t i = 0; i < series_arr->count; i++) {
        const struct json_value *s = series_arr->items[i];
        const char *name = NULL;
        uint32_t color = 0;
        const struct json_value *values = s;
        if (s->type == JSON_OBJECT) {
            name = value_as_string(obj_get(s, "name"));
            color = parse_color_string(value_as_string(obj_get(s, "color")));
            values = obj_get_any(s, values_keys);
        }
        struct yetty_ycore_int_result sr = yetty_ychart_add_series(chart, name, color);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "json: add series");
        size_t series_index = (size_t)sr.value;
        if (values && values->type == JSON_ARRAY) {
            for (size_t j = 0; j < values->count; j++) {
                const struct json_value *v = values->items[j];
                if (v->type == JSON_OBJECT) {
                    double x = (double)j, y = 0.0;
                    value_as_number(obj_get(v, "x"), &x);
                    value_as_number(obj_get_any(v, (const char *const[]){"y", "value", NULL}), &y);
                    struct yetty_ycore_void_result pv =
                        yetty_ychart_series_push_xy(chart, series_index, x, y);
                    YETTY_RETURN_IF_ERR(yetty_ycore_void, pv, "json: series xy");
                } else {
                    double y = 0.0;
                    value_as_number(v, &y);
                    struct yetty_ycore_void_result pv =
                        yetty_ychart_series_push(chart, series_index, y);
                    YETTY_RETURN_IF_ERR(yetty_ycore_void, pv, "json: series value");
                }
            }
        }
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result map_links(struct yetty_ychart_chart *chart,
                                                const struct json_value *links)
{
    if (links->type != JSON_ARRAY) {
        return YETTY_OK_VOID();
    }
    for (size_t i = 0; i < links->count; i++) {
        const struct json_value *link = links->items[i];
        if (link->type != JSON_OBJECT) {
            continue;
        }
        const char *source = value_as_string(obj_get(link, "source"));
        const char *target = value_as_string(obj_get(link, "target"));
        double value = 1.0;
        value_as_number(obj_get_any(link, (const char *const[]){"value", "weight", NULL}), &value);
        if (source && target) {
            struct yetty_ycore_void_result fr = yetty_ychart_add_flow(chart, source, target, value);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, fr, "json: add flow");
        }
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ychart_parse_json(const char *input, size_t len,
                                                       struct yetty_ychart_chart *chart)
{
    if (!input || !chart) {
        return YETTY_ERR(yetty_ycore_void, "parse_json: NULL input or chart");
    }
    struct json_cursor cur = {.p = input, .end = input + len, .depth = 0, .error = false};
    struct json_value *root = json_parse_value(&cur);
    if (cur.error || !root) {
        json_free(root);
        return YETTY_ERR(yetty_ycore_void, "parse_json: malformed JSON");
    }

    struct yetty_ycore_void_result result = YETTY_OK_VOID();

    if (root->type == JSON_OBJECT) {
        const struct json_value *kind =
            obj_get_any(root, (const char *const[]){"chart", "type", NULL});
        const char *kind_str = value_as_string(kind);
        if (kind_str) {
            chart->kind = yetty_ychart_kind_from_name(kind_str);
        }
        const char *title = value_as_string(obj_get(root, "title"));
        if (title) {
            result = yetty_ychart_set_title(chart, title);
        }
        if (YETTY_IS_OK(result)) {
            const char *xl =
                value_as_string(obj_get_any(root, (const char *const[]){"x", "xlabel", NULL}));
            const char *yl =
                value_as_string(obj_get_any(root, (const char *const[]){"y", "ylabel", NULL}));
            if (xl || yl) {
                result = yetty_ychart_set_axis_labels(chart, xl, yl);
            }
        }
        const struct json_value *legend = obj_get(root, "legend");
        if (legend && legend->type == JSON_BOOL) {
            chart->show_legend = legend->boolean;
        }
        const struct json_value *stacked = obj_get(root, "stacked");
        if (stacked && stacked->type == JSON_BOOL) {
            chart->stacked = stacked->boolean;
        }
        const struct json_value *show_values = obj_get(root, "values");
        if (show_values && show_values->type == JSON_BOOL) {
            chart->show_values = show_values->boolean;
        }

        const struct json_value *links =
            obj_get_any(root, (const char *const[]){"links", "flows", NULL});
        const struct json_value *categories = obj_get(root, "categories");
        const struct json_value *series =
            obj_get_any(root, (const char *const[]){"series", "datasets", NULL});
        const struct json_value *data = obj_get(root, "data");

        if (YETTY_IS_OK(result) && links) {
            if (chart->kind == YETTY_YCHART_KIND_AUTO) {
                chart->kind = YETTY_YCHART_KIND_SANKEY;
            }
            result = map_links(chart, links);
        }
        if (YETTY_IS_OK(result) && categories) {
            result = map_categories(chart, categories);
        }
        if (YETTY_IS_OK(result) && series) {
            result = map_series(chart, series);
        } else if (YETTY_IS_OK(result) && data) {
            result = map_data(chart, data);
        }
    } else if (root->type == JSON_ARRAY) {
        result = map_data(chart, root);
    } else {
        result = YETTY_ERR(yetty_ycore_void, "parse_json: root is not an object or array");
    }

    json_free(root);
    if (YETTY_IS_ERR(result)) {
        return result;
    }

    /* If series were given without categories, auto-generate index labels to
     * the longest series. */
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
            YETTY_RETURN_IF_ERR(yetty_ycore_void, cr, "parse_json: auto category");
        }
    }
    return YETTY_OK_VOID();
}
