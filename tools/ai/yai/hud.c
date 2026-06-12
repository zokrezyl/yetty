/*
 * hud.c — yai's non-scrolling ygui status window.
 */
#include "hud.h"
#include "render.h"

#include <yetty/ygui/framework.h>
#include <yetty/ygui/widget.h>
#include <yetty/ygui/widgets/checkbox.h>
#include <yetty/ygui/widgets/dialog.h>
#include <yetty/ygui/widgets/hbox.h>
#include <yetty/ygui/widgets/label.h>
#include <yetty/ygui/widgets/radio.h>
#include <yetty/ygui/widgets/slider.h>
#include <yetty/ygui/widgets/tabbar.h>
#include <yetty/ygui/widgets/vbox.h>
#include <yetty/ygui/widgets/window.h>
#include <yetty/yplatform/pty.h>

#include <yetty/ytrace/ytrace.h>

#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define YAI_HUD_WINDOW_WIDTH 380.0f
#define YAI_HUD_WINDOW_HEIGHT 128.0f
#define YAI_HUD_WINDOW_MIN_WIDTH 220.0f
#define YAI_HUD_WINDOW_MIN_HEIGHT 96.0f
#define YAI_HUD_MARGIN 16.0f
#define YAI_HUD_LABEL_HEIGHT 18.0f
/* Bottom-right corner square that starts a yai-side resize drag. */
#define YAI_HUD_GRIP_HIT 18.0f
/* Docked mode: the HUD is a fixed-height bar across the bottom edge,
 * and yai reserves this many pixels of the pane (via the terminal
 * scroll region) so conversation text never scrolls under it. */
#define YAI_HUD_DOCK_HEIGHT 70.0f

/* Config dialog geometry + widget pools. The dialog widget reserves
 * 32px title bar + 16px padding (top), 16px bottom/sides, and stacks
 * children with an 8px gap — the height computation below must match
 * those (see ygui dialog.c). */
#define YAI_HUD_CONFIG_WIDTH 560.0f
#define YAI_HUD_CONFIG_TOP_PAD 48.0f
#define YAI_HUD_CONFIG_BOTTOM_PAD 16.0f
#define YAI_HUD_CONFIG_GAP 8.0f
#define YAI_HUD_CONFIG_INFO_HEIGHT 20.0f
#define YAI_HUD_CONFIG_CONTROL_HEIGHT 24.0f
#define YAI_HUD_CONFIG_TABBAR_HEIGHT 28.0f
/* Shared across all tabs (only the active tab's rows are visible), so it
 * must cover the SUM of every tab's read-only rows. */
#define YAI_HUD_CONFIG_MAX_INFO_ROWS 36
#define YAI_HUD_CONFIG_FOLD_MIN 1.0f
#define YAI_HUD_CONFIG_FOLD_MAX 40.0f
/* Matches the ygui dialog widget's title strip height + close button
 * width (dialog.c DIALOG_TITLE_H / DIALOG_CLOSE_W). */
#define YAI_HUD_CONFIG_TITLE_H 32.0f
#define YAI_HUD_CONFIG_CLOSE_W 32.0f

/*---------------------------------------------------------------------------
 * Blocking stdout pty shim. The framework writes its compositor envelope
 * through this; stdout is fflushed first, so envelope bytes can never be
 * interleaved with buffered text (yai is the single PTY writer).
 *---------------------------------------------------------------------------*/

static struct yetty_ycore_size_result hud_pty_write(struct yetty_platform_pty *self,
                                                    const char *data, size_t len)
{
    (void)self;
    struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
    YETTY_RETURN_IF_ERR(yetty_ycore_size, flush_res, "hud_pty_write: flush pending text");
    size_t written = 0;
    while (written < len) {
        ssize_t chunk = write(STDOUT_FILENO, data + written, len - written);
        if (chunk < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                /* stdout shares the tty's open file description with
                 * the non-blocking stdin poll — wait for writability
                 * instead of truncating the envelope. */
                struct pollfd ready = {.fd = STDOUT_FILENO, .events = POLLOUT};
                (void)poll(&ready, 1, -1);
                continue;
            }
            return YETTY_ERR(yetty_ycore_size, "hud_pty_write: write failed");
        }
        written += (size_t)chunk;
    }
    return YETTY_OK(yetty_ycore_size, len);
}

static struct yetty_ycore_size_result hud_pty_read(struct yetty_platform_pty *self, char *buf,
                                                   size_t len)
{
    (void)self;
    (void)buf;
    (void)len;
    return YETTY_OK(yetty_ycore_size, 0);
}

static struct yetty_ycore_void_result hud_pty_noop(struct yetty_platform_pty *self)
{
    (void)self;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result hud_pty_resize(struct yetty_platform_pty *self, uint32_t cols,
                                                     uint32_t rows, uint32_t width_px,
                                                     uint32_t height_px)
{
    (void)self;
    (void)cols;
    (void)rows;
    (void)width_px;
    (void)height_px;
    return YETTY_OK_VOID();
}

static struct yetty_platform_pty_pipe_source *hud_pty_pipe_source(struct yetty_platform_pty *self)
{
    (void)self;
    return NULL;
}

static const struct yetty_platform_pty_ops *hud_pty_ops(void)
{
    static const struct yetty_platform_pty_ops ops = {
        .destroy = hud_pty_noop,
        .read = hud_pty_read,
        .write = hud_pty_write,
        .resize = hud_pty_resize,
        .stop = hud_pty_noop,
        .pipe_source = hud_pty_pipe_source,
    };
    return &ops;
}

/*---------------------------------------------------------------------------
 * HUD
 *---------------------------------------------------------------------------*/

struct yai_hud {
    struct yetty_platform_pty pty;
    struct yetty_ygui_framework *framework;
    struct yetty_yclass_object *root;
    struct yetty_yclass_object *window;
    /* Left column — live conversation status. */
    struct yetty_yclass_object *state_label;
    struct yetty_yclass_object *turn_label;
    struct yetty_yclass_object *session_label;
    /* Right column — lower-priority reference (model + statistics). */
    struct yetty_yclass_object *model_label;
    struct yetty_yclass_object *stats_label;
    struct yetty_yclass_object *stats2_label;

    /* Floating EDITABLE, TABBED config dialog (built lazily on the first
     * /config). Widget pools are fixed; every page's widgets are
     * pre-built and tagged with their tab, and only the active tab's are
     * visible. Radio exclusivity is enforced in the poll. */
    struct yetty_yclass_object *config_dialog;
    struct yetty_yclass_object *config_tabbar;
    int config_active_tab;
    int config_tab_count;
    struct yetty_yclass_object *config_info_rows[YAI_HUD_CONFIG_MAX_INFO_ROWS];
    int config_info_tab[YAI_HUD_CONFIG_MAX_INFO_ROWS]; /* tab of each used row */
    int config_info_used;
    struct yetty_yclass_object *config_thinking_checkbox;
    struct yetty_yclass_object *config_fold_label;
    struct yetty_yclass_object *config_fold_slider;
    int config_controls_tab; /* tab hosting checkbox+slider; -1 = none */
    /* One radio group per knob (label + option pool), placed on a tab. */
    struct {
        struct yetty_yclass_object *label;
        struct yetty_yclass_object *radios[YAI_HUD_CONFIG_KNOB_MAX_OPTIONS];
        int active_options;
        int last_selected; /* exclusivity anchor */
        int tab;           /* tab hosting this knob */
    } config_knobs[YAI_HUD_CONFIG_MAX_KNOBS];
    int config_knob_count;
    /* Client-side titlebar drag for the config dialog (the dialog
     * widget, unlike the window widget, has no built-in drag). */
    int config_dragging;
    float config_drag_pos_x;
    float config_drag_pos_y;
    float config_drag_cursor_x;
    float config_drag_cursor_y;

