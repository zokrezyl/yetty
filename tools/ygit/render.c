/* render.c — draw a git blob inline, reusing ycat's renderers + OSC encoder.
 *
 * This is what makes ygit worth switching to: `git show HEAD~5:logo.svg` prints
 * raw XML; here the same blob is drawn. The whole decision tree (detect →
 * streaming handler → single-shot handler → tree-sitter → raw) mirrors ycat's
 * own process_one(), so every format ycat can render, ygit renders too.
 */

#include "render.h"

#include <yetty/ycat/ycat.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/terminal-detect.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/ygit/commit-graph.h>
#include <yetty/ygit/git-backend.h>
#include <yetty/ysdf/funcs.gen.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Per-envelope callback for the streaming handlers (Markdown, PDF): forward
 * each rendered envelope straight to the OSC encoder. */
struct ygit_emit_ctx {
    FILE *out;
    size_t total;
};

static struct yetty_ycore_void_result ygit_emit_envelope(void *user_data,
                                                         const struct yetty_ydraw_drawable_list *env)
{
    struct ygit_emit_ctx *ctx = user_data;
    struct yetty_ycore_size_result emit_res = yetty_ycat_osc_bin_emit(env, ctx->out);
    if (YETTY_IS_ERR(emit_res)) {
        return YETTY_ERR(yetty_ycore_void, "ygit view: envelope emit failed", emit_res);
    }
    ctx->total += emit_res.value;
    return YETTY_OK_VOID();
}

/* Emit one already-rendered buffer, then destroy it. Returns 0 on success. */
static int ygit_emit_buffer(struct yetty_ydraw_drawable_list *buffer)
{
    struct yetty_ycore_size_result emit_res = yetty_ycat_osc_bin_emit(buffer, stdout);
    yetty_ydraw_drawable_list_destroy(buffer);
    if (YETTY_IS_ERR(emit_res)) {
        yetty_ycore_error_print(stderr, "ygit view", emit_res.error);
        yetty_ycore_error_destroy(emit_res.error);
        return 1;
    }
    return 0;
}

/* Bytes with a NUL in the first 4 KiB are treated as binary (not safe to dump
 * to a plain terminal). */
static int ygit_looks_binary(const unsigned char *bytes, size_t len)
{
    size_t scan = len < 4096 ? len : 4096;
    for (size_t index = 0; index < scan; index++) {
        if (bytes[index] == 0) {
            return 1;
        }
    }
    return 0;
}

int ygit_render_blob(const unsigned char *bytes, size_t len, const char *name, int width_cells)
{
    struct yetty_ycat_config config = {
        .cell_width = 8,
        .cell_height = 16,
        .width_cells = (uint32_t)(width_cells > 0 ? width_cells : 80),
        .height_cells = 0,
    };

    if (yetty_running_under_yetty()) {
        enum yetty_ycat_type type = yetty_ycat_detect(bytes, len, name);

        /* Markdown / PDF stream one envelope per screen-tile / page. */
        yetty_ycat_handler_streaming_fn streaming = yetty_ycat_get_handler_streaming(type);
        if (streaming) {
            struct ygit_emit_ctx ctx = {.out = stdout, .total = 0};
            struct yetty_ycore_void_result render_res =
                streaming(bytes, len, name, &config, ygit_emit_envelope, &ctx);
            if (YETTY_IS_ERR(render_res)) {
                yetty_ycore_error_print(stderr, "ygit view", render_res.error);
                yetty_ycore_error_destroy(render_res.error);
                return 1;
            }
            return 0;
        }

        /* Single-shot rich handlers: image, SVG, chart, mermaid, … */
        yetty_ycat_handler_fn handler = yetty_ycat_get_handler(type);
        if (handler) {
            struct yetty_ydraw_drawable_list_result render_res =
                handler(bytes, len, name, &config);
            if (YETTY_IS_ERR(render_res)) {
                yetty_ycore_error_print(stderr, "ygit view", render_res.error);
                yetty_ycore_error_destroy(render_res.error);
                return 1;
            }
            return ygit_emit_buffer(render_res.value);
        }

        /* Source code / plain text: draw it syntax-highlighted when a grammar
         * matches the filename. */
        const char *grammar = yetty_ycat_grammar_lookup(NULL, name);
        if (grammar) {
            struct yetty_ydraw_drawable_list_result render_res =
                yetty_ycat_ts_render(bytes, len, grammar, &config);
            if (YETTY_IS_OK(render_res)) {
                return ygit_emit_buffer(render_res.value);
            }
            yetty_ycore_error_destroy(render_res.error);
        }
        /* No grammar (or highlight failed): fall back to the raw text. */
        fwrite(bytes, 1, len, stdout);
        return 0;
    }

    /* Plain terminal: 24-bit-coloured source if we can, else raw text; refuse to
     * spew binary at the tty. */
    const char *grammar = yetty_ycat_grammar_lookup(NULL, name);
    if (grammar && yetty_ycat_ts_emit_sgr(bytes, len, grammar, stdout) == 0) {
        return 0;
    }
    if (ygit_looks_binary(bytes, len)) {
        fprintf(stderr, "ygit view: '%s' is binary (%zu bytes) — run inside yetty to render it\n",
                name, len);
        return 1;
    }
    fwrite(bytes, 1, len, stdout);
    return 0;
}

