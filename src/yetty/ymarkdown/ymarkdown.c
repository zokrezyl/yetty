/*
 * ymarkdown.c - Markdown → ydraw buffer.
 *
 * Port of yetty-poc/src/yetty/cards/markdown/markdown.cpp stripped of the
 * Card/GPU lifecycle; the output is just a ydraw buffer with text spans and
 * SDF primitives. The renderer uses the same geometric conventions as the
 * C++ version: layout starts at (2, 2), text advances by 0.6 * font_size per
 * character (proportional approximation), lines advance by font_size *
 * line_spacing.
 *
 * Supported block constructs:
 *   - headers (#..######)
 *   - paragraphs / blank lines
 *   - bullet lists ('-', '*', '+') and ordered lists ('1.', '2)')
 *   - blockquotes ('>' possibly nested) with an accent gutter bar
 *   - fenced code blocks (``` or ~~~) drawn on a shared background panel
 *   - horizontal rules (---, ***, ___)
 *   - GFM tables (| a | b |\n|---|:--:|) with per-column alignment and grid
 *
 * Supported inline constructs:
 *   - bold (**..**), italic (*..*), bold+italic (***..***)
 *   - inline code (`..`) on a tight background box
 *   - strikethrough (~~..~~)
 *   - links ([text](url)) rendered as the accent-coloured link text
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
 * Defaults / layout constants
 *===========================================================================*/

#define YMD_DEFAULT_FONT_SIZE 14.0f
#define YMD_DEFAULT_LINE_SPACING 1.4f

/* Proportional-advance factor: width of one byte at a given font size. The
 * whole renderer is byte-oriented (text_len is a byte count), so multi-byte
 * UTF-8 over-counts width slightly — consistent with the original. */
#define YMD_CHAR_W 0.6f

#define YMD_MARGIN 2.0f         /* left/top margin of the document */
#define YMD_LIST_INDENT 20.0f   /* bullet / ordered-list hanging indent */
#define YMD_QUOTE_GUTTER 12.0f  /* horizontal space reserved per quote level */
#define YMD_QUOTE_BAR_W 3.0f    /* accent bar thickness for blockquotes */
#define YMD_CODE_PAD 6.0f       /* horizontal padding inside a code panel */
#define YMD_TABLE_PAD 6.0f      /* horizontal padding inside a table cell */
#define YMD_TABLE_MIN_COLW 24.0f
#define YMD_TABLE_BORDER_W 1.0f

#define YMD_MAX_COLS 64 /* table columns past this are dropped */

/* Existing palette (kept identical to the original look). */
#define YMD_COLOR_TEXT 0xFFE6E6E6u
#define YMD_COLOR_BOLD 0xFFFFFFFFu
#define YMD_COLOR_CODE 0xFF66CC99u
#define YMD_COLOR_HEADER 0xFFFFFFFFu
#define YMD_COLOR_CODE_BG 0xFF3D3D3Du

/* New surfaces follow the brand palette (mint accent, teal borders,
 * off-white/muted text, raised background rows). */
#define YMD_COLOR_LINK 0xFF74C5A5u        /* accent bright */
#define YMD_COLOR_QUOTE_TEXT 0xFF9FA7A8u  /* secondary text */
#define YMD_COLOR_QUOTE_BAR 0xFF6BA892u   /* accent */
#define YMD_COLOR_RULE 0xFF364A47u        /* border */
#define YMD_COLOR_TABLE_BORDER 0xFF364A47u /* border */
#define YMD_COLOR_TABLE_HDR_BG 0xFF1E262Cu /* raised row */
#define YMD_COLOR_STRIKE 0xFF9FA7A8u       /* secondary text */

/*=============================================================================
 * Parse tree (internal)
 *===========================================================================*/

enum yetty_ymarkdown_ymd_style {
    YETTY_YMARKDOWN_YMD_REGULAR = 0,
    YETTY_YMARKDOWN_YMD_BOLD = 1,
    YETTY_YMARKDOWN_YMD_ITALIC = 2,
    YETTY_YMARKDOWN_YMD_BOLD_ITALIC = 3,
};

enum yetty_ymarkdown_ymd_kind {
    YETTY_YMARKDOWN_YMD_NORMAL = 0, /* paragraph / heading / list / quote */
    YETTY_YMARKDOWN_YMD_HRULE,      /* horizontal rule */
    YETTY_YMARKDOWN_YMD_CODE,       /* one verbatim line of a fenced block */
    YETTY_YMARKDOWN_YMD_TABLE,      /* owns a table block */
};

enum yetty_ymarkdown_ymd_align {
    YETTY_YMARKDOWN_YMD_ALIGN_LEFT = 0,
    YETTY_YMARKDOWN_YMD_ALIGN_CENTER,
    YETTY_YMARKDOWN_YMD_ALIGN_RIGHT,
};

struct yetty_ymarkdown_ymd_span {
    const char *text; /* slice into owning line/cell storage */
    size_t text_len;
    enum yetty_ymarkdown_ymd_style style;
    uint8_t header_level; /* 0 = normal */
    bool is_code;
    bool is_bullet;
    bool is_link;
    bool strike;
};

/* A table cell owns its own backing buffer; spans slice into it. */
struct yetty_ymarkdown_ymd_cell {
    struct yetty_ymarkdown_ymd_span *spans;
    size_t span_count;
    size_t span_cap;
    char *raw;
    enum yetty_ymarkdown_ymd_align align;
};

