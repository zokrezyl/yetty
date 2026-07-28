/*
 * Demo 00_simple: a single button with a click callback.
 *
 * A yguiapp:app subclass that overrides only build(): the shared yguiapp host
 * brings up window + GPU (standalone) or the in-terminal client; this file only
 * adds one button and wires its click event. Click the button to change its
 * label; press 'q' (or Ctrl-C / Ctrl-D) to quit.
 */

#include <yetty/yguiapp/app.h>
#include <yetty/yguiapp/run.h>
#include <yetty/ygui/ygui.h>
#include <yetty/ygui/mixins/clickable.h>

/* Demo app class: a yguiapp:app subclass with no extra state. */
struct [[clang::annotate("class@demoygui:00_simple")]] [[clang::annotate("parent@yguiapp:app")]]
yetty_demoygui_00_simple {
    int unused;
};

/* Result wrapper + class accessor forward-decls (this TU does not include its
 * own generated header; main.gen.c is #included at the foot). */
YETTY_YRESULT_DECLARE(yetty_demoygui_00_simple_ptr, struct yetty_demoygui_00_simple *);
struct yetty_yclass_ptr_result yetty_demoygui_00_simple_class_get(void);

static inline void err_ok(struct yetty_ycore_void_result r)
{
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
    }
}

/* Click event handler. `obj` is the button that was clicked; the click
 * mixin invokes this on a press/release inside the widget. */
static struct yetty_ycore_void_result on_click(struct yetty_yclass_object *obj, void *userdata)
{
    (void)userdata;
    return yetty_ygui_button_set_label(obj, "Clicked!");
}

[[clang::annotate("override@yguiapp:app:build")]]
static struct yetty_ycore_void_result build(struct yetty_yclass_object *app,
                                            struct yetty_yclass_object *root)
{
    (void)app;

    struct yetty_yclass_object_ptr_result br =
        yetty_ygui_widget_add(root, yetty_ygui_button_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "button");
    struct yetty_yclass_object *button = br.value;

    err_ok(yetty_ygui_button_set_label(button, "Click me"));

    struct yetty_ygui_layout_const_ptr_result layout_res = yetty_ygui_widget_layout_get(button);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "00_simple: layout_get");
    struct yetty_ygui_layout l = *layout_res.value;
    l.width = 200;
    l.height = 40;
    err_ok(yetty_ygui_widget_layout_set(button, &l));

    return yetty_ygui_clickable_on_click_set(button, on_click, NULL);
}

int main(int argc, char **argv)
{
    return yetty_yguiapp_run_main(argc, argv, yetty_demoygui_00_simple_class_get().value);
}

#include "yetty/gen/impl/demoygui/00_simple/main.c"
