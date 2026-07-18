#ifndef YETTY_YRICH_YRICH_TYPES_H
#define YETTY_YRICH_YRICH_TYPES_H

/*
 * yrich-types — primitive value types shared across yrich.
 *
 * Ported from yetty-poc/src/yetty/yrich/yrich-types.h. C-side rules:
 *   - structs are POD, copied by value
 *   - colours stored as packed ABGR uint32_t (matches ydraw convention)
 *   - all helpers are static inline; no library code needed
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yclass_object;

/* Pointer-result types for exposed getters whose Result crosses yrich TU
 * boundaries (document base getters consumed by the concrete kinds). The
 * pointee structs live in their own headers; only the pointer crosses here. */
struct yetty_ydraw_drawable_list;
struct yetty_yrich_selection;
YETTY_YRESULT_DECLARE(yetty_yrich_drawable_list_ptr, struct yetty_ydraw_drawable_list *);
YETTY_YRESULT_DECLARE(yetty_yrich_selection_ptr, struct yetty_yrich_selection *);

/*=============================================================================
 * Element identity
 *===========================================================================*/

typedef uint64_t yetty_yrich_element_id;

#define YETTY_YRICH_INVALID_ELEMENT_ID ((yetty_yrich_element_id)0)

YETTY_YRESULT_DECLARE(yetty_yrich_element_id, yetty_yrich_element_id);

/*=============================================================================
 * Document callbacks — documents are yclass objects; both callbacks
 * receive the document object.
 *===========================================================================*/

/* Outgoing-operations callback — used by the future sync layer. */
struct yetty_yrich_operation;
typedef void (*yetty_yrich_sync_cb)(struct yetty_yclass_object *doc_obj,
                                    struct yetty_yrich_operation *const *ops, size_t op_count,
                                    void *userdata);

/* Dirty notification — fired when state changes between renders. */
typedef void (*yetty_yrich_dirty_cb)(struct yetty_yclass_object *doc_obj, void *userdata);

/*=============================================================================
 * Rect — axis-aligned rectangle
 *===========================================================================*/

struct yetty_yrich_rect {
    float x, y, w, h;
};

static inline bool yetty_yrich_rect_contains(const struct yetty_yrich_rect *r, float px, float py)
{
    return px >= r->x && px < r->x + r->w && py >= r->y && py < r->y + r->h;
}

static inline bool yetty_yrich_rect_intersects(const struct yetty_yrich_rect *a,
                                               const struct yetty_yrich_rect *b)
{
    return a->x < b->x + b->w && a->x + a->w > b->x && a->y < b->y + b->h && a->y + a->h > b->y;
}

/*=============================================================================
 * Color — packed RGBA stored as ABGR (ydraw convention)
 *===========================================================================*/

#define YETTY_YRICH_RGBA(r, g, b, a)                                                               \
    (((uint32_t)(uint8_t)(a) << 24) | ((uint32_t)(uint8_t)(b) << 16) |                             \
     ((uint32_t)(uint8_t)(g) << 8) | (uint32_t)(uint8_t)(r))

#define YETTY_YRICH_COLOR_BLACK YETTY_YRICH_RGBA(0, 0, 0, 255)
#define YETTY_YRICH_COLOR_WHITE YETTY_YRICH_RGBA(255, 255, 255, 255)
#define YETTY_YRICH_COLOR_TRANSPARENT YETTY_YRICH_RGBA(0, 0, 0, 0)

/*=============================================================================
 * Text formatting flags
 *===========================================================================*/

enum yetty_yrich_text_format {
    YETTY_YRICH_FMT_NONE = 0,
    YETTY_YRICH_FMT_BOLD = 1u << 0,
    YETTY_YRICH_FMT_ITALIC = 1u << 1,
    YETTY_YRICH_FMT_UNDERLINE = 1u << 2,
    YETTY_YRICH_FMT_STRIKE = 1u << 3,
    YETTY_YRICH_FMT_SUBSCRIPT = 1u << 4,
    YETTY_YRICH_FMT_SUPERSCRIPT = 1u << 5,
};

struct yetty_yrich_text_style {
    float font_size;
    uint32_t color;    /* packed ABGR */
    uint32_t bg_color; /* packed ABGR (0 = transparent) */
    uint32_t format;   /* enum yetty_yrich_text_format flags */
    int32_t font_id;
};

static inline struct yetty_yrich_text_style yetty_yrich_text_style_default(void)
{
    struct yetty_yrich_text_style s = {
        .font_size = 14.0f,
        .color = YETTY_YRICH_COLOR_BLACK,
        .bg_color = YETTY_YRICH_COLOR_TRANSPARENT,
        .format = YETTY_YRICH_FMT_NONE,
        .font_id = 0,
    };
    return s;
}

/*=============================================================================
 * TextRun — a contiguous span of text with uniform style.
 *===========================================================================*/

struct yetty_yrich_text_run {
    int32_t start;
    int32_t end;
    struct yetty_yrich_text_style style;
    /* Hyperlink target id (0 = none); resolves to a URL in the owning ydoc's
     * link table. Runs split on link boundaries so a link is a maximal span. */
    uint32_t link_id;
};

/*=============================================================================
 * Slide — plain value aggregate owned by the slides document (not a class).
 *===========================================================================*/

struct yetty_yrich_slide {
    int32_t index;
    uint32_t bg_color;

    struct yetty_yclass_object **shapes; /* alias pointers; document owns */
    size_t shape_count;
    size_t shape_capacity;
};

YETTY_YRESULT_DECLARE(yetty_yrich_slide_ptr, struct yetty_yrich_slide *);

/*=============================================================================
 * Border style
 *===========================================================================*/

