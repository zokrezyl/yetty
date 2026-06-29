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
struct [[clang::annotate("class@demoygui:10_panel_layout")]] [[clang::annotate(
    "parent@yguiapp:app")]] yetty_demoygui_10_panel_layout {
    int unused;
};

/* Result wrapper + class accessor forward-decls (this TU does not include its
 * own generated header; main.gen.c is #included at the foot). */
YETTY_YRESULT_DECLARE(yetty_demoygui_10_panel_layout_ptr, struct yetty_demoygui_10_panel_layout *);
struct yetty_yclass_ptr_result yetty_demoygui_10_panel_layout_class_get(void);

[[clang::annotate("override@yguiapp:app:build")]]
static struct yetty_ycore_void_result build(struct yetty_yclass_object *app,
                                            struct yetty_yclass_object *root)
{
    (void)app;
    struct yetty_yclass_object_ptr_result pr =
        yetty_ygui_widget_add(root, yetty_ygui_panel_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, pr, "panel");
    {
        struct yetty_ygui_layout_const_ptr_result layout_res =
            yetty_ygui_widget_layout_get(pr.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "10_panel_layout: layout_get");
        struct yetty_ygui_layout l = *layout_res.value;
        l.flex_grow = 1.0f;
        l.padding_left = l.padding_right = 16;
        l.padding_top = l.padding_bottom = 16;
        l.gap = 8;
        err_ok(yetty_ygui_widget_layout_set(pr.value, &l));
    }
    for (int i = 0; i < 3; ++i) {
        struct yetty_yclass_object_ptr_result lr =
            yetty_ygui_widget_add(pr.value, yetty_ygui_label_class_get().value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, lr, "label");
        char buf[24];
        snprintf(buf, sizeof(buf), "Row %d", i + 1);
        err_ok(yetty_ygui_label_set_text(lr.value, buf));
        struct yetty_yclass_object *w = lr.value;
        {
            struct yetty_ygui_layout_const_ptr_result layout_res2 = yetty_ygui_widget_layout_get(w);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res2, "10_panel_layout: layout_get");
            struct yetty_ygui_layout l = *layout_res2.value;
            l.height = 24;
            err_ok(yetty_ygui_widget_layout_set(w, &l));
        }
    }
    return YETTY_OK_VOID();
}

int main(int argc, char **argv)
{
    return yetty_yguiapp_run_main(argc, argv, yetty_demoygui_10_panel_layout_class_get().value);
}

#include "main.gen.c"
