/*
 * Demo 34_textarea: Textarea — multi-line input.
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
    struct yetty_yclass_object_ptr_result tr =
        yetty_ygui_widget_add(root, yetty_ygui_textarea_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, tr, "textarea");
    struct yetty_ygui_layout_const_ptr_result layout_res =
        yetty_ygui_widget_layout_get(tr.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "34_textarea: layout_get");
    struct yetty_ygui_layout l = *layout_res.value;
    l.flex_grow = 1.0f;
    l.min_height = 200.0f;
    err_ok(yetty_ygui_widget_layout_set(tr.value, &l));
    return yetty_ygui_textarea_set_text(tr.value,
                                        "Type here.\nMulti-line input via the textarea widget.");
}

int main(int argc, char **argv)
{
    return demo_runner_run(argc, argv, "34_textarea", build);
}
