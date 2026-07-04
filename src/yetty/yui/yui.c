/* yui.c — app-level yui singleton.
 *
 * See yui.h for the producer → transport → consumer chain. This file owns
 * the wiring; the ydraw-layer (KIND_SCENE) does the rendering. The ygui
 * producer engine (new yclass-based framework) ships its per-frame
 * envelope straight into yui's own yfigure root container via the
 * in-process yclass slot path (framework_set_container_obj) — no PTY, no
 * OSC framing, no wire-statemachine on the receive side.
 */

#include "yui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/ygui/ygui.h>
#include <yetty/yetty/yetty.h>
#include <yetty/yframework/yframework.h>
#include <yetty/yfigure/figure.h>
#include <yetty/yfigure/container.h>
#include <yetty/yfigure/registry.h>
#include <yetty/ydraw-factory/composite-factory.h>
#include <yetty/yplot/yplot-gen.h>
#include <yetty/yimage/yimage-gen.h>
#include <yetty/yfigure/wire.h>
#include <yetty/ygrid/ygrid.h>
#include <yetty/yconfig/config.h>
#include <yetty/yevent/event-loop.h>
#include <yetty/yfont/font.h>
#include <yetty/yfont/msdf-font.h>
#include <yetty/ynotify/ynotify.h>
#include <yetty/yplatform/thread.h>
#include <yetty/yrender/gpu-allocator.h>
#include <yetty/yrender/render-target.h>
#include <yetty/yterminal/terminal.h>
#include <yetty/ytrace/ytrace.h>
#include <yetty/ywebgpu/utils.h>
#include <yetty/yui/workspace.h>
#include <yetty/yevent/event.h>
#include <yetty/yevent/dispatch.h>
#include <yetty/yplatform/platform-input-pipe.h>
#include <yetty/yplatform/ywindow-chrome/window-chrome.h>
#include <yetty/ychrome/chrome.h> /* YETTY_YCHROME_FLAG_* */
#include <yetty/ychrome/host.h>   /* the shared window chrome (the ONE chrome) */

#include "config-dialog.h"
#include "debug-window.h"

#include <yetty/ywire/wire-statemachine.h>
#include "tabbar-model.h"

/* GLFW keycode for Backspace — KEY_DOWN delivers raw GLFW keycodes;
 * the new ygui textinput wants the ASCII DEL byte. */
#define YUI_GLFW_KEY_BACKSPACE 259

/* Titlebar chrome sizing — picked to feel browser-y: the strip is 32px
 * (the native TABBAR widget's default header height), the side buttons
 * are square-ish pills matching the per-tab close-x footprint. */
#define TITLEBAR_STRIP_H 32.0f
#define TITLEBAR_BTN_W 28.0f
#define TITLEBAR_STRIP_BG 0xFF2C261Eu

#define YETTY_YUI_SPLITTER_THICKNESS 6.0f

static inline void yetty_ycore_error_destroy_safe(struct yetty_ycore_void_result r)
{
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
    }
}

struct yetty_yui {
    /* yui's own root container — owns the per-widget ygrid figures that
     * ygui's emit creates through the typed yfigure yclass stubs. Renders
     * LAST in the frame so the chrome sits above terminal panes painted
     * earlier. */
    struct yetty_yclass_object *root_container_obj;
    /* The same container's yclass object — handed to the ygui framework
     * via set_container_obj so framework_emit drives it in-process. */
    struct yetty_yclass_object *container_obj;
    struct yetty_yfigure_registry *figure_registry;

    struct yetty_yfont_font *font;

    struct yetty_ydraw_composite_factory *composite_factory;
    struct yetty_ygrid_factory_args figure_args;

    /* Producer engine (new yclass framework). framework_emit lays out the
     * widget tree and ships the envelope into root_container. */
    struct yetty_yclass_object *engine;

    /* Engine widget-tree root — a column vbox holding the titlebar
     * (top), a flex spacer, and the statusbar (bottom). Floating
     * overlays (menus, dialogs, splitters, debug windows) are absolute
     * children added on top. */
    struct yetty_yclass_object *root;

    /* Single hamburger / app menu, parked under root and starting
     * closed. Drill-down levels reuse the same popup widget. */
    struct yetty_yclass_object *app_menu;
    int app_menu_level; /* 0 = root, 1 = "New view" submenu */
    struct yetty_yclass_object *dialogs[YETTY_YUI_VIEW_KIND_COUNT]; /* indexed by view_kind */

    /* Per-dialog textinput handles, indexed by [view_kind][field_idx]. */
    struct yetty_yclass_object *dialog_inputs[YETTY_YUI_VIEW_KIND_COUNT][4];

    /* GPU info dialog. */
    struct yetty_yclass_object *gpu_info_dialog;
    struct yetty_yclass_object *gpu_info_textarea;
    WGPUAdapter gpu_info_adapter;
    const struct yetty_ydraw_gpu_allocator *gpu_info_allocator;

    /* Settings dialog — construction lives in config-dialog.c. */
    struct yetty_yui_config_dialog *config_dialog;

    /* Bound tabbar model. */
    struct yetty_yui_tabbar_model *tabbar_model;

    /* The window chrome — the ONE shared engine (drag / edge-resize / maximize +
     * SDF min/max/close + edge cursor), composited as a pinned figure over
     * root_container. yui implements none of that itself anymore; it just paints
     * its ygui tabbar into the title-bar area and lets chrome handle whatever the
     * tabbar didn't (client-first). NULL if chrome creation failed. */
    struct yetty_ychrome_host *chrome;

    /* Engine titlebar — flex-row hbox holding:
     *   [≡ hamburger][native TABBAR widget (flex:1, owns its own "+")][_][□][×] */
    struct yetty_yclass_object *titlebar;
    struct yetty_yclass_object *titlebar_hamburger;
    struct yetty_yclass_object *titlebar_tabbar; /* native ygui tabbar widget */
    struct yetty_yclass_object *titlebar_min;
    struct yetty_yclass_object *titlebar_max;
    struct yetty_yclass_object *titlebar_close;
    int titlebar_synced_active;
    size_t titlebar_synced_count;

    /* Application statusbar — pinned to the bottom of the root vbox. */
    struct yetty_yclass_object *statusbar;

    /* Connect dispatch — invoked from each dialog's "Connect" button. */
    yetty_yui_connect_cb connect_cb;
    void *connect_userdata;

    /* Split dispatch — invoked from the context menu's "Split V/H ▸". */
    yetty_yui_split_cb split_cb;
    void *split_userdata;

    /* Cached for the memory-pty wake bridge. */
    struct yetty_yevent_event_loop *loop;

    const struct yetty_context *ctx;

    float surface_w;
    float surface_h;

    /* HiDPI scale = framebuffer px / logical px (1.0 on non-HiDPI),
     * captured from the gpu context at create. The figure container and
     * the terminal workspace live in framebuffer pixels, but the ygui
     * chrome (tabbar/statusbar/dialogs/splitters) is authored and laid
     * out in display-independent LOGICAL pixels: the receiver-side ygrid
     * multiplies every chrome coordinate back up by this factor. So the
     * boundary code here divides framebuffer-pixel inputs (viewport,
     * pointer events, tile-derived splitter bounds) by content_scale on
     * the way into ygui, and multiplies ygui-reported logical deltas
     * back out on the way to the framebuffer-pixel tile model. */
    float content_scale;

    /* Per-split visual divider widgets. */
    struct yetty_yui_splitter_entry {
        yetty_ycore_object_id split_id;     /* tile_id of the yui_split */
        yetty_ycore_object_id workspace_id; /* parent workspace id */
        struct yetty_yclass_object *widget;
        struct yetty_yui *yui;
        struct yetty_yui_splitter_thunk *thunk; /* owned; widget cb userdata */
        int seen;
    } *splitters;
    size_t splitter_count;
    size_t splitter_cap;

    /* Per-pane debug window widgets. */
    struct yetty_yui_debug_window_entry {
        yetty_ycore_object_id pane_id;
        yetty_ycore_object_id workspace_id;
        struct yetty_yui_debug_window *dw;
        int seen;
    } *debug_windows;
    size_t debug_window_count;
    size_t debug_window_cap;

    int last_cursor_shape;

    float cell_w;
    float cell_h;
};

/* Add `cls` under `parent`, returning the new object. Propagates the
 * class-getter error and the add error so the caller surfaces the cause.
 * A NULL parent yields a NULL object (no error) — callers that treat a
 * missing parent as a soft skip rely on this. */
static struct yetty_yclass_object_ptr_result yui_add(struct yetty_yclass_object *parent,
                                                     struct yetty_yclass_ptr_result cls_r)
{
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, cls_r, "yui_add: class getter");
    if (!parent) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_object_ptr_result r = yetty_ygui_widget_add(parent, cls_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, r, "yui_add: add");
    return r;
}

/*===========================================================================
 * ynotify bridge — yui registers itself as the global notification handler.
 *===========================================================================*/

static struct yetty_yui *s_active_yui = NULL;
static struct yetty_yplatform_ymutex *s_active_yui_mutex = NULL;

struct yui_ynotify_thunk {
    int severity;
    uint32_t ttl_ms; /* 0 = use severity default */
    char msg[];      /* NUL-terminated */
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
            yetty_ygui_framework_notify_ttl(yui->engine, t->severity, t->msg,
                                            (float)t->ttl_ms / 1000.0f);
        } else {
            yetty_ygui_framework_notify(yui->engine, t->severity, t->msg);
        }
    }
    free(t);
}

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
    t->ttl_ms = 0;
    memcpy(t->msg, msg, mlen);
    loop->ops->post_to_loop(loop, yui_ynotify_dispatch, t);
}

/*===========================================================================
 * View kind metadata
 *===========================================================================*/

struct view_meta {
    const char *title;
    const char *id_prefix;
    int num_fields;
    struct {
        const char *label;
        const char *id_suffix;
        const char *placeholder;
        const char *default_text;
    } fields[4];
};

static const struct view_meta s_views[YETTY_YUI_VIEW_KIND_COUNT] = {
    [YETTY_YUI_VIEW_SHELL] =
        {
            .title = "Open local shell",
            .id_prefix = "yui_dlg_shell",
            .num_fields = 0,
            .fields = {{0}},
        },
    [YETTY_YUI_VIEW_SSH] =
        {
            .title = "Open SSH",
            .id_prefix = "yui_dlg_ssh",
            .num_fields = 3,
            .fields =
                {
                    {"Host", "/host", "user@host", ""},
                    {"Port", "/port", "22", "22"},
                    {"Key path", "/key", "~/.ssh/id_rsa", ""},
                },
        },
    [YETTY_YUI_VIEW_TELNET] =
        {
            .title = "Open Telnet",
            .id_prefix = "yui_dlg_telnet",
            .num_fields = 2,
            .fields =
                {
                    {"Host", "/host", "host", ""},
                    {"Port", "/port", "23", "23"},
                },
        },
    [YETTY_YUI_VIEW_YVNC] =
        {
            .title = "Open yVNC",
            .id_prefix = "yui_dlg_yvnc",
            .num_fields = 2,
            .fields =
                {
                    {"Host", "/host", "host", ""},
                    {"Port", "/port", "5900", "5900"},
                },
        },
    [YETTY_YUI_VIEW_EXEC] =
        {
            .title = "Run a command",
            .id_prefix = "yui_dlg_exec",
            .num_fields = 1,
            .fields =
                {
                    {"Command", "/cmd", "/usr/bin/htop", ""},
                },
        },
};

/*===========================================================================
 * Menu / dialog callbacks — new yclass signatures.
 *   menu_item_cb : (ctx, menu, item_index, userdata)
 *   click_cb     : (ctx, obj, userdata)
 *===========================================================================*/

static struct yetty_ycore_void_result yui_menu_open_dialog(struct yetty_yclass_object *menu,
                                                           int item_index, void *userdata);
static struct yetty_ycore_void_result yui_menu_spawn_shell(struct yetty_yclass_object *menu,
                                                           int item_index, void *userdata);
static struct yetty_ycore_void_result yui_dialog_connect(struct yetty_yclass_object *button,
                                                         void *userdata);
static struct yetty_ycore_void_result yui_dialog_cancel(struct yetty_yclass_object *button,
                                                        void *userdata);
static struct yetty_ycore_void_result yui_app_menu_open_new_view(struct yetty_yclass_object *menu,
                                                                 int item_index, void *userdata);
static struct yetty_ycore_void_result yui_app_menu_open_gpu_info(struct yetty_yclass_object *menu,
                                                                 int item_index, void *userdata);
static struct yetty_ycore_void_result yui_app_menu_back_to_root(struct yetty_yclass_object *menu,
                                                                int item_index, void *userdata);
static struct yetty_ycore_void_result yui_app_menu_populate_root(struct yetty_yui *yui);
static struct yetty_ycore_void_result yui_app_menu_populate_new_view(struct yetty_yui *yui);
static struct yetty_ycore_void_result yui_app_menu_populate_context_root(struct yetty_yui *yui);
static struct yetty_ycore_void_result yui_app_menu_populate_split_kind(struct yetty_yui *yui,
                                                                       int horizontal);
static struct yetty_ycore_void_result yui_context_open_split_vertical(
    struct yetty_yclass_object *menu, int item_index, void *userdata);
static struct yetty_ycore_void_result yui_context_open_split_horizontal(
    struct yetty_yclass_object *menu, int item_index, void *userdata);
static struct yetty_ycore_void_result yui_split_kind_action(struct yetty_yclass_object *menu,
                                                            int item_index, void *userdata);
static struct yetty_ycore_void_result yui_split_back_to_context(struct yetty_yclass_object *menu,
                                                                int item_index, void *userdata);
static struct yetty_ycore_void_result yui_context_toggle_debug_window(
    struct yetty_yclass_object *menu, int item_index, void *userdata);
static struct yetty_ycore_void_result yui_gpu_info_refresh(struct yetty_yclass_object *button,
                                                           void *userdata);
static struct yetty_ycore_void_result yui_gpu_info_close(struct yetty_yclass_object *button,
                                                         void *userdata);
static struct yetty_ycore_void_result yui_app_menu_open_settings(struct yetty_yclass_object *menu,
                                                                 int item_index, void *userdata);

/* Titlebar (ygui-driven). */
static struct yetty_ycore_void_result yui_titlebar_build(struct yetty_yui *yui);
static struct yetty_ycore_void_result yui_titlebar_sync(struct yetty_yui *yui);
static struct yetty_ycore_void_result yui_titlebar_on_hamburger(struct yetty_yclass_object *btn,
                                                                void *userdata);
