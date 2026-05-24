/*
 * ymarkdown.c - Markdown → ydraw buffer.
 *
 * Port of yetty-poc/src/yetty/cards/markdown/markdown.cpp stripped of the
 * Card/GPU lifecycle; the output is just a ydraw buffer with text spans and
 * SDF boxes for code-run backgrounds. The renderer uses the same geometric
 * conventions as the C++ version: layout starts at (2, 2), text advances by
 * 0.6 * font_size per character (proportional approximation), lines advance
 * by font_size * line_spacing.
 */

#include <yetty/ymarkdown/ymarkdown.h>

#include <yetty/ydraw-core/draw-list.h>
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/ysdf/types.gen.h>
#include <yetty/ycore/types.h>
#include <yetty/ytrace/ytrace.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*=============================================================================
 * Defaults (match markdown.cpp)
 *===========================================================================*/

#define YMD_DEFAULT_FONT_SIZE 14.0f
#define YMD_DEFAULT_LINE_SPACING 1.4f

#define YMD_COLOR_TEXT 0xFFE6E6E6u
#define YMD_COLOR_BOLD 0xFFFFFFFFu
#define YMD_COLOR_CODE 0xFF66CC99u
#define YMD_COLOR_HEADER 0xFFFFFFFFu
#define YMD_COLOR_CODE_BG 0xFF3D3D3Du

/*=============================================================================
 * Parse tree (internal)
 *===========================================================================*/

enum yetty_ymarkdown_ymd_style {
    YETTY_YMARKDOWN_YMD_REGULAR = 0,
    YETTY_YMARKDOWN_YMD_BOLD = 1,
    YETTY_YMARKDOWN_YMD_ITALIC = 2,
    YETTY_YMARKDOWN_YMD_BOLD_ITALIC = 3,
};

struct yetty_ymarkdown_ymd_span {
    const char *text; /* slice into owned line storage */
    size_t text_len;
    enum yetty_ymarkdown_ymd_style style;
    uint8_t header_level; /* 0 = normal */
    bool is_code;
    bool is_bullet;
};

struct yetty_ymarkdown_ymd_line {
    struct yetty_ymarkdown_ymd_span *spans;
    size_t span_count;
    size_t span_cap;
    float indent;
    float scale;

    /* Owned buffer that backs span text slices. Inline styles are stripped
	 * from the raw source line when copied here, so slices into this buffer
	 * never contain markdown markers. */
    char *raw;
    size_t raw_len;
};

struct yetty_ymarkdown_ymd_doc {
    struct yetty_ymarkdown_ymd_line *lines;
    size_t line_count;
    size_t line_cap;
};

static void ymd_line_destroy(struct yetty_ymarkdown_ymd_line *line)
{
    if (!line) {
        return;
    }
    free(line->spans);
    free(line->raw);
    line->spans = NULL;
    line->raw = NULL;
}

static void ymd_doc_destroy(struct yetty_ymarkdown_ymd_doc *doc)
{
    if (!doc) {
        return;
    }
    for (size_t i = 0; i < doc->line_count; i++) {
        ymd_line_destroy(&doc->lines[i]);
    }
    free(doc->lines);
    doc->lines = NULL;
    doc->line_count = 0;
    doc->line_cap = 0;
}

static int ymd_line_push_span(struct yetty_ymarkdown_ymd_line *line,
                              struct yetty_ymarkdown_ymd_span span)
{
    if (line->span_count == line->span_cap) {
        size_t new_cap = line->span_cap ? line->span_cap * 2 : 4;
        struct yetty_ymarkdown_ymd_span *ns = realloc(line->spans, new_cap * sizeof(*ns));
        if (!ns) {
            return -1;
        }
        line->spans = ns;
        line->span_cap = new_cap;
    }
    line->spans[line->span_count++] = span;
    return 0;
}

static int ymd_line_insert_span_front(struct yetty_ymarkdown_ymd_line *line,
                                      struct yetty_ymarkdown_ymd_span span)
{
    if (line->span_count == line->span_cap) {
        size_t new_cap = line->span_cap ? line->span_cap * 2 : 4;
        struct yetty_ymarkdown_ymd_span *ns = realloc(line->spans, new_cap * sizeof(*ns));
        if (!ns) {
            return -1;
        }
        line->spans = ns;
        line->span_cap = new_cap;
    }
    memmove(&line->spans[1], &line->spans[0], line->span_count * sizeof(line->spans[0]));
    line->spans[0] = span;
    line->span_count++;
    return 0;
}

