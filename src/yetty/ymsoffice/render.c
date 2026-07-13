/*
 * render.c - neutral ymsoffice model → ydraw buffer.
 *
 * Same geometric conventions as ymarkdown: layout starts at (2, 2), text
 * advances 0.6 * font_size per byte, lines advance font_size * 1.4, glyph
 * baselines sit 0.8 * font_size below the line top. Word documents flow as
 * wrapped paragraphs / tables / image placeholders; workbooks draw as a
 * bordered grid with A/1 headers per sheet; decks draw one scaled slide box
 * per slide with its shapes inside.
 *
 * Dark-terminal readability: document colors close to black (Word's default
 * body color) are remapped to the off-white body color unless the run sits
 * on a highlight, where the original (usually dark) color stays readable.
 */

#include <yetty/ymsoffice/render.h>

#include <yetty/ysdf/funcs.gen.h>
#include <yetty/ysdf/types.gen.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MSO_DEFAULT_FONT_SIZE 14.0f
#define MSO_LINE_SPACING 1.4f
#define MSO_CHAR_W 0.6f
#define MSO_ASCENT 0.8f
#define MSO_MARGIN 2.0f
#define MSO_LIST_INDENT 20.0f
#define MSO_PARAGRAPH_GAP 0.4f /* fraction of a base line advance */
#define MSO_BODY_PT 11.0f      /* Word's default body size — maps to base px */

#define MSO_TABLE_PAD 6.0f
#define MSO_TABLE_VPAD 3.0f
#define MSO_TABLE_MIN_COLW 24.0f
#define MSO_TABLE_BORDER_W 1.0f

#define MSO_SHEET_MAX_ROWS 500
#define MSO_SHEET_MAX_COLS 64
#define MSO_SHEET_MIN_COLW 48.0f
#define MSO_SHEET_MAX_COLW 300.0f

#define MSO_SLIDE_GAP 14.0f
#define MSO_SHAPE_PAD 4.0f
#define MSO_SLIDE_DEFAULT_TEXT_PT 18.0f

/* ydraw packs colors as 0xAABBGGRR (see ymarkdown.c for the convention). */
#define MSO_COLOR_TEXT 0xFFE6E6E6u
#define MSO_COLOR_BOLD 0xFFFFFFFFu
#define MSO_COLOR_HEADER 0xFFFFFFFFu
#define MSO_COLOR_LINK 0xFFA5C574u     /* #74C5A5 accent bright */
#define MSO_COLOR_MUTED 0xFFA8A79Fu    /* #9FA7A8 secondary text */
#define MSO_COLOR_BORDER 0xFF474A36u   /* #364A47 border */
#define MSO_COLOR_HDR_BG 0xFF2C261Eu   /* #1E262C raised row */
#define MSO_COLOR_PANEL_BG 0xFF1F1A14u /* #141A1F lifted background */
#define MSO_COLOR_TRANSPARENT 0x00000000u

struct mso_ctx {
    struct yetty_ydraw_drawable_list *buffer;
    float base_font_px;
    float pt_scale; /* px per document point */
    float content_w;
    float cursor_y;
};

/*=============================================================================
 * Color helpers
 *===========================================================================*/

static uint32_t mso_pack_rgb(uint32_t rgb)
{
    uint32_t red = (rgb >> 16) & 0xFFu;
    uint32_t green = (rgb >> 8) & 0xFFu;
    uint32_t blue = rgb & 0xFFu;
    return 0xFF000000u | (blue << 16) | (green << 8) | red;
}

static uint32_t mso_luma(uint32_t rgb)
{
    uint32_t red = (rgb >> 16) & 0xFFu;
    uint32_t green = (rgb >> 8) & 0xFFu;
    uint32_t blue = rgb & 0xFFu;
    return (red * 299u + green * 587u + blue * 114u) / 1000u;
}

static uint32_t mso_run_color(const struct yetty_ymsoffice_text_run *run, int heading_level)
{
    if (run->has_color) {
        /* Keep explicit colors, except near-black on the dark canvas —
         * unless a highlight box sits behind the text. */
        if (run->has_highlight || mso_luma(run->color_rgb) >= 80u) {
            return mso_pack_rgb(run->color_rgb);
        }
    } else if (run->has_highlight) {
        return 0xFF000000u; /* default ink on a bright highlight */
    }
    if (run->hyperlink) {
        return MSO_COLOR_LINK;
    }
    if (heading_level > 0) {
        return MSO_COLOR_HEADER;
    }
    if (run->bold) {
        return MSO_COLOR_BOLD;
    }
    return MSO_COLOR_TEXT;
}

/*=============================================================================
 * Primitive wrappers
 *===========================================================================*/

static struct yetty_ycore_void_result mso_emit_text(struct mso_ctx *ctx, float x, float baseline,
                                                    const char *text, size_t text_len,
                                                    float font_px, uint32_t color)
{
    struct yetty_ycore_buffer text_buffer = {
        .data = (uint8_t *)(uintptr_t)text,
        .size = text_len,
        .capacity = text_len,
    };
    return yetty_ydraw_drawable_list_add_text(ctx->buffer, x, baseline, &text_buffer, font_px,
                                              color, 0, -1, 0.0f);
}

static struct yetty_ycore_void_result mso_emit_box(struct mso_ctx *ctx, float center_x,
                                                   float center_y, float half_w, float half_h,
                                                   uint32_t fill, uint32_t stroke,
                                                   float stroke_width)
{
    struct yetty_ysdf_box geometry = {
        .center_x = center_x,
        .center_y = center_y,
        .half_width = half_w,
        .half_height = half_h,
        .corner_radius = 0.0f,
    };
    return yetty_ydraw_drawable_list_add_cmd_add_box(ctx->buffer, 0, 0, fill, stroke, stroke_width,
                                                     &geometry);
}

static struct yetty_ycore_void_result mso_emit_segment(struct mso_ctx *ctx, float x0, float y0,
                                                       float x1, float y1, uint32_t color,
                                                       float width)
{
    struct yetty_ysdf_segment geometry = {
        .start_x = x0,
        .start_y = y0,
        .end_x = x1,
        .end_y = y1,
    };
    return yetty_ydraw_drawable_list_add_cmd_add_segment(ctx->buffer, 0, 0, 0u, color, width,
                                                         &geometry);
}