YETTY_EXTERNAL_CALLBACK
static void yui_titlebar_on_new_tab(struct yetty_yclass_object *tabbar, void *userdata);
static struct yetty_ycore_void_result yui_titlebar_on_min(struct yetty_yclass_object *btn,
                                                          void *userdata);
static struct yetty_ycore_void_result yui_titlebar_on_max(struct yetty_yclass_object *btn,
                                                          void *userdata);
static struct yetty_ycore_void_result yui_titlebar_on_close_window(struct yetty_yclass_object *btn,
                                                                   void *userdata);
static struct yetty_ycore_void_result yui_titlebar_on_tab_change(
    struct yetty_yclass_object *target, const struct yetty_ygui_event *event, void *userdata);
YETTY_EXTERNAL_CALLBACK
static void yui_titlebar_on_tab_close(struct yetty_yclass_object *tabbar, int index,
                                      void *userdata);

/* Per-(orientation, kind) bundle for split-from-context. */
struct yui_split_ctx {
    struct yetty_yui *yui;
    enum yetty_yui_view_kind kind;
    int horizontal;
};
static struct yui_split_ctx s_split_ctx[2][YETTY_YUI_VIEW_KIND_COUNT];

struct yui_cb_ctx {
    struct yetty_yui *yui;
    enum yetty_yui_view_kind kind;
};

static struct yui_cb_ctx s_cb_ctx[YETTY_YUI_VIEW_KIND_COUNT];

/*===========================================================================
 * Lifecycle
 *===========================================================================*/

/* Build one config dialog window for view kind `k` under `root`. */
static struct yetty_ycore_void_result yui_build_view_dialog(struct yetty_yui *yui, int k)
{
    struct yetty_yclass_object_ptr_result dlg_r = yui_add(yui->root, yetty_ygui_window_class_get());
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dlg_r, "build_view_dialog: window");
    struct yetty_yclass_object *dlg = dlg_r.value;
    yui->dialogs[k] = dlg;
    if (!dlg) {
        return YETTY_OK_VOID();
    }
    yetty_ycore_error_destroy_safe(yetty_ygui_window_set_title(dlg, s_views[k].title));
    yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_size(dlg, 360.0f, 220.0f));
    yetty_ycore_error_destroy_safe(
        yetty_ygui_widget_set_position(dlg, 80.0f + (float)k * 12.0f, 60.0f));
    yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_visible(dlg, 0));

    struct yetty_yclass_object_ptr_result body_res = yetty_ygui_window_body(dlg);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, body_res, "build_view_dialog: window body");
    struct yetty_yclass_object *body = body_res.value;
    if (!body) {
        return YETTY_OK_VOID();
    }
    yetty_ycore_error_destroy_safe(yetty_ygui_widget_apply_css(
        body, "display:flex;flex-direction:column;gap:14;padding:14 14 14 14;"));

    for (int f = 0; f < s_views[k].num_fields; f++) {
        struct yetty_yclass_object_ptr_result row_r = yui_add(body, yetty_ygui_hbox_class_get());
        YETTY_RETURN_IF_ERR(yetty_ycore_void, row_r, "build_view_dialog: row");
        struct yetty_yclass_object *row = row_r.value;
        if (!row) {
            continue;
        }
        yetty_ycore_error_destroy_safe(yetty_ygui_widget_apply_css(
            row, "display:flex;flex-direction:row;gap:10;align-items:center;min-height:28;"));
        yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_size(row, 0.0f, 28.0f));

        struct yetty_yclass_object_ptr_result lbl_r = yui_add(row, yetty_ygui_label_class_get());
        YETTY_RETURN_IF_ERR(yetty_ycore_void, lbl_r, "build_view_dialog: label");
        struct yetty_yclass_object *lbl = lbl_r.value;
        if (lbl) {
            yetty_ycore_error_destroy_safe(
                yetty_ygui_label_set_text(lbl, s_views[k].fields[f].label));
            yetty_ycore_error_destroy_safe(yetty_ygui_widget_apply_css(lbl, "width:30%;"));
        }
        struct yetty_yclass_object_ptr_result input_r =
            yui_add(row, yetty_ygui_textinput_class_get());
        YETTY_RETURN_IF_ERR(yetty_ycore_void, input_r, "build_view_dialog: textinput");
        struct yetty_yclass_object *in = input_r.value;
        if (in) {
            yetty_ycore_error_destroy_safe(
                yetty_ygui_textinput_set_placeholder(in, s_views[k].fields[f].placeholder));
            yetty_ycore_error_destroy_safe(yetty_ygui_widget_apply_css(in, "flex:1 0 0;"));
            yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_size(in, 0.0f, 24.0f));
            if (s_views[k].fields[f].default_text && s_views[k].fields[f].default_text[0]) {
                yetty_ycore_error_destroy_safe(
                    yetty_ygui_textinput_set_text(in, s_views[k].fields[f].default_text));
            }
            if (f < 4) {
                yui->dialog_inputs[k][f] = in;
            }
        }
    }

    struct yetty_yclass_object_ptr_result actions_r = yui_add(body, yetty_ygui_hbox_class_get());
    YETTY_RETURN_IF_ERR(yetty_ycore_void, actions_r, "build_view_dialog: actions");
    struct yetty_yclass_object *actions = actions_r.value;
    if (actions) {
        yetty_ycore_error_destroy_safe(yetty_ygui_widget_apply_css(
            actions, "display:flex;flex-direction:row;justify-content:end;gap:8;"));
        yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_size(actions, 0.0f, 36.0f));
        struct yetty_yclass_object_ptr_result cancel_r =
            yui_add(actions, yetty_ygui_button_class_get());
        YETTY_RETURN_IF_ERR(yetty_ycore_void, cancel_r, "build_view_dialog: cancel");
        struct yetty_yclass_object *cancel = cancel_r.value;
        if (cancel) {
            yetty_ycore_error_destroy_safe(yetty_ygui_button_set_label(cancel, "Cancel"));
            yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_size(cancel, 80.0f, 28.0f));
            yetty_ycore_error_destroy_safe(
                yetty_ygui_clickable_on_click_set(cancel, yui_dialog_cancel, &s_cb_ctx[k]));
        }
        struct yetty_yclass_object_ptr_result connect_r =
            yui_add(actions, yetty_ygui_button_class_get());
        YETTY_RETURN_IF_ERR(yetty_ycore_void, connect_r, "build_view_dialog: connect");
        struct yetty_yclass_object *connect = connect_r.value;
        if (connect) {
            yetty_ycore_error_destroy_safe(yetty_ygui_button_set_label(connect, "Connect"));
            yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_size(connect, 96.0f, 28.0f));
            yetty_ycore_error_destroy_safe(
                yetty_ygui_clickable_on_click_set(connect, yui_dialog_connect, &s_cb_ctx[k]));
        }
    }
    return YETTY_OK_VOID();
}