    /* yai-side corner-resize drag state (the window widget on this
     * branch has no resize grip of its own; resize lives here). */
    int resizing;
    float resize_start_width;
    float resize_start_height;
    float resize_cursor_x;
    float resize_cursor_y;

    /* Viewport truth. TIOCGWINSZ is only an estimate (pixel fields are
     * often zero); the host's resize envelope is authoritative. Until
     * the user drags/resizes the window, viewport changes re-place it
     * top-right; afterwards they only clamp it back into view. */
    float viewport_width;
    float viewport_height;
    int viewport_from_host;
    int user_touched;
    /* Docked: a fixed bottom bar (default). Floating (YAI_HUD_FLOAT):
     * the old draggable/resizable top-right window. */
    int docked;
};

/* (width_px, height_px) of the pane; estimated from the cell grid when
 * the terminal does not report pixel fields. */
static void terminal_pixels(float *width_px, float *height_px)
{
    struct winsize size = {0};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) != 0) {
        memset(&size, 0, sizeof(size));
    }
    unsigned cols = size.ws_col ? size.ws_col : 80;
    unsigned rows = size.ws_row ? size.ws_row : 24;
    *width_px = size.ws_xpixel ? (float)size.ws_xpixel : (float)cols * 9.0f;
    *height_px = size.ws_ypixel ? (float)size.ws_ypixel : (float)rows * 19.0f;
}

/* Current window rect in viewport pixels (authored layout state — drag
 * updates pos, yai-side resize updates size). */
static void window_rect(const struct yai_hud *hud, float *min_x, float *min_y, float *width,
                        float *height)
{
    const struct yetty_ygui_layout *layout = yetty_ygui_widget_layout_get(hud->window);
    *min_x = layout->pos_x;
    *min_y = layout->pos_y;
    *width = layout->width;
    *height = layout->height;
}

static struct yetty_yclass_object_ptr_result hud_add(struct yetty_yclass_object *parent,
                                                   struct yetty_yclass_ptr_result class_result)
{
    if (YETTY_IS_ERR(class_result)) {
        return YETTY_ERR(yetty_yclass_object_ptr, "hud_add: class_get failed", class_result);
    }
    if (!parent) {
        return yetty_ygui_widget_new(class_result.value);
    }
    return yetty_ygui_widget_add(parent, class_result.value);
}

static struct yetty_yclass_object_ptr_result hud_add_label(struct yetty_yclass_object *parent,
                                                         const char *initial_text,
                                                         struct yetty_ycore_rgba color)
{
    if (!parent) {
        return YETTY_ERR(yetty_yclass_object_ptr, "hud_add_label: NULL parent");
    }
    struct yetty_yclass_object_ptr_result label_res = hud_add(parent, yetty_ygui_label_class_get());
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, label_res, "hud_add_label: add label");
    struct yetty_yclass_object *label = label_res.value;

    struct yetty_ycore_void_result size_res =
        yetty_ygui_widget_set_size(label, 0.0f, YAI_HUD_LABEL_HEIGHT);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, size_res, "hud_add_label: set_size");
    struct yetty_ycore_void_result text_res = yetty_ygui_label_set_text(label, initial_text);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, text_res, "hud_add_label: set_text");
    struct yetty_ycore_void_result color_res = yetty_ygui_label_set_color(label, color);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, color_res, "hud_add_label: set_color");
    return YETTY_OK(yetty_yclass_object_ptr, label);
}

/* Reserved pixel height the docked HUD takes off the bottom of the
 * pane (0 when floating). yai turns this into reserved terminal rows. */
float yai_hud_dock_height(const struct yai_hud *hud)
{
    if (!hud || !hud->docked) {
        return 0.0f;
    }
    return YAI_HUD_DOCK_HEIGHT;
}

/* Apply the current viewport: framework layout space + window
 * placement. Docked: a full-width bar pinned to the bottom edge.
 * Floating: top-right until the user has moved/resized it. */
static struct yetty_ycore_void_result hud_apply_viewport(struct yai_hud *hud)
{
    struct yetty_ycore_void_result viewport_res = yetty_ygui_framework_set_viewport(
        hud->framework, hud->viewport_width, hud->viewport_height);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, viewport_res, "hud_apply_viewport: set_viewport");

    if (hud->docked) {
        /* Full-width bar across the bottom edge. */
        struct yetty_ycore_void_result size_res = yetty_ygui_widget_set_size(
            hud->window, hud->viewport_width, YAI_HUD_DOCK_HEIGHT);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, size_res, "hud_apply_viewport: dock size");
        struct yetty_ycore_void_result position_res = yetty_ygui_widget_set_position(
            hud->window, 0.0f, hud->viewport_height - YAI_HUD_DOCK_HEIGHT);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, position_res, "hud_apply_viewport: dock pos");
        yetty_ygui_framework_mark_dirty(hud->framework);
        return yai_hud_flush(hud);
    }

    float min_x = 0.0f;
    float min_y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    window_rect(hud, &min_x, &min_y, &width, &height);

    float target_x = min_x;
    float target_y = min_y;
    if (!hud->user_touched) {
        target_x = hud->viewport_width - width - YAI_HUD_MARGIN;
        target_y = YAI_HUD_MARGIN;
    }
    if (target_x > hud->viewport_width - 48.0f) {
        target_x = hud->viewport_width - 48.0f;
    }
    if (target_y > hud->viewport_height - 24.0f) {
        target_y = hud->viewport_height - 24.0f;
    }
    if (target_x < 0.0f) {
        target_x = 0.0f;
    }
    if (target_y < 0.0f) {
        target_y = 0.0f;
    }
    if (target_x != min_x || target_y != min_y) {
        struct yetty_ycore_void_result position_res =
            yetty_ygui_widget_set_position(hud->window, target_x, target_y);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, position_res, "hud_apply_viewport: set_position");
    }
    yetty_ygui_framework_mark_dirty(hud->framework);
    return yai_hud_flush(hud);
}

/* Build the widget tree + initial geometry. Split out so create() can
 * tear the framework down on any failure without repeating cleanup. */
