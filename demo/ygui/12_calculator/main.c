/*
 * Demo 12_calculator: Calculator — 4x4 button grid.
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
    const char *keys[] = {
        "7", "8", "9", "/", "4", "5", "6", "*", "1", "2", "3", "-", "0", ".", "=", "+",
    };
    for (int row = 0; row < 4; ++row) {
        struct yetty_ygui_object_ptr_result rr =
            yetty_ygui_add(yetty_ygui_hbox_class_get().value, root);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "row");
        struct yetty_ygui_layout rl = *yetty_ygui_widget_layout_get(rr.value);
        rl.gap = 4;
        rl.height = 48;
        err_ok(yetty_ygui_widget_layout_set(rr.value, &rl));
        for (int col = 0; col < 4; ++col) {
            struct yetty_ygui_object_ptr_result br =
                yetty_ygui_add(yetty_ygui_button_class_get().value, rr.value);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "key");
            err_ok(yetty_ygui_button_set_label(br.value, keys[row * 4 + col]));
            struct yetty_ygui_layout bl = *yetty_ygui_widget_layout_get(br.value);
            bl.flex_grow = 1.0f;
            err_ok(yetty_ygui_widget_layout_set(br.value, &bl));
        }
    }
    return YETTY_OK_VOID();
}

int main(int argc, char **argv)
{
    return demo_runner_run(argc, argv, "12_calculator", build);
}
