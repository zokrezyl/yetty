#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yetty/platform/platform-input-pipe.h>
#include <yetty/platform/pty-factory.h>
#include <yetty/platform/pty-pipe-source.h>
#include <yetty/platform/pty.h>
#include <yetty/yconfig/config.h>
#include <yetty/ycore/event-loop.h>
#include <yetty/ycore/event.h>
#include <yetty/yface/yface.h>
#include <yetty/ymgui/wire.h>
#include <yetty/yrender/gpu-allocator.h>
#include <yetty/yrender/gpu-resource-set.h>
#include <yetty/yrender/render-target.h>
#include <yetty/yterm/pty-reader.h>
#include <yetty/yterm/terminal.h>
#include <yetty/yterm/text-layer.h>
#include <yetty/yterm/ypaint-layer.h>
#include <yetty/yterm/ymgui-layer.h>
#include <yetty/yterm/shader-glyph-layer.h>
#include <yetty/ytrace/ytrace.h>
#include <yetty/yui/view.h>

#define YETTY_YTERM_TERMINAL_MAX_LAYERS 256

/* Forward declarations for view ops */
static struct yetty_ycore_void_result terminal_view_destroy(struct yetty_yterm_view *view);
static struct yetty_ycore_void_result terminal_view_render(
    struct yetty_yterm_view *view, struct yetty_ypaint_core_target *render_target);
static struct yetty_ycore_void_result terminal_view_set_bounds(struct yetty_yterm_view *view,
                                                               struct yetty_yui_rect bounds);
static struct yetty_ycore_int_result terminal_view_on_event(struct yetty_yterm_view *view,
                                                            const struct yetty_yui_event *event);

static const struct yetty_yui_view_ops terminal_view_ops = {
    .destroy = terminal_view_destroy,
    .render = terminal_view_render,
    .set_bounds = terminal_view_set_bounds,
    .on_event = terminal_view_on_event,
};

struct yetty_yterm_terminal {
    struct yetty_yterm_view view; /* MUST be first - allows cast to view */
    struct yetty_ycore_event_listener listener;
    struct yetty_yterm_terminal_context context;
    uint32_t cols;
    uint32_t rows;
    struct yetty_yrender_terminal_layer *layers[YETTY_YTERM_TERMINAL_MAX_LAYERS];
    size_t layer_count;
    yetty_ycore_pipe_id pty_pipe_id;
    /* Render targets - one per layer for render_layer */
    struct yetty_ypaint_core_target *layer_targets[YETTY_YTERM_TERMINAL_MAX_LAYERS];
    int shutting_down;
    struct yetty_yterm_pty_reader *pty_reader;

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
};

/* How many lines a single mouse-wheel notch moves the scrollback view. */
#define YETTY_YTERM_WHEEL_LINES_PER_TICK 3

/* Forward declarations */
static void terminal_read_pty(struct yetty_yterm_terminal *terminal);
static struct yetty_ycore_void_result terminal_render_frame(struct yetty_yterm_terminal *terminal,
                                                            struct yetty_ypaint_core_target *target);

/* PTY pipe alloc callback — provides buffer for uv_pipe_t reads.
 * One reusable per-terminal buffer, lazily allocated. 64KB matches
 * libuv's default suggested_size on Linux/macOS and is plenty for one
 * PTY chunk; the read callback drains it before the next call. */
#define YETTY_YTERM_PTY_READ_BUF_SIZE (64 * 1024)
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

/* PTY pipe read callback — feeds data to pty_reader, triggers render */
static void terminal_pty_pipe_read(void *ctx, const char *buf, long nread)
{
    struct yetty_yterm_terminal *terminal = ctx;

    if (nread > 0 && terminal->pty_reader) {
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
        yetty_yterm_pty_reader_feed(terminal->pty_reader, buf, (size_t)nread);
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
        struct yetty_ycore_input_pipe *pipe =
            terminal->context.yetty_context.app_context.platform_input_pipe;
        if (pipe && pipe->ops && pipe->ops->write) {
            struct yetty_yui_event ev = {.type = YETTY_YCORE_SHUTDOWN};
            pipe->ops->write(pipe, &ev, sizeof(ev));
        }
    } else {
        ydebug("terminal_pty_pipe_read: skipped (nread=%ld pty_reader=%p)", nread,
               (void *)(terminal ? terminal->pty_reader : NULL));
    }
}

/* PTY write callback for layers */
static void terminal_pty_write_callback(const char *data, size_t len, void *userdata)
{
    struct yetty_yterm_terminal *terminal = userdata;
    if (terminal->context.pty && terminal->context.pty->ops && terminal->context.pty->ops->write) {
        terminal->context.pty->ops->write(terminal->context.pty, data, len);
        ydebug("terminal_pty_write: wrote %zu bytes to PTY", len);
    }
}

/* Direct PTY write — used by the mouse-OSC emitter. Bypasses the layer
 * callback path because mouse events come from the GLFW input pipe, not
 * from a layer. */
static void terminal_pty_write_raw(struct yetty_yterm_terminal *terminal, const char *data,
                                   size_t len)
{
    if (terminal->context.pty && terminal->context.pty->ops && terminal->context.pty->ops->write) {
        terminal->context.pty->ops->write(terminal->context.pty, data, len);
    }
}

/* Build one yface envelope around `payload` and ship it to the inferior.
 * compressed=0 because input events are short — LZ4 framing would dominate. */
