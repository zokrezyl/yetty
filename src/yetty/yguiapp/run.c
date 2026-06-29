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

#include <yetty/ycore/result.h>
#include <yetty/ycore/terminal-detect.h>
#include <yetty/ycore/types.h>
#include <yetty/yclass/class.h>
#include <yetty/ygui/framework.h>
#include <yetty/ygui/ygui.h>
#include <yetty/ygui/widget.h>
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
    uv_signal_t sigwinch;
    uv_prepare_t prep;
    uv_timer_t frame_timer;                       /* keeps the loop ticking so prep drains output */
    struct yetty_yclass_transport_pty *transport; /* sole STDIN/STDOUT owner */
    struct yetty_ywire_connection *conn;          /* multiplexed link */
    struct yetty_yclass_object *engine;           /* ygui framework */
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

/* Input channel sink — yetty forwards pointer events as client-input OSC codes
 * carrying a yetty_client_input_mouse; dispatch to ygui's framework_feed_mouse_*. */
static void yguiapp_client_input_sink(void *user, int wire_code, const uint8_t *args,
                                      size_t args_len, const uint8_t *payload, size_t payload_len)
{
    (void)args;
    (void)args_len;
    struct yetty_yguiapp_client *cs = (struct yetty_yguiapp_client *)user;
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

/* Tell the hosting yetty to forward mouse events to our stdin (DEC private modes
 * 1500 button / 1501 move). */
static struct yetty_ycore_void_result yguiapp_client_enable_mouse_forwarding(
    struct yetty_yguiapp_client *cs)
{
    struct yetty_ywire_channel *raw =
        yetty_ywire_connection_channel(cs->conn, YETTY_YWIRE_CHANNEL_RAW);
    if (!raw) {
        return YETTY_ERR(yetty_ycore_void, "yguiapp client: no raw channel");
    }
    static const char enable[] = "\033[?1500h\033[?1501h";
    struct yetty_ycore_size_result wr = yetty_ywire_channel_write(raw, enable, sizeof(enable) - 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, wr, "yguiapp client: enable mouse write");
    return yetty_ywire_channel_flush(raw);
}

/* Drain any queued outbound bytes (non-blocking). */
static void yguiapp_client_pump_out(struct yetty_yguiapp_client *cs)
{
    struct yetty_ycore_size_result r = yetty_ywire_connection_pump_writable(cs->conn);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
    }
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

    /* The connection's transport owns STDIN/STDOUT. Raw mode runs BEFORE any OSC
     * write — the first thing the host might echo back is the ?1500/?1501 enable,
     * and a cooked tty would loop that echo through libvterm. */
    struct yetty_yclass_transport_pty_ptr_result tr =
        yetty_yclass_transport_pty_create(STDIN_FILENO, STDOUT_FILENO);
    if (YETTY_IS_ERR(tr)) {
        return YETTY_ERR(yetty_ycore_void, "yguiapp client: transport_pty_create", tr);
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

    /* Bind the framework's figure output to the connection's rpc channel: the RPC
     * requests ride a yetty_ywire_channel transport adapter. The get_root
     * handshake reads stdin synchronously here and MUST complete before the uv
     * loop below takes the fd. framework_destroy tears the session down. */
    {
        struct yetty_ywire_channel *rpc_channel =
            yetty_ywire_connection_channel(cs.conn, YETTY_YWIRE_CHANNEL_RPC);
        struct yetty_yclass_transport_ptr_result rpc_transport =
            yetty_ywire_channel_transport(rpc_channel);
        if (YETTY_IS_ERR(rpc_transport)) {
            YGUIAPP_TERMINAL_FAIL("yguiapp client: rpc channel transport", rpc_transport);
        }
        struct yetty_ycore_void_result attach_res =
            yetty_ygui_framework_attach_transport(cs.engine, rpc_transport.value);
        if (YETTY_IS_ERR(attach_res)) {
            YGUIAPP_TERMINAL_FAIL("yguiapp client: framework_attach_transport", attach_res);
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

fail:
    /* Stop + close only the uv handles that were initialized, then drain their
     * close callbacks. */
    if (loop_inited) {
        if (stdin_inited) {
            uv_poll_stop(&cs.stdin_poll);
            uv_close((uv_handle_t *)&cs.stdin_poll, yguiapp_client_close_cb);
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
