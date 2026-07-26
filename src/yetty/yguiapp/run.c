/*
 * yguiapp/run.c — dual-mode launcher + in-terminal host for yguiapp:app.
 *
 * Plain C (not a yclass file). Hosts a yguiapp:app subclass instance in one of
 * two modes, deciding by yetty_running_under_yetty():
 *
 *   STANDALONE  — register the platform + yapp classes, create the glfw_platform
 *                 and drive the app's run() override (the full window/GPU bring-up
 *                 in app.c).
 *   TERMINAL    — inside a host yetty: a libuv loop over a single transport-pty +
 *                 ywire_connection (the #380 single-reader path). The framework's
 *                 figure output rides the rpc channel; forwarded mouse arrives on
 *                 the input channel; raw keystrokes on the raw channel. Terminal
 *                 raw mode is restored on every exit/error path.
 *
 * From the framework's perspective the two modes are identical: there is an
 * output transport to write to and a caller pushing input bytes via
 * framework_feed_input. The framework has no knowledge of which mode it's in.
 *
 * The shared two-level root/body construction (yetty_yguiapp_build_root_body) is
 * here so both the standalone run() (app.c) and the terminal host build the same
 * styled canvas; the build virtual is dispatched the same way in both modes.
 */

#include "yetty/yguiapp/run.h"

#include "input-encode.h"

#include <yetty/ycore/result.h>
#include <yetty/ycore/terminal-detect.h>
#include <yetty/ycore/types.h>
#include <yetty/yclass/class.h>
#include <yetty/yfont/font.h>
#include <yetty/yfont/msdf-font.h>
#include <yetty/ygui/framework-defs.h>
#include <yetty/ygui/framework.h>
#include <yetty/ygui/ygui.h>
#include <yetty/ygui/widget.h>
#include <yetty/yplatform/paths.h>
#include <yetty/ytrace/ytrace.h>

#include <stdio.h>
#include <stdlib.h>

/* Generated build-virtual stub (dispatches to the subclass override). */
struct yetty_ycore_void_result yetty_yguiapp_build(struct yetty_yclass_object *app,
                                                   struct yetty_yclass_object *root);

/* Platform bring-up symbols (provided by the executable's bootstrap sources). */
struct yetty_ycore_void_result yetty_yplatform_register(void);
struct yetty_ycore_void_result yetty_yapp_register(void);
struct yetty_yclass_object_ptr_result yetty_yplatform_glfw_platform_create(
    struct yetty_yclass_ctx *ctx);
struct yetty_ycore_void_result yetty_yplatform_platform_run(struct yetty_yclass_object *obj,
                                                            struct yetty_yclass_object *app,
                                                            int argc, char **argv);

/* Caption inset must match app.c's YGUIAPP_CHROME_CAPTION_H; the terminal host
 * has no chrome, so it always passes 0. */

/*===========================================================================
 * Shared root/body construction. Outer stretch vbox owns the viewport; an inner
 * brand body panel (column, padding, gap, justify-center) is what the app builds
 * into. Sets the framework root to the outer vbox.
 *=========================================================================*/
struct yetty_ycore_void_result yetty_yguiapp_build_root_body(struct yetty_yclass_object *engine,
                                                             float chrome_inset_top,
                                                             struct yetty_yclass_object **root_out,
                                                             struct yetty_yclass_object **body_out)
{
    if (root_out) {
        *root_out = NULL;
    }
    if (body_out) {
        *body_out = NULL;
    }

    struct yetty_yclass_object_ptr_result rr =
        yetty_ygui_widget_new(yetty_ygui_vbox_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "yguiapp: root new");
    struct yetty_yclass_object *root = rr.value;

    struct yetty_ygui_layout_const_ptr_result layout_res = yetty_ygui_widget_layout_get(root);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "yguiapp: root layout_get");
    struct yetty_ygui_layout l = *layout_res.value;
    l.align = YETTY_YGUI_ALIGN_STRETCH;
    struct yetty_ycore_void_result lr = yetty_ygui_widget_layout_set(root, &l);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, lr, "yguiapp: root layout");

    struct yetty_ycore_void_result sr = yetty_ygui_framework_set_root(engine, root);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "yguiapp: set_root");

    /* Body panel: defaults to ROW direction, so flip to COLUMN and style it. */
    struct yetty_yclass_object_ptr_result br =
        yetty_ygui_widget_add(root, yetty_ygui_panel_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "yguiapp: body add");
    struct yetty_yclass_object *body = br.value;

    struct yetty_ygui_layout_const_ptr_result body_layout_res = yetty_ygui_widget_layout_get(body);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, body_layout_res, "yguiapp: body layout_get");
    struct yetty_ygui_layout bl = *body_layout_res.value;
    bl.direction = YETTY_YGUI_FLEX_COLUMN;
    bl.align = YETTY_YGUI_ALIGN_STRETCH;
    bl.flex_grow = 1.0f;
    bl.padding_top = bl.padding_bottom = 24;
    bl.padding_left = bl.padding_right = 24;
    bl.padding_top += chrome_inset_top;
    bl.gap = 12;
    /* Center the demo's content vertically when it doesn't fill the available
     * height; demos that DO fill it set flex_grow on their own children, making
     * the justify a no-op. */
    bl.justify = YETTY_YGUI_JUSTIFY_CENTER;
    struct yetty_ycore_void_result blr = yetty_ygui_widget_layout_set(body, &bl);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, blr, "yguiapp: body layout");

    if (root_out) {
        *root_out = root;
    }
    if (body_out) {
        *body_out = body;
    }
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Terminal (in-host-yetty) host. Ported from demo/ygui/runner.c's client mode.
 *=========================================================================*/