static int ymd_doc_push_line(struct yetty_ymarkdown_ymd_doc *doc,
                             struct yetty_ymarkdown_ymd_line line)
{
    if (doc->line_count == doc->line_cap) {
        size_t new_cap = doc->line_cap ? doc->line_cap * 2 : 16;
        struct yetty_ymarkdown_ymd_line *nl = realloc(doc->lines, new_cap * sizeof(*nl));
        if (!nl) {
            return -1;
        }
        doc->lines = nl;
        doc->line_cap = new_cap;
    }
    doc->lines[doc->line_count++] = line;
    return 0;
}

/*=============================================================================
 * Arg parsing
 *===========================================================================*/

struct yetty_ymarkdown_ymd_params {
    float font_size;
    float line_spacing;
    bool user_font_size;
};

static void ymd_params_init(struct yetty_ymarkdown_ymd_params *p)
{
    p->font_size = YMD_DEFAULT_FONT_SIZE;
    p->line_spacing = YMD_DEFAULT_LINE_SPACING;
    p->user_font_size = false;
}

/* Parse a float prefix. Returns 1 on success, 0 on failure. */
static int ymd_parse_float(const char *s, size_t len, float *out)
{
    if (!s || len == 0) {
        return 0;
    }
    char buf[64];
    if (len >= sizeof(buf)) {
        len = sizeof(buf) - 1;
    }
    memcpy(buf, s, len);
    buf[len] = '\0';
    char *end = NULL;
    float v = strtof(buf, &end);
    if (end == buf) {
        return 0;
    }
    *out = v;
    return 1;
}

static bool starts_with(const char *s, size_t s_len, const char *prefix)
{
    size_t pl = strlen(prefix);
    if (s_len < pl) {
        return false;
    }
    return memcmp(s, prefix, pl) == 0;
}

static void ymd_parse_args(const char *args, size_t args_len, struct yetty_ymarkdown_ymd_params *p)
{
    if (!args || args_len == 0) {
        return;
    }

    size_t i = 0;
    while (i < args_len) {
        /* skip whitespace */
        while (i < args_len &&
               (args[i] == ' ' || args[i] == '\t' || args[i] == '\n' || args[i] == '\r')) {
            i++;
        }
        if (i >= args_len) {
            break;
        }
        size_t tok_start = i;
        while (i < args_len && args[i] != ' ' && args[i] != '\t' && args[i] != '\n' &&
               args[i] != '\r') {
            i++;
        }
        size_t tok_len = i - tok_start;
        const char *tok = args + tok_start;

        if (starts_with(tok, tok_len, "--font-size=")) {
            float v;
            if (ymd_parse_float(tok + 12, tok_len - 12, &v)) {
                p->font_size = v;
                p->user_font_size = true;
            }
        } else if (starts_with(tok, tok_len, "--line-spacing=")) {
            float v;
            if (ymd_parse_float(tok + 15, tok_len - 15, &v)) {
                p->line_spacing = v;
            }
        }
        /* unknown flags ignored */
    }
}

/*=============================================================================
 * Markdown parser
 *
 * Produces a ymd_doc. Each line owns a `raw` buffer with markdown markers
 * stripped out; spans point into that buffer.
 *===========================================================================*/

static bool match_fixed(const char *src, size_t src_len, size_t pos, const char *needle)
{
    size_t n = strlen(needle);
    if (pos + n > src_len) {
        return false;
    }
    return memcmp(src + pos, needle, n) == 0;
}

/* Find next occurrence of needle in [pos, src_len). Returns index or
 * SIZE_MAX. */
static size_t find_from(const char *src, size_t src_len, size_t pos, const char *needle)
{
    size_t n = strlen(needle);
    if (n == 0 || pos >= src_len || n > src_len - pos) {
        return (size_t)-1;
    }
    for (size_t i = pos; i + n <= src_len; i++) {
        if (memcmp(src + i, needle, n) == 0) {
            return i;
        }
    }
    return (size_t)-1;
}

static size_t find_char_from(const char *src, size_t src_len, size_t pos, const char *chars)
{
    for (size_t i = pos; i < src_len; i++) {
        for (const char *c = chars; *c; c++) {
            if (src[i] == *c) {
                return i;
            }
        }
    }
    return (size_t)-1;
}

