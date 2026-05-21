#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <yetty/yplatform/platform-input-pipe.h>
#include <yetty/yplatform/pty.h>
#include <yetty/yplatform/pty-pipe-source.h>
#include <yetty/yplatform/pty.h>
#include <yetty/yplatform/clipboard-manager.h>
#include <yetty/yconfig/config.h>
#include <yetty/yevent/event-loop.h>
#include <yetty/yevent/event.h>
#include <yetty/yface/yface.h>
#include <yetty/ymgui/wire.h>
#include <yetty/yrdawn/wire.h>
#include <yetty/yrender/gpu-allocator.h>
#include <yetty/yrender/gpu-resource-set.h>
#include <yetty/yrender/render-target.h>
#include <yetty/yterm/osc-codes.h>
#include <yetty/ywire/wire-statemachine.h>
#include <yetty/yterm/terminal.h>
#include <yetty/yterm/text-layer.h>
#include <yetty/yterm/ydraw-layer.h>
#include <yetty/yterm/ymgui-layer.h>
#include <yetty/yterm/yrdawn-layer.h>
#include <yetty/yterm/shader-glyph-layer.h>
#include <yetty/ytrace/ytrace.h>
#include <yetty/yui-core/view.h>

#define YETTY_YTERM_TERMINAL_MAX_LAYERS 256

/* GLFW modifier bit layout — kept in sync with src/yetty/yetty/yetty.c.
 * Used here for clipboard shortcuts (Ctrl+Shift+C/V). */
#define YETTY_MOD_SHIFT 0x0001
#define YETTY_MOD_CONTROL 0x0002

/* Forward declarations for view ops */
static struct yetty_ycore_void_result terminal_view_destroy(struct yetty_yui_view *view);
static struct yetty_ycore_void_result terminal_view_render(
    struct yetty_yui_view *view, struct yetty_ydraw_target *render_target, int force_redraw);
static struct yetty_ycore_void_result terminal_view_set_bounds(struct yetty_yui_view *view,
                                                               struct yetty_yui_rect bounds);
static struct yetty_ycore_int_result terminal_view_on_event(struct yetty_yui_view *view,
                                                            const struct yetty_yui_event *event);

static const struct yetty_yui_view_ops terminal_view_ops = {
    .destroy = terminal_view_destroy,
    .render = terminal_view_render,
    .set_bounds = terminal_view_set_bounds,
    .on_event = terminal_view_on_event,
};

struct yetty_yterm_terminal {
    struct yetty_yui_view view; /* MUST be first - allows cast to view */
    struct yetty_yevent_event_listener listener;
    struct yetty_yterm_terminal_context context;
    uint32_t cols;
    uint32_t rows;
    /* Set by workspace_set_active via SET_FOCUS — true means this terminal
     * is the foreground view in its workspace AND the workspace is the
     * tabbar's active one. Layers can read this to switch cursor style
     * (block vs hollow), and we'll forward FocusIn/FocusOut CSI to the
     * PTY once focus reporting (DECSET 1004) is wired through. */
    int focused;
    struct yetty_yrender_terminal_layer *layers[YETTY_YTERM_TERMINAL_MAX_LAYERS];
    size_t layer_count;
    yetty_yevent_pipe_id pty_pipe_id;
    /* Render targets - one per layer for render_layer */
    struct yetty_ydraw_target *layer_targets[YETTY_YTERM_TERMINAL_MAX_LAYERS];
    int shutting_down;
    struct yetty_ywire_wire_statemachine *sm;

    /* Reusable read buffer handed back from terminal_pty_pipe_alloc.
     * Lazily allocated on the first read; freed in destroy. */
    char *pty_read_buf;

    /* Pixel-precise mouse forwarding (DEC ?1500/?1501; OSC 777777/777778).
   * The text-layer's libvterm settermprop hook flips these and reports
   * via terminal_mouse_sub_callback, which also emits OSC 777780 with
   * the current pixel size on the rising edge so the client can layout. */
    int mouse_click_subscribed;
    int mouse_move_subscribed;
    int mouse_buttons_held; /* OR of (1 << button) for currently-down buttons */

    /* Terminal-wide input subscription (YMGUI_OSC_CS_TERM_INPUT_SUB).
     * Independent from the card-aware path above: when set, every mouse /
     * key event ALSO fans out as YMGUI_OSC_SC_TERM_* tagged with card_id=0
     * and pane-local pixel coords. Programs that draw their own UI
     * directly into the pane (browser, file manager) subscribe via this
     * channel instead of registering a card. */
    uint32_t term_input_sub_flags;

    /* Long-lived yface for emitting input events to the inferior over the
   * PTY. Reused across every emit; out_buf is cleared after each write. */
    struct yetty_yface *emit_yface;

    /* tmux-style scrollback view. Mouse wheel enters scrollback and shifts
   * the absolute viewport top (view_top_total_idx). Enter exits back to
   * live. While active, both layers freeze their viewport at this index
   * even as new content keeps arriving. */
    int scrollback_active;
    uint32_t view_top_total_idx;

    /* The ymgui layer (cards). Cached during creation so the mouse / KB
   * handlers can hit-test cards without scanning the layers array.
   * Borrowed pointer — owned by the layers[] array. */
    struct yetty_yrender_terminal_layer *ymgui_layer;

    /* Cell-precise selection state.
     *
     * The terminal tracks one (anchor, head) pair in visible-grid
     * coordinates and broadcasts it to every layer via set_selection.
     * Each layer decides what to do with it:
     *   - text-layer interprets it as an xterm-style cell stream
     *     (column-granular highlight + extract)
     *   - ydraw-layer treats the row range as "touched rows" and
     *     selects the first overlapping primitive per row
     * sel_dragging tracks an in-progress button-1 drag. */
    int sel_active;
    int sel_dragging;
    uint32_t sel_anchor_row;
    uint32_t sel_anchor_col;
    uint32_t sel_head_row;
    uint32_t sel_head_col;
};

/* How many lines a single mouse-wheel notch moves the scrollback view. */
#define YETTY_YTERM_WHEEL_LINES_PER_TICK 3

/* Forward declarations */
static struct yetty_ycore_void_result terminal_read_pty(struct yetty_yterm_terminal *terminal);
static struct yetty_ycore_void_result terminal_render_frame(
    struct yetty_yterm_terminal *terminal, struct yetty_ydraw_target *target, int force_redraw);

/* PTY pipe alloc callback — provides buffer for uv_pipe_t reads.
 * One reusable per-terminal buffer, lazily allocated. 64KB matches
 * libuv's default suggested_size on Linux/macOS and is plenty for one
 * PTY chunk; the read callback drains it before the next call. */
#define YETTY_YTERM_PTY_READ_BUF_SIZE (64 * 1024)

/* libuv-shaped buffer-alloc callback: signature dictated by yetty_pipe_alloc_cb
 * which is dispatched from the libuv read path. Cannot return a Result. */
YETTY_EXTERNAL_CALLBACK
static void terminal_pty_pipe_alloc(void *ctx, size_t suggested_size, char **buf, size_t *buflen)
{
    (void)suggested_size;
    struct yetty_yterm_terminal *terminal = ctx;
    if (!terminal->pty_read_buf) {
        terminal->pty_read_buf = malloc(YETTY_YTERM_PTY_READ_BUF_SIZE);
        if (!terminal->pty_read_buf) {
            *buf = NULL;
            *buflen = 0;
            return;
        }
    }
    *buf = terminal->pty_read_buf;
    *buflen = YETTY_YTERM_PTY_READ_BUF_SIZE;
}

/* libuv-shaped pipe-read callback. Errors from feed/process have no
 * Result to propagate to — absorb them at this boundary by logging the
 * full chain and destroying it. */
YETTY_EXTERNAL_CALLBACK
static void terminal_pty_pipe_read(void *ctx, const char *buf, long nread)
{
    struct yetty_yterm_terminal *terminal = ctx;

    if (nread > 0) {
        /* Dump first/last bytes as hex+ascii to see what ConPTY sent */
        char hex[512] = {0};
        char asc[256] = {0};
        size_t dump_n = nread > 80 ? 80 : (size_t)nread;
        size_t hoff = 0, aoff = 0;
        for (size_t i = 0; i < dump_n; i++) {
            unsigned char c = (unsigned char)buf[i];
            if (hoff + 4 < sizeof(hex)) {
                hoff += (size_t)snprintf(hex + hoff, sizeof(hex) - hoff, "%02x ", c);
            }
            if (aoff + 2 < sizeof(asc)) {
                asc[aoff++] = (c >= 0x20 && c < 0x7f) ? (char)c : '.';
            }
        }
        ydebug("terminal_pty_pipe_read: nread=%ld dump=[%s] ascii=[%s]", nread, hex, asc);
        /* feed() now resumes the SM coro itself — no separate process()
         * call needed. The SM scanner runs as far as it can, then yields
         * back when it needs more bytes (or surfaces a fatal error). */
        struct yetty_ycore_void_result fr =
            yetty_ywire_wire_statemachine_feed(terminal->sm, buf, (size_t)nread);
        if (YETTY_IS_ERR(fr)) {
            /* libuv-callback boundary: no Result to propagate. Park the
             * error on the event loop; libuv_start will surface it from
             * its own return, which yetty_run propagates to main.
             * Build via YETTY_ERR so the loop frame gets file/line/func. */
            struct yetty_ycore_void_result wrap = YETTY_ERR(
                yetty_ycore_void,
                "terminal_pty_pipe_read: wire_statemachine_feed failed", fr);
            struct yetty_yevent_event_loop *loop =
                terminal->context.yetty_context.event_loop;
            loop->ops->post_fatal_error(loop, wrap.error);
            return;
        }
        if (terminal->layer_count > 0) {
            struct yetty_yrender_terminal_layer *layer = terminal->layers[0];
            ydebug("terminal_pty_pipe_read: after feed layer=%p dirty=%d", (void *)layer,
                   layer ? layer->dirty : -1);
            if (layer && layer->dirty) {
                terminal->context.yetty_context.event_loop->ops->request_render(
                    terminal->context.yetty_context.event_loop);
            }
        }
    } else if (nread < 0 && !terminal->shutting_down) {
        /* PTY closed (UV_EOF / read error): the child shell exited — typically
     * Ctrl-D in the prompt, or the user typed `exit`. Trigger the same
     * graceful teardown as window-close and SIGINT by posting SHUTDOWN
     * through the platform input pipe.
     *
     * The shutting_down guard avoids re-posting if SHUTDOWN was already
     * issued (e.g. fork_pty_stop closed the master while we were tearing
     * down for another reason).
     *
     * NOTE: today there is exactly one terminal per yetty instance, so
     * "PTY closed" is always "the last terminal closed". When multi-
     * terminal support lands, this should walk the workspace tree and
     * only post SHUTDOWN if no other live terminal remains. */
        ydebug("terminal_pty_pipe_read: PTY EOF (nread=%ld), posting SHUTDOWN", nread);
        struct yetty_ycore_xthread_event_pipe *pipe =
            terminal->context.yetty_context.app_context.platform_input_pipe;
        if (pipe && pipe->ops && pipe->ops->write) {
            struct yetty_yui_event ev = {.type = YETTY_YCORE_SHUTDOWN};
            pipe->ops->write(pipe, &ev, sizeof(ev));
        }
    } else {
        ydebug("terminal_pty_pipe_read: skipped (nread=%ld)", nread);
    }
}

/* Direct PTY write — shared by the layer callback (text-layer keypress
 * forwarding) and the direct emitter path (mouse-OSC, paste). The PTY ops
 * table is populated by the backend at creation time; absence of `write`
 * is a build/configuration bug, not a runtime condition to silence. */
static struct yetty_ycore_size_result terminal_pty_write_raw(
    struct yetty_yterm_terminal *terminal, const char *data, size_t len)
{
    if (!terminal->context.pty->ops->write) {
        return YETTY_ERR(yetty_ycore_size,
                         "terminal_pty_write_raw: PTY backend has no `write` op");
    }
    return terminal->context.pty->ops->write(terminal->context.pty, data, len);
}

/* yetty_yterm_pty_write_fn impl — adapts the Result-returning PTY op
 * (size_result) to the typedef (void_result). */
static struct yetty_ycore_void_result terminal_pty_write_callback(
    const char *data, size_t len, void *userdata)
{
    struct yetty_yterm_terminal *terminal = userdata;
    struct yetty_ycore_size_result r = terminal_pty_write_raw(terminal, data, len);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r,
                        "terminal_pty_write_callback: pty_write_raw failed");
    ydebug("terminal_pty_write_callback: wrote %zu bytes to PTY", len);
    return YETTY_OK_VOID();
}

/* Build one yface envelope around `payload` and ship it to the inferior.
 * compressed=0 because input events are short — LZ4 framing would dominate.
 *
 * The yface out buffer is reused across calls; success path drains it via
 * pty_write_raw, error paths clear it explicitly so the next call starts
 * from a known state. */
