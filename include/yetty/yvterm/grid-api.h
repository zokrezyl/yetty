/*
 * grid-api.h - unified yvterm grid data model.
 *
 * This is the model layer, not the renderer. It owns the libvterm State
 * integration and the terminal line ring. A yfigure renderer should read this
 * model and build text, primitive, and composite draw work from it.
 *
 * One CPU-side truth: every visible row is a line in a ring; each line carries
 * text cells, anchored rich-content storage (primitive arena + composite
 * objects), and per-cell references that name the rich content covering that
 * cell. libvterm State callbacks transform this one model; scrolling advances
 * the ring base so text and rich content move together.
 */
#ifndef YETTY_YVTERM_GRID_API_H
#define YETTY_YVTERM_GRID_API_H

#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    YETTY_YVTERM_ATTR_BOLD = 1u << 0,
    YETTY_YVTERM_ATTR_UNDERLINE = 1u << 1,
    YETTY_YVTERM_ATTR_UNDERLINE2 = 1u << 2,
    YETTY_YVTERM_ATTR_ITALIC = 1u << 3,
    YETTY_YVTERM_ATTR_REVERSE = 1u << 4,
    YETTY_YVTERM_ATTR_BLINK = 1u << 5,
    YETTY_YVTERM_ATTR_STRIKE = 1u << 6,
    YETTY_YVTERM_ATTR_CONCEAL = 1u << 7,
};

struct yetty_yvterm_text_cell {
    uint32_t glyph_index; /* renderer-owned cache; 0 means unresolved/blank */
    uint32_t codepoint;   /* source scalar for selection and glyph lookup */
    uint32_t fg;          /* packed RGBA, byte order expected by shaders */
    uint32_t bg;
    uint16_t attrs;
    uint8_t width; /* 1 normal, 2 wide head, 0 wide spill cell */
    uint8_t flags;
};

/* Opaque rich composite (yplot, ypdf, ymarkdown, …). The anchor line OWNS the
 * composite objects it stores and destroys them through ops->destroy when the
 * line is rolled off, blanked, or cleared. Cells only ever hold borrowed
 * {rel_line, index_in_list} references into the anchor line's composite list. */
struct yetty_ydraw_composite;

/* A primitive anchored on a line: a contiguous span of words in that line's
 * arena. The renderer reads `word_count` words at `arena_offset` from the
 * owning line's arena and interprets them per the ydraw primitive layout. */
struct yetty_yvterm_primitive {
    uint32_t arena_offset; /* first word in the owning line's arena */
    uint32_t word_count;   /* number of words this primitive occupies */
};

/* A reference from a covered cell to a primitive anchored on another line.
 * rel_line walks downward from the covered cell to the anchor line; the anchor
 * line is `covered_line + rel_line`. Anchoring at the bottom row keeps rich
 * content alive until its last visible row exits. index_in_list selects the
 * primitive within the anchor line's `primitives` array. */
struct yetty_yvterm_primitive_ref {
    uint16_t rel_line;
    uint16_t index_in_list;
};

/* A reference from a covered cell to a composite anchored on another line.
 * Same anchor convention as a primitive ref; index_in_list selects the
 * composite within the anchor line's `composites` array. */
struct yetty_yvterm_composite_ref {
    uint16_t rel_line;
    uint16_t index_in_list;
};

/* Per-cell borrowed references to the rich content covering this cell. Both
 * arrays are appendable and carry explicit capacities so the model can append,
 * move, clear, and free deterministically — never pointer-to-pointer without
 * a capacity field. */
struct yetty_yvterm_drawable_refs {
    struct yetty_yvterm_primitive_ref *primitive_refs;
    uint16_t primitive_ref_count;
    uint16_t primitive_ref_capacity;

    struct yetty_yvterm_composite_ref *composite_refs;
    uint16_t composite_ref_count;
    uint16_t composite_ref_capacity;
};

struct yetty_yvterm_line {
    struct yetty_yvterm_text_cell *text_cells;
    struct yetty_yvterm_drawable_refs *drawable_refs;

    /* Anchored primitive storage owned by this line. */
    struct yetty_yvterm_primitive *primitives;
    uint32_t primitive_count;
    uint32_t primitive_capacity;

    /* Word arena the line's primitives index into. */
    uint32_t *arena;
    uint32_t arena_count;
    uint32_t arena_capacity;

    /* Anchored composite storage owned by this line; destroyed on roll-off. */
    struct yetty_ydraw_composite **composites;
    uint32_t composite_count;
    uint32_t composite_capacity;

    int dirty;
};

struct yetty_yvterm_grid;

YETTY_YRESULT_DECLARE(yetty_yvterm_grid_ptr, struct yetty_yvterm_grid *);

