/*
 * Demo 25: ypdf widget with scrolling.
 *
 * Layout: hbox { ypdf | vscrollbar }.
 *
 * Scrolling is unified through one path: the scrollbar is BOUND to the
 * ypdf widget via yetty_ygui_widget_scrollbar_bind. The scrollbar then
 * acts as a pure view — its thumb position + size are read from the
 * PDF's content_h / viewport_h / scroll_y; click / drag / wheel on the
 * scrollbar all call back into the PDF's scroll_to. The PDF marks the
 * scrollbar dirty whenever its scroll changes, so wheel-on-PDF and
 * keyboard moves update the thumb in the same frame.
 *
 * Inputs that scroll:
 *   - mouse wheel on the PDF
 *   - mouse wheel on the scrollbar
 *   - click / drag the scrollbar thumb or track
 *   - keyboard: Up/Down, PgUp/PgDn, Home/End
 *
 * Pass a PDF path as argv[1]. Press 'q' to quit.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yetty/ygui/ygui.h>
#include <yetty/ygui/ygui_ypdf.h>

static struct yetty_ygui_widget *g_outer = NULL;
static struct yetty_ygui_widget *g_pdf = NULL;
static struct yetty_ygui_widget *g_scroll = NULL;
static struct yetty_ygui_engine *g_engine = NULL;

enum {
    KEY_UP = 0x1000,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_PAGE_UP,
    KEY_PAGE_DOWN,
    KEY_HOME,
    KEY_END,
};

static void on_key(struct yetty_ygui_engine *e, uint32_t key, int mods, void *u)
{
    (void)mods;
    (void)u;
    if (key == 'q' || key == 'Q') {
        yetty_ygui_engine_stop(e);
        return;
    }
    if (!g_pdf) {
        return;
    }
    float viewport = yetty_ygui_widget_ypdf_viewport_height(g_pdf);
    float line = 60.0f;
    switch (key) {
    case KEY_UP:        yetty_ygui_widget_ypdf_scroll_by(g_pdf, -line); break;
    case KEY_DOWN:      yetty_ygui_widget_ypdf_scroll_by(g_pdf,  line); break;
    case KEY_PAGE_UP:   yetty_ygui_widget_ypdf_scroll_by(g_pdf, -viewport * 0.9f); break;
    case KEY_PAGE_DOWN: yetty_ygui_widget_ypdf_scroll_by(g_pdf,  viewport * 0.9f); break;
    case KEY_HOME:      yetty_ygui_widget_ypdf_scroll_to(g_pdf, 0.0f); break;
    case KEY_END:
        yetty_ygui_widget_ypdf_scroll_to(g_pdf,
                                         yetty_ygui_widget_ypdf_max_scroll(g_pdf));
        break;
    default: break;
    }
}

static void on_resize(struct yetty_ygui_engine *e, float nw, float nh, float pw, float ph, void *u)
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
    const char *path = argv[1];

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

    g_outer = yetty_ygui_engine_hbox(g_engine, "outer", 0, 0, 100, 100);
    yetty_ygui_widget_apply_css(g_outer, "padding: 0; gap: 0; align-items: stretch;");

    float cw = 800, ch = 600;
    yetty_ygui_engine_get_size(g_engine, &cw, &ch);
    if (cw <= 0) cw = 800;
    if (ch <= 0) ch = 600;
    yetty_ygui_widget_set_size(g_outer, cw, ch);

    const float sbar_w = 16.0f;
    g_pdf = yetty_ygui_engine_ypdf_from_file(g_engine, "pdf", 0, 0, cw - sbar_w, ch, path);
    if (!g_pdf) {
        fprintf(stderr, "ypdf-demo: failed to render %s\n", path);
        yetty_ygui_engine_destroy(g_engine);
        yetty_ygui_shutdown();
        return 1;
    }
    yetty_ygui_widget_apply_css(g_pdf, "flex: 1 0 0; align-self: stretch;");
    yetty_ygui_widget_add_child(g_outer, g_pdf);

    g_scroll = yetty_ygui_engine_vscrollbar(g_engine, "vbar", 0, 0, sbar_w, ch);
    yetty_ygui_widget_apply_css(g_scroll, "align-self: stretch;");
    yetty_ygui_widget_add_child(g_outer, g_scroll);

    /* One line — scrollbar becomes a pure view of the PDF. No callbacks
     * to wire, no reentrancy guards. Wheel / click / drag / keyboard all
     * flow through the PDF's scroll_to; the PDF marks the scrollbar
     * dirty so the thumb tracks scroll_y in the same frame. */
    yetty_ygui_widget_scrollbar_bind(g_scroll, g_pdf);

    fprintf(stderr, "ypdf-demo: %d pages, content_h=%.0fpx\n",
            yetty_ygui_widget_ypdf_page_count(g_pdf),
            yetty_ygui_widget_ypdf_content_height(g_pdf));

    yetty_ygui_engine_on_resize(g_engine, on_resize, NULL);
    yetty_ygui_engine_on_key(g_engine, on_key, NULL);

    yetty_ygui_engine_run(g_engine);
    yetty_ygui_engine_destroy(g_engine);
    yetty_ygui_shutdown();
    return 0;
}
