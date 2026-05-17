/*
 * Demo 25: ypdf widget.
 *
 * Renders a PDF document via ygui's ypdf widget. argv[1] is the PDF
 * path — required, since we have no inline PDF bytes to fall back on.
 *
 * Press 'q' to quit.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yetty/ygui/ygui.h>
#include <yetty/ygui/ygui_ypdf.h>

static struct yetty_ygui_widget *g_outer = NULL;
static struct yetty_ygui_widget *g_pdf = NULL;
static struct yetty_ygui_engine *g_engine = NULL;
static const char *g_path = NULL;

static void on_key(struct yetty_ygui_engine *e, uint32_t key, int mods, void *u)
{
    (void)mods;
    (void)u;
    if (key == 'q' || key == 'Q') {
        yetty_ygui_engine_stop(e);
    }
}

static void on_resize(struct yetty_ygui_engine *e, float nw, float nh,
                      float pw, float ph, void *u)
{
    (void)e; (void)pw; (void)ph; (void)u;
    yetty_ygui_widget_set_size(g_outer, nw, nh);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <file.pdf>\n", argv[0]);
        return 2;
    }
    g_path = argv[1];

    if (yetty_ygui_init() != 0) {
        return 1;
    }
    struct ygui_engine_ptr_result eng_r = yetty_ygui_engine_create(
        (struct yetty_ygui_engine_args){.name = "ypdf-demo"});
    if (YETTY_IS_ERR(eng_r)) {
        yetty_ycore_error_destroy(eng_r.error);
        yetty_ygui_shutdown();
        return 1;
    }
    g_engine = eng_r.value;

    g_outer = yetty_ygui_engine_vbox(g_engine, "outer", 0, 0, 100, 100);
    yetty_ygui_widget_apply_css(g_outer, "padding: 0; gap: 0; align-items: stretch;");

    float cw = 800, ch = 600;
    yetty_ygui_engine_get_size(g_engine, &cw, &ch);
    if (cw <= 0) cw = 800;
    if (ch <= 0) ch = 600;
    yetty_ygui_widget_set_size(g_outer, cw, ch);

    g_pdf = yetty_ygui_engine_ypdf_from_file(g_engine, "pdf",
                                             0, 0, cw, ch, g_path);
    if (!g_pdf) {
        fprintf(stderr, "ypdf-demo: failed to render %s\n", g_path);
        yetty_ygui_engine_destroy(g_engine);
        yetty_ygui_shutdown();
        return 1;
    }
    yetty_ygui_widget_apply_css(g_pdf, "flex: 1 0 0; align-self: stretch;");
    yetty_ygui_widget_add_child(g_outer, g_pdf);

    yetty_ygui_engine_on_resize(g_engine, on_resize, NULL);
    yetty_ygui_engine_on_key(g_engine, on_key, NULL);

    yetty_ygui_engine_run(g_engine);
    yetty_ygui_engine_destroy(g_engine);
    yetty_ygui_shutdown();
    return 0;
}