YETTY_EXTERNAL_CALLBACK
static void terminal_yface_emit(struct yetty_yterm_terminal *terminal, int osc_code,
                                const void *payload, size_t len)
{
    if (!terminal->emit_yface) {
        return;
    }
    struct yetty_ycore_void_result r = yetty_yface_start_write(terminal->emit_yface, osc_code,
                                                               /*compressed=*/0,
                                                               /*args=*/NULL, /*args_len=*/0);
    if (!r.ok) {
        goto reset;
    }
    r = yetty_yface_write(terminal->emit_yface, payload, len);
    if (!r.ok) {
        goto reset;
    }
    r = yetty_yface_finish_write(terminal->emit_yface);
    if (!r.ok) {
        goto reset;
    }

    struct yetty_ycore_buffer *out = yetty_yface_out_buf(terminal->emit_yface);
    if (out && out->size) {
        terminal_pty_write_raw(terminal, (const char *)out->data, out->size);
    }

reset:
    if (terminal->emit_yface) {
        struct yetty_ycore_buffer *out = yetty_yface_out_buf(terminal->emit_yface);
        if (out) {
            yetty_ycore_buffer_clear(out);
        }
    }
}

/* Layer → terminal OSC emit. Wired into ymgui-layer at create time so
 * the layer can ship FOCUS / RESIZE events back to the focused client
 * without owning its own emit_yface. */
static void terminal_layer_emit_osc(int osc_code, const void *payload, size_t len, void *userdata)
{
    struct yetty_yterm_terminal *terminal = userdata;
    terminal_yface_emit(terminal, osc_code, payload, len);
}

/* Card-aware mouse forwarding. Each emit carries a card_id and
 * card-local pixel coords. card_id=0 means "no card here" — clients
 * use that to clear their hover state. */
static void terminal_emit_card_mouse_button(struct yetty_yterm_terminal *terminal, uint32_t card_id,
                                            float lx, float ly, int button, int press,
                                            float wheel_dy)
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
    terminal_yface_emit(terminal, YMGUI_OSC_SC_MOUSE, &msg, sizeof(msg));
}

static void terminal_emit_card_mouse_move(struct yetty_yterm_terminal *terminal, uint32_t card_id,
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
    terminal_yface_emit(terminal, YMGUI_OSC_SC_MOUSE, &msg, sizeof(msg));
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
 * (caller should treat the keystroke as consumed), 0 otherwise. */
static int terminal_emit_card_key(struct yetty_yterm_terminal *terminal, uint32_t kind, int key,
                                  int mods, uint32_t codepoint)
{
    uint32_t focused =
        terminal->ymgui_layer ? yetty_yterm_terminal_layer_ymgui_layer_focused_card(terminal->ymgui_layer) : 0;
    if (focused == 0) {
        return 0;
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
    terminal_yface_emit(terminal, YMGUI_OSC_SC_KEY, &msg, sizeof(msg));
    return 1;
}

/*-----------------------------------------------------------------------
 * Scrollback view (tmux-style copy mode)
 *
 * Mouse-wheel events drive both layers into scrollback mode together.
 * view_top_total_idx is an absolute line index — text-layer's sb_count
 * and ypaint canvas's rolling_row_0 stay in lockstep (every text scroll
 * triggers a ypaint scroll and vice versa), so the same index identifies
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
 * has scrolled further (e.g. ypaint just absorbed a multi-page PDF) doesn't
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
static void terminal_push_view_top(struct yetty_yterm_terminal *terminal)
{
    for (size_t i = 0; i < terminal->layer_count; i++) {
        struct yetty_yrender_terminal_layer *layer = terminal->layers[i];
        if (layer && layer->ops && layer->ops->set_view_top) {
            layer->ops->set_view_top(layer, terminal->scrollback_active,
                                     terminal->view_top_total_idx);
        }
    }
    if (terminal->context.yetty_context.event_loop) {
        terminal->context.yetty_context.event_loop->ops->request_render(
            terminal->context.yetty_context.event_loop);
    }
}

/* Apply a relative wheel delta. Positive lines = scroll up (older). On the
 * first wheel-up out of live mode we anchor view_top one line back from
 * the current live position and enter scrollback. Scrolling past the live
 * anchor exits back to live. */
static void terminal_scrollback_apply(struct yetty_yterm_terminal *terminal, int lines)
{
    uint32_t live = terminal_live_anchor(terminal);

    if (!terminal->scrollback_active) {
        if (lines <= 0) {
            return; /* downward wheel in live mode: nothing to do */
        }
        if (live == 0) {
            return; /* nothing in scrollback yet */
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
    terminal_push_view_top(terminal);
}

/* Force a return to live, regardless of current view position. */
static void terminal_scrollback_exit(struct yetty_yterm_terminal *terminal)
{
    if (!terminal->scrollback_active) {
        return;
    }
    terminal->scrollback_active = 0;
    terminal->view_top_total_idx = terminal_live_anchor(terminal);
    ydebug("scrollback: EXIT");
    terminal_push_view_top(terminal);
}

/* Mouse-subscription callback fired by the text-layer when libvterm flips
 * DEC mode 1500/1501. Latch state on the terminal. (No pane-size emission
 * on the rising edge — under the card model, each CARD_PLACE confirms
 * the card's pixel size via YMGUI_OSC_SC_RESIZE individually.) */
static void terminal_mouse_sub_callback(int click_enabled, int move_enabled, void *userdata)
{
    struct yetty_yterm_terminal *terminal = userdata;
    terminal->mouse_click_subscribed = click_enabled;
    terminal->mouse_move_subscribed = move_enabled;
    ydebug("terminal: mouse_sub click=%d move=%d", click_enabled, move_enabled);
}

/* Alt-screen toggle from text-layer (libvterm). Broadcast to every
 * layer that implements set_alt_screen so each can save/restore. */
static void terminal_alt_screen_callback(int active, void *userdata)
{
    struct yetty_yterm_terminal *terminal = userdata;
    ydebug("terminal: alt_screen=%d (broadcasting to %zu layers)", active, terminal->layer_count);
    for (size_t i = 0; i < terminal->layer_count; i++) {
        struct yetty_yrender_terminal_layer *layer = terminal->layers[i];
        if (layer && layer->ops && layer->ops->set_alt_screen) {
            layer->ops->set_alt_screen(layer, active);
        }
    }
    if (terminal->context.yetty_context.event_loop &&
        terminal->context.yetty_context.event_loop->ops &&
        terminal->context.yetty_context.event_loop->ops->request_render) {
        terminal->context.yetty_context.event_loop->ops->request_render(
            terminal->context.yetty_context.event_loop);
    }
}

/* Request render callback for layers */
static void terminal_request_render_callback(void *userdata)
{
    struct yetty_yterm_terminal *terminal = userdata;
    ydebug("terminal_request_render_callback: event_loop=%p",
           (void *)terminal->context.yetty_context.event_loop);
    if (terminal->context.yetty_context.event_loop &&
        terminal->context.yetty_context.event_loop->ops &&
        terminal->context.yetty_context.event_loop->ops->request_render) {
        ydebug("terminal_request_render_callback: calling request_render");
        terminal->context.yetty_context.event_loop->ops->request_render(
            terminal->context.yetty_context.event_loop);
    }
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
            if (YETTY_IS_ERR(res)) {
                yerror("terminal_scroll_callback: layer[%zu] scroll failed: %s", i, res.error.msg);
                return res;
            }
        }
    }
    ydebug("terminal_scroll_callback EXIT: lines=%d", lines);
    return YETTY_OK_VOID();
}

/* Cursor callback - propagate cursor position from source layer to all other
 * layers */
static void terminal_cursor_callback(struct yetty_yrender_terminal_layer *source,
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
            layer->ops->set_cursor(layer, cursor_pos.cols, cursor_pos.rows);
        } else {
            ydebug("terminal_cursor_callback: skipping layer[%zu]=%p (source=%d "
                   "has_set_cursor=%d)",
                   i, (void *)layer, layer == source, layer->ops && layer->ops->set_cursor);
        }
    }
    ydebug("terminal_cursor_callback EXIT: col=%u row=%u", cursor_pos.cols, cursor_pos.rows);
}

