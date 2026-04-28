/*
 * yrich-yaml.c — libyaml-driven loader for ydoc / yspreadsheet / yslides.
 *
 * Mirrors yetty-poc/src/yetty/yrich/yrich-persist.cpp's reading paths but
 * targets the C document types directly. The parser is event-driven; each
 * doc type has its own state machine since the schemas differ.
 */

#include <yetty/yrich/yrich-yaml.h>

#include <yetty/yrich/ydoc.h>
#include <yetty/yrich/yrich-document.h>
#include <yetty/yrich/yslides.h>
#include <yetty/yrich/yspreadsheet.h>

#include <yaml.h>

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*=============================================================================
 * Common helpers
 *===========================================================================*/

/* "#AARRGGBB" or "#RRGGBB" → ABGR uint32 (alpha defaulting to FF). */
static uint32_t parse_color_argb(const char *s)
{
    if (!s || s[0] != '#') {
        return 0;
    }
    const char *hex = s + 1;
    size_t n = strlen(hex);
    uint32_t v = 0;
    for (size_t i = 0; i < n; i++) {
        char c = hex[i];
        uint32_t d = 0;
        if (c >= '0' && c <= '9') {
            d = (uint32_t)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            d = (uint32_t)(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            d = (uint32_t)(c - 'A' + 10);
        } else {
            return 0;
        }
        v = (v << 4) | d;
    }
    uint8_t a, r, g, b;
    if (n == 8) {
        a = (uint8_t)((v >> 24) & 0xFF);
        r = (uint8_t)((v >> 16) & 0xFF);
        g = (uint8_t)((v >> 8) & 0xFF);
        b = (uint8_t)((v >> 0) & 0xFF);
    } else if (n == 6) {
        a = 0xFF;
        r = (uint8_t)((v >> 16) & 0xFF);
        g = (uint8_t)((v >> 8) & 0xFF);
        b = (uint8_t)((v >> 0) & 0xFF);
    } else {
        return 0;
    }
    return YETTY_YRICH_RGBA(r, g, b, a);
}

/* "A1" / "AA10" → row, col (0-based). Returns false on malformed input. */
static bool parse_cell_ref(const char *s, int32_t *row, int32_t *col)
{
    if (!s || !*s) {
        return false;
    }
    int32_t c = 0;
    const char *p = s;
    while (*p && isalpha((unsigned char)*p)) {
        char up = (char)toupper((unsigned char)*p);
        c = c * 26 + (up - 'A' + 1);
        p++;
    }
    if (c == 0 || !*p) {
        return false;
    }
    int32_t r = 0;
    while (*p && isdigit((unsigned char)*p)) {
        r = r * 10 + (*p - '0');
        p++;
    }
    if (r == 0) {
        return false;
    }
    *col = c - 1;
    *row = r - 1;
    return true;
}

static int read_file_all(const char *path, char **out, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return -1;
    }
    if (fseek(f, 0, SEEK_END) < 0) {
        fclose(f);
        return -1;
    }
    long sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return -1;
    }
    rewind(f);
    char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return -1;
    }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) {
        free(buf);
        return -1;
    }
    buf[sz] = '\0';
    *out = buf;
    *out_len = (size_t)sz;
    return 0;
}

/* Skip a YAML node. start_depth=0 means we haven't consumed anything yet
 * (we'll read one scalar OR a balanced collection). start_depth=1 means
 * the caller has already consumed a MAPPING_START / SEQUENCE_START and we
 * need to read events up to the matching END. */
static int skip_node_at(yaml_parser_t *p, int start_depth)
{
    yaml_event_t ev;
    int depth = start_depth;
    for (;;) {
        if (!yaml_parser_parse(p, &ev)) {
            return -1;
        }
        switch (ev.type) {
        case YAML_MAPPING_START_EVENT:
        case YAML_SEQUENCE_START_EVENT:
            depth++;
            break;
        case YAML_MAPPING_END_EVENT:
        case YAML_SEQUENCE_END_EVENT:
            yaml_event_delete(&ev);
            if (--depth <= 0) {
                return 0;
            }
            continue;
        case YAML_SCALAR_EVENT:
            if (depth == 0) {
                yaml_event_delete(&ev);
                return 0;
            }
            break;
        case YAML_STREAM_END_EVENT:
        case YAML_NO_EVENT:
            yaml_event_delete(&ev);
            return -1;
        default:
            break;
        }
        yaml_event_delete(&ev);
    }
}

static int skip_node(yaml_parser_t *p)
{
    return skip_node_at(p, 0);
}

/* Caller consumed the opening MAPPING_START / SEQUENCE_START, we read up to
 * the matching END. */
