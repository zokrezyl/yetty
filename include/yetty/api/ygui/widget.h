/* GENERATED — do not edit. */
/* Object API for regular class(es) `widget` (implementation module: ygui).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YGUI_WIDGET_H
#define YETTY_YCLASSGEN_API_YGUI_WIDGET_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ycore_rectangle;
struct yetty_ygui_emit_ctx;

#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YGUI_FLEX_DIRECTION
#define YETTY_YCLASSGEN_TYPE_YETTY_YGUI_FLEX_DIRECTION
enum yetty_ygui_flex_direction {
    YETTY_YGUI_FLEX_ROW = 0,
    YETTY_YGUI_FLEX_COLUMN = 1,
};
#endif
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YGUI_FLEX_JUSTIFY
#define YETTY_YCLASSGEN_TYPE_YETTY_YGUI_FLEX_JUSTIFY
enum yetty_ygui_flex_justify {
    YETTY_YGUI_JUSTIFY_START = 0,
    YETTY_YGUI_JUSTIFY_CENTER = 1,
    YETTY_YGUI_JUSTIFY_END = 2,
    YETTY_YGUI_JUSTIFY_SPACE_BETWEEN = 3,
};
#endif
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YGUI_FLEX_ALIGN
#define YETTY_YCLASSGEN_TYPE_YETTY_YGUI_FLEX_ALIGN
enum yetty_ygui_flex_align {
    YETTY_YGUI_ALIGN_START = 0,
    YETTY_YGUI_ALIGN_CENTER = 1,
    YETTY_YGUI_ALIGN_END = 2,
    YETTY_YGUI_ALIGN_STRETCH = 3,
};
#endif
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YGUI_FLEX_ALIGN_SELF
#define YETTY_YCLASSGEN_TYPE_YETTY_YGUI_FLEX_ALIGN_SELF
enum yetty_ygui_flex_align_self {
    YETTY_YGUI_ALIGN_SELF_AUTO = 0,
    YETTY_YGUI_ALIGN_SELF_START = 1,
    YETTY_YGUI_ALIGN_SELF_CENTER = 2,
    YETTY_YGUI_ALIGN_SELF_END = 3,
    YETTY_YGUI_ALIGN_SELF_STRETCH = 4,
};
#endif
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YGUI_FLEX_WRAP
#define YETTY_YCLASSGEN_TYPE_YETTY_YGUI_FLEX_WRAP
enum yetty_ygui_flex_wrap {
    YETTY_YGUI_WRAP_NOWRAP = 0,
    YETTY_YGUI_WRAP_WRAP = 1,
};
#endif
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YGUI_LAYOUT
#define YETTY_YCLASSGEN_TYPE_YETTY_YGUI_LAYOUT
struct yetty_ygui_layout {
    enum yetty_ygui_flex_direction direction;
    enum yetty_ygui_flex_justify justify;
    enum yetty_ygui_flex_align align;
    enum yetty_ygui_flex_align_self align_self;
    enum yetty_ygui_flex_wrap wrap;
    float gap;
    float padding_top;
    float padding_right;
    float padding_bottom;
    float padding_left;
    float margin_top;
    float margin_right;
    float margin_bottom;
    float margin_left;
    float width;
    float height;
    float flex_grow;
    float flex_shrink;
    float min_width;
    float max_width;
    float min_height;
    float max_height;
    int absolute;
    float pos_x;
    float pos_y;
    int hidden;
};
#endif
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YGUI_LAYOUT_CONST_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YGUI_LAYOUT_CONST_PTR_RESULT
struct yetty_ygui_layout_const_ptr_result {
    int ok;
    union {
        const struct yetty_ygui_layout *value;
        struct yetty_ycore_error error;
    } ;
};
#endif



/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ygui_widget;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YGUI_WIDGET_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YGUI_WIDGET_PTR_RESULT
struct yetty_ygui_widget_ptr_result {
    int ok;
    union {
        struct yetty_ygui_widget *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ygui_widget_ptr_result yetty_ygui_widget_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ygui_widget_to(struct yetty_ygui_widget *data);

struct yetty_ycore_void_result yetty_ygui_destructor(struct yetty_yclass_object * obj);
struct yetty_ycore_int_result yetty_ygui_widget_on_press(struct yetty_yclass_object * obj, float x, float y, int button);
struct yetty_ycore_int_result yetty_ygui_widget_on_release(struct yetty_yclass_object * obj, float x, float y, int button);
struct yetty_ycore_int_result yetty_ygui_widget_on_motion(struct yetty_yclass_object * obj, float x, float y);
struct yetty_ycore_void_result yetty_ygui_widget_emit_body(struct yetty_yclass_object * obj, struct yetty_ygui_emit_ctx * emit_ctx);
/* Wheel / trackpad scroll. (dx, dy) are the deltas at (x, y). Default:
 * not handled (0), so the framework keeps bubbling to an ancestor that
 * scrolls. Scrollable widgets (scrollarea, filepicker) override this. */
