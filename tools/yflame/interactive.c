/*
 * interactive.c — the interactive yflame client.
 *
 * Ships the flame graph as a positioned server figure via `yview` over
 * yclass-RPC, then stays foreground: it subscribes to pane-wide mouse
 * events, and on each event drives the `yflame:flame` class —
 *   hover  → hit_test → set_highlight → re-render → yview_set_content
 *   click  → hit_test → focus (zoom in) / reset (right button) → re-render
 * Keyboard (raw tty): q / Ctrl-C quit, r / Esc reset zoom.
 *
 * Coordinates: the graph is fit to the viewport height (no scrolling), and the
 * figure sits at the pane origin, so pane-pixel mouse coordinates are the
 * flame's content coordinates directly.
 *
 * Input model follows tools/ynetsurf (yface splits stdin into OSC envelopes +
 * raw keystrokes); the figure lifecycle follows tools/yless (yview).
 */
#include <yetty/yflame/flame.h>
#include <yetty/yclass/class.h>
#include <yetty/yview/view.h> /* yetty_yview_configure / _set_content / _set_rect / _destroy */
#include <yetty/yface/yface.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/ymgui/wire.h>
#include <yetty/yterminal/client-input.h>
#include <yetty/yplatform/term.h> /* yetty_yplatform_term_get_size */
#include <yetty/yplatform/tty.h>  /* cross-platform raw mode + stdin read */
#include <yetty/ytrace/ytrace.h>

#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>      /* _write, _fileno */
#include <process.h> /* _getpid */
#define YFLAME_STDOUT_FD (_fileno(stdout))
#define YFLAME_GETPID() ((uint32_t)_getpid())
#define yflame_write(fd, p, n) ((int)_write((fd), (p), (unsigned)(n)))
#else
#include <unistd.h>
#define YFLAME_STDOUT_FD (STDOUT_FILENO)
#define YFLAME_GETPID() ((uint32_t)getpid())
#define yflame_write(fd, p, n) ((int)write((fd), (p), (n)))
#endif

/* Brand near-black, opaque (0xAARRGGBB) — the figure background. */
#define YFLAME_BG_COLOR 0xFF0B1014u

/* Signal flag: a foreground CLI needs file-scope here because a signal handler
 * cannot carry userdata (same shape as tools/ynetsurf). Raw-mode state lives
 * in the yplatform tty layer, restored via yetty_yplatform_tty_restore. */
static volatile sig_atomic_t g_signal_quit = 0;

static void on_signal(int sig)
{
    (void)sig;
    g_signal_quit = 1;
}

struct ui_state {
    struct yetty_yclass_object *flame;
    struct yetty_yclass_object *view;
    int out_fd;
    float viewport_w;
    float viewport_h;
    float frame_height; /* FIXED row height — does not change with zoom */
    float min_width;
    uint32_t base_flags;
    int32_t hover; /* currently highlighted node id, or -1 */
    bool dirty;
    bool want_quit;
};

static int emit_envelope(int out_fd, int osc_code, int compressed, const void *body,
                         size_t body_len)
{
    struct yetty_ycore_buffer envelope = {0};
    struct yetty_ycore_void_result r =
        yetty_yface_emit(osc_code, compressed, NULL, 0, body, body_len, &envelope);
    int rc = 0;
    if (YETTY_IS_OK(r) && envelope.size > 0) {
        if (yflame_write(out_fd, envelope.data, envelope.size) < 0) {
            rc = 1;
        }
    } else if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
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
    (void)emit_envelope(out_fd, YETTY_OSC_CS_CLIENT_INPUT_SUB, /*compressed=*/0, &msg, sizeof(msg));
}

/* Render at the FIXED row height (zoom changes the focus subtree, never the row
 * height). The graph grows downward from the figure top; the figure clips to
 * the viewport, so a graph taller than the window simply extends past the
 * bottom (zoom in to shrink it). Figure-local coords == content coords. */
