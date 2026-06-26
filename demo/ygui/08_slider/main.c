/*
 * Demo 08_slider: Slider.
 *
 * Standalone-mode ygui demo. The runner brings up window + GPU +
 * receiver-side container; this file only populates the widget tree.
 * Press 'q' (or Ctrl-C / Ctrl-D) to quit.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../runner.h"
#include <yetty/ygui/ygui.h>

static inline void err_ok(struct yetty_ycore_void_result r)
{
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
    }
}

static struct yetty_ycore_void_result build(struct demo_runner *runner,
                                            struct yetty_yclass_object *root)
{
    (void)runner;
    struct yetty_yclass_object_ptr_result sr =
        yetty_ygui_widget_add(root, yetty_ygui_slider_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "slider");
    err_ok(yetty_ygui_slider_set_value(sr.value, 0.25f));
    struct yetty_ygui_layout_const_ptr_result layout_res =
        yetty_ygui_widget_layout_get(sr.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "08_slider: layout_get");
    struct yetty_ygui_layout l = *layout_res.value;
    l.height = 32;
    return yetty_ygui_widget_layout_set(sr.value, &l);
}

int main(int argc, char **argv)
{
    return demo_runner_run(argc, argv, "08_slider", build);
}
