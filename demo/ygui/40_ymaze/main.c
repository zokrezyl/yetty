/*
 * Demo 40_ymaze: ymaze — animated maze rendered via the ygui ydraw_embed
 * → ygrid path (same infrastructure as ymarkdown / yjungle).
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
    struct yetty_yclass_object_ptr_result mr =
        yetty_ygui_widget_add(root, yetty_ygui_ymaze_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, mr, "ymaze");
    struct yetty_ygui_layout_const_ptr_result layout_res =
        yetty_ygui_widget_layout_get(mr.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "40_ymaze: layout_get");
    struct yetty_ygui_layout l = *layout_res.value;
    l.flex_grow = 1.0f;
    l.min_height = 300.0f;
    return yetty_ygui_widget_layout_set(mr.value, &l);
}

int main(int argc, char **argv)
{
    return demo_runner_run(argc, argv, "40_ymaze", build);
}
