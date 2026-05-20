/*
 * Demo 24: ymarkdown widget.
 *
 * Renders a Markdown document as a ygui widget that fills the canvas.
 * Pass a .md path as argv[1] to render that file; otherwise an inline
 * sample buffer is used so the demo runs out of the box.
 *
 * Press 'q' to quit.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yetty/ygui/ygui.h>
#include <yetty/ygui/ygui_ymarkdown.h>

static struct yetty_ygui_widget *g_outer = NULL;
static struct yetty_ygui_widget *g_md = NULL;
static struct yetty_ygui_engine *g_engine = NULL;
static const char *g_path = NULL;

static const char SAMPLE_MD[] =
    "# ymarkdown widget\n"
    "\n"
    "This pane is produced by **ygui_ymarkdown**. The widget owns a\n"
    "`ydraw-core` buffer assembled by the *ymarkdown* renderer; the\n"
    "RICH widget plumbing translates each primitive by the box origin\n"
    "at render time.\n"
    "\n"
    "## Supported syntax\n"
    "\n"
    "- headings (`#` .. `######`)\n"
    "- **bold**, *italic*, ***bold-italic***\n"
    "- inline `code` runs\n"
    "- bullet lists with `-` or `*`\n"
    "\n"
    "## Use it\n"
    "\n"
    "Pass a `.md` file path on the command line to render that file\n"
    "instead of this default sample.\n";

static void rebuild_md(float w, float h)
{
    if (g_md) {
        yetty_ygui_widget_remove(g_md);
        g_md = NULL;
    }
    if (g_path) {
        g_md = yetty_ygui_engine_ymarkdown_from_file(g_engine, "md",
                                                     0, 0, w, h, g_path);
    } else {
        g_md = yetty_ygui_engine_ymarkdown_from_buffer(
            g_engine, "md", 0, 0, w, h,
            (const uint8_t *)SAMPLE_MD, sizeof(SAMPLE_MD) - 1);
    }
    if (g_md) {
        yetty_ygui_widget_apply_css(g_md, "flex: 1 0 0; align-self: stretch;");
        yetty_ygui_widget_add_child(g_outer, g_md);
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
    /* Re-render the markdown for the new viewport so the cell grid
     * tracks the canvas. */
    rebuild_md(nw, nh);
}

int main(int argc, char **argv)
{
    if (argc >= 2) {
        g_path = argv[1];
    }

    if (yetty_ygui_init() != 0) {
        return 1;
    }
    struct ygui_engine_ptr_result eng_r = yetty_ygui_engine_create(
        (struct yetty_ygui_engine_args){.name = "ymarkdown-demo"});
    if (YETTY_IS_ERR(eng_r)) {
        yetty_ycore_error_destroy(eng_r.error);
        yetty_ygui_shutdown();
        return 1;
    }
    g_engine = eng_r.value;

    g_outer = yetty_ygui_engine_vbox(g_engine, "outer", 0, 0, 100, 100);
    yetty_ygui_widget_apply_css(g_outer, "padding: 8; gap: 0; align-items: stretch;");

    float cw = 800, ch = 600;
    struct pixel_size_result sr = yetty_ygui_engine_get_size(g_engine);
    if (YETTY_IS_OK(sr)) {
        if (sr.value.width  > 0) cw = sr.value.width;
        if (sr.value.height > 0) ch = sr.value.height;
    } else {
        yetty_ycore_error_destroy(sr.error);
    }
    yetty_ygui_widget_set_size(g_outer, cw, ch);
    rebuild_md(cw, ch);

    yetty_ygui_engine_on_resize(g_engine, on_resize, NULL);
    yetty_ygui_engine_on_key(g_engine, on_key, NULL);

    yetty_ygui_engine_run(g_engine);
    yetty_ygui_engine_destroy(g_engine);
    yetty_ygui_shutdown();
    return 0;
}
