/*
 * Demo 42_window_chrome — window-chrome POC and adoption model.
 *
 * Runs via demo_runner_run_chrome instead of demo_runner_run. In standalone
 * (own-window) mode that makes the shared runner draw a caption strip and wire
 * the ychrome engine to the OS window_manager, so this borderless window can be:
 *
 *   - moved    — drag the caption strip at the top
 *   - maximized — double-click the caption strip
 *   - resized   — drag the right or bottom edge / bottom-right corner
 *
 * None of that touches ygui or yui: it's the standalone ychrome class driving
 * a yplatform:window_manager. The body below is ordinary ygui content (a label
 * + a clickable button), which proves the chrome gestures and the widget tree
 * coexist — a press on a widget is claimed by the widget, a press on the empty
 * caption falls through to chrome.
 *
 * To adopt window chrome in another app, copy the integration in
 * demo/ygui/runner.c (search for `enable_chrome`): create + configure a chrome
 * object against rt->window_manager, draw a caption strip, feed it set_size on
 * resize, and route unclaimed mouse events through it.
 */
#include "runner.h"

#include <yetty/ygui/widgets/button.h>
#include <yetty/ygui/widgets/label.h>

static struct yetty_ycore_void_result build(struct demo_runner *runner,
                                            struct yetty_ygui_object *root)
{
    (void)runner;

    struct yetty_ygui_object_ptr_result label_r =
        yetty_ygui_add(yetty_ygui_label_class_get().value, root);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, label_r, "42_window_chrome: label add");
    struct yetty_ycore_void_result label_text = yetty_ygui_label_set_text(
        label_r.value, "Window-chrome POC — the strip above is a real OS-window handle.");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, label_text, "42_window_chrome: label text");

    struct yetty_ygui_object_ptr_result button_r =
        yetty_ygui_add(yetty_ygui_button_class_get().value, root);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, button_r, "42_window_chrome: button add");
    struct yetty_ycore_void_result button_label =
        yetty_ygui_button_set_label(button_r.value, "A normal ygui button — still clickable");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, button_label, "42_window_chrome: button label");

    return YETTY_OK_VOID();
}

int main(int argc, char **argv)
{
    return demo_runner_run_chrome(argc, argv, "42_window_chrome", build);
}