/*=============================================================================
 * Diff rendering
 *
 * Two modes, chosen per changed file:
 *   - Renderable assets (image / SVG / PDF / chart / …): draw the OLD blob and
 *     the NEW blob as inline figures — a genuine visual before/after. No diff
 *     tool shows you the old logo next to the new logo; ygit does.
 *   - Text / source: a unified diff whose code content is syntax-highlighted by
 *     the file's own tree-sitter grammar, with the changed lines banded — a
 *     step past git's line-level red/green.
 *===========================================================================*/

/* ANSI palette for the textual diff, active only when the sink is a terminal
 * (or we are inside yetty). Empty strings when writing to a pipe/file. */
struct ygit_diff_style {
    const char *header; /* file banner */
    const char *hunk;   /* @@ hunk header */
    const char *add;    /* '+' marker / whole-line add */
    const char *del;    /* '-' marker / whole-line delete */
    const char *dim;    /* context marker */
    const char *add_bg; /* addition background band */
    const char *del_bg; /* deletion background band */
    const char *reset;
    int colored;
};

static struct ygit_diff_style ygit_diff_style_get(void)
{
    if (isatty(fileno(stdout)) || yetty_running_under_yetty()) {
        return (struct ygit_diff_style){
            .header = "\x1b[1;36m",
            .hunk = "\x1b[36m",
            .add = "\x1b[32m",
            .del = "\x1b[31m",
            .dim = "\x1b[90m",
            .add_bg = "\x1b[48;2;18;40;24m",
            .del_bg = "\x1b[48;2;48;22;22m",
            .reset = "\x1b[0m",
            .colored = 1,
        };
    }
    return (struct ygit_diff_style){.header = "", .hunk = "", .add = "", .del = "", .dim = "",
                                    .add_bg = "", .del_bg = "", .reset = "", .colored = 0};
}

/* A file type worth drawing as a before/after figure rather than diffing as
 * text: anything with a single-shot rich handler (image, SVG, chart, mermaid,
 * …) plus PDF (which streams). Markdown and source fall through to the text
 * diff, where seeing which lines changed is the point. */
static int ygit_type_is_visual(enum yetty_ycat_type type)
{
    if (type == YETTY_YCAT_TYPE_PDF) {
        return 1;
    }
    return yetty_ycat_get_handler(type) != NULL;
}

static const char *ygit_status_word(char status)
{
    switch (status) {
    case 'A':
        return "added";
    case 'D':
        return "deleted";
    case 'R':
        return "renamed";
    case 'C':
        return "copied";
    case 'T':
        return "typechange";
    case 'M':
    default:
        return "modified";
    }
}

/* Per-line syntax-highlighted source: run the tree-sitter SGR emitter into a
 * memory buffer, then split it into one owned string per source line (1-based
 * line N is lines[N-1]). Returns 0 on success; lines/count are zeroed on any
 * failure so the caller falls back to raw content. */
