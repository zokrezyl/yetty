/*
 * Demo 21_tree_with_panes: Tree + panes — splitter layout.
 *
 * Standalone-mode ygui demo. The runner brings up window + GPU +
 * receiver-side container; this file only populates the widget tree.
 * Press 'q' (or Ctrl-C / Ctrl-D) to quit.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/yguiapp/app.h>
#include <yetty/yguiapp/run.h>
#include <yetty/ygui/ygui.h>

static inline void err_ok(struct yetty_ycore_void_result r)
{
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
    }
}

/* Demo app class: a yguiapp:app subclass with no extra state. */
struct [[clang::annotate("class@demoygui:21_tree_with_panes")]] [[clang::annotate(
    "parent@yguiapp:app")]] yetty_demoygui_21_tree_with_panes {
    int unused;
};

/* Result wrapper + class accessor forward-decls (this TU does not include its
 * own generated header; main.gen.c is #included at the foot). */
YETTY_YRESULT_DECLARE(yetty_demoygui_21_tree_with_panes_ptr,
                      struct yetty_demoygui_21_tree_with_panes *);
struct yetty_yclass_ptr_result yetty_demoygui_21_tree_with_panes_class_get(void);

[[clang::annotate("override@yguiapp:app:build")]]
static struct yetty_ycore_void_result build(struct yetty_yclass_object *app,
                                            struct yetty_yclass_object *root)
{
    (void)app;
    {
        struct yetty_ygui_layout_const_ptr_result layout_res = yetty_ygui_widget_layout_get(root);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "21_tree_with_panes: layout_get");
        struct yetty_ygui_layout l = *layout_res.value;
        l.direction = YETTY_YGUI_FLEX_ROW;
        err_ok(yetty_ygui_widget_layout_set(root, &l));
    }
    struct yetty_yclass_object_ptr_result tr =
        yetty_ygui_widget_add(root, yetty_ygui_tree_node_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, tr, "tree");
    err_ok(yetty_ygui_tree_node_set_label(tr.value, "files/"));
    err_ok(yetty_ygui_tree_node_set_open(tr.value, 1));
    {
        struct yetty_ygui_layout_const_ptr_result layout_res =
            yetty_ygui_widget_layout_get(tr.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "21_tree_with_panes: layout_get");
        struct yetty_ygui_layout l = *layout_res.value;
        l.width = 220;
        err_ok(yetty_ygui_widget_layout_set(tr.value, &l));
    }
    struct yetty_yclass_object_ptr_result sp =
        yetty_ygui_widget_add(root, yetty_ygui_splitter_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sp, "splitter");
    {
        struct yetty_ygui_layout_const_ptr_result layout_res =
            yetty_ygui_widget_layout_get(sp.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "21_tree_with_panes: layout_get");
        struct yetty_ygui_layout l = *layout_res.value;
        l.width = 4;
        err_ok(yetty_ygui_widget_layout_set(sp.value, &l));
    }
    struct yetty_yclass_object_ptr_result body =
        yetty_ygui_widget_add(root, yetty_ygui_panel_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, body, "body");
    struct yetty_ygui_layout_const_ptr_result layout_res = yetty_ygui_widget_layout_get(body.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "21_tree_with_panes: layout_get");
    struct yetty_ygui_layout l = *layout_res.value;
    l.flex_grow = 1.0f;
    return yetty_ygui_widget_layout_set(body.value, &l);
}

int main(int argc, char **argv)
{
    return yetty_yguiapp_run_main(argc, argv, yetty_demoygui_21_tree_with_panes_class_get().value);
}

#include "yetty/gen/impl/demoygui/21_tree_with_panes/main.c"
