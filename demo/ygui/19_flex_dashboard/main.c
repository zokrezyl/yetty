/*
 * Demo 19_flex_dashboard: Flex dashboard — header + row + status.
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
    {
        struct yetty_ygui_object_ptr_result r =
            yetty_ygui_add(yetty_ygui_label_class_get(), root);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "header");
        err_ok(yetty_ygui_label_set_text(r.value, "Header"));
        struct yetty_ygui_object *w = r.value;
    {
        struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(w);
        l.height = 32;
        err_ok(yetty_ygui_widget_layout_set(w, &l));
    }
    }
    struct yetty_ygui_object_ptr_result mid =
        yetty_ygui_add(yetty_ygui_hbox_class_get(), root);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, mid, "mid row");
    {
        struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(mid.value);
        l.flex_grow = 1.0f;
        l.gap = 8;
        err_ok(yetty_ygui_widget_layout_set(mid.value, &l));
    }
    for (int i = 0; i < 3; ++i) {
        struct yetty_ygui_object_ptr_result p =
            yetty_ygui_add(yetty_ygui_panel_class_get(), mid.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, p, "pane");
        struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(p.value);
        l.flex_grow = 1.0f;
        err_ok(yetty_ygui_widget_layout_set(p.value, &l));
    }
    {
        struct yetty_ygui_object_ptr_result r =
            yetty_ygui_add(yetty_ygui_statusbar_class_get(), root);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "status");
        err_ok(yetty_ygui_statusbar_set_left(r.value, "Status"));
        struct yetty_ygui_object *w = r.value;
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
    return demo_runner_run(argc, argv, "19_flex_dashboard", build);
}
