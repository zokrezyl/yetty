/*
 * Demo 28_yzoo: yzoo — placeholder.
 *
 * Standalone-mode ygui demo. The runner brings up window + GPU +
 * receiver-side container; this file only populates the widget tree.
 * Press 'q' (or Ctrl-C / Ctrl-D) to quit.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/api/yguiapp/app.h>
#include <yetty/yguiapp/run.h>
#include <yetty/ygui/ygui.h>

/* Demo app class: a yguiapp:app subclass with no extra state. */
struct [[clang::annotate("class@demoygui:28_yzoo")]] [[clang::annotate("parent@yguiapp:app")]]
yetty_demoygui_28_yzoo {
    int unused;
};

/* Result wrapper + class accessor forward-decls (this TU does not include its
 * own generated header; main.gen.c is #included at the foot). */
YETTY_YRESULT_DECLARE(yetty_demoygui_28_yzoo_ptr, struct yetty_demoygui_28_yzoo *);
struct yetty_yclass_ptr_result yetty_demoygui_28_yzoo_class_get(void);

[[clang::annotate("override@yguiapp:app:build")]]
static struct yetty_ycore_void_result build(struct yetty_yclass_object *app,
                                            struct yetty_yclass_object *root)
{
    (void)app;
    struct yetty_yclass_object_ptr_result zr =
        yetty_ygui_widget_add(root, yetty_ygui_yzoo_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, zr, "yzoo");
    struct yetty_ygui_layout_const_ptr_result layout_res = yetty_ygui_widget_layout_get(zr.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "28_yzoo: layout_get");
    struct yetty_ygui_layout l = *layout_res.value;
    l.flex_grow = 1.0f;
    l.min_height = 200.0f;
    return yetty_ygui_widget_layout_set(zr.value, &l);
}

int main(int argc, char **argv)
{
    return yetty_yguiapp_run_main(argc, argv, yetty_demoygui_28_yzoo_class_get().value);
}

#include "yetty/gen/impl/demoygui/28_yzoo/main.c"
