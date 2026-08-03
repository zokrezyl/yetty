/*
 * tools/ycompositor-ygui/main.c — ygui rendered via the new
 * ycompositor pipeline (yclass class `ycompositorygui:app`).
 *
 * Subclass of yapp:app. main() splices out `-e <cmd...>` (the child argv) then
 * drives the platform bring-up sequence directly — it keeps its own main() so
 * the `-e` tail never reaches yconfig. The platform hands the runtime to run.
 *
 * Two modes, selected by command line:
 *
 *  1. headless ygui demo (default, no args)
 *     Mirrors the platform setup of tools/ycompositor (glfw + texture
 *     render target + compositor render loop) but instead of a hand-
 *     built scene figure full of SDF primitives spins up a headless ygui
 *     framework, builds a tiny widget tree, and emits it into the root
 *     container in-process via the typed yfigure stubs (the framework's
 *     container_obj is the root, so emit marshals straight onto it with
 *     no wire/session). On every RESIZE event the surface is
 *     reconfigured, the viewport is updated, and the ygui scene is
 *     re-emitted so the compositor's per-widget figures track the new
 *     geometry.
 *
 *  2. interpose mode (`-e <cmd...>`)
 *     Acts as a debug terminal sitting between an app and a real yetty:
 *     forks the app under a PTY and becomes a yclass RPC server. The
 *     forked producer obtains a proxy to this tool's root figure
 *     container via RPC_OP_GET_ROOT and drives it with the typed yclass
 *     stubs (create_child / set_child_rect / apply_child_body / …) over
 *     DCS envelopes on YETTY_DCS_YCLASS_RPC; the RPC server dispatches
 *     each call straight onto app->root, mutating the figure tree so the
 *     producer's rendering shows up in this window. The YMGUI factory is
 *     registered alongside YGRID so ymgui-shaped apps (the demo) get
 *     their figures minted. Use this to see exactly what the app is
 *     driving, in isolation from yetty itself.
 */

#include <yetty/yplatform/gpu-context.h>
#include <yetty/yplatform/yplatform/platform.h>
#include "yetty/gen/impl/yapp/app.h"
#include <yetty/yclass/class.h>
#include <yetty/yframework/yframework.h>
#include <yetty/yetty/yetty.h>
#include <yetty/yconfig/config.h>
#include <yetty/yevent/event.h>
#include <yetty/yplatform/platform-input-pipe.h>
#include <yetty/yrender/render-target.h>
#include "yetty/gen/impl/yfigure/figure.h"
#include "yetty/gen/impl/yfigure/container.h"
#include <yetty/yfigure/registry.h>
#include <yetty/api/yscene/scene.h>
#include <yetty/yfont/font.h>
#include <yetty/yfont/msdf-font.h>
#include "yetty/gen/impl/ychrome/chrome.h" /* YETTY_YCHROME_FLAG_* + yetty_ychrome_handle_event */
#include <yetty/ychrome/host.h>
#include <yetty/ygui/ygui.h>
#include "yetty/gen/impl/ymgui/figure.h"
#include <yetty/ywire/wire-statemachine.h>
#include <yetty/yclass/rpc.h>
#include <yetty/yclass/rpc-dcs-server.h>
#include <yetty/yterminal/dcs-codes.h>
#include <yetty/ytrace/ytrace.h>
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
#define COL_BG_LIFTED 0xFF141A1Fu
#define COL_BORDER 0xFF364A47u
#define COL_TEXT_PRI 0xFFE0E5E4u
#define COL_ACCENT 0xFF6BA892u
#define COL_ACCENT_HI 0xFF74C5A5u

static inline void yetty_ycore_error_destroy_safe(struct yetty_ycore_void_result r)
{
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
    }
}

struct YETTY_ANNOTATE("class@ycompositorygui:app") YETTY_ANNOTATE("parent@yapp:app")
    yetty_ycompositorygui_app {
    int quit;
    struct yetty_context ctx;
    struct yetty_yframework *yrt;
    struct yetty_ydraw_target *target;
    /* The root container's yclass object. The container public API takes
     * the object (not the private body pointer); it's also wired into the
     * ygui framework via set_container_obj so framework_emit ships records
     * straight in (in-process, no PTY). */
    struct yetty_yclass_object *root;
    struct yetty_yfigure_registry *registry;
    struct yetty_yclass_object *ygui;
    /* Window chrome host (draggable/resizable titlebar + min/max/close). */
    struct yetty_ychrome_host *chrome;
    /* Borrowed pointer to the outer window — it is the framework root,
     * so the layout pass stretches it to the viewport automatically. */
    struct yetty_yclass_object *win;
    /* Default font handed to the compositor; every per-group scene the
     * compositor creates borrows this at slot 0 so TEXT_DRAWABLE_LIST records
     * (button labels, etc.) expand into renderable glyphs. */
    struct yetty_yfont_font *font;
    /* yscene factory bundle — borrowed by every scene the registry mints;
     * must outlive every scene (i.e. outlive the container). */
    struct yetty_yscene_factory_args figure_args;
    /* YMGUI factory bundle — registered alongside yscene in interpose
     * mode so client apps that ship CREATE_CHILD records with
     * kind=YETTY_YFIGURE_KIND_YMGUI (the ymgui demo, ygui, …) get
     * their figures created instead of failing in registry_mint. */
    struct yetty_ymgui_factory_args ymgui_factory_args;
    void *surface;
    uint32_t surface_w;
    uint32_t surface_h;

    /* Interpose mode: forkpty + wire statemachine RPC server. argv slice
     * borrowed from the process argv vector; NULL/0 means "headless demo
     * mode" and the in-process ygui scene is used. */
    char **child_argv;
    int child_argc;
    pid_t child_pid;
    int pty_master_fd;
    /* Cell size used to translate (cols, rows) into pixel TIOCSWINSZ
     * for the child. Matches imgui_impl_yetty's defaults so the demo
     * gets the pane size it expects. */
    float cell_w_px;
    float cell_h_px;
    /* Wire statemachine driven by the producer's PTY output. It hosts the
     * yclass RPC DCS server (YETTY_DCS_YCLASS_RPC) that dispatches the
     * producer's typed create_child/set_child_rect/apply_child_body/...
     * calls straight onto app->root, plus a catch-all that logs every
     * other envelope for analysis. The tool pushes PTY bytes into it via
     * yetty_ywire_wire_statemachine_feed (the SM is created with a NULL
     * PTY — it never pulls bytes itself). */
    struct yetty_ywire_wire_statemachine *wire_sm;
    struct yetty_yclass_rpc_dcs_server *dcs_rpc_server;
    /* Envelope analyzer counters — populated by the catch-all envelope
     * callback (the RPC code is consumed by the server handler and not
     * counted here); logged on shutdown. */
    uint64_t n_osc_total;
    uint64_t n_osc_other;
    uint64_t n_bytes_in;
};

