/* GENERATED — do not edit. */
/* Object API for regular class(es) `figure` (implementation module: yfigure).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YFIGURE_FIGURE_H
#define YETTY_YCLASSGEN_API_YFIGURE_FIGURE_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ydraw_target;
struct yetty_ywire_wire_statemachine;

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yfigure_figure;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YFIGURE_FIGURE_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YFIGURE_FIGURE_PTR_RESULT
struct yetty_yfigure_figure_ptr_result {
    int ok;
    union {
        struct yetty_yfigure_figure *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_yfigure_figure_ptr_result yetty_yfigure_figure_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yfigure_figure_to(struct yetty_yfigure_figure *data);
struct rectangle_result yetty_yfigure_figure_rect_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yfigure_figure_rect_set(struct yetty_yclass_object *obj,
                                                             struct yetty_ycore_rectangle value);
struct yetty_ycore_int_result yetty_yfigure_figure_z_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yfigure_figure_z_set(struct yetty_yclass_object *obj,
                                                          int value);
struct yetty_ycore_int_result yetty_yfigure_figure_hidden_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yfigure_figure_hidden_set(struct yetty_yclass_object *obj,
                                                               int value);
struct yetty_ycore_int_result yetty_yfigure_figure_dirty_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yfigure_figure_dirty_set(struct yetty_yclass_object *obj,
                                                              int value);
struct yetty_ycore_int_result yetty_yfigure_figure_absolute_coords_get(
    struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yfigure_figure_absolute_coords_set(
    struct yetty_yclass_object *obj, int value);

/* destroy: tears down concrete state and frees the figure. The base
 * default is a no-op — the base class is never instantiated as a leaf;
 * every concrete kind overrides this and frees via object_free. */
struct yetty_ycore_void_result yetty_yfigure_destroy(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yfigure_render(struct yetty_yclass_object *obj,
                                                    struct yetty_ydraw_target *target);
/* dump_state: heap text snapshot for tests. Base default yields a NULL
 * string so the yetty_yfigure_dump wrapper emits its rect fallback. */
struct yetty_ycore_char_ptr_result yetty_yfigure_dump_state(struct yetty_yclass_object *obj,
                                                            int indent);
/* process_input: consume the figure's wire body straight off the SM.
 * Base default rejects — a purely visual figure ignores wire updates.
 * The container only routes here for kinds that override it (capability
 * detected via yetty_yfigure_figure_implements). */
struct yetty_ycore_void_result yetty_yfigure_process_input(
    struct yetty_yclass_object *obj, struct yetty_ywire_wire_statemachine *statemachine);
/* process_bytes: apply a buffered wire body. Base default rejects. */
struct yetty_ycore_void_result yetty_yfigure_process_bytes(struct yetty_yclass_object *obj,
                                                           const uint8_t *bytes, size_t bytes_len);
/* reset_content: drop content, keep GPU state. Base default rejects so the
 * container falls back to destroy + mint for kinds that don't support it. */
struct yetty_ycore_void_result yetty_yfigure_reset_content(struct yetty_yclass_object *obj);
/* set_scroll: move the figure's scroll offset (the content coordinate shown
 * at the rect's top-left). Base default rejects — a non-scrolling figure has
 * no content larger than its rect. Scrollable kinds (ygrid) override this so
 * the container can drive the offset by id from a wire SET_CHILD_SCROLL
 * record or the terminal's autonomous wheel/key handler, without re-shipping
 * content. */
struct yetty_ycore_void_result yetty_yfigure_set_scroll(struct yetty_yclass_object *obj,
                                                        float scroll_x, float scroll_y);
/* set_content_size: declare the figure's content extent in px (the rect stays
 * the visible window; content may be larger, making the figure a scroll
 * viewport). Base default rejects. Scrollable kinds override it. */
struct yetty_ycore_void_result yetty_yfigure_set_content_size(struct yetty_yclass_object *obj,
                                                              float content_w, float content_h);
/* apply_scroll_anchor: re-anchor a scroll-anchored figure's rendered content
 * to the terminal's current scroll position. `rolling_row_offset` is the
 * number of content rows the view has advanced since the figure was created
 * (content_root_row - creation_row); `cell_height` is the terminal cell
 * height in px, so the pixel shift is offset*cell_height. A ygrid-backed card
 * overrides this to slide its absolute-coord content by that many pixels so it
 * tracks the surrounding text. Base default is a no-op: a figure that draws
 * nothing content-relative needs no anchoring beyond its rect. */
struct yetty_ycore_void_result yetty_yfigure_apply_scroll_anchor(struct yetty_yclass_object *obj,
                                                                 int32_t rolling_row_offset,
                                                                 float cell_height);

struct yetty_yclass_object_ptr_result yetty_yfigure_figure_create(struct yetty_yclass_ctx *ctx);

#ifdef __cplusplus
}
#endif

#endif
