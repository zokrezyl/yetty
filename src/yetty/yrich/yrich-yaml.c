/*
 * yrich-yaml.c — libyaml-driven loader for ydoc / yspreadsheet / yslides.
 *
 * Mirrors yetty-poc/src/yetty/yrich/yrich-persist.cpp's reading paths but
 * targets the yclass document objects directly. The parser is event-driven;
 * each doc type has its own state machine since the schemas differ.
 */

#include <yetty/yrich/yrich-yaml.h>

#include <yetty/yrich/yrich-operation.h>
#include <yetty/yrich/yrich-types.h>

#include <yetty/yrich/document.h>
#include <yetty/yrich/element.h>
#include <yetty/yrich/slides.h>
#include <yetty/yrich/spreadsheet.h>
#include <yetty/yrich/ydoc.h>

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

static struct yetty_ycore_void_result read_file_all(const char *path, char **out, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return YETTY_ERR(yetty_ycore_void, "yrich yaml: cannot open file");
    }
    if (fseek(f, 0, SEEK_END) < 0) {
        fclose(f);
        return YETTY_ERR(yetty_ycore_void, "yrich yaml: seek failed");
    }
    long sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return YETTY_ERR(yetty_ycore_void, "yrich yaml: tell failed");
    }
    rewind(f);
    char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return YETTY_ERR(yetty_ycore_void, "yrich yaml: file buffer alloc failed");
    }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) {
        free(buf);
        return YETTY_ERR(yetty_ycore_void, "yrich yaml: short read");
    }
    buf[sz] = '\0';
    *out = buf;
    *out_len = (size_t)sz;
    return YETTY_OK_VOID();
}

/* Skip a YAML node. start_depth=0 means we haven't consumed anything yet
 * (we'll read one scalar OR a balanced collection). start_depth=1 means
 * the caller has already consumed a MAPPING_START / SEQUENCE_START and we
 * need to read events up to the matching END. */
static struct yetty_ycore_void_result skip_node_at(struct yaml_parser_s *p, int start_depth)
{
    yaml_event_t ev;
    int depth = start_depth;
    for (;;) {
        if (!yaml_parser_parse(p, &ev)) {
            return YETTY_ERR(yetty_ycore_void, "yrich yaml: parser error while skipping node");
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
                return YETTY_OK_VOID();
            }
            continue;
        case YAML_SCALAR_EVENT:
            if (depth == 0) {
                yaml_event_delete(&ev);
                return YETTY_OK_VOID();
            }
            break;
        case YAML_STREAM_END_EVENT:
        case YAML_NO_EVENT:
            yaml_event_delete(&ev);
            return YETTY_ERR(yetty_ycore_void, "yrich yaml: unexpected end while skipping node");
        default:
            break;
        }
        yaml_event_delete(&ev);
    }
}

static struct yetty_ycore_void_result skip_node(struct yaml_parser_s *p)
{
    return skip_node_at(p, 0);
}

/* Caller consumed the opening MAPPING_START / SEQUENCE_START, we read up to
 * the matching END. */
static struct yetty_ycore_void_result skip_collection_body(struct yaml_parser_s *p)
{
    return skip_node_at(p, 1);
}

/* Read the next event. Caller must yaml_event_delete(*ev) on success. */
static struct yetty_ycore_void_result next_event(struct yaml_parser_s *p, yaml_event_t *ev)
{
    if (!yaml_parser_parse(p, ev)) {
        return YETTY_ERR(yetty_ycore_void, "yrich yaml: parser error");
    }
    return YETTY_OK_VOID();
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

struct parsed_run {
    int32_t start;
    int32_t end;
    uint32_t format;
    uint32_t color;
};

/* Parse one run mapping {start, end, format, color}. */
static struct yetty_ycore_void_result parse_paragraph_run(struct yaml_parser_s *p,
                                                          struct parsed_run *out_run)
{
    memset(out_run, 0, sizeof(*out_run));
    yaml_event_t ev;
    for (;;) {
        struct yetty_ycore_void_result ev_res = next_event(p, &ev);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, ev_res, "yrich yaml: run read failed");
        if (ev.type == YAML_MAPPING_END_EVENT) {
            yaml_event_delete(&ev);
            return YETTY_OK_VOID();
        }
        if (ev.type != YAML_SCALAR_EVENT) {
            yaml_event_delete(&ev);
            return YETTY_ERR(yetty_ycore_void, "yrich yaml: run expected scalar key");
        }
        bool key_start = scalar_eq(&ev, "start");
        bool key_end = scalar_eq(&ev, "end");
        bool key_format = scalar_eq(&ev, "format");
        bool key_color = scalar_eq(&ev, "color");
        yaml_event_delete(&ev);

        ev_res = next_event(p, &ev);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, ev_res, "yrich yaml: run read failed");
        if (ev.type == YAML_SCALAR_EVENT) {
            if (key_start) {
                out_run->start = (int32_t)scalar_to_l(&ev);
            } else if (key_end) {
                out_run->end = (int32_t)scalar_to_l(&ev);
            } else if (key_format) {
                out_run->format = (uint32_t)scalar_to_l(&ev);
            } else if (key_color) {
                char *raw = scalar_dup(&ev);
                if (raw) {
                    out_run->color = parse_color_argb(raw);
                    free(raw);
                }
            }
        }
        yaml_event_delete(&ev);
    }
}

/* Parse one paragraph mapping: text, fontSize, color, format, align and
 * the styled runs[]. Unknown collections are skipped. */
static struct yetty_ycore_void_result parse_ydoc_paragraph(struct yaml_parser_s *p,
                                                           struct yetty_yclass_object *doc_obj)
{
    char *text = NULL;
    size_t text_len = 0;
    float font_size = 0.0f;
    uint32_t color = 0;
    uint32_t format = 0;
    uint32_t align = 0;
    int have_align = 0;
    struct parsed_run *runs = NULL;
    size_t run_count = 0;
    size_t run_capacity = 0;
    struct yetty_ycore_void_result fail_res;