struct ygit_hl_lines {
    char **lines;
    size_t count;
};

static void ygit_hl_lines_free(struct ygit_hl_lines *hl)
{
    for (size_t index = 0; index < hl->count; index++) {
        free(hl->lines[index]);
    }
    free(hl->lines);
    hl->lines = NULL;
    hl->count = 0;
}

static int ygit_highlight_lines(const unsigned char *bytes, size_t len, const char *grammar,
                                struct ygit_hl_lines *out)
{
    out->lines = NULL;
    out->count = 0;
    if (!grammar || !bytes || len == 0) {
        return -1;
    }

    char *buffer = NULL;
    size_t buffer_size = 0;
    FILE *mem = open_memstream(&buffer, &buffer_size);
    if (!mem) {
        return -1;
    }
    int emit_status = yetty_ycat_ts_emit_sgr(bytes, len, grammar, mem);
    fclose(mem);
    if (emit_status != 0) {
        free(buffer);
        return -1;
    }

    size_t capacity = 0;
    size_t line_start = 0;
    for (size_t index = 0; index <= buffer_size; index++) {
        if (index == buffer_size || buffer[index] == '\n') {
            size_t line_len = index - line_start;
            char *line = malloc(line_len + 1);
            if (!line) {
                free(buffer);
                ygit_hl_lines_free(out);
                return -1;
            }
            if (line_len) {
                memcpy(line, buffer + line_start, line_len);
            }
            line[line_len] = '\0';

            if (out->count == capacity) {
                size_t new_capacity = capacity ? capacity * 2 : 64;
                char **grown = realloc(out->lines, new_capacity * sizeof(*grown));
                if (!grown) {
                    free(line);
                    free(buffer);
                    ygit_hl_lines_free(out);
                    return -1;
                }
                out->lines = grown;
                capacity = new_capacity;
            }
            out->lines[out->count++] = line;
            line_start = index + 1;
            /* A trailing '\n' produces a final empty element we never index. */
            if (index == buffer_size) {
                break;
            }
        }
    }
    free(buffer);
    return 0;
}

/* Print an already-SGR-coloured line inside a background band. The tree-sitter
 * emitter resets SGR (which also clears the band) at every colour change, so
 * re-assert the band after each reset to keep it continuous across the line. */
static void ygit_print_banded(const char *colored_line, const char *band, const char *reset)
{
    static const char sgr_reset[] = "\x1b[0m";
    fputs(band, stdout);
    const char *cursor = colored_line;
    const char *hit = NULL;
    while ((hit = strstr(cursor, sgr_reset)) != NULL) {
        fwrite(cursor, 1, (size_t)(hit - cursor) + sizeof(sgr_reset) - 1, stdout);
        fputs(band, stdout); /* re-assert the band the reset just cleared */
        cursor = hit + sizeof(sgr_reset) - 1;
    }
    fputs(cursor, stdout);
    fputs(reset, stdout);
}

/* One textual line of a hunk: a coloured gutter marker, then the code content —
 * syntax-highlighted from the pre-computed side arrays when available, banded
 * for additions/deletions; otherwise the raw content in the marker colour. */
static void ygit_render_diff_line(const struct yetty_ygit_diff_line *line,
                                  const struct ygit_hl_lines *old_hl,
                                  const struct ygit_hl_lines *new_hl,
                                  const struct ygit_diff_style *style)
{
    const char *marker_color = style->dim;
    const char *band = NULL;
    const struct ygit_hl_lines *side = new_hl;
    int lineno = line->new_lineno;

    if (line->origin == '+') {
        marker_color = style->add;
        band = style->add_bg;
    } else if (line->origin == '-') {
        marker_color = style->del;
        band = style->del_bg;
        side = old_hl;
        lineno = line->old_lineno;
    }

    const char *highlighted = NULL;
    if (side && lineno > 0 && (size_t)lineno <= side->count) {
        highlighted = side->lines[lineno - 1];
    }

    printf("%s%c%s ", marker_color, line->origin, style->reset);
    if (highlighted && highlighted[0]) {
        if (style->colored && band && band[0]) {
            ygit_print_banded(highlighted, band, style->reset);
        } else {
            fputs(highlighted, stdout);
            fputs(style->reset, stdout);
        }
    } else {
        /* No grammar highlight: colour the whole line by its change kind. */
        printf("%s%s%s", marker_color, line->content, style->reset);
    }
    fputc('\n', stdout);
}