static struct yetty_ycore_void_result hud_build(struct yai_hud *hud)
{
    struct yetty_yclass_object_ptr_result root_res = hud_add(NULL, yetty_ygui_vbox_class_get());
    YETTY_RETURN_IF_ERR(yetty_ycore_void, root_res, "hud_build: root vbox");
    hud->root = root_res.value;
    struct yetty_ycore_void_result set_root_res =
        yetty_ygui_framework_set_root(hud->framework, hud->root);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, set_root_res, "hud_build: set_root");

    struct yetty_yclass_object_ptr_result window_res =
        hud_add(hud->root, yetty_ygui_window_class_get());
    YETTY_RETURN_IF_ERR(yetty_ycore_void, window_res, "hud_build: window");
    hud->window = window_res.value;
    if (hud->docked) {
        /* Docked bar: no titlebar — it would just take space. */
        struct yetty_ycore_void_result chrome_res =
            yetty_ygui_window_set_chromeless(hud->window, 1);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, chrome_res, "hud_build: chromeless");
    } else {
        struct yetty_ycore_void_result title_res = yetty_ygui_window_set_title(hud->window, "yai");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, title_res, "hud_build: set_title");
    }

    /* TIOCGWINSZ estimate only — the host's resize envelope replaces it
     * with the authoritative pixel size right after the mouse subscribe. */
    terminal_pixels(&hud->viewport_width, &hud->viewport_height);
    struct yetty_ycore_void_result size_res =
        yetty_ygui_widget_set_size(hud->window, YAI_HUD_WINDOW_WIDTH, YAI_HUD_WINDOW_HEIGHT);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, size_res, "hud_build: set_size");
    struct yetty_ycore_void_result place_res = hud_apply_viewport(hud);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, place_res, "hud_build: apply_viewport");

    struct yetty_ycore_rgba accent = {.r = 107, .g = 168, .b = 146, .a = 255};
    struct yetty_ycore_rgba secondary = {.r = 159, .g = 167, .b = 168, .a = 255};
    struct yetty_ycore_rgba muted = {.r = 133, .g = 141, .b = 143, .a = 255};

    /* Two-column body: left = live status, right = model + statistics.
     * An hbox splits the window body; each column flex-grows so the bar
     * tracks pane width without per-resize re-layout of the columns. */
    struct yetty_yclass_object *body = yetty_ygui_window_body(hud->window);
    if (!body) {
        return YETTY_ERR(yetty_ycore_void, "hud_build: window has no body");
    }
    struct yetty_yclass_object_ptr_result split_res = hud_add(body, yetty_ygui_hbox_class_get());
    YETTY_RETURN_IF_ERR(yetty_ycore_void, split_res, "hud_build: split hbox");
    struct yetty_ycore_void_result split_css_res =
        yetty_ygui_widget_apply_css(split_res.value, "gap: 14; flex-grow: 1");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, split_css_res, "hud_build: split css");

    struct yetty_yclass_object_ptr_result left_res =
        hud_add(split_res.value, yetty_ygui_vbox_class_get());
    YETTY_RETURN_IF_ERR(yetty_ycore_void, left_res, "hud_build: left column");
    struct yetty_ycore_void_result left_css_res =
        yetty_ygui_widget_apply_css(left_res.value, "flex-grow: 3");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, left_css_res, "hud_build: left css");
    struct yetty_yclass_object *left = left_res.value;

    struct yetty_yclass_object_ptr_result right_res =
        hud_add(split_res.value, yetty_ygui_vbox_class_get());
    YETTY_RETURN_IF_ERR(yetty_ycore_void, right_res, "hud_build: right column");
    struct yetty_ycore_void_result right_css_res =
        yetty_ygui_widget_apply_css(right_res.value, "flex-grow: 2");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, right_css_res, "hud_build: right css");
    struct yetty_yclass_object *right = right_res.value;

    struct yetty_yclass_object_ptr_result state_res = hud_add_label(left, "idle", accent);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, state_res, "hud_build: state label");
    hud->state_label = state_res.value;
    struct yetty_yclass_object_ptr_result turn_res =
        hud_add_label(left, "waiting for first turn…", secondary);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, turn_res, "hud_build: turn label");
    hud->turn_label = turn_res.value;
    struct yetty_yclass_object_ptr_result session_res = hud_add_label(left, "", secondary);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, session_res, "hud_build: session label");
    hud->session_label = session_res.value;

    struct yetty_yclass_object_ptr_result model_res = hud_add_label(right, "", accent);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, model_res, "hud_build: model label");
    hud->model_label = model_res.value;
    struct yetty_yclass_object_ptr_result stats_res = hud_add_label(right, "", secondary);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, stats_res, "hud_build: stats label");
    hud->stats_label = stats_res.value;
    struct yetty_yclass_object_ptr_result stats2_res = hud_add_label(right, "", muted);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, stats2_res, "hud_build: stats2 label");
    hud->stats2_label = stats2_res.value;

    float window_min_x = 0.0f;
    float window_min_y = 0.0f;
    float window_width = 0.0f;
    float window_height = 0.0f;
    window_rect(hud, &window_min_x, &window_min_y, &window_width, &window_height);
    ydebug("yai hud: viewport %.0fx%.0f (estimate) window (%.0f,%.0f)+%.0fx%.0f",
           hud->viewport_width, hud->viewport_height, window_min_x, window_min_y, window_width,
           window_height);
    return yai_hud_flush(hud);
}

struct yai_hud_ptr_result yai_hud_create(void)
{
    const char *no_hud = getenv("YAI_NO_HUD");
    if (no_hud && strcmp(no_hud, "0") != 0 && strcmp(no_hud, "") != 0) {
        return YETTY_OK(yai_hud_ptr, NULL); /* intentionally disabled */
    }
    if (!isatty(STDOUT_FILENO)) {
        return YETTY_OK(yai_hud_ptr, NULL); /* no pane to float over */
    }

    struct yai_hud *hud = calloc(1, sizeof(*hud));
    if (!hud) {
        return YETTY_ERR(yai_hud_ptr, "yai_hud_create: calloc");
    }
    hud->pty.ops = hud_pty_ops();
    /* Docked bottom bar by default; YAI_HUD_FLOAT restores the old
     * draggable top-right window. */
    hud->docked = getenv("YAI_HUD_FLOAT") == NULL;

    struct yetty_ygui_framework_ptr_result framework_res = yetty_ygui_framework_create(&hud->pty);
    if (YETTY_IS_ERR(framework_res)) {
        free(hud);
        return YETTY_ERR(yai_hud_ptr, "yai_hud_create: framework_create", framework_res);
    }
    hud->framework = framework_res.value;

    struct yetty_ycore_void_result build_res = hud_build(hud);
    if (YETTY_IS_ERR(build_res)) {
        /* Best-effort teardown on the failure path; the build error is
         * the one worth surfacing. */
        struct yetty_ycore_void_result destroy_res = yetty_ygui_framework_destroy(hud->framework);
        if (YETTY_IS_ERR(destroy_res)) {
            yetty_ycore_error_destroy(destroy_res.error);
        }
        free(hud);
        return YETTY_ERR(yai_hud_ptr, "yai_hud_create: build", build_res);
    }
    return YETTY_OK(yai_hud_ptr, hud);
}

struct yetty_ycore_void_result yai_hud_set_state(struct yai_hud *hud, const char *text)
{
    if (!hud) {
        return YETTY_ERR(yetty_ycore_void, "yai_hud_set_state: NULL hud");
    }
    struct yetty_ycore_void_result res = yetty_ygui_label_set_text(hud->state_label, text);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "yai_hud_set_state: set_text");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yai_hud_set_turn(struct yai_hud *hud, const char *text)
{
    if (!hud) {
        return YETTY_ERR(yetty_ycore_void, "yai_hud_set_turn: NULL hud");
    }
    struct yetty_ycore_void_result res = yetty_ygui_label_set_text(hud->turn_label, text);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "yai_hud_set_turn: set_text");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yai_hud_set_session(struct yai_hud *hud, const char *text)
{
    if (!hud) {
        return YETTY_ERR(yetty_ycore_void, "yai_hud_set_session: NULL hud");
    }
    struct yetty_ycore_void_result res = yetty_ygui_label_set_text(hud->session_label, text);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "yai_hud_set_session: set_text");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yai_hud_set_model(struct yai_hud *hud, const char *text)
{
    if (!hud) {
        return YETTY_ERR(yetty_ycore_void, "yai_hud_set_model: NULL hud");
    }
    struct yetty_ycore_void_result res = yetty_ygui_label_set_text(hud->model_label, text);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "yai_hud_set_model: set_text");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yai_hud_set_stats(struct yai_hud *hud, const char *primary,
                                                 const char *secondary)
{
    if (!hud) {
        return YETTY_ERR(yetty_ycore_void, "yai_hud_set_stats: NULL hud");
    }
    struct yetty_ycore_void_result res = yetty_ygui_label_set_text(hud->stats_label, primary);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "yai_hud_set_stats: primary");
    res = yetty_ygui_label_set_text(hud->stats2_label, secondary);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "yai_hud_set_stats: secondary");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yai_hud_flush(struct yai_hud *hud)
{
    if (!hud) {
        return YETTY_ERR(yetty_ycore_void, "yai_hud_flush: NULL hud");
    }
    if (!yetty_ygui_framework_is_dirty(hud->framework)) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result res = yetty_ygui_framework_emit(hud->framework);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "yai_hud_flush: emit");
    return YETTY_OK_VOID();
}
/*---------------------------------------------------------------------------
 * Config dialog — an EDITABLE floating panel, toggled by /config
 *---------------------------------------------------------------------------*/

