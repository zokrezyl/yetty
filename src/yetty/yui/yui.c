/* yui.c — app-level chrome singleton.
 *
 * See yui.h for the producer → transport → consumer chain. This file owns
 * the wiring; the ypaint-layer (KIND_STATIC) does the rendering and the
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
#include <yetty/yevent/event-loop.h>
#include <yetty/ynotify/ynotify.h>
#include <yetty/yplatform/pty.h>
#include <yetty/yplatform/thread.h>
#include <yetty/yrender/render-target.h>
#include <yetty/yterm/osc-codes.h>
#include <yetty/yterm/osc-statemachine.h>
#include <yetty/yterm/terminal.h>
#include <yetty/yterm/ypaint-layer.h>
#include <yetty/ytrace/ytrace.h>

struct yetty_yui {
    /* Producer-side endpoint. ygui's output flows in here via
     * pty->ops->write. */
    struct yetty_platform_pty *yui_endpoint;

    /* Consumer-side endpoint. The SM reads from here via pty->ops->read
     * (zero copy from the memory-pty's ring). */
    struct yetty_platform_pty *render_endpoint;

    /* Owns the YPAINT decode pipeline (b64 + lz4 today, kept as-is per
     * the simplified plan — we do NOT change the wire codec yet, only
     * the transport). Bound to render_endpoint. */
    struct yetty_yterm_osc_statemachine *sm;

    /* Static-canvas ypaint layer registered against YPAINT_CLEAR/BIN/OVERLAY
     * on `sm`. Same constructor used by the per-terminal static placeholder. */
    struct yetty_yrender_terminal_layer *layer;

    /* Producer engine. Its OSC output is routed via output_pty into
     * `yui_endpoint`; bytes flow through the memory pty → render side
     * → SM → layer. */
    struct yetty_ygui_engine *engine;

    /* The v-menu (popup) and per-view config dialogs, parked on `engine`.
     * Menus start closed; tabbar's v-click opens v_menu, and selecting
     * an item opens the matching dialog. Dialog widget pointers are
     * needed so item callbacks can address-by-name. */
    struct yetty_ygui_widget *v_menu;
    struct yetty_ygui_widget *dialogs[YETTY_YUI_VIEW_KIND_COUNT]; /* indexed by view_kind */

    /* Per-dialog textinput handles, indexed by [view_kind][field_idx]
     * matching s_views[kind].fields[]. yui_dialog_connect / the connect
     * subscribers read these to recover what the user typed. NULL for
     * unused slots (kinds with fewer fields, or kinds with no dialog). */
    struct yetty_ygui_widget *dialog_inputs[YETTY_YUI_VIEW_KIND_COUNT][4];

    /* Connect dispatch — invoked from each dialog's "Connect" button. */
    yetty_yui_connect_cb connect_cb;
    void               *connect_userdata;

    /* Cached for the memory-pty wake bridge. */
    struct yetty_yevent_event_loop *loop;

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
    struct yetty_ycore_void_result r = yetty_yterm_osc_statemachine_process(yui->sm);
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

    /* Static-canvas ypaint layer. Computes a coarse cols/rows from the
     * window dims; chrome primitives are absolute-pixel so the grid is
     * just an addressing index for the static-canvas's primitive lookup. */
    uint32_t cols = (uint32_t)((float)surface_w / cell_w);
    uint32_t rows = (uint32_t)((float)surface_h / cell_h);
    if (cols == 0) {
        cols = 1;
    }
    if (rows == 0) {
        rows = 1;
    }

    struct yetty_yterm_terminal_layer_result lr = yetty_yterm_ypaint_layer_create(
        YETTY_YDRAW_LAYER_KIND_STATIC, cols, rows, cell_w, cell_h, context,
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
    struct yetty_yterm_osc_statemachine_ptr_result sr =
        yetty_yterm_osc_statemachine_create(yui->render_endpoint);
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

    /* Register YPAINT codes against the SM. */
    struct yetty_ycore_void_result rr;
    rr = yetty_yterm_osc_statemachine_register(yui->sm, YETTY_OSC_YPAINT_CLEAR, yui->layer);
    YETTY_RETURN_IF_ERR(yetty_yui_ptr, rr, "yui_create: register CLEAR");
    rr = yetty_yterm_osc_statemachine_register(yui->sm, YETTY_OSC_YPAINT_BIN, yui->layer);
    YETTY_RETURN_IF_ERR(yetty_yui_ptr, rr, "yui_create: register BIN");
    rr = yetty_yterm_osc_statemachine_register(yui->sm, YETTY_OSC_YPAINT_OVERLAY, yui->layer);
    YETTY_RETURN_IF_ERR(yetty_yui_ptr, rr, "yui_create: register OVERLAY");

    /* Wake the consumer side via post_to_loop whenever the producer
     * writes — defers even in same-thread mode, so the wake never
     * re-enters dispatch mid-write. */
    yetty_yplatform_memory_pty_set_wake(yui->render_endpoint, yui_wake, yui);

    /* Producer engine. Local-only — no init/show/subscribe, no parent
     * yetty to talk to via stdout. `output_pty` redirects OSC frame
     * envelopes through the memory pty instead. */
    struct ygui_engine_ptr_result er = yetty_ygui_engine_create("yui", 0, 0, (int)cols, (int)rows);
    if (YETTY_IS_OK(er)) {
        yui->engine = er.value;
        yetty_ygui_engine_set_output_pty(yui->engine, yui->yui_endpoint);
        yetty_ygui_engine_set_display_pixel_size(yui->engine, (float)surface_w, (float)surface_h);

        /* Build the v-menu (closed at start) and one config dialog per
         * view kind. Each dialog is positioned roughly under the v-button
         * with a small offset so successive dialogs don't stack on top
         * of one another. */
        yui->v_menu = yetty_ygui_engine_popup_menu(yui->engine, "yui_v_menu",
                                                   /*x=*/0, /*y=*/0,
                                                   /*w=*/220);
        if (yui->v_menu) {
            yetty_ygui_widget_popup_menu_set_modal(yui->v_menu, 0);
        }

        for (int k = 0; k < YETTY_YUI_VIEW_KIND_COUNT; k++) {
            s_cb_ctx[k].yui = yui;
            s_cb_ctx[k].kind = (enum yetty_yui_view_kind)k;

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

        /* Populate the menu rows. SHELL spawns directly; the rest open
         * their kind's dialog. Order chosen to match how often each is
         * used: default shell first, exec right after, then remote
         * transports grouped at the bottom. */
        if (yui->v_menu) {
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
            for (int i = 0; i < YETTY_YUI_VIEW_KIND_COUNT; i++) {
                int k = MENU_ORDER[i];
                /* SHELL is the only no-dialog item — it dispatches via
                 * yui_menu_spawn_shell so the connect_cb fires immediately
                 * with VIEW_SHELL. The rest open their config dialog. */
                ygui_click_callback_t cb = (k == YETTY_YUI_VIEW_SHELL)
                                               ? yui_menu_spawn_shell
                                               : yui_menu_open_dialog;
                yetty_ygui_widget_popup_menu_add_item(yui->v_menu, LABELS[k], cb,
                                                       &s_cb_ctx[k]);
            }
        }
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

    ydebug("yui_create: grid=%ux%u cell=%.1fx%.1f", cols, rows, cell_w, cell_h);
    return YETTY_OK(yetty_yui_ptr, yui);
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
    if (yui->sm) {
        struct yetty_ycore_void_result r = yetty_yterm_osc_statemachine_destroy(yui->sm);
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
    free(yui);
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Render
 *===========================================================================*/

struct yetty_ycore_void_result yetty_yui_render(struct yetty_yui *yui,
                                                struct yetty_ydraw_core_target *target)
{
    if (!yui || !yui->layer || !target) {
        return YETTY_OK_VOID();
    }

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
        struct yetty_ycore_void_result pr = yetty_yterm_osc_statemachine_process(yui->sm);
        if (YETTY_IS_ERR(pr)) {
            ywarn("yui_render: SM process: %s", pr.error.msg);
            yetty_ycore_error_destroy(pr.error);
        }
    }

    if (yui->layer->ops->is_empty && yui->layer->ops->is_empty(yui->layer)) {
        return YETTY_OK_VOID();
    }
    return yui->layer->ops->render(yui->layer, target);
}

struct yetty_platform_pty *yetty_yui_producer_pty(struct yetty_yui *yui)
{
    return yui ? yui->yui_endpoint : NULL;
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
    if (!yui || !yui->v_menu) {
        return;
    }
    yetty_ygui_widget_popup_menu_open_at(yui->v_menu, anchor_x, anchor_y);
    if (yui->engine) {
        yetty_ygui_engine_mark_dirty(yui->engine);
    }
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

int yetty_yui_has_active_chrome(const struct yetty_yui *yui)
{
    if (!yui) {
        return 0;
    }
    if (yui->v_menu && yetty_ygui_widget_popup_menu_is_open(yui->v_menu)) {
        return 1;
    }
    for (int k = 0; k < YETTY_YUI_VIEW_KIND_COUNT; k++) {
        if (yui->dialogs[k] && yetty_ygui_widget_is_visible(yui->dialogs[k])) {
            return 1;
        }
    }
    return 0;
}

struct yetty_ycore_int_result yetty_yui_on_event(struct yetty_yui *yui,
                                                  const struct yetty_yui_event *event)
{
    if (!yui || !event || !yui->engine) {
        return YETTY_OK(yetty_ycore_int, 0);
    }

    /* yui only owns the pointer while chrome is up. Without an open menu /
     * visible dialog there's nothing for the engine to hit-test — fall
     * through so the workspace below gets the event. */
    if (!yetty_yui_has_active_chrome(yui)) {
        return YETTY_OK(yetty_ycore_int, 0);
    }

    switch (event->type) {
    case YETTY_YCORE_MOUSE_DOWN:
        yetty_ygui_engine_mouse_down(yui->engine, event->mouse.x, event->mouse.y,
                                     event->mouse.button);
        return YETTY_OK(yetty_ycore_int, 1);
    case YETTY_YCORE_MOUSE_UP:
        yetty_ygui_engine_mouse_up(yui->engine, event->mouse.x, event->mouse.y,
                                   event->mouse.button);
        return YETTY_OK(yetty_ycore_int, 1);
    case YETTY_YCORE_MOUSE_MOVE:
    case YETTY_YCORE_MOUSE_DRAG:
        yetty_ygui_engine_mouse_move(yui->engine, event->mouse.x, event->mouse.y);
        return YETTY_OK(yetty_ycore_int, 1);
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

struct yetty_ycore_void_result yetty_yui_resize(struct yetty_yui *yui, uint32_t surface_w,
                                                uint32_t surface_h)
{
    if (!yui || !yui->layer) {
        return YETTY_OK_VOID();
    }
    if (surface_w == 0 || surface_h == 0 || yui->cell_w <= 0.0f || yui->cell_h <= 0.0f) {
        return YETTY_OK_VOID();
    }
    uint32_t cols = (uint32_t)((float)surface_w / yui->cell_w);
    uint32_t rows = (uint32_t)((float)surface_h / yui->cell_h);
    if (cols == 0) {
        cols = 1;
    }
    if (rows == 0) {
        rows = 1;
    }
    if (yui->engine) {
        yetty_ygui_engine_set_display_pixel_size(yui->engine, (float)surface_w, (float)surface_h);
    }
    if (yui->layer->ops->resize_grid) {
        struct yetty_ycore_grid_size gs = {.cols = cols, .rows = rows};
        return yui->layer->ops->resize_grid(yui->layer, gs);
    }
    return YETTY_OK_VOID();
}