    yaml_event_t ev;
    for (;;) {
        struct yetty_ycore_void_result ev_res = next_event(p, &ev);
        if (YETTY_IS_ERR(ev_res)) {
            fail_res = YETTY_ERR(yetty_ycore_void, "yrich yaml: paragraph read failed", ev_res);
            goto err;
        }
        if (ev.type == YAML_MAPPING_END_EVENT) {
            yaml_event_delete(&ev);
            break;
        }
        if (ev.type != YAML_SCALAR_EVENT) {
            yaml_event_delete(&ev);
            fail_res = YETTY_ERR(yetty_ycore_void, "yrich yaml: paragraph expected scalar key");
            goto err;
        }
        bool key_text = scalar_eq(&ev, "text");
        bool key_font = scalar_eq(&ev, "fontSize");
        bool key_col = scalar_eq(&ev, "color");
        bool key_fmt = scalar_eq(&ev, "format");
        bool key_align = scalar_eq(&ev, "align");
        bool key_runs = scalar_eq(&ev, "runs");
        yaml_event_delete(&ev);

        if (key_runs) {
            ev_res = next_event(p, &ev);
            if (YETTY_IS_ERR(ev_res)) {
                fail_res = YETTY_ERR(yetty_ycore_void, "yrich yaml: runs read failed", ev_res);
                goto err;
            }
            if (ev.type != YAML_SEQUENCE_START_EVENT) {
                yaml_event_delete(&ev);
                fail_res = YETTY_ERR(yetty_ycore_void, "yrich yaml: runs expected sequence");
                goto err;
            }
            yaml_event_delete(&ev);
            for (;;) {
                ev_res = next_event(p, &ev);
                if (YETTY_IS_ERR(ev_res)) {
                    fail_res = YETTY_ERR(yetty_ycore_void, "yrich yaml: runs read failed", ev_res);
                    goto err;
                }
                if (ev.type == YAML_SEQUENCE_END_EVENT) {
                    yaml_event_delete(&ev);
                    break;
                }
                if (ev.type != YAML_MAPPING_START_EVENT) {
                    yaml_event_delete(&ev);
                    fail_res = YETTY_ERR(yetty_ycore_void, "yrich yaml: run expected mapping");
                    goto err;
                }
                yaml_event_delete(&ev);
                if (run_count == run_capacity) {
                    size_t new_cap = run_capacity ? run_capacity * 2 : 4;
                    struct parsed_run *new_runs = realloc(runs, new_cap * sizeof(*new_runs));
                    if (!new_runs) {
                        fail_res = YETTY_ERR(yetty_ycore_void, "yrich yaml: run array grow failed");
                        goto err;
                    }
                    runs = new_runs;
                    run_capacity = new_cap;
                }
                struct yetty_ycore_void_result run_res = parse_paragraph_run(p, &runs[run_count]);
                if (YETTY_IS_ERR(run_res)) {
                    fail_res = YETTY_ERR(yetty_ycore_void, "yrich yaml: run parse failed", run_res);
                    goto err;
                }
                run_count++;
            }
            continue;
        }

        ev_res = next_event(p, &ev);
        if (YETTY_IS_ERR(ev_res)) {
            fail_res = YETTY_ERR(yetty_ycore_void, "yrich yaml: paragraph read failed", ev_res);
            goto err;
        }
        if (key_text && ev.type == YAML_SCALAR_EVENT) {
            free(text);
            text_len = ev.data.scalar.length;
            text = malloc(text_len + 1);
            if (!text) {
                yaml_event_delete(&ev);
                fail_res = YETTY_ERR(yetty_ycore_void, "yrich yaml: paragraph text alloc failed");
                goto err;
            }
            memcpy(text, ev.data.scalar.value, text_len);
            text[text_len] = '\0';
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
        } else if (key_align && ev.type == YAML_SCALAR_EVENT) {
            align = (uint32_t)scalar_to_l(&ev);
            have_align = 1;
        } else if (ev.type == YAML_MAPPING_START_EVENT || ev.type == YAML_SEQUENCE_START_EVENT) {
            yaml_event_delete(&ev);
            struct yetty_ycore_void_result skip_res = skip_collection_body(p);
            if (YETTY_IS_ERR(skip_res)) {
                fail_res =
                    YETTY_ERR(yetty_ycore_void, "yrich yaml: paragraph skip failed", skip_res);
                goto err;
            }
            continue;
        }
        yaml_event_delete(&ev);
    }

    struct yetty_yclass_object_ptr_result paragraph_res =
        yetty_yrich_ydoc_add_paragraph(doc_obj, text ? text : "", text_len);
    free(text);
    text = NULL;
    if (YETTY_IS_ERR(paragraph_res)) {
        free(runs);
        return YETTY_ERR(yetty_ycore_void, "yrich yaml: add_paragraph failed", paragraph_res);
    }
    struct yetty_yclass_object *paragraph_obj = paragraph_res.value;
    if (font_size > 0.0f) {
        struct yetty_ycore_void_result font_res =
            yetty_yrich_paragraph_set_font_size(paragraph_obj, font_size);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, font_res, "yrich yaml: set_font_size failed");
    }
    if (color) {
        struct yetty_ycore_void_result color_res =
            yetty_yrich_paragraph_set_color(paragraph_obj, color);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, color_res, "yrich yaml: set_color failed");
    }
    if (format) {
        struct yetty_ycore_void_result format_res =
            yetty_yrich_paragraph_set_format(paragraph_obj, format);
        if (YETTY_IS_ERR(format_res)) {
            free(runs);
            return YETTY_ERR(yetty_ycore_void, "yrich yaml: set_format failed", format_res);
        }
    }
    if (have_align) {
        struct yetty_ycore_void_result align_res =
            yetty_yrich_paragraph_set_alignment(paragraph_obj, align);
        if (YETTY_IS_ERR(align_res)) {
            free(runs);
            return YETTY_ERR(yetty_ycore_void, "yrich yaml: set_alignment failed", align_res);
        }
    }
    for (size_t i = 0; i < run_count; i++) {
        struct yetty_ycore_void_result run_res = yetty_yrich_paragraph_add_run(
            paragraph_obj, runs[i].start, runs[i].end, runs[i].format, runs[i].color);
        if (YETTY_IS_ERR(run_res)) {
            free(runs);
            return YETTY_ERR(yetty_ycore_void, "yrich yaml: add_run failed", run_res);
        }
    }
    free(runs);
    return YETTY_OK_VOID();

err:
    free(text);
    free(runs);
    return fail_res;
}

static struct yetty_ycore_void_result parse_ydoc_paragraphs(struct yaml_parser_s *p,
                                                            struct yetty_yclass_object *doc_obj)
{
    yaml_event_t ev;
    struct yetty_ycore_void_result ev_res = next_event(p, &ev);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ev_res, "yrich yaml: paragraphs read failed");
    if (ev.type != YAML_SEQUENCE_START_EVENT) {
        yaml_event_delete(&ev);
        return YETTY_ERR(yetty_ycore_void, "yrich yaml: paragraphs expected sequence");
    }
    yaml_event_delete(&ev);

    for (;;) {
        ev_res = next_event(p, &ev);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, ev_res, "yrich yaml: paragraphs read failed");
        if (ev.type == YAML_SEQUENCE_END_EVENT) {
            yaml_event_delete(&ev);
            return YETTY_OK_VOID();
        }
        if (ev.type != YAML_MAPPING_START_EVENT) {
            yaml_event_delete(&ev);
            return YETTY_ERR(yetty_ycore_void, "yrich yaml: paragraphs expected mapping");
        }
        yaml_event_delete(&ev);
        struct yetty_ycore_void_result paragraph_res = parse_ydoc_paragraph(p, doc_obj);
        if (YETTY_IS_ERR(paragraph_res)) {
            return paragraph_res;
        }
    }
}