static int skip_collection_body(yaml_parser_t *p)
{
    return skip_node_at(p, 1);
}

/* Read the next event. Caller must yaml_event_delete(*ev). */
static int next_event(yaml_parser_t *p, yaml_event_t *ev)
{
    return yaml_parser_parse(p, ev) ? 0 : -1;
}

static char *scalar_dup(const yaml_event_t *ev)
{
    const char *s = (const char *)ev->data.scalar.value;
    size_t n = ev->data.scalar.length;
    char *out = malloc(n + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, s, n);
    out[n] = '\0';
    return out;
}

static double scalar_to_d(const yaml_event_t *ev)
{
    char tmp[64];
    size_t n = ev->data.scalar.length;
    if (n >= sizeof(tmp)) {
        n = sizeof(tmp) - 1;
    }
    memcpy(tmp, ev->data.scalar.value, n);
    tmp[n] = '\0';
    return strtod(tmp, NULL);
}

static long scalar_to_l(const yaml_event_t *ev)
{
    char tmp[64];
    size_t n = ev->data.scalar.length;
    if (n >= sizeof(tmp)) {
        n = sizeof(tmp) - 1;
    }
    memcpy(tmp, ev->data.scalar.value, n);
    tmp[n] = '\0';
    return strtol(tmp, NULL, 10);
}

static bool scalar_eq(const yaml_event_t *ev, const char *s)
{
    size_t n = strlen(s);
    return ev->data.scalar.length == n && memcmp(ev->data.scalar.value, s, n) == 0;
}

/*=============================================================================
 * ydoc loader
 *===========================================================================*/

/* Parse one paragraph mapping. Currently supports keys: text, fontSize,
 * color, format. Other keys (runs, etc.) are skipped. */
static int parse_ydoc_paragraph(yaml_parser_t *p, struct yetty_yrich_ydoc *d)
{
    char *text = NULL;
    size_t text_len = 0;
    float font_size = 0.0f;
    uint32_t color = 0;
    uint32_t format = 0;

    yaml_event_t ev;
    for (;;) {
        if (next_event(p, &ev) < 0) {
            goto err;
        }
        if (ev.type == YAML_MAPPING_END_EVENT) {
            yaml_event_delete(&ev);
            break;
        }
        if (ev.type != YAML_SCALAR_EVENT) {
            goto err_ev;
        }
        bool key_text = scalar_eq(&ev, "text");
        bool key_font = scalar_eq(&ev, "fontSize");
        bool key_col = scalar_eq(&ev, "color");
        bool key_fmt = scalar_eq(&ev, "format");
        yaml_event_delete(&ev);

        if (next_event(p, &ev) < 0) {
            goto err;
        }
        if (key_text && ev.type == YAML_SCALAR_EVENT) {
            free(text);
            text_len = ev.data.scalar.length;
            text = malloc(text_len + 1);
            if (text) {
                memcpy(text, ev.data.scalar.value, text_len);
                text[text_len] = '\0';
            }
        } else if (key_font && ev.type == YAML_SCALAR_EVENT) {
            font_size = (float)scalar_to_d(&ev);
        } else if (key_col && ev.type == YAML_SCALAR_EVENT) {
            char *c = scalar_dup(&ev);
            if (c) {
                color = parse_color_argb(c);
                free(c);
            }
        } else if (key_fmt && ev.type == YAML_SCALAR_EVENT) {
            format = (uint32_t)scalar_to_l(&ev);
        } else if (ev.type == YAML_MAPPING_START_EVENT || ev.type == YAML_SEQUENCE_START_EVENT) {
            yaml_event_delete(&ev);
            if (skip_collection_body(p) < 0) {
                goto err;
            }
            continue;
        }
        yaml_event_delete(&ev);
    }

    struct yetty_yrich_paragraph_ptr_result pr =
        yetty_yrich_ydoc_add_paragraph(d, text ? text : "", text_len);
    free(text);
    if (YETTY_IS_ERR(pr)) {
        return -1;
    }
    struct yetty_yrich_paragraph *para = pr.value;
    if (font_size > 0.0f) {
        para->style.font_size = font_size;
        para->line_height = font_size * 1.4f;
        size_t lines = 1;
        for (size_t i = 0; i < para->text_len; i++) {
            if (para->text[i] == '\n') {
                lines++;
            }
        }
        para->bounds.h = (float)lines * para->line_height;
    }
    if (color) {
        para->style.color = color;
    }
    if (format) {
        para->style.format = format;
    }
    return 0;

err_ev:
    yaml_event_delete(&ev);
err:
    free(text);
    return -1;
}