struct yetty_ycore_int_result yetty_ygui_widget_on_scroll(struct yetty_yclass_object * obj, float x, float y, float dx, float dy);
struct yetty_ycore_void_result yetty_ygui_widget_paint(struct yetty_yclass_object * obj, struct yetty_ygui_emit_ctx * emit_ctx);
struct yetty_ycore_void_result yetty_ygui_widget_emit_container(struct yetty_yclass_object * obj, struct yetty_ygui_emit_ctx * emit_ctx);

struct yetty_yclass_object_ptr_result yetty_ygui_widget_create(struct yetty_yclass_ctx *ctx);



/* The flex layout pass is defined in layout.c — a non-class TU that produces
 * no header of its own — but its public declaration belongs to the widget API,
 * so expose it from here (codegen emits the prototype into widget.h). */
struct yetty_ycore_void_result yetty_ygui_layout_compute(struct yetty_yclass_object *root, struct yetty_ycore_rectangle root_rect);
struct yetty_ygui_layout yetty_ygui_layout_default(void);
struct yetty_ycore_void_result yetty_ygui_widget_set_rect(struct yetty_yclass_object *obj, struct yetty_ycore_rectangle rect);
struct yetty_ycore_rectangle_result yetty_ygui_widget_rect(const struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui_widget_layout_set(struct yetty_yclass_object *obj, const struct yetty_ygui_layout *layout);
struct yetty_ygui_layout_const_ptr_result yetty_ygui_widget_layout_get(const struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui_widget_set_visible(struct yetty_yclass_object *obj, int visible);
struct yetty_ycore_int_result yetty_ygui_widget_is_visible(const struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui_widget_set_size(struct yetty_yclass_object *obj, float w, float h);
struct yetty_ycore_void_result yetty_ygui_widget_set_position(struct yetty_yclass_object *obj, float x, float y);
struct yetty_ycore_void_result yetty_ygui_widget_make_figure(struct yetty_yclass_object *obj, uint32_t kind, int32_t z);
struct yetty_ycore_void_result yetty_ygui_widget_set_figure_z(struct yetty_yclass_object *obj, int32_t z);
struct yetty_ycore_void_result yetty_ygui_widget_set_floating(struct yetty_yclass_object *obj, int floating);
struct yetty_ycore_int_result yetty_ygui_widget_is_floating(const struct yetty_yclass_object *obj);
struct yetty_ycore_uint32_result yetty_ygui_widget_figure_kind(const struct yetty_yclass_object *obj);
struct yetty_ycore_int_result yetty_ygui_widget_figure_z(const struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui_widget_set_bg_color(struct yetty_yclass_object *obj, uint32_t color);
struct yetty_ycore_uint32_result yetty_ygui_widget_bg(const struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui_widget_apply_css(struct yetty_yclass_object *obj, const char *css);
/*
 * Generic "call the parent's implementation of this slot" helpers.
 *
 * ABI CONTRACT: read before adding a new caller. These two generic helpers
 * cast the resolved parent impl to a function pointer that takes only:
 *
 *     (struct yetty_yclass_object *)
 *
 * They are therefore valid only for slots whose implementation signature is
 * exactly that. In the current ygui widget slot set, that means constructor
 * and destructor for super_void.
 *
 * They must not be used for slots that carry extra parameters. In particular,
 * these widget slots have wider signatures:
 *
 *     widget_on_press(obj, float x, float y, int button)
 *     widget_on_release(obj, float x, float y, int button)
 *     widget_on_motion(obj, float x, float y)
 *     widget_on_scroll(obj, float x, float y, float dx, float dy)
 *     widget_paint(obj, struct yetty_ygui_emit_ctx *)
 *     widget_emit_container(obj, struct yetty_ygui_emit_ctx *)
 *     widget_emit_body(obj, struct yetty_ygui_emit_ctx *)
 *
 * Routing one of those through super_void/super_int casts away the extra
 * arguments and calls with the wrong ABI (undefined behavior). If a subclass
 * needs to chain to a parent implementation for a wider slot, add a typed
 * per-slot super helper with the matching signature rather than widening
 * these generic helpers.
 *
 * This contract is ENFORCED at runtime, not merely documented: super_void
 * rejects any method_id other than constructor/destructor, and super_int —
 * for which no valid (obj)-only int virtual exists and which has no callers —
 * rejects unconditionally. Both return a Result error rather than performing
 * the unsafe cast, so misuse fails loudly at the call site.
 */
struct yetty_ycore_void_result yetty_ygui_super_void(struct yetty_yclass_object *obj, const struct yetty_yclass *self_class, yetty_yclass_method_id_t method_id);
struct yetty_ycore_int_result yetty_ygui_super_int(struct yetty_yclass_object *obj, const struct yetty_yclass *self_class, yetty_yclass_method_id_t method_id);
struct yetty_ycore_int_result yetty_ygui_super_on_press(struct yetty_yclass_object *obj, const struct yetty_yclass *self_class, float x, float y, int button);
struct yetty_ycore_int_result yetty_ygui_super_on_release(struct yetty_yclass_object *obj, const struct yetty_yclass *self_class, float x, float y, int button);
struct yetty_ycore_int_result yetty_ygui_super_on_motion(struct yetty_yclass_object *obj, const struct yetty_yclass *self_class, float x, float y);
struct yetty_ycore_int_result yetty_ygui_super_on_scroll(struct yetty_yclass_object *obj, const struct yetty_yclass *self_class, float x, float y, float dx, float dy);
struct yetty_ycore_int_result yetty_ygui_mixin_on_press(struct yetty_yclass_object *obj, const struct yetty_yclass *mixin_class, float x, float y, int button);
struct yetty_ycore_int_result yetty_ygui_mixin_on_release(struct yetty_yclass_object *obj, const struct yetty_yclass *mixin_class, float x, float y, int button);
struct yetty_ycore_int_result yetty_ygui_mixin_on_motion(struct yetty_yclass_object *obj, const struct yetty_yclass *mixin_class, float x, float y);
struct yetty_ycore_int_result yetty_ygui_mixin_on_scroll(struct yetty_yclass_object *obj, const struct yetty_yclass *mixin_class, float x, float y, float dx, float dy);
const struct yetty_yclass *yetty_ygui_class_expect(struct yetty_yclass_ptr_result class_result, const char *name);
struct yetty_yclass_object_ptr_result yetty_ygui_widget_parent(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ygui_widget_first_child(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ygui_widget_next_sibling(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ygui_widget_framework(struct yetty_yclass_object *obj);
struct yetty_ycore_uint32_result yetty_ygui_widget_id(const struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui_widget_set_dirty(struct yetty_yclass_object *obj);
struct yetty_ycore_int_result yetty_ygui_widget_is_dirty(const struct yetty_yclass_object *obj);
struct yetty_ycore_int_result yetty_ygui_widget_is_hovered(const struct yetty_yclass_object *obj);
/* Create a parentless (root / top-level) widget of `cls`. */
struct yetty_yclass_object_ptr_result yetty_ygui_widget_new(const struct yetty_yclass *cls);
/* Create a widget of `cls` and add it as the last child of `parent`. */
struct yetty_yclass_object_ptr_result yetty_ygui_widget_add(struct yetty_yclass_object *parent, const struct yetty_yclass *cls);
struct yetty_ycore_void_result yetty_ygui_widget_destroy(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui_widget_raise(struct yetty_yclass_object *obj);

#ifdef __cplusplus
}
#endif

#endif