static struct yetty_ycore_void_result parse_ydoc_document(struct yaml_parser_s *p,
                                                          struct yetty_yclass_object *doc_obj)
{
    yaml_event_t ev;
    struct yetty_ycore_void_result ev_res = next_event(p, &ev);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ev_res, "yrich yaml: document read failed");
    if (ev.type != YAML_MAPPING_START_EVENT) {
        yaml_event_delete(&ev);
        return YETTY_ERR(yetty_ycore_void, "yrich yaml: document expected mapping");
    }
    yaml_event_delete(&ev);

    for (;;) {
        ev_res = next_event(p, &ev);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, ev_res, "yrich yaml: document read failed");
        if (ev.type == YAML_MAPPING_END_EVENT) {
            yaml_event_delete(&ev);
            return YETTY_OK_VOID();
        }
        if (ev.type != YAML_SCALAR_EVENT) {
            yaml_event_delete(&ev);
            return YETTY_ERR(yetty_ycore_void, "yrich yaml: document expected scalar key");
        }
        bool key_pw = scalar_eq(&ev, "pageWidth");
        bool key_mg = scalar_eq(&ev, "margin");
        bool key_pp = scalar_eq(&ev, "paragraphs");
        yaml_event_delete(&ev);

        if (key_pp) {
            struct yetty_ycore_void_result paragraphs_res = parse_ydoc_paragraphs(p, doc_obj);
            if (YETTY_IS_ERR(paragraphs_res)) {
                return paragraphs_res;
            }
            continue;
        }
        ev_res = next_event(p, &ev);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, ev_res, "yrich yaml: document read failed");
        if (key_pw && ev.type == YAML_SCALAR_EVENT) {
            struct yetty_ycore_void_result width_res =
                yetty_yrich_ydoc_set_page_width(doc_obj, (float)scalar_to_d(&ev));
            if (YETTY_IS_ERR(width_res)) {
                yaml_event_delete(&ev);
                return YETTY_ERR(yetty_ycore_void, "yrich yaml: set_page_width failed", width_res);
            }
        } else if (key_mg && ev.type == YAML_SCALAR_EVENT) {
            struct yetty_ycore_void_result margin_res =
                yetty_yrich_ydoc_set_margin(doc_obj, (float)scalar_to_d(&ev));
            if (YETTY_IS_ERR(margin_res)) {
                yaml_event_delete(&ev);
                return YETTY_ERR(yetty_ycore_void, "yrich yaml: set_margin failed", margin_res);
            }
        } else if (ev.type == YAML_MAPPING_START_EVENT || ev.type == YAML_SEQUENCE_START_EVENT) {
            yaml_event_delete(&ev);
            struct yetty_ycore_void_result skip_res = skip_collection_body(p);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, skip_res, "yrich yaml: document skip failed");
            continue;
        }
        yaml_event_delete(&ev);
    }
}

struct yetty_yclass_object_ptr_result yetty_yrich_ydoc_load_yaml(const char *yaml, size_t len)
{
    if (!yaml) {
        return YETTY_ERR(yetty_yclass_object_ptr, "yrich ydoc load: NULL yaml");
    }

    struct yetty_yclass_object_ptr_result doc_res = yetty_yrich_ydoc_create(NULL);
    if (YETTY_IS_ERR(doc_res)) {
        return doc_res;
    }
    struct yetty_yclass_object *doc_obj = doc_res.value;

    struct yaml_parser_s parser;
    yaml_parser_initialize(&parser);
    yaml_parser_set_input_string(&parser, (const unsigned char *)yaml, len);

    yaml_event_t ev;
    bool found_doc = false;
    struct yetty_ycore_void_result parse_res = YETTY_OK_VOID();

