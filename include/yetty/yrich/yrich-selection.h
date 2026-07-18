#ifndef YETTY_YRICH_YRICH_SELECTION_H
#define YETTY_YRICH_YRICH_SELECTION_H

/*
 * yrich-selection — unified selection state.
 *
 * Replaces the C++ std::variant with an explicit tagged union. The original
 * supported four kinds: none, element list, cell range, text range. The C
 * type owns its element-id buffer; mutators reallocate as needed.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>
#include <yetty/yrich/yrich-types.h>

#ifdef __cplusplus
extern "C" {
#endif

enum yetty_yrich_selection_kind {
    YETTY_YRICH_SEL_NONE = 0,
    YETTY_YRICH_SEL_ELEMENTS,
    YETTY_YRICH_SEL_CELLS,
    YETTY_YRICH_SEL_TEXT,
};

struct yetty_yrich_selection_elements {
    yetty_yrich_element_id *ids; /* ordered by selection time */
    size_t count;
    size_t capacity;
};

struct yetty_yrich_selection_cells {
    struct yetty_yrich_cell_range range;
    struct yetty_yrich_cell_addr active;
};

struct yetty_yrich_selection_text {
    yetty_yrich_element_id element_id; /* anchor paragraph */
    int32_t start;                     /* anchor offset within it */
    /* Focus (caret) end. Equal to element_id for a selection contained in one
     * paragraph; a differing id means the selection spans paragraphs from the
     * anchor to the focus in document order. */
    yetty_yrich_element_id focus_element_id;
    int32_t end; /* focus offset within focus_element_id's paragraph */
};

struct yetty_yrich_selection {
    uint32_t kind; /* enum yetty_yrich_selection_kind */
    union {
        struct yetty_yrich_selection_elements elements;
        struct yetty_yrich_selection_cells cells;
        struct yetty_yrich_selection_text text;
    } u;
};

/*=============================================================================
 * Lifecycle
 *===========================================================================*/

/* Initialise to NONE. Safe to call on already-initialised selection (frees
 * existing storage first). */
void yetty_yrich_selection_init(struct yetty_yrich_selection *s);

/* Free any owned storage and reset to NONE. */
void yetty_yrich_selection_clear(struct yetty_yrich_selection *s);

/*=============================================================================
 * Setters — replace current selection
 *===========================================================================*/

/* Replace selection with a single element. Errors on alloc failure. */
struct yetty_ycore_void_result yetty_yrich_selection_select_element(struct yetty_yrich_selection *s,
                                                                    yetty_yrich_element_id id);

void yetty_yrich_selection_select_cells(struct yetty_yrich_selection *s,
                                        struct yetty_yrich_cell_range range,
                                        struct yetty_yrich_cell_addr active);

void yetty_yrich_selection_select_cell(struct yetty_yrich_selection *s,
                                       struct yetty_yrich_cell_addr addr);

void yetty_yrich_selection_select_text(struct yetty_yrich_selection *s,
                                       yetty_yrich_element_id element_id, int32_t start,
                                       int32_t end);

/* Text selection spanning paragraphs: anchor stays at (anchor_id, start),
 * the focus caret sits at (focus_id, end). */
void yetty_yrich_selection_select_text_range(struct yetty_yrich_selection *s,
                                             yetty_yrich_element_id anchor_id, int32_t start,
                                             yetty_yrich_element_id focus_id, int32_t end);

void yetty_yrich_selection_set_cursor(struct yetty_yrich_selection *s,
                                      yetty_yrich_element_id element_id, int32_t position);

/*=============================================================================
 * Element-set mutators (only valid when kind == ELEMENTS)
 *===========================================================================*/

bool yetty_yrich_selection_contains(const struct yetty_yrich_selection *s,
                                    yetty_yrich_element_id id);

struct yetty_ycore_void_result yetty_yrich_selection_add(struct yetty_yrich_selection *s,
                                                         yetty_yrich_element_id id);

void yetty_yrich_selection_remove(struct yetty_yrich_selection *s, yetty_yrich_element_id id);

struct yetty_ycore_void_result yetty_yrich_selection_toggle(struct yetty_yrich_selection *s,
                                                            yetty_yrich_element_id id);

/* "Extend" — add element to existing element selection or convert to one. */
struct yetty_ycore_void_result yetty_yrich_selection_extend_element(struct yetty_yrich_selection *s,
                                                                    yetty_yrich_element_id id);

/* "Extend" cell range to addr (anchor stays put). */
void yetty_yrich_selection_extend_cell(struct yetty_yrich_selection *s,
                                       struct yetty_yrich_cell_addr addr);

/* "Extend" text selection end position. */
void yetty_yrich_selection_extend_text(struct yetty_yrich_selection *s, int32_t position);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YRICH_YRICH_SELECTION_H */
