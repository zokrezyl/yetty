/*
 * Demo 24_ymarkdown: ymarkdown — render markdown source.
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
    if (YETTY_IS_ERR(r)) yetty_ycore_error_destroy(r.error);
}

static struct yetty_ycore_void_result build(struct demo_runner *runner,
                                            struct yetty_ygui_object *root)
{
    (void)runner;
    struct yetty_ygui_object_ptr_result mr =
        yetty_ygui_add(yetty_ygui_ymarkdown_class_get(), root);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, mr, "ymarkdown");
    struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(mr.value);
    l.flex_grow = 1.0f;
    l.min_height = 200.0f;
    err_ok(yetty_ygui_widget_layout_set(mr.value, &l));
    const char *src = "# Hello\n\nThis is **markdown** rendered by ygui.\n";
    return yetty_ygui_ymarkdown_set_source(mr.value, src, strlen(src));
}

int main(int argc, char **argv)
{
    return demo_runner_run(argc, argv, "24_ymarkdown", build);
}
