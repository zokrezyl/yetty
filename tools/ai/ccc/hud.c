/*
 * hud.c — ccc's non-scrolling ygui status panel.
 */
#include "hud.h"

#include <yetty/ygui/framework.h>
#include <yetty/ygui/object.h>
#include <yetty/ygui/widget.h>
#include <yetty/ygui/widgets/label.h>
#include <yetty/ygui/widgets/panel.h>
#include <yetty/ygui/widgets/vbox.h>
#include <yetty/yplatform/pty.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define CCC_HUD_PANEL_WIDTH 380.0f
#define CCC_HUD_PANEL_HEIGHT 96.0f
#define CCC_HUD_MARGIN 16.0f
#define CCC_HUD_PAD_X 10.0f
#define CCC_HUD_LINE_HEIGHT 20.0f

static void drop_error(struct yetty_ycore_void_result result)
{
    if (YETTY_IS_ERR(result)) {
        yetty_ycore_error_destroy(result.error);
    }
}

/*---------------------------------------------------------------------------
 * Blocking stdout pty shim. The framework writes its compositor envelope
 * through this; the caller fflushes stdout first, so envelope bytes can
 * never be interleaved with buffered text (ccc is the single PTY writer).
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
    struct yetty_ygui_object *root;
    struct yetty_ygui_object *panel;
    struct yetty_ygui_object *title_label;
    struct yetty_ygui_object *state_label;
    struct yetty_ygui_object *turn_label;
    struct yetty_ygui_object *session_label;
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

static void hud_layout(struct ccc_hud *hud)
{
    float width_px = 0.0f;
    float height_px = 0.0f;
    terminal_pixels(&width_px, &height_px);

    float panel_x = width_px - CCC_HUD_PANEL_WIDTH - CCC_HUD_MARGIN;
    if (panel_x < CCC_HUD_MARGIN) {
        panel_x = CCC_HUD_MARGIN;
    }
    float panel_y = CCC_HUD_MARGIN;

    drop_error(yetty_ygui_framework_set_viewport(hud->framework, width_px, height_px));
    drop_error(yetty_ygui_widget_set_position(hud->panel, panel_x, panel_y));
    drop_error(yetty_ygui_widget_set_size(hud->panel, CCC_HUD_PANEL_WIDTH, CCC_HUD_PANEL_HEIGHT));

    struct yetty_ygui_object *lines[] = {hud->title_label, hud->state_label, hud->turn_label,
                                         hud->session_label};
    for (size_t line_index = 0; line_index < sizeof(lines) / sizeof(lines[0]); line_index++) {
        if (!lines[line_index]) {
            continue;
        }
        drop_error(yetty_ygui_widget_set_position(lines[line_index], panel_x + CCC_HUD_PAD_X,
                                                  panel_y + 6.0f +
                                                      (float)line_index * CCC_HUD_LINE_HEIGHT));
        drop_error(yetty_ygui_widget_set_size(lines[line_index],
                                              CCC_HUD_PANEL_WIDTH - 2.0f * CCC_HUD_PAD_X, 18.0f));
    }
}

static struct yetty_ygui_object *hud_add(struct ccc_hud *hud,
                                         struct yetty_yclass_ptr_result class_result)
{
    if (YETTY_IS_ERR(class_result)) {
        yetty_ycore_error_destroy(class_result.error);
        return NULL;
    }
    struct yetty_ygui_object_ptr_result object_result =
        yetty_ygui_add(class_result.value, hud->root);
    if (YETTY_IS_ERR(object_result)) {
        yetty_ycore_error_destroy(object_result.error);
        return NULL;
    }
    return object_result.value;
}

struct ccc_hud *ccc_hud_create(void)
{
    const char *no_hud = getenv("CCC_NO_HUD");
    if (no_hud && strcmp(no_hud, "0") != 0 && strcmp(no_hud, "") != 0) {
        return NULL;
    }
    if (!isatty(STDOUT_FILENO)) {
        return NULL;
    }

    struct ccc_hud *hud = calloc(1, sizeof(*hud));
    if (!hud) {
        return NULL;
    }
    hud->pty.ops = hud_pty_ops();

    struct yetty_ygui_framework_ptr_result framework_result =
        yetty_ygui_framework_create(&hud->pty);
    if (YETTY_IS_ERR(framework_result)) {
        yetty_ycore_error_destroy(framework_result.error);
        free(hud);
        return NULL;
    }
    hud->framework = framework_result.value;

    struct yetty_ygui_object_ptr_result root_result =
        yetty_ygui_add(yetty_ygui_vbox_class_get().value, NULL);
    if (YETTY_IS_ERR(root_result)) {
        yetty_ycore_error_destroy(root_result.error);
        drop_error(yetty_ygui_framework_destroy(hud->framework));
        free(hud);
        return NULL;
    }
    hud->root = root_result.value;
    drop_error(yetty_ygui_framework_set_root(hud->framework, hud->root));

    hud->panel = hud_add(hud, yetty_ygui_panel_class_get());
    if (hud->panel) {
        /* BRAND_BG_LIFTED, near-opaque. */
        drop_error(yetty_ygui_panel_set_bg(
            hud->panel, (struct yetty_ycore_rgba){.r = 20, .g = 26, .b = 31, .a = 245}));
        drop_error(yetty_ygui_panel_set_border(
            hud->panel, (struct yetty_ycore_rgba){.r = 54, .g = 74, .b = 71, .a = 255}, 1.0f));
    }
    hud->title_label = hud_add(hud, yetty_ygui_label_class_get());
    hud->state_label = hud_add(hud, yetty_ygui_label_class_get());
    hud->turn_label = hud_add(hud, yetty_ygui_label_class_get());
    hud->session_label = hud_add(hud, yetty_ygui_label_class_get());
    if (hud->title_label) {
        drop_error(yetty_ygui_label_set_text(hud->title_label, "◆ ccc"));
        drop_error(yetty_ygui_label_set_color(
            hud->title_label, (struct yetty_ycore_rgba){.r = 107, .g = 168, .b = 146, .a = 255}));
    }
    if (hud->state_label) {
        drop_error(yetty_ygui_label_set_text(hud->state_label, "idle"));
    }
    if (hud->turn_label) {
        drop_error(yetty_ygui_label_set_text(hud->turn_label, "waiting for first turn…"));
        drop_error(yetty_ygui_label_set_color(
            hud->turn_label, (struct yetty_ycore_rgba){.r = 159, .g = 167, .b = 168, .a = 255}));
    }
    if (hud->session_label) {
        drop_error(yetty_ygui_label_set_text(hud->session_label, ""));
        drop_error(yetty_ygui_label_set_color(
            hud->session_label, (struct yetty_ycore_rgba){.r = 159, .g = 167, .b = 168, .a = 255}));
    }

    hud_layout(hud);
    ccc_hud_flush(hud);
    return hud;
}

