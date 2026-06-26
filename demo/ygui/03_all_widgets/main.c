/*
 * Demo 03_all_widgets: All widgets — one of each.
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
    struct yetty_yclass_object_ptr_result r;

    r = yetty_ygui_widget_add(root, yetty_ygui_label_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "label");
    err_ok(yetty_ygui_label_set_text(r.value, "Label"));
    {
        struct yetty_yclass_object *w = r.value;
        {
            struct yetty_ygui_layout_const_ptr_result layout_res =
                yetty_ygui_widget_layout_get(w);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "03_all_widgets: layout_get");
            struct yetty_ygui_layout l = *layout_res.value;
            l.height = 24;
            err_ok(yetty_ygui_widget_layout_set(w, &l));
        }
    }

    r = yetty_ygui_widget_add(root, yetty_ygui_button_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "button");
    err_ok(yetty_ygui_button_set_label(r.value, "Button"));
    {
        struct yetty_yclass_object *w = r.value;
        {
            struct yetty_ygui_layout_const_ptr_result layout_res2 =
                yetty_ygui_widget_layout_get(w);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res2, "03_all_widgets: layout_get");
            struct yetty_ygui_layout l = *layout_res2.value;
            l.height = 32;
            err_ok(yetty_ygui_widget_layout_set(w, &l));
        }
    }

    r = yetty_ygui_widget_add(root, yetty_ygui_checkbox_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "checkbox");
    err_ok(yetty_ygui_checkbox_set_label(r.value, "Checkbox"));
    {
        struct yetty_yclass_object *w = r.value;
        {
            struct yetty_ygui_layout_const_ptr_result layout_res3 =
                yetty_ygui_widget_layout_get(w);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res3, "03_all_widgets: layout_get");
            struct yetty_ygui_layout l = *layout_res3.value;
            l.height = 24;
            err_ok(yetty_ygui_widget_layout_set(w, &l));
        }
    }

    r = yetty_ygui_widget_add(root, yetty_ygui_slider_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "slider");
    err_ok(yetty_ygui_slider_set_value(r.value, 0.5f));
    {
        struct yetty_yclass_object *w = r.value;
        {
            struct yetty_ygui_layout_const_ptr_result layout_res4 =
                yetty_ygui_widget_layout_get(w);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res4, "03_all_widgets: layout_get");
            struct yetty_ygui_layout l = *layout_res4.value;
            l.height = 24;
            err_ok(yetty_ygui_widget_layout_set(w, &l));
        }
    }

    r = yetty_ygui_widget_add(root, yetty_ygui_progress_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "progress");
    err_ok(yetty_ygui_progress_set_value(r.value, 0.6f));
    {
        struct yetty_yclass_object *w = r.value;
        {
            struct yetty_ygui_layout_const_ptr_result layout_res5 =
                yetty_ygui_widget_layout_get(w);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res5, "03_all_widgets: layout_get");
            struct yetty_ygui_layout l = *layout_res5.value;
            l.height = 14;
            err_ok(yetty_ygui_widget_layout_set(w, &l));
        }
    }
    return YETTY_OK_VOID();
}

int main(int argc, char **argv)
{
    return demo_runner_run(argc, argv, "03_all_widgets", build);
}
