/* GENERATED — do not edit. */
/* Public interface for regular class(es) `widget` (module: ygui).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGET_H
#define YETTY_YCLASSGEN_YGUI_WIDGET_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ycore_rectangle;
struct yetty_ygui_emit_ctx;

enum yetty_ygui_flex_direction {
    YETTY_YGUI_FLEX_ROW = 0,
    YETTY_YGUI_FLEX_COLUMN = 1,
};
enum yetty_ygui_flex_justify {
    YETTY_YGUI_JUSTIFY_START = 0,
    YETTY_YGUI_JUSTIFY_CENTER = 1,
    YETTY_YGUI_JUSTIFY_END = 2,
    YETTY_YGUI_JUSTIFY_SPACE_BETWEEN = 3,
};
enum yetty_ygui_flex_align {
    YETTY_YGUI_ALIGN_START = 0,
    YETTY_YGUI_ALIGN_CENTER = 1,
    YETTY_YGUI_ALIGN_END = 2,
    YETTY_YGUI_ALIGN_STRETCH = 3,
};
struct yetty_ygui_layout {
    enum yetty_ygui_flex_direction direction;
    enum yetty_ygui_flex_justify justify;
    enum yetty_ygui_flex_align align;
    float gap;
    float padding_top;
    float padding_right;
    float padding_bottom;
    float padding_left;
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
struct yetty_ygui_layout_const_ptr_result {
    int ok;
    union {
        const struct yetty_ygui_layout *value;
        struct yetty_ycore_error error;
    } ;
};

struct yetty_yclass_ptr_result yetty_ygui_widget_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ygui_widget;
struct yetty_ygui_widget_ptr_result {
    int ok;
    union {
        struct yetty_ygui_widget *value;
        struct yetty_ycore_error error;
    };
};
struct yetty_ygui_widget_ptr_result yetty_ygui_widget_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ygui_widget_to(struct yetty_ygui_widget *data);

struct yetty_ycore_int_result yetty_ygui_widget_on_press(struct yetty_yclass_object * obj, float x, float y, int button);
struct yetty_ycore_int_result yetty_ygui_widget_on_release(struct yetty_yclass_object * obj, float x, float y, int button);
struct yetty_ycore_int_result yetty_ygui_widget_on_motion(struct yetty_yclass_object * obj, float x, float y);
struct yetty_ycore_void_result yetty_ygui_widget_emit_body(struct yetty_yclass_object * obj, struct yetty_ygui_emit_ctx * emit_ctx);
struct yetty_ycore_void_result yetty_ygui_constructor(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_ygui_destructor(struct yetty_yclass_object * obj);
/* Wheel / trackpad scroll. (dx, dy) are the deltas at (x, y). Default:
 * not handled (0), so the framework keeps bubbling to an ancestor that
 * scrolls. Scrollable widgets (scrollarea, filepicker) override this. */
struct yetty_ycore_int_result yetty_ygui_widget_on_scroll(struct yetty_yclass_object * obj, float x, float y, float dx, float dy);
struct yetty_ycore_void_result yetty_ygui_widget_paint(struct yetty_yclass_object * obj, struct yetty_ygui_emit_ctx * emit_ctx);
struct yetty_ycore_void_result yetty_ygui_widget_emit_container(struct yetty_yclass_object * obj, struct yetty_ygui_emit_ctx * emit_ctx);

typedef struct yetty_ycore_int_result (*yetty_ygui_widget_on_press_fn)(struct yetty_yclass_object *, float, float, int);
typedef struct yetty_ycore_int_result (*yetty_ygui_widget_on_release_fn)(struct yetty_yclass_object *, float, float, int);
typedef struct yetty_ycore_int_result (*yetty_ygui_widget_on_motion_fn)(struct yetty_yclass_object *, float, float);
typedef struct yetty_ycore_void_result (*yetty_ygui_widget_emit_body_fn)(struct yetty_yclass_object *, struct yetty_ygui_emit_ctx *);
typedef struct yetty_ycore_void_result (*yetty_ygui_constructor_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ygui_destructor_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_int_result (*yetty_ygui_widget_on_scroll_fn)(struct yetty_yclass_object *, float, float, float, float);
typedef struct yetty_ycore_void_result (*yetty_ygui_widget_paint_fn)(struct yetty_yclass_object *, struct yetty_ygui_emit_ctx *);
typedef struct yetty_ycore_void_result (*yetty_ygui_widget_emit_container_fn)(struct yetty_yclass_object *, struct yetty_ygui_emit_ctx *);

struct yetty_yclass_object_ptr_result yetty_ygui_widget_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygui_register(void);

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
struct yetty_ycore_void_result yetty_ygui_super_void(struct yetty_yclass_object *obj, const struct yetty_yclass *self_class, yetty_yclass_method_id_t method_id);
struct yetty_ycore_int_result yetty_ygui_super_int(struct yetty_yclass_object *obj, const struct yetty_yclass *self_class, yetty_yclass_method_id_t method_id);
const struct yetty_yclass *yetty_ygui_class_expect(struct yetty_yclass_ptr_result class_result, const char *name);
struct yetty_yclass_object_ptr_result yetty_ygui_widget_parent(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ygui_widget_first_child(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ygui_widget_next_sibling(struct yetty_yclass_object *obj);
struct yetty_ygui_framework_ptr_result yetty_ygui_widget_framework(struct yetty_yclass_object *obj);
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