#ifdef YETTY_YGUI_HAS_UV

#include <yetty/yclass/transport-pty.h>
#include <yetty/yterminal/client-input.h>
#include <yetty/yterminal/dcs-codes.h>
#include <yetty/ymgui/wire.h>
#include <yetty/ywire/channel.h>
#include <yetty/ywire/connection.h>
#include <yetty/ywire/wire-statemachine.h>

#include <signal.h>
#include <unistd.h>

#include <uv.h>

/*-----------------------------------------------------------------------------
 * The connection's transport (yetty_yclass_transport_pty) is the sole owner of
 * STDIN/STDOUT: it owns terminal raw-mode (the echo-loop fix — a cooked tty
 * would echo our OSC writes back), the non-blocking writer, and the fd()/pump()
 * reactor seam. The framework's figure output rides the rpc channel; forwarded
 * mouse events arrive on the input channel; raw keystrokes on the raw channel.
 *---------------------------------------------------------------------------*/
struct yetty_yguiapp_client {
    uv_loop_t loop;
    uv_poll_t stdin_poll;
    /* Writable-interest poll on the connection's out fd — armed only while
     * connection_want_write() reports queued outbound bytes, so a multi-MB
     * figure body drains at wire speed instead of 64 KB per 33 ms frame tick
     * (#457). */
    uv_poll_t out_poll;
    int out_poll_inited;
    int out_poll_armed;
    uv_signal_t sigwinch;
    uv_prepare_t prep;
    uv_timer_t frame_timer;                       /* keeps the loop ticking so prep drains output */
    struct yetty_yclass_transport_pty *transport; /* sole STDIN/STDOUT owner */
    struct yetty_ywire_connection *conn;          /* multiplexed link */
    struct yetty_yclass_object *engine;           /* ygui framework */
    /* Measurement font handed to the framework so widgets (textinput caret /
     * click hit-test) place carets against the SAME glyph advances the host
     * yetty renders the figure text with (font_id 0 = the shared default mono
     * font). Owned here — borrowed by the framework — destroyed after the
     * framework in teardown. NULL when the font could not be loaded, in which
     * case widgets fall back to the fixed per-char advance. */
    struct yetty_yfont_font *measure_font;
    int running;
};

static int yguiapp_client_on_key(struct yetty_yclass_object *engine, uint32_t key, int mods,
                                 void *userdata)
{
    (void)engine;
    (void)mods;
    struct yetty_yguiapp_client *cs = (struct yetty_yguiapp_client *)userdata;
    if (key == 'q' || key == 'Q' || key == 0x03 || key == 0x04) {
        cs->running = 0;
        return 1;
    }
    return 0;
}

/* Raw channel sink — bytes outside any envelope are real keyboard input from the
 * controlling terminal. Forward them to ygui's input decoder verbatim. */
static void yguiapp_client_raw_sink(void *user, const uint8_t *bytes, size_t n)
{
    struct yetty_yguiapp_client *cs = (struct yetty_yguiapp_client *)user;
    struct yetty_ycore_void_result fr =
        yetty_ygui_framework_feed_input(cs->engine, (const char *)bytes, n);
    if (YETTY_IS_ERR(fr)) {
        yetty_ycore_error_destroy(fr.error);
    }
}

/* Bounce a pointer event the ygui app did not consume back to the host
 * terminal, so it applies its default handling (wheel → scrollback, and the
 * inline cards then track the scroll). Mirrors the ccc/yai reinject path, but
 * routed through the ywire RAW channel: yguiapp multiplexes over ywire rather
 * than writing raw stdout, so the reinject DCS envelope goes down RAW (the same
 * channel the ?1500h mouse-enable sequence uses) to reach the terminal's OSC
 * parser. */
static struct yetty_ycore_void_result yguiapp_client_reinject_mouse(
    struct yetty_yguiapp_client *cs, const struct yetty_client_input_mouse *msg)
{
    struct yetty_ywire_channel *raw =
        yetty_ywire_connection_channel(cs->conn, YETTY_YWIRE_CHANNEL_RAW);
    if (!raw) {
        return YETTY_ERR(yetty_ycore_void, "yguiapp reinject: no raw channel");
    }
    struct yetty_ycore_buffer_result buf_res = yetty_ycore_buffer_create(64);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, buf_res, "yguiapp reinject: buffer create");
    struct yetty_ycore_buffer buf = buf_res.value;
    struct yetty_ycore_void_result emit_res =
        yetty_ywire_emit(YETTY_YWIRE_ENVELOPE_DCS, YETTY_OSC_CS_CLIENT_INPUT_REINJECT,
                         /*has_args=*/1, /*compressed=*/0, NULL, 0, msg, sizeof(*msg), &buf);
    if (YETTY_IS_ERR(emit_res)) {
        yetty_ycore_buffer_destroy(&buf);
        return YETTY_ERR(yetty_ycore_void, "yguiapp reinject: emit", emit_res);
    }
    struct yetty_ycore_size_result write_res = yetty_ywire_channel_write(raw, buf.data, buf.size);
    yetty_ycore_buffer_destroy(&buf);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, write_res, "yguiapp reinject: channel write");
    return yetty_ywire_channel_flush(raw);
}