/* Event handler - only for PTY poll events registered directly with event loop
 */
static struct yetty_ycore_int_result terminal_event_handler(
    struct yetty_ycore_event_listener *listener, const struct yetty_yui_event *event)
{
    struct yetty_yterm_terminal *terminal =
        container_of(listener, struct yetty_yterm_terminal, listener);

    /* PTY data now arrives via uv_pipe_t read callback, not through events */
    (void)terminal;
    (void)event;

    return YETTY_OK(yetty_ycore_int, 0);
}

/* Render a frame using layered rendering */
static struct yetty_ycore_void_result terminal_render_frame(struct yetty_yterm_terminal *terminal,
                                                            struct yetty_ypaint_core_target *target)
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

    /*
   * Render each layer to its target. Layer 0 is text_layer, layer 1 is
   * ypaint_layer (see terminal_create). Time them separately so we can tell
   * which layer dominates the frame cost.
   */
    ytime_start(layers);
    for (size_t i = 0; i < terminal->layer_count; i++) {
        struct yetty_yrender_terminal_layer *layer = terminal->layers[i];
        struct yetty_ypaint_core_target *layer_target = terminal->layer_targets[i];

        if (!layer || !layer_target) {
            yerror("//TODO: THIS SHOULD BE FIXED!!!!!!! IF THIS CONDITION HAPPENS "
                   "SHOULD RETURN ERROR");
            continue;
        }

        struct yetty_ycore_void_result res;
        if (i == 0) {
            ytime_start(text_layer);
            res = layer->ops->render(layer, layer_target);
            ytime_report(text_layer);
        } else if (i == 1) {
            ytime_start(ypaint_layer);
            res = layer->ops->render(layer, layer_target);
            ytime_report(ypaint_layer);
        } else {
            res = layer->ops->render(layer, layer_target);
        }

        if (!YETTY_IS_OK(res)) {
            yerror("terminal_render_frame: layer %zu render failed: %s", i, res.error.msg);
            return res;
        }
    }
    ytime_report(layers);

    /* Blend all layer targets into the provided target (big_target from yetty) */
    ytime_start(blend);
    struct yetty_ycore_void_result res =
        target->ops->blend(target, terminal->layer_targets, terminal->layer_count);
    ytime_report(blend);

    if (!YETTY_IS_OK(res)) {
        yerror("terminal_render_frame: blend failed: %s", res.error.msg);
        return res;
    }

    ydebug("terminal_render_frame: done, rendered %zu layers", terminal->layer_count);
    ytime_report(frame_render);
    return YETTY_OK_VOID();
}

