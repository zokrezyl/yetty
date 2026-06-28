/*
 * interactive.c — the interactive ymap client.
 *
 * Ships the map as a positioned server figure via `yview` over yclass-RPC,
 * then stays foreground and re-renders on input:
 *
 *   Ctrl-Shift-wheel  → zoom in / out, anchored at the cursor (the lat/lon
 *                       under the pointer stays under the pointer)
 *   left-drag         → pan
 *   + / -             → zoom at the viewport center
 *   q / Ctrl-C / Esc  → quit
 *
 * Input model follows tools/yflame/interactive.c: subscribe to pane-wide
 * mouse events, DEC ?1500h/?1501h so the terminal hit-tests the cursor
 * against our figure and forwards FIGURE_MOUSE events; yface splits stdin
 * into OSC envelopes + raw keystrokes. The wheel events carry the GLFW
 * modifier mask (client_input_mouse.mods) — the terminal only forwards a
 * Ctrl-Shift-wheel to a subscribed figure under the cursor, falling back
 * to its own cell zoom elsewhere.
 *
 * Each zoom/pan does a full tile fetch + re-render; the disk cache makes
 * revisited areas instant, fresh areas block the loop for the download
 * (acceptable: the gesture is discrete, not continuous).
 */

#include "interactive.h"

#include <yetty/ymap/map.h>

#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/yface/yface.h>
#include <yetty/ymgui/wire.h>
#include <yetty/yterminal/client-input.h>
#include <yetty/yview/view.h>
#include <yetty/yplatform/term.h>
#include <yetty/yplatform/time.h>
#include <yetty/yplatform/tty.h>
#include <yetty/ytrace/ytrace.h>

#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#include <process.h>
#define YMAP_STDOUT_FD (_fileno(stdout))
#define YMAP_GETPID() ((uint32_t)_getpid())
#define ymap_write(fd, p, n) ((int)_write((fd), (p), (unsigned)(n)))
#else
#include <unistd.h>
#define YMAP_STDOUT_FD (STDOUT_FILENO)
#define YMAP_GETPID() ((uint32_t)getpid())
#define ymap_write(fd, p, n) ((int)write((fd), (p), (n)))
#endif

/* GLFW-compatible modifier bits (client_input_mouse.mods). */
#define YMAP_MOD_SHIFT 1
#define YMAP_MOD_CONTROL 2

#define YMAP_BG_COLOR 0xfff2efe9u /* land color — blank while tiles load */

/* Signal flag: a foreground CLI needs file-scope here because a signal
 * handler cannot carry userdata (same shape as tools/yflame). */
static volatile sig_atomic_t g_signal_quit = 0;

static void on_signal(int sig)
{
    (void)sig;
    g_signal_quit = 1;
}

struct map_ui {
    struct yetty_yclass_object *view;
    struct yetty_yclass_object *map; /* the ymap:map model (borrowed) */
    uint32_t width_px;
    uint32_t height_px;
    bool vector_mode;
    int out_fd;
    bool dragging;
    float drag_last_x;
    float drag_last_y;
    bool dirty;
    bool want_quit;
    /* Drag-render throttling. A raster re-render ships the whole
     * viewport as raw pixels (tens of MB through the PTY) — doing that
     * per mouse-move event drowns the pipe and the map appears frozen.
     * During a drag, renders coalesce to one per interval; release
     * flushes immediately. Vector envelopes are ~50x smaller, so their
     * interval is short. */
    double last_render_sec;
};

/* Minimum seconds between re-renders WHILE DRAGGING. */
#define YMAP_DRAG_RENDER_INTERVAL_VECTOR 0.08
#define YMAP_DRAG_RENDER_INTERVAL_RASTER 0.5

static int emit_envelope(int out_fd, int osc_code, const void *body, size_t body_len)
{
    struct yetty_ycore_buffer envelope = {0};
    struct yetty_ycore_void_result emit_res =
        yetty_yface_emit(osc_code, /*compressed=*/0, NULL, 0, body, body_len, &envelope);
    int rc = 0;
    if (YETTY_IS_OK(emit_res) && envelope.size > 0) {
        if (ymap_write(out_fd, envelope.data, envelope.size) < 0) {
            rc = 1;
        }
    } else if (YETTY_IS_ERR(emit_res)) {
        yetty_ycore_error_destroy(emit_res.error);
        rc = 1;
    }
    yetty_ycore_buffer_destroy(&envelope);
    return rc;
}

