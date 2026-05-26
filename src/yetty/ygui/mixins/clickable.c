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

struct clickable_data {
    int pressed;
    float press_x, press_y;
    yetty_ygui_click_cb on_click;
    void *userdata;
};

static struct yetty_ycore_int_result clickable_on_press(struct yetty_ygui_object *obj, float x,
                                                        float y, int button)
{
    (void)button;
    struct clickable_data *cd = yetty_ygui_data_get(obj, yetty_ygui_clickable_mixin_get());
    cd->pressed = 1;
    cd->press_x = x;
    cd->press_y = y;
    struct yetty_ycore_void_result dr = yetty_ygui_object_set_dirty(obj);
    if (YETTY_IS_ERR(dr)) {
        return YETTY_ERR(yetty_ycore_int, "clickable_on_press: set_dirty", dr);
    }
    return YETTY_OK(yetty_ycore_int, 1);
}

static struct yetty_ycore_int_result clickable_on_release(struct yetty_ygui_object *obj, float x,
                                                          float y, int button)
{
    (void)x;
    (void)y;
    (void)button;
    struct clickable_data *cd = yetty_ygui_data_get(obj, yetty_ygui_clickable_mixin_get());
    int was_pressed = cd->pressed;
    cd->pressed = 0;
    if (was_pressed && cd->on_click) {
        struct yetty_ycore_void_result r = cd->on_click(obj, cd->userdata);
        if (YETTY_IS_ERR(r)) {
            return YETTY_ERR(yetty_ycore_int, "clickable_on_release: on_click", r);
        }
    }
    struct yetty_ycore_void_result dr = yetty_ygui_object_set_dirty(obj);
    if (YETTY_IS_ERR(dr)) {
        return YETTY_ERR(yetty_ycore_int, "clickable_on_release: set_dirty", dr);
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
    struct clickable_data *cd = yetty_ygui_data_get(obj, yetty_ygui_clickable_mixin_get());
    cd->on_click = cb;
    cd->userdata = userdata;
    return YETTY_OK_VOID();
}

int yetty_ygui_clickable_is_pressed(const struct yetty_ygui_object *obj)
{
    if (!obj) {
        return 0;
    }
    struct clickable_data *cd =
        yetty_ygui_data_get((struct yetty_ygui_object *)obj, yetty_ygui_clickable_mixin_get());
    return cd->pressed;
}

static const struct yetty_ygui_op clickable_ops[] = {
    YETTY_YGUI_OP(yetty_ygui_widget_on_press, clickable_on_press),
    YETTY_YGUI_OP(yetty_ygui_widget_on_release, clickable_on_release),
};

static const struct yetty_ygui_class_descriptor clickable_desc = {
    .name = "yetty_ygui_clickable",
    .type = YETTY_YGUI_CLASS_TYPE_MIXIN,
    .data_size = sizeof(struct clickable_data),
};

YETTY_YGUI_DEFINE_CLASS(yetty_ygui_clickable_mixin_get, &clickable_desc, clickable_ops, NULL, NULL)