/* Read from PTY via pty_reader */
static void terminal_read_pty(struct yetty_yterm_terminal *terminal)
{
    if (!terminal->pty_reader) {
        return;
    }

    int bytes_read = yetty_yterm_pty_reader_read(terminal->pty_reader);
    if (bytes_read > 0 && terminal->layer_count > 0) {
        struct yetty_yrender_terminal_layer *layer = terminal->layers[0];
        if (layer && layer->dirty) {
            terminal->context.yetty_context.event_loop->ops->request_render(
                terminal->context.yetty_context.event_loop);
        }
    }
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

    /* Create PTY */
    struct yetty_yplatform_pty_factory *pty_factory = yetty_context->app_context.pty_factory;
    if (pty_factory && pty_factory->ops && pty_factory->ops->create_pty) {
        struct yetty_yplatform_pty_result pty_res =
            pty_factory->ops->create_pty(pty_factory, terminal->context.yetty_context.event_loop);
        if (YETTY_IS_OK(pty_res)) {
            terminal->context.pty = pty_res.value;
            ydebug("terminal_create: PTY created at %p", (void *)terminal->context.pty);

            /* Create PTY reader */
            struct yetty_yterm_pty_reader_result reader_res =
                yetty_yterm_pty_reader_create(terminal->context.pty);
            if (YETTY_IS_OK(reader_res)) {
                terminal->pty_reader = reader_res.value;
                ydebug("terminal_create: pty_reader created");
            }

            /* Long-lived yface for emit_*. One per terminal — out_buf is
       * cleared after every send so it stays at the steady-state
       * high-water mark rather than growing per-event. */
            {
                struct yetty_yface_ptr_result yr = yetty_yface_create();
                if (YETTY_IS_OK(yr)) {
                    terminal->emit_yface = yr.value;
                } else {
                    ydebug("terminal_create: emit_yface alloc failed: %s", yr.error.msg);
                }
            }

            /* Register PTY pipe — uv_pipe_t reads data, callbacks handle it */
            struct yetty_platform_pty_pipe_source *pipe_source =
                terminal->context.pty->ops->pipe_source(terminal->context.pty);
            if (pipe_source && terminal->pty_reader) {
                struct yetty_ycore_pipe_id_result pipe_res =
                    terminal->context.yetty_context.event_loop->ops->register_pty_pipe(
                        terminal->context.yetty_context.event_loop, pipe_source,
                        terminal_pty_pipe_alloc, terminal_pty_pipe_read, terminal);
                if (YETTY_IS_OK(pipe_res)) {
                    terminal->pty_pipe_id = pipe_res.value;
                    ydebug("terminal_create: PTY pipe registered");
                }
            }
        } else {
            ydebug("terminal_create: failed to create PTY (non-fatal)");
        }
    }

    /* Create text layer */
    struct yetty_yterm_terminal_layer_result text_layer_res =
        yetty_yterm_terminal_text_layer_create(
            cols, rows, yetty_context, terminal_pty_write_callback, terminal,
            terminal_request_render_callback, terminal, terminal_scroll_callback, terminal,
            terminal_cursor_callback, terminal);
    if (YETTY_IS_OK(text_layer_res)) {
        /* Mouse-subscription callback — text-layer's libvterm settermprop hook
     * forwards DEC ?1500 / ?1501 changes here. */
        text_layer_res.value->mouse_sub_fn = terminal_mouse_sub_callback;
        text_layer_res.value->mouse_sub_userdata = terminal;
        /* Alt-screen toggle callback — text-layer's settermprop hook fires
     * this when libvterm processes DEC ?1047/?1049/?47. */
        text_layer_res.value->alt_screen_fn = terminal_alt_screen_callback;
        text_layer_res.value->alt_screen_userdata = terminal;
    }
    if (!YETTY_IS_OK(text_layer_res)) {
        yerror("terminal_create: failed to create text layer: %s", text_layer_res.error.msg);
        yetty_yterm_pty_reader_destroy(terminal->pty_reader);
        if (terminal->context.pty) {
            terminal->context.pty->ops->destroy(terminal->context.pty);
        }
        free(terminal);
        return YETTY_ERR(yetty_yterm_terminal, text_layer_res.error.msg);
    }
    yetty_yterm_terminal_layer_add(terminal, text_layer_res.value);
    ydebug("terminal_create: text_layer created and added");

    /* Register text layer as default sink for pty_reader */
    if (terminal->pty_reader) {
        yetty_yterm_pty_reader_register_default_sink(terminal->pty_reader, text_layer_res.value);
        ydebug("terminal_create: text_layer registered as default sink");
    }

    /* Create ypaint scrolling layer (overlay on top of text) */
    {
        struct yetty_yrender_terminal_layer *text_layer = text_layer_res.value;
        struct yetty_yterm_terminal_layer_result ypaint_res = yetty_yterm_ypaint_layer_create(
            cols, rows, text_layer->cell_size.width, text_layer->cell_size.height,
            1, /* scrolling_mode = true */
            yetty_context, terminal_request_render_callback, terminal, terminal_scroll_callback,
            terminal, terminal_cursor_callback, terminal);
        if (YETTY_IS_OK(ypaint_res)) {
            yetty_yterm_terminal_layer_add(terminal, ypaint_res.value);
            ydebug("terminal_create: ypaint scrolling layer created and added");

            /* Register ypaint layer for the four ypaint OSC codes
       * (clear/bin/yaml/overlay live in the 600000–600003 block). */
            if (terminal->pty_reader) {
                yetty_yterm_pty_reader_register_osc_sink(terminal->pty_reader,
                                                         YETTY_OSC_YPAINT_CLEAR, ypaint_res.value);
                yetty_yterm_pty_reader_register_osc_sink(terminal->pty_reader, YETTY_OSC_YPAINT_BIN,
                                                         ypaint_res.value);
                yetty_yterm_pty_reader_register_osc_sink(terminal->pty_reader,
                                                         YETTY_OSC_YPAINT_YAML, ypaint_res.value);
                yetty_yterm_pty_reader_register_osc_sink(
                    terminal->pty_reader, YETTY_OSC_YPAINT_OVERLAY, ypaint_res.value);
                ydebug("terminal_create: ypaint layer registered for OSC 600000-600003");
            }
        } else {
            ydebug("terminal_create: failed to create ypaint layer (non-fatal): %s",
                   ypaint_res.error.msg);
        }
    }

    /* Create shader-glyph layer — animated procedurals at cells whose
   * glyph_index is in the shader-glyph range (top half of u32). Sits
   * above text-layer in compose order so the animations overlay text.
   * Reads text-layer's cell buffer directly (text_layer must outlive). */
    {
        struct yetty_yrender_terminal_layer *text_layer = text_layer_res.value;
        struct yetty_yterm_terminal_layer_result sg_res = yetty_yterm_shader_glyph_layer_create(
            cols, rows, text_layer->cell_size.width, text_layer->cell_size.height, text_layer,
            yetty_context, terminal_request_render_callback, terminal, terminal_scroll_callback,
            terminal, terminal_cursor_callback, terminal);
        if (YETTY_IS_OK(sg_res)) {
            yetty_yterm_terminal_layer_add(terminal, sg_res.value);
            ydebug("terminal_create: shader_glyph layer created and added");
        } else {
            ydebug("terminal_create: failed to create shader_glyph layer (non-fatal): %s",
                   sg_res.error.msg);
        }
    }

    /* Create ymgui layer (Dear ImGui frame, cursor-anchored, terminal-scrolling) */
    {
        struct yetty_yrender_terminal_layer *text_layer = text_layer_res.value;
        struct yetty_yterm_terminal_layer_result ymgui_res = yetty_yterm_ymgui_layer_create(
            cols, rows, text_layer->cell_size.width, text_layer->cell_size.height, yetty_context,
            terminal_request_render_callback, terminal, terminal_scroll_callback, terminal,
            terminal_cursor_callback, terminal);
        if (YETTY_IS_OK(ymgui_res)) {
            yetty_yterm_terminal_layer_add(terminal, ymgui_res.value);
            terminal->ymgui_layer = ymgui_res.value;
            /* Wire the layer's emit-back path so it can ship FOCUS / RESIZE
       * events through this terminal's emit_yface. */
            ymgui_res.value->emit_osc_fn = terminal_layer_emit_osc;
            ymgui_res.value->emit_osc_userdata = terminal;
            if (terminal->pty_reader) {
                yetty_yterm_pty_reader_register_osc_sink(terminal->pty_reader, YMGUI_OSC_CS_CLEAR,
                                                         ymgui_res.value);
                yetty_yterm_pty_reader_register_osc_sink(terminal->pty_reader, YMGUI_OSC_CS_FRAME,
                                                         ymgui_res.value);
                yetty_yterm_pty_reader_register_osc_sink(terminal->pty_reader, YMGUI_OSC_CS_TEX,
                                                         ymgui_res.value);
                yetty_yterm_pty_reader_register_osc_sink(terminal->pty_reader,
                                                         YMGUI_OSC_CS_CARD_PLACE, ymgui_res.value);
                yetty_yterm_pty_reader_register_osc_sink(terminal->pty_reader,
                                                         YMGUI_OSC_CS_CARD_REMOVE, ymgui_res.value);
                ydebug("terminal_create: ymgui layer registered for OSC 610000-610004");
            }
        } else {
            ydebug("terminal_create: failed to create ymgui layer (non-fatal): %s",
                   ymgui_res.error.msg);
        }
    }

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
        if (!YETTY_IS_OK(target_res)) {
            ydebug("terminal_create: failed to create layer target %zu", i);
            /* Clean up already created targets */
            for (size_t j = 0; j < i; j++) {
                if (terminal->layer_targets[j]) {
                    terminal->layer_targets[j]->ops->destroy(terminal->layer_targets[j]);
                }
            }
            if (terminal->context.pty) {
                terminal->context.pty->ops->destroy(terminal->context.pty);
            }
            free(terminal);
            return YETTY_ERR(yetty_yterm_terminal, "failed to create layer target");
        }
        terminal->layer_targets[i] = target_res.value;
    }
    ydebug("terminal_create: layer targets created");

    return YETTY_OK(yetty_yterm_terminal, terminal);
}

