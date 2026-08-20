/*
 * tools/ymaze/app.c — animated maze, IN-TERMINAL ONLY.
 *
 * Runs exclusively inside a hosting yetty (TERM_PROGRAM=yetty): the shared
 * GPU-free yguiapp terminal host renders the yetty_ygui_ymaze widget into the
 * pane over ywire (the ygui ydraw_embed → yscene path, same as demo
 * 40_ymaze). There is no standalone window mode and no WebGPU linkage —
 * outside yetty the tool prints an error and exits.
 *
 * Keys: q / ESC quit.
 */

#include <yetty/ycore/result.h>
#include <yetty/yclass/class.h>
#include <yetty/yguiapp/run.h>
#include <yetty/ygui/ygui.h>

static struct yetty_ycore_void_result ymaze_build(struct yetty_yclass_object *body, void *userdata)
{
    (void)userdata;
    struct yetty_yclass_object_ptr_result maze_res =
        yetty_ygui_widget_add(body, yetty_ygui_ymaze_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, maze_res, "ymaze: widget_add");
    struct yetty_ygui_layout_const_ptr_result layout_res =
        yetty_ygui_widget_layout_get(maze_res.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "ymaze: layout_get");
    struct yetty_ygui_layout layout = *layout_res.value;
    layout.flex_grow = 1.0f;
    layout.min_height = 300.0f;
    return yetty_ygui_widget_layout_set(maze_res.value, &layout);
}

int main(int argc, char **argv)
{
    return yetty_yguiapp_terminal_main(argc, argv, ymaze_build, NULL);
}
