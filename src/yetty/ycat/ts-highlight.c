/*
 * ts-highlight.c - tree-sitter → colored ydraw buffer.
 *
 * Parses source with the matched grammar, runs the embedded highlights.scm
 * query, paints a per-byte color map (width-sorted so that narrower / more
 * specific captures overwrite wider ones), then emits one text span per
 * maximal same-color run per line.
 *
 * Layout is monospace: x advances 0.6 * font_size per byte (matching the
 * approximation used by ymarkdown), y advances by font_size * line_spacing
 * per '\n'. The goal is reasonable visual fidelity, not exact font metrics.
 */

#include <yetty/ycat/ycat.h>
#include "ts-grammars.h"

#include <yetty/ydraw-core/draw-list.h>
#include <yetty/ycore/types.h>
#include <yetty/ytrace/ytrace.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef YETTY_YCAT_HAS_TREESITTER
#include <tree_sitter/api.h>
#endif

/*=============================================================================
 * Theme — capture name → color (0xAABBGGRR, matches ydraw text span)
 *===========================================================================*/

/* Theme colours, packed as ydraw text-span format 0xAABBGGRR.
 * R/G/B are unpacked on the fly for 24-bit true-colour SGR
 * (ESC[38;2;R;G;Bm) when emitting for a non-yetty terminal. */

#define TS_COLOR_DEFAULT 0xFFD0D0D0u
#define TS_COLOR_KEYWORD 0xFFAF87AFu
#define TS_COLOR_STRING 0xFF87AF87u
#define TS_COLOR_NUMBER 0xFF5F87FFu
#define TS_COLOR_COMMENT 0xFF8A8A8Au
#define TS_COLOR_FUNCTION 0xFFFFAF5Fu
#define TS_COLOR_TYPE 0xFF87D7D7u
#define TS_COLOR_FIELD 0xFFFFD787u
#define TS_COLOR_PUNCT 0xFFB8B8B8u
#define TS_COLOR_LINE_NUM 0xFF4E4E4Eu

static uint32_t theme_color(const char *capname, size_t caplen)
{
    /* Strip leading '@' */
    if (caplen > 0 && capname[0] == '@') {
        capname++;
        caplen--;
    }

    /* Match on prefix since captures use dot-separated hierarchy:
	 * "keyword.return" → "keyword", etc. */
#define PREFIX(s, p) (caplen >= sizeof(p) - 1 && memcmp(s, p, sizeof(p) - 1) == 0)

    if (PREFIX(capname, "keyword")) {
        return TS_COLOR_KEYWORD;
    }
    if (PREFIX(capname, "include")) {
        return TS_COLOR_KEYWORD;
    }
    if (PREFIX(capname, "label")) {
        return TS_COLOR_KEYWORD;
    }

    if (PREFIX(capname, "string")) {
        return TS_COLOR_STRING;
    }
    if (PREFIX(capname, "character")) {
        return TS_COLOR_STRING;
    }

    if (PREFIX(capname, "number")) {
        return TS_COLOR_NUMBER;
    }
    if (PREFIX(capname, "float")) {
        return TS_COLOR_NUMBER;
    }
    if (PREFIX(capname, "boolean")) {
        return TS_COLOR_NUMBER;
    }
    if (PREFIX(capname, "constant")) {
        return TS_COLOR_NUMBER;
    }
    if (PREFIX(capname, "escape")) {
        return TS_COLOR_NUMBER;
    }

    if (PREFIX(capname, "comment")) {
        return TS_COLOR_COMMENT;
    }

    if (PREFIX(capname, "function")) {
        return TS_COLOR_FUNCTION;
    }
    if (PREFIX(capname, "method")) {
        return TS_COLOR_FUNCTION;
    }

    if (PREFIX(capname, "type")) {
        return TS_COLOR_TYPE;
    }
    if (PREFIX(capname, "namespace")) {
        return TS_COLOR_TYPE;
    }
    if (PREFIX(capname, "module")) {
        return TS_COLOR_TYPE;
    }
    if (PREFIX(capname, "constructor")) {
        return TS_COLOR_TYPE;
    }
    if (PREFIX(capname, "attribute")) {
        return TS_COLOR_TYPE;
    }

    if (PREFIX(capname, "property")) {
        return TS_COLOR_FIELD;
    }
    if (PREFIX(capname, "field")) {
        return TS_COLOR_FIELD;
    }
    if (PREFIX(capname, "variable.member")) {
        return TS_COLOR_FIELD;
    }

    if (PREFIX(capname, "punctuation")) {
        return TS_COLOR_PUNCT;
    }
    if (PREFIX(capname, "operator")) {
        return TS_COLOR_PUNCT;
    }

    if (PREFIX(capname, "tag")) {
        return TS_COLOR_FUNCTION;
    }

    return 0; /* no match — don't override default */
#undef PREFIX
}