struct yetty_ycore_void_result yetty_yterm_terminal_destroy(struct yetty_yterm_terminal *terminal)
{
    size_t i;
    struct yetty_ycore_void_result first_err = YETTY_OK_VOID();

    if (!terminal) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yterm_terminal_destroy: NULL terminal");
    }

    ydebug("terminal_destroy: starting");

    /* Destroy layer targets */
    for (i = 0; i < terminal->layer_count; i++) {
        if (terminal->layer_targets[i] && terminal->layer_targets[i]->ops &&
            terminal->layer_targets[i]->ops->destroy) {
            ydebug("terminal_destroy: destroying layer_target %zu", i);
            terminal->layer_targets[i]->ops->destroy(terminal->layer_targets[i]);
        }
    }
    ydebug("terminal_destroy: layer_targets destroyed");

    /* Destroy layers */
    for (i = 0; i < terminal->layer_count; i++) {
        struct yetty_yrender_terminal_layer *layer = terminal->layers[i];
        if (layer && layer->ops && layer->ops->destroy) {
            ydebug("terminal_destroy: destroying layer %zu", i);
            layer->ops->destroy(layer);
        }
    }
    ydebug("terminal_destroy: layers destroyed");

    if (terminal->context.pty && terminal->context.pty->ops &&
        terminal->context.pty->ops->destroy) {
        ydebug("terminal_destroy: destroying pty");
        struct yetty_ycore_void_result pr =
            terminal->context.pty->ops->destroy(terminal->context.pty);
        if (YETTY_IS_ERR(pr)) {
            first_err = pr;
        }
    }

    /* event_loop is owned by yetty, not terminal - do not destroy */

    /* Destroy PTY reader */
    if (terminal->pty_reader) {
        ydebug("terminal_destroy: destroying pty_reader");
        yetty_yterm_pty_reader_destroy(terminal->pty_reader);
    }

    if (terminal->emit_yface) {
        yetty_yface_destroy(terminal->emit_yface);
        terminal->emit_yface = NULL;
    }

    free(terminal->pty_read_buf);

    ydebug("terminal_destroy: freeing terminal struct");
    free(terminal);
    ydebug("terminal_destroy: done");

    if (YETTY_IS_ERR(first_err)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yterm_terminal_destroy: pty destroy failed",
                         first_err);
    }
    return YETTY_OK_VOID();
}