/* Syntax-highlighted unified diff for one text file. */
static void ygit_render_text_diff(const struct yetty_ygit_diff_file *file, const char *name,
                                  const struct ygit_diff_style *style)
{
    if (file->hunk_count == 0) {
        printf("%s  (no textual changes)%s\n", style->dim, style->reset);
        return;
    }

    const char *grammar = yetty_ycat_grammar_lookup(NULL, name);
    struct ygit_hl_lines old_hl = {0};
    struct ygit_hl_lines new_hl = {0};
    if (style->colored && grammar) {
        ygit_highlight_lines(file->old_data, file->old_size, grammar, &old_hl);
        ygit_highlight_lines(file->new_data, file->new_size, grammar, &new_hl);
    }

    for (size_t hunk_index = 0; hunk_index < file->hunk_count; hunk_index++) {
        const struct yetty_ygit_diff_hunk *hunk = &file->hunks[hunk_index];
        printf("%s%s%s\n", style->hunk, hunk->header, style->reset);
        for (size_t line_index = 0; line_index < hunk->line_count; line_index++) {
            ygit_render_diff_line(&hunk->lines[line_index], &old_hl, &new_hl, style);
        }
    }

    ygit_hl_lines_free(&old_hl);
    ygit_hl_lines_free(&new_hl);
}

/* Visual before/after for a renderable asset: draw whichever sides exist. */
static int ygit_render_visual_diff(const struct yetty_ygit_diff_file *file, const char *name,
                                   int width_cells, const struct ygit_diff_style *style)
{
    int status = 0;
    if (file->old_data) {
        printf("%s  ── before ──%s\n", style->dim, style->reset);
        status |= ygit_render_blob(file->old_data, file->old_size, name, width_cells);
    }
    if (file->new_data) {
        printf("%s  ── after ──%s\n", style->dim, style->reset);
        status |= ygit_render_blob(file->new_data, file->new_size, name, width_cells);
    }
    return status;
}

int ygit_render_diff(const struct yetty_ygit_diff *diff, int width_cells)
{
    struct ygit_diff_style style = ygit_diff_style_get();
    int status = 0;

    for (size_t index = 0; index < diff->file_count; index++) {
        const struct yetty_ygit_diff_file *file = &diff->files[index];
        /* The path/name for detection: prefer the post-image, fall back to the
         * pre-image (a deletion has only the old side). */
        const char *path = file->new_path ? file->new_path : file->old_path;
        const char *name = path ? path : "(unknown)";
        const char *base = strrchr(name, '/');
        base = base ? base + 1 : name;

        printf("\n%s━━ %s ── %s ━━%s\n", style.header, name, ygit_status_word(file->status),
               style.reset);

        if (file->is_binary) {
            /* Binary but renderable (an image!) → draw before/after; otherwise
             * there is nothing textual to show. */
            const unsigned char *probe = file->new_data ? file->new_data : file->old_data;
            size_t probe_len = file->new_data ? file->new_size : file->old_size;
            enum yetty_ycat_type type = probe ? yetty_ycat_detect(probe, probe_len, base)
                                              : YETTY_YCAT_TYPE_UNKNOWN;
            if (probe && ygit_type_is_visual(type)) {
                status |= ygit_render_visual_diff(file, base, width_cells, &style);
            } else {
                printf("%s  Binary file differs%s\n", style.dim, style.reset);
            }
            continue;
        }

        const unsigned char *probe = file->new_data ? file->new_data : file->old_data;
        size_t probe_len = file->new_data ? file->new_size : file->old_size;
        enum yetty_ycat_type type =
            probe ? yetty_ycat_detect(probe, probe_len, base) : YETTY_YCAT_TYPE_UNKNOWN;

        if (probe && ygit_type_is_visual(type) && yetty_running_under_yetty()) {
            status |= ygit_render_visual_diff(file, base, width_cells, &style);
        } else {
            ygit_render_text_diff(file, base, &style);
        }
    }
    return status;
}

