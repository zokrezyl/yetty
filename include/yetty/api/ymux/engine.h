/* GENERATED — do not edit. */
/* Object API for regular class(es) `engine` (implementation module: ymux).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YMUX_ENGINE_H
#define YETTY_YCLASSGEN_API_YMUX_ENGINE_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ycore_buffer;
struct yetty_ymux_cell;

typedef struct yetty_ycore_void_result (*yetty_ymux_engine_bell_fn)(void *);
typedef struct yetty_ycore_void_result (*yetty_ymux_engine_clipboard_fn)(const char *, size_t, int,
                                                                         void *);
typedef struct yetty_ycore_void_result (*yetty_ymux_engine_output_fn)(const char *, size_t, void *);
typedef struct yetty_ycore_void_result (*yetty_ymux_engine_rich_fn)(uint32_t, const char *, size_t,
                                                                    uint64_t, uint32_t, void *);
typedef struct yetty_ycore_void_result (*yetty_ymux_engine_scroll_out_fn)(
    const struct yetty_ymux_cell *, uint32_t, uint64_t, uint32_t, int, void *);
typedef struct yetty_ycore_void_result (*yetty_ymux_engine_title_fn)(const char *, size_t, void *);

#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YMUX_CELL_LIMITS
#define YETTY_YCLASSGEN_TYPE_YETTY_YMUX_CELL_LIMITS
/* Combining marks one cell carries beyond its base codepoint. Locked to
 * libvterm's per-cell cluster capacity by the static assert below. */
enum yetty_ymux_cell_limits {
    YETTY_YMUX_CELL_MAX_MARKS = 5,
};
#endif
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YMUX_CELL_ATTR
#define YETTY_YCLASSGEN_TYPE_YETTY_YMUX_CELL_ATTR
enum yetty_ymux_cell_attr {
    YETTY_YMUX_ATTR_BOLD = 1,
    YETTY_YMUX_ATTR_UNDERLINE = 2,
    YETTY_YMUX_ATTR_UNDERLINE2 = 4,
    YETTY_YMUX_ATTR_ITALIC = 8,
    YETTY_YMUX_ATTR_REVERSE = 16,
    YETTY_YMUX_ATTR_BLINK = 32,
    YETTY_YMUX_ATTR_STRIKE = 64,
    YETTY_YMUX_ATTR_CONCEAL = 128,
    YETTY_YMUX_ATTR_EXOTIC = 256,
    YETTY_YMUX_ATTR_DIM = 512,
    YETTY_YMUX_ATTR_FG_RGB_INTENT = 1024,
    YETTY_YMUX_ATTR_BG_RGB_INTENT = 2048,
    YETTY_YMUX_ATTR_OVERLINE = 4096,
};
#endif
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YMUX_CELL
#define YETTY_YCLASSGEN_TYPE_YETTY_YMUX_CELL
/* One semantic terminal cell. Colors are packed 0xAABBGGRR (alpha forced
 * opaque). width: 1 normal, 2 wide-glyph head, 0 wide-glyph spill. Never
 * carries renderer-derived state (no glyph indices, no atlas locations). */
struct yetty_ymux_cell {
    uint32_t codepoint;
    uint32_t fg;
    uint32_t bg;
    uint16_t attrs;
    uint8_t width;
    uint8_t mark_count;
    uint8_t underline_style;
    uint32_t exotic_ref;
    uint32_t marks[5];
};
#endif
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YMUX_ENGINE_HOST
#define YETTY_YCLASSGEN_TYPE_YETTY_YMUX_ENGINE_HOST
/* Narrow host table — the engine's ONLY upward channel. NULL members are
 * skipped. Copied by value at make. */
struct yetty_ymux_engine_host {
    yetty_ymux_engine_output_fn output;
    yetty_ymux_engine_clipboard_fn clipboard;
    yetty_ymux_engine_bell_fn bell;
    yetty_ymux_engine_title_fn title;
    yetty_ymux_engine_scroll_out_fn scroll_out;
    yetty_ymux_engine_rich_fn rich;
    void *userdata;
};
#endif
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YMUX_KEY
#define YETTY_YCLASSGEN_TYPE_YETTY_YMUX_KEY
/* Special keys for structured input; the engine's libvterm encodes them
 * according to the canonical terminal modes. */
