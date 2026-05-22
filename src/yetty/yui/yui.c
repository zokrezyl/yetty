/* yui.c — app-level yui singleton.
 *
 * See yui.h for the producer → transport → consumer chain. This file owns
 * the wiring; the ydraw-layer (KIND_SCENE) does the rendering and the
 * osc_statemachine does the wire decode. Memory-pty bytes flow
 * producer→consumer via in-process ring buffers and the event loop is
 * woken via post_to_loop so the wake never re-enters synchronously
 * (works the same in same-thread mode and once threading splits later).
 */

#include "yui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/ygui/ygui.h>
#include <yetty/yetty/yetty.h>

/* yui-only entry into ygui — declared in src/yetty/ygui/ygui_internal.h.
 * The public ygui.h is the only header outside-of-ygui code includes,
 * so forward-declare here instead of widening the public API. */
struct ygui_engine_ptr_result yetty_ygui_engine_internal_alloc_for_yui(
    const char *name, struct yetty_ygui_theme *theme);
#include <yetty/yevent/event-loop.h>
#include <yetty/ynotify/ynotify.h>
#include <yetty/yplatform/pty.h>
#include <yetty/yplatform/thread.h>
#include <yetty/yrender/gpu-allocator.h>
#include <yetty/yrender/render-target.h>
#include <yetty/yterm/osc-codes.h>
#include <yetty/ywire/wire-statemachine.h>
#include <yetty/yterm/terminal.h>
#include <yetty/yterm/ydraw-layer.h>
#include <yetty/ytrace/ytrace.h>
#include <yetty/ywebgpu/utils.h>
#include <yetty/yui/workspace.h>
#include <yetty/yevent/event.h>
#include <yetty/yevent/dispatch.h>
#include <yetty/yplatform/platform-input-pipe.h>
#include <yetty/yplatform/window-manager.h>

#include "config-dialog.h"
#include "tabbar.h"

struct yetty_yui {
    /* Producer-side endpoint. ygui's output flows in here via
     * pty->ops->write. */
    struct yetty_platform_pty *yui_endpoint;

    /* Consumer-side endpoint. The SM reads from here via pty->ops->read
     * (zero copy from the memory-pty's ring). */
    struct yetty_platform_pty *render_endpoint;

    /* Owns the YDRAW decode pipeline (b64 + lz4 today, kept as-is per
     * the simplified plan — we do NOT change the wire codec yet, only
     * the transport). Bound to render_endpoint. */
    struct yetty_ywire_wire_statemachine *sm;

    /* Static-canvas ydraw layer registered against YDRAW_CLEAR/BIN/OVERLAY
     * on `sm`. Same constructor used by the per-terminal static placeholder. */
    struct yetty_yrender_terminal_layer *layer;

    /* Producer engine. Its OSC output is routed via output_pty into
     * `yui_endpoint`; bytes flow through the memory pty → render side
     * → SM → layer. */
    struct yetty_ygui_engine *engine;

    /* Single hamburger / app menu, parked on `engine` and starting
     * closed. Drill-down levels reuse the same popup widget — when
     * the user clicks "New view ▸" the callback clears the items in
     * place and re-populates with the view choices + a `<` back
     * handler that restores the root level. The "current level" is
     * tracked by app_menu_level so the back handler knows where to
     * land. Dialog widget pointers are needed so item callbacks can
     * address-by-name. */
    struct yetty_ygui_widget *app_menu;
    int app_menu_level; /* 0 = root, 1 = "New view" submenu */
    struct yetty_ygui_widget *dialogs[YETTY_YUI_VIEW_KIND_COUNT]; /* indexed by view_kind */

    /* Per-dialog textinput handles, indexed by [view_kind][field_idx]
     * matching s_views[kind].fields[]. yui_dialog_connect / the connect
     * subscribers read these to recover what the user typed. NULL for
     * unused slots (kinds with fewer fields, or kinds with no dialog). */
    struct yetty_ygui_widget *dialog_inputs[YETTY_YUI_VIEW_KIND_COUNT][4];

    /* GPU info dialog — opened from the app menu's "GPU info…" item.
     * Read-only textarea summarising WebGPU adapter info (vendor,
     * device, backend, limits) and live GPU-allocator stats. The
     * adapter + allocator pointers are stashed at create so the
     * "Refresh" button can re-fetch on demand. Borrowed pointers; both
     * are owned by the parent yetty context which outlives yui. */
    struct yetty_ygui_widget *gpu_info_dialog;
    struct yetty_ygui_widget *gpu_info_textarea;
    WGPUAdapter gpu_info_adapter;
    const struct yetty_ydraw_gpu_allocator *gpu_info_allocator;

    /* Settings dialog — opened from the hamburger menu's and context
     * menu's "Settings…" item. Two-pane window (config tree on the
     * left, selected branch's leaves on the right); construction +
     * tree walking + the on-toggle handler all live in config-dialog.c
     * so this file doesn't grow another 80 lines of widget tree. */
    struct yetty_yui_config_dialog *config_dialog;

    /* Bound tabbar model. The native ygui TABBAR widget (used by the
     * yui titlebar) reads from this and calls its mutators on
     * close-x / "+" / tab-switch. yui owns the reconciliation. */
    struct yetty_yui_tabbar *tabbar_model;

    /* Engine titlebar — flex-row hbox pinned to the top of the canvas
     * via engine_set_titlebar. Composition:
     *   [≡ hamburger][native TABBAR widget (flex:1)][min][max][close]
     * The native tabbar paints its own tabs + close-x + optional "+"
     * pill; yui keeps its tab count + active index in sync with the
     * tabbar_model on every render. */
    struct yetty_ygui_widget *titlebar;
    struct yetty_ygui_widget *titlebar_hamburger;
    struct yetty_ygui_widget *titlebar_tabbar;   /* native engine_tabbar widget */
    struct yetty_ygui_widget *titlebar_min;
    struct yetty_ygui_widget *titlebar_max;
    struct yetty_ygui_widget *titlebar_close;
    int titlebar_synced_active;
    size_t titlebar_synced_count;

    /* Application statusbar — STATUSBAR widget pinned to the bottom of
     * the engine's canvas via engine_set_statusbar. The widget paints
     * the brand-themed strip (bg_header band + top hairline) plus
     * vertically-centered left/right text via its own render. The
     * default render_all walks children too, so apps can layer ygui
     * widgets on top via yetty_yui_statusbar(). */
    struct yetty_ygui_widget *statusbar;

    /* Connect dispatch — invoked from each dialog's "Connect" button. */
    yetty_yui_connect_cb connect_cb;
    void               *connect_userdata;

    /* Split dispatch — invoked when the user picks a view kind under
     * the context menu's "Split V/H ▸" submenu. The host (yetty.c)
     * splits the focused pane with the given orientation and creates
     * the right view in the new sibling. */
    yetty_yui_split_cb split_cb;
    void              *split_userdata;

    /* Cached for the memory-pty wake bridge. */
    struct yetty_yevent_event_loop *loop;

    /* Borrowed context pointer — needed for posting events (input pipe
     * lives at ctx->app_context.platform_input_pipe). yetty owns the
     * context and outlives yui. */
    const struct yetty_context *ctx;

    /* Cached surface dims so the per-split splitter sync knows the
     * workspace area. Updated by yetty_yui_resize. */
    float surface_w;
    float surface_h;

    /* Per-split visual divider widgets. yui creates one engine_splitter
     * per yetty_yui_split node in the active workspace and positions it
     * absolutely on the boundary between the two child tiles. Drags fire
     * a callback that posts SPLIT_RESIZE so the workspace re-lays out.
     * Keyed by tile_id; reconciled every render against the active
     * workspace's tile tree. */
    struct yetty_yui_splitter_entry {
        yetty_ycore_object_id split_id;     /* tile_id of the yui_split */
        yetty_ycore_object_id workspace_id; /* parent workspace id */
        struct yetty_ygui_widget *widget;
        struct yetty_yui *yui;              /* back-pointer for callback */
        struct yetty_yui_splitter_thunk *thunk; /* owned; passed as widget cb userdata */
        int seen;                           /* per-sync mark */
    } *splitters;
    size_t splitter_count;
    size_t splitter_cap;

    /* Last cursor shape we asked the OS to display. Cached so the
     * MOUSE_MOVE → cursor decision can skip the cross-thread post when
     * nothing changed since last frame. -1 means "not initialised yet". */
    int last_cursor_shape;

    /* Cell stride captured at create — `resize` recomputes grid cols/rows
     * from new surface dims but leaves cell size alone. */
    float cell_w;
    float cell_h;
};

/*===========================================================================
 * ynotify bridge — yui registers itself as the global notification handler
 * so any subsystem can call ynotify() and have the toast surface here.
 *
 * The handler can be invoked from any thread, so we trampoline through
 * post_to_loop into the event-loop thread before touching ygui state.
 * yui is a process-wide singleton in practice; we store the active one
 * in `s_active_yui` so a thunk that lands after yui_destroy can just
 * drop the message instead of dereferencing a freed pointer.
 *===========================================================================*/

static struct yetty_yui *s_active_yui = NULL;
static struct yetty_yplatform_ymutex *s_active_yui_mutex = NULL;

struct yui_ynotify_thunk {
    int      severity;
    uint32_t ttl_ms;     /* 0 = use severity default */
    char     msg[];      /* NUL-terminated */
};

static void yui_active_lock(void)
{
    if (!s_active_yui_mutex) {
        s_active_yui_mutex = yetty_yplatform_ymutex_create();
    }
    yetty_yplatform_ymutex_lock(s_active_yui_mutex);
}

static void yui_active_unlock(void)
{
    yetty_yplatform_ymutex_unlock(s_active_yui_mutex);
}

/* Runs on the event-loop thread. Reads the current yui under the lock
 * to defeat the obvious destroy/dispatch race. */
static void yui_ynotify_dispatch(void *arg)
{
    struct yui_ynotify_thunk *t = arg;
    if (!t) {
        return;
    }
    yui_active_lock();
    struct yetty_yui *yui = s_active_yui;
    yui_active_unlock();

    if (yui && yui->engine) {
        if (t->ttl_ms > 0) {
            yetty_ygui_engine_notify_ttl(yui->engine,
                                         (enum yetty_ygui_severity)t->severity,
                                         t->ttl_ms, "%s", t->msg);
        } else {
            yetty_ygui_engine_notify(yui->engine,
                                     (enum yetty_ygui_severity)t->severity,
                                     "%s", t->msg);
        }
    }
    free(t);
}

/* Installed via ynotify_set_handler. userdata is the event loop pointer
 * (which outlives yui — yetty owns the loop, yui is a child of yetty).
 * Producers may call ynotify from any thread, so we never touch yui
 * directly here; we just post to the loop and let dispatch find the
 * active yui (or skip if none). */
static void yui_ynotify_handler(int severity, const char *msg, void *userdata)
{
    struct yetty_yevent_event_loop *loop = userdata;
    if (!loop || !loop->ops || !loop->ops->post_to_loop || !msg) {
        return;
    }
    size_t mlen = strlen(msg) + 1;
    struct yui_ynotify_thunk *t = malloc(sizeof(*t) + mlen);
    if (!t) {
        return;
    }
    t->severity = severity;
    t->ttl_ms   = 0;
    memcpy(t->msg, msg, mlen);
    loop->ops->post_to_loop(loop, yui_ynotify_dispatch, t);
}

/*===========================================================================
 * View kind metadata
 *===========================================================================*/

struct view_meta {
    const char *title;
    const char *id_prefix;
    int         num_fields; /* number of textinput rows; Shell has 0. */
    struct {
        const char *label;
        const char *id_suffix;
        const char *placeholder;
        const char *default_text;
    } fields[4];
};