    for (;;) {
        struct yetty_ycore_void_result ev_res = next_event(&parser, &ev);
        if (YETTY_IS_ERR(ev_res)) {
            parse_res = ev_res;
            break;
        }
        if (ev.type == YAML_STREAM_END_EVENT) {
            yaml_event_delete(&ev);
            break;
        }
        if (ev.type == YAML_MAPPING_START_EVENT) {
            yaml_event_delete(&ev);
            for (;;) {
                ev_res = next_event(&parser, &ev);
                if (YETTY_IS_ERR(ev_res)) {
                    parse_res = ev_res;
                    goto done;
                }
                if (ev.type == YAML_MAPPING_END_EVENT) {
                    yaml_event_delete(&ev);
                    break;
                }
                if (ev.type != YAML_SCALAR_EVENT) {
                    yaml_event_delete(&ev);
                    parse_res = YETTY_ERR(yetty_ycore_void, "yrich yaml: expected scalar key");
                    goto done;
                }
                bool key = scalar_eq(&ev, "document");
                yaml_event_delete(&ev);
                if (key) {
                    found_doc = true;
                    struct yetty_ycore_void_result doc_parse_res =
                        parse_ydoc_document(&parser, doc_obj);
                    if (YETTY_IS_ERR(doc_parse_res)) {
                        parse_res = doc_parse_res;
                        goto done;
                    }
                } else {
                    struct yetty_ycore_void_result skip_res = skip_node(&parser);
                    if (YETTY_IS_ERR(skip_res)) {
                        parse_res = skip_res;
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

    if (YETTY_IS_ERR(parse_res) || !found_doc) {
        struct yetty_ycore_void_result destroy_res = yetty_yrich_document_destroy(doc_obj);
        if (YETTY_IS_ERR(destroy_res)) {
            yetty_ycore_error_destroy(destroy_res.error);
        }
        if (YETTY_IS_ERR(parse_res)) {
            return YETTY_ERR(yetty_yclass_object_ptr, "yrich ydoc: yaml parse failed", parse_res);
        }
        return YETTY_ERR(yetty_yclass_object_ptr, "yrich ydoc: no 'document' mapping found");
    }
    return YETTY_OK(yetty_yclass_object_ptr, doc_obj);
}

struct yetty_yclass_object_ptr_result yetty_yrich_ydoc_load_yaml_file(const char *path)
{
    char *buf = NULL;
    size_t len = 0;
    struct yetty_ycore_void_result read_res = read_file_all(path, &buf, &len);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, read_res, "yrich ydoc: cannot read file");
    struct yetty_yclass_object_ptr_result r = yetty_yrich_ydoc_load_yaml(buf, len);
    free(buf);
    return r;
}

/*=============================================================================
 * ydoc writer — File > Save. Emits the same schema the loader reads.
 *===========================================================================*/

/* Emit one event, consuming it. Returns 0 on emitter failure. */
static int emit_event(struct yaml_emitter_s *emitter, yaml_event_t *event)
{
    return yaml_emitter_emit(emitter, event);
}

static int emit_plain_scalar(struct yaml_emitter_s *emitter, const char *value)
{
    yaml_event_t event;
    if (!yaml_scalar_event_initialize(&event, NULL, NULL, (yaml_char_t *)value, (int)strlen(value),
                                      1, 1, YAML_PLAIN_SCALAR_STYLE)) {
        return 0;
    }
    return emit_event(emitter, &event);
}

static int emit_quoted_scalar(struct yaml_emitter_s *emitter, const char *value, size_t len)
{
    yaml_event_t event;
    if (!yaml_scalar_event_initialize(&event, NULL, NULL, (yaml_char_t *)value, (int)len, 0, 1,
                                      YAML_DOUBLE_QUOTED_SCALAR_STYLE)) {
        return 0;
    }
    return emit_event(emitter, &event);
}

static int emit_key_float(struct yaml_emitter_s *emitter, const char *key, float value)
{
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%.6g", (double)value);
    return emit_plain_scalar(emitter, key) && emit_plain_scalar(emitter, buffer);
}

static int emit_key_uint(struct yaml_emitter_s *emitter, const char *key, uint32_t value)
{
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%u", value);
    return emit_plain_scalar(emitter, key) && emit_plain_scalar(emitter, buffer);
}

/* Packed ABGR → "#AARRGGBB" (the format parse_color_argb reads). */
static int emit_key_color(struct yaml_emitter_s *emitter, const char *key, uint32_t abgr)
{
    uint32_t alpha = (abgr >> 24) & 0xFF;
    uint32_t blue = (abgr >> 16) & 0xFF;
    uint32_t green = (abgr >> 8) & 0xFF;
    uint32_t red = abgr & 0xFF;
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "#%02X%02X%02X%02X", alpha, red, green, blue);
    return emit_plain_scalar(emitter, key) && emit_plain_scalar(emitter, buffer);
}

static int emit_mapping_start(struct yaml_emitter_s *emitter)
{
    yaml_event_t event;
    if (!yaml_mapping_start_event_initialize(&event, NULL, NULL, 1, YAML_BLOCK_MAPPING_STYLE)) {
        return 0;
    }
    return emit_event(emitter, &event);
}

static int emit_mapping_end(struct yaml_emitter_s *emitter)
{
    yaml_event_t event;
    if (!yaml_mapping_end_event_initialize(&event)) {
        return 0;
    }
    return emit_event(emitter, &event);
}

struct yetty_ycore_void_result yetty_yrich_ydoc_save_yaml_file(struct yetty_yclass_object *doc_obj,
                                                               const char *path)
{
    if (!doc_obj || !path) {
        return YETTY_ERR(yetty_ycore_void, "ydoc save: NULL document/path");
    }
    struct yetty_ycore_float_result page_width_res = yetty_yrich_ydoc_page_width(doc_obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, page_width_res, "ydoc save: page_width");
    struct yetty_ycore_float_result margin_res = yetty_yrich_ydoc_margin(doc_obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, margin_res, "ydoc save: margin");
    struct yetty_ycore_size_result count_res = yetty_yrich_ydoc_paragraph_count(doc_obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, count_res, "ydoc save: paragraph_count");

    FILE *file = fopen(path, "wb");
    if (!file) {
        return YETTY_ERR(yetty_ycore_void, "ydoc save: cannot open file for writing");
    }

    struct yaml_emitter_s emitter;
    if (!yaml_emitter_initialize(&emitter)) {
        fclose(file);
        return YETTY_ERR(yetty_ycore_void, "ydoc save: emitter init failed");
    }
    yaml_emitter_set_output_file(&emitter, file);

    int ok = 1;
    yaml_event_t event;
    ok = ok && yaml_stream_start_event_initialize(&event, YAML_UTF8_ENCODING) &&
         emit_event(&emitter, &event);
    ok = ok && yaml_document_start_event_initialize(&event, NULL, NULL, NULL, 1) &&
         emit_event(&emitter, &event);
    ok = ok && emit_mapping_start(&emitter);
    ok = ok && emit_plain_scalar(&emitter, "document");
    ok = ok && emit_mapping_start(&emitter);
    ok = ok && emit_key_float(&emitter, "pageWidth", page_width_res.value);
    ok = ok && emit_key_float(&emitter, "margin", margin_res.value);
    ok = ok && emit_plain_scalar(&emitter, "paragraphs");
    if (ok) {
        ok = yaml_sequence_start_event_initialize(&event, NULL, NULL, 1,
                                                  YAML_BLOCK_SEQUENCE_STYLE) &&
             emit_event(&emitter, &event);
    }
    for (size_t i = 0; ok && i < count_res.value; i++) {
        struct yetty_yclass_object *paragraph_obj =
            yetty_yrich_ydoc_paragraph_at(doc_obj, (int32_t)i);
        if (!paragraph_obj) {
            ok = 0;
            break;
        }
        struct yetty_ycore_size_result text_len_res = yetty_yrich_paragraph_text_len(paragraph_obj);
        struct yetty_ycore_float_result font_size_res =
            yetty_yrich_paragraph_font_size(paragraph_obj);
        struct yetty_ycore_uint32_result color_res = yetty_yrich_paragraph_color(paragraph_obj);
        struct yetty_ycore_uint32_result format_res = yetty_yrich_paragraph_format(paragraph_obj);
        if (YETTY_IS_ERR(text_len_res) || YETTY_IS_ERR(font_size_res) || YETTY_IS_ERR(color_res) ||
            YETTY_IS_ERR(format_res)) {
            if (YETTY_IS_ERR(text_len_res)) {
                yetty_ycore_error_destroy(text_len_res.error);
            }
            if (YETTY_IS_ERR(font_size_res)) {
                yetty_ycore_error_destroy(font_size_res.error);
            }
            if (YETTY_IS_ERR(color_res)) {
                yetty_ycore_error_destroy(color_res.error);
            }
            if (YETTY_IS_ERR(format_res)) {
                yetty_ycore_error_destroy(format_res.error);
            }
            ok = 0;
            break;
        }
        ok = ok && emit_mapping_start(&emitter);
        ok = ok && emit_plain_scalar(&emitter, "text") &&
             emit_quoted_scalar(&emitter, yetty_yrich_paragraph_text(paragraph_obj),
                                text_len_res.value);
        ok = ok && emit_key_float(&emitter, "fontSize", font_size_res.value);
        ok = ok && emit_key_color(&emitter, "color", color_res.value);
        ok = ok && emit_key_uint(&emitter, "format", format_res.value);
        struct yetty_ycore_uint32_result align_res = yetty_yrich_paragraph_alignment(paragraph_obj);
        if (YETTY_IS_ERR(align_res)) {
            yetty_ycore_error_destroy(align_res.error);
            ok = 0;
        } else {
            ok = ok && emit_key_uint(&emitter, "align", align_res.value);
        }
        struct yetty_ycore_size_result run_count_res =
            yetty_yrich_paragraph_run_count(paragraph_obj);
        if (YETTY_IS_ERR(run_count_res)) {
            yetty_ycore_error_destroy(run_count_res.error);
            ok = 0;
        } else if (ok && run_count_res.value > 0) {
            ok = emit_plain_scalar(&emitter, "runs");
            if (ok) {
                ok = yaml_sequence_start_event_initialize(&event, NULL, NULL, 1,
                                                          YAML_BLOCK_SEQUENCE_STYLE) &&
                     emit_event(&emitter, &event);
            }
            for (size_t run_index = 0; ok && run_index < run_count_res.value; run_index++) {
                int32_t run_start = 0;
                int32_t run_end = 0;
                uint32_t run_format = 0;
                uint32_t run_color = 0;
                struct yetty_ycore_void_result run_res = yetty_yrich_paragraph_run_get(
                    paragraph_obj, run_index, &run_start, &run_end, &run_format, &run_color);
                if (YETTY_IS_ERR(run_res)) {
                    yetty_ycore_error_destroy(run_res.error);
                    ok = 0;
                    break;
                }
                ok = ok && emit_mapping_start(&emitter);
                ok = ok && emit_key_uint(&emitter, "start", (uint32_t)run_start);
                ok = ok && emit_key_uint(&emitter, "end", (uint32_t)run_end);
                ok = ok && emit_key_uint(&emitter, "format", run_format);
                ok = ok && emit_key_color(&emitter, "color", run_color);
                ok = ok && emit_mapping_end(&emitter);
            }
            if (ok) {
                ok = yaml_sequence_end_event_initialize(&event) && emit_event(&emitter, &event);
            }
        }
        ok = ok && emit_mapping_end(&emitter);
    }
    if (ok) {
        ok = yaml_sequence_end_event_initialize(&event) && emit_event(&emitter, &event);
    }
    ok = ok && emit_mapping_end(&emitter);
    ok = ok && emit_mapping_end(&emitter);
    if (ok) {
        ok = yaml_document_end_event_initialize(&event, 1) && emit_event(&emitter, &event);
    }
    if (ok) {
        ok = yaml_stream_end_event_initialize(&event) && emit_event(&emitter, &event);
    }

    yaml_emitter_delete(&emitter);
    if (fclose(file) != 0) {
        return YETTY_ERR(yetty_ycore_void, "ydoc save: close failed");
    }
    if (!ok) {
        return YETTY_ERR(yetty_ycore_void, "ydoc save: yaml emit failed");
    }
    return YETTY_OK_VOID();
}

/*=============================================================================
 * yspreadsheet loader
 *===========================================================================*/

static struct yetty_ycore_void_result parse_sheet_cells(struct yaml_parser_s *p,
                                                        struct yetty_yclass_object *sheet_obj)
{
    yaml_event_t ev;
    struct yetty_ycore_void_result ev_res = next_event(p, &ev);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ev_res, "yrich yaml: cells read failed");
    if (ev.type != YAML_MAPPING_START_EVENT) {
        yaml_event_delete(&ev);
        return YETTY_ERR(yetty_ycore_void, "yrich yaml: cells expected mapping");
    }
    yaml_event_delete(&ev);

    for (;;) {
        ev_res = next_event(p, &ev);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, ev_res, "yrich yaml: cells read failed");
        if (ev.type == YAML_MAPPING_END_EVENT) {
            yaml_event_delete(&ev);
            return YETTY_OK_VOID();
        }
        if (ev.type != YAML_SCALAR_EVENT) {
            yaml_event_delete(&ev);
            return YETTY_ERR(yetty_ycore_void, "yrich yaml: cells expected scalar key");
        }
        char *key = scalar_dup(&ev);
        yaml_event_delete(&ev);

        ev_res = next_event(p, &ev);
        if (YETTY_IS_ERR(ev_res)) {
            free(key);
            return YETTY_ERR(yetty_ycore_void, "yrich yaml: cells read failed", ev_res);
        }
        if (ev.type == YAML_SCALAR_EVENT && key) {
            int32_t row, col;
            if (parse_cell_ref(key, &row, &col)) {
                struct yetty_ycore_buffer value = {
                    .data = ev.data.scalar.value,
                    .size = ev.data.scalar.length,
                    .capacity = ev.data.scalar.length,
                };
                struct yetty_ycore_void_result set_res =
                    yetty_yrich_spreadsheet_set_cell_value(sheet_obj, row, col, value);
                if (YETTY_IS_ERR(set_res)) {
                    yaml_event_delete(&ev);
                    free(key);
                    return YETTY_ERR(yetty_ycore_void, "yrich yaml: set_cell_value failed",
                                     set_res);
                }
            }
        } else if (ev.type == YAML_MAPPING_START_EVENT || ev.type == YAML_SEQUENCE_START_EVENT) {
            yaml_event_delete(&ev);
            free(key);
            struct yetty_ycore_void_result skip_res = skip_collection_body(p);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, skip_res, "yrich yaml: cells skip failed");
            continue;
        }
        yaml_event_delete(&ev);
        free(key);
    }
}