/* Parse a single line (without the newline). Appends to doc. */
static int ymd_parse_line(struct yetty_ymarkdown_ymd_doc *doc, const char *src, size_t src_len)
{
    struct yetty_ymarkdown_ymd_line line = {0};
    line.scale = 1.0f;

    /* Copy source into a line-owned buffer; spans will slice it. We don't
	 * strip markup — inline markers (*, `) will be skipped by span offsets,
	 * but the characters remain in raw. This is fine because spans point to
	 * exact regions with explicit lengths. */
    if (src_len > 0) {
        line.raw = malloc(src_len);
        if (!line.raw) {
            return -1;
        }
        memcpy(line.raw, src, src_len);
        line.raw_len = src_len;
    }

    const char *ln = line.raw;
    size_t len = line.raw_len;
    size_t start = 0;

    /* Headers */
    int header_level = 0;
    while (start < len && ln[start] == '#') {
        header_level++;
        start++;
    }
    if (header_level > 0 && start < len && ln[start] == ' ') {
        start++;
        int capped = header_level > 6 ? 6 : header_level;
        line.scale = 1.0f + 0.15f * (float)(7 - capped);
    } else {
        header_level = 0;
        start = 0;
    }

    /* Bullet list */
    bool is_bullet = false;
    if (start + 1 < len && (ln[start] == '-' || ln[start] == '*') && ln[start + 1] == ' ') {
        is_bullet = true;
        line.indent = 20.0f;
        start += 2;
    }

    /* Inline styles */
    size_t pos = start;
    while (pos < len) {
        /* Inline code `code` */
        if (ln[pos] == '`') {
            size_t end = find_char_from(ln, len, pos + 1, "`");
            if (end != (size_t)-1) {
                struct yetty_ymarkdown_ymd_span s = {0};
                s.text = ln + pos + 1;
                s.text_len = end - pos - 1;
                s.is_code = true;
                s.style = YETTY_YMARKDOWN_YMD_REGULAR;
                if (ymd_line_push_span(&line, s) < 0) {
                    goto oom;
                }
                pos = end + 1;
                continue;
            }
        }

        /* ***bold italic*** */
        if (pos + 2 < len && match_fixed(ln, len, pos, "***")) {
            size_t end = find_from(ln, len, pos + 3, "***");
            if (end != (size_t)-1) {
                struct yetty_ymarkdown_ymd_span s = {0};
                s.text = ln + pos + 3;
                s.text_len = end - pos - 3;
                s.style = YETTY_YMARKDOWN_YMD_BOLD_ITALIC;
                if (ymd_line_push_span(&line, s) < 0) {
                    goto oom;
                }
                pos = end + 3;
                continue;
            }
        }

        /* **bold** */
        if (pos + 1 < len && match_fixed(ln, len, pos, "**")) {
            size_t end = find_from(ln, len, pos + 2, "**");
            if (end != (size_t)-1) {
                struct yetty_ymarkdown_ymd_span s = {0};
                s.text = ln + pos + 2;
                s.text_len = end - pos - 2;
                s.style = YETTY_YMARKDOWN_YMD_BOLD;
                if (ymd_line_push_span(&line, s) < 0) {
                    goto oom;
                }
                pos = end + 2;
                continue;
            }
        }

        /* *italic* */
        if (ln[pos] == '*') {
            size_t end = find_char_from(ln, len, pos + 1, "*");
            if (end != (size_t)-1) {
                struct yetty_ymarkdown_ymd_span s = {0};
                s.text = ln + pos + 1;
                s.text_len = end - pos - 1;
                s.style = YETTY_YMARKDOWN_YMD_ITALIC;
                if (ymd_line_push_span(&line, s) < 0) {
                    goto oom;
                }
                pos = end + 1;
                continue;
            }
        }

        /* Regular run */
        size_t next = find_char_from(ln, len, pos, "*`");
        if (next == (size_t)-1) {
            next = len;
        }
        if (next > pos) {
            struct yetty_ymarkdown_ymd_span s = {0};
            s.text = ln + pos;
            s.text_len = next - pos;
            s.style = (header_level > 0) ? YETTY_YMARKDOWN_YMD_BOLD : YETTY_YMARKDOWN_YMD_REGULAR;
            s.header_level = (uint8_t)header_level;
            if (ymd_line_push_span(&line, s) < 0) {
                goto oom;
            }
            pos = next;
        } else {
            /* isolated * or ` that didn't close — emit literally */
            struct yetty_ymarkdown_ymd_span s = {0};
            s.text = ln + pos;
            s.text_len = 1;
            s.style = YETTY_YMARKDOWN_YMD_REGULAR;
            if (ymd_line_push_span(&line, s) < 0) {
                goto oom;
            }
            pos++;
        }
    }

    if (is_bullet && line.span_count > 0) {
        /* UTF-8 bullet "• " (E2 80 A2 20) — 4 bytes.
		 * We point into a static literal; no alloc needed. */
        static const char bullet_text[] = "\xE2\x80\xA2 ";
        struct yetty_ymarkdown_ymd_span bullet = {0};
        bullet.text = bullet_text;
        bullet.text_len = sizeof(bullet_text) - 1;
        bullet.style = YETTY_YMARKDOWN_YMD_REGULAR;
        bullet.is_bullet = true;
        if (ymd_line_insert_span_front(&line, bullet) < 0) {
            goto oom;
        }
    }

    if (line.span_count == 0) {
        struct yetty_ymarkdown_ymd_span empty = {0};
        empty.text = "";
        empty.text_len = 0;
        empty.style = YETTY_YMARKDOWN_YMD_REGULAR;
        if (ymd_line_push_span(&line, empty) < 0) {
            goto oom;
        }
    }

    if (ymd_doc_push_line(doc, line) < 0) {
        goto oom;
    }
    return 0;

oom:
    ymd_line_destroy(&line);
    return -1;
}

