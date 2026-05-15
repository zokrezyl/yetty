/*
 * yspreadsheet.c — grid document.
 *
 * Cells are concrete elements with an SDF box body, optional border, and a
 * single text span (left-aligned by default). The grid keeps a cumulative
 * position cache for fast row/col -> screen coordinate conversion; sparse
 * row/col size overrides hang off two small linear arrays.
 */

#include <yetty/yrich/yspreadsheet.h>
#include <yetty/yrich/yrich-document.h>
#include <yetty/yrich/yrich-element.h>

#include <yetty/ycore/types.h>
#include <yetty/ydraw-core/buffer.h>
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/ysdf/types.gen.h>

#include <stdlib.h>
#include <string.h>

#define CELL_GRID_DEFAULT_ROWS 100
#define CELL_GRID_DEFAULT_COLS 26
#define CELL_GRID_DEFAULT_HEIGHT 24.0f
#define CELL_GRID_DEFAULT_WIDTH 80.0f

/*=============================================================================
 * Cell vtable
 *===========================================================================*/

static struct yetty_yrich_rect cell_bounds(const struct yetty_yrich_element *e)
{
    const struct yetty_yrich_cell *c = (const struct yetty_yrich_cell *)e;
    return c->bounds;
}

static void cell_destroy(struct yetty_yrich_element *e)
{
    struct yetty_yrich_cell *c = (struct yetty_yrich_cell *)e;
    free(c->text);
    free(c->formula);
    free(c);
}

static bool cell_is_editable(const struct yetty_yrich_element *e)
{
    (void)e;
    return true;
}

static void cell_begin_edit(struct yetty_yrich_element *e)
{
    struct yetty_yrich_cell *c = (struct yetty_yrich_cell *)e;
    c->editing = true;
    c->cursor_pos = (int32_t)c->text_len;
    c->sel_start = 0;
    c->sel_end = (int32_t)c->text_len;
}

static void cell_end_edit(struct yetty_yrich_element *e)
{
    struct yetty_yrich_cell *c = (struct yetty_yrich_cell *)e;
    c->editing = false;
    c->sel_start = c->sel_end = c->cursor_pos;
}

static bool cell_is_editing(const struct yetty_yrich_element *e)
{
    const struct yetty_yrich_cell *c = (const struct yetty_yrich_cell *)e;
    return c->editing;
}

static struct yetty_ycore_void_result cell_render(struct yetty_yrich_element *e,
                                                  struct yetty_ydraw_core_buffer *buf,
                                                  uint32_t layer, bool selected)
{
    struct yetty_yrich_cell *c = (struct yetty_yrich_cell *)e;
    if (!buf) {
        return YETTY_ERR(yetty_ycore_void, "cell_render: NULL buffer");
    }

    uint32_t fill = c->fill_color;
    if (selected && !c->editing) {
        fill = YETTY_YRICH_RGBA(220, 235, 255, 255);
    }

    struct yetty_ysdf_box body = {
        .center_x = c->bounds.x + c->bounds.w * 0.5f,
        .center_y = c->bounds.y + c->bounds.h * 0.5f,
        .half_width = c->bounds.w * 0.5f,
        .half_height = c->bounds.h * 0.5f,
        .corner_radius = 0.0f,
    };
    struct yetty_ydraw_core_id_result br =
        yetty_ysdf_add_box(buf, layer, fill, c->border.color, c->border.width, &body);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "cell_render: body box add failed");

    if (c->text_len > 0) {
        float text_x = c->bounds.x + 4.0f;
        float text_y = c->bounds.y + c->bounds.h * 0.5f + c->style.font_size / 3.0f;

        if (c->halign == YETTY_YRICH_HALIGN_CENTER) {
            text_x = c->bounds.x + c->bounds.w * 0.5f;
        } else if (c->halign == YETTY_YRICH_HALIGN_RIGHT) {
            text_x = c->bounds.x + c->bounds.w - 4.0f;
        }

        struct yetty_ycore_buffer text = {
            .data = (uint8_t *)c->text,
            .size = c->text_len,
            .capacity = c->text_len,
        };
        struct yetty_ycore_void_result tr =
            yetty_ydraw_core_buffer_add_text(buf, text_x, text_y, &text, c->style.font_size,
                                              c->style.color, layer + 1, c->style.font_id, 0.0f);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, tr, "cell_render: add_text failed");
    }

    if (c->editing) {
        float cursor_x = c->bounds.x + 4.0f + (float)c->cursor_pos * c->style.font_size * 0.6f;
        struct yetty_ysdf_segment seg = {
            .start_x = cursor_x,
            .start_y = c->bounds.y + 2.0f,
            .end_x = cursor_x,
            .end_y = c->bounds.y + c->bounds.h - 2.0f,
        };
        struct yetty_ydraw_core_id_result sr =
            yetty_ysdf_add_segment(buf, layer + 2, 0, YETTY_YRICH_COLOR_BLACK, 1.0f, &seg);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "cell_render: cursor segment add failed");
    }

    if (selected) {
        struct yetty_ysdf_box border = body;
        struct yetty_ydraw_core_id_result br2 = yetty_ysdf_add_box(
            buf, layer + 3, 0, YETTY_YRICH_RGBA(0, 100, 200, 255), 2.0f, &border);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, br2, "cell_render: selection border add failed");
    }
    return YETTY_OK_VOID();
}

