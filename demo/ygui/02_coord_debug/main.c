/*
 * Demo 02_coord_debug: Coordinate debug — three flexed labels.
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
    const char *labels[] = {"top", "middle", "bottom"};
    for (size_t i = 0; i < sizeof(labels) / sizeof(labels[0]); ++i) {
        struct yetty_ygui_object_ptr_result lr =
            yetty_ygui_add(yetty_ygui_label_class_get().value, root);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, lr, "label");
        err_ok(yetty_ygui_label_set_text(lr.value, labels[i]));
        struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(lr.value);
        l.flex_grow = 1.0f;
        l.min_height = 24.0f;
        err_ok(yetty_ygui_widget_layout_set(lr.value, &l));
    }
    return YETTY_OK_VOID();
}

int main(int argc, char **argv)
{
    return demo_runner_run(argc, argv, "02_coord_debug", build);
}