static int ymd_parse(struct yetty_ymarkdown_ymd_doc *doc, const char *content, size_t len)
{
    size_t i = 0;
    while (i <= len) {
        size_t start = i;
        while (i < len && content[i] != '\n') {
            i++;
        }
        if (ymd_parse_line(doc, content + start, i - start) < 0) {
            return -1;
        }
        if (i >= len) {
            break;
        }
        i++; /* skip '\n' */
    }
    return 0;
}

/*=============================================================================
 * Primitive generation
 *===========================================================================*/

static uint32_t ymd_span_color(const struct yetty_ymarkdown_ymd_span *s)
{
    if (s->is_code) {
        return YMD_COLOR_CODE;
    }
    if (s->header_level > 0) {
        return YMD_COLOR_HEADER;
    }
    if (s->style == YETTY_YMARKDOWN_YMD_BOLD || s->style == YETTY_YMARKDOWN_YMD_BOLD_ITALIC) {
        return YMD_COLOR_BOLD;
    }
    return YMD_COLOR_TEXT;
}

static struct yetty_ycore_void_result ymd_emit(struct yetty_ydraw_draw_list *buf,
                                               const struct yetty_ymarkdown_ymd_doc *doc,
                                               const struct yetty_ymarkdown_ymd_params *p);

/* Emit a single markdown line's spans into `buf` at vertical cursor `cy`,
 * starting horizontally at `cx`. The caller is responsible for advancing
 * its own cursor by the line height — this function only mutates `buf`. */
static struct yetty_ycore_void_result ymd_emit_one_line(struct yetty_ydraw_draw_list *buf,
                                                        const struct yetty_ymarkdown_ymd_line *line,
                                                        float cx_start, float cy,
                                                        const struct yetty_ymarkdown_ymd_params *p)
{
    float cursor_x = cx_start + line->indent;
    float scale = line->scale;
    float scaled_size = p->font_size * scale;

    for (size_t si = 0; si < line->span_count; si++) {
        const struct yetty_ymarkdown_ymd_span *span = &line->spans[si];
        if (span->text_len == 0) {
            continue;
        }

        uint32_t color = ymd_span_color(span);

        if (span->is_code) {
            float text_w = (float)span->text_len * scaled_size * 0.6f;
            struct yetty_ysdf_box geom = {
                .center_x = cursor_x + text_w * 0.5f,
                .center_y = cy + scaled_size * 0.4f,
                .half_width = text_w * 0.5f + 1.0f,
                .half_height = scaled_size * 0.5f + 0.5f,
                .corner_radius = 0.0f,
            };
            struct yetty_ycore_void_result r =
                yetty_ydraw_draw_list_add_cmd_add_box(buf, 0, 0, YMD_COLOR_CODE_BG, 0, 0.0f, &geom);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "ymd line emit: code box add failed");
        }

        struct yetty_ycore_buffer text = {
            .data = (uint8_t *)(uintptr_t)span->text,
            .size = span->text_len,
            .capacity = span->text_len,
        };
        struct yetty_ycore_void_result tr = yetty_ydraw_draw_list_add_text(
            buf, cursor_x, cy, &text, scaled_size, color, 0, -1, 0.0f);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, tr, "ymd line emit: text add failed");

        cursor_x += (float)span->text_len * scaled_size * 0.6f;
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result ymd_emit(struct yetty_ydraw_draw_list *buf,
                                               const struct yetty_ymarkdown_ymd_doc *doc,
                                               const struct yetty_ymarkdown_ymd_params *p)
{
    float cursor_y = 2.0f;
    float line_height = p->font_size * p->line_spacing;

    for (size_t li = 0; li < doc->line_count; li++) {
        const struct yetty_ymarkdown_ymd_line *line = &doc->lines[li];
        struct yetty_ycore_void_result r = ymd_emit_one_line(buf, line, 2.0f, cursor_y, p);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "ymd_emit: line emit failed");
        cursor_y += line_height * line->scale;
    }

    return YETTY_OK_VOID();
}