/* Build the GPU info dialog under `root`. */
static struct yetty_ycore_void_result yui_build_gpu_dialog(struct yetty_yui *yui,
                                                           const struct yetty_context *context)
{
    yui->gpu_info_adapter = context->runtime->gpu.adapter;
    yui->gpu_info_allocator = context->runtime->gpu.allocator;

    struct yetty_yclass_object_ptr_result gpu_dlg_r =
        yui_add(yui->root, yetty_ygui_window_class_get());
    YETTY_RETURN_IF_ERR(yetty_ycore_void, gpu_dlg_r, "build_gpu_dialog: window");
    struct yetty_yclass_object *gpu_dlg = gpu_dlg_r.value;
    yui->gpu_info_dialog = gpu_dlg;
    if (!gpu_dlg) {
        return YETTY_OK_VOID();
    }
    yetty_ycore_error_destroy_safe(yetty_ygui_window_set_title(gpu_dlg, "GPU info"));
    yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_size(gpu_dlg, 560.0f, 360.0f));
    yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_position(gpu_dlg, 120.0f, 80.0f));
    yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_floating(gpu_dlg, 1));
    yetty_ycore_error_destroy_safe(
        yetty_ygui_widget_set_visible(gpu_dlg, getenv("YUI_DEBUG_OPEN_GPU_DIALOG") ? 1 : 0));

    struct yetty_yclass_object_ptr_result body_res = yetty_ygui_window_body(gpu_dlg);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, body_res, "build_gpu_dialog: window body");
    struct yetty_yclass_object *body = body_res.value;
    if (!body) {
        return YETTY_OK_VOID();
    }
    yetty_ycore_error_destroy_safe(yetty_ygui_widget_apply_css(
        body, "display:flex;flex-direction:column;gap:10;padding:14 14 14 14;"));
    struct yetty_yclass_object_ptr_result ta_r = yui_add(body, yetty_ygui_textarea_class_get());
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ta_r, "build_gpu_dialog: textarea");
    struct yetty_yclass_object *ta = ta_r.value;
    if (ta) {
        yetty_ycore_error_destroy_safe(yetty_ygui_widget_apply_css(ta, "flex:1 1 0;"));
        yetty_ycore_error_destroy_safe(
            yetty_ygui_textarea_set_text(ta, "(no info yet — click Refresh)"));
        yui->gpu_info_textarea = ta;
    }
    struct yetty_yclass_object_ptr_result actions_r = yui_add(body, yetty_ygui_hbox_class_get());
    YETTY_RETURN_IF_ERR(yetty_ycore_void, actions_r, "build_gpu_dialog: actions");
    struct yetty_yclass_object *actions = actions_r.value;
    if (actions) {
        yetty_ycore_error_destroy_safe(yetty_ygui_widget_apply_css(
            actions, "display:flex;flex-direction:row;justify-content:end;gap:8;"
                     "flex:0 0 auto;align-items:center;"));
        yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_size(actions, 0.0f, 36.0f));
        struct yetty_yclass_object_ptr_result refresh_r =
            yui_add(actions, yetty_ygui_button_class_get());
        YETTY_RETURN_IF_ERR(yetty_ycore_void, refresh_r, "build_gpu_dialog: refresh");
        struct yetty_yclass_object *refresh = refresh_r.value;
        if (refresh) {
            yetty_ycore_error_destroy_safe(yetty_ygui_button_set_label(refresh, "Refresh"));
            yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_size(refresh, 96.0f, 28.0f));
            yetty_ycore_error_destroy_safe(
                yetty_ygui_clickable_on_click_set(refresh, yui_gpu_info_refresh, yui));
        }
        struct yetty_yclass_object_ptr_result close_r =
            yui_add(actions, yetty_ygui_button_class_get());
        YETTY_RETURN_IF_ERR(yetty_ycore_void, close_r, "build_gpu_dialog: close");
        struct yetty_yclass_object *close = close_r.value;
        if (close) {
            yetty_ycore_error_destroy_safe(yetty_ygui_button_set_label(close, "Close"));
            yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_size(close, 80.0f, 28.0f));
            yetty_ycore_error_destroy_safe(
                yetty_ygui_clickable_on_click_set(close, yui_gpu_info_close, yui));
        }
    }
    return YETTY_OK_VOID();
}

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
    yui->content_scale = context->runtime->gpu.app_gpu_context.content_scale;
    if (yui->content_scale <= 0.0f) {
        yui->content_scale = 1.0f;
    }
    yui->last_cursor_shape = -1;
    yui->cell_w = cell_w;
    yui->cell_h = cell_h;

    /* Default MSDF font handed to every ygrid figure the root mints. */
    {
        struct yetty_yconfig_config *config = context->runtime->config;
        const char *fonts_dir = config->ops->get_string(config, "paths/fonts", "");
        const char *shaders_dir = config->ops->get_string(config, "paths/shaders", "");
        const char *font_family = "DejaVuSansMNerdFontMono";
        char cdb_path[768];
        char shader_path[768];
        snprintf(cdb_path, sizeof(cdb_path), "%s/../msdf-fonts/%s-Regular.cdb", fonts_dir,
                 font_family);
        snprintf(shader_path, sizeof(shader_path), "%s/msdf-font.wgsl", shaders_dir);
        struct yetty_font_font_result fr =
            yetty_yfont_msdf_font_create(cdb_path, shader_path, "yui_default");
        if (!YETTY_IS_OK(fr)) {
            free(yui);
            return YETTY_ERR(yetty_yui_ptr, "yui_create: msdf_font_create", fr);
        }
        yui->font = fr.value;
        struct yetty_ycore_void_result load = yui->font->ops->load_basic_latin(yui->font);
        if (!YETTY_IS_OK(load)) {
            yui->font->ops->destroy(yui->font);
            free(yui);
            return YETTY_ERR(yetty_yui_ptr, "yui_create: load_basic_latin", load);
        }
    }

    /* Build registry + register ygrid factory, then the root container
     * that consumes ygui's records. */
    {
        struct yetty_ydraw_composite_factory_ptr_result ffr = yetty_ydraw_composite_factory_create(
            context->runtime->gpu.device, context->runtime->gpu.queue,
            context->runtime->gpu.surface_format, context->runtime->gpu.allocator,
            context->event_loop);
        if (!YETTY_IS_OK(ffr)) {
            yui->font->ops->destroy(yui->font);
            free(yui);
            return YETTY_ERR(yetty_yui_ptr, "yui_create: raw_composite_factory create", ffr);
        }
        yui->composite_factory = ffr.value;
        struct yetty_ydraw_concrete_factory *yplot_f = yetty_yplot_factory_create();
        if (yplot_f) {
            yplot_f->destroy = yetty_yplot_factory_destroy;
            struct yetty_ycore_void_result rr =
                yetty_ydraw_composite_factory_register(yui->composite_factory, yplot_f);
            if (YETTY_IS_ERR(rr)) {
                yetty_ycore_error_destroy(rr.error);
                yetty_yplot_factory_destroy(yplot_f);
            }
        }
        struct yetty_ydraw_concrete_factory *yimage_f = yetty_yimage_factory_create();
        if (yimage_f) {
            yimage_f->destroy = yetty_yimage_factory_destroy;
            struct yetty_ycore_void_result rr =
                yetty_ydraw_composite_factory_register(yui->composite_factory, yimage_f);
            if (YETTY_IS_ERR(rr)) {
                yetty_ycore_error_destroy(rr.error);
                yetty_yimage_factory_destroy(yimage_f);
            }
        }
        yui->figure_args.default_font = yui->font;
        yui->figure_args.composite_factory = yui->composite_factory;

        struct yetty_yfigure_registry_ptr_result reg_res = yetty_yfigure_registry_create();
        if (!YETTY_IS_OK(reg_res)) {
            yetty_ydraw_composite_factory_destroy(yui->composite_factory);
            yui->font->ops->destroy(yui->font);
            free(yui);
            return YETTY_ERR(yetty_yui_ptr, "yui_create: registry", reg_res);
        }
        yui->figure_registry = reg_res.value;
        struct yetty_ycore_void_result rf =
            yetty_ygrid_register_factory(yui->figure_registry, &yui->figure_args);
        if (!YETTY_IS_OK(rf)) {
            (void)yetty_yfigure_registry_destroy(yui->figure_registry);
            yetty_ydraw_composite_factory_destroy(yui->composite_factory);
            yui->font->ops->destroy(yui->font);
            free(yui);
            return YETTY_ERR(yetty_yui_ptr, "yui_create: ygrid register_factory", rf);
        }
        static const char *const producer_kind_names[] = {"yplot", "yimage", "yvideo", "yzoo",
                                                          "yjungle"};
        for (size_t i = 0; i < sizeof(producer_kind_names) / sizeof(producer_kind_names[0]); i++) {
            struct yetty_ycore_void_result kr = yetty_ygrid_register_factory_for_kind(
                yui->figure_registry, yetty_yfigure_kind_token(producer_kind_names[i]),
                &yui->figure_args);
            if (!YETTY_IS_OK(kr)) {
                (void)yetty_yfigure_registry_destroy(yui->figure_registry);
                yetty_ydraw_composite_factory_destroy(yui->composite_factory);
                yui->font->ops->destroy(yui->font);
                free(yui);
                return YETTY_ERR(yetty_yui_ptr, "yui_create: ygrid register_factory_for_kind", kr);
            }
        }
        {
            struct yetty_ycore_void_result fr = yetty_yframework_register_figure_factories(
                context->runtime, yui->figure_registry, context);
            if (!YETTY_IS_OK(fr)) {
                (void)yetty_yfigure_registry_destroy(yui->figure_registry);
                yetty_ydraw_composite_factory_destroy(yui->composite_factory);
                yui->font->ops->destroy(yui->font);
                free(yui);
                return YETTY_ERR(yetty_yui_ptr, "yui_create: framework register_figure_factories",
                                 fr);
            }
        }
        struct yetty_ycore_rectangle root_rect = {
            .min = {.x = 0.0f, .y = 0.0f},
            .max = {.x = (float)surface_w, .y = (float)surface_h},
        };
        struct yetty_yclass_ctx yclass_ctx = {0};
        struct yetty_yclass_object_ptr_result obj_res = yetty_yfigure_container_create(&yclass_ctx);
        if (!YETTY_IS_OK(obj_res)) {
            (void)yetty_yfigure_registry_destroy(yui->figure_registry);
            yui->font->ops->destroy(yui->font);
            free(yui);
            return YETTY_ERR(yetty_yui_ptr, "yui_create: root_container", obj_res);
        }
        yui->container_obj = obj_res.value;
        yui->root_container_obj = obj_res.value;
        yetty_yfigure_container_set_context(yui->root_container_obj, context);
        yetty_yfigure_container_set_registry(yui->root_container_obj, yui->figure_registry);
        yetty_yfigure_container_set_rect(yui->root_container_obj, root_rect);
    }

    /* Producer engine (new framework). No output pty — the envelope is
     * shipped in-process into root_container via the yclass slot path. */
    struct yetty_yclass_object_ptr_result er = yetty_ygui_framework_create(NULL);
    if (YETTY_IS_OK(er)) {
        yui->engine = er.value;
        yetty_ycore_error_destroy_safe(
            yetty_ygui_framework_set_container_obj(yui->engine, yui->container_obj));
        /* Logical viewport: the chrome lays out in display-independent
         * pixels; the ygrid receiver scales back to framebuffer pixels. */
        yetty_ycore_error_destroy_safe(
            yetty_ygui_framework_set_viewport(yui->engine, (float)surface_w / yui->content_scale,
                                              (float)surface_h / yui->content_scale));
        /* Theme: overlay any style.ygui.* / style.yui.* keys from the
         * user's config onto the engine's brand-default theme. Missing
         * keys leave the defaults untouched. Once this returns, widget
         * paint (tabbar pills, button label color, accent bar) reads
         * its colors from engine->theme. */
        yetty_ycore_error_destroy_safe(
            yetty_ygui_framework_apply_config_to_theme(yui->engine, yui->ctx->runtime->config));

        /* Root vbox — titlebar (top), spacer (flex), statusbar (bottom).
         * Overlays float on top as absolute children added afterwards. */
        struct yetty_yclass_object_ptr_result rr =
            yetty_ygui_widget_new(yetty_ygui_vbox_class_get().value);
        if (YETTY_IS_OK(rr)) {
            yui->root = rr.value;
            struct yetty_ygui_layout_const_ptr_result root_layout_res =
                yetty_ygui_widget_layout_get(yui->root);
            if (YETTY_IS_OK(root_layout_res)) {
                struct yetty_ygui_layout l = *root_layout_res.value;
                l.direction = YETTY_YGUI_FLEX_COLUMN;
                l.align = YETTY_YGUI_ALIGN_STRETCH;
                l.gap = 0.0f;
                yetty_ycore_error_destroy_safe(yetty_ygui_widget_layout_set(yui->root, &l));
            } else {
                ywarn("yui_create: root layout_get: %s", root_layout_res.error.msg);
                yetty_ycore_error_destroy(root_layout_res.error);
            }
            yetty_ycore_error_destroy_safe(yetty_ygui_framework_set_root(yui->engine, yui->root));
        } else {
            yetty_ycore_error_destroy(rr.error);
        }

        /* Titlebar tree first so overlays (added below) paint over it.
         * Chrome construction is best-effort: a build error leaves the
         * engine without that piece of chrome rather than aborting the
         * whole yui (mirrors the engine-create failure path below). */
        {
            struct yetty_ycore_void_result tb_r = yui_titlebar_build(yui);
            if (YETTY_IS_ERR(tb_r)) {
                ywarn("yui_create: titlebar_build: %s", tb_r.error.msg);
                yetty_ycore_error_destroy(tb_r.error);
            }
        }

        /* Window chrome — the shared ychrome, composited over the same container
         * the engine paints into. It overlays the SDF min/max/close and owns
         * drag/edge-resize/maximize for whatever the title-bar tabbar doesn't
         * consume (client-first). Best-effort, like the titlebar above. */
        if (context->runtime && context->runtime->window_chrome) {
            struct yetty_ychrome_host_ptr_result ch = yetty_ychrome_host_create(
                yui->root_container_obj, yui->font, context, context->runtime->window_chrome,
                (float)surface_w, (float)surface_h, TITLEBAR_STRIP_H, 8.0f, YETTY_YCHROME_FLAG_ALL);
            if (YETTY_IS_OK(ch)) {
                yui->chrome = ch.value;
            } else {
                ywarn("yui_create: chrome host: %s", ch.error.msg);
                yetty_ycore_error_destroy(ch.error);
            }
        }

        /* Spacer pushes the statusbar to the bottom. */
        struct yetty_yclass_object_ptr_result spacer_r =
            yui_add(yui->root, yetty_ygui_vbox_class_get());
        if (YETTY_IS_ERR(spacer_r)) {
            ywarn("yui_create: spacer add: %s", spacer_r.error.msg);
            yetty_ycore_error_destroy(spacer_r.error);
        } else if (spacer_r.value) {
            struct yetty_ygui_layout_const_ptr_result spacer_layout_res =
                yetty_ygui_widget_layout_get(spacer_r.value);
            if (YETTY_IS_OK(spacer_layout_res)) {
                struct yetty_ygui_layout l = *spacer_layout_res.value;
                l.flex_grow = 1.0f;
                yetty_ycore_error_destroy_safe(yetty_ygui_widget_layout_set(spacer_r.value, &l));
            } else {
                ywarn("yui_create: spacer layout_get: %s", spacer_layout_res.error.msg);
                yetty_ycore_error_destroy(spacer_layout_res.error);
            }
        }

        /* Statusbar — bottom strip. */
        struct yetty_yclass_object_ptr_result sb_r =
            yui_add(yui->root, yetty_ygui_statusbar_class_get());
        if (YETTY_IS_ERR(sb_r)) {
            ywarn("yui_create: statusbar add: %s", sb_r.error.msg);
            yetty_ycore_error_destroy(sb_r.error);
        } else if (sb_r.value) {
            yui->statusbar = sb_r.value;
            yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_size(yui->statusbar, 0.0f, 22.0f));
            yetty_ycore_error_destroy_safe(yetty_ygui_statusbar_set_left(yui->statusbar, "Ready"));
            yetty_ycore_error_destroy_safe(yetty_ygui_statusbar_set_right(yui->statusbar, "yetty"));
        }

        /* App / hamburger menu — drill-down popup reused across levels. */
        struct yetty_yclass_object_ptr_result app_menu_r =
            yui_add(yui->root, yetty_ygui_popup_menu_class_get());
        if (YETTY_IS_ERR(app_menu_r)) {
            ywarn("yui_create: app_menu add: %s", app_menu_r.error.msg);
            yetty_ycore_error_destroy(app_menu_r.error);
        } else {
            yui->app_menu = app_menu_r.value;
        }
        yui->app_menu_level = 0;
        if (yui->app_menu) {
            yetty_ycore_error_destroy_safe(yetty_ygui_popup_menu_set_modal(yui->app_menu, 0));
        }

        for (int k = 0; k < YETTY_YUI_VIEW_KIND_COUNT; k++) {
            s_cb_ctx[k].yui = yui;
            s_cb_ctx[k].kind = (enum yetty_yui_view_kind)k;
            for (int o = 0; o < 2; o++) {
                s_split_ctx[o][k].yui = yui;
                s_split_ctx[o][k].kind = (enum yetty_yui_view_kind)k;
                s_split_ctx[o][k].horizontal = o;
            }
            if (s_views[k].num_fields == 0) {
                yui->dialogs[k] = NULL;
                continue;
            }
            struct yetty_ycore_void_result view_r = yui_build_view_dialog(yui, k);
            if (YETTY_IS_ERR(view_r)) {
                ywarn("yui_create: build_view_dialog[%d]: %s", k, view_r.error.msg);
                yetty_ycore_error_destroy(view_r.error);
            }
        }

        {
            struct yetty_ycore_void_result gpu_r = yui_build_gpu_dialog(yui, context);
            if (YETTY_IS_ERR(gpu_r)) {
                ywarn("yui_create: build_gpu_dialog: %s", gpu_r.error.msg);
                yetty_ycore_error_destroy(gpu_r.error);
            }
        }

        /* Settings dialog — best-effort. */
        struct yetty_yui_config_dialog_ptr_result cdr =
            yetty_yui_config_dialog_create(yui->engine, context->runtime->config);
        if (YETTY_IS_OK(cdr)) {
            yui->config_dialog = cdr.value;
        } else {
            ywarn("yui_create: config_dialog create: %s", cdr.error.msg);
            yetty_ycore_error_destroy(cdr.error);
        }

        {
            struct yetty_ycore_void_result populate_r = yui_app_menu_populate_root(yui);
            if (YETTY_IS_ERR(populate_r)) {
                ywarn("yui_create: populate_root: %s", populate_r.error.msg);
                yetty_ycore_error_destroy(populate_r.error);
            }
        }

        yetty_ygui_framework_mark_dirty(yui->engine);
    } else {
        ywarn("yui_create: ygui framework create failed: %s", er.error.msg);
        yetty_ycore_error_destroy(er.error);
    }

    yui_active_lock();
    s_active_yui = yui;
    yui_active_unlock();
    ynotify_set_handler(yui_ynotify_handler, context->event_loop);

    ydebug("yui_create: surface=%ux%u cell=%.1fx%.1f", surface_w, surface_h, cell_w, cell_h);
    return YETTY_OK(yetty_yui_ptr, yui);
}

void yetty_yui_set_tabbar_model(struct yetty_yui *yui, struct yetty_yui_tabbar_model *tabbar)
{
    if (!yui) {
        return;
    }
    yui->tabbar_model = tabbar;
    yui->titlebar_synced_active = -1;
    yui->titlebar_synced_count = 0;
    if (yui->engine) {
        yetty_ygui_framework_mark_dirty(yui->engine);
    }
}

struct yetty_ycore_void_result yetty_yui_destroy(struct yetty_yui *yui)
{
    if (!yui) {
        return YETTY_OK_VOID();
    }
    ynotify_set_handler(NULL, NULL);
    yui_active_lock();
    if (s_active_yui == yui) {
        s_active_yui = NULL;
    }
    yui_active_unlock();

    if (yui->chrome) {
        struct yetty_ycore_void_result cr = yetty_ychrome_host_destroy(yui->chrome);
        if (YETTY_IS_ERR(cr)) {
            yetty_ycore_error_destroy(cr.error);
        }
        yui->chrome = NULL;
    }

