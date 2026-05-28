/*
 * ygui-widget.c — base widget class, ported to yclass.
 *
 * Every widget inherits from this class. It owns:
 *   - geometry: rect (absolute target pixels)
 *   - layout struct (flex / CSS authoring values)
 *
 * Public stubs (`yetty_ygui_widget_paint`, on_press, …) stay
 * historic-shaped (`obj, …`) for caller convenience; the slot impls
 * are yclass-shaped (`ctx, obj, …`). The stub looks up the impl via
 * `yetty_ygui_dispatch_lookup(obj->klass, slot)` and invokes it,
 * passing NULL ctx and casting the ygui_object* to yclass_object*
 * (layout-compatible: first member `klass` is the same field).
 */

#include "internal.h"

#include <yetty/yfigure/wire.h>
#include <string.h>

/*===========================================================================
 * Widget per-class data slice.
 *=========================================================================*/

struct [[clang::annotate("class@ygui:widget")]] yetty_ygui_widget_data {
    struct yetty_ycore_rectangle rect;
    struct yetty_ygui_layout layout;
};

/* Convenience accessor — every internal helper that needs the widget
 * class pointer goes through this. The codegen-shaped accessor returns
 * a Result; this unwraps to the plain pointer for callers that treat
 * registration failure as fatal (which it is — class registration
 * happens at startup or never). */
static const struct yetty_yclass *widget_class(void)
{
    return yetty_ygui_widget_class_get().value;
}

/*===========================================================================
 * Public stubs `yetty_ygui_widget_paint` / `_emit_container` / `_emit_body`
 * / `_on_press` / `_on_release` / `_on_motion` are emitted by yclass
 * codegen from the override annotations on `widget_default_*` below.
 * The generated stubs live in methods.gen.c; they have the canonical
 * yclass slot signature `(struct yetty_yclass_ctx *, struct
 * yetty_yclass_object *, …)`.
 *=========================================================================*/

/*===========================================================================
 * Default implementations registered on the base widget class. Each
 * impl is yclass-shaped (ctx, obj, …) — `ctx` is unused (local
 * dispatch only); `obj` is the ygui_object cast as yclass_object,
 * recovered here as the original type via the layout-compatible first
 * member.
 *=========================================================================*/

static struct yetty_ygui_object *self_of(struct yetty_yclass_object *obj)
{
    return (struct yetty_ygui_object *)obj;
}

[[clang::annotate("override@ygui:widget:constructor")]]
static struct yetty_ycore_void_result
widget_default_constructor(struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj)
{
    (void)ctx;
    struct yetty_ygui_widget_data *wd = yetty_ygui_data_get(self_of(obj), widget_class());
    wd->rect.min.x = 0;
    wd->rect.min.y = 0;
    wd->rect.max.x = 0;
    wd->rect.max.y = 0;
    wd->layout = yetty_ygui_layout_default();
    return YETTY_OK_VOID();
}

[[clang::annotate("override@ygui:widget:destructor")]]
static struct yetty_ycore_void_result
widget_default_destructor(struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj)
{
    (void)ctx;
    (void)obj;
    return YETTY_OK_VOID();
}

[[clang::annotate("override@ygui:widget:widget_on_press")]]
static struct yetty_ycore_int_result
widget_default_on_press(struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj, float x,
                        float y, int button)
{
    (void)ctx;
    (void)obj;
    (void)x;
    (void)y;
    (void)button;
    return YETTY_OK(yetty_ycore_int, 0);
}

[[clang::annotate("override@ygui:widget:widget_on_release")]]
static struct yetty_ycore_int_result
widget_default_on_release(struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj, float x,
                          float y, int button)
{
    (void)ctx;
    (void)obj;
    (void)x;
    (void)y;
    (void)button;
    return YETTY_OK(yetty_ycore_int, 0);
}