struct yetty_ymarkdown_ymd_table {
    struct yetty_ymarkdown_ymd_cell *cells; /* nrows * ncols, row-major */
    size_t nrows;
    size_t ncols;
};

struct yetty_ymarkdown_ymd_line {
    struct yetty_ymarkdown_ymd_span *spans;
    size_t span_count;
    size_t span_cap;
    float indent;
    float scale;

    enum yetty_ymarkdown_ymd_kind kind;
    int quote_depth;        /* >0 for blockquote lines */
    size_t code_cols;       /* CODE lines: panel width in characters */

    /* Owned buffer that backs span text slices (and the verbatim text of a
     * CODE line). Inline markers stay in the buffer; spans address exact
     * regions with explicit lengths so the markers are never drawn. */
    char *raw;
    size_t raw_len;

    struct yetty_ymarkdown_ymd_table *table; /* non-NULL iff kind == TABLE */
};

struct yetty_ymarkdown_ymd_doc {
    struct yetty_ymarkdown_ymd_line *lines;
    size_t line_count;
    size_t line_cap;
};

/*=============================================================================
 * Span / line / table containers
 *===========================================================================*/

static int ymd_spans_push(struct yetty_ymarkdown_ymd_span **spans, size_t *count, size_t *cap,
                          struct yetty_ymarkdown_ymd_span span)
{
    if (*count == *cap) {
        size_t new_cap = *cap ? *cap * 2 : 4;
        struct yetty_ymarkdown_ymd_span *ns = realloc(*spans, new_cap * sizeof(*ns));
        if (!ns) {
            return -1;
        }
        *spans = ns;
        *cap = new_cap;
    }
    (*spans)[(*count)++] = span;
    return 0;
}