    if (yui->engine) {
        struct yetty_ycore_void_result r = yetty_ygui_framework_destroy(yui->engine);
        if (!YETTY_IS_OK(r)) {
            ywarn("yui_destroy: engine destroy: %s", r.error.msg);
            yetty_ycore_error_destroy(r.error);
        }
        yui->engine = NULL;
    }
    yetty_yui_config_dialog_destroy(yui->config_dialog);
    yui->config_dialog = NULL;
    if (yui->root_container_obj) {
        struct yetty_yfigure_figure_ptr_result rf_res =
            yetty_yfigure_container_as_figure(yui->root_container_obj);
        if (YETTY_IS_OK(rf_res)) {
            struct yetty_yfigure_figure *rf = rf_res.value;
            struct yetty_ycore_void_result r =
                yetty_yfigure_destroy((struct yetty_yclass_object *)rf - 1);
            if (!YETTY_IS_OK(r)) {
                ywarn("yui_destroy: root_container destroy: %s", r.error.msg);
                yetty_ycore_error_destroy(r.error);
            }
        } else {
            ywarn("yui_destroy: root_container as_figure: %s", rf_res.error.msg);
            yetty_ycore_error_destroy(rf_res.error);
        }
        yui->root_container_obj = NULL;
    }
    if (yui->figure_registry) {
        yetty_ycore_error_destroy_safe(yetty_yfigure_registry_destroy(yui->figure_registry));
        yui->figure_registry = NULL;
    }
    if (yui->composite_factory) {
        yetty_ydraw_composite_factory_destroy(yui->composite_factory);
        yui->composite_factory = NULL;
    }
    if (yui->font) {
        yui->font->ops->destroy(yui->font);
        yui->font = NULL;
    }
    /* Splitter widgets were owned by the engine (destroyed above); free
     * the per-split thunks + the entry array. */
    for (size_t i = 0; i < yui->splitter_count; i++) {
        free(yui->splitters[i].thunk);
    }
    free(yui->splitters);
    for (size_t i = 0; i < yui->debug_window_count; i++) {
        free(yui->debug_windows[i].dw);
        yui->debug_windows[i].dw = NULL;
    }
    free(yui->debug_windows);
    free(yui);
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Per-split divider widgets
 *===========================================================================*/

struct yetty_yui_splitter_thunk {
    struct yetty_yui *yui;
    yetty_ycore_object_id workspace_id;
    yetty_ycore_object_id split_id;
};

static void yui_splitter_on_change(struct yetty_yclass_object *widget, float delta, void *userdata);

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

/* Best-effort teardown: deletes the widget and frees the thunk. The
 * widget delete runs even if it errors; the first error (if any) is
 * returned so a sweep can surface it. */
static struct yetty_ycore_void_result yui_splitter_entry_destroy_widget(
    struct yetty_yui_splitter_entry *e)
{
    if (!e) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result first_err = YETTY_OK_VOID();
    if (e->widget) {
        struct yetty_ycore_void_result del_r = yetty_ygui_widget_destroy(e->widget);
        if (YETTY_IS_ERR(del_r)) {
            first_err = del_r;
        }
        e->widget = NULL;
    }
    free(e->thunk);
    e->thunk = NULL;
    return first_err;
}

static struct yetty_ycore_void_result yui_splitter_remove_at(struct yetty_yui *yui, size_t idx)
{
    if (!yui || idx >= yui->splitter_count) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result destroy_r =
        yui_splitter_entry_destroy_widget(&yui->splitters[idx]);
    if (idx != yui->splitter_count - 1) {
        yui->splitters[idx] = yui->splitters[yui->splitter_count - 1];
    }
    yui->splitter_count--;
    YETTY_RETURN_IF_ERR(yetty_ycore_void, destroy_r, "splitter_remove_at: destroy widget");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result yui_splitter_walk_tree(struct yetty_yui *yui,
                                                             struct yetty_yui_tile *tile,
                                                             yetty_ycore_object_id workspace_id)
{
    if (!tile || yetty_yui_tile_type(tile) != YETTY_YUI_TILE_SPLIT) {
        return YETTY_OK_VOID();
    }

    yetty_ycore_object_id split_id = yetty_yui_tile_id(tile);
    struct yetty_yui_tile *first = yetty_yui_tile_split_first(tile);
    struct yetty_yui_tile *second = yetty_yui_tile_split_second(tile);
    enum yetty_yui_orientation orient = yetty_yui_tile_split_orientation(tile);
    struct yetty_yui_rect fb =
        first ? yetty_yui_tile_bounds(first) : (struct yetty_yui_rect){0, 0, 0, 0};
    struct yetty_yui_rect sb =
        second ? yetty_yui_tile_bounds(second) : (struct yetty_yui_rect){0, 0, 0, 0};

    struct yetty_yui_splitter_entry *e = yui_splitter_find_entry(yui, split_id);
    if (!e) {
        e = yui_splitter_alloc_entry(yui);
        if (!e) {
            return YETTY_OK_VOID();
        }
        e->split_id = split_id;
        e->workspace_id = workspace_id;
        e->yui = yui;

        struct yetty_yclass_object_ptr_result widget_r =
            yui_add(yui->root, yetty_ygui_splitter_class_get());
        YETTY_RETURN_IF_ERR(yetty_ycore_void, widget_r, "splitter_walk_tree: add splitter");
        e->widget = widget_r.value;
        if (e->widget) {
            yetty_ycore_error_destroy_safe(
                yetty_ygui_splitter_set_axis(e->widget, orient == YETTY_YUI_VERTICAL ? 1 : 0));
            yetty_ycore_error_destroy_safe(yetty_ygui_splitter_set_min(e->widget, 30.0f));

            struct yetty_yui_splitter_thunk *t = calloc(1, sizeof(*t));
            if (t) {
                t->yui = yui;
                t->workspace_id = workspace_id;
                t->split_id = split_id;
                e->thunk = t;
                yetty_ycore_error_destroy_safe(
                    yetty_ygui_splitter_on_change(e->widget, yui_splitter_on_change, t));
            }
        }
    } else if (e->widget) {
        e->workspace_id = workspace_id;
        yetty_ycore_error_destroy_safe(
            yetty_ygui_splitter_set_axis(e->widget, orient == YETTY_YUI_VERTICAL ? 1 : 0));
    }
    e->seen = 1;

    if (e->widget && first && second) {
        /* Tile bounds (sb/fb) are framebuffer pixels; the splitter is a
         * chrome widget laid out in logical pixels, so divide the derived
         * position/extent by content_scale. The thickness `t` is already a
         * logical constant and is left as-is. */
        const float scale = yui->content_scale > 0.0f ? yui->content_scale : 1.0f;
        const float t = YETTY_YUI_SPLITTER_THICKNESS;
        if (orient == YETTY_YUI_VERTICAL) {
            float x = sb.x / scale - t * 0.5f;
            float y = fb.y / scale;
            float h = fb.h / scale;
            yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_position(e->widget, x, y));
            yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_size(e->widget, t, h));
        } else {
            float x = fb.x / scale;
            float y = sb.y / scale - t * 0.5f;
            float w = fb.w / scale;
            yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_position(e->widget, x, y));
            yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_size(e->widget, w, t));
        }
    }

    struct yetty_ycore_void_result first_walk_r = yui_splitter_walk_tree(yui, first, workspace_id);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, first_walk_r, "splitter_walk_tree: first child");
    struct yetty_ycore_void_result second_walk_r =
        yui_splitter_walk_tree(yui, second, workspace_id);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, second_walk_r, "splitter_walk_tree: second child");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result yui_splitters_sync(struct yetty_yui *yui)
{
    if (!yui || !yui->engine || !yui->tabbar_model) {
        return YETTY_OK_VOID();
    }
    struct yetty_yui_workspace *ws = yetty_yui_tabbar_model_active_workspace(yui->tabbar_model);
    yetty_ycore_object_id ws_id = ws ? yetty_yui_workspace_id(ws) : 0;
    struct yetty_yui_tile *root = ws ? yetty_yui_workspace_root(ws) : NULL;

    for (size_t i = 0; i < yui->splitter_count; i++) {
        yui->splitters[i].seen = 0;
    }
    if (root && ws_id != 0) {
        struct yetty_ycore_void_result walk_r = yui_splitter_walk_tree(yui, root, ws_id);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, walk_r, "splitters_sync: walk");
    }
    for (size_t i = yui->splitter_count; i-- > 0;) {
        if (!yui->splitters[i].seen) {
            struct yetty_ycore_void_result remove_r = yui_splitter_remove_at(yui, i);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, remove_r, "splitters_sync: remove");
        }
    }
    return YETTY_OK_VOID();
}

static void yui_splitter_on_change(struct yetty_yclass_object *widget, float delta, void *userdata)
{
    (void)widget;
    struct yetty_yui_splitter_thunk *t = userdata;
    if (!t || !t->yui || !t->yui->ctx) {
        return;
    }
    struct yetty_yui_tabbar_model *bar = t->yui->tabbar_model;
    struct yetty_yui_workspace *ws =
        bar ? yetty_yui_tabbar_model_find_workspace(bar, t->workspace_id) : NULL;
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
    /* `span` is framebuffer pixels (tile bounds); `delta` is the logical
     * drag distance ygui reports. Lift the delta to framebuffer pixels so
     * the new split ratio tracks the pointer 1:1 on HiDPI. */
    const float scale = t->yui->content_scale > 0.0f ? t->yui->content_scale : 1.0f;
    float old_ratio = yetty_yui_tile_split_ratio(split);
    float new_first = old_ratio * span + delta * scale;
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
    yetty_yevent_post_async(t->yui->ctx->runtime->platform_input_pipe, &ev);
}

/*===========================================================================
 * Per-pane debug window reconciliation
 *===========================================================================*/

static struct yetty_yui_debug_window_entry *yui_debug_window_find_entry(
    struct yetty_yui *yui, yetty_ycore_object_id pane_id)
{
    if (!yui) {
        return NULL;
    }
    for (size_t i = 0; i < yui->debug_window_count; i++) {
        if (yui->debug_windows[i].pane_id == pane_id) {
            return &yui->debug_windows[i];
        }
    }
    return NULL;
}

static struct yetty_yui_debug_window_entry *yui_debug_window_alloc_entry(struct yetty_yui *yui)
{
    if (yui->debug_window_count == yui->debug_window_cap) {
        size_t new_cap = yui->debug_window_cap ? yui->debug_window_cap * 2 : 4;
        struct yetty_yui_debug_window_entry *arr =
            realloc(yui->debug_windows, new_cap * sizeof(*yui->debug_windows));
        if (!arr) {
            return NULL;
        }
        yui->debug_windows = arr;
        yui->debug_window_cap = new_cap;
    }
    struct yetty_yui_debug_window_entry *e = &yui->debug_windows[yui->debug_window_count++];
    memset(e, 0, sizeof(*e));
    return e;
}

/* Best-effort teardown: destroys the debug window. Returns the first
 * error so a sweep can surface it. */
static struct yetty_ycore_void_result yui_debug_window_entry_destroy_widget(
    struct yetty_yui_debug_window_entry *e)
{
    if (!e) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result first_err = YETTY_OK_VOID();
    if (e->dw) {
        struct yetty_ycore_void_result destroy_r = yetty_yui_debug_window_destroy(e->dw);
        if (YETTY_IS_ERR(destroy_r)) {
            first_err = destroy_r;
        }
        e->dw = NULL;
    }
    return first_err;
}

static struct yetty_ycore_void_result yui_debug_window_remove_at(struct yetty_yui *yui, size_t idx)
{
    if (!yui || idx >= yui->debug_window_count) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result destroy_r =
        yui_debug_window_entry_destroy_widget(&yui->debug_windows[idx]);
    if (idx != yui->debug_window_count - 1) {
        yui->debug_windows[idx] = yui->debug_windows[yui->debug_window_count - 1];
    }
    yui->debug_window_count--;
    YETTY_RETURN_IF_ERR(yetty_ycore_void, destroy_r, "debug_window_remove_at: destroy widget");
    return YETTY_OK_VOID();
}

/* Close-button callback wired into every debug window: clear the owning
 * pane's debug_open flag so the next reconcile pass sweeps the window. */
