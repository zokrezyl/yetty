/*
 * Demo 00_simple: Simple — tabbar + yplot.
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
    struct yetty_yclass_object_ptr_result tbr =
        yetty_ygui_widget_add(root, yetty_ygui_tabbar_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, tbr, "tabbar");
    struct yetty_yclass_object *w = tbr.value;
    {
        struct yetty_ygui_layout_const_ptr_result layout_res = yetty_ygui_widget_layout_get(w);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "00_simple: layout_get");
        struct yetty_ygui_layout l = *layout_res.value;
        l.height = 36;
        l.gap = 4;
        err_ok(yetty_ygui_widget_layout_set(w, &l));
    }
    struct yetty_yclass_object_ptr_result hr = yetty_ygui_tabbar_add_tab(tbr.value, "Plot");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, hr, "tabbar add_tab");

    struct yetty_yclass_object_ptr_result pr =
        yetty_ygui_widget_add(root, yetty_ygui_yplot_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, pr, "yplot");
    {
        struct yetty_ygui_layout_const_ptr_result layout_res2 =
            yetty_ygui_widget_layout_get(pr.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res2, "00_simple: layout_get");
        struct yetty_ygui_layout l = *layout_res2.value;
        l.flex_grow = 1.0f;
        l.min_height = 240.0f;
        err_ok(yetty_ygui_widget_layout_set(pr.value, &l));
    }
    return yetty_ygui_yplot_set_source(pr.value, "f = sin(x)");
}

int main(int argc, char **argv)
{
    return demo_runner_run(argc, argv, "00_simple", build);
}
