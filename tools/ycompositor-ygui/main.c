/*
 * tools/ycompositor-ygui/main.c — ygui rendered via the new
 * ycompositor pipeline.
 *
 * Two modes, selected by command line:
 *
 *  1. headless ygui demo (default, no args)
 *     Mirrors the platform setup of tools/ycompositor (glfw + texture
 *     render target + compositor render loop) but instead of a hand-
 *     built ygrid full of SDF primitives spins up a headless ygui
 *     engine, builds a tiny widget tree, and pushes the engine's
 *     draw_list bytes through root container_process_records. On every
 *     RESIZE event the surface is reconfigured, the engine's display
 *     pixel size is updated, and the ygui scene is re-emitted so the
 *     compositor's per-widget figures track the new geometry.
 *
 *  2. interpose mode (`-e <cmd...>`)
 *     Acts as a debug terminal sitting between an app and a real yetty:
 *     forks the app under a PTY, scans its stdout for OSC envelopes,
 *     logs every envelope (code + payload size + decoded type), and
 *     feeds figure-tree records (YETTY_OSC_YCOMPOSITOR_BIN) into the
 *     existing compositor pipeline so the app's own rendering shows up
 *     in this window. The YMGUI factory is registered alongside YGRID
 *     so ymgui-shaped apps (the demo) render natively. Use this to see
 *     exactly what the app is emitting, in isolation from yetty itself.
 */

#include <yetty/yinit/yinit.h>
#include <yetty/yframework/yframework.h>
#include <yetty/yetty/yetty.h>
#include <yetty/yconfig/config.h>
#include <yetty/yevent/event.h>
#include <yetty/yplatform/platform-input-pipe.h>
#include <yetty/yplatform/extract-assets.h>
#include <yetty/yrender/render-target.h>
#include <yetty/yfigure/figure.h>
#include <yetty/yfigure/registry.h>
#include <yetty/yfigure/rpc.h>
#include <yetty/yfigure/wire.h>
#include <yetty/ygrid/ygrid.h>
#include <yetty/ydraw-core/cmds.h>
#include <yetty/ydraw-core/draw-list.h>
#include <yetty/yfigure/figure.h>
#include <yetty/yfont/font.h>
#include <yetty/yfont/msdf-font.h>
#include <yetty/ygui-old/ygui.h>
/* ygui_internal.h exposes the headless allocator (used by yui) and the
 * full engine struct so we can read engine->buffer post-rebuild. Same
 * pattern src/yetty/yui/yui.c uses. */
#include "yetty/ygui-old/ygui_internal.h"
#include <yetty/ymgui/figure.h>
#include <yetty/yface/yface.h>
#include <yetty/yterm/osc-codes.h>
#include <yetty/ytrace/ytrace.h>
#include <lz4frame.h>
#include <webgpu/webgpu.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

/* Brand palette (rules/08-branding.md) — used directly on widget
 * properties since the default theme uses a darker surface that's hard
 * to see against the brand-bg backdrop. */
#define COL_BG_LIFTED   0xFF141A1Fu
#define COL_BORDER      0xFF364A47u
#define COL_TEXT_PRI    0xFFE0E5E4u
#define COL_ACCENT      0xFF6BA892u
#define COL_ACCENT_HI   0xFF74C5A5u

struct ycomp_ygui_app {
    int quit;
    struct yetty_context       ctx;
    struct yetty_yframework     *yrt;
    struct yetty_ydraw_target *target;
    struct yetty_yfigure_container *root;
    struct yetty_yfigure_registry *registry;
    struct yetty_ygui_old_engine  *ygui;
    /* Borrowed pointer to the outer window so we can resize it to the
     * canvas dims on every push (ygui doesn't auto-fill via CSS in
     * the absence of a parent context — see ygreeter's on_resize). */
    struct yetty_ygui_old_widget  *win;
    /* Default font handed to the compositor; every per-group ygrid the
     * compositor creates borrows this at slot 0 so TEXT_SPAN records
     * (button labels, etc.) expand into renderable glyphs. */
    struct yetty_ydraw_font   *font;
    /* ygrid factory bundle — borrowed by every ygrid the registry mints;
     * must outlive every ygrid (i.e. outlive the container). */
    struct yetty_ygrid_factory_args figure_args;
    /* YMGUI factory bundle — registered alongside ygrid in interpose
     * mode so client apps that ship CREATE_CHILD records with
     * kind=YETTY_YFIGURE_KIND_YMGUI (the ymgui demo, ygui, …) get
     * their figures created instead of failing in registry_mint. */
    struct yetty_ymgui_factory_args ymgui_factory_args;
    void                       *surface;
    uint32_t                    surface_w;
    uint32_t                    surface_h;