static struct yetty_ycore_void_result yui_debug_window_on_close(void *userdata,
                                                                yetty_ycore_object_id pane_id)
{
    struct yetty_yui *yui = userdata;
    if (!yui || !yui->tabbar_model) {
        return YETTY_OK_VOID();
    }
    struct yetty_yui_workspace *ws = yetty_yui_tabbar_model_active_workspace(yui->tabbar_model);
    struct yetty_yui_tile *root = ws ? yetty_yui_workspace_root(ws) : NULL;
    struct yetty_yui_tile *pane = root ? yetty_yui_tile_find_by_id(root, pane_id) : NULL;
    if (pane) {
        yetty_yui_tile_pane_set_debug_open(pane, 0);
    }
    if (yui->engine) {
        yetty_ygui_framework_mark_dirty(yui->engine);
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result yui_debug_window_walk_tree(struct yetty_yui *yui,
                                                                 struct yetty_yui_tile *tile,
                                                                 yetty_ycore_object_id workspace_id)
{
    if (!tile) {
        return YETTY_OK_VOID();
    }

    if (yetty_yui_tile_type(tile) == YETTY_YUI_TILE_SPLIT) {
        struct yetty_ycore_void_result first_r =
            yui_debug_window_walk_tree(yui, yetty_yui_tile_split_first(tile), workspace_id);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, first_r, "debug_window_walk_tree: first child");
        struct yetty_ycore_void_result second_r =
            yui_debug_window_walk_tree(yui, yetty_yui_tile_split_second(tile), workspace_id);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, second_r, "debug_window_walk_tree: second child");
        return YETTY_OK_VOID();
    }

    yetty_ycore_object_id pane_id = yetty_yui_tile_id(tile);
    struct yetty_yui_rect b = yetty_yui_tile_bounds(tile);

    /* Closed by default — a pane only carries a debug window while its
     * debug_open flag is set (context-menu toggle / close button). An
     * existing window stays unseen here and the sweep destroys it. */
    if (!yetty_yui_tile_pane_debug_open(tile)) {
        return YETTY_OK_VOID();
    }

    struct yetty_yui_debug_window_entry *e = yui_debug_window_find_entry(yui, pane_id);
    if (!e) {
        e = yui_debug_window_alloc_entry(yui);
        if (!e) {
            return YETTY_OK_VOID();
        }
        e->pane_id = pane_id;
        e->workspace_id = workspace_id;
        struct yetty_yui_debug_window_ptr_result dr =
            yetty_yui_debug_window_create(yui->engine, pane_id, yui_debug_window_on_close, yui);
        if (YETTY_IS_ERR(dr)) {
            ywarn("yui: debug_window_create pane=%llu failed: %s", (unsigned long long)pane_id,
                  dr.error.msg);
            yetty_ycore_error_destroy(dr.error);
            yui->debug_window_count--;
            return YETTY_OK_VOID();
        }
        e->dw = dr.value;
    } else {
        e->workspace_id = workspace_id;
    }
    e->seen = 1;

    if (e->dw) {
        yetty_ycore_error_destroy_safe(yetty_yui_debug_window_layout(e->dw, b.x, b.y, b.w, b.h));

        struct yetty_yui_view *view = yetty_yui_tile_pane_active_view(tile);
        struct yetty_yterminal_terminal *term = yetty_yterminal_terminal_from_view(view);
        struct yetty_ywire_wire_statemachine *sm =
            term ? yetty_yterminal_terminal_wire_sm(term) : NULL;
        if (sm) {
            struct yetty_ywire_stats_snapshot s = yetty_ywire_wire_statemachine_stats_snapshot(sm);
            yetty_ycore_error_destroy_safe(yetty_yui_debug_window_set_stats(e->dw, &s));
        } else {
            yetty_ycore_error_destroy_safe(yetty_yui_debug_window_set_stats(e->dw, NULL));
        }
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result yui_debug_windows_sync(struct yetty_yui *yui)
{
    if (!yui || !yui->engine || !yui->tabbar_model) {
        return YETTY_OK_VOID();
    }
    struct yetty_yui_workspace *ws = yetty_yui_tabbar_model_active_workspace(yui->tabbar_model);
    yetty_ycore_object_id ws_id = ws ? yetty_yui_workspace_id(ws) : 0;
    struct yetty_yui_tile *root = ws ? yetty_yui_workspace_root(ws) : NULL;

    for (size_t i = 0; i < yui->debug_window_count; i++) {
        yui->debug_windows[i].seen = 0;
    }
    if (root && ws_id != 0) {
        struct yetty_ycore_void_result walk_r = yui_debug_window_walk_tree(yui, root, ws_id);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, walk_r, "debug_windows_sync: walk");
    }
    for (size_t i = yui->debug_window_count; i-- > 0;) {
        if (!yui->debug_windows[i].seen) {
            struct yetty_ycore_void_result remove_r = yui_debug_window_remove_at(yui, i);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, remove_r, "debug_windows_sync: remove");
        }
    }
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Render
 *===========================================================================*/

struct yetty_ycore_void_result yetty_yui_render(struct yetty_yui *yui,
                                                struct yetty_ydraw_target *target)
{
    if (!yui || !yui->root_container_obj || !target) {
        return YETTY_OK_VOID();
    }

    struct yetty_ycore_void_result titlebar_sync_r = yui_titlebar_sync(yui);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, titlebar_sync_r, "yui_render: titlebar_sync");
    struct yetty_ycore_void_result splitters_sync_r = yui_splitters_sync(yui);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, splitters_sync_r, "yui_render: splitters_sync");
    struct yetty_ycore_void_result debug_sync_r = yui_debug_windows_sync(yui);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, debug_sync_r, "yui_render: debug_windows_sync");

    /* When dirty, run layout + emit into yui's container via the
     * in-process yclass slot path (framework_set_container_obj). */
    if (yui->engine && yetty_ygui_framework_is_dirty(yui->engine)) {
        struct yetty_ycore_void_result er = yetty_ygui_framework_emit(yui->engine);
        if (YETTY_IS_ERR(er)) {
            int depth = 0;
            for (struct yetty_ycore_error *e = &er.error; e; e = e->cause, depth++) {
                yerror("yui_render emit chain[%d]: %s  (%s:%d %s)", depth, e->msg,
                       e->file ? e->file : "?", e->line, e->func ? e->func : "?");
            }
            return YETTY_ERR(yetty_ycore_void, "yui_render: framework_emit", er);
        }
    }

    {
        struct yetty_yfigure_figure_ptr_result rf_res =
            yetty_yfigure_container_as_figure(yui->root_container_obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rf_res, "yui root_container as_figure");
        struct yetty_yfigure_figure *rf = rf_res.value;
        struct yetty_ycore_void_result rr =
            yetty_yfigure_render((struct yetty_yclass_object *)rf - 1, target);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "yui root_container render");
        {
            struct yetty_ycore_void_result drop_r =
                yetty_yfigure_figure_dirty_set((struct yetty_yclass_object *)(rf)-1, 0);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, drop_r, "drop: yetty_yfigure_figure_dirty_set");
        }
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_int_result yetty_yui_is_dirty(const struct yetty_yui *yui)
{
    if (!yui) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    struct yetty_yui *mut = (struct yetty_yui *)yui;
    struct yetty_ycore_void_result titlebar_sync_r = yui_titlebar_sync(mut);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, titlebar_sync_r, "is_dirty: titlebar_sync");
    struct yetty_ycore_void_result splitters_sync_r = yui_splitters_sync(mut);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, splitters_sync_r, "is_dirty: splitters_sync");
    struct yetty_ycore_void_result debug_sync_r = yui_debug_windows_sync(mut);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, debug_sync_r, "is_dirty: debug_windows_sync");
    return YETTY_OK(yetty_ycore_int, mut->engine ? yetty_ygui_framework_is_dirty(mut->engine) : 0);
}

/*===========================================================================
 * Menu / dialog callback implementations
 *===========================================================================*/

static struct yetty_ycore_void_result yui_menu_open_dialog(struct yetty_yclass_object *menu,
                                                           int item_index, void *userdata)
{
    (void)menu;
    (void)item_index;
    struct yui_cb_ctx *cb = userdata;
    if (!cb || !cb->yui || (int)cb->kind < 0 || (int)cb->kind >= YETTY_YUI_VIEW_KIND_COUNT) {
        return YETTY_OK_VOID();
    }
    struct yetty_yclass_object *dlg = cb->yui->dialogs[(int)cb->kind];
    if (!dlg) {
        return YETTY_OK_VOID();
    }
    yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_visible(dlg, 1));
    if (cb->yui->engine) {
        yetty_ygui_framework_mark_dirty(cb->yui->engine);
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result yui_menu_spawn_shell(struct yetty_yclass_object *menu,
                                                           int item_index, void *userdata)
{
    (void)menu;
    (void)item_index;
    struct yui_cb_ctx *cb = userdata;
    if (!cb || !cb->yui) {
        return YETTY_OK_VOID();
    }
    if (cb->yui->engine) {
        yetty_ygui_framework_mark_dirty(cb->yui->engine);
    }
    if (cb->yui->connect_cb) {
        cb->yui->connect_cb(cb->yui->connect_userdata, YETTY_YUI_VIEW_SHELL);
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result yui_dialog_cancel(struct yetty_yclass_object *button,
                                                        void *userdata)
{
    (void)button;
    struct yui_cb_ctx *cb = userdata;
    if (!cb || !cb->yui || (int)cb->kind < 0 || (int)cb->kind >= YETTY_YUI_VIEW_KIND_COUNT) {
        return YETTY_OK_VOID();
    }
    struct yetty_yclass_object *dlg = cb->yui->dialogs[(int)cb->kind];
    if (dlg) {
        yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_visible(dlg, 0));
    }
    if (cb->yui->engine) {
        yetty_ygui_framework_mark_dirty(cb->yui->engine);
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result yui_app_menu_populate_root(struct yetty_yui *yui)
{
    if (!yui || !yui->app_menu) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result clear_r = yetty_ygui_popup_menu_clear(yui->app_menu);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, clear_r, "populate_root: clear");
    struct yetty_ycore_void_result title_r = yetty_ygui_popup_menu_set_title(yui->app_menu, "Menu");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, title_r, "populate_root: set_title");
    struct yetty_ycore_void_result new_view_r = yetty_ygui_popup_menu_add_drill_item(
        yui->app_menu, "New view  ▸", yui_app_menu_open_new_view, yui);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, new_view_r, "populate_root: add new view");
    struct yetty_ycore_void_result sep_r = yetty_ygui_popup_menu_add_separator(yui->app_menu);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sep_r, "populate_root: separator");
    struct yetty_ycore_void_result gpu_r =
        yetty_ygui_popup_menu_add_item(yui->app_menu, "GPU info…", yui_app_menu_open_gpu_info, yui);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, gpu_r, "populate_root: add gpu info");
    struct yetty_ycore_void_result settings_r =
        yetty_ygui_popup_menu_add_item(yui->app_menu, "Settings…", yui_app_menu_open_settings, yui);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, settings_r, "populate_root: add settings");
    yui->app_menu_level = 0;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result yui_app_menu_populate_new_view(struct yetty_yui *yui)
{
    if (!yui || !yui->app_menu) {
        return YETTY_OK_VOID();
    }
    static const char *const LABELS[YETTY_YUI_VIEW_KIND_COUNT] = {
        [YETTY_YUI_VIEW_SHELL] = "Shell", [YETTY_YUI_VIEW_EXEC] = "Exec…",
        [YETTY_YUI_VIEW_SSH] = "SSH…",    [YETTY_YUI_VIEW_TELNET] = "Telnet…",
        [YETTY_YUI_VIEW_YVNC] = "yVNC…",
    };
    static const int MENU_ORDER[YETTY_YUI_VIEW_KIND_COUNT] = {
        YETTY_YUI_VIEW_SHELL,  YETTY_YUI_VIEW_EXEC, YETTY_YUI_VIEW_SSH,
        YETTY_YUI_VIEW_TELNET, YETTY_YUI_VIEW_YVNC,
    };
    struct yetty_ycore_void_result clear_r = yetty_ygui_popup_menu_clear(yui->app_menu);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, clear_r, "populate_new_view: clear");
    struct yetty_ycore_void_result title_r =
        yetty_ygui_popup_menu_set_title(yui->app_menu, "Menu  ›  New view");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, title_r, "populate_new_view: set_title");
    struct yetty_ycore_void_result back_r =
        yetty_ygui_popup_menu_set_back(yui->app_menu, "‹ Back", yui_app_menu_back_to_root, yui);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, back_r, "populate_new_view: set_back");
    for (int i = 0; i < YETTY_YUI_VIEW_KIND_COUNT; i++) {
        int k = MENU_ORDER[i];
        yetty_ygui_menu_item_cb cb =
            (k == YETTY_YUI_VIEW_SHELL) ? yui_menu_spawn_shell : yui_menu_open_dialog;
        struct yetty_ycore_void_result item_r =
            yetty_ygui_popup_menu_add_item(yui->app_menu, LABELS[k], cb, &s_cb_ctx[k]);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, item_r, "populate_new_view: add item");
    }
    yui->app_menu_level = 1;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result yui_app_menu_open_new_view(struct yetty_yclass_object *menu,
                                                                 int item_index, void *userdata)
{
    (void)menu;
    (void)item_index;
    struct yetty_ycore_void_result populate_r =
        yui_app_menu_populate_new_view((struct yetty_yui *)userdata);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, populate_r, "open_new_view: populate");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result yui_app_menu_back_to_root(struct yetty_yclass_object *menu,
                                                                int item_index, void *userdata)
{
    (void)menu;
    (void)item_index;
    struct yetty_ycore_void_result populate_r =
        yui_app_menu_populate_root((struct yetty_yui *)userdata);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, populate_r, "back_to_root: populate");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result yui_app_menu_populate_context_root(struct yetty_yui *yui)
{
    if (!yui || !yui->app_menu) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result clear_r = yetty_ygui_popup_menu_clear(yui->app_menu);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, clear_r, "populate_context_root: clear");
    struct yetty_ycore_void_result title_r = yetty_ygui_popup_menu_set_title(yui->app_menu, "Pane");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, title_r, "populate_context_root: set_title");
    struct yetty_ycore_void_result gpu_r =
        yetty_ygui_popup_menu_add_item(yui->app_menu, "GPU info…", yui_app_menu_open_gpu_info, yui);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, gpu_r, "populate_context_root: add gpu info");
    struct yetty_ycore_void_result settings_r =
        yetty_ygui_popup_menu_add_item(yui->app_menu, "Settings…", yui_app_menu_open_settings, yui);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, settings_r, "populate_context_root: add settings");
    /* Debug-window toggle for the focused pane (the right-clicked pane
     * is focused before this menu opens). */
    {
        struct yetty_yui_workspace *ws =
            yui->tabbar_model ? yetty_yui_tabbar_model_active_workspace(yui->tabbar_model) : NULL;
        struct yetty_yui_tile *root = ws ? yetty_yui_workspace_root(ws) : NULL;
        struct yetty_yui_tile *pane = root ? yetty_yui_tile_find_focused_pane(root) : NULL;
        const char *debug_label = (pane && yetty_yui_tile_pane_debug_open(pane))
                                      ? "Hide debug window"
                                      : "Show debug window";
        struct yetty_ycore_void_result debug_r = yetty_ygui_popup_menu_add_item(
            yui->app_menu, debug_label, yui_context_toggle_debug_window, yui);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, debug_r, "populate_context_root: add debug window");
    }
    struct yetty_ycore_void_result sep_r = yetty_ygui_popup_menu_add_separator(yui->app_menu);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sep_r, "populate_context_root: separator");
    struct yetty_ycore_void_result split_v_r = yetty_ygui_popup_menu_add_drill_item(
        yui->app_menu, "Split vertically  ▸", yui_context_open_split_vertical, yui);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, split_v_r, "populate_context_root: add split vertical");
    struct yetty_ycore_void_result split_h_r = yetty_ygui_popup_menu_add_drill_item(
        yui->app_menu, "Split horizontally  ▸", yui_context_open_split_horizontal, yui);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, split_h_r, "populate_context_root: add split horizontal");
    yui->app_menu_level = 2;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result yui_app_menu_populate_split_kind(struct yetty_yui *yui,
                                                                       int horizontal)
{
    if (!yui || !yui->app_menu) {
        return YETTY_OK_VOID();
    }
    static const char *const LABELS[YETTY_YUI_VIEW_KIND_COUNT] = {
        [YETTY_YUI_VIEW_SHELL] = "Shell", [YETTY_YUI_VIEW_EXEC] = "Exec…",
        [YETTY_YUI_VIEW_SSH] = "SSH…",    [YETTY_YUI_VIEW_TELNET] = "Telnet…",
        [YETTY_YUI_VIEW_YVNC] = "yVNC…",
    };
    static const int MENU_ORDER[YETTY_YUI_VIEW_KIND_COUNT] = {
        YETTY_YUI_VIEW_SHELL,  YETTY_YUI_VIEW_EXEC, YETTY_YUI_VIEW_SSH,
        YETTY_YUI_VIEW_TELNET, YETTY_YUI_VIEW_YVNC,
    };
    struct yetty_ycore_void_result clear_r = yetty_ygui_popup_menu_clear(yui->app_menu);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, clear_r, "populate_split_kind: clear");
    struct yetty_ycore_void_result title_r = yetty_ygui_popup_menu_set_title(
        yui->app_menu, horizontal ? "Pane  ›  Split horizontally" : "Pane  ›  Split vertically");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, title_r, "populate_split_kind: set_title");
    struct yetty_ycore_void_result back_r =
        yetty_ygui_popup_menu_set_back(yui->app_menu, "‹ Back", yui_split_back_to_context, yui);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, back_r, "populate_split_kind: set_back");
    int o = horizontal ? 1 : 0;
    for (int i = 0; i < YETTY_YUI_VIEW_KIND_COUNT; i++) {
        int k = MENU_ORDER[i];
        struct yetty_ycore_void_result item_r = yetty_ygui_popup_menu_add_item(
            yui->app_menu, LABELS[k], yui_split_kind_action, &s_split_ctx[o][k]);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, item_r, "populate_split_kind: add item");
    }
    yui->app_menu_level = horizontal ? 4 : 3;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result yui_context_open_split_vertical(
    struct yetty_yclass_object *menu, int item_index, void *userdata)
{
    (void)menu;
    (void)item_index;
    struct yetty_ycore_void_result populate_r =
        yui_app_menu_populate_split_kind((struct yetty_yui *)userdata, /*horizontal=*/0);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, populate_r, "open_split_vertical: populate");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result yui_context_open_split_horizontal(
    struct yetty_yclass_object *menu, int item_index, void *userdata)
{
    (void)menu;
    (void)item_index;
    struct yetty_ycore_void_result populate_r =
        yui_app_menu_populate_split_kind((struct yetty_yui *)userdata, /*horizontal=*/1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, populate_r, "open_split_horizontal: populate");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result yui_split_back_to_context(struct yetty_yclass_object *menu,
                                                                int item_index, void *userdata)
{
    (void)menu;
    (void)item_index;
    struct yetty_ycore_void_result populate_r =
        yui_app_menu_populate_context_root((struct yetty_yui *)userdata);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, populate_r, "split_back_to_context: populate");
    return YETTY_OK_VOID();
}