static void subscribe_input(int out_fd, uint32_t flags)
{
    struct yetty_client_input_sub msg = {
        .magic = YETTY_CLIENT_INPUT_SUB_MAGIC,
        .version = YMGUI_WIRE_VERSION,
        .flags = flags,
        ._pad0 = 0,
    };
    (void)emit_envelope(out_fd, YETTY_OSC_CS_CLIENT_INPUT_SUB, &msg, sizeof(msg));
}

/* Full map re-render → yview content. Blocks on uncached tile downloads. */
static void rerender(struct map_ui *ui)
{
    struct yetty_ydraw_drawable_list_result map_res = yetty_ymap_render(ui->map);
    if (YETTY_IS_ERR(map_res)) {
        ywarn("ymap interactive: render failed: %s", map_res.error.msg);
        yetty_ycore_error_destroy(map_res.error);
        return;
    }

    /* Tile providers require their attribution visible on the map. */
    {
        struct yetty_ycore_const_char_ptr_result attribution_res = yetty_ymap_attribution(ui->map);
        const char *attribution =
            YETTY_IS_OK(attribution_res) ? attribution_res.value : "(C) tile provider";
        if (YETTY_IS_ERR(attribution_res)) {
            yetty_ycore_error_destroy(attribution_res.error);
        }
        size_t attribution_len = strlen(attribution);
        struct yetty_ycore_buffer text_view = {
            .data = (uint8_t *)(uintptr_t)attribution,
            .capacity = attribution_len,
            .size = attribution_len,
        };
        struct yetty_ycore_void_result attr_res = yetty_ydraw_drawable_list_add_text(
            map_res.value, 4.0f, (float)ui->height_px - 4.0f, &text_view, 9.0f, 0xff444444u,
            /*layer=*/200, /*font_id=*/-1, 0.0f);
        if (YETTY_IS_ERR(attr_res)) {
            yetty_ycore_error_destroy(attr_res.error);
        }
    }

    struct yetty_ycore_void_result content_res = yetty_yview_set_content(ui->view, map_res.value);
    if (YETTY_IS_ERR(content_res)) {
        ywarn("ymap interactive: set_content failed: %s", content_res.error.msg);
        yetty_ycore_error_destroy(content_res.error);
    }
    yetty_ydraw_drawable_list_destroy(map_res.value);
}

/* Zoom / pan — thin wrappers over the ymap model's navigation verbs. */
static void zoom_at(struct map_ui *ui, int step, float anchor_x, float anchor_y)
{
    struct yetty_ycore_int_result zoom_res =
        yetty_ymap_zoom_by_at(ui->map, step, (double)anchor_x, (double)anchor_y);
    if (YETTY_IS_ERR(zoom_res)) {
        yetty_ycore_error_destroy(zoom_res.error);
        return;
    }
    ui->dirty = true;
}

static void pan_by(struct map_ui *ui, float delta_x, float delta_y)
{
    if (delta_x == 0.0f && delta_y == 0.0f) {
        return;
    }
    struct yetty_ycore_void_result pan_res =
        yetty_ymap_pan_by_pixels(ui->map, (double)delta_x, (double)delta_y);
    if (YETTY_IS_ERR(pan_res)) {
        yetty_ycore_error_destroy(pan_res.error);
        return;
    }
    ui->dirty = true;
}