static const struct view_meta s_views[YETTY_YUI_VIEW_KIND_COUNT] = {
    [YETTY_YUI_VIEW_SHELL] = {
        /* SHELL has no dialog — the v-menu item spawns the default shell
         * directly. Keep num_fields=0 so the dialog-builder loop skips it
         * entirely. */
        .title = "Open local shell",
        .id_prefix = "yui_dlg_shell",
        .num_fields = 0,
        .fields = {{0}},
    },
    [YETTY_YUI_VIEW_SSH] = {
        .title = "Open SSH",
        .id_prefix = "yui_dlg_ssh",
        .num_fields = 3,
        .fields = {
            {"Host",     "/host", "user@host", ""},
            {"Port",     "/port", "22",        "22"},
            {"Key path", "/key",  "~/.ssh/id_rsa", ""},
        },
    },
    [YETTY_YUI_VIEW_TELNET] = {
        .title = "Open Telnet",
        .id_prefix = "yui_dlg_telnet",
        .num_fields = 2,
        .fields = {
            {"Host", "/host", "host", ""},
            {"Port", "/port", "23",   "23"},
        },
    },
    [YETTY_YUI_VIEW_YVNC] = {
        .title = "Open yVNC",
        .id_prefix = "yui_dlg_yvnc",
        .num_fields = 2,
        .fields = {
            {"Host", "/host", "host", ""},
            {"Port", "/port", "5900", "5900"},
        },
    },
    [YETTY_YUI_VIEW_EXEC] = {
        /* Single-field dialog: the user types a command line (executable
         * path + optional args). yetty.c stuffs that into the
         * `shell/command` config key before spawning a SHELL tab, so the
         * PTY's get_shell_argv tokenizes it instead of running $SHELL. */
        .title = "Run a command",
        .id_prefix = "yui_dlg_exec",
        .num_fields = 1,
        .fields = {
            {"Command", "/cmd", "/usr/bin/htop", ""},
        },
    },
};

/*===========================================================================
 * Menu / dialog callbacks
 *===========================================================================*/

static void yui_menu_open_dialog(struct yetty_ygui_widget *item, void *userdata);
static void yui_menu_spawn_shell(struct yetty_ygui_widget *item, void *userdata);
static void yui_dialog_connect(struct yetty_ygui_widget *button, void *userdata);
static void yui_dialog_cancel(struct yetty_ygui_widget *button, void *userdata);
static void yui_app_menu_open_new_view(struct yetty_ygui_widget *item, void *userdata);
static void yui_app_menu_open_gpu_info(struct yetty_ygui_widget *item, void *userdata);
static void yui_app_menu_back_to_root(struct yetty_ygui_widget *item, void *userdata);
static void yui_app_menu_populate_root(struct yetty_yui *yui);
static void yui_app_menu_populate_new_view(struct yetty_yui *yui);
static void yui_app_menu_populate_context_root(struct yetty_yui *yui);
static void yui_app_menu_populate_split_kind(struct yetty_yui *yui, int horizontal);
static void yui_context_open_split_vertical(struct yetty_ygui_widget *item, void *userdata);
static void yui_context_open_split_horizontal(struct yetty_ygui_widget *item, void *userdata);
static void yui_split_kind_action(struct yetty_ygui_widget *item, void *userdata);
static void yui_split_back_to_context(struct yetty_ygui_widget *item, void *userdata);
static void yui_gpu_info_refresh(struct yetty_ygui_widget *button, void *userdata);
static void yui_gpu_info_close(struct yetty_ygui_widget *button, void *userdata);
static void yui_app_menu_open_settings(struct yetty_ygui_widget *item, void *userdata);

/* Titlebar (ygui-driven). build runs once at yui_create when the engine
 * is up; sync runs every render to reconcile widgets with the tabbar
 * model. */
static void yui_titlebar_build(struct yetty_yui *yui);
static void yui_titlebar_sync(struct yetty_yui *yui);
static void yui_titlebar_on_hamburger(struct yetty_ygui_widget *btn, void *userdata);
static void yui_titlebar_on_new_tab(struct yetty_ygui_widget *btn, void *userdata);
static void yui_titlebar_on_min(struct yetty_ygui_widget *btn, void *userdata);
static void yui_titlebar_on_max(struct yetty_ygui_widget *btn, void *userdata);
static void yui_titlebar_on_close_window(struct yetty_ygui_widget *btn, void *userdata);
static void yui_titlebar_on_tab_change(struct yetty_ygui_widget *tabbar, float value,
                                       void *userdata);
static void yui_titlebar_on_tab_close(struct yetty_ygui_widget *tabbar, float value,
                                      void *userdata);

/* Per-kind callback bundle for split-from-context. Like s_cb_ctx but
 * stashes orientation too. Indexed [orientation 0=vertical/1=horizontal][kind]. */
struct yui_split_ctx {
    struct yetty_yui *yui;
    enum yetty_yui_view_kind kind;
    int horizontal;
};
static struct yui_split_ctx s_split_ctx[2][YETTY_YUI_VIEW_KIND_COUNT];

/* Per-callback bundle: which yui, which kind. Lives for the engine's
 * lifetime — freed in destroy. */
struct yui_cb_ctx {
    struct yetty_yui     *yui;
    enum yetty_yui_view_kind kind;
};

static struct yui_cb_ctx s_cb_ctx[YETTY_YUI_VIEW_KIND_COUNT];

/*===========================================================================
 * Memory-pty wake → event loop bridge
 *
 * post_to_loop is thread-safe in both directions (caller thread + loop
 * thread) and always defers — it is the right primitive even in
 * same-thread mode, because waking synchronously inside pty->ops->write
 * would re-enter dispatch in the middle of a write. Stays identical
 * once yui moves to its own thread.
 *===========================================================================*/

static void yui_drain_cb(void *arg)
{
    struct yetty_yui *yui = arg;
    if (!yui || !yui->sm) {
        return;
    }
    struct yetty_ycore_void_result r = yetty_ywire_wire_statemachine_process(yui->sm);
    if (!YETTY_IS_OK(r)) {
        ywarn("yui: SM process failed: %s", r.error.msg);
        yetty_ycore_error_destroy(r.error);
    }
}

static void yui_wake(void *userdata)
{
    struct yetty_yui *yui = userdata;
    if (!yui || !yui->loop || !yui->loop->ops || !yui->loop->ops->post_to_loop) {
        return;
    }
    yui->loop->ops->post_to_loop(yui->loop, yui_drain_cb, yui);
}

/*===========================================================================
 * Lifecycle
 *===========================================================================*/

