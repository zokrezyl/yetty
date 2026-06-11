/*
 * hud.c — ccc's non-scrolling ygui status window.
 */
#include "hud.h"

#include <yetty/ygui/framework.h>
#include <yetty/ygui/widget.h>
#include <yetty/ygui/widgets/label.h>
#include <yetty/ygui/widgets/vbox.h>
#include <yetty/ygui/widgets/window.h>
#include <yetty/yplatform/pty.h>

#include <yetty/ytrace/ytrace.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define CCC_HUD_WINDOW_WIDTH 380.0f
#define CCC_HUD_WINDOW_HEIGHT 128.0f
#define CCC_HUD_WINDOW_MIN_WIDTH 220.0f
#define CCC_HUD_WINDOW_MIN_HEIGHT 96.0f
#define CCC_HUD_MARGIN 16.0f
#define CCC_HUD_LABEL_HEIGHT 18.0f
/* Bottom-right corner square that starts a ccc-side resize drag. */
#define CCC_HUD_GRIP_HIT 18.0f

/*---------------------------------------------------------------------------
 * Blocking stdout pty shim. The framework writes its compositor envelope
 * through this; stdout is fflushed first, so envelope bytes can never be
 * interleaved with buffered text (ccc is the single PTY writer).
 *---------------------------------------------------------------------------*/

static struct yetty_ycore_size_result hud_pty_write(struct yetty_platform_pty *self,
                                                    const char *data, size_t len)
{
    (void)self;
    fflush(stdout);
    size_t written = 0;
    while (written < len) {
        ssize_t chunk = write(STDOUT_FILENO, data + written, len - written);
        if (chunk < 0) {
            if (errno == EINTR) {
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

struct ccc_hud {
    struct yetty_platform_pty pty;
    struct yetty_ygui_framework *framework;
    struct yetty_yclass_object *root;
    struct yetty_yclass_object *window;
    struct yetty_yclass_object *state_label;
    struct yetty_yclass_object *turn_label;
    struct yetty_yclass_object *session_label;

    /* ccc-side corner-resize drag state (the window widget on this
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
 * updates pos, ccc-side resize updates size). */
static void window_rect(const struct ccc_hud *hud, float *min_x, float *min_y, float *width,
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

static struct yetty_yclass_object_ptr_result hud_add_label(struct ccc_hud *hud,
                                                         const char *initial_text,
                                                         struct yetty_ycore_rgba color)
{
    struct yetty_yclass_object *body = yetty_ygui_window_body(hud->window);
    if (!body) {
        return YETTY_ERR(yetty_yclass_object_ptr, "hud_add_label: window has no body");
    }
    struct yetty_yclass_object_ptr_result label_res = hud_add(body, yetty_ygui_label_class_get());
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, label_res, "hud_add_label: add label");
    struct yetty_yclass_object *label = label_res.value;

    struct yetty_ycore_void_result size_res =
        yetty_ygui_widget_set_size(label, 0.0f, CCC_HUD_LABEL_HEIGHT);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, size_res, "hud_add_label: set_size");
    struct yetty_ycore_void_result text_res = yetty_ygui_label_set_text(label, initial_text);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, text_res, "hud_add_label: set_text");
    struct yetty_ycore_void_result color_res = yetty_ygui_label_set_color(label, color);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, color_res, "hud_add_label: set_color");
    return YETTY_OK(yetty_yclass_object_ptr, label);
}

/* Apply the current viewport: framework layout space + window
 * placement. Top-right until the user has moved/resized the window;
 * afterwards only clamp the titlebar back into reach. */
static struct yetty_ycore_void_result hud_apply_viewport(struct ccc_hud *hud)
{
    struct yetty_ycore_void_result viewport_res = yetty_ygui_framework_set_viewport(
        hud->framework, hud->viewport_width, hud->viewport_height);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, viewport_res, "hud_apply_viewport: set_viewport");

    float min_x = 0.0f;
    float min_y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    window_rect(hud, &min_x, &min_y, &width, &height);

    float target_x = min_x;
    float target_y = min_y;
    if (!hud->user_touched) {
        target_x = hud->viewport_width - width - CCC_HUD_MARGIN;
        target_y = CCC_HUD_MARGIN;
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
    return ccc_hud_flush(hud);
}

/* Build the widget tree + initial geometry. Split out so create() can
 * tear the framework down on any failure without repeating cleanup. */
static struct yetty_ycore_void_result hud_build(struct ccc_hud *hud)
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
    struct yetty_ycore_void_result title_res = yetty_ygui_window_set_title(hud->window, "ccc");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, title_res, "hud_build: set_title");

    /* TIOCGWINSZ estimate only — the host's resize envelope replaces it
     * with the authoritative pixel size right after the mouse subscribe. */
    terminal_pixels(&hud->viewport_width, &hud->viewport_height);
    struct yetty_ycore_void_result size_res =
        yetty_ygui_widget_set_size(hud->window, CCC_HUD_WINDOW_WIDTH, CCC_HUD_WINDOW_HEIGHT);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, size_res, "hud_build: set_size");
    struct yetty_ycore_void_result place_res = hud_apply_viewport(hud);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, place_res, "hud_build: apply_viewport");

    struct yetty_ycore_rgba accent = {.r = 107, .g = 168, .b = 146, .a = 255};
    struct yetty_ycore_rgba secondary = {.r = 159, .g = 167, .b = 168, .a = 255};
    struct yetty_yclass_object_ptr_result state_res = hud_add_label(hud, "idle", accent);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, state_res, "hud_build: state label");
    hud->state_label = state_res.value;
    struct yetty_yclass_object_ptr_result turn_res =
        hud_add_label(hud, "waiting for first turn…", secondary);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, turn_res, "hud_build: turn label");
    hud->turn_label = turn_res.value;
    struct yetty_yclass_object_ptr_result session_res = hud_add_label(hud, "", secondary);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, session_res, "hud_build: session label");
    hud->session_label = session_res.value;

    float window_min_x = 0.0f;
    float window_min_y = 0.0f;
    float window_width = 0.0f;
    float window_height = 0.0f;
    window_rect(hud, &window_min_x, &window_min_y, &window_width, &window_height);
    ydebug("ccc hud: viewport %.0fx%.0f (estimate) window (%.0f,%.0f)+%.0fx%.0f",
           hud->viewport_width, hud->viewport_height, window_min_x, window_min_y, window_width,
           window_height);
    return ccc_hud_flush(hud);
}