/*=============================================================================
 * Handler (enabled only when tree-sitter is compiled in)
 *===========================================================================*/

#ifdef YETTY_YCAT_HAS_TREESITTER

struct yetty_ycat_span {
    uint32_t start;
    uint32_t end;
    uint32_t pattern_idx;
    uint32_t color;
};

static int span_cmp(const void *a, const void *b)
{
    const struct yetty_ycat_span *x = a, *y = b;
    uint32_t wa = x->end - x->start;
    uint32_t wb = y->end - y->start;
    /* Wider first → narrower overwrites later in the paint pass. */
    if (wa != wb) {
        return (wa > wb) ? -1 : 1;
    }
    /* Same width: lower pattern index first. */
    if (x->pattern_idx != y->pattern_idx) {
        return (x->pattern_idx < y->pattern_idx) ? -1 : 1;
    }
    return 0;
}

static uint32_t *compute_color_map(const TSLanguage *lang, const char *query_str,
                                   uint32_t query_len, TSTree *tree, const char *source,
                                   uint32_t source_len)
{
    uint32_t *color_map = calloc(source_len ? source_len : 1, sizeof(*color_map));
    if (!color_map) {
        return NULL;
    }

    uint32_t err_off = 0;
    TSQueryError err_type = TSQueryErrorNone;
    TSQuery *query = ts_query_new(lang, query_str, query_len, &err_off, &err_type);
    if (!query) {
        ydebug("ts query compile err type=%d offset=%u", err_type, err_off);
        return color_map; /* empty map, will use default color */
    }

    TSQueryCursor *cursor = ts_query_cursor_new();
    ts_query_cursor_exec(cursor, query, ts_tree_root_node(tree));

    size_t span_cap = 256, span_count = 0;
    struct yetty_ycat_span *spans = malloc(span_cap * sizeof(*spans));
    if (!spans) {
        ts_query_cursor_delete(cursor);
        ts_query_delete(query);
        return color_map;
    }

    TSQueryMatch match;
    while (ts_query_cursor_next_match(cursor, &match)) {
        for (uint16_t i = 0; i < match.capture_count; i++) {
            const TSQueryCapture *cap = &match.captures[i];
            uint32_t nlen = 0;
            const char *nm = ts_query_capture_name_for_id(query, cap->index, &nlen);
            uint32_t c = theme_color(nm, nlen);
            if (c == 0) {
                continue;
            }
            uint32_t s = ts_node_start_byte(cap->node);
            uint32_t e = ts_node_end_byte(cap->node);
            if (s >= source_len || e > source_len) {
                continue;
            }

            if (span_count == span_cap) {
                span_cap *= 2;
                struct yetty_ycat_span *ns = realloc(spans, span_cap * sizeof(*ns));
                if (!ns) {
                    break;
                }
                spans = ns;
            }
            spans[span_count++] = (struct yetty_ycat_span){
                .start = s,
                .end = e,
                .pattern_idx = match.pattern_index,
                .color = c,
            };
        }
    }

    ts_query_cursor_delete(cursor);
    ts_query_delete(query);

    qsort(spans, span_count, sizeof(*spans), span_cmp);
    for (size_t si = 0; si < span_count; si++) {
        const struct yetty_ycat_span *sp = &spans[si];
        for (uint32_t i = sp->start; i < sp->end; i++) {
            color_map[i] = sp->color;
        }
    }

    free(spans);
    (void)source;
    return color_map;
}

/*=============================================================================
 * Emit per-line, per-color runs as text spans
 *===========================================================================*/

