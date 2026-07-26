/*
 * ygui-clickable.c — clickable mixin.
 *
 * Per-instance data:
 *   - pressed flag, press_x, press_y captured at on_press
 *   - on_click callback + userdata
 *
 * Ops registered:
 *   - on_press → sets pressed flag, records coords, returns 1 (consumed)
 *   - on_release → if currently pressed, fires the on_click callback
 *     and clears the flag; returns 1
 *
 * Mixins have no parent and are never instantiated alone — their data
 * slice lives inside whatever regular class includes them.
 */

#include "../internal.h"
#include <yetty/ygui/event.h>
#include <yetty/ygui/widget.h>

/* The click callback type. Defined here in the owning .c; codegen reproduces
 * it into the generated header for any public signature that references it. */
typedef struct yetty_ycore_void_result (*yetty_ygui_click_cb)(struct yetty_yclass_object *obj,
                                                              void *userdata);

struct YETTY_ANNOTATE("mixin@ygui:clickable") yetty_ygui_clickable {
    int pressed;
    float press_x, press_y;
    yetty_ygui_click_cb on_click;
    void *userdata;
};

/* This TU deliberately does NOT include its own generated header — that
 * header is a downstream artifact for other modules and would redefine
 * the YETTY_YRESULT_DECLARE this TU declares manually below. The class
 * handle Result wrapper plus the codegen accessor/downcast the appended
 * clickable.gen.c defines are declared here so the foot include and the impls
 * have them in scope. The generated public header publishes the identical
 * declarations for consumers. */
YETTY_YRESULT_DECLARE(yetty_ygui_clickable_ptr, struct yetty_ygui_clickable *);
struct yetty_yclass_ptr_result yetty_ygui_clickable_mixin_get(void);
struct yetty_ygui_clickable_ptr_result yetty_ygui_clickable_from(struct yetty_yclass_object *obj);

YETTY_ANNOTATE("override@ygui:clickable:widget_on_press")
static struct yetty_ycore_int_result clickable_on_press(struct yetty_yclass_object *yclass_obj,
                                                        float x, float y, int button)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    (void)button;
    struct yetty_ygui_clickable_ptr_result cd_dr = yetty_ygui_clickable_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, cd_dr, "clickable_on_press: data_get");
    struct yetty_ygui_clickable *cd = cd_dr.value;
    cd->pressed = 1;
    cd->press_x = x;
    cd->press_y = y;
    struct yetty_ycore_void_result dr = yetty_ygui_widget_set_dirty(obj);
    if (YETTY_IS_ERR(dr)) {
        return YETTY_ERR(yetty_ycore_int, "clickable_on_press: set_dirty", dr);
    }
    return YETTY_OK(yetty_ycore_int, 1);
}

YETTY_ANNOTATE("override@ygui:clickable:widget_on_release")
static struct yetty_ycore_int_result clickable_on_release(struct yetty_yclass_object *yclass_obj,
                                                          float x, float y, int button)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    (void)button;
    struct yetty_ygui_clickable_ptr_result cd_dr = yetty_ygui_clickable_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, cd_dr, "clickable_on_release: data_get");
    struct yetty_ygui_clickable *cd = cd_dr.value;
    int was_pressed = cd->pressed;
    cd->pressed = 0;
    /* Capture the direct callback + userdata NOW: any click handler (the
     * on_click below, or a CLICK subscriber fired via widget_emit) is free to
     * destroy this widget, after which `cd` is dangling. */
    yetty_ygui_click_cb on_click = cd->on_click;
    void *on_click_userdata = cd->userdata;
    /* Mark dirty BEFORE invoking any callback. A callback is free to destroy
     * this widget — apps frequently use it as a "navigate" trigger that
     * rebuilds the entire subtree the button lives in (see ygreeter's
     * on_row_clicked → rebuild_tab_content). After it returns, `obj` may already
     * have been yetty_ygui_del'd, so dereferencing it (set_dirty walks
     * obj->parent to find the root's engine pointer) is a use-after-free that
     * surfaces as a garbage engine pointer and a SEGV in framework_mark_dirty.
     * Setting dirty first preserves the "something happened this frame" hint
     * without depending on the widget surviving. */
    struct yetty_ycore_void_result dr = yetty_ygui_widget_set_dirty(obj);
    if (YETTY_IS_ERR(dr)) {
        return YETTY_ERR(yetty_ycore_int, "clickable_on_release: set_dirty", dr);
    }
    if (was_pressed) {
        /* Emit a CLICK event so widgets wired through widget_subscribe(CLICK)
         * (e.g. the yrich toolbar buttons) actually fire — the clickable mixin
         * otherwise only invokes the on_click callback below, leaving subscribe-
         * based handlers dead. A widget uses one mechanism or the other, so this
         * does not double-fire. */
        struct yetty_ygui_event click_event = {
            .type = YETTY_YGUI_EVENT_CLICK,
            .source = obj,
            .x = x,
            .y = y,
        };
        struct yetty_ycore_void_result er = yetty_ygui_widget_emit(obj, &click_event);
        if (YETTY_IS_ERR(er)) {
            return YETTY_ERR(yetty_ycore_int, "clickable_on_release: emit click", er);
        }
        if (on_click) {
            struct yetty_ycore_void_result r = on_click(obj, on_click_userdata);
            if (YETTY_IS_ERR(r)) {
                return YETTY_ERR(yetty_ycore_int, "clickable_on_release: on_click", r);
            }
        }
    }
    return YETTY_OK(yetty_ycore_int, 1);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_clickable_on_click_set(struct yetty_yclass_object *obj,
                                                                 yetty_ygui_click_cb cb,
                                                                 void *userdata)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_clickable_on_click_set: NULL obj");
    }
    struct yetty_ygui_clickable_ptr_result cd_dr = yetty_ygui_clickable_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, cd_dr, "yetty_ygui_clickable_on_click_set: data_get");
    struct yetty_ygui_clickable *cd = cd_dr.value;
    cd->on_click = cb;
    cd->userdata = userdata;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_ygui_clickable_is_pressed(const struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ygui_clickable_is_pressed: NULL obj");
    }
    struct yetty_ygui_clickable_ptr_result cd_dr =
        yetty_ygui_clickable_from((struct yetty_yclass_object *)obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, cd_dr, "yetty_ygui_clickable_is_pressed: data_get");
    struct yetty_ygui_clickable *cd = cd_dr.value;
    return YETTY_OK(yetty_ycore_int, cd->pressed);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_clickable_press_pos(const struct yetty_yclass_object *obj,
                                                              float *x, float *y)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_clickable_press_pos: NULL obj");
    }
    struct yetty_ygui_clickable_ptr_result cd_dr =
        yetty_ygui_clickable_from((struct yetty_yclass_object *)obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, cd_dr, "yetty_ygui_clickable_press_pos: data_get");
    struct yetty_ygui_clickable *cd = cd_dr.value;
    if (x) {
        *x = cd->press_x;
    }
    if (y) {
        *y = cd->press_y;
    }
    return YETTY_OK_VOID();
}

#include "yetty/gen/impl/ygui/mixins/clickable.c"