struct ccc_hud_ptr_result ccc_hud_create(void)
{
    const char *no_hud = getenv("CCC_NO_HUD");
    if (no_hud && strcmp(no_hud, "0") != 0 && strcmp(no_hud, "") != 0) {
        return YETTY_OK(ccc_hud_ptr, NULL); /* intentionally disabled */
    }
    if (!isatty(STDOUT_FILENO)) {
        return YETTY_OK(ccc_hud_ptr, NULL); /* no pane to float over */
    }

    struct ccc_hud *hud = calloc(1, sizeof(*hud));
    if (!hud) {
        return YETTY_ERR(ccc_hud_ptr, "ccc_hud_create: calloc");
    }
    hud->pty.ops = hud_pty_ops();

    struct yetty_ygui_framework_ptr_result framework_res = yetty_ygui_framework_create(&hud->pty);
    if (YETTY_IS_ERR(framework_res)) {
        free(hud);
        return YETTY_ERR(ccc_hud_ptr, "ccc_hud_create: framework_create", framework_res);
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
        return YETTY_ERR(ccc_hud_ptr, "ccc_hud_create: build", build_res);
    }
    return YETTY_OK(ccc_hud_ptr, hud);
}

struct yetty_ycore_void_result ccc_hud_set_state(struct ccc_hud *hud, const char *text)
{
    if (!hud) {
        return YETTY_ERR(yetty_ycore_void, "ccc_hud_set_state: NULL hud");
    }
    struct yetty_ycore_void_result res = yetty_ygui_label_set_text(hud->state_label, text);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "ccc_hud_set_state: set_text");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result ccc_hud_set_turn(struct ccc_hud *hud, const char *text)
{
    if (!hud) {
        return YETTY_ERR(yetty_ycore_void, "ccc_hud_set_turn: NULL hud");
    }
    struct yetty_ycore_void_result res = yetty_ygui_label_set_text(hud->turn_label, text);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "ccc_hud_set_turn: set_text");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result ccc_hud_set_session(struct ccc_hud *hud, const char *text)
{
    if (!hud) {
        return YETTY_ERR(yetty_ycore_void, "ccc_hud_set_session: NULL hud");
    }
    struct yetty_ycore_void_result res = yetty_ygui_label_set_text(hud->session_label, text);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "ccc_hud_set_session: set_text");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result ccc_hud_flush(struct ccc_hud *hud)
{
    if (!hud) {
        return YETTY_ERR(yetty_ycore_void, "ccc_hud_flush: NULL hud");
    }
    if (!yetty_ygui_framework_is_dirty(hud->framework)) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result res = yetty_ygui_framework_emit(hud->framework);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "ccc_hud_flush: emit");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result ccc_hud_set_viewport(struct ccc_hud *hud, float width, float height)
{
    if (!hud) {
        return YETTY_ERR(yetty_ycore_void, "ccc_hud_set_viewport: NULL hud");
    }
    if (width <= 0.0f || height <= 0.0f) {
        return YETTY_ERR(yetty_ycore_void, "ccc_hud_set_viewport: non-positive size");
    }
    hud->viewport_width = width;
    hud->viewport_height = height;
    hud->viewport_from_host = 1;
    ydebug("ccc hud: viewport %.0fx%.0f (host)", width, height);
    struct yetty_ycore_void_result res = hud_apply_viewport(hud);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "ccc_hud_set_viewport: apply");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result ccc_hud_viewport_changed(struct ccc_hud *hud)
{
    if (!hud) {
        return YETTY_ERR(yetty_ycore_void, "ccc_hud_viewport_changed: NULL hud");
    }
    if (hud->viewport_from_host) {
        /* The host re-sends its resize envelope on every pane resize —
         * that cue carries real pixels; the tty winsize often doesn't. */
        return YETTY_OK_VOID();
    }
    terminal_pixels(&hud->viewport_width, &hud->viewport_height);
    struct yetty_ycore_void_result res = hud_apply_viewport(hud);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "ccc_hud_viewport_changed: apply");
    return YETTY_OK_VOID();
}

