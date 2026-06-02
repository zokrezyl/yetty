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

#include <yetty/ygui/mixins/clickable.h>

struct [[clang::annotate("mixin@ygui:clickable")]] clickable_data {
    int pressed;
    float press_x, press_y;
    yetty_ygui_click_cb on_click;
    void *userdata;
};

[[clang::annotate("override@ygui:clickable:widget_on_press")]]
static struct yetty_ycore_int_result clickable_on_press(struct yetty_yclass_ctx *yclass_ctx,
                                                        struct yetty_yclass_object *yclass_obj,
                                                        float x, float y, int button)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    (void)button;
    struct clickable_data *cd =
        yetty_ygui_data_get(obj, yetty_ygui_clickable_mixin_get().value);
    cd->pressed = 1;
    cd->press_x = x;
    cd->press_y = y;
    struct yetty_ycore_void_result dr = yetty_ygui_object_set_dirty(obj);
    if (YETTY_IS_ERR(dr)) {
        return YETTY_ERR(yetty_ycore_int, "clickable_on_press: set_dirty", dr);
    }
    return YETTY_OK(yetty_ycore_int, 1);
}

[[clang::annotate("override@ygui:clickable:widget_on_release")]]
static struct yetty_ycore_int_result clickable_on_release(struct yetty_yclass_ctx *yclass_ctx,
                                                          struct yetty_yclass_object *yclass_obj,
                                                          float x, float y, int button)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    (void)x;
    (void)y;
    (void)button;
    struct clickable_data *cd =
        yetty_ygui_data_get(obj, yetty_ygui_clickable_mixin_get().value);
    int was_pressed = cd->pressed;
    cd->pressed = 0;
    /* Mark dirty BEFORE invoking on_click. The callback is free to
     * destroy this widget — apps frequently use it as a "navigate"
     * trigger that rebuilds the entire subtree the button lives in
     * (see ygreeter's on_row_clicked → rebuild_tab_content). After
     * on_click returns, `obj` may already have been yetty_ygui_del'd,
     * so dereferencing it (set_dirty walks obj->parent until it finds
     * the root's engine pointer) is a use-after-free that surfaces as
     * a garbage engine pointer and a SEGV in framework_mark_dirty.
     * Setting dirty first preserves the "something happened this
     * frame" hint without depending on the widget surviving. */
    struct yetty_ycore_void_result dr = yetty_ygui_object_set_dirty(obj);
    if (YETTY_IS_ERR(dr)) {
        return YETTY_ERR(yetty_ycore_int, "clickable_on_release: set_dirty", dr);
    }
    if (was_pressed && cd->on_click) {
        struct yetty_ycore_void_result r =
            cd->on_click(NULL, (struct yetty_yclass_object *)obj, cd->userdata);
        if (YETTY_IS_ERR(r)) {
            return YETTY_ERR(yetty_ycore_int, "clickable_on_release: on_click", r);
        }
    }
    return YETTY_OK(yetty_ycore_int, 1);
}

struct yetty_ycore_void_result yetty_ygui_clickable_on_click_set(struct yetty_ygui_object *obj,
                                                                 yetty_ygui_click_cb cb,
                                                                 void *userdata)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_clickable_on_click_set: NULL obj");
    }
    struct clickable_data *cd =
        yetty_ygui_data_get(obj, yetty_ygui_clickable_mixin_get().value);
    cd->on_click = cb;
    cd->userdata = userdata;
    return YETTY_OK_VOID();
}

int yetty_ygui_clickable_is_pressed(const struct yetty_ygui_object *obj)
{
    if (!obj) {
        return 0;
    }
    struct clickable_data *cd = yetty_ygui_data_get(
        (struct yetty_ygui_object *)obj, yetty_ygui_clickable_mixin_get().value);
    return cd->pressed;
}

void yetty_ygui_clickable_press_pos(const struct yetty_ygui_object *obj, float *x, float *y)
{
    if (!obj) {
        if (x) {
            *x = 0.0f;
        }
        if (y) {
            *y = 0.0f;
        }
        return;
    }
    struct clickable_data *cd = yetty_ygui_data_get(
        (struct yetty_ygui_object *)obj, yetty_ygui_clickable_mixin_get().value);
    if (x) {
        *x = cd->press_x;
    }
    if (y) {
        *y = cd->press_y;
    }
}

#include "clickable.gen.c"
