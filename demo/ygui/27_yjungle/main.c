/*
 * Demo 27_yjungle: yjungle — placeholder.
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
    struct yetty_ygui_object_ptr_result jr =
        yetty_ygui_add(yetty_ygui_yjungle_class_get().value, root);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, jr, "yjungle");
    struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(jr.value);
    l.flex_grow = 1.0f;
    l.min_height = 200.0f;
    return yetty_ygui_widget_layout_set(jr.value, &l);
}

int main(int argc, char **argv)
{
    return demo_runner_run(argc, argv, "27_yjungle", build);
}