/*=============================================================================
 * Public entry point
 *===========================================================================*/

struct yetty_ymarkdown_render_result yetty_ymarkdown_render(
    const char *content, size_t content_len, const char *args, size_t args_len,
    const struct yetty_ymarkdown_render_config *config)
{
    if (!content && content_len > 0) {
        return YETTY_ERR(yetty_ymarkdown_render, "content is NULL but content_len > 0");
    }

    struct yetty_ymarkdown_ymd_params params;
    ymd_params_init(&params);
    ymd_parse_args(args, args_len, &params);

    if (config && config->cell_height > 0 && !params.user_font_size) {
        params.font_size = (float)config->cell_height;
    }

    float scene_w = 0.0f, scene_h = 0.0f;
    if (config) {
        scene_w = (float)(config->width_cells * config->cell_width);
        scene_h = (float)(config->height_cells * config->cell_height);
    }

    struct yetty_ydraw_draw_list_config bcfg = {
        .scene_min_x = 0.0f,
        .scene_min_y = 0.0f,
        .scene_max_x = scene_w,
        .scene_max_y = scene_h,
    };
    struct yetty_ydraw_draw_list_result br = yetty_ydraw_draw_list_config_buffer_create(&bcfg);
    if (YETTY_IS_ERR(br)) {
        return YETTY_ERR(yetty_ymarkdown_render, br.error.msg);
    }
    struct yetty_ydraw_draw_list *buf = br.value;

    struct yetty_ymarkdown_ymd_doc doc = {0};
    if (ymd_parse(&doc, content, content_len) < 0) {
        ymd_doc_destroy(&doc);
        yetty_ydraw_draw_list_destroy(buf);
        return YETTY_ERR(yetty_ymarkdown_render, "parse failed");
    }

    struct yetty_ycore_void_result er = ymd_emit(buf, &doc, &params);
    if (YETTY_IS_ERR(er)) {
        ymd_doc_destroy(&doc);
        yetty_ydraw_draw_list_destroy(buf);
        return YETTY_ERR(yetty_ymarkdown_render, "ymarkdown: primitive emission failed", er);
    }

    ymd_doc_destroy(&doc);

    struct yetty_ymarkdown_render_output out = {
        .buffer = buf,
        .scene_width = scene_w,
        .scene_height = scene_h,
    };
    return YETTY_OK(yetty_ymarkdown_render, out);
}

/*=============================================================================
 * Streaming entry point
 *===========================================================================*/

/* Build a fresh per-chunk buffer with the right scene bounds. */
static struct yetty_ydraw_draw_list_result ymd_make_chunk(float scene_w, float chunk_h)
{
    struct yetty_ydraw_draw_list_config bcfg = {
        .scene_min_x = 0.0f,
        .scene_min_y = 0.0f,
        .scene_max_x = scene_w,
        .scene_max_y = chunk_h,
    };
    return yetty_ydraw_draw_list_config_buffer_create(&bcfg);
}

