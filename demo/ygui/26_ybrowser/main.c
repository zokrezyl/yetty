/*
 * Demo 26_ybrowser: ybrowser — render HTML source.
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
    if (YETTY_IS_ERR(r)) yetty_ycore_error_destroy(r.error);
}

static struct yetty_ycore_void_result build(struct demo_runner *runner,
                                            struct yetty_ygui_object *root)
{
    (void)runner;
    struct yetty_ygui_object_ptr_result br =
        yetty_ygui_add(yetty_ygui_ybrowser_class_get(), root);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "ybrowser");
    struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(br.value);
    l.flex_grow = 1.0f;
    l.min_height = 200.0f;
    err_ok(yetty_ygui_widget_layout_set(br.value, &l));
    const char *html = "<h1>Hello</h1><p>ygui rendering via ylexbor.</p>";
    return yetty_ygui_ybrowser_set_html(br.value, html, strlen(html));
}

int main(int argc, char **argv)
{
    return demo_runner_run(argc, argv, "26_ybrowser", build);
}
