/*
 * Demo 14_todo_list: Todo list — checkbox rows.
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
    const char *items[] = {"Buy milk", "Walk dog", "Write demo", "Ship code"};
    for (size_t i = 0; i < sizeof(items) / sizeof(items[0]); ++i) {
        struct yetty_yclass_object_ptr_result r =
            yetty_ygui_widget_add(root, yetty_ygui_checkbox_class_get().value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "todo");
        err_ok(yetty_ygui_checkbox_set_label(r.value, items[i]));
        struct yetty_yclass_object *w = r.value;
        {
            struct yetty_ygui_layout_const_ptr_result layout_res =
                yetty_ygui_widget_layout_get(w);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "14_todo_list: layout_get");
            struct yetty_ygui_layout l = *layout_res.value;
            l.height = 28;
            err_ok(yetty_ygui_widget_layout_set(w, &l));
        }
    }
    return YETTY_OK_VOID();
}

int main(int argc, char **argv)
{
    return demo_runner_run(argc, argv, "14_todo_list", build);
}