/* Structured keyboard from the host: once a figure is click-focused, yetty
 * stops writing keystrokes to our PTY and instead forwards them as
 * YETTY_OSC_SC_CLIENT_INPUT_FIGURE_KEY envelopes (the keyboard twin of the
 * forwarded-mouse path). Translate each to the byte sequence ygui's input
 * decoder expects — CHAR → the UTF-8 text (Ctrl+letter folded to its control
 * byte, matching the standalone host); DOWN → the navigation/editing CSI;
 * UP → nothing — and feed it in. Without this, keyboard silently dies the
 * moment the user clicks into the field. */
static void yguiapp_client_feed_figure_key(struct yetty_yguiapp_client *cs,
                                           const struct yetty_client_input_key *key_msg)
{
    char buf[8];
    size_t len = 0;
    const char *bytes = NULL;
    switch (key_msg->kind) {
    case YETTY_YMGUI_INPUT_KEY_CHAR: {
        uint32_t codepoint = key_msg->codepoint;
        if ((key_msg->mods & 0x0002 /* CTRL */) &&
            ((codepoint >= 'a' && codepoint <= 'z') || (codepoint >= 'A' && codepoint <= 'Z'))) {
            buf[0] = (char)(codepoint & 0x1F);
            len = 1;
            bytes = buf;
        } else if (codepoint >= 1 && codepoint <= 26) {
            /* An already-folded control byte (Ctrl-A..Ctrl-Z). Widgets act on
             * these directly — Ctrl-A select-all, Ctrl-C/V/X clipboard. */
            buf[0] = (char)codepoint;
            len = 1;
            bytes = buf;
        } else if (codepoint >= 32) {
            len = yguiapp_utf8_encode(codepoint, buf);
            bytes = buf;
        }
        break;
    }
    case YETTY_YMGUI_INPUT_KEY_DOWN:
        bytes = yguiapp_encode_key((uint32_t)key_msg->key, key_msg->mods, buf, sizeof(buf), &len);
        break;
    case YETTY_YMGUI_INPUT_KEY_UP:
    default:
        return;
    }
    if (bytes && len > 0) {
        struct yetty_ycore_void_result fr = yetty_ygui_framework_feed_input(cs->engine, bytes, len);
        if (YETTY_IS_ERR(fr)) {
            yetty_ycore_error_destroy(fr.error);
        }
    }
}

/* Input channel sink — yetty forwards pointer events as client-input OSC codes
 * carrying a yetty_client_input_mouse, and (once a figure is focused) keyboard
 * events carrying a yetty_client_input_key; dispatch to ygui's framework. */
static void yguiapp_client_input_sink(void *user, int wire_code, const uint8_t *args,
                                      size_t args_len, const uint8_t *payload, size_t payload_len)
{
    (void)args;
    (void)args_len;
    struct yetty_yguiapp_client *cs = (struct yetty_yguiapp_client *)user;
    if (wire_code == YETTY_OSC_SC_CLIENT_INPUT_FIGURE_KEY ||
        wire_code == YETTY_OSC_SC_CLIENT_INPUT_KEY) {
        if (payload_len < sizeof(struct yetty_client_input_key)) {
            return;
        }
        const struct yetty_client_input_key *key_msg =
            (const struct yetty_client_input_key *)payload;
        if (key_msg->magic != YETTY_CLIENT_INPUT_KEY_MAGIC) {
            return;
        }
        yguiapp_client_feed_figure_key(cs, key_msg);
        return;
    }
    if (wire_code != YETTY_OSC_SC_CLIENT_INPUT_FIGURE_MOUSE &&
        wire_code != YETTY_OSC_SC_CLIENT_INPUT_MOUSE) {
        return;
    }
    if (payload_len < sizeof(struct yetty_client_input_mouse)) {
        return;
    }
    const struct yetty_client_input_mouse *msg = (const struct yetty_client_input_mouse *)payload;
    if (msg->magic != YETTY_CLIENT_INPUT_MOUSE_MAGIC) {
        return;
    }
    switch (msg->kind) {
    case YETTY_YMGUI_INPUT_MOUSE_BUTTON: {
        struct yetty_ycore_int_result r = yetty_ygui_framework_feed_mouse_button(
            cs->engine, msg->x, msg->y, msg->button, msg->pressed, 0);
        if (YETTY_IS_ERR(r)) {
            yetty_ycore_error_destroy(r.error);
        }
        break;
    }
    case YETTY_YMGUI_INPUT_MOUSE_POS: {
        struct yetty_ycore_int_result r =
            yetty_ygui_framework_feed_mouse_motion(cs->engine, msg->x, msg->y);
        if (YETTY_IS_ERR(r)) {
            yetty_ycore_error_destroy(r.error);
        }
        break;
    }
    case YETTY_YMGUI_INPUT_MOUSE_WHEEL: {
        /* Offer the wheel to ygui first (a scrollarea under the cursor takes
         * it); if nothing consumes it, bounce it back so the terminal scrolls
         * its scrollback and the inline cards track the scroll. */
        struct yetty_ycore_int_result consumed =
            yetty_ygui_framework_feed_mouse_scroll(cs->engine, msg->x, msg->y, 0.0f, msg->wheel_dy);
        if (YETTY_IS_ERR(consumed)) {
            yetty_ycore_error_destroy(consumed.error);
            break;
        }
        if (!consumed.value) {
            struct yetty_ycore_void_result reinject_res = yguiapp_client_reinject_mouse(cs, msg);
            if (YETTY_IS_ERR(reinject_res)) {
                yetty_ycore_error_destroy(reinject_res.error);
            }
        }
        break;
    }
    default:
        break;
    }
}