enum yetty_ymux_key {
    YETTY_YMUX_KEY_ENTER = 1,
    YETTY_YMUX_KEY_TAB = 2,
    YETTY_YMUX_KEY_BACKSPACE = 3,
    YETTY_YMUX_KEY_ESCAPE = 4,
    YETTY_YMUX_KEY_UP = 5,
    YETTY_YMUX_KEY_DOWN = 6,
    YETTY_YMUX_KEY_LEFT = 7,
    YETTY_YMUX_KEY_RIGHT = 8,
    YETTY_YMUX_KEY_INSERT = 9,
    YETTY_YMUX_KEY_DELETE = 10,
    YETTY_YMUX_KEY_HOME = 11,
    YETTY_YMUX_KEY_END = 12,
    YETTY_YMUX_KEY_PAGE_UP = 13,
    YETTY_YMUX_KEY_PAGE_DOWN = 14,
    YETTY_YMUX_KEY_KP_0 = 15,
    YETTY_YMUX_KEY_KP_1 = 16,
    YETTY_YMUX_KEY_KP_2 = 17,
    YETTY_YMUX_KEY_KP_3 = 18,
    YETTY_YMUX_KEY_KP_4 = 19,
    YETTY_YMUX_KEY_KP_5 = 20,
    YETTY_YMUX_KEY_KP_6 = 21,
    YETTY_YMUX_KEY_KP_7 = 22,
    YETTY_YMUX_KEY_KP_8 = 23,
    YETTY_YMUX_KEY_KP_9 = 24,
    YETTY_YMUX_KEY_KP_MULT = 25,
    YETTY_YMUX_KEY_KP_PLUS = 26,
    YETTY_YMUX_KEY_KP_COMMA = 27,
    YETTY_YMUX_KEY_KP_MINUS = 28,
    YETTY_YMUX_KEY_KP_PERIOD = 29,
    YETTY_YMUX_KEY_KP_DIVIDE = 30,
    YETTY_YMUX_KEY_KP_ENTER = 31,
    YETTY_YMUX_KEY_KP_EQUAL = 32,
    YETTY_YMUX_KEY_F1 = 33,
    YETTY_YMUX_KEY_F2 = 34,
    YETTY_YMUX_KEY_F3 = 35,
    YETTY_YMUX_KEY_F4 = 36,
    YETTY_YMUX_KEY_F5 = 37,
    YETTY_YMUX_KEY_F6 = 38,
    YETTY_YMUX_KEY_F7 = 39,
    YETTY_YMUX_KEY_F8 = 40,
    YETTY_YMUX_KEY_F9 = 41,
    YETTY_YMUX_KEY_F10 = 42,
    YETTY_YMUX_KEY_F11 = 43,
    YETTY_YMUX_KEY_F12 = 44,
};
#endif
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YMUX_MOD
#define YETTY_YCLASSGEN_TYPE_YETTY_YMUX_MOD
enum yetty_ymux_mod {
    YETTY_YMUX_MOD_SHIFT = 1,
    YETTY_YMUX_MOD_CTRL = 2,
    YETTY_YMUX_MOD_ALT = 4,
};
#endif
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YMUX_CELL_CONST_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YMUX_CELL_CONST_PTR_RESULT
struct yetty_ymux_cell_const_ptr_result {
    int ok;
    union {
        const struct yetty_ymux_cell *value;
        struct yetty_ycore_error error;
    };
};
#endif

