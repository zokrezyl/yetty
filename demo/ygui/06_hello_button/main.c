/*
 * Demo 06_hello_button: Hello — single button.
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
    struct yetty_yclass_object_ptr_result br =
        yetty_ygui_widget_add(root, yetty_ygui_button_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "button");
    err_ok(yetty_ygui_button_set_label(br.value, "Hello"));
    struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(br.value);
    l.width = 200;
    l.height = 40;
    return yetty_ygui_widget_layout_set(br.value, &l);
}

int main(int argc, char **argv)
{
    return demo_runner_run(argc, argv, "06_hello_button", build);
}