static int parse_ydoc_paragraphs(yaml_parser_t *p, struct yetty_yrich_ydoc *d)
{
    yaml_event_t ev;
    if (next_event(p, &ev) < 0 || ev.type != YAML_SEQUENCE_START_EVENT) {
        yaml_event_delete(&ev);
        return -1;
    }
    yaml_event_delete(&ev);

    for (;;) {
        if (next_event(p, &ev) < 0) {
            return -1;
        }
        if (ev.type == YAML_SEQUENCE_END_EVENT) {
            yaml_event_delete(&ev);
            return 0;
        }
        if (ev.type != YAML_MAPPING_START_EVENT) {
            yaml_event_delete(&ev);
            return -1;
        }
        yaml_event_delete(&ev);
        if (parse_ydoc_paragraph(p, d) < 0) {
            return -1;
        }
    }
}

static int parse_ydoc_document(yaml_parser_t *p, struct yetty_yrich_ydoc *d)
{
    yaml_event_t ev;
    if (next_event(p, &ev) < 0 || ev.type != YAML_MAPPING_START_EVENT) {
        yaml_event_delete(&ev);
        return -1;
    }
    yaml_event_delete(&ev);

    for (;;) {
        if (next_event(p, &ev) < 0) {
            return -1;
        }
        if (ev.type == YAML_MAPPING_END_EVENT) {
            yaml_event_delete(&ev);
            return 0;
        }
        if (ev.type != YAML_SCALAR_EVENT) {
            yaml_event_delete(&ev);
            return -1;
        }
        bool key_pw = scalar_eq(&ev, "pageWidth");
        bool key_mg = scalar_eq(&ev, "margin");
        bool key_pp = scalar_eq(&ev, "paragraphs");
        yaml_event_delete(&ev);

        if (key_pp) {
            if (parse_ydoc_paragraphs(p, d) < 0) {
                return -1;
            }
            continue;
        }
        if (next_event(p, &ev) < 0) {
            return -1;
        }
        if (key_pw && ev.type == YAML_SCALAR_EVENT) {
            yetty_yrich_ydoc_set_page_width(d, (float)scalar_to_d(&ev));
        } else if (key_mg && ev.type == YAML_SCALAR_EVENT) {
            yetty_yrich_ydoc_set_margin(d, (float)scalar_to_d(&ev));
        } else if (ev.type == YAML_MAPPING_START_EVENT || ev.type == YAML_SEQUENCE_START_EVENT) {
            yaml_event_delete(&ev);
            if (skip_collection_body(p) < 0) {
                return -1;
            }
            continue;
        }
        yaml_event_delete(&ev);
    }
}

struct yetty_yrich_ydoc_ptr_result yetty_yrich_ydoc_load_yaml(const char *yaml, size_t len)
{
    if (!yaml) {
        return YETTY_ERR(yetty_yrich_ydoc_ptr, "yrich ydoc load: NULL yaml");
    }

    struct yetty_yrich_ydoc_ptr_result dr = yetty_yrich_ydoc_create();
    if (YETTY_IS_ERR(dr)) {
        return dr;
    }
    struct yetty_yrich_ydoc *d = dr.value;

    yaml_parser_t parser;
    yaml_parser_initialize(&parser);
    yaml_parser_set_input_string(&parser, (const unsigned char *)yaml, len);

    yaml_event_t ev;
    bool found_doc = false;
    int rc = 0;

    for (;;) {
        if (next_event(&parser, &ev) < 0) {
            rc = -1;
            break;
        }
        if (ev.type == YAML_STREAM_END_EVENT) {
            yaml_event_delete(&ev);
            break;
        }
        if (ev.type == YAML_MAPPING_START_EVENT) {
            yaml_event_delete(&ev);
            for (;;) {
                if (next_event(&parser, &ev) < 0) {
                    rc = -1;
                    goto done;
                }
                if (ev.type == YAML_MAPPING_END_EVENT) {
                    yaml_event_delete(&ev);
                    break;
                }
                if (ev.type != YAML_SCALAR_EVENT) {
                    yaml_event_delete(&ev);
                    rc = -1;
                    goto done;
                }
                bool key = scalar_eq(&ev, "document");
                yaml_event_delete(&ev);
                if (key) {
                    found_doc = true;
                    if (parse_ydoc_document(&parser, d) < 0) {
                        rc = -1;
                        goto done;
                    }
                } else {
                    if (skip_node(&parser) < 0) {
                        rc = -1;
                        goto done;
                    }
                }
            }
            continue;
        }
        yaml_event_delete(&ev);
    }

done:
    yaml_parser_delete(&parser);