/*=============================================================================
 * Commit DAG as a GPU figure
 *
 * The lane layout in commit-graph.c keeps every lane in a fixed column, so each
 * segment between two adjacent rows is fully determined by the two rows'
 * occupancy plus their commit columns: a lane occupied in both rows is a
 * straight line; one that ends is a branch collapsing into the lower commit; one
 * that appears was opened by the upper commit (a merge). Nodes are filled
 * circles (merges ringed); hash / refs / subject are drawn as text alongside.
 *===========================================================================*/

/* Geometry (device pixels). */
#define YGIT_FIG_FONT 15.0f
#define YGIT_FIG_ROW_H 26.0f
#define YGIT_FIG_LANE_W 20.0f
#define YGIT_FIG_NODE_R 5.5f
#define YGIT_FIG_MARGIN_X 12.0f
#define YGIT_FIG_MARGIN_Y 16.0f
#define YGIT_FIG_LINE_W 2.0f
#define YGIT_FIG_ADVANCE (YGIT_FIG_FONT * 0.6f)

/* Colours, packed 0xAABBGGRR (the format every ydraw shader unpacks). */
#define YGIT_FIG_HASH 0xFF87C7D7u    /* soft yellow */
#define YGIT_FIG_REF 0xFF92A86Bu     /* brand accent #6BA892 */
#define YGIT_FIG_SUBJECT 0xFFE4E5E0u /* brand primary text #E0E5E4 */
#define YGIT_FIG_META 0xFFA8A79Fu    /* brand secondary #9FA7A8 */
#define YGIT_FIG_RING 0xFFE4E5E0u    /* merge-node ring */

/* A distinct hue per lane so parallel branches read apart. */
static uint32_t ygit_fig_lane_color(int column)
{
    static const uint32_t palette[] = {
        0xFFA5C574u, /* green  #74C5A5 */
        0xFFE0A06Au, /* blue   #6AA0E0 */
        0xFF60B0E0u, /* amber  #E0B060 */
        0xFFC080D0u, /* pink   #D080C0 */
        0xFFC0C060u, /* teal   #60C0C0 */
        0xFFE080A0u, /* purple #A080E0 */
    };
    int count = (int)(sizeof(palette) / sizeof(palette[0]));
    return palette[((column % count) + count) % count];
}

static float ygit_fig_lane_x(int column)
{
    return YGIT_FIG_MARGIN_X + (float)column * YGIT_FIG_LANE_W;
}

static float ygit_fig_node_cy(size_t row)
{
    return YGIT_FIG_MARGIN_Y + (float)row * YGIT_FIG_ROW_H + YGIT_FIG_FONT * 0.35f;
}

static int ygit_fig_segment(struct yetty_ydraw_drawable_list *buf, float x0, float y0, float x1,
                            float y1, uint32_t color)
{
    struct yetty_ysdf_segment geom = {x0, y0, x1, y1};
    struct yetty_ycore_void_result add_res =
        yetty_ydraw_drawable_list_add_cmd_add_segment(buf, 0, 0, 0u, color, YGIT_FIG_LINE_W, &geom);
    if (YETTY_IS_ERR(add_res)) {
        yetty_ycore_error_destroy(add_res.error);
        return -1;
    }
    return 0;
}

static int ygit_fig_circle(struct yetty_ydraw_drawable_list *buf, float cx, float cy, float radius,
                           uint32_t fill, uint32_t stroke, float stroke_w)
{
    struct yetty_ysdf_circle geom = {cx, cy, radius};
    struct yetty_ycore_void_result add_res =
        yetty_ydraw_drawable_list_add_cmd_add_circle(buf, 0, 1, fill, stroke, stroke_w, &geom);
    if (YETTY_IS_ERR(add_res)) {
        yetty_ycore_error_destroy(add_res.error);
        return -1;
    }
    return 0;
}

