/*
 * Demo 15_dashboard: Dashboard — header / body / status.
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
    {
        struct yetty_yclass_object_ptr_result r =
            yetty_ygui_widget_add(root, yetty_ygui_label_class_get().value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "header");
        err_ok(yetty_ygui_label_set_text(r.value, "Dashboard"));
        struct yetty_yclass_object *w = r.value;
        {
            struct yetty_ygui_layout_const_ptr_result layout_res = yetty_ygui_widget_layout_get(w);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "15_dashboard: layout_get");
            struct yetty_ygui_layout l = *layout_res.value;
            l.height = 32;
            err_ok(yetty_ygui_widget_layout_set(w, &l));
        }
    }
    struct yetty_yclass_object_ptr_result bp =
        yetty_ygui_widget_add(root, yetty_ygui_panel_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, bp, "body");
    {
        struct yetty_ygui_layout_const_ptr_result layout_res2 =
            yetty_ygui_widget_layout_get(bp.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res2, "15_dashboard: layout_get");
        struct yetty_ygui_layout l = *layout_res2.value;
        l.flex_grow = 1.0f;
        err_ok(yetty_ygui_widget_layout_set(bp.value, &l));
    }
    {
        struct yetty_yclass_object_ptr_result r =
            yetty_ygui_widget_add(root, yetty_ygui_statusbar_class_get().value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "status");
        err_ok(yetty_ygui_statusbar_set_left(r.value, "Ready"));
        struct yetty_yclass_object *w = r.value;
        {
            struct yetty_ygui_layout_const_ptr_result layout_res3 = yetty_ygui_widget_layout_get(w);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res3, "15_dashboard: layout_get");
            struct yetty_ygui_layout l = *layout_res3.value;
            l.height = 24;
            err_ok(yetty_ygui_widget_layout_set(w, &l));
        }
    }
    return YETTY_OK_VOID();
}

int main(int argc, char **argv)
{
    return demo_runner_run(argc, argv, "15_dashboard", build);
}
