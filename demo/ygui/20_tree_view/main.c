/*
 * Demo 20_tree_view: Tree view — node with two children.
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
    struct yetty_yclass_object_ptr_result tr =
        yetty_ygui_widget_add(root, yetty_ygui_tree_node_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, tr, "tree");
    err_ok(yetty_ygui_tree_node_set_label(tr.value, "src/"));
    err_ok(yetty_ygui_tree_node_set_open(tr.value, 1));
    {
        struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(tr.value);
        l.flex_grow = 1.0f;
        l.min_height = 200.0f;
        err_ok(yetty_ygui_widget_layout_set(tr.value, &l));
    }
    {
        struct yetty_yclass_object_ptr_result c =
            yetty_ygui_widget_add(tr.value, yetty_ygui_label_class_get().value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, c, "child");
        err_ok(yetty_ygui_label_set_text(c.value, "main.c"));
        struct yetty_yclass_object *w = c.value;
        {
            struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(w);
            l.height = 24;
            err_ok(yetty_ygui_widget_layout_set(w, &l));
        }
    }
    {
        struct yetty_yclass_object_ptr_result c =
            yetty_ygui_widget_add(tr.value, yetty_ygui_label_class_get().value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, c, "child");
        err_ok(yetty_ygui_label_set_text(c.value, "utils.c"));
        struct yetty_yclass_object *w = c.value;
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
    return demo_runner_run(argc, argv, "20_tree_view", build);
}