/* Add one widget of `widget_class` to the dialog, hidden. Hiding (not
 * zero-sizing) keeps an unused pool entry out of BOTH the layout pass
 * (no stray vbox gap) and the paint walk (no orphan radio/checkbox
 * indicator). */
static struct yetty_yclass_object_ptr_result
hud_config_add(struct yai_hud *hud, struct yetty_yclass_ptr_result widget_class)
{
    struct yetty_yclass_object_ptr_result widget_res = hud_add(hud->config_dialog, widget_class);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, widget_res, "hud_config_add: add");
    struct yetty_ycore_void_result hide_res = yetty_ygui_widget_set_visible(widget_res.value, 0);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, hide_res, "hud_config_add: hide");
    return widget_res;
}

/* Show a config widget and set its row height. */
static struct yetty_ycore_void_result hud_config_show(struct yetty_yclass_object *widget,
                                                      float height)
{
    struct yetty_ycore_void_result visible_res = yetty_ygui_widget_set_visible(widget, 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, visible_res, "hud_config_show: visible");
    struct yetty_ycore_void_result size_res = yetty_ygui_widget_set_size(widget, 0.0f, height);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, size_res, "hud_config_show: size");
    return YETTY_OK_VOID();
}

/* Show only the widgets tagged with `active`; hide every other page's.
 * Sizes were authored at open time, so flipping `visible` is enough — a
 * restored widget keeps its height. The tabbar itself stays visible. */
static struct yetty_ycore_void_result hud_config_apply_active_tab(struct yai_hud *hud, int active)
{
    hud->config_active_tab = active;
    for (int row = 0; row < hud->config_info_used; row++) {
        struct yetty_ycore_void_result res = yetty_ygui_widget_set_visible(
            hud->config_info_rows[row], hud->config_info_tab[row] == active);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "apply_active_tab: info row");
    }
    int show_controls = (hud->config_controls_tab == active);
    struct yetty_ycore_void_result checkbox_res =
        yetty_ygui_widget_set_visible(hud->config_thinking_checkbox, show_controls);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, checkbox_res, "apply_active_tab: checkbox");
    struct yetty_ycore_void_result fold_label_res =
        yetty_ygui_widget_set_visible(hud->config_fold_label, show_controls);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, fold_label_res, "apply_active_tab: fold label");
    struct yetty_ycore_void_result slider_res =
        yetty_ygui_widget_set_visible(hud->config_fold_slider, show_controls);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, slider_res, "apply_active_tab: slider");
    for (int knob = 0; knob < YAI_HUD_CONFIG_MAX_KNOBS; knob++) {
        int show = (hud->config_knobs[knob].active_options > 0 &&
                    hud->config_knobs[knob].tab == active);
        struct yetty_ycore_void_result label_res =
            yetty_ygui_widget_set_visible(hud->config_knobs[knob].label, show);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, label_res, "apply_active_tab: knob label");
        for (int option = 0; option < YAI_HUD_CONFIG_KNOB_MAX_OPTIONS; option++) {
            int radio_show = show && option < hud->config_knobs[knob].active_options;
            struct yetty_ycore_void_result radio_res =
                yetty_ygui_widget_set_visible(hud->config_knobs[knob].radios[option], radio_show);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, radio_res, "apply_active_tab: knob radio");
        }
    }
    return YETTY_OK_VOID();
}

/* Build the dialog + its fixed widget pools once; every open fills the
 * needed entries and collapses the rest. */
