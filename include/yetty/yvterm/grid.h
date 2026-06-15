/* GENERATED — do not edit. */
/* Public interface for regular class(es) `grid` (module: yvterm).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), and any
 * `expose`d API. Public types come from `expose` annotations. */
#ifndef YETTY_YCLASSGEN_YVTERM_GRID_H
#define YETTY_YCLASSGEN_YVTERM_GRID_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yfigure/figure.h>
#include <yetty/yvterm/vterm.h>

struct yetty_yvterm_grid;

typedef struct yetty_ycore_void_result (*yetty_yvterm_grid_clear_hook_fn)(void *);

struct yetty_yvterm_grid_ptr_result {
    int ok;
    union {
        struct yetty_yvterm_grid *value;
        struct yetty_ycore_error error;
    };
};

typedef struct yetty_ycore_void_result (*yetty_yvterm_grid_pty_write_fn)(const char *, size_t,
                                                                         void *);

enum yetty_yvterm_text_attr {
    YETTY_YVTERM_ATTR_BOLD = 1,
    YETTY_YVTERM_ATTR_UNDERLINE = 2,
    YETTY_YVTERM_ATTR_UNDERLINE2 = 4,
    YETTY_YVTERM_ATTR_ITALIC = 8,
    YETTY_YVTERM_ATTR_REVERSE = 16,
    YETTY_YVTERM_ATTR_BLINK = 32,
    YETTY_YVTERM_ATTR_STRIKE = 64,
    YETTY_YVTERM_ATTR_CONCEAL = 128,
};

struct yetty_yvterm_text_cell {
    uint32_t glyph_index;
    uint32_t codepoint;
    uint32_t fg;
    uint32_t bg;
    uint16_t attrs;
    uint8_t width;
    uint8_t flags;
};

struct yetty_yclass_ptr_result yetty_yvterm_grid_class_get(void);

/* Data-block accessors. The data struct stays private (only a
 * forward declaration crosses into the header); reach members
 * through the per-property getters/setters below. */
struct yetty_yvterm_grid_ptr_result yetty_yvterm_grid_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object *yetty_yvterm_grid_to(struct yetty_yvterm_grid *data);

struct yetty_yclass_object_ptr_result yetty_yvterm_grid_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_yvterm_register(void);

struct yetty_yclass_object_ptr_result yetty_yvterm_grid_make(uint32_t cols, uint32_t rows);
struct yetty_ycore_void_result yetty_yvterm_grid_dispose(struct yetty_yclass_object *obj);
void yetty_yvterm_grid_set_pty_write(struct yetty_yclass_object *obj,
                                     yetty_yvterm_grid_pty_write_fn fn, void *userdata);
void yetty_yvterm_grid_set_clear_hook(struct yetty_yclass_object *obj,
                                      yetty_yvterm_grid_clear_hook_fn fn, void *userdata);
struct yetty_ycore_void_result yetty_yvterm_grid_feed(struct yetty_yclass_object *obj,
                                                      const char *bytes, size_t len);
struct yetty_ycore_void_result yetty_yvterm_grid_resize(struct yetty_yclass_object *obj,
                                                        uint32_t cols, uint32_t rows);
int yetty_yvterm_grid_is_dirty(struct yetty_yclass_object *obj);
void yetty_yvterm_grid_cursor(struct yetty_yclass_object *obj, uint32_t *out_row, uint32_t *out_col,
                              uint32_t *out_visible);
uint32_t yetty_yvterm_grid_scroll_origin(struct yetty_yclass_object *obj);
struct yetty_ycore_uint32_result yetty_yvterm_grid_append_primitive(struct yetty_yclass_object *obj,
                                                                    uint32_t row,
                                                                    const uint32_t *words,
                                                                    uint32_t word_count);
struct yetty_ycore_void_result yetty_yvterm_grid_add_primitive_ref(struct yetty_yclass_object *obj,
                                                                   uint32_t row, uint32_t col,
                                                                   uint16_t rel_line,
                                                                   uint16_t index_in_list);
struct yetty_ycore_uint32_result yetty_yvterm_grid_attach_composite(
    struct yetty_yclass_object *obj, uint32_t row, struct yetty_ydraw_composite *composite);
struct yetty_ycore_void_result yetty_yvterm_grid_add_composite_ref(struct yetty_yclass_object *obj,
                                                                   uint32_t row, uint32_t col,
                                                                   uint16_t rel_line,
                                                                   uint16_t index_in_list);
struct yetty_ycore_void_result yetty_yvterm_grid_clear_rich_line(struct yetty_yclass_object *obj,
                                                                 uint32_t row);
struct yetty_ycore_void_result yetty_yvterm_grid_clear_rich_all(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yvterm_grid_register_wire(
    struct yetty_yclass_object *obj, struct yetty_ywire_wire_statemachine *sm);
int yetty_yvterm_grid_on_char(struct yetty_yclass_object *obj, uint32_t codepoint, int mods);
int yetty_yvterm_grid_on_key(struct yetty_yclass_object *obj, int key, int mods);
struct yetty_ycore_void_result yetty_yvterm_grid_set_selection(struct yetty_yclass_object *obj,
                                                               int active, uint32_t anchor_row,
                                                               uint32_t anchor_col,
                                                               uint32_t head_row,
                                                               uint32_t head_col);
struct yetty_ycore_void_result yetty_yvterm_grid_get_selection_text(struct yetty_yclass_object *obj,
                                                                    struct yetty_ycore_buffer *out);
void yetty_yvterm_grid_word_bounds(struct yetty_yclass_object *obj, uint32_t row, uint32_t col,
                                   uint32_t *out_start_col, uint32_t *out_end_col);
void yetty_yvterm_grid_dims(struct yetty_yclass_object *obj, uint32_t *out_cols, uint32_t *out_rows,
                            uint32_t *out_base);
const struct yetty_yvterm_text_cell *yetty_yvterm_grid_line_cells(struct yetty_yclass_object *obj,
                                                                  uint32_t row);
int yetty_yvterm_grid_line_dirty(struct yetty_yclass_object *obj, uint32_t row);
struct yetty_ydraw_composite *const *yetty_yvterm_grid_line_composites(
    struct yetty_yclass_object *obj, uint32_t row, uint32_t *out_count);
void yetty_yvterm_grid_selection(struct yetty_yclass_object *obj, int *out_active,
                                 uint32_t *out_anchor_row, uint32_t *out_anchor_col,
                                 uint32_t *out_head_row, uint32_t *out_head_col);
void yetty_yvterm_grid_clear_dirty(struct yetty_yclass_object *obj);
uint32_t yetty_yvterm_grid_slot_count(struct yetty_yclass_object *obj);
const struct yetty_yvterm_text_cell *yetty_yvterm_grid_slot_cells(struct yetty_yclass_object *obj,
                                                                  uint32_t slot);
int yetty_yvterm_grid_slot_dirty(struct yetty_yclass_object *obj, uint32_t slot);

#endif
