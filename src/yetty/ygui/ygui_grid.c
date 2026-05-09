/*
 * ygui_grid.c - Spatial grid for O(1) widget lookup
 */

#include "ygui_internal.h"
#include <stdio.h>

static FILE *_grid_log(void)
{
    static FILE *f = NULL;
    static int checked = 0;
    if (!checked) {
        checked = 1;
        const char *p = getenv("YGUI_C_LOG");
        if (p) {
            f = fopen(p, "a");
        }
    }
    return f;
}
#define GRID_LOG(...)                                                                              \
    do {                                                                                           \
        FILE *_f = _grid_log();                                                                    \
        if (_f) {                                                                                  \
            fprintf(_f, "[GRID] " __VA_ARGS__);                                                    \
            fprintf(_f, "\n");                                                                     \
            fflush(_f);                                                                            \
        }                                                                                          \
    } while (0)

/*=============================================================================
 * Grid Cell Helpers
 *===========================================================================*/

static void cell_init(ygui_grid_cell_t *cell)
{
    cell->widgets = NULL;
    cell->count = 0;
    cell->capacity = 0;
}

static void cell_destroy(ygui_grid_cell_t *cell)
{
    free(cell->widgets);
    cell->widgets = NULL;
    cell->count = 0;
    cell->capacity = 0;
}

static void cell_clear(ygui_grid_cell_t *cell)
{
    cell->count = 0;
}

static void cell_add(ygui_grid_cell_t *cell, struct yetty_ygui_widget *widget)
{
    if (cell->count >= cell->capacity) {
        int new_cap = cell->capacity == 0 ? 4 : cell->capacity * 2;
        struct yetty_ygui_widget **new_arr = (struct yetty_ygui_widget **)realloc(
            cell->widgets, new_cap * sizeof(struct yetty_ygui_widget *));
        if (!new_arr) {
            return;
        }
        cell->widgets = new_arr;
        cell->capacity = new_cap;
    }
    cell->widgets[cell->count++] = widget;
}

static void cell_remove(ygui_grid_cell_t *cell, struct yetty_ygui_widget *widget)
{
    for (int i = 0; i < cell->count; i++) {
        if (cell->widgets[i] == widget) {
            /* Shift remaining elements */
            for (int j = i; j < cell->count - 1; j++) {
                cell->widgets[j] = cell->widgets[j + 1];
            }
            cell->count--;
            return;
        }
    }
}

/*=============================================================================
 * Grid Implementation
 *===========================================================================*/

void yetty_ygui_grid_init(ygui_spatial_grid_t *grid, float width, float height, float cell_size)
{
    grid->width = width;
    grid->height = height;
    grid->cell_size = cell_size;
    grid->cols = (int)(width / cell_size) + 1;
    grid->rows = (int)(height / cell_size) + 1;

    int total_cells = grid->cols * grid->rows;
    grid->cells = (ygui_grid_cell_t *)calloc(total_cells, sizeof(ygui_grid_cell_t));

    for (int i = 0; i < total_cells; i++) {
        cell_init(&grid->cells[i]);
    }
}

void yetty_ygui_grid_destroy(ygui_spatial_grid_t *grid)
{
    if (!grid->cells) {
        return;
    }

    int total_cells = grid->cols * grid->rows;
    for (int i = 0; i < total_cells; i++) {
        cell_destroy(&grid->cells[i]);
    }
    free(grid->cells);
    grid->cells = NULL;
    grid->cols = 0;
    grid->rows = 0;
}

void yetty_ygui_grid_clear(ygui_spatial_grid_t *grid)
{
    if (!grid->cells) {
        return;
    }

    int total_cells = grid->cols * grid->rows;
    for (int i = 0; i < total_cells; i++) {
        cell_clear(&grid->cells[i]);
    }
}