/* Draw a text span, returning the advanced x. Sets *err (and no-ops) on the
 * first failure so a chain of calls can be checked once at the end. */
static float ygit_fig_text(struct yetty_ydraw_drawable_list *buf, float x, float y,
                           const char *text, uint32_t color, int *err)
{
    if (*err || !text || !text[0]) {
        return x;
    }
    size_t len = strlen(text);
    struct yetty_ycore_buffer span = {
        .data = (uint8_t *)(uintptr_t)text,
        .size = len,
        .capacity = len,
    };
    struct yetty_ycore_void_result add_res =
        yetty_ydraw_drawable_list_add_text(buf, x, y, &span, YGIT_FIG_FONT, color, 2, -1, 0.0f);
    if (YETTY_IS_ERR(add_res)) {
        yetty_ycore_error_destroy(add_res.error);
        *err = 1;
        return x;
    }
    return x + YGIT_FIG_ADVANCE * (float)len;
}

/* Approximate character count of a row's composed text, to size the scene. */
static size_t ygit_fig_row_chars(const struct yetty_ygit_commit *commit)
{
    size_t chars = strlen(commit->abbrev_hash) + 2;
    for (size_t index = 0; index < commit->ref_count; index++) {
        chars += strlen(commit->ref_names[index]) + 3;
    }
    chars += strlen(commit->subject) + 3 + strlen(commit->author_name);
    return chars;
}

static int ygit_fig_row_occupied(const struct yetty_ygit_graph_row *row, int lane)
{
    return lane >= 0 && lane < row->lane_count && row->occupied[lane];
}

/* Lane segments between row i and row i+1. */
static int ygit_fig_draw_edges(struct yetty_ydraw_drawable_list *buf,
                               const struct yetty_ygit_log *log, const struct yetty_ygit_graph *graph,
                               size_t upper)
{
    const struct yetty_ygit_graph_row *top = &graph->rows[upper];
    const struct yetty_ygit_graph_row *bottom = &graph->rows[upper + 1];
    float y_top = ygit_fig_node_cy(upper);
    float y_bottom = ygit_fig_node_cy(upper + 1);
    int lanes = top->lane_count > bottom->lane_count ? top->lane_count : bottom->lane_count;

    for (int lane = 0; lane < lanes; lane++) {
        int in_top = ygit_fig_row_occupied(top, lane);
        int in_bottom = ygit_fig_row_occupied(bottom, lane);
        if (in_top && in_bottom) {
            /* Lane continues straight down its column. */
            if (ygit_fig_segment(buf, ygit_fig_lane_x(lane), y_top, ygit_fig_lane_x(lane), y_bottom,
                                  ygit_fig_lane_color(lane)) < 0) {
                return -1;
            }
        } else if (in_top && !in_bottom) {
            /* Lane ends: the root's own lane simply stops; any other lane
             * collapses into the lower commit's node. */
            if (lane == top->column && log->commits[upper].parent_count == 0) {
                continue;
            }
            if (ygit_fig_segment(buf, ygit_fig_lane_x(lane), y_top, ygit_fig_lane_x(bottom->column),
                                  y_bottom, ygit_fig_lane_color(lane)) < 0) {
                return -1;
            }
        } else if (!in_top && in_bottom) {
            /* Lane opened by the upper commit (a merge's extra parent). */
            if (ygit_fig_segment(buf, ygit_fig_lane_x(top->column), y_top, ygit_fig_lane_x(lane),
                                  y_bottom, ygit_fig_lane_color(lane)) < 0) {
                return -1;
            }
        }
    }
    return 0;
}

