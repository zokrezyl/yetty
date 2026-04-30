#ifndef YETTY_YRICH_YSPREADSHEET_H
#define YETTY_YRICH_YSPREADSHEET_H

/*
 * yspreadsheet — grid document.
 *
 * Cells are subclasses of element. A cell knows its address; the grid knows
 * how to derive screen-space bounds from row/col cumulative offsets.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>
#include <yetty/yrich/yrich-document.h>
#include <yetty/yrich/yrich-element.h>
#include <yetty/yrich/yrich-types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*=============================================================================
 * Cell — concrete element type with text + cell-specific styling.
 *===========================================================================*/

struct yetty_yrich_cell {
    struct yetty_yrich_element base;

    struct yetty_yrich_cell_addr address;
    struct yetty_yrich_rect bounds;
    char *text; /* owned, NUL-terminated */
    size_t text_len;
    char *formula; /* owned, may be NULL */

    struct yetty_yrich_text_style style;
    uint32_t fill_color;
    struct yetty_yrich_border border;

    uint32_t halign;
    uint32_t valign;

    bool editing;
    int32_t cursor_pos;
    int32_t sel_start;
    int32_t sel_end;
};

YETTY_YRESULT_DECLARE(yetty_yrich_cell_ptr, struct yetty_yrich_cell *);

struct yetty_yrich_cell_ptr_result yetty_yrich_cell_create(yetty_yrich_element_id id,
                                                           struct yetty_yrich_cell_addr addr,
                                                           struct yetty_yrich_rect bounds);

void yetty_yrich_cell_set_text(struct yetty_yrich_cell *cell, const char *text, size_t len);

/*=============================================================================
 * Spreadsheet
 *===========================================================================*/

struct yetty_yrich_row_size {
    int32_t row;
    float height;
};

struct yetty_yrich_col_size {
    int32_t col;
    float width;
};

struct yetty_yrich_cell_entry {
    struct yetty_yrich_cell_addr addr;
    struct yetty_yrich_cell *cell; /* aliases doc->elements entry */
};

struct yetty_yrich_spreadsheet {
    struct yetty_yrich_document base;

    int32_t row_count;
    int32_t col_count;
    float default_row_height;
    float default_col_width;

    struct yetty_yrich_row_size *row_overrides;
    size_t row_override_count;
    size_t row_override_capacity;

    struct yetty_yrich_col_size *col_overrides;
    size_t col_override_count;
    size_t col_override_capacity;

    /* Cumulative-position cache. */
    float *row_y_cache;
    size_t row_y_cache_count;
    float *col_x_cache;
    size_t col_x_cache_count;
    bool cache_valid;

    /* Address → cell lookup (linear scan, fine for typical grids). */
    struct yetty_yrich_cell_entry *cells;
    size_t cell_count;
    size_t cell_capacity;

    struct yetty_yrich_cell *editing_cell; /* aliases */
};

YETTY_YRESULT_DECLARE(yetty_yrich_spreadsheet_ptr, struct yetty_yrich_spreadsheet *);

struct yetty_yrich_spreadsheet_ptr_result yetty_yrich_spreadsheet_create(void);

/*=============================================================================
 * Grid configuration
 *===========================================================================*/

void yetty_yrich_spreadsheet_set_grid_size(struct yetty_yrich_spreadsheet *s, int32_t rows,
                                           int32_t cols);

void yetty_yrich_spreadsheet_set_row_height(struct yetty_yrich_spreadsheet *s, int32_t row,
                                            float h);

void yetty_yrich_spreadsheet_set_col_width(struct yetty_yrich_spreadsheet *s, int32_t col, float w);

float yetty_yrich_spreadsheet_row_height(const struct yetty_yrich_spreadsheet *s, int32_t row);

float yetty_yrich_spreadsheet_col_width(const struct yetty_yrich_spreadsheet *s, int32_t col);

float yetty_yrich_spreadsheet_row_y(struct yetty_yrich_spreadsheet *s, int32_t row);

float yetty_yrich_spreadsheet_col_x(struct yetty_yrich_spreadsheet *s, int32_t col);

/*=============================================================================
 * Cell access
 *===========================================================================*/

struct yetty_yrich_cell *yetty_yrich_spreadsheet_cell_at(const struct yetty_yrich_spreadsheet *s,
                                                         struct yetty_yrich_cell_addr addr);

struct yetty_yrich_cell_ptr_result yetty_yrich_spreadsheet_ensure_cell(
    struct yetty_yrich_spreadsheet *s, struct yetty_yrich_cell_addr addr);

struct yetty_ycore_void_result yetty_yrich_spreadsheet_set_cell_value(
    struct yetty_yrich_spreadsheet *s, struct yetty_yrich_cell_addr addr, const char *value,
    size_t value_len);

const char *yetty_yrich_spreadsheet_cell_value(const struct yetty_yrich_spreadsheet *s,
                                               struct yetty_yrich_cell_addr addr);

struct yetty_yrich_cell_addr yetty_yrich_spreadsheet_cell_addr_at(struct yetty_yrich_spreadsheet *s,
                                                                  float x, float y);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YRICH_YSPREADSHEET_H */