    /* Interpose mode: forkpty + yface scanner. argv slice borrowed from
     * the process argv vector; NULL/0 means "headless demo mode" and the
     * in-process ygui scene is used. */
    char                      **child_argv;
    int                         child_argc;
    pid_t                       child_pid;
    int                         pty_master_fd;
    /* Cell size used to translate (cols, rows) into pixel TIOCSWINSZ
     * for the child. Matches imgui_impl_yetty's defaults so the demo
     * gets the pane size it expects. */
    float                       cell_w_px;
    float                       cell_h_px;
    struct yetty_yface         *yface_in;
    /* OSC analyzer counters — populated by on_osc; logged on shutdown. */
    uint64_t                    n_osc_total;
    uint64_t                    n_osc_compositor; /* OSC 630000 */
    uint64_t                    n_osc_other;
    uint64_t                    n_bytes_in;
};

/* Build the widget tree once. A window holds a vbox body containing a
 * header label, a row of action buttons, a panel with checkboxes /
 * progress, and a list. Default theme drives all styling so we see
 * exactly what ygui produces.
 *
 * Sizes / positions are picked for a typical desktop window size; the
 * window auto-fills via flex CSS so the actual canvas dim doesn't
 * matter much. */
static void build_scene(struct ycomp_ygui_app *app)
{
    /* Outer window — placeholder size; push_ygui_scene resizes it to
     * the live surface dims on every push (same pattern ygreeter's
     * on_resize uses). */
    struct yetty_ygui_old_widget *win = yetty_ygui_old_engine_window(
        app->ygui, "win", 0.0f, 0.0f, 100.0f, 100.0f, "ycompositor-ygui demo");
    if (!win)
        return;
    app->win = win;
    struct yetty_ygui_old_widget *body = yetty_ygui_old_widget_window_body(win);

    /* Menubar with one menu so the chrome looks real. */
    struct yetty_ygui_old_widget *mb =
        yetty_ygui_old_engine_menubar(app->ygui, "mb", 0.0f, 0.0f, 100.0f, 26.0f);
    struct yetty_ygui_old_widget *mf =
        yetty_ygui_old_engine_popup_menu(app->ygui, "mf", 0.0f, 0.0f, 180.0f);
    yetty_ygui_old_widget_popup_menu_add_item(mf, "New tab", NULL, NULL);
    yetty_ygui_old_widget_popup_menu_add_item(mf, "Reload", NULL, NULL);
    yetty_ygui_old_widget_popup_menu_add_separator(mf);
    yetty_ygui_old_widget_popup_menu_add_item(mf, "Quit", NULL, NULL);
    yetty_ygui_old_widget_menubar_add(mb, "File", mf);
    yetty_ygui_old_widget_window_set_menubar(win, mb);

    /* Statusbar at the bottom. */
    struct yetty_ygui_old_widget *sb = yetty_ygui_old_engine_statusbar(
        app->ygui, "sb", 0.0f, 0.0f, 100.0f, 22.0f,
        "ycompositor-ygui — ygui via process_records");
    yetty_ygui_old_widget_statusbar_set_right(sb, "v0.1");
    yetty_ygui_old_widget_window_set_statusbar(win, sb);

    /* Body: vertical flex. Header label, action row, panel, list. */
    yetty_ygui_old_widget_apply_css(body,
        "display: flex; flex-direction: column; padding: 16px; gap: 12px;");

    struct yetty_ygui_old_widget *hdr = yetty_ygui_old_engine_label(
        app->ygui, "hdr", 0.0f, 0.0f,
        "ygui scene rendered through ycompositor");
    yetty_ygui_old_widget_add_child(body, hdr);

    /* Action row — horizontal flex with several buttons. */
    struct yetty_ygui_old_widget *actions =
        yetty_ygui_old_engine_vbox(app->ygui, "actions", 0.0f, 0.0f, 100.0f, 48.0f);
    yetty_ygui_old_widget_apply_css(actions,
        "display: flex; flex-direction: row; gap: 8px; height: 48px;");
    struct yetty_ygui_old_widget *a1 = yetty_ygui_old_engine_button(
        app->ygui, "act_new", 0.0f, 0.0f, 120.0f, 36.0f, "New");
    struct yetty_ygui_old_widget *a2 = yetty_ygui_old_engine_button(
        app->ygui, "act_open", 0.0f, 0.0f, 120.0f, 36.0f, "Open");
    struct yetty_ygui_old_widget *a3 = yetty_ygui_old_engine_button(
        app->ygui, "act_save", 0.0f, 0.0f, 120.0f, 36.0f, "Save");
    struct yetty_ygui_old_widget *a4 = yetty_ygui_old_engine_button(
        app->ygui, "act_del", 0.0f, 0.0f, 140.0f, 36.0f, "Delete");
    yetty_ygui_old_widget_add_child(actions, a1);
    yetty_ygui_old_widget_add_child(actions, a2);
    yetty_ygui_old_widget_add_child(actions, a3);
    yetty_ygui_old_widget_add_child(actions, a4);
    yetty_ygui_old_widget_add_child(body, actions);

    /* Settings panel with several checkboxes + a slider. */
    struct yetty_ygui_old_widget *panel =
        yetty_ygui_old_engine_panel(app->ygui, "panel", 0.0f, 0.0f, 100.0f, 220.0f);
    yetty_ygui_old_widget_apply_css(panel,
        "display: flex; flex-direction: column; padding: 12px; gap: 8px;");
    struct yetty_ygui_old_widget *plabel =
        yetty_ygui_old_engine_label(app->ygui, "plabel", 0.0f, 0.0f, "Settings");
    struct yetty_ygui_old_widget *cb1 = yetty_ygui_old_engine_checkbox(
        app->ygui, "cb1", 0.0f, 0.0f, 220.0f, 26.0f, "Enable animations", 1);
    struct yetty_ygui_old_widget *cb2 = yetty_ygui_old_engine_checkbox(
        app->ygui, "cb2", 0.0f, 0.0f, 220.0f, 26.0f, "Reduce motion", 0);
    struct yetty_ygui_old_widget *cb3 = yetty_ygui_old_engine_checkbox(
        app->ygui, "cb3", 0.0f, 0.0f, 220.0f, 26.0f, "Show debug overlay", 0);
    struct yetty_ygui_old_widget *slabel =
        yetty_ygui_old_engine_label(app->ygui, "slabel", 0.0f, 0.0f, "Brightness");
    struct yetty_ygui_old_widget *slider = yetty_ygui_old_engine_slider(
        app->ygui, "sl", 0.0f, 0.0f, 320.0f, 24.0f, 0.0f, 100.0f, 65.0f);
    yetty_ygui_old_widget_add_child(panel, plabel);
    yetty_ygui_old_widget_add_child(panel, cb1);
    yetty_ygui_old_widget_add_child(panel, cb2);
    yetty_ygui_old_widget_add_child(panel, cb3);
    yetty_ygui_old_widget_add_child(panel, slabel);
    yetty_ygui_old_widget_add_child(panel, slider);
    yetty_ygui_old_widget_add_child(body, panel);

    /* Trailing button row to confirm flex layout stops where it
     * should and the panel takes its share. */
    struct yetty_ygui_old_widget *footer = yetty_ygui_old_engine_button(
        app->ygui, "footer_btn", 0.0f, 0.0f, 160.0f, 38.0f, "Apply");
    yetty_ygui_old_widget_add_child(body, footer);
}