/* Resize callback — the connection reads TIOCGWINSZ and hands us the geometry;
 * inject it as the framework viewport. */
static void yguiapp_client_resize_cb(void *user, int width_px, int height_px, int cols, int rows)
{
    (void)cols;
    (void)rows;
    struct yetty_yguiapp_client *cs = (struct yetty_yguiapp_client *)user;
    if (width_px > 0 && height_px > 0) {
        struct yetty_ycore_void_result r =
            yetty_ygui_framework_set_viewport(cs->engine, (float)width_px, (float)height_px);
        if (YETTY_IS_ERR(r)) {
            yetty_ycore_error_destroy(r.error);
        }
    }
}

/* Tell the hosting yetty to forward input to our stdin: DEC private modes
 * 1500 (mouse button) / 1501 (mouse move) / 1502 (keyboard). Subscribing for
 * keyboard makes yetty stop interpreting scroll keys (PageUp/PageDown/Up/Down)
 * for its own scrollback and deliver them to us instead, so our widgets (e.g.
 * a scrollarea) scroll on the keyboard the same way they do on the wheel. */
static struct yetty_ycore_void_result yguiapp_client_enable_mouse_forwarding(
    struct yetty_yguiapp_client *cs)
{
    struct yetty_ywire_channel *raw =
        yetty_ywire_connection_channel(cs->conn, YETTY_YWIRE_CHANNEL_RAW);
    if (!raw) {
        return YETTY_ERR(yetty_ycore_void, "yguiapp client: no raw channel");
    }
    static const char enable[] = "\033[?1500h\033[?1501h\033[?1502h";
    struct yetty_ycore_size_result wr = yetty_ywire_channel_write(raw, enable, sizeof(enable) - 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, wr, "yguiapp client: enable input write");
    return yetty_ywire_channel_flush(raw);
}

/* Restore the host terminal's input modes (DEC ?1500/?1501/?1502) on graceful
 * exit — the way a full-screen program leaves the terminal as it found it, so
 * the shell's own wheel/keyboard scrollback works again. Best-effort: the uv
 * loop has already stopped by the time this runs, so pump the bytes out
 * synchronously before the connection is torn down. */
static struct yetty_ycore_void_result yguiapp_client_disable_mouse_forwarding(
    struct yetty_yguiapp_client *cs)
{
    struct yetty_ywire_channel *raw =
        yetty_ywire_connection_channel(cs->conn, YETTY_YWIRE_CHANNEL_RAW);
    if (!raw) {
        return YETTY_OK_VOID();
    }
    static const char disable[] = "\033[?1500l\033[?1501l\033[?1502l";
    struct yetty_ycore_size_result wr =
        yetty_ywire_channel_write(raw, disable, sizeof(disable) - 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, wr, "yguiapp client: disable input write");
    struct yetty_ycore_void_result fr = yetty_ywire_channel_flush(raw);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, fr, "yguiapp client: disable input flush");
    struct yetty_ycore_size_result pr = yetty_ywire_connection_pump_writable(cs->conn);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, pr, "yguiapp client: disable input pump");
    return YETTY_OK_VOID();
}

static void yguiapp_client_out_poll_cb(uv_poll_t *handle, int status, int events);

/* Drain any queued outbound bytes (non-blocking), then keep the loop's
 * writable interest in sync with the queue: armed while bytes remain,
 * disarmed once empty so an idle app doesn't spin on a writable tty. */
static void yguiapp_client_pump_out(struct yetty_yguiapp_client *cs)
{
    struct yetty_ycore_size_result r = yetty_ywire_connection_pump_writable(cs->conn);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
    }
    if (!cs->out_poll_inited) {
        return;
    }
    int want_write = yetty_ywire_connection_want_write(cs->conn);
    if (want_write && !cs->out_poll_armed) {
        if (uv_poll_start(&cs->out_poll, UV_WRITABLE, yguiapp_client_out_poll_cb) == 0) {
            cs->out_poll_armed = 1;
        }
    } else if (!want_write && cs->out_poll_armed) {
        uv_poll_stop(&cs->out_poll);
        cs->out_poll_armed = 0;
    }
}

static void yguiapp_client_out_poll_cb(uv_poll_t *handle, int status, int events)
{
    struct yetty_yguiapp_client *cs = (struct yetty_yguiapp_client *)handle->data;
    if (status < 0 || !(events & UV_WRITABLE)) {
        return;
    }
    yguiapp_client_pump_out(cs);
}

static void yguiapp_client_stdin_cb(uv_poll_t *handle, int status, int events)
{
    struct yetty_yguiapp_client *cs = (struct yetty_yguiapp_client *)handle->data;
    if (status < 0 || !(events & UV_READABLE)) {
        return;
    }
    /* The connection is the single reader: it reads the fd, demuxes, and routes
     * each lane to its channel — rpc bytes buffer for the transport adapter,
     * forwarded mouse fires the input sink, raw keystrokes fire the raw sink. */
    struct yetty_ycore_size_result r = yetty_ywire_connection_pump_readable(cs->conn);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
    }
    yguiapp_client_pump_out(cs);
    if (yetty_ywire_connection_is_eof(cs->conn)) {
        cs->running = 0;
    }
}

