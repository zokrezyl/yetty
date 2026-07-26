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

#include <yetty/api/yrich/document.h>
#include <yetty/api/yrich/element.h>
#include <yetty/api/yrich/slides.h>
#include <yetty/api/yrich/spreadsheet.h>
#include <yetty/api/yrich/ydoc.h>

#include <yaml.h>

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Atomic save uses a durable flush + atomic replace; the primitives differ
 * on Windows vs POSIX (emscripten/android/macOS/Linux all take the POSIX
 * branch). */
#if defined(_WIN32)
#include <io.h>      /* _commit, _fileno */
#include <windows.h> /* MoveFileExA */
#else
#include <unistd.h> /* fsync */
#endif

/* Native `.ydoc.yaml` schema version. Bump when the on-disk format changes
 * incompatibly and add a migration step in migrate_ydoc_document(). A file with
 * no `version` key is treated as version 0 (pre-versioning); a version newer
 * than this is rejected as an unsupported future format. */
#define YETTY_YRICH_YDOC_SCHEMA_VERSION 1u

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
    uint32_t bg_color;
    float font_size;
    char *link; /* owned hyperlink URL, or NULL; applied after the run is added */
};

/* Parse one run mapping {start, end, format, color, bg, fs, link}. */
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
        bool key_bg = scalar_eq(&ev, "bg");
        bool key_fs = scalar_eq(&ev, "fs");
        bool key_link = scalar_eq(&ev, "link");
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
            } else if (key_bg) {
                char *raw = scalar_dup(&ev);
                if (raw) {
                    out_run->bg_color = parse_color_argb(raw);
                    free(raw);
                }
            } else if (key_fs) {
                out_run->font_size = (float)scalar_to_d(&ev);
            } else if (key_link) {
                free(out_run->link);
                out_run->link = scalar_dup(&ev);
            }
        }
        yaml_event_delete(&ev);
    }
}

/* Free a parsed-run array along with each run's owned link URL. */
static void free_parsed_runs(struct parsed_run *runs, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        free(runs[i].link);
    }
    free(runs);
}

/* Parse one paragraph mapping: text, fontSize, color, format, align and
 * the styled runs[]. Unknown collections are skipped. */
static struct yetty_ycore_void_result parse_ydoc_paragraph(struct yaml_parser_s *p,
                                                           struct yetty_yclass_object *doc_obj)
{
    char *text = NULL;
    size_t text_len = 0;
    float font_size = 0.0f;
    float line_spacing = 0.0f; /* 0 = not present in the file */
    float indent = 0.0f;
    int have_indent = 0;
    uint32_t heading_level = 0;
    uint32_t list_kind = 0;
    uint32_t list_checked = 0;
    uint32_t block_kind = 0;
    uint32_t list_level = 0;
    float space_before = 0.0f;
    float space_after = 0.0f;
    uint32_t color = 0;
    uint32_t format = 0;
    uint32_t align = 0;
    int have_align = 0;
    char *bookmark = NULL;
    struct parsed_run *runs = NULL;
    size_t run_count = 0;
    size_t run_capacity = 0;
    uint32_t table_rows = 0;
    uint32_t table_cols = 0;
    char **cells = NULL;
    size_t cell_count = 0;
    size_t cell_capacity = 0;
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
        bool key_spacing = scalar_eq(&ev, "lineSpacing");
        bool key_indent = scalar_eq(&ev, "indent");
        bool key_heading = scalar_eq(&ev, "heading");
        bool key_list = scalar_eq(&ev, "list");
        bool key_checked = scalar_eq(&ev, "checked");
        bool key_block = scalar_eq(&ev, "block");
        bool key_list_level = scalar_eq(&ev, "listLevel");
        bool key_space_before = scalar_eq(&ev, "spaceBefore");
        bool key_space_after = scalar_eq(&ev, "spaceAfter");
        bool key_table_rows = scalar_eq(&ev, "tableRows");
        bool key_table_cols = scalar_eq(&ev, "tableCols");
        bool key_bookmark = scalar_eq(&ev, "bookmark");
        bool key_cells = scalar_eq(&ev, "cells");
        bool key_runs = scalar_eq(&ev, "runs");
        yaml_event_delete(&ev);

