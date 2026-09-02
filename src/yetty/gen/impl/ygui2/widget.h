/* GENERATED — do not edit. */
/* Public interface for regular class(es) `widget` (module: ygui2).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YGUI2_WIDGET_H
#define YETTY_YCLASSGEN_YGUI2_WIDGET_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ydraw_drawable_list;
struct yetty_ygui2_layout;
struct yetty_ygui2_theme;

struct yetty_yclass_ptr_result yetty_ygui2_widget_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ygui2_widget;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YGUI2_WIDGET_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YGUI2_WIDGET_PTR_RESULT
struct yetty_ygui2_widget_ptr_result {
    int ok;
    union {
        struct yetty_ygui2_widget *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ygui2_widget_ptr_result yetty_ygui2_widget_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ygui2_widget_to(struct yetty_ygui2_widget *data);

struct yetty_ycore_void_result yetty_ygui2_constructor(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui2_destructor(struct yetty_yclass_object *obj);
/* Paint the widget's own primitives into `list` in WIDGET-LOCAL pixels
 * (0,0 = widget top-left; the minted group's offset places them). The base
 * paints nothing. */
struct yetty_ycore_void_result yetty_ygui2_widget_paint(struct yetty_yclass_object *obj,
                                                        struct yetty_ydraw_drawable_list *list);
/* RETAINED content (T5): emitted in the widget's CONTAINMENT group, after
 * the skin subgroup — hosted complex records whose runtime must survive
 * skin repaints, theme restyles, movement and ancestor resizes. Replaced
 * ONLY by an intentional structural reopen (set_record and friends mark
 * structure dirt). The base emits nothing. */
struct yetty_ycore_void_result yetty_ygui2_widget_paint_retained(
    struct yetty_yclass_object *obj, struct yetty_ydraw_drawable_list *list);
/* Geometry follow-up for retained content: called by the incremental emit
 * walk when the widget's SIZE changed (dirty_geometry), appending into
 * the SAME frame envelope. A widget hosting a resizable runtime (plot)
 * emits its addressed geometry op here — a few dozen bytes; the receiver
 * re-plans the runtime and its chrome locally, so the record and its
 * data are NEVER re-shipped on resize. The base emits nothing. */
struct yetty_ycore_void_result yetty_ygui2_widget_emit_geometry(
    struct yetty_yclass_object *obj, struct yetty_ydraw_drawable_list *list);
/* Pointer input (widget-local pixels). Return 1 = consumed, 0 = bubble. */
struct yetty_ycore_int_result yetty_ygui2_widget_on_press(struct yetty_yclass_object *obj,
                                                          float local_x, float local_y, int button,
                                                          int mods);
struct yetty_ycore_int_result yetty_ygui2_widget_on_release(struct yetty_yclass_object *obj,
                                                            float local_x, float local_y,
                                                            int button, int mods);
struct yetty_ycore_int_result yetty_ygui2_widget_on_motion(struct yetty_yclass_object *obj,
                                                           float local_x, float local_y,
                                                           uint32_t buttons_held);
struct yetty_ycore_int_result yetty_ygui2_widget_on_scroll(struct yetty_yclass_object *obj,
                                                           float local_x, float local_y,
                                                           float wheel_dy);
/* Keyboard input for the focused widget. Return 1 = consumed. */
struct yetty_ycore_int_result yetty_ygui2_widget_on_key(struct yetty_yclass_object *obj,
                                                        uint32_t key, uint32_t mods);
/* Subclass teardown hook: release owned heap state (record copies, owned
 * drawable lists) before the base free. The base owns nothing. */
struct yetty_ycore_void_result yetty_ygui2_widget_cleanup(struct yetty_yclass_object *obj);

typedef struct yetty_ycore_void_result (*yetty_ygui2_constructor_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ygui2_destructor_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ygui2_widget_paint_fn)(
    struct yetty_yclass_object *, struct yetty_ydraw_drawable_list *);
typedef struct yetty_ycore_void_result (*yetty_ygui2_widget_paint_retained_fn)(
    struct yetty_yclass_object *, struct yetty_ydraw_drawable_list *);
typedef struct yetty_ycore_void_result (*yetty_ygui2_widget_emit_geometry_fn)(
    struct yetty_yclass_object *, struct yetty_ydraw_drawable_list *);
typedef struct yetty_ycore_int_result (*yetty_ygui2_widget_on_press_fn)(
    struct yetty_yclass_object *, float, float, int, int);
typedef struct yetty_ycore_int_result (*yetty_ygui2_widget_on_release_fn)(
    struct yetty_yclass_object *, float, float, int, int);
typedef struct yetty_ycore_int_result (*yetty_ygui2_widget_on_motion_fn)(
    struct yetty_yclass_object *, float, float, uint32_t);
typedef struct yetty_ycore_int_result (*yetty_ygui2_widget_on_scroll_fn)(
    struct yetty_yclass_object *, float, float, float);
typedef struct yetty_ycore_int_result (*yetty_ygui2_widget_on_key_fn)(struct yetty_yclass_object *,
                                                                      uint32_t, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_ygui2_widget_cleanup_fn)(
    struct yetty_yclass_object *);

struct yetty_yclass_object_ptr_result yetty_ygui2_widget_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygui2_register(void);