enum yetty_yrich_border_style {
    YETTY_YRICH_BORDER_NONE,
    YETTY_YRICH_BORDER_SOLID,
    YETTY_YRICH_BORDER_DASHED,
    YETTY_YRICH_BORDER_DOTTED,
};

struct yetty_yrich_border {
    float width;
    uint32_t color;
    uint32_t style; /* enum yetty_yrich_border_style */
};

/*=============================================================================
 * Alignment — shared by spreadsheet, slides, and ydoc.
 *===========================================================================*/

enum yetty_yrich_halign {
    YETTY_YRICH_HALIGN_LEFT,
    YETTY_YRICH_HALIGN_CENTER,
    YETTY_YRICH_HALIGN_RIGHT,
    YETTY_YRICH_HALIGN_JUSTIFY,
};

enum yetty_yrich_valign {
    YETTY_YRICH_VALIGN_TOP,
    YETTY_YRICH_VALIGN_MIDDLE,
    YETTY_YRICH_VALIGN_BOTTOM,
};

/*=============================================================================
 * Spreadsheet cell addressing
 *===========================================================================*/

struct yetty_yrich_cell_addr {
    int32_t row;
    int32_t col;
};

YETTY_YRESULT_DECLARE(yetty_yrich_cell_addr, struct yetty_yrich_cell_addr);

static inline bool yetty_yrich_cell_addr_eq(struct yetty_yrich_cell_addr a,
                                            struct yetty_yrich_cell_addr b)
{
    return a.row == b.row && a.col == b.col;
}

struct yetty_yrich_cell_range {
    struct yetty_yrich_cell_addr start;
    struct yetty_yrich_cell_addr end;
};

static inline bool yetty_yrich_cell_range_contains(const struct yetty_yrich_cell_range *r,
                                                   struct yetty_yrich_cell_addr a)
{
    return a.row >= r->start.row && a.row <= r->end.row && a.col >= r->start.col &&
           a.col <= r->end.col;
}

/*=============================================================================
 * Input
 *===========================================================================*/

/* Modifier bitmask — scalar so input slots stay wire-marshallable. */
enum yetty_yrich_mod_flags {
    YETTY_YRICH_MOD_SHIFT = 1u << 0,
    YETTY_YRICH_MOD_CTRL = 1u << 1,
    YETTY_YRICH_MOD_ALT = 1u << 2,
    YETTY_YRICH_MOD_META = 1u << 3,
};

enum yetty_yrich_mouse_button {
    YETTY_YRICH_MOUSE_LEFT = 0,
    YETTY_YRICH_MOUSE_RIGHT = 1,
    YETTY_YRICH_MOUSE_MIDDLE = 2,
};

enum yetty_yrich_key {
    YETTY_YRICH_KEY_UNKNOWN = 0,
    YETTY_YRICH_KEY_ENTER,
    YETTY_YRICH_KEY_TAB,
    YETTY_YRICH_KEY_BACKSPACE,
    YETTY_YRICH_KEY_DELETE,
    YETTY_YRICH_KEY_ESCAPE,
    YETTY_YRICH_KEY_LEFT,
    YETTY_YRICH_KEY_RIGHT,
    YETTY_YRICH_KEY_UP,
    YETTY_YRICH_KEY_DOWN,
    YETTY_YRICH_KEY_HOME,
    YETTY_YRICH_KEY_END,
    YETTY_YRICH_KEY_PAGEUP,
    YETTY_YRICH_KEY_PAGEDOWN,
    YETTY_YRICH_KEY_A,
    YETTY_YRICH_KEY_B,
    YETTY_YRICH_KEY_C,
    YETTY_YRICH_KEY_D,
    YETTY_YRICH_KEY_E,
    YETTY_YRICH_KEY_F,
    YETTY_YRICH_KEY_G,
    YETTY_YRICH_KEY_H,
    YETTY_YRICH_KEY_I,
    YETTY_YRICH_KEY_J,
    YETTY_YRICH_KEY_K,
    YETTY_YRICH_KEY_L,
    YETTY_YRICH_KEY_M,
    YETTY_YRICH_KEY_N,
    YETTY_YRICH_KEY_O,
    YETTY_YRICH_KEY_P,
    YETTY_YRICH_KEY_Q,
    YETTY_YRICH_KEY_R,
    YETTY_YRICH_KEY_S,
    YETTY_YRICH_KEY_T,
    YETTY_YRICH_KEY_U,
    YETTY_YRICH_KEY_V,
    YETTY_YRICH_KEY_W,
    YETTY_YRICH_KEY_X,
    YETTY_YRICH_KEY_Y,
    YETTY_YRICH_KEY_Z,
    YETTY_YRICH_KEY_F1,
    YETTY_YRICH_KEY_F2,
    YETTY_YRICH_KEY_F3,
    YETTY_YRICH_KEY_F4,
    YETTY_YRICH_KEY_F5,
    YETTY_YRICH_KEY_F6,
    YETTY_YRICH_KEY_F7,
    YETTY_YRICH_KEY_F8,
    YETTY_YRICH_KEY_F9,
    YETTY_YRICH_KEY_F10,
    YETTY_YRICH_KEY_F11,
    YETTY_YRICH_KEY_F12,
    /* Digit keys (kept contiguous so KEY_0 + n works). */
    YETTY_YRICH_KEY_0,
    YETTY_YRICH_KEY_1,
    YETTY_YRICH_KEY_2,
    YETTY_YRICH_KEY_3,
    YETTY_YRICH_KEY_4,
    YETTY_YRICH_KEY_5,
    YETTY_YRICH_KEY_6,
    YETTY_YRICH_KEY_7,
    YETTY_YRICH_KEY_8,
    YETTY_YRICH_KEY_9,
};

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YRICH_YRICH_TYPES_H */