static void on_osc(void *user, int osc_code, const uint8_t *args, size_t args_len,
                   const uint8_t *payload, size_t payload_len)
{
    (void)args;
    (void)args_len;
    struct map_ui *ui = user;

    if ((osc_code == YETTY_OSC_SC_CLIENT_INPUT_MOUSE ||
         osc_code == YETTY_OSC_SC_CLIENT_INPUT_FIGURE_MOUSE) &&
        payload_len >= sizeof(struct yetty_client_input_mouse)) {
        const struct yetty_client_input_mouse *mouse =
            (const struct yetty_client_input_mouse *)payload;

        if (mouse->kind == YETTY_YMGUI_INPUT_MOUSE_WHEEL) {
            bool ctrl = (mouse->mods & YMAP_MOD_CONTROL) != 0;
            bool shift = (mouse->mods & YMAP_MOD_SHIFT) != 0;
            ydebug("ymap wheel: dy=%.2f mods=%d at (%.1f,%.1f)", (double)mouse->wheel_dy,
                   mouse->mods, (double)mouse->x, (double)mouse->y);
            if (ctrl && shift && mouse->wheel_dy != 0.0f) {
                zoom_at(ui, mouse->wheel_dy > 0.0f ? +1 : -1, mouse->x, mouse->y);
            }
        } else if (mouse->kind == YETTY_YMGUI_INPUT_MOUSE_BUTTON) {
            if (mouse->button == 0) {
                if (mouse->pressed) {
                    ui->dragging = true;
                    ui->drag_last_x = mouse->x;
                    ui->drag_last_y = mouse->y;
                } else {
                    ui->dragging = false;
                }
            }
        } else if (mouse->kind == YETTY_YMGUI_INPUT_MOUSE_POS) {
            if (mouse->figure_id == 0u) {
                /* Cursor left our figure — end any drag. */
                ui->dragging = false;
            } else if (ui->dragging && (mouse->buttons_held & 1u)) {
                pan_by(ui, mouse->x - ui->drag_last_x, mouse->y - ui->drag_last_y);
                ui->drag_last_x = mouse->x;
                ui->drag_last_y = mouse->y;
            } else if (ui->dragging && !(mouse->buttons_held & 1u)) {
                ui->dragging = false;
            }
        }
    } else if ((osc_code == YETTY_OSC_SC_CLIENT_INPUT_RESIZE ||
                osc_code == YETTY_OSC_SC_CLIENT_INPUT_FIGURE_RESIZE) &&
               payload_len >= sizeof(struct yetty_client_input_resize)) {
        const struct yetty_client_input_resize *resize =
            (const struct yetty_client_input_resize *)payload;
        if (resize->width > 0.0f && resize->height > 0.0f) {
            ui->width_px = (uint32_t)resize->width;
            ui->height_px = (uint32_t)resize->height;
            struct yetty_ycore_void_result viewport_res =
                yetty_ymap_set_viewport(ui->map, ui->width_px, ui->height_px);
            if (YETTY_IS_ERR(viewport_res)) {
                yetty_ycore_error_destroy(viewport_res.error);
            }
            struct yetty_ycore_void_result rect_res =
                yetty_yview_set_rect(ui->view, 0.0f, 0.0f, resize->width, resize->height);
            if (YETTY_IS_ERR(rect_res)) {
                yetty_ycore_error_destroy(rect_res.error);
            }
            ui->dirty = true;
        }
    } else {
        ydebug("ymap osc: code=%d len=%zu (unhandled)", osc_code, payload_len);
    }
}

static void on_raw(void *user, const char *bytes, size_t count)
{
    struct map_ui *ui = user;
    for (size_t i = 0; i < count; i++) {
        char key = bytes[i];
        if (key == 'q' || key == 0x03 /* Ctrl-C */ || key == 0x04 /* Ctrl-D */ ||
            key == 0x1b /* Esc */) {
            ui->want_quit = true;
        } else if (key == '+' || key == '=') {
            zoom_at(ui, +1, (float)ui->width_px / 2.0f, (float)ui->height_px / 2.0f);
        } else if (key == '-') {
            zoom_at(ui, -1, (float)ui->width_px / 2.0f, (float)ui->height_px / 2.0f);
        }
    }
}

