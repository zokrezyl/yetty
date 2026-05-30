/*
 * Demo 23_rich_tabbar: Rich text — multi-style spans.
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
    struct yetty_ygui_object_ptr_result rr =
        yetty_ygui_add(yetty_ygui_rich_class_get().value, root);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "rich");
    err_ok(yetty_ygui_rich_add_line(rr.value));
    err_ok(yetty_ygui_rich_add_span(rr.value, "Bold",   16.0f, 0xFFFFFFFFu));
    err_ok(yetty_ygui_rich_add_span(rr.value, " accent ", 16.0f, 0xFF92A86Bu));
    err_ok(yetty_ygui_rich_add_span(rr.value, "muted",  16.0f, 0xFFA8A79Fu));
    struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(rr.value);
    l.flex_grow = 1.0f;
    l.min_height = 64.0f;
    return yetty_ygui_widget_layout_set(rr.value, &l);
}

int main(int argc, char **argv)
{
    return demo_runner_run(argc, argv, "23_rich_tabbar", build);
}
