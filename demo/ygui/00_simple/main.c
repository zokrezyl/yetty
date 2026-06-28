/*
 * Demo 00_simple: a single button with a click callback.
 *
 * Standalone-mode ygui demo. The runner brings up window + GPU +
 * receiver-side container; this file only adds one button and wires its
 * click event. Click the button to change its label; press 'q' (or
 * Ctrl-C / Ctrl-D) to quit.
 */

#include "../runner.h"
#include <yetty/ygui/ygui.h>
#include <yetty/ygui/mixins/clickable.h>

static inline void err_ok(struct yetty_ycore_void_result r)
{
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
    }
}

/* Click event handler. `obj` is the button that was clicked; the click
 * mixin invokes this on a press/release inside the widget. */
static struct yetty_ycore_void_result on_click(struct yetty_yclass_object *obj, void *userdata)
{
    (void)userdata;
    return yetty_ygui_button_set_label(obj, "Clicked!");
}

static struct yetty_ycore_void_result build(struct demo_runner *runner,
                                            struct yetty_yclass_object *root)
{
    (void)runner;

    struct yetty_yclass_object_ptr_result br =
        yetty_ygui_widget_add(root, yetty_ygui_button_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "button");
    struct yetty_yclass_object *button = br.value;

    err_ok(yetty_ygui_button_set_label(button, "Click me"));

    struct yetty_ygui_layout_const_ptr_result layout_res = yetty_ygui_widget_layout_get(button);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "00_simple: layout_get");
    struct yetty_ygui_layout l = *layout_res.value;
    l.width = 200;
    l.height = 40;
    err_ok(yetty_ygui_widget_layout_set(button, &l));

    return yetty_ygui_clickable_on_click_set(button, on_click, NULL);
}

int main(int argc, char **argv)
{
    return demo_runner_run(argc, argv, "00_simple", build);
}