static void cell_insert_text(struct yetty_yrich_element *e, const char *text, size_t text_len)
{
    if (!text || text_len == 0) {
        return;
    }
    struct yetty_yrich_cell *c = (struct yetty_yrich_cell *)e;

    int32_t pos = c->cursor_pos;
    if (pos < 0) {
        pos = 0;
    }
    if ((size_t)pos > c->text_len) {
        pos = (int32_t)c->text_len;
    }

    size_t new_len = c->text_len + text_len;
    char *new_buf = realloc(c->text, new_len + 1);
    if (!new_buf) {
        return;
    }
    memmove(new_buf + pos + text_len, new_buf + pos, c->text_len - pos);
    memcpy(new_buf + pos, text, text_len);
    new_buf[new_len] = '\0';
    c->text = new_buf;
    c->text_len = new_len;
    c->cursor_pos = pos + (int32_t)text_len;
    c->sel_start = c->sel_end = c->cursor_pos;
}

static void cell_delete_sel(struct yetty_yrich_element *e)
{
    struct yetty_yrich_cell *c = (struct yetty_yrich_cell *)e;
    if (c->text_len == 0) {
        return;
    }
    if (c->cursor_pos > 0) {
        memmove(c->text + c->cursor_pos - 1, c->text + c->cursor_pos, c->text_len - c->cursor_pos);
        c->text_len--;
        c->text[c->text_len] = '\0';
        c->cursor_pos--;
        c->sel_start = c->sel_end = c->cursor_pos;
    }
}

static const struct yetty_yrich_element_ops cell_element_ops = {
    .destroy = cell_destroy,
    .bounds = cell_bounds,
    .hit_test = NULL, /* default = bounds */
    .render = cell_render,
    .is_editable = cell_is_editable,
    .begin_edit = cell_begin_edit,
    .end_edit = cell_end_edit,
    .is_editing = cell_is_editing,
    .insert_text = cell_insert_text,
    .delete_sel = cell_delete_sel,
};

/*=============================================================================
 * Cell creation
 *===========================================================================*/