/*---------------------------------------------------------------------------
 * Client-side input
 *---------------------------------------------------------------------------*/

static int point_in_window(const struct ccc_hud *hud, float x, float y)
{
    float min_x = 0.0f;
    float min_y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    window_rect(hud, &min_x, &min_y, &width, &height);
    return x >= min_x && x <= min_x + width && y >= min_y && y <= min_y + height;
}

static int point_in_grip(const struct ccc_hud *hud, float x, float y)
{
    float min_x = 0.0f;
    float min_y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    window_rect(hud, &min_x, &min_y, &width, &height);
    return x >= min_x + width - CCC_HUD_GRIP_HIT && x <= min_x + width &&
           y >= min_y + height - CCC_HUD_GRIP_HIT && y <= min_y + height;
}

struct yetty_ycore_int_result ccc_hud_mouse_button(struct ccc_hud *hud, float x, float y,
                                                   int button, int pressed)
{
    if (!hud) {
        return YETTY_ERR(yetty_ycore_int, "ccc_hud_mouse_button: NULL hud");
    }
    if (pressed && button == 0 && point_in_grip(hud, x, y)) {
        /* Corner grip: start the ccc-side resize drag; the framework
         * never sees this press. */
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
    int inside = point_in_window(hud, x, y);
    struct yetty_ycore_int_result consumed_res =
        yetty_ygui_framework_feed_mouse_button(hud->framework, x, y, button, pressed, 0);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, consumed_res, "ccc_hud_mouse_button: feed");
    struct yetty_ycore_void_result flush_res = ccc_hud_flush(hud);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, flush_res, "ccc_hud_mouse_button: flush");
    /* Focus is geometric, not widget-based: any press inside the window
     * rect keeps the GUI focused even if no interactive widget consumed
     * it (e.g. a click on the body background). */
    ydebug("ccc hud: button=%d pressed=%d at (%.0f,%.0f) inside=%d consumed=%d", button, pressed, x,
           y, inside, consumed_res.value);
    if (pressed && inside) {
        /* A press in the window (titlebar drag included) pins the
         * user-chosen placement: viewport changes stop re-placing it. */
        hud->user_touched = 1;
    }
    return YETTY_OK(yetty_ycore_int, inside || consumed_res.value);
}

