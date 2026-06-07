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

#include "../runner.h"
#include <yetty/ygui/ygui.h>

static inline void err_ok(struct yetty_ycore_void_result r)
{
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
    }
}

static struct yetty_ycore_void_result build(struct demo_runner *runner,
                                            struct yetty_ygui_object *root)
{
    (void)runner;
    {
        struct yetty_ygui_object_ptr_result r =
            yetty_ygui_add(yetty_ygui_chip_class_get().value, root);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "chip");
        err_ok(yetty_ygui_chip_set_label(r.value, "tag"));
        struct yetty_ygui_object *w = r.value;
        {
            struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(w);
            l.height = 24;
            err_ok(yetty_ygui_widget_layout_set(w, &l));
        }
    }
    {
        struct yetty_ygui_object_ptr_result r =
            yetty_ygui_add(yetty_ygui_breadcrumbs_class_get().value, root);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "bc");
        err_ok(yetty_ygui_breadcrumbs_add(r.value, "Home"));
        err_ok(yetty_ygui_breadcrumbs_add(r.value, "Settings"));
        struct yetty_ygui_object *w = r.value;
        {
            struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(w);
            l.height = 24;
            err_ok(yetty_ygui_widget_layout_set(w, &l));
        }
    }
    {
        struct yetty_ygui_object_ptr_result r =
            yetty_ygui_add(yetty_ygui_stepper_class_get().value, root);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "stepper");
        err_ok(yetty_ygui_stepper_add_step(r.value, "Start"));
        err_ok(yetty_ygui_stepper_add_step(r.value, "Configure"));
        err_ok(yetty_ygui_stepper_add_step(r.value, "Finish"));
        err_ok(yetty_ygui_stepper_set_current(r.value, 1));
        struct yetty_ygui_object *w = r.value;
        {
            struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(w);
            l.height = 56;
            err_ok(yetty_ygui_widget_layout_set(w, &l));
        }
    }
    return YETTY_OK_VOID();
}

int main(int argc, char **argv)
{
    return demo_runner_run(argc, argv, "16_new_widgets", build);
}