static void yguiapp_client_sigwinch_cb(uv_signal_t *handle, int signum)
{
    (void)signum;
    struct yetty_yguiapp_client *cs = (struct yetty_yguiapp_client *)handle->data;
    struct yetty_ycore_void_result r = yetty_ywire_connection_pickup_winsize(cs->conn);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
    }
}

/* The client renders on demand: real input and state changes mark the framework
 * dirty (feed_mouse_*, key dispatch, widget callbacks), and the prepare hook ships
 * a frame only when dirty. The timer just keeps the loop ticking so the prepare
 * hook runs promptly after a change — it deliberately does NOT force a redraw. */
static void yguiapp_client_frame_cb(uv_timer_t *handle)
{
    (void)handle;
}

static void yguiapp_client_prep_cb(uv_prepare_t *handle)
{
    struct yetty_yguiapp_client *cs = (struct yetty_yguiapp_client *)handle->data;
    if (yetty_ygui_framework_is_dirty(cs->engine)) {
        struct yetty_ycore_void_result r = yetty_ygui_framework_emit(cs->engine);
        if (YETTY_IS_ERR(r)) {
            yetty_ycore_error_destroy(r.error);
        }
    }
    yguiapp_client_pump_out(cs);
    if (!cs->running) {
        uv_stop(handle->loop);
    }
}

static void yguiapp_client_close_cb(uv_handle_t *h)
{
    (void)h;
}

/* Load a measurement-only copy of the shared default mono font. The host yetty
 * renders this client's figure text (font_id 0) with that same font, so
 * measuring against it makes the textinput caret and click hit-test land on the
 * exact glyph boundaries the host draws. Rendering happens in the host, so this
 * copy needs no GPU — msdf measure_text reads advances straight from the cdb.
 * Best-effort: returns NULL on any failure and the widgets fall back to the
 * fixed per-char advance. Caller owns the returned font. */
static struct yetty_yfont_font *yguiapp_client_measure_font_create(void)
{
    struct yetty_yplatform_paths_ptr_result paths_res = yetty_yplatform_paths_create();
    if (YETTY_IS_ERR(paths_res)) {
        yetty_ycore_error_destroy(paths_res.error);
        return NULL;
    }
    char cdb_path[768];
    char shader_path[768];
    /* Client side has no GPU generator, but it can still reuse a CDB the server
     * already generated into the cache (or the installed one). Pass NULL
     * generator: on a miss the resolver returns quietly and the measure font is
     * simply unavailable. */
    struct yetty_ycore_void_result cdb_res = yetty_yfont_msdf_resolve_cdb(
        NULL, paths_res.value->fonts_dir_buf, paths_res.value->cache_dir_buf,
        "DejaVuSansMNerdFontMono", "-Regular", cdb_path, sizeof(cdb_path));
    snprintf(shader_path, sizeof(shader_path), "%s/msdf-font.wgsl",
             paths_res.value->shaders_dir_buf);

    struct yetty_yfont_font *font = NULL;
    if (YETTY_IS_OK(cdb_res)) {
        struct yetty_font_font_result font_res =
            yetty_yfont_msdf_font_create(cdb_path, shader_path, "yguiapp_measure");
        if (YETTY_IS_OK(font_res)) {
            font = font_res.value;
            struct yetty_ycore_void_result load = font->ops->load_basic_latin(font);
            if (YETTY_IS_ERR(load)) {
                yetty_ycore_error_destroy(load.error);
            }
        } else {
            yetty_ycore_error_destroy(font_res.error);
        }
    } else {
        yetty_ycore_error_destroy(cdb_res.error);
    }

    struct yetty_ycore_void_result paths_destroy = yetty_yplatform_paths_destroy(paths_res.value);
    if (YETTY_IS_ERR(paths_destroy)) {
        yetty_ycore_error_destroy(paths_destroy.error);
    }
    return font;
}

struct yetty_ycore_void_result yetty_yguiapp_run_terminal(struct yetty_yclass_object *app)
{
    /* Cleanup state up front so every error path can `goto fail` and the single
     * teardown restores whatever was set up — crucially raw mode, which once
     * enabled MUST be restored (by destroying the transport) on every exit. */
    struct yetty_yguiapp_client cs = {0};
    struct yetty_yclass_object *body = NULL;
    struct yetty_ycore_error err = {0};
    int have_err = 0;
    int loop_inited = 0, stdin_inited = 0, sigwinch_inited = 0, prep_inited = 0, timer_inited = 0;
    cs.running = 1;

#define YGUIAPP_TERMINAL_FAIL(msg, res)                                                            \
    do {                                                                                           \
        err = YETTY_ERR(yetty_ycore_void, (msg), (res)).error;                                     \
        have_err = 1;                                                                              \
        goto fail;                                                                                 \
    } while (0)