struct yetty_yui_ptr_result yetty_yui_create(const struct yetty_context *context,
                                             uint32_t surface_w, uint32_t surface_h, float cell_w,
                                             float cell_h)
{
    if (!context) {
        return YETTY_ERR(yetty_yui_ptr, "yui_create: NULL context");
    }
    if (cell_w <= 0.0f || cell_h <= 0.0f) {
        return YETTY_ERR(yetty_yui_ptr, "yui_create: bad cell size");
    }
    if (surface_w == 0 || surface_h == 0) {
        return YETTY_ERR(yetty_yui_ptr, "yui_create: zero surface");
    }

    struct yetty_yui *yui = calloc(1, sizeof(*yui));
    if (!yui) {
        return YETTY_ERR(yetty_yui_ptr, "yui_create: alloc failed");
    }
    yui->loop = context->event_loop;
    yui->ctx = context;
    yui->surface_w = (float)surface_w;
    yui->surface_h = (float)surface_h;
    yui->last_cursor_shape = -1;
    yui->cell_w = cell_w;
    yui->cell_h = cell_h;

    /* Memory-pty pair — default 16 MiB per direction. */
    struct yetty_yplatform_memory_pty_pair_result pp =
        yetty_yplatform_memory_pty_pair_create(0);
    if (!YETTY_IS_OK(pp)) {
        free(yui);
        return YETTY_ERR(yetty_yui_ptr, "yui_create: memory pty pair", pp);
    }
    yui->yui_endpoint = pp.value.a;
    yui->render_endpoint = pp.value.b;

    /* Scene-canvas ydraw layer. yui primitives are absolute-pixel, so
     * we don't care about the grid stride for layout — only that the
     * canvas's `cols * cell_w x rows * cell_h` equals the framebuffer
     * exactly (otherwise the shader's NDC→pixel map stretches and
     * primitives pinned to the edges, like the statusbar at H-22, get
     * shifted/clipped). Algorithm: round the requested cell stride
     * against the framebuffer to pick integer cols/rows, then re-derive
     * actual cell stride as `pixel / cells`. */
    uint32_t cols = (uint32_t)((float)surface_w / cell_w + 0.5f);
    uint32_t rows = (uint32_t)((float)surface_h / cell_h + 0.5f);
    if (cols == 0) {
        cols = 1;
    }
    if (rows == 0) {
        rows = 1;
    }
    float actual_cell_w = (float)surface_w / (float)cols;
    float actual_cell_h = (float)surface_h / (float)rows;

    struct yetty_yterm_terminal_layer_result lr = yetty_yterm_ydraw_layer_create(
        YETTY_YDRAW_LAYER_KIND_SCENE, cols, rows, actual_cell_w, actual_cell_h, context,
        /*request_render_fn=*/NULL, /*request_render_userdata=*/NULL,
        /*scroll_fn=*/NULL, /*scroll_userdata=*/NULL,
        /*cursor_fn=*/NULL, /*cursor_userdata=*/NULL);
    if (!YETTY_IS_OK(lr)) {
        yui->yui_endpoint->ops->destroy(yui->yui_endpoint);
        yui->render_endpoint->ops->destroy(yui->render_endpoint);
        free(yui);
        return YETTY_ERR(yetty_yui_ptr, "yui_create: layer create", lr);
    }
    yui->layer = lr.value;

    /* SM bound to the consumer-side endpoint. */
    struct yetty_ywire_wire_statemachine_ptr_result sr =
        yetty_ywire_wire_statemachine_create(yui->render_endpoint);
    if (!YETTY_IS_OK(sr)) {
        if (yui->layer && yui->layer->ops && yui->layer->ops->destroy) {
            yui->layer->ops->destroy(yui->layer);
        }
        yui->yui_endpoint->ops->destroy(yui->yui_endpoint);
        yui->render_endpoint->ops->destroy(yui->render_endpoint);
        free(yui);
        return YETTY_ERR(yetty_yui_ptr, "yui_create: SM create", sr);
    }
    yui->sm = sr.value;

    /* Register YDRAW codes against the SM. ygui's producer (ygui_osc.c)
     * emits SCENE_BIN — that's the one that must be wired for the v-menu
     * to reach the scene canvas. CLEAR/BIN/OVERLAY are kept registered so
     * future producers that target the same yui layer (e.g. an external
     * yface tool reusing this transport) still work. */
    struct yetty_ycore_void_result rr;
    rr = yetty_ywire_wire_statemachine_register(yui->sm, YETTY_OSC_YDRAW_CLEAR, yui->layer);
    YETTY_RETURN_IF_ERR(yetty_yui_ptr, rr, "yui_create: register CLEAR");
    rr = yetty_ywire_wire_statemachine_register(yui->sm, YETTY_OSC_YDRAW_BIN, yui->layer);
    YETTY_RETURN_IF_ERR(yetty_yui_ptr, rr, "yui_create: register BIN");
    rr = yetty_ywire_wire_statemachine_register(yui->sm, YETTY_OSC_YDRAW_OVERLAY, yui->layer);
    YETTY_RETURN_IF_ERR(yetty_yui_ptr, rr, "yui_create: register OVERLAY");
    rr = yetty_ywire_wire_statemachine_register(yui->sm, YETTY_OSC_YDRAW_SCENE_BIN, yui->layer);
    YETTY_RETURN_IF_ERR(yetty_yui_ptr, rr, "yui_create: register SCENE_BIN");

    /* Wake the consumer side via post_to_loop whenever the producer
     * writes — defers even in same-thread mode, so the wake never
     * re-enters dispatch mid-write. */
    yetty_yplatform_memory_pty_set_wake(yui->render_endpoint, yui_wake, yui);

    /* Producer engine. yui is in-process — no parent yetty over a pty,
     * no stdin polling, no handshake. Allocation-only constructor, then
     * we plug in the memory pty and the display size directly. */
    (void)cols;
    (void)rows;
    struct ygui_engine_ptr_result er =
        yetty_ygui_engine_internal_alloc_for_yui("yui", /*theme=*/NULL);
    if (YETTY_IS_OK(er)) {
        yui->engine = er.value;
        yetty_ygui_engine_set_output_pty(yui->engine, yui->yui_endpoint);
        yetty_ygui_engine_set_display_pixel_size(yui->engine, (float)surface_w, (float)surface_h);

        /* Build the single hamburger / app menu, then one config dialog
         * per view kind. The menu starts closed and at the root level;
         * tabbar's hamburger click opens it via
         * yetty_yui_show_view_menu, and the drill-down items swap the
         * contents in place. Each dialog is positioned roughly under
         * the hamburger button with a small offset so successive
         * dialogs don't stack on top of one another. */
        yui->app_menu = yetty_ygui_engine_popup_menu(yui->engine, "yui_app_menu",
                                                     /*x=*/0, /*y=*/0,
                                                     /*w=*/240);
        yui->app_menu_level = 0;
        if (yui->app_menu) {
            yetty_ygui_widget_popup_menu_set_modal(yui->app_menu, 0);
        }

        for (int k = 0; k < YETTY_YUI_VIEW_KIND_COUNT; k++) {
            s_cb_ctx[k].yui = yui;
            s_cb_ctx[k].kind = (enum yetty_yui_view_kind)k;
            /* Seed the per-(orientation, kind) bundle used by the
             * right-click context menu's split items. */
            for (int o = 0; o < 2; o++) {
                s_split_ctx[o][k].yui = yui;
                s_split_ctx[o][k].kind = (enum yetty_yui_view_kind)k;
                s_split_ctx[o][k].horizontal = o; /* 0 = vertical, 1 = horizontal */
            }

            /* SHELL has no dialog — its menu item spawns the default shell
             * directly. Skip the widget allocation entirely. */
            if (s_views[k].num_fields == 0) {
                yui->dialogs[k] = NULL;
                continue;
            }

            /* Each dialog is a top-level window — created with placeholder
             * geometry; the body widget below collects the field rows. */
            char dlg_id[64];
            snprintf(dlg_id, sizeof(dlg_id), "%s", s_views[k].id_prefix);
            struct yetty_ygui_widget *dlg = yetty_ygui_engine_window(
                yui->engine, dlg_id, 80.0f + (float)k * 12.0f, 60.0f, 360.0f, 220.0f,
                s_views[k].title);
            yui->dialogs[k] = dlg;
            if (!dlg) {
                continue;
            }
            yetty_ygui_widget_set_visible(dlg, 0);

            struct yetty_ygui_widget *body = yetty_ygui_widget_window_body(dlg);
            if (!body) {
                continue;
            }
            /* gap:14 between rows gives ~30px line-height with a 16px
             * default label + a 24px textinput. Earlier gap:6 left labels
             * visually colliding with the textinput on the row below
             * (rows have no implicit min-height — they shrink to content,
             * so we need the gap to carry all the vertical air). */
            yetty_ygui_widget_apply_css(body, "display:flex;flex-direction:column;gap:14;padding:14 14 14 14;");

            /* One labeled textinput per field. Labels use ygui labels; the
             * input itself carries the placeholder + default. */
            for (int f = 0; f < s_views[k].num_fields; f++) {
                char row_id[80], lbl_id[80], in_id[80];
                snprintf(row_id, sizeof(row_id), "%s/row%d", s_views[k].id_prefix, f);
                snprintf(lbl_id, sizeof(lbl_id), "%s/lbl%d", s_views[k].id_prefix, f);
                snprintf(in_id,  sizeof(in_id),  "%s%s",   s_views[k].id_prefix,
                         s_views[k].fields[f].id_suffix);
                struct yetty_ygui_widget *row = yetty_ygui_engine_hbox(yui->engine, row_id,
                                                                       0, 0, 0, 0);
                if (!row) continue;
                /* Explicit min-height: the row has no inherent dimension —
                 * it sizes from its children. Pin it at 28 (the textinput
                 * row height) so a single-line label can't shrink the row
                 * below the input's height and bleed into the gap. */
                yetty_ygui_widget_apply_css(row,
                    "display:flex;flex-direction:row;gap:10;align-items:center;min-height:28;");
                yetty_ygui_widget_add_child(body, row);

                struct yetty_ygui_widget *lbl = yetty_ygui_engine_label(yui->engine, lbl_id, 0, 0,
                                                                        s_views[k].fields[f].label);
                if (lbl) {
                    yetty_ygui_widget_apply_css(lbl, "width:30%;");
                    yetty_ygui_widget_add_child(row, lbl);
                }
                struct yetty_ygui_widget *in = yetty_ygui_engine_textinput(yui->engine, in_id, 0, 0, 0,
                                                                            24,
                                                                            s_views[k].fields[f].placeholder);
                if (in) {
                    yetty_ygui_widget_apply_css(in, "flex:1 0 0;");
                    if (s_views[k].fields[f].default_text && s_views[k].fields[f].default_text[0]) {
                        yetty_ygui_widget_textinput_set_text(in, s_views[k].fields[f].default_text);
                    }
                    yetty_ygui_widget_add_child(row, in);
                    /* Stash per-kind textinput so the connect subscriber
                     * can read what the user typed. dialog_inputs[][] is
                     * the single source of truth — yetty_yui_get_exec_command
                     * and the SSH/Telnet field getters all read through it. */
                    if (f < 4) {
                        yui->dialog_inputs[k][f] = in;
                    }
                }
            }

            /* Action row — Cancel + Connect at the bottom. */
            char actions_id[80], cancel_id[80], connect_id[80];
            snprintf(actions_id, sizeof(actions_id), "%s/actions", s_views[k].id_prefix);
            snprintf(cancel_id,  sizeof(cancel_id),  "%s/cancel",  s_views[k].id_prefix);
            snprintf(connect_id, sizeof(connect_id), "%s/connect", s_views[k].id_prefix);
            struct yetty_ygui_widget *actions = yetty_ygui_engine_hbox(yui->engine, actions_id,
                                                                        0, 0, 0, 0);
            if (actions) {
                yetty_ygui_widget_apply_css(actions, "display:flex;flex-direction:row;justify-content:end;gap:8;");
                yetty_ygui_widget_add_child(body, actions);
                struct yetty_ygui_widget *cancel = yetty_ygui_engine_button(yui->engine, cancel_id,
                                                                              0, 0, 80, 28, "Cancel");
                if (cancel) {
                    yetty_ygui_widget_button_on_click(cancel, yui_dialog_cancel, &s_cb_ctx[k]);
                    yetty_ygui_widget_add_child(actions, cancel);
                }
                struct yetty_ygui_widget *connect = yetty_ygui_engine_button(yui->engine, connect_id,
                                                                               0, 0, 96, 28, "Connect");
                if (connect) {
                    yetty_ygui_widget_button_on_click(connect, yui_dialog_connect, &s_cb_ctx[k]);
                    yetty_ygui_widget_add_child(actions, connect);
                }
            }
        }

        /* GPU info dialog. Stash adapter + allocator from the yetty
         * context so the dialog can re-fetch live stats on demand
         * (the "Refresh" button). Dialog widget tree:
         *   yui_dlg_gpu_info  (window, hidden at start)
         *     body            (auto vbox)
         *       textarea      (readonly-ish; we just rewrite on open)
         *       actions hbox
         *         Refresh button
         *         Close button
         */
        yui->gpu_info_adapter = context->gpu_context.adapter;
        yui->gpu_info_allocator = context->gpu_context.allocator;

        struct yetty_ygui_widget *gpu_dlg = yetty_ygui_engine_window(
            yui->engine, "yui_dlg_gpu_info", /*x=*/120.0f, /*y=*/80.0f,
            /*w=*/560.0f, /*h=*/360.0f, "GPU info");
        yui->gpu_info_dialog = gpu_dlg;
        if (gpu_dlg) {
            yetty_ygui_widget_set_visible(gpu_dlg, 0);
            struct yetty_ygui_widget *body = yetty_ygui_widget_window_body(gpu_dlg);
            if (body) {
                yetty_ygui_widget_apply_css(body,
                    "display:flex;flex-direction:column;gap:10;padding:14 14 14 14;");
                /* Word-wrapped textarea — long lines (notably the
                 * GPU-limits dump) break at word boundaries instead of
                 * being truncated or, worse, painting past the right
                 * edge of the widget. */
                struct yetty_ygui_widget *ta = yetty_ygui_engine_textarea_wrapped(
                    yui->engine, "yui_dlg_gpu_info/text", 0, 0, 0, 0,
                    "(no info yet — click Refresh)");
                if (ta) {
                    yetty_ygui_widget_apply_css(ta, "flex:1 1 0;");
                    yetty_ygui_widget_add_child(body, ta);
                    yui->gpu_info_textarea = ta;
                }
                /* Authored_h = 36 — NOT just `min-height:36;` — because
                 * the flex algorithm distributes free space using
                 * `flex_basis = authored_h` when no explicit basis is
                 * set, and applies `min-height` only as a clamp AFTER
                 * the distribution. With basis=0, the textarea above
                 * (flex:1) absorbed the entire body content area, and
                 * the post-distribution min-height clamp on actions
                 * grew it again, pushing the total past the window's
                 * bottom edge (visible as buttons rendering below the
                 * darker dialog frame). Giving the row a real
                 * authored_h forces the layout to reserve the space up
                 * front. */
                struct yetty_ygui_widget *actions = yetty_ygui_engine_hbox(
                    yui->engine, "yui_dlg_gpu_info/actions", 0, 0, 0, 36);
                if (actions) {
                    yetty_ygui_widget_apply_css(actions,
                        "display:flex;flex-direction:row;justify-content:end;gap:8;"
                        "flex:0 0 auto;align-items:center;");
                    yetty_ygui_widget_add_child(body, actions);
                    struct yetty_ygui_widget *refresh = yetty_ygui_engine_button(
                        yui->engine, "yui_dlg_gpu_info/refresh", 0, 0, 96, 28, "Refresh");
                    if (refresh) {
                        yetty_ygui_widget_button_on_click(refresh, yui_gpu_info_refresh, yui);
                        yetty_ygui_widget_add_child(actions, refresh);
                    }
                    struct yetty_ygui_widget *close = yetty_ygui_engine_button(
                        yui->engine, "yui_dlg_gpu_info/close", 0, 0, 80, 28, "Close");
                    if (close) {
                        yetty_ygui_widget_button_on_click(close, yui_gpu_info_close, yui);
                        yetty_ygui_widget_add_child(actions, close);
                    }
                }
            }
        }

        /* Settings dialog — widget tree + tree-walk + on_toggle handler
         * all live in config-dialog.c. Construction is best-effort: a
         * failure here is logged but doesn't abort yui_create (the user
         * just doesn't get a Settings dialog). */
        struct yetty_yui_config_dialog_ptr_result cdr =
            yetty_yui_config_dialog_create(yui->engine,
                                           context->app_context.config);
        if (YETTY_IS_OK(cdr)) {
            yui->config_dialog = cdr.value;
        } else {
            ywarn("yui_create: config_dialog create: %s", cdr.error.msg);
            yetty_ycore_error_destroy(cdr.error);
        }

        /* Seed the menu at its root level. The drill-down callbacks
         * call popup_menu_clear + the matching populate_* helper, so
         * level transitions reuse the same single widget. */
        yui_app_menu_populate_root(yui);

        /* Statusbar — STATUSBAR widget pinned to the bottom of the
         * engine canvas by engine_set_statusbar; engine_pin_bars
         * rewrites its width/y every layout pass so resize follows the
         * framebuffer for free.
         *
         * Default content lives in the widget's own left/right text
         * slots — statusbar_render centers them vertically inside the
         * strip ([self->y + (self->h - font_size) * 0.5]), which a
         * child label widget can't do (label_render is top-aligned
         * inside its own box). The standard default render_all walks
         * children too, so callers adding their own widgets via
         * yetty_yui_statusbar() still get their tree drawn on top of
         * the centered text. */
        struct yetty_ygui_widget *sb = yetty_ygui_engine_statusbar(
            yui->engine, "yui_statusbar", 0, 0, /*w=*/0, /*h=*/22, "Ready");
        if (sb) {
            yui->statusbar = sb;
            yetty_ygui_widget_statusbar_set_right(sb, "yetty");
            yetty_ygui_engine_set_statusbar(yui->engine, sb);
        }

        /* Force a full redraw on the next render: the engine had widgets
         * added one-by-one during this constructor; add_to_engine sets
         * dirty=1 per widget, but none of them flip needs_full_redraw.
         * Without this, the very first frame after create can emit a
         * stale GROUP set whose dedup cache then locks the bottom bar
         * out of view until the next resize. */
        yetty_ygui_engine_mark_dirty(yui->engine);
    } else {
        ywarn("yui_create: ygui engine create failed: %s", er.error.msg);
        yetty_ycore_error_destroy(er.error);
        /* Non-fatal — yui still owns the SM + layer; just no producer yet. */
    }

    /* Register as the global ynotify sink. Producers that call ynotify
     * from any thread will land here via post_to_loop. We pass the loop
     * (not yui) as userdata since the loop outlives this yui instance.
     * The dispatch trampoline looks up the active yui under a lock. */
    yui_active_lock();
    s_active_yui = yui;
    yui_active_unlock();
    ynotify_set_handler(yui_ynotify_handler, context->event_loop);

    /* Build the titlebar widget tree. The tabbar model is bound later
     * by yetty.c via yetty_yui_set_tabbar_model; sync runs each render
     * and is a no-op until the model is non-NULL. */
    yui_titlebar_build(yui);

    ydebug("yui_create: grid=%ux%u cell=%.1fx%.1f", cols, rows, cell_w, cell_h);
    return YETTY_OK(yetty_yui_ptr, yui);
}

