/* GENERATED — do not edit. */
/* Public interface for regular class(es) `vterm` (module: yvterm).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), and any
 * `expose`d API. Public types come from `expose` annotations. */
#ifndef YETTY_YCLASSGEN_YVTERM_VTERM_H
#define YETTY_YCLASSGEN_YVTERM_VTERM_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yetty/yetty.h>
#include <yetty/yfigure/figure.h>
#include <yetty/yterminal/terminal.h>

struct yetty_ydraw_composite;

struct yetty_yvterm_vterm;

typedef struct yetty_ycore_void_result (*yetty_yvterm_clear_hook_fn)(void *);

struct yetty_yclass_ptr_result yetty_yvterm_vterm_class_get(void);

/* Data-block accessors. The data struct stays private (only a
 * forward declaration crosses into the header); reach members
 * through the per-property getters/setters below. */
struct yetty_yvterm_vterm_ptr_result yetty_yvterm_vterm_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object *yetty_yvterm_vterm_to(struct yetty_yvterm_vterm *data);

struct yetty_yclass_object_ptr_result yetty_yvterm_vterm_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_yvterm_register(void);

struct yetty_yclass_object_ptr_result yetty_yvterm_vterm_figure_create(
    uint32_t cols, uint32_t rows, const struct yetty_context *context,
    yetty_yterminal_pty_write_fn pty_write_fn, void *pty_write_userdata,
    yetty_yterminal_request_render_fn request_render_fn, void *request_render_userdata,
    yetty_yterminal_mouse_sub_fn mouse_sub_fn, void *mouse_sub_userdata);
struct yetty_yfigure_figure *yetty_yvterm_vterm_as_figure(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yvterm_vterm_feed(struct yetty_yclass_object *obj,
                                                       const char *bytes, size_t len);
struct yetty_ycore_void_result yetty_yvterm_vterm_resize(struct yetty_yclass_object *obj,
                                                         struct yetty_ycore_grid_size grid_size,
                                                         struct yetty_ycore_pixel_size cell_size);
struct yetty_ycore_pixel_size yetty_yvterm_vterm_cell_size(struct yetty_yclass_object *obj);
int yetty_yvterm_vterm_is_dirty(struct yetty_yclass_object *obj);
void yetty_yvterm_vterm_set_content_inset(struct yetty_yclass_object *obj, float top, float right,
                                          float bottom, float left);
void yetty_yvterm_vterm_get_content_inset(struct yetty_yclass_object *obj, float *out_top,
                                          float *out_right, float *out_bottom, float *out_left);
void yetty_yvterm_vterm_set_clear_hook(struct yetty_yclass_object *obj,
                                       yetty_yvterm_clear_hook_fn fn, void *userdata);
void yetty_yvterm_vterm_cursor(struct yetty_yclass_object *obj, uint32_t *out_row,
                               uint32_t *out_col, uint32_t *out_visible);
void yetty_yvterm_vterm_word_bounds(struct yetty_yclass_object *obj, uint32_t row, uint32_t col,
                                    uint32_t *out_start_col, uint32_t *out_end_col);
uint32_t yetty_yvterm_vterm_scroll_origin(struct yetty_yclass_object *obj);
struct yetty_ycore_uint32_result yetty_yvterm_vterm_append_primitive(
    struct yetty_yclass_object *obj, uint32_t row, const uint32_t *words, uint32_t word_count);
struct yetty_ycore_void_result yetty_yvterm_vterm_add_primitive_ref(struct yetty_yclass_object *obj,
                                                                    uint32_t row, uint32_t col,
                                                                    uint16_t rel_line,
                                                                    uint16_t index_in_list);
struct yetty_ycore_uint32_result yetty_yvterm_vterm_attach_composite(
    struct yetty_yclass_object *obj, uint32_t row, struct yetty_ydraw_composite *composite);
struct yetty_ycore_void_result yetty_yvterm_vterm_add_composite_ref(struct yetty_yclass_object *obj,
                                                                    uint32_t row, uint32_t col,
                                                                    uint16_t rel_line,
                                                                    uint16_t index_in_list);
struct yetty_ycore_void_result yetty_yvterm_vterm_clear_rich_line(struct yetty_yclass_object *obj,
                                                                  uint32_t row);
struct yetty_ycore_void_result yetty_yvterm_vterm_clear_rich_all(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yvterm_vterm_register_wire(
    struct yetty_yclass_object *obj, struct yetty_ywire_wire_statemachine *sm);
int yetty_yvterm_vterm_on_char(struct yetty_yclass_object *obj, uint32_t codepoint, int mods);
int yetty_yvterm_vterm_on_key(struct yetty_yclass_object *obj, int key, int mods);
struct yetty_ycore_void_result yetty_yvterm_vterm_set_selection(struct yetty_yclass_object *obj,
                                                                int active, uint32_t anchor_row,
                                                                uint32_t anchor_col,
                                                                uint32_t head_row,
                                                                uint32_t head_col);
struct yetty_ycore_void_result yetty_yvterm_vterm_get_selection_text(
    struct yetty_yclass_object *obj, struct yetty_ycore_buffer *out);
uint32_t yetty_yvterm_vterm_get_live_anchor(struct yetty_yclass_object *obj);
uint32_t yetty_yvterm_vterm_get_scrollback_floor(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yvterm_vterm_set_view_top(struct yetty_yclass_object *obj,
                                                               int active,
                                                               uint32_t view_top_total_idx);
struct yetty_ycore_void_result yetty_yvterm_vterm_set_visual_zoom(struct yetty_yclass_object *obj,
                                                                  float scale, float offset_x,
                                                                  float offset_y);

#endif
