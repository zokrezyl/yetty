/*
 * Demo 16_new_widgets: New widgets — chip / breadcrumbs / stepper.
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
struct [[clang::annotate("class@demoygui:16_new_widgets")]] [[clang::annotate(
    "parent@yguiapp:app")]] yetty_demoygui_16_new_widgets {
    int unused;
};

/* Result wrapper + class accessor forward-decls (this TU does not include its
 * own generated header; main.gen.c is #included at the foot). */
YETTY_YRESULT_DECLARE(yetty_demoygui_16_new_widgets_ptr, struct yetty_demoygui_16_new_widgets *);
struct yetty_yclass_ptr_result yetty_demoygui_16_new_widgets_class_get(void);

[[clang::annotate("override@yguiapp:app:build")]]
static struct yetty_ycore_void_result build(struct yetty_yclass_object *app,
                                            struct yetty_yclass_object *root)
{
    (void)app;
    {
        struct yetty_yclass_object_ptr_result r =
            yetty_ygui_widget_add(root, yetty_ygui_chip_class_get().value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "chip");
        err_ok(yetty_ygui_chip_set_label(r.value, "tag"));
        struct yetty_yclass_object *w = r.value;
        {
            struct yetty_ygui_layout_const_ptr_result layout_res = yetty_ygui_widget_layout_get(w);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "16_new_widgets: layout_get");
            struct yetty_ygui_layout l = *layout_res.value;
            l.height = 24;
            err_ok(yetty_ygui_widget_layout_set(w, &l));
        }
    }
    {
        struct yetty_yclass_object_ptr_result r =
            yetty_ygui_widget_add(root, yetty_ygui_breadcrumbs_class_get().value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "bc");
        err_ok(yetty_ygui_breadcrumbs_add(r.value, "Home"));
        err_ok(yetty_ygui_breadcrumbs_add(r.value, "Settings"));
        struct yetty_yclass_object *w = r.value;
        {
            struct yetty_ygui_layout_const_ptr_result layout_res2 = yetty_ygui_widget_layout_get(w);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res2, "16_new_widgets: layout_get");
            struct yetty_ygui_layout l = *layout_res2.value;
            l.height = 24;
            err_ok(yetty_ygui_widget_layout_set(w, &l));
        }
    }
    {
        struct yetty_yclass_object_ptr_result r =
            yetty_ygui_widget_add(root, yetty_ygui_stepper_class_get().value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "stepper");
        err_ok(yetty_ygui_stepper_add_step(r.value, "Start"));
        err_ok(yetty_ygui_stepper_add_step(r.value, "Configure"));
        err_ok(yetty_ygui_stepper_add_step(r.value, "Finish"));
        err_ok(yetty_ygui_stepper_set_current(r.value, 1));
        struct yetty_yclass_object *w = r.value;
        {
            struct yetty_ygui_layout_const_ptr_result layout_res3 = yetty_ygui_widget_layout_get(w);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res3, "16_new_widgets: layout_get");
            struct yetty_ygui_layout l = *layout_res3.value;
            l.height = 56;
            err_ok(yetty_ygui_widget_layout_set(w, &l));
        }
    }
    return YETTY_OK_VOID();
}

int main(int argc, char **argv)
{
    return yetty_yguiapp_run_main(argc, argv, yetty_demoygui_16_new_widgets_class_get().value);
}

#include "yetty/gen/impl/demoygui/16_new_widgets/main.c"