static struct yetty_ycore_void_result terminal_yface_emit(
    struct yetty_yterm_terminal *terminal, int osc_code,
    const void *payload, size_t len)
{
    struct yetty_ycore_void_result sr = yetty_yface_start_write(
        terminal->emit_yface, osc_code, /*compressed=*/0, /*args=*/NULL, /*args_len=*/0);
    if (YETTY_IS_ERR(sr)) {
        struct yetty_ycore_buffer *out_buf = yetty_yface_out_buf(terminal->emit_yface);
        if (out_buf) {
            yetty_ycore_buffer_clear(out_buf);
        }
        return YETTY_ERR(yetty_ycore_void,
                         "terminal_yface_emit: yface_start_write failed", sr);
    }
    struct yetty_ycore_void_result wr = yetty_yface_write(terminal->emit_yface, payload, len);
    if (YETTY_IS_ERR(wr)) {
        struct yetty_ycore_buffer *out_buf = yetty_yface_out_buf(terminal->emit_yface);
        if (out_buf) {
            yetty_ycore_buffer_clear(out_buf);
        }
        return YETTY_ERR(yetty_ycore_void,
                         "terminal_yface_emit: yface_write failed", wr);
    }
    struct yetty_ycore_void_result fr = yetty_yface_finish_write(terminal->emit_yface);
    if (YETTY_IS_ERR(fr)) {
        struct yetty_ycore_buffer *out_buf = yetty_yface_out_buf(terminal->emit_yface);
        if (out_buf) {
            yetty_ycore_buffer_clear(out_buf);
        }
        return YETTY_ERR(yetty_ycore_void,
                         "terminal_yface_emit: yface_finish_write failed", fr);
    }

    struct yetty_ycore_buffer *out = yetty_yface_out_buf(terminal->emit_yface);
    if (out && out->size) {
        /* Loop until every byte is on the PTY. The backend's write op
         * is non-looping — one write(2) per call — so a short write
         * (typical when the PTY's kernel buffer fills) drops the tail
         * unless we keep going. Single-shot is fine for small OSC
         * messages but the bridge's REPLY payloads carry up to a
         * full readback buffer of pixels. */
        size_t off = 0;
        while (off < out->size) {
            struct yetty_ycore_size_result pwr = terminal_pty_write_raw(
                terminal, (const char *)out->data + off, out->size - off);
            if (YETTY_IS_ERR(pwr)) {
                yetty_ycore_buffer_clear(out);
                return YETTY_ERR(yetty_ycore_void,
                                 "terminal_yface_emit: pty_write_raw failed", pwr);
            }
            if (pwr.value == 0) {
                /* EAGAIN territory — back off briefly so we don't spin
                 * the main thread while the kernel drains the buffer. */
                struct timespec ts = {.tv_sec = 0, .tv_nsec = 100000L};
                (void)nanosleep(&ts, NULL);
                continue;
            }
            off += pwr.value;
        }
        yetty_ycore_buffer_clear(out);
    }
    return YETTY_OK_VOID();
}

/* yetty_yterm_emit_osc_fn impl — wired into ymgui-layer at create time so
 * the layer can ship FOCUS / RESIZE events back to the focused client
 * without owning its own emit_yface. */
static struct yetty_ycore_void_result terminal_layer_emit_osc(
    int osc_code, const void *payload, size_t len, void *userdata)
{
    struct yetty_yterm_terminal *terminal = userdata;
    struct yetty_ycore_void_result r =
        terminal_yface_emit(terminal, osc_code, payload, len);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "terminal_layer_emit_osc: yface_emit failed");
    return YETTY_OK_VOID();
}

/* Forward the layer's "switch PTY to raw / restore" intent to the
 * platform PTY backend. yrdawn-layer calls this on HELLO/BYE so the
 * bridge protocol bytes don't get mangled by any cooked-mode hop
 * between yetty and the demo (ssh, screen, tmux, the user's local
 * shell). Backends without termios (memory-pty, webasm) leave the
 * ops NULL and we silently skip — no-op is the right behaviour. */
static struct yetty_ycore_void_result terminal_set_pty_raw(int enable, void *userdata)
{
    struct yetty_yterm_terminal *terminal = userdata;
    if (!terminal || !terminal->context.pty || !terminal->context.pty->ops)
        return YETTY_OK_VOID();
    if (enable) {
        if (!terminal->context.pty->ops->set_raw) return YETTY_OK_VOID();
        return terminal->context.pty->ops->set_raw(terminal->context.pty);
    }
    if (!terminal->context.pty->ops->restore_termios) return YETTY_OK_VOID();
    return terminal->context.pty->ops->restore_termios(terminal->context.pty);
}

/* Card-aware mouse forwarding. Each emit carries a card_id and
 * card-local pixel coords. card_id=0 means "no card here" — clients
 * use that to clear their hover state. */
static struct yetty_ycore_void_result terminal_emit_card_mouse_button(
    struct yetty_yterm_terminal *terminal, uint32_t card_id,
    float lx, float ly, int button, int press, float wheel_dy)
{
    struct yetty_ymgui_wire_input_mouse msg = {
        .magic = YMGUI_WIRE_MAGIC_INPUT_MOUSE,
        .version = YMGUI_WIRE_VERSION,
        .card_id = card_id,
        .x = lx,
        .y = ly,
    };
    if (wheel_dy != 0.0f) {
        msg.kind = YETTY_YMGUI_INPUT_MOUSE_WHEEL;
        msg.button = -1;
        msg.wheel_dy = wheel_dy;
    } else {
        msg.kind = YETTY_YMGUI_INPUT_MOUSE_BUTTON;
        msg.button = button;
        msg.pressed = press;
    }
    struct yetty_ycore_void_result r =
        terminal_yface_emit(terminal, YMGUI_OSC_SC_MOUSE, &msg, sizeof(msg));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "terminal_emit_card_mouse_button: yface_emit failed");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result terminal_emit_card_mouse_move(
    struct yetty_yterm_terminal *terminal, uint32_t card_id,
    float lx, float ly, int buttons_held)
{
    struct yetty_ymgui_wire_input_mouse msg = {
        .magic = YMGUI_WIRE_MAGIC_INPUT_MOUSE,
        .version = YMGUI_WIRE_VERSION,
        .card_id = card_id,
        .kind = YETTY_YMGUI_INPUT_MOUSE_POS,
        .button = -1,
        .buttons_held = (uint32_t)buttons_held,
        .x = lx,
        .y = ly,
    };
    struct yetty_ycore_void_result r =
        terminal_yface_emit(terminal, YMGUI_OSC_SC_MOUSE, &msg, sizeof(msg));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "terminal_emit_card_mouse_move: yface_emit failed");
    return YETTY_OK_VOID();
}

/* Terminal-wide variants — same wire structs, card_id=0, pane-local px.
 * Each is gated on a bit of terminal->term_input_sub_flags so an
 * uninterested subscriber doesn't receive (e.g.) wheel events when it
 * only asked for clicks + moves. */
static struct yetty_ycore_void_result terminal_emit_term_mouse_button(
    struct yetty_yterm_terminal *terminal, float lx, float ly,
    int button, int press, float wheel_dy)
{
    int is_wheel = (wheel_dy != 0.0f);
    uint32_t need = is_wheel ? YETTY_YMGUI_TERM_SUB_MOUSE_WHEEL : YETTY_YMGUI_TERM_SUB_MOUSE_CLICK;
    if (!(terminal->term_input_sub_flags & need)) {
        return YETTY_OK_VOID();
    }
    struct yetty_ymgui_wire_input_mouse msg = {
        .magic = YMGUI_WIRE_MAGIC_INPUT_MOUSE,
        .version = YMGUI_WIRE_VERSION,
        .card_id = 0,
        .x = lx,
        .y = ly,
    };
    if (is_wheel) {
        msg.kind = YETTY_YMGUI_INPUT_MOUSE_WHEEL;
        msg.button = -1;
        msg.wheel_dy = wheel_dy;
    } else {
        msg.kind = YETTY_YMGUI_INPUT_MOUSE_BUTTON;
        msg.button = button;
        msg.pressed = press;
    }
    struct yetty_ycore_void_result r =
        terminal_yface_emit(terminal, YMGUI_OSC_SC_TERM_MOUSE, &msg, sizeof(msg));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "terminal_emit_term_mouse_button: yface_emit failed");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result terminal_emit_term_mouse_move(
    struct yetty_yterm_terminal *terminal, float lx, float ly, int buttons_held)
{
    if (!(terminal->term_input_sub_flags & YETTY_YMGUI_TERM_SUB_MOUSE_MOVE)) {
        return YETTY_OK_VOID();
    }
    struct yetty_ymgui_wire_input_mouse msg = {
        .magic = YMGUI_WIRE_MAGIC_INPUT_MOUSE,
        .version = YMGUI_WIRE_VERSION,
        .card_id = 0,
        .kind = YETTY_YMGUI_INPUT_MOUSE_POS,
        .button = -1,
        .buttons_held = (uint32_t)buttons_held,
        .x = lx,
        .y = ly,
    };
    struct yetty_ycore_void_result r =
        terminal_yface_emit(terminal, YMGUI_OSC_SC_TERM_MOUSE, &msg, sizeof(msg));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "terminal_emit_term_mouse_move: yface_emit failed");
    return YETTY_OK_VOID();
}

/* Pane pixel size — emitted on subscribe (rising edge) and on resize so
 * subscribers can size their canvases. card_id=0 = pane-wide. */
static struct yetty_ycore_void_result terminal_emit_term_resize(
    struct yetty_yterm_terminal *terminal, float w_px, float h_px)
{
    if (!terminal->term_input_sub_flags) {
        return YETTY_OK_VOID();
    }
    struct yetty_ymgui_wire_input_resize msg = {
        .magic = YMGUI_WIRE_MAGIC_INPUT_RESIZE,
        .version = YMGUI_WIRE_VERSION,
        .card_id = 0,
        .width = w_px,
        .height = h_px,
    };
    struct yetty_ycore_void_result r =
        terminal_yface_emit(terminal, YMGUI_OSC_SC_TERM_RESIZE, &msg, sizeof(msg));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "terminal_emit_term_resize: yface_emit failed");
    return YETTY_OK_VOID();
}

/* Term-wide keyboard. Sent for any key (including with no card focused),
 * so console programs can do their own focus tracking. card_id=0. */
static struct yetty_ycore_void_result terminal_emit_term_key(
    struct yetty_yterm_terminal *terminal, uint32_t kind, int key,
    int mods, uint32_t codepoint)
{
    if (!(terminal->term_input_sub_flags & YETTY_YMGUI_TERM_SUB_KEY)) {
        return YETTY_OK_VOID();
    }
    struct yetty_ymgui_wire_input_key msg = {
        .magic = YMGUI_WIRE_MAGIC_INPUT_KEY,
        .version = YMGUI_WIRE_VERSION,
        .card_id = 0,
        .kind = kind,
        .key = key,
        .mods = mods,
        .codepoint = codepoint,
    };
    struct yetty_ycore_void_result r =
        terminal_yface_emit(terminal, YMGUI_OSC_SC_TERM_KEY, &msg, sizeof(msg));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "terminal_emit_term_key: yface_emit failed");
    return YETTY_OK_VOID();
}

/* Resolve the pane-pixel point (lx, ly) to a card-local hit. If a card
 * is "captured" (drag in progress), the captured card always wins and
 * coords are reported as-if-projected into that card's local space.
 * Otherwise the topmost visible card under the cursor wins. */
static struct yetty_yterm_ymgui_hit terminal_resolve_card_hit(struct yetty_yterm_terminal *terminal,
                                                              float lx, float ly,
                                                              uint32_t captured_card_id)
{
    struct yetty_yterm_ymgui_hit hit = {0, 0, 0};
    if (!terminal->ymgui_layer) {
        return hit;
    }

    if (captured_card_id != 0) {
        /* Drag: route to the captured card; project the cursor into its
     * local space even when the cursor leaves the card's rect. */
        hit = yetty_yterm_terminal_layer_ymgui_layer_hit_test(terminal->ymgui_layer, lx, ly);
        if (hit.card_id == captured_card_id) {
            return hit;
        }
        /* Cursor left the captured card. Report local coords by re-asking
     * the layer for the captured card's origin via a dummy probe at
     * its own anchor — but the layer only has hit_test today, so fall
     * back to (lx, ly) tagged with the captured id; clients clamp. */
        struct yetty_yterm_ymgui_hit captured = {captured_card_id, lx, ly};
        return captured;
    }

    return yetty_yterm_terminal_layer_ymgui_layer_hit_test(terminal->ymgui_layer, lx, ly);
}

/* Emit a keyboard event for the focused card. Returns 1 if delivered
 * (caller treats the keystroke as consumed), 0 otherwise. Emit failures
 * propagate via the Result. */
static struct yetty_ycore_int_result terminal_emit_card_key(
    struct yetty_yterm_terminal *terminal, uint32_t kind, int key,
    int mods, uint32_t codepoint)
{
    uint32_t focused =
        terminal->ymgui_layer
            ? yetty_yterm_terminal_layer_ymgui_layer_focused_card(terminal->ymgui_layer)
            : 0;
    if (focused == 0) {
        return YETTY_OK(yetty_ycore_int, 0);
    }

    struct yetty_ymgui_wire_input_key msg = {
        .magic = YMGUI_WIRE_MAGIC_INPUT_KEY,
        .version = YMGUI_WIRE_VERSION,
        .card_id = focused,
        .kind = kind,
        .key = key,
        .mods = mods,
        .codepoint = codepoint,
    };
    struct yetty_ycore_void_result r =
        terminal_yface_emit(terminal, YMGUI_OSC_SC_KEY, &msg, sizeof(msg));
    YETTY_RETURN_IF_ERR(yetty_ycore_int, r, "terminal_emit_card_key: yface_emit failed");
    return YETTY_OK(yetty_ycore_int, 1);
}