static struct yetty_ycore_void_result mso_emit_ellipse(struct mso_ctx *ctx, float center_x,
                                                       float center_y, float radius_x,
                                                       float radius_y, uint32_t fill,
                                                       uint32_t stroke, float stroke_width)
{
    struct yetty_ysdf_ellipse geometry = {
        .center_x = center_x,
        .center_y = center_y,
        .radius_x = radius_x,
        .radius_y = radius_y,
    };
    return yetty_ydraw_drawable_list_add_cmd_add_ellipse(ctx->buffer, 0, 0, fill, stroke,
                                                         stroke_width, &geometry);
}

/*=============================================================================
 * Paragraph layout: greedy word wrap across styled runs
 *===========================================================================*/

struct mso_fragment {
    const struct yetty_ymsoffice_text_run *run;
    const char *text;
    size_t len;
    float x; /* line-relative */
    float width;
    float font_px;
};

struct mso_row {
    size_t first_fragment;
    size_t fragment_count;
    float width;
    float font_px; /* tallest font on the row */
};

struct mso_lines {
    struct mso_fragment *fragments;
    size_t fragment_count;
    size_t fragment_cap;
    struct mso_row *rows;
    size_t row_count;
    size_t row_cap;
};

static void mso_lines_free(struct mso_lines *lines)
{
    free(lines->fragments);
    free(lines->rows);
    memset(lines, 0, sizeof(*lines));
}

static float mso_heading_scale(int heading_level)
{
    if (heading_level <= 0) {
        return 1.0f;
    }
    int capped = heading_level > 6 ? 6 : heading_level;
    return 1.0f + 0.15f * (float)(7 - capped);
}

static float mso_run_font_px(const struct mso_ctx *ctx,
                             const struct yetty_ymsoffice_paragraph *paragraph,
                             const struct yetty_ymsoffice_text_run *run)
{
    if (run->font_size_pt > 0.0f) {
        return run->font_size_pt * ctx->pt_scale;
    }
    return ctx->base_font_px * mso_heading_scale(paragraph->heading_level);
}

static int mso_lines_push_row(struct mso_lines *lines, float default_font_px)
{
    if (lines->row_count == lines->row_cap) {
        size_t new_cap = lines->row_cap ? lines->row_cap * 2 : 4;
        struct mso_row *grown = realloc(lines->rows, new_cap * sizeof(*grown));
        if (!grown) {
            return -1;
        }
        lines->rows = grown;
        lines->row_cap = new_cap;
    }
    struct mso_row *row = &lines->rows[lines->row_count++];
    row->first_fragment = lines->fragment_count;
    row->fragment_count = 0;
    row->width = 0.0f;
    row->font_px = default_font_px;
    return 0;
}

static int mso_lines_push_fragment(struct mso_lines *lines, struct mso_fragment fragment)
{
    if (lines->fragment_count == lines->fragment_cap) {
        size_t new_cap = lines->fragment_cap ? lines->fragment_cap * 2 : 8;
        struct mso_fragment *grown = realloc(lines->fragments, new_cap * sizeof(*grown));
        if (!grown) {
            return -1;
        }
        lines->fragments = grown;
        lines->fragment_cap = new_cap;
    }
    lines->fragments[lines->fragment_count++] = fragment;
    struct mso_row *row = &lines->rows[lines->row_count - 1];
    row->fragment_count++;
    if (fragment.font_px > row->font_px) {
        row->font_px = fragment.font_px;
    }
    return 0;
}

/* Wrap a paragraph into rows of fragments for the given available width. */
static int mso_paragraph_layout(const struct mso_ctx *ctx,
                                const struct yetty_ymsoffice_paragraph *paragraph, float avail_w,
                                struct mso_lines *lines)
{
    float default_font = ctx->base_font_px * mso_heading_scale(paragraph->heading_level);
    if (mso_lines_push_row(lines, default_font) < 0) {
        return -1;
    }
    if (avail_w < 8.0f) {
        avail_w = 8.0f;
    }

    for (size_t run_index = 0; run_index < paragraph->run_count; run_index++) {
        const struct yetty_ymsoffice_text_run *run = &paragraph->runs[run_index];
        float font_px = mso_run_font_px(ctx, paragraph, run);
        float char_w = font_px * MSO_CHAR_W;
        const char *text = run->text;
        size_t len = run->text_len;
        size_t pos = 0;
        float pending_space = 0.0f;

        while (pos < len) {
            if (text[pos] == '\n') {
                if (mso_lines_push_row(lines, default_font) < 0) {
                    return -1;
                }
                pending_space = 0.0f;
                pos++;
                continue;
            }
            if (text[pos] == ' ') {
                pending_space += char_w;
                pos++;
                continue;
            }
            size_t word_start = pos;
            while (pos < len && text[pos] != ' ' && text[pos] != '\n') {
                pos++;
            }
            size_t word_len = pos - word_start;
            float word_w = (float)word_len * char_w;

            struct mso_row *row = &lines->rows[lines->row_count - 1];
            if (row->fragment_count > 0 && row->width + pending_space + word_w > avail_w) {
                if (mso_lines_push_row(lines, default_font) < 0) {
                    return -1;
                }
                row = &lines->rows[lines->row_count - 1];
                pending_space = 0.0f;
            }
            struct mso_fragment fragment = {
                .run = run,
                .text = text + word_start,
                .len = word_len,
                .x = row->width + pending_space,
                .width = word_w,
                .font_px = font_px,
            };
            if (mso_lines_push_fragment(lines, fragment) < 0) {
                return -1;
            }
            lines->rows[lines->row_count - 1].width += pending_space + word_w;
            pending_space = 0.0f;
        }
        /* Trailing spaces separate this run's last word from the next
         * run's first word. */
        if (pending_space > 0.0f && lines->rows[lines->row_count - 1].fragment_count > 0) {
            lines->rows[lines->row_count - 1].width += pending_space;
        }
    }
    return 0;
}