    /* The connection's transport owns STDIN/STDOUT — or, when the host passed a
     * dedicated side channel (YETTY_YWIRE_SIDE_CHANNEL), that fd pair instead,
     * leaving the terminal byte stream alone. Raw mode runs BEFORE any OSC
     * write — the first thing the host might echo back is the ?1500/?1501 enable,
     * and a cooked tty would loop that echo through libvterm. */
    struct yetty_yclass_transport_pty_ptr_result tr =
        yetty_yclass_transport_pty_create_from_env(STDIN_FILENO, STDOUT_FILENO);
    if (YETTY_IS_ERR(tr)) {
        return YETTY_ERR(yetty_ycore_void, "yguiapp client: transport_pty_create_from_env", tr);
    }
    cs.transport = tr.value;

    {
        struct yetty_ycore_void_result rr =
            yetty_yclass_transport_pty_enable_raw_mode(cs.transport);
        if (YETTY_IS_ERR(rr)) {
            YGUIAPP_TERMINAL_FAIL("yguiapp client: enable_raw_mode", rr);
        }
    }

    struct yetty_ywire_connection_ptr_result cr = yetty_ywire_connection_create(
        yetty_yclass_transport_pty_reactor(cs.transport), /*compressed=*/1);
    if (YETTY_IS_ERR(cr)) {
        YGUIAPP_TERMINAL_FAIL("yguiapp client: connection_create", cr);
    }
    cs.conn = cr.value;

    if (uv_loop_init(&cs.loop) != 0) {
        err = YETTY_ERR(yetty_ycore_void, "yguiapp client: uv_loop_init failed").error;
        have_err = 1;
        goto fail;
    }
    loop_inited = 1;

    struct yetty_yclass_object_ptr_result fr = yetty_ygui_framework_create(NULL);
    if (YETTY_IS_ERR(fr)) {
        YGUIAPP_TERMINAL_FAIL("yguiapp client: framework_create", fr);
    }
    cs.engine = fr.value;
    yetty_ygui_framework_set_key_cb(cs.engine, yguiapp_client_on_key, &cs);

    /* Wire the measurement font so widget carets / hit-tests match the host's
     * rendered glyph advances (see yguiapp_client_measure_font_create). */
    cs.measure_font = yguiapp_client_measure_font_create();
    if (cs.measure_font) {
        yetty_ygui_framework_set_font(cs.engine, cs.measure_font);
    }

    /* Bind the framework's figure output to its OWN dynamic RPC channel on the
     * connection (the SSH model): the framework opens the channel, the host
     * serves it via its accept callback. The attach handshake reads stdin
     * synchronously here and MUST complete before the uv loop below takes the
     * fd. framework_destroy tears the session down. */
    {
        struct yetty_ycore_void_result attach_res =
            yetty_ygui_framework_attach_connection(cs.engine, cs.conn);
        if (YETTY_IS_ERR(attach_res)) {
            YGUIAPP_TERMINAL_FAIL("yguiapp client: framework_attach_connection", attach_res);
        }
    }

    /* Same styled two-level root the standalone path uses (no chrome inset). */
    {
        struct yetty_yclass_object *root = NULL;
        struct yetty_ycore_void_result rbr =
            yetty_yguiapp_build_root_body(cs.engine, 0.0f, &root, &body);
        if (YETTY_IS_ERR(rbr)) {
            YGUIAPP_TERMINAL_FAIL("yguiapp client: build_root_body", rbr);
        }
    }

    /* Hand the styled body to the app subclass to populate (virtual dispatch). */
    {
        struct yetty_ycore_void_result rb = yetty_yguiapp_build(app, body);
        if (YETTY_IS_ERR(rb)) {
            YGUIAPP_TERMINAL_FAIL("yguiapp client: build", rb);
        }
    }

    /* Route inbound lanes: raw keystrokes → input decoder; forwarded mouse →
     * feed_mouse_*; resize (TIOCGWINSZ) → framework viewport. */
    {
        struct yetty_ywire_channel *raw =
            yetty_ywire_connection_channel(cs.conn, YETTY_YWIRE_CHANNEL_RAW);
        struct yetty_ywire_channel *input =
            yetty_ywire_connection_channel(cs.conn, YETTY_YWIRE_CHANNEL_INPUT);
        struct yetty_ycore_void_result rs =
            yetty_ywire_channel_set_raw_sink(raw, yguiapp_client_raw_sink, &cs);
        if (YETTY_IS_ERR(rs)) {
            yetty_ycore_error_destroy(rs.error);
        }
        struct yetty_ycore_void_result is =
            yetty_ywire_channel_set_envelope_sink(input, yguiapp_client_input_sink, &cs);
        if (YETTY_IS_ERR(is)) {
            yetty_ycore_error_destroy(is.error);
        }
    }
    {
        struct yetty_ycore_void_result resize_res =
            yetty_ywire_connection_set_resize_cb(cs.conn, yguiapp_client_resize_cb, &cs);
        if (YETTY_IS_ERR(resize_res)) {
            yetty_ycore_error_destroy(resize_res.error);
        }
    }
    {
        struct yetty_ycore_void_result sr = yguiapp_client_enable_mouse_forwarding(&cs);
        if (YETTY_IS_ERR(sr)) {
            yetty_ycore_error_destroy(sr.error);
        }
    }
    {
        struct yetty_ycore_void_result wr = yetty_ywire_connection_pickup_winsize(cs.conn);
        if (YETTY_IS_ERR(wr)) {
            yetty_ycore_error_destroy(wr.error);
        }
    }