static struct yetty_ycore_void_result hud_config_dialog_build(struct yai_hud *hud)
{
    struct yetty_yclass_object_ptr_result dialog_res =
        hud_add(hud->root, yetty_ygui_dialog_class_get());
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dialog_res, "config dialog: create");
    hud->config_dialog = dialog_res.value;
    struct yetty_ycore_void_result title_res =
        yetty_ygui_dialog_set_title(hud->config_dialog, "yai config");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, title_res, "config dialog: title");
    struct yetty_ycore_void_result closable_res =
        yetty_ygui_dialog_set_closable(hud->config_dialog, 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, closable_res, "config dialog: closable");

    /* Tab strip — the FIRST dialog child so it pins to the top, above
     * every page's widgets. Always visible; the pages below it toggle. */
    struct yetty_yclass_object_ptr_result tabbar_res =
        hud_add(hud->config_dialog, yetty_ygui_tabbar_class_get());
    YETTY_RETURN_IF_ERR(yetty_ycore_void, tabbar_res, "config dialog: tabbar");
    hud->config_tabbar = tabbar_res.value;
    struct yetty_ycore_void_result tabbar_size_res =
        yetty_ygui_widget_set_size(hud->config_tabbar, 0.0f, YAI_HUD_CONFIG_TABBAR_HEIGHT);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, tabbar_size_res, "config dialog: tabbar size");

    for (int row = 0; row < YAI_HUD_CONFIG_MAX_INFO_ROWS; row++) {
        struct yetty_yclass_object_ptr_result label_res =
            hud_config_add(hud, yetty_ygui_label_class_get());
        YETTY_RETURN_IF_ERR(yetty_ycore_void, label_res, "config dialog: info label");
        hud->config_info_rows[row] = label_res.value;
    }
    struct yetty_yclass_object_ptr_result checkbox_res =
        hud_config_add(hud, yetty_ygui_checkbox_class_get());
    YETTY_RETURN_IF_ERR(yetty_ycore_void, checkbox_res, "config dialog: thinking checkbox");
    hud->config_thinking_checkbox = checkbox_res.value;
    struct yetty_ycore_void_result checkbox_label_res =
        yetty_ygui_checkbox_set_label(hud->config_thinking_checkbox, "show thinking");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, checkbox_label_res, "config dialog: checkbox label");

    struct yetty_yclass_object_ptr_result fold_label_res =
        hud_config_add(hud, yetty_ygui_label_class_get());
    YETTY_RETURN_IF_ERR(yetty_ycore_void, fold_label_res, "config dialog: fold label");
    hud->config_fold_label = fold_label_res.value;
    struct yetty_yclass_object_ptr_result slider_res =
        hud_config_add(hud, yetty_ygui_slider_class_get());
    YETTY_RETURN_IF_ERR(yetty_ycore_void, slider_res, "config dialog: fold slider");
    hud->config_fold_slider = slider_res.value;
    struct yetty_ycore_void_result range_res = yetty_ygui_slider_set_range(
        hud->config_fold_slider, YAI_HUD_CONFIG_FOLD_MIN, YAI_HUD_CONFIG_FOLD_MAX);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, range_res, "config dialog: fold range");

    for (int knob = 0; knob < YAI_HUD_CONFIG_MAX_KNOBS; knob++) {
        struct yetty_yclass_object_ptr_result knob_label_res =
            hud_config_add(hud, yetty_ygui_label_class_get());
        YETTY_RETURN_IF_ERR(yetty_ycore_void, knob_label_res, "config dialog: knob label");
        hud->config_knobs[knob].label = knob_label_res.value;
        for (int option = 0; option < YAI_HUD_CONFIG_KNOB_MAX_OPTIONS; option++) {
            struct yetty_yclass_object_ptr_result radio_res =
                hud_config_add(hud, yetty_ygui_radio_class_get());
            YETTY_RETURN_IF_ERR(yetty_ycore_void, radio_res, "config dialog: knob radio");
            hud->config_knobs[knob].radios[option] = radio_res.value;
        }
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yai_hud_toggle_config(struct yai_hud *hud,
                                                     const struct yai_hud_config_setup *setup)
{
    if (!hud) {
        return YETTY_ERR(yetty_ycore_void, "yai_hud_toggle_config: NULL hud");
    }
    if (!setup || setup->tab_count <= 0) {
        return YETTY_ERR(yetty_ycore_void, "yai_hud_toggle_config: NULL/empty setup");
    }
    if (hud->config_dialog) {
        struct yetty_ycore_int_result open_res = yetty_ygui_dialog_is_open(hud->config_dialog);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, open_res, "yai_hud_toggle_config: is_open");
        if (open_res.value) {
            struct yetty_ycore_void_result close_res =
                yetty_ygui_dialog_close(hud->config_dialog);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, close_res, "yai_hud_toggle_config: close");
            yetty_ygui_framework_mark_dirty(hud->framework);
            struct yetty_ycore_void_result flush_res = yai_hud_flush(hud);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "yai_hud_toggle_config: flush");
            return YETTY_OK_VOID();
        }
    } else {
        struct yetty_ycore_void_result build_res = hud_config_dialog_build(hud);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, build_res, "yai_hud_toggle_config: build");
    }

    struct yetty_ycore_rgba accent = {.r = 107, .g = 168, .b = 146, .a = 255};
    struct yetty_ycore_rgba secondary = {.r = 159, .g = 167, .b = 168, .a = 255};

    int tab_count = setup->tab_count;
    if (tab_count > YAI_HUD_CONFIG_TAB_MAX) {
        tab_count = YAI_HUD_CONFIG_TAB_MAX;
    }
    hud->config_tab_count = tab_count;

    /* Sync the tab strip to the requested titles (the tabbar persists
     * across opens; the page set can change when the engine changes). */
    int have_tabs = yetty_ygui_tabbar_count(hud->config_tabbar);
    while (have_tabs > tab_count) {
        struct yetty_ycore_void_result rm =
            yetty_ygui_tabbar_remove_tab(hud->config_tabbar, have_tabs - 1);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rm, "yai_hud_toggle_config: remove tab");
        have_tabs--;
    }
    while (have_tabs < tab_count) {
        struct yetty_yclass_object_ptr_result add =
            yetty_ygui_tabbar_add_tab(hud->config_tabbar, "");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, add, "yai_hud_toggle_config: add tab");
        have_tabs++;
    }
    for (int tab = 0; tab < tab_count; tab++) {
        const char *title = setup->tab_titles[tab] ? setup->tab_titles[tab] : "";
        struct yetty_ycore_void_result lbl =
            yetty_ygui_tabbar_set_label(hud->config_tabbar, tab, title);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, lbl, "yai_hud_toggle_config: tab label");
    }
    struct yetty_ycore_void_result active_res = yetty_ygui_tabbar_set_active(hud->config_tabbar, 0);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, active_res, "yai_hud_toggle_config: set active");

    /* Content height per tab (only one page is visible at a time, but the
     * dialog is sized to the tallest so a tab switch never resizes it). */
    float tab_content[YAI_HUD_CONFIG_TAB_MAX] = {0};

    /* 1. Read-only info rows — one shared pool, each row tagged with its
     * tab. "## " lines are accent section headers. */
    int used_rows = 0;
    for (int tab = 0; tab < tab_count; tab++) {
        const char *cursor = setup->tab_info[tab];
        while (cursor && *cursor && used_rows < YAI_HUD_CONFIG_MAX_INFO_ROWS) {
            const char *line_end = strchr(cursor, '\n');
            size_t line_len = line_end ? (size_t)(line_end - cursor) : strlen(cursor);
            int is_header = line_len > 3 && strncmp(cursor, "## ", 3) == 0;
            const char *body = is_header ? cursor + 3 : cursor;
            size_t body_len = is_header ? line_len - 3 : line_len;
            char row_text[192];
            if (body_len >= sizeof(row_text)) {
                body_len = sizeof(row_text) - 1;
            }
            memcpy(row_text, body, body_len);
            row_text[body_len] = '\0';
            struct yetty_yclass_object *row_label = hud->config_info_rows[used_rows];
            struct yetty_ycore_void_result text_res =
                yetty_ygui_label_set_text(row_label, row_text);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, text_res, "yai_hud_toggle_config: row text");
            struct yetty_ycore_void_result color_res =
                yetty_ygui_label_set_color(row_label, is_header ? accent : secondary);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, color_res, "yai_hud_toggle_config: row color");
            struct yetty_ycore_void_result show_res =
                hud_config_show(row_label, YAI_HUD_CONFIG_INFO_HEIGHT);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, show_res, "yai_hud_toggle_config: row show");
            hud->config_info_tab[used_rows] = tab;
            tab_content[tab] += YAI_HUD_CONFIG_INFO_HEIGHT + YAI_HUD_CONFIG_GAP;
            used_rows++;
            if (!line_end) {
                break;
            }
            cursor = line_end + 1;
        }
    }
    hud->config_info_used = used_rows;
    for (int row = used_rows; row < YAI_HUD_CONFIG_MAX_INFO_ROWS; row++) {
        struct yetty_ycore_void_result hide_res =
            yetty_ygui_widget_set_visible(hud->config_info_rows[row], 0);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hide_res, "yai_hud_toggle_config: hide row");
    }

    /* 2. yai controls (show-thinking checkbox + fold-lines slider) on the
     * controls tab. */
    hud->config_controls_tab = setup->controls_tab;
    if (setup->controls_tab >= 0 && setup->controls_tab < tab_count) {
        int controls_tab = setup->controls_tab;
        struct yetty_ycore_void_result checked_res =
            yetty_ygui_checkbox_set_checked(hud->config_thinking_checkbox, setup->show_thinking);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, checked_res, "yai_hud_toggle_config: checkbox state");
        struct yetty_ycore_void_result checkbox_show_res =
            hud_config_show(hud->config_thinking_checkbox, YAI_HUD_CONFIG_CONTROL_HEIGHT);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, checkbox_show_res, "yai_hud_toggle_config: checkbox");

        char fold_text[64];
        snprintf(fold_text, sizeof(fold_text), "fold lines: %d", (int)(setup->fold_lines + 0.5f));
        struct yetty_ycore_void_result fold_text_res =
            yetty_ygui_label_set_text(hud->config_fold_label, fold_text);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, fold_text_res, "yai_hud_toggle_config: fold text");
        struct yetty_ycore_void_result fold_color_res =
            yetty_ygui_label_set_color(hud->config_fold_label, secondary);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, fold_color_res, "yai_hud_toggle_config: fold color");
        struct yetty_ycore_void_result fold_show_res =
            hud_config_show(hud->config_fold_label, YAI_HUD_CONFIG_INFO_HEIGHT);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, fold_show_res, "yai_hud_toggle_config: fold show");
        struct yetty_ycore_void_result slider_value_res =
            yetty_ygui_slider_set_value(hud->config_fold_slider, setup->fold_lines);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, slider_value_res,
                            "yai_hud_toggle_config: slider value");
        struct yetty_ycore_void_result slider_show_res =
            hud_config_show(hud->config_fold_slider, YAI_HUD_CONFIG_CONTROL_HEIGHT);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, slider_show_res, "yai_hud_toggle_config: slider show");
        tab_content[controls_tab] += YAI_HUD_CONFIG_CONTROL_HEIGHT + YAI_HUD_CONFIG_GAP;
        tab_content[controls_tab] += YAI_HUD_CONFIG_INFO_HEIGHT + YAI_HUD_CONFIG_GAP;
        tab_content[controls_tab] += YAI_HUD_CONFIG_CONTROL_HEIGHT + YAI_HUD_CONFIG_GAP;
    }

    /* 3. Knob radio groups (edit-mode, engine knob, model), each on its
     * own tab. */
    int knob_count = setup->knob_count;
    if (knob_count > YAI_HUD_CONFIG_MAX_KNOBS) {
        knob_count = YAI_HUD_CONFIG_MAX_KNOBS;
    }
    hud->config_knob_count = knob_count;
    for (int knob = 0; knob < YAI_HUD_CONFIG_MAX_KNOBS; knob++) {
        const struct yai_hud_config_knob *spec = &setup->knobs[knob];
        int knob_tab = spec->tab;
        int active = (knob < knob_count && spec->label && spec->option_count > 0 && knob_tab >= 0 &&
                      knob_tab < tab_count)
                         ? spec->option_count
                         : 0;
        if (active > YAI_HUD_CONFIG_KNOB_MAX_OPTIONS) {
            active = YAI_HUD_CONFIG_KNOB_MAX_OPTIONS;
        }
        hud->config_knobs[knob].tab = knob_tab;
        if (active > 0) {
            struct yetty_ycore_void_result knob_text_res =
                yetty_ygui_label_set_text(hud->config_knobs[knob].label, spec->label);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, knob_text_res, "yai_hud_toggle_config: knob text");
            struct yetty_ycore_void_result knob_color_res =
                yetty_ygui_label_set_color(hud->config_knobs[knob].label, accent);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, knob_color_res,
                                "yai_hud_toggle_config: knob color");
            struct yetty_ycore_void_result knob_show_res =
                hud_config_show(hud->config_knobs[knob].label, YAI_HUD_CONFIG_INFO_HEIGHT);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, knob_show_res, "yai_hud_toggle_config: knob show");
            tab_content[knob_tab] += YAI_HUD_CONFIG_INFO_HEIGHT + YAI_HUD_CONFIG_GAP;
            for (int option = 0; option < active; option++) {
                struct yetty_yclass_object *radio = hud->config_knobs[knob].radios[option];
                struct yetty_ycore_void_result radio_label_res =
                    yetty_ygui_radio_set_label(radio, spec->options[option]);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, radio_label_res,
                                    "yai_hud_toggle_config: radio label");
                struct yetty_ycore_void_result radio_state_res =
                    yetty_ygui_radio_set_selected(radio, option == spec->selected);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, radio_state_res,
                                    "yai_hud_toggle_config: radio state");
                struct yetty_ycore_void_result radio_show_res =
                    hud_config_show(radio, YAI_HUD_CONFIG_CONTROL_HEIGHT);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, radio_show_res,
                                    "yai_hud_toggle_config: radio show");
                tab_content[knob_tab] += YAI_HUD_CONFIG_CONTROL_HEIGHT + YAI_HUD_CONFIG_GAP;
            }
        } else {
            struct yetty_ycore_void_result knob_hide_res =
                yetty_ygui_widget_set_visible(hud->config_knobs[knob].label, 0);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, knob_hide_res,
                                "yai_hud_toggle_config: hide knob");
        }
        hud->config_knobs[knob].active_options = active;
        hud->config_knobs[knob].last_selected = active > 0 ? spec->selected : -1;
        for (int option = active; option < YAI_HUD_CONFIG_KNOB_MAX_OPTIONS; option++) {
            struct yetty_ycore_void_result radio_hide_res =
                yetty_ygui_widget_set_visible(hud->config_knobs[knob].radios[option], 0);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, radio_hide_res,
                                "yai_hud_toggle_config: hide radio");
        }
    }

    /* Show tab 0; every other page stays built but hidden. */
    struct yetty_ycore_void_result tab_apply_res = hud_config_apply_active_tab(hud, 0);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, tab_apply_res, "yai_hud_toggle_config: apply tab");

    /* Open centered, sized to the tallest page; never off-viewport. */
    float max_content = 0.0f;
    for (int tab = 0; tab < tab_count; tab++) {
        if (tab_content[tab] > max_content) {
            max_content = tab_content[tab];
        }
    }
    float dialog_height = YAI_HUD_CONFIG_TOP_PAD + YAI_HUD_CONFIG_TABBAR_HEIGHT +
                          YAI_HUD_CONFIG_GAP + max_content - YAI_HUD_CONFIG_GAP +
                          YAI_HUD_CONFIG_BOTTOM_PAD;
    float dialog_x = (hud->viewport_width - YAI_HUD_CONFIG_WIDTH) / 2.0f;
    float dialog_y = (hud->viewport_height - dialog_height) / 2.0f;
    if (dialog_x < 0.0f) {
        dialog_x = 0.0f;
    }
    if (dialog_y < 0.0f) {
        dialog_y = 0.0f;
    }
    struct yetty_ycore_void_result open_res = yetty_ygui_dialog_open_at(
        hud->config_dialog, dialog_x, dialog_y, YAI_HUD_CONFIG_WIDTH, dialog_height);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, open_res, "yai_hud_toggle_config: open");
    yetty_ygui_framework_mark_dirty(hud->framework);
    struct yetty_ycore_void_result flush_res = yai_hud_flush(hud);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "yai_hud_toggle_config: flush");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yai_hud_config_poll(struct yai_hud *hud,
                                                   struct yai_hud_config_values *out)
{
    if (!hud || !out) {
        return YETTY_ERR(yetty_ycore_void, "yai_hud_config_poll: NULL argument");
    }
    memset(out, 0, sizeof(*out));
    for (int knob = 0; knob < YAI_HUD_CONFIG_MAX_KNOBS; knob++) {
        out->knob_selected[knob] = -1;
    }
    if (!hud->config_dialog) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_int_result open_res = yetty_ygui_dialog_is_open(hud->config_dialog);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, open_res, "yai_hud_config_poll: is_open");
    if (!open_res.value) {
        return YETTY_OK_VOID();
    }
    out->open = 1;

    /* Tab strip: if the user clicked a different tab, swap the visible
     * page. The widget state of the now-hidden page is retained. */
    struct yetty_ycore_int_result active_res = yetty_ygui_tabbar_active(hud->config_tabbar);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, active_res, "yai_hud_config_poll: tabbar active");
    int active_tab = active_res.value;
    if (active_tab >= 0 && active_tab < hud->config_tab_count &&
        active_tab != hud->config_active_tab) {
        struct yetty_ycore_void_result apply_res = hud_config_apply_active_tab(hud, active_tab);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, apply_res, "yai_hud_config_poll: switch tab");
    }
    out->active_tab = hud->config_active_tab;

    struct yetty_ycore_int_result checked_res =
        yetty_ygui_checkbox_get_checked(hud->config_thinking_checkbox);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, checked_res, "yai_hud_config_poll: checkbox");
    out->show_thinking = checked_res.value;

    struct yetty_ycore_float_result fold_res =
        yetty_ygui_slider_get_value(hud->config_fold_slider);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, fold_res, "yai_hud_config_poll: slider");
    out->fold_lines = fold_res.value;
    char fold_text[64];
    snprintf(fold_text, sizeof(fold_text), "fold lines: %d", (int)(fold_res.value + 0.5f));
    struct yetty_ycore_void_result fold_text_res =
        yetty_ygui_label_set_text(hud->config_fold_label, fold_text);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, fold_text_res, "yai_hud_config_poll: fold text");

    out->knob_count = hud->config_knob_count;
    for (int knob = 0; knob < YAI_HUD_CONFIG_MAX_KNOBS; knob++) {
        out->knob_selected[knob] = -1;
        int active = hud->config_knobs[knob].active_options;
        if (active <= 0) {
            continue;
        }
        /* A radio click only SELECTS — exclusivity is enforced here:
         * the newly selected option (any selected one that is not the
         * previous anchor) wins; everything else is cleared. */
        int chosen = hud->config_knobs[knob].last_selected;
        for (int option = 0; option < active; option++) {
            struct yetty_ycore_int_result selected_res =
                yetty_ygui_radio_is_selected(hud->config_knobs[knob].radios[option]);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, selected_res, "yai_hud_config_poll: radio");
            if (selected_res.value && option != hud->config_knobs[knob].last_selected) {
                chosen = option;
            }
        }
        for (int option = 0; option < active; option++) {
            struct yetty_ycore_void_result state_res = yetty_ygui_radio_set_selected(
                hud->config_knobs[knob].radios[option], option == chosen);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, state_res,
                                "yai_hud_config_poll: radio exclusivity");
        }
        hud->config_knobs[knob].last_selected = chosen;
        out->knob_selected[knob] = chosen;
    }
    yetty_ygui_framework_mark_dirty(hud->framework);
    struct yetty_ycore_void_result flush_res = yai_hud_flush(hud);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "yai_hud_config_poll: flush");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yai_hud_set_viewport(struct yai_hud *hud, float width, float height)
{
    if (!hud) {
        return YETTY_ERR(yetty_ycore_void, "yai_hud_set_viewport: NULL hud");
    }
    if (width <= 0.0f || height <= 0.0f) {
        return YETTY_ERR(yetty_ycore_void, "yai_hud_set_viewport: non-positive size");
    }
    hud->viewport_width = width;
    hud->viewport_height = height;
    hud->viewport_from_host = 1;
    ydebug("yai hud: viewport %.0fx%.0f (host)", width, height);
    struct yetty_ycore_void_result res = hud_apply_viewport(hud);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "yai_hud_set_viewport: apply");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yai_hud_viewport_changed(struct yai_hud *hud)
{
    if (!hud) {
        return YETTY_ERR(yetty_ycore_void, "yai_hud_viewport_changed: NULL hud");
    }
    if (hud->viewport_from_host) {
        /* The host re-sends its resize envelope on every pane resize —
         * that cue carries real pixels; the tty winsize often doesn't. */
        return YETTY_OK_VOID();
    }
    terminal_pixels(&hud->viewport_width, &hud->viewport_height);
    struct yetty_ycore_void_result res = hud_apply_viewport(hud);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "yai_hud_viewport_changed: apply");
    return YETTY_OK_VOID();
}