static float mso_lines_height(const struct mso_lines *lines)
{
    float height = 0.0f;
    for (size_t i = 0; i < lines->row_count; i++) {
        height += lines->rows[i].font_px * MSO_LINE_SPACING;
    }
    return height;
}

static float mso_lines_max_width(const struct mso_lines *lines)
{
    float width = 0.0f;
    for (size_t i = 0; i < lines->row_count; i++) {
        if (lines->rows[i].width > width) {
            width = lines->rows[i].width;
        }
    }
    return width;
}

/* Emit laid-out rows at (x0, *cursor_y), honouring paragraph alignment
 * within avail_w. Advances *cursor_y by the block height. */
static struct yetty_ycore_void_result mso_lines_emit(
    struct mso_ctx *ctx, const struct mso_lines *lines,
    const struct yetty_ymsoffice_paragraph *paragraph, float x0, float avail_w, float *cursor_y)
{
    float y = *cursor_y;
    for (size_t row_index = 0; row_index < lines->row_count; row_index++) {
        const struct mso_row *row = &lines->rows[row_index];
        float align_offset = 0.0f;
        if (paragraph->align == YETTY_YMSOFFICE_ALIGN_CENTER && avail_w > row->width) {
            align_offset = (avail_w - row->width) * 0.5f;
        } else if (paragraph->align == YETTY_YMSOFFICE_ALIGN_RIGHT && avail_w > row->width) {
            align_offset = avail_w - row->width;
        }
        float row_h = row->font_px * MSO_LINE_SPACING;
        float baseline = y + row->font_px * MSO_ASCENT;

        for (size_t f = 0; f < row->fragment_count; f++) {
            const struct mso_fragment *fragment = &lines->fragments[row->first_fragment + f];
            const struct yetty_ymsoffice_text_run *run = fragment->run;
            float x = x0 + align_offset + fragment->x;

            if (run->has_highlight) {
                struct yetty_ycore_void_result highlight_res =
                    mso_emit_box(ctx, x + fragment->width * 0.5f, y + fragment->font_px * 0.55f,
                                 fragment->width * 0.5f + 1.0f, fragment->font_px * 0.55f,
                                 mso_pack_rgb(run->highlight_rgb), 0u, 0.0f);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, highlight_res, "ymsoffice: highlight box");
            }

            uint32_t color = mso_run_color(run, paragraph->heading_level);
            struct yetty_ycore_void_result text_res = mso_emit_text(
                ctx, x, baseline, fragment->text, fragment->len, fragment->font_px, color);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, text_res, "ymsoffice: text span");

            if (run->bold) {
                /* Faux bold: a second draw offset by a fraction of a pixel. */
                struct yetty_ycore_void_result bold_res =
                    mso_emit_text(ctx, x + 0.6f, baseline, fragment->text, fragment->len,
                                  fragment->font_px, color);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, bold_res, "ymsoffice: bold overdraw");
            }
            if (run->underline) {
                struct yetty_ycore_void_result underline_res = mso_emit_segment(
                    ctx, x, baseline + 2.0f, x + fragment->width, baseline + 2.0f, color, 1.0f);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, underline_res, "ymsoffice: underline");
            }
            if (run->strike) {
                struct yetty_ycore_void_result strike_res =
                    mso_emit_segment(ctx, x, y + fragment->font_px * 0.5f, x + fragment->width,
                                     y + fragment->font_px * 0.5f, MSO_COLOR_MUTED, 1.0f);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, strike_res, "ymsoffice: strike");
            }
        }
        y += row_h;
    }
    *cursor_y = y;
    return YETTY_OK_VOID();
}

/* Layout + emit one paragraph at x0 within avail_w; advances *cursor_y. */
static struct yetty_ycore_void_result mso_emit_paragraph(
    struct mso_ctx *ctx, const struct yetty_ymsoffice_paragraph *paragraph, float x0, float avail_w,
    float *cursor_y)
{
    struct mso_lines lines = {0};
    if (mso_paragraph_layout(ctx, paragraph, avail_w, &lines) < 0) {
        mso_lines_free(&lines);
        return YETTY_ERR(yetty_ycore_void, "ymsoffice: out of memory (layout)");
    }
    struct yetty_ycore_void_result emit_res =
        mso_lines_emit(ctx, &lines, paragraph, x0, avail_w, cursor_y);
    mso_lines_free(&lines);
    return emit_res;
}

struct mso_paragraph_metrics {
    float height;
    float max_width;
};

/* Measure a paragraph without emitting. Returns 0 / -1 on allocation
 * failure. */
static int mso_measure_paragraph(const struct mso_ctx *ctx,
                                 const struct yetty_ymsoffice_paragraph *paragraph, float avail_w,
                                 struct mso_paragraph_metrics *out_metrics)
{
    struct mso_lines lines = {0};
    if (mso_paragraph_layout(ctx, paragraph, avail_w, &lines) < 0) {
        mso_lines_free(&lines);
        return -1;
    }
    out_metrics->height = mso_lines_height(&lines);
    out_metrics->max_width = mso_lines_max_width(&lines);
    mso_lines_free(&lines);
    return 0;
}

/*=============================================================================
 * Word document
 *===========================================================================*/

/* Running ordinals for numbered lists, one per indent level. */
struct mso_list_state {
    int ordinal[10];
};

static struct yetty_ycore_void_result mso_emit_word_paragraph_block(
    struct mso_ctx *ctx, const struct yetty_ymsoffice_paragraph *paragraph,
    struct mso_list_state *list_state)
{
    float base_line = ctx->base_font_px * MSO_LINE_SPACING;

    if (paragraph->run_count == 0) {
        ctx->cursor_y += base_line; /* empty paragraph = vertical space */
        memset(list_state, 0, sizeof(*list_state));
        return YETTY_OK_VOID();
    }

    float x0 = MSO_MARGIN;
    float avail_w = ctx->content_w;