/* Push the current ygui scene through the compositor. Called from
 * worker startup and from every RESIZE event so the compositor's
 * per-widget figures track the live surface geometry.
 *
 * engine_rebuild itself doesn't clear the draw_list or emit CMD_ZERO
 * — that's engine_render's job, and engine_render also tries to ship
 * over OSC which we don't want in-process. So we wipe + lead with
 * CMD_ZERO manually here. */
static struct yetty_ycore_void_result push_ygui_scene(struct ycomp_ygui_app *app)
{
    if (!app->ygui)
        return YETTY_OK_VOID();
    yetty_ygui_old_engine_set_display_pixel_size(
        app->ygui, (float)app->surface_w, (float)app->surface_h);
    /* Resize the outer window to fill the surface — ygui doesn't
     * auto-stretch the root widget to the canvas. */
    if (app->win) {
        yetty_ygui_old_widget_set_size(
            app->win, (float)app->surface_w, (float)app->surface_h);
    }
    yetty_ydraw_draw_list_clear(app->ygui->buffer);
    struct yetty_ycore_void_result zr =
        yetty_ydraw_draw_list_add_admin_clear_all(app->ygui->buffer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, zr, "push_ygui_scene: add CLEAR_ALL");
    struct yetty_ycore_void_result rr = yetty_ygui_old_engine_rebuild(app->ygui);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "push_ygui_scene: engine_rebuild");
    const uint8_t *bytes =
        (const uint8_t *)yetty_ydraw_draw_list_data(app->ygui->buffer);
    size_t size = yetty_ydraw_draw_list_size(app->ygui->buffer);
    yinfo("ycompositor-ygui: feeding %zu ygui wire bytes through process_records",
          size);
    return yetty_yfigure_container_process_records(app->root, bytes, size);
}

/*===========================================================================
 * Interpose-mode helpers — forkpty + yface scanner + OSC analyzer.
 *===========================================================================*/

/* Best-effort name lookup for the small set of OSC codes we care about,
 * for human-readable analyzer logs. Unknown codes fall through and get
 * logged numerically. */
static const char *osc_code_name(int code)
{
    switch (code) {
    case 620000: return "YETTY_OSC_YDRAW_BIN";
    case 620001: return "YETTY_OSC_YDRAW_TEXT_ZONE";
    case 620002: return "YETTY_OSC_YDRAW_TEXT_END";
    case 620003: return "YETTY_OSC_YDRAW_CLEAR";
    case 630000: return "YETTY_OSC_YCOMPOSITOR_BIN";
    case 610010: return "YETTY_OSC_CS_CLIENT_INPUT_SUB";
    case 700000: return "YETTY_OSC_SC_CLIENT_INPUT_FIGURE_MOUSE";
    case 700001: return "YETTY_OSC_SC_CLIENT_INPUT_FIGURE_RESIZE";
    case 700002: return "YETTY_OSC_SC_CLIENT_INPUT_FIGURE_FOCUS";
    case 700003: return "YETTY_OSC_SC_CLIENT_INPUT_FIGURE_KEY";
    case 700010: return "YETTY_OSC_SC_CLIENT_INPUT_MOUSE";
    case 700011: return "YETTY_OSC_SC_CLIENT_INPUT_RESIZE";
    case 700012: return "YETTY_OSC_SC_CLIENT_INPUT_KEY";
    default:     return NULL;
    }
}