/*-----------------------------------------------------------------------
 * Scrollback view (tmux-style copy mode)
 *
 * Mouse-wheel events drive both layers into scrollback mode together.
 * view_top_total_idx is an absolute line index — text-layer's sb_count
 * and ydraw canvas's rolling_row_0 stay in lockstep (every text scroll
 * triggers a ydraw scroll and vice versa), so the same index identifies
 * the same line in both.
 *
 * PR #89 ("Ymgui 5") rewrote the OSC mouse path to forward wheel events
 * out to the inferior as binary mouse messages. That removed the only
 * caller of the canvas/text-layer set_view_top APIs and the scrollback
 * regressed to dead code on origin/main. The four helpers below restore
 * the wheel→scrollback driver while leaving #89's outbound mouse OSC
 * behaviour untouched: when no client is subscribed (vim/less/mc not
 * consuming clicks), wheel drives scrollback; when subscribed, wheel
 * goes outbound. See YETTY_EVENT_SCROLL handler.
 *---------------------------------------------------------------------*/

/* Find the live anchor across layers. We use the maximum so a layer that
 * has scrolled further (e.g. ydraw just absorbed a multi-page PDF) doesn't
 * leave the others behind — both layers have the same anchor by design,
 * but max() is a safe fallback in case they ever drift. */
static uint32_t terminal_live_anchor(struct yetty_yterm_terminal *terminal)
{
    uint32_t anchor = 0;
    for (size_t i = 0; i < terminal->layer_count; i++) {
        struct yetty_yrender_terminal_layer *layer = terminal->layers[i];
        if (layer && layer->ops && layer->ops->get_live_anchor) {
            uint32_t a = layer->ops->get_live_anchor(layer);
            if (a > anchor) {
                anchor = a;
            }
        }
    }
    return anchor;
}

/* Push the current scrollback view state to every layer that supports it. */
static struct yetty_ycore_void_result terminal_push_view_top(struct yetty_yterm_terminal *terminal)
{
    for (size_t i = 0; i < terminal->layer_count; i++) {
        struct yetty_yrender_terminal_layer *layer = terminal->layers[i];
        if (layer && layer->ops && layer->ops->set_view_top) {
            struct yetty_ycore_void_result r = layer->ops->set_view_top(
                layer, terminal->scrollback_active, terminal->view_top_total_idx);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, r,
                                "terminal_push_view_top: layer set_view_top failed");
        }
    }
    terminal->context.yetty_context.event_loop->ops->request_render(
        terminal->context.yetty_context.event_loop);
    return YETTY_OK_VOID();
}

/* Apply a relative wheel delta. Positive lines = scroll up (older). On the
 * first wheel-up out of live mode we anchor view_top one line back from
 * the current live position and enter scrollback. Scrolling past the live
 * anchor exits back to live. */
static struct yetty_ycore_void_result terminal_scrollback_apply(
    struct yetty_yterm_terminal *terminal, int lines)
{
    uint32_t live = terminal_live_anchor(terminal);

    if (!terminal->scrollback_active) {
        if (lines <= 0) {
            return YETTY_OK_VOID(); /* downward wheel in live mode: nothing to do */
        }
        if (live == 0) {
            return YETTY_OK_VOID(); /* nothing in scrollback yet */
        }
        terminal->scrollback_active = 1;
        terminal->view_top_total_idx = live - 1;
        if ((uint32_t)lines > 1) {
            lines -= 1; /* the entry already consumed one notch */
        } else {
            lines = 0;
        }
    }

    if (lines > 0) {
        if ((uint32_t)lines > terminal->view_top_total_idx) {
            terminal->view_top_total_idx = 0;
        } else {
            terminal->view_top_total_idx -= (uint32_t)lines;
        }
    } else if (lines < 0) {
        uint32_t n = (uint32_t)(-lines);
        uint64_t target = (uint64_t)terminal->view_top_total_idx + n;
        if (target >= live) {
            /* Scrolled forward into the live region — exit scrollback. */
            terminal->scrollback_active = 0;
            terminal->view_top_total_idx = live;
        } else {
            terminal->view_top_total_idx = (uint32_t)target;
        }
    }

    ydebug("scrollback: active=%d view_top=%u live=%u", terminal->scrollback_active,
           terminal->view_top_total_idx, live);
    struct yetty_ycore_void_result r = terminal_push_view_top(terminal);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "terminal_scrollback_apply: push_view_top failed");
    return YETTY_OK_VOID();
}

/* Force a return to live, regardless of current view position. */
static struct yetty_ycore_void_result terminal_scrollback_exit(struct yetty_yterm_terminal *terminal)
{
    if (!terminal->scrollback_active) {
        return YETTY_OK_VOID();
    }
    terminal->scrollback_active = 0;
    terminal->view_top_total_idx = terminal_live_anchor(terminal);
    ydebug("scrollback: EXIT");
    struct yetty_ycore_void_result r = terminal_push_view_top(terminal);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "terminal_scrollback_exit: push_view_top failed");
    return YETTY_OK_VOID();
}

/* yetty_yterm_mouse_sub_fn impl — fired by the text-layer when libvterm
 * flips DEC mode 1500/1501. Latch state on the terminal. (No pane-size
 * emission on the rising edge — under the card model, each CARD_PLACE
 * confirms the card's pixel size via YMGUI_OSC_SC_RESIZE individually.) */
static struct yetty_ycore_void_result terminal_mouse_sub_callback(
    int click_enabled, int move_enabled, void *userdata)
{
    struct yetty_yterm_terminal *terminal = userdata;
    terminal->mouse_click_subscribed = click_enabled;
    terminal->mouse_move_subscribed = move_enabled;
    ydebug("terminal: mouse_sub click=%d move=%d", click_enabled, move_enabled);
    return YETTY_OK_VOID();
}

/* yetty_yterm_term_input_sub_fn impl — YMGUI_OSC_CS_TERM_INPUT_SUB arrived
 * (forwarded by ymgui-layer). Latch the new flag bitmask, and on the rising
 * edge of any subscription emit a YMGUI_OSC_SC_TERM_RESIZE so the
 * subscriber knows the pane pixel size before the first event arrives. */
static struct yetty_ycore_void_result terminal_term_input_sub_callback(
    uint32_t flags, void *userdata)
{
    struct yetty_yterm_terminal *terminal = userdata;
    uint32_t prev = terminal->term_input_sub_flags;
    terminal->term_input_sub_flags = flags;
    ydebug("terminal: term_input_sub flags=0x%x (was 0x%x)", flags, prev);

    /* Rising edge → emit pane pixel size now so the subscriber can size
     * its canvas before the first event arrives. Bounds come from the
     * view already used by the mouse handlers above (terminal->view). */
    if (flags && !prev) {
        struct yetty_ycore_void_result r =
            terminal_emit_term_resize(terminal, terminal->view.bounds.w, terminal->view.bounds.h);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r,
                            "terminal_term_input_sub_callback: emit_term_resize failed");
    }
    return YETTY_OK_VOID();
}

/* yetty_yterm_alt_screen_fn impl — alt-screen toggle from text-layer
 * (libvterm). Broadcast to every layer that implements set_alt_screen so
 * each can save/restore. */
static struct yetty_ycore_void_result terminal_alt_screen_callback(int active, void *userdata)
{
    struct yetty_yterm_terminal *terminal = userdata;
    ydebug("terminal: alt_screen=%d (broadcasting to %zu layers)", active, terminal->layer_count);
    for (size_t i = 0; i < terminal->layer_count; i++) {
        struct yetty_yrender_terminal_layer *layer = terminal->layers[i];
        if (layer && layer->ops && layer->ops->set_alt_screen) {
            struct yetty_ycore_void_result r = layer->ops->set_alt_screen(layer, active);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, r,
                                "terminal_alt_screen_callback: layer set_alt_screen failed");
        }
    }
    terminal->context.yetty_context.event_loop->ops->request_render(
        terminal->context.yetty_context.event_loop);
    return YETTY_OK_VOID();
}

/* yetty_yterm_clear_screen_fn impl — full-screen erase from text-layer.
 * Each layer's clear_screen wipes the active half (primary or alt — the
 * layer's own alt-screen swap state already encodes which). No explicit
 * request_render here: this is invoked from inside libvterm's input
 * processing on the same feed that's about to fire on_damage for the
 * erased cells, and terminal_pty_pipe_read's after-feed dirty check
 * pumps the render. Calling request_render here causes a premature
 * frame that consumes text-layer dirty before subsequent in-feed
 * damages can accumulate. */
static struct yetty_ycore_void_result terminal_clear_screen_callback(void *userdata)
{
    struct yetty_yterm_terminal *terminal = userdata;
    ydebug("terminal: clear_screen (broadcasting to %zu layers)", terminal->layer_count);
    for (size_t i = 0; i < terminal->layer_count; i++) {
        struct yetty_yrender_terminal_layer *layer = terminal->layers[i];
        if (layer && layer->ops && layer->ops->clear_screen) {
            struct yetty_ycore_void_result r = layer->ops->clear_screen(layer);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, r,
                                "terminal_clear_screen_callback: layer clear_screen failed");
        }
    }
    return YETTY_OK_VOID();
}

/* yetty_yterm_request_render_fn impl — called when a layer needs a
 * render frame. */
static struct yetty_ycore_void_result terminal_request_render_callback(void *userdata)
{
    struct yetty_yterm_terminal *terminal = userdata;
    ydebug("terminal_request_render_callback: calling request_render");
    terminal->context.yetty_context.event_loop->ops->request_render(
        terminal->context.yetty_context.event_loop);
    return YETTY_OK_VOID();
}

/* Scroll callback - propagate scroll from source layer to all other layers */
static struct yetty_ycore_void_result terminal_scroll_callback(
    struct yetty_yrender_terminal_layer *source, int lines, void *userdata)
{
    struct yetty_yterm_terminal *terminal = userdata;
    ydebug("terminal_scroll_callback ENTER: source=%p lines=%d layer_count=%zu", (void *)source,
           lines, terminal->layer_count);

    for (size_t i = 0; i < terminal->layer_count; i++) {
        struct yetty_yrender_terminal_layer *layer = terminal->layers[i];
        if (layer == source) {
            continue;
        }
        if (layer->ops && layer->ops->scroll) {
            ydebug("terminal_scroll_callback: calling layer[%zu]=%p scroll(%d)", i, (void *)layer,
                   lines);
            layer->in_external_scroll = 1;
            struct yetty_ycore_void_result res = layer->ops->scroll(layer, lines);
            layer->in_external_scroll = 0;
            YETTY_RETURN_IF_ERR(yetty_ycore_void, res,
                                "terminal_scroll_callback: layer scroll failed");
        }
    }
    ydebug("terminal_scroll_callback EXIT: lines=%d", lines);
    return YETTY_OK_VOID();
}

/* yetty_yterm_cursor_fn impl — propagate cursor position from source
 * layer to all other layers. */
static struct yetty_ycore_void_result terminal_cursor_callback(
    struct yetty_yrender_terminal_layer *source,
    struct yetty_ycore_grid_cursor_pos cursor_pos, void *userdata)
{
    struct yetty_yterm_terminal *terminal = userdata;
    ydebug("terminal_cursor_callback ENTER: source=%p col=%u row=%u layer_count=%zu",
           (void *)source, cursor_pos.cols, cursor_pos.rows, terminal->layer_count);

    for (size_t i = 0; i < terminal->layer_count; i++) {
        struct yetty_yrender_terminal_layer *layer = terminal->layers[i];
        if (layer != source && layer->ops && layer->ops->set_cursor) {
            ydebug("terminal_cursor_callback: calling layer[%zu]=%p set_cursor(%u,%u)", i,
                   (void *)layer, cursor_pos.cols, cursor_pos.rows);
            struct yetty_ycore_void_result r =
                layer->ops->set_cursor(layer, cursor_pos.cols, cursor_pos.rows);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, r,
                                "terminal_cursor_callback: layer set_cursor failed");
        } else {
            ydebug("terminal_cursor_callback: skipping layer[%zu]=%p (source=%d "
                   "has_set_cursor=%d)",
                   i, (void *)layer, layer == source, layer->ops && layer->ops->set_cursor);
        }
    }
    ydebug("terminal_cursor_callback EXIT: col=%u row=%u", cursor_pos.cols, cursor_pos.rows);
    return YETTY_OK_VOID();
}

/*-------------------------------------------------------------------------
 * Cell-precise selection.
 *
 * The terminal owns one (anchor, head) pair in visible-grid coordinates
 * (row in [0, rows), col in [0, cols]). Each layer interprets it on its
 * own terms:
 *
 *   text-layer  — xterm-style cell stream (column-granular highlight + copy)
 *   ydraw-layer — row range only; selects whole primitives that overlap
 *                  the touched rows
 *
 * Copy walks every layer's get_selection_text in order and concatenates
 * their UTF-8 output. Paste pushes clipboard bytes straight to the PTY;
 * bracketed-paste mode is the shell/libvterm's responsibility.
 *-----------------------------------------------------------------------*/

/* Push the current (anchor, head) to every layer that opted in. Called
 * on every drag tick so the highlight tracks the mouse. */
static struct yetty_ycore_void_result terminal_push_selection(struct yetty_yterm_terminal *terminal)
{
    for (size_t i = 0; i < terminal->layer_count; i++) {
        struct yetty_yrender_terminal_layer *layer = terminal->layers[i];
        if (layer && layer->ops && layer->ops->set_selection) {
            struct yetty_ycore_void_result r = layer->ops->set_selection(
                layer, terminal->sel_active, terminal->sel_anchor_row,
                terminal->sel_anchor_col, terminal->sel_head_row, terminal->sel_head_col);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, r,
                                "terminal_push_selection: layer set_selection failed");
        }
    }
    terminal->context.yetty_context.event_loop->ops->request_render(
        terminal->context.yetty_context.event_loop);
    return YETTY_OK_VOID();
}