static struct yetty_ycore_void_result parse_sheet_col_widths(struct yaml_parser_s *p,
                                                             struct yetty_yclass_object *sheet_obj)
{
    yaml_event_t ev;
    struct yetty_ycore_void_result ev_res = next_event(p, &ev);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ev_res, "yrich yaml: col widths read failed");
    if (ev.type != YAML_SEQUENCE_START_EVENT) {
        yaml_event_delete(&ev);
        return YETTY_ERR(yetty_ycore_void, "yrich yaml: col widths expected sequence");
    }
    yaml_event_delete(&ev);

    int32_t col = 0;
    for (;;) {
        ev_res = next_event(p, &ev);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, ev_res, "yrich yaml: col widths read failed");
        if (ev.type == YAML_SEQUENCE_END_EVENT) {
            yaml_event_delete(&ev);
            return YETTY_OK_VOID();
        }
        if (ev.type == YAML_SCALAR_EVENT) {
            float width = (float)scalar_to_d(&ev);
            struct yetty_ycore_void_result width_res =
                yetty_yrich_spreadsheet_set_col_width(sheet_obj, col++, width);
            if (YETTY_IS_ERR(width_res)) {
                yaml_event_delete(&ev);
                return YETTY_ERR(yetty_ycore_void, "yrich yaml: set_col_width failed", width_res);
            }
        }
        yaml_event_delete(&ev);
    }
}

static struct yetty_ycore_void_result parse_sheet_body(struct yaml_parser_s *p,
                                                       struct yetty_yclass_object *sheet_obj)
{
    yaml_event_t ev;
    struct yetty_ycore_void_result ev_res = next_event(p, &ev);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ev_res, "yrich yaml: sheet body read failed");
    if (ev.type != YAML_MAPPING_START_EVENT) {
        yaml_event_delete(&ev);
        return YETTY_ERR(yetty_ycore_void, "yrich yaml: sheet body expected mapping");
    }
    yaml_event_delete(&ev);

    int32_t rows = 100, cols = 26;
    for (;;) {
        ev_res = next_event(p, &ev);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, ev_res, "yrich yaml: sheet body read failed");
        if (ev.type == YAML_MAPPING_END_EVENT) {
            yaml_event_delete(&ev);
            return yetty_yrich_spreadsheet_set_grid_size(sheet_obj, rows, cols);
        }
        if (ev.type != YAML_SCALAR_EVENT) {
            yaml_event_delete(&ev);
            return YETTY_ERR(yetty_ycore_void, "yrich yaml: sheet body expected scalar key");
        }
        bool key_rows = scalar_eq(&ev, "rows");
        bool key_cols = scalar_eq(&ev, "cols");
        bool key_cw = scalar_eq(&ev, "columnWidths");
        bool key_cells = scalar_eq(&ev, "cells");
        yaml_event_delete(&ev);

        if (key_cw) {
            struct yetty_ycore_void_result widths_res = parse_sheet_col_widths(p, sheet_obj);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, widths_res,
                                "yrich yaml: col widths parse failed");
            continue;
        }
        if (key_cells) {
            struct yetty_ycore_void_result grid_res =
                yetty_yrich_spreadsheet_set_grid_size(sheet_obj, rows, cols);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, grid_res, "yrich yaml: set_grid_size failed");
            struct yetty_ycore_void_result cells_res = parse_sheet_cells(p, sheet_obj);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, cells_res, "yrich yaml: cells parse failed");
            continue;
        }
        ev_res = next_event(p, &ev);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, ev_res, "yrich yaml: sheet body read failed");
        if (key_rows && ev.type == YAML_SCALAR_EVENT) {
            rows = (int32_t)scalar_to_l(&ev);
        } else if (key_cols && ev.type == YAML_SCALAR_EVENT) {
            cols = (int32_t)scalar_to_l(&ev);
        } else if (ev.type == YAML_MAPPING_START_EVENT || ev.type == YAML_SEQUENCE_START_EVENT) {
            yaml_event_delete(&ev);
            struct yetty_ycore_void_result skip_res = skip_collection_body(p);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, skip_res, "yrich yaml: sheet body skip failed");
            continue;
        }
        yaml_event_delete(&ev);
    }
}

