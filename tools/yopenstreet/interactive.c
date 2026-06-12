/*
 * interactive.c — the interactive yopenstreet client.
 *
 * Ships the map as a positioned server figure via `yview`
 * (YCOMPOSITOR_BIN), then stays foreground and re-renders on input:
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

#include <yetty/yopenstreet/openstreet.h>

#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/yface/yface.h>
#include <yetty/ymgui/wire.h>
#include <yetty/yterminal/client-input.h>
#include <yetty/yview/view.h>
#include <yetty/yplatform/term.h>
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
#define YOSM_STDOUT_FD (_fileno(stdout))
#define YOSM_GETPID() ((uint32_t)_getpid())
#define yosm_write(fd, p, n) ((int)_write((fd), (p), (unsigned)(n)))
#else
#include <unistd.h>
#define YOSM_STDOUT_FD (STDOUT_FILENO)
#define YOSM_GETPID() ((uint32_t)getpid())
#define yosm_write(fd, p, n) ((int)write((fd), (p), (n)))
#endif

/* GLFW-compatible modifier bits (client_input_mouse.mods). */
#define YOSM_MOD_SHIFT 1
#define YOSM_MOD_CONTROL 2

#define YOSM_BG_COLOR 0xfff2efe9u /* land color — blank while tiles load */

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
    struct yetty_yopenstreet_config config;
    bool vector_mode;
    uint32_t max_zoom;
    int out_fd;
    bool dragging;
    float drag_last_x;
    float drag_last_y;
    bool dirty;
    bool want_quit;
};