struct yetty_yrich_cell_ptr_result yetty_yrich_cell_create(yetty_yrich_element_id id,
                                                           struct yetty_yrich_cell_addr addr,
                                                           struct yetty_yrich_rect bounds)
{
    struct yetty_yrich_cell *c = calloc(1, sizeof(struct yetty_yrich_cell));
    if (!c) {
        return YETTY_ERR(yetty_yrich_cell_ptr, "yrich cell alloc failed");
    }
    c->base.ops = &cell_element_ops;
    c->base.id = id;
    c->address = addr;
    c->bounds = bounds;
    c->style = yetty_yrich_text_style_default();
    c->style.font_size = 12.0f;
    c->fill_color = YETTY_YRICH_COLOR_WHITE;
    c->border.width = 1.0f;
    c->border.color = YETTY_YRICH_RGBA(200, 200, 200, 255);
    c->border.style = YETTY_YRICH_BORDER_SOLID;
    c->halign = YETTY_YRICH_HALIGN_LEFT;
    c->valign = YETTY_YRICH_VALIGN_MIDDLE;
    return YETTY_OK(yetty_yrich_cell_ptr, c);
}

void yetty_yrich_cell_set_text(struct yetty_yrich_cell *cell, const char *text, size_t len)
{
    if (!cell) {
        return;
    }
    char *buf = malloc(len + 1);
    if (!buf) {
        return;
    }
    if (len > 0) {
        memcpy(buf, text, len);
    }
    buf[len] = '\0';
    free(cell->text);
    cell->text = buf;
    cell->text_len = len;
    cell->cursor_pos = (int32_t)len;
}

/*=============================================================================
 * Spreadsheet vtable
 *===========================================================================*/

static void invalidate_cache(struct yetty_yrich_spreadsheet *s)
{
    s->cache_valid = false;
}

static void rebuild_cache(struct yetty_yrich_spreadsheet *s)
{
    if (s->cache_valid) {
        return;
    }

    size_t row_count = (size_t)s->row_count + 1;
    size_t col_count = (size_t)s->col_count + 1;

    float *new_rows = realloc(s->row_y_cache, row_count * sizeof(float));
    float *new_cols = realloc(s->col_x_cache, col_count * sizeof(float));
    if (!new_rows || !new_cols) {
        /* On failure leave cache invalid; getter will recompute next
		 * call. */
        free(new_rows);
        free(new_cols);
        return;
    }
    s->row_y_cache = new_rows;
    s->row_y_cache_count = row_count;
    s->col_x_cache = new_cols;
    s->col_x_cache_count = col_count;

    float y = 0.0f;
    for (int32_t r = 0; r < s->row_count; r++) {
        s->row_y_cache[r] = y;
        float h = s->default_row_height;
        for (size_t i = 0; i < s->row_override_count; i++) {
            if (s->row_overrides[i].row == r) {
                h = s->row_overrides[i].height;
                break;
            }
        }
        y += h;
    }
    s->row_y_cache[s->row_count] = y;

    float x = 0.0f;
    for (int32_t c = 0; c < s->col_count; c++) {
        s->col_x_cache[c] = x;
        float w = s->default_col_width;
        for (size_t i = 0; i < s->col_override_count; i++) {
            if (s->col_overrides[i].col == c) {
                w = s->col_overrides[i].width;
                break;
            }
        }
        x += w;
    }
    s->col_x_cache[s->col_count] = x;

    s->cache_valid = true;
}

float yetty_yrich_spreadsheet_row_height(const struct yetty_yrich_spreadsheet *s, int32_t row)
{
    for (size_t i = 0; i < s->row_override_count; i++) {
        if (s->row_overrides[i].row == row) {
            return s->row_overrides[i].height;
        }
    }
    return s->default_row_height;
}

float yetty_yrich_spreadsheet_col_width(const struct yetty_yrich_spreadsheet *s, int32_t col)
{
    for (size_t i = 0; i < s->col_override_count; i++) {
        if (s->col_overrides[i].col == col) {
            return s->col_overrides[i].width;
        }
    }
    return s->default_col_width;
}

