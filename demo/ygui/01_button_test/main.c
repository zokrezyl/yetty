/*
 * Demo 01_button_test: Three buttons.
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
    {
        struct yetty_ygui_object_ptr_result lr =
            yetty_ygui_add(yetty_ygui_label_class_get().value, root);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, lr, "label");
        err_ok(yetty_ygui_label_set_text(lr.value, "Click any button"));
        struct yetty_ygui_object *w = lr.value;
    {
        struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(w);
        l.height = 24;
        err_ok(yetty_ygui_widget_layout_set(w, &l));
    }
    }
    for (int i = 0; i < 3; ++i) {
        struct yetty_ygui_object_ptr_result br =
            yetty_ygui_add(yetty_ygui_button_class_get().value, root);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "button");
        char buf[16];
        snprintf(buf, sizeof(buf), "Button %d", i + 1);
        err_ok(yetty_ygui_button_set_label(br.value, buf));
        struct yetty_ygui_object *w = br.value;
    {
        struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(w);
        l.height = 40;
        err_ok(yetty_ygui_widget_layout_set(w, &l));
    }
    }
    return YETTY_OK_VOID();
}

int main(int argc, char **argv)
{
    return demo_runner_run(argc, argv, "01_button_test", build);
}