static int emit_envelope(int out_fd, int osc_code, const void *body, size_t body_len)
{
    struct yetty_ycore_buffer envelope = {0};
    struct yetty_ycore_void_result emit_res =
        yetty_yface_emit(osc_code, /*compressed=*/0, NULL, 0, body, body_len, &envelope);
    int rc = 0;
    if (YETTY_IS_OK(emit_res) && envelope.size > 0) {
        if (yosm_write(out_fd, envelope.data, envelope.size) < 0) {
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
    struct yetty_ydraw_drawable_list_result map_res =
        ui->vector_mode ? yetty_yopenstreet_render_vector(&ui->config)
                        : yetty_yopenstreet_render(&ui->config);
    if (YETTY_IS_ERR(map_res)) {
        ywarn("yopenstreet interactive: render failed: %s", map_res.error.msg);
        yetty_ycore_error_destroy(map_res.error);
        return;
    }

    /* OSM tile policy: attribution must be visible on the map. */
    {
        static const char attribution[] = "(C) OpenStreetMap contributors";
        struct yetty_ycore_buffer text_view = {
            .data = (uint8_t *)(uintptr_t)attribution,
            .capacity = sizeof(attribution) - 1,
            .size = sizeof(attribution) - 1,
        };
        struct yetty_ycore_void_result attr_res = yetty_ydraw_drawable_list_add_text(
            map_res.value, 4.0f, (float)ui->config.height_px - 4.0f, &text_view, 9.0f,
            0xff444444u, /*layer=*/200, /*font_id=*/-1, 0.0f);
        if (YETTY_IS_ERR(attr_res)) {
            yetty_ycore_error_destroy(attr_res.error);
        }
    }

    struct yetty_ycore_void_result content_res =
        yetty_yview_set_content(NULL, ui->view, map_res.value);
    if (YETTY_IS_ERR(content_res)) {
        ywarn("yopenstreet interactive: set_content failed: %s", content_res.error.msg);
        yetty_ycore_error_destroy(content_res.error);
    }
    yetty_ydraw_drawable_list_destroy(map_res.value);
}

/* Zoom by `step` keeping the figure-local pixel (anchor_x, anchor_y) on
 * the same lat/lon. Doubling the zoom doubles global pixel coordinates,
 * so: anchor_gpx' = anchor_gpx * 2^step; new_origin = anchor_gpx' -
 * anchor_px; new center = new_origin + viewport/2. */
static void zoom_at(struct map_ui *ui, int step, float anchor_x, float anchor_y)
{
    int64_t new_zoom = (int64_t)ui->config.zoom + step;
    if (new_zoom < 0) {
        new_zoom = 0;
    }
    if (new_zoom > (int64_t)ui->max_zoom) {
        new_zoom = (int64_t)ui->max_zoom;
    }
    if ((uint32_t)new_zoom == ui->config.zoom) {
        return;
    }

    double center_x = 0.0;
    double center_y = 0.0;
    yetty_yopenstreet_lonlat_to_global_pixel(ui->config.longitude, ui->config.latitude,
                                             ui->config.zoom, &center_x, &center_y);
    double origin_x = center_x - (double)ui->config.width_px / 2.0;
    double origin_y = center_y - (double)ui->config.height_px / 2.0;
    double anchor_gpx_x = origin_x + (double)anchor_x;
    double anchor_gpx_y = origin_y + (double)anchor_y;

    double scale = (new_zoom > (int64_t)ui->config.zoom)
                       ? (double)(1u << (uint32_t)(new_zoom - (int64_t)ui->config.zoom))
                       : 1.0 / (double)(1u << (uint32_t)((int64_t)ui->config.zoom - new_zoom));
    double new_anchor_x = anchor_gpx_x * scale;
    double new_anchor_y = anchor_gpx_y * scale;
    double new_center_x = new_anchor_x - (double)anchor_x + (double)ui->config.width_px / 2.0;
    double new_center_y = new_anchor_y - (double)anchor_y + (double)ui->config.height_px / 2.0;

    ui->config.zoom = (uint32_t)new_zoom;
    yetty_yopenstreet_global_pixel_to_lonlat(new_center_x, new_center_y, ui->config.zoom,
                                             &ui->config.longitude, &ui->config.latitude);
    ui->dirty = true;
}

/* Pan by a figure-local pixel delta (drag direction: content follows the
 * pointer, so the center moves opposite the drag). */
static void pan_by(struct map_ui *ui, float delta_x, float delta_y)
{
    if (delta_x == 0.0f && delta_y == 0.0f) {
        return;
    }
    double center_x = 0.0;
    double center_y = 0.0;
    yetty_yopenstreet_lonlat_to_global_pixel(ui->config.longitude, ui->config.latitude,
                                             ui->config.zoom, &center_x, &center_y);
    center_x -= (double)delta_x;
    center_y -= (double)delta_y;
    /* Clamp y so the viewport stays on the map. */
    double world_px = (double)(1u << ui->config.zoom) * 256.0;
    double half_h = (double)ui->config.height_px / 2.0;
    if (center_y < half_h) {
        center_y = half_h;
    }
    if (center_y > world_px - half_h) {
        center_y = world_px - half_h;
    }
    yetty_yopenstreet_global_pixel_to_lonlat(center_x, center_y, ui->config.zoom,
                                             &ui->config.longitude, &ui->config.latitude);
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
            bool ctrl = (mouse->mods & YOSM_MOD_CONTROL) != 0;
            bool shift = (mouse->mods & YOSM_MOD_SHIFT) != 0;
            ydebug("yopenstreet wheel: dy=%.2f mods=%d at (%.1f,%.1f)", (double)mouse->wheel_dy,
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
            ui->config.width_px = (uint32_t)resize->width;
            ui->config.height_px = (uint32_t)resize->height;
            struct yetty_ycore_void_result rect_res =
                yetty_yview_set_rect(NULL, ui->view, 0.0f, 0.0f, resize->width, resize->height);
            if (YETTY_IS_ERR(rect_res)) {
                yetty_ycore_error_destroy(rect_res.error);
            }
            ui->dirty = true;
        }
    } else {
        ydebug("yopenstreet osc: code=%d len=%zu (unhandled)", osc_code, payload_len);
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
            zoom_at(ui, +1, (float)ui->config.width_px / 2.0f,
                    (float)ui->config.height_px / 2.0f);
        } else if (key == '-') {
            zoom_at(ui, -1, (float)ui->config.width_px / 2.0f,
                    (float)ui->config.height_px / 2.0f);
        }
    }
}

