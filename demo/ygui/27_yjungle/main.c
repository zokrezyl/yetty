/*
 * Demo 27: yjungle widget.
 *
 * Renders one yjungle snapshot — the initial randomized chain — into a
 * ygui canvas. CMD_GROUP wrappers and the leading CMD_ZERO are
 * flattened away inside the widget so the RICH render path can paint
 * the primitives directly.
 *
 * Pass an unsigned integer as argv[1] for a reproducible seed.
 * Press 'q' to quit.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yetty/ygui/ygui.h>
#include <yetty/ygui/ygui_yjungle.h>
#include <yetty/yjungle/yjungle.h>

static struct yetty_ygui_widget *g_outer = NULL;
static struct yetty_ygui_widget *g_jungle = NULL;
static struct yetty_ygui_engine *g_engine = NULL;
static uint32_t g_seed = 0;

static void rebuild_jungle(float w, float h)
{
    if (g_jungle) {
        yetty_ygui_widget_remove(g_jungle);
        g_jungle = NULL;
    }
    struct yetty_yjungle_config cfg = yetty_yjungle_config_default();
    /* Bigger initial chain than the default so the canvas isn't empty
     * on first paint — animation is off, this is the only frame. */
    cfg.initial_chain_length = 20;
    cfg.max_depth = 2;
    cfg.group_prob_depth0 = 0.4f;
    g_jungle = yetty_ygui_engine_yjungle(g_engine, "jungle", 0, 0, w, h,
                                         &cfg, g_seed);
    if (g_jungle) {
        yetty_ygui_widget_apply_css(g_jungle, "flex: 1 0 0; align-self: stretch;");
        yetty_ygui_widget_add_child(g_outer, g_jungle);
    }
}

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
    rebuild_jungle(nw, nh);
}

int main(int argc, char **argv)
{
    if (argc >= 2) {
        g_seed = (uint32_t)strtoul(argv[1], NULL, 10);
    }

    if (yetty_ygui_init() != 0) {
        return 1;
    }
    struct ygui_engine_ptr_result eng_r = yetty_ygui_engine_create(
        (struct yetty_ygui_engine_args){.name = "yjungle-demo"});
    if (YETTY_IS_ERR(eng_r)) {
        yetty_ycore_error_destroy(eng_r.error);
        yetty_ygui_shutdown();
        return 1;
    }
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
    rebuild_jungle(cw, ch);

    yetty_ygui_engine_on_resize(g_engine, on_resize, NULL);
    yetty_ygui_engine_on_key(g_engine, on_key, NULL);

    yetty_ygui_engine_run(g_engine);
    yetty_ygui_engine_destroy(g_engine);
    yetty_ygui_shutdown();
    return 0;
}