        if (key_cells) {
            ev_res = next_event(p, &ev);
            if (YETTY_IS_ERR(ev_res)) {
                fail_res = YETTY_ERR(yetty_ycore_void, "yrich yaml: cells read failed", ev_res);
                goto err;
            }
            if (ev.type != YAML_SEQUENCE_START_EVENT) {
                yaml_event_delete(&ev);
                fail_res = YETTY_ERR(yetty_ycore_void, "yrich yaml: cells expected sequence");
                goto err;
            }
            yaml_event_delete(&ev);
            for (;;) {
                ev_res = next_event(p, &ev);
                if (YETTY_IS_ERR(ev_res)) {
                    fail_res = YETTY_ERR(yetty_ycore_void, "yrich yaml: cells read failed", ev_res);
                    goto err;
                }
                if (ev.type == YAML_SEQUENCE_END_EVENT) {
                    yaml_event_delete(&ev);
                    break;
                }
                if (ev.type != YAML_SCALAR_EVENT) {
                    yaml_event_delete(&ev);
                    fail_res = YETTY_ERR(yetty_ycore_void, "yrich yaml: cell expected scalar");
                    goto err;
                }
                if (cell_count == cell_capacity) {
                    size_t new_cap = cell_capacity ? cell_capacity * 2 : 8;
                    char **grown = realloc(cells, new_cap * sizeof(*cells));
                    if (!grown) {
                        yaml_event_delete(&ev);
                        fail_res = YETTY_ERR(yetty_ycore_void, "yrich yaml: cells grow failed");
                        goto err;
                    }
                    cells = grown;
                    cell_capacity = new_cap;
                }
                cells[cell_count] = scalar_dup(&ev);
                yaml_event_delete(&ev);
                if (!cells[cell_count]) {
                    fail_res = YETTY_ERR(yetty_ycore_void, "yrich yaml: cell dup failed");
                    goto err;
                }
                cell_count++;
            }
            continue;
        }

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
                    /* The partial run at [run_count] is not yet counted; free its
                     * link so free_parsed_runs (which frees only [0,run_count))
                     * does not leak it. */
                    free(runs[run_count].link);
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
        } else if (key_spacing && ev.type == YAML_SCALAR_EVENT) {
            line_spacing = (float)scalar_to_d(&ev);
        } else if (key_indent && ev.type == YAML_SCALAR_EVENT) {
            indent = (float)scalar_to_d(&ev);
            have_indent = 1;
        } else if (key_heading && ev.type == YAML_SCALAR_EVENT) {
            heading_level = (uint32_t)scalar_to_l(&ev);
        } else if (key_list && ev.type == YAML_SCALAR_EVENT) {
            list_kind = (uint32_t)scalar_to_l(&ev);
        } else if (key_checked && ev.type == YAML_SCALAR_EVENT) {
            list_checked = (uint32_t)scalar_to_l(&ev);
        } else if (key_block && ev.type == YAML_SCALAR_EVENT) {
            block_kind = (uint32_t)scalar_to_l(&ev);
        } else if (key_list_level && ev.type == YAML_SCALAR_EVENT) {
            list_level = (uint32_t)scalar_to_l(&ev);
        } else if (key_space_before && ev.type == YAML_SCALAR_EVENT) {
            space_before = (float)scalar_to_d(&ev);
        } else if (key_space_after && ev.type == YAML_SCALAR_EVENT) {
            space_after = (float)scalar_to_d(&ev);
        } else if (key_table_rows && ev.type == YAML_SCALAR_EVENT) {
            table_rows = (uint32_t)scalar_to_l(&ev);
        } else if (key_table_cols && ev.type == YAML_SCALAR_EVENT) {
            table_cols = (uint32_t)scalar_to_l(&ev);
        } else if (key_bookmark && ev.type == YAML_SCALAR_EVENT) {
            free(bookmark);
            bookmark = scalar_dup(&ev);
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
        free(bookmark);
        free_parsed_runs(runs, run_count);
        return YETTY_ERR(yetty_ycore_void, "yrich yaml: add_paragraph failed", paragraph_res);
    }
    struct yetty_yclass_object *paragraph_obj = paragraph_res.value;
    /* Apply and release the bookmark immediately so later error returns cannot
     * leak it. */
    if (bookmark) {
        struct yetty_ycore_void_result bm_res =
            yetty_yrich_paragraph_set_bookmark(paragraph_obj, bookmark);
        free(bookmark);
        bookmark = NULL;
        if (YETTY_IS_ERR(bm_res)) {
            free_parsed_runs(runs, run_count);
            return YETTY_ERR(yetty_ycore_void, "yrich yaml: set_bookmark failed", bm_res);
        }
    }
    if (font_size > 0.0f) {
        struct yetty_ycore_void_result font_res =
            yetty_yrich_paragraph_set_font_size(paragraph_obj, font_size);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, font_res, "yrich yaml: set_font_size failed");
    }
    /* After font size — set_line_spacing recomputes line_height from font_size. */
    if (line_spacing > 0.0f) {
        struct yetty_ycore_void_result spacing_res =
            yetty_yrich_paragraph_set_line_spacing(paragraph_obj, line_spacing);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, spacing_res, "yrich yaml: set_line_spacing failed");
    }
    if (have_indent) {
        struct yetty_ycore_void_result indent_res =
            yetty_yrich_paragraph_set_indent(paragraph_obj, indent);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, indent_res, "yrich yaml: set_indent failed");
    }
    if (heading_level > 0) {
        struct yetty_ycore_void_result heading_res =
            yetty_yrich_paragraph_set_heading_level(paragraph_obj, heading_level);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, heading_res, "yrich yaml: set_heading_level failed");
    }
    if (list_kind > 0) {
        struct yetty_ycore_void_result list_res =
            yetty_yrich_paragraph_set_list_kind(paragraph_obj, list_kind);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, list_res, "yrich yaml: set_list_kind failed");
        if (list_checked) {
            struct yetty_ycore_void_result checked_res =
                yetty_yrich_paragraph_set_list_checked(paragraph_obj, list_checked);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, checked_res,
                                "yrich yaml: set_list_checked failed");
        }
    }
    if (block_kind > 0) {
        struct yetty_ycore_void_result block_res =
            yetty_yrich_paragraph_set_block_kind(paragraph_obj, block_kind);
        if (YETTY_IS_ERR(block_res)) {
            for (size_t i = 0; i < cell_count; i++) {
                free(cells[i]);
            }
            free(cells);
            free_parsed_runs(runs, run_count);
            return YETTY_ERR(yetty_ycore_void, "yrich yaml: set_block_kind failed", block_res);
        }
    }
    if (block_kind == 2 && table_rows > 0 && table_cols > 0) {
        struct yetty_ycore_void_result table_res =
            yetty_yrich_paragraph_set_table(paragraph_obj, table_rows, table_cols);
        if (!YETTY_IS_ERR(table_res)) {
            for (uint32_t r = 0; r < table_rows; r++) {
                for (uint32_t c = 0; c < table_cols; c++) {
                    size_t flat = (size_t)r * table_cols + c;
                    const char *text = flat < cell_count ? cells[flat] : NULL;
                    struct yetty_ycore_void_result cell_res =
                        yetty_yrich_paragraph_set_table_cell(paragraph_obj, r, c, text);
                    if (YETTY_IS_ERR(cell_res)) {
                        yetty_ycore_error_destroy(cell_res.error);
                    }
                }
            }
        } else {
            yetty_ycore_error_destroy(table_res.error);
        }
    }
    for (size_t i = 0; i < cell_count; i++) {
        free(cells[i]);
    }
    free(cells);
    cells = NULL;
    cell_count = 0;
    if (list_level > 0) {
        struct yetty_ycore_void_result level_res =
            yetty_yrich_paragraph_set_list_level(paragraph_obj, list_level);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, level_res, "yrich yaml: set_list_level failed");
    }
    if (space_before > 0.0f) {
        struct yetty_ycore_void_result before_res =
            yetty_yrich_paragraph_set_space_before(paragraph_obj, space_before);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, before_res, "yrich yaml: set_space_before failed");
    }
    if (space_after > 0.0f) {
        struct yetty_ycore_void_result after_res =
            yetty_yrich_paragraph_set_space_after(paragraph_obj, space_after);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, after_res, "yrich yaml: set_space_after failed");
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
            free_parsed_runs(runs, run_count);
            return YETTY_ERR(yetty_ycore_void, "yrich yaml: set_format failed", format_res);
        }
    }
    if (have_align) {
        struct yetty_ycore_void_result align_res =
            yetty_yrich_paragraph_set_alignment(paragraph_obj, align);
        if (YETTY_IS_ERR(align_res)) {
            free_parsed_runs(runs, run_count);
            return YETTY_ERR(yetty_ycore_void, "yrich yaml: set_alignment failed", align_res);
        }
    }
    for (size_t i = 0; i < run_count; i++) {
        struct yetty_ycore_void_result run_res =
            yetty_yrich_paragraph_add_run(paragraph_obj, runs[i].start, runs[i].end, runs[i].format,
                                          runs[i].color, runs[i].bg_color, runs[i].font_size);
        if (YETTY_IS_ERR(run_res)) {
            free_parsed_runs(runs, run_count);
            return YETTY_ERR(yetty_ycore_void, "yrich yaml: add_run failed", run_res);
        }
    }
    /* Hyperlinks are applied after the runs exist: interning the URL in the
     * document link table and stamping the run's byte span. */
    for (size_t i = 0; i < run_count; i++) {
        if (!runs[i].link || runs[i].link[0] == '\0') {
            continue;
        }
        struct yetty_ycore_void_result link_res = yetty_yrich_ydoc_apply_run_link(
            doc_obj, paragraph_obj, runs[i].start, runs[i].end, runs[i].link);
        if (YETTY_IS_ERR(link_res)) {
            free_parsed_runs(runs, run_count);
            return YETTY_ERR(yetty_ycore_void, "yrich yaml: apply_run_link failed", link_res);
        }
    }
    free_parsed_runs(runs, run_count);
    return YETTY_OK_VOID();

