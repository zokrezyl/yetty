/* GENERATED — do not edit. */
/* Public interface for regular class(es) `vterm` (module: yvterm).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YVTERM_VTERM_H
#define YETTY_YCLASSGEN_YVTERM_VTERM_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yterminal/terminal.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_context;
struct yetty_ycore_buffer;
struct yetty_ycore_grid_size;
struct yetty_ycore_pixel_size;
struct yetty_ydraw_composite;
struct yetty_ywire_wire_statemachine;

typedef struct yetty_ycore_void_result (*yetty_yvterm_clear_hook_fn)(void *);

struct yetty_yclass_ptr_result yetty_yvterm_vterm_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yvterm_vterm;
struct yetty_yvterm_vterm_ptr_result {
    int ok;
    union {
        struct yetty_yvterm_vterm *value;
        struct yetty_ycore_error error;
    };
};
struct yetty_yvterm_vterm_ptr_result yetty_yvterm_vterm_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yvterm_vterm_to(struct yetty_yvterm_vterm *data);

struct yetty_yclass_object_ptr_result yetty_yvterm_vterm_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_yvterm_register(void);

/* Create the terminal-content figure for one pane. The rect is set in pixels;
 * cell_size is stored so the terminal can read it immediately after create. The
 * terminal hooks (pty write, render request, mouse subscription) are stored for
 * keyboard output and mouse-mode reporting.
 *
 * cell_size starts at a placeholder until the renderer build-out resolves exact
 * metrics from the active font; the terminal overwrites it on the first resize.
 * `context` is accepted now for that future font setup. */
struct yetty_yclass_object_ptr_result yetty_yvterm_vterm_figure_create(uint32_t cols, uint32_t rows, const struct yetty_context *context, yetty_yterminal_pty_write_fn pty_write_fn, void *pty_write_userdata, yetty_yterminal_request_render_fn request_render_fn, void *request_render_userdata, yetty_yterminal_mouse_sub_fn mouse_sub_fn, void *mouse_sub_userdata);
/* Upcast to the figure base (first slice in the object). */
struct yetty_yfigure_figure_ptr_result yetty_yvterm_vterm_as_figure(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yvterm_vterm_feed(struct yetty_yclass_object *obj, const char *bytes, size_t len);
struct yetty_ycore_void_result yetty_yvterm_vterm_resize(struct yetty_yclass_object *obj, struct yetty_ycore_grid_size grid_size, struct yetty_ycore_pixel_size cell_size);
struct pixel_size_result yetty_yvterm_vterm_cell_size(struct yetty_yclass_object *obj);
struct yetty_ycore_int_result yetty_yvterm_vterm_is_dirty(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yvterm_vterm_set_content_inset(struct yetty_yclass_object *obj, float top, float right, float bottom, float left);
struct yetty_ycore_void_result yetty_yvterm_vterm_get_content_inset(struct yetty_yclass_object *obj, float *out_top, float *out_right, float *out_bottom, float *out_left);
struct yetty_ycore_void_result yetty_yvterm_vterm_set_clear_hook(struct yetty_yclass_object *obj, yetty_yvterm_clear_hook_fn fn, void *userdata);
struct yetty_ycore_void_result yetty_yvterm_vterm_cursor(struct yetty_yclass_object *obj, uint32_t *out_row, uint32_t *out_col, uint32_t *out_visible);
struct yetty_ycore_void_result yetty_yvterm_vterm_word_bounds(struct yetty_yclass_object *obj, uint32_t row, uint32_t col, uint32_t *out_start_col, uint32_t *out_end_col);
struct yetty_ycore_uint32_result yetty_yvterm_vterm_scroll_origin(struct yetty_yclass_object *obj);
struct yetty_ycore_uint32_result yetty_yvterm_vterm_append_primitive(struct yetty_yclass_object *obj, uint32_t row, const uint32_t *words, uint32_t word_count);
struct yetty_ycore_uint32_result yetty_yvterm_vterm_attach_composite(struct yetty_yclass_object *obj, uint32_t row, struct yetty_ydraw_composite *composite);
struct yetty_ycore_void_result yetty_yvterm_vterm_relocate_rich_to_bottom(struct yetty_yclass_object *obj, uint32_t span_rows);
struct yetty_ycore_void_result yetty_yvterm_vterm_clear_rich_line(struct yetty_yclass_object *obj, uint32_t row);
struct yetty_ycore_void_result yetty_yvterm_vterm_clear_rich_all(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yvterm_vterm_register_wire(struct yetty_yclass_object *obj, struct yetty_ywire_wire_statemachine *sm);
struct yetty_ycore_int_result yetty_yvterm_vterm_on_char(struct yetty_yclass_object *obj, uint32_t codepoint, int mods);
struct yetty_ycore_int_result yetty_yvterm_vterm_on_key(struct yetty_yclass_object *obj, int key, int mods);
struct yetty_ycore_void_result yetty_yvterm_vterm_set_selection(struct yetty_yclass_object *obj, int active, uint32_t anchor_row, uint32_t anchor_col, uint32_t head_row, uint32_t head_col);
struct yetty_ycore_void_result yetty_yvterm_vterm_get_selection_text(struct yetty_yclass_object *obj, struct yetty_ycore_buffer *out);
/* Absolute index of the line at the live screen top: everything scrolled off so
 * far. A wheel-up anchors one line below this and walks toward the floor. */
struct yetty_ycore_uint32_result yetty_yvterm_vterm_get_live_anchor(struct yetty_yclass_object *obj);
/* Oldest absolute line index still retained in the scrollback ring. Lines below
 * this have been evicted, so a wheel-up clamps here. The ring keeps
 * (slot_count - visible_rows) history lines. */
struct yetty_ycore_uint32_result yetty_yvterm_vterm_get_scrollback_floor(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yvterm_vterm_set_view_top(struct yetty_yclass_object *obj, int active, uint32_t view_top_total_idx);
struct yetty_ycore_void_result yetty_yvterm_vterm_set_visual_zoom(struct yetty_yclass_object *obj, float scale, float offset_x, float offset_y);
/* Set the OSC-driven post-color effect for the terminal text surface. index 0
 * disables it; params are the 6 effect parameters (unused ones may be 0). */
struct yetty_ycore_void_result yetty_yvterm_vterm_set_post_effect(struct yetty_yclass_object *obj, uint32_t index, float p0, float p1, float p2, float p3, float p4, float p5);
/* Set the OSC-driven coordinate distortion for the terminal text surface.
 * index 0 disables it; p0/p1 are strength and radius/aspect. */
struct yetty_ycore_void_result yetty_yvterm_vterm_set_coord_effect(struct yetty_yclass_object *obj, uint32_t index, float p0, float p1, float p2, float p3, float p4, float p5);
/* Report the live pointer position (pane-local pixels) so mouse-following
 * effects track it. Only forces a repaint while a coordinate effect is active
 * — otherwise pointer motion must not drive frames. */
struct yetty_ycore_void_result yetty_yvterm_vterm_set_mouse(struct yetty_yclass_object *obj, float x, float y);

#ifdef __cplusplus
}
#endif

#endif