int ymap_interactive_run(struct yetty_yclass_object *map_object, uint32_t width_px,
                         uint32_t height_px)
{
    if (!yetty_yplatform_tty_stdin_is_tty() || !yetty_yplatform_tty_stdout_is_tty()) {
        fprintf(stderr, "ymap: --interactive needs a terminal (run inside yetty)\n");
        return 2;
    }
    bool vector_mode = false;
    {
        struct yetty_ycore_int_result vector_res = yetty_ymap_is_vector(map_object);
        if (YETTY_IS_OK(vector_res)) {
            vector_mode = vector_res.value != 0;
        } else {
            yetty_ycore_error_destroy(vector_res.error);
        }
    }

    struct yetty_ycore_void_result register_res = yetty_yview_register();
    if (YETTY_IS_ERR(register_res)) {
        fprintf(stderr, "ymap: yview register failed: %s\n", register_res.error.msg);
        yetty_ycore_error_destroy(register_res.error);
        return 1;
    }
    struct yetty_yclass_object_ptr_result view_res = yetty_yview_view_create(NULL);
    if (YETTY_IS_ERR(view_res)) {
        fprintf(stderr, "ymap: view create failed: %s\n", view_res.error.msg);
        yetty_ycore_error_destroy(view_res.error);
        return 1;
    }

    struct map_ui ui = {
        .view = view_res.value,
        .map = map_object,
        .width_px = width_px,
        .height_px = height_px,
        .vector_mode = vector_mode,
        .out_fd = YMAP_STDOUT_FD,
    };

    {
        struct yetty_ycore_void_result cfg_res = yetty_yview_configure(
            ui.view, YMAP_STDOUT_FD, YMAP_GETPID(), /*kind=*/0u, YMAP_BG_COLOR, 0.0f, 0.0f,
            (float)ui.width_px, (float)ui.height_px);
        if (YETTY_IS_ERR(cfg_res)) {
            fprintf(stderr, "ymap: view configure failed: %s\n", cfg_res.error.msg);
            yetty_ycore_error_destroy(cfg_res.error);
            (void)yetty_yview_destroy(ui.view);
            return 1;
        }
    }

    struct yetty_yface_ptr_result yface_res = yetty_yface_create();
    if (YETTY_IS_ERR(yface_res)) {
        fprintf(stderr, "ymap: yface create failed: %s\n", yface_res.error.msg);
        yetty_ycore_error_destroy(yface_res.error);
        (void)yetty_yview_destroy(ui.view);
        return 1;
    }
    struct yetty_yface *yface = yface_res.value;
    yetty_yface_set_handlers(yface, on_osc, on_raw, &ui);

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
#ifdef SIGHUP
    signal(SIGHUP, on_signal);
#endif

    yetty_yplatform_tty_binary_io();
    if (yetty_yplatform_tty_set_raw() < 0) {
        fprintf(stderr, "ymap: cannot put stdin into raw mode\n");
        yetty_yface_destroy(yface);
        (void)yetty_yview_destroy(ui.view);
        return 1;
    }

    subscribe_input(YMAP_STDOUT_FD, YETTY_CLIENT_INPUT_SUB_MOUSE_CLICK |
                                        YETTY_CLIENT_INPUT_SUB_MOUSE_MOVE |
                                        YETTY_CLIENT_INPUT_SUB_MOUSE_WHEEL);
    /* DEC ?1500h (click) + ?1501h (move): the terminal hit-tests the
     * cursor against figures and forwards FIGURE_MOUSE events to ours —
     * including the Ctrl-Shift-wheel zoom gesture. */
    {
        static const char mouse_on[] = "\x1b[?1500h\x1b[?1501h";
        (void)ymap_write(YMAP_STDOUT_FD, mouse_on, sizeof(mouse_on) - 1);
    }

    rerender(&ui);
    ui.last_render_sec = yetty_yplatform_ytime_monotonic_sec();

    char buf[8192];
    double drag_interval =
        vector_mode ? YMAP_DRAG_RENDER_INTERVAL_VECTOR : YMAP_DRAG_RENDER_INTERVAL_RASTER;
    while (!g_signal_quit && !ui.want_quit) {
        int ready = yetty_yplatform_tty_stdin_wait(50);
        if (ready < 0) {
            break;
        }
        if (ready > 0) {
            int bytes_read = yetty_yplatform_tty_stdin_read(buf, sizeof(buf));
            if (bytes_read > 0) {
                (void)yetty_yface_feed_bytes(yface, buf, (size_t)bytes_read);
            } else if (bytes_read == 0 && !yetty_yplatform_tty_stdin_is_tty()) {
                break;
            }
        }
        if (ui.dirty) {
            double now = yetty_yplatform_ytime_monotonic_sec();
            /* Mid-drag renders coalesce; everything else (wheel zoom,
             * key zoom, resize, drag release) renders right away. */
            if (!ui.dragging || now - ui.last_render_sec >= drag_interval) {
                rerender(&ui);
                ui.last_render_sec = yetty_yplatform_ytime_monotonic_sec();
                ui.dirty = false;
            }
        }
    }

    /* Teardown: mouse reporting off, unsubscribe, drop the figure, restore. */
    {
        static const char mouse_off[] = "\x1b[?1500l\x1b[?1501l";
        (void)ymap_write(YMAP_STDOUT_FD, mouse_off, sizeof(mouse_off) - 1);
    }
    subscribe_input(YMAP_STDOUT_FD, 0u);
    (void)yetty_yview_destroy(ui.view);
    yetty_yplatform_tty_restore();
    yetty_yface_destroy(yface);
    return 0;
}
