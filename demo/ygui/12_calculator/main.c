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
struct [[clang::annotate("class@demoygui:12_calculator")]] [[clang::annotate("parent@yguiapp:app")]]
yetty_demoygui_12_calculator {
    int unused;
};

/* Result wrapper + class accessor forward-decls (this TU does not include its
 * own generated header; main.gen.c is #included at the foot). */
YETTY_YRESULT_DECLARE(yetty_demoygui_12_calculator_ptr, struct yetty_demoygui_12_calculator *);
struct yetty_yclass_ptr_result yetty_demoygui_12_calculator_class_get(void);

[[clang::annotate("override@yguiapp:app:build")]]
static struct yetty_ycore_void_result build(struct yetty_yclass_object *app,
                                            struct yetty_yclass_object *root)
{
    (void)app;
    const char *keys[] = {
        "7", "8", "9", "/", "4", "5", "6", "*", "1", "2", "3", "-", "0", ".", "=", "+",
    };
    for (int row = 0; row < 4; ++row) {
        struct yetty_yclass_object_ptr_result rr =
            yetty_ygui_widget_add(root, yetty_ygui_hbox_class_get().value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "row");
        struct yetty_ygui_layout_const_ptr_result layout_res =
            yetty_ygui_widget_layout_get(rr.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "12_calculator: layout_get");
        struct yetty_ygui_layout rl = *layout_res.value;
        rl.gap = 4;
        rl.height = 48;
        err_ok(yetty_ygui_widget_layout_set(rr.value, &rl));
        for (int col = 0; col < 4; ++col) {
            struct yetty_yclass_object_ptr_result br =
                yetty_ygui_widget_add(rr.value, yetty_ygui_button_class_get().value);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "key");
            err_ok(yetty_ygui_button_set_label(br.value, keys[row * 4 + col]));
            struct yetty_ygui_layout_const_ptr_result layout_res2 =
                yetty_ygui_widget_layout_get(br.value);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res2, "12_calculator: layout_get");
            struct yetty_ygui_layout bl = *layout_res2.value;
            bl.flex_grow = 1.0f;
            err_ok(yetty_ygui_widget_layout_set(br.value, &bl));
        }
    }
    return YETTY_OK_VOID();
}

int main(int argc, char **argv)
{
    return yetty_yguiapp_run_main(argc, argv, yetty_demoygui_12_calculator_class_get().value);
}

#include "main.gen.c"