int yopenstreet_interactive_run(const struct yetty_yopenstreet_config *initial_config,
                                bool vector_mode)
{
    if (!yetty_yplatform_tty_stdin_is_tty() || !yetty_yplatform_tty_stdout_is_tty()) {
        fprintf(stderr, "yopenstreet: --interactive needs a terminal (run inside yetty)\n");
        return 2;
    }

    struct yetty_ycore_void_result register_res = yetty_yview_register();
    if (YETTY_IS_ERR(register_res)) {
        fprintf(stderr, "yopenstreet: yview register failed: %s\n", register_res.error.msg);
        yetty_ycore_error_destroy(register_res.error);
        return 1;
    }
    struct yetty_yclass_object_ptr_result view_res = yetty_yview_view_create(NULL);
    if (YETTY_IS_ERR(view_res)) {
        fprintf(stderr, "yopenstreet: view create failed: %s\n", view_res.error.msg);
        yetty_ycore_error_destroy(view_res.error);
        return 1;
    }

    struct map_ui ui = {
        .view = view_res.value,
        .config = *initial_config,
        .vector_mode = vector_mode,
        .max_zoom =
            vector_mode ? YETTY_YOPENSTREET_MAX_VECTOR_ZOOM : YETTY_YOPENSTREET_MAX_ZOOM,
        .out_fd = YOSM_STDOUT_FD,
    };

    {
        struct yetty_ycore_void_result cfg_res = yetty_yview_configure(
            NULL, ui.view, YOSM_STDOUT_FD, YOSM_GETPID(), /*kind=*/0u, YOSM_BG_COLOR, 0.0f, 0.0f,
            (float)ui.config.width_px, (float)ui.config.height_px);
        if (YETTY_IS_ERR(cfg_res)) {
            fprintf(stderr, "yopenstreet: view configure failed: %s\n", cfg_res.error.msg);
            yetty_ycore_error_destroy(cfg_res.error);
            (void)yetty_yview_destroy(NULL, ui.view);
            return 1;
        }
    }

    struct yetty_yface_ptr_result yface_res = yetty_yface_create();
    if (YETTY_IS_ERR(yface_res)) {
        fprintf(stderr, "yopenstreet: yface create failed: %s\n", yface_res.error.msg);
        yetty_ycore_error_destroy(yface_res.error);
        (void)yetty_yview_destroy(NULL, ui.view);
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
        fprintf(stderr, "yopenstreet: cannot put stdin into raw mode\n");
        yetty_yface_destroy(yface);
        (void)yetty_yview_destroy(NULL, ui.view);
        return 1;
    }

    subscribe_input(YOSM_STDOUT_FD, YETTY_CLIENT_INPUT_SUB_MOUSE_CLICK |
                                        YETTY_CLIENT_INPUT_SUB_MOUSE_MOVE |
                                        YETTY_CLIENT_INPUT_SUB_MOUSE_WHEEL);
    /* DEC ?1500h (click) + ?1501h (move): the terminal hit-tests the
     * cursor against figures and forwards FIGURE_MOUSE events to ours —
     * including the Ctrl-Shift-wheel zoom gesture. */
    {
        static const char mouse_on[] = "\x1b[?1500h\x1b[?1501h";
        (void)yosm_write(YOSM_STDOUT_FD, mouse_on, sizeof(mouse_on) - 1);
    }

    rerender(&ui);

    char buf[8192];
    while (!g_signal_quit && !ui.want_quit) {
        int ready = yetty_yplatform_tty_stdin_wait(200);
        if (ready < 0) {
            break;
        }
        ui.dirty = false;
        if (ready > 0) {
            int bytes_read = yetty_yplatform_tty_stdin_read(buf, sizeof(buf));
            if (bytes_read > 0) {
                (void)yetty_yface_feed_bytes(yface, buf, (size_t)bytes_read);
            } else if (bytes_read == 0 && !yetty_yplatform_tty_stdin_is_tty()) {
                break;
            }
        }
        if (ui.dirty) {
            rerender(&ui);
        }
    }

    /* Teardown: mouse reporting off, unsubscribe, drop the figure, restore. */
    {
        static const char mouse_off[] = "\x1b[?1500l\x1b[?1501l";
        (void)yosm_write(YOSM_STDOUT_FD, mouse_off, sizeof(mouse_off) - 1);
    }
    subscribe_input(YOSM_STDOUT_FD, 0u);
    (void)yetty_yview_destroy(NULL, ui.view);
    yetty_yplatform_tty_restore();
    yetty_yface_destroy(yface);
    return 0;
}