static void rerender(struct ui_state *ui)
{
    (void)yetty_yflame_configure(ui->flame, ui->viewport_w, ui->frame_height, ui->min_width,
                                 ui->base_flags);
    struct yetty_ydraw_drawable_list_result rr = yetty_yflame_render(ui->flame);
    if (YETTY_IS_OK(rr)) {
        (void)yetty_yview_set_content(ui->view, rr.value);
        yetty_ydraw_drawable_list_destroy(rr.value);
    } else {
        yetty_ycore_error_destroy(rr.error);
    }
}

static int32_t hit(struct ui_state *ui, float x, float y)
{
    struct yetty_ycore_int_result h = yetty_yflame_hit_test(ui->flame, x, y);
    if (YETTY_IS_ERR(h)) {
        yetty_ycore_error_destroy(h.error);
        return -1;
    }
    return (int32_t)h.value;
}

static void on_osc(void *user, int osc_code, const uint8_t *args, size_t args_len,
                   const uint8_t *payload, size_t payload_len)
{
    (void)args;
    (void)args_len;
    struct ui_state *ui = user;

    if ((osc_code == YETTY_OSC_SC_CLIENT_INPUT_MOUSE ||
         osc_code == YETTY_OSC_SC_CLIENT_INPUT_FIGURE_MOUSE) &&
        payload_len >= sizeof(struct yetty_client_input_mouse)) {
        const struct yetty_client_input_mouse *m = (const struct yetty_client_input_mouse *)payload;
        ydebug("yflame mouse: osc=%d kind=%u fig=%u x=%.1f y=%.1f btn=%d pressed=%d wheel=%.2f",
               osc_code, m->kind, m->figure_id, (double)m->x, (double)m->y, m->button, m->pressed,
               (double)m->wheel_dy);
        /* figure_id == 0 means the cursor left our figure — drop the hover. */
        if (m->figure_id == 0u && m->kind == YETTY_YMGUI_INPUT_MOUSE_POS) {
            if (ui->hover != -1) {
                ui->hover = -1;
                (void)yetty_yflame_set_highlight(ui->flame, -1);
                ui->dirty = true;
            }
            return;
        }
        if (m->kind == YETTY_YMGUI_INPUT_MOUSE_POS) {
            int32_t id = hit(ui, m->x, m->y);
            if (id != ui->hover) {
                ui->hover = id;
                (void)yetty_yflame_set_highlight(ui->flame, id);
                ui->dirty = true;
            }
        } else if (m->kind == YETTY_YMGUI_INPUT_MOUSE_BUTTON && m->pressed) {
            if (m->button == 0) { /* left → button action or zoom into a frame */
                int32_t id = hit(ui, m->x, m->y);
                ydebug("yflame left-click: hit_test(%.1f,%.1f) -> id=%d", (double)m->x,
                       (double)m->y, id);
                if (id == YETTY_YFLAME_HIT_UP) {
                    (void)yetty_yflame_focus_parent(ui->flame);
                    ui->dirty = true;
                } else if (id == YETTY_YFLAME_HIT_ROOT) {
                    (void)yetty_yflame_reset(ui->flame);
                    ui->dirty = true;
                } else if (id >= 0) {
                    (void)yetty_yflame_focus(ui->flame, id);
                    ui->dirty = true;
                }
            } else { /* right → zoom back out one level */
                (void)yetty_yflame_focus_parent(ui->flame);
                ui->dirty = true;
            }
        } else if (m->kind == YETTY_YMGUI_INPUT_MOUSE_WHEEL) {
            /* Mouse-anchored zoom: wheel up drills into the frame under the
             * cursor, wheel down climbs out — the fixed row height is unchanged. */
            if (m->wheel_dy > 0.0f) {
                int32_t id = hit(ui, m->x, m->y);
                if (id >= 0) {
                    (void)yetty_yflame_focus(ui->flame, id);
                    ui->dirty = true;
                }
            } else if (m->wheel_dy < 0.0f) {
                (void)yetty_yflame_focus_parent(ui->flame);
                ui->dirty = true;
            }
        }
    } else if ((osc_code == YETTY_OSC_SC_CLIENT_INPUT_RESIZE ||
                osc_code == YETTY_OSC_SC_CLIENT_INPUT_FIGURE_RESIZE) &&
               payload_len >= sizeof(struct yetty_client_input_resize)) {
        const struct yetty_client_input_resize *rz =
            (const struct yetty_client_input_resize *)payload;
        ydebug("yflame resize: osc=%d w=%.1f h=%.1f", osc_code, (double)rz->width,
               (double)rz->height);
        if (rz->width > 0.0f && rz->height > 0.0f) {
            ui->viewport_w = rz->width;
            ui->viewport_h = rz->height;
            (void)yetty_yview_set_rect(ui->view, 0.0f, 0.0f, ui->viewport_w, ui->viewport_h);
            ui->dirty = true;
        }
    } else {
        ydebug("yflame osc: code=%d len=%zu (unhandled)", osc_code, payload_len);
    }
}