static struct yetty_ycore_void_result emit_runs(struct yetty_ydraw_core_draw_list *buf,
                                                const char *source, uint32_t source_len,
                                                const uint32_t *color_map, float font_size,
                                                float line_spacing)
{
    float cursor_x = 2.0f;
    float cursor_y = 2.0f;
    float advance_x = font_size * 0.6f;
    float advance_y = font_size * line_spacing;

    uint32_t line_start = 0;
    for (uint32_t i = 0; i <= source_len; i++) {
        if (i == source_len || source[i] == '\n') {
            /* Flush line [line_start .. i) as same-color runs. */
            uint32_t p = line_start;
            float x = cursor_x;
            while (p < i) {
                uint32_t c = color_map[p];
                uint32_t q = p + 1;
                while (q < i && color_map[q] == c && source[q] != '\t') {
                    q++;
                }

                uint32_t run_len = q - p;
                if (source[p] == '\t') {
                    x += advance_x * 4; /* tab = 4 cols */
                    p = q;
                    continue;
                }

                struct yetty_ycore_buffer text = {
                    .data = (uint8_t *)(uintptr_t)(source + p),
                    .size = run_len,
                    .capacity = run_len,
                };
                uint32_t col = c ? c : TS_COLOR_DEFAULT;
                struct yetty_ycore_void_result r = yetty_ydraw_core_draw_list_add_text(
                    buf, x, cursor_y, &text, font_size, col, 0, -1, 0.0f);
                if (YETTY_IS_ERR(r)) {
                    return r;
                }

                x += advance_x * (float)run_len;
                p = q;
            }

            cursor_y += advance_y;
            line_start = i + 1;
        }
    }
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Shared parse + color-map helper
 *===========================================================================*/

/* Parse `bytes` as `grammar_name` and build a per-byte color map.
 * Caller owns both the returned color_map (free()) and the returned
 * tree/parser (via the out-params — pass them back to ts_parse_done()).
 * Returns NULL on failure with a static error msg in *err_msg. */
static uint32_t *ts_parse_and_map(const uint8_t *bytes, size_t len, const char *grammar_name,
                                  TSParser **parser_out, TSTree **tree_out, const char **err_msg)
{
    *parser_out = NULL;
    *tree_out = NULL;

    const struct yetty_ycat_grammar *g = yetty_ycat_grammar_get(grammar_name);
    if (!g || !g->language_fn) {
        *err_msg = "unknown tree-sitter grammar";
        return NULL;
    }
    const TSLanguage *lang = g->language_fn();
    if (!lang) {
        *err_msg = "grammar language fn returned NULL";
        return NULL;
    }

    TSParser *parser = ts_parser_new();
    if (!ts_parser_set_language(parser, lang)) {
        ts_parser_delete(parser);
        *err_msg = "ts_parser_set_language failed";
        return NULL;
    }
    TSTree *tree = ts_parser_parse_string(parser, NULL, (const char *)bytes, (uint32_t)len);
    if (!tree) {
        ts_parser_delete(parser);
        *err_msg = "ts_parser_parse_string failed";
        return NULL;
    }

    uint32_t *color_map =
        compute_color_map(lang, g->highlights_scm, (uint32_t)g->highlights_scm_len, tree,
                          (const char *)bytes, (uint32_t)len);

    *parser_out = parser;
    *tree_out = tree;
    return color_map;
}

static void ts_parse_done(TSParser *parser, TSTree *tree, uint32_t *color_map)
{
    free(color_map);
    if (tree) {
        ts_tree_delete(tree);
    }
    if (parser) {
        ts_parser_delete(parser);
    }
}

/*=============================================================================
 * Emitter #1 — ydraw buffer (for in-yetty consumers)
 *===========================================================================*/

struct yetty_ydraw_core_draw_list_result yetty_ycat_ts_render(const uint8_t *bytes, size_t len,
                                                            const char *grammar_name,
                                                            const struct yetty_ycat_config *config)
{
    TSParser *parser = NULL;
    TSTree *tree = NULL;
    const char *err = NULL;
    uint32_t *color_map = ts_parse_and_map(bytes, len, grammar_name, &parser, &tree, &err);
    if (!color_map && err) {
        return YETTY_ERR(yetty_ydraw_core_draw_list, err);
    }

    float font_size = config && config->cell_height > 0 ? (float)config->cell_height : 14.0f;

    float scene_w = 0.0f, scene_h = 0.0f;
    if (config) {
        scene_w = (float)(config->width_cells * config->cell_width);
        scene_h = (float)(config->height_cells * config->cell_height);
    }
    struct yetty_ydraw_core_draw_list_config bcfg = {
        .scene_min_x = 0.0f,
        .scene_min_y = 0.0f,
        .scene_max_x = scene_w,
        .scene_max_y = scene_h,
    };
    struct yetty_ydraw_core_draw_list_result br =
        yetty_ydraw_core_draw_list_config_buffer_create(&bcfg);
    if (YETTY_IS_ERR(br)) {
        ts_parse_done(parser, tree, color_map);
        return br;
    }

    struct yetty_ycore_void_result er =
        emit_runs(br.value, (const char *)bytes, (uint32_t)len, color_map, font_size, 1.2f);
    ts_parse_done(parser, tree, color_map);

    if (YETTY_IS_ERR(er)) {
        yetty_ydraw_core_draw_list_destroy(br.value);
        return YETTY_ERR(yetty_ydraw_core_draw_list, "ts-highlight: emit_runs failed", er);
    }
    return br;
}

/*=============================================================================
 * Emitter #2 — ANSI SGR true-color to a FILE* (for any terminal)
 *
 * For each byte of input, look up its colour in the color map. When the colour
 * changes, emit `ESC[38;2;R;G;Bm`. Reset at newlines (to prevent bleed into
 * subsequent scrollback) and at end of file.
 *===========================================================================*/

#define SGR_RESET "\033[0m"

static int emit_color(FILE *out, uint32_t color)
{
    uint8_t r = color & 0xFFu;
    uint8_t g = (color >> 8) & 0xFFu;
    uint8_t b = (color >> 16) & 0xFFu;
    return fprintf(out, "\033[38;2;%u;%u;%um", r, g, b);
}

int yetty_ycat_ts_emit_sgr(const uint8_t *bytes, size_t len, const char *grammar_name, FILE *out)
{
    if (!out) {
        return -1;
    }

    TSParser *parser = NULL;
    TSTree *tree = NULL;
    const char *err = NULL;
    uint32_t *color_map = ts_parse_and_map(bytes, len, grammar_name, &parser, &tree, &err);
    if (!color_map) {
        return -1;
    }

    uint32_t active = 0; /* 0 = none emitted */
    for (size_t i = 0; i < len; i++) {
        char c = (char)bytes[i];
        if (c == '\n') {
            if (active) {
                fputs(SGR_RESET, out);
                active = 0;
            }
            fputc('\n', out);
            continue;
        }
        uint32_t col = color_map[i] ? color_map[i] : TS_COLOR_DEFAULT;
        if (col != active) {
            if (active) {
                fputs(SGR_RESET, out);
            }
            if (emit_color(out, col) < 0) {
                ts_parse_done(parser, tree, color_map);
                return -1;
            }
            active = col;
        }
        if (fputc(c, out) == EOF) {
            ts_parse_done(parser, tree, color_map);
            return -1;
        }
    }
    if (active) {
        fputs(SGR_RESET, out);
    }

    ts_parse_done(parser, tree, color_map);
    return 0;
}

#else /* !YETTY_YCAT_HAS_TREESITTER */

struct yetty_ydraw_core_draw_list_result yetty_ycat_ts_render(const uint8_t *bytes, size_t len,
                                                            const char *grammar_name,
                                                            const struct yetty_ycat_config *config)
{
    (void)bytes;
    (void)len;
    (void)grammar_name;
    (void)config;
    return YETTY_ERR(yetty_ydraw_core_draw_list, "tree-sitter not compiled in");
}

int yetty_ycat_ts_emit_sgr(const uint8_t *bytes, size_t len, const char *grammar_name, FILE *out)
{
    (void)bytes;
    (void)len;
    (void)grammar_name;
    (void)out;
    return -1;
}

#endif