/* Terminal input */

void yetty_yterm_terminal_write(struct yetty_yterm_terminal *terminal, const char *data, size_t len)
{
    if (!terminal || !data || len == 0) {
        return;
    }

    /* Send to first layer (text layer) */
    if (terminal->layer_count > 0) {
        struct yetty_yrender_terminal_layer *layer = terminal->layers[0];
        if (layer && layer->ops && layer->ops->write) {
            layer->ops->write(layer, 0, data, len);
            ydebug("terminal_write: sent %zu bytes to text layer", len);
        }
    }
}

void yetty_yterm_terminal_resize_grid(struct yetty_yterm_terminal *terminal,
                                      struct yetty_ycore_grid_size grid_size)
{
    if (!terminal) {
        return;
    }

    terminal->cols = grid_size.cols;
    terminal->rows = grid_size.rows;

    for (size_t i = 0; i < terminal->layer_count; i++) {
        struct yetty_yrender_terminal_layer *layer = terminal->layers[i];
        if (layer && layer->ops && layer->ops->resize_grid) {
            layer->ops->resize_grid(layer, grid_size);
        }
    }
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

void yetty_yterm_terminal_layer_add(struct yetty_yterm_terminal *terminal,
                                    struct yetty_yrender_terminal_layer *layer)
{
    if (!terminal || !layer) {
        return;
    }

    if (terminal->layer_count >= YETTY_YTERM_TERMINAL_MAX_LAYERS) {
        return;
    }

    terminal->layers[terminal->layer_count++] = layer;
}

void yetty_yterm_terminal_layer_remove(struct yetty_yterm_terminal *terminal,
                                       struct yetty_yrender_terminal_layer *layer)
{
    size_t i;

    if (!terminal || !layer) {
        return;
    }

    for (i = 0; i < terminal->layer_count; i++) {
        if (terminal->layers[i] == layer) {
            memmove(&terminal->layers[i], &terminal->layers[i + 1],
                    (terminal->layer_count - i - 1) * sizeof(terminal->layers[0]));
            terminal->layer_count--;
            return;
        }
    }
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

struct yetty_yterm_view *yetty_yterm_terminal_as_view(struct yetty_yterm_terminal *terminal)
{
    return terminal ? &terminal->view : NULL;
}

static struct yetty_ycore_void_result terminal_view_destroy(struct yetty_yterm_view *view)
{
    struct yetty_yterm_terminal *terminal = container_of(view, struct yetty_yterm_terminal, view);
    return yetty_yterm_terminal_destroy(terminal);
}

static struct yetty_ycore_void_result terminal_view_render(
    struct yetty_yterm_view *view, struct yetty_ypaint_core_target *render_target)
{
    struct yetty_yterm_terminal *terminal = container_of(view, struct yetty_yterm_terminal, view);

    return terminal_render_frame(terminal, render_target);
}

static struct yetty_ycore_void_result terminal_view_set_bounds(struct yetty_yterm_view *view,
                                                               struct yetty_yui_rect bounds)
{
    struct yetty_yterm_terminal *terminal = container_of(view, struct yetty_yterm_terminal, view);

    /* Store bounds in view */
    view->bounds = bounds;

    /* Terminal handles resize via YETTY_EVENT_RESIZE from event loop */
    /* For now, just log - the actual resize happens through the event system */
    ydebug("terminal_view_set_bounds: %.0fx%.0f at (%.0f,%.0f)", bounds.w, bounds.h, bounds.x,
           bounds.y);