void yetty_yui_set_tabbar_model(struct yetty_yui *yui, struct yetty_yui_tabbar *tabbar)
{
    if (!yui) {
        return;
    }
    yui->tabbar_model = tabbar;
    /* Reset the sync watermarks so the next render rebuilds the tab
     * widget set against the new model. */
    yui->titlebar_synced_active = -1;
    yui->titlebar_synced_count = 0;
    if (yui->engine) {
        yetty_ygui_engine_mark_dirty(yui->engine);
    }
}

struct yetty_ycore_void_result yetty_yui_destroy(struct yetty_yui *yui)
{
    if (!yui) {
        return YETTY_OK_VOID();
    }
    /* Detach the ynotify sink before tearing anything down. New ynotify
     * calls will be a no-op; in-flight post_to_loop thunks see
     * s_active_yui == NULL and drop the message. */
    ynotify_set_handler(NULL, NULL);
    yui_active_lock();
    if (s_active_yui == yui) {
        s_active_yui = NULL;
    }
    yui_active_unlock();

    if (yui->engine) {
        struct yetty_ycore_void_result r = yetty_ygui_engine_destroy(yui->engine);
        if (!YETTY_IS_OK(r)) {
            ywarn("yui_destroy: engine destroy: %s", r.error.msg);
            yetty_ycore_error_destroy(r.error);
        }
        yui->engine = NULL;
    }
    /* config_dialog's widgets are owned by the engine (just destroyed
     * above); free only the per-dialog bookkeeping (bundle array +
     * struct). NULL-safe. */
    yetty_yui_config_dialog_destroy(yui->config_dialog);
    yui->config_dialog = NULL;
    if (yui->sm) {
        struct yetty_ycore_void_result r = yetty_ywire_wire_statemachine_destroy(yui->sm);
        if (!YETTY_IS_OK(r)) {
            ywarn("yui_destroy: sm destroy: %s", r.error.msg);
            yetty_ycore_error_destroy(r.error);
        }
    }
    if (yui->layer && yui->layer->ops && yui->layer->ops->destroy) {
        struct yetty_ycore_void_result r = yui->layer->ops->destroy(yui->layer);
        if (!YETTY_IS_OK(r)) {
            ywarn("yui_destroy: layer destroy: %s", r.error.msg);
            yetty_ycore_error_destroy(r.error);
        }
    }
    if (yui->yui_endpoint) {
        yui->yui_endpoint->ops->destroy(yui->yui_endpoint);
    }
    if (yui->render_endpoint) {
        yui->render_endpoint->ops->destroy(yui->render_endpoint);
    }
    free(yui->splitters);
    free(yui);
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Per-split divider widgets
 *
 * Each yetty_yui_split node in the active workspace's tile tree gets
 * exactly one engine_splitter widget pinned at the boundary between
 * its two children. The widgets are absolutely positioned (outside any
 * flex parent) and use the splitter's external-drive mode: drags fire
 * yui_splitter_on_change, which posts a SPLIT_RESIZE event so the
 * workspace re-lays out and the next sync re-positions the widget.
 *
 * Reconciliation is per-frame: walk the active workspace's tile tree,
 * mark every existing entry whose split is still present (creating
 * fresh widgets for new splits and updating their bounds), then drop
 * any entry that wasn't marked.
 *===========================================================================*/

#define YETTY_YUI_SPLITTER_THICKNESS 6.0f

/* Thunk passed to the engine_splitter widget. Holds enough state to
 * compose a SPLIT_RESIZE event: yui (for posting + dim), workspace_id +
 * split_id (the routing key). Owned by the splitter entry; freed when
 * the widget is removed. */
struct yetty_yui_splitter_thunk {
    struct yetty_yui *yui;
    yetty_ycore_object_id workspace_id;
    yetty_ycore_object_id split_id;
};

static void yui_splitter_on_change(struct yetty_ygui_widget *widget, float delta,
                                   void *userdata);

static struct yetty_yui_splitter_entry *yui_splitter_find_entry(struct yetty_yui *yui,
                                                                yetty_ycore_object_id split_id)
{
    if (!yui) {
        return NULL;
    }
    for (size_t i = 0; i < yui->splitter_count; i++) {
        if (yui->splitters[i].split_id == split_id) {
            return &yui->splitters[i];
        }
    }
    return NULL;
}

static struct yetty_yui_splitter_entry *yui_splitter_alloc_entry(struct yetty_yui *yui)
{
    if (yui->splitter_count == yui->splitter_cap) {
        size_t new_cap = yui->splitter_cap ? yui->splitter_cap * 2 : 4;
        struct yetty_yui_splitter_entry *arr =
            realloc(yui->splitters, new_cap * sizeof(*yui->splitters));
        if (!arr) {
            return NULL;
        }
        yui->splitters = arr;
        yui->splitter_cap = new_cap;
    }
    struct yetty_yui_splitter_entry *e = &yui->splitters[yui->splitter_count++];
    memset(e, 0, sizeof(*e));
    return e;
}

static void yui_splitter_entry_destroy_widget(struct yetty_yui_splitter_entry *e)
{
    if (!e) {
        return;
    }
    if (e->widget) {
        yetty_ygui_widget_remove(e->widget);
        e->widget = NULL;
    }
    free(e->thunk);
    e->thunk = NULL;
}

static void yui_splitter_remove_at(struct yetty_yui *yui, size_t idx)
{
    if (!yui || idx >= yui->splitter_count) {
        return;
    }
    yui_splitter_entry_destroy_widget(&yui->splitters[idx]);
    /* Compact (swap with last). */
    if (idx != yui->splitter_count - 1) {
        yui->splitters[idx] = yui->splitters[yui->splitter_count - 1];
    }
    yui->splitter_count--;
}

static void yui_splitter_walk_tree(struct yetty_yui *yui, struct yetty_yui_tile *tile,
                                   yetty_ycore_object_id workspace_id)
{
    if (!tile || yetty_yui_tile_type(tile) != YETTY_YUI_TILE_SPLIT) {
        return;
    }

    yetty_ycore_object_id split_id = yetty_yui_tile_id(tile);
    struct yetty_yui_tile *first = yetty_yui_tile_split_first(tile);
    struct yetty_yui_tile *second = yetty_yui_tile_split_second(tile);
    enum yetty_yui_orientation orient = yetty_yui_tile_split_orientation(tile);
    struct yetty_yui_rect fb = first ? yetty_yui_tile_bounds(first)
                                     : (struct yetty_yui_rect){0, 0, 0, 0};
    struct yetty_yui_rect sb = second ? yetty_yui_tile_bounds(second)
                                      : (struct yetty_yui_rect){0, 0, 0, 0};

    struct yetty_yui_splitter_entry *e = yui_splitter_find_entry(yui, split_id);
    if (!e) {
        e = yui_splitter_alloc_entry(yui);
        if (!e) {
            return;
        }
        e->split_id = split_id;
        e->workspace_id = workspace_id;
        e->yui = yui;

        char id_buf[48];
        snprintf(id_buf, sizeof(id_buf), "yui_splitter_%llu",
                 (unsigned long long)split_id);
        e->widget = yetty_ygui_engine_splitter(yui->engine, id_buf, 0, 0,
                                               YETTY_YUI_SPLITTER_THICKNESS,
                                               YETTY_YUI_SPLITTER_THICKNESS);
        if (e->widget) {
            yetty_ygui_widget_set_position_mode(e->widget, YETTY_YGUI_POSITION_ABSOLUTE);
            yetty_ygui_widget_splitter_set_axis(
                e->widget, orient == YETTY_YUI_VERTICAL ? 1 : 0);
            yetty_ygui_widget_splitter_set_min(e->widget, 30.0f);

            struct yetty_yui_splitter_thunk *t = calloc(1, sizeof(*t));
            if (t) {
                t->yui = yui;
                t->workspace_id = workspace_id;
                t->split_id = split_id;
                e->thunk = t;
                yetty_ygui_widget_splitter_on_change(e->widget, yui_splitter_on_change, t);
            }
        }
    } else if (e->widget) {
        /* Orientation can't actually change after creation today, but
         * keep the axis in sync defensively — and refresh workspace_id
         * in case the same split_id is reused across reloads. */
        e->workspace_id = workspace_id;
        yetty_ygui_widget_splitter_set_axis(e->widget,
                                            orient == YETTY_YUI_VERTICAL ? 1 : 0);
    }
    e->seen = 1;

    /* Position the splitter on the boundary between the two child
     * tiles. VERTICAL split = side-by-side panes → vertical bar at the
     * shared x edge. HORIZONTAL split = stacked panes → horizontal bar
     * at the shared y edge. Span the whole shared edge so the user can
     * grab anywhere on it. */
    if (e->widget && first && second) {
        const float t = YETTY_YUI_SPLITTER_THICKNESS;
        if (orient == YETTY_YUI_VERTICAL) {
            /* Vertical bar between fb and sb (sb starts where fb ends in x). */
            float x = sb.x - t * 0.5f;
            float y = fb.y;
            float h = fb.h;
            yetty_ygui_widget_set_position(e->widget, x, y);
            yetty_ygui_widget_set_size(e->widget, t, h);
        } else {
            /* Horizontal bar between fb (top) and sb (bottom). */
            float x = fb.x;
            float y = sb.y - t * 0.5f;
            float w = fb.w;
            yetty_ygui_widget_set_position(e->widget, x, y);
            yetty_ygui_widget_set_size(e->widget, w, t);
        }
    }

    yui_splitter_walk_tree(yui, first, workspace_id);
    yui_splitter_walk_tree(yui, second, workspace_id);
}

static void yui_splitters_sync(struct yetty_yui *yui)
{
    if (!yui || !yui->engine || !yui->tabbar_model) {
        return;
    }
    struct yetty_yui_workspace *ws = yetty_yui_tabbar_active_workspace(yui->tabbar_model);
    yetty_ycore_object_id ws_id = ws ? yetty_yui_workspace_id(ws) : 0;
    struct yetty_yui_tile *root = ws ? yetty_yui_workspace_root(ws) : NULL;

    for (size_t i = 0; i < yui->splitter_count; i++) {
        yui->splitters[i].seen = 0;
    }
    if (root && ws_id != 0) {
        yui_splitter_walk_tree(yui, root, ws_id);
    }
    /* Drop entries that disappeared from the tree (splits removed,
     * workspace switched, etc.). Iterate backwards so swap-and-shrink
     * stays consistent. */
    for (size_t i = yui->splitter_count; i-- > 0;) {
        if (!yui->splitters[i].seen) {
            yui_splitter_remove_at(yui, i);
        }
    }
}

static void yui_splitter_on_change(struct yetty_ygui_widget *widget, float delta,
                                   void *userdata)
{
    (void)widget;
    struct yetty_yui_splitter_thunk *t = userdata;
    if (!t || !t->yui || !t->yui->ctx) {
        return;
    }
    struct yetty_yui_tabbar *bar = t->yui->tabbar_model;
    struct yetty_yui_workspace *ws =
        bar ? yetty_yui_tabbar_find_workspace(bar, t->workspace_id) : NULL;
    if (!ws) {
        return;
    }
    struct yetty_yui_tile *split =
        yetty_yui_tile_find_by_id(yetty_yui_workspace_root(ws), t->split_id);
    if (!split || yetty_yui_tile_type(split) != YETTY_YUI_TILE_SPLIT) {
        return;
    }
    struct yetty_yui_rect split_b = yetty_yui_tile_bounds(split);
    enum yetty_yui_orientation orient = yetty_yui_tile_split_orientation(split);
    float span = (orient == YETTY_YUI_VERTICAL) ? split_b.w : split_b.h;
    if (span <= 0.0f) {
        return;
    }
    float old_ratio = yetty_yui_tile_split_ratio(split);
    float new_first = old_ratio * span + delta;
    float ratio = new_first / span;
    if (ratio < 0.05f) {
        ratio = 0.05f;
    }
    if (ratio > 0.95f) {
        ratio = 0.95f;
    }

    struct yetty_yui_event ev = {0};
    ev.type = YETTY_YCORE_SPLIT_RESIZE;
    ev.split_resize.workspace_id = t->workspace_id;
    ev.split_resize.split_id = t->split_id;
    ev.split_resize.ratio = ratio;
    yetty_yevent_post_async(t->yui->ctx->app_context.platform_input_pipe, &ev);
}

/*===========================================================================
 * Render
 *===========================================================================*/

struct yetty_ycore_void_result yetty_yui_render(struct yetty_yui *yui,
                                                struct yetty_ydraw_target *target)
{
    if (!yui || !yui->layer || !target) {
        return YETTY_OK_VOID();
    }