struct yetty_ymarkdown_stream_render_result yetty_ymarkdown_render_streaming(
    const char *content, size_t content_len, const char *args, size_t args_len,
    const struct yetty_ymarkdown_render_config *config, yetty_ymarkdown_chunk_emit_fn on_chunk,
    void *user_data)
{
    if (!content && content_len > 0) {
        return YETTY_ERR(yetty_ymarkdown_stream_render, "content is NULL but content_len > 0");
    }
    if (!on_chunk) {
        return YETTY_ERR(yetty_ymarkdown_stream_render, "on_chunk is NULL");
    }
    if (!config || config->cell_height == 0 || config->height_cells == 0 ||
        config->width_cells == 0 || config->cell_width == 0) {
        return YETTY_ERR(yetty_ymarkdown_stream_render,
                         "config must carry cell + screen dimensions for streaming");
    }

    struct yetty_ymarkdown_ymd_params params;
    ymd_params_init(&params);
    ymd_parse_args(args, args_len, &params);
    if (!params.user_font_size) {
        params.font_size = (float)config->cell_height;
    }

    float scene_w = (float)(config->width_cells * config->cell_width);
    float chunk_h = (float)(config->height_cells * config->cell_height);

    struct yetty_ymarkdown_ymd_doc doc = {0};
    if (ymd_parse(&doc, content, content_len) < 0) {
        ymd_doc_destroy(&doc);
        return YETTY_ERR(yetty_ymarkdown_stream_render, "parse failed");
    }

    struct yetty_ydraw_draw_list_result br = ymd_make_chunk(scene_w, chunk_h);
    if (YETTY_IS_ERR(br)) {
        ymd_doc_destroy(&doc);
        return YETTY_ERR(yetty_ymarkdown_stream_render, "buffer create failed", br);
    }
    struct yetty_ydraw_draw_list *buf = br.value;

    float cursor_y = 2.0f;
    float line_height = params.font_size * params.line_spacing;
    int chunk_index = 0;
    int chunks_emitted = 0;
    struct yetty_ycore_void_result fail = YETTY_OK_VOID();

    for (size_t li = 0; li < doc.line_count; li++) {
        const struct yetty_ymarkdown_ymd_line *line = &doc.lines[li];
        float scaled_line_height = line_height * line->scale;

        /* Line wouldn't fit in the current chunk → ship it and start fresh.
         * Skip the ship when the chunk is still empty (current line is taller
         * than the budget) — emit it anyway and let it overflow rather than
         * loop forever shipping empty chunks. */
        bool chunk_has_content = cursor_y > 2.0f;
        if (chunk_has_content && cursor_y + scaled_line_height > chunk_h) {
            struct yetty_ycore_void_result er = on_chunk(user_data, chunk_index, buf);
            yetty_ydraw_draw_list_destroy(buf);
            buf = NULL;
            chunks_emitted++;
            if (YETTY_IS_ERR(er)) {
                fail = YETTY_ERR(yetty_ycore_void, "ymarkdown stream: on_chunk failed", er);
                goto cleanup;
            }
            chunk_index++;
            struct yetty_ydraw_draw_list_result nbr = ymd_make_chunk(scene_w, chunk_h);
            if (YETTY_IS_ERR(nbr)) {
                fail = YETTY_ERR(yetty_ycore_void, "ymarkdown stream: chunk buffer alloc", nbr);
                goto cleanup;
            }
            buf = nbr.value;
            cursor_y = 2.0f;
        }

        struct yetty_ycore_void_result r = ymd_emit_one_line(buf, line, 2.0f, cursor_y, &params);
        if (YETTY_IS_ERR(r)) {
            fail = YETTY_ERR(yetty_ycore_void, "ymarkdown stream: line emit", r);
            goto cleanup;
        }
        cursor_y += scaled_line_height;
    }

    /* Final chunk: ship if it carries anything past the top margin. */
    if (buf && cursor_y > 2.0f) {
        struct yetty_ycore_void_result er = on_chunk(user_data, chunk_index, buf);
        chunks_emitted++;
        if (YETTY_IS_ERR(er)) {
            fail = YETTY_ERR(yetty_ycore_void, "ymarkdown stream: final on_chunk failed", er);
        }
    }

cleanup:
    if (buf) {
        yetty_ydraw_draw_list_destroy(buf);
    }
    ymd_doc_destroy(&doc);

    if (YETTY_IS_ERR(fail)) {
        return YETTY_ERR(yetty_ymarkdown_stream_render, "ymarkdown streaming render aborted", fail);
    }

    struct yetty_ymarkdown_stream_render_output out = {
        .chunk_count = chunks_emitted,
        .scene_width = scene_w,
        .chunk_height = chunk_h,
    };
    return YETTY_OK(yetty_ymarkdown_stream_render, out);
}
