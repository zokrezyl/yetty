/*
 * Demo 25_ypdf: ypdf — empty placeholder.
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

static struct yetty_ycore_void_result build(struct demo_runner *runner,
                                            struct yetty_yclass_object *root)
{
    (void)runner;
    struct yetty_yclass_object_ptr_result pr =
        yetty_ygui_widget_add(root, yetty_ygui_ypdf_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, pr, "ypdf");
    struct yetty_ygui_layout_const_ptr_result layout_res =
        yetty_ygui_widget_layout_get(pr.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "25_ypdf: layout_get");
    struct yetty_ygui_layout l = *layout_res.value;
    l.flex_grow = 1.0f;
    l.min_height = 200.0f;
    return yetty_ygui_widget_layout_set(pr.value, &l);
}

int main(int argc, char **argv)
{
    return demo_runner_run(argc, argv, "25_ypdf", build);
}