/* Result wrapper + codegen accessor/downcast forward-decls (this TU does not
 * include its own generated header). */
YETTY_YRESULT_DECLARE(yetty_ycompositorygui_app_ptr, struct yetty_ycompositorygui_app *);
struct yetty_yclass_ptr_result yetty_ycompositorygui_app_class_get(void);
struct yetty_ycompositorygui_app_ptr_result yetty_ycompositorygui_app_from(
    struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ycompositorygui_app_create(
    struct yetty_yclass_ctx *ctx);

/* Platform bring-up sequence symbols. ycompositor-ygui owns its own main()
 * (`-e` child-argv splicing), so it drives this sequence directly. */
struct yetty_ycore_void_result yetty_yplatform_register(void);
struct yetty_yclass_object_ptr_result yetty_yplatform_glfw_platform_create(
    struct yetty_yclass_ctx *ctx);
struct yetty_ycore_void_result yetty_yplatform_platform_run(struct yetty_yclass_object *obj,
                                                            struct yetty_yclass_object *app,
                                                            int argc, char **argv);

/* Build the widget tree once. A window holds a vbox body containing a
 * header label, a row of action buttons, a panel with checkboxes /
 * progress, and a list. Default theme drives all styling so we see
 * exactly what ygui produces.
 *
 * Sizes / positions are picked for a typical desktop window size; the
 * window auto-fills via flex CSS so the actual canvas dim doesn't
 * matter much. */
/* Add `cls` under `parent`, returning the new object or NULL. */
static struct yetty_yclass_object *cy_add(struct yetty_yclass_object *parent,
                                          struct yetty_yclass_ptr_result cls_r)
{
    if (YETTY_IS_ERR(cls_r)) {
        yetty_ycore_error_destroy(cls_r.error);
        return NULL;
    }
    struct yetty_yclass_object_ptr_result r = yetty_ygui_widget_add(parent, cls_r.value);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
        return NULL;
    }
    return r.value;
}