static int ymd_line_push_span(struct yetty_ymarkdown_ymd_line *line,
                              struct yetty_ymarkdown_ymd_span span)
{
    return ymd_spans_push(&line->spans, &line->span_count, &line->span_cap, span);
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

static void ymd_table_destroy(struct yetty_ymarkdown_ymd_table *table)
{
    if (!table) {
        return;
    }
    if (table->cells) {
        for (size_t i = 0; i < table->nrows * table->ncols; i++) {
            free(table->cells[i].spans);
            free(table->cells[i].raw);
        }
        free(table->cells);
    }
    free(table);
}

static void ymd_line_destroy(struct yetty_ymarkdown_ymd_line *line)
{
    if (!line) {
        return;
    }
    free(line->spans);
    free(line->raw);
    ymd_table_destroy(line->table);
    line->spans = NULL;
    line->raw = NULL;
    line->table = NULL;
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
 * Small text helpers
 *===========================================================================*/

static bool match_fixed(const char *src, size_t src_len, size_t pos, const char *needle)
{
    size_t n = strlen(needle);
    if (pos + n > src_len) {
        return false;
    }
    return memcmp(src + pos, needle, n) == 0;
}

/* Find next occurrence of needle in [pos, src_len). Returns index or SIZE_MAX. */
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

static bool ymd_is_space(char c)
{
    return c == ' ' || c == '\t';
}

/* Strip leading/trailing spaces/tabs, returning the inner slice. */
static void ymd_trim(const char *s, size_t len, const char **out, size_t *out_len)
{
    size_t a = 0;
    size_t b = len;
    while (a < b && ymd_is_space(s[a])) {
        a++;
    }
    while (b > a && ymd_is_space(s[b - 1])) {
        b--;
    }
    *out = s + a;
    *out_len = b - a;
}

/*=============================================================================
 * Inline span parser (shared by paragraphs and table cells)
 *
 * Slices `ln[start, len)` into styled spans pushed onto (*spans). The spans
 * point straight into `ln`, so the caller must keep `ln` alive for as long
 * as the spans are used.
 *===========================================================================*/

static int ymd_parse_inline(struct yetty_ymarkdown_ymd_span **spans, size_t *count, size_t *cap,
                            const char *ln, size_t start, size_t len, int header_level)
{
    size_t pos = start;
    while (pos < len) {
        /* inline code `code` */
        if (ln[pos] == '`') {
            size_t end = find_char_from(ln, len, pos + 1, "`");
            if (end != (size_t)-1) {
                struct yetty_ymarkdown_ymd_span s = {0};
                s.text = ln + pos + 1;
                s.text_len = end - pos - 1;
                s.is_code = true;
                s.style = YETTY_YMARKDOWN_YMD_REGULAR;
                if (ymd_spans_push(spans, count, cap, s) < 0) {
                    return -1;
                }
                pos = end + 1;
                continue;
            }
        }

        /* link [text](url) — render the visible text only */
        if (ln[pos] == '[') {
            size_t close = find_char_from(ln, len, pos + 1, "]");
            if (close != (size_t)-1 && close + 1 < len && ln[close + 1] == '(') {
                size_t paren = find_char_from(ln, len, close + 2, ")");
                if (paren != (size_t)-1) {
                    struct yetty_ymarkdown_ymd_span s = {0};
                    s.text = ln + pos + 1;
                    s.text_len = close - pos - 1;
                    s.is_link = true;
                    s.style = (header_level > 0) ? YETTY_YMARKDOWN_YMD_BOLD
                                                 : YETTY_YMARKDOWN_YMD_REGULAR;
                    if (ymd_spans_push(spans, count, cap, s) < 0) {
                        return -1;
                    }
                    pos = paren + 1;
                    continue;
                }
            }
        }

        /* ~~strikethrough~~ */
        if (pos + 1 < len && match_fixed(ln, len, pos, "~~")) {
            size_t end = find_from(ln, len, pos + 2, "~~");
            if (end != (size_t)-1) {
                struct yetty_ymarkdown_ymd_span s = {0};
                s.text = ln + pos + 2;
                s.text_len = end - pos - 2;
                s.strike = true;
                s.style = (header_level > 0) ? YETTY_YMARKDOWN_YMD_BOLD
                                             : YETTY_YMARKDOWN_YMD_REGULAR;
                if (ymd_spans_push(spans, count, cap, s) < 0) {
                    return -1;
                }
                pos = end + 2;
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
                if (ymd_spans_push(spans, count, cap, s) < 0) {
                    return -1;
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
                if (ymd_spans_push(spans, count, cap, s) < 0) {
                    return -1;
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
                if (ymd_spans_push(spans, count, cap, s) < 0) {
                    return -1;
                }
                pos = end + 1;
                continue;
            }
        }

        /* regular run up to the next inline marker */
        size_t next = find_char_from(ln, len, pos, "*`~[");
        if (next == (size_t)-1) {
            next = len;
        }
        if (next > pos) {
            struct yetty_ymarkdown_ymd_span s = {0};
            s.text = ln + pos;
            s.text_len = next - pos;
            s.style = (header_level > 0) ? YETTY_YMARKDOWN_YMD_BOLD : YETTY_YMARKDOWN_YMD_REGULAR;
            s.header_level = (uint8_t)header_level;
            if (ymd_spans_push(spans, count, cap, s) < 0) {
                return -1;
            }
            pos = next;
        } else {
            /* isolated marker that didn't form a construct — emit literally */
            struct yetty_ymarkdown_ymd_span s = {0};
            s.text = ln + pos;
            s.text_len = 1;
            s.style = (header_level > 0) ? YETTY_YMARKDOWN_YMD_BOLD : YETTY_YMARKDOWN_YMD_REGULAR;
            s.header_level = (uint8_t)header_level;
            if (ymd_spans_push(spans, count, cap, s) < 0) {
                return -1;
            }
            pos++;
        }
    }
    return 0;
}

/*=============================================================================
 * Block detection
 *===========================================================================*/

/* A fenced-code delimiter: 3+ backticks or tildes (after trimming leading
 * spaces). Returns the fence char (`` ` `` or '~'), or 0 if not a fence. */
static char ymd_fence_char(const char *s, size_t len)
{
    const char *t;
    size_t tl;
    ymd_trim(s, len, &t, &tl);
    if (tl < 3) {
        return 0;
    }
    char c = t[0];
    if (c != '`' && c != '~') {
        return 0;
    }
    size_t run = 0;
    while (run < tl && t[run] == c) {
        run++;
    }
    return run >= 3 ? c : 0;
}

/* A horizontal rule: 3+ of a single marker ('-', '*', '_'), spaces allowed
 * between them and nothing else. */
static bool ymd_is_hrule(const char *s, size_t len)
{
    const char *t;
    size_t tl;
    ymd_trim(s, len, &t, &tl);
    if (tl < 3) {
        return false;
    }
    char marker = 0;
    size_t count = 0;
    for (size_t i = 0; i < tl; i++) {
        char c = t[i];
        if (c == ' ' || c == '\t') {
            continue;
        }
        if (c != '-' && c != '*' && c != '_') {
            return false;
        }
        if (marker == 0) {
            marker = c;
        } else if (c != marker) {
            return false;
        }
        count++;
    }
    return count >= 3;
}

static bool ymd_has_pipe(const char *s, size_t len)
{
    const char *t;
    size_t tl;
    ymd_trim(s, len, &t, &tl);
    if (tl == 0) {
        return false;
    }
    for (size_t i = 0; i < tl; i++) {
        if (t[i] == '|') {
            return true;
        }
    }
    return false;
}

/* A table separator row: only '|', '-', ':', and spaces, with at least one
 * '-'. e.g. "| --- | :--: | --: |". */
static bool ymd_is_table_separator(const char *s, size_t len)
{
    const char *t;
    size_t tl;
    ymd_trim(s, len, &t, &tl);
    if (tl == 0) {
        return false;
    }
    bool saw_dash = false;
    for (size_t i = 0; i < tl; i++) {
        char c = t[i];
        if (c == '-') {
            saw_dash = true;
        } else if (c != '|' && c != ':' && c != ' ' && c != '\t') {
            return false;
        }
    }
    return saw_dash;
}

/*=============================================================================
 * Table parsing
 *===========================================================================*/

/* Split a table row into trimmed cell slices. Optional outer pipes are
 * dropped. Returns the number of cells (clamped to YMD_MAX_COLS). */
static size_t ymd_split_row(const char *s, size_t len, const char **cells, size_t *cell_lens)
{
    const char *t;
    size_t tl;
    ymd_trim(s, len, &t, &tl);
    /* drop a trailing pipe (and any trailing space it left behind) */
    if (tl > 0 && t[tl - 1] == '|') {
        tl--;
        while (tl > 0 && ymd_is_space(t[tl - 1])) {
            tl--;
        }
    }
    size_t i = 0;
    if (tl > 0 && t[0] == '|') {
        i = 1;
    }
    size_t count = 0;
    size_t cell_start = i;
    for (; i <= tl; i++) {
        if (i == tl || t[i] == '|') {
            const char *cs;
            size_t cl;
            ymd_trim(t + cell_start, i - cell_start, &cs, &cl);
            if (count < YMD_MAX_COLS) {
                cells[count] = cs;
                cell_lens[count] = cl;
                count++;
            }
            cell_start = i + 1;
        }
    }
    return count;
}

static enum yetty_ymarkdown_ymd_align ymd_parse_align(const char *s, size_t len)
{
    const char *t;
    size_t tl;
    ymd_trim(s, len, &t, &tl);
    bool left = tl > 0 && t[0] == ':';
    bool right = tl > 0 && t[tl - 1] == ':';
    if (left && right) {
        return YETTY_YMARKDOWN_YMD_ALIGN_CENTER;
    }
    if (right) {
        return YETTY_YMARKDOWN_YMD_ALIGN_RIGHT;
    }
    return YETTY_YMARKDOWN_YMD_ALIGN_LEFT;
}

static int ymd_cell_fill(struct yetty_ymarkdown_ymd_cell *cell, const char *text, size_t len,
                         int header_level)
{
    if (len == 0) {
        return 0; /* empty cell: no raw, no spans */
    }
    cell->raw = malloc(len);
    if (!cell->raw) {
        return -1;
    }
    memcpy(cell->raw, text, len);
    return ymd_parse_inline(&cell->spans, &cell->span_count, &cell->span_cap, cell->raw, 0, len,
                            header_level);
}

struct ymd_line_slice {
    const char *p;
    size_t len;
};

/* Parse a table that starts at slices[*idx] (header) with slices[*idx+1] as
 * the separator. Pushes one TABLE line onto `doc` and advances *idx past the
 * whole table. */
static int ymd_parse_table(struct yetty_ymarkdown_ymd_doc *doc,
                           const struct ymd_line_slice *slices, size_t nslices, size_t *idx)
{
    size_t header = *idx;
    size_t sep = header + 1;

    const char *hcells[YMD_MAX_COLS];
    size_t hlens[YMD_MAX_COLS];
    size_t ncols = ymd_split_row(slices[header].p, slices[header].len, hcells, hlens);
    if (ncols == 0) {
        return 1; /* not really a table — let caller fall back */
    }

    /* count body rows */
    size_t body_end = sep + 1;
    while (body_end < nslices && ymd_has_pipe(slices[body_end].p, slices[body_end].len) &&
           !ymd_is_table_separator(slices[body_end].p, slices[body_end].len)) {
        body_end++;
    }
    size_t nrows = 1 + (body_end - (sep + 1)); /* header + body rows */

    struct yetty_ymarkdown_ymd_table *table = calloc(1, sizeof(*table));
    if (!table) {
        return -1;
    }
    table->ncols = ncols;
    table->nrows = nrows;
    table->cells = calloc(nrows * ncols, sizeof(*table->cells));
    if (!table->cells) {
        ymd_table_destroy(table);
        return -1;
    }

    /* alignments from the separator row */
    const char *acells[YMD_MAX_COLS];
    size_t alens[YMD_MAX_COLS];
    size_t na = ymd_split_row(slices[sep].p, slices[sep].len, acells, alens);

    /* header row (bold) */
    for (size_t c = 0; c < ncols; c++) {
        struct yetty_ymarkdown_ymd_cell *cell = &table->cells[c];
        cell->align = c < na ? ymd_parse_align(acells[c], alens[c]) : YETTY_YMARKDOWN_YMD_ALIGN_LEFT;
        if (ymd_cell_fill(cell, hcells[c], hlens[c], 1) < 0) {
            ymd_table_destroy(table);
            return -1;
        }
    }

    /* body rows */
    for (size_t r = 1; r < nrows; r++) {
        size_t si = sep + r; /* sep + 1 is first body row */
        const char *rcells[YMD_MAX_COLS];
        size_t rlens[YMD_MAX_COLS];
        size_t rc = ymd_split_row(slices[si].p, slices[si].len, rcells, rlens);
        for (size_t c = 0; c < ncols; c++) {
            struct yetty_ymarkdown_ymd_cell *cell = &table->cells[r * ncols + c];
            cell->align = table->cells[c].align;
            if (c < rc) {
                if (ymd_cell_fill(cell, rcells[c], rlens[c], 0) < 0) {
                    ymd_table_destroy(table);
                    return -1;
                }
            }
        }
    }

    struct yetty_ymarkdown_ymd_line line = {0};
    line.scale = 1.0f;
    line.kind = YETTY_YMARKDOWN_YMD_TABLE;
    line.table = table;
    if (ymd_doc_push_line(doc, line) < 0) {
        ymd_table_destroy(table);
        return -1;
    }

    *idx = body_end;
    return 0;
}

/*=============================================================================
 * Normal-line (paragraph / heading / list / quote) parsing
 *===========================================================================*/

static int ymd_parse_normal_line(struct yetty_ymarkdown_ymd_doc *doc, const char *src,
                                  size_t src_len)
{
    struct yetty_ymarkdown_ymd_line line = {0};
    line.scale = 1.0f;
    line.kind = YETTY_YMARKDOWN_YMD_NORMAL;

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

    /* Blockquote: one or more leading '>' (each may be followed by a space). */
    while (start < len && ln[start] == '>') {
        line.quote_depth++;
        start++;
        if (start < len && ln[start] == ' ') {
            start++;
        }
    }
    if (line.quote_depth > 0) {
        line.indent += (float)line.quote_depth * YMD_QUOTE_GUTTER;
    }

    /* Headers */
    int header_level = 0;
    {
        size_t h = start;
        while (h < len && ln[h] == '#') {
            header_level++;
            h++;
        }
        if (header_level > 0 && h < len && ln[h] == ' ') {
            start = h + 1;
            int capped = header_level > 6 ? 6 : header_level;
            line.scale = 1.0f + 0.15f * (float)(7 - capped);
        } else {
            header_level = 0;
        }
    }

    /* List marker: bullet ('-', '*', '+') or ordered ('1.', '2)'). */
    bool is_bullet = false;
    const char *num_prefix = NULL;
    size_t num_prefix_len = 0;
    if (header_level == 0) {
        if (start + 1 < len && (ln[start] == '-' || ln[start] == '*' || ln[start] == '+') &&
            ln[start + 1] == ' ') {
            is_bullet = true;
            line.indent += YMD_LIST_INDENT;
            start += 2;
        } else {
            size_t d = start;
            while (d < len && ln[d] >= '0' && ln[d] <= '9') {
                d++;
            }
            if (d > start && d + 1 < len && (ln[d] == '.' || ln[d] == ')') && ln[d + 1] == ' ') {
                num_prefix = ln + start;
                num_prefix_len = (d - start) + 2; /* digits + delimiter + space */
                line.indent += YMD_LIST_INDENT;
                start = d + 2;
            }
        }
    }

    /* Inline content */
    if (ymd_parse_inline(&line.spans, &line.span_count, &line.span_cap, ln, start, len,
                         header_level) < 0) {
        goto oom;
    }

    /* List prefix span */
    if (is_bullet && line.span_count > 0) {
        static const char bullet_text[] = "\xE2\x80\xA2 "; /* "• " */
        struct yetty_ymarkdown_ymd_span bullet = {0};
        bullet.text = bullet_text;
        bullet.text_len = sizeof(bullet_text) - 1;
        bullet.is_bullet = true;
        if (ymd_line_insert_span_front(&line, bullet) < 0) {
            goto oom;
        }
    } else if (num_prefix && line.span_count > 0) {
        struct yetty_ymarkdown_ymd_span num = {0};
        num.text = num_prefix; /* "1. " — slices into line.raw */
        num.text_len = num_prefix_len;
        num.is_bullet = true;
        if (ymd_line_insert_span_front(&line, num) < 0) {
            goto oom;
        }
    }

    if (line.span_count == 0) {
        struct yetty_ymarkdown_ymd_span empty = {0};
        empty.text = "";
        empty.text_len = 0;
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

/* Push one verbatim code line. */
static int ymd_push_code_line(struct yetty_ymarkdown_ymd_doc *doc, const char *src, size_t src_len,
                              size_t code_cols)
{
    struct yetty_ymarkdown_ymd_line line = {0};
    line.scale = 1.0f;
    line.kind = YETTY_YMARKDOWN_YMD_CODE;
    line.code_cols = code_cols;
    if (src_len > 0) {
        line.raw = malloc(src_len);
        if (!line.raw) {
            return -1;
        }
        memcpy(line.raw, src, src_len);
        line.raw_len = src_len;
    }
    if (ymd_doc_push_line(doc, line) < 0) {
        ymd_line_destroy(&line);
        return -1;
    }
    return 0;
}

static int ymd_push_hrule(struct yetty_ymarkdown_ymd_doc *doc)
{
    struct yetty_ymarkdown_ymd_line line = {0};
    line.scale = 1.0f;
    line.kind = YETTY_YMARKDOWN_YMD_HRULE;
    return ymd_doc_push_line(doc, line);
}

/*=============================================================================
 * Top-level parse: split into lines, dispatch block constructs
 *===========================================================================*/

static int ymd_parse(struct yetty_ymarkdown_ymd_doc *doc, const char *content, size_t len)
{
    /* Slice content into lines (CRLF tolerant). */
    struct ymd_line_slice *slices = NULL;
    size_t nslices = 0;
    size_t cap = 0;
    {
        size_t i = 0;
        while (i <= len) {
            size_t start = i;
            while (i < len && content[i] != '\n') {
                i++;
            }
            size_t ll = i - start;
            if (ll > 0 && content[start + ll - 1] == '\r') {
                ll--;
            }
            if (nslices == cap) {
                size_t nc = cap ? cap * 2 : 32;
                struct ymd_line_slice *ns = realloc(slices, nc * sizeof(*ns));
                if (!ns) {
                    free(slices);
                    return -1;
                }
                slices = ns;
                cap = nc;
            }
            slices[nslices].p = content + start;
            slices[nslices].len = ll;
            nslices++;
            if (i >= len) {
                break;
            }
            i++; /* skip '\n' */
        }
    }

    int rc = 0;
    for (size_t idx = 0; idx < nslices;) {
        const struct ymd_line_slice *s = &slices[idx];

        char fence = ymd_fence_char(s->p, s->len);
        if (fence) {
            /* Gather body lines up to the matching closing fence, measuring
             * the widest line for the shared panel. */
            size_t body_start = idx + 1;
            size_t body_end = body_start;
            size_t max_cols = 0;
            while (body_end < nslices && ymd_fence_char(slices[body_end].p, slices[body_end].len) != fence) {
                if (slices[body_end].len > max_cols) {
                    max_cols = slices[body_end].len;
                }
                body_end++;
            }
            for (size_t b = body_start; b < body_end; b++) {
                if (ymd_push_code_line(doc, slices[b].p, slices[b].len, max_cols) < 0) {
                    rc = -1;
                    goto done;
                }
            }
            /* skip body and the closing fence (if present) */
            idx = (body_end < nslices) ? body_end + 1 : body_end;
            continue;
        }

        if (ymd_is_hrule(s->p, s->len)) {
            if (ymd_push_hrule(doc) < 0) {
                rc = -1;
                goto done;
            }
            idx++;
            continue;
        }

        if (idx + 1 < nslices && ymd_has_pipe(s->p, s->len) &&
            ymd_is_table_separator(slices[idx + 1].p, slices[idx + 1].len)) {
            int tr = ymd_parse_table(doc, slices, nslices, &idx);
            if (tr < 0) {
                rc = -1;
                goto done;
            }
            if (tr == 0) {
                continue;
            }
            /* tr == 1: not actually a table; fall through to normal parse */
        }

        if (ymd_parse_normal_line(doc, s->p, s->len) < 0) {
            rc = -1;
            goto done;
        }
        idx++;
    }

done:
    free(slices);
    return rc;
}

/*=============================================================================
 * Primitive generation
 *===========================================================================*/

static uint32_t ymd_span_color(const struct yetty_ymarkdown_ymd_span *s)
{
    if (s->is_link) {
        return YMD_COLOR_LINK;
    }
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

static float ymd_text_px(size_t bytes, float scaled_size)
{
    return (float)bytes * scaled_size * YMD_CHAR_W;
}

static float ymd_measure_spans(const struct yetty_ymarkdown_ymd_span *spans, size_t count,
                               float scaled_size)
{
    float w = 0.0f;
    for (size_t i = 0; i < count; i++) {
        w += ymd_text_px(spans[i].text_len, scaled_size);
    }
    return w;
}

static struct yetty_ycore_void_result ymd_emit_text(struct yetty_ydraw_draw_list *buf, float x,
                                                    float y, const char *text, size_t text_len,
                                                    float scaled_size, uint32_t color)
{
    struct yetty_ycore_buffer t = {
        .data = (uint8_t *)(uintptr_t)text,
        .size = text_len,
        .capacity = text_len,
    };
    return yetty_ydraw_draw_list_add_text(buf, x, y, &t, scaled_size, color, 0, -1, 0.0f);
}

static struct yetty_ycore_void_result ymd_emit_box(struct yetty_ydraw_draw_list *buf, float cx,
                                                   float cy, float half_w, float half_h,
                                                   uint32_t fill)
{
    struct yetty_ysdf_box geom = {
        .center_x = cx,
        .center_y = cy,
        .half_width = half_w,
        .half_height = half_h,
        .corner_radius = 0.0f,
    };
    return yetty_ydraw_draw_list_add_cmd_add_box(buf, 0, 0, fill, 0, 0.0f, &geom);
}

static struct yetty_ycore_void_result ymd_emit_hline(struct yetty_ydraw_draw_list *buf, float x0,
                                                     float x1, float y, uint32_t color)
{
    struct yetty_ysdf_segment geom = {.start_x = x0, .start_y = y, .end_x = x1, .end_y = y};
    return yetty_ydraw_draw_list_add_cmd_add_segment(buf, 0, 0, 0u, color, YMD_TABLE_BORDER_W,
                                                     &geom);
}

static struct yetty_ycore_void_result ymd_emit_vline(struct yetty_ydraw_draw_list *buf, float x,
                                                     float y0, float y1, uint32_t color)
{
    struct yetty_ysdf_segment geom = {.start_x = x, .start_y = y0, .end_x = x, .end_y = y1};
    return yetty_ydraw_draw_list_add_cmd_add_segment(buf, 0, 0, 0u, color, YMD_TABLE_BORDER_W,
                                                     &geom);
}

/* Emit a run of spans starting at (x, y). `override` recolours spans that
 * would otherwise use the default body colour (used for blockquote text).
 * Writes the trailing x to *out_x when non-NULL. */
static struct yetty_ycore_void_result ymd_emit_spans(struct yetty_ydraw_draw_list *buf,
                                                     const struct yetty_ymarkdown_ymd_span *spans,
                                                     size_t count, float x, float y,
                                                     float scaled_size, uint32_t override,
                                                     float *out_x)
{
    float cursor_x = x;
    for (size_t i = 0; i < count; i++) {
        const struct yetty_ymarkdown_ymd_span *span = &spans[i];
        if (span->text_len == 0) {
            continue;
        }
        uint32_t color = ymd_span_color(span);
        if (override && color == YMD_COLOR_TEXT) {
            color = override;
        }
        float text_w = ymd_text_px(span->text_len, scaled_size);

        if (span->is_code) {
            struct yetty_ycore_void_result r =
                ymd_emit_box(buf, cursor_x + text_w * 0.5f, y + scaled_size * 0.4f,
                             text_w * 0.5f + 1.0f, scaled_size * 0.5f + 0.5f, YMD_COLOR_CODE_BG);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "ymd span: code box add failed");
        }

        struct yetty_ycore_void_result tr =
            ymd_emit_text(buf, cursor_x, y, span->text, span->text_len, scaled_size, color);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, tr, "ymd span: text add failed");

        if (span->strike) {
            struct yetty_ycore_void_result sr = ymd_emit_box(
                buf, cursor_x + text_w * 0.5f, y + scaled_size * 0.32f, text_w * 0.5f, 0.75f,
                YMD_COLOR_STRIKE);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "ymd span: strike add failed");
        }

        cursor_x += text_w;
    }
    if (out_x) {
        *out_x = cursor_x;
    }
    return YETTY_OK_VOID();
}

static float ymd_table_row_h(const struct yetty_ymarkdown_ymd_params *p)
{
    return p->font_size * p->line_spacing;
}

static struct yetty_ycore_void_result ymd_emit_table(struct yetty_ydraw_draw_list *buf,
                                                     const struct yetty_ymarkdown_ymd_line *line,
                                                     float x0, float y0,
                                                     const struct yetty_ymarkdown_ymd_params *p)
{
    const struct yetty_ymarkdown_ymd_table *table = line->table;
    size_t ncols = table->ncols;
    size_t nrows = table->nrows;
    if (ncols == 0 || nrows == 0 || ncols > YMD_MAX_COLS) {
        return YETTY_OK_VOID();
    }
    float scaled = p->font_size;
    float row_h = ymd_table_row_h(p);

    float colw[YMD_MAX_COLS];
    float colx[YMD_MAX_COLS];
    for (size_t c = 0; c < ncols; c++) {
        float w = YMD_TABLE_MIN_COLW;
        for (size_t r = 0; r < nrows; r++) {
            const struct yetty_ymarkdown_ymd_cell *cell = &table->cells[r * ncols + c];
            float cw = ymd_measure_spans(cell->spans, cell->span_count, scaled) + 2.0f * YMD_TABLE_PAD;
            if (cw > w) {
                w = cw;
            }
        }
        colw[c] = w;
    }
    float table_w = 0.0f;
    for (size_t c = 0; c < ncols; c++) {
        colx[c] = x0 + table_w;
        table_w += colw[c];
    }
    float table_h = (float)nrows * row_h;

    /* header background */
    struct yetty_ycore_void_result hb = ymd_emit_box(buf, x0 + table_w * 0.5f, y0 + row_h * 0.5f,
                                                     table_w * 0.5f, row_h * 0.5f,
                                                     YMD_COLOR_TABLE_HDR_BG);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, hb, "ymd table: header bg");

    /* horizontal grid lines */
    for (size_t r = 0; r <= nrows; r++) {
        float gy = y0 + (float)r * row_h;
        struct yetty_ycore_void_result r1 =
            ymd_emit_hline(buf, x0, x0 + table_w, gy, YMD_COLOR_TABLE_BORDER);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r1, "ymd table: hline");
    }
    /* vertical grid lines */
    for (size_t c = 0; c <= ncols; c++) {
        float gx = (c < ncols) ? colx[c] : x0 + table_w;
        struct yetty_ycore_void_result r2 =
            ymd_emit_vline(buf, gx, y0, y0 + table_h, YMD_COLOR_TABLE_BORDER);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r2, "ymd table: vline");
    }

    /* cells */
    for (size_t r = 0; r < nrows; r++) {
        for (size_t c = 0; c < ncols; c++) {
            const struct yetty_ymarkdown_ymd_cell *cell = &table->cells[r * ncols + c];
            if (cell->span_count == 0) {
                continue;
            }
            float content_w = ymd_measure_spans(cell->spans, cell->span_count, scaled);
            float text_x;
            switch (cell->align) {
            case YETTY_YMARKDOWN_YMD_ALIGN_RIGHT:
                text_x = colx[c] + colw[c] - YMD_TABLE_PAD - content_w;
                break;
            case YETTY_YMARKDOWN_YMD_ALIGN_CENTER:
                text_x = colx[c] + (colw[c] - content_w) * 0.5f;
                break;
            case YETTY_YMARKDOWN_YMD_ALIGN_LEFT:
            default:
                text_x = colx[c] + YMD_TABLE_PAD;
                break;
            }
            float text_y = y0 + (float)r * row_h + (row_h - scaled) * 0.5f;
            struct yetty_ycore_void_result cr =
                ymd_emit_spans(buf, cell->spans, cell->span_count, text_x, text_y, scaled, 0, NULL);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, cr, "ymd table: cell");
        }
    }