/* Context-menu action: toggle the focused pane's debug window. The flag
 * lives on the pane; the per-frame reconcile creates/destroys the actual
 * widget on the next sync. */
static struct yetty_ycore_void_result yui_context_toggle_debug_window(
    struct yetty_yclass_object *menu, int item_index, void *userdata)
{
    (void)menu;
    (void)item_index;
    struct yetty_yui *yui = userdata;
    if (!yui || !yui->tabbar_model) {
        return YETTY_OK_VOID();
    }
    struct yetty_yui_workspace *ws = yetty_yui_tabbar_model_active_workspace(yui->tabbar_model);
    struct yetty_yui_tile *root = ws ? yetty_yui_workspace_root(ws) : NULL;
    struct yetty_yui_tile *pane = root ? yetty_yui_tile_find_focused_pane(root) : NULL;
    if (pane) {
        yetty_yui_tile_pane_set_debug_open(pane, !yetty_yui_tile_pane_debug_open(pane));
    }
    if (yui->engine) {
        yetty_ygui_framework_mark_dirty(yui->engine);
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result yui_split_kind_action(struct yetty_yclass_object *menu,
                                                            int item_index, void *userdata)
{
    (void)menu;
    (void)item_index;
    struct yui_split_ctx *sctx = userdata;
    if (!sctx || !sctx->yui) {
        return YETTY_OK_VOID();
    }
    if (sctx->yui->split_cb) {
        sctx->yui->split_cb(sctx->yui->split_userdata, sctx->kind, sctx->horizontal);
    }
    if (sctx->yui->engine) {
        yetty_ygui_framework_mark_dirty(sctx->yui->engine);
    }
    return YETTY_OK_VOID();
}

/*===========================================================================
 * GPU info dialog
 *===========================================================================*/

static char *yui_gpu_info_build_text(const struct yetty_yui *yui)
{
    char *adapter_desc = NULL;
    if (yui->gpu_info_adapter) {
        /* The yui GPU-info panel doesn't keep a handle to the active
         * surface; only the adapter block is shown here. */
        adapter_desc = yetty_ywebgpu_get_webgpu_description(yui->gpu_info_adapter, NULL);
    }
    const char *adapter_block = adapter_desc ? adapter_desc : "(WebGPU adapter unavailable)\n";

    struct yetty_yrender_gpu_allocator_stats st = {0};
    int have_stats = 0;
    if (yui->gpu_info_allocator && yui->gpu_info_allocator->ops &&
        yui->gpu_info_allocator->ops->get_stats) {
        yui->gpu_info_allocator->ops->get_stats(yui->gpu_info_allocator, &st);
        have_stats = 1;
    }

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
                 st.live_allocations, st.capacity, st.buffer_count,
                 (unsigned long long)st.buffer_bytes, st.texture_count,
                 (unsigned long long)st.texture_bytes, (unsigned long long)st.total_bytes,
                 st.peak_allocations, (unsigned long long)st.peak_total_bytes);
    } else {
        snprintf(alloc_block, sizeof(alloc_block), "\nGPU allocator stats: (unavailable)\n");
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

/* Pixel width of a UTF-8 byte range at `fs`, via yui's MSDF font. 0 when
 * the font can't measure. */
static float yui_text_width(struct yetty_yui *yui, const char *s, size_t n, float fs)
{
    if (!yui->font || !yui->font->ops || !yui->font->ops->measure_text || n == 0) {
        return 0.0f;
    }
    struct float_result r = yui->font->ops->measure_text(yui->font, s, n, fs);
    return YETTY_IS_OK(r) ? r.value : 0.0f;
}

/* Word-wrap `text` to `max_w` pixels using the font for measurement
 * (the textarea widget can't measure on the producer side). Explicit
 * newlines are preserved; over-long lines break at spaces. Returns a
 * malloc'd string the caller frees, or NULL on OOM. */
static char *yui_wrap_text(struct yetty_yui *yui, const char *text, float max_w, float fs)
{
    if (!text) {
        return NULL;
    }
    int can_measure = max_w > 1.0f && yui->font && yui->font->ops && yui->font->ops->measure_text;
    size_t cap = strlen(text) + 64;
    size_t len = 0;
    char *out = malloc(cap);
    if (!out) {
        return NULL;
    }
#define WRAP_PUT(ch)                                                                               \
    do {                                                                                           \
        if (len + 1 >= cap) {                                                                      \
            size_t nc = cap * 2;                                                                   \
            char *nb = realloc(out, nc);                                                           \
            if (!nb) {                                                                             \
                free(out);                                                                         \
                return NULL;                                                                       \
            }                                                                                      \
            out = nb;                                                                              \
            cap = nc;                                                                              \
        }                                                                                          \
        out[len++] = (char)(ch);                                                                   \
    } while (0)

    const char *p = text;
    for (;;) {
        const char *nl = strchr(p, '\n');
        const char *line_end = nl ? nl : p + strlen(p);
        float line_w = 0.0f;
        const char *q = p;
        while (q < line_end) {
            const char *tok = q; /* leading spaces + one word */
            while (q < line_end && (*q == ' ' || *q == '\t')) {
                q++;
            }
            const char *wstart = q;
            while (q < line_end && *q != ' ' && *q != '\t') {
                q++;
            }
            float tok_w = yui_text_width(yui, tok, (size_t)(q - tok), fs);
            float word_w = yui_text_width(yui, wstart, (size_t)(q - wstart), fs);
            if (can_measure && line_w > 0.0f && (q - wstart) > 0 && line_w + tok_w > max_w) {
                /* Break before the word; drop the leading spaces at the
                 * new line's head. */
                WRAP_PUT('\n');
                line_w = 0.0f;
                for (const char *c = wstart; c < q; c++) {
                    WRAP_PUT(*c);
                }
                line_w += word_w;
            } else {
                for (const char *c = tok; c < q; c++) {
                    WRAP_PUT(*c);
                }
                line_w += tok_w;
            }
        }
        if (!nl) {
            break;
        }
        WRAP_PUT('\n');
        p = nl + 1;
    }
    WRAP_PUT('\0');
#undef WRAP_PUT
    return out;
}

static struct yetty_ycore_void_result yui_gpu_info_load_into_textarea(struct yetty_yui *yui)
{
    if (!yui || !yui->gpu_info_textarea) {
        return YETTY_OK_VOID();
    }
    char *text = yui_gpu_info_build_text(yui);
    /* Word-wrap to the textarea's pixel width using the font, so long
     * lines (the GPU-limits dump) don't run past the right edge. The
     * textarea's resolved width is known once it has been laid out
     * (Refresh on an open dialog); fall back to a width derived from the
     * fixed dialog geometry on the first open. */
    float fs = 13.0f;
    struct yetty_ycore_rectangle_result tr_res = yetty_ygui_widget_rect(yui->gpu_info_textarea);
    if (YETTY_IS_ERR(tr_res)) {
        free(text);
        return YETTY_ERR(yetty_ycore_void, "gpu_info_load: textarea widget_rect", tr_res);
    }
    struct yetty_ycore_rectangle tr = tr_res.value;
    float avail = tr.max.x - tr.min.x;
    float max_w = avail > 16.0f ? avail - 16.0f : 500.0f;
    char *wrapped = text ? yui_wrap_text(yui, text, max_w, fs) : NULL;
    struct yetty_ycore_void_result set_text_r = yetty_ygui_textarea_set_text(
        yui->gpu_info_textarea, wrapped ? wrapped : (text ? text : "(allocation failed)"));
    free(wrapped);
    free(text);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, set_text_r, "gpu_info_load: textarea_set_text");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result yui_app_menu_open_gpu_info(struct yetty_yclass_object *menu,
                                                                 int item_index, void *userdata)
{
    (void)menu;
    (void)item_index;
    struct yetty_yui *yui = userdata;
    if (!yui || !yui->gpu_info_dialog) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result load_r = yui_gpu_info_load_into_textarea(yui);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, load_r, "open_gpu_info: load textarea");
    yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_visible(yui->gpu_info_dialog, 1));
    if (yui->engine) {
        yetty_ygui_framework_mark_dirty(yui->engine);
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result yui_gpu_info_refresh(struct yetty_yclass_object *button,
                                                           void *userdata)
{
    (void)button;
    struct yetty_yui *yui = userdata;
    if (!yui) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result load_r = yui_gpu_info_load_into_textarea(yui);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, load_r, "gpu_info_refresh: load textarea");
    if (yui->engine) {
        yetty_ygui_framework_mark_dirty(yui->engine);
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result yui_gpu_info_close(struct yetty_yclass_object *button,
                                                         void *userdata)
{
    (void)button;
    struct yetty_yui *yui = userdata;
    if (!yui || !yui->gpu_info_dialog) {
        return YETTY_OK_VOID();
    }
    yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_visible(yui->gpu_info_dialog, 0));
    if (yui->engine) {
        yetty_ygui_framework_mark_dirty(yui->engine);
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result yui_app_menu_open_settings(struct yetty_yclass_object *menu,
                                                                 int item_index, void *userdata)
{
    (void)menu;
    (void)item_index;
    struct yetty_yui *yui = userdata;
    if (!yui) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result show_r = yetty_yui_config_dialog_show(yui->config_dialog);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, show_r, "open_settings: config_dialog_show");
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Titlebar (ygui-driven)
 *===========================================================================*/

static struct yetty_yclass_object_ptr_result yui_titlebar_button(struct yetty_yui *yui,
                                                                 const char *glyph,
                                                                 yetty_ygui_click_cb cb)
{
    struct yetty_yclass_object_ptr_result button_r =
        yui_add(yui->titlebar, yetty_ygui_button_class_get());
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, button_r, "titlebar_button: add");
    struct yetty_yclass_object *b = button_r.value;
    if (!b) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    yetty_ycore_error_destroy_safe(yetty_ygui_button_set_label(b, glyph));
    yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_size(b, TITLEBAR_BTN_W, TITLEBAR_STRIP_H));
    yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_bg_color(b, TITLEBAR_STRIP_BG));
    yetty_ycore_error_destroy_safe(yetty_ygui_clickable_on_click_set(b, cb, yui));
    return button_r;
}

/* Like yui_titlebar_button but draws an SDF window-control icon (1=minimize,
 * 2=maximize, 3=close) instead of a font glyph, so yetty's controls match the
 * ychrome SDF controls the tools use. */
static struct yetty_yclass_object_ptr_result yui_titlebar_icon_button(struct yetty_yui *yui,
                                                                      int kind,
                                                                      yetty_ygui_click_cb cb)
{
    struct yetty_yclass_object_ptr_result button_r =
        yui_add(yui->titlebar, yetty_ygui_button_class_get());
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, button_r, "titlebar_icon_button: add");
    struct yetty_yclass_object *b = button_r.value;
    if (!b) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    yetty_ycore_error_destroy_safe(yetty_ygui_button_set_chrome_icon(b, kind));
    yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_size(b, TITLEBAR_BTN_W, TITLEBAR_STRIP_H));
    yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_bg_color(b, TITLEBAR_STRIP_BG));
    yetty_ycore_error_destroy_safe(yetty_ygui_clickable_on_click_set(b, cb, yui));
    return button_r;
}

static struct yetty_ycore_void_result yui_titlebar_build(struct yetty_yui *yui)
{
    if (!yui || !yui->engine || !yui->root) {
        return YETTY_OK_VOID();
    }