/* Translate a pane-local pixel coordinate to (row, col) in the visible
 * grid. col is allowed to reach `cols` (one past the last cell) to encode
 * "past EOL" — needed for "drag past the right edge selects to end of
 * line". row is clamped to [0, rows-1]; rows below the grid clamp to the
 * last row, matching how xterm extends selection downward. */
static void terminal_cell_from_local(const struct yetty_yterm_terminal *terminal, float lx,
                                     float ly, uint32_t *out_row, uint32_t *out_col)
{
    float cell_w = 10.0f;
    float cell_h = 20.0f;
    if (terminal->layer_count > 0 && terminal->layers[0]) {
        if (terminal->layers[0]->cell_size.width > 0.0f) {
            cell_w = terminal->layers[0]->cell_size.width;
        }
        if (terminal->layers[0]->cell_size.height > 0.0f) {
            cell_h = terminal->layers[0]->cell_size.height;
        }
    }
    if (lx < 0.0f) {
        lx = 0.0f;
    }
    if (ly < 0.0f) {
        ly = 0.0f;
    }
    uint32_t row = (uint32_t)(ly / cell_h);
    uint32_t col = (uint32_t)(lx / cell_w);
    if (terminal->rows > 0 && row >= terminal->rows) {
        row = terminal->rows - 1;
    }
    if (col > terminal->cols) {
        col = terminal->cols;
    }
    *out_row = row;
    *out_col = col;
}

/* Walk layers and concatenate each layer's selection-text contribution. */
static struct yetty_ycore_void_result terminal_collect_selection_text(
    struct yetty_yterm_terminal *terminal, struct yetty_ycore_buffer *out)
{
    if (!terminal->sel_active) {
        return YETTY_OK_VOID();
    }
    for (size_t i = 0; i < terminal->layer_count; i++) {
        struct yetty_yrender_terminal_layer *layer = terminal->layers[i];
        if (layer && layer->ops && layer->ops->get_selection_text) {
            struct yetty_ycore_void_result r = layer->ops->get_selection_text(layer, out);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "layer selection text");
        }
    }
    return YETTY_OK_VOID();
}

/* Ctrl+Shift+C — extract the selection, set it on the system clipboard,
 * leave the highlight in place so the user can verify. No-op when nothing
 * is selected or no clipboard manager exists (headless). */
static struct yetty_ycore_void_result terminal_copy_selection(
    struct yetty_yterm_terminal *terminal)
{
    if (!terminal->sel_active) {
        return YETTY_OK_VOID();
    }
    struct yetty_platform_clipboard_manager *cm =
        terminal->context.yetty_context.app_context.clipboard_manager;
    if (!cm || !cm->ops || !cm->ops->set_text) {
        ydebug("terminal_copy_selection: no clipboard manager");
        return YETTY_OK_VOID();
    }

    struct yetty_ycore_buffer_result br = yetty_ycore_buffer_create(256);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "terminal_copy_selection: buffer_create failed");
    struct yetty_ycore_buffer buf = br.value;

    struct yetty_ycore_void_result cr = terminal_collect_selection_text(terminal, &buf);
    if (YETTY_IS_ERR(cr)) {
        yetty_ycore_buffer_destroy(&buf);
        return YETTY_ERR(yetty_ycore_void,
                         "terminal_copy_selection: collect_selection_text failed", cr);
    }

    if (buf.size > 0) {
        struct yetty_ycore_void_result sr =
            cm->ops->set_text(cm, (const char *)buf.data, buf.size);
        if (YETTY_IS_ERR(sr)) {
            yetty_ycore_buffer_destroy(&buf);
            return YETTY_ERR(yetty_ycore_void,
                             "terminal_copy_selection: clipboard set_text failed", sr);
        }
        yinfo("terminal: copied %zu bytes to clipboard", buf.size);
    }
    yetty_ycore_buffer_destroy(&buf);
    return YETTY_OK_VOID();
}

/* Ctrl+Shift+V — kick off an asynchronous clipboard fetch. The result
 * comes back on the input pipe as a YETTY_YCORE_PASTE event whose
 * payload holds the text; the event handler below writes it to the
 * PTY. We don't block the render thread because GLFW clipboard calls
 * must run on the main thread. */
static struct yetty_ycore_void_result terminal_paste_clipboard(
    struct yetty_yterm_terminal *terminal)
{
    struct yetty_platform_clipboard_manager *cm =
        terminal->context.yetty_context.app_context.clipboard_manager;
    if (!cm || !cm->ops || !cm->ops->request_paste) {
        ydebug("terminal_paste_clipboard: no clipboard manager");
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result r = cm->ops->request_paste(cm);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r,
                        "terminal_paste_clipboard: clipboard request_paste failed");
    return YETTY_OK_VOID();
}

/* Clear any active selection and tell the layers to drop their highlight. */
static struct yetty_ycore_void_result terminal_clear_selection(
    struct yetty_yterm_terminal *terminal)
{
    if (!terminal->sel_active && !terminal->sel_dragging) {
        return YETTY_OK_VOID();
    }
    terminal->sel_active = 0;
    terminal->sel_dragging = 0;
    terminal->sel_anchor_row = 0;
    terminal->sel_anchor_col = 0;
    terminal->sel_head_row = 0;
    terminal->sel_head_col = 0;
    struct yetty_ycore_void_result r = terminal_push_selection(terminal);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r,
                        "terminal_clear_selection: push_selection failed");
    return YETTY_OK_VOID();
}

/* Event handler - only for PTY poll events registered directly with event loop
 */
static struct yetty_ycore_int_result terminal_event_handler(
    struct yetty_yevent_event_listener *listener, const struct yetty_yui_event *event)
{
    struct yetty_yterm_terminal *terminal =
        container_of(listener, struct yetty_yterm_terminal, listener);

    /* PTY data now arrives via uv_pipe_t read callback, not through events */
    (void)terminal;
    (void)event;

    return YETTY_OK(yetty_ycore_int, 0);
}

/* Render a frame using layered rendering.
 *
 * force_redraw: 1 when the root render (yetty_event_handler) detected
 * the yui scene-canvas is dirty this frame — the yui's chrome (cards,
 * dialogs, titlebar drag) is about to repaint and any pixels it
 * vacates would otherwise show its previous frame's content. Treated
 * the same as "any of our own layers is dirty": the two-pass scan
 * below seeds force=1 and every layer in this pane redraws. */
static struct yetty_ycore_void_result terminal_render_frame(struct yetty_yterm_terminal *terminal,
                                                            struct yetty_ydraw_target *target,
                                                            int force_redraw)
{
    if (terminal->shutting_down) {
        ydebug("terminal_render_frame: shutting down, skipping render");
        return YETTY_OK_VOID();
    }

    if (!target) {
        yerror("terminal_render_frame: no target provided");
        return YETTY_ERR(yetty_ycore_void, "no target provided");
    }

    ydebug("terminal_render_frame: starting");
    ytime_start(frame_render);

    /* Render all layers directly into the provided big_target — no per-layer
     * intermediate textures, no blend pass. The multi-layer blend round-trip
     * (4 × 33 MB layer RTs + 33 MB blend output read/written every frame at
     * 4K) starves the display compositor on tvOS.
     *
     * Every layer pass uses LoadOp_Load (hardcoded in render-target-texture
     * render_layer). The single per-frame wipe is the global clear() in
     * yetty_event_handler. No layer pass ever clears, so multiple panes
     * sharing the big target can't stomp each other — loadOp ignores
     * scissor, so a Clear would have wiped every other pane. */
    ytime_start(layers);
    /* Layers stack bottom→top (background → text → ydraw-scrolling →
     * ydraw-scene → shader-glyph → ymgui) and all paint into the same
     * big_target with LoadOp_Load — pixels persist frame to frame.
     *
     * Two passes:
     *
     *   1. Scan every layer's is_dirty. If ANY layer is dirty, start the
     *      render pass with force=1 — every layer below the dirty one
     *      must repaint so the dirty layer's old pixels are covered
     *      (e.g. ymgui window moves: ymgui is dirty, background + text
     *      + ydraw underneath must repaint the region the window
     *      vacated, otherwise the old window silhouette stays painted).
     *
     *   2. Render bottom→top. Each layer's render still returns 1 iff it
     *      drew, which propagates `force` upward to layers that might
     *      not have looked dirty themselves but whose pixels were just
     *      overwritten by a lower repaint.
     *
     * The asymmetry the previous single-pass missed: `force` only flowed
     * low→high. A dirty top layer never invalidated the layers below it,
     * so its stale pixels survived. */
    int force = force_redraw;
    for (size_t i = 0; !force && i < terminal->layer_count; i++) {
        struct yetty_yrender_terminal_layer *layer = terminal->layers[i];
        if (!layer) {
            return YETTY_ERR(yetty_ycore_void,
                             "terminal_render_frame: terminal->layers[i] is NULL");
        }
        if (layer->ops && layer->ops->is_empty && layer->ops->is_empty(layer)) {
            continue;
        }
        if (layer->ops && layer->ops->is_dirty && layer->ops->is_dirty(layer)) {
            force = 1;
        }
    }

    for (size_t i = 0; i < terminal->layer_count; i++) {
        struct yetty_yrender_terminal_layer *layer = terminal->layers[i];
        if (!layer) {
            return YETTY_ERR(yetty_ycore_void,
                             "terminal_render_frame: terminal->layers[i] is NULL");
        }
        if (!layer->ops || !layer->ops->render) {
            return YETTY_ERR(yetty_ycore_void,
                             "terminal_render_frame: layer has no render op");
        }
        /* Skip layers with nothing to draw at all (ydraw/ymgui report
         * empty when their canvas has no primitives). is_empty is an
         * optimisation only — a missing op is fine. */
        if (layer->ops->is_empty && layer->ops->is_empty(layer)) {
            continue;
        }
        struct yetty_ycore_int_result res = layer->ops->render(layer, target, force);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "terminal_render_frame: layer render failed");
        if (res.value == 1) {
            force = 1;
        }
    }
    ytime_report(layers);

    ydebug("terminal_render_frame: done (all %zu layers direct, no blend)", terminal->layer_count);
    ytime_report(frame_render);
    return YETTY_OK_VOID();
}

/* Drive the wire state machine — pulls PTY bytes (sync-read backends) and
 * dispatches to registered layers. */
static struct yetty_ycore_void_result terminal_read_pty(struct yetty_yterm_terminal *terminal)
{
    struct yetty_ycore_void_result r = yetty_ywire_wire_statemachine_process(terminal->sm);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r,
                        "terminal_read_pty: wire_statemachine_process failed");
    if (terminal->layer_count > 0) {
        struct yetty_yrender_terminal_layer *layer = terminal->layers[0];
        if (layer && layer->dirty) {
            terminal->context.yetty_context.event_loop->ops->request_render(
                terminal->context.yetty_context.event_loop);
        }
    }
    return YETTY_OK_VOID();
}

/* Terminal creation/destruction */

struct yetty_yterm_terminal_result yetty_yterm_terminal_create(
    struct yetty_ycore_grid_size grid_size, const struct yetty_context *yetty_context)
{
    struct yetty_yterm_terminal *terminal;
    uint32_t cols = grid_size.cols;
    uint32_t rows = grid_size.rows;

    ydebug("terminal_create: cols=%u rows=%u", cols, rows);

    terminal = calloc(1, sizeof(struct yetty_yterm_terminal));
    if (!terminal) {
        return YETTY_ERR(yetty_yterm_terminal, "failed to allocate terminal");
    }

    /* Initialize view base */
    terminal->view.ops = &terminal_view_ops;
    terminal->view.id = yetty_yui_view_next_id();

    terminal->cols = cols;
    terminal->rows = rows;
    terminal->layer_count = 0;
    terminal->context.yetty_context = *yetty_context;

    /* Validate event loop from context */
    if (!yetty_context->event_loop) {
        ydebug("terminal_create: no event_loop in context");
        free(terminal);
        return YETTY_ERR(yetty_yterm_terminal, "no event_loop in context");
    }
    ydebug("terminal_create: using event_loop at %p",
           (void *)terminal->context.yetty_context.event_loop);

    /* Set up listener for PTY poll events */
    terminal->listener.handler = terminal_event_handler;

    /* PTY factory is required — every supported platform installs one at
     * startup. A missing factory means yetty_context was constructed wrong. */
    struct yetty_yplatform_pty_factory *pty_factory = yetty_context->app_context.pty_factory;
    if (!pty_factory || !pty_factory->ops || !pty_factory->ops->create_pty) {
        free(terminal);
        return YETTY_ERR(yetty_yterm_terminal,
                         "terminal_create: yetty_context.app_context.pty_factory is NULL or has no create_pty op");
    }

    /* Create PTY */
    struct yetty_yplatform_pty_ptr_result pty_res =
        pty_factory->ops->create_pty(pty_factory, terminal->context.yetty_context.event_loop);
    YETTY_RETURN_IF_ERR(yetty_yterm_terminal, pty_res, "terminal_create: pty_factory create_pty failed");
    terminal->context.pty = pty_res.value;
    ydebug("terminal_create: PTY created at %p", (void *)terminal->context.pty);

    /* Create wire state machine — owns the PTY pointer, the decode stack,
     * and the per-OSC-code layer registry. */
    struct yetty_ywire_wire_statemachine_ptr_result sm_res =
        yetty_ywire_wire_statemachine_create(terminal->context.pty);
    YETTY_RETURN_IF_ERR(yetty_yterm_terminal, sm_res, "terminal_create: wire_statemachine_create failed");
    terminal->sm = sm_res.value;
    ydebug("terminal_create: wire state machine created");