    return YETTY_OK_VOID();
}

/* Vertical advance for one block. */
static float ymd_block_height(const struct yetty_ymarkdown_ymd_line *line,
                              const struct yetty_ymarkdown_ymd_params *p)
{
    float line_height = p->font_size * p->line_spacing;
    switch (line->kind) {
    case YETTY_YMARKDOWN_YMD_TABLE:
        return (float)line->table->nrows * ymd_table_row_h(p) + line_height * 0.3f;
    case YETTY_YMARKDOWN_YMD_HRULE:
    case YETTY_YMARKDOWN_YMD_CODE:
        return line_height;
    case YETTY_YMARKDOWN_YMD_NORMAL:
    default:
        return line_height * line->scale;
    }
}

/* Emit one block's primitives at vertical cursor `cy`, starting horizontally
 * at `cx_start`. `content_w` is the usable document width (used to size code
 * panels and rules); 0 means "unknown — fall back to content size". */
static struct yetty_ycore_void_result ymd_emit_one_line(struct yetty_ydraw_draw_list *buf,
                                                        const struct yetty_ymarkdown_ymd_line *line,
                                                        float cx_start, float cy, float content_w,
                                                        const struct yetty_ymarkdown_ymd_params *p)
{
    float line_height = p->font_size * p->line_spacing;

    switch (line->kind) {
    case YETTY_YMARKDOWN_YMD_HRULE: {
        float w = content_w > 0.0f ? content_w : 320.0f;
        return ymd_emit_box(buf, cx_start + w * 0.5f, cy + line_height * 0.5f, w * 0.5f, 0.75f,
                            YMD_COLOR_RULE);
    }
    case YETTY_YMARKDOWN_YMD_CODE: {
        float scaled = p->font_size;
        float panel_w = (float)line->code_cols * scaled * YMD_CHAR_W + 2.0f * YMD_CODE_PAD;
        if (content_w > 0.0f && panel_w > content_w) {
            panel_w = content_w;
        }
        struct yetty_ycore_void_result bg =
            ymd_emit_box(buf, cx_start + panel_w * 0.5f, cy + scaled * 0.5f, panel_w * 0.5f,
                         line_height * 0.5f, YMD_COLOR_CODE_BG);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, bg, "ymd code: panel");
        if (line->raw_len > 0) {
            struct yetty_ycore_void_result tr = ymd_emit_text(
                buf, cx_start + YMD_CODE_PAD, cy, line->raw, line->raw_len, scaled, YMD_COLOR_CODE);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, tr, "ymd code: text");
        }
        return YETTY_OK_VOID();
    }
    case YETTY_YMARKDOWN_YMD_TABLE:
        return ymd_emit_table(buf, line, cx_start, cy, p);
    case YETTY_YMARKDOWN_YMD_NORMAL:
    default:
        break;
    }

    float scaled = p->font_size * line->scale;

    /* blockquote gutter bars */
    if (line->quote_depth > 0) {
        for (int d = 0; d < line->quote_depth; d++) {
            float bx = cx_start + (float)d * YMD_QUOTE_GUTTER + YMD_QUOTE_BAR_W * 0.5f;
            struct yetty_ycore_void_result qr =
                ymd_emit_box(buf, bx, cy + line_height * 0.5f, YMD_QUOTE_BAR_W * 0.5f,
                             line_height * 0.5f, YMD_COLOR_QUOTE_BAR);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, qr, "ymd quote: bar");
        }
    }

    uint32_t override = line->quote_depth > 0 ? YMD_COLOR_QUOTE_TEXT : 0;
    return ymd_emit_spans(buf, line->spans, line->span_count, cx_start + line->indent, cy, scaled,
                          override, NULL);
}