void yetty_ygui_grid_insert(ygui_spatial_grid_t *grid, struct yetty_ygui_widget *widget)
{
    if (!grid->cells || !widget) {
        return;
    }

    /* Resolved (post-layout) absolute box. */
    float x0 = widget->layout_x;
    float y0 = widget->layout_y;
    float x1 = x0 + widget->layout_w;
    float y1 = y0 + widget->layout_h;

    GRID_LOG("insert widget=%s pos=(%.1f,%.1f) size=(%.1f,%.1f) lay=(%.1f,%.1f,%.1f,%.1f)",
             widget->id ? widget->id : "?", widget->x, widget->y, widget->w, widget->h, x0, y0,
             widget->layout_w, widget->layout_h);

    int col0 = (int)(x0 / grid->cell_size);
    int row0 = (int)(y0 / grid->cell_size);
    int col1 = (int)(x1 / grid->cell_size);
    int row1 = (int)(y1 / grid->cell_size);

    col0 = col0 < 0 ? 0 : (col0 >= grid->cols ? grid->cols - 1 : col0);
    row0 = row0 < 0 ? 0 : (row0 >= grid->rows ? grid->rows - 1 : row0);
    col1 = col1 < 0 ? 0 : (col1 >= grid->cols ? grid->cols - 1 : col1);
    row1 = row1 < 0 ? 0 : (row1 >= grid->rows ? grid->rows - 1 : row1);

    for (int r = row0; r <= row1; r++) {
        for (int c = col0; c <= col1; c++) {
            int idx = r * grid->cols + c;
            cell_add(&grid->cells[idx], widget);
        }
    }

    /* Recursively insert children */
    for (struct yetty_ygui_widget *child = widget->first_child; child;
         child = child->next_sibling) {
        if (child->was_rendered) {
            yetty_ygui_grid_insert(grid, child);
        }
    }
}

void yetty_ygui_grid_remove(ygui_spatial_grid_t *grid, struct yetty_ygui_widget *widget)
{
    if (!grid->cells || !widget) {
        return;
    }

    float x0 = widget->layout_x;
    float y0 = widget->layout_y;
    float x1 = x0 + widget->layout_w;
    float y1 = y0 + widget->layout_h;

    int col0 = (int)(x0 / grid->cell_size);
    int row0 = (int)(y0 / grid->cell_size);
    int col1 = (int)(x1 / grid->cell_size);
    int row1 = (int)(y1 / grid->cell_size);

    col0 = col0 < 0 ? 0 : (col0 >= grid->cols ? grid->cols - 1 : col0);
    row0 = row0 < 0 ? 0 : (row0 >= grid->rows ? grid->rows - 1 : row0);
    col1 = col1 < 0 ? 0 : (col1 >= grid->cols ? grid->cols - 1 : col1);
    row1 = row1 < 0 ? 0 : (row1 >= grid->rows ? grid->rows - 1 : row1);

    for (int r = row0; r <= row1; r++) {
        for (int c = col0; c <= col1; c++) {
            int idx = r * grid->cols + c;
            cell_remove(&grid->cells[idx], widget);
        }
    }

    /* Recursively remove children */
    for (struct yetty_ygui_widget *child = widget->first_child; child;
         child = child->next_sibling) {
        yetty_ygui_grid_remove(grid, child);
    }
}

struct yetty_ygui_widget *yetty_ygui_grid_query(const ygui_spatial_grid_t *grid, float x, float y)
{
    if (!grid->cells) {
        return NULL;
    }

    int col = (int)(x / grid->cell_size);
    int row = (int)(y / grid->cell_size);

    GRID_LOG("query at (%.1f,%.1f) -> cell(%d,%d)", x, y, col, row);

    if (col < 0 || col >= grid->cols || row < 0 || row >= grid->rows) {
        GRID_LOG("  out of bounds");
        return NULL;
    }

    int idx = row * grid->cols + col;
    ygui_grid_cell_t *cell = &grid->cells[idx];

    GRID_LOG("  cell has %d widgets", cell->count);

    /* Return topmost widget (last in list) that contains the point */
    for (int i = cell->count - 1; i >= 0; i--) {
        struct yetty_ygui_widget *w = cell->widgets[i];
        int in_x = (x >= w->layout_x && x < w->layout_x + w->layout_w);
        int in_y = (y >= w->layout_y && y < w->layout_y + w->layout_h);
        GRID_LOG("  check %s: lay=(%.1f,%.1f,%.1f,%.1f) in_x=%d in_y=%d flags=0x%x",
                 w->id ? w->id : "?", w->layout_x, w->layout_y, w->layout_w, w->layout_h, in_x,
                 in_y, w->flags);
        if (in_x && in_y) {
            /* Check visibility */
            if (!(w->flags & YETTY_YGUI_FLAG_VISIBLE) || (w->flags & YETTY_YGUI_FLAG_DISABLED)) {
                GRID_LOG("  -> skipped (not visible or disabled)");
                continue;
            }
            GRID_LOG("  -> HIT: %s", w->id);
            return w;
        }
    }

    GRID_LOG("  -> no hit");
    return NULL;
}
