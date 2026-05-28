/*
 * Demo 22_tree_complex: Tree complex — nested levels.
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
    struct yetty_ygui_object_ptr_result a =
        yetty_ygui_add(yetty_ygui_tree_node_class_get().value, root);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, a, "a");
    err_ok(yetty_ygui_tree_node_set_label(a.value, "project/"));
    err_ok(yetty_ygui_tree_node_set_open(a.value, 1));
    {
        struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(a.value);
        l.flex_grow = 1.0f;
        l.min_height = 240.0f;
        err_ok(yetty_ygui_widget_layout_set(a.value, &l));
    }
    {
        struct yetty_ygui_object_ptr_result b =
            yetty_ygui_add(yetty_ygui_tree_node_class_get().value, a.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, b, "b");
        err_ok(yetty_ygui_tree_node_set_label(b.value, "src/"));
        err_ok(yetty_ygui_tree_node_set_open(b.value, 1));
        { struct yetty_ygui_object *w = b.value;
    {
        struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(w);
        l.height = 120;
        err_ok(yetty_ygui_widget_layout_set(w, &l));
    } }
        struct yetty_ygui_object_ptr_result c =
            yetty_ygui_add(yetty_ygui_label_class_get().value, b.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, c, "c");
        err_ok(yetty_ygui_label_set_text(c.value, "main.c"));
        { struct yetty_ygui_object *w = c.value;
    {
        struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(w);
        l.height = 24;
        err_ok(yetty_ygui_widget_layout_set(w, &l));
    } }
    }
    {
        struct yetty_ygui_object_ptr_result b =
            yetty_ygui_add(yetty_ygui_label_class_get().value, a.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, b, "readme");
        err_ok(yetty_ygui_label_set_text(b.value, "README.md"));
        struct yetty_ygui_object *w = b.value;
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
    return demo_runner_run(argc, argv, "22_tree_complex", build);
}