/* The engine — the yclass data block. */
struct yetty_yclass_ptr_result yetty_ymux_engine_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ymux_engine;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YMUX_ENGINE_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YMUX_ENGINE_PTR_RESULT
struct yetty_ymux_engine_ptr_result {
    int ok;
    union {
        struct yetty_ymux_engine *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ymux_engine_ptr_result yetty_ymux_engine_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ymux_engine_to(struct yetty_ymux_engine *data);

struct yetty_yclass_object_ptr_result yetty_ymux_engine_create(struct yetty_yclass_ctx *ctx);

struct yetty_yclass_object_ptr_result yetty_ymux_engine_make(
    uint32_t rows, uint32_t cols, const struct yetty_ymux_engine_host *host);
struct yetty_ycore_void_result yetty_ymux_engine_dispose(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ymux_engine_feed(struct yetty_yclass_object *obj,
                                                      const char *bytes, size_t len);
/* The client reports its cell pixel height so a captured rich figure reserves
 * the right number of terminal rows (a figure's pixel height only maps to rows
 * once the cell height is known — the same reason the local terminal computes
 * this receiver-side, not in the producer). */
struct yetty_ycore_void_result yetty_ymux_engine_set_cell_height(struct yetty_yclass_object *obj,
                                                                 uint32_t cell_pixel_height);
/* Reserve terminal rows for a rich figure of the given pixel height. The rows
 * are fed as newlines on the next engine_feed drain (advancing the cursor past
 * the figure, so the next prompt lands below it and the figure scrolls with the
 * text). Returns the reserved row count = the figure's span_rows. */
struct yetty_ycore_uint32_result yetty_ymux_engine_reserve_rich_rows(
    struct yetty_yclass_object *obj, uint32_t figure_pixel_height);
struct yetty_ycore_void_result yetty_ymux_engine_resize(struct yetty_yclass_object *obj,
                                                        uint32_t rows, uint32_t cols);
struct yetty_ycore_void_result yetty_ymux_engine_input_char(struct yetty_yclass_object *obj,
                                                            uint32_t codepoint, int mods);
struct yetty_ycore_void_result yetty_ymux_engine_input_key(struct yetty_yclass_object *obj,
                                                           enum yetty_ymux_key key, int mods);
struct yetty_ycore_void_result yetty_ymux_engine_input_mouse_move(struct yetty_yclass_object *obj,
                                                                  uint32_t row, uint32_t col,
                                                                  int mods);
struct yetty_ycore_void_result yetty_ymux_engine_input_mouse_button(struct yetty_yclass_object *obj,
                                                                    int button, int pressed,
                                                                    int mods);
struct yetty_ycore_void_result yetty_ymux_engine_input_paste(struct yetty_yclass_object *obj,
                                                             const char *text, size_t len);
struct yetty_ycore_void_result yetty_ymux_engine_set_palette_color(struct yetty_yclass_object *obj,
                                                                   int index, uint32_t rgb);
/* The live default colors (0xAABBGGRR — OSC 10/11 can move them). */
struct yetty_ycore_void_result yetty_ymux_engine_default_colors(struct yetty_yclass_object *obj,
                                                                uint32_t *out_fg, uint32_t *out_bg);
/* One base-16 palette entry, packed 0xAABBGGRR. */
struct yetty_ycore_uint32_result yetty_ymux_engine_palette_color(struct yetty_yclass_object *obj,
                                                                 int index);
struct yetty_ycore_void_result yetty_ymux_engine_set_default_colors(struct yetty_yclass_object *obj,
                                                                    uint32_t fg_rgb,
                                                                    uint32_t bg_rgb);
struct yetty_ycore_void_result yetty_ymux_engine_dims(struct yetty_yclass_object *obj,
                                                      uint32_t *out_rows, uint32_t *out_cols);
struct yetty_ycore_int_result yetty_ymux_engine_alt_active(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ymux_engine_cursor(struct yetty_yclass_object *obj,
                                                        uint32_t *out_row, uint32_t *out_col,
                                                        int *out_visible);
/* Cursor STYLE (shape + blink) for the VT projection's DECSCUSR emission —
 * shape is a VTERM_PROP_CURSORSHAPE_* value (1 block, 2 underline, 3 bar). */
struct yetty_ycore_void_result yetty_ymux_engine_cursor_style(struct yetty_yclass_object *obj,
                                                              int *out_shape, int *out_blink);
/* Exotic-value table accessors (review #17): the projector replays the
 * VERBATIM interned values on enabled capability profiles. Returns the
 * table count; copies the ref's token/URI when in range. */
struct yetty_ycore_uint32_result yetty_ymux_engine_exotic_colour(struct yetty_yclass_object *obj,
                                                                 uint32_t ref, char *out_text,
                                                                 uint32_t out_capacity);
struct yetty_ycore_uint32_result yetty_ymux_engine_exotic_link(struct yetty_yclass_object *obj,
                                                               uint32_t ref, char *out_text,
                                                               uint32_t out_capacity);
struct yetty_ymux_cell_const_ptr_result yetty_ymux_engine_row_cells(struct yetty_yclass_object *obj,
                                                                    uint32_t row);
/* Stable identity of one ACTIVE-screen row. */
struct yetty_ycore_void_result yetty_ymux_engine_row_identity(struct yetty_yclass_object *obj,
                                                              uint32_t row,
                                                              uint64_t *out_logical_line_id,
                                                              uint32_t *out_logical_cell_start,
                                                              int *out_continuation);
struct yetty_ycore_void_result yetty_ymux_engine_snapshot(struct yetty_yclass_object *obj,
                                                          struct yetty_ycore_buffer *out);

#ifdef __cplusplus
}
#endif

#endif