static void hud_set_label(struct ccc_hud *hud, struct yetty_ygui_object *label, const char *text)
{
    if (!hud || !label) {
        return;
    }
    drop_error(yetty_ygui_label_set_text(label, text));
}

void ccc_hud_set_state(struct ccc_hud *hud, const char *text)
{
    hud_set_label(hud, hud ? hud->state_label : NULL, text);
}

void ccc_hud_set_turn(struct ccc_hud *hud, const char *text)
{
    hud_set_label(hud, hud ? hud->turn_label : NULL, text);
}

void ccc_hud_set_session(struct ccc_hud *hud, const char *text)
{
    hud_set_label(hud, hud ? hud->session_label : NULL, text);
}

void ccc_hud_flush(struct ccc_hud *hud)
{
    if (!hud) {
        return;
    }
    if (!yetty_ygui_framework_is_dirty(hud->framework)) {
        return;
    }
    drop_error(yetty_ygui_framework_emit(hud->framework));
}

void ccc_hud_viewport_changed(struct ccc_hud *hud)
{
    if (!hud) {
        return;
    }
    hud_layout(hud);
    ccc_hud_flush(hud);
}

void ccc_hud_destroy(struct ccc_hud *hud)
{
    if (!hud) {
        return;
    }
    fflush(stdout);
    drop_error(yetty_ygui_framework_clear_remote_fd(hud->framework, STDOUT_FILENO));
    drop_error(yetty_ygui_framework_destroy(hud->framework));
    free(hud);
}
