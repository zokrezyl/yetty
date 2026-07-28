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
struct [[clang::annotate("class@demoygui:19_flex_dashboard")]] [[clang::annotate(
    "parent@yguiapp:app")]] yetty_demoygui_19_flex_dashboard {
    int unused;
};

/* Result wrapper + class accessor forward-decls (this TU does not include its
 * own generated header; main.gen.c is #included at the foot). */
YETTY_YRESULT_DECLARE(yetty_demoygui_19_flex_dashboard_ptr,
                      struct yetty_demoygui_19_flex_dashboard *);
struct yetty_yclass_ptr_result yetty_demoygui_19_flex_dashboard_class_get(void);

[[clang::annotate("override@yguiapp:app:build")]]
static struct yetty_ycore_void_result build(struct yetty_yclass_object *app,
                                            struct yetty_yclass_object *root)
{
    (void)app;
    {
        struct yetty_yclass_object_ptr_result r =
            yetty_ygui_widget_add(root, yetty_ygui_label_class_get().value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "header");
        err_ok(yetty_ygui_label_set_text(r.value, "Header"));
        struct yetty_yclass_object *w = r.value;
        {
            struct yetty_ygui_layout_const_ptr_result layout_res = yetty_ygui_widget_layout_get(w);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "19_flex_dashboard: layout_get");
            struct yetty_ygui_layout l = *layout_res.value;
            l.height = 32;
            err_ok(yetty_ygui_widget_layout_set(w, &l));
        }
    }
    struct yetty_yclass_object_ptr_result mid =
        yetty_ygui_widget_add(root, yetty_ygui_hbox_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, mid, "mid row");
    {
        struct yetty_ygui_layout_const_ptr_result layout_res2 =
            yetty_ygui_widget_layout_get(mid.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res2, "19_flex_dashboard: layout_get");
        struct yetty_ygui_layout l = *layout_res2.value;
        l.flex_grow = 1.0f;
        l.gap = 8;
        err_ok(yetty_ygui_widget_layout_set(mid.value, &l));
    }
    for (int i = 0; i < 3; ++i) {
        struct yetty_yclass_object_ptr_result p =
            yetty_ygui_widget_add(mid.value, yetty_ygui_panel_class_get().value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, p, "pane");
        struct yetty_ygui_layout_const_ptr_result layout_res3 =
            yetty_ygui_widget_layout_get(p.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res3, "19_flex_dashboard: layout_get");
        struct yetty_ygui_layout l = *layout_res3.value;
        l.flex_grow = 1.0f;
        err_ok(yetty_ygui_widget_layout_set(p.value, &l));
    }
    {
        struct yetty_yclass_object_ptr_result r =
            yetty_ygui_widget_add(root, yetty_ygui_statusbar_class_get().value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "status");
        err_ok(yetty_ygui_statusbar_set_left(r.value, "Status"));
        struct yetty_yclass_object *w = r.value;
        {
            struct yetty_ygui_layout_const_ptr_result layout_res4 = yetty_ygui_widget_layout_get(w);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res4, "19_flex_dashboard: layout_get");
            struct yetty_ygui_layout l = *layout_res4.value;
            l.height = 24;
            err_ok(yetty_ygui_widget_layout_set(w, &l));
        }
    }
    return YETTY_OK_VOID();
}

int main(int argc, char **argv)
{
    return yetty_yguiapp_run_main(argc, argv, yetty_demoygui_19_flex_dashboard_class_get().value);
}

#include "yetty/gen/impl/demoygui/19_flex_dashboard/main.c"
