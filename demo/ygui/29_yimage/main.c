/*
 * Demo 29: yimage widget.
 *
 * Renders a PNG/JPEG via ygui's yimage widget. Pass an image path as
 * argv[1]; required (no inline image bytes baked in).
 *
 * Press 'q' to quit.
 */

#include <stdio.h>
#include <stdlib.h>
#include <yetty/ygui/ygui.h>
#include <yetty/ygui/ygui_yimage.h>

static struct yetty_ygui_widget *g_outer = NULL;
static struct yetty_ygui_widget *g_img = NULL;
static struct yetty_ygui_engine *g_engine = NULL;
static const char *g_path = NULL;

static void rebuild_img(float w, float h)
{
    if (g_img) {
        yetty_ygui_widget_remove(g_img);
        g_img = NULL;
    }
    g_img = yetty_ygui_engine_yimage_from_file(g_engine, "img", 0, 0, w, h, g_path);
    if (g_img) {
        yetty_ygui_widget_apply_css(g_img, "flex: 1 0 0; align-self: stretch;");
        yetty_ygui_widget_add_child(g_outer, g_img);
    }
}

static void on_key(struct yetty_ygui_engine *e, uint32_t key, int mods, void *u)
{
    (void)mods; (void)u;
    if (key == 'q' || key == 'Q') yetty_ygui_engine_stop(e);
}

static void on_resize(struct yetty_ygui_engine *e, float nw, float nh, float pw, float ph, void *u)
{
    (void)e; (void)pw; (void)ph; (void)u;
    yetty_ygui_widget_set_size(g_outer, nw, nh);
    rebuild_img(nw, nh);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <image.png|jpg|...>\n", argv[0]);
        return 2;
    }
    g_path = argv[1];

    if (yetty_ygui_init() != 0) return 1;
    struct ygui_engine_ptr_result eng_r = yetty_ygui_engine_create(
        (struct yetty_ygui_engine_args){.name = "yimage-demo"});
    if (YETTY_IS_ERR(eng_r)) { yetty_ycore_error_destroy(eng_r.error); yetty_ygui_shutdown(); return 1; }
    g_engine = eng_r.value;

    g_outer = yetty_ygui_engine_vbox(g_engine, "outer", 0, 0, 100, 100);
    yetty_ygui_widget_apply_css(g_outer, "padding: 0; gap: 0; align-items: stretch;");

    float cw = 800, ch = 600;
    struct pixel_size_result sr = yetty_ygui_engine_get_size(g_engine);
    if (YETTY_IS_OK(sr)) {
        if (sr.value.width  > 0) cw = sr.value.width;
        if (sr.value.height > 0) ch = sr.value.height;
    } else {
        yetty_ycore_error_destroy(sr.error);
    }
    yetty_ygui_widget_set_size(g_outer, cw, ch);
    rebuild_img(cw, ch);

    yetty_ygui_engine_on_resize(g_engine, on_resize, NULL);
    yetty_ygui_engine_on_key(g_engine, on_key, NULL);

    yetty_ygui_engine_run(g_engine);
    yetty_ygui_engine_destroy(g_engine);
    yetty_ygui_shutdown();
    return 0;
}