static struct yetty_ycore_void_result build_scene(struct yetty_ycompositorygui_app *app)
{
    /* Outer window — it becomes the framework root, so the layout pass
     * stretches it to the viewport automatically. */
    struct yetty_yclass_object *win = cy_add(NULL, yetty_ygui_window_class_get());
    if (!win) {
        return YETTY_ERR(yetty_ycore_void, "build_scene: window create");
    }
    app->win = win;
    yetty_ycore_error_destroy_safe(yetty_ygui_window_set_title(win, "ycompositor-ygui demo"));
    struct yetty_yclass_object_ptr_result body_res = yetty_ygui_window_body(win);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, body_res, "build_scene: window body");
    struct yetty_yclass_object *body = body_res.value;
    if (!body) {
        return YETTY_ERR(yetty_ycore_void, "build_scene: window body is NULL");
    }
    yetty_ycore_error_destroy_safe(yetty_ygui_widget_apply_css(
        body, "display: flex; flex-direction: column; padding: 16px; gap: 12px;"));

    /* Menubar with one menu so the chrome looks real. The popup lives
     * under the window so the tree owns it; the menubar borrows it. */
    struct yetty_yclass_object *mf = cy_add(win, yetty_ygui_popup_menu_class_get());
    if (mf) {
        yetty_ycore_error_destroy_safe(yetty_ygui_popup_menu_add_item(mf, "New tab", NULL, NULL));
        yetty_ycore_error_destroy_safe(yetty_ygui_popup_menu_add_item(mf, "Reload", NULL, NULL));
        yetty_ycore_error_destroy_safe(yetty_ygui_popup_menu_add_separator(mf));
        yetty_ycore_error_destroy_safe(yetty_ygui_popup_menu_add_item(mf, "Quit", NULL, NULL));
    }
    struct yetty_yclass_object *mb = cy_add(body, yetty_ygui_menubar_class_get());
    if (mb) {
        yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_size(mb, 0.0f, 26.0f));
        if (mf) {
            yetty_ycore_error_destroy_safe(yetty_ygui_menubar_add(mb, "File", mf));
        }
    }

    struct yetty_yclass_object *hdr = cy_add(body, yetty_ygui_label_class_get());
    if (hdr) {
        yetty_ycore_error_destroy_safe(
            yetty_ygui_label_set_text(hdr, "ygui scene rendered through ycompositor"));
        yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_size(hdr, 0.0f, 24.0f));
    }

    /* Action row — horizontal flex with several buttons. */
    struct yetty_yclass_object *actions = cy_add(body, yetty_ygui_hbox_class_get());
    if (actions) {
        yetty_ycore_error_destroy_safe(yetty_ygui_widget_apply_css(
            actions, "display: flex; flex-direction: row; gap: 8px; height: 48px;"));
        yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_size(actions, 0.0f, 48.0f));
        static const char *const acts[] = {"New", "Open", "Save", "Delete"};
        for (size_t i = 0; i < sizeof(acts) / sizeof(acts[0]); i++) {
            struct yetty_yclass_object *b = cy_add(actions, yetty_ygui_button_class_get());
            if (b) {
                yetty_ycore_error_destroy_safe(yetty_ygui_button_set_label(b, acts[i]));
                yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_size(b, 120.0f, 36.0f));
            }
        }
    }

    /* Settings panel with several checkboxes + a slider. */
    struct yetty_yclass_object *panel = cy_add(body, yetty_ygui_panel_class_get());
    if (panel) {
        yetty_ycore_error_destroy_safe(yetty_ygui_widget_apply_css(
            panel, "display: flex; flex-direction: column; padding: 12px; gap: 8px;"));
        yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_size(panel, 0.0f, 220.0f));
        struct yetty_yclass_object *plabel = cy_add(panel, yetty_ygui_label_class_get());
        if (plabel) {
            yetty_ycore_error_destroy_safe(yetty_ygui_label_set_text(plabel, "Settings"));
            yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_size(plabel, 0.0f, 24.0f));
        }
        static const struct {
            const char *label;
            int checked;
        } cbs[] = {{"Enable animations", 1}, {"Reduce motion", 0}, {"Show debug overlay", 0}};
        for (size_t i = 0; i < sizeof(cbs) / sizeof(cbs[0]); i++) {
            struct yetty_yclass_object *cb = cy_add(panel, yetty_ygui_checkbox_class_get());
            if (cb) {
                yetty_ycore_error_destroy_safe(yetty_ygui_checkbox_set_label(cb, cbs[i].label));
                yetty_ycore_error_destroy_safe(yetty_ygui_checkbox_set_checked(cb, cbs[i].checked));
                yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_size(cb, 0.0f, 26.0f));
            }
        }
        struct yetty_yclass_object *slabel = cy_add(panel, yetty_ygui_label_class_get());
        if (slabel) {
            yetty_ycore_error_destroy_safe(yetty_ygui_label_set_text(slabel, "Brightness"));
            yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_size(slabel, 0.0f, 24.0f));
        }
        struct yetty_yclass_object *slider = cy_add(panel, yetty_ygui_slider_class_get());
        if (slider) {
            yetty_ycore_error_destroy_safe(yetty_ygui_slider_set_range(slider, 0.0f, 100.0f));
            yetty_ycore_error_destroy_safe(yetty_ygui_slider_set_value(slider, 65.0f));
            yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_size(slider, 0.0f, 24.0f));
        }
    }

    /* Trailing button to confirm flex layout stops where it should. */
    struct yetty_yclass_object *footer = cy_add(body, yetty_ygui_button_class_get());
    if (footer) {
        yetty_ycore_error_destroy_safe(yetty_ygui_button_set_label(footer, "Apply"));
        yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_size(footer, 160.0f, 38.0f));
    }

    /* Statusbar at the bottom of the body. */
    struct yetty_yclass_object *sb = cy_add(body, yetty_ygui_statusbar_class_get());
    if (sb) {
        yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_size(sb, 0.0f, 22.0f));
        yetty_ycore_error_destroy_safe(
            yetty_ygui_statusbar_set_left(sb, "ycompositor-ygui — ygui via typed yfigure stubs"));
        yetty_ycore_error_destroy_safe(yetty_ygui_statusbar_set_right(sb, "v0.1"));
    }
    return YETTY_OK_VOID();
}

/* Push the current ygui scene through the compositor. Called from
 * worker startup and from every RESIZE event so the compositor's
 * per-widget figures track the live surface geometry.
 *
 * engine_rebuild itself doesn't clear the drawable_list or emit CMD_ZERO
 * — that's engine_render's job, and engine_render also tries to ship
 * over OSC which we don't want in-process. So we wipe + lead with
 * CMD_ZERO manually here. */
static struct yetty_ycore_void_result push_ygui_scene(struct yetty_ycompositorygui_app *app)
{
    if (!app->ygui) {
        return YETTY_OK_VOID();
    }
    /* The window is the framework root, so the layout pass stretches it
     * to the viewport — just keep the viewport in sync with the surface
     * and emit. framework_emit lays out, walks the tree, and ships the
     * record stream straight into app->root via the in-process yclass
     * slot path (set_container_obj was wired at startup). */
    struct yetty_ycore_void_result vr =
        yetty_ygui_framework_set_viewport(app->ygui, (float)app->surface_w, (float)app->surface_h);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, vr, "push_ygui_scene: set_viewport");
    yetty_ygui_framework_mark_dirty(app->ygui);
    return yetty_ygui_framework_emit(app->ygui);
}

/*===========================================================================
 * Interpose-mode helpers — forkpty + wire statemachine RPC server.
 *===========================================================================*/

/* Best-effort name lookup for the small set of OSC codes we care about,
 * for human-readable analyzer logs. Unknown codes fall through and get
 * logged numerically. */
static const char *osc_code_name(int code)
{
    switch (code) {
    case 620000:
        return "YETTY_DCS_YDRAW_BIN";
    case 620001:
        return "YETTY_DCS_YDRAW_TEXT_ZONE";
    case 620002:
        return "YETTY_DCS_YDRAW_TEXT_END";
    case 620003:
        return "YETTY_DCS_YDRAW_CLEAR";
    case 800000:
        return "YETTY_DCS_YCLASS_RPC";
    case 610010:
        return "YETTY_OSC_CS_CLIENT_INPUT_SUB";
    case 700000:
        return "YETTY_OSC_SC_CLIENT_INPUT_FIGURE_MOUSE";
    case 700001:
        return "YETTY_OSC_SC_CLIENT_INPUT_FIGURE_RESIZE";
    case 700002:
        return "YETTY_OSC_SC_CLIENT_INPUT_FIGURE_FOCUS";
    case 700003:
        return "YETTY_OSC_SC_CLIENT_INPUT_FIGURE_KEY";
    case 700010:
        return "YETTY_OSC_SC_CLIENT_INPUT_MOUSE";
    case 700011:
        return "YETTY_OSC_SC_CLIENT_INPUT_RESIZE";
    case 700012:
        return "YETTY_OSC_SC_CLIENT_INPUT_KEY";
    default:
        return NULL;
    }
}

