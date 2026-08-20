/*
 * tools/yjungle/app.c — animated jungle, IN-TERMINAL ONLY.
 *
 * Runs exclusively inside a hosting yetty (TERM_PROGRAM=yetty): the shared
 * GPU-free yguiapp terminal host renders the yetty_ygui_yjungle widget into
 * the pane over ywire (same path as demo 27_yjungle). There is no standalone
 * window mode and no WebGPU linkage — outside yetty the tool prints an error
 * and exits.
 *
 * Keys: q / ESC quit.
 */

#include <yetty/ycore/result.h>
#include <yetty/yclass/class.h>
#include <yetty/yguiapp/run.h>
#include <yetty/ygui/ygui.h>

static struct yetty_ycore_void_result yjungle_build(struct yetty_yclass_object *body,
                                                    void *userdata)
{
    (void)userdata;
    struct yetty_yclass_object_ptr_result jungle_res =
        yetty_ygui_widget_add(body, yetty_ygui_yjungle_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, jungle_res, "yjungle: widget_add");
    struct yetty_ygui_layout_const_ptr_result layout_res =
        yetty_ygui_widget_layout_get(jungle_res.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "yjungle: layout_get");
    struct yetty_ygui_layout layout = *layout_res.value;
    layout.flex_grow = 1.0f;
    layout.min_height = 200.0f;
    return yetty_ygui_widget_layout_set(jungle_res.value, &layout);
}

int main(int argc, char **argv)
{
    return yetty_yguiapp_terminal_main(argc, argv, yjungle_build, NULL);
}