    if (paragraph->list_level >= 0) {
        int level = paragraph->list_level > 8 ? 8 : paragraph->list_level;
        float indent = MSO_LIST_INDENT * (float)(level + 1);
        char prefix[24];
        if (paragraph->list_ordered) {
            for (int deeper = level + 1; deeper < 10; deeper++) {
                list_state->ordinal[deeper] = 0;
            }
            list_state->ordinal[level]++;
            snprintf(prefix, sizeof(prefix), "%d. ", list_state->ordinal[level]);
        } else {
            snprintf(prefix, sizeof(prefix), "\xE2\x80\xA2 "); /* "• " */
        }
        size_t prefix_len = strlen(prefix);
        float prefix_w = (float)prefix_len * ctx->base_font_px * MSO_CHAR_W;
        float prefix_x = MSO_MARGIN + indent - prefix_w;
        if (prefix_x < MSO_MARGIN) {
            prefix_x = MSO_MARGIN;
        }
        struct yetty_ycore_void_result prefix_res =
            mso_emit_text(ctx, prefix_x, ctx->cursor_y + ctx->base_font_px * MSO_ASCENT, prefix,
                          prefix_len, ctx->base_font_px, MSO_COLOR_TEXT);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, prefix_res, "ymsoffice: list prefix");
        x0 = MSO_MARGIN + indent;
        avail_w = ctx->content_w > indent ? ctx->content_w - indent : 8.0f;
    } else {
        memset(list_state, 0, sizeof(*list_state));
    }

    struct yetty_ycore_void_result emit_res =
        mso_emit_paragraph(ctx, paragraph, x0, avail_w, &ctx->cursor_y);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, emit_res, "ymsoffice: paragraph");
    ctx->cursor_y += base_line * MSO_PARAGRAPH_GAP;
    return YETTY_OK_VOID();
}

static size_t mso_table_column_count(const struct yetty_ymsoffice_table *table)
{
    size_t max_cols = 0;
    for (size_t r = 0; r < table->row_count; r++) {
        const struct yetty_ymsoffice_table_row *row = &table->rows[r];
        size_t cols = 0;
        for (size_t c = 0; c < row->cell_count; c++) {
            cols += (size_t)(row->cells[c].col_span > 0 ? row->cells[c].col_span : 1);
        }
        if (cols > max_cols) {
            max_cols = cols;
        }
    }
    return max_cols > 64 ? 64 : max_cols;
}

