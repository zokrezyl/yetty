/*
 * Demo 33_dialog: Dialog — open centered.
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
                                            struct yetty_ygui_object *root)
{
    (void)runner;
    struct yetty_ygui_object_ptr_result br =
        yetty_ygui_add(yetty_ygui_button_class_get().value, root);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "trigger");
    err_ok(yetty_ygui_button_set_label(br.value, "Dialog is open"));
    {
        struct yetty_ygui_object *w = br.value;
        {
            struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(w);
            l.height = 32;
            err_ok(yetty_ygui_widget_layout_set(w, &l));
        }
    }
    struct yetty_ygui_object_ptr_result dr =
        yetty_ygui_add(yetty_ygui_dialog_class_get().value, root);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dr, "dialog");
    err_ok(yetty_ygui_dialog_set_title(dr.value, "Hello"));
    {
        struct yetty_ygui_object_ptr_result l =
            yetty_ygui_add(yetty_ygui_label_class_get().value, dr.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, l, "body");
        err_ok(yetty_ygui_label_set_text(l.value, "Dialog body — press q to quit"));
    }
    return yetty_ygui_dialog_open_at(dr.value, 200, 150, 400, 200);
}

int main(int argc, char **argv)
{
    return demo_runner_run(argc, argv, "33_dialog", build);
}
