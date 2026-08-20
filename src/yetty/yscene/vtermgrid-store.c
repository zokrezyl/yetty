/* vtermgrid-store.c — the cluster-preserving terminal cell store (#699.5).
 * See vtermgrid-store.h; a pure model with no libvterm/GPU dependency. */

#include "vtermgrid-store.h"

#include <stdlib.h>
#include <string.h>

/* The grid's attr bits (kept in sync with vtermgrid.c's exposed enum — the
 * store is module-private to the same library). */
enum {
    VTERMGRID_STORE_ATTR_REVERSE = 1u << 4,
};

static void store_blank_cell(struct yetty_yscene_vtermgrid_store_cell *cell, uint32_t fg,
                             uint32_t bg)
{
    memset(cell, 0, sizeof(*cell));
    cell->width = 1;
    cell->fg = fg;
    cell->bg = bg;
}

struct yetty_ycore_void_result yetty_yscene_vtermgrid_store_init(
    struct yetty_yscene_vtermgrid_store *store, uint32_t rows, uint32_t cols, uint32_t default_fg,
    uint32_t default_bg)
{
    if (!store || rows == 0 || cols == 0) {
        return YETTY_ERR(yetty_ycore_void, "vtermgrid store init: invalid arguments");
    }
    memset(store, 0, sizeof(*store));
    store->cells = calloc((size_t)rows * cols, sizeof(struct yetty_yscene_vtermgrid_store_cell));
    if (!store->cells) {
        return YETTY_ERR(yetty_ycore_void, "vtermgrid store init: cell alloc");
    }
    store->rows = rows;
    store->cols = cols;
    store->default_fg = default_fg;
    store->default_bg = default_bg;
    store->pen_fg = default_fg;
    store->pen_bg = default_bg;
    store->cursor_visible = 1;
    for (size_t index = 0; index < (size_t)rows * cols; ++index) {
        store_blank_cell(&store->cells[index], default_fg, default_bg);
    }
    return YETTY_OK_VOID();
}

void yetty_yscene_vtermgrid_store_free(struct yetty_yscene_vtermgrid_store *store)
{
    if (!store) {
        return;
    }
    free(store->cells);
    store->cells = NULL;
    store->rows = 0;
    store->cols = 0;
}

static struct yetty_yscene_vtermgrid_store_cell *store_cell_at(
    struct yetty_yscene_vtermgrid_store *store, uint32_t row, uint32_t col)
{
    if (!store->cells || row >= store->rows || col >= store->cols) {
        return NULL;
    }
    return &store->cells[(size_t)row * store->cols + col];
}

struct yetty_ycore_void_result yetty_yscene_vtermgrid_store_put(
    struct yetty_yscene_vtermgrid_store *store, uint32_t row, uint32_t col,
    const uint32_t *codepoints, uint32_t codepoint_count, int wide)
{
    struct yetty_yscene_vtermgrid_store_cell *cell = store_cell_at(store, row, col);
    if (!cell) {
        return YETTY_ERR(yetty_ycore_void, "vtermgrid store put: out of range");
    }
    /* The base codepoint plus RETAINED combining marks — the whole point of
     * the store (the fork's screen layer drops the marks). */
    cell->codepoint = codepoint_count ? codepoints[0] : 0;
    cell->mark_count = 0;
    memset(cell->marks, 0, sizeof(cell->marks));
    for (uint32_t mark = 1;
         mark < codepoint_count && cell->mark_count < YETTY_YSCENE_VTERMGRID_STORE_MAX_MARKS;
         ++mark) {
        cell->marks[cell->mark_count++] = codepoints[mark];
    }
    cell->fg = store->pen_fg;
    cell->bg = store->pen_bg;
    cell->attrs = store->pen_attrs;
    if (store->pen_protected) {
        cell->attrs |= YETTY_YSCENE_VTERMGRID_STORE_ATTR_PROTECTED;
    }
    cell->width = (uint8_t)(wide ? 2 : 1);
    if (wide) {
        /* The continuation cell: width 0, pen colors, no glyph — the render
         * paints the head's right half over it (the fork's WIDE_CONT shape). */
        struct yetty_yscene_vtermgrid_store_cell *spill = store_cell_at(store, row, col + 1);
        if (spill) {
            store_blank_cell(spill, store->pen_fg, store->pen_bg);
            spill->attrs = store->pen_attrs;
            spill->width = 0;
        }
    }
    return YETTY_OK_VOID();
}

void yetty_yscene_vtermgrid_store_erase(struct yetty_yscene_vtermgrid_store *store,
                                        uint32_t start_row, uint32_t end_row, uint32_t start_col,
                                        uint32_t end_col)
{
    yetty_yscene_vtermgrid_store_erase_selective(store, start_row, end_row, start_col, end_col, 0);
}

void yetty_yscene_vtermgrid_store_erase_selective(struct yetty_yscene_vtermgrid_store *store,
                                                  uint32_t start_row, uint32_t end_row,
                                                  uint32_t start_col, uint32_t end_col,
                                                  int selective)
{
    for (uint32_t row = start_row; row < end_row && row < store->rows; ++row) {
        for (uint32_t col = start_col; col < end_col && col < store->cols; ++col) {
            struct yetty_yscene_vtermgrid_store_cell *cell = store_cell_at(store, row, col);
            if (selective && (cell->attrs & YETTY_YSCENE_VTERMGRID_STORE_ATTR_PROTECTED) != 0) {
                continue; /* DECSCA-protected: selective erase passes over */
            }
            /* BCE: erased cells take the current pen colors. */
            store_blank_cell(cell, store->pen_fg, store->pen_bg);
            cell->attrs = 0;
        }
    }
}