/* yetty_yclass_rpc_dcs_emit_fn impl — ships an already-encoded DCS
 * response envelope back to the producer over the SAME channel the tool
 * reads its requests from: the PTY master. The producer's stdin is the
 * PTY slave, so these bytes land where its RPC session's read_fd
 * (transport-dcs) decodes them. Looping write because a single write(2)
 * may be short. Mirrors terminal_dcs_emit_response. */
YETTY_EXTERNAL_CALLBACK
static struct yetty_ycore_void_result on_rpc_response(const uint8_t *bytes, size_t n, void *user)
{
    struct yetty_ycompositorygui_app *app = user;
    if (app->pty_master_fd < 0) {
        return YETTY_ERR(yetty_ycore_void, "on_rpc_response: PTY master closed");
    }
    size_t off = 0;
    while (off < n) {
        ssize_t written = write(app->pty_master_fd, bytes + off, n - off);
        if (written > 0) {
            off += (size_t)written;
            continue;
        }
        if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            continue;
        }
        return YETTY_ERR(yetty_ycore_void, "on_rpc_response: PTY master write failed");
    }
    return YETTY_OK_VOID();
}

/* Catch-all envelope callback — fires for every envelope the producer
 * ships that ISN'T the yclass RPC code (that one is consumed by the RPC
 * server handler registered on the same SM). Pure analyzer logging; the
 * figure tree is mutated by the RPC server dispatching typed calls onto
 * app->root, not here. The body is already fully decoded (b64 + LZ4F) by
 * the wire statemachine. */