struct yetty_yclass_object_ptr_result yetty_yrich_spreadsheet_load_yaml(const char *yaml,
                                                                        size_t len)
{
    if (!yaml) {
        return YETTY_ERR(yetty_yclass_object_ptr, "yrich sheet load: NULL yaml");
    }

    struct yetty_yclass_object_ptr_result sheet_res = yetty_yrich_spreadsheet_create(NULL);
    if (YETTY_IS_ERR(sheet_res)) {
        return sheet_res;
    }
    struct yetty_yclass_object *sheet_obj = sheet_res.value;

    struct yaml_parser_s parser;
    yaml_parser_initialize(&parser);
    yaml_parser_set_input_string(&parser, (const unsigned char *)yaml, len);

    yaml_event_t ev;
    bool found = false;
    struct yetty_ycore_void_result parse_res = YETTY_OK_VOID();
    for (;;) {
        struct yetty_ycore_void_result ev_res = next_event(&parser, &ev);
        if (YETTY_IS_ERR(ev_res)) {
            parse_res = ev_res;
            break;
        }
        if (ev.type == YAML_STREAM_END_EVENT) {
            yaml_event_delete(&ev);
            break;
        }
        if (ev.type == YAML_MAPPING_START_EVENT) {
            yaml_event_delete(&ev);
            for (;;) {
                ev_res = next_event(&parser, &ev);
                if (YETTY_IS_ERR(ev_res)) {
                    parse_res = ev_res;
                    goto done;
                }
                if (ev.type == YAML_MAPPING_END_EVENT) {
                    yaml_event_delete(&ev);
                    break;
                }
                if (ev.type != YAML_SCALAR_EVENT) {
                    yaml_event_delete(&ev);
                    parse_res = YETTY_ERR(yetty_ycore_void, "yrich yaml: expected scalar key");
                    goto done;
                }
                bool key = scalar_eq(&ev, "spreadsheet");
                yaml_event_delete(&ev);
                if (key) {
                    found = true;
                    struct yetty_ycore_void_result body_res = parse_sheet_body(&parser, sheet_obj);
                    if (YETTY_IS_ERR(body_res)) {
                        parse_res = body_res;
                        goto done;
                    }
                } else {
                    struct yetty_ycore_void_result skip_res = skip_node(&parser);
                    if (YETTY_IS_ERR(skip_res)) {
                        parse_res = skip_res;
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
    if (YETTY_IS_ERR(parse_res) || !found) {
        struct yetty_ycore_void_result destroy_res = yetty_yrich_document_destroy(sheet_obj);
        if (YETTY_IS_ERR(destroy_res)) {
            yetty_ycore_error_destroy(destroy_res.error);
        }
        if (YETTY_IS_ERR(parse_res)) {
            return YETTY_ERR(yetty_yclass_object_ptr, "yrich sheet: yaml parse failed", parse_res);
        }
        return YETTY_ERR(yetty_yclass_object_ptr, "yrich sheet: no 'spreadsheet' mapping");
    }
    return YETTY_OK(yetty_yclass_object_ptr, sheet_obj);
}

struct yetty_yclass_object_ptr_result yetty_yrich_spreadsheet_load_yaml_file(const char *path)
{
    char *buf = NULL;
    size_t len = 0;
    struct yetty_ycore_void_result read_res = read_file_all(path, &buf, &len);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, read_res, "yrich sheet: cannot read file");
    struct yetty_yclass_object_ptr_result r = yetty_yrich_spreadsheet_load_yaml(buf, len);
    free(buf);
    return r;
}

/*=============================================================================
 * yslides loader
 *===========================================================================*/

struct yetty_yrich_shape_fields {
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

static void shape_fields_init(struct yetty_yrich_shape_fields *f)
{
    memset(f, 0, sizeof(*f));
    f->w = 100.0f;
    f->h = 100.0f;
    f->stroke_width = 1.0f;
    f->font_size = 24.0f;
}

static void shape_fields_free(struct yetty_yrich_shape_fields *f)
{
    free(f->text);
    free(f->image_source);
}

/* Apply the parsed optional fields onto a freshly added shape object. */
static struct yetty_ycore_void_result apply_shape_fields(struct yetty_yclass_object *shape_obj,
                                                         const struct yetty_yrich_shape_fields *f)
{
    if (f->has_fill) {
        struct yetty_ycore_void_result set_res =
            yetty_yrich_shape_fill_color_set(shape_obj, f->fill);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, set_res, "yrich yaml: fill_color set");
    }
    if (f->has_stroke) {
        struct yetty_ycore_void_result set_res =
            yetty_yrich_shape_stroke_color_set(shape_obj, f->stroke);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, set_res, "yrich yaml: stroke_color set");
    }
    if (f->has_stroke_w) {
        struct yetty_ycore_void_result set_res =
            yetty_yrich_shape_stroke_width_set(shape_obj, f->stroke_width);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, set_res, "yrich yaml: stroke_width set");
    }
    struct yetty_ycore_void_result rotation_res =
        yetty_yrich_shape_rotation_set(shape_obj, f->rotation);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rotation_res, "yrich yaml: rotation set");
    struct yetty_ycore_void_result radius_res =
        yetty_yrich_shape_corner_radius_set(shape_obj, f->corner_radius);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, radius_res, "yrich yaml: corner_radius set");
    if (f->font_size > 0.0f) {
        struct yetty_ycore_void_result set_res =
            yetty_yrich_shape_set_font_size(shape_obj, f->font_size);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, set_res, "yrich yaml: font_size set");
    }
    if (f->has_text_color) {
        struct yetty_ycore_void_result set_res =
            yetty_yrich_shape_set_text_color(shape_obj, f->text_color);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, set_res, "yrich yaml: text_color set");
    }
    if (f->has_text_align) {
        struct yetty_ycore_void_result set_res =
            yetty_yrich_shape_text_align_set(shape_obj, f->text_align);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, set_res, "yrich yaml: text_align set");
    }
    if (f->has_text_valign) {
        struct yetty_ycore_void_result set_res =
            yetty_yrich_shape_text_valign_set(shape_obj, f->text_valign);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, set_res, "yrich yaml: text_valign set");
    }
    if (f->image_source) {
        struct yetty_ycore_void_result set_res =
            yetty_yrich_shape_set_image_source(shape_obj, f->image_source);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, set_res, "yrich yaml: image_source set");
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result parse_shape(struct yaml_parser_s *p,
                                                  struct yetty_yclass_object *slides_obj)
{
    struct yetty_yrich_shape_fields f;
    shape_fields_init(&f);
    struct yetty_ycore_void_result fail_res;