/* LZ4F frame magic; producers that pass compressed=1 to yface emit a
 * frame starting with this little-endian word but don't write a
 * bin_meta into the args slot, so yface's scanner can't auto-flip its
 * decoder. ywire's SM sniffs the magic instead; we mirror that here. */
#define LZ4F_FRAME_MAGIC_LE 0x184D2204u

/* If `payload` is an LZ4F frame, decompress into a malloc'd buffer and
 * return it (caller frees). Otherwise return NULL and leave the caller
 * to process the bytes as-is. */
static uint8_t *maybe_decompress_lz4f(const uint8_t *payload, size_t payload_len,
                                       size_t *out_len)
{
    if (payload_len < 4) return NULL;
    uint32_t magic;
    memcpy(&magic, payload, 4);
    if (magic != LZ4F_FRAME_MAGIC_LE) return NULL;

    LZ4F_decompressionContext_t ctx = NULL;
    if (LZ4F_isError(LZ4F_createDecompressionContext(&ctx, LZ4F_VERSION))) {
        return NULL;
    }
    size_t cap = payload_len * 4 + 4096;
    size_t used = 0;
    uint8_t *buf = (uint8_t *)malloc(cap);
    if (!buf) {
        LZ4F_freeDecompressionContext(ctx);
        return NULL;
    }
    size_t in_pos = 0;
    while (in_pos < payload_len) {
        size_t out_left = cap - used;
        size_t in_left = payload_len - in_pos;
        size_t r = LZ4F_decompress(ctx, buf + used, &out_left,
                                   payload + in_pos, &in_left, NULL);
        if (LZ4F_isError(r)) {
            free(buf);
            LZ4F_freeDecompressionContext(ctx);
            return NULL;
        }
        used += out_left;
        in_pos += in_left;
        if (r == 0) break; /* frame complete */
        if (out_left == 0 && used == cap) {
            cap *= 2;
            uint8_t *nb = (uint8_t *)realloc(buf, cap);
            if (!nb) { free(buf); LZ4F_freeDecompressionContext(ctx); return NULL; }
            buf = nb;
        }
    }
    LZ4F_freeDecompressionContext(ctx);
    *out_len = used;
    return buf;
}

/* yface on_osc — called once per complete envelope scanned from the
 * child's PTY output. Logs the envelope and feeds figure-tree records
 * straight into the existing compositor pipeline so the child's actual
 * rendering becomes visible in this window. */
static void on_osc(void *user, int osc_code, const uint8_t *args, size_t args_len,
                   const uint8_t *payload, size_t payload_len)
{
    struct ycomp_ygui_app *app = user;
    (void)args; (void)args_len;
    app->n_osc_total++;

    const char *name = osc_code_name(osc_code);

    /* Producers that ship LZ4F-compressed bodies via yface (the ymgui
     * FRAME emit path, ydraw bin records, …) don't supply a bin_meta in
     * args, so the receiving yface scanner hands us raw LZ4F frame
     * bytes. Detect by magic and decompress; everything else passes
     * through untouched. */
    size_t inflated_len = 0;
    uint8_t *inflated = maybe_decompress_lz4f(payload, payload_len, &inflated_len);
    const uint8_t *body = inflated ? inflated : payload;
    size_t body_len = inflated ? inflated_len : payload_len;

    yinfo("ycompositor-ygui[osc]: code=%d (%s) args=%zu payload=%zu%s",
          osc_code, name ? name : "?", args_len, body_len,
          inflated ? " [lz4f-decompressed]" : "");

    if (osc_code == YETTY_OSC_YCOMPOSITOR_BIN) {
        app->n_osc_compositor++;
        if (app->root && body_len > 0) {
            struct yetty_ycore_void_result pr =
                yetty_yfigure_container_process_records(app->root, body, body_len);
            if (YETTY_IS_ERR(pr)) {
                yerror("ycompositor-ygui[osc]: process_records failed: %s",
                       pr.error.msg);
                yetty_ycore_error_destroy(pr.error);
            }
            struct yetty_yfigure_figure *rf =
                yetty_yfigure_container_as_figure(app->root);
            if (rf) rf->dirty = 1;
        }
    } else {
        app->n_osc_other++;
    }

    free(inflated);
}

static void on_raw(void *user, const char *bytes, size_t n)
{
    struct ycomp_ygui_app *app = user;
    (void)bytes;
    /* Raw output (printf / fprintf / non-OSC ANSI) is reported but
     * dropped — this tool is for OSC analysis, not full terminal
     * emulation. */
    app->n_bytes_in += n;
}

