/*
 * grid-api.h — app-facing prototypes for the yvterm:grid figure.
 *
 * The terminal drives the grid figure through this object-keyed public API
 * (every entry takes the grid's `struct yetty_yclass_object *`). The yclass
 * identity surface (class_get / create / register / from / to) lives in the
 * generated `grid.h`; this header carries only the hand-written terminal-facing
 * operations that grid.c implements and yterminal/terminal.c consumes.
 *
 * Signatures mirror the definitions in src/yetty/yvterm/grid.c exactly.
 */
#ifndef YETTY_YVTERM_GRID_API_H
#define YETTY_YVTERM_GRID_API_H

#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yterminal/terminal.h>

struct yetty_context;
struct yetty_yfigure_figure;
struct yetty_ywire_wire_statemachine;

/* Invoked when the grid clears its screen, so the owning terminal can react
 * (e.g. drop pinned figures). Returns void-Result; receives the userdata
 * registered with set_clear_hook. */
typedef struct yetty_ycore_void_result (*yetty_yvterm_grid_clear_hook_fn)(void *userdata);

/* Construct a grid figure with its libvterm text grid + ydraw canvas wired to
 * the terminal's pty-write / request-render / mouse-subscribe callbacks. */
struct yetty_yclass_object_ptr_result yetty_yvterm_grid_figure_create(
    uint32_t cols, uint32_t rows, const struct yetty_context *context,
    yetty_yterminal_pty_write_fn pty_write_fn, void *pty_write_userdata,
    yetty_yterminal_request_render_fn request_render_fn, void *request_render_userdata,
    yetty_yterminal_mouse_sub_fn mouse_sub_fn, void *mouse_sub_userdata);

/* The grid's yfigure base (for seating it in the terminal's container). */
struct yetty_yfigure_figure *yetty_yvterm_grid_as_figure(struct yetty_yclass_object *obj);

/* Current cell metrics in pixels. */
struct yetty_ycore_pixel_size yetty_yvterm_grid_cell_size(struct yetty_yclass_object *obj);

/* Input routing — return non-zero when the grid consumed the event. */
int yetty_yvterm_grid_on_key(struct yetty_yclass_object *obj, int key, int mods);
int yetty_yvterm_grid_on_char(struct yetty_yclass_object *obj, uint32_t codepoint, int mods);

/* Geometry / view state. */
struct yetty_ycore_void_result yetty_yvterm_grid_resize(struct yetty_yclass_object *obj,
                                                        struct yetty_ycore_grid_size grid_size,
                                                        struct yetty_ycore_pixel_size cell_size);
struct yetty_ycore_void_result yetty_yvterm_grid_set_view_top(struct yetty_yclass_object *obj,
                                                              int active,
                                                              uint32_t view_top_total_idx);
struct yetty_ycore_void_result yetty_yvterm_grid_set_visual_zoom(struct yetty_yclass_object *obj,
                                                                 float scale, float offset_x,
                                                                 float offset_y);

/* Dirty flag for the render scheduler. */
int yetty_yvterm_grid_is_dirty(struct yetty_yclass_object *obj);

/* Bind a wire state machine so the grid receives compositor records. */
struct yetty_ycore_void_result yetty_yvterm_grid_register_wire(
    struct yetty_yclass_object *obj, struct yetty_ywire_wire_statemachine *sm);

/* Content inset (the padding the chrome reserves around the text area). */
void yetty_yvterm_grid_get_content_inset(struct yetty_yclass_object *obj, float *out_top,
                                         float *out_right, float *out_bottom, float *out_left);
void yetty_yvterm_grid_set_content_inset(struct yetty_yclass_object *obj, float top, float right,
                                         float bottom, float left);

/* Scrollback anchors. */
uint32_t yetty_yvterm_grid_get_live_anchor(struct yetty_yclass_object *obj);
uint32_t yetty_yvterm_grid_get_scrollback_floor(struct yetty_yclass_object *obj);

/* Selection. */
struct yetty_ycore_void_result yetty_yvterm_grid_get_selection_text(struct yetty_yclass_object *obj,
                                                                    struct yetty_ycore_buffer *out);
struct yetty_ycore_void_result yetty_yvterm_grid_set_selection(struct yetty_yclass_object *obj,
                                                               int active, uint32_t anchor_row,
                                                               uint32_t anchor_col,
                                                               uint32_t head_row,
                                                               uint32_t head_col);

/* Clear-screen notification hook. */
void yetty_yvterm_grid_set_clear_hook(struct yetty_yclass_object *obj,
                                      yetty_yvterm_grid_clear_hook_fn fn, void *userdata);

#endif /* YETTY_YVTERM_GRID_API_H */
