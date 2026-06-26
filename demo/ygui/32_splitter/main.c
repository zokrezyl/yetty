/*
 * Demo 32_splitter: Splitter — resizable divider.
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
        struct yetty_ygui_layout_const_ptr_result layout_res =
            yetty_ygui_widget_layout_get(root);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "32_splitter: layout_get");
        struct yetty_ygui_layout l = *layout_res.value;
        l.direction = YETTY_YGUI_FLEX_ROW;
        err_ok(yetty_ygui_widget_layout_set(root, &l));
    }
    {
        struct yetty_yclass_object_ptr_result p =
            yetty_ygui_widget_add(root, yetty_ygui_panel_class_get().value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, p, "left");
        struct yetty_ygui_layout_const_ptr_result layout_res =
            yetty_ygui_widget_layout_get(p.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "32_splitter: layout_get");
        struct yetty_ygui_layout l = *layout_res.value;
        l.flex_grow = 1.0f;
        err_ok(yetty_ygui_widget_layout_set(p.value, &l));
    }
    {
        struct yetty_yclass_object_ptr_result s =
            yetty_ygui_widget_add(root, yetty_ygui_splitter_class_get().value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, s, "splitter");
        struct yetty_ygui_layout_const_ptr_result layout_res =
            yetty_ygui_widget_layout_get(s.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "32_splitter: layout_get");
        struct yetty_ygui_layout l = *layout_res.value;
        l.width = 4;
        err_ok(yetty_ygui_widget_layout_set(s.value, &l));
    }
    {
        struct yetty_yclass_object_ptr_result p =
            yetty_ygui_widget_add(root, yetty_ygui_panel_class_get().value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, p, "right");
        struct yetty_ygui_layout_const_ptr_result layout_res =
            yetty_ygui_widget_layout_get(p.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "32_splitter: layout_get");
        struct yetty_ygui_layout l = *layout_res.value;
        l.flex_grow = 1.0f;
        err_ok(yetty_ygui_widget_layout_set(p.value, &l));
    }
    return YETTY_OK_VOID();
}

int main(int argc, char **argv)
{
    return demo_runner_run(argc, argv, "32_splitter", build);
}