/* fork+exec the child under a PTY. cell size matches imgui_impl_yetty's
 * defaults so the demo's TIOCGWINSZ-derived "fill" computation reports
 * the surface dims this tool actually shows. Returns 0 on success. */
static int spawn_child(struct ycomp_ygui_app *app)
{
    struct winsize ws = {0};
    if (app->cell_w_px > 0.0f) {
        ws.ws_col = (unsigned short)(app->surface_w / (uint32_t)app->cell_w_px);
    }
    if (app->cell_h_px > 0.0f) {
        ws.ws_row = (unsigned short)(app->surface_h / (uint32_t)app->cell_h_px);
    }
    if (ws.ws_col == 0) ws.ws_col = 80;
    if (ws.ws_row == 0) ws.ws_row = 24;
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;

    pid_t pid = forkpty(&app->pty_master_fd, NULL, NULL, &ws);
    if (pid < 0) {
        yerror("ycompositor-ygui: forkpty failed: %s", strerror(errno));
        return -1;
    }
    if (pid == 0) {
        /* Child — close inherited high fds and exec. */
        for (int fd = 3; fd < 1024; fd++) close(fd);
        execvp(app->child_argv[0], app->child_argv);
        _exit(127);
    }
    app->child_pid = pid;
    int flags = fcntl(app->pty_master_fd, F_GETFL, 0);
    if (flags >= 0) fcntl(app->pty_master_fd, F_SETFL, flags | O_NONBLOCK);
    yinfo("ycompositor-ygui: spawned child pid=%d cmd='%s' winsize=%ux%u cells",
          pid, app->child_argv[0], ws.ws_col, ws.ws_row);
    return 0;
}

static void update_child_winsize(struct ycomp_ygui_app *app)
{
    if (app->pty_master_fd < 0) return;
    struct winsize ws = {0};
    if (app->cell_w_px > 0.0f) {
        ws.ws_col = (unsigned short)(app->surface_w / (uint32_t)app->cell_w_px);
    }
    if (app->cell_h_px > 0.0f) {
        ws.ws_row = (unsigned short)(app->surface_h / (uint32_t)app->cell_h_px);
    }
    if (ws.ws_col == 0) ws.ws_col = 80;
    if (ws.ws_row == 0) ws.ws_row = 24;
    if (ioctl(app->pty_master_fd, TIOCSWINSZ, &ws) != 0) {
        ywarn("ycompositor-ygui: TIOCSWINSZ failed: %s", strerror(errno));
    }
}

/* Drain whatever is currently readable from the child's PTY and feed it
 * to the yface scanner. Returns 1 if the child closed (EOF). */
static int pump_pty_in(struct ycomp_ygui_app *app)
{
    if (app->pty_master_fd < 0) return 0;
    char buf[8192];
    for (;;) {
        ssize_t n = read(app->pty_master_fd, buf, sizeof(buf));
        if (n > 0) {
            struct yetty_ycore_void_result fr =
                yetty_yface_feed_bytes(app->yface_in, buf, (size_t)n);
            if (YETTY_IS_ERR(fr)) {
                yerror("ycompositor-ygui: yface_feed_bytes failed: %s", fr.error.msg);
                yetty_ycore_error_destroy(fr.error);
            }
            continue;
        }
        if (n == 0) return 1;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        if (errno == EINTR) continue;
        /* PTY master read errors after the child exits with EIO on Linux
         * — treat as EOF, not a hard error. */
        if (errno == EIO) return 1;
        yerror("ycompositor-ygui: pty read failed: %s", strerror(errno));
        return 1;
    }
}