    struct yetty_yclass_object_ptr_result tb_r = yui_add(yui->root, yetty_ygui_hbox_class_get());
    YETTY_RETURN_IF_ERR(yetty_ycore_void, tb_r, "titlebar_build: hbox");
    struct yetty_yclass_object *tb = tb_r.value;
    if (!tb) {
        return YETTY_OK_VOID();
    }
    yui->titlebar = tb;
    {
        struct yetty_ygui_layout_const_ptr_result tb_layout_res = yetty_ygui_widget_layout_get(tb);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, tb_layout_res, "titlebar_build: hbox layout_get");
        struct yetty_ygui_layout l = *tb_layout_res.value;
        l.direction = YETTY_YGUI_FLEX_ROW;
        l.align = YETTY_YGUI_ALIGN_CENTER;
        l.gap = 0.0f;
        l.height = TITLEBAR_STRIP_H;
        yetty_ycore_error_destroy_safe(yetty_ygui_widget_layout_set(tb, &l));
    }

    struct yetty_yclass_object_ptr_result hamburger_r =
        yui_titlebar_button(yui, "\xE2\x89\xA1" /* ≡ */, yui_titlebar_on_hamburger);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, hamburger_r, "titlebar_build: hamburger");
    yui->titlebar_hamburger = hamburger_r.value;

    /* Native TABBAR widget — absorbs the space between the hamburger and
     * the window-control buttons. */
    struct yetty_yclass_object_ptr_result tabbar_r = yui_add(tb, yetty_ygui_tabbar_class_get());
    YETTY_RETURN_IF_ERR(yetty_ycore_void, tabbar_r, "titlebar_build: tabbar");
    yui->titlebar_tabbar = tabbar_r.value;
    if (yui->titlebar_tabbar) {
        struct yetty_ygui_layout_const_ptr_result tabbar_layout_res =
            yetty_ygui_widget_layout_get(yui->titlebar_tabbar);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, tabbar_layout_res,
                            "titlebar_build: tabbar layout_get");
        struct yetty_ygui_layout l = *tabbar_layout_res.value;
        l.flex_grow = 1.0f;
        l.height = TITLEBAR_STRIP_H;
        /* Leave room on the right for the window controls the chrome overlays on
         * this strip — the client's responsibility, so no tab sits under a
         * control (3 × 46 px). */
        l.padding_right = 3.0f * 46.0f;
        yetty_ycore_error_destroy_safe(yetty_ygui_widget_layout_set(yui->titlebar_tabbar, &l));
        yetty_ycore_error_destroy_safe(yetty_ygui_widget_subscribe(
            yui->titlebar_tabbar, YETTY_YGUI_EVENT_VALUE_CHANGED, yui_titlebar_on_tab_change, yui));
        yetty_ycore_error_destroy_safe(
            yetty_ygui_tabbar_set_on_close(yui->titlebar_tabbar, yui_titlebar_on_tab_close, yui));
        /* "+" new-tab affordance is a built-in feature of the tabbar; it
         * paints right of the rightmost tab and routes clicks here. */
        yetty_ycore_error_destroy_safe(
            yetty_ygui_tabbar_set_on_new_tab(yui->titlebar_tabbar, yui_titlebar_on_new_tab, yui));
    }

    /* No window-control buttons here anymore — the shared ychrome overlays the
     * SDF min/max/close on the right of this strip and handles their clicks.
     * The tabbar's padding_right (set above) keeps tabs clear of them. */
    yui->titlebar_min = NULL;
    yui->titlebar_max = NULL;
    yui->titlebar_close = NULL;

    yui->titlebar_synced_active = -1;
    yui->titlebar_synced_count = 0;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result yui_titlebar_sync(struct yetty_yui *yui)
{
    if (!yui || !yui->titlebar_tabbar || !yui->tabbar_model) {
        return YETTY_OK_VOID();
    }
    size_t count = yetty_yui_tabbar_model_count(yui->tabbar_model);
    size_t active = yetty_yui_tabbar_model_active_index(yui->tabbar_model);

    struct yetty_ycore_int_result wcount_res = yetty_ygui_tabbar_count(yui->titlebar_tabbar);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, wcount_res, "titlebar_sync: tabbar_count");
    int wcount = wcount_res.value;
    while ((size_t)wcount < count) {
        char label[16];
        snprintf(label, sizeof(label), "Tab %d", wcount + 1);
        struct yetty_yclass_object_ptr_result ar =
            yetty_ygui_tabbar_add_tab(yui->titlebar_tabbar, label);
        if (YETTY_IS_ERR(ar)) {
            yetty_ycore_error_destroy(ar.error);
            break;
        }
        wcount++;
    }
    while ((size_t)wcount > count) {
        wcount--;
        yetty_ycore_error_destroy_safe(yetty_ygui_tabbar_remove_tab(yui->titlebar_tabbar, wcount));
    }
    yui->titlebar_synced_count = count;

    struct yetty_ycore_int_result wactive_r = yetty_ygui_tabbar_active(yui->titlebar_tabbar);
    int wactive = -1;
    if (YETTY_IS_OK(wactive_r)) {
        wactive = wactive_r.value;
    } else {
        yetty_ycore_error_destroy(wactive_r.error);
    }
    if (count > 0 && wactive != (int)active) {
        yetty_ycore_error_destroy_safe(
            yetty_ygui_tabbar_set_active(yui->titlebar_tabbar, (int)active));
    }
    yui->titlebar_synced_active = (int)active;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result yui_titlebar_on_hamburger(struct yetty_yclass_object *btn,
                                                                void *userdata)
{
    (void)btn;
    struct yetty_ycore_void_result show_r =
        yetty_yui_show_view_menu((struct yetty_yui *)userdata, 4.0f, 36.0f);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, show_r, "titlebar_on_hamburger: show_view_menu");
    return YETTY_OK_VOID();
}

YETTY_EXTERNAL_CALLBACK
static void yui_titlebar_on_new_tab(struct yetty_yclass_object *tabbar, void *userdata)
{
    (void)tabbar;
    struct yetty_yui *yui = userdata;
    if (!yui || !yui->tabbar_model) {
        return;
    }
    struct yetty_ycore_void_result add_r =
        yetty_yui_tabbar_model_add_workspace_of_kind(yui->tabbar_model, YETTY_YUI_TAB_SHELL);
    if (YETTY_IS_ERR(add_r)) {
        yerror("yui: titlebar new-tab failed: %s", add_r.error.msg);
        yetty_ycore_error_destroy(add_r.error);
    }
}

static struct yetty_ycore_void_result yui_titlebar_on_min(struct yetty_yclass_object *btn,
                                                          void *userdata)
{
    (void)btn;
    struct yetty_yui *yui = userdata;
    struct yetty_ycore_void_result iconify_result =
        yetty_yui_tabbar_model_iconify(yui ? yui->tabbar_model : NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, iconify_result, "yui: titlebar iconify");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result yui_titlebar_on_max(struct yetty_yclass_object *btn,
                                                          void *userdata)
{
    (void)btn;
    struct yetty_yui *yui = userdata;
    struct yetty_ycore_void_result toggle_result =
        yetty_yui_tabbar_model_toggle_maximize(yui ? yui->tabbar_model : NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, toggle_result, "yui: titlebar toggle maximize");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result yui_titlebar_on_close_window(struct yetty_yclass_object *btn,
                                                                   void *userdata)
{
    (void)btn;
    struct yetty_yui *yui = userdata;
    struct yetty_ycore_void_result close_result =
        yetty_yui_tabbar_model_close_window(yui ? yui->tabbar_model : NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, close_result, "yui: titlebar close window");
    return YETTY_OK_VOID();
}

/* TABBAR VALUE_CHANGED fires after the widget updates its own active
 * state (user click OR our own sync set_active). Forward to the model
 * only on a genuine change so the sync path doesn't recurse. */
static struct yetty_ycore_void_result yui_titlebar_on_tab_change(
    struct yetty_yclass_object *target, const struct yetty_ygui_event *event, void *userdata)
{
    (void)target;
    struct yetty_yui *yui = userdata;
    if (!yui || !yui->tabbar_model || !event) {
        return YETTY_OK_VOID();
    }
    size_t idx = (size_t)event->i0;
    if (idx != yetty_yui_tabbar_model_active_index(yui->tabbar_model)) {
        {
            struct yetty_ycore_void_result drop_r =
                yetty_yui_tabbar_model_switch_to(yui->tabbar_model, idx);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, drop_r, "drop: yetty_yui_tabbar_model_switch_to");
        }
    }
    return YETTY_OK_VOID();
}

/* Close-x click on a tab pill — close the matching workspace; the next
 * sync mirrors the new count into the widget. */
YETTY_EXTERNAL_CALLBACK
static void yui_titlebar_on_tab_close(struct yetty_yclass_object *tabbar, int index, void *userdata)
{
    (void)tabbar;
    struct yetty_yui *yui = userdata;
    if (!yui || !yui->tabbar_model || index < 0) {
        return;
    }
    struct yetty_ycore_void_result close_r =
        yetty_yui_tabbar_model_close_at(yui->tabbar_model, (size_t)index);
    if (YETTY_IS_ERR(close_r)) {
        yerror("yui: titlebar close-tab failed: %s", close_r.error.msg);
        yetty_ycore_error_destroy(close_r.error);
    }
}

static struct yetty_ycore_void_result yui_dialog_connect(struct yetty_yclass_object *button,
                                                         void *userdata)
{
    (void)button;
    struct yui_cb_ctx *cb = userdata;
    if (!cb || !cb->yui || (int)cb->kind < 0 || (int)cb->kind >= YETTY_YUI_VIEW_KIND_COUNT) {
        return YETTY_OK_VOID();
    }
    struct yetty_yclass_object *dlg = cb->yui->dialogs[(int)cb->kind];
    if (dlg) {
        yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_visible(dlg, 0));
    }
    if (cb->yui->engine) {
        yetty_ygui_framework_mark_dirty(cb->yui->engine);
    }
    if (cb->yui->connect_cb) {
        cb->yui->connect_cb(cb->yui->connect_userdata, cb->kind);
    }
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Public menu / connect API
 *===========================================================================*/

struct yetty_ycore_void_result yetty_yui_show_view_menu(struct yetty_yui *yui, float anchor_x,
                                                        float anchor_y)
{
    if (!yui || !yui->app_menu) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result populate_r = yui_app_menu_populate_root(yui);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, populate_r, "show_view_menu: populate");
    struct yetty_ycore_void_result open_r =
        yetty_ygui_popup_menu_open_at(yui->app_menu, anchor_x, anchor_y);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, open_r, "show_view_menu: open_at");
    if (yui->engine) {
        yetty_ygui_framework_mark_dirty(yui->engine);
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yui_show_context_menu(struct yetty_yui *yui, float anchor_x,
                                                           float anchor_y)
{
    if (!yui || !yui->app_menu) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result populate_r = yui_app_menu_populate_context_root(yui);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, populate_r, "show_context_menu: populate");
    struct yetty_ycore_void_result open_r =
        yetty_ygui_popup_menu_open_at(yui->app_menu, anchor_x, anchor_y);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, open_r, "show_context_menu: open_at");
    if (yui->engine) {
        yetty_ygui_framework_mark_dirty(yui->engine);
    }
    return YETTY_OK_VOID();
}

struct yetty_yclass_object *yetty_yui_statusbar(struct yetty_yui *yui)
{
    return yui ? yui->statusbar : NULL;
}

struct yetty_yclass_object *yetty_yui_engine(struct yetty_yui *yui)
{
    return yui ? yui->engine : NULL;
}

struct yetty_ycore_void_result yetty_yui_set_status_left(struct yetty_yui *yui, const char *text)
{
    if (!yui || !yui->statusbar) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result set_r =
        yetty_ygui_statusbar_set_left(yui->statusbar, text ? text : "");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "set_status_left: statusbar_set_left");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yui_set_status_right(struct yetty_yui *yui, const char *text)
{
    if (!yui || !yui->statusbar) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result set_r =
        yetty_ygui_statusbar_set_right(yui->statusbar, text ? text : "");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "set_status_right: statusbar_set_right");
    return YETTY_OK_VOID();
}

struct yetty_ycore_float_result yetty_yui_statusbar_height(const struct yetty_yui *yui)
{
    if (!yui || !yui->statusbar) {
        return YETTY_OK(yetty_ycore_float, 0.0f);
    }
    /* The statusbar is a ygui chrome widget laid out in logical pixels;
     * its height feeds the terminal workspace layout, which works in
     * framebuffer pixels. Convert back up so the reserved strip matches
     * the statusbar's physical (receiver-scaled) render height. Prefer
     * the runtime's live scale over the cached copy: the app-level
     * RESIZE handler queries this height after adopting a remote
     * viewer's density but before yetty_yui_resize refreshes the cache,
     * and the strip must be reserved at the height the statusbar will
     * actually repaint at. */
    float scale = yui->content_scale > 0.0f ? yui->content_scale : 1.0f;
    if (yui->ctx && yui->ctx->runtime) {
        float runtime_scale = yui->ctx->runtime->gpu.app_gpu_context.content_scale;
        if (runtime_scale > 0.0f) {
            scale = runtime_scale;
        }
    }
    struct yetty_ycore_rectangle_result rect_res = yetty_ygui_widget_rect(yui->statusbar);
    YETTY_RETURN_IF_ERR(yetty_ycore_float, rect_res, "statusbar_height: widget_rect");
    struct yetty_ycore_rectangle r = rect_res.value;
    float h = (r.max.y - r.min.y) * scale;
    return YETTY_OK(yetty_ycore_float, h > 0.0f ? h : 22.0f * scale);
}

struct yetty_ycore_const_char_ptr_result yetty_yui_get_field_text(const struct yetty_yui *yui,
                                                                  enum yetty_yui_view_kind kind,
                                                                  int field_idx)
{
    if (!yui || (int)kind < 0 || (int)kind >= YETTY_YUI_VIEW_KIND_COUNT) {
        return YETTY_OK(yetty_ycore_const_char_ptr, NULL);
    }
    if (field_idx < 0 || field_idx >= 4) {
        return YETTY_OK(yetty_ycore_const_char_ptr, NULL);
    }
    struct yetty_yclass_object *in = yui->dialog_inputs[(int)kind][field_idx];
    if (!in) {
        return YETTY_OK(yetty_ycore_const_char_ptr, NULL);
    }
    struct yetty_ycore_const_char_ptr_result text_r = yetty_ygui_textinput_get_text(in);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, text_r, "get_field_text: textinput_get_text");
    return text_r;
}

struct yetty_ycore_const_char_ptr_result yetty_yui_get_exec_command(const struct yetty_yui *yui)
{
    return yetty_yui_get_field_text(yui, YETTY_YUI_VIEW_EXEC, 0);
}

struct yetty_ycore_int_result yetty_yui_is_active(const struct yetty_yui *yui)
{
    if (!yui) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    if (yui->app_menu) {
        struct yetty_ycore_int_result open_r = yetty_ygui_popup_menu_is_open(yui->app_menu);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, open_r, "is_active: popup_menu_is_open");
        if (open_r.value) {
            return YETTY_OK(yetty_ycore_int, 1);
        }
    }
    for (int k = 0; k < YETTY_YUI_VIEW_KIND_COUNT; k++) {
        if (yui->dialogs[k]) {
            struct yetty_ycore_int_result visible_res =
                yetty_ygui_widget_is_visible(yui->dialogs[k]);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, visible_res, "is_active: dialog is_visible");
            if (visible_res.value) {
                return YETTY_OK(yetty_ycore_int, 1);
            }
        }
    }
    if (yui->gpu_info_dialog) {
        struct yetty_ycore_int_result gpu_visible_res =
            yetty_ygui_widget_is_visible(yui->gpu_info_dialog);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, gpu_visible_res, "is_active: gpu dialog is_visible");
        if (gpu_visible_res.value) {
            return YETTY_OK(yetty_ycore_int, 1);
        }
    }
    struct yetty_ycore_int_result config_visible_res =
        yetty_yui_config_dialog_is_visible(yui->config_dialog);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, config_visible_res, "is_active: config_dialog is_visible");
    if (config_visible_res.value) {
        return YETTY_OK(yetty_ycore_int, 1);
    }
    return YETTY_OK(yetty_ycore_int, 0);
}

/* Feed a decoded key to whichever dialog textinput currently has focus.
 * Returns 1 if a focused input consumed it. */