/*---------------------------------------------------------------------------
 * Client-side input
 *---------------------------------------------------------------------------*/

static int point_in_window(const struct yai_hud *hud, float x, float y)
{
    float min_x = 0.0f;
    float min_y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    window_rect(hud, &min_x, &min_y, &width, &height);
    return x >= min_x && x <= min_x + width && y >= min_y && y <= min_y + height;
}

/* Inside the config dialog's rect while it is open. */
static struct yetty_ycore_int_result point_in_config_dialog(const struct yai_hud *hud, float x,
                                                            float y)
{
    if (!hud->config_dialog) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    struct yetty_ycore_int_result open_res = yetty_ygui_dialog_is_open(hud->config_dialog);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, open_res, "point_in_config_dialog: is_open");
    if (!open_res.value) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    const struct yetty_ygui_layout *layout = yetty_ygui_widget_layout_get(hud->config_dialog);
    int inside = x >= layout->pos_x && x <= layout->pos_x + layout->width && y >= layout->pos_y &&
                 y <= layout->pos_y + layout->height;
    return YETTY_OK(yetty_ycore_int, inside);
}

/* The config dialog's titlebar ✕ close button (top-right
 * YAI_HUD_CONFIG_CLOSE_W square). Value 1 when (x, y) is on it. */