static struct yetty_ycore_void_result mso_emit_word_table(struct mso_ctx *ctx,
                                                          const struct yetty_ymsoffice_table *table)
{
    size_t ncols = mso_table_column_count(table);
    if (ncols == 0 || table->row_count == 0) {
        return YETTY_OK_VOID();
    }

    float column_width[64];
    for (size_t c = 0; c < ncols; c++) {
        column_width[c] = MSO_TABLE_MIN_COLW;
    }

    /* Column widths from unwrapped span-1 cell content. */
    for (size_t r = 0; r < table->row_count; r++) {
        const struct yetty_ymsoffice_table_row *row = &table->rows[r];
        size_t col = 0;
        for (size_t c = 0; c < row->cell_count && col < ncols; c++) {
            const struct yetty_ymsoffice_table_cell *cell = &row->cells[c];
            size_t span = (size_t)(cell->col_span > 0 ? cell->col_span : 1);
            if (span == 1 && !cell->merged_continue) {
                float widest = 0.0f;
                for (size_t p = 0; p < cell->paragraph_count; p++) {
                    struct mso_paragraph_metrics metrics;
                    if (mso_measure_paragraph(ctx, &cell->paragraphs[p], 1.0e9f, &metrics) < 0) {
                        return YETTY_ERR(yetty_ycore_void,
                                         "ymsoffice: out of memory (table measure)");
                    }
                    if (metrics.max_width > widest) {
                        widest = metrics.max_width;
                    }
                }
                float needed = widest + 2.0f * MSO_TABLE_PAD;
                if (needed > column_width[col]) {
                    column_width[col] = needed;
                }
            }
            col += span;
        }
    }

    float table_w = 0.0f;
    for (size_t c = 0; c < ncols; c++) {
        table_w += column_width[c];
    }
    if (table_w > ctx->content_w && table_w > 0.0f) {
        float scale = ctx->content_w / table_w;
        for (size_t c = 0; c < ncols; c++) {
            column_width[c] *= scale;
        }
        table_w = ctx->content_w;
    }

    float base_line = ctx->base_font_px * MSO_LINE_SPACING;

    for (size_t r = 0; r < table->row_count; r++) {
        const struct yetty_ymsoffice_table_row *row = &table->rows[r];

        /* Row height = tallest wrapped cell. */
        float row_h = base_line;
        {
            size_t col = 0;
            for (size_t c = 0; c < row->cell_count && col < ncols; c++) {
                const struct yetty_ymsoffice_table_cell *cell = &row->cells[c];
                size_t span = (size_t)(cell->col_span > 0 ? cell->col_span : 1);
                float cell_w = 0.0f;
                for (size_t s = 0; s < span && col + s < ncols; s++) {
                    cell_w += column_width[col + s];
                }
                if (!cell->merged_continue) {
                    float content_h = 0.0f;
                    for (size_t p = 0; p < cell->paragraph_count; p++) {
                        struct mso_paragraph_metrics metrics;
                        if (mso_measure_paragraph(ctx, &cell->paragraphs[p],
                                                  cell_w - 2.0f * MSO_TABLE_PAD, &metrics) < 0) {
                            return YETTY_ERR(yetty_ycore_void,
                                             "ymsoffice: out of memory (row measure)");
                        }
                        content_h += metrics.height;
                    }
                    if (content_h > row_h) {
                        row_h = content_h;
                    }
                }
                col += span;
            }
        }
        row_h += 2.0f * MSO_TABLE_VPAD;

        /* Emit the row: bordered cell boxes + wrapped content. */
        float x = MSO_MARGIN;
        size_t col = 0;
        for (size_t c = 0; c < row->cell_count && col < ncols; c++) {
            const struct yetty_ymsoffice_table_cell *cell = &row->cells[c];
            size_t span = (size_t)(cell->col_span > 0 ? cell->col_span : 1);
            float cell_w = 0.0f;
            for (size_t s = 0; s < span && col + s < ncols; s++) {
                cell_w += column_width[col + s];
            }

            struct yetty_ycore_void_result border_res = mso_emit_box(
                ctx, x + cell_w * 0.5f, ctx->cursor_y + row_h * 0.5f, cell_w * 0.5f, row_h * 0.5f,
                MSO_COLOR_TRANSPARENT, MSO_COLOR_BORDER, MSO_TABLE_BORDER_W);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, border_res, "ymsoffice: cell border");

            if (!cell->merged_continue) {
                float cell_cursor = ctx->cursor_y + MSO_TABLE_VPAD;
                for (size_t p = 0; p < cell->paragraph_count; p++) {
                    struct yetty_ycore_void_result cell_res =
                        mso_emit_paragraph(ctx, &cell->paragraphs[p], x + MSO_TABLE_PAD,
                                           cell_w - 2.0f * MSO_TABLE_PAD, &cell_cursor);
                    YETTY_RETURN_IF_ERR(yetty_ycore_void, cell_res, "ymsoffice: cell text");
                }
            }
            x += cell_w;
            col += span;
        }
        ctx->cursor_y += row_h;
    }

    ctx->cursor_y += base_line * MSO_PARAGRAPH_GAP;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result mso_emit_word_image(struct mso_ctx *ctx,
                                                          const struct yetty_ymsoffice_image *image)
{
    float width = image->width_pt > 0.0f ? image->width_pt * ctx->pt_scale : 160.0f;
    float height = image->height_pt > 0.0f ? image->height_pt * ctx->pt_scale : 60.0f;
    if (width > ctx->content_w && width > 0.0f) {
        height *= ctx->content_w / width;
        width = ctx->content_w;
    }
    if (height < ctx->base_font_px * 2.0f) {
        height = ctx->base_font_px * 2.0f;
    }

    struct yetty_ycore_void_result box_res =
        mso_emit_box(ctx, MSO_MARGIN + width * 0.5f, ctx->cursor_y + height * 0.5f, width * 0.5f,
                     height * 0.5f, MSO_COLOR_PANEL_BG, MSO_COLOR_BORDER, 1.0f);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, box_res, "ymsoffice: image box");

    char label[128];
    snprintf(label, sizeof(label), "[image: %s]", image->name ? image->name : "embedded");
    size_t label_len = strlen(label);
    float label_w = (float)label_len * ctx->base_font_px * MSO_CHAR_W;
    float label_x = MSO_MARGIN + (width - label_w) * 0.5f;
    if (label_x < MSO_MARGIN) {
        label_x = MSO_MARGIN;
    }
    struct yetty_ycore_void_result label_res = mso_emit_text(
        ctx, label_x, ctx->cursor_y + height * 0.5f + ctx->base_font_px * (MSO_ASCENT - 0.5f),
        label, label_len, ctx->base_font_px, MSO_COLOR_MUTED);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, label_res, "ymsoffice: image label");

    ctx->cursor_y += height + ctx->base_font_px * MSO_LINE_SPACING * MSO_PARAGRAPH_GAP;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result mso_emit_word(
    struct mso_ctx *ctx, const struct yetty_ymsoffice_word_document *word)
{
    struct mso_list_state list_state = {0};
    for (size_t i = 0; i < word->block_count; i++) {
        const struct yetty_ymsoffice_block *block = &word->blocks[i];
        struct yetty_ycore_void_result block_res;
        switch (block->kind) {
        case YETTY_YMSOFFICE_BLOCK_PARAGRAPH:
            block_res = mso_emit_word_paragraph_block(ctx, &block->paragraph, &list_state);
            break;
        case YETTY_YMSOFFICE_BLOCK_TABLE:
            memset(&list_state, 0, sizeof(list_state));
            block_res = mso_emit_word_table(ctx, &block->table);
            break;
        case YETTY_YMSOFFICE_BLOCK_IMAGE:
        default:
            memset(&list_state, 0, sizeof(list_state));
            block_res = mso_emit_word_image(ctx, &block->image);
            break;
        }
        YETTY_RETURN_IF_ERR(yetty_ycore_void, block_res, "ymsoffice: word block");
    }
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Workbook grid
 *===========================================================================*/

/* 0 → "A", 27 → "AB". Returns length. */
static size_t mso_column_label(uint32_t col, char *out, size_t out_cap)
{
    char reversed[8];
    size_t len = 0;
    uint32_t value = col;
    do {
        reversed[len++] = (char)('A' + (value % 26u));
        value = value / 26u;
        if (value == 0) {
            break;
        }
        value -= 1; /* bijective base-26 */
    } while (len < sizeof(reversed));
    size_t written = 0;
    while (written < len && written + 1 < out_cap) {
        out[written] = reversed[len - 1 - written];
        written++;
    }
    out[written] = '\0';
    return written;
}

static const struct yetty_ymsoffice_sheet_cell *mso_sheet_cell_at(
    const struct yetty_ymsoffice_sheet *sheet, uint32_t row, uint32_t col)
{
    for (size_t i = 0; i < sheet->cell_count; i++) {
        if (sheet->cells[i].row == row && sheet->cells[i].col == col) {
            return &sheet->cells[i];
        }
    }
    return NULL;
}

static struct yetty_ycore_void_result mso_emit_sheet(struct mso_ctx *ctx,
                                                     const struct yetty_ymsoffice_sheet *sheet)
{
    float base_line = ctx->base_font_px * MSO_LINE_SPACING;
    float char_w = ctx->base_font_px * MSO_CHAR_W;

    /* Sheet name as a small heading. */
    if (sheet->name) {
        size_t name_len = strlen(sheet->name);
        float title_px = ctx->base_font_px * 1.15f;
        struct yetty_ycore_void_result title_res =
            mso_emit_text(ctx, MSO_MARGIN, ctx->cursor_y + title_px * MSO_ASCENT, sheet->name,
                          name_len, title_px, MSO_COLOR_HEADER);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, title_res, "ymsoffice: sheet title");
        ctx->cursor_y += title_px * MSO_LINE_SPACING + base_line * 0.2f;
    }

