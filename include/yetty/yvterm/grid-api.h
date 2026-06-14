/*
 * grid-api.h — hand-written data model + API for yvterm_new:grid.
 *
 * Holds the line / cell / primitive-reference structs the grid is built from
 * and that a renderer walks to build GPU buffers, plus the grid lifecycle and
 * accessor prototypes. The grid object itself (the yclass body) stays private
 * to grid.c; this header forward-declares it.
 *
 * Once yclass codegen is wired, the generated grid.h carries the class
 * identity; this header stays the hand-written data/API surface.
 */
#ifndef YETTY_YVTERM_NEW_GRID_API_H
#define YETTY_YVTERM_NEW_GRID_API_H

#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===========================================================================
 * Cell — one text cell, composed from VTermState. Colors are packed 0xRRGGBBAA.
 * ========================================================================= */

enum {
    YETTY_YVTERM_NEW_ATTR_BOLD = 1u << 0,
    YETTY_YVTERM_NEW_ATTR_UNDERLINE = 1u << 1, /* 2 bits: off/single/double/curly */
    YETTY_YVTERM_NEW_ATTR_UNDERLINE2 = 1u << 2,
    YETTY_YVTERM_NEW_ATTR_ITALIC = 1u << 3,
    YETTY_YVTERM_NEW_ATTR_REVERSE = 1u << 4,
    YETTY_YVTERM_NEW_ATTR_BLINK = 1u << 5,
    YETTY_YVTERM_NEW_ATTR_STRIKE = 1u << 6,
    YETTY_YVTERM_NEW_ATTR_CONCEAL = 1u << 7,
};

struct yetty_yvterm_new_text_cell {
    uint32_t glyph_index; /* resolved MSDF/raster glyph; 0 = blank          */
    uint32_t codepoint;   /* source character, kept for selection / reflow  */
    uint32_t fg;          /* foreground, packed 0xRRGGBBAA                   */
    uint32_t bg;          /* background, packed 0xRRGGBBAA                   */
    uint16_t attrs;       /* YETTY_YVTERM_NEW_ATTR_* bitfield               */
    uint8_t width;        /* 1 normal, 2 double-width head, 0 spillover cell */
    uint8_t flags;        /* reserved (overlay-present, wrapped, ...)        */
};

/* ===========================================================================
 * Primitives & references — REUSED from the old model (src/yetty/ydraw). A
 * primitive is stored as the old grid_line stores it: a `struct drawable_data`
 * descriptor { rolling_row, arena_offset, word_count } pointing into the line's
 * word `arena` (words[0] = type, then style + geometry). Composites use the old
 * lower-level `struct yetty_ydraw_composite` (ydraw-core/figure.h). Both are
 * forward-declared here; see grid.c for the full notes.
 * ========================================================================= */

struct drawable_data;         /* old model — src/yetty/ydraw/scrolling-grid.c   */
struct yetty_ydraw_composite; /* old model — include/yetty/ydraw-core/figure.h  */

/* A back-reference from a covered cell to a primitive on its ANCHOR line. The
 * anchor sits BELOW the covered cell: anchor_line = covered_line + rel_line,
 * the anchor being the primitive's bottom row (last to leave on scroll-up). */
struct primitive_ref {
    uint16_t rel_line;      /* rows DOWN from this cell to the anchor line     */
    uint16_t index_in_list; /* index into the anchor line's primitive list     */
};

/* Per-cell drawable references — ONE per text cell: that cell's covering
 * primitives plus the composites over it. */
struct yetty_yvterm_new_drawable_refs {
    struct primitive_ref **primitive_refs; /* this cell's covering primitives  */
    uint16_t num_primitive_refs;
    struct yetty_ydraw_composite **figures; /* composites over this cell        */
    uint16_t num_figures;
};

struct yetty_yvterm_new_line {
    /* `cols` text cells, one per column. */
    struct yetty_yvterm_new_text_cell *text_cells;

    /* Parallel grid to text_cells — `cols` entries, one per cell. */
    struct yetty_yvterm_new_drawable_refs *drawable_cells_refs;

    /* Primitives this line anchors — OWNED, drawable_data descriptors into the
     * word arena below. */
    struct drawable_data *prims;
    uint32_t num_primitives;
    uint32_t *arena;
    uint32_t arena_count;
    uint32_t arena_capacity;

    int dirty; /* needs GPU re-upload */
};

/* The grid object (yclass body) stays private to grid.c. */
struct yetty_yvterm_new_grid;

YETTY_YRESULT_DECLARE(yetty_yvterm_new_grid_ptr, struct yetty_yvterm_new_grid *);

/* ===========================================================================
 * Lifecycle / API
 * ========================================================================= */

/* Create the grid at cols x rows. Drives libvterm's STATE layer; the line ring
 * is `rows` lines. */
struct yetty_yvterm_new_grid_ptr_result yetty_yvterm_new_grid_create(uint32_t cols, uint32_t rows);

struct yetty_ycore_void_result yetty_yvterm_new_grid_destroy(struct yetty_yvterm_new_grid *grid);

/* Feed raw PTY bytes through the state machine; updates cells, marks lines
 * dirty. */
struct yetty_ycore_void_result yetty_yvterm_new_grid_feed(struct yetty_yvterm_new_grid *grid,
                                                          const char *bytes, size_t len);

/* Reflow to a new viewport: reallocate the line ring and resize the vterm
 * state grid. Re-blanks and marks everything dirty. */
struct yetty_ycore_void_result yetty_yvterm_new_grid_resize(struct yetty_yvterm_new_grid *grid,
                                                            uint32_t cols, uint32_t rows);

/* Accessors for the renderer. */
uint32_t yetty_yvterm_new_grid_cols(const struct yetty_yvterm_new_grid *grid);
uint32_t yetty_yvterm_new_grid_visible_rows(const struct yetty_yvterm_new_grid *grid);
uint32_t yetty_yvterm_new_grid_base(const struct yetty_yvterm_new_grid *grid);
struct yetty_yvterm_new_line *yetty_yvterm_new_grid_lines(struct yetty_yvterm_new_grid *grid);
void yetty_yvterm_new_grid_cursor(const struct yetty_yvterm_new_grid *grid, uint32_t *out_row,
                                  uint32_t *out_col, uint32_t *out_visible);

int yetty_yvterm_new_grid_is_dirty(const struct yetty_yvterm_new_grid *grid);
void yetty_yvterm_new_grid_clear_dirty(struct yetty_yvterm_new_grid *grid);
void yetty_yvterm_new_grid_force_full_dirty(struct yetty_yvterm_new_grid *grid);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YVTERM_NEW_GRID_API_H */