err:
    free(text);
    free(bookmark);
    free_parsed_runs(runs, run_count);
    for (size_t i = 0; i < cell_count; i++) {
        free(cells[i]);
    }
    free(cells);
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

/* Parse the `images:` sequence — each mapping is {source, x, y, w, h}. Each
 * becomes an inline image on the document. */
static struct yetty_ycore_void_result parse_ydoc_images(struct yaml_parser_s *p,
                                                        struct yetty_yclass_object *doc_obj)
{
    yaml_event_t ev;
    struct yetty_ycore_void_result ev_res = next_event(p, &ev);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ev_res, "yrich yaml: images read failed");
    if (ev.type != YAML_SEQUENCE_START_EVENT) {
        yaml_event_delete(&ev);
        return YETTY_ERR(yetty_ycore_void, "yrich yaml: images expected sequence");
    }
    yaml_event_delete(&ev);

    for (;;) {
        ev_res = next_event(p, &ev);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, ev_res, "yrich yaml: images read failed");
        if (ev.type == YAML_SEQUENCE_END_EVENT) {
            yaml_event_delete(&ev);
            return YETTY_OK_VOID();
        }
        if (ev.type != YAML_MAPPING_START_EVENT) {
            yaml_event_delete(&ev);
            return YETTY_ERR(yetty_ycore_void, "yrich yaml: image expected mapping");
        }
        yaml_event_delete(&ev);

        char *source = NULL;
        float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
        for (;;) {
            ev_res = next_event(p, &ev);
            if (YETTY_IS_ERR(ev_res)) {
                free(source);
                return YETTY_ERR(yetty_ycore_void, "yrich yaml: image read failed", ev_res);
            }
            if (ev.type == YAML_MAPPING_END_EVENT) {
                yaml_event_delete(&ev);
                break;
            }
            bool key_source = scalar_eq(&ev, "source");
            bool key_x = scalar_eq(&ev, "x");
            bool key_y = scalar_eq(&ev, "y");
            bool key_w = scalar_eq(&ev, "w");
            bool key_h = scalar_eq(&ev, "h");
            yaml_event_delete(&ev);
            ev_res = next_event(p, &ev);
            if (YETTY_IS_ERR(ev_res)) {
                free(source);
                return YETTY_ERR(yetty_ycore_void, "yrich yaml: image value read failed", ev_res);
            }
            if (ev.type == YAML_SCALAR_EVENT) {
                if (key_source) {
                    free(source);
                    source = scalar_dup(&ev);
                } else if (key_x) {
                    x = (float)scalar_to_d(&ev);
                } else if (key_y) {
                    y = (float)scalar_to_d(&ev);
                } else if (key_w) {
                    w = (float)scalar_to_d(&ev);
                } else if (key_h) {
                    h = (float)scalar_to_d(&ev);
                }
            }
            yaml_event_delete(&ev);
        }

        struct yetty_yclass_object_ptr_result image_res =
            yetty_yrich_ydoc_insert_image(doc_obj, -1, w, h);
        if (YETTY_IS_ERR(image_res)) {
            free(source);
            return YETTY_ERR(yetty_ycore_void, "yrich yaml: insert_image failed", image_res);
        }
        struct yetty_ycore_void_result bounds_res =
            yetty_yrich_inline_image_set_bounds(image_res.value, x, y, w, h);
        if (YETTY_IS_ERR(bounds_res)) {
            yetty_ycore_error_destroy(bounds_res.error);
        }
        if (source) {
            struct yetty_ycore_void_result source_res =
                yetty_yrich_inline_image_set_source(image_res.value, source);
            if (YETTY_IS_ERR(source_res)) {
                yetty_ycore_error_destroy(source_res.error);
            }
        }
        free(source);
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
        bool key_version = scalar_eq(&ev, "version");
        bool key_pp = scalar_eq(&ev, "paragraphs");
        bool key_images = scalar_eq(&ev, "images");
        yaml_event_delete(&ev);

        if (key_pp) {
            struct yetty_ycore_void_result paragraphs_res = parse_ydoc_paragraphs(p, doc_obj);
            if (YETTY_IS_ERR(paragraphs_res)) {
                return paragraphs_res;
            }
            continue;
        }
        if (key_images) {
            struct yetty_ycore_void_result images_res = parse_ydoc_images(p, doc_obj);
            if (YETTY_IS_ERR(images_res)) {
                return images_res;
            }
            continue;
        }
        ev_res = next_event(p, &ev);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, ev_res, "yrich yaml: document read failed");
        if (key_version && ev.type == YAML_SCALAR_EVENT) {
            uint32_t version = (uint32_t)scalar_to_l(&ev);
            if (version > YETTY_YRICH_YDOC_SCHEMA_VERSION) {
                yaml_event_delete(&ev);
                return YETTY_ERR(yetty_ycore_void,
                                 "yrich yaml: document schema version is newer than supported");
            }
            /* version < current: no incompatible change yet, so the load is a
             * no-op migration. Future bumps add cases in
             * migrate_ydoc_document(). */
        } else if (key_pw && ev.type == YAML_SCALAR_EVENT) {
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

    /* Atomic save: write to a sibling temp file, flush + fsync it, then
     * atomically rename over the destination. A crash, disk error, or emit
     * failure therefore never truncates an existing document — the original
     * file survives untouched. */
    size_t path_len = strlen(path);
    char *tmp_path = malloc(path_len + 5); /* ".tmp" + NUL */
    if (!tmp_path) {
        return YETTY_ERR(yetty_ycore_void, "ydoc save: temp path alloc failed");
    }
    memcpy(tmp_path, path, path_len);
    memcpy(tmp_path + path_len, ".tmp", 5);

    FILE *file = fopen(tmp_path, "wb");
    if (!file) {
        free(tmp_path);
        return YETTY_ERR(yetty_ycore_void, "ydoc save: cannot open temp file for writing");
    }

    struct yaml_emitter_s emitter;
    if (!yaml_emitter_initialize(&emitter)) {
        fclose(file);
        remove(tmp_path);
        free(tmp_path);
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
    ok = ok && emit_key_uint(&emitter, "version", YETTY_YRICH_YDOC_SCHEMA_VERSION);
    ok = ok && emit_key_float(&emitter, "pageWidth", page_width_res.value);
    ok = ok && emit_key_float(&emitter, "margin", margin_res.value);
    ok = ok && emit_plain_scalar(&emitter, "paragraphs");
    if (ok) {
        ok = yaml_sequence_start_event_initialize(&event, NULL, NULL, 1,
                                                  YAML_BLOCK_SEQUENCE_STYLE) &&
             emit_event(&emitter, &event);
    }
    for (size_t i = 0; ok && i < count_res.value; i++) {
        struct yetty_yclass_object_ptr_result paragraph_res =
            yetty_yrich_ydoc_paragraph_at(doc_obj, (int32_t)i);
        if (YETTY_IS_ERR(paragraph_res)) {
            yetty_ycore_error_destroy(paragraph_res.error);
            ok = 0;
            break;
        }
        struct yetty_yclass_object *paragraph_obj = paragraph_res.value;
        if (!paragraph_obj) {
            ok = 0;
            break;
        }
        struct yetty_ycore_const_char_ptr_result text_res =
            yetty_yrich_paragraph_text(paragraph_obj);
        struct yetty_ycore_size_result text_len_res = yetty_yrich_paragraph_text_len(paragraph_obj);
        struct yetty_ycore_float_result font_size_res =
            yetty_yrich_paragraph_font_size(paragraph_obj);
        struct yetty_ycore_uint32_result color_res = yetty_yrich_paragraph_color(paragraph_obj);
        struct yetty_ycore_uint32_result format_res = yetty_yrich_paragraph_format(paragraph_obj);
        if (YETTY_IS_ERR(text_res) || YETTY_IS_ERR(text_len_res) || YETTY_IS_ERR(font_size_res) ||
            YETTY_IS_ERR(color_res) || YETTY_IS_ERR(format_res)) {
            if (YETTY_IS_ERR(text_res)) {
                yetty_ycore_error_destroy(text_res.error);
            }
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
             emit_quoted_scalar(&emitter, text_res.value, text_len_res.value);
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
        struct yetty_ycore_float_result spacing_res =
            yetty_yrich_paragraph_line_spacing(paragraph_obj);
        if (YETTY_IS_ERR(spacing_res)) {
            yetty_ycore_error_destroy(spacing_res.error);
            ok = 0;
        } else {
            ok = ok && emit_key_float(&emitter, "lineSpacing", spacing_res.value);
        }
        struct yetty_ycore_float_result indent_res = yetty_yrich_paragraph_indent(paragraph_obj);
        if (YETTY_IS_ERR(indent_res)) {
            yetty_ycore_error_destroy(indent_res.error);
            ok = 0;
        } else if (indent_res.value > 0.0f) {
            ok = ok && emit_key_float(&emitter, "indent", indent_res.value);
        }
        struct yetty_ycore_uint32_result heading_res =
            yetty_yrich_paragraph_heading_level(paragraph_obj);
        if (YETTY_IS_ERR(heading_res)) {
            yetty_ycore_error_destroy(heading_res.error);
            ok = 0;
        } else if (heading_res.value > 0) {
            ok = ok && emit_key_uint(&emitter, "heading", heading_res.value);
        }
        struct yetty_ycore_uint32_result list_kind_res =
            yetty_yrich_paragraph_list_kind(paragraph_obj);
        if (YETTY_IS_ERR(list_kind_res)) {
            yetty_ycore_error_destroy(list_kind_res.error);
            ok = 0;
        } else if (list_kind_res.value > 0) {
            ok = ok && emit_key_uint(&emitter, "list", list_kind_res.value);
            struct yetty_ycore_uint32_result checked_res =
                yetty_yrich_paragraph_list_checked(paragraph_obj);
            if (YETTY_IS_ERR(checked_res)) {
                yetty_ycore_error_destroy(checked_res.error);
                ok = 0;
            } else if (checked_res.value) {
                ok = ok && emit_key_uint(&emitter, "checked", checked_res.value);
            }
        }
        struct yetty_ycore_uint32_result block_kind_res =
            yetty_yrich_paragraph_block_kind(paragraph_obj);
        if (YETTY_IS_ERR(block_kind_res)) {
            yetty_ycore_error_destroy(block_kind_res.error);
            ok = 0;
        } else if (block_kind_res.value > 0) {
            ok = ok && emit_key_uint(&emitter, "block", block_kind_res.value);
            if (block_kind_res.value == 2) {
                uint32_t rows = 0;
                uint32_t cols = 0;
                struct yetty_ycore_void_result size_res =
                    yetty_yrich_paragraph_table_size(paragraph_obj, &rows, &cols);
                if (YETTY_IS_ERR(size_res)) {
                    yetty_ycore_error_destroy(size_res.error);
                    ok = 0;
                } else {
                    ok = ok && emit_key_uint(&emitter, "tableRows", rows);
                    ok = ok && emit_key_uint(&emitter, "tableCols", cols);
                    ok = ok && emit_plain_scalar(&emitter, "cells");
                    if (ok) {
                        ok = yaml_sequence_start_event_initialize(&event, NULL, NULL, 1,
                                                                  YAML_BLOCK_SEQUENCE_STYLE) &&
                             emit_event(&emitter, &event);
                    }
                    for (uint32_t r = 0; ok && r < rows; r++) {
                        for (uint32_t c = 0; ok && c < cols; c++) {
                            struct yetty_ycore_const_char_ptr_result cell_res =
                                yetty_yrich_paragraph_table_cell(paragraph_obj, r, c);
                            const char *cell = "";
                            if (YETTY_IS_OK(cell_res) && cell_res.value) {
                                cell = cell_res.value;
                            } else if (YETTY_IS_ERR(cell_res)) {
                                yetty_ycore_error_destroy(cell_res.error);
                            }
                            ok = ok && emit_quoted_scalar(&emitter, cell, strlen(cell));
                        }
                    }
                    if (ok) {
                        ok = yaml_sequence_end_event_initialize(&event) &&
                             emit_event(&emitter, &event);
                    }
                }
            }
        }
        struct yetty_ycore_float_result space_before_res =
            yetty_yrich_paragraph_space_before(paragraph_obj);
        if (YETTY_IS_ERR(space_before_res)) {
            yetty_ycore_error_destroy(space_before_res.error);
            ok = 0;
        } else if (space_before_res.value > 0.0f) {
            ok = ok && emit_key_float(&emitter, "spaceBefore", space_before_res.value);
        }
        struct yetty_ycore_float_result space_after_res =
            yetty_yrich_paragraph_space_after(paragraph_obj);
        if (YETTY_IS_ERR(space_after_res)) {
            yetty_ycore_error_destroy(space_after_res.error);
            ok = 0;
        } else if (space_after_res.value > 0.0f) {
            ok = ok && emit_key_float(&emitter, "spaceAfter", space_after_res.value);
        }
        struct yetty_ycore_uint32_result list_level_res =
            yetty_yrich_paragraph_list_level(paragraph_obj);
        if (YETTY_IS_ERR(list_level_res)) {
            yetty_ycore_error_destroy(list_level_res.error);
            ok = 0;
        } else if (list_level_res.value > 0) {
            ok = ok && emit_key_uint(&emitter, "listLevel", list_level_res.value);
        }
        struct yetty_ycore_const_char_ptr_result bookmark_res =
            yetty_yrich_paragraph_bookmark(paragraph_obj);
        if (YETTY_IS_ERR(bookmark_res)) {
            yetty_ycore_error_destroy(bookmark_res.error);
        } else if (bookmark_res.value && bookmark_res.value[0] != '\0') {
            ok = ok && emit_plain_scalar(&emitter, "bookmark") &&
                 emit_quoted_scalar(&emitter, bookmark_res.value, strlen(bookmark_res.value));
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
                uint32_t run_bg = 0;
                float run_fs = 0.0f;
                struct yetty_ycore_void_result run_res =
                    yetty_yrich_paragraph_run_get(paragraph_obj, run_index, &run_start, &run_end,
                                                  &run_format, &run_color, &run_bg, &run_fs);
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
                if (run_bg != YETTY_YRICH_COLOR_TRANSPARENT) {
                    ok = ok && emit_key_color(&emitter, "bg", run_bg);
                }
                if (run_fs > 0.0f) {
                    ok = ok && emit_key_float(&emitter, "fs", run_fs);
                }
                /* Hyperlink: denormalize the run's link id to its URL (the id
                 * table itself is runtime-only and never serialized). */
                struct yetty_ycore_uint32_result link_id_res =
                    yetty_yrich_paragraph_run_link_id(paragraph_obj, run_index);
                if (YETTY_IS_ERR(link_id_res)) {
                    yetty_ycore_error_destroy(link_id_res.error);
                } else if (link_id_res.value != 0) {
                    struct yetty_ycore_const_char_ptr_result url_res =
                        yetty_yrich_ydoc_link_url(doc_obj, link_id_res.value);
                    if (YETTY_IS_ERR(url_res)) {
                        yetty_ycore_error_destroy(url_res.error);
                    } else if (url_res.value) {
                        ok = ok && emit_plain_scalar(&emitter, "link") &&
                             emit_quoted_scalar(&emitter, url_res.value, strlen(url_res.value));
                    }
                }
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

    /* Inline images — an `images:` sequence of {source, x, y, w, h}. */
    struct yetty_ycore_size_result image_count_res = yetty_yrich_ydoc_image_count(doc_obj);
    if (ok && YETTY_IS_OK(image_count_res) && image_count_res.value > 0) {
        ok = emit_plain_scalar(&emitter, "images");
        if (ok) {
            ok = yaml_sequence_start_event_initialize(&event, NULL, NULL, 1,
                                                      YAML_BLOCK_SEQUENCE_STYLE) &&
                 emit_event(&emitter, &event);
        }
        for (size_t image_index = 0; ok && image_index < image_count_res.value; image_index++) {
            struct yetty_yclass_object_ptr_result image_res =
                yetty_yrich_ydoc_image_at(doc_obj, (int32_t)image_index);
            if (YETTY_IS_ERR(image_res) || !image_res.value) {
                if (YETTY_IS_ERR(image_res)) {
                    yetty_ycore_error_destroy(image_res.error);
                }
                continue;
            }
            struct yetty_ycore_const_char_ptr_result source_res =
                yetty_yrich_inline_image_source(image_res.value);
            float image_x = 0.0f, image_y = 0.0f, image_w = 0.0f, image_h = 0.0f;
            struct yetty_ycore_void_result bounds_res = yetty_yrich_inline_image_bounds(
                image_res.value, &image_x, &image_y, &image_w, &image_h);
            if (YETTY_IS_ERR(bounds_res)) {
                yetty_ycore_error_destroy(bounds_res.error);
            }
            ok = ok && emit_mapping_start(&emitter);
            if (YETTY_IS_OK(source_res) && source_res.value) {
                ok = ok && emit_plain_scalar(&emitter, "source") &&
                     emit_quoted_scalar(&emitter, source_res.value, strlen(source_res.value));
            } else if (YETTY_IS_ERR(source_res)) {
                yetty_ycore_error_destroy(source_res.error);
            }
            ok = ok && emit_key_float(&emitter, "x", image_x);
            ok = ok && emit_key_float(&emitter, "y", image_y);
            ok = ok && emit_key_float(&emitter, "w", image_w);
            ok = ok && emit_key_float(&emitter, "h", image_h);
            ok = ok && emit_mapping_end(&emitter);
        }
        if (ok) {
            ok = yaml_sequence_end_event_initialize(&event) && emit_event(&emitter, &event);
        }
    } else if (YETTY_IS_ERR(image_count_res)) {
        yetty_ycore_error_destroy(image_count_res.error);
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

    /* Durably flush the temp file before the rename so a crash between
     * rename and writeback cannot leave a zero-length document. */
    if (ok && fflush(file) != 0) {
        ok = 0;
    }
    if (ok) {
#if defined(_WIN32)
        if (_commit(_fileno(file)) != 0) {
            ok = 0;
        }
#else
        if (fsync(fileno(file)) != 0) {
            ok = 0;
        }
#endif
    }
    if (fclose(file) != 0) {
        ok = 0;
    }

    if (!ok) {
        /* Emit/flush failed — discard the temp; the destination is untouched. */
        remove(tmp_path);
        free(tmp_path);
        return YETTY_ERR(yetty_ycore_void, "ydoc save: yaml emit failed");
    }

    /* Atomically replace the destination with the fully-written temp. */
#if defined(_WIN32)
    if (!MoveFileExA(tmp_path, path, MOVEFILE_REPLACE_EXISTING)) {
        remove(tmp_path);
        free(tmp_path);
        return YETTY_ERR(yetty_ycore_void, "ydoc save: atomic replace failed");
    }
#else
    if (rename(tmp_path, path) != 0) {
        remove(tmp_path);
        free(tmp_path);
        return YETTY_ERR(yetty_ycore_void, "ydoc save: atomic rename failed");
    }
#endif
    free(tmp_path);
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