    if (sheet->cell_count == 0) {
        ctx->cursor_y += base_line;
        return YETTY_OK_VOID();
    }

    uint32_t rows = sheet->max_row + 1;
    uint32_t cols = sheet->max_col + 1;
    if (rows > MSO_SHEET_MAX_ROWS) {
        rows = MSO_SHEET_MAX_ROWS;
    }
    if (cols > MSO_SHEET_MAX_COLS) {
        cols = MSO_SHEET_MAX_COLS;
    }

    /* Column widths from content. */
    float column_width[MSO_SHEET_MAX_COLS];
    for (uint32_t c = 0; c < cols; c++) {
        column_width[c] = MSO_SHEET_MIN_COLW;
    }
    for (size_t i = 0; i < sheet->cell_count; i++) {
        const struct yetty_ymsoffice_sheet_cell *cell = &sheet->cells[i];
        if (cell->row >= rows || cell->col >= cols) {
            continue;
        }
        float needed = (float)cell->text_len * char_w + 2.0f * MSO_TABLE_PAD;
        if (needed > MSO_SHEET_MAX_COLW) {
            needed = MSO_SHEET_MAX_COLW;
        }
        if (needed > column_width[cell->col]) {
            column_width[cell->col] = needed;
        }
    }

    /* Row-number gutter width. */
    char row_label[16];
    snprintf(row_label, sizeof(row_label), "%u", rows);
    float gutter_w = (float)strlen(row_label) * char_w + 2.0f * MSO_TABLE_PAD;

    float grid_x = MSO_MARGIN + gutter_w;
    float grid_w = 0.0f;
    float column_x[MSO_SHEET_MAX_COLS];
    for (uint32_t c = 0; c < cols; c++) {
        column_x[c] = grid_x + grid_w;
        grid_w += column_width[c];
    }
    float header_h = base_line;
    float grid_top = ctx->cursor_y + header_h;
    float grid_h = (float)rows * base_line;

    /* Header backgrounds (column letters row + row-number gutter). */
    struct yetty_ycore_void_result header_bg_res =
        mso_emit_box(ctx, grid_x + grid_w * 0.5f, ctx->cursor_y + header_h * 0.5f, grid_w * 0.5f,
                     header_h * 0.5f, MSO_COLOR_HDR_BG, 0u, 0.0f);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, header_bg_res, "ymsoffice: sheet header bg");
    struct yetty_ycore_void_result gutter_bg_res =
        mso_emit_box(ctx, MSO_MARGIN + gutter_w * 0.5f, grid_top + grid_h * 0.5f, gutter_w * 0.5f,
                     grid_h * 0.5f, MSO_COLOR_HDR_BG, 0u, 0.0f);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, gutter_bg_res, "ymsoffice: sheet gutter bg");

    /* Column letters. */
    for (uint32_t c = 0; c < cols; c++) {
        char label[8];
        size_t label_len = mso_column_label(c, label, sizeof(label));
        float label_w = (float)label_len * char_w;
        float x = column_x[c] + (column_width[c] - label_w) * 0.5f;
        struct yetty_ycore_void_result label_res =
            mso_emit_text(ctx, x, ctx->cursor_y + ctx->base_font_px * MSO_ASCENT, label, label_len,
                          ctx->base_font_px, MSO_COLOR_MUTED);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, label_res, "ymsoffice: column label");
    }

    /* Row numbers. */
    for (uint32_t r = 0; r < rows; r++) {
        snprintf(row_label, sizeof(row_label), "%u", r + 1);
        size_t label_len = strlen(row_label);
        float label_w = (float)label_len * char_w;
        float y = grid_top + (float)r * base_line;
        struct yetty_ycore_void_result label_res =
            mso_emit_text(ctx, MSO_MARGIN + gutter_w - MSO_TABLE_PAD - label_w,
                          y + ctx->base_font_px * MSO_ASCENT, row_label, label_len,
                          ctx->base_font_px, MSO_COLOR_MUTED);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, label_res, "ymsoffice: row label");
    }

    /* Grid lines. */
    for (uint32_t r = 0; r <= rows; r++) {
        float y = grid_top + (float)r * base_line;
        struct yetty_ycore_void_result line_res =
            mso_emit_segment(ctx, MSO_MARGIN, y, grid_x + grid_w, y, MSO_COLOR_BORDER, 1.0f);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, line_res, "ymsoffice: sheet hline");
    }
    for (uint32_t c = 0; c <= cols; c++) {
        float x = c < cols ? column_x[c] : grid_x + grid_w;
        struct yetty_ycore_void_result line_res =
            mso_emit_segment(ctx, x, ctx->cursor_y, x, grid_top + grid_h, MSO_COLOR_BORDER, 1.0f);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, line_res, "ymsoffice: sheet vline");
    }
    {
        struct yetty_ycore_void_result line_res = mso_emit_segment(
            ctx, MSO_MARGIN, ctx->cursor_y, MSO_MARGIN, grid_top + grid_h, MSO_COLOR_BORDER, 1.0f);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, line_res, "ymsoffice: gutter vline");
    }

    /* Cell content. */
    for (size_t i = 0; i < sheet->cell_count; i++) {
        const struct yetty_ymsoffice_sheet_cell *cell = &sheet->cells[i];
        if (cell->row >= rows || cell->col >= cols) {
            continue;
        }
        const char *text = cell->text;
        size_t text_len = cell->text_len;
        char formula_buffer[64];
        if (text_len == 0 && cell->formula) {
            snprintf(formula_buffer, sizeof(formula_buffer), "=%s", cell->formula);
            text = formula_buffer;
            text_len = strlen(formula_buffer);
        }
        /* Truncate to the column so long values don't bleed across cells. */
        size_t fit = (size_t)((column_width[cell->col] - 2.0f * MSO_TABLE_PAD) / char_w);
        if (text_len > fit) {
            text_len = fit;
        }
        float text_w = (float)text_len * char_w;
        float x = cell->is_number
                      ? column_x[cell->col] + column_width[cell->col] - MSO_TABLE_PAD - text_w
                      : column_x[cell->col] + MSO_TABLE_PAD;
        float y = grid_top + (float)cell->row * base_line;
        struct yetty_ycore_void_result cell_res =
            mso_emit_text(ctx, x, y + ctx->base_font_px * MSO_ASCENT, text, text_len,
                          ctx->base_font_px, MSO_COLOR_TEXT);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, cell_res, "ymsoffice: sheet cell");
    }

    ctx->cursor_y = grid_top + grid_h + base_line;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result mso_emit_sheet_document(
    struct mso_ctx *ctx, const struct yetty_ymsoffice_sheet_document *document)
{
    for (size_t i = 0; i < document->sheet_count; i++) {
        struct yetty_ycore_void_result sheet_res = mso_emit_sheet(ctx, &document->sheets[i]);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sheet_res, "ymsoffice: sheet");
    }
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Slides
 *===========================================================================*/

