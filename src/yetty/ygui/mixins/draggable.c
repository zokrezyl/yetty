/*
 * ygui-draggable.c — draggable mixin.
 *
 * Per-instance data:
 *   - dragging flag; last_x, last_y of the previous motion sample
 *   - on_drag callback + userdata
 *
 * Ops registered:
 *   - on_press   → sets dragging, records the press point, returns 1
 *     (consumed) so the framework captures the pointer for the drag
 *   - on_motion  → while dragging, fires on_drag with the (dx, dy)
 *     delta since the last sample and returns 1; otherwise returns 0
 *     so a plain hover-move keeps bubbling
 *   - on_release → clears dragging; returns 1
 *
 * Mixins have no parent and are never instantiated alone — their data
 * slice lives inside whatever regular class includes them. Widgets like
 * the scrollarea thumb and resizable dividers list this mixin to get
 * drag semantics without re-implementing pointer-capture bookkeeping.
 */

#include "../internal.h"

#include <yetty/ygui/mixins/draggable.h>

struct [[clang::annotate("mixin@ygui:draggable")]] draggable_data {
    int dragging;
    float last_x, last_y;
    yetty_ygui_drag_cb on_drag;
    void *userdata;
};

[[clang::annotate("override@ygui:draggable:widget_on_press")]]
static struct yetty_ycore_int_result draggable_on_press(struct yetty_yclass_ctx *yclass_ctx,
                                                        struct yetty_yclass_object *yclass_obj,
                                                        float x, float y, int button)
{
    (void)yclass_ctx;
    (void)button;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct draggable_data *dd =
        yetty_ygui_data_get(obj, yetty_ygui_draggable_mixin_get().value);
    dd->dragging = 1;
    dd->last_x = x;
    dd->last_y = y;
    struct yetty_ycore_void_result dr = yetty_ygui_object_set_dirty(obj);
    if (YETTY_IS_ERR(dr)) {
        return YETTY_ERR(yetty_ycore_int, "draggable_on_press: set_dirty", dr);
    }
    /* Consume so the framework captures the pointer: subsequent motion is
     * routed here even after the cursor leaves this widget's rect. */
    return YETTY_OK(yetty_ycore_int, 1);
}

[[clang::annotate("override@ygui:draggable:widget_on_motion")]]
static struct yetty_ycore_int_result draggable_on_motion(struct yetty_yclass_ctx *yclass_ctx,
                                                         struct yetty_yclass_object *yclass_obj,
                                                         float x, float y)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct draggable_data *dd =
        yetty_ygui_data_get(obj, yetty_ygui_draggable_mixin_get().value);
    /* The framework also delivers motion to whatever the pointer hovers,
     * not just the capture target — only act while a press of ours is in
     * flight, otherwise let the event bubble. */
    if (!dd->dragging) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    float dx = x - dd->last_x;
    float dy = y - dd->last_y;
    dd->last_x = x;
    dd->last_y = y;
    if (dd->on_drag) {
        struct yetty_ycore_void_result r = dd->on_drag(obj, dx, dy, dd->userdata);
        if (YETTY_IS_ERR(r)) {
            return YETTY_ERR(yetty_ycore_int, "draggable_on_motion: on_drag", r);
        }
    }
    return YETTY_OK(yetty_ycore_int, 1);
}

[[clang::annotate("override@ygui:draggable:widget_on_release")]]
static struct yetty_ycore_int_result draggable_on_release(struct yetty_yclass_ctx *yclass_ctx,
                                                          struct yetty_yclass_object *yclass_obj,
                                                          float x, float y, int button)
{
    (void)yclass_ctx;
    (void)x;
    (void)y;
    (void)button;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct draggable_data *dd =
        yetty_ygui_data_get(obj, yetty_ygui_draggable_mixin_get().value);
    dd->dragging = 0;
    struct yetty_ycore_void_result dr = yetty_ygui_object_set_dirty(obj);
    if (YETTY_IS_ERR(dr)) {
        return YETTY_ERR(yetty_ycore_int, "draggable_on_release: set_dirty", dr);
    }
    return YETTY_OK(yetty_ycore_int, 1);
}

struct yetty_ycore_void_result yetty_ygui_draggable_on_drag_set(struct yetty_ygui_object *obj,
                                                                yetty_ygui_drag_cb cb,
                                                                void *userdata)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_draggable_on_drag_set: NULL obj");
    }
    struct draggable_data *dd =
        yetty_ygui_data_get(obj, yetty_ygui_draggable_mixin_get().value);
    dd->on_drag = cb;
    dd->userdata = userdata;
    return YETTY_OK_VOID();
}

int yetty_ygui_draggable_is_dragging(const struct yetty_ygui_object *obj)
{
    if (!obj) {
        return 0;
    }
    struct draggable_data *dd = yetty_ygui_data_get(
        (struct yetty_ygui_object *)obj, yetty_ygui_draggable_mixin_get().value);
    return dd->dragging;
}

#include "draggable.gen.c"