static void handle_event(struct ycomp_ygui_app *app, const struct yetty_yui_event *ev)
{
    switch (ev->type) {
    case YETTY_YCORE_SHUTDOWN:
    case YETTY_YCORE_WINDOW_CLOSE:
        app->quit = 1;
        return;
    case YETTY_YCORE_RESIZE: {
        uint32_t w = (uint32_t)ev->resize.width;
        uint32_t h = (uint32_t)ev->resize.height;
        if (w == 0 || h == 0) return;
        app->surface_w = w;
        app->surface_h = h;
        struct yetty_ycore_void_result rr =
            yetty_yframework_reconfigure_surface(app->yrt, w, h);
        if (YETTY_IS_ERR(rr)) {
            yerror("ycompositor-ygui: reconfigure_surface failed: %s", rr.error.msg);
            yetty_ycore_error_destroy(rr.error);
        }
        struct yetty_yrender_viewport vp = {.x = 0, .y = 0, .w = (float)w, .h = (float)h};
        struct yetty_ycore_void_result tr =
            app->target->ops->resize(app->target, vp);
        if (YETTY_IS_ERR(tr)) {
            yerror("ycompositor-ygui: render_target resize failed: %s", tr.error.msg);
            yetty_ycore_error_destroy(tr.error);
        }
        /* Root container's rect tracks the framebuffer. The producer
         * re-emits the scene at the new dims; child figures move via
         * SET_CHILD_RECT records embedded in that re-emit. */
        struct yetty_yfigure_figure *rf =
            yetty_yfigure_container_as_figure(app->root);
        rf->rect = (struct yetty_ycore_rectangle){
            .min = {.x = 0.0f, .y = 0.0f}, .max = {.x = (float)w, .y = (float)h},
        };
        rf->dirty = 1;
        if (app->child_argv) {
            /* Interpose: tell the child the new pane size. The child
             * decides how to re-emit (the ymgui demo re-runs
             * figure_tree_rect_for_figure on the next on_frame, sees
             * the larger TIOCGWINSZ, and ships a SET_CHILD_RECT). */
            update_child_winsize(app);
        } else {
            struct yetty_ycore_void_result pr = push_ygui_scene(app);
            if (YETTY_IS_ERR(pr)) {
                yerror("ycompositor-ygui: push_ygui_scene (resize) failed: %s", pr.error.msg);
                yetty_ycore_error_destroy(pr.error);
            }
        }
        return;
    }
    case YETTY_YCORE_KEY_DOWN:
        if (ev->key.key == 256 || ev->key.key == 81) {
            app->quit = 1;
        }
        return;
    case YETTY_YCORE_MOUSE_DOWN:
        if (app->ygui) {
            yetty_ygui_old_engine_mouse_down(app->ygui, ev->mouse.x, ev->mouse.y,
                                         ev->mouse.button);
            /* Mouse input may have set HOVER/PRESSED flags or fired a
             * widget callback that mutated state; re-emit the scene so
             * the visual catches up. The CMD_ZERO at the head of every
             * push wipes the previous frame's groups before re-adding. */
            struct yetty_ycore_void_result pr = push_ygui_scene(app);
            if (YETTY_IS_ERR(pr)) {
                yerror("ycompositor-ygui: push (mouse_down) failed: %s", pr.error.msg);
                yetty_ycore_error_destroy(pr.error);
            }
        }
        return;
    case YETTY_YCORE_MOUSE_UP:
        if (app->ygui) {
            yetty_ygui_old_engine_mouse_up(app->ygui, ev->mouse.x, ev->mouse.y,
                                       ev->mouse.button);
            struct yetty_ycore_void_result pr = push_ygui_scene(app);
            if (YETTY_IS_ERR(pr)) {
                yerror("ycompositor-ygui: push (mouse_up) failed: %s", pr.error.msg);
                yetty_ycore_error_destroy(pr.error);
            }
        }
        return;
    case YETTY_YCORE_MOUSE_MOVE:
    case YETTY_YCORE_MOUSE_DRAG:
        if (app->ygui) {
            yetty_ygui_old_engine_mouse_move(app->ygui, ev->mouse.x, ev->mouse.y);
            /* Re-emit only if ygui flagged a hover-state change; for now
             * always re-emit since dirty tracking inside the engine
             * already gates per-widget. */
            struct yetty_ycore_void_result pr = push_ygui_scene(app);
            if (YETTY_IS_ERR(pr)) {
                yerror("ycompositor-ygui: push (mouse_move) failed: %s", pr.error.msg);
                yetty_ycore_error_destroy(pr.error);
            }
        }
        return;
    default:
        return;
    }
}