struct yetty_ycore_void_result yetty_ygui2_widget_mark_skin_dirty(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui2_widget_mark_structure_dirty(
    struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui2_widget_layout_set(struct yetty_yclass_object *obj,
                                                             const struct yetty_ygui2_layout *spec);
struct yetty_ycore_void_result yetty_ygui2_widget_rect(struct yetty_yclass_object *obj,
                                                       float *out_x, float *out_y, float *out_w,
                                                       float *out_h);
struct yetty_ycore_void_result yetty_ygui2_widget_init_base(struct yetty_yclass_object *obj,
                                                            struct yetty_yclass_object *framework,
                                                            struct yetty_yclass_object *parent,
                                                            uint32_t node_id);
struct yetty_ycore_void_result yetty_ygui2_widget_link_child(struct yetty_yclass_object *parent,
                                                             struct yetty_yclass_object *child);
struct yetty_yclass_object_ptr_result yetty_ygui2_widget_first_child(
    struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ygui2_widget_next_sibling(
    struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ygui2_widget_parent_obj(
    struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ygui2_widget_framework_obj(
    struct yetty_yclass_object *obj);
struct yetty_ycore_uint32_result yetty_ygui2_widget_node_id(struct yetty_yclass_object *obj);
struct yetty_ycore_uint32_result yetty_ygui2_widget_skin_node_id(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui2_widget_set_skin_node_id(struct yetty_yclass_object *obj,
                                                                   uint32_t skin_node_id);
struct yetty_ycore_void_result yetty_ygui2_widget_set_node_id(struct yetty_yclass_object *obj,
                                                              uint32_t node_id);
struct yetty_ycore_void_result yetty_ygui2_widget_set_transparent(struct yetty_yclass_object *obj);
struct yetty_ycore_int_result yetty_ygui2_widget_is_transparent(struct yetty_yclass_object *obj);
struct yetty_ycore_int_result yetty_ygui2_widget_is_visible(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui2_widget_set_focusable(struct yetty_yclass_object *obj,
                                                                int focusable);
struct yetty_ycore_int_result yetty_ygui2_widget_is_focusable(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui2_widget_set_dismiss_on_outside(
    struct yetty_yclass_object *obj, int dismiss);
struct yetty_ycore_int_result yetty_ygui2_widget_dismiss_on_outside(
    struct yetty_yclass_object *obj);
struct yetty_ycore_int_result yetty_ygui2_widget_has_focus(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui2_widget_theme_copy(struct yetty_yclass_object *obj,
                                                             struct yetty_ygui2_theme *out_theme);
struct yetty_ycore_void_result yetty_ygui2_widget_set_rect(struct yetty_yclass_object *obj, float x,
                                                           float y, float w, float h);
struct yetty_ycore_void_result yetty_ygui2_widget_layout_copy(struct yetty_yclass_object *obj,
                                                              struct yetty_ygui2_layout *out_spec);
/* Size dirt for the geometry follow-up (see widget_emit_geometry). Read
 * by the incremental walk; consumed alongside the other classes by
 * clear_dirty. */
struct yetty_ycore_void_result yetty_ygui2_widget_geometry_dirty(struct yetty_yclass_object *obj,
                                                                 int *out_geometry);
struct yetty_ycore_void_result yetty_ygui2_widget_dirty_flags(struct yetty_yclass_object *obj,
                                                              int *out_skin, int *out_structure,
                                                              int *out_position);
struct yetty_ycore_void_result yetty_ygui2_widget_clear_dirty(struct yetty_yclass_object *obj);
/* Absolute placement (compatibility with ytop-style hand layout). A move is
 * position dirt (one offset update on the wire); a size change repaints. */
struct yetty_ycore_void_result yetty_ygui2_widget_set_position(struct yetty_yclass_object *obj,
                                                               float x, float y);
struct yetty_ycore_void_result yetty_ygui2_widget_set_size(struct yetty_yclass_object *obj, float w,
                                                           float h);
struct yetty_ycore_void_result yetty_ygui2_widget_absolute_rect(struct yetty_yclass_object *obj,
                                                                int *out_absolute, float *out_x,
                                                                float *out_y, float *out_w,
                                                                float *out_h);
struct yetty_ycore_void_result yetty_ygui2_widget_set_visible(struct yetty_yclass_object *obj,
                                                              int visible);
struct yetty_ycore_void_result yetty_ygui2_widget_emitted_offset(struct yetty_yclass_object *obj,
                                                                 float *out_x, float *out_y,
                                                                 int *out_ever);
struct yetty_ycore_void_result yetty_ygui2_widget_set_emitted_offset(
    struct yetty_yclass_object *obj, float x, float y);
struct yetty_ycore_void_result yetty_ygui2_widget_set_clip_enabled(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui2_widget_clip_state(struct yetty_yclass_object *obj,
                                                             int *out_enabled, float *out_w,
                                                             float *out_h);
struct yetty_ycore_void_result yetty_ygui2_widget_set_emitted_clip(struct yetty_yclass_object *obj,
                                                                   float w, float h);
/* Forget everything ever emitted for this widget's group instance. Called
 * when that wire instance is (about to be) destroyed — clear, rebuild, or
 * an ancestor reopen that recreates descendants with default group state —
 * so the next emission re-sends all non-default projection state instead
 * of trusting a cache describing the dead instance. */
struct yetty_ycore_void_result yetty_ygui2_widget_reset_emitted(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui2_widget_set_scroll(struct yetty_yclass_object *obj,
                                                             float scroll_x, float scroll_y);
struct yetty_ycore_void_result yetty_ygui2_widget_scroll(struct yetty_yclass_object *obj,
                                                         float *out_x, float *out_y);
/* Remove a widget from the live tree: unlink from the parent chain, mark
 * the parent structure-dirty (the next emit reopens it without this
 * subtree), invalidate focus/capture pointers into the subtree, run the
 * cleanup chain, free. The roots cannot be removed (dispose owns them). */
struct yetty_ycore_void_result yetty_ygui2_widget_remove(struct yetty_yclass_object *obj);

#ifdef __cplusplus
}
#endif

#endif
