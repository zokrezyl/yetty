/*
 * grid.c - yvterm unified terminal model datatypes.
 *
 * This file intentionally contains only the data layout for the new yvterm
 * model. No libvterm callbacks, no renderer, no yfigure wrapper, no methods.
 *
 * Design rule: one CPU-side terminal truth. Text cells, primitive references,
 * composite references, and anchored rich-content ownership live together on
 * the same scrolling line ring.
 */
#include <stddef.h>
#include <stdint.h>

/* Opaque external types. The implementation layer includes the real headers. */
typedef struct VTerm VTerm;
typedef struct VTermState VTermState;
struct yetty_ydraw_composite;
struct yetty_ycore_void_result;

/* Text attributes copied from libvterm pen state into each written cell. */
enum yetty_yvterm_text_attr {
    YETTY_YVTERM_ATTR_BOLD = 1u << 0,
    YETTY_YVTERM_ATTR_UNDERLINE = 1u << 1,
    YETTY_YVTERM_ATTR_UNDERLINE2 = 1u << 2,
    YETTY_YVTERM_ATTR_ITALIC = 1u << 3,
    YETTY_YVTERM_ATTR_REVERSE = 1u << 4,
    YETTY_YVTERM_ATTR_BLINK = 1u << 5,
    YETTY_YVTERM_ATTR_STRIKE = 1u << 6,
    YETTY_YVTERM_ATTR_CONCEAL = 1u << 7,
};

/* One terminal text cell. Glyph lookup is renderer-owned; the model stores the
 * source codepoint and style. width is 1 for normal cells, 2 for a wide glyph
 * head, and 0 for the wide glyph spill cell. */
struct yetty_yvterm_text_cell {
    uint32_t glyph_index;
    uint32_t codepoint;
    uint32_t fg;
    uint32_t bg;
    uint16_t attrs;
    uint8_t width;
    uint8_t flags;
};

/* A simple primitive anchored on a terminal line. The primitive payload is a
 * span of u32 words in the owning line's arena. */
struct yetty_yvterm_primitive {
    uint32_t arena_offset;
    uint32_t word_count;
};

/* Borrowed reference from a covered cell to a primitive anchored on another
 * line. The anchor is below the covered cell: anchor_row = row + rel_line. */
struct yetty_yvterm_primitive_ref {
    uint16_t rel_line;
    uint16_t index_in_list;
};

/* Borrowed reference from a covered cell to a composite anchored on another
 * line, using the same downward-anchor convention as primitive refs. */
struct yetty_yvterm_composite_ref {
    uint16_t rel_line;
    uint16_t index_in_list;
};

/* Rich content covering one terminal cell. These are borrowed refs only; owned
 * primitive/composite storage lives on the anchor line. */
struct yetty_yvterm_drawable_refs {
    struct yetty_yvterm_primitive_ref *primitive_refs;
    uint16_t primitive_ref_count;
    uint16_t primitive_ref_capacity;

    struct yetty_yvterm_composite_ref *composite_refs;
    uint16_t composite_ref_count;
    uint16_t composite_ref_capacity;
};

/* One slot in the scrolling line ring. The line owns its text cells, per-cell
 * rich refs, primitive arena/descriptors, and anchored composites. */
struct yetty_yvterm_line {
    struct yetty_yvterm_text_cell *text_cells;
    struct yetty_yvterm_drawable_refs *drawable_refs;

    struct yetty_yvterm_primitive *primitives;
    uint32_t primitive_count;
    uint32_t primitive_capacity;

    uint32_t *arena;
    uint32_t arena_count;
    uint32_t arena_capacity;

    struct yetty_ydraw_composite **composites;
    uint32_t composite_count;
    uint32_t composite_capacity;

    int dirty;
};

/* Optional hook owned by the later integration layer. Full-screen terminal
 * clear may need to drop external yfigures that the grid model does not own. */
typedef struct yetty_ycore_void_result (*yetty_yvterm_grid_clear_hook_fn)(void *userdata);

/* The unified terminal grid. visible row N maps to:
 *
 *     lines[(base + N) % visible_rows]
 *
 * Whole-screen scroll advances base and reuses rolled-off line slots. */
struct yetty_yvterm_grid {
    struct yetty_yvterm_line *lines;
    uint32_t line_count;
    uint32_t visible_rows;
    uint32_t cols;
    uint32_t base;

    VTerm *vterm;
    VTermState *state;

    uint32_t pen_fg;
    uint32_t pen_bg;
    uint32_t default_fg;
    uint32_t default_bg;
    uint16_t pen_attrs;
    int pen_reverse;

    uint32_t cursor_row;
    uint32_t cursor_col;
    uint32_t cursor_visible;

    int alt_screen;
    int mouse_mode;

    yetty_yvterm_grid_clear_hook_fn clear_hook_fn;
    void *clear_hook_userdata;

    int has_dirty;
};
