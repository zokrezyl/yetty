/*
 * Demo 26: ybrowser widget.
 *
 * Renders an HTML document via ygui's ybrowser widget. Pass an .html
 * path as argv[1] to load that file; ybrowser derives the base URL
 * from the file's directory so relative <link rel=stylesheet>,
 * <script src=...> and <img src=...> resolve to neighbouring files.
 *
 * With no argv the demo uses an inline HTML buffer. Cross-file
 * references are only resolved when --base <url> is passed
 * (or via the file-path entry point).
 *
 * Press 'q' to quit.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yetty/ygui-old/ygui.h>
#include <yetty/ygui-old/ygui_ybrowser.h>

static struct yetty_ygui_old_widget *g_outer = NULL;
static struct yetty_ygui_old_widget *g_browser = NULL;
static struct yetty_ygui_old_engine *g_engine = NULL;

static const char SAMPLE_HTML[] =
    "<!doctype html>\n"
    "<html><head><title>ybrowser inline</title>\n"
    "<style>\n"
    "  body { font-family: sans-serif; background: #0b1014; color: #e0e5e4;\n"
    "         padding: 24px; }\n"
    "  h1   { color: #6ba892; margin: 0 0 12px 0; }\n"
    "  p    { line-height: 1.5; }\n"
    "  code { background: #1e262c; padding: 2px 6px; border-radius: 4px; }\n"
    "</style>\n"
    "</head><body>\n"
    "  <h1>ybrowser widget</h1>\n"
    "  <p>This pane is produced by <code>ygui_ybrowser</code>. The\n"
    "     widget owns a <code>ydraw-core</code> buffer assembled by\n"
    "     the <em>ylexbor</em> backend; ygui's RICH plumbing translates\n"
    "     each primitive by the box origin at render time.</p>\n"
    "  <p>Pass an <code>.html</code> file path on the command line to\n"
    "     load that file. The base URL is derived from the file's\n"
    "     directory, so relative <code>&lt;link rel=stylesheet&gt;</code>\n"
    "     and <code>&lt;script src=...&gt;</code> resolve against the\n"
    "     same folder.</p>\n"
    "</body></html>\n";

static void on_key(struct yetty_ygui_old_engine *e, uint32_t key, int mods, void *u)
{
    (void)mods;
    (void)u;
    if (key == 'q' || key == 'Q') {
        yetty_ygui_old_engine_stop(e);
    }
}

static void on_resize(struct yetty_ygui_old_engine *e, float nw, float nh,
                      float pw, float ph, void *u)
{
    (void)e; (void)pw; (void)ph; (void)u;
    yetty_ygui_old_widget_set_size(g_outer, nw, nh);
}

int main(int argc, char **argv)
{
    const char *path = NULL;
    const char *base_url = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--base") == 0 && i + 1 < argc) {
            base_url = argv[++i];
        } else if (argv[i][0] != '-') {
            path = argv[i];
        }
    }

    if (yetty_ygui_old_init() != 0) {
        return 1;
    }
    struct ygui_engine_ptr_result eng_r = yetty_ygui_old_engine_create(
        (struct yetty_ygui_old_engine_args){.name = "ybrowser-demo"});
    if (YETTY_IS_ERR(eng_r)) {
        yetty_ycore_error_destroy(eng_r.error);
        yetty_ygui_old_shutdown();
        return 1;
    }
    g_engine = eng_r.value;

    g_outer = yetty_ygui_old_engine_vbox(g_engine, "outer", 0, 0, 100, 100);
    yetty_ygui_old_widget_apply_css(g_outer, "padding: 0; gap: 0; align-items: stretch;");

    float cw = 800, ch = 600;
    struct pixel_size_result sr = yetty_ygui_old_engine_get_size(g_engine);
    if (YETTY_IS_OK(sr)) {
        if (sr.value.width  > 0) cw = sr.value.width;
        if (sr.value.height > 0) ch = sr.value.height;
    } else {
        yetty_ycore_error_destroy(sr.error);
    }
    yetty_ygui_old_widget_set_size(g_outer, cw, ch);

    if (path) {
        g_browser = yetty_ygui_old_engine_ybrowser_from_file(
            g_engine, "browser", 0, 0, cw, ch, path);
    } else {
        g_browser = yetty_ygui_old_engine_ybrowser_from_buffer(
            g_engine, "browser", 0, 0, cw, ch,
            (const uint8_t *)SAMPLE_HTML, sizeof(SAMPLE_HTML) - 1, base_url);
    }
    if (!g_browser) {
        fprintf(stderr, "ybrowser-demo: failed to render document\n");
        yetty_ygui_old_engine_destroy(g_engine);
        yetty_ygui_old_shutdown();
        return 1;
    }
    yetty_ygui_old_widget_apply_css(g_browser, "flex: 1 0 0; align-self: stretch;");
    yetty_ygui_old_widget_add_child(g_outer, g_browser);

    yetty_ygui_old_engine_on_resize(g_engine, on_resize, NULL);
    yetty_ygui_old_engine_on_key(g_engine, on_key, NULL);

    yetty_ygui_old_engine_run(g_engine);
    yetty_ygui_old_engine_destroy(g_engine);
    yetty_ygui_old_shutdown();
    return 0;
}
