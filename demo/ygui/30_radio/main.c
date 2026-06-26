/*
 * Demo 30_radio: Radio group.
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
    const char *opts[] = {"Option A", "Option B", "Option C"};
    for (size_t i = 0; i < sizeof(opts) / sizeof(opts[0]); ++i) {
        struct yetty_yclass_object_ptr_result r =
            yetty_ygui_widget_add(root, yetty_ygui_radio_class_get().value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "radio");
        err_ok(yetty_ygui_radio_set_label(r.value, opts[i]));
        if (i == 0) {
            err_ok(yetty_ygui_radio_set_selected(r.value, 1));
        }
        struct yetty_yclass_object *w = r.value;
        {
            struct yetty_ygui_layout_const_ptr_result layout_res =
                yetty_ygui_widget_layout_get(w);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "30_radio: layout_get");
            struct yetty_ygui_layout l = *layout_res.value;
            l.height = 28;
            err_ok(yetty_ygui_widget_layout_set(w, &l));
        }
    }
    return YETTY_OK_VOID();
}

int main(int argc, char **argv)
{
    return demo_runner_run(argc, argv, "30_radio", build);
}