void yetty_yscene_vtermgrid_store_scroll(struct yetty_yscene_vtermgrid_store *store, int amount)
{
    if (!store->cells || amount == 0) {
        return;
    }
    uint32_t magnitude = amount > 0 ? (uint32_t)amount : (uint32_t)(-amount);
    if (magnitude >= store->rows) {
        yetty_yscene_vtermgrid_store_erase(store, 0, store->rows, 0, store->cols);
        return;
    }
    size_t row_bytes = (size_t)store->cols * sizeof(struct yetty_yscene_vtermgrid_store_cell);
    if (amount > 0) {
        /* Content moves UP; the bottom rows vacate. */
        memmove(store->cells, store->cells + (size_t)magnitude * store->cols,
                (size_t)(store->rows - magnitude) * row_bytes);
        yetty_yscene_vtermgrid_store_erase(store, store->rows - magnitude, store->rows, 0,
                                           store->cols);
    } else {
        /* Content moves DOWN; the top rows vacate. */
        memmove(store->cells + (size_t)magnitude * store->cols, store->cells,
                (size_t)(store->rows - magnitude) * row_bytes);
        yetty_yscene_vtermgrid_store_erase(store, 0, magnitude, 0, store->cols);
    }
}

void yetty_yscene_vtermgrid_store_hshift(struct yetty_yscene_vtermgrid_store *store, uint32_t row,
                                         uint32_t start_col, int amount)
{
    if (!store->cells || row >= store->rows || start_col >= store->cols || amount == 0) {
        return;
    }
    uint32_t magnitude = amount > 0 ? (uint32_t)amount : (uint32_t)(-amount);
    uint32_t span = store->cols - start_col;
    if (magnitude >= span) {
        yetty_yscene_vtermgrid_store_erase(store, row, row + 1, start_col, store->cols);
        return;
    }
    struct yetty_yscene_vtermgrid_store_cell *base = store_cell_at(store, row, start_col);
    size_t cell_bytes = sizeof(struct yetty_yscene_vtermgrid_store_cell);
    if (amount > 0) {
        /* ICH: cells shift right; the gap at start_col blanks (BCE). */
        memmove(base + magnitude, base, (size_t)(span - magnitude) * cell_bytes);
        yetty_yscene_vtermgrid_store_erase(store, row, row + 1, start_col, start_col + magnitude);
    } else {
        /* DCH: cells shift left; the tail blanks (BCE). */
        memmove(base, base + magnitude, (size_t)(span - magnitude) * cell_bytes);
        yetty_yscene_vtermgrid_store_erase(store, row, row + 1, store->cols - magnitude,
                                           store->cols);
    }
}

struct yetty_ycore_void_result yetty_yscene_vtermgrid_store_resize(
    struct yetty_yscene_vtermgrid_store *store, uint32_t rows, uint32_t cols)
{
    if (!store || rows == 0 || cols == 0) {
        return YETTY_ERR(yetty_ycore_void, "vtermgrid store resize: invalid size");
    }
    struct yetty_yscene_vtermgrid_store_cell *grown =
        calloc((size_t)rows * cols, sizeof(struct yetty_yscene_vtermgrid_store_cell));
    if (!grown) {
        return YETTY_ERR(yetty_ycore_void, "vtermgrid store resize: alloc");
    }
    for (size_t index = 0; index < (size_t)rows * cols; ++index) {
        store_blank_cell(&grown[index], store->default_fg, store->default_bg);
    }
    /* Preserve the overlapping top-left rectangle. */
    uint32_t copy_rows = rows < store->rows ? rows : store->rows;
    uint32_t copy_cols = cols < store->cols ? cols : store->cols;
    for (uint32_t row = 0; row < copy_rows; ++row) {
        memcpy(grown + (size_t)row * cols, store->cells + (size_t)row * store->cols,
               (size_t)copy_cols * sizeof(struct yetty_yscene_vtermgrid_store_cell));
    }
    free(store->cells);
    store->cells = grown;
    store->rows = rows;
    store->cols = cols;
    return YETTY_OK_VOID();
}

void yetty_yscene_vtermgrid_store_read(const struct yetty_yscene_vtermgrid_store *store,
                                       uint32_t row, uint32_t col,
                                       struct yetty_yscene_vtermgrid_store_cell *out)
{
    memset(out, 0, sizeof(*out));
    out->width = 1;
    out->fg = store ? store->default_fg : 0;
    out->bg = store ? store->default_bg : 0;
    if (!store || !store->cells || row >= store->rows || col >= store->cols) {
        return;
    }
    *out = store->cells[(size_t)row * store->cols + col];
    /* Reverse resolution at READ time — the render contract the fork's screen
     * layer establishes by pre-swapping: per-cell reverse XOR screen-wide
     * DECSCNM swaps fg/bg. */
    int reversed =
        ((out->attrs & VTERMGRID_STORE_ATTR_REVERSE) != 0) ^ (store->global_reverse != 0);
    if (reversed) {
        uint32_t swap = out->fg;
        out->fg = out->bg;
        out->bg = swap;
    }
}
