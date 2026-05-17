/*
 * Demo 34: multi-line text area.
 *
 * Click to focus, then type. Backspace / Enter / arrow keys / Home /
 * End work. Press 'q' to quit (when the textarea is NOT focused —
 * focused 'q' types a 'q'; click outside or Esc to defocus, or use
 * the close button on the host yetty).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yetty/ygui/ygui.h>

static struct yetty_ygui_widget *g_outer = NULL;
static struct yetty_ygui_widget *g_ta = NULL;
static struct yetty_ygui_widget *g_status = NULL;

static void on_text_change(struct yetty_ygui_widget *w, const char *text, void *u)
{
    (void)w; (void)u;
    char buf[128];
    int len = text ? (int)strlen(text) : 0;
    snprintf(buf, sizeof(buf), "%d bytes", len);
    yetty_ygui_widget_label_set_text(g_status, buf);
}

static void on_key(struct yetty_ygui_engine *e, uint32_t key, int mods, void *u)
{
    (void)mods; (void)u;
    /* Only let 'q' quit when the textarea is not focused — otherwise
     * the user can't type a 'q' character. */
    if ((key == 'q' || key == 'Q')) {
        /* This callback fires AFTER the focused widget's on_key. If
         * the textarea consumed it the key event won't reach here in
         * the normal flow. But the engine's global on_key fires
         * unconditionally for now, so we gate on focus here. */
        struct yetty_ygui_widget *ta = yetty_ygui_engine_find(e, "ta");
        if (!ta || !(yetty_ygui_widget_get_flags(ta) & YETTY_YGUI_FLAG_FOCUSED)) {
            yetty_ygui_engine_stop(e);
        }
    }
}

static void on_resize(struct yetty_ygui_engine *e, float nw, float nh, float pw, float ph, void *u)
{
    (void)e; (void)pw; (void)ph; (void)u;
    yetty_ygui_widget_set_size(g_outer, nw, nh);
}

int main(void)
{
    if (yetty_ygui_init() != 0) return 1;
    struct ygui_engine_ptr_result eng_r = yetty_ygui_engine_create(
        (struct yetty_ygui_engine_args){.name = "textarea-demo"});
    if (YETTY_IS_ERR(eng_r)) { yetty_ycore_error_destroy(eng_r.error); yetty_ygui_shutdown(); return 1; }
    struct yetty_ygui_engine *engine = eng_r.value;

    g_outer = yetty_ygui_engine_vbox(engine, "outer", 0, 0, 100, 100);
    yetty_ygui_widget_apply_css(g_outer, "padding: 16; gap: 12; align-items: stretch;");

    float cw = 800, ch = 600;
    yetty_ygui_engine_get_size(engine, &cw, &ch);
    if (cw <= 0) cw = 800;
    if (ch <= 0) ch = 600;
    yetty_ygui_widget_set_size(g_outer, cw, ch);

    struct yetty_ygui_widget *title = yetty_ygui_engine_label(engine, "title", 0, 0,
        "Multi-line text area — click to focus, then type.");
    yetty_ygui_widget_add_child(g_outer, title);

    g_ta = yetty_ygui_engine_textarea(engine, "ta", 0, 0, 0, 0,
        "Hello, world!\n\nThis is a multi-line text area.\nType, hit Enter, arrow around.\n");
    yetty_ygui_widget_apply_css(g_ta, "flex: 1 0 0; align-self: stretch;");
    yetty_ygui_widget_textarea_on_change(g_ta, on_text_change, NULL);
    yetty_ygui_widget_add_child(g_outer, g_ta);

    g_status = yetty_ygui_engine_label(engine, "status", 0, 0, "0 bytes");
    yetty_ygui_widget_add_child(g_outer, g_status);
    on_text_change(g_ta, yetty_ygui_widget_textarea_get_text(g_ta), NULL);

    yetty_ygui_engine_on_resize(engine, on_resize, NULL);
    yetty_ygui_engine_on_key(engine, on_key, NULL);

    yetty_ygui_engine_run(engine);
    yetty_ygui_engine_destroy(engine);
    yetty_ygui_shutdown();
    return 0;
}
