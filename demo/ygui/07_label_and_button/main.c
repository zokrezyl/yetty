/*
 * Demo 07_label_and_button: Label + button.
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
    {
        struct yetty_yclass_object_ptr_result r =
            yetty_ygui_widget_add(root, yetty_ygui_label_class_get().value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "label");
        err_ok(yetty_ygui_label_set_text(r.value, "A label above a button"));
        struct yetty_yclass_object *w = r.value;
        {
            struct yetty_ygui_layout_const_ptr_result layout_res = yetty_ygui_widget_layout_get(w);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "07_label_and_button: layout_get");
            struct yetty_ygui_layout l = *layout_res.value;
            l.height = 24;
            err_ok(yetty_ygui_widget_layout_set(w, &l));
        }
    }
    struct yetty_yclass_object_ptr_result br =
        yetty_ygui_widget_add(root, yetty_ygui_button_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "button");
    err_ok(yetty_ygui_button_set_label(br.value, "OK"));
    struct yetty_ygui_layout_const_ptr_result layout_res2 = yetty_ygui_widget_layout_get(br.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res2, "07_label_and_button: layout_get");
    struct yetty_ygui_layout l = *layout_res2.value;
    l.height = 32;
    return yetty_ygui_widget_layout_set(br.value, &l);
}

int main(int argc, char **argv)
{
    return demo_runner_run(argc, argv, "07_label_and_button", build);
}
