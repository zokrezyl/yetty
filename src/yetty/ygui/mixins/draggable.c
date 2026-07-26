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
#include <yetty/ygui/widget.h>

/* Drag callback type. Defined here in the owning .c; codegen reproduces it
 * into the generated header for any public signature that references it. */
typedef struct yetty_ycore_void_result (*yetty_ygui_drag_cb)(struct yetty_yclass_object *obj,
                                                             float dx, float dy, void *userdata);

struct YETTY_ANNOTATE("mixin@ygui:draggable") yetty_ygui_draggable {
    int dragging;
    float last_x, last_y;
    /* Where the current drag STARTED — consumers pick gesture semantics
	 * from it (a scrollarea grab on the thumb scrolls WITH the cursor,
	 * a grab on the content scrolls inversely, touch-style). */
    float press_x, press_y;
    yetty_ygui_drag_cb on_drag;
    void *userdata;
};

/* This TU deliberately does NOT include its own generated header — that
 * header is a downstream artifact for other modules and would redefine
 * the YETTY_YRESULT_DECLARE this TU declares manually below. The class
 * handle Result wrapper plus the codegen accessor/downcast the appended
 * draggable.gen.c defines are declared here so the foot include and the impls
 * have them in scope. The generated public header publishes the identical
 * declarations for consumers. */
YETTY_YRESULT_DECLARE(yetty_ygui_draggable_ptr, struct yetty_ygui_draggable *);
struct yetty_yclass_ptr_result yetty_ygui_draggable_mixin_get(void);
struct yetty_ygui_draggable_ptr_result yetty_ygui_draggable_from(struct yetty_yclass_object *obj);

YETTY_ANNOTATE("override@ygui:draggable:widget_on_press")
static struct yetty_ycore_int_result draggable_on_press(struct yetty_yclass_object *yclass_obj,
                                                        float x, float y, int button)
{
    (void)button;
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ygui_draggable_ptr_result dd_dr = yetty_ygui_draggable_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, dd_dr, "draggable_on_press: data_get");
    struct yetty_ygui_draggable *dd = dd_dr.value;
    dd->dragging = 1;
    dd->last_x = x;
    dd->last_y = y;
    dd->press_x = x;
    dd->press_y = y;
    struct yetty_ycore_void_result dr = yetty_ygui_widget_set_dirty(obj);
    if (YETTY_IS_ERR(dr)) {
        return YETTY_ERR(yetty_ycore_int, "draggable_on_press: set_dirty", dr);
    }
    /* Consume so the framework captures the pointer: subsequent motion is
     * routed here even after the cursor leaves this widget's rect. */
    return YETTY_OK(yetty_ycore_int, 1);
}

YETTY_ANNOTATE("override@ygui:draggable:widget_on_motion")
static struct yetty_ycore_int_result draggable_on_motion(struct yetty_yclass_object *yclass_obj,
                                                         float x, float y)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ygui_draggable_ptr_result dd_dr = yetty_ygui_draggable_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, dd_dr, "draggable_on_motion: data_get");
    struct yetty_ygui_draggable *dd = dd_dr.value;
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

YETTY_ANNOTATE("override@ygui:draggable:widget_on_release")
static struct yetty_ycore_int_result draggable_on_release(struct yetty_yclass_object *yclass_obj,
                                                          float x, float y, int button)
{
    (void)x;
    (void)y;
    (void)button;
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ygui_draggable_ptr_result dd_dr = yetty_ygui_draggable_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, dd_dr, "draggable_on_release: data_get");
    struct yetty_ygui_draggable *dd = dd_dr.value;
    dd->dragging = 0;
    struct yetty_ycore_void_result dr = yetty_ygui_widget_set_dirty(obj);
    if (YETTY_IS_ERR(dr)) {
        return YETTY_ERR(yetty_ycore_int, "draggable_on_release: set_dirty", dr);
    }
    return YETTY_OK(yetty_ycore_int, 1);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_draggable_on_drag_set(struct yetty_yclass_object *obj,
                                                                yetty_ygui_drag_cb cb,
                                                                void *userdata)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_draggable_on_drag_set: NULL obj");
    }
    struct yetty_ygui_draggable_ptr_result dd_dr = yetty_ygui_draggable_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dd_dr, "yetty_ygui_draggable_on_drag_set: data_get");
    struct yetty_ygui_draggable *dd = dd_dr.value;
    dd->on_drag = cb;
    dd->userdata = userdata;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_draggable_press_point(struct yetty_yclass_object *obj,
                                                                float *out_x, float *out_y)
{
    if (!obj || !out_x || !out_y) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_draggable_press_point: NULL arg");
    }
    struct yetty_ygui_draggable_ptr_result dd_dr = yetty_ygui_draggable_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dd_dr, "yetty_ygui_draggable_press_point: data_get");
    struct yetty_ygui_draggable *dd = dd_dr.value;
    *out_x = dd->press_x;
    *out_y = dd->press_y;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_ygui_draggable_is_dragging(
    const struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ygui_draggable_is_dragging: NULL obj");
    }
    struct yetty_ygui_draggable_ptr_result dd_dr =
        yetty_ygui_draggable_from((struct yetty_yclass_object *)obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, dd_dr, "yetty_ygui_draggable_is_dragging: data_get");
    struct yetty_ygui_draggable *dd = dd_dr.value;
    return YETTY_OK(yetty_ycore_int, dd->dragging);
}

#include "yetty/gen/impl/ygui/mixins/draggable.c"