    /* Long-lived yface for emit_*. One per terminal — out_buf is cleared
     * after every send so it stays at the steady-state high-water mark
     * rather than growing per-event. */
    struct yetty_yface_ptr_result yr = yetty_yface_create();
    YETTY_RETURN_IF_ERR(yetty_yterm_terminal, yr, "terminal_create: emit_yface yetty_yface_create failed");
    terminal->emit_yface = yr.value;

    /* Register PTY pipe with the event loop for async-delivery backends.
     * Sync-read backends (e.g. memory-pty) legitimately return NULL here;
     * the SM pulls bytes itself via pty->ops->read on each process(). */
    struct yetty_platform_pty_pipe_source *pipe_source =
        terminal->context.pty->ops->pipe_source(terminal->context.pty);
    if (pipe_source) {
        struct yetty_yevent_pipe_id_result pipe_res =
            terminal->context.yetty_context.event_loop->ops->register_pty_pipe(
                terminal->context.yetty_context.event_loop, pipe_source,
                terminal_pty_pipe_alloc, terminal_pty_pipe_read, terminal);
        YETTY_RETURN_IF_ERR(yetty_yterm_terminal, pipe_res,
                            "terminal_create: event_loop register_pty_pipe failed");
        terminal->pty_pipe_id = pipe_res.value;
        ydebug("terminal_create: PTY pipe registered");
    }

    /* Create text layer */
    struct yetty_yterm_terminal_layer_result text_layer_res =
        yetty_yterm_terminal_text_layer_create(
            cols, rows, yetty_context, terminal_pty_write_callback, terminal,
            terminal_request_render_callback, terminal, terminal_scroll_callback, terminal,
            terminal_cursor_callback, terminal);
    YETTY_RETURN_IF_ERR(yetty_yterm_terminal, text_layer_res,
                        "terminal_create: text_layer_create failed");
    /* Mouse-subscription callback — text-layer's libvterm settermprop hook
     * forwards DEC ?1500 / ?1501 changes here. */
    text_layer_res.value->mouse_sub_fn = terminal_mouse_sub_callback;
    text_layer_res.value->mouse_sub_userdata = terminal;
    /* Alt-screen toggle callback — text-layer's settermprop hook fires
     * this when libvterm processes DEC ?1047/?1049/?47. */
    text_layer_res.value->alt_screen_fn = terminal_alt_screen_callback;
    text_layer_res.value->alt_screen_userdata = terminal;
    /* Clear-screen callback — text-layer's erase hook fires this when
     * libvterm processes a full-screen CSI ED. */
    text_layer_res.value->clear_screen_fn = terminal_clear_screen_callback;
    text_layer_res.value->clear_screen_userdata = terminal;
    struct yetty_ycore_void_result add_r =
        yetty_yterm_terminal_layer_add(terminal, text_layer_res.value);
    YETTY_RETURN_IF_ERR(yetty_yterm_terminal, add_r,
                        "terminal_create: terminal_layer_add(text_layer) failed");
    ydebug("terminal_create: text_layer created and added");

    /* Register text layer as default (raw-passthrough) sink */
    struct yetty_ycore_void_result rr = yetty_ywire_wire_statemachine_set_default(
        terminal->sm, text_layer_res.value);
    YETTY_RETURN_IF_ERR(yetty_yterm_terminal, rr,
                        "terminal_create: wire_statemachine_set_default(text_layer) failed");
    ydebug("terminal_create: text_layer registered as default sink");

    /* Create ydraw scrolling layer (overlay on top of text). Registered
     * for the ydraw OSC codes — the SM does b64+lz4 decoding
     * (protocol-fixed), so registration carries no codec parameter. */
    struct yetty_yrender_terminal_layer *text_layer = text_layer_res.value;
    struct yetty_yterm_terminal_layer_result ydraw_res = yetty_yterm_ydraw_layer_create(
        YETTY_YDRAW_LAYER_KIND_SCROLLING, cols, rows,
        text_layer->cell_size.width, text_layer->cell_size.height,
        yetty_context, terminal_request_render_callback, terminal, terminal_scroll_callback,
        terminal, terminal_cursor_callback, terminal);
    YETTY_RETURN_IF_ERR(yetty_yterm_terminal, ydraw_res,
                        "terminal_create: ydraw scrolling layer create failed");
    add_r = yetty_yterm_terminal_layer_add(terminal, ydraw_res.value);
    YETTY_RETURN_IF_ERR(yetty_yterm_terminal, add_r,
                        "terminal_create: terminal_layer_add(ydraw scrolling) failed");
    ydebug("terminal_create: ydraw scrolling layer created and added");

    rr = yetty_ywire_wire_statemachine_register(terminal->sm, YETTY_OSC_YDRAW_CLEAR, ydraw_res.value);
    YETTY_RETURN_IF_ERR(yetty_yterm_terminal, rr,
                        "terminal_create: register ydraw layer for YETTY_OSC_YDRAW_CLEAR failed");
    rr = yetty_ywire_wire_statemachine_register(terminal->sm, YETTY_OSC_YDRAW_BIN, ydraw_res.value);
    YETTY_RETURN_IF_ERR(yetty_yterm_terminal, rr,
                        "terminal_create: register ydraw layer for YETTY_OSC_YDRAW_BIN failed");
    rr = yetty_ywire_wire_statemachine_register(terminal->sm, YETTY_OSC_YDRAW_OVERLAY, ydraw_res.value);
    YETTY_RETURN_IF_ERR(yetty_yterm_terminal, rr,
                        "terminal_create: register ydraw layer for YETTY_OSC_YDRAW_OVERLAY failed");
    ydebug("terminal_create: ydraw layer registered for OSC CLEAR/BIN/OVERLAY");

    /* yui layer — scene canvas, placed above the scrolling one.
     * Placeholder for yui until its content path lands; no OSC codes. */
    struct yetty_yterm_terminal_layer_result ydraw_static_res = yetty_yterm_ydraw_layer_create(
        YETTY_YDRAW_LAYER_KIND_SCENE, cols, rows,
        text_layer->cell_size.width, text_layer->cell_size.height,
        yetty_context, terminal_request_render_callback, terminal,
        terminal_scroll_callback, terminal, terminal_cursor_callback, terminal);
    YETTY_RETURN_IF_ERR(yetty_yterm_terminal, ydraw_static_res,
                        "terminal_create: ydraw static layer create failed");
    add_r = yetty_yterm_terminal_layer_add(terminal, ydraw_static_res.value);
    YETTY_RETURN_IF_ERR(yetty_yterm_terminal, add_r,
                        "terminal_create: terminal_layer_add(ydraw static) failed");
    ydebug("terminal_create: ydraw static layer created and added");

    /* Register the scene layer for YDRAW_SCENE_BIN — ygui's incremental
     * GROUP/DELETE shipments land here so they don't compete with the
     * scrolling layer for flat YDRAW_BIN envelopes. */
    rr = yetty_ywire_wire_statemachine_register(terminal->sm, YETTY_OSC_YDRAW_SCENE_BIN,
                                                ydraw_static_res.value);
    YETTY_RETURN_IF_ERR(yetty_yterm_terminal, rr,
                        "terminal_create: register scene layer for YDRAW_SCENE_BIN failed");
    ydebug("terminal_create: ydraw scene layer registered for OSC SCENE_BIN");

    /* Shader-glyph layer — animated procedurals at cells whose glyph_index
     * is in the shader-glyph range (top half of u32). Sits above text-layer
     * in compose order so animations overlay text. Reads text-layer's cell
     * buffer directly (text_layer must outlive). */
    struct yetty_yterm_terminal_layer_result sg_res = yetty_yterm_shader_glyph_layer_create(
        cols, rows, text_layer->cell_size.width, text_layer->cell_size.height, text_layer,
        yetty_context, terminal_request_render_callback, terminal, terminal_scroll_callback,
        terminal, terminal_cursor_callback, terminal);
    YETTY_RETURN_IF_ERR(yetty_yterm_terminal, sg_res,
                        "terminal_create: shader_glyph layer create failed");
    add_r = yetty_yterm_terminal_layer_add(terminal, sg_res.value);
    YETTY_RETURN_IF_ERR(yetty_yterm_terminal, add_r,
                        "terminal_create: terminal_layer_add(shader_glyph) failed");
    ydebug("terminal_create: shader_glyph layer created and added");

    /* ymgui layer (Dear ImGui frame, cursor-anchored, terminal-scrolling) */
    struct yetty_yterm_terminal_layer_result ymgui_res = yetty_yterm_ymgui_layer_create(
        cols, rows, text_layer->cell_size.width, text_layer->cell_size.height, yetty_context,
        terminal_request_render_callback, terminal, terminal_scroll_callback, terminal,
        terminal_cursor_callback, terminal);
    YETTY_RETURN_IF_ERR(yetty_yterm_terminal, ymgui_res,
                        "terminal_create: ymgui layer create failed");
    add_r = yetty_yterm_terminal_layer_add(terminal, ymgui_res.value);
    YETTY_RETURN_IF_ERR(yetty_yterm_terminal, add_r,
                        "terminal_create: terminal_layer_add(ymgui) failed");
    terminal->ymgui_layer = ymgui_res.value;
    /* Layer's emit-back path so it can ship FOCUS / RESIZE events through
     * this terminal's emit_yface. */
    ymgui_res.value->emit_osc_fn = terminal_layer_emit_osc;
    ymgui_res.value->emit_osc_userdata = terminal;
    /* Terminal-wide input subscription updates flow through the same layer
     * (it owns the OSC parser); the layer just forwards the new bitmask. */
    ymgui_res.value->term_input_sub_fn = terminal_term_input_sub_callback;
    ymgui_res.value->term_input_sub_userdata = terminal;

    rr = yetty_ywire_wire_statemachine_register(terminal->sm, YMGUI_OSC_CS_CLEAR, ymgui_res.value);
    YETTY_RETURN_IF_ERR(yetty_yterm_terminal, rr,
                        "terminal_create: register ymgui layer for YMGUI_OSC_CS_CLEAR failed");
    rr = yetty_ywire_wire_statemachine_register(terminal->sm, YMGUI_OSC_CS_FRAME, ymgui_res.value);
    YETTY_RETURN_IF_ERR(yetty_yterm_terminal, rr,
                        "terminal_create: register ymgui layer for YMGUI_OSC_CS_FRAME failed");
    rr = yetty_ywire_wire_statemachine_register(terminal->sm, YMGUI_OSC_CS_TEX, ymgui_res.value);
    YETTY_RETURN_IF_ERR(yetty_yterm_terminal, rr,
                        "terminal_create: register ymgui layer for YMGUI_OSC_CS_TEX failed");
    rr = yetty_ywire_wire_statemachine_register(terminal->sm, YMGUI_OSC_CS_CARD_PLACE, ymgui_res.value);
    YETTY_RETURN_IF_ERR(yetty_yterm_terminal, rr,
                        "terminal_create: register ymgui layer for YMGUI_OSC_CS_CARD_PLACE failed");
    rr = yetty_ywire_wire_statemachine_register(terminal->sm, YMGUI_OSC_CS_CARD_REMOVE, ymgui_res.value);
    YETTY_RETURN_IF_ERR(yetty_yterm_terminal, rr,
                        "terminal_create: register ymgui layer for YMGUI_OSC_CS_CARD_REMOVE failed");
    rr = yetty_ywire_wire_statemachine_register(terminal->sm, YMGUI_OSC_CS_TERM_INPUT_SUB, ymgui_res.value);
    YETTY_RETURN_IF_ERR(yetty_yterm_terminal, rr,
                        "terminal_create: register ymgui layer for YMGUI_OSC_CS_TERM_INPUT_SUB failed");
    ydebug("terminal_create: ymgui layer registered for OSC 610000-610004 + 610010");

    /* yrdawn layer (WebGPU-over-OSC bridge — remote wasm process renders
     * here as if our Dawn were its local GPU). */
    struct yetty_yterm_terminal_layer_result yrdawn_res = yetty_yterm_yrdawn_layer_create(
        cols, rows, text_layer->cell_size.width, text_layer->cell_size.height,
        yetty_context);
    YETTY_RETURN_IF_ERR(yetty_yterm_terminal, yrdawn_res,
                        "terminal_create: yrdawn layer create failed");
    add_r = yetty_yterm_terminal_layer_add(terminal, yrdawn_res.value);
    YETTY_RETURN_IF_ERR(yetty_yterm_terminal, add_r,
                        "terminal_create: terminal_layer_add(yrdawn) failed");
    yrdawn_res.value->emit_osc_fn = terminal_layer_emit_osc;
    yrdawn_res.value->emit_osc_userdata = terminal;
    yrdawn_res.value->request_render_fn = terminal_request_render_callback;
    yrdawn_res.value->request_render_userdata = terminal;
    yrdawn_res.value->set_pty_raw_fn = terminal_set_pty_raw;
    yrdawn_res.value->set_pty_raw_userdata = terminal;

