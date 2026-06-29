/*
 * Demo 42_window_chrome — window-chrome POC and adoption model.
 *
 * A plain yguiapp:app subclass. The shared yguiapp host enables window chrome by
 * default in standalone (own-window) mode: it draws a caption strip and wires
 * the ychrome engine to the OS window_chrome, so this borderless window can be:
 *
 *   - moved    — drag the caption strip at the top
 *   - maximized — double-click the caption strip
 *   - resized   — drag the right or bottom edge / bottom-right corner
 *
 * None of that touches ygui or yui: it's the standalone ychrome class driving
 * a yplatform:window_chrome. The body below is ordinary ygui content (a label
 * + a clickable button), which proves the chrome gestures and the widget tree
 * coexist — a press on a widget is claimed by the widget, a press on the empty
 * caption falls through to chrome.
 *
 * To adopt window chrome in another app, copy the integration in
 * src/yetty/yguiapp/app.c (search for `chrome`): create + configure a chrome
 * object against yframework->window_chrome, draw a caption strip, feed it
 * set_size on resize, and route unclaimed mouse events through it.
 */
#include <yetty/yguiapp/app.h>
#include <yetty/yguiapp/run.h>
#include <yetty/ygui/ygui.h>
#include <yetty/ygui/widgets/button.h>
#include <yetty/ygui/widgets/label.h>

/* Demo app class: a yguiapp:app subclass with no extra state. */
struct [[clang::annotate("class@demoygui:42_window_chrome")]] [[clang::annotate(
    "parent@yguiapp:app")]] yetty_demoygui_42_window_chrome {
    int unused;
};

/* Result wrapper + class accessor forward-decls (this TU does not include its
 * own generated header; main.gen.c is #included at the foot). */
YETTY_YRESULT_DECLARE(yetty_demoygui_42_window_chrome_ptr,
                      struct yetty_demoygui_42_window_chrome *);
struct yetty_yclass_ptr_result yetty_demoygui_42_window_chrome_class_get(void);

[[clang::annotate("override@yguiapp:app:build")]]
static struct yetty_ycore_void_result build(struct yetty_yclass_object *app,
                                            struct yetty_yclass_object *root)
{
    (void)app;

    struct yetty_yclass_object_ptr_result label_r =
        yetty_ygui_widget_add(root, yetty_ygui_label_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, label_r, "42_window_chrome: label add");
    struct yetty_ycore_void_result label_text = yetty_ygui_label_set_text(
        label_r.value, "Window-chrome POC — the strip above is a real OS-window handle.");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, label_text, "42_window_chrome: label text");

    struct yetty_yclass_object_ptr_result button_r =
        yetty_ygui_widget_add(root, yetty_ygui_button_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, button_r, "42_window_chrome: button add");
    struct yetty_ycore_void_result button_label =
        yetty_ygui_button_set_label(button_r.value, "A normal ygui button — still clickable");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, button_label, "42_window_chrome: button label");

    return YETTY_OK_VOID();
}

int main(int argc, char **argv)
{
    return yetty_yguiapp_run_main(argc, argv, yetty_demoygui_42_window_chrome_class_get().value);
}

#include "main.gen.c"