    if (rc < 0 || !found_doc) {
        yetty_yrich_document_destroy(&d->base);
        return YETTY_ERR(yetty_yrich_ydoc_ptr, "yrich ydoc: yaml parse failed");
    }
    return YETTY_OK(yetty_yrich_ydoc_ptr, d);
}

struct yetty_yrich_ydoc_ptr_result yetty_yrich_ydoc_load_yaml_file(const char *path)
{
    char *buf = NULL;
    size_t len = 0;
    if (read_file_all(path, &buf, &len) < 0) {
        return YETTY_ERR(yetty_yrich_ydoc_ptr, "yrich ydoc: cannot read file");
    }
    struct yetty_yrich_ydoc_ptr_result r = yetty_yrich_ydoc_load_yaml(buf, len);
    free(buf);
    return r;
}

/*=============================================================================
 * yspreadsheet loader
 *===========================================================================*/

static int parse_sheet_cells(yaml_parser_t *p, struct yetty_yrich_spreadsheet *s)
{
    yaml_event_t ev;
    if (next_event(p, &ev) < 0 || ev.type != YAML_MAPPING_START_EVENT) {
        yaml_event_delete(&ev);
        return -1;
    }
    yaml_event_delete(&ev);

    for (;;) {
        if (next_event(p, &ev) < 0) {
            return -1;
        }
        if (ev.type == YAML_MAPPING_END_EVENT) {
            yaml_event_delete(&ev);
            return 0;
        }
        if (ev.type != YAML_SCALAR_EVENT) {
            yaml_event_delete(&ev);
            return -1;
        }
        char *key = scalar_dup(&ev);
        yaml_event_delete(&ev);

        if (next_event(p, &ev) < 0) {
            free(key);
            return -1;
        }
        if (ev.type == YAML_SCALAR_EVENT && key) {
            int32_t row, col;
            if (parse_cell_ref(key, &row, &col)) {
                struct yetty_yrich_cell_addr addr = {row, col};
                yetty_yrich_spreadsheet_set_cell_value(s, addr, (const char *)ev.data.scalar.value,
                                                       ev.data.scalar.length);
            }
        } else if (ev.type == YAML_MAPPING_START_EVENT || ev.type == YAML_SEQUENCE_START_EVENT) {
            yaml_event_delete(&ev);
            free(key);
            if (skip_collection_body(p) < 0) {
                return -1;
            }
            continue;
        }
        yaml_event_delete(&ev);
        free(key);
    }
}

static int parse_sheet_col_widths(yaml_parser_t *p, struct yetty_yrich_spreadsheet *s)
{
    yaml_event_t ev;
    if (next_event(p, &ev) < 0 || ev.type != YAML_SEQUENCE_START_EVENT) {
        yaml_event_delete(&ev);
        return -1;
    }
    yaml_event_delete(&ev);

    int32_t col = 0;
    for (;;) {
        if (next_event(p, &ev) < 0) {
            return -1;
        }
        if (ev.type == YAML_SEQUENCE_END_EVENT) {
            yaml_event_delete(&ev);
            return 0;
        }
        if (ev.type == YAML_SCALAR_EVENT) {
            float w = (float)scalar_to_d(&ev);
            yetty_yrich_spreadsheet_set_col_width(s, col++, w);
        }
        yaml_event_delete(&ev);
    }
}

static int parse_sheet_body(yaml_parser_t *p, struct yetty_yrich_spreadsheet *s)
{
    yaml_event_t ev;
    if (next_event(p, &ev) < 0 || ev.type != YAML_MAPPING_START_EVENT) {
        yaml_event_delete(&ev);
        return -1;
    }
    yaml_event_delete(&ev);

    int32_t rows = 100, cols = 26;
    for (;;) {
        if (next_event(p, &ev) < 0) {
            return -1;
        }
        if (ev.type == YAML_MAPPING_END_EVENT) {
            yaml_event_delete(&ev);
            yetty_yrich_spreadsheet_set_grid_size(s, rows, cols);
            return 0;
        }
        if (ev.type != YAML_SCALAR_EVENT) {
            yaml_event_delete(&ev);
            return -1;
        }
        bool key_rows = scalar_eq(&ev, "rows");
        bool key_cols = scalar_eq(&ev, "cols");
        bool key_cw = scalar_eq(&ev, "columnWidths");
        bool key_cells = scalar_eq(&ev, "cells");
        yaml_event_delete(&ev);

        if (key_cw) {
            if (parse_sheet_col_widths(p, s) < 0) {
                return -1;
            }
            continue;
        }
        if (key_cells) {
            yetty_yrich_spreadsheet_set_grid_size(s, rows, cols);
            if (parse_sheet_cells(p, s) < 0) {
                return -1;
            }
            continue;
        }
        if (next_event(p, &ev) < 0) {
            return -1;
        }
        if (key_rows && ev.type == YAML_SCALAR_EVENT) {
            rows = (int32_t)scalar_to_l(&ev);
        } else if (key_cols && ev.type == YAML_SCALAR_EVENT) {
            cols = (int32_t)scalar_to_l(&ev);
        } else if (ev.type == YAML_MAPPING_START_EVENT || ev.type == YAML_SEQUENCE_START_EVENT) {
            yaml_event_delete(&ev);
            if (skip_collection_body(p) < 0) {
                return -1;
            }
            continue;
        }
        yaml_event_delete(&ev);
    }
}