    /* Reconcile the engine titlebar against the bound tabbar model
     * before deciding to emit. Sync mutates widget state (adds/removes
     * tab widgets, repaints active) which marks the engine dirty when
     * something changed. Idempotent / no-op when the model and widget
     * tree already match. */
    yui_titlebar_sync(yui);

    /* Same reconciliation pattern for the per-split divider widgets:
     * one engine_splitter per yui_split node in the active workspace,
     * positioned absolutely on the shared edge. New splits appear,
     * stale ones get removed; positions track tile bounds. */
    yui_splitters_sync(yui);

    /* Drive the producer engine when dirty. Writes the OSC frame envelope
     * into the yui_endpoint memory pty (no stdout, no b64-round-trip). */
    if (yui->engine && yetty_ygui_engine_is_dirty(yui->engine)) {
        struct yetty_ycore_void_result er = yetty_ygui_engine_render(yui->engine);
        if (YETTY_IS_ERR(er)) {
            ywarn("yui_render: engine render: %s", er.error.msg);
            yetty_ycore_error_destroy(er.error);
        }
    }

    /* Drain the SM synchronously — bytes the engine just wrote are sitting
     * in the memory pty waiting to be parsed. Doing this in the same frame
     * avoids a one-frame lag (the post_to_loop wake would otherwise defer
     * this to next iteration). */
    if (yui->sm) {
        struct yetty_ycore_void_result pr = yetty_ywire_wire_statemachine_process(yui->sm);
        if (YETTY_IS_ERR(pr)) {
            ywarn("yui_render: SM process: %s", pr.error.msg);
            yetty_ycore_error_destroy(pr.error);
        }
    }