    rr = yetty_ywire_wire_statemachine_register(terminal->sm, YETTY_YRDAWN_OSC_CS_HELLO,
                                                yrdawn_res.value);
    YETTY_RETURN_IF_ERR(yetty_yterm_terminal, rr,
                        "terminal_create: register yrdawn layer for YETTY_YRDAWN_OSC_CS_HELLO failed");
    rr = yetty_ywire_wire_statemachine_register(terminal->sm, YETTY_YRDAWN_OSC_CS_CMD,
                                                yrdawn_res.value);
    YETTY_RETURN_IF_ERR(yetty_yterm_terminal, rr,
                        "terminal_create: register yrdawn layer for YETTY_YRDAWN_OSC_CS_CMD failed");
    rr = yetty_ywire_wire_statemachine_register(terminal->sm, YETTY_YRDAWN_OSC_CS_BULK,
                                                yrdawn_res.value);
    YETTY_RETURN_IF_ERR(yetty_yterm_terminal, rr,
                        "terminal_create: register yrdawn layer for YETTY_YRDAWN_OSC_CS_BULK failed");
    rr = yetty_ywire_wire_statemachine_register(terminal->sm, YETTY_YRDAWN_OSC_CS_BYE,
                                                yrdawn_res.value);
    YETTY_RETURN_IF_ERR(yetty_yterm_terminal, rr,
                        "terminal_create: register yrdawn layer for YETTY_YRDAWN_OSC_CS_BYE failed");
    ydebug("terminal_create: yrdawn layer registered for OSC 620000-620003");

    /* Create render targets for each layer */
    const struct yetty_yetty_app_gpu_context *app_gpu = &yetty_context->gpu_context.app_gpu_context;
    struct yetty_yrender_viewport layer_vp = {
        .x = 0, .y = 0, .w = (float)app_gpu->surface_width, .h = (float)app_gpu->surface_height};
    for (size_t i = 0; i < terminal->layer_count; i++) {
        struct yetty_yrender_target_ptr_result target_res = yetty_yrender_target_texture_create(
            yetty_context->gpu_context.device, yetty_context->gpu_context.queue,
            yetty_context->gpu_context.surface_format, yetty_context->gpu_context.allocator,
            NULL, /* no surface for layer targets */
            layer_vp);
        YETTY_RETURN_IF_ERR(yetty_yterm_terminal, target_res,
                            "terminal_create: yrender_target_texture_create failed for a layer");
        terminal->layer_targets[i] = target_res.value;
    }
    ydebug("terminal_create: layer targets created");

    return YETTY_OK(yetty_yterm_terminal, terminal);
}

struct yetty_ycore_void_result yetty_yterm_terminal_destroy(struct yetty_yterm_terminal *terminal)
{
    if (!terminal) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yterm_terminal_destroy: NULL terminal");
    }

    /* Best-effort cleanup: every step must run so resources are released.
     * Stash the first error and keep going; surface it at the end. */
    struct yetty_ycore_void_result first_err = YETTY_OK_VOID();
    bool have_err = false;
    ydebug("terminal_destroy: starting");

    /* Destroy layer targets (render_target ops->destroy is void by signature). */
    for (size_t i = 0; i < terminal->layer_count; i++) {
        if (terminal->layer_targets[i] && terminal->layer_targets[i]->ops &&
            terminal->layer_targets[i]->ops->destroy) {
            ydebug("terminal_destroy: destroying layer_target %zu", i);
            terminal->layer_targets[i]->ops->destroy(terminal->layer_targets[i]);
        }
    }
    ydebug("terminal_destroy: layer_targets destroyed");

    /* Destroy layers. */
    for (size_t i = 0; i < terminal->layer_count; i++) {
        struct yetty_yrender_terminal_layer *layer = terminal->layers[i];
        if (layer && layer->ops && layer->ops->destroy) {
            ydebug("terminal_destroy: destroying layer %zu", i);
            struct yetty_ycore_void_result r = layer->ops->destroy(layer);
            if (YETTY_IS_ERR(r)) {
                yerror("terminal_destroy: layer[%zu] destroy failed: %s", i, r.error.msg);
                if (!have_err) {
                    first_err = r;
                    have_err = true;
                } else {
                    yetty_ycore_error_destroy(r.error);
                }
            }
        }
    }
    ydebug("terminal_destroy: layers destroyed");

    /* Destroy wire state machine BEFORE the PTY — the SM holds a
     * non-owning pointer to the PTY and must not outlive it. */
    ydebug("terminal_destroy: destroying wire state machine");
    {
        struct yetty_ycore_void_result r =
            yetty_ywire_wire_statemachine_destroy(terminal->sm);
        if (YETTY_IS_ERR(r)) {
            yerror("terminal_destroy: wire_statemachine_destroy failed: %s", r.error.msg);
            if (!have_err) {
                first_err = r;
                have_err = true;
            } else {
                yetty_ycore_error_destroy(r.error);
            }
        }
    }

    if (terminal->context.pty && terminal->context.pty->ops &&
        terminal->context.pty->ops->destroy) {
        ydebug("terminal_destroy: destroying pty");
        struct yetty_ycore_void_result r =
            terminal->context.pty->ops->destroy(terminal->context.pty);
        if (YETTY_IS_ERR(r)) {
            yerror("terminal_destroy: pty destroy failed: %s", r.error.msg);
            if (!have_err) {
                first_err = r;
                have_err = true;
            } else {
                yetty_ycore_error_destroy(r.error);
            }
        }
    }

    /* event_loop is owned by yetty, not terminal — do not destroy. */

    yetty_yface_destroy(terminal->emit_yface);
    free(terminal->pty_read_buf);

    ydebug("terminal_destroy: freeing terminal struct");
    free(terminal);
    ydebug("terminal_destroy: done");

    if (have_err) {
        return YETTY_ERR(yetty_ycore_void,
                         "yetty_yterm_terminal_destroy: one or more cleanup steps failed",
                         first_err);
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yterm_terminal_resize_grid(
    struct yetty_yterm_terminal *terminal, struct yetty_ycore_grid_size grid_size,
    struct yetty_ycore_pixel_size cell_size)
{
    if (!terminal) {
        return YETTY_ERR(yetty_ycore_void,
                         "yetty_yterm_terminal_resize_grid: terminal is NULL");
    }

    terminal->cols = grid_size.cols;
    terminal->rows = grid_size.rows;

    for (size_t i = 0; i < terminal->layer_count; i++) {
        struct yetty_yrender_terminal_layer *layer = terminal->layers[i];
        if (layer && layer->ops && layer->ops->resize_grid) {
            struct yetty_ycore_void_result r =
                layer->ops->resize_grid(layer, grid_size, cell_size);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, r,
                                "yetty_yterm_terminal_resize_grid: layer resize_grid failed");
        }
    }
    return YETTY_OK_VOID();
}

/* Terminal state */

uint32_t yetty_yterm_terminal_get_cols(const struct yetty_yterm_terminal *terminal)
{
    return terminal ? terminal->cols : 0;
}

uint32_t yetty_yterm_terminal_get_rows(const struct yetty_yterm_terminal *terminal)
{
    return terminal ? terminal->rows : 0;
}

/* Layer management */

struct yetty_ycore_void_result yetty_yterm_terminal_layer_add(
    struct yetty_yterm_terminal *terminal, struct yetty_yrender_terminal_layer *layer)
{
    if (!terminal) {
        return YETTY_ERR(yetty_ycore_void,
                         "yetty_yterm_terminal_layer_add: terminal is NULL");
    }
    if (!layer) {
        return YETTY_ERR(yetty_ycore_void,
                         "yetty_yterm_terminal_layer_add: layer is NULL");
    }
    if (terminal->layer_count >= YETTY_YTERM_TERMINAL_MAX_LAYERS) {
        return YETTY_ERR(yetty_ycore_void,
                         "yetty_yterm_terminal_layer_add: terminal->layers is full (YETTY_YTERM_TERMINAL_MAX_LAYERS reached)");
    }
    terminal->layers[terminal->layer_count++] = layer;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yterm_terminal_layer_remove(
    struct yetty_yterm_terminal *terminal, struct yetty_yrender_terminal_layer *layer)
{
    if (!terminal) {
        return YETTY_ERR(yetty_ycore_void,
                         "yetty_yterm_terminal_layer_remove: terminal is NULL");
    }
    if (!layer) {
        return YETTY_ERR(yetty_ycore_void,
                         "yetty_yterm_terminal_layer_remove: layer is NULL");
    }

    for (size_t i = 0; i < terminal->layer_count; i++) {
        if (terminal->layers[i] == layer) {
            memmove(&terminal->layers[i], &terminal->layers[i + 1],
                    (terminal->layer_count - i - 1) * sizeof(terminal->layers[0]));
            terminal->layer_count--;
            return YETTY_OK_VOID();
        }
    }
    return YETTY_ERR(yetty_ycore_void,
                     "yetty_yterm_terminal_layer_remove: layer not found in terminal->layers");
}

size_t yetty_yterm_terminal_layer_count(const struct yetty_yterm_terminal *terminal)
{
    return terminal ? terminal->layer_count : 0;
}

struct yetty_yrender_terminal_layer *yetty_yterm_terminal_layer_get(
    const struct yetty_yterm_terminal *terminal, size_t index)
{
    if (!terminal || index >= terminal->layer_count) {
        return NULL;
    }

    return terminal->layers[index];
}

/*=============================================================================
 * View interface implementation
 *===========================================================================*/

struct yetty_yui_view *yetty_yterm_terminal_as_view(struct yetty_yterm_terminal *terminal)
{
    return terminal ? &terminal->view : NULL;
}

static struct yetty_ycore_void_result terminal_view_destroy(struct yetty_yui_view *view)
{
    struct yetty_yterm_terminal *terminal = container_of(view, struct yetty_yterm_terminal, view);
    return yetty_yterm_terminal_destroy(terminal);
}

static struct yetty_ycore_void_result terminal_view_render(
    struct yetty_yui_view *view, struct yetty_ydraw_target *render_target, int force_redraw)
{
    struct yetty_yterm_terminal *terminal = container_of(view, struct yetty_yterm_terminal, view);

    return terminal_render_frame(terminal, render_target, force_redraw);
}

static struct yetty_ycore_void_result terminal_view_set_bounds(struct yetty_yui_view *view,
                                                               struct yetty_yui_rect bounds)
{
    struct yetty_yterm_terminal *terminal = container_of(view, struct yetty_yterm_terminal, view);

    /* Store bounds in view */
    int changed = (view->bounds.w != bounds.w) || (view->bounds.h != bounds.h);
    view->bounds = bounds;

    /* Terminal handles resize via YETTY_EVENT_RESIZE from event loop */
    /* For now, just log - the actual resize happens through the event system */
    ydebug("terminal_view_set_bounds: %.0fx%.0f at (%.0f,%.0f)", bounds.w, bounds.h, bounds.x,
           bounds.y);

    /* Fan out the new pane pixel size to any terminal-wide subscriber.
     * Card-aware subscribers get their per-card resize through a separate
     * path (ymgui-layer's CARD_PLACE → SC_RESIZE). */
    if (changed && terminal->term_input_sub_flags) {
        struct yetty_ycore_void_result r =
            terminal_emit_term_resize(terminal, bounds.w, bounds.h);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r,
                            "terminal_view_set_bounds: emit_term_resize failed");
    }

    return YETTY_OK_VOID();
}