struct yetty_yrich_spreadsheet_ptr_result yetty_yrich_spreadsheet_load_yaml(const char *yaml,
                                                                            size_t len)
{
    if (!yaml) {
        return YETTY_ERR(yetty_yrich_spreadsheet_ptr, "yrich sheet load: NULL yaml");
    }

    struct yetty_yrich_spreadsheet_ptr_result sr = yetty_yrich_spreadsheet_create();
    if (YETTY_IS_ERR(sr)) {
        return sr;
    }
    struct yetty_yrich_spreadsheet *s = sr.value;

    yaml_parser_t parser;
    yaml_parser_initialize(&parser);
    yaml_parser_set_input_string(&parser, (const unsigned char *)yaml, len);

    yaml_event_t ev;
    bool found = false;
    int rc = 0;
    for (;;) {
        if (next_event(&parser, &ev) < 0) {
            rc = -1;
            break;
        }
        if (ev.type == YAML_STREAM_END_EVENT) {
            yaml_event_delete(&ev);
            break;
        }
        if (ev.type == YAML_MAPPING_START_EVENT) {
            yaml_event_delete(&ev);
            for (;;) {
                if (next_event(&parser, &ev) < 0) {
                    rc = -1;
                    goto done;
                }
                if (ev.type == YAML_MAPPING_END_EVENT) {
                    yaml_event_delete(&ev);
                    break;
                }
                if (ev.type != YAML_SCALAR_EVENT) {
                    yaml_event_delete(&ev);
                    rc = -1;
                    goto done;
                }
                bool key = scalar_eq(&ev, "spreadsheet");
                yaml_event_delete(&ev);
                if (key) {
                    found = true;
                    if (parse_sheet_body(&parser, s) < 0) {
                        rc = -1;
                        goto done;
                    }
                } else {
                    if (skip_node(&parser) < 0) {
                        rc = -1;
                        goto done;
                    }
                }
            }
            continue;
        }
        yaml_event_delete(&ev);
    }

done:
    yaml_parser_delete(&parser);
    if (rc < 0 || !found) {
        yetty_yrich_document_destroy(&s->base);
        return YETTY_ERR(yetty_yrich_spreadsheet_ptr, "yrich sheet: yaml parse failed");
    }
    return YETTY_OK(yetty_yrich_spreadsheet_ptr, s);
}

struct yetty_yrich_spreadsheet_ptr_result yetty_yrich_spreadsheet_load_yaml_file(const char *path)
{
    char *buf = NULL;
    size_t len = 0;
    if (read_file_all(path, &buf, &len) < 0) {
        return YETTY_ERR(yetty_yrich_spreadsheet_ptr, "yrich sheet: cannot read file");
    }
    struct yetty_yrich_spreadsheet_ptr_result r = yetty_yrich_spreadsheet_load_yaml(buf, len);
    free(buf);
    return r;
}

/*=============================================================================
 * yslides loader
 *===========================================================================*/

struct shape_fields {
    uint32_t type; /* 0..5 */
    float x, y, w, h;
    float rotation;
    float corner_radius;
    uint32_t fill;
    uint32_t stroke;
    float stroke_width;
    bool has_fill;
    bool has_stroke;
    bool has_stroke_w;
    char *text;
    size_t text_len;
    float font_size;
    uint32_t text_color;
    bool has_text_color;
    uint32_t text_align;
    uint32_t text_valign;
    bool has_text_align;
    bool has_text_valign;
    char *image_source;
};

static void shape_fields_init(struct shape_fields *f)
{
    memset(f, 0, sizeof(*f));
    f->w = 100.0f;
    f->h = 100.0f;
    f->stroke_width = 1.0f;
    f->font_size = 24.0f;
}

static void shape_fields_free(struct shape_fields *f)
{
    free(f->text);
    free(f->image_source);
}