static struct yetty_ydraw_drawable_list_result ygit_build_graph_figure(
    const struct yetty_ygit_log *log, const struct yetty_ygit_graph *graph)
{
    /* Widest composed row → scene width; row count → scene height. */
    size_t widest = 0;
    for (size_t index = 0; index < log->count; index++) {
        size_t chars = ygit_fig_row_chars(&log->commits[index]);
        if (chars > widest) {
            widest = chars;
        }
    }
    float text_x = YGIT_FIG_MARGIN_X + (float)graph->width * YGIT_FIG_LANE_W + 14.0f;
    struct yetty_ydraw_drawable_list_config config = {
        .scene_min_x = 0.0f,
        .scene_min_y = 0.0f,
        .scene_max_x = text_x + (float)widest * YGIT_FIG_ADVANCE + 20.0f,
        .scene_max_y = YGIT_FIG_MARGIN_Y + (float)log->count * YGIT_FIG_ROW_H + 10.0f,
    };
    struct yetty_ydraw_drawable_list_result buffer_res =
        yetty_ydraw_drawable_list_config_buffer_create(&config);
    if (YETTY_IS_ERR(buffer_res)) {
        return buffer_res;
    }
    struct yetty_ydraw_drawable_list *buf = buffer_res.value;

    /* Lane lines first (under the nodes). */
    for (size_t upper = 0; upper + 1 < graph->count; upper++) {
        if (ygit_fig_draw_edges(buf, log, graph, upper) < 0) {
            yetty_ydraw_drawable_list_destroy(buf);
            return YETTY_ERR(yetty_ydraw_drawable_list, "ygit graph figure: lane emit failed");
        }
    }

    /* Nodes + per-commit text. */
    int text_err = 0;
    for (size_t index = 0; index < graph->count; index++) {
        const struct yetty_ygit_commit *commit = &log->commits[index];
        const struct yetty_ygit_graph_row *row = &graph->rows[index];
        float cx = ygit_fig_lane_x(row->column);
        float cy = ygit_fig_node_cy(index);
        int is_merge = commit->parent_count >= 2;
        if (ygit_fig_circle(buf, cx, cy, YGIT_FIG_NODE_R, ygit_fig_lane_color(row->column),
                            is_merge ? YGIT_FIG_RING : 0u, is_merge ? 1.5f : 0.0f) < 0) {
            yetty_ydraw_drawable_list_destroy(buf);
            return YETTY_ERR(yetty_ydraw_drawable_list, "ygit graph figure: node emit failed");
        }

        float baseline = YGIT_FIG_MARGIN_Y + (float)index * YGIT_FIG_ROW_H;
        float x = ygit_fig_text(buf, text_x, baseline, commit->abbrev_hash, YGIT_FIG_HASH, &text_err);
        x += YGIT_FIG_ADVANCE;
        for (size_t ref = 0; ref < commit->ref_count; ref++) {
            char label[128];
            snprintf(label, sizeof(label), "%s%s", ref ? " " : "", commit->ref_names[ref]);
            x = ygit_fig_text(buf, x, baseline, label, YGIT_FIG_REF, &text_err);
        }
        x += YGIT_FIG_ADVANCE;
        x = ygit_fig_text(buf, x, baseline, commit->subject, YGIT_FIG_SUBJECT, &text_err);
        char meta[160];
        snprintf(meta, sizeof(meta), "  · %s", commit->author_name);
        ygit_fig_text(buf, x, baseline, meta, YGIT_FIG_META, &text_err);
    }
    if (text_err) {
        yetty_ydraw_drawable_list_destroy(buf);
        return YETTY_ERR(yetty_ydraw_drawable_list, "ygit graph figure: text emit failed");
    }

    return YETTY_OK(yetty_ydraw_drawable_list, buf);
}

int ygit_render_graph_figure(const struct yetty_ygit_log *log, const struct yetty_ygit_graph *graph,
                             int width_cells)
{
    (void)width_cells;
    if (log->count == 0) {
        return 0;
    }
    struct yetty_ydraw_drawable_list_result figure_res = ygit_build_graph_figure(log, graph);
    if (YETTY_IS_ERR(figure_res)) {
        yetty_ycore_error_print(stderr, "ygit graph", figure_res.error);
        yetty_ycore_error_destroy(figure_res.error);
        return 1;
    }
    return ygit_emit_buffer(figure_res.value);
}