static void on_raw(void *user, const char *bytes, size_t n)
{
    struct ui_state *ui = user;
    for (size_t i = 0; i < n; i++) {
        char c = bytes[i];
        if (c == 'q' || c == 0x03 /* Ctrl-C */ || c == 0x04 /* Ctrl-D */) {
            ui->want_quit = true;
        } else if (c == 'r' || c == 0x1b /* Esc */) {
            (void)yetty_yflame_reset(ui->flame);
            ui->dirty = true;
        }
    }
}

int yflame_interactive_run(const char *input, size_t input_len, float min_width,
                           uint32_t base_flags)
{
    if (!yetty_yplatform_tty_stdin_is_tty() || !yetty_yplatform_tty_stdout_is_tty()) {
        fprintf(stderr, "yflame: --interactive needs a terminal (run inside yetty)\n");
        return 2;
    }

    /* Viewport in pixels, approximated from the terminal cell size (yplatform
     * reports cells, not pixels; the server clips to the rect regardless). */
    int cols = 80, rows = 24;
    (void)yetty_yplatform_term_get_size(&cols, &rows);
    if (cols <= 0) {
        cols = 80;
    }
    if (rows <= 0) {
        rows = 24;
    }
    float viewport_w = (float)cols * 8.0f;
    float viewport_h = (float)rows * 16.0f;

    struct yetty_ycore_void_result reg = yetty_yflame_register();
    if (YETTY_IS_ERR(reg)) {
        fprintf(stderr, "yflame: class register failed: %s\n", reg.error.msg);
        yetty_ycore_error_destroy(reg.error);
        return 1;
    }
    struct yetty_yclass_object_ptr_result obj_r = yetty_yflame_flame_create(NULL);
    if (YETTY_IS_ERR(obj_r)) {
        fprintf(stderr, "yflame: create failed: %s\n", obj_r.error.msg);
        yetty_ycore_error_destroy(obj_r.error);
        return 1;
    }
    struct yetty_yclass_object *flame = obj_r.value;

    struct yetty_ycore_void_result pr = yetty_yflame_parse(flame, input, input_len);
    if (YETTY_IS_ERR(pr)) {
        fprintf(stderr, "yflame: parse failed: %s\n", pr.error.msg);
        for (const struct yetty_ycore_error *e = pr.error.cause; e; e = e->cause) {
            fprintf(stderr, "  caused by: %s\n", e->msg);
        }
        yetty_ycore_error_destroy(pr.error);
        (void)yetty_yflame_destroy(flame);
        return 1;
    }

    struct yetty_yclass_object_ptr_result view_r = yetty_yview_view_create(NULL);
    if (YETTY_IS_ERR(view_r)) {
        fprintf(stderr, "yflame: view create failed: %s\n", view_r.error.msg);
        yetty_ycore_error_destroy(view_r.error);
        (void)yetty_yflame_destroy(flame);
        return 1;
    }
    {
        struct yetty_ycore_void_result cfg_r =
            yetty_yview_configure(view_r.value, YFLAME_STDOUT_FD, YFLAME_GETPID(),
                                  /*kind=*/0u, YFLAME_BG_COLOR, 0.0f, 0.0f, viewport_w, viewport_h);
        if (YETTY_IS_ERR(cfg_r)) {
            fprintf(stderr, "yflame: view configure failed: %s\n", cfg_r.error.msg);
            yetty_ycore_error_destroy(cfg_r.error);
            (void)yetty_yview_destroy(view_r.value);
            (void)yetty_yflame_destroy(flame);
            return 1;
        }
    }

    struct ui_state ui = {
        .flame = flame,
        .view = view_r.value,
        .out_fd = YFLAME_STDOUT_FD,
        .viewport_w = viewport_w,
        .viewport_h = viewport_h,
        .frame_height = 34.0f, /* fixed, readable row height (font ≈ 25px) */
        .min_width = min_width,
        .base_flags = base_flags,
        .hover = -1,
    };

    struct yetty_yface_ptr_result yface_r = yetty_yface_create();
    if (YETTY_IS_ERR(yface_r)) {
        fprintf(stderr, "yflame: yface create failed: %s\n", yface_r.error.msg);
        yetty_ycore_error_destroy(yface_r.error);
        (void)yetty_yview_destroy(ui.view);
        (void)yetty_yflame_destroy(flame);
        return 1;
    }
    struct yetty_yface *yface = yface_r.value;
    yetty_yface_set_handlers(yface, on_osc, on_raw, &ui);

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
#ifdef SIGHUP
    signal(SIGHUP, on_signal);
#endif

    yetty_yplatform_tty_binary_io();
    if (yetty_yplatform_tty_set_raw() < 0) {
        fprintf(stderr, "yflame: cannot put stdin into raw mode\n");
        yetty_yface_destroy(yface);
        (void)yetty_yview_destroy(ui.view);
        (void)yetty_yflame_destroy(flame);
        return 1;
    }

    subscribe_input(YFLAME_STDOUT_FD,
                    YETTY_CLIENT_INPUT_SUB_MOUSE_CLICK | YETTY_CLIENT_INPUT_SUB_MOUSE_MOVE);
    /* Enable mouse reporting the way the terminal's libvterm path expects:
     * DEC ?1500h (click) + ?1501h (move) set VTERM_PROP_CARDCLICK/CARDMOVE, so
     * the terminal hit-tests the cursor against figures and forwards
     * FIGURE_MOUSE events to ours. Without this the terminal never forwards. */
    {
        static const char mouse_on[] = "\x1b[?1500h\x1b[?1501h";
        (void)yflame_write(YFLAME_STDOUT_FD, mouse_on, sizeof(mouse_on) - 1);
    }

    rerender(&ui);

    char buf[8192];
    while (!g_signal_quit && !ui.want_quit) {
        int rdy = yetty_yplatform_tty_stdin_wait(200);
        if (rdy < 0) {
            break;
        }
        ui.dirty = false;
        if (rdy > 0) {
            int n = yetty_yplatform_tty_stdin_read(buf, sizeof(buf));
            if (n > 0) {
                (void)yetty_yface_feed_bytes(yface, buf, (size_t)n);
            } else if (n == 0 && !yetty_yplatform_tty_stdin_is_tty()) {
                break;
            }
        }
        if (ui.dirty) {
            rerender(&ui);
        }
    }

    /* Teardown: disable mouse reporting, drop the figure, unsubscribe, restore. */
    {
        static const char mouse_off[] = "\x1b[?1500l\x1b[?1501l";
        (void)yflame_write(YFLAME_STDOUT_FD, mouse_off, sizeof(mouse_off) - 1);
    }
    subscribe_input(YFLAME_STDOUT_FD, 0u);
    (void)yetty_yview_destroy(ui.view);
    yetty_yplatform_tty_restore();
    yetty_yface_destroy(yface);
    (void)yetty_yflame_destroy(flame);
    return 0;
}