    (void)terminal;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_int_result terminal_view_on_event(struct yetty_yterm_view *view,
                                                            const struct yetty_yui_event *event)
{
    struct yetty_yterm_terminal *terminal = container_of(view, struct yetty_yterm_terminal, view);

    switch (event->type) {
    case YETTY_YCORE_KEY_DOWN:
        ydebug("terminal: KEY_DOWN key=%d mods=%d", event->key.key, event->key.mods);
        /* In scrollback view, Enter exits and is consumed (matches tmux copy
     * mode). Other keys also exit scrollback before falling through to
     * normal dispatch — this means typing while in scrollback returns to
     * live and delivers the keystroke to the shell, which is what users
     * expect when they meant to interact with the prompt. */
        if (terminal->scrollback_active) {
            int is_enter = (event->key.key == 257); /* GLFW_KEY_ENTER */
            terminal_scrollback_exit(terminal);
            if (is_enter) {
                return YETTY_OK(yetty_ycore_int, 1);
            }
        }
        /* If a ymgui card has focus, route the keystroke to it as an OSC
     * envelope and DO NOT also feed libvterm — otherwise the shell
     * would see the keystroke alongside the card. */
        if (terminal_emit_card_key(terminal, YETTY_YMGUI_INPUT_KEY_DOWN, event->key.key, event->key.mods,
                                   0)) {
            return YETTY_OK(yetty_ycore_int, 1);
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

    case YETTY_YCORE_KEY_UP:
        ydebug("terminal: KEY_UP key=%d mods=%d", event->key.key, event->key.mods);
        if (terminal_emit_card_key(terminal, YETTY_YMGUI_INPUT_KEY_UP, event->key.key, event->key.mods,
                                   0)) {
            return YETTY_OK(yetty_ycore_int, 1);
        }
        return YETTY_OK(yetty_ycore_int, 0);

    case YETTY_YCORE_CHAR:
        ydebug("terminal: CHAR codepoint=U+%04X mods=%d", event->chr.codepoint, event->chr.mods);
        if (terminal->scrollback_active) {
            terminal_scrollback_exit(terminal);
        }
        /* See KEY_DOWN: focused card consumes the codepoint. */
        if (terminal_emit_card_key(terminal, YETTY_YMGUI_INPUT_KEY_CHAR, -1, event->chr.mods,
                                   event->chr.codepoint)) {
            return YETTY_OK(yetty_ycore_int, 1);
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
                terminal->layer_targets[i]->ops->resize(terminal->layer_targets[i], vp);
            }
        }

        /* Calculate grid dimensions from first layer's cell size */
        if (terminal->layer_count > 0) {
            struct yetty_yrender_terminal_layer *layer = terminal->layers[0];
            float cell_w = layer->cell_size.width > 0 ? layer->cell_size.width : 10.0f;
            float cell_h = layer->cell_size.height > 0 ? layer->cell_size.height : 20.0f;
            uint32_t new_cols = (uint32_t)(width / cell_w);
            uint32_t new_rows = (uint32_t)(height / cell_h);

            if (new_cols > 0 && new_rows > 0 &&
                (new_cols != terminal->cols || new_rows != terminal->rows)) {
                yetty_yterm_terminal_resize_grid(
                    terminal, (struct yetty_ycore_grid_size){.cols = new_cols, .rows = new_rows});
                if (terminal->context.pty && terminal->context.pty->ops &&
                    terminal->context.pty->ops->resize) {
                    terminal->context.pty->ops->resize(terminal->context.pty, new_cols, new_rows);
                }
            }
        }
        /* Cards self-announce their pixel size via per-card YMGUI_OSC_SC_RESIZE
     * fired from the layer (see ymgui-layer.c::ymgui_resize_grid). No
     * pane-size emission needed here. */
        return YETTY_OK(yetty_ycore_int, 1);
    }

    case YETTY_YCORE_ZOOM_CELL_SIZE: {
        /* Structural zoom — scale each layer's cell pixel size (via set_cell_size,
     * which updates BOTH the layer field AND the shader uniform), then
     * re-derive cols/rows from the current view bounds and propagate to the
     * PTY + vterm. */
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

        for (size_t i = 0; i < terminal->layer_count; i++) {
            struct yetty_yrender_terminal_layer *layer = terminal->layers[i];
            if (!layer) {
                continue;
            }
            struct yetty_ycore_pixel_size new_cs = layer->cell_size;
            if (new_cs.width > 0) {
                new_cs.width *= factor;
            }
            if (new_cs.height > 0) {
                new_cs.height *= factor;
            }
            if (layer->ops && layer->ops->set_cell_size) {
                layer->ops->set_cell_size(layer, new_cs);
            } else {
                layer->cell_size = new_cs;
                layer->dirty = 1;
            }
        }

        if (terminal->layer_count > 0) {
            struct yetty_yrender_terminal_layer *layer = terminal->layers[0];
            float cw = layer->cell_size.width > 0 ? layer->cell_size.width : 10.0f;
            float ch = layer->cell_size.height > 0 ? layer->cell_size.height : 20.0f;
            uint32_t new_cols = (uint32_t)(view_w / cw);
            uint32_t new_rows = (uint32_t)(view_h / ch);
            if (new_cols == 0) {
                new_cols = 1;
            }
            if (new_rows == 0) {
                new_rows = 1;
            }
            if (new_cols != terminal->cols || new_rows != terminal->rows) {
                yetty_yterm_terminal_resize_grid(
                    terminal, (struct yetty_ycore_grid_size){.cols = new_cols, .rows = new_rows});
                if (terminal->context.pty && terminal->context.pty->ops &&
                    terminal->context.pty->ops->resize) {
                    terminal->context.pty->ops->resize(terminal->context.pty, new_cols, new_rows);
                }
            }
        }

        if (terminal->context.yetty_context.event_loop &&
            terminal->context.yetty_context.event_loop->ops->request_render) {
            terminal->context.yetty_context.event_loop->ops->request_render(
                terminal->context.yetty_context.event_loop);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }

    case YETTY_YCORE_ZOOM_VISUAL_APPLY: {
        float scale = event->zoom_visual_apply.scale;
        float ox = event->zoom_visual_apply.offset_x;
        float oy = event->zoom_visual_apply.offset_y;
        for (size_t i = 0; i < terminal->layer_count; i++) {
            struct yetty_yrender_terminal_layer *layer = terminal->layers[i];
            if (layer && layer->ops && layer->ops->set_visual_zoom) {
                layer->ops->set_visual_zoom(layer, scale, ox, oy);
            }
        }
        ydebug("terminal: ZOOM_VISUAL_APPLY scale=%.2f off=(%.1f,%.1f)", scale, ox, oy);
        return YETTY_OK(yetty_ycore_int, 1);
    }

    case YETTY_YCORE_SHUTDOWN:
        ydebug("terminal: SHUTDOWN received");
        terminal->shutting_down = 1;
        return YETTY_OK(yetty_ycore_int, 1);

    case YETTY_YCORE_POLL_READABLE:
        ydebug("terminal: POLL_READABLE");
        terminal_read_pty(terminal);
        return YETTY_OK(yetty_ycore_int, 1);

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
               "click_sub=%d",
               event->type == YETTY_YCORE_MOUSE_DOWN ? "DOWN" : "UP", event->mouse.x,
               event->mouse.y, view->bounds.w, view->bounds.h, view->bounds.x, view->bounds.y,
               terminal->mouse_click_subscribed);
        if (!terminal->mouse_click_subscribed) {
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

        uint32_t focused =
            terminal->ymgui_layer ? yetty_yterm_terminal_layer_ymgui_layer_focused_card(terminal->ymgui_layer) : 0;
        /* On release, route to the captured (focused) card so the client
     * sees a paired down/up. On press, hit-test the cursor. */
        struct yetty_yterm_ymgui_hit hit;
        if (press) {
            hit = terminal_resolve_card_hit(terminal, lx, ly, 0);
            /* Click-focus: update focus to whoever was clicked (incl. 0). */
            if (terminal->ymgui_layer) {
                yetty_yterm_terminal_layer_ymgui_layer_set_focus(terminal->ymgui_layer, hit.card_id);
            }
        } else {
            hit = terminal_resolve_card_hit(terminal, lx, ly, focused);
        }
        if (hit.card_id != 0) {
            terminal_emit_card_mouse_button(terminal, hit.card_id, hit.local_x, hit.local_y, btn,
                                            press, 0.0f);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }

    case YETTY_YCORE_MOUSE_MOVE:
    case YETTY_YCORE_MOUSE_DRAG: {
        ydebug("terminal: MOUSE_MOVE win=(%.1f,%.1f) move_sub=%d", event->mouse.x, event->mouse.y,
               terminal->mouse_move_subscribed);
        if (!terminal->mouse_move_subscribed) {
            return YETTY_OK(yetty_ycore_int, 0);
        }
        float lx = event->mouse.x - view->bounds.x;
        float ly = event->mouse.y - view->bounds.y;
        if (lx < 0.0f || ly < 0.0f || lx >= view->bounds.w || ly >= view->bounds.h) {
            return YETTY_OK(yetty_ycore_int, 0);
        }

        /* Capture during drag → route to focused card; otherwise topmost
     * under cursor. */
        uint32_t captured = 0;
        if (terminal->mouse_buttons_held && terminal->ymgui_layer) {
            captured = yetty_yterm_terminal_layer_ymgui_layer_focused_card(terminal->ymgui_layer);
        }
        struct yetty_yterm_ymgui_hit hit = terminal_resolve_card_hit(terminal, lx, ly, captured);
        if (hit.card_id != 0) {
            terminal_emit_card_mouse_move(terminal, hit.card_id, hit.local_x, hit.local_y,
                                          terminal->mouse_buttons_held);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }

    case YETTY_YCORE_SCROLL: {
        /* dy==0 dropped: wire only carries wheel_dy. */
        if (event->scroll.dy == 0.0f) {
            return YETTY_OK(yetty_ycore_int, 0);
        }

        float lx = event->scroll.x - view->bounds.x;
        float ly = event->scroll.y - view->bounds.y;
        if (lx < 0.0f || ly < 0.0f || lx >= view->bounds.w || ly >= view->bounds.h) {
            return YETTY_OK(yetty_ycore_int, 0);
        }

        /* Once in scrollback view, wheel always drives history. Otherwise
     * if a card is under the cursor (or if a subscribed-but-cardless
     * client is listening) the wheel goes outbound; else scrollback. */
        if (!terminal->scrollback_active && terminal->mouse_click_subscribed) {
            struct yetty_yterm_ymgui_hit hit = terminal_resolve_card_hit(terminal, lx, ly, 0);
            if (hit.card_id != 0) {
                terminal_emit_card_mouse_button(terminal, hit.card_id, hit.local_x, hit.local_y, 0,
                                                0, event->scroll.dy);
                return YETTY_OK(yetty_ycore_int, 1);
            }
        }

        int lines = (int)(event->scroll.dy * YETTY_YTERM_WHEEL_LINES_PER_TICK);
        if (lines == 0 && event->scroll.dy != 0.0f) {
            lines = (event->scroll.dy > 0) ? 1 : -1;
        }
        if (lines != 0) {
            terminal_scrollback_apply(terminal, lines);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }

    default:
        return YETTY_OK(yetty_ycore_int, 0);
    }
}