float yetty_yrich_spreadsheet_row_y(struct yetty_yrich_spreadsheet *s, int32_t row)
{
    rebuild_cache(s);
    if (!s->row_y_cache || s->row_y_cache_count == 0) {
        return 0.0f;
    }
    if (row < 0) {
        return 0.0f;
    }
    if ((size_t)row >= s->row_y_cache_count) {
        return s->row_y_cache[s->row_y_cache_count - 1];
    }
    return s->row_y_cache[row];
}

float yetty_yrich_spreadsheet_col_x(struct yetty_yrich_spreadsheet *s, int32_t col)
{
    rebuild_cache(s);
    if (!s->col_x_cache || s->col_x_cache_count == 0) {
        return 0.0f;
    }
    if (col < 0) {
        return 0.0f;
    }
    if ((size_t)col >= s->col_x_cache_count) {
        return s->col_x_cache[s->col_x_cache_count - 1];
    }
    return s->col_x_cache[col];
}

static float spreadsheet_content_width(const struct yetty_yrich_document *doc)
{
    struct yetty_yrich_spreadsheet *s = (struct yetty_yrich_spreadsheet *)doc;
    rebuild_cache(s);
    if (!s->col_x_cache || s->col_x_cache_count == 0) {
        return 0.0f;
    }
    return s->col_x_cache[s->col_x_cache_count - 1];
}

static float spreadsheet_content_height(const struct yetty_yrich_document *doc)
{
    struct yetty_yrich_spreadsheet *s = (struct yetty_yrich_spreadsheet *)doc;
    rebuild_cache(s);
    if (!s->row_y_cache || s->row_y_cache_count == 0) {
        return 0.0f;
    }
    return s->row_y_cache[s->row_y_cache_count - 1];
}

static void spreadsheet_destroy(struct yetty_yrich_document *doc)
{
    struct yetty_yrich_spreadsheet *s = (struct yetty_yrich_spreadsheet *)doc;
    yetty_yrich_document_fini(doc);
    free(s->row_overrides);
    free(s->col_overrides);
    free(s->row_y_cache);
    free(s->col_x_cache);
    free(s->cells);
    free(s);
}

static const struct yetty_yrich_document_ops spreadsheet_doc_ops = {
    .destroy = spreadsheet_destroy,
    .content_width = spreadsheet_content_width,
    .content_height = spreadsheet_content_height,
    /* render/apply_op/input handlers fall back to base defaults. */
};

/*=============================================================================
 * Create
 *===========================================================================*/

struct yetty_yrich_spreadsheet_ptr_result yetty_yrich_spreadsheet_create(void)
{
    struct yetty_yrich_spreadsheet *s = calloc(1, sizeof(struct yetty_yrich_spreadsheet));
    if (!s) {
        return YETTY_ERR(yetty_yrich_spreadsheet_ptr, "yrich spreadsheet alloc failed");
    }

    struct yetty_ycore_void_result init_r = yetty_yrich_document_init(&s->base);
    if (YETTY_IS_ERR(init_r)) {
        free(s);
        return YETTY_ERR(yetty_yrich_spreadsheet_ptr, init_r.error.msg);
    }

    s->base.ops = &spreadsheet_doc_ops;
    s->row_count = CELL_GRID_DEFAULT_ROWS;
    s->col_count = CELL_GRID_DEFAULT_COLS;
    s->default_row_height = CELL_GRID_DEFAULT_HEIGHT;
    s->default_col_width = CELL_GRID_DEFAULT_WIDTH;
    return YETTY_OK(yetty_yrich_spreadsheet_ptr, s);
}

/*=============================================================================
 * Grid configuration
 *===========================================================================*/

void yetty_yrich_spreadsheet_set_grid_size(struct yetty_yrich_spreadsheet *s, int32_t rows,
                                           int32_t cols)
{
    if (!s) {
        return;
    }
    s->row_count = rows;
    s->col_count = cols;
    invalidate_cache(s);
    yetty_yrich_document_mark_dirty(&s->base);
}