static struct yetty_ycore_int_result point_in_config_close(const struct yai_hud *hud, float x,
                                                           float y)
{
    if (!hud->config_dialog) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    struct yetty_ycore_int_result open_res = yetty_ygui_dialog_is_open(hud->config_dialog);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, open_res, "point_in_config_close: is_open");
    if (!open_res.value) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    const struct yetty_ygui_layout *layout = yetty_ygui_widget_layout_get(hud->config_dialog);
    int inside = x >= layout->pos_x + layout->width - YAI_HUD_CONFIG_CLOSE_W &&
                 x <= layout->pos_x + layout->width && y >= layout->pos_y &&
                 y <= layout->pos_y + YAI_HUD_CONFIG_TITLE_H;
    return YETTY_OK(yetty_ycore_int, inside);
}

/* The config dialog's title strip (top YAI_HUD_CONFIG_TITLE_H px), used
 * to start a client-side drag. Value 1 when (x, y) is on it. */
static struct yetty_ycore_int_result point_in_config_titlebar(const struct yai_hud *hud, float x,
                                                              float y)
{
    if (!hud->config_dialog) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    struct yetty_ycore_int_result open_res = yetty_ygui_dialog_is_open(hud->config_dialog);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, open_res, "point_in_config_titlebar: is_open");
    if (!open_res.value) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    const struct yetty_ygui_layout *layout = yetty_ygui_widget_layout_get(hud->config_dialog);
    int inside = x >= layout->pos_x && x <= layout->pos_x + layout->width && y >= layout->pos_y &&
                 y <= layout->pos_y + YAI_HUD_CONFIG_TITLE_H;
    return YETTY_OK(yetty_ycore_int, inside);
}

static int point_in_grip(const struct yai_hud *hud, float x, float y)
{
    float min_x = 0.0f;
    float min_y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    window_rect(hud, &min_x, &min_y, &width, &height);
    return x >= min_x + width - YAI_HUD_GRIP_HIT && x <= min_x + width &&
           y >= min_y + height - YAI_HUD_GRIP_HIT && y <= min_y + height;
}