static struct yetty_ycore_void_result ymd_emit(struct yetty_ydraw_draw_list *buf,
                                               const struct yetty_ymarkdown_ymd_doc *doc,
                                               float content_w,
                                               const struct yetty_ymarkdown_ymd_params *p)
{
    float cursor_y = YMD_MARGIN;

    for (size_t li = 0; li < doc->line_count; li++) {
        const struct yetty_ymarkdown_ymd_line *line = &doc->lines[li];
        struct yetty_ycore_void_result r =
            ymd_emit_one_line(buf, line, YMD_MARGIN, cursor_y, content_w, p);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "ymd_emit: line emit failed");
        cursor_y += ymd_block_height(line, p);
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
    float content_w = scene_w > 2.0f * YMD_MARGIN ? scene_w - 2.0f * YMD_MARGIN : 0.0f;

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

    struct yetty_ycore_void_result er = ymd_emit(buf, &doc, content_w, &params);
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
    float content_w = scene_w > 2.0f * YMD_MARGIN ? scene_w - 2.0f * YMD_MARGIN : 0.0f;

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

    float cursor_y = YMD_MARGIN;
    int chunk_index = 0;
    int chunks_emitted = 0;
    struct yetty_ycore_void_result fail = YETTY_OK_VOID();

    for (size_t li = 0; li < doc.line_count; li++) {
        const struct yetty_ymarkdown_ymd_line *line = &doc.lines[li];
        float block_h = ymd_block_height(line, &params);

        /* Block wouldn't fit in the current chunk → ship it and start fresh.
         * Skip the ship when the chunk is still empty (current block is taller
         * than the budget) — emit it anyway and let it overflow rather than
         * loop forever shipping empty chunks. */
        bool chunk_has_content = cursor_y > YMD_MARGIN;
        if (chunk_has_content && cursor_y + block_h > chunk_h) {
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
            cursor_y = YMD_MARGIN;
        }

        struct yetty_ycore_void_result r =
            ymd_emit_one_line(buf, line, YMD_MARGIN, cursor_y, content_w, &params);
        if (YETTY_IS_ERR(r)) {
            fail = YETTY_ERR(yetty_ycore_void, "ymarkdown stream: line emit", r);
            goto cleanup;
        }
        cursor_y += block_h;
    }

    /* Final chunk: ship if it carries anything past the top margin. */
    if (buf && cursor_y > YMD_MARGIN) {
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
