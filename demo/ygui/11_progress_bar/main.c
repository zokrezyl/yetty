/*
 * Demo 11_progress_bar: Progress bars.
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
    float values[] = {0.1f, 0.4f, 0.75f, 1.0f};
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        struct yetty_ygui_object_ptr_result r =
            yetty_ygui_add(yetty_ygui_progress_class_get().value, root);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "progress");
        err_ok(yetty_ygui_progress_set_value(r.value, values[i]));
        struct yetty_ygui_object *w = r.value;
        {
            struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(w);
            l.height = 14;
            err_ok(yetty_ygui_widget_layout_set(w, &l));
        }
    }
    return YETTY_OK_VOID();
}

int main(int argc, char **argv)
{
    return demo_runner_run(argc, argv, "11_progress_bar", build);
}