static int parse_shape(yaml_parser_t *p, struct yetty_yrich_slides *s)
{
    struct shape_fields f;
    shape_fields_init(&f);

    yaml_event_t ev;
    for (;;) {
        if (next_event(p, &ev) < 0) {
            goto err;
        }
        if (ev.type == YAML_MAPPING_END_EVENT) {
            yaml_event_delete(&ev);
            break;
        }
        if (ev.type != YAML_SCALAR_EVENT) {
            yaml_event_delete(&ev);
            goto err;
        }
        const char *key = (const char *)ev.data.scalar.value;
        size_t klen = ev.data.scalar.length;
        char keybuf[32];
        size_t cp = klen < sizeof(keybuf) - 1 ? klen : sizeof(keybuf) - 1;
        memcpy(keybuf, key, cp);
        keybuf[cp] = '\0';
        yaml_event_delete(&ev);

        if (next_event(p, &ev) < 0) {
            goto err;
        }
        if (ev.type == YAML_SCALAR_EVENT) {
            if (!strcmp(keybuf, "type")) {
                f.type = (uint32_t)scalar_to_l(&ev);
            } else if (!strcmp(keybuf, "x")) {
                f.x = (float)scalar_to_d(&ev);
            } else if (!strcmp(keybuf, "y")) {
                f.y = (float)scalar_to_d(&ev);
            } else if (!strcmp(keybuf, "width")) {
                f.w = (float)scalar_to_d(&ev);
            } else if (!strcmp(keybuf, "height")) {
                f.h = (float)scalar_to_d(&ev);
            } else if (!strcmp(keybuf, "rotation")) {
                f.rotation = (float)scalar_to_d(&ev);
            } else if (!strcmp(keybuf, "cornerRadius")) {
                f.corner_radius = (float)scalar_to_d(&ev);
            } else if (!strcmp(keybuf, "fillColor")) {
                char *c = scalar_dup(&ev);
                if (c) {
                    f.fill = parse_color_argb(c);
                    f.has_fill = true;
                    free(c);
                }
            } else if (!strcmp(keybuf, "strokeColor")) {
                char *c = scalar_dup(&ev);
                if (c) {
                    f.stroke = parse_color_argb(c);
                    f.has_stroke = true;
                    free(c);
                }
            } else if (!strcmp(keybuf, "strokeWidth")) {
                f.stroke_width = (float)scalar_to_d(&ev);
                f.has_stroke_w = true;
            } else if (!strcmp(keybuf, "text")) {
                free(f.text);
                f.text_len = ev.data.scalar.length;
                f.text = malloc(f.text_len + 1);
                if (f.text) {
                    memcpy(f.text, ev.data.scalar.value, f.text_len);
                    f.text[f.text_len] = '\0';
                }
            } else if (!strcmp(keybuf, "fontSize")) {
                f.font_size = (float)scalar_to_d(&ev);
            } else if (!strcmp(keybuf, "textColor")) {
                char *c = scalar_dup(&ev);
                if (c) {
                    f.text_color = parse_color_argb(c);
                    f.has_text_color = true;
                    free(c);
                }
            } else if (!strcmp(keybuf, "textAlign")) {
                f.text_align = (uint32_t)scalar_to_l(&ev);
                f.has_text_align = true;
            } else if (!strcmp(keybuf, "textVAlign")) {
                f.text_valign = (uint32_t)scalar_to_l(&ev);
                f.has_text_valign = true;
            } else if (!strcmp(keybuf, "imageSource")) {
                free(f.image_source);
                f.image_source = scalar_dup(&ev);
            }
        } else if (ev.type == YAML_MAPPING_START_EVENT || ev.type == YAML_SEQUENCE_START_EVENT) {
            yaml_event_delete(&ev);
            if (skip_collection_body(p) < 0) {
                goto err;
            }
            continue;
        }
        yaml_event_delete(&ev);
    }

    struct yetty_yrich_shape_ptr_result r;
    switch (f.type) {
    case 0:
        r = yetty_yrich_slides_add_rectangle(s, f.x, f.y, f.w, f.h);
        break;
    case 1:
        r = yetty_yrich_slides_add_ellipse(s, f.x, f.y, f.w, f.h);
        break;
    case 2:
        r = yetty_yrich_slides_add_textbox(s, f.x, f.y, f.w, f.h, f.text, f.text_len);
        break;
    case 3:
        r = yetty_yrich_slides_add_line(s, f.x, f.y, f.x + f.w, f.y + f.h);
        break;
    case 5:
        r = yetty_yrich_slides_add_image(s, f.x, f.y, f.w, f.h);
        break;
    default:
        r = yetty_yrich_slides_add_rectangle(s, f.x, f.y, f.w, f.h);
        break;
    }
    if (YETTY_IS_ERR(r)) {
        goto err;
    }
    struct yetty_yrich_shape *sh = r.value;
    if (f.has_fill) {
        sh->fill_color = f.fill;
    }
    if (f.has_stroke) {
        sh->stroke_color = f.stroke;
    }
    if (f.has_stroke_w) {
        sh->stroke_width = f.stroke_width;
    }
    sh->rotation = f.rotation;
    sh->corner_radius = f.corner_radius;
    if (f.font_size > 0.0f) {
        sh->text_style.font_size = f.font_size;
    }
    if (f.has_text_color) {
        sh->text_style.color = f.text_color;
    }
    if (f.has_text_align) {
        sh->text_align = f.text_align;
    }
    if (f.has_text_valign) {
        sh->text_valign = f.text_valign;
    }
    if (f.image_source) {
        sh->image_source = f.image_source;
        f.image_source = NULL; /* handed off */
    }
    shape_fields_free(&f);
    return 0;
err:
    shape_fields_free(&f);
    return -1;
}