    yaml_event_t ev;
    for (;;) {
        struct yetty_ycore_void_result ev_res = next_event(p, &ev);
        if (YETTY_IS_ERR(ev_res)) {
            fail_res = YETTY_ERR(yetty_ycore_void, "yrich yaml: shape read failed", ev_res);
            goto err;
        }
        if (ev.type == YAML_MAPPING_END_EVENT) {
            yaml_event_delete(&ev);
            break;
        }
        if (ev.type != YAML_SCALAR_EVENT) {
            yaml_event_delete(&ev);
            fail_res = YETTY_ERR(yetty_ycore_void, "yrich yaml: shape expected scalar key");
            goto err;
        }
        const char *key = (const char *)ev.data.scalar.value;
        size_t klen = ev.data.scalar.length;
        char keybuf[32];
        size_t cp = klen < sizeof(keybuf) - 1 ? klen : sizeof(keybuf) - 1;
        memcpy(keybuf, key, cp);
        keybuf[cp] = '\0';
        yaml_event_delete(&ev);

        ev_res = next_event(p, &ev);
        if (YETTY_IS_ERR(ev_res)) {
            fail_res = YETTY_ERR(yetty_ycore_void, "yrich yaml: shape read failed", ev_res);
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
                if (!f.text) {
                    yaml_event_delete(&ev);
                    fail_res = YETTY_ERR(yetty_ycore_void, "yrich yaml: shape text alloc failed");
                    goto err;
                }
                memcpy(f.text, ev.data.scalar.value, f.text_len);
                f.text[f.text_len] = '\0';
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
            struct yetty_ycore_void_result skip_res = skip_collection_body(p);
            if (YETTY_IS_ERR(skip_res)) {
                fail_res = YETTY_ERR(yetty_ycore_void, "yrich yaml: shape skip failed", skip_res);
                goto err;
            }
            continue;
        }
        yaml_event_delete(&ev);
    }

    struct yetty_yclass_object_ptr_result shape_res;
    switch (f.type) {
    case 0:
        shape_res = yetty_yrich_slides_add_rectangle(slides_obj, f.x, f.y, f.w, f.h);
        break;
    case 1:
        shape_res = yetty_yrich_slides_add_ellipse(slides_obj, f.x, f.y, f.w, f.h);
        break;
    case 2:
        shape_res =
            yetty_yrich_slides_add_textbox(slides_obj, f.x, f.y, f.w, f.h, f.text, f.text_len);
        break;
    case 3:
        shape_res = yetty_yrich_slides_add_line(slides_obj, f.x, f.y, f.x + f.w, f.y + f.h);
        break;
    case 5:
        shape_res = yetty_yrich_slides_add_image(slides_obj, f.x, f.y, f.w, f.h);
        break;
    default:
        shape_res = yetty_yrich_slides_add_rectangle(slides_obj, f.x, f.y, f.w, f.h);
        break;
    }
    if (YETTY_IS_ERR(shape_res)) {
        fail_res = YETTY_ERR(yetty_ycore_void, "yrich yaml: shape add failed", shape_res);
        goto err;
    }
    struct yetty_ycore_void_result apply_res = apply_shape_fields(shape_res.value, &f);
    if (YETTY_IS_ERR(apply_res)) {
        fail_res = YETTY_ERR(yetty_ycore_void, "yrich yaml: shape apply failed", apply_res);
        goto err;
    }
    shape_fields_free(&f);
    return YETTY_OK_VOID();
err:
    shape_fields_free(&f);
    return fail_res;
}

static struct yetty_ycore_void_result parse_slide(struct yaml_parser_s *p,
                                                  struct yetty_yclass_object *slides_obj)
{
    yaml_event_t ev;
    int32_t index = -1;
    uint32_t bg = YETTY_YRICH_COLOR_WHITE;
    bool have_bg = false;

    for (;;) {
        struct yetty_ycore_void_result ev_res = next_event(p, &ev);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, ev_res, "yrich yaml: slide read failed");
        if (ev.type == YAML_MAPPING_END_EVENT) {
            yaml_event_delete(&ev);
            break;
        }
        if (ev.type != YAML_SCALAR_EVENT) {
            yaml_event_delete(&ev);
            return YETTY_ERR(yetty_ycore_void, "yrich yaml: slide expected scalar key");
        }
        bool key_index = scalar_eq(&ev, "index");
        bool key_bg = scalar_eq(&ev, "bgColor");
        bool key_shapes = scalar_eq(&ev, "shapes");
        yaml_event_delete(&ev);

        if (key_shapes) {
            /* We need the slide to exist before adding shapes — make sure
			 * we've seen index by now (POC always lists it first). Add the
			 * slide if needed. */
            if (index < 0) {
                return YETTY_ERR(yetty_ycore_void, "yrich yaml: slide 'shapes' before 'index'");
            }
            for (;;) {
                struct yetty_yrich_slide_ptr_result slide_at_res =
                    yetty_yrich_slides_slide_at(slides_obj, index);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, slide_at_res,
                                    "yrich yaml: slide lookup failed");
                if (slide_at_res.value != NULL) {
                    break;
                }
                struct yetty_yrich_slide_ptr_result slide_res =
                    yetty_yrich_slides_add_slide(slides_obj);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, slide_res, "yrich yaml: slide create failed");
            }
            struct yetty_ycore_void_result current_res =
                yetty_yrich_slides_set_current(slides_obj, index);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, current_res, "yrich yaml: set_current failed");
            struct yetty_yrich_slide_ptr_result current_at_res =
                yetty_yrich_slides_slide_at(slides_obj, index);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, current_at_res,
                                "yrich yaml: slide lookup failed");
            struct yetty_yrich_slide *current = current_at_res.value;
            if (current && have_bg) {
                current->bg_color = bg;
            }

            ev_res = next_event(p, &ev);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, ev_res, "yrich yaml: slide shapes read failed");
            if (ev.type != YAML_SEQUENCE_START_EVENT) {
                yaml_event_delete(&ev);
                return YETTY_ERR(yetty_ycore_void, "yrich yaml: shapes expected sequence");
            }
            yaml_event_delete(&ev);
            for (;;) {
                ev_res = next_event(p, &ev);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, ev_res, "yrich yaml: shapes read failed");
                if (ev.type == YAML_SEQUENCE_END_EVENT) {
                    yaml_event_delete(&ev);
                    break;
                }
                if (ev.type != YAML_MAPPING_START_EVENT) {
                    yaml_event_delete(&ev);
                    return YETTY_ERR(yetty_ycore_void, "yrich yaml: shape expected mapping");
                }
                yaml_event_delete(&ev);
                struct yetty_ycore_void_result shape_res = parse_shape(p, slides_obj);
                if (YETTY_IS_ERR(shape_res)) {
                    return shape_res;
                }
            }
            continue;
        }

        ev_res = next_event(p, &ev);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, ev_res, "yrich yaml: slide read failed");
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
            struct yetty_ycore_void_result skip_res = skip_collection_body(p);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, skip_res, "yrich yaml: slide skip failed");
            continue;
        }
        yaml_event_delete(&ev);
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result parse_slides_seq(struct yaml_parser_s *p,
                                                       struct yetty_yclass_object *slides_obj)
{
    yaml_event_t ev;
    struct yetty_ycore_void_result ev_res = next_event(p, &ev);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ev_res, "yrich yaml: slides read failed");
    if (ev.type != YAML_SEQUENCE_START_EVENT) {
        yaml_event_delete(&ev);
        return YETTY_ERR(yetty_ycore_void, "yrich yaml: slides expected sequence");
    }
    yaml_event_delete(&ev);
    for (;;) {
        ev_res = next_event(p, &ev);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, ev_res, "yrich yaml: slides read failed");
        if (ev.type == YAML_SEQUENCE_END_EVENT) {
            yaml_event_delete(&ev);
            return YETTY_OK_VOID();
        }
        if (ev.type != YAML_MAPPING_START_EVENT) {
            yaml_event_delete(&ev);
            return YETTY_ERR(yetty_ycore_void, "yrich yaml: slide expected mapping");
        }
        yaml_event_delete(&ev);
        struct yetty_ycore_void_result slide_res = parse_slide(p, slides_obj);
        if (YETTY_IS_ERR(slide_res)) {
            return slide_res;
        }
    }
}