static int set_row_override(struct yetty_yrich_spreadsheet *s, int32_t row, float h)
{
    for (size_t i = 0; i < s->row_override_count; i++) {
        if (s->row_overrides[i].row == row) {
            s->row_overrides[i].height = h;
            return 0;
        }
    }
    if (s->row_override_count == s->row_override_capacity) {
        size_t new_cap = s->row_override_capacity ? s->row_override_capacity * 2 : 8;
        struct yetty_yrich_row_size *new_arr =
            realloc(s->row_overrides, new_cap * sizeof(*new_arr));
        if (!new_arr) {
            return -1;
        }
        s->row_overrides = new_arr;
        s->row_override_capacity = new_cap;
    }
    s->row_overrides[s->row_override_count++] = (struct yetty_yrich_row_size){row, h};
    return 0;
}

static int set_col_override(struct yetty_yrich_spreadsheet *s, int32_t col, float w)
{
    for (size_t i = 0; i < s->col_override_count; i++) {
        if (s->col_overrides[i].col == col) {
            s->col_overrides[i].width = w;
            return 0;
        }
    }
    if (s->col_override_count == s->col_override_capacity) {
        size_t new_cap = s->col_override_capacity ? s->col_override_capacity * 2 : 8;
        struct yetty_yrich_col_size *new_arr =
            realloc(s->col_overrides, new_cap * sizeof(*new_arr));
        if (!new_arr) {
            return -1;
        }
        s->col_overrides = new_arr;
        s->col_override_capacity = new_cap;
    }
    s->col_overrides[s->col_override_count++] = (struct yetty_yrich_col_size){col, w};
    return 0;
}

static void recalculate_cell_bounds(struct yetty_yrich_spreadsheet *s);

void yetty_yrich_spreadsheet_set_row_height(struct yetty_yrich_spreadsheet *s, int32_t row, float h)
{
    if (!s) {
        return;
    }
    if (set_row_override(s, row, h) < 0) {
        return;
    }
    invalidate_cache(s);
    recalculate_cell_bounds(s);
    yetty_yrich_document_mark_dirty(&s->base);
}

void yetty_yrich_spreadsheet_set_col_width(struct yetty_yrich_spreadsheet *s, int32_t col, float w)
{
    if (!s) {
        return;
    }
    if (set_col_override(s, col, w) < 0) {
        return;
    }
    invalidate_cache(s);
    recalculate_cell_bounds(s);
    yetty_yrich_document_mark_dirty(&s->base);
}

/*=============================================================================
 * Cell access
 *===========================================================================*/

struct yetty_yrich_cell *yetty_yrich_spreadsheet_cell_at(const struct yetty_yrich_spreadsheet *s,
                                                         struct yetty_yrich_cell_addr addr)
{
    if (!s) {
        return NULL;
    }
    for (size_t i = 0; i < s->cell_count; i++) {
        if (yetty_yrich_cell_addr_eq(s->cells[i].addr, addr)) {
            return s->cells[i].cell;
        }
    }
    return NULL;
}

static int register_cell_entry(struct yetty_yrich_spreadsheet *s, struct yetty_yrich_cell_addr addr,
                               struct yetty_yrich_cell *cell)
{
    if (s->cell_count == s->cell_capacity) {
        size_t new_cap = s->cell_capacity ? s->cell_capacity * 2 : 32;
        struct yetty_yrich_cell_entry *new_arr = realloc(s->cells, new_cap * sizeof(*new_arr));
        if (!new_arr) {
            return -1;
        }
        s->cells = new_arr;
        s->cell_capacity = new_cap;
    }
    s->cells[s->cell_count++] = (struct yetty_yrich_cell_entry){addr, cell};
    return 0;
}

static void recalculate_cell_bounds(struct yetty_yrich_spreadsheet *s)
{
    for (size_t i = 0; i < s->cell_count; i++) {
        struct yetty_yrich_cell_addr a = s->cells[i].addr;
        struct yetty_yrich_cell *c = s->cells[i].cell;
        c->bounds.x = yetty_yrich_spreadsheet_col_x(s, a.col);
        c->bounds.y = yetty_yrich_spreadsheet_row_y(s, a.row);
        c->bounds.w = yetty_yrich_spreadsheet_col_width(s, a.col);
        c->bounds.h = yetty_yrich_spreadsheet_row_height(s, a.row);
    }
}