static int parse_slide(yaml_parser_t *p, struct yetty_yrich_slides *s)
{
    yaml_event_t ev;
    int32_t index = -1;
    uint32_t bg = YETTY_YRICH_COLOR_WHITE;
    bool have_bg = false;

    /* Pass 1: collect index/bgColor; defer shapes until after the slide
	 * has been added so that the runner can target the right slide. */
    struct shape_buffer {
        yaml_event_t **events;
        size_t count, capacity;
    } sbuf = {0};

    for (;;) {
        if (next_event(p, &ev) < 0) {
            return -1;
        }
        if (ev.type == YAML_MAPPING_END_EVENT) {
            yaml_event_delete(&ev);
            break;
        }
        if (ev.type != YAML_SCALAR_EVENT) {
            yaml_event_delete(&ev);
            return -1;
        }
        bool key_index = scalar_eq(&ev, "index");
        bool key_bg = scalar_eq(&ev, "bgColor");
        bool key_shapes = scalar_eq(&ev, "shapes");
        yaml_event_delete(&ev);

        if (key_shapes) {
            /* We need the slide to exist before adding shapes —
			 * make sure we've seen index by now (POC always lists
			 * it first). Add the slide if needed. */
            while (yetty_yrich_slides_slide_at(s, index) == NULL) {
                yetty_yrich_slides_add_slide(s);
            }
            yetty_yrich_slides_set_current(s, index);
            struct yetty_yrich_slide *cur = yetty_yrich_slides_slide_at(s, index);
            if (cur && have_bg) {
                cur->bg_color = bg;
            }

            if (next_event(p, &ev) < 0) {
                return -1;
            }
            if (ev.type != YAML_SEQUENCE_START_EVENT) {
                yaml_event_delete(&ev);
                return -1;
            }
            yaml_event_delete(&ev);
            for (;;) {
                if (next_event(p, &ev) < 0) {
                    return -1;
                }
                if (ev.type == YAML_SEQUENCE_END_EVENT) {
                    yaml_event_delete(&ev);
                    break;
                }
                if (ev.type != YAML_MAPPING_START_EVENT) {
                    yaml_event_delete(&ev);
                    return -1;
                }
                yaml_event_delete(&ev);
                if (parse_shape(p, s) < 0) {
                    return -1;
                }
            }
            continue;
        }

        if (next_event(p, &ev) < 0) {
            return -1;
        }
        if (key_index && ev.type == YAML_SCALAR_EVENT) {
            index = (int32_t)scalar_to_l(&ev);
        } else if (key_bg && ev.type == YAML_SCALAR_EVENT) {
            char *c = scalar_dup(&ev);
            if (c) {
                bg = parse_color_argb(c);
                have_bg = true;
                free(c);
            }
        } else if (ev.type == YAML_MAPPING_START_EVENT || ev.type == YAML_SEQUENCE_START_EVENT) {
            yaml_event_delete(&ev);
            if (skip_collection_body(p) < 0) {
                return -1;
            }
            continue;
        }
        yaml_event_delete(&ev);
    }
    (void)sbuf;
    return 0;
}

static int parse_slides_seq(yaml_parser_t *p, struct yetty_yrich_slides *s)
{
    yaml_event_t ev;
    if (next_event(p, &ev) < 0 || ev.type != YAML_SEQUENCE_START_EVENT) {
        yaml_event_delete(&ev);
        return -1;
    }
    yaml_event_delete(&ev);
    for (;;) {
        if (next_event(p, &ev) < 0) {
            return -1;
        }
        if (ev.type == YAML_SEQUENCE_END_EVENT) {
            yaml_event_delete(&ev);
            return 0;
        }
        if (ev.type != YAML_MAPPING_START_EVENT) {
            yaml_event_delete(&ev);
            return -1;
        }
        yaml_event_delete(&ev);
        if (parse_slide(p, s) < 0) {
            return -1;
        }
    }
}