    /* stdin polling is the input path — fatal on init OR start failure. The
     * *_inited flag is set right after a successful init so cleanup closes the
     * handle even if its start fails. */
    int uvrc = uv_poll_init(&cs.loop, &cs.stdin_poll, yetty_ywire_connection_fd(cs.conn));
    if (uvrc != 0) {
        err = YETTY_ERR(yetty_ycore_void, "yguiapp client: uv_poll_init").error;
        have_err = 1;
        goto fail;
    }
    stdin_inited = 1;
    cs.stdin_poll.data = &cs;
    uvrc = uv_poll_start(&cs.stdin_poll, UV_READABLE, yguiapp_client_stdin_cb);
    if (uvrc != 0) {
        err = YETTY_ERR(yetty_ycore_void, "yguiapp client: uv_poll_start").error;
        have_err = 1;
        goto fail;
    }

    /* Writable-interest poll (see the struct comment) — armed on demand by
     * yguiapp_client_pump_out. Non-fatal if the fd can't be polled: output
     * then degrades to the frame-timer drain cadence. */
    uvrc = uv_poll_init(&cs.loop, &cs.out_poll, yetty_ywire_connection_out_fd(cs.conn));
    if (uvrc == 0) {
        cs.out_poll.data = &cs;
        cs.out_poll_inited = 1;
    } else {
        ywarn("yguiapp client: out-fd poll init failed — writes drain on the frame tick");
    }

    /* SIGWINCH is optional — a resize just won't be picked up live. */
    uvrc = uv_signal_init(&cs.loop, &cs.sigwinch);
    if (uvrc == 0) {
        sigwinch_inited = 1;
        cs.sigwinch.data = &cs;
        uvrc = uv_signal_start(&cs.sigwinch, yguiapp_client_sigwinch_cb, SIGWINCH);
        if (uvrc != 0) {
            ywarn("yguiapp client: uv_signal_start: %s", uv_strerror(uvrc));
        }
    } else {
        ywarn("yguiapp client: uv_signal_init: %s", uv_strerror(uvrc));
    }

    /* The prepare hook ships dirty frames each tick — fatal without it. */
    uvrc = uv_prepare_init(&cs.loop, &cs.prep);
    if (uvrc != 0) {
        err = YETTY_ERR(yetty_ycore_void, "yguiapp client: uv_prepare_init").error;
        have_err = 1;
        goto fail;
    }
    prep_inited = 1;
    cs.prep.data = &cs;
    uvrc = uv_prepare_start(&cs.prep, yguiapp_client_prep_cb);
    if (uvrc != 0) {
        err = YETTY_ERR(yetty_ycore_void, "yguiapp client: uv_prepare_start").error;
        have_err = 1;
        goto fail;
    }

    /* The frame timer keeps the loop ticking so prep drains output — fatal. */
    uvrc = uv_timer_init(&cs.loop, &cs.frame_timer);
    if (uvrc != 0) {
        err = YETTY_ERR(yetty_ycore_void, "yguiapp client: uv_timer_init").error;
        have_err = 1;
        goto fail;
    }
    timer_inited = 1;
    cs.frame_timer.data = &cs;
    uvrc = uv_timer_start(&cs.frame_timer, yguiapp_client_frame_cb, 33, 33);
    if (uvrc != 0) {
        err = YETTY_ERR(yetty_ycore_void, "yguiapp client: uv_timer_start").error;
        have_err = 1;
        goto fail;
    }

    uv_run(&cs.loop, UV_RUN_DEFAULT);

    /* Normal loop exit (the app asked to quit): restore the host terminal's
     * input modes so scrollback keys/wheel belong to the shell again. Only on
     * this path — the early goto-fail cases never subscribed. */
    {
        struct yetty_ycore_void_result dr = yguiapp_client_disable_mouse_forwarding(&cs);
        if (YETTY_IS_ERR(dr)) {
            yetty_ycore_error_destroy(dr.error);
        }
    }

fail:
    /* Stop + close only the uv handles that were initialized, then drain their
     * close callbacks. */
    if (loop_inited) {
        if (stdin_inited) {
            uv_poll_stop(&cs.stdin_poll);
            uv_close((uv_handle_t *)&cs.stdin_poll, yguiapp_client_close_cb);
        }
        if (cs.out_poll_inited) {
            uv_poll_stop(&cs.out_poll);
            uv_close((uv_handle_t *)&cs.out_poll, yguiapp_client_close_cb);
        }
        if (sigwinch_inited) {
            uv_signal_stop(&cs.sigwinch);
            uv_close((uv_handle_t *)&cs.sigwinch, yguiapp_client_close_cb);
        }
        if (prep_inited) {
            uv_prepare_stop(&cs.prep);
            uv_close((uv_handle_t *)&cs.prep, yguiapp_client_close_cb);
        }
        if (timer_inited) {
            uv_timer_stop(&cs.frame_timer);
            uv_close((uv_handle_t *)&cs.frame_timer, yguiapp_client_close_cb);
        }
        /* uv_close() completion is async; run until all close callbacks fired and
         * no active handles remain before uv_loop_close(). */
        uv_run(&cs.loop, UV_RUN_DEFAULT);
    }

