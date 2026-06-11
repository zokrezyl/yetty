/*
 * Demo 04_edge_test: Edge test — panel stretched to viewport.
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
        yetty_ygui_widget_add(root, yetty_ygui_panel_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, pr, "panel");
    struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(pr.value);
    l.flex_grow = 1.0f;
    return yetty_ygui_widget_layout_set(pr.value, &l);
}

int main(int argc, char **argv)
{
    return demo_runner_run(argc, argv, "04_edge_test", build);
}