static int parse_presentation(yaml_parser_t *p, struct yetty_yrich_slides *s)
{
    yaml_event_t ev;
    if (next_event(p, &ev) < 0 || ev.type != YAML_MAPPING_START_EVENT) {
        yaml_event_delete(&ev);
        return -1;
    }
    yaml_event_delete(&ev);

    float w = 960.0f, h = 540.0f;
    for (;;) {
        if (next_event(p, &ev) < 0) {
            return -1;
        }
        if (ev.type == YAML_MAPPING_END_EVENT) {
            yaml_event_delete(&ev);
            yetty_yrich_slides_set_slide_size(s, w, h);
            yetty_yrich_slides_set_current(s, 0);
            return 0;
        }
        if (ev.type != YAML_SCALAR_EVENT) {
            yaml_event_delete(&ev);
            return -1;
        }
        bool key_w = scalar_eq(&ev, "slideWidth");
        bool key_h = scalar_eq(&ev, "slideHeight");
        bool key_s = scalar_eq(&ev, "slides");
        yaml_event_delete(&ev);

        if (key_s) {
            yetty_yrich_slides_set_slide_size(s, w, h);
            if (parse_slides_seq(p, s) < 0) {
                return -1;
            }
            continue;
        }
        if (next_event(p, &ev) < 0) {
            return -1;
        }
        if (key_w && ev.type == YAML_SCALAR_EVENT) {
            w = (float)scalar_to_d(&ev);
        } else if (key_h && ev.type == YAML_SCALAR_EVENT) {
            h = (float)scalar_to_d(&ev);
        } else if (ev.type == YAML_MAPPING_START_EVENT || ev.type == YAML_SEQUENCE_START_EVENT) {
            yaml_event_delete(&ev);
            if (skip_collection_body(p) < 0) {
                return -1;
            }
            continue;
        }
        yaml_event_delete(&ev);
    }
}

struct yetty_yrich_slides_ptr_result yetty_yrich_slides_load_yaml(const char *yaml, size_t len)
{
    if (!yaml) {
        return YETTY_ERR(yetty_yrich_slides_ptr, "yrich slides load: NULL yaml");
    }

    struct yetty_yrich_slides_ptr_result sr = yetty_yrich_slides_create();
    if (YETTY_IS_ERR(sr)) {
        return sr;
    }
    struct yetty_yrich_slides *s = sr.value;

    yaml_parser_t parser;
    yaml_parser_initialize(&parser);
    yaml_parser_set_input_string(&parser, (const unsigned char *)yaml, len);

    yaml_event_t ev;
    bool found = false;
    int rc = 0;
    for (;;) {
        if (next_event(&parser, &ev) < 0) {
            rc = -1;
            break;
        }
        if (ev.type == YAML_STREAM_END_EVENT) {
            yaml_event_delete(&ev);
            break;
        }
        if (ev.type == YAML_MAPPING_START_EVENT) {
            yaml_event_delete(&ev);
            for (;;) {
                if (next_event(&parser, &ev) < 0) {
                    rc = -1;
                    goto done;
                }
                if (ev.type == YAML_MAPPING_END_EVENT) {
                    yaml_event_delete(&ev);
                    break;
                }
                if (ev.type != YAML_SCALAR_EVENT) {
                    yaml_event_delete(&ev);
                    rc = -1;
                    goto done;
                }
                bool key = scalar_eq(&ev, "presentation");
                yaml_event_delete(&ev);
                if (key) {
                    found = true;
                    if (parse_presentation(&parser, s) < 0) {
                        rc = -1;
                        goto done;
                    }
                } else {
                    if (skip_node(&parser) < 0) {
                        rc = -1;
                        goto done;
                    }
                }
            }
            continue;
        }
        yaml_event_delete(&ev);
    }

done:
    yaml_parser_delete(&parser);
    if (rc < 0 || !found) {
        yetty_yrich_document_destroy(&s->base);
        return YETTY_ERR(yetty_yrich_slides_ptr, "yrich slides: yaml parse failed");
    }
    return YETTY_OK(yetty_yrich_slides_ptr, s);
}

struct yetty_yrich_slides_ptr_result yetty_yrich_slides_load_yaml_file(const char *path)
{
    char *buf = NULL;
    size_t len = 0;
    if (read_file_all(path, &buf, &len) < 0) {
        return YETTY_ERR(yetty_yrich_slides_ptr, "yrich slides: cannot read file");
    }
    struct yetty_yrich_slides_ptr_result r = yetty_yrich_slides_load_yaml(buf, len);
    free(buf);
    return r;
}
