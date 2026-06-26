/*
 * Demo 13_color_mixer: Color mixer — R/G/B sliders.
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
    const char *channels[] = {"R", "G", "B"};
    for (size_t i = 0; i < 3; ++i) {
        struct yetty_yclass_object_ptr_result lr =
            yetty_ygui_widget_add(root, yetty_ygui_label_class_get().value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, lr, "label");
        err_ok(yetty_ygui_label_set_text(lr.value, channels[i]));
        {
            struct yetty_yclass_object *w = lr.value;
            {
                struct yetty_ygui_layout_const_ptr_result layout_res =
                    yetty_ygui_widget_layout_get(w);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "13_color_mixer: layout_get");
                struct yetty_ygui_layout l = *layout_res.value;
                l.height = 20;
                err_ok(yetty_ygui_widget_layout_set(w, &l));
            }
        }
        struct yetty_yclass_object_ptr_result sr =
            yetty_ygui_widget_add(root, yetty_ygui_slider_class_get().value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "slider");
        err_ok(yetty_ygui_slider_set_value(sr.value, 0.5f));
        {
            struct yetty_yclass_object *w = sr.value;
            {
                struct yetty_ygui_layout_const_ptr_result layout_res2 =
                    yetty_ygui_widget_layout_get(w);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res2, "13_color_mixer: layout_get");
                struct yetty_ygui_layout l = *layout_res2.value;
                l.height = 28;
                err_ok(yetty_ygui_widget_layout_set(w, &l));
            }
        }
    }
    return YETTY_OK_VOID();
}

int main(int argc, char **argv)
{
    return demo_runner_run(argc, argv, "13_color_mixer", build);
}