struct yetty_yrich_cell_ptr_result yetty_yrich_spreadsheet_ensure_cell(
    struct yetty_yrich_spreadsheet *s, struct yetty_yrich_cell_addr addr)
{
    if (!s) {
        return YETTY_ERR(yetty_yrich_cell_ptr, "yrich ensure_cell: NULL spreadsheet");
    }

    struct yetty_yrich_cell *existing = yetty_yrich_spreadsheet_cell_at(s, addr);
    if (existing) {
        return YETTY_OK(yetty_yrich_cell_ptr, existing);
    }

    struct yetty_yrich_rect bounds = {
        .x = yetty_yrich_spreadsheet_col_x(s, addr.col),
        .y = yetty_yrich_spreadsheet_row_y(s, addr.row),
        .w = yetty_yrich_spreadsheet_col_width(s, addr.col),
        .h = yetty_yrich_spreadsheet_row_height(s, addr.row),
    };

    yetty_yrich_element_id id = yetty_yrich_document_next_id(&s->base);
    struct yetty_yrich_cell_ptr_result cr = yetty_yrich_cell_create(id, addr, bounds);
    if (YETTY_IS_ERR(cr)) {
        return cr;
    }

    struct yetty_yrich_cell *cell = cr.value;
    struct yetty_ycore_void_result add_r = yetty_yrich_document_add_element(&s->base, &cell->base);
    if (YETTY_IS_ERR(add_r)) {
        return YETTY_ERR(yetty_yrich_cell_ptr, add_r.error.msg);
    }

    if (register_cell_entry(s, addr, cell) < 0) {
        /* Cell is now owned by the document; we just won't have a
		 * fast lookup entry. Continue. */
    }
    return YETTY_OK(yetty_yrich_cell_ptr, cell);
}

struct yetty_ycore_void_result yetty_yrich_spreadsheet_set_cell_value(
    struct yetty_yrich_spreadsheet *s, struct yetty_yrich_cell_addr addr, const char *value,
    size_t value_len)
{
    if (!s) {
        return YETTY_ERR(yetty_ycore_void, "yrich set_cell_value: NULL spreadsheet");
    }
    struct yetty_yrich_cell_ptr_result cr = yetty_yrich_spreadsheet_ensure_cell(s, addr);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, cr, "yrich set_cell_value: ensure_cell failed");
    yetty_yrich_cell_set_text(cr.value, value, value_len);
    yetty_yrich_document_mark_dirty(&s->base);
    return YETTY_OK_VOID();
}

const char *yetty_yrich_spreadsheet_cell_value(const struct yetty_yrich_spreadsheet *s,
                                               struct yetty_yrich_cell_addr addr)
{
    struct yetty_yrich_cell *c = yetty_yrich_spreadsheet_cell_at(s, addr);
    return c && c->text ? c->text : "";
}

struct yetty_yrich_cell_addr yetty_yrich_spreadsheet_cell_addr_at(struct yetty_yrich_spreadsheet *s,
                                                                  float x, float y)
{
    struct yetty_yrich_cell_addr addr = {0, 0};
    if (!s) {
        return addr;
    }
    rebuild_cache(s);
    if (!s->col_x_cache || !s->row_y_cache) {
        return addr;
    }

    for (int32_t c = 0; c < s->col_count; c++) {
        if (s->col_x_cache[c + 1] > x) {
            addr.col = c;
            break;
        }
        addr.col = c;
    }
    for (int32_t r = 0; r < s->row_count; r++) {
        if (s->row_y_cache[r + 1] > y) {
            addr.row = r;
            break;
        }
        addr.row = r;
    }
    return addr;
}