/* Fired after a full-screen erase clears the visible lines, so a terminal can
 * also drop external figures (e.g. the root figure container) the model does
 * not own. */
typedef struct yetty_ycore_void_result (*yetty_yvterm_grid_clear_hook_fn)(void *userdata);

/* ---------------------------------------------------------------------------
 * Lifecycle / feed / resize
 * ------------------------------------------------------------------------- */
struct yetty_yvterm_grid_ptr_result yetty_yvterm_grid_model_create(uint32_t cols, uint32_t rows);
struct yetty_ycore_void_result yetty_yvterm_grid_model_destroy(struct yetty_yvterm_grid *grid);
struct yetty_ycore_void_result yetty_yvterm_grid_model_feed(struct yetty_yvterm_grid *grid,
                                                            const char *bytes, size_t len);
struct yetty_ycore_void_result yetty_yvterm_grid_model_resize(struct yetty_yvterm_grid *grid,
                                                              uint32_t cols, uint32_t rows);

/* ---------------------------------------------------------------------------
 * Read-only views (renderer + terminal)
 * ------------------------------------------------------------------------- */
uint32_t yetty_yvterm_grid_model_cols(const struct yetty_yvterm_grid *grid);
uint32_t yetty_yvterm_grid_model_visible_rows(const struct yetty_yvterm_grid *grid);
uint32_t yetty_yvterm_grid_model_base(const struct yetty_yvterm_grid *grid);
struct yetty_yvterm_line *yetty_yvterm_grid_model_lines(struct yetty_yvterm_grid *grid);
void yetty_yvterm_grid_model_cursor(const struct yetty_yvterm_grid *grid, uint32_t *out_row,
                                    uint32_t *out_col, uint32_t *out_visible);
int yetty_yvterm_grid_model_alt_screen(const struct yetty_yvterm_grid *grid);
int yetty_yvterm_grid_model_mouse_mode(const struct yetty_yvterm_grid *grid);

int yetty_yvterm_grid_model_is_dirty(const struct yetty_yvterm_grid *grid);
void yetty_yvterm_grid_model_clear_dirty(struct yetty_yvterm_grid *grid);
void yetty_yvterm_grid_model_force_full_dirty(struct yetty_yvterm_grid *grid);

void yetty_yvterm_grid_model_set_clear_hook(struct yetty_yvterm_grid *grid,
                                            yetty_yvterm_grid_clear_hook_fn fn, void *userdata);

/* ---------------------------------------------------------------------------
 * Rich-content insertion. Primitives and composites are anchored to a terminal
 * line (preferably the bottom row of the content they cover); covered cells
 * then reference them with a downward rel_line + the index returned here.
 * ------------------------------------------------------------------------- */

/* Append `word_count` arena words to the anchor line `row` as one primitive.
 * Returns the new primitive's index within that line's `primitives` list. */
struct yetty_ycore_uint32_result yetty_yvterm_grid_model_append_primitive(
    struct yetty_yvterm_grid *grid, uint32_t row, const uint32_t *words, uint32_t word_count);

/* Record on the covered cell (row, col) that a primitive `rel_line` rows below
 * at `index_in_list` covers it. */
struct yetty_ycore_void_result yetty_yvterm_grid_model_add_primitive_ref(
    struct yetty_yvterm_grid *grid, uint32_t row, uint32_t col, uint16_t rel_line,
    uint16_t index_in_list);

/* Attach a composite to the anchor line `row`; the line takes ownership and
 * destroys it on roll-off/clear. Returns its index within `composites`. */
struct yetty_ycore_uint32_result yetty_yvterm_grid_model_attach_composite(
    struct yetty_yvterm_grid *grid, uint32_t row, struct yetty_ydraw_composite *composite);

/* Record on the covered cell (row, col) that a composite `rel_line` rows below
 * at `index_in_list` covers it. */
struct yetty_ycore_void_result yetty_yvterm_grid_model_add_composite_ref(
    struct yetty_yvterm_grid *grid, uint32_t row, uint32_t col, uint16_t rel_line,
    uint16_t index_in_list);

/* Clear rich content (anchored storage + all per-cell refs) for one line, a
 * row range [start_row, end_row), or every visible line. Owned composites on
 * the affected lines are destroyed. */
struct yetty_ycore_void_result yetty_yvterm_grid_model_clear_rich_line(
    struct yetty_yvterm_grid *grid, uint32_t row);
struct yetty_ycore_void_result yetty_yvterm_grid_model_clear_rich_range(
    struct yetty_yvterm_grid *grid, uint32_t start_row, uint32_t end_row);
struct yetty_ycore_void_result yetty_yvterm_grid_model_clear_rich_all(
    struct yetty_yvterm_grid *grid);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YVTERM_GRID_API_H */