[[clang::annotate("override@ygui:widget:widget_on_motion")]]
static struct yetty_ycore_int_result
widget_default_on_motion(struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj, float x,
                         float y)
{
    (void)ctx;
    (void)obj;
    (void)x;
    (void)y;
    return YETTY_OK(yetty_ycore_int, 0);
}

[[clang::annotate("override@ygui:widget:widget_paint")]]
static struct yetty_ycore_void_result widget_default_paint(struct yetty_yclass_ctx *ctx,
                                                           struct yetty_yclass_object *obj,
                                                           struct yetty_ygui_emit_ctx *emit_ctx)
{
    (void)ctx;
    (void)obj;
    (void)emit_ctx;
    return YETTY_OK_VOID();
}

[[clang::annotate("override@ygui:widget:widget_emit_container")]]
static struct yetty_ycore_void_result
widget_default_emit_container(struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj,
                              struct yetty_ygui_emit_ctx *emit_ctx)
{
    (void)ctx;
    (void)obj;
    (void)emit_ctx;
    return YETTY_OK_VOID();
}

[[clang::annotate("override@ygui:widget:widget_emit_body")]]
static struct yetty_ycore_void_result widget_default_emit_body(struct yetty_yclass_ctx *ctx,
                                                               struct yetty_yclass_object *obj,
                                                               struct yetty_ygui_emit_ctx *emit_ctx)
{
    (void)ctx;
    (void)obj;
    (void)emit_ctx;
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Layout setters / getters.
 *=========================================================================*/

struct yetty_ygui_layout yetty_ygui_layout_default(void)
{
    struct yetty_ygui_layout l = {0};
    l.direction = YETTY_YGUI_FLEX_ROW;
    l.justify = YETTY_YGUI_JUSTIFY_START;
    l.align = YETTY_YGUI_ALIGN_START;
    l.gap = 0;
    l.width = -1.0f;
    l.height = -1.0f;
    l.flex_grow = 0;
    l.flex_shrink = 0;
    l.min_width = -1.0f;
    l.max_width = -1.0f;
    l.min_height = -1.0f;
    l.max_height = -1.0f;
    l.absolute = 0;
    l.pos_x = 0.0f;
    l.pos_y = 0.0f;
    return l;
}

struct yetty_ycore_void_result yetty_ygui_widget_set_rect(struct yetty_ygui_object *obj,
                                                          struct yetty_ycore_rectangle rect)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_set_rect: NULL obj");
    }
    struct yetty_ygui_widget_data *wd = yetty_ygui_data_get(obj, widget_class());
    wd->rect = rect;
    obj->dirty = 1;
    return YETTY_OK_VOID();
}

struct yetty_ycore_rectangle yetty_ygui_widget_rect(const struct yetty_ygui_object *obj)
{
    if (!obj) {
        struct yetty_ycore_rectangle z = {0};
        return z;
    }
    struct yetty_ygui_widget_data *wd =
        yetty_ygui_data_get((struct yetty_ygui_object *)obj, widget_class());
    return wd->rect;
}

struct yetty_ycore_void_result yetty_ygui_widget_layout_set(struct yetty_ygui_object *obj,
                                                            const struct yetty_ygui_layout *layout)
{
    if (!obj || !layout) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_layout_set: NULL arg");
    }
    struct yetty_ygui_widget_data *wd = yetty_ygui_data_get(obj, widget_class());
    wd->layout = *layout;
    obj->dirty = 1;
    return YETTY_OK_VOID();
}

const struct yetty_ygui_layout *yetty_ygui_widget_layout_get(const struct yetty_ygui_object *obj)
{
    if (!obj) {
        return NULL;
    }
    struct yetty_ygui_widget_data *wd =
        yetty_ygui_data_get((struct yetty_ygui_object *)obj, widget_class());
    return &wd->layout;
}

/*===========================================================================
 * Class accessor — yclass-shaped, returns Result. First call registers
 * the class with the yclass runtime; subsequent calls return the
 * cached pointer.
 *=========================================================================*/

#include "widget.gen.c"