    if (yui->layer->ops->is_empty && yui->layer->ops->is_empty(yui->layer)) {
        return YETTY_OK_VOID();
    }
    /* The yui scene-canvas is the topmost paint in the frame — every
     * pane has already painted into the shared big_target with
     * LoadOp_Load before us, and any pane that drew this frame wiped
     * its area (background-layer is a full opaque pane fill), which
     * also wipes whatever yui chrome we painted into that area last
     * frame. RENDER events fire only when something is dirty, so when
     * we get here at least one of {yui itself, some pane} was dirty;
     * in either case the chrome region of big_target has been
     * disturbed, and we must repaint our cached content unconditionally
     * to keep it on screen. Pre-fix this was force=0 and the chrome
     * flickered on every pane redraw (cursor blink, mouse move
     * provoking ymgui repaint, …). Drop the int return (success /
     * failure is all that matters at this site). */
    struct yetty_ycore_int_result rr = yui->layer->ops->render(yui->layer, target, /*force=*/1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "yui layer render");
    return YETTY_OK_VOID();
}

struct yetty_platform_pty *yetty_yui_producer_pty(struct yetty_yui *yui)
{
    return yui ? yui->yui_endpoint : NULL;
}

int yetty_yui_is_dirty(const struct yetty_yui *yui)
{
    if (!yui) {
        return 0;
    }
    /* Reconcile so the engine dirty bit reflects pending tabbar /
     * splitter mutations BEFORE the panes render. Without this, a tab
     * added between frames wouldn't appear dirty until yui_render's own
     * sync — too late for the pane underneath to know to wipe. Casts
     * away const: the sync mutates the widget tree but no caller-
     * observable state on the yui handle itself. Both syncs are
     * idempotent; yui_render calls them again and the second pass is a
     * no-op. */
    struct yetty_yui *mut = (struct yetty_yui *)yui;
    yui_titlebar_sync(mut);
    yui_splitters_sync(mut);
    return mut->engine ? yetty_ygui_engine_is_dirty(mut->engine) : 0;
}

/*===========================================================================
 * Menu / dialog callback implementations
 *===========================================================================*/

static void yui_menu_open_dialog(struct yetty_ygui_widget *item, void *userdata)
{
    (void)item;
    struct yui_cb_ctx *ctx = userdata;
    if (!ctx || !ctx->yui || (int)ctx->kind < 0 ||
        (int)ctx->kind >= YETTY_YUI_VIEW_KIND_COUNT) {
        return;
    }
    struct yetty_ygui_widget *dlg = ctx->yui->dialogs[(int)ctx->kind];
    if (!dlg) {
        return;
    }
    yetty_ygui_widget_set_visible(dlg, 1);
    if (ctx->yui->engine) {
        yetty_ygui_engine_mark_dirty(ctx->yui->engine);
    }
}

/* Menu handler for SHELL — bypass the dialog entirely. There's nothing
 * to configure for "spawn the default shell", so the click fires the
 * connect callback immediately. yetty.c's connect handler is expected
 * to clear/ignore `shell/command` for the SHELL kind so the spawn runs
 * the user's $SHELL (or shell/default fallback). */
static void yui_menu_spawn_shell(struct yetty_ygui_widget *item, void *userdata)
{
    (void)item;
    struct yui_cb_ctx *ctx = userdata;
    if (!ctx || !ctx->yui) {
        return;
    }
    if (ctx->yui->engine) {
        yetty_ygui_engine_mark_dirty(ctx->yui->engine);
    }
    if (ctx->yui->connect_cb) {
        ctx->yui->connect_cb(ctx->yui->connect_userdata, YETTY_YUI_VIEW_SHELL);
    }
}

static void yui_dialog_cancel(struct yetty_ygui_widget *button, void *userdata)
{
    (void)button;
    struct yui_cb_ctx *ctx = userdata;
    if (!ctx || !ctx->yui || (int)ctx->kind < 0 ||
        (int)ctx->kind >= YETTY_YUI_VIEW_KIND_COUNT) {
        return;
    }
    struct yetty_ygui_widget *dlg = ctx->yui->dialogs[(int)ctx->kind];
    if (dlg) {
        yetty_ygui_widget_set_visible(dlg, 0);
    }
    if (ctx->yui->engine) {
        yetty_ygui_engine_mark_dirty(ctx->yui->engine);
    }
}

/* Root level: seed the menu with the top-level entries and clear any
 * back handler. Called from yetty_yui_create and from the back arrow
 * in deeper levels. */
static void yui_app_menu_populate_root(struct yetty_yui *yui)
{
    if (!yui || !yui->app_menu) {
        return;
    }
    yetty_ygui_widget_popup_menu_clear(yui->app_menu);
    yetty_ygui_widget_popup_menu_set_title(yui->app_menu, "Menu");
    yetty_ygui_widget_popup_menu_set_back(yui->app_menu, NULL, NULL);
    yetty_ygui_widget_popup_menu_add_drill_item(yui->app_menu, "New view  ▸",
                                                yui_app_menu_open_new_view, yui);
    yetty_ygui_widget_popup_menu_add_separator(yui->app_menu);
    yetty_ygui_widget_popup_menu_add_item(yui->app_menu, "GPU info…",
                                          yui_app_menu_open_gpu_info, yui);
    yetty_ygui_widget_popup_menu_add_item(yui->app_menu, "Settings…",
                                          yui_app_menu_open_settings, yui);
    yui->app_menu_level = 0;
}

/* "New view" level: clear the menu, set the breadcrumb + a back
 * handler, then populate with the per-view-kind action items. */
static void yui_app_menu_populate_new_view(struct yetty_yui *yui)
{
    if (!yui || !yui->app_menu) {
        return;
    }
    static const char *const LABELS[YETTY_YUI_VIEW_KIND_COUNT] = {
        [YETTY_YUI_VIEW_SHELL]  = "Shell",
        [YETTY_YUI_VIEW_EXEC]   = "Exec…",
        [YETTY_YUI_VIEW_SSH]    = "SSH…",
        [YETTY_YUI_VIEW_TELNET] = "Telnet…",
        [YETTY_YUI_VIEW_YVNC]   = "yVNC…",
    };
    static const int MENU_ORDER[YETTY_YUI_VIEW_KIND_COUNT] = {
        YETTY_YUI_VIEW_SHELL,
        YETTY_YUI_VIEW_EXEC,
        YETTY_YUI_VIEW_SSH,
        YETTY_YUI_VIEW_TELNET,
        YETTY_YUI_VIEW_YVNC,
    };
    yetty_ygui_widget_popup_menu_clear(yui->app_menu);
    yetty_ygui_widget_popup_menu_set_title(yui->app_menu, "Menu  ›  New view");
    yetty_ygui_widget_popup_menu_set_back(yui->app_menu, yui_app_menu_back_to_root, yui);
    for (int i = 0; i < YETTY_YUI_VIEW_KIND_COUNT; i++) {
        int k = MENU_ORDER[i];
        /* SHELL is the only no-dialog item — fires the connect callback
         * directly. The rest open their config dialog. Both are
         * action items (the menu closes after the click). */
        ygui_click_callback_t cb =
            (k == YETTY_YUI_VIEW_SHELL) ? yui_menu_spawn_shell : yui_menu_open_dialog;
        yetty_ygui_widget_popup_menu_add_item(yui->app_menu, LABELS[k], cb, &s_cb_ctx[k]);
    }
    yui->app_menu_level = 1;
}

/* Drill into the "New view" submenu — repopulates app_menu in place.
 * Items added via add_drill_item keep the menu open after their
 * callback fires, so the user sees the submenu replace the root
 * content. */
static void yui_app_menu_open_new_view(struct yetty_ygui_widget *item, void *userdata)
{
    (void)item;
    yui_app_menu_populate_new_view((struct yetty_yui *)userdata);
}

/* `<` back-handler installed on the New-view level. Returns to root by
 * re-populating with the root content. Same in-place swap. */
static void yui_app_menu_back_to_root(struct yetty_ygui_widget *item, void *userdata)
{
    (void)item;
    yui_app_menu_populate_root((struct yetty_yui *)userdata);
}

/* Right-click context-menu root. Populates the same app_menu widget
 * with context-flavoured entries (GPU info, Split V ▸, Split H ▸).
 * Entered via yetty_yui_show_context_menu; the menu uses the SAME
 * popup widget as the hamburger so we don't pay for a second
 * scene-canvas entity tree. */
static void yui_app_menu_populate_context_root(struct yetty_yui *yui)
{
    if (!yui || !yui->app_menu) {
        return;
    }
    yetty_ygui_widget_popup_menu_clear(yui->app_menu);
    yetty_ygui_widget_popup_menu_set_title(yui->app_menu, "Pane");
    yetty_ygui_widget_popup_menu_set_back(yui->app_menu, NULL, NULL);
    yetty_ygui_widget_popup_menu_add_item(yui->app_menu, "GPU info…",
                                          yui_app_menu_open_gpu_info, yui);
    yetty_ygui_widget_popup_menu_add_item(yui->app_menu, "Settings…",
                                          yui_app_menu_open_settings, yui);
    yetty_ygui_widget_popup_menu_add_separator(yui->app_menu);
    yetty_ygui_widget_popup_menu_add_drill_item(yui->app_menu, "Split vertically  ▸",
                                                yui_context_open_split_vertical, yui);
    yetty_ygui_widget_popup_menu_add_drill_item(yui->app_menu, "Split horizontally  ▸",
                                                yui_context_open_split_horizontal, yui);
    yui->app_menu_level = 2;
}

/* Split submenu: same view kinds as the "New view" submenu, but each
 * action splits the focused pane (via split_cb) instead of opening a
 * new tab. orientation is captured per-item via s_split_ctx. */
static void yui_app_menu_populate_split_kind(struct yetty_yui *yui, int horizontal)
{
    if (!yui || !yui->app_menu) {
        return;
    }
    static const char *const LABELS[YETTY_YUI_VIEW_KIND_COUNT] = {
        [YETTY_YUI_VIEW_SHELL]  = "Shell",
        [YETTY_YUI_VIEW_EXEC]   = "Exec…",
        [YETTY_YUI_VIEW_SSH]    = "SSH…",
        [YETTY_YUI_VIEW_TELNET] = "Telnet…",
        [YETTY_YUI_VIEW_YVNC]   = "yVNC…",
    };
    static const int MENU_ORDER[YETTY_YUI_VIEW_KIND_COUNT] = {
        YETTY_YUI_VIEW_SHELL, YETTY_YUI_VIEW_EXEC, YETTY_YUI_VIEW_SSH,
        YETTY_YUI_VIEW_TELNET, YETTY_YUI_VIEW_YVNC,
    };
    yetty_ygui_widget_popup_menu_clear(yui->app_menu);
    yetty_ygui_widget_popup_menu_set_title(yui->app_menu,
                                           horizontal ? "Pane  ›  Split horizontally"
                                                      : "Pane  ›  Split vertically");
    yetty_ygui_widget_popup_menu_set_back(yui->app_menu, yui_split_back_to_context, yui);
    int o = horizontal ? 1 : 0;
    for (int i = 0; i < YETTY_YUI_VIEW_KIND_COUNT; i++) {
        int k = MENU_ORDER[i];
        yetty_ygui_widget_popup_menu_add_item(yui->app_menu, LABELS[k], yui_split_kind_action,
                                              &s_split_ctx[o][k]);
    }
    yui->app_menu_level = horizontal ? 4 : 3;
}

static void yui_context_open_split_vertical(struct yetty_ygui_widget *item, void *userdata)
{
    (void)item;
    yui_app_menu_populate_split_kind((struct yetty_yui *)userdata, /*horizontal=*/0);
}

static void yui_context_open_split_horizontal(struct yetty_ygui_widget *item, void *userdata)
{
    (void)item;
    yui_app_menu_populate_split_kind((struct yetty_yui *)userdata, /*horizontal=*/1);
}

/* `<` back from split-kind → context root. */
static void yui_split_back_to_context(struct yetty_ygui_widget *item, void *userdata)
{
    (void)item;
    yui_app_menu_populate_context_root((struct yetty_yui *)userdata);
}

/* Action item under a Split V/H submenu — fires the split_cb with the
 * kind + orientation captured in the per-item context bundle. */
static void yui_split_kind_action(struct yetty_ygui_widget *item, void *userdata)
{
    (void)item;
    struct yui_split_ctx *ctx = userdata;
    if (!ctx || !ctx->yui) {
        return;
    }
    if (ctx->yui->split_cb) {
        ctx->yui->split_cb(ctx->yui->split_userdata, ctx->kind, ctx->horizontal);
    }
    if (ctx->yui->engine) {
        yetty_ygui_engine_mark_dirty(ctx->yui->engine);
    }
}

/*===========================================================================
 * GPU info dialog — built once in yetty_yui_create; the menu item flips
 * its visibility on. Each open refreshes the textarea with current
 * adapter info + live allocator stats.
 *===========================================================================*/

/* Build the multi-line info text. Caller frees with free(). Returns
 * NULL only on allocation failure; missing adapter / allocator produce
 * a "(unavailable)" placeholder line instead so the dialog still has
 * something useful to show in headless / partial-init contexts. */
static char *yui_gpu_info_build_text(const struct yetty_yui *yui)
{
    /* Adapter section — reuse yetty_log_gpu_info's formatter so this
     * dialog stays in sync with the console banner. */
    char *adapter_desc = NULL;
    if (yui->gpu_info_adapter) {
        adapter_desc = yetty_ywebgpu_get_webgpu_description(yui->gpu_info_adapter);
    }
    const char *adapter_block =
        adapter_desc ? adapter_desc : "(WebGPU adapter unavailable)\n";

    /* Allocator section — pull live stats. */
    struct yetty_yrender_gpu_allocator_stats st = {0};
    int have_stats = 0;
    if (yui->gpu_info_allocator && yui->gpu_info_allocator->ops &&
        yui->gpu_info_allocator->ops->get_stats) {
        yui->gpu_info_allocator->ops->get_stats(yui->gpu_info_allocator, &st);
        have_stats = 1;
    }

    /* Compose into a single growing buffer. snprintf-twice idiom keeps
     * the size exact. */
    char alloc_block[512];
    if (have_stats) {
        snprintf(alloc_block, sizeof(alloc_block),
                 "\n"
                 "GPU allocator stats:\n"
                 "  live allocations:   %u / %u\n"
                 "  buffers:            %u  (%llu bytes)\n"
                 "  textures:           %u  (%llu bytes)\n"
                 "  total bytes:        %llu\n"
                 "  peak allocations:   %u\n"
                 "  peak total bytes:   %llu\n",
                 st.live_allocations, st.capacity,
                 st.buffer_count, (unsigned long long)st.buffer_bytes,
                 st.texture_count, (unsigned long long)st.texture_bytes,
                 (unsigned long long)st.total_bytes,
                 st.peak_allocations,
                 (unsigned long long)st.peak_total_bytes);
    } else {
        snprintf(alloc_block, sizeof(alloc_block),
                 "\nGPU allocator stats: (unavailable)\n");
    }

    size_t a_len = strlen(adapter_block);
    size_t b_len = strlen(alloc_block);
    char *out = (char *)malloc(a_len + b_len + 1);
    if (!out) {
        free(adapter_desc);
        return NULL;
    }
    memcpy(out, adapter_block, a_len);
    memcpy(out + a_len, alloc_block, b_len);
    out[a_len + b_len] = '\0';
    free(adapter_desc);
    return out;
}

static void yui_gpu_info_load_into_textarea(struct yetty_yui *yui)
{
    if (!yui || !yui->gpu_info_textarea) {
        return;
    }
    char *text = yui_gpu_info_build_text(yui);
    /* The textarea is word-wrapping, so long lines (notably the
     * GPU-limits dump) break at word boundaries inside the widget
     * instead of escaping the right edge — no manual dialog resize
     * needed here. */
    yetty_ygui_widget_textarea_set_text(yui->gpu_info_textarea,
                                        text ? text : "(allocation failed)");
    free(text);
}

static void yui_app_menu_open_gpu_info(struct yetty_ygui_widget *item, void *userdata)
{
    (void)item;
    struct yetty_yui *yui = userdata;
    if (!yui || !yui->gpu_info_dialog) {
        return;
    }
    yui_gpu_info_load_into_textarea(yui);
    yetty_ygui_widget_set_visible(yui->gpu_info_dialog, 1);
    if (yui->engine) {
        yetty_ygui_engine_mark_dirty(yui->engine);
    }
}

static void yui_gpu_info_refresh(struct yetty_ygui_widget *button, void *userdata)
{
    (void)button;
    struct yetty_yui *yui = userdata;
    if (!yui) {
        return;
    }
    yui_gpu_info_load_into_textarea(yui);
    if (yui->engine) {
        yetty_ygui_engine_mark_dirty(yui->engine);
    }
}

static void yui_gpu_info_close(struct yetty_ygui_widget *button, void *userdata)
{
    (void)button;
    struct yetty_yui *yui = userdata;
    if (!yui || !yui->gpu_info_dialog) {
        return;
    }
    yetty_ygui_widget_set_visible(yui->gpu_info_dialog, 0);
    if (yui->engine) {
        yetty_ygui_engine_mark_dirty(yui->engine);
    }
}

static void yui_app_menu_open_settings(struct yetty_ygui_widget *item, void *userdata)
{
    (void)item;
    struct yetty_yui *yui = userdata;
    if (!yui) {
        return;
    }
    yetty_yui_config_dialog_show(yui->config_dialog);
}

/*===========================================================================
 * Titlebar (ygui-driven)
 *
 * The pre-ygui tabbar painted its strip with a custom GPU pipeline. That
 * code is gone; the strip is now a hbox pinned via the engine titlebar
 * slot containing:
 *   [≡ hamburger][native TABBAR widget][_][□][×]
 * The TABBAR widget owns its own pills + per-tab close-x + optional "+"
 * pill (built-in to the widget). yui's job is just to reconcile the
 * tabbar's tab count + active index against the tabbar_model on every
 * render and forward TABBAR events back to the model.
 *===========================================================================*/

/* Pixel sizes for the titlebar chrome. Picked to feel browser-y: the
 * header strip is 32 px (the native TABBAR widget's default header_h),
 * the hamburger / window-control buttons are square-ish 28×28 pills
 * that match the per-tab close-x footprint. */
#define TITLEBAR_STRIP_H 32.0f
#define TITLEBAR_BTN_W   28.0f

static void yui_titlebar_build(struct yetty_yui *yui)
{
    if (!yui || !yui->engine) {
        return;
    }

    struct yetty_ygui_widget *tb =
        yetty_ygui_engine_hbox(yui->engine, "yui_titlebar", 0, 0, 0, TITLEBAR_STRIP_H);
    if (!tb) {
        return;
    }
    /* gap:0 + tight padding so the hamburger / tabbar / window-control
     * buttons sit flush — combined with bg_color = bg_surface on the
     * chrome buttons below, the whole strip reads as one continuous
     * band matching the tabbar widget's own bg. */
    yetty_ygui_widget_apply_css(tb,
        "display:flex;flex-direction:row;align-items:center;gap:0;padding:0 0 0 0;");

    /* The tabbar widget paints its strip with theme->bg_surface
     * (0xFF2C261E by default). Give the side buttons (hamburger +
     * window controls) the same base color so they blend into one
     * continuous strip — otherwise they sit on whatever bg_color
     * the button widget happens to inherit and look detached. */
    const uint32_t STRIP_BG = 0xFF2C261Eu;

    yui->titlebar_hamburger = yetty_ygui_engine_button(
        yui->engine, "yui_titlebar/hamburger", 0, 0, TITLEBAR_BTN_W, TITLEBAR_STRIP_H,
        "\xE2\x89\xA1"); /* ≡ */
    if (yui->titlebar_hamburger) {
        yetty_ygui_widget_set_bg_color(yui->titlebar_hamburger, STRIP_BG);
        yetty_ygui_widget_button_on_click(yui->titlebar_hamburger,
                                          yui_titlebar_on_hamburger, yui);
        yetty_ygui_widget_add_child(tb, yui->titlebar_hamburger);
    }

    /* Native TABBAR widget. It paints its own pills + close-x + "+"
     * pill (we install on_new_tab below). flex:1 0 0 makes it absorb
     * all the unused horizontal space between the hamburger and the
     * window-control buttons. */
    yui->titlebar_tabbar = yetty_ygui_engine_tabbar(
        yui->engine, "yui_titlebar/tabbar", 0, 0, 0, TITLEBAR_STRIP_H);
    if (yui->titlebar_tabbar) {
        yetty_ygui_widget_apply_css(yui->titlebar_tabbar, "flex:1 0 0;");
        yetty_ygui_widget_tabbar_on_change(yui->titlebar_tabbar,
                                           yui_titlebar_on_tab_change, yui);
        yetty_ygui_widget_tabbar_on_tab_close(yui->titlebar_tabbar,
                                              yui_titlebar_on_tab_close, yui);
        yetty_ygui_widget_tabbar_on_new_tab(yui->titlebar_tabbar,
                                            yui_titlebar_on_new_tab, yui);
        yetty_ygui_widget_add_child(tb, yui->titlebar_tabbar);
    }

    yui->titlebar_min = yetty_ygui_engine_button(
        yui->engine, "yui_titlebar/min", 0, 0, TITLEBAR_BTN_W, TITLEBAR_STRIP_H,
        "\xE2\x88\x92"); /* − */
    if (yui->titlebar_min) {
        yetty_ygui_widget_set_bg_color(yui->titlebar_min, STRIP_BG);
        yetty_ygui_widget_button_on_click(yui->titlebar_min, yui_titlebar_on_min, yui);
        yetty_ygui_widget_add_child(tb, yui->titlebar_min);
    }
    yui->titlebar_max = yetty_ygui_engine_button(
        yui->engine, "yui_titlebar/max", 0, 0, TITLEBAR_BTN_W, TITLEBAR_STRIP_H,
        "\xE2\x96\xA1"); /* □ */
    if (yui->titlebar_max) {
        yetty_ygui_widget_set_bg_color(yui->titlebar_max, STRIP_BG);
        yetty_ygui_widget_button_on_click(yui->titlebar_max, yui_titlebar_on_max, yui);
        yetty_ygui_widget_add_child(tb, yui->titlebar_max);
    }
    yui->titlebar_close = yetty_ygui_engine_button(
        yui->engine, "yui_titlebar/close", 0, 0, TITLEBAR_BTN_W, TITLEBAR_STRIP_H,
        "\xC3\x97"); /* × */
    if (yui->titlebar_close) {
        yetty_ygui_widget_set_bg_color(yui->titlebar_close, STRIP_BG);
        yetty_ygui_widget_button_on_click(yui->titlebar_close, yui_titlebar_on_close_window,
                                          yui);
        yetty_ygui_widget_add_child(tb, yui->titlebar_close);
    }

    yui->titlebar = tb;
    yui->titlebar_synced_active = -1;
    yui->titlebar_synced_count = 0;
    yetty_ygui_engine_set_titlebar(yui->engine, tb);
}

static void yui_titlebar_sync(struct yetty_yui *yui)
{
    if (!yui || !yui->titlebar_tabbar || !yui->tabbar_model) {
        return;
    }
    size_t count = yetty_yui_tabbar_count(yui->tabbar_model);
    size_t active = yetty_yui_tabbar_active_index(yui->tabbar_model);

    /* Tab count drift — add or remove tabs to match the model. The
     * tabbar widget keeps its own arrays so this is a per-delta op
     * rather than a wipe-and-rebuild. */
    int wcount = yetty_ygui_widget_tabbar_count(yui->titlebar_tabbar);
    while ((size_t)wcount < count) {
        char label[16];
        snprintf(label, sizeof(label), "Tab %d", wcount + 1);
        yetty_ygui_widget_tabbar_add_tab(yui->titlebar_tabbar, label);
        wcount++;
    }
    while ((size_t)wcount > count) {
        wcount--;
        yetty_ygui_widget_tabbar_remove_tab(yui->titlebar_tabbar, wcount);
    }
    yui->titlebar_synced_count = count;

    /* Active index drift — set_active also re-renders the active
     * highlight + accent bar. Guard so we don't fire on_change every
     * frame: only when the widget's current active != model's. */
    int wactive = yetty_ygui_widget_tabbar_get_active(yui->titlebar_tabbar);
    if (count > 0 && wactive != (int)active) {
        yetty_ygui_widget_tabbar_set_active(yui->titlebar_tabbar, (int)active);
    }
    yui->titlebar_synced_active = (int)active;
}

static void yui_titlebar_on_hamburger(struct yetty_ygui_widget *btn, void *userdata)
{
    (void)btn;
    yetty_yui_show_view_menu((struct yetty_yui *)userdata, 4.0f, 36.0f);
}

static void yui_titlebar_on_new_tab(struct yetty_ygui_widget *btn, void *userdata)
{
    (void)btn;
    struct yetty_yui *yui = userdata;
    if (!yui || !yui->tabbar_model) {
        return;
    }
    /* Spawn a default shell tab; the next sync run will add the
     * matching widget tab. */
    yetty_yui_tabbar_add_workspace_of_kind(yui->tabbar_model, YETTY_YUI_TAB_SHELL);
}

static void yui_titlebar_on_min(struct yetty_ygui_widget *btn, void *userdata)
{
    (void)btn;
    struct yetty_yui *yui = userdata;
    yetty_yui_tabbar_iconify(yui ? yui->tabbar_model : NULL);
}

static void yui_titlebar_on_max(struct yetty_ygui_widget *btn, void *userdata)
{
    (void)btn;
    struct yetty_yui *yui = userdata;
    yetty_yui_tabbar_toggle_maximize(yui ? yui->tabbar_model : NULL);
}

static void yui_titlebar_on_close_window(struct yetty_ygui_widget *btn, void *userdata)
{
    (void)btn;
    struct yetty_yui *yui = userdata;
    yetty_yui_tabbar_close_window(yui ? yui->tabbar_model : NULL);
}

/* TABBAR's on_change fires after the widget has already updated its
 * own active state; forward to the model so the workspace switches.
 * Guard against re-entrancy by checking the model's current active —
 * the model fires its own request_render which will re-sync the widget. */
static void yui_titlebar_on_tab_change(struct yetty_ygui_widget *tabbar, float value,
                                       void *userdata)
{
    (void)tabbar;
    struct yetty_yui *yui = userdata;
    if (!yui || !yui->tabbar_model) {
        return;
    }
    size_t idx = (size_t)value;
    if (idx != yetty_yui_tabbar_active_index(yui->tabbar_model)) {
        yetty_yui_tabbar_switch_to(yui->tabbar_model, idx);
    }
}

/* TABBAR's on_tab_close fires BEFORE the widget removes the tab
 * (the widget's default removal is skipped when this callback is set).
 * We close the matching workspace in the model; the next sync run
 * mirrors the new count into the widget. */
static void yui_titlebar_on_tab_close(struct yetty_ygui_widget *tabbar, float value,
                                      void *userdata)
{
    (void)tabbar;
    struct yetty_yui *yui = userdata;
    if (!yui || !yui->tabbar_model) {
        return;
    }
    size_t idx = (size_t)value;
    yetty_yui_tabbar_close_at(yui->tabbar_model, idx);
}

static void yui_dialog_connect(struct yetty_ygui_widget *button, void *userdata)
{
    (void)button;
    struct yui_cb_ctx *ctx = userdata;
    if (!ctx || !ctx->yui || (int)ctx->kind < 0 ||
        (int)ctx->kind >= YETTY_YUI_VIEW_KIND_COUNT) {
        return;
    }
    /* Hide the dialog first so the next frame already shows it gone,
     * then fire the connect callback. The callback (yetty.c) will spawn
     * the workspace on the same thread, but the dialog is no longer in
     * the way visually. */
    struct yetty_ygui_widget *dlg = ctx->yui->dialogs[(int)ctx->kind];
    if (dlg) {
        yetty_ygui_widget_set_visible(dlg, 0);
    }
    if (ctx->yui->engine) {
        yetty_ygui_engine_mark_dirty(ctx->yui->engine);
    }
    if (ctx->yui->connect_cb) {
        ctx->yui->connect_cb(ctx->yui->connect_userdata, ctx->kind);
    }
}

/*===========================================================================
 * Public menu / connect API
 *===========================================================================*/

void yetty_yui_show_view_menu(struct yetty_yui *yui, float anchor_x, float anchor_y)
{
    /* "view_menu" is now the top-level app (hamburger) menu — the
     * tabbar's hamburger callback still uses this entry point, so the
     * name is kept for API stability. Reset to root level on every
     * open so the user always lands at "Menu" rather than wherever
     * they were when the menu was last closed. */
    if (!yui || !yui->app_menu) {
        return;
    }
    yui_app_menu_populate_root(yui);
    yetty_ygui_widget_popup_menu_open_at(yui->app_menu, anchor_x, anchor_y);
    if (yui->engine) {
        yetty_ygui_engine_mark_dirty(yui->engine);
    }
}

void yetty_yui_show_context_menu(struct yetty_yui *yui, float anchor_x, float anchor_y)
{
    if (!yui || !yui->app_menu) {
        return;
    }
    yui_app_menu_populate_context_root(yui);
    yetty_ygui_widget_popup_menu_open_at(yui->app_menu, anchor_x, anchor_y);
    if (yui->engine) {
        yetty_ygui_engine_mark_dirty(yui->engine);
    }
}

struct yetty_ygui_widget *yetty_yui_statusbar(struct yetty_yui *yui)
{
    return yui ? yui->statusbar : NULL;
}

struct yetty_ygui_engine *yetty_yui_engine(struct yetty_yui *yui)
{
    return yui ? yui->engine : NULL;
}

void yetty_yui_set_status_left(struct yetty_yui *yui, const char *text)
{
    if (!yui || !yui->statusbar) {
        return;
    }
    yetty_ygui_widget_statusbar_set_left(yui->statusbar, text ? text : "");
}

void yetty_yui_set_status_right(struct yetty_yui *yui, const char *text)
{
    if (!yui || !yui->statusbar) {
        return;
    }
    yetty_ygui_widget_statusbar_set_right(yui->statusbar, text ? text : "");
}

float yetty_yui_statusbar_height(const struct yetty_yui *yui)
{
    /* Read the resolved size from the widget via the public accessor —
     * engine_pin_bars rewrites the height every layout pass, so this
     * stays in sync with whatever the bar is currently rendered at.
     * The 22 fallback matches the constructor default for the case
     * where layout hasn't run yet (pre-first-frame). */
    if (!yui || !yui->statusbar) {
        return 0.0f;
    }
    struct pixel_size_result sr = yetty_ygui_widget_get_size(yui->statusbar);
    if (YETTY_IS_ERR(sr)) {
        yetty_ycore_error_destroy(sr.error);
        return 22.0f;
    }
    return sr.value.height > 0.0f ? sr.value.height : 22.0f;
}

const char *yetty_yui_get_field_text(const struct yetty_yui *yui,
                                     enum yetty_yui_view_kind kind, int field_idx)
{
    if (!yui || (int)kind < 0 || (int)kind >= YETTY_YUI_VIEW_KIND_COUNT) {
        return NULL;
    }
    if (field_idx < 0 || field_idx >= 4) {
        return NULL;
    }
    struct yetty_ygui_widget *in = yui->dialog_inputs[(int)kind][field_idx];
    if (!in) {
        return NULL;
    }
    return yetty_ygui_widget_textinput_get_text(in);
}

const char *yetty_yui_get_exec_command(const struct yetty_yui *yui)
{
    return yetty_yui_get_field_text(yui, YETTY_YUI_VIEW_EXEC, 0);
}

int yetty_yui_is_active(const struct yetty_yui *yui)
{
    if (!yui) {
        return 0;
    }
    if (yui->app_menu && yetty_ygui_widget_popup_menu_is_open(yui->app_menu)) {
        return 1;
    }
    for (int k = 0; k < YETTY_YUI_VIEW_KIND_COUNT; k++) {
        if (yui->dialogs[k] && yetty_ygui_widget_is_visible(yui->dialogs[k])) {
            return 1;
        }
    }
    if (yui->gpu_info_dialog && yetty_ygui_widget_is_visible(yui->gpu_info_dialog)) {
        return 1;
    }
    if (yetty_yui_config_dialog_is_visible(yui->config_dialog)) {
        return 1;
    }
    return 0;
}

/* Pick the OS cursor shape for the current engine state. Drag in
 * progress (pressed != NULL) wins over hover so the cursor stays the
 * resize shape even if the user's pointer momentarily strays off the
 * splitter bar mid-drag. (mouse_x, mouse_y) feed the tabbar edge-band
 * check — passed in instead of cached so one mouse event drives exactly
 * one cursor decision (no stale state, no race). */
static int yui_compute_cursor_shape(const struct yetty_yui *yui, float mouse_x, float mouse_y)
{
    if (!yui || !yui->engine) {
        return YETTY_YCORE_CURSOR_DEFAULT;
    }
    struct yetty_ygui_widget *w = yetty_ygui_engine_pressed_widget(yui->engine);
    if (!w) {
        w = yetty_ygui_engine_hovered_widget(yui->engine);
    }
    if (w && yetty_ygui_widget_get_type(w) == YETTY_YGUI_WIDGET_SPLITTER) {
        int axis = yetty_ygui_widget_splitter_get_axis(w);
        /* axis: 1 = row-bar (vertical bar splitting side-by-side panes)
         *   → user resizes the horizontal extent → ↔ HRESIZE cursor.
         *  0 = column-bar (horizontal bar splitting stacked panes)
         *   → user resizes the vertical extent → ↕ VRESIZE cursor. */
        return axis == 1 ? YETTY_YCORE_CURSOR_HRESIZE
                         : YETTY_YCORE_CURSOR_VRESIZE;
    }
    /* No splitter under the cursor — consult the tabbar's invisible
     * edge-resize bands. tabbar_model is bound after yui_create via
     * yetty_yui_set_tabbar_model; before that it's NULL and the helper
     * returns DEFAULT, which is correct (no resize possible yet). */
    if (yui->tabbar_model) {
        int edge_shape = yetty_yui_tabbar_edge_cursor_at(yui->tabbar_model, mouse_x, mouse_y);
        if (edge_shape != YETTY_YCORE_CURSOR_DEFAULT) {
            return edge_shape;
        }
    }
    return YETTY_YCORE_CURSOR_DEFAULT;
}

static void yui_apply_cursor(struct yetty_yui *yui, float mouse_x, float mouse_y)
{
    if (!yui || !yui->ctx) {
        return;
    }
    int shape = yui_compute_cursor_shape(yui, mouse_x, mouse_y);
    if (shape == yui->last_cursor_shape) {
        return; /* nothing to do */
    }
    struct yetty_yplatform_window_manager *wm =
        yui->ctx->app_context.window_manager;
    if (wm && wm->ops && wm->ops->set_cursor) {
        wm->ops->set_cursor(wm, shape);
    }
    yui->last_cursor_shape = shape;
}

struct yetty_ycore_int_result yetty_yui_on_event(struct yetty_yui *yui,
                                                  const struct yetty_yui_event *event)
{
    if (!yui || !event || !yui->engine) {
        return YETTY_OK(yetty_ycore_int, 0);
    }

    /* Three ways yui captures mouse:
     *   (a) Globally — when a menu / dialog is open (is_active). All
     *       mouse events route to the engine and are consumed.
     *   (b) Locally in the titlebar — engine-pinned titlebar widgets
     *       (≡, tabs, +, _, □, ✕) must receive clicks even with no
     *       overlay open.
     *   (c) Locally in the workspace area — only the per-split
     *       divider widgets (engine_splitter, absolutely positioned)
     *       live there. We MOUSE_DOWN through to the engine to give
     *       it a chance to grab a widget; if it does, ongoing
     *       MOVE/UP belong to that drag. If no widget grabs the
     *       click, the workspace beneath sees it unchanged. */
    int active = yetty_yui_is_active(yui);
    int has_pressed = yetty_ygui_engine_has_pressed_widget(yui->engine);
    int in_titlebar = 0;
    switch (event->type) {
    case YETTY_YCORE_MOUSE_DOWN:
    case YETTY_YCORE_MOUSE_UP:
    case YETTY_YCORE_MOUSE_MOVE:
    case YETTY_YCORE_MOUSE_DRAG:
        in_titlebar = event->mouse.y < 36.0f /* YETTY_YUI_TABBAR_HEIGHT */;
        break;
    default:
        break;
    }
    /* For pure-keyboard or non-mouse events with no overlay active,
     * pass through. Mouse events ALWAYS go through the engine hit-test
     * so absolutely-positioned splitter widgets in the workspace area
     * can grab them. */
    if (!active && !in_titlebar && !has_pressed &&
        event->type != YETTY_YCORE_MOUSE_DOWN &&
        event->type != YETTY_YCORE_MOUSE_UP &&
        event->type != YETTY_YCORE_MOUSE_MOVE &&
        event->type != YETTY_YCORE_MOUSE_DRAG) {
        return YETTY_OK(yetty_ycore_int, 0);
    }

    switch (event->type) {
    case YETTY_YCORE_MOUSE_DOWN:
        yetty_ygui_engine_mouse_down(yui->engine, event->mouse.x, event->mouse.y,
                                     event->mouse.button);
        yui_apply_cursor(yui, event->mouse.x, event->mouse.y);
        if (active || yetty_ygui_engine_has_pressed_widget(yui->engine)) {
            return YETTY_OK(yetty_ycore_int, 1);
        }
        /* No widget grabbed it — fall through (tabbar drag handler in
         * the strip, workspace click in the pane area). */
        return YETTY_OK(yetty_ycore_int, 0);
    case YETTY_YCORE_MOUSE_UP:
        yetty_ygui_engine_mouse_up(yui->engine, event->mouse.x, event->mouse.y,
                                   event->mouse.button);
        yui_apply_cursor(yui, event->mouse.x, event->mouse.y);
        /* Consume only if a widget was actually tracking the click
         * (had a pressed widget before this UP) or an overlay is up.
         * Previously we also consumed every titlebar UP, but that
         * starved tabbar.c's window-drag handler of the MOUSE_UP it
         * needs to clear its `dragging` flag — making drag-to-move
         * stick after release. Titlebar UPs that no widget claimed
         * are still safely swallowed by tabbar.c's in-strip return
         * before they can reach the workspace. */
        return YETTY_OK(yetty_ycore_int, active || has_pressed ? 1 : 0);
    case YETTY_YCORE_MOUSE_MOVE:
    case YETTY_YCORE_MOUSE_DRAG:
        yetty_ygui_engine_mouse_move(yui->engine, event->mouse.x, event->mouse.y);
        yui_apply_cursor(yui, event->mouse.x, event->mouse.y);
        /* Consume during an active drag (a splitter or other widget
         * holds the press) so the workspace below doesn't also see
         * the motion. Hover-only moves still pass through so terminal
         * mouse modes work in the panes. */
        return YETTY_OK(yetty_ycore_int, active || has_pressed ? 1 : 0);
    case YETTY_YCORE_KEY_DOWN:
        yetty_ygui_engine_key_down(yui->engine, event->key.key, event->key.mods);
        return YETTY_OK(yetty_ycore_int, 1);
    case YETTY_YCORE_KEY_UP:
        yetty_ygui_engine_key_up(yui->engine, event->key.key, event->key.mods);
        return YETTY_OK(yetty_ycore_int, 1);
    case YETTY_YCORE_CHAR: {
        /* CHAR carries a unicode codepoint; the engine's text_input wants
         * a NUL-terminated UTF-8 chunk. Encode in place — codepoints up to
         * U+10FFFF fit in 4 bytes + NUL. */
        uint32_t cp = event->chr.codepoint;
        char utf8[5];
        size_t n = 0;
        if (cp < 0x80) {
            utf8[n++] = (char)cp;
        } else if (cp < 0x800) {
            utf8[n++] = (char)(0xC0 | (cp >> 6));
            utf8[n++] = (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            utf8[n++] = (char)(0xE0 | (cp >> 12));
            utf8[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            utf8[n++] = (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x110000) {
            utf8[n++] = (char)(0xF0 | (cp >> 18));
            utf8[n++] = (char)(0x80 | ((cp >> 12) & 0x3F));
            utf8[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            utf8[n++] = (char)(0x80 | (cp & 0x3F));
        }
        utf8[n] = '\0';
        if (n > 0) {
            yetty_ygui_engine_text_input(yui->engine, utf8);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }
    default:
        /* Other event types (resize, focus, etc.) pass through. */
        return YETTY_OK(yetty_ycore_int, 0);
    }
}

void yetty_yui_set_connect_callback(struct yetty_yui *yui, yetty_yui_connect_cb cb,
                                    void *userdata)
{
    if (!yui) {
        return;
    }
    yui->connect_cb = cb;
    yui->connect_userdata = userdata;
}

void yetty_yui_set_split_callback(struct yetty_yui *yui, yetty_yui_split_cb cb, void *userdata)
{
    if (!yui) {
        return;
    }
    yui->split_cb = cb;
    yui->split_userdata = userdata;
}

struct yetty_ycore_void_result yetty_yui_resize(struct yetty_yui *yui, uint32_t surface_w,
                                                uint32_t surface_h)
{
    if (!yui || !yui->layer) {
        return YETTY_OK_VOID();
    }
    if (surface_w == 0 || surface_h == 0 || yui->cell_w <= 0.0f || yui->cell_h <= 0.0f) {
        return YETTY_OK_VOID();
    }
    /* Same algorithm as yui_create — round the requested cell stride
     * against the new framebuffer, then re-derive actual cell stride so
     * cols*cell_w == surface_w and rows*cell_h == surface_h. The unified
     * resize_grid pushes both onto the canvas in one atomic step. */
    uint32_t cols = (uint32_t)((float)surface_w / yui->cell_w + 0.5f);
    uint32_t rows = (uint32_t)((float)surface_h / yui->cell_h + 0.5f);
    if (cols == 0) {
        cols = 1;
    }
    if (rows == 0) {
        rows = 1;
    }
    struct yetty_ycore_pixel_size actual_cs = {
        .width = (float)surface_w / (float)cols,
        .height = (float)surface_h / (float)rows,
    };
    yui->surface_w = (float)surface_w;
    yui->surface_h = (float)surface_h;
    if (yui->engine) {
        yetty_ygui_engine_set_display_pixel_size(yui->engine, (float)surface_w, (float)surface_h);
    }
    if (yui->layer->ops->resize_grid) {
        struct yetty_ycore_grid_size gs = {.cols = cols, .rows = rows};
        return yui->layer->ops->resize_grid(yui->layer, gs, actual_cs);
    }
    return YETTY_OK_VOID();
}