static struct yetty_ycore_void_result
ycomp_ygui_worker(struct yetty_yinit_runtime *rt, void *user)
{
    struct ycomp_ygui_app *app = user;

    struct yetty_yframework_ptr_result yr = yetty_yframework_create(rt);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, yr, "yframework_create failed");
    app->yrt = yr.value;

    app->ctx.runtime     = app->yrt;
    app->ctx.pty_factory = NULL;
    app->ctx.event_loop  = app->yrt->event_loop;

    app->surface   = rt->surface;
    app->surface_w = rt->surface_width;
    app->surface_h = rt->surface_height;

    /* Replace yframework's render target with a texture target that
     * blits to the GLFW surface on present — same setup as
     * tools/ycompositor. */
    app->yrt->render_target->ops->destroy(app->yrt->render_target);
    app->yrt->render_target = NULL;
    struct yetty_yrender_viewport vp = {
        .x = 0, .y = 0,
        .w = (float)app->surface_w, .h = (float)app->surface_h,
    };
    struct yetty_yrender_target_ptr_result tr = yetty_yrender_target_texture_create(
        app->yrt->gpu.device, app->yrt->gpu.queue, app->yrt->gpu.surface_format,
        app->yrt->gpu.allocator, (WGPUSurface)app->surface, vp);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, tr, "texture target create failed");
    app->target = tr.value;

    /* Load font first — it's needed as user-data for the ygrid factory. */
    {
        struct yetty_yconfig_config *config = app->yrt->config;
        const char *fonts_dir   = config->ops->get_string(config, "paths/fonts", "");
        const char *shaders_dir = config->ops->get_string(config, "paths/shaders", "");
        const char *font_family = "DejaVuSansMNerdFontMono";
        char cdb_path[768];
        char shader_path[768];
        snprintf(cdb_path, sizeof(cdb_path),
                 "%s/../msdf-fonts/%s-Regular.cdb", fonts_dir, font_family);
        snprintf(shader_path, sizeof(shader_path),
                 "%s/msdf-font.wgsl", shaders_dir);
        yinfo("ycompositor-ygui: loading font cdb='%s'", cdb_path);
        struct yetty_font_font_result font_result =
            yetty_yfont_msdf_font_create(cdb_path, shader_path, "ycompositor_ygui");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, font_result, "msdf_font_create failed");
        app->font = font_result.value;
        struct yetty_ycore_void_result load_result =
            app->font->ops->load_basic_latin(app->font);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, load_result, "font load_basic_latin failed");
    }

    /* Registry + root container — in headless-demo mode ygui emits
     * CREATE_CHILD records with KIND_YGRID; in interpose mode the
     * captured app can also emit KIND_YMGUI (the ymgui demo, etc.) so
     * we register both factories then. */
    struct yetty_yfigure_registry_ptr_result reg_r =
        yetty_yfigure_registry_create();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, reg_r, "yfigure_registry_create failed");
    app->registry = reg_r.value;
    /* Bundle the font into the factory-args struct; no complex-prim
     * factory at this layer — tool is a minimal POC. */
    app->figure_args.default_font = app->font;
    app->figure_args.figure_factory = NULL;
    struct yetty_ycore_void_result rf =
        yetty_ygrid_register_factory(app->registry, &app->figure_args);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rf, "ygrid_register_factory failed");

    if (app->child_argv) {
        app->ymgui_factory_args.context = &app->ctx;
        app->ymgui_factory_args.pipeline = NULL;
        struct yetty_ycore_void_result mr = yetty_ymgui_register_factory(
            app->registry, &app->ymgui_factory_args);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, mr,
                            "ymgui_register_factory failed");
    }

    struct yetty_ycore_rectangle root_rect = {
        .min = {.x = 0.0f, .y = 0.0f},
        .max = {.x = (float)app->surface_w, .y = (float)app->surface_h},
    };
    struct yetty_yclass_ctx yclass_ctx = {0};
    struct yetty_yclass_object_ptr_result obj_res =
        yetty_yfigure_container_create(&yclass_ctx);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, obj_res, "root_container create failed");
    app->root = yetty_yfigure_container_from(obj_res.value);
    yetty_yfigure_container_set_context(app->root, &app->ctx);
    yetty_yfigure_container_set_registry(app->root, app->registry);
    yetty_yfigure_container_set_rect(app->root, root_rect);

    if (app->child_argv) {
        /* Interpose mode — no in-process ygui scene; we render whatever
         * the child app emits. */
        struct yetty_yface_ptr_result yr = yetty_yface_create();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, yr, "yface_create failed");
        app->yface_in = yr.value;
        yetty_yface_set_handlers(app->yface_in, on_osc, on_raw, app);
        if (spawn_child(app) != 0) {
            return YETTY_ERR(yetty_ycore_void, "spawn_child failed");
        }
    } else {
        /* Headless ygui engine — same path yui uses (no libuv loop, no
         * PTY handshake). Built once at startup; the widget tree stays
         * across resizes, only the canvas size changes. */
        struct ygui_engine_ptr_result eng_r =
            yetty_ygui_old_engine_internal_alloc_for_yui("ycompositor-ygui", /*theme=*/NULL);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, eng_r, "ygui engine alloc failed");
        app->ygui = eng_r.value;
        build_scene(app);

        struct yetty_ycore_void_result pr0 = push_ygui_scene(app);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, pr0, "initial push_ygui_scene failed");
    }

    /* Event-driven render loop — polls the platform input pipe (window
     * events) plus, in interpose mode, the child's PTY master so OSC
     * envelopes arrive in the same iteration. */
    struct yetty_ycore_int_result fdr =
        rt->platform_input_pipe->ops->read_fd(rt->platform_input_pipe);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, fdr, "pipe read_fd failed");
    int pipe_fd = fdr.value;

    int needs_render = 1;
    while (!app->quit) {
        struct pollfd pfds[2];
        pfds[0].fd = pipe_fd; pfds[0].events = POLLIN; pfds[0].revents = 0;
        nfds_t nfds = 1;
        if (app->pty_master_fd >= 0) {
            pfds[1].fd = app->pty_master_fd;
            pfds[1].events = POLLIN;
            pfds[1].revents = 0;
            nfds = 2;
        }
        int pr = poll(pfds, nfds, needs_render ? 0 : -1);
        int had_events = 0;
        if (pr > 0 && (pfds[0].revents & POLLIN)) {
            for (;;) {
                struct yetty_yui_event ev = {0};
                struct yetty_ycore_size_result rr =
                    rt->platform_input_pipe->ops->read(rt->platform_input_pipe,
                                                       &ev, sizeof(ev));
                if (YETTY_IS_ERR(rr) || rr.value != sizeof(ev)) break;
                handle_event(app, &ev);
                had_events = 1;
            }
        }
        if (pr > 0 && nfds == 2 && (pfds[1].revents & (POLLIN | POLLHUP))) {
            int eof = pump_pty_in(app);
            had_events = 1;
            if (eof) {
                yinfo("ycompositor-ygui: child PTY closed (osc_total=%llu "
                      "compositor=%llu other=%llu raw_bytes=%llu)",
                      (unsigned long long)app->n_osc_total,
                      (unsigned long long)app->n_osc_compositor,
                      (unsigned long long)app->n_osc_other,
                      (unsigned long long)app->n_bytes_in);
                close(app->pty_master_fd);
                app->pty_master_fd = -1;
            }
        }
        if (rt->instance) {
            wgpuInstanceProcessEvents((WGPUInstance)rt->instance);
        }
        struct yetty_yfigure_figure *rrf =
            yetty_yfigure_container_as_figure(app->root);
        if (!(needs_render || had_events || rrf->dirty)) {
            continue;
        }

        struct yetty_ydraw_target *target = app->target;
        struct yetty_ycore_void_result cl = target->ops->clear(target);
        if (YETTY_IS_ERR(cl)) {
            yerror("ycompositor-ygui: clear failed: %s", cl.error.msg);
            yetty_ycore_error_destroy(cl.error);
        }
        struct yetty_ycore_void_result rr =
            yetty_yfigure_render(NULL, (struct yetty_yclass_object *)rrf - 1, target);
        if (YETTY_IS_ERR(rr)) {
            yerror("ycompositor-ygui: root render failed: %s", rr.error.msg);
            yetty_ycore_error_destroy(rr.error);
        } else {
            rrf->dirty = 0;
        }
        struct yetty_ycore_void_result pp = target->ops->present(target);
        if (YETTY_IS_ERR(pp)) {
            yerror("ycompositor-ygui: present failed: %s", pp.error.msg);
            yetty_ycore_error_destroy(pp.error);
        }
        needs_render = 0;
    }

    /* Teardown — root container first so any pending GPU work bound
     * to the runtime's device flushes before yframework_destroy. */
    {
        struct yetty_yfigure_figure *rrf =
            yetty_yfigure_container_as_figure(app->root);
        struct yetty_ycore_void_result dr =
            yetty_yfigure_destroy(NULL, (struct yetty_yclass_object *)rrf - 1);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dr, "root destroy");
    }
    app->root = NULL;
    if (app->registry) {
        struct yetty_ycore_void_result r =
            yetty_yfigure_registry_destroy(app->registry);
        if (YETTY_IS_ERR(r)) yetty_ycore_error_destroy(r.error);
        app->registry = NULL;
    }

    app->target->ops->destroy(app->target);
    app->target = NULL;

    if (app->ygui) {
        struct yetty_ycore_void_result er = yetty_ygui_old_engine_destroy(app->ygui);
        if (YETTY_IS_ERR(er)) {
            yerror("ycompositor-ygui: ygui engine destroy failed: %s", er.error.msg);
            yetty_ycore_error_destroy(er.error);
        }
        app->ygui = NULL;
    }

    if (app->yface_in) {
        yetty_yface_destroy(app->yface_in);
        app->yface_in = NULL;
    }
    if (app->pty_master_fd >= 0) {
        close(app->pty_master_fd);
        app->pty_master_fd = -1;
    }
    if (app->child_pid > 0) {
        kill(app->child_pid, SIGTERM);
        int status;
        waitpid(app->child_pid, &status, 0);
        app->child_pid = -1;
    }
    if (app->child_argv) {
        struct yetty_ycore_void_result rr =
            yetty_ymgui_factory_args_release(&app->ymgui_factory_args);
        if (YETTY_IS_ERR(rr)) yetty_ycore_error_destroy(rr.error);
    }

    if (app->font) {
        app->font->ops->destroy(app->font);
        app->font = NULL;
    }

    yetty_yframework_destroy(app->yrt);
    app->yrt = NULL;
    return YETTY_OK_VOID();
}

int main(int argc, char **argv)
{
    struct ycomp_ygui_app app = {0};
    app.cell_w_px = 8.0f;
    app.cell_h_px = 16.0f;
    app.pty_master_fd = -1;
    app.child_pid = -1;

    /* `-e <cmd> [args...]`: everything after -e is the child argv. We
     * still let yinit_run see the leading argv slice (it parses its own
     * flags), so we splice -e out by setting argc to the index of -e and
     * stashing the tail on the app for the worker. */
    int trimmed_argc = argc;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-e") == 0 && i + 1 < argc) {
            app.child_argv = &argv[i + 1];
            app.child_argc = argc - (i + 1);
            trimmed_argc = i; /* hide -e and tail from yinit */
            break;
        }
    }

    struct yetty_yinit_app_config cfg = {
        .extract_assets_fn = yetty_platform_extract_assets,
    };
    return yetty_yinit_run(trimmed_argc, argv, &cfg, ycomp_ygui_worker, &app);
}