static struct yetty_ycore_void_result mso_emit_shape_text(struct mso_ctx *ctx,
                                                          const struct yetty_ymsoffice_shape *shape,
                                                          float shape_x, float shape_y,
                                                          float shape_w, float slide_scale)
{
    float text_cursor = shape_y + MSO_SHAPE_PAD;
    float avail_w = shape_w - 2.0f * MSO_SHAPE_PAD;

    /* Slide text sizes are in points scaled by the slide fit; override the
     * ctx pt scale for the duration of the shape. */
    struct mso_ctx shape_ctx = *ctx;
    shape_ctx.pt_scale = slide_scale;
    shape_ctx.base_font_px = MSO_SLIDE_DEFAULT_TEXT_PT * slide_scale;
    if (shape_ctx.base_font_px < 6.0f) {
        shape_ctx.base_font_px = 6.0f;
    }

    for (size_t p = 0; p < shape->paragraph_count; p++) {
        struct yetty_ycore_void_result paragraph_res = mso_emit_paragraph(
            &shape_ctx, &shape->paragraphs[p], shape_x + MSO_SHAPE_PAD, avail_w, &text_cursor);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, paragraph_res, "ymsoffice: shape text");
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result mso_emit_slide(struct mso_ctx *ctx,
                                                     const struct yetty_ymsoffice_slide *slide,
                                                     size_t slide_index, float slide_w_pt,
                                                     float slide_h_pt)
{
    float base_line = ctx->base_font_px * MSO_LINE_SPACING;

    char label[32];
    snprintf(label, sizeof(label), "Slide %zu", slide_index + 1);
    struct yetty_ycore_void_result label_res =
        mso_emit_text(ctx, MSO_MARGIN, ctx->cursor_y + ctx->base_font_px * MSO_ASCENT, label,
                      strlen(label), ctx->base_font_px, MSO_COLOR_MUTED);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, label_res, "ymsoffice: slide label");
    ctx->cursor_y += base_line;

    float slide_scale = slide_w_pt > 0.0f ? ctx->content_w / slide_w_pt : 1.0f;
    float slide_w = ctx->content_w;
    float slide_h = slide_h_pt > 0.0f ? slide_h_pt * slide_scale : slide_w * 0.5625f;
    float slide_x = MSO_MARGIN;
    float slide_y = ctx->cursor_y;

    struct yetty_ycore_void_result bg_res =
        mso_emit_box(ctx, slide_x + slide_w * 0.5f, slide_y + slide_h * 0.5f, slide_w * 0.5f,
                     slide_h * 0.5f, MSO_COLOR_PANEL_BG, MSO_COLOR_BORDER, 1.0f);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, bg_res, "ymsoffice: slide background");

    /* Shapes without an explicit frame (layout-inherited placeholders)
     * stack from the top of the slide. */
    float fallback_y = slide_y + MSO_SHAPE_PAD;

    for (size_t s = 0; s < slide->shape_count; s++) {
        const struct yetty_ymsoffice_shape *shape = &slide->shapes[s];

        float shape_x;
        float shape_y;
        float shape_w;
        float shape_h;
        if (shape->has_frame) {
            shape_x = slide_x + shape->x_pt * slide_scale;
            shape_y = slide_y + shape->y_pt * slide_scale;
            shape_w = shape->width_pt * slide_scale;
            shape_h = shape->height_pt * slide_scale;
        } else {
            shape_x = slide_x + MSO_SHAPE_PAD;
            shape_y = fallback_y;
            shape_w = slide_w - 2.0f * MSO_SHAPE_PAD;
            float text_h = 0.0f;
            for (size_t p = 0; p < shape->paragraph_count; p++) {
                struct mso_ctx measure_ctx = *ctx;
                measure_ctx.pt_scale = slide_scale;
                measure_ctx.base_font_px = MSO_SLIDE_DEFAULT_TEXT_PT * slide_scale;
                struct mso_paragraph_metrics metrics;
                if (mso_measure_paragraph(&measure_ctx, &shape->paragraphs[p],
                                          shape_w - 2.0f * MSO_SHAPE_PAD, &metrics) < 0) {
                    return YETTY_ERR(yetty_ycore_void, "ymsoffice: out of memory (shape)");
                }
                text_h += metrics.height;
            }
            shape_h = text_h + 2.0f * MSO_SHAPE_PAD;
            fallback_y += shape_h + MSO_SHAPE_PAD;
        }

        uint32_t fill = shape->has_fill ? mso_pack_rgb(shape->fill_rgb) : MSO_COLOR_TRANSPARENT;
        switch (shape->kind) {
        case YETTY_YMSOFFICE_SHAPE_ELLIPSE: {
            struct yetty_ycore_void_result shape_res =
                mso_emit_ellipse(ctx, shape_x + shape_w * 0.5f, shape_y + shape_h * 0.5f,
                                 shape_w * 0.5f, shape_h * 0.5f, fill, MSO_COLOR_BORDER, 1.0f);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, shape_res, "ymsoffice: ellipse shape");
            break;
        }
        case YETTY_YMSOFFICE_SHAPE_LINE: {
            struct yetty_ycore_void_result shape_res =
                mso_emit_segment(ctx, shape_x, shape_y, shape_x + shape_w, shape_y + shape_h,
                                 shape->has_fill ? fill : MSO_COLOR_MUTED, 1.5f);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, shape_res, "ymsoffice: line shape");
            break;
        }
        case YETTY_YMSOFFICE_SHAPE_PICTURE:
        case YETTY_YMSOFFICE_SHAPE_FRAME: {
            struct yetty_ycore_void_result shape_res = mso_emit_box(
                ctx, shape_x + shape_w * 0.5f, shape_y + shape_h * 0.5f, shape_w * 0.5f,
                shape_h * 0.5f, shape->has_fill ? fill : MSO_COLOR_HDR_BG, MSO_COLOR_BORDER, 1.0f);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, shape_res, "ymsoffice: placeholder shape");
            if (shape->kind == YETTY_YMSOFFICE_SHAPE_PICTURE && shape->paragraph_count == 0) {
                char picture_label[96];
                snprintf(picture_label, sizeof(picture_label), "[%s]",
                         shape->name ? shape->name : "picture");
                size_t picture_label_len = strlen(picture_label);
                float small_px = ctx->base_font_px * 0.9f;
                float label_w = (float)picture_label_len * small_px * MSO_CHAR_W;
                float label_x = shape_x + (shape_w - label_w) * 0.5f;
                if (label_x < shape_x) {
                    label_x = shape_x;
                }
                struct yetty_ycore_void_result picture_res = mso_emit_text(
                    ctx, label_x, shape_y + shape_h * 0.5f + small_px * (MSO_ASCENT - 0.5f),
                    picture_label, picture_label_len, small_px, MSO_COLOR_MUTED);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, picture_res, "ymsoffice: picture label");
            }
            break;
        }
        case YETTY_YMSOFFICE_SHAPE_BOX:
        default:
            if (shape->has_fill || shape->paragraph_count == 0) {
                struct yetty_ycore_void_result shape_res =
                    mso_emit_box(ctx, shape_x + shape_w * 0.5f, shape_y + shape_h * 0.5f,
                                 shape_w * 0.5f, shape_h * 0.5f, fill, MSO_COLOR_BORDER, 1.0f);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, shape_res, "ymsoffice: box shape");
            }
            break;
        }

        if (shape->paragraph_count > 0) {
            struct yetty_ycore_void_result text_res =
                mso_emit_shape_text(ctx, shape, shape_x, shape_y, shape_w, slide_scale);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, text_res, "ymsoffice: shape body");
        }
    }

    ctx->cursor_y = slide_y + slide_h + MSO_SLIDE_GAP;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result mso_emit_slides(
    struct mso_ctx *ctx, const struct yetty_ymsoffice_slides_document *document)
{
    float slide_w_pt = document->width_pt > 0.0f ? document->width_pt : 960.0f;
    float slide_h_pt = document->height_pt > 0.0f ? document->height_pt : 540.0f;
    for (size_t i = 0; i < document->slide_count; i++) {
        struct yetty_ycore_void_result slide_res =
            mso_emit_slide(ctx, &document->slides[i], i, slide_w_pt, slide_h_pt);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, slide_res, "ymsoffice: slide");
    }
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Entry point
 *===========================================================================*/