static struct yetty_ycore_void_result on_envelope(void *user, enum yetty_ywire_envelope_kind kind,
                                                  int code, const uint8_t *args, size_t args_len,
                                                  const uint8_t *payload, size_t payload_len)
{
    struct yetty_ycompositorygui_app *app = user;
    (void)kind;
    (void)args;
    (void)payload;
    /* The RPC code is consumed by the dedicated server handler, so it
     * never reaches this catch-all — everything seen here is "other". */
    app->n_osc_total++;
    app->n_osc_other++;
    const char *name = osc_code_name(code);
    yinfo("ycompositor-ygui[env]: code=%d (%s) args=%zu payload=%zu", code, name ? name : "?",
          args_len, payload_len);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result on_raw(void *user, const uint8_t *bytes, size_t n)
{
    struct yetty_ycompositorygui_app *app = user;
    (void)bytes;
    /* Raw output (printf / fprintf / non-OSC ANSI) is reported but
     * dropped — this tool renders figures driven over RPC, not full
     * terminal emulation. */
    app->n_bytes_in += n;
    return YETTY_OK_VOID();
}

/* fork+exec the child under a PTY. cell size matches imgui_impl_yetty's
 * defaults so the demo's TIOCGWINSZ-derived "fill" computation reports
 * the surface dims this tool actually shows. Returns 0 on success. */
static int spawn_child(struct yetty_ycompositorygui_app *app)
{
    struct winsize ws = {0};
    if (app->cell_w_px > 0.0f) {
        ws.ws_col = (unsigned short)(app->surface_w / (uint32_t)app->cell_w_px);
    }
    if (app->cell_h_px > 0.0f) {
        ws.ws_row = (unsigned short)(app->surface_h / (uint32_t)app->cell_h_px);
    }
    if (ws.ws_col == 0) {
        ws.ws_col = 80;
    }
    if (ws.ws_row == 0) {
        ws.ws_row = 24;
    }
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;

    pid_t pid = forkpty(&app->pty_master_fd, NULL, NULL, &ws);
    if (pid < 0) {
        yerror("ycompositor-ygui: forkpty failed: %s", strerror(errno));
        return -1;
    }
    if (pid == 0) {
        /* Child — close inherited high fds and exec. */
        for (int fd = 3; fd < 1024; fd++) {
            close(fd);
        }
        execvp(app->child_argv[0], app->child_argv);
        _exit(127);
    }
    app->child_pid = pid;
    int flags = fcntl(app->pty_master_fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(app->pty_master_fd, F_SETFL, flags | O_NONBLOCK);
    }
    yinfo("ycompositor-ygui: spawned child pid=%d cmd='%s' winsize=%ux%u cells", pid,
          app->child_argv[0], ws.ws_col, ws.ws_row);
    return 0;
}

static void update_child_winsize(struct yetty_ycompositorygui_app *app)
{
    if (app->pty_master_fd < 0) {
        return;
    }
    struct winsize ws = {0};
    if (app->cell_w_px > 0.0f) {
        ws.ws_col = (unsigned short)(app->surface_w / (uint32_t)app->cell_w_px);
    }
    if (app->cell_h_px > 0.0f) {
        ws.ws_row = (unsigned short)(app->surface_h / (uint32_t)app->cell_h_px);
    }
    if (ws.ws_col == 0) {
        ws.ws_col = 80;
    }
    if (ws.ws_row == 0) {
        ws.ws_row = 24;
    }
    if (ioctl(app->pty_master_fd, TIOCSWINSZ, &ws) != 0) {
        ywarn("ycompositor-ygui: TIOCSWINSZ failed: %s", strerror(errno));
    }
}

/* Drain whatever is currently readable from the child's PTY and feed it
 * to the wire statemachine. The SM scans for DCS envelopes, dispatches
 * the yclass RPC server handler for YETTY_DCS_YCLASS_RPC requests (which
 * mutate app->root and emit responses back over the PTY master), and
 * routes everything else through the analyzer callbacks. Returns 1 if
 * the child closed (EOF). */
static int pump_pty_in(struct yetty_ycompositorygui_app *app)
{
    if (app->pty_master_fd < 0) {
        return 0;
    }
    char buf[8192];
    for (;;) {
        ssize_t n = read(app->pty_master_fd, buf, sizeof(buf));
        if (n > 0) {
            struct yetty_ycore_void_result fr =
                yetty_ywire_wire_statemachine_feed(app->wire_sm, buf, (size_t)n);
            if (YETTY_IS_ERR(fr)) {
                yerror("ycompositor-ygui: wire_statemachine_feed failed: %s", fr.error.msg);
                yetty_ycore_error_destroy(fr.error);
            }
            continue;
        }
        if (n == 0) {
            return 1;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        if (errno == EINTR) {
            continue;
        }
        /* PTY master read errors after the child exits with EIO on Linux
         * — treat as EOF, not a hard error. */
        if (errno == EIO) {
            return 1;
        }
        yerror("ycompositor-ygui: pty read failed: %s", strerror(errno));
        return 1;
    }
}

static void handle_event(struct yetty_ycompositorygui_app *app, const struct yetty_yui_event *ev)
{
    /* Window chrome gets first dibs on pointer events. It only claims caption
     * drags / edge resizes / its own buttons (all outside the app's content),
     * so anything it doesn't consume falls through to the scene below. */
    if (app->chrome && (ev->type == YETTY_YCORE_MOUSE_DOWN || ev->type == YETTY_YCORE_MOUSE_UP ||
                        ev->type == YETTY_YCORE_MOUSE_MOVE || ev->type == YETTY_YCORE_MOUSE_DRAG ||
                        ev->type == YETTY_YCORE_MOUSE_DOUBLE_CLICK)) {
        struct yetty_ycore_int_result cr = yetty_ychrome_host_handle_event(app->chrome, ev);
        int consumed = YETTY_IS_OK(cr) && cr.value;
        if (YETTY_IS_ERR(cr)) {
            yetty_ycore_error_destroy(cr.error);
        }
        if (consumed) {
            return;
        }
    }
    switch (ev->type) {
    case YETTY_YCORE_SHUTDOWN:
    case YETTY_YCORE_WINDOW_CLOSE:
        app->quit = 1;
        return;
    case YETTY_YCORE_RESIZE: {
        uint32_t w = (uint32_t)ev->resize.width;
        uint32_t h = (uint32_t)ev->resize.height;
        if (w == 0 || h == 0) {
            return;
        }
        app->surface_w = w;
        app->surface_h = h;
        struct yetty_ycore_void_result rr = yetty_yframework_reconfigure_surface(app->yrt, w, h);
        if (YETTY_IS_ERR(rr)) {
            yerror("ycompositor-ygui: reconfigure_surface failed: %s", rr.error.msg);
            yetty_ycore_error_destroy(rr.error);
        }
        struct yetty_yrender_viewport vp = {.x = 0, .y = 0, .w = (float)w, .h = (float)h};
        struct yetty_ycore_void_result tr = app->target->ops->resize(app->target, vp);
        if (YETTY_IS_ERR(tr)) {
            yerror("ycompositor-ygui: render_target resize failed: %s", tr.error.msg);
            yetty_ycore_error_destroy(tr.error);
        }
        /* Root container's rect tracks the framebuffer. The producer
         * re-emits the scene at the new dims; child figures move via
         * SET_CHILD_RECT records embedded in that re-emit. */
        yetty_yfigure_figure_rect_set(app->root, (struct yetty_ycore_rectangle){
                                                     .min = {.x = 0.0f, .y = 0.0f},
                                                     .max = {.x = (float)w, .y = (float)h},
                                                 });
        yetty_yfigure_figure_dirty_set(app->root, 1);
        if (app->chrome) {
            struct yetty_ycore_void_result cr =
                yetty_ychrome_host_resized(app->chrome, (float)w, (float)h);
            if (YETTY_IS_ERR(cr)) {
                yetty_ycore_error_destroy(cr.error);
            }
        }
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
            {
                struct yetty_ycore_int_result fr = yetty_ygui_framework_feed_mouse_button(
                    app->ygui, ev->mouse.x, ev->mouse.y, ev->mouse.button, 1, ev->mouse.mods);
                if (YETTY_IS_ERR(fr)) {
                    yetty_ycore_error_destroy(fr.error);
                }
            }
            /* Mouse input may have fired a widget callback that mutated
             * state; re-emit so the visual catches up. */
            struct yetty_ycore_void_result pr = push_ygui_scene(app);
            if (YETTY_IS_ERR(pr)) {
                yerror("ycompositor-ygui: push (mouse_down) failed: %s", pr.error.msg);
                yetty_ycore_error_destroy(pr.error);
            }
        }
        return;
    case YETTY_YCORE_MOUSE_UP:
        if (app->ygui) {
            {
                struct yetty_ycore_int_result fr = yetty_ygui_framework_feed_mouse_button(
                    app->ygui, ev->mouse.x, ev->mouse.y, ev->mouse.button, 0, ev->mouse.mods);
                if (YETTY_IS_ERR(fr)) {
                    yetty_ycore_error_destroy(fr.error);
                }
            }
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
            {
                struct yetty_ycore_int_result fr =
                    yetty_ygui_framework_feed_mouse_motion(app->ygui, ev->mouse.x, ev->mouse.y);
                if (YETTY_IS_ERR(fr)) {
                    yetty_ycore_error_destroy(fr.error);
                }
            }
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

YETTY_ANNOTATE("override@yapp:app:init")
static struct yetty_ycore_void_result ycompositorygui_app_init(struct yetty_yclass_object *obj,
                                                               struct yetty_yclass_object *platform)
{
    (void)obj;
    (void)platform;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@yapp:app:run")
static struct yetty_ycore_void_result ycompositorygui_app_run(struct yetty_yclass_object *obj,
                                                              struct yetty_yclass_object *platform)
{
    struct yetty_ycompositorygui_app_ptr_result app_res = yetty_ycompositorygui_app_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, app_res, "ycompositorygui:app:run: app_from");
    struct yetty_ycompositorygui_app *app = app_res.value;

    struct yetty_yplatform_gpu_context_const_ptr_result gpu_res =
        yetty_yplatform_platform_gpu_context(platform);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, gpu_res, "ycompositorygui:app:run: gpu_context");
    const struct yetty_yplatform_gpu_context *gpu = gpu_res.value;

    struct yetty_ycore_xthread_event_pipe_ptr_result input_pipe_res =
        yetty_yplatform_platform_input_pipe(platform);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, input_pipe_res, "ycompositorygui:app:run: input_pipe");
    struct yetty_ycore_xthread_event_pipe *input_pipe = input_pipe_res.value;

    if (!gpu || !input_pipe) {
        return YETTY_ERR(yetty_ycore_void, "ycompositorygui:app:run: platform state not populated");
    }

    struct yetty_yframework_ptr_result yr = yetty_yframework_create(platform);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, yr, "yframework_create failed");
    app->yrt = yr.value;

    app->ctx.runtime = app->yrt;
    app->ctx.pty_factory = NULL;
    app->ctx.event_loop = app->yrt->event_loop;

    app->surface = gpu->surface;
    app->surface_w = gpu->surface_width;
    app->surface_h = gpu->surface_height;

    /* Replace yframework's render target with a texture target that
     * blits to the GLFW surface on present — same setup as
     * tools/ycompositor. */
    app->yrt->render_target->ops->destroy(app->yrt->render_target);
    app->yrt->render_target = NULL;
    struct yetty_yrender_viewport vp = {
        .x = 0,
        .y = 0,
        .w = (float)app->surface_w,
        .h = (float)app->surface_h,
    };
    struct yetty_yrender_target_ptr_result tr = yetty_yrender_target_texture_create(
        app->yrt->gpu.device, app->yrt->gpu.queue, app->yrt->gpu.surface_format,
        app->yrt->gpu.allocator, (WGPUSurface)app->surface, vp);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, tr, "texture target create failed");
    app->target = tr.value;

    /* Load font first — it's needed as user-data for the yscene factory. */
    {
        struct yetty_yconfig_config *config = app->yrt->config;
        const char *fonts_dir = config->ops->get_string(config, "paths/fonts", "");
        const char *shaders_dir = config->ops->get_string(config, "paths/shaders", "");
        const char *font_family = "DejaVuSansMNerdFontMono";
        char cdb_path[768];
        char shader_path[768];
        snprintf(cdb_path, sizeof(cdb_path), "%s/../msdf-fonts/%s-Regular.cdb", fonts_dir,
                 font_family);
        snprintf(shader_path, sizeof(shader_path), "%s/msdf-font.wgsl", shaders_dir);
        yinfo("ycompositor-ygui: loading font cdb='%s'", cdb_path);
        struct yetty_font_font_result font_result =
            yetty_yfont_msdf_font_create(cdb_path, shader_path, "ycompositor_ygui");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, font_result, "msdf_font_create failed");
        app->font = font_result.value;
        struct yetty_ycore_void_result load_result = app->font->ops->load_basic_latin(app->font);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, load_result, "font load_basic_latin failed");
    }

    /* Registry + root container — in headless-demo mode ygui emits
     * CREATE_CHILD records with KIND_YGRID; in interpose mode the
     * captured app can also emit KIND_YMGUI (the ymgui demo, etc.) so
     * we register both factories then. */
    struct yetty_yfigure_registry_ptr_result reg_r = yetty_yfigure_registry_create();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, reg_r, "yfigure_registry_create failed");
    app->registry = reg_r.value;
    /* Bundle the font into the factory-args struct; no composite
     * factory at this layer — tool is a minimal POC. */
    app->figure_args.default_font = app->font;
    app->figure_args.composite_factory = NULL;
    /* The legacy "ygrid" kind token renders through yscene. Absolute
     * (logical-pane) coordinates — the mode the ygrid factory forced. */
    app->figure_args.absolute_coords = 1;
    struct yetty_ycore_void_result rf = yetty_yscene_register_factory_for_kind(
        app->registry, yetty_yfigure_kind_token("ygrid"), &app->figure_args);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rf, "yscene_register_factory_for_kind failed");

    if (app->child_argv) {
        app->ymgui_factory_args.context = &app->ctx;
        app->ymgui_factory_args.pipeline = NULL;
        struct yetty_ycore_void_result mr =
            yetty_ymgui_register_factory(app->registry, &app->ymgui_factory_args);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, mr, "ymgui_register_factory failed");
    }

    struct yetty_ycore_rectangle root_rect = {
        .min = {.x = 0.0f, .y = 0.0f},
        .max = {.x = (float)app->surface_w, .y = (float)app->surface_h},
    };
    struct yetty_yclass_ctx yclass_ctx = {0};
    struct yetty_yclass_object_ptr_result obj_res = yetty_yfigure_container_create(&yclass_ctx);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, obj_res, "root_container create failed");
    app->root = obj_res.value;
    yetty_yfigure_container_set_context(app->root, &app->ctx);
    yetty_yfigure_container_set_registry(app->root, app->registry);
    yetty_yfigure_container_set_rect(app->root, root_rect);

    if (app->child_argv) {
        /* Interpose mode — no in-process ygui scene. This tool becomes a
         * yclass RPC server: the forked producer obtains a proxy to
         * app->root via RPC_OP_GET_ROOT and drives it with typed yclass
         * stubs over DCS envelopes on YETTY_DCS_YCLASS_RPC.
         *
         * Register the yfigure class accessor/skel lookups so the RPC
         * server can mint and dispatch yfigure classes, then rpc_init
         * (handle minting starts at 1; handle 0 is the invalid sentinel)
         * before publishing the root. */
        struct yetty_ycore_void_result fig_reg = yetty_yfigure_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, fig_reg, "yfigure_register failed");
        struct yetty_ycore_void_result rpc_init_r = yetty_yclass_rpc_init();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_init_r, "rpc_init failed");

        /* Wire statemachine fed from the producer's PTY output. NULL PTY
         * — the tool pushes bytes in via wire_statemachine_feed; the SM
         * never pulls bytes itself. */
        struct yetty_ywire_wire_statemachine_ptr_result sm_r =
            yetty_ywire_wire_statemachine_create(NULL);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sm_r, "wire_statemachine_create failed");
        app->wire_sm = sm_r.value;

        /* Analyzer logging for every non-RPC envelope (the RPC code is
         * consumed by the RPC server handler attached below) plus raw
         * bytes outside any envelope. */
        struct yetty_ycore_void_result env_r =
            yetty_ywire_wire_statemachine_set_envelope_default_buffered(
                app->wire_sm, /*has_args=*/1, on_envelope, app);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, env_r, "set_envelope_default_buffered failed");
        struct yetty_ycore_void_result raw_r =
            yetty_ywire_wire_statemachine_set_default_buffered(app->wire_sm, on_raw, app);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, raw_r, "set_default_buffered failed");

        /* Attach the yclass RPC DCS server to the SAME statemachine. Each
         * inbound DCS envelope on YETTY_DCS_YCLASS_RPC is one request:
         * the handler dispatches it and ships the response back to the
         * producer via on_rpc_response (PTY master write). */
        struct yetty_yclass_rpc_dcs_server_ptr_result dcs_r = yetty_yclass_rpc_dcs_server_attach(
            app->wire_sm, YETTY_DCS_YCLASS_RPC, /*compressed=*/0, on_rpc_response, app);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dcs_r, "dcs_server_attach failed");
        app->dcs_rpc_server = dcs_r.value;

        /* Publish the root container so the producer reaches it via
         * RPC_OP_GET_ROOT. */
        struct yetty_yclass_handle_result root_r = yetty_yclass_rpc_set_root(app->root);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, root_r, "rpc_set_root failed");
        yinfo("ycompositor-ygui: RPC server ready (root handle=%llu, dcs code=%d)",
              (unsigned long long)root_r.value, YETTY_DCS_YCLASS_RPC);

        if (spawn_child(app) != 0) {
            return YETTY_ERR(yetty_ycore_void, "spawn_child failed");
        }
    } else {
        /* Headless ygui framework — same path yui uses (no libuv loop,
         * no PTY; the per-frame envelope ships in-process into app->root
         * via set_container_obj). Built once at startup; the widget tree
         * stays across resizes, only the viewport changes. */
        struct yetty_yclass_object_ptr_result eng_r = yetty_ygui_framework_create(NULL);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, eng_r, "ygui framework alloc failed");
        app->ygui = eng_r.value;
        struct yetty_ycore_void_result scr =
            yetty_ygui_framework_set_container_obj(app->ygui, app->root);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, scr, "framework set_container_obj failed");
        struct yetty_ycore_void_result scene_res = build_scene(app);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, scene_res, "build_scene failed");
        if (app->win) {
            struct yetty_ycore_void_result rootr =
                yetty_ygui_framework_set_root(app->ygui, app->win);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, rootr, "framework set_root failed");
        }

        struct yetty_ycore_void_result pr0 = push_ygui_scene(app);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, pr0, "initial push_ygui_scene failed");
    }

    /* Window chrome: draggable/resizable titlebar + min/max/close, composited
     * as a pinned figure over the scene. */
    {
        struct yetty_ychrome_host_ptr_result chrome_r = yetty_ychrome_host_create(
            app->root, app->font, &app->ctx, app->yrt->window_chrome, (float)app->surface_w,
            (float)app->surface_h, 34.0f, 8.0f, YETTY_YCHROME_FLAG_ALL);
        if (YETTY_IS_OK(chrome_r)) {
            app->chrome = chrome_r.value;
        } else {
            ywarn("ycompositor-ygui: chrome host create failed: %s", chrome_r.error.msg);
            yetty_ycore_error_destroy(chrome_r.error);
        }
    }

    /* Event-driven render loop — polls the platform input pipe (window
     * events) plus, in interpose mode, the child's PTY master so OSC
     * envelopes arrive in the same iteration. */
    struct yetty_ycore_int_result fdr = input_pipe->ops->read_fd(input_pipe);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, fdr, "pipe read_fd failed");
    int pipe_fd = fdr.value;

    int needs_render = 1;
    while (!app->quit) {
        struct pollfd pfds[2];
        pfds[0].fd = pipe_fd;
        pfds[0].events = POLLIN;
        pfds[0].revents = 0;
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
                    input_pipe->ops->read(input_pipe, &ev, sizeof(ev));
                if (YETTY_IS_ERR(rr) || rr.value != sizeof(ev)) {
                    break;
                }
                handle_event(app, &ev);
                had_events = 1;
            }
        }
        if (pr > 0 && nfds == 2 && (pfds[1].revents & (POLLIN | POLLHUP))) {
            int eof = pump_pty_in(app);
            had_events = 1;
            if (eof) {
                yinfo("ycompositor-ygui: child PTY closed (env_total=%llu "
                      "other=%llu raw_bytes=%llu)",
                      (unsigned long long)app->n_osc_total, (unsigned long long)app->n_osc_other,
                      (unsigned long long)app->n_bytes_in);
                close(app->pty_master_fd);
                app->pty_master_fd = -1;
            }
        }
        if (gpu->instance) {
            wgpuInstanceProcessEvents(gpu->instance);
        }
        if (!(needs_render || had_events || yetty_yfigure_figure_dirty_get(app->root).value)) {
            continue;
        }

        struct yetty_ydraw_target *target = app->target;
        struct yetty_ycore_void_result cl = target->ops->clear(target);
        if (YETTY_IS_ERR(cl)) {
            yerror("ycompositor-ygui: clear failed: %s", cl.error.msg);
            yetty_ycore_error_destroy(cl.error);
        }
        struct yetty_ycore_void_result rr = yetty_yfigure_render(app->root, target);
        if (YETTY_IS_ERR(rr)) {
            yerror("ycompositor-ygui: root render failed: %s", rr.error.msg);
            yetty_ycore_error_destroy(rr.error);
        } else {
            yetty_yfigure_figure_dirty_set(app->root, 0);
        }
        struct yetty_ycore_void_result pp = target->ops->present(target);
        if (YETTY_IS_ERR(pp)) {
            yerror("ycompositor-ygui: present failed: %s", pp.error.msg);
            yetty_ycore_error_destroy(pp.error);
        }
        needs_render = 0;
    }

    /* Chrome engine (its caption figure is owned by the container, freed with
     * it below). */
    if (app->chrome) {
        struct yetty_ycore_void_result dc = yetty_ychrome_host_destroy(app->chrome);
        if (YETTY_IS_ERR(dc)) {
            yetty_ycore_error_destroy(dc.error);
        }
        app->chrome = NULL;
    }

    /* Teardown — root container first so any pending GPU work bound
     * to the runtime's device flushes before yframework_destroy. */
    {
        struct yetty_ycore_void_result dr = yetty_yfigure_destroy(app->root);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dr, "root destroy");
    }
    app->root = NULL;
    if (app->registry) {
        struct yetty_ycore_void_result r = yetty_yfigure_registry_destroy(app->registry);
        if (YETTY_IS_ERR(r)) {
            yetty_ycore_error_destroy(r.error);
        }
        app->registry = NULL;
    }

    app->target->ops->destroy(app->target);
    app->target = NULL;

    if (app->ygui) {
        struct yetty_ycore_void_result er = yetty_ygui_framework_destroy(app->ygui);
        if (YETTY_IS_ERR(er)) {
            yerror("ycompositor-ygui: ygui framework destroy failed: %s", er.error.msg);
            yetty_ycore_error_destroy(er.error);
        }
        app->ygui = NULL;
    }

    /* Destroy the wire statemachine first — it borrows the RPC server as
     * its handler userdata, so the server is only safe to free once the
     * SM (and its registered handler) is gone. */
    if (app->wire_sm) {
        struct yetty_ycore_void_result dr = yetty_ywire_wire_statemachine_destroy(app->wire_sm);
        if (YETTY_IS_ERR(dr)) {
            yerror("ycompositor-ygui: wire_statemachine destroy failed: %s", dr.error.msg);
            yetty_ycore_error_destroy(dr.error);
        }
        app->wire_sm = NULL;
    }
    yetty_yclass_rpc_dcs_server_destroy(app->dcs_rpc_server);
    app->dcs_rpc_server = NULL;
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
        if (YETTY_IS_ERR(rr)) {
            yetty_ycore_error_destroy(rr.error);
        }
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
    struct yetty_ycore_void_result platform_reg = yetty_yplatform_register();
    if (YETTY_IS_ERR(platform_reg)) {
        yetty_ycore_error_print(stderr, "ycompositor-ygui: platform register", platform_reg.error);
        yetty_ycore_error_destroy(platform_reg.error);
        return 1;
    }
    struct yetty_ycore_void_result yapp_reg = yetty_yapp_register();
    if (YETTY_IS_ERR(yapp_reg)) {
        yetty_ycore_error_print(stderr, "ycompositor-ygui: yapp register", yapp_reg.error);
        yetty_ycore_error_destroy(yapp_reg.error);
        return 1;
    }

    struct yetty_yclass_object_ptr_result app_res = yetty_ycompositorygui_app_create(NULL);
    if (YETTY_IS_ERR(app_res)) {
        yetty_ycore_error_print(stderr, "ycompositor-ygui: app create", app_res.error);
        yetty_ycore_error_destroy(app_res.error);
        return 1;
    }
    struct yetty_ycompositorygui_app_ptr_result app_data =
        yetty_ycompositorygui_app_from(app_res.value);
    if (YETTY_IS_ERR(app_data)) {
        yetty_ycore_error_print(stderr, "ycompositor-ygui: app data", app_data.error);
        yetty_ycore_error_destroy(app_data.error);
        return 1;
    }
    struct yetty_ycompositorygui_app *app = app_data.value;
    app->cell_w_px = 8.0f;
    app->cell_h_px = 16.0f;
    app->pty_master_fd = -1;
    app->child_pid = -1;

    /* `-e <cmd> [args...]`: everything after -e is the child argv. We still let
     * yconfig see the leading argv slice (it parses its own flags), so we splice
     * -e out by setting argc to the index of -e and stashing the tail on the app
     * for run(). */
    int trimmed_argc = argc;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-e") == 0 && i + 1 < argc) {
            app->child_argv = &argv[i + 1];
            app->child_argc = argc - (i + 1);
            trimmed_argc = i; /* hide -e and tail from yconfig */
            break;
        }
    }

    struct yetty_yclass_object_ptr_result platform_res = yetty_yplatform_glfw_platform_create(NULL);
    if (YETTY_IS_ERR(platform_res)) {
        yetty_ycore_error_print(stderr, "ycompositor-ygui: platform create", platform_res.error);
        yetty_ycore_error_destroy(platform_res.error);
        return 1;
    }

    struct yetty_ycore_void_result run_result =
        yetty_yplatform_platform_run(platform_res.value, app_res.value, trimmed_argc, argv);
    if (YETTY_IS_ERR(run_result)) {
        yetty_ycore_error_print(stderr, "ycompositor-ygui: run", run_result.error);
        yetty_ycore_error_destroy(run_result.error);
        return 1;
    }
    return 0;
}

#include "yetty/gen/impl/ycompositorygui/main.c"