static struct yetty_ycore_void_result parse_presentation(struct yaml_parser_s *p,
                                                         struct yetty_yclass_object *slides_obj)
{
    yaml_event_t ev;
    struct yetty_ycore_void_result ev_res = next_event(p, &ev);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ev_res, "yrich yaml: presentation read failed");
    if (ev.type != YAML_MAPPING_START_EVENT) {
        yaml_event_delete(&ev);
        return YETTY_ERR(yetty_ycore_void, "yrich yaml: presentation expected mapping");
    }
    yaml_event_delete(&ev);

    float width = 960.0f, height = 540.0f;
    for (;;) {
        ev_res = next_event(p, &ev);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, ev_res, "yrich yaml: presentation read failed");
        if (ev.type == YAML_MAPPING_END_EVENT) {
            yaml_event_delete(&ev);
            struct yetty_ycore_void_result size_res =
                yetty_yrich_slides_set_slide_size(slides_obj, width, height);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, size_res, "yrich yaml: set_slide_size failed");
            return yetty_yrich_slides_set_current(slides_obj, 0);
        }
        if (ev.type != YAML_SCALAR_EVENT) {
            yaml_event_delete(&ev);
            return YETTY_ERR(yetty_ycore_void, "yrich yaml: presentation expected scalar key");
        }
        bool key_w = scalar_eq(&ev, "slideWidth");
        bool key_h = scalar_eq(&ev, "slideHeight");
        bool key_s = scalar_eq(&ev, "slides");
        yaml_event_delete(&ev);

        if (key_s) {
            struct yetty_ycore_void_result size_res =
                yetty_yrich_slides_set_slide_size(slides_obj, width, height);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, size_res, "yrich yaml: set_slide_size failed");
            struct yetty_ycore_void_result slides_res = parse_slides_seq(p, slides_obj);
            if (YETTY_IS_ERR(slides_res)) {
                return slides_res;
            }
            continue;
        }
        ev_res = next_event(p, &ev);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, ev_res, "yrich yaml: presentation read failed");
        if (key_w && ev.type == YAML_SCALAR_EVENT) {
            width = (float)scalar_to_d(&ev);
        } else if (key_h && ev.type == YAML_SCALAR_EVENT) {
            height = (float)scalar_to_d(&ev);
        } else if (ev.type == YAML_MAPPING_START_EVENT || ev.type == YAML_SEQUENCE_START_EVENT) {
            yaml_event_delete(&ev);
            struct yetty_ycore_void_result skip_res = skip_collection_body(p);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, skip_res, "yrich yaml: presentation skip failed");
            continue;
        }
        yaml_event_delete(&ev);
    }
}

struct yetty_yclass_object_ptr_result yetty_yrich_slides_load_yaml(const char *yaml, size_t len)
{
    if (!yaml) {
        return YETTY_ERR(yetty_yclass_object_ptr, "yrich slides load: NULL yaml");
    }

    struct yetty_yclass_object_ptr_result slides_res = yetty_yrich_slides_create(NULL);
    if (YETTY_IS_ERR(slides_res)) {
        return slides_res;
    }
    struct yetty_yclass_object *slides_obj = slides_res.value;

    struct yaml_parser_s parser;
    yaml_parser_initialize(&parser);
    yaml_parser_set_input_string(&parser, (const unsigned char *)yaml, len);

    yaml_event_t ev;
    bool found = false;
    struct yetty_ycore_void_result parse_res = YETTY_OK_VOID();
    for (;;) {
        struct yetty_ycore_void_result ev_res = next_event(&parser, &ev);
        if (YETTY_IS_ERR(ev_res)) {
            parse_res = ev_res;
            break;
        }
        if (ev.type == YAML_STREAM_END_EVENT) {
            yaml_event_delete(&ev);
            break;
        }
        if (ev.type == YAML_MAPPING_START_EVENT) {
            yaml_event_delete(&ev);
            for (;;) {
                ev_res = next_event(&parser, &ev);
                if (YETTY_IS_ERR(ev_res)) {
                    parse_res = ev_res;
                    goto done;
                }
                if (ev.type == YAML_MAPPING_END_EVENT) {
                    yaml_event_delete(&ev);
                    break;
                }
                if (ev.type != YAML_SCALAR_EVENT) {
                    yaml_event_delete(&ev);
                    parse_res = YETTY_ERR(yetty_ycore_void, "yrich yaml: expected scalar key");
                    goto done;
                }
                bool key = scalar_eq(&ev, "presentation");
                yaml_event_delete(&ev);
                if (key) {
                    found = true;
                    struct yetty_ycore_void_result pres_res =
                        parse_presentation(&parser, slides_obj);
                    if (YETTY_IS_ERR(pres_res)) {
                        parse_res = pres_res;
                        goto done;
                    }
                } else {
                    struct yetty_ycore_void_result skip_res = skip_node(&parser);
                    if (YETTY_IS_ERR(skip_res)) {
                        parse_res = skip_res;
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
    if (YETTY_IS_ERR(parse_res) || !found) {
        struct yetty_ycore_void_result destroy_res = yetty_yrich_document_destroy(slides_obj);
        if (YETTY_IS_ERR(destroy_res)) {
            yetty_ycore_error_destroy(destroy_res.error);
        }
        if (YETTY_IS_ERR(parse_res)) {
            return YETTY_ERR(yetty_yclass_object_ptr, "yrich slides: yaml parse failed", parse_res);
        }
        return YETTY_ERR(yetty_yclass_object_ptr, "yrich slides: no 'presentation' mapping");
    }
    return YETTY_OK(yetty_yclass_object_ptr, slides_obj);
}

struct yetty_yclass_object_ptr_result yetty_yrich_slides_load_yaml_file(const char *path)
{
    char *buf = NULL;
    size_t len = 0;
    struct yetty_ycore_void_result read_res = read_file_all(path, &buf, &len);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, read_res, "yrich slides: cannot read file");
    struct yetty_yclass_object_ptr_result r = yetty_yrich_slides_load_yaml(buf, len);
    free(buf);
    return r;
}