struct yetty_ymsoffice_render_result yetty_ymsoffice_render(
    const struct yetty_ymsoffice_document *document,
    const struct yetty_ymsoffice_render_config *config)
{
    if (!document) {
        return YETTY_ERR(yetty_ymsoffice_render, "ymsoffice render: document is NULL");
    }

    float scene_w = 0.0f;
    float scene_h = 0.0f;
    float base_font = MSO_DEFAULT_FONT_SIZE;
    if (config) {
        scene_w = (float)(config->width_cells * config->cell_width);
        scene_h = (float)(config->height_cells * config->cell_height);
        if (config->cell_height > 0) {
            base_font = (float)config->cell_height;
        }
    }
    float content_w = scene_w > 2.0f * MSO_MARGIN ? scene_w - 2.0f * MSO_MARGIN : 640.0f;

    struct yetty_ydraw_drawable_list_config buffer_config = {
        .scene_min_x = 0.0f,
        .scene_min_y = 0.0f,
        .scene_max_x = scene_w,
        .scene_max_y = scene_h,
    };
    struct yetty_ydraw_drawable_list_result buffer_res =
        yetty_ydraw_drawable_list_config_buffer_create(&buffer_config);
    if (YETTY_IS_ERR(buffer_res)) {
        return YETTY_ERR(yetty_ymsoffice_render, "ymsoffice render: buffer create failed",
                         buffer_res);
    }

    struct mso_ctx ctx = {
        .buffer = buffer_res.value,
        .base_font_px = base_font,
        .pt_scale = base_font / MSO_BODY_PT,
        .content_w = content_w,
        .cursor_y = MSO_MARGIN,
    };

    struct yetty_ycore_void_result emit_res;
    switch (document->kind) {
    case YETTY_YMSOFFICE_KIND_WORD:
        emit_res = mso_emit_word(&ctx, &document->word);
        break;
    case YETTY_YMSOFFICE_KIND_SHEET:
        emit_res = mso_emit_sheet_document(&ctx, &document->sheet);
        break;
    case YETTY_YMSOFFICE_KIND_SLIDES:
        emit_res = mso_emit_slides(&ctx, &document->slides);
        break;
    case YETTY_YMSOFFICE_KIND_UNKNOWN:
    default:
        emit_res = YETTY_ERR(yetty_ycore_void, "ymsoffice render: unknown document kind");
        break;
    }
    if (YETTY_IS_ERR(emit_res)) {
        yetty_ydraw_drawable_list_destroy(ctx.buffer);
        return YETTY_ERR(yetty_ymsoffice_render, "ymsoffice render: emission failed", emit_res);
    }

    struct yetty_ymsoffice_render_output output = {
        .buffer = ctx.buffer,
        .scene_width = scene_w,
        .scene_height = scene_h,
    };
    return YETTY_OK(yetty_ymsoffice_render, output);
}