static struct yetty_ycore_int_result terminal_view_on_event(struct yetty_yui_view *view,
                                                            const struct yetty_yui_event *event)
{
    struct yetty_yterm_terminal *terminal = container_of(view, struct yetty_yterm_terminal, view);

    switch (event->type) {
    case YETTY_YCORE_KEY_DOWN:
        ydebug("terminal: KEY_DOWN key=%d mods=%d", event->key.key, event->key.mods);
        /* Clipboard shortcuts (terminal-standard: Ctrl+Shift+C / V).
         * Checked BEFORE scrollback handling so the selection survives
         * the implicit scrollback-exit-on-any-key path. Don't fall
         * through to libvterm — these keystrokes are ours. */
        if ((event->key.mods & YETTY_MOD_CONTROL) && (event->key.mods & YETTY_MOD_SHIFT)) {
            if (event->key.key == 67 /* GLFW_KEY_C */) {
                struct yetty_ycore_void_result cr = terminal_copy_selection(terminal);
                YETTY_RETURN_IF_ERR(yetty_ycore_int, cr,
                                    "terminal_view_on_event: copy_selection failed");
                return YETTY_OK(yetty_ycore_int, 1);
            }
            if (event->key.key == 86 /* GLFW_KEY_V */) {
                struct yetty_ycore_void_result pr = terminal_paste_clipboard(terminal);
                YETTY_RETURN_IF_ERR(yetty_ycore_int, pr,
                                    "terminal_view_on_event: paste_clipboard failed");
                return YETTY_OK(yetty_ycore_int, 1);
            }
        }
        /* PageUp / PageDown drive scrollback by one viewport at a
         * time. Handled BEFORE the "any-key-exits-scrollback" rule
         * so PageUp/Down keep working while in scrollback view —
         * otherwise PageDown would exit scrollback instead of
         * scrolling forward inside it. terminal_scrollback_apply
         * takes positive lines = older content (up). */
        if (event->key.key == 266 /* GLFW_KEY_PAGE_UP */ ||
            event->key.key == 267 /* GLFW_KEY_PAGE_DOWN */) {
            int page = (int)terminal->rows;
            if (page < 1) {
                page = 1;
            }
            struct yetty_ycore_void_result sr = terminal_scrollback_apply(
                terminal, event->key.key == 266 ? +page : -page);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, sr,
                                "terminal_view_on_event: scrollback_apply failed");
            return YETTY_OK(yetty_ycore_int, 1);
        }
        /* In scrollback view, Enter exits and is consumed (matches tmux copy
     * mode). Other keys also exit scrollback before falling through to
     * normal dispatch — this means typing while in scrollback returns to
     * live and delivers the keystroke to the shell, which is what users
     * expect when they meant to interact with the prompt. */
        if (terminal->scrollback_active) {
            int is_enter = (event->key.key == 257); /* GLFW_KEY_ENTER */
            struct yetty_ycore_void_result xr = terminal_scrollback_exit(terminal);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, xr,
                                "terminal_view_on_event: scrollback_exit failed");
            if (is_enter) {
                return YETTY_OK(yetty_ycore_int, 1);
            }
        }
        /* Terminal-wide keyboard fan-out — opt-in via TERM_INPUT_SUB. Fires
         * BEFORE the focused-card path so subscribers see every key the
         * pane received, regardless of card focus. Subscribers that also
         * want libvterm-formatted bytes get those via stdin as usual; this
         * channel just adds structured (key+mods+codepoint) info on top. */
        {
            struct yetty_ycore_void_result tkr = terminal_emit_term_key(
                terminal, YETTY_YMGUI_INPUT_KEY_DOWN, event->key.key, event->key.mods, 0);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, tkr,
                                "terminal_view_on_event: emit_term_key(KEY_DOWN) failed");
        }
        /* If a ymgui card has focus, route the keystroke to it as an OSC
     * envelope and DO NOT also feed libvterm — otherwise the shell
     * would see the keystroke alongside the card. */
        {
            struct yetty_ycore_int_result ckr = terminal_emit_card_key(
                terminal, YETTY_YMGUI_INPUT_KEY_DOWN, event->key.key, event->key.mods, 0);
            if (YETTY_IS_ERR(ckr)) {
                return YETTY_ERR(yetty_ycore_int,
                                 "terminal_view_on_event: emit_card_key(KEY_DOWN) failed", ckr);
            }
            if (ckr.value) {
                return YETTY_OK(yetty_ycore_int, 1);
            }
        }
        for (size_t i = 0; i < terminal->layer_count; i++) {
            struct yetty_yrender_terminal_layer *layer = terminal->layers[i];
            if (layer && layer->ops && layer->ops->on_key) {
                if (layer->ops->on_key(layer, event->key.key, event->key.mods)) {
                    return YETTY_OK(yetty_ycore_int, 1);
                }
            }
        }
        return YETTY_OK(yetty_ycore_int, 1);

    case YETTY_YCORE_KEY_UP: {
        ydebug("terminal: KEY_UP key=%d mods=%d", event->key.key, event->key.mods);
        struct yetty_ycore_void_result tkr = terminal_emit_term_key(
            terminal, YETTY_YMGUI_INPUT_KEY_UP, event->key.key, event->key.mods, 0);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, tkr,
                            "terminal_view_on_event: emit_term_key(KEY_UP) failed");
        struct yetty_ycore_int_result ckr = terminal_emit_card_key(
            terminal, YETTY_YMGUI_INPUT_KEY_UP, event->key.key, event->key.mods, 0);
        if (YETTY_IS_ERR(ckr)) {
            return YETTY_ERR(yetty_ycore_int,
                             "terminal_view_on_event: emit_card_key(KEY_UP) failed", ckr);
        }
        if (ckr.value) {
            return YETTY_OK(yetty_ycore_int, 1);
        }
        return YETTY_OK(yetty_ycore_int, 0);
    }

    case YETTY_YCORE_CHAR:
        ydebug("terminal: CHAR codepoint=U+%04X mods=%d", event->chr.codepoint, event->chr.mods);
        if (terminal->scrollback_active) {
            struct yetty_ycore_void_result xr = terminal_scrollback_exit(terminal);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, xr,
                                "terminal_view_on_event: scrollback_exit failed");
        }
        {
            struct yetty_ycore_void_result tkr = terminal_emit_term_key(
                terminal, YETTY_YMGUI_INPUT_KEY_CHAR, -1, event->chr.mods, event->chr.codepoint);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, tkr,
                                "terminal_view_on_event: emit_term_key(CHAR) failed");
        }
        /* See KEY_DOWN: focused card consumes the codepoint. */
        {
            struct yetty_ycore_int_result ckr = terminal_emit_card_key(
                terminal, YETTY_YMGUI_INPUT_KEY_CHAR, -1, event->chr.mods, event->chr.codepoint);
            if (YETTY_IS_ERR(ckr)) {
                return YETTY_ERR(yetty_ycore_int,
                                 "terminal_view_on_event: emit_card_key(CHAR) failed", ckr);
            }
            if (ckr.value) {
                return YETTY_OK(yetty_ycore_int, 1);
            }
        }
        for (size_t i = 0; i < terminal->layer_count; i++) {
            struct yetty_yrender_terminal_layer *layer = terminal->layers[i];
            if (layer && layer->ops && layer->ops->on_char) {
                if (layer->ops->on_char(layer, event->chr.codepoint, event->chr.mods)) {
                    return YETTY_OK(yetty_ycore_int, 1);
                }
            }
        }
        return YETTY_OK(yetty_ycore_int, 1);

    case YETTY_YCORE_RESIZE: {
        float width = event->resize.width;
        float height = event->resize.height;
        ydebug("terminal: RESIZE %.0fx%.0f", width, height);

        if (width <= 0 || height <= 0) {
            return YETTY_OK(yetty_ycore_int, 1);
        }

        /* Resize layer targets - yetty handles surface reconfiguration */
        struct yetty_yrender_viewport vp = {.x = 0, .y = 0, .w = width, .h = height};
        for (size_t i = 0; i < terminal->layer_count; i++) {
            if (terminal->layer_targets[i] && terminal->layer_targets[i]->ops->resize) {
                struct yetty_ycore_void_result tr =
                    terminal->layer_targets[i]->ops->resize(terminal->layer_targets[i], vp);
                YETTY_RETURN_IF_ERR(yetty_ycore_int, tr,
                                    "terminal_view_on_event: layer_target resize failed");
            }
        }

        /* Grid + cell stride: rows are derived from the desired target
         * stride (the font's natural cell height, or a sensible fallback),
         * then cell_w/cell_h are re-derived as workspace / rows so the
         * canvas's `cols * cell_w x rows * cell_h` equals the workspace
         * exactly. Without that, the shader's NDC-to-pixel map shifts /
         * scales by a few px against the framebuffer and primitives
         * pinned to the bottom edge (yui statusbar at H-22, terminal
         * cells at the bottom row) end up cut or stretched. */
        if (terminal->layer_count > 0) {
            struct yetty_yrender_terminal_layer *layer = terminal->layers[0];
            float cell_w_target = layer->cell_size.width > 0 ? layer->cell_size.width : 10.0f;
            float cell_h_target = layer->cell_size.height > 0 ? layer->cell_size.height : 20.0f;
            uint32_t new_cols = (uint32_t)(width / cell_w_target + 0.5f);
            uint32_t new_rows = (uint32_t)(height / cell_h_target + 0.5f);
            if (new_cols == 0) {
                new_cols = 1;
            }
            if (new_rows == 0) {
                new_rows = 1;
            }
            struct yetty_ycore_pixel_size new_cell = {
                .width = width / (float)new_cols,
                .height = height / (float)new_rows,
            };
            struct yetty_ycore_void_result rgr = yetty_yterm_terminal_resize_grid(
                terminal, (struct yetty_ycore_grid_size){.cols = new_cols, .rows = new_rows},
                new_cell);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, rgr,
                                "terminal_view_on_event: terminal_resize_grid failed");
            if (terminal->context.pty->ops->resize) {
                struct yetty_ycore_void_result pr = terminal->context.pty->ops->resize(
                    terminal->context.pty, new_cols, new_rows);
                YETTY_RETURN_IF_ERR(yetty_ycore_int, pr,
                                    "terminal_view_on_event: pty resize failed");
            }
        }
        /* Cards self-announce their pixel size via per-card YMGUI_OSC_SC_RESIZE
     * fired from the layer (see ymgui-layer.c::ymgui_resize_grid). No
     * pane-size emission needed here. */
        return YETTY_OK(yetty_ycore_int, 1);
    }

    case YETTY_YCORE_ZOOM_CELL_SIZE: {
        /* Structural zoom — scale the target cell stride by `factor`, then
         * resnap rows/cols so the canvas covers the view bounds exactly.
         * One call into terminal_resize_grid pushes both grid and cell
         * into every layer (and the PTY) in lockstep — the old separate
         * set_cell_size + resize_grid pair drifted because the cell write
         * and the grid write weren't atomic. */
        float delta = event->zoom_cell_size.delta;
        float factor;
        if (event->zoom_cell_size.reset) {
            /* Baseline isn't currently cached; treat reset as "no scale change". */
            factor = 1.0f;
        } else {
            factor = 1.0f + delta;
            if (factor < 0.5f) {
                factor = 0.5f;
            }
            if (factor > 3.0f) {
                factor = 3.0f;
            }
        }
        ydebug("terminal: ZOOM_CELL_SIZE delta=%.3f factor=%.3f", delta, factor);
        if (factor == 1.0f) {
            return YETTY_OK(yetty_ycore_int, 1);
        }

        float view_w = terminal->view.bounds.w;
        float view_h = terminal->view.bounds.h;
        if (view_w <= 0.0f || view_h <= 0.0f) {
            ydebug("terminal: ZOOM_CELL_SIZE skipped, zero view bounds");
            return YETTY_OK(yetty_ycore_int, 1);
        }

        if (terminal->layer_count > 0) {
            struct yetty_yrender_terminal_layer *layer = terminal->layers[0];
            float cell_w_target =
                (layer->cell_size.width > 0 ? layer->cell_size.width : 10.0f) * factor;
            float cell_h_target =
                (layer->cell_size.height > 0 ? layer->cell_size.height : 20.0f) * factor;
            uint32_t new_cols = (uint32_t)(view_w / cell_w_target + 0.5f);
            uint32_t new_rows = (uint32_t)(view_h / cell_h_target + 0.5f);
            if (new_cols == 0) {
                new_cols = 1;
            }
            if (new_rows == 0) {
                new_rows = 1;
            }
            struct yetty_ycore_pixel_size new_cell = {
                .width = view_w / (float)new_cols,
                .height = view_h / (float)new_rows,
            };
            struct yetty_ycore_void_result rgr = yetty_yterm_terminal_resize_grid(
                terminal, (struct yetty_ycore_grid_size){.cols = new_cols, .rows = new_rows},
                new_cell);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, rgr,
                                "terminal_view_on_event: terminal_resize_grid (zoom) failed");
            if (terminal->context.pty->ops->resize) {
                struct yetty_ycore_void_result pr = terminal->context.pty->ops->resize(
                    terminal->context.pty, new_cols, new_rows);
                YETTY_RETURN_IF_ERR(yetty_ycore_int, pr,
                                    "terminal_view_on_event: pty resize (zoom) failed");
            }
        }

        terminal->context.yetty_context.event_loop->ops->request_render(
            terminal->context.yetty_context.event_loop);
        return YETTY_OK(yetty_ycore_int, 1);
    }

    case YETTY_YCORE_ZOOM_VISUAL_APPLY: {
        float scale = event->zoom_visual_apply.scale;
        float ox = event->zoom_visual_apply.offset_x;
        float oy = event->zoom_visual_apply.offset_y;
        for (size_t i = 0; i < terminal->layer_count; i++) {
            struct yetty_yrender_terminal_layer *layer = terminal->layers[i];
            if (layer && layer->ops && layer->ops->set_visual_zoom) {
                struct yetty_ycore_void_result vr =
                    layer->ops->set_visual_zoom(layer, scale, ox, oy);
                YETTY_RETURN_IF_ERR(yetty_ycore_int, vr,
                                    "terminal_view_on_event: layer set_visual_zoom failed");
            }
        }
        ydebug("terminal: ZOOM_VISUAL_APPLY scale=%.2f off=(%.1f,%.1f)", scale, ox, oy);
        return YETTY_OK(yetty_ycore_int, 1);
    }

    case YETTY_YCORE_SHUTDOWN:
        ydebug("terminal: SHUTDOWN received");
        terminal->shutting_down = 1;
        return YETTY_OK(yetty_ycore_int, 1);

    case YETTY_YCORE_SET_FOCUS:
        /* Foreground-view notification from the workspace. object_id != 0
         * means the tabbar's active workspace is now this terminal's
         * one and we're its focused pane — i.e. we're the terminal
         * keys go to. object_id == 0 means we just got backgrounded.
         * Today we just track the bit (downstream layers + future PTY
         * focus reporting read it); rendering style updates will land
         * separately. */
        terminal->focused = event->set_focus.object_id != 0;
        ydebug("terminal: SET_FOCUS focused=%d", terminal->focused);
        return YETTY_OK(yetty_ycore_int, 1);

    case YETTY_YCORE_PASTE: {
        /* Async paste response — the clipboard manager fetched the text
         * on the main thread and pushed it back to us through the input
         * pipe. Payload is a heap-allocated NUL-terminated UTF-8 string
         * we own; write its bytes to the PTY (libvterm/the shell deals
         * with bracketed-paste wrapping) and free. */
        char *text = event->payload;
        if (text) {
            size_t len = strlen(text);
            if (len > 0) {
                struct yetty_ycore_size_result wr =
                    terminal_pty_write_raw(terminal, text, len);
                if (YETTY_IS_ERR(wr)) {
                    free(text);
                    return YETTY_ERR(yetty_ycore_int,
                                     "terminal_view_on_event: pty_write_raw(paste) failed", wr);
                }
                yinfo("terminal: pasted %zu bytes from clipboard", len);
            }
            free(text);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }

    case YETTY_YCORE_POLL_READABLE: {
        ydebug("terminal: POLL_READABLE");
        struct yetty_ycore_void_result rr = terminal_read_pty(terminal);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, rr,
                            "terminal_view_on_event: terminal_read_pty failed");
        return YETTY_OK(yetty_ycore_int, 1);
    }

    /*-------------------------------------------------------------------------
   * Card-aware mouse forwarding.
   *
   * Coordinates start window-absolute (GLFW pipe), get de-offset against
   * view bounds for terminal-local pane pixels, then hit-tested against
   * the ymgui-layer's card registry. Each emit carries a card_id and
   * card-local coords so the client never has to know where its cards
   * sit in the pane.
   *
   * Click-focus: MOUSE_DOWN updates the focused card to whatever sat
   * under the cursor at click time (may be 0 = release focus).
   *
   * Drag capture: while any button is held, MOVE events route to the
   * focused (= clicked) card even if the cursor leaves the rect — same
   * convention as desktop drag.
   *-----------------------------------------------------------------------*/
    case YETTY_YCORE_MOUSE_DOWN:
    case YETTY_YCORE_MOUSE_UP: {
        ydebug("terminal: MOUSE_%s win=(%.1f,%.1f) bounds=(%.0fx%.0f@%.0f,%.0f) "
               "click_sub=%d term_sub=0x%x",
               event->type == YETTY_YCORE_MOUSE_DOWN ? "DOWN" : "UP", event->mouse.x,
               event->mouse.y, view->bounds.w, view->bounds.h, view->bounds.x, view->bounds.y,
               terminal->mouse_click_subscribed, terminal->term_input_sub_flags);

        /* Cell-precise selection. Left-button drag starts/extends/finalises
         * a selection when nothing else owns the click (no card-aware
         * subscriber, no terminal-wide subscriber, no scrollback view).
         * Anchor is the cell under MOUSE_DOWN; head tracks MOUSE_DRAG. */
        float lx_sel = event->mouse.x - view->bounds.x;
        float ly_sel = event->mouse.y - view->bounds.y;
        int in_bounds = !(lx_sel < 0.0f || ly_sel < 0.0f || lx_sel >= view->bounds.w ||
                          ly_sel >= view->bounds.h);
        int term_owns_click = !terminal->mouse_click_subscribed && !terminal->scrollback_active &&
                              !(terminal->term_input_sub_flags & YETTY_YMGUI_TERM_SUB_MOUSE_CLICK);

        /* X-Windows-style middle-click paste. Button 2 press inside the
         * pane, when no subscriber owns the click, pulls the current
         * clipboard text and pushes it to the PTY — same path as
         * Ctrl+Shift+V. Release is consumed silently. */
        if (in_bounds && term_owns_click && event->mouse.button == 2) {
            if (event->type == YETTY_YCORE_MOUSE_DOWN) {
                struct yetty_ycore_void_result pr = terminal_paste_clipboard(terminal);
                YETTY_RETURN_IF_ERR(yetty_ycore_int, pr,
                                    "terminal_view_on_event: middle-click paste_clipboard failed");
            }
            return YETTY_OK(yetty_ycore_int, 1);
        }

        int sel_eligible = in_bounds && event->mouse.button == 0 && term_owns_click;
        if (sel_eligible) {
            if (event->type == YETTY_YCORE_MOUSE_DOWN) {
                uint32_t r, c;
                terminal_cell_from_local(terminal, lx_sel, ly_sel, &r, &c);
                terminal->sel_anchor_row = r;
                terminal->sel_anchor_col = c;
                terminal->sel_head_row = r;
                terminal->sel_head_col = c;
                terminal->sel_active = 1;
                terminal->sel_dragging = 1;
                struct yetty_ycore_void_result psr = terminal_push_selection(terminal);
                YETTY_RETURN_IF_ERR(yetty_ycore_int, psr,
                                    "terminal_view_on_event: push_selection (anchor) failed");
                return YETTY_OK(yetty_ycore_int, 1);
            }
            /* MOUSE_UP for our drag — finalise. A pure click (no movement)
             * is treated as "clear selection" so users have an obvious way
             * to dismiss the highlight without dragging into the gutter.
             *
             * If the user has X-Windows-style auto-copy enabled (default
             * true, see terminal/selection/auto-copy), a successful drag
             * also pushes the new selection to the clipboard right here —
             * no Ctrl+Shift+C needed. */
            if (terminal->sel_dragging) {
                terminal->sel_dragging = 0;
                if (terminal->sel_anchor_row == terminal->sel_head_row &&
                    terminal->sel_anchor_col == terminal->sel_head_col) {
                    struct yetty_ycore_void_result cr = terminal_clear_selection(terminal);
                    YETTY_RETURN_IF_ERR(yetty_ycore_int, cr,
                                        "terminal_view_on_event: clear_selection failed");
                } else {
                    struct yetty_ycore_void_result psr = terminal_push_selection(terminal);
                    YETTY_RETURN_IF_ERR(yetty_ycore_int, psr,
                                        "terminal_view_on_event: push_selection (final) failed");
                    struct yetty_yconfig_config *cfg =
                        terminal->context.yetty_context.app_context.config;
                    int auto_copy = 1;
                    if (cfg && cfg->ops && cfg->ops->get_bool) {
                        auto_copy = cfg->ops->get_bool(cfg, "terminal/selection/auto-copy", 1);
                    }
                    if (auto_copy) {
                        struct yetty_ycore_void_result cs = terminal_copy_selection(terminal);
                        YETTY_RETURN_IF_ERR(yetty_ycore_int, cs,
                                            "terminal_view_on_event: auto-copy_selection failed");
                    }
                }
                return YETTY_OK(yetty_ycore_int, 1);
            }
        }

        int term_wants = (terminal->term_input_sub_flags & YETTY_YMGUI_TERM_SUB_MOUSE_CLICK) != 0;
        if (!terminal->mouse_click_subscribed && !term_wants) {
            return YETTY_OK(yetty_ycore_int, 0);
        }
        float lx = event->mouse.x - view->bounds.x;
        float ly = event->mouse.y - view->bounds.y;
        if (lx < 0.0f || ly < 0.0f || lx >= view->bounds.w || ly >= view->bounds.h) {
            return YETTY_OK(yetty_ycore_int, 0);
        }
        int btn = event->mouse.button;
        int press = (event->type == YETTY_YCORE_MOUSE_DOWN) ? 1 : 0;
        if (press) {
            terminal->mouse_buttons_held |= (1 << btn);
        } else {
            terminal->mouse_buttons_held &= ~(1 << btn);
        }

        /* Card-aware path (unchanged). */
        if (terminal->mouse_click_subscribed) {
            uint32_t focused =
                terminal->ymgui_layer
                    ? yetty_yterm_terminal_layer_ymgui_layer_focused_card(terminal->ymgui_layer)
                    : 0;
            struct yetty_yterm_ymgui_hit hit;
            if (press) {
                hit = terminal_resolve_card_hit(terminal, lx, ly, 0);
                if (terminal->ymgui_layer) {
                    struct yetty_ycore_void_result fr =
                        yetty_yterm_terminal_layer_ymgui_layer_set_focus(terminal->ymgui_layer,
                                                                         hit.card_id);
                    YETTY_RETURN_IF_ERR(yetty_ycore_int, fr,
                                        "terminal_view_on_event: ymgui_layer_set_focus failed");
                }
            } else {
                hit = terminal_resolve_card_hit(terminal, lx, ly, focused);
            }
            if (hit.card_id != 0) {
                struct yetty_ycore_void_result mr = terminal_emit_card_mouse_button(
                    terminal, hit.card_id, hit.local_x, hit.local_y, btn, press, 0.0f);
                YETTY_RETURN_IF_ERR(yetty_ycore_int, mr,
                                    "terminal_view_on_event: emit_card_mouse_button failed");
            }
        }

        /* Terminal-wide fan-out — fires regardless of card hit, with
         * pane-local pixel coords. */
        {
            struct yetty_ycore_void_result tmr =
                terminal_emit_term_mouse_button(terminal, lx, ly, btn, press, 0.0f);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, tmr,
                                "terminal_view_on_event: emit_term_mouse_button failed");
        }

        return YETTY_OK(yetty_ycore_int, 1);
    }

    case YETTY_YCORE_MOUSE_MOVE:
    case YETTY_YCORE_MOUSE_DRAG: {
        ydebug("terminal: MOUSE_MOVE win=(%.1f,%.1f) move_sub=%d term_sub=0x%x", event->mouse.x,
               event->mouse.y, terminal->mouse_move_subscribed, terminal->term_input_sub_flags);

        /* Extend an in-flight selection. The platform's GLFW dispatcher
         * never synthesises MOUSE_DRAG — it only emits MOUSE_MOVE — so we
         * key off our own sel_dragging flag (set on MOUSE_DOWN, cleared on
         * MOUSE_UP) rather than the event type. Treat any cursor move
         * while we're dragging as drag extension. */
        if (terminal->sel_dragging) {
            float lx_sel = event->mouse.x - view->bounds.x;
            float ly_sel = event->mouse.y - view->bounds.y;
            uint32_t r, c;
            terminal_cell_from_local(terminal, lx_sel, ly_sel, &r, &c);
            if (r != terminal->sel_head_row || c != terminal->sel_head_col) {
                terminal->sel_head_row = r;
                terminal->sel_head_col = c;
                struct yetty_ycore_void_result psr = terminal_push_selection(terminal);
                YETTY_RETURN_IF_ERR(yetty_ycore_int, psr,
                                    "terminal_view_on_event: push_selection (drag) failed");
            }
            return YETTY_OK(yetty_ycore_int, 1);
        }

        int term_wants = (terminal->term_input_sub_flags & YETTY_YMGUI_TERM_SUB_MOUSE_MOVE) != 0;
        if (!terminal->mouse_move_subscribed && !term_wants) {
            return YETTY_OK(yetty_ycore_int, 0);
        }
        float lx = event->mouse.x - view->bounds.x;
        float ly = event->mouse.y - view->bounds.y;
        if (lx < 0.0f || ly < 0.0f || lx >= view->bounds.w || ly >= view->bounds.h) {
            return YETTY_OK(yetty_ycore_int, 0);
        }

        /* Card-aware path (unchanged). */
        if (terminal->mouse_move_subscribed) {
            uint32_t captured = 0;
            if (terminal->mouse_buttons_held && terminal->ymgui_layer) {
                captured =
                    yetty_yterm_terminal_layer_ymgui_layer_focused_card(terminal->ymgui_layer);
            }
            struct yetty_yterm_ymgui_hit hit =
                terminal_resolve_card_hit(terminal, lx, ly, captured);
            if (hit.card_id != 0) {
                struct yetty_ycore_void_result mr = terminal_emit_card_mouse_move(
                    terminal, hit.card_id, hit.local_x, hit.local_y, terminal->mouse_buttons_held);
                YETTY_RETURN_IF_ERR(yetty_ycore_int, mr,
                                    "terminal_view_on_event: emit_card_mouse_move failed");
            }
        }

        /* Terminal-wide fan-out. */
        {
            struct yetty_ycore_void_result tmr = terminal_emit_term_mouse_move(
                terminal, lx, ly, terminal->mouse_buttons_held);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, tmr,
                                "terminal_view_on_event: emit_term_mouse_move failed");
        }

        return YETTY_OK(yetty_ycore_int, 1);
    }

    case YETTY_YCORE_MOUSE_SCROLL: {
        /* dy==0 dropped: wire only carries wheel_dy. */
        if (event->mouse_scroll.dy == 0.0f) {
            return YETTY_OK(yetty_ycore_int, 0);
        }

        float lx = event->mouse_scroll.x - view->bounds.x;
        float ly = event->mouse_scroll.y - view->bounds.y;
        if (lx < 0.0f || ly < 0.0f || lx >= view->bounds.w || ly >= view->bounds.h) {
            return YETTY_OK(yetty_ycore_int, 0);
        }

        /* Once in scrollback view, wheel always drives history. Otherwise
     * if a card is under the cursor OR a terminal-wide wheel subscriber
     * is listening, the wheel goes outbound; else scrollback. */
        int term_wheel_wants =
            (terminal->term_input_sub_flags & YETTY_YMGUI_TERM_SUB_MOUSE_WHEEL) != 0;
        if (!terminal->scrollback_active &&
            (terminal->mouse_click_subscribed || term_wheel_wants)) {
            int delivered = 0;
            if (terminal->mouse_click_subscribed) {
                struct yetty_yterm_ymgui_hit hit = terminal_resolve_card_hit(terminal, lx, ly, 0);
                if (hit.card_id != 0) {
                    struct yetty_ycore_void_result mr = terminal_emit_card_mouse_button(
                        terminal, hit.card_id, hit.local_x, hit.local_y,
                        0, 0, event->mouse_scroll.dy);
                    YETTY_RETURN_IF_ERR(yetty_ycore_int, mr,
                                        "terminal_view_on_event: emit_card_mouse_button (wheel) failed");
                    delivered = 1;
                }
            }
            if (term_wheel_wants) {
                struct yetty_ycore_void_result tmr = terminal_emit_term_mouse_button(
                    terminal, lx, ly, 0, 0, event->mouse_scroll.dy);
                YETTY_RETURN_IF_ERR(yetty_ycore_int, tmr,
                                    "terminal_view_on_event: emit_term_mouse_button (wheel) failed");
                delivered = 1;
            }
            if (delivered) {
                return YETTY_OK(yetty_ycore_int, 1);
            }
        }

        int lines = (int)(event->mouse_scroll.dy * YETTY_YTERM_WHEEL_LINES_PER_TICK);
        if (lines == 0 && event->mouse_scroll.dy != 0.0f) {
            lines = (event->mouse_scroll.dy > 0) ? 1 : -1;
        }
        if (lines != 0) {
            struct yetty_ycore_void_result sr = terminal_scrollback_apply(terminal, lines);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, sr,
                                "terminal_view_on_event: scrollback_apply (wheel) failed");
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }

    default:
        return YETTY_OK(yetty_ycore_int, 0);
    }
}
