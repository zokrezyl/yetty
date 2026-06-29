/*
 * Demo 18_flex_column: Flex column — vbox with three labels.
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
struct [[clang::annotate("class@demoygui:18_flex_column")]] [[clang::annotate(
    "parent@yguiapp:app")]] yetty_demoygui_18_flex_column {
    int unused;
};

/* Result wrapper + class accessor forward-decls (this TU does not include its
 * own generated header; main.gen.c is #included at the foot). */
YETTY_YRESULT_DECLARE(yetty_demoygui_18_flex_column_ptr, struct yetty_demoygui_18_flex_column *);
struct yetty_yclass_ptr_result yetty_demoygui_18_flex_column_class_get(void);

[[clang::annotate("override@yguiapp:app:build")]]
static struct yetty_ycore_void_result build(struct yetty_yclass_object *app,
                                            struct yetty_yclass_object *root)
{
    (void)app;
    const char *names[] = {"top", "middle", "bottom"};
    for (size_t i = 0; i < 3; ++i) {
        struct yetty_yclass_object_ptr_result r =
            yetty_ygui_widget_add(root, yetty_ygui_label_class_get().value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "label");
        err_ok(yetty_ygui_label_set_text(r.value, names[i]));
        struct yetty_ygui_layout_const_ptr_result layout_res =
            yetty_ygui_widget_layout_get(r.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "18_flex_column: layout_get");
        struct yetty_ygui_layout l = *layout_res.value;
        l.flex_grow = 1.0f;
        err_ok(yetty_ygui_widget_layout_set(r.value, &l));
    }
    return YETTY_OK_VOID();
}

int main(int argc, char **argv)
{
    return yetty_yguiapp_run_main(argc, argv, yetty_demoygui_18_flex_column_class_get().value);
}

#include "main.gen.c"