struct yetty_ycore_int_result yai_hud_mouse_button(struct yai_hud *hud, float x, float y,
                                                   int button, int pressed)
{
    if (!hud) {
        return YETTY_ERR(yetty_ycore_int, "yai_hud_mouse_button: NULL hud");
    }
    if (pressed && button == 0 && !hud->docked && point_in_grip(hud, x, y)) {
        /* Corner grip: start the yai-side resize drag; the framework
         * never sees this press. (Disabled when docked — the bar's
         * size is fixed.) */
        float min_x = 0.0f;
        float min_y = 0.0f;
        window_rect(hud, &min_x, &min_y, &hud->resize_start_width, &hud->resize_start_height);
        hud->resizing = 1;
        hud->user_touched = 1;
        hud->resize_cursor_x = x;
        hud->resize_cursor_y = y;
        return YETTY_OK(yetty_ycore_int, 1);
    }
    if (!pressed && hud->resizing) {
        hud->resizing = 0;
        return YETTY_OK(yetty_ycore_int, 1);
    }
    if (pressed && button == 0) {
        /* Config dialog ✕ close button: close on press, consume it. */
        struct yetty_ycore_int_result close_res = point_in_config_close(hud, x, y);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, close_res, "yai_hud_mouse_button: close hit");
        if (close_res.value) {
            struct yetty_ycore_void_result do_close = yetty_ygui_dialog_close(hud->config_dialog);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, do_close, "yai_hud_mouse_button: dialog close");
            yetty_ygui_framework_mark_dirty(hud->framework);
            struct yetty_ycore_void_result flush_res = yai_hud_flush(hud);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, flush_res, "yai_hud_mouse_button: close flush");
            return YETTY_OK(yetty_ycore_int, 1);
        }
        /* Config dialog titlebar: start a client-side drag (the dialog
         * widget has no built-in drag); the framework never sees it. */
        struct yetty_ycore_int_result titlebar_res = point_in_config_titlebar(hud, x, y);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, titlebar_res, "yai_hud_mouse_button: titlebar hit");
        if (titlebar_res.value) {
            const struct yetty_ygui_layout *layout =
                yetty_ygui_widget_layout_get(hud->config_dialog);
            hud->config_dragging = 1;
            hud->config_drag_pos_x = layout->pos_x;
            hud->config_drag_pos_y = layout->pos_y;
            hud->config_drag_cursor_x = x;
            hud->config_drag_cursor_y = y;
            return YETTY_OK(yetty_ycore_int, 1);
        }
    }
    if (!pressed && hud->config_dragging) {
        hud->config_dragging = 0;
        return YETTY_OK(yetty_ycore_int, 1);
    }
    int inside = point_in_window(hud, x, y);
    if (!inside) {
        struct yetty_ycore_int_result dialog_hit_res = point_in_config_dialog(hud, x, y);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, dialog_hit_res, "yai_hud_mouse_button: dialog hit");
        inside = dialog_hit_res.value;
    }
    struct yetty_ycore_int_result consumed_res =
        yetty_ygui_framework_feed_mouse_button(hud->framework, x, y, button, pressed, 0);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, consumed_res, "yai_hud_mouse_button: feed");
    struct yetty_ycore_void_result flush_res = yai_hud_flush(hud);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, flush_res, "yai_hud_mouse_button: flush");
    /* Focus is geometric, not widget-based: any press inside the window
     * rect keeps the GUI focused even if no interactive widget consumed
     * it (e.g. a click on the body background). */
    ydebug("yai hud: button=%d pressed=%d at (%.0f,%.0f) inside=%d consumed=%d", button, pressed, x,
           y, inside, consumed_res.value);
    if (pressed && inside) {
        /* A press in the window (titlebar drag included) pins the
         * user-chosen placement: viewport changes stop re-placing it. */
        hud->user_touched = 1;
    }
    return YETTY_OK(yetty_ycore_int, inside || consumed_res.value);
}

struct yetty_ycore_void_result yai_hud_mouse_motion(struct yai_hud *hud, float x, float y)
{
    if (!hud) {
        return YETTY_ERR(yetty_ycore_void, "yai_hud_mouse_motion: NULL hud");
    }
    if (hud->resizing) {
        float new_width = hud->resize_start_width + (x - hud->resize_cursor_x);
        float new_height = hud->resize_start_height + (y - hud->resize_cursor_y);
        if (new_width < YAI_HUD_WINDOW_MIN_WIDTH) {
            new_width = YAI_HUD_WINDOW_MIN_WIDTH;
        }
        if (new_height < YAI_HUD_WINDOW_MIN_HEIGHT) {
            new_height = YAI_HUD_WINDOW_MIN_HEIGHT;
        }
        struct yetty_ycore_void_result size_res =
            yetty_ygui_widget_set_size(hud->window, new_width, new_height);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, size_res, "yai_hud_mouse_motion: resize");
        return yai_hud_flush(hud);
    }
    if (hud->config_dragging) {
        float new_x = hud->config_drag_pos_x + (x - hud->config_drag_cursor_x);
        float new_y = hud->config_drag_pos_y + (y - hud->config_drag_cursor_y);
        /* Keep the titlebar reachable: clamp into the viewport. */
        const struct yetty_ygui_layout *layout = yetty_ygui_widget_layout_get(hud->config_dialog);
        if (new_x > hud->viewport_width - 48.0f) {
            new_x = hud->viewport_width - 48.0f;
        }
        if (new_y > hud->viewport_height - YAI_HUD_CONFIG_TITLE_H) {
            new_y = hud->viewport_height - YAI_HUD_CONFIG_TITLE_H;
        }
        if (new_x < 48.0f - layout->width) {
            new_x = 48.0f - layout->width;
        }
        if (new_y < 0.0f) {
            new_y = 0.0f;
        }
        struct yetty_ycore_void_result move_res =
            yetty_ygui_widget_set_position(hud->config_dialog, new_x, new_y);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, move_res, "yai_hud_mouse_motion: dialog move");
        return yai_hud_flush(hud);
    }
    struct yetty_ycore_int_result motion_res =
        yetty_ygui_framework_feed_mouse_motion(hud->framework, x, y);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, motion_res, "yai_hud_mouse_motion: feed");
    return yai_hud_flush(hud);
}

struct yetty_ycore_void_result yai_hud_mouse_wheel(struct yai_hud *hud, float x, float y,
                                                   float delta_y)
{
    if (!hud) {
        return YETTY_ERR(yetty_ycore_void, "yai_hud_mouse_wheel: NULL hud");
    }
    struct yetty_ycore_void_result scroll_res =
        yetty_ygui_framework_feed_mouse_scroll(hud->framework, x, y, 0.0f, delta_y);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, scroll_res, "yai_hud_mouse_wheel: feed");
    return yai_hud_flush(hud);
}

struct yetty_ycore_int_result yai_hud_contains_point(struct yai_hud *hud, float x, float y)
{
    if (!hud) {
        return YETTY_ERR(yetty_ycore_int, "yai_hud_contains_point: NULL hud");
    }
    if (point_in_window(hud, x, y)) {
        return YETTY_OK(yetty_ycore_int, 1);
    }
    struct yetty_ycore_int_result dialog_hit_res = point_in_config_dialog(hud, x, y);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, dialog_hit_res, "yai_hud_contains_point: dialog hit");
    return dialog_hit_res;
}

struct yetty_ycore_void_result yai_hud_feed_keys(struct yai_hud *hud, const char *bytes, size_t len)
{
    if (!hud) {
        return YETTY_ERR(yetty_ycore_void, "yai_hud_feed_keys: NULL hud");
    }
    if (len == 0) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result feed_res =
        yetty_ygui_framework_feed_input(hud->framework, bytes, len);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, feed_res, "yai_hud_feed_keys: feed");
    return yai_hud_flush(hud);
}

struct yetty_ycore_void_result yai_hud_destroy(struct yai_hud *hud)
{
    if (!hud) {
        return YETTY_ERR(yetty_ycore_void, "yai_hud_destroy: NULL hud");
    }
    /* Best-effort: run every teardown step, accumulate failures. */
    struct yetty_ycore_void_result teardown = yai_render_flush_stdout();
    teardown = yetty_ycore_void_chain(
        teardown, yetty_ygui_framework_clear_remote_fd(hud->framework, STDOUT_FILENO));
    teardown = yetty_ycore_void_chain(teardown, yetty_ygui_framework_destroy(hud->framework));
    free(hud);
    return teardown;
}
