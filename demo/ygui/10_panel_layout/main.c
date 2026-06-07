/*
 * Demo 10_panel_layout: Panel layout — nested rows.
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
    struct yetty_ygui_object_ptr_result pr =
        yetty_ygui_add(yetty_ygui_panel_class_get().value, root);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, pr, "panel");
    {
        struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(pr.value);
        l.flex_grow = 1.0f;
        l.padding_left = l.padding_right = 16;
        l.padding_top = l.padding_bottom = 16;
        l.gap = 8;
        err_ok(yetty_ygui_widget_layout_set(pr.value, &l));
    }
    for (int i = 0; i < 3; ++i) {
        struct yetty_ygui_object_ptr_result lr =
            yetty_ygui_add(yetty_ygui_label_class_get().value, pr.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, lr, "label");
        char buf[24];
        snprintf(buf, sizeof(buf), "Row %d", i + 1);
        err_ok(yetty_ygui_label_set_text(lr.value, buf));
        struct yetty_ygui_object *w = lr.value;
        {
            struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(w);
            l.height = 24;
            err_ok(yetty_ygui_widget_layout_set(w, &l));
        }
    }
    return YETTY_OK_VOID();
}

int main(int argc, char **argv)
{
    return demo_runner_run(argc, argv, "10_panel_layout", build);
}
