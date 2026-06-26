/*
 * Demo 29_yimage: yimage — placeholder.
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
    struct yetty_yclass_object_ptr_result ir =
        yetty_ygui_widget_add(root, yetty_ygui_yimage_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ir, "yimage");
    struct yetty_ygui_layout_const_ptr_result layout_res =
        yetty_ygui_widget_layout_get(ir.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "29_yimage: layout_get");
    struct yetty_ygui_layout l = *layout_res.value;
    l.flex_grow = 1.0f;
    l.min_height = 200.0f;
    return yetty_ygui_widget_layout_set(ir.value, &l);
}

int main(int argc, char **argv)
{
    return demo_runner_run(argc, argv, "29_yimage", build);
}