struct yetty_ycore_void_result ccc_hud_mouse_motion(struct ccc_hud *hud, float x, float y)
{
    if (!hud) {
        return YETTY_ERR(yetty_ycore_void, "ccc_hud_mouse_motion: NULL hud");
    }
    if (hud->resizing) {
        float new_width = hud->resize_start_width + (x - hud->resize_cursor_x);
        float new_height = hud->resize_start_height + (y - hud->resize_cursor_y);
        if (new_width < CCC_HUD_WINDOW_MIN_WIDTH) {
            new_width = CCC_HUD_WINDOW_MIN_WIDTH;
        }
        if (new_height < CCC_HUD_WINDOW_MIN_HEIGHT) {
            new_height = CCC_HUD_WINDOW_MIN_HEIGHT;
        }
        struct yetty_ycore_void_result size_res =
            yetty_ygui_widget_set_size(hud->window, new_width, new_height);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, size_res, "ccc_hud_mouse_motion: resize");
        return ccc_hud_flush(hud);
    }
    struct yetty_ycore_int_result motion_res =
        yetty_ygui_framework_feed_mouse_motion(hud->framework, x, y);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, motion_res, "ccc_hud_mouse_motion: feed");
    return ccc_hud_flush(hud);
}

struct yetty_ycore_void_result ccc_hud_mouse_wheel(struct ccc_hud *hud, float x, float y,
                                                   float delta_y)
{
    if (!hud) {
        return YETTY_ERR(yetty_ycore_void, "ccc_hud_mouse_wheel: NULL hud");
    }
    struct yetty_ycore_void_result scroll_res =
        yetty_ygui_framework_feed_mouse_scroll(hud->framework, x, y, 0.0f, delta_y);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, scroll_res, "ccc_hud_mouse_wheel: feed");
    return ccc_hud_flush(hud);
}

struct yetty_ycore_int_result ccc_hud_contains_point(struct ccc_hud *hud, float x, float y)
{
    if (!hud) {
        return YETTY_ERR(yetty_ycore_int, "ccc_hud_contains_point: NULL hud");
    }
    return YETTY_OK(yetty_ycore_int, point_in_window(hud, x, y));
}

struct yetty_ycore_void_result ccc_hud_feed_keys(struct ccc_hud *hud, const char *bytes, size_t len)
{
    if (!hud) {
        return YETTY_ERR(yetty_ycore_void, "ccc_hud_feed_keys: NULL hud");
    }
    if (len == 0) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result feed_res =
        yetty_ygui_framework_feed_input(hud->framework, bytes, len);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, feed_res, "ccc_hud_feed_keys: feed");
    return ccc_hud_flush(hud);
}

struct yetty_ycore_void_result ccc_hud_destroy(struct ccc_hud *hud)
{
    if (!hud) {
        return YETTY_ERR(yetty_ycore_void, "ccc_hud_destroy: NULL hud");
    }
    /* Best-effort: run every teardown step, accumulate failures. */
    struct yetty_ycore_void_result teardown = YETTY_OK_VOID();
    fflush(stdout);
    teardown = yetty_ycore_void_chain(
        teardown, yetty_ygui_framework_clear_remote_fd(hud->framework, STDOUT_FILENO));
    teardown = yetty_ycore_void_chain(teardown, yetty_ygui_framework_destroy(hud->framework));
    free(hud);
    return teardown;
}
