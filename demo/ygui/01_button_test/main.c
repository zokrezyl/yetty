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
struct [[clang::annotate("class@demoygui:01_button_test")]] [[clang::annotate(
    "parent@yguiapp:app")]] yetty_demoygui_01_button_test {
    int unused;
};

/* Result wrapper + class accessor forward-decls (this TU does not include its
 * own generated header; main.gen.c is #included at the foot). */
YETTY_YRESULT_DECLARE(yetty_demoygui_01_button_test_ptr, struct yetty_demoygui_01_button_test *);
struct yetty_yclass_ptr_result yetty_demoygui_01_button_test_class_get(void);

[[clang::annotate("override@yguiapp:app:build")]]
static struct yetty_ycore_void_result build(struct yetty_yclass_object *app,
                                            struct yetty_yclass_object *root)
{
    (void)app;
    {
        struct yetty_yclass_object_ptr_result lr =
            yetty_ygui_widget_add(root, yetty_ygui_label_class_get().value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, lr, "label");
        err_ok(yetty_ygui_label_set_text(lr.value, "Click any button"));
        struct yetty_yclass_object *w = lr.value;
        {
            struct yetty_ygui_layout_const_ptr_result layout_res = yetty_ygui_widget_layout_get(w);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "01_button_test: layout_get");
            struct yetty_ygui_layout l = *layout_res.value;
            l.height = 24;
            err_ok(yetty_ygui_widget_layout_set(w, &l));
        }
    }
    for (int i = 0; i < 3; ++i) {
        struct yetty_yclass_object_ptr_result br =
            yetty_ygui_widget_add(root, yetty_ygui_button_class_get().value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "button");
        char buf[16];
        snprintf(buf, sizeof(buf), "Button %d", i + 1);
        err_ok(yetty_ygui_button_set_label(br.value, buf));
        struct yetty_yclass_object *w = br.value;
        {
            struct yetty_ygui_layout_const_ptr_result layout_res2 = yetty_ygui_widget_layout_get(w);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res2, "01_button_test: layout_get");
            struct yetty_ygui_layout l = *layout_res2.value;
            l.height = 40;
            err_ok(yetty_ygui_widget_layout_set(w, &l));
        }
    }
    return YETTY_OK_VOID();
}

int main(int argc, char **argv)
{
    return yetty_yguiapp_run_main(argc, argv, yetty_demoygui_01_button_test_class_get().value);
}

#include "yetty/gen/impl/demoygui/01_button_test/main.c"