static struct yetty_ycore_int_result yui_feed_key_to_focused(struct yetty_yui *yui, uint32_t key)
{
    for (int k = 0; k < YETTY_YUI_VIEW_KIND_COUNT; k++) {
        for (int f = 0; f < 4; f++) {
            struct yetty_yclass_object *in = yui->dialog_inputs[k][f];
            if (!in) {
                continue;
            }
            struct yetty_ycore_int_result handled = yetty_ygui_textinput_handle_key(in, key);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, handled, "yui_feed_key_to_focused: handle_key");
            if (handled.value) {
                if (yui->engine) {
                    yetty_ygui_framework_mark_dirty(yui->engine);
                }
                return YETTY_OK(yetty_ycore_int, 1);
            }
        }
    }
    return YETTY_OK(yetty_ycore_int, 0);
}

static struct yetty_ycore_int_result yui_compute_cursor_shape(struct yetty_yui *yui, float mouse_x,
                                                              float mouse_y)
{
    if (!yui || !yui->engine) {
        return YETTY_OK(yetty_ycore_int, YETTY_YCORE_CURSOR_DEFAULT);
    }
    struct yetty_yclass_object *w = yetty_ygui_framework_pressed_widget(yui->engine);
    if (!w) {
        w = yetty_ygui_framework_hovered_widget(yui->engine);
    }
    struct yetty_ycore_int_result axis_r =
        yetty_ygui_splitter_get_axis(w); /* -1 if not a splitter */
    YETTY_RETURN_IF_ERR(yetty_ycore_int, axis_r, "compute_cursor_shape: splitter_get_axis");
    int axis = axis_r.value;
    if (axis >= 0) {
        /* axis 1 = vertical bar splitting side-by-side panes → ↔ HRESIZE;
         * axis 0 = horizontal bar splitting stacked panes → ↕ VRESIZE. */
        return YETTY_OK(yetty_ycore_int,
                        axis == 1 ? YETTY_YCORE_CURSOR_HRESIZE : YETTY_YCORE_CURSOR_VRESIZE);
    }
    /* Window-edge resize cursor is owned by the chrome now (it sets it directly
     * while hovering an edge); yui only decides the splitter cursor here. */
    return YETTY_OK(yetty_ycore_int, YETTY_YCORE_CURSOR_DEFAULT);
}

static struct yetty_ycore_void_result yui_apply_cursor(struct yetty_yui *yui, float mouse_x,
                                                       float mouse_y)
{
    if (!yui || !yui->ctx) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_int_result shape_r = yui_compute_cursor_shape(yui, mouse_x, mouse_y);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, shape_r, "apply_cursor: compute_cursor_shape");
    int shape = shape_r.value;
    if (shape == yui->last_cursor_shape) {
        return YETTY_OK_VOID();
    }
    struct yetty_yclass_object *wm = yui->ctx->runtime->window_chrome;
    if (wm) {
        struct yetty_ycore_void_result cursor_result =
            yetty_yplatform_window_chrome_set_cursor(wm, shape);
        if (YETTY_IS_ERR(cursor_result)) {
            yetty_ycore_error_destroy(cursor_result.error);
        }
    }
    yui->last_cursor_shape = shape;
    return YETTY_OK_VOID();
}

/* Client-first / chrome-fallback: hand a pointer event the ygui engine didn't
 * consume to the window chrome (drag / edge-resize / maximize / window
 * controls). Chrome works in raw framebuffer px, so the unscaled event passes
 * straight through. Returns 1 if chrome claimed it. */
static struct yetty_ycore_int_result yui_chrome_fallback(struct yetty_yui *yui,
                                                         const struct yetty_yui_event *event)
{
    if (!yui->chrome) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    struct yetty_ycore_int_result chrome_result =
        yetty_ychrome_host_handle_event(yui->chrome, event);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, chrome_result, "yui_chrome_fallback: handle_event");
    return YETTY_OK(yetty_ycore_int, chrome_result.value ? 1 : 0);
}

struct yetty_ycore_int_result yetty_yui_on_event(struct yetty_yui *yui,
                                                 const struct yetty_yui_event *event)
{
    if (!yui || !event || !yui->engine) {
        return YETTY_OK(yetty_ycore_int, 0);
    }

    struct yetty_ycore_int_result active_r = yetty_yui_is_active(yui);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, active_r, "on_event: is_active");
    int active = active_r.value;
    int has_pressed = yetty_ygui_framework_has_pressed_widget(yui->engine);
    int in_titlebar = 0;
    /* The ygui chrome is laid out in logical pixels; pointer events arrive
     * in framebuffer pixels. Convert once so every ygui-facing hit-test and
     * feed below works in the chrome's own coordinate space. The yui tile
     * model (cursor edge / tab hit) stays in framebuffer pixels, so
     * yui_apply_cursor below keeps the raw event coordinates. */
    const float scale = yui->content_scale > 0.0f ? yui->content_scale : 1.0f;
    switch (event->type) {
    case YETTY_YCORE_MOUSE_DOWN:
    case YETTY_YCORE_MOUSE_UP:
    case YETTY_YCORE_MOUSE_MOVE:
    case YETTY_YCORE_MOUSE_DRAG:
        in_titlebar = event->mouse.y / scale < 36.0f /* YETTY_YUI_TABBAR_HEIGHT */;
        break;
    default:
        break;
    }
    if (!active && !in_titlebar && !has_pressed && event->type != YETTY_YCORE_MOUSE_DOWN &&
        event->type != YETTY_YCORE_MOUSE_UP && event->type != YETTY_YCORE_MOUSE_MOVE &&
        event->type != YETTY_YCORE_MOUSE_DRAG) {
        return YETTY_OK(yetty_ycore_int, 0);
    }

    switch (event->type) {
    case YETTY_YCORE_MOUSE_DOWN: {
        /* A press outside an open context / app menu dismisses it (and
         * is consumed so the stray click doesn't also act). A press
         * inside falls through to the engine, which routes it to the
         * menu's item dispatch. */
        struct yetty_ycore_int_result app_open_r =
            yui->app_menu ? yetty_ygui_popup_menu_is_open(yui->app_menu)
                          : YETTY_OK(yetty_ycore_int, 0);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, app_open_r, "yui: app_menu is_open");
        if (app_open_r.value) {
            struct yetty_ycore_rectangle_result mr_res = yetty_ygui_widget_rect(yui->app_menu);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, mr_res, "on_event: app_menu widget_rect");
            struct yetty_ycore_rectangle mr = mr_res.value;
            float mx = event->mouse.x / scale;
            float my = event->mouse.y / scale;
            int inside = mx >= mr.min.x && mx < mr.max.x && my >= mr.min.y && my < mr.max.y;
            if (!inside) {
                yetty_ycore_error_destroy_safe(yetty_ygui_popup_menu_close(yui->app_menu));
                yetty_ygui_framework_mark_dirty(yui->engine);
                return YETTY_OK(yetty_ycore_int, 1);
            }
        }
        int down_consumed;
        {
            struct yetty_ycore_int_result feed_r = yetty_ygui_framework_feed_mouse_button(
                yui->engine, event->mouse.x / scale, event->mouse.y / scale, event->mouse.button, 1,
                event->mouse.mods);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, feed_r, "on_event: feed button down");
            down_consumed = feed_r.value;
        }
        {
            struct yetty_ycore_void_result cursor_r =
                yui_apply_cursor(yui, event->mouse.x, event->mouse.y);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, cursor_r, "on_event: apply_cursor (down)");
        }
        if (down_consumed || active || yetty_ygui_framework_has_pressed_widget(yui->engine)) {
            return YETTY_OK(yetty_ycore_int, 1);
        }
        /* Client-first: the ygui engine didn't take it — give the window chrome
         * a shot (title-bar drag / edges / controls). If chrome claims it, stop;
         * otherwise it's a body click → fall through to the terminal. */
        struct yetty_ycore_int_result fallback_r = yui_chrome_fallback(yui, event);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, fallback_r, "on_event: chrome fallback (down)");
        return YETTY_OK(yetty_ycore_int, fallback_r.value ? 1 : 0);
    }
    case YETTY_YCORE_MOUSE_UP: {
        struct yetty_ycore_int_result feed_r = yetty_ygui_framework_feed_mouse_button(
            yui->engine, event->mouse.x / scale, event->mouse.y / scale, event->mouse.button, 0,
            event->mouse.mods);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, feed_r, "on_event: feed button up");
        {
            struct yetty_ycore_void_result cursor_r =
                yui_apply_cursor(yui, event->mouse.x, event->mouse.y);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, cursor_r, "on_event: apply_cursor (up)");
        }
        if (feed_r.value || active || has_pressed) {
            return YETTY_OK(yetty_ycore_int, 1);
        }
        struct yetty_ycore_int_result fallback_r = yui_chrome_fallback(yui, event);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, fallback_r, "on_event: chrome fallback (up)");
        return YETTY_OK(yetty_ycore_int, fallback_r.value ? 1 : 0);
    }
    case YETTY_YCORE_MOUSE_DOUBLE_CLICK: {
        /* ygui has no double-click here; it's the chrome's title-bar maximize. */
        struct yetty_ycore_int_result fallback_r = yui_chrome_fallback(yui, event);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, fallback_r,
                            "on_event: chrome fallback (double-click)");
        return YETTY_OK(yetty_ycore_int, fallback_r.value ? 1 : 0);
    }
    case YETTY_YCORE_MOUSE_MOVE:
    case YETTY_YCORE_MOUSE_DRAG: {
        struct yetty_ycore_int_result feed_r = yetty_ygui_framework_feed_mouse_motion(
            yui->engine, event->mouse.x / scale, event->mouse.y / scale);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, feed_r, "on_event: feed motion");
        {
            struct yetty_ycore_void_result cursor_r =
                yui_apply_cursor(yui, event->mouse.x, event->mouse.y);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, cursor_r, "on_event: apply_cursor (move)");
        }
        /* Chrome runs after apply_cursor so its resize-edge cursor wins at the
         * window margins; over the body it leaves the cursor alone. */
        if (!feed_r.value && !has_pressed) {
            struct yetty_ycore_int_result fallback_r = yui_chrome_fallback(yui, event);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, fallback_r, "on_event: chrome fallback (move)");
        }
        return YETTY_OK(yetty_ycore_int, active || has_pressed ? 1 : 0);
    }
    case YETTY_YCORE_KEY_DOWN:
        /* Route editing keys to the focused dialog textinput. Printable
         * characters arrive via YETTY_YCORE_CHAR; KEY_DOWN carries the
         * special keys we care about (backspace). */
        if (event->key.key == YUI_GLFW_KEY_BACKSPACE) {
            struct yetty_ycore_int_result feed_r = yui_feed_key_to_focused(yui, 0x7F);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, feed_r, "on_event: feed key (backspace)");
        }
        return YETTY_OK(yetty_ycore_int, active ? 1 : 0);
    case YETTY_YCORE_KEY_UP:
        return YETTY_OK(yetty_ycore_int, active ? 1 : 0);
    case YETTY_YCORE_CHAR: {
        uint32_t cp = event->chr.codepoint;
        if (cp >= 32 && cp < 127) {
            struct yetty_ycore_int_result feed_r = yui_feed_key_to_focused(yui, cp);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, feed_r, "on_event: feed key (char)");
        }
        return YETTY_OK(yetty_ycore_int, active ? 1 : 0);
    }
    case YETTY_YCORE_MOUSE_SCROLL:
        /* Wheel / trackpad → ygui, so a focused dialog's scrollarea scrolls
         * instead of the input falling through to terminal scrollback. */
        /* Pointer position → logical; wheel deltas (dx/dy) are notches,
         * not pixels, so they pass through unscaled. */
        yetty_ycore_error_destroy_safe(yetty_ygui_framework_feed_mouse_scroll(
            yui->engine, event->mouse_scroll.x / scale, event->mouse_scroll.y / scale,
            event->mouse_scroll.dx, event->mouse_scroll.dy));
        return YETTY_OK(yetty_ycore_int, active ? 1 : 0);
    default:
        return YETTY_OK(yetty_ycore_int, 0);
    }
}

void yetty_yui_set_connect_callback(struct yetty_yui *yui, yetty_yui_connect_cb cb, void *userdata)
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
    if (!yui || !yui->root_container_obj) {
        return YETTY_OK_VOID();
    }
    if (surface_w == 0 || surface_h == 0) {
        return YETTY_OK_VOID();
    }
    yui->surface_w = (float)surface_w;
    yui->surface_h = (float)surface_h;
    /* The runtime's content scale can change between resizes — a yvnc
     * viewer pushes its display density with its resize request and the
     * app-level handler adopts it before cascading here. Refresh the
     * cached copy so the logical viewport below and all pointer /
     * splitter scaling stay consistent with the new density. */
    if (yui->ctx && yui->ctx->runtime) {
        float runtime_scale = yui->ctx->runtime->gpu.app_gpu_context.content_scale;
        if (runtime_scale > 0.0f) {
            yui->content_scale = runtime_scale;
        }
    }
    if (yui->chrome) {
        struct yetty_ycore_void_result cr =
            yetty_ychrome_host_resized(yui->chrome, (float)surface_w, (float)surface_h);
        if (YETTY_IS_ERR(cr)) {
            yetty_ycore_error_destroy(cr.error);
        }
    }
    if (yui->engine) {
        /* Logical viewport (see yui_create); container rect below stays
         * in framebuffer pixels. */
        yetty_ycore_error_destroy_safe(
            yetty_ygui_framework_set_viewport(yui->engine, (float)surface_w / yui->content_scale,
                                              (float)surface_h / yui->content_scale));
    }
    struct yetty_yfigure_figure_ptr_result rf_res =
        yetty_yfigure_container_as_figure(yui->root_container_obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rf_res, "yui_resize: root_container as_figure");
    struct yetty_yfigure_figure *rf = rf_res.value;
    {
        struct yetty_ycore_void_result drop_r =
            yetty_yfigure_figure_rect_set((struct yetty_yclass_object *)(rf)-1,
                                          (struct yetty_ycore_rectangle){
                                              .min = {.x = 0.0f, .y = 0.0f},
                                              .max = {.x = (float)surface_w, .y = (float)surface_h},
                                          });
        YETTY_RETURN_IF_ERR(yetty_ycore_void, drop_r, "yui_resize: root rect");
    }
    {
        struct yetty_ycore_void_result drop_r =
            yetty_yfigure_figure_dirty_set((struct yetty_yclass_object *)(rf)-1, 1);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, drop_r, "drop: yetty_yfigure_figure_dirty_set");
    }
    return YETTY_OK_VOID();
}