    /* Tear down in order: framework first (detaches the producer session, which
     * destroys the rpc channel transport adapter), then the connection (state
     * machine + channels), then the transport (restores raw mode). */
    if (cs.engine) {
        struct yetty_ycore_void_result dr = yetty_ygui_framework_destroy(cs.engine);
        if (YETTY_IS_ERR(dr)) {
            yetty_ycore_error_destroy(dr.error);
        }
    }
    /* Framework is gone (it only borrowed the font); now release it. */
    if (cs.measure_font) {
        cs.measure_font->ops->destroy(cs.measure_font);
        cs.measure_font = NULL;
    }
    if (cs.transport) {
        /* The framework teardown queued its figure clears through the
         * non-blocking writer — force the tail onto the wire before the
         * transport goes away. */
        struct yetty_ycore_void_result fl = yetty_yclass_transport_pty_flush_blocking(cs.transport);
        if (YETTY_IS_ERR(fl)) {
            yetty_ycore_error_destroy(fl.error);
        }
    }
    if (cs.conn) {
        struct yetty_ycore_void_result cd = yetty_ywire_connection_destroy(cs.conn);
        if (YETTY_IS_ERR(cd)) {
            yetty_ycore_error_destroy(cd.error);
        }
    }
    if (cs.transport) {
        struct yetty_ycore_void_result td = yetty_yclass_transport_pty_destroy(cs.transport);
        if (YETTY_IS_ERR(td)) {
            yetty_ycore_error_destroy(td.error);
        }
    }
    if (loop_inited) {
        int close_rc = uv_loop_close(&cs.loop);
        if (close_rc != 0) {
            ywarn("yguiapp client: uv_loop_close: %s", uv_strerror(close_rc));
        }
    }

#undef YGUIAPP_TERMINAL_FAIL

    if (have_err) {
        return (struct yetty_ycore_void_result){.ok = 0, .error = err};
    }
    return YETTY_OK_VOID();
}

#else /* !YETTY_YGUI_HAS_UV */

struct yetty_ycore_void_result yetty_yguiapp_run_terminal(struct yetty_yclass_object *app)
{
    (void)app;
    return YETTY_ERR(yetty_ycore_void, "yguiapp: terminal host requires libuv (YETTY_YGUI_HAS_UV)");
}

#endif /* YETTY_YGUI_HAS_UV */

/*===========================================================================
 * Standalone host — drive the platform bring-up sequence directly.
 *=========================================================================*/
static int yguiapp_run_standalone(int argc, char **argv, const struct yetty_yclass *app_class)
{
    struct yetty_ycore_void_result platform_reg = yetty_yplatform_register();
    if (YETTY_IS_ERR(platform_reg)) {
        yetty_ycore_error_print(stderr, "yguiapp: platform register", platform_reg.error);
        yetty_ycore_error_destroy(platform_reg.error);
        return 1;
    }
    struct yetty_ycore_void_result yapp_reg = yetty_yapp_register();
    if (YETTY_IS_ERR(yapp_reg)) {
        yetty_ycore_error_print(stderr, "yguiapp: yapp register", yapp_reg.error);
        yetty_ycore_error_destroy(yapp_reg.error);
        return 1;
    }

    struct yetty_yclass_object_ptr_result app_res = yetty_yclass_object_alloc(app_class);
    if (YETTY_IS_ERR(app_res)) {
        yetty_ycore_error_print(stderr, "yguiapp: app alloc", app_res.error);
        yetty_ycore_error_destroy(app_res.error);
        return 1;
    }

    struct yetty_yclass_object_ptr_result platform_res = yetty_yplatform_glfw_platform_create(NULL);
    if (YETTY_IS_ERR(platform_res)) {
        yetty_ycore_error_print(stderr, "yguiapp: platform create", platform_res.error);
        yetty_ycore_error_destroy(platform_res.error);
        return 1;
    }

    /* Single step — run() calls init(app, argc, argv) internally. */
    struct yetty_ycore_void_result run_result =
        yetty_yplatform_platform_run(platform_res.value, app_res.value, argc, argv);
    if (YETTY_IS_ERR(run_result)) {
        yetty_ycore_error_print(stderr, "yguiapp: run", run_result.error);
        yetty_ycore_error_destroy(run_result.error);
        return 1;
    }
    return 0;
}

int yetty_yguiapp_run_main(int argc, char **argv, const struct yetty_yclass *app_class)
{
    ytrace_init();
    if (!app_class) {
        fprintf(stderr, "yguiapp: run_main: NULL app class\n");
        return 1;
    }

#ifdef YETTY_YGUI_HAS_UV
    if (yetty_running_under_yetty()) {
        /* Terminal mode runs inside a host yetty pane — no borderless OS window
         * to drag, so chrome is irrelevant here. */
        struct yetty_yclass_object_ptr_result app_res = yetty_yclass_object_alloc(app_class);
        if (YETTY_IS_ERR(app_res)) {
            yetty_ycore_error_print(stderr, "yguiapp: app alloc (terminal)", app_res.error);
            yetty_ycore_error_destroy(app_res.error);
            return 1;
        }
        struct yetty_ycore_void_result rt = yetty_yguiapp_run_terminal(app_res.value);
        if (YETTY_IS_ERR(rt)) {
            yetty_ycore_error_print(stderr, "yguiapp: run_terminal", rt.error);
            yetty_ycore_error_destroy(rt.error);
            return 1;
        }
        return 0;
    }
#endif

    return yguiapp_run_standalone(argc, argv, app_class);
}
