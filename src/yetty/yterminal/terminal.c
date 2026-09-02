#include <yetty/yframework/yframework.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yetty/yplatform/platform-input-pipe.h>
#include <yetty/yplatform/pty-io.h>
#include <yetty/yplatform/pty-pipe-source.h>
#include <yetty/yplatform/pty.h>
#include <yetty/yplatform/yclipboard/clipboard.h>
#include <yetty/yplatform/time.h>
#include <yetty/yconfig/config.h>
#include <yetty/yfont/font.h>
#include <yetty/yfont/msdf-font.h>
#include <yetty/yevent/event-loop.h>
#include <yetty/yevent/event.h>
#include <yetty/yface/yface.h>
/* <yetty/ymgui/wire.h> is still pulled in below for YMGUI_WIRE_VERSION
 * and the YETTY_YMGUI_INPUT_* enum constants used when emitting input
 * envelopes back to the focused figure. The ymgui *figure* header is
 * no longer needed — the factory is registered via yframework and the
 * hit-test lives in yfigure now. */
#include <yetty/ymgui/wire.h>
#include <yetty/yterminal/client-input.h>
#include "yetty/gen/impl/yrdawn/figure.h"
#include <yetty/yrdawn/wire.h>
#include <yetty/yrender/gpu-allocator.h>
#include <yetty/yrender/gpu-resource-set.h>
#include <yetty/yrender/render-target.h>
#include <yetty/yterminal/dcs-codes.h>
#include <yetty/ydraw-list/cmds.h>
#include <yetty/yclass/rpc-dcs-server.h>
#include <yetty/ywire/connection.h>
#include <yetty/ywire/wire-statemachine.h>
#include <yetty/yplatform/ycoroutine.h>
#include <yetty/ydraw-factory/complex-factory.h>
#include <yetty/ydraw-list/cmds.h>
#include <yetty/ydraw-list/complex.h>
#include <yetty/ydraw-list/drawable-iterator.h>
#include <yetty/ydraw-list/drawable-list-registry.h>
#include <yetty/ydraw-list/font-resource.h>
#include <yetty/api/yscene/scene.h>
#include <yetty/yplot/yplot-gen.h>
#include <yetty/yimage/yimage-gen.h>
#include <yetty/yshadertoy/prim.h>
#if defined(YETTY_HAS_YMESH) && YETTY_HAS_YMESH
#include <yetty/ymesh/ymesh-gen.h>
#endif
#if defined(YETTY_HAS_YVIDEO) && YETTY_HAS_YVIDEO
#include <yetty/yvideo/yvideo-gen.h>
#endif
#if defined(YETTY_HAS_YSIXEL) && YETTY_HAS_YSIXEL
#include <yetty/ydraw-list/drawable-list.h>
#include <yetty/ysixel/sixel.h>
#endif
#include <yetty/yterminal/terminal.h>
#include <yetty/ysdf/default-registry.h>
#include <yetty/ysdf/types.gen.h>
#include <yetty/api/yvterm/grid.h>
#include <yetty/yvterm/group-key.h>
#include "yetty/gen/impl/ytermsink/sink.h"
#include "yetty/gen/impl/yvterm/vterm.h"
#include "yetty/gen/impl/yfigure/figure.h"
#include "yetty/gen/impl/yfigure/container.h"
#include <yetty/yfigure/registry.h>
#include <yetty/yclass/class.h>
/* The terminal is itself the yclass class `yterminal:terminal` — the object
 * a connecting tool receives as its session root. The generated impl glue
 * (accessor, from/to, factory, the figure_root_container skel, register) is
 * #included at the foot of this TU; these forward decls let the create /
 * destroy / navigation code above the include name it. terminal.c does not
 * include its own generated api header (a downstream artifact). */
struct yetty_yterminal_terminal_ptr_result {
    int ok;
    union {
        struct yetty_yterminal_terminal *value;
        struct yetty_ycore_error error;
    };
};
struct yetty_yclass_ptr_result yetty_yterminal_terminal_class_get(void);
struct yetty_yterminal_terminal_ptr_result yetty_yterminal_terminal_from(
    struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yterminal_terminal_to(
    struct yetty_yterminal_terminal *data);
struct yetty_yclass_object_ptr_result yetty_yterminal_terminal_create(struct yetty_yclass_ctx *ctx);
struct yetty_ycore_void_result yetty_yterminal_register(void);
#include "yetty/gen/impl/yscene/scene.h"
#include <yetty/ytrace/ytrace.h>
#include <yetty/yui-core/view.h>

#include "terminal-mime.h"

/* GLFW modifier bit layout — kept in sync with src/yetty/yetty/yetty.c.
 * Used here for clipboard shortcuts (Ctrl+Shift+C/V). */
#define YETTY_MOD_SHIFT 0x0001
#define YETTY_MOD_CONTROL 0x0002

/* Forward declarations for view ops */
static struct yetty_ycore_void_result terminal_view_destroy(struct yetty_yui_view *view);
static struct yetty_ycore_void_result terminal_view_render(struct yetty_yui_view *view,
                                                           struct yetty_ydraw_target *render_target,
                                                           int force_redraw);
static struct yetty_ycore_void_result terminal_view_set_bounds(struct yetty_yui_view *view,
                                                               struct yetty_yui_rect bounds);
static struct yetty_ycore_int_result terminal_view_on_event(struct yetty_yui_view *view,
                                                            const struct yetty_yui_event *event);
static const char *terminal_view_title(const struct yetty_yui_view *view);
static struct yetty_ycore_void_result terminal_apply_pane_geometry(
    struct yetty_yterminal_terminal *terminal, float pane_w, float pane_h);
/* Selection helpers (defined below) — reached early by the reinject path,
 * which drives cell selection from a subscriber's bounced mouse events. */
static struct yetty_ycore_void_result terminal_cell_from_local(
    const struct yetty_yterminal_terminal *terminal, float lx, float ly, uint32_t *out_row,
    uint32_t *out_col);
static struct yetty_ycore_void_result terminal_push_selection(
    struct yetty_yterminal_terminal *terminal);
static struct yetty_ycore_void_result terminal_copy_selection(
    struct yetty_yterminal_terminal *terminal);
static struct yetty_ycore_void_result terminal_clear_selection(
    struct yetty_yterminal_terminal *terminal);
static struct yetty_ycore_void_result terminal_paste_clipboard(
    struct yetty_yterminal_terminal *terminal);

static const struct yetty_yui_view_ops terminal_view_ops = {
    .destroy = terminal_view_destroy,
    .render = terminal_view_render,
    .set_bounds = terminal_view_set_bounds,
    .on_event = terminal_view_on_event,
    .title = terminal_view_title,
};

/* Terminal context - contains yetty context plus terminal-owned objects */
struct yetty_yterminal_terminal_context {
    struct yetty_context yetty_context;
    struct yetty_platform_pty *pty;
};

struct YETTY_ANNOTATE("class@yterminal:terminal") YETTY_ANNOTATE("parent@ytermsink:sink")
    yetty_yterminal_terminal {
    struct yetty_yui_view view; /* MUST be first - allows cast to view */
    struct yetty_yevent_event_listener listener;
    struct yetty_yterminal_terminal_context context;
    uint32_t cols;
    uint32_t rows;
    /* Pixel size this terminal was last actually resized to. The generic
     * view wrapper (yetty_yui_view_set_bounds) writes view->bounds before
     * dispatching to our set_bounds, so view->bounds can't be used to
     * detect a real change — track the applied size here instead. */
    float applied_w;
    float applied_h;
    /* Content scale the current cell metrics were derived at. The runtime
     * scale can change after creation (a yvnc viewer pushes its display
     * density with a resize); terminal_apply_pane_geometry compares this
     * against the runtime's live value and rescales the cell stride by
     * the ratio, so text density follows the viewer's display without
     * re-baking the font (MSDF glyphs stay crisp at any cell size). */
    float layout_content_scale;
    /* Structural (Ctrl+Shift+wheel) zoom state. cell_zoom accumulates the
     * wheel factor over zoom_base_cell — the cell stride captured when the
     * first zoom event arrives (rescaled on display-density changes).
     * Each tick derives the snapped cell from base × cell_zoom; deriving
     * from the previous SNAPPED cell instead would feed the rounding back
     * into the next tick: a 9 px cell width times 1.05 rounds back to
     * 9 forever while the 19 px height moves — zoom then grows only
     * vertically. cell_zoom <= 0 means "not seeded yet"; a reset event
     * returns to exactly 1.0 (the pre-zoom cell). */
    struct yetty_ycore_pixel_size zoom_base_cell;
    float cell_zoom;
    /* Set by workspace_set_active via SET_FOCUS — true means this terminal
     * is the foreground view in its workspace AND the workspace is the
     * tabbar's active one. Layers can read this to switch cursor style
     * (block vs hollow), and we'll forward FocusIn/FocusOut CSI to the
     * PTY once focus reporting (DECSET 1004) is wired through. */
    int focused;
    /* Terminal title reported by the running program (OSC 0/2), delivered
     * through the ytermsink set_title override and surfaced via the view
     * title op so yui can label the tab. Empty = none set yet. */
    char title[256];
    yetty_yevent_pipe_id pty_pipe_id;
    int shutting_down;
    struct yetty_ywire_wire_statemachine *sm;

    /* yclass-RPC DCS server state attached to `sm`. The SM borrows it
     * (handler userdata); we own it and free it after the SM is gone. */
    struct yetty_yclass_rpc_dcs_server *dcs_rpc_server;

    /* Host-side connection layer attached to `sm` (acceptor of dynamic ywire
     * channels opened by in-pane clients). Same borrow contract as the RPC
     * server: destroyed only after the SM is gone. */
    struct yetty_ywire_connection *channel_host;

    /* Distinct userdata for the content-inset wire handler. The SM dedups
     * handler coroutines by userdata pointer (one coro per pointer, running
     * the first-registered fn), and the reinject handler already owns the
     * bare-terminal pointer. Registering the inset handler with the address
     * of this self-back-pointer hands it its own coroutine running its own
     * fn; the handler recovers the terminal by dereferencing it. */
    struct yetty_yterminal_terminal *inset_handler_self;

    /* Same trick for the content-rect wire handler. */
    struct yetty_yterminal_terminal *content_rect_handler_self;

    /* Same trick for the exit-window input-hold wire handler. */
    struct yetty_yterminal_terminal *input_hold_handler_self;

    /* Host-side timer that ACTIVELY enforces the input barrier's deadline: armed
     * alongside the barrier (INPUT_HOLD), it fires if the client wedges without
     * further pane output or user input and force-releases the held bytes — so
     * expiry does not depend on a later keystroke happening to observe it.
     * Created lazily on first arm; destroyed at teardown. */
    yetty_yevent_timer_id input_barrier_timer;
    struct yetty_yevent_event_listener input_barrier_listener;
    int input_barrier_timer_ready;

    /* Same trick for the pane-wide subscription wire handler. */
    struct yetty_yterminal_terminal *client_sub_handler_self;

    /* Content reservation requested by the client (YETTY_OSC_CS_CONTENT_RECT,
     * or the legacy _INSET converted to the same form), in pane-local pixels
     * with edge-anchored extents: spec w/h > 0 are absolute; <= 0 anchor to
     * the pane's right/bottom edge with |value| px margin. All zero (default)
     * = full pane. terminal_apply_pane_geometry resolves this against the
     * live pane size into the effective content rect. */
    float content_spec_x;
    float content_spec_y;
    float content_spec_w;
    float content_spec_h;

    /* Same self-back-pointer trick for the YDRAW handler coroutine. */
    struct yetty_yterminal_terminal *ydraw_handler_self;

    /* Same trick for the raw-file (YETTY_DCS_MIME_FILE) handler coroutine. */
    struct yetty_yterminal_terminal *mime_handler_self;

    /* Same trick for the effect-OSC handler coroutines. One distinct
     * self-pointer per effect class (pre/post/coord) so each OSC code gets its
     * own coroutine rather than sharing. */
    struct yetty_yterminal_terminal *effect_handler_self[3];

    /* Reusable read buffer handed back from terminal_pty_pipe_alloc.
     * Lazily allocated on the first read; freed in destroy. */
    char *pty_read_buf;

    /* Pixel-precise mouse forwarding (DEC ?1500/?1501) plus keyboard
   * forwarding (DEC ?1502).
   * The text-layer's libvterm settermprop hook flips these and reports
   * via terminal_mouse_sub_callback, which also emits
   * YETTY_OSC_SC_CLIENT_INPUT_FIGURE_RESIZE with the current pixel size on
   * the rising edge so the client can lay out. When key_subscribed is set the
   * hosted app owns the keyboard: the terminal stops interpreting scroll keys
   * (PageUp/PageDown/Up/Down) for its own scrollback and hands them to the
   * app like any full-screen program. */
    int mouse_click_subscribed;
    int mouse_move_subscribed;
    int key_subscribed;
    /* CLIENT_INPUT_SUB_KEY_FANOUT (review: ygreeter key theft): the app
     * OPTED INTO the structured figure-key fan-out via the pane-wide
     * subscription envelope. Without it a click-focused figure must NOT
     * consume keystrokes — apps that read raw keys from their PTY
     * (ygreeter) went deaf after any click. */
    int figure_key_optin;
    /* Geometry-only subscription: the client wants pane size + content_scale
     * on the resize envelope, with no input forwarding. Latched from the
     * pane-wide subscription envelope. */
    int resize_subscribed;
    /* Pane-wide mouse forwarding (CLIENT_INPUT_SUB MOUSE_* bits): events not
     * owned by a yscene figure are emitted as SC_CLIENT_INPUT_MOUSE with
     * figure_id 0 and PANE-LOCAL pixels — the input path for a pure
     * drawable-tree (group) surface, which has no figures for the DEC-mode
     * hit-test to find. A subscribed wheel is claimed by the client instead
     * of driving scrollback (same philosophy as ?1502 claiming scroll keys). */
    int pane_mouse_click_subscribed;
    int pane_mouse_move_subscribed;
    int pane_mouse_wheel_subscribed;
    int mouse_buttons_held; /* OR of (1 << button) for currently-down buttons */
    /* Capture OWNER of the in-flight drag, recorded at the press that
     * started it: held-button motion and releases route to this owner —
     * even outside the pane — until every button is up. Without it, a
     * figure-owned drag leaving the pane would degrade to pane events (the
     * figure never sees its release), and a pane-background drag crossing a
     * figure would switch owners mid-drag. */
    uint32_t mouse_capture_figure_id; /* nonzero: a figure owns the drag */
    int mouse_capture_pane;           /* the pane subscriber owns the drag */

    /* Long-lived yface for emitting input events to the inferior over the
   * PTY. Reused across every emit; out_buf is cleared after each write. */
    struct yetty_yface *emit_yface;

    /* tmux-style scrollback view. Mouse wheel enters scrollback and shifts
   * the absolute viewport top (grid-owned view state). Enter exits back to
   * live. While active, both layers freeze their viewport at this index
   * even as new content keeps arriving. */

    /* Currently focused ymgui figure (click-focus model). Tracked
     * directly on the terminal now that hit-testing walks the root
     * container instead of going through a dedicated ymgui layer.
     * 0 = no figure focused. */
    uint32_t focused_figure_id;

    /* Root container — positioned-figure root of the rendering stack.
     * Holds every figure the producer addresses by id; producers mutate
     * it through the yclass-RPC DCS server (typed create_child /
     * set_child_rect / apply_child_body / … slots). Owned directly. */
    struct yetty_yclass_object *root_container_obj;
    struct yetty_yfigure_registry *figure_registry;

    /* The content (libvterm text grid + ydraw canvas) as a yvterm:grid figure,
     * seated as the lowest-z child of root_container and rendered through the
     * figure path. It directly owns its two sub-renderers; the container owns
     * the figure (destroys it on teardown). The terminal borrows this handle to
     * drive scroll / selection / resize / input via the yetty_yvterm_vterm_* API.
     * Everything else on screen — ymgui, yrdawn, yplot, the shader-glyph — is a
     * figure in the root container too. NULL until terminal_create wires it. */
    struct yetty_yclass_object *grid;

    /* Default MSDF font attached to every scene figure the root
     * container mints (registered as user-data on the figure factories).
     * Borrowed by each figure; terminal owns lifetime. Teardown destroys
     * the root container first (cascades into per-figure scenes that
     * hold borrowed refs to this font) then the font. */
    struct yetty_yfont_font *compositor_font;

    /* Complex-prim factory (yplot / yimage / yvideo / yzoo / yjungle …).
     * One instance per terminal — every scene the root container mints
     * borrows the same pointer via the factory args (below) so they all
     * share the same per-type pipeline cache. */
    struct yetty_ydraw_complex_factory *complex_factory;

    /* Drawable-list registry for parsing inbound YDRAW_BIN record streams into
     * the content grid's anchored rich model. Borrowed from the framework's
     * shared instance (runtime->drawable_registry) — never owned. */
    struct yetty_ydraw_drawable_list_registry *ydraw_registry;

    /* Figures whose receiver-local chrome replacement failed transiently
     * (reserve OOM, paint-z scope momentarily full): retried at every
     * ingest-envelope close, so a figure that goes quiet after ONE
     * geometry/range op still converges instead of showing stale chrome
     * until its next update. Tiny and deduplicated; on overflow the
     * figure falls back to retry-on-its-next-update (the chrome_dirty
     * flag it keeps). */
    struct {
        uint64_t update_key;
        uint64_t parent_scope;
    } rechrome_retry[8];
    uint32_t rechrome_retry_count;

    /* CMD_UPDATE routing for scrollback figures: each envelope's complexes
     * register here under their ordinal (1, 2, ...) so a producer's later
     * update envelopes (live plot samples, chunked video NALs) reach the
     * instance. Instances unregister themselves on destroy. */

    /* Bundles handed as registry user-data — one per coordinate mode.
     * Live on the terminal because the registry stores pointers to them,
     * and the host has to outlive every figure the registry might still
     * mint. figure_args serves the absolute "ygrid" chrome kind;
     * yscene_factory_args serves the local content kinds + "yscene". */
    struct yetty_yscene_factory_args figure_args;
    struct yetty_yscene_factory_args yscene_factory_args;

    /* The figure tree is reached via
     * yetty_yfigure_container_as_figure(root_container); producers drive it
     * through the yclass-RPC DCS server (see dcs_rpc_server below). */

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

    /* Multi-click detection for word (double) / line (triple) selection. A
     * press within sel_multiclick_sec of the previous one on the SAME cell bumps
     * the count; otherwise it resets to a single click. */
    double sel_last_click_sec;
    uint32_t sel_last_click_row;
    uint32_t sel_last_click_col;
    int sel_click_count;
};

/* How many lines a single mouse-wheel notch moves the scrollback view. */
#define YETTY_YTERMINAL_WHEEL_LINES_PER_TICK 3

/* Reserved root-container child id for the content grid figure (text + ydraw),
 * seated at the lowest z. High sentinel so it can't collide with producer
 * figure ids arriving over the wire. */
#define YETTY_YTERMINAL_GRID_FIGURE_ID 0xFFFFFFFDu

/* Forward declarations */
static struct yetty_ycore_void_result terminal_read_pty(struct yetty_yterminal_terminal *terminal);
static struct yetty_ycore_void_result terminal_render_frame(
    struct yetty_yterminal_terminal *terminal, struct yetty_ydraw_target *target, int force_redraw);

/* PTY pipe alloc callback — provides buffer for uv_pipe_t reads.
 * One reusable per-terminal buffer, lazily allocated. 64KB matches
 * libuv's default suggested_size on Linux/macOS and is plenty for one
 * PTY chunk; the read callback drains it before the next call. */
#define YETTY_YTERMINAL_PTY_READ_BUF_SIZE (64 * 1024)

/* libuv-shaped buffer-alloc callback: signature dictated by yetty_pipe_alloc_cb
 * which is dispatched from the libuv read path. Cannot return a Result. */
YETTY_EXTERNAL_CALLBACK
static void terminal_pty_pipe_alloc(void *ctx, size_t suggested_size, char **buf, size_t *buflen)
{
    (void)suggested_size;
    struct yetty_yterminal_terminal *terminal = ctx;
    if (!terminal->pty_read_buf) {
        terminal->pty_read_buf = malloc(YETTY_YTERMINAL_PTY_READ_BUF_SIZE);
        if (!terminal->pty_read_buf) {
            *buf = NULL;
            *buflen = 0;
            return;
        }
    }
    *buf = terminal->pty_read_buf;
    *buflen = YETTY_YTERMINAL_PTY_READ_BUF_SIZE;
}

static struct yetty_ycore_size_result terminal_pty_write_raw(
    struct yetty_yterminal_terminal *terminal, const char *data, size_t len);

/* Reliably write an entire buffer to the pane PTY (retries EAGAIN). Used to
 * flush released input-barrier bytes to the resumed shell. Returns the delivery
 * result; the teardown/expiry call sites choose to absorb it (best-effort). */
static struct yetty_ycore_void_result terminal_pty_write_all(
    struct yetty_yterminal_terminal *terminal, const uint8_t *data, size_t size);

/* Stop the barrier-expiry timer. Returns the stop result. Forward-declared
 * because terminal_pty_pipe_read (above its definition) stops it on release. */
static struct yetty_ycore_void_result terminal_input_barrier_stop_timer(
    struct yetty_yterminal_terminal *terminal);

/* libuv-shaped pipe-read callback. Errors from feed/process have no
 * Result to propagate to — absorb them at this boundary by logging the
 * full chain and destroying it. */
YETTY_EXTERNAL_CALLBACK
static void terminal_pty_pipe_read(void *ctx, const char *buf, long nread)
{
    struct yetty_yterminal_terminal *terminal = ctx;

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
            /* libuv-callback boundary: no Result to propagate. Surface
             * the chain as a ynotify card; the loop keeps running. Build
             * via YETTY_ERR so this frame gets file/line/func. */
            struct yetty_ycore_void_result wrap = YETTY_ERR(
                yetty_ycore_void, "terminal_pty_pipe_read: wire_statemachine_feed failed", fr);
            struct yetty_yevent_event_loop *loop = terminal->context.yetty_context.event_loop;
            loop->ops->post_fatal_error(loop, wrap.error);
            return;
        }
        /* This feed may have processed the in-pane client's final channel
         * CLOSE (channel_host arms/holds the input barrier). If the client is
         * now fully gone, RELEASE the keystrokes the host held during its
         * teardown into the pane PTY — the resumed shell reads them exactly
         * once. Host-owned bytes only; nothing from pane output. */
        if (terminal->channel_host) {
            struct yetty_ycore_buffer released = {0};
            int released_len =
                yetty_ywire_connection_input_barrier_release(terminal->channel_host, &released);
            if (released_len > 0) {
                /* Best-effort at the read callback: a failed flush means a dead
                 * child (nothing left to deliver to) — absorb and keep serving. */
                struct yetty_ycore_void_result flush_res =
                    terminal_pty_write_all(terminal, released.data, released.size);
                if (YETTY_IS_ERR(flush_res)) {
                    yetty_ycore_error_destroy(flush_res.error);
                }
                struct yetty_ycore_void_result stop_res =
                    terminal_input_barrier_stop_timer(terminal); /* barrier disarmed */
                if (YETTY_IS_ERR(stop_res)) {
                    yetty_ycore_error_destroy(stop_res.error);
                }
            }
            yetty_ycore_buffer_destroy(&released);
        }
        {
            /* Ask the content grid whether anything went dirty this feed. Its
             * own figure dirty bit is never set — the real dirty bits live on
             * the text grid + ydraw canvas it owns, which is_dirty aggregates. */
            struct yetty_ycore_int_result grid_dirty_res =
                yetty_yvterm_vterm_is_dirty(terminal->grid);
            int grid_dirty = 0;
            if (YETTY_IS_OK(grid_dirty_res)) {
                grid_dirty = grid_dirty_res.value;
            } else {
                yetty_ycore_error_destroy(grid_dirty_res.error);
            }
            ydebug("terminal_pty_pipe_read: after feed grid=%p dirty=%d", (void *)terminal->grid,
                   grid_dirty);
            if (grid_dirty) {
                terminal->context.yetty_context.event_loop->ops->request_render(
                    terminal->context.yetty_context.event_loop);
            }
            /* Rich content lives on yvterm's own line ring now, so it scrolls
             * with the text automatically — no separate rolling-row origin to
             * keep in sync. */
        }
        /* Root-container path: yclass-RPC figure mutations (create_child,
         * apply_child_body, …) applied during this feed mark the container
         * dirty here. The text-layer dirty check above only covers
         * libvterm-side mutations, so without this a figure-only frame lands
         * silently and the screen stays stale until something else triggers
         * a render. */
        if (terminal->root_container_obj) {
            /* External-callback boundary (this fn is YETTY_EXTERNAL_CALLBACK):
             * the obj→figure downcast cannot propagate, so absorb here. */
            struct yetty_yfigure_figure_ptr_result rf_res =
                yetty_yfigure_container_as_figure(terminal->root_container_obj);
            if (YETTY_IS_ERR(rf_res)) {
                yetty_ycore_error_destroy(rf_res.error);
            } else if (rf_res.value && yetty_yfigure_figure_dirty_get(
                                           (struct yetty_yclass_object *)(rf_res.value) - 1)
                                           .value) {
                terminal->context.yetty_context.event_loop->ops->request_render(
                    terminal->context.yetty_context.event_loop);
            }
        }
    } else if (nread < 0 && !terminal->shutting_down) {
        /* Negative read: the child shell exited (UV_EOF after Ctrl-D /
     * `exit`) — OR a tty hangup the CLIENT side provoked while the shell
     * is perfectly alive: a pty master reads EIO after session-leader
     * games or a SIGKILLed foreground group (a `timeout`-killed client,
     * Ctrl-C at the wrong moment). Closing the pane — and with it,
     * potentially the whole app — is only correct when the child is
     * verifiably gone; otherwise keep the terminal (its output may stall
     * until the next feed, but the session survives).
     *
     * Post CLOSE with this terminal's view id through the platform input
     * pipe; the yetty event handler resolves the hosting pane and closes
     * just that pane (or the workspace when it was the pane's only tab —
     * and only when it is also the last workspace does this escalate to a
     * full SHUTDOWN).
     *
     * The shutting_down guard avoids re-posting if teardown already
     * started (e.g. fork_pty_stop closed the master while we were tearing
     * down for another reason). Setting it here also stops this terminal's
     * render path from doing further GPU work while the close event is in
     * flight. */
        struct yetty_platform_pty *pty = terminal->context.pty;
        /* No way to check the child → assume it is gone and close, rather
         * than leak a pane that can never be dismissed. */
        int child_alive = 0;
        if (pty && pty->ops->child_alive) {
            /* A dying child closes its pty fds a hair BEFORE it becomes
             * waitable (exit_files precedes exit_notify); on Linux the master
             * read then returns EIO, not EOF, so this callback routinely
             * outruns the child's zombie transition. Poll with a real delay
             * between attempts — a tight spin resolves in microseconds and
             * loses the race against a heavier child tree (e.g. bash → uv →
             * python from `yetty -e 'uvx …'`), which is exactly how a fully
             * exited session was being misread as "still alive" and the pane
             * (and the whole app, when it is the last one) left hanging.
             *
             * A normally-exiting child is reaped within a few ms and closes
             * the pane; only a child still alive after the entire grace
             * window is a genuine transient hangup (Ctrl-C that the shell
             * survived) we keep the terminal open for. Checking before each
             * sleep means the common case adds no latency. This runs on the
             * event-loop thread, but only once, at terminal death, where a
             * sub-second stall is invisible. */
            enum { CHILD_REAP_GRACE_MS = 500, CHILD_REAP_STEP_MS = 10 };
            child_alive = 1;
            for (int waited_ms = 0;; waited_ms += CHILD_REAP_STEP_MS) {
                if (pty->ops->child_alive(pty) != 1) {
                    child_alive = 0;
                    break;
                }
                if (waited_ms >= CHILD_REAP_GRACE_MS) {
                    break;
                }
                yetty_yplatform_ytime_sleep_ms(CHILD_REAP_STEP_MS);
            }
        }
        if (child_alive) {
            ywarn("terminal_pty_pipe_read: PTY read error (nread=%ld) with the child still "
                  "alive — transient tty hangup, keeping the terminal open",
                  nread);
            return;
        }
        ydebug("terminal_pty_pipe_read: PTY EOF (nread=%ld), posting CLOSE for view %llu", nread,
               (unsigned long long)terminal->view.id);
        terminal->shutting_down = 1;
        /* Ungraceful teardown (PTY EOF with an armed barrier): recover any held
         * input-barrier bytes exactly once so nothing is stranded in memory. The
         * forced release disarms; the write is best-effort (a closed PTY no-ops
         * it). */
        if (terminal->channel_host) {
            struct yetty_ycore_buffer released = {0};
            int released_len = yetty_ywire_connection_input_barrier_release_forced(
                terminal->channel_host, &released);
            if (released_len > 0) {
                struct yetty_ycore_void_result flush_res =
                    terminal_pty_write_all(terminal, released.data, released.size);
                if (YETTY_IS_ERR(flush_res)) {
                    yetty_ycore_error_destroy(flush_res.error);
                }
            }
            yetty_ycore_buffer_destroy(&released);
            struct yetty_ycore_void_result stop_res = terminal_input_barrier_stop_timer(terminal);
            if (YETTY_IS_ERR(stop_res)) {
                yetty_ycore_error_destroy(stop_res.error);
            }
        }
        struct yetty_ycore_xthread_event_pipe *pipe =
            terminal->context.yetty_context.runtime->platform_input_pipe;
        if (pipe && pipe->ops && pipe->ops->write) {
            struct yetty_yui_event ev = {.type = YETTY_YCORE_CLOSE};
            ev.close.object_id = terminal->view.id;
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
    struct yetty_yterminal_terminal *terminal, const char *data, size_t len)
{
    if (!terminal->context.pty->ops->write) {
        return YETTY_ERR(yetty_ycore_size, "terminal_pty_write_raw: PTY backend has no `write` op");
    }
    return terminal->context.pty->ops->write(terminal->context.pty, data, len);
}

static struct yetty_ycore_void_result terminal_pty_write_all(
    struct yetty_yterminal_terminal *terminal, const uint8_t *data, size_t size)
{
    /* Reliable full-buffer delivery: retries EAGAIN (a zero-byte return on the
     * non-blocking master) so a busy PTY or a large released paste never drops
     * its tail. Surfaces a genuine write error (a dead child) to the caller,
     * which decides whether to absorb it. */
    struct yetty_ycore_void_result wr =
        yetty_yplatform_pty_write_all(terminal->context.pty, data, size);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, wr, "terminal_pty_write_all");
    return YETTY_OK_VOID();
}

/* yetty_yterminal_pty_write_fn impl — adapts the Result-returning PTY op
 * (size_result) to the typedef (void_result). */
YETTY_ANNOTATE("override@ytermsink:sink:pty_write")
static struct yetty_ycore_void_result terminal_sink_pty_write(struct yetty_yclass_object *obj,
                                                              const char *data, size_t len)
{
    struct yetty_yterminal_terminal_ptr_result terminal_res = yetty_yterminal_terminal_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, terminal_res, "terminal sink pty_write: from_obj");
    struct yetty_yterminal_terminal *terminal = terminal_res.value;
    /* Host-owned input barrier: while an in-pane client is tearing down, HOLD
     * these keystrokes host-side instead of forwarding them into the stream the
     * client's close drain would consume (and lose). They release to the pane
     * once the client is gone (terminal_pty_pipe_read). Held bytes are our OWN
     * user input, never pane output — so this cannot inject. */
    if (terminal->channel_host) {
        if (yetty_ywire_connection_input_barrier_hold(terminal->channel_host, data, len)) {
            return YETTY_OK_VOID();
        }
        /* Not held: either unarmed, or the barrier passed its host-side deadline
         * (a wedged/dead client). On expiry, flush the held backlog to the pane
         * NOW so retention stays bounded, then forward this keystroke below.
         * Bail on the first error (explicit blocks so the released buffer is
         * freed before returning). */
        struct yetty_ycore_buffer expired = {0};
        int flushed =
            yetty_ywire_connection_input_barrier_release(terminal->channel_host, &expired);
        if (flushed > 0) {
            struct yetty_ycore_void_result flush_res =
                terminal_pty_write_all(terminal, expired.data, expired.size);
            if (YETTY_IS_ERR(flush_res)) {
                yetty_ycore_buffer_destroy(&expired);
                return YETTY_ERR(yetty_ycore_void,
                                 "terminal sink pty_write: flush expired barrier backlog",
                                 flush_res);
            }
            struct yetty_ycore_void_result stop_res = terminal_input_barrier_stop_timer(terminal);
            if (YETTY_IS_ERR(stop_res)) {
                yetty_ycore_buffer_destroy(&expired);
                return YETTY_ERR(yetty_ycore_void, "terminal sink pty_write: stop barrier timer",
                                 stop_res);
            }
        }
        yetty_ycore_buffer_destroy(&expired);
    }
    struct yetty_ycore_size_result r = terminal_pty_write_raw(terminal, data, len);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "terminal sink pty_write: pty_write_raw failed");
    ydebug("terminal sink pty_write: wrote %zu bytes to PTY", len);
    return YETTY_OK_VOID();
}

/* Content-layer clear hook (full-screen erase / reset): wipe every positioned
 * compositor figure in the root container. Registered in terminal_create after
 * the container exists. */
static struct yetty_ycore_void_result terminal_clear_figures_callback(void *userdata)
{
    struct yetty_yterminal_terminal *terminal = userdata;
    if (!terminal || !terminal->root_container_obj) {
        return YETTY_OK_VOID();
    }
    ydebug("terminal_clear_figures_callback: full-screen erase/reset -> clearing root container");
    struct yetty_ycore_void_result r =
        yetty_yfigure_container_clear_all(terminal->root_container_obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r,
                        "terminal_clear_figures_callback: container clear_all");
    return YETTY_OK_VOID();
}

/* Terminal reset hook (RIS / DECSTR): drop every client-input subscription.
 * These are per-session opt-ins from a PTY client; when that client dies
 * without unsubscribing, the pane keeps spraying SC_CLIENT_INPUT_MOUSE
 * envelopes at whatever now owns the PTY (the shell). `reset` must cure
 * that, exactly as it cures a leaked DEC mouse mode. */
static struct yetty_ycore_void_result terminal_reset_subscriptions_callback(int hard,
                                                                            void *userdata)
{
    struct yetty_yterminal_terminal *terminal = userdata;
    (void)hard; /* RIS and DECSTR both clear subscription state */
    if (!terminal) {
        return YETTY_OK_VOID();
    }
    if (terminal->resize_subscribed || terminal->figure_key_optin ||
        terminal->pane_mouse_click_subscribed || terminal->pane_mouse_move_subscribed ||
        terminal->pane_mouse_wheel_subscribed) {
        ydebug("terminal reset: dropping client-input subscriptions");
    }
    terminal->resize_subscribed = 0;
    terminal->figure_key_optin = 0;
    terminal->pane_mouse_click_subscribed = 0;
    terminal->pane_mouse_move_subscribed = 0;
    terminal->pane_mouse_wheel_subscribed = 0;
    if (terminal->channel_host) {
        /* Dropped subscriptions also drop the RPC-less presence marker. */
        yetty_ywire_connection_input_barrier_client_present(terminal->channel_host, 0);
    }
    return YETTY_OK_VOID();
}

/* Figure re-materialization hook: replay a wire envelope retained on a grid
 * line through the complex factory — the same call that minted the figure
 * when the envelope first arrived over the PTY. The grid invokes this when an
 * evicted history line (past the scrollback hot window) scrolls back into
 * view; the fresh instance's ownership transfers to the grid line. */
static struct yetty_ycore_void_result terminal_materialize_figure_callback(
    const uint32_t *envelope_words, uint32_t envelope_word_count, const uint32_t *journal_words,
    uint32_t journal_word_count, void *userdata, struct yetty_ydraw_complex **out_instance)
{
    struct yetty_yterminal_terminal *terminal = userdata;
    *out_instance = NULL;
    if (!terminal || !terminal->complex_factory || envelope_word_count == 0) {
        return YETTY_OK_VOID();
    }
    struct yetty_ydraw_complex_ptr_result instance_res =
        yetty_ydraw_complex_factory_create_instance(terminal->complex_factory, envelope_words,
                                                    (size_t)envelope_word_count * sizeof(uint32_t),
                                                    /*rolling_row=*/0u);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, instance_res,
                        "terminal_materialize_figure: create_instance");
    struct yetty_ydraw_complex *instance = instance_res.value;
    /* Replay the accepted-update journal so the figure comes back at its
     * last state, not its creation state: entries are {target_field,
     * payload_words, payload...} back to back. Best-effort per entry. */
    uint32_t offset = 0;
    while (instance && instance->ops && instance->ops->update &&
           offset + 2u <= journal_word_count) {
        uint32_t target_field = journal_words[offset];
        uint32_t payload_words = journal_words[offset + 1u];
        if (payload_words > journal_word_count - offset - 2u) {
            break; /* corrupt tail — stop replaying */
        }
        struct yetty_ycore_void_result replay_res = instance->ops->update(
            instance, target_field, (const uint8_t *)(journal_words + offset + 2u),
            (size_t)payload_words * sizeof(uint32_t));
        if (YETTY_IS_ERR(replay_res)) {
            yetty_ycore_error_destroy(replay_res.error);
        }
        offset += 2u + payload_words;
    }
    *out_instance = instance;
    return YETTY_OK_VOID();
}

/* yetty_yclass_rpc_dcs_emit_fn impl — ships an already-encoded DCS
 * envelope through the PTY master so the subprocess sees it on its
 * stdin. Looping write because pty write ops are single-shot (one
 * write(2) per call) and a short write would otherwise drop the
 * tail. */
YETTY_EXTERNAL_CALLBACK
static struct yetty_ycore_void_result terminal_dcs_emit_response(const uint8_t *bytes, size_t n,
                                                                 void *userdata)
{
    struct yetty_yterminal_terminal *terminal = userdata;
    size_t off = 0;
    while (off < n) {
        struct yetty_ycore_size_result wr =
            terminal_pty_write_raw(terminal, (const char *)bytes + off, n - off);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, wr,
                            "terminal_dcs_emit_response: pty_write_raw failed");
        if (wr.value == 0) {
            yetty_yplatform_ytime_sleep_ms(1);
            continue;
        }
        off += wr.value;
    }
    return YETTY_OK_VOID();
}

/* Build one yface envelope around `payload` and ship it to the inferior.
 * compressed=0 because input events are short — LZ4 framing would dominate.
 *
 * The yface out buffer is reused across calls; success path drains it via
 * pty_write_raw, error paths clear it explicitly so the next call starts
 * from a known state. */
static struct yetty_ycore_void_result terminal_yface_emit(struct yetty_yterminal_terminal *terminal,
                                                          int osc_code, const void *payload,
                                                          size_t len)
{
    struct yetty_ycore_void_result sr =
        yetty_yface_start_write(terminal->emit_yface, YETTY_YWIRE_ENVELOPE_OSC, osc_code,
                                /*compressed=*/0, /*args=*/NULL, /*args_len=*/0);
    if (YETTY_IS_ERR(sr)) {
        struct yetty_ycore_buffer *out_buf = yetty_yface_out_buf(terminal->emit_yface);
        if (out_buf) {
            yetty_ycore_buffer_clear(out_buf);
        }
        return YETTY_ERR(yetty_ycore_void, "terminal_yface_emit: yface_start_write failed", sr);
    }
    struct yetty_ycore_void_result wr = yetty_yface_write(terminal->emit_yface, payload, len);
    if (YETTY_IS_ERR(wr)) {
        struct yetty_ycore_buffer *out_buf = yetty_yface_out_buf(terminal->emit_yface);
        if (out_buf) {
            yetty_ycore_buffer_clear(out_buf);
        }
        return YETTY_ERR(yetty_ycore_void, "terminal_yface_emit: yface_write failed", wr);
    }
    struct yetty_ycore_void_result fr = yetty_yface_finish_write(terminal->emit_yface);
    if (YETTY_IS_ERR(fr)) {
        struct yetty_ycore_buffer *out_buf = yetty_yface_out_buf(terminal->emit_yface);
        if (out_buf) {
            yetty_ycore_buffer_clear(out_buf);
        }
        return YETTY_ERR(yetty_ycore_void, "terminal_yface_emit: yface_finish_write failed", fr);
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
            struct yetty_ycore_size_result pwr =
                terminal_pty_write_raw(terminal, (const char *)out->data + off, out->size - off);
            if (YETTY_IS_ERR(pwr)) {
                yetty_ycore_buffer_clear(out);
                return YETTY_ERR(yetty_ycore_void, "terminal_yface_emit: pty_write_raw failed",
                                 pwr);
            }
            if (pwr.value == 0) {
                /* EAGAIN territory — back off briefly so we don't spin
                 * the main thread while the kernel drains the buffer. */
                yetty_yplatform_ytime_sleep_ms(1);
                continue;
            }
            off += pwr.value;
        }
        yetty_ycore_buffer_clear(out);
    }
    return YETTY_OK_VOID();
}

/* yetty_yterminal_emit_osc_fn impl — wired into ymgui-layer at create time so
 * the layer can ship FOCUS / RESIZE events back to the focused client
 * without owning its own emit_yface. */
static struct yetty_ycore_void_result terminal_layer_emit_osc(int osc_code, const void *payload,
                                                              size_t len, void *userdata)
{
    struct yetty_yterminal_terminal *terminal = userdata;
    ydebug("terminal_layer_emit_osc: code=%d payload_len=%zu", osc_code, len);
    struct yetty_ycore_void_result emit_res = terminal_yface_emit(terminal, osc_code, payload, len);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, emit_res, "terminal_layer_emit_osc: yface_emit failed");
    return YETTY_OK_VOID();
}

/* Card-aware mouse forwarding. Each emit carries a figure_id and
 * card-local pixel coords. figure_id=0 means "no card here" — clients
 * use that to clear their hover state. */
static struct yetty_ycore_void_result terminal_emit_card_focus(
    struct yetty_yterminal_terminal *terminal, uint32_t figure_id, int gained)
{
    struct yetty_client_input_focus msg = {
        .magic = YETTY_CLIENT_INPUT_FOCUS_MAGIC,
        .version = YMGUI_WIRE_VERSION,
        .figure_id = figure_id,
        .gained = gained,
    };
    struct yetty_ycore_void_result emit_res =
        terminal_yface_emit(terminal, YETTY_OSC_SC_CLIENT_INPUT_FIGURE_FOCUS, &msg, sizeof(msg));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, emit_res, "terminal_emit_card_focus: yface_emit failed");
    return YETTY_OK_VOID();
}

/* Tell a windowed/figure client its pixel size so it can lay out. This is
 * the only size signal a client gets over transports that don't carry
 * pixels in TIOCGWINSZ — telnet NAWS (the --temu/--qemu guest path) ships
 * character cols/rows only, so ws_xpixel/ws_ypixel arrive as 0. Emitted on
 * the mouse-subscribe rising edge (initial size) and on every pane resize. */
/* Protocol family and target id are DISTINCT inputs: the family comes from
 * which subscription is being served (DEC figure path vs pane-wide
 * CLIENT_INPUT_SUB), never inferred from the id — a DEC subscriber gets the
 * figure-tagged code even before any figure has focus (figure_id 0). */
static struct yetty_ycore_void_result terminal_emit_resize_envelope(
    struct yetty_yterminal_terminal *terminal, uint32_t osc_code, uint32_t figure_id, float width,
    float height, float content_scale)
{
    struct yetty_client_input_resize msg = {
        .magic = YETTY_CLIENT_INPUT_RESIZE_MAGIC,
        .version = YMGUI_WIRE_VERSION,
        .figure_id = figure_id,
        .content_scale = content_scale,
        .width = width,
        .height = height,
    };
    struct yetty_ycore_void_result emit_res =
        terminal_yface_emit(terminal, osc_code, &msg, sizeof(msg));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, emit_res,
                        "terminal_emit_resize_envelope: yface_emit failed");
    return YETTY_OK_VOID();
}

/* The DEC figure family (SC_CLIENT_INPUT_FIGURE_RESIZE, any figure_id incl.
 * 0 before the first click-focus). Publishes the host display's HiDPI
 * factor — figure bodies render through the compositor, which does not
 * apply the structural cell zoom, so their clients divide by the bare
 * density. */
static struct yetty_ycore_void_result terminal_emit_card_resize(
    struct yetty_yterminal_terminal *terminal, uint32_t figure_id, float width, float height)
{
    float content_scale =
        terminal->layout_content_scale > 0.0f ? terminal->layout_content_scale : 1.0f;
    return terminal_emit_resize_envelope(terminal, YETTY_OSC_SC_CLIENT_INPUT_FIGURE_RESIZE,
                                         figure_id, width, height, content_scale);
}

/* The pane-wide family (SC_CLIENT_INPUT_RESIZE, figure_id always 0).
 * Publishes the COMMITTED logical→device product: display density x
 * structural cell zoom — the exact factor the terminal renders a
 * drawable-tree client's retained content by. A Ctrl+Shift zoom tick
 * grows the product, the client's logical viewport shrinks and it
 * REFLOWS (exactly like terminal text does), and the raw device-px
 * pointer contract (client divides by this scale) stays correct at any
 * zoom with no per-event coordinate rewriting. */
static struct yetty_ycore_void_result terminal_emit_pane_resize(
    struct yetty_yterminal_terminal *terminal, float width, float height)
{
    /* Publish the COMMITTED product read back from yvterm — the exact
     * value retirement, reserve conversion and rendering use. Nominal
     * terminal-side state (layout scale x accumulated zoom) differs from
     * it whenever the cell stride SNAPPED: an 18px baseline at nominal
     * zoom 1.1 commits a 20px cell, so the projection is 20/18 = 1.111…,
     * not 1.1 — publishing the nominal value would make the client lay
     * out and hit-test in a different coordinate system from the
     * rendered content. Fallback: the bare layout scale (no grid yet). */
    float content_scale =
        terminal->layout_content_scale > 0.0f ? terminal->layout_content_scale : 1.0f;
    if (terminal->grid) {
        struct yetty_ycore_void_result density_res =
            yetty_yvterm_vterm_rich_density(terminal->grid, &content_scale);
        if (YETTY_IS_ERR(density_res)) {
            yetty_ycore_error_destroy(density_res.error);
        }
    }
    return terminal_emit_resize_envelope(terminal, YETTY_OSC_SC_CLIENT_INPUT_RESIZE, 0u, width,
                                         height, content_scale);
}

/* figure_id != 0 → figure-tagged envelope, coords are figure-local;
 * figure_id == 0 → pane-wide envelope (SC_CLIENT_INPUT_MOUSE), coords are
 * pane-local — the documented coordinate-space convention of
 * client-input.h. Same struct either way. */
static struct yetty_ycore_void_result terminal_emit_card_mouse_button(
    struct yetty_yterminal_terminal *terminal, uint32_t figure_id, float lx, float ly, int button,
    int press, float wheel_dy, int mods)
{
    struct yetty_client_input_mouse msg = {
        .magic = YETTY_CLIENT_INPUT_MOUSE_MAGIC,
        .version = YMGUI_WIRE_VERSION,
        .figure_id = figure_id,
        .x = lx,
        .y = ly,
        .mods = mods,
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
    uint32_t osc_code =
        figure_id != 0 ? YETTY_OSC_SC_CLIENT_INPUT_FIGURE_MOUSE : YETTY_OSC_SC_CLIENT_INPUT_MOUSE;
    struct yetty_ycore_void_result emit_res =
        terminal_yface_emit(terminal, osc_code, &msg, sizeof(msg));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, emit_res,
                        "terminal_emit_card_mouse_button: yface_emit failed");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result terminal_emit_card_mouse_move(
    struct yetty_yterminal_terminal *terminal, uint32_t figure_id, float lx, float ly,
    int buttons_held)
{
    struct yetty_client_input_mouse msg = {
        .magic = YETTY_CLIENT_INPUT_MOUSE_MAGIC,
        .version = YMGUI_WIRE_VERSION,
        .figure_id = figure_id,
        .kind = YETTY_YMGUI_INPUT_MOUSE_POS,
        .button = -1,
        .buttons_held = (uint32_t)buttons_held,
        .x = lx,
        .y = ly,
    };
    uint32_t osc_code =
        figure_id != 0 ? YETTY_OSC_SC_CLIENT_INPUT_FIGURE_MOUSE : YETTY_OSC_SC_CLIENT_INPUT_MOUSE;
    struct yetty_ycore_void_result emit_res =
        terminal_yface_emit(terminal, osc_code, &msg, sizeof(msg));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, emit_res,
                        "terminal_emit_card_mouse_move: yface_emit failed");
    return YETTY_OK_VOID();
}

/* Resolve the pane-pixel point (lx, ly) to a figure-local hit by
 * walking the root container's ymgui figures. If a figure is "captured"
 * (drag in progress), the captured figure always wins and coords are
 * reported as-if-projected into its local space. Otherwise the topmost
 * visible ymgui figure under the cursor wins. */
static struct yetty_yfigure_hit_result terminal_resolve_figure_hit(
    struct yetty_yterminal_terminal *terminal, float lx, float ly, uint32_t captured_figure_id)
{
    struct yetty_yfigure_hit hit = {0, 0, 0};
    if (!terminal->root_container_obj) {
        return YETTY_OK(yetty_yfigure_hit, hit);
    }

    /* The pointer arrives in FRAMEBUFFER px, but child figure rects are stored
     * in LOGICAL px — the container adds viewport_offset to each rect_local and
     * that offset is itself divided by layout_content_scale (see
     * terminal_set_bounds). Hit-testing raw framebuffer coords against logical
     * rects picks the wrong figure by a factor of content_scale, and the
     * reported local_* then carries a (rect_origin - rect_origin/scale) error
     * — 7 px at 1.25, ~18 at 2.0, invisible at 1.0. Test in the rects' own
     * space, then scale the figure-local result back OUT to framebuffer px:
     * the client-input wire contract is device px and every ygui client
     * divides by the content_scale it learned from the resize envelope. */
    const float hit_scale =
        terminal->layout_content_scale > 0.0f ? terminal->layout_content_scale : 1.0f;
    const float logical_x = lx / hit_scale;
    const float logical_y = ly / hit_scale;

    if (captured_figure_id != 0) {
        /* Drag: route to the captured figure; project the cursor into
         * its local space even when the cursor leaves the figure's rect.
         * Hit-test first — if cursor is still inside the captured figure,
         * use the natural local coords; otherwise fall back to the raw
         * pane coords tagged with the captured id. */
        struct yetty_yfigure_hit_result hit_res =
            yetty_yfigure_container_hit_test(terminal->root_container_obj, logical_x, logical_y);
        YETTY_RETURN_IF_ERR(yetty_yfigure_hit, hit_res, "resolve_figure_hit: hit_test");
        hit = hit_res.value;
        if (hit.figure_id == captured_figure_id) {
            hit.local_x *= hit_scale;
            hit.local_y *= hit_scale;
            return YETTY_OK(yetty_yfigure_hit, hit);
        }
        /* Cursor left the captured figure: ship the raw pane coords (already
         * framebuffer px) tagged with the captured id, as before. */
        struct yetty_yfigure_hit captured = {captured_figure_id, lx, ly};
        return YETTY_OK(yetty_yfigure_hit, captured);
    }

    struct yetty_yfigure_hit_result plain_res =
        yetty_yfigure_container_hit_test(terminal->root_container_obj, logical_x, logical_y);
    YETTY_RETURN_IF_ERR(yetty_yfigure_hit, plain_res, "resolve_figure_hit: hit_test");
    hit = plain_res.value;
    hit.local_x *= hit_scale;
    hit.local_y *= hit_scale;
    ydebug("terminal resolve_figure_hit: (%.1f,%.1f) -> figure_id=%u local=(%.1f,%.1f)", lx, ly,
           hit.figure_id, hit.local_x, hit.local_y);
    return YETTY_OK(yetty_yfigure_hit, hit);
}

/* Emit a keyboard event for the focused figure. Returns 1 if delivered
 * (caller treats the keystroke as consumed), 0 otherwise. Emit failures
 * propagate via the Result. */
static struct yetty_ycore_int_result terminal_emit_figure_key(
    struct yetty_yterminal_terminal *terminal, uint32_t kind, int key, int mods, uint32_t codepoint)
{
    uint32_t focused = terminal->focused_figure_id;
    if (focused == 0) {
        return YETTY_OK(yetty_ycore_int, 0);
    }

    struct yetty_client_input_key msg = {
        .magic = YETTY_CLIENT_INPUT_KEY_MAGIC,
        .version = YMGUI_WIRE_VERSION,
        .figure_id = focused,
        .kind = kind,
        .key = key,
        .mods = mods,
        .codepoint = codepoint,
    };
    struct yetty_ycore_void_result emit_res =
        terminal_yface_emit(terminal, YETTY_OSC_SC_CLIENT_INPUT_FIGURE_KEY, &msg, sizeof(msg));
    YETTY_RETURN_IF_ERR(yetty_ycore_int, emit_res, "terminal_emit_figure_key: yface_emit failed");
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

/* Live anchor of the content grid (it maxes its own text + ydraw anchors). */
static struct yetty_ycore_uint64_result terminal_live_anchor(
    struct yetty_yterminal_terminal *terminal)
{
    return yetty_yvterm_vterm_get_live_anchor(terminal->grid);
}

/* Oldest absolute line index a scrollback view may scroll up to. Lines below
 * this have aged out of the grid's bounded history (scrollback/lines), so a
 * wheel-up clamps here instead of marching into blank evicted rows. */
static struct yetty_ycore_uint64_result terminal_scrollback_floor(
    struct yetty_yterminal_terminal *terminal)
{
    return yetty_yvterm_vterm_get_scrollback_floor(terminal->grid);
}

/* Push a scrollback view transition into the content grid (the single view
 * owner) and schedule a repaint. */
static struct yetty_ycore_void_result terminal_push_view_top(
    struct yetty_yterminal_terminal *terminal, int active, uint64_t view_top)
{
    struct yetty_ycore_void_result r =
        yetty_yvterm_vterm_set_view_top(terminal->grid, active, view_top);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "terminal_push_view_top: set_view_top failed");
    /* Rich content rides yvterm's own line ring, so set_view_top already scrolls
     * it with the text — nothing extra to pin. */
    terminal->context.yetty_context.event_loop->ops->request_render(
        terminal->context.yetty_context.event_loop);
    return YETTY_OK_VOID();
}

/* Apply a relative wheel delta. Positive lines = scroll up (older). On the
 * first wheel-up out of live mode we anchor view_top one line back from
 * the current live position and enter scrollback. Scrolling past the live
 * anchor exits back to live. */
static struct yetty_ycore_void_result terminal_scrollback_apply(
    struct yetty_yterminal_terminal *terminal, int lines)
{
    struct yetty_ycore_uint64_result live_res = terminal_live_anchor(terminal);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, live_res, "terminal_scrollback_apply: live anchor");
    uint64_t live = live_res.value;

    /* The grid owns the view — read the CURRENT state instead of trusting a
     * local copy that resize or eviction may have invalidated meanwhile. */
    int active = 0;
    uint64_t view_top = 0;
    struct yetty_ycore_void_result view_res =
        yetty_yvterm_vterm_get_view(terminal->grid, &active, &view_top);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, view_res, "terminal_scrollback_apply: get view");

    if (!active) {
        if (lines <= 0) {
            return YETTY_OK_VOID(); /* downward wheel in live mode: nothing to do */
        }
        if (live == 0) {
            return YETTY_OK_VOID(); /* nothing in scrollback yet */
        }
        active = 1;
        view_top = live - 1u;
        if (lines > 1) {
            lines -= 1; /* the entry already consumed one notch */
        } else {
            lines = 0;
        }
    }

    if (lines > 0) {
        /* Clamp to the oldest line still retained, not to 0 — once eviction
         * starts the floor rises above 0 and scrolling to 0 would show blank
         * aged-out rows. */
        struct yetty_ycore_uint64_result floor_res = terminal_scrollback_floor(terminal);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, floor_res,
                            "terminal_scrollback_apply: scrollback floor");
        uint64_t floor = floor_res.value;
        uint64_t step = (uint64_t)lines;
        if (view_top < floor || step >= view_top - floor) {
            view_top = floor;
        } else {
            view_top -= step;
        }
    } else if (lines < 0) {
        uint64_t step = (uint64_t)(-(int64_t)lines);
        uint64_t target = view_top + step;
        if (target >= live) {
            /* Scrolled forward into the live region — exit scrollback. */
            active = 0;
            view_top = live;
        } else {
            view_top = target;
        }
    }

    ydebug("scrollback: active=%d view_top=%llu live=%llu", active, (unsigned long long)view_top,
           (unsigned long long)live);
    struct yetty_ycore_void_result r = terminal_push_view_top(terminal, active, view_top);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "terminal_scrollback_apply: push_view_top failed");
    return YETTY_OK_VOID();
}

/* Force a return to live, regardless of current view position. */
static struct yetty_ycore_void_result terminal_scrollback_exit(
    struct yetty_yterminal_terminal *terminal)
{
    int active = 0;
    struct yetty_ycore_void_result view_res =
        yetty_yvterm_vterm_get_view(terminal->grid, &active, NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, view_res, "terminal_scrollback_exit: get view");
    if (!active) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_uint64_result live_res = terminal_live_anchor(terminal);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, live_res, "terminal_scrollback_exit: live anchor");
    ydebug("scrollback: EXIT");
    struct yetty_ycore_void_result r = terminal_push_view_top(terminal, 0, live_res.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "terminal_scrollback_exit: push_view_top failed");
    return YETTY_OK_VOID();
}

/* Whether a scrolled-back view is currently active (grid-owned state). */
static int terminal_scrollback_is_active(struct yetty_yterminal_terminal *terminal)
{
    int active = 0;
    struct yetty_ycore_void_result view_res =
        yetty_yvterm_vterm_get_view(terminal->grid, &active, NULL);
    if (YETTY_IS_ERR(view_res)) {
        yetty_ycore_error_destroy(view_res.error);
        return 0;
    }
    return active;
}

/*---------------------------------------------------------------------------
 * Client-input reinject — YETTY_OSC_CS_CLIENT_INPUT_REINJECT.
 *
 * A pane-wide input subscriber (client-side focus model: it previews
 * every forwarded mouse event and hit-tests against its own GUI) ships
 * the events it did NOT consume back on this code. The terminal applies
 * the default, unsubscribed behavior — currently the wheel → scrollback
 * driver; other kinds are accepted and ignored until their defaults
 * (selection, …) are routed through here too. Reinjected events are
 * never re-forwarded to the subscriber, so no ping-pong is possible.
 *---------------------------------------------------------------------------*/

static struct yetty_ycore_void_result terminal_reinject_apply(
    struct yetty_yterminal_terminal *terminal, const struct yetty_client_input_mouse *msg)
{
    switch (msg->kind) {
    case YETTY_YMGUI_INPUT_MOUSE_WHEEL: {
        int lines = (int)(msg->wheel_dy * YETTY_YTERMINAL_WHEEL_LINES_PER_TICK);
        if (lines == 0 && msg->wheel_dy != 0.0f) {
            lines = (msg->wheel_dy > 0) ? 1 : -1;
        }
        if (lines != 0) {
            struct yetty_ycore_void_result scroll_res = terminal_scrollback_apply(terminal, lines);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, scroll_res,
                                "terminal_reinject_apply: scrollback_apply");
        }
        return YETTY_OK_VOID();
    }
    case YETTY_YMGUI_INPUT_MOUSE_BUTTON: {
        /* The subscriber bounced a button it did not consume — run the
         * same default the unsubscribed terminal would: left button drives
         * cell selection, middle button pastes. Coords are already
         * pane-local (the subscriber computed them). Scrollback view owns
         * the wheel but not clicks, so selection still works while paused. */
        if (msg->button == 2 && msg->pressed) {
            return terminal_paste_clipboard(terminal);
        }
        if (msg->button != 0) {
            return YETTY_OK_VOID();
        }
        uint32_t row = 0;
        uint32_t col = 0;
        struct yetty_ycore_void_result cell_res =
            terminal_cell_from_local(terminal, msg->x, msg->y, &row, &col);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, cell_res, "terminal_reinject_apply: cell from local");
        if (msg->pressed) {
            terminal->sel_anchor_row = row;
            terminal->sel_anchor_col = col;
            terminal->sel_head_row = row;
            terminal->sel_head_col = col;
            terminal->sel_active = 1;
            terminal->sel_dragging = 1;
            return terminal_push_selection(terminal);
        }
        /* Release: finalise. A pure click (anchor == head) clears the
         * highlight; a real drag pushes it and auto-copies when enabled. */
        if (!terminal->sel_dragging) {
            return YETTY_OK_VOID();
        }
        terminal->sel_dragging = 0;
        if (terminal->sel_anchor_row == terminal->sel_head_row &&
            terminal->sel_anchor_col == terminal->sel_head_col) {
            return terminal_clear_selection(terminal);
        }
        struct yetty_ycore_void_result psr = terminal_push_selection(terminal);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, psr, "terminal_reinject_apply: push_selection final");
        struct yetty_yconfig_config *cfg = terminal->context.yetty_context.runtime->config;
        int auto_copy = 1;
        if (cfg && cfg->ops && cfg->ops->get_bool) {
            auto_copy = cfg->ops->get_bool(cfg, "terminal/selection/auto-copy", 1);
        }
        if (auto_copy) {
            struct yetty_ycore_void_result cs = terminal_copy_selection(terminal);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, cs, "terminal_reinject_apply: auto-copy");
        }
        return YETTY_OK_VOID();
    }
    case YETTY_YMGUI_INPUT_MOUSE_POS: {
        /* Drag tracking — extend the selection head while a button-1 drag
         * is in flight (the press set sel_dragging). */
        if (!terminal->sel_dragging) {
            return YETTY_OK_VOID();
        }
        uint32_t row = 0;
        uint32_t col = 0;
        struct yetty_ycore_void_result cell_res =
            terminal_cell_from_local(terminal, msg->x, msg->y, &row, &col);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, cell_res, "terminal_reinject_apply: drag cell");
        terminal->sel_head_row = row;
        terminal->sel_head_col = col;
        return terminal_push_selection(terminal);
    }
    default:
        ydebug("terminal: reinject kind=%u ignored", msg->kind);
        return YETTY_OK_VOID();
    }
}

/* Read one reinject envelope off the SM: exactly one
 * yetty_client_input_mouse, tolerating (and draining) any trailing
 * bytes a future, larger struct revision might carry. */
static struct yetty_ycore_void_result terminal_reinject_consume_envelope(
    struct yetty_yterminal_terminal *terminal, struct yetty_ywire_wire_statemachine *sm)
{
    struct yetty_client_input_mouse msg;
    uint8_t *cursor = (uint8_t *)&msg;
    size_t have = 0;
    while (have < sizeof(msg)) {
        struct yetty_ycore_size_result read_res =
            yetty_ywire_wire_statemachine_read(sm, cursor + have, sizeof(msg) - have);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, read_res, "terminal_reinject: sm read");
        if (read_res.value == 0) {
            if (yetty_ywire_wire_statemachine_at_end(sm)) {
                if (have == 0) {
                    return YETTY_OK_VOID(); /* empty envelope: nothing to do */
                }
                return YETTY_ERR(yetty_ycore_void, "terminal_reinject: short payload at EOE");
            }
            yetty_yplatform_coro_yield();
            continue;
        }
        have += read_res.value;
    }
    /* Drain any excess to keep the stream aligned. */
    for (;;) {
        uint8_t scratch[256];
        struct yetty_ycore_size_result drain_res =
            yetty_ywire_wire_statemachine_read(sm, scratch, sizeof(scratch));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, drain_res, "terminal_reinject: sm drain");
        if (drain_res.value == 0) {
            if (yetty_ywire_wire_statemachine_at_end(sm)) {
                break;
            }
            yetty_yplatform_coro_yield();
        }
    }
    if (msg.magic != YETTY_CLIENT_INPUT_MOUSE_MAGIC) {
        return YETTY_ERR(yetty_ycore_void, "terminal_reinject: bad payload magic");
    }
    return terminal_reinject_apply(terminal, &msg);
}

static struct yetty_ycore_void_result terminal_reinject_process_input(
    void *userdata, struct yetty_ywire_wire_statemachine *sm)
{
    struct yetty_yterminal_terminal *terminal = userdata;
    for (;;) {
        struct yetty_ycore_void_result envelope_res =
            terminal_reinject_consume_envelope(terminal, sm);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, envelope_res, "terminal_reinject_process_input");
        yetty_yplatform_coro_yield();
    }
}

/*-------------------------------------------------------------------------
 * Exit-window input HOLD — YETTY_OSC_CS_CLIENT_INPUT_HOLD (no payload).
 *
 * A client sends this at the start of its teardown to ARM the host's input
 * barrier: the host holds the user's keystrokes host-side instead of
 * forwarding them into the stream the client's close drain would consume,
 * releasing them to the pane once the client is gone. The envelope carries
 * no bytes — it can only DEFER the host's own user input, never synthesize
 * any (the safe replacement for the removed 610014 handback). We just drain
 * the empty body and arm the barrier on channel_host.
 *-----------------------------------------------------------------------*/
/* Emit the no-payload HOLD-ACK back to the client — its arrival PROVES the
 * barrier is armed, so the client can safely detach its sinks. Written straight
 * to the PTY master (the host→client inline path, same as
 * terminal_dcs_emit_response), never the keystroke sink, so the barrier does
 * not hold it. */
static struct yetty_ycore_void_result terminal_emit_input_hold_ack(
    struct yetty_yterminal_terminal *terminal)
{
    struct yetty_ycore_buffer ack_env = {0};
    struct yetty_ycore_void_result emit_res =
        yetty_ywire_emit(YETTY_YWIRE_ENVELOPE_DCS, YETTY_OSC_CS_CLIENT_INPUT_HOLD_ACK,
                         /*has_args=*/1, /*compressed=*/0, NULL, 0, NULL, 0, &ack_env);
    if (YETTY_IS_ERR(emit_res)) {
        yetty_ycore_buffer_destroy(&ack_env);
        return YETTY_ERR(yetty_ycore_void, "terminal_emit_input_hold_ack: emit", emit_res);
    }
    /* Ship through the RELIABLE response-write path: it loops over short writes
     * and RETRIES zero-byte writes (a busy PTY master) instead of dropping the
     * tail — the client blocks on this ACK, so a partial write must not silently
     * succeed. */
    struct yetty_ycore_void_result write_res =
        terminal_dcs_emit_response(ack_env.data, ack_env.size, terminal);
    yetty_ycore_buffer_destroy(&ack_env);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, write_res,
                        "terminal_emit_input_hold_ack: response write");
    return YETTY_OK_VOID();
}

/* Stop the barrier-expiry timer (kept registered for reuse; deregistered +
 * destroyed at terminal teardown). Safe when no timer exists / already stopped. */
static struct yetty_ycore_void_result terminal_input_barrier_stop_timer(
    struct yetty_yterminal_terminal *terminal)
{
    if (!terminal->input_barrier_timer_ready) {
        return YETTY_OK_VOID();
    }
    struct yetty_yevent_event_loop *loop = terminal->context.yetty_context.event_loop;
    if (!loop) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result stop_res =
        loop->ops->stop_timer(loop, terminal->input_barrier_timer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, stop_res, "terminal_input_barrier_stop_timer");
    return YETTY_OK_VOID();
}

/* Timer wakeup: the barrier's host-side deadline elapsed while the client was
 * still (apparently) present — a wedged/dead client. Force-release the held
 * bytes to the pane so retention is bounded and the input is recovered, then
 * stop the timer (one-shot per arm). */
YETTY_EXTERNAL_CALLBACK
static struct yetty_ycore_int_result terminal_input_barrier_on_expiry(
    struct yetty_yevent_event_listener *listener, const struct yetty_yui_event *event)
{
    (void)event;
    struct yetty_yterminal_terminal *terminal =
        container_of(listener, struct yetty_yterminal_terminal, input_barrier_listener);
    if (terminal->channel_host) {
        struct yetty_ycore_buffer released = {0};
        int released_len =
            yetty_ywire_connection_input_barrier_release_forced(terminal->channel_host, &released);
        if (released_len > 0) {
            struct yetty_ycore_void_result flush_res =
                terminal_pty_write_all(terminal, released.data, released.size);
            if (YETTY_IS_ERR(flush_res)) {
                yetty_ycore_error_destroy(flush_res.error);
            }
        }
        yetty_ycore_buffer_destroy(&released);
    }
    /* External-callback boundary (timer wakeup): absorb the stop result. */
    struct yetty_ycore_void_result stop_res = terminal_input_barrier_stop_timer(terminal);
    if (YETTY_IS_ERR(stop_res)) {
        yetty_ycore_error_destroy(stop_res.error);
    }
    return YETTY_OK(yetty_ycore_int, 0);
}

/* (Lazily create + register, then) (re)start the barrier-expiry timer for
 * `deadline_ms`. Surfaces any timer error to the caller, which chooses to
 * degrade to the passive deadline (checked in _hold/_release) rather than fail
 * teardown. On a register failure the just-created timer is destroyed first. */
static struct yetty_ycore_void_result terminal_input_barrier_start_timer(
    struct yetty_yterminal_terminal *terminal, int deadline_ms)
{
    struct yetty_yevent_event_loop *loop = terminal->context.yetty_context.event_loop;
    if (!loop) {
        return YETTY_ERR(yetty_ycore_void, "terminal_input_barrier_start_timer: no event loop");
    }
    if (!terminal->input_barrier_timer_ready) {
        struct yetty_yevent_timer_id_result id_res = loop->ops->create_timer(loop);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, id_res,
                            "terminal_input_barrier_start_timer: create_timer");
        terminal->input_barrier_timer = id_res.value;
        terminal->input_barrier_listener.handler = terminal_input_barrier_on_expiry;
        struct yetty_ycore_void_result reg_res = loop->ops->register_timer_listener(
            loop, terminal->input_barrier_timer, &terminal->input_barrier_listener);
        if (YETTY_IS_ERR(reg_res)) {
            struct yetty_ycore_void_result destroy_res =
                loop->ops->destroy_timer(loop, terminal->input_barrier_timer);
            if (YETTY_IS_ERR(destroy_res)) {
                yetty_ycore_error_destroy(destroy_res.error);
            }
            return YETTY_ERR(yetty_ycore_void,
                             "terminal_input_barrier_start_timer: register_timer_listener",
                             reg_res);
        }
        terminal->input_barrier_timer_ready = 1;
    }
    /* Restart cleanly so a re-arm resets the countdown. */
    struct yetty_ycore_void_result stop_res = terminal_input_barrier_stop_timer(terminal);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, stop_res, "terminal_input_barrier_start_timer: stop");
    struct yetty_ycore_void_result cfg_res =
        loop->ops->config_timer(loop, terminal->input_barrier_timer, deadline_ms);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, cfg_res, "terminal_input_barrier_start_timer: config");
    struct yetty_ycore_void_result start_res =
        loop->ops->start_timer(loop, terminal->input_barrier_timer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, start_res, "terminal_input_barrier_start_timer: start");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result terminal_input_hold_process_input(
    void *userdata, struct yetty_ywire_wire_statemachine *sm)
{
    struct yetty_yterminal_terminal *terminal = *(struct yetty_yterminal_terminal **)userdata;
    for (;;) {
        /* Drain the (empty) envelope body to stay frame-aligned. */
        for (;;) {
            uint8_t scratch[64];
            struct yetty_ycore_size_result read_res =
                yetty_ywire_wire_statemachine_read(sm, scratch, sizeof(scratch));
            YETTY_RETURN_IF_ERR(yetty_ycore_void, read_res, "terminal_input_hold: sm read");
            if (read_res.value == 0) {
                if (yetty_ywire_wire_statemachine_at_end(sm)) {
                    break;
                }
                yetty_yplatform_coro_yield();
                continue;
            }
        }
        if (terminal->channel_host) {
            /* Bounded host-side retention: past this deadline the barrier stops
             * holding and releases, so a client that arms then crashes/wedges
             * cannot make the host swallow the keyboard forever. Comfortably
             * covers a healthy teardown (client ACK-wait + close drain are each
             * bounded well under a second); the healthy path releases far
             * earlier, on the client-gone signal. */
            enum { INPUT_BARRIER_DEADLINE_MS = 3000 };
            /* Arm FIRST, then ACK: once armed every keystroke is held, so the
             * ACK the client waits on can only ever be followed by held input,
             * never forwarded input. */
            yetty_ywire_connection_input_barrier_arm(terminal->channel_host,
                                                     INPUT_BARRIER_DEADLINE_MS);
            /* Confirm the arm to the client (it blocks on this before detaching
             * its sinks). Bail on failure — a failed ACK write means the PTY is
             * gone, and the caller surfaces it. */
            struct yetty_ycore_void_result ack_res = terminal_emit_input_hold_ack(terminal);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, ack_res, "terminal_input_hold: emit HOLD-ACK");
            /* ACTIVELY enforce the same deadline with a host-loop timer, so a
             * wedged client that sends no further output and receives no further
             * input still has its held bytes released on time (the passive check
             * only fires when a later event happens to observe expiry). */
            struct yetty_ycore_void_result timer_res =
                terminal_input_barrier_start_timer(terminal, INPUT_BARRIER_DEADLINE_MS);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, timer_res,
                                "terminal_input_hold: arm barrier expiry timer");
        }
        yetty_yplatform_coro_yield();
    }
}

/*-------------------------------------------------------------------------
 * Content reservation — YETTY_OSC_CS_CONTENT_RECT / _INSET.
 *
 * A client places the terminal content surface on part of the pane (a
 * docked status bar / HUD keeps the rest) by sending a content rect
 * (position + edge-anchored size) or the legacy per-edge insets. We own
 * the real cell metrics, so we convert the resolved rect → whole rows/cols
 * and shrink the actual libvterm surface through the normal resize path:
 * text reflows into the content rect, the PTY winsize shrinks, the child
 * sees SIGWINCH, and the grid figure renders inside the same rect
 * (vterm.c). The reserved area is then free for the client's own overlay
 * figure.
 *-----------------------------------------------------------------------*/

/* Store a content spec (pane-local px, edge-anchored extents — see
 * yetty_content_rect in client-input.h) and reflow the grid at the current
 * pane size so the libvterm surface tracks the new content rect. */
static struct yetty_ycore_void_result terminal_content_spec_apply(
    struct yetty_yterminal_terminal *terminal, float spec_x, float spec_y, float spec_w,
    float spec_h)
{
    if (!terminal->grid) {
        return YETTY_OK_VOID();
    }
    /* Clamp a negative origin to zero — a malformed client must never push
     * the content rect out of the pane. */
    terminal->content_spec_x = spec_x > 0.0f ? spec_x : 0.0f;
    terminal->content_spec_y = spec_y > 0.0f ? spec_y : 0.0f;
    terminal->content_spec_w = spec_w;
    terminal->content_spec_h = spec_h;
    ydebug("terminal: content spec x=%.0f y=%.0f w=%.0f h=%.0f", terminal->content_spec_x,
           terminal->content_spec_y, spec_w, spec_h);

    /* Reflow at the last applied pane size; the grid figure picks the new
     * content rect up on its next render. applied_w/h is 0 before the first
     * layout — the first RESIZE then applies the stored spec for free. */
    if (terminal->applied_w > 0.0f && terminal->applied_h > 0.0f) {
        struct yetty_ycore_void_result gr =
            terminal_apply_pane_geometry(terminal, terminal->applied_w, terminal->applied_h);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, gr, "terminal_content_spec_apply: reflow");
    }
    terminal->context.yetty_context.event_loop->ops->request_render(
        terminal->context.yetty_context.event_loop);
    return YETTY_OK_VOID();
}

/* Legacy per-edge insets are the anchored-spec special case
 * {left, top, -right, -bottom}: all-zero insets → all-zero spec → full
 * pane. Negative insets clamp to zero before the sign flip. */
static struct yetty_ycore_void_result terminal_content_inset_apply(
    struct yetty_yterminal_terminal *terminal, const struct yetty_content_inset *inset)
{
    float top = inset->top > 0.0f ? inset->top : 0.0f;
    float right = inset->right > 0.0f ? inset->right : 0.0f;
    float bottom = inset->bottom > 0.0f ? inset->bottom : 0.0f;
    float left = inset->left > 0.0f ? inset->left : 0.0f;
    return terminal_content_spec_apply(terminal, left, top, -right, -bottom);
}

/* Read one content-inset envelope off the SM: exactly one
 * yetty_content_inset, draining any trailing bytes a future revision adds. */
static struct yetty_ycore_void_result terminal_content_inset_consume_envelope(
    struct yetty_yterminal_terminal *terminal, struct yetty_ywire_wire_statemachine *sm)
{
    struct yetty_content_inset msg;
    uint8_t *cursor = (uint8_t *)&msg;
    size_t have = 0;
    while (have < sizeof(msg)) {
        struct yetty_ycore_size_result read_res =
            yetty_ywire_wire_statemachine_read(sm, cursor + have, sizeof(msg) - have);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, read_res, "terminal_content_inset: sm read");
        if (read_res.value == 0) {
            if (yetty_ywire_wire_statemachine_at_end(sm)) {
                if (have == 0) {
                    return YETTY_OK_VOID(); /* empty envelope: nothing to do */
                }
                return YETTY_ERR(yetty_ycore_void, "terminal_content_inset: short payload at EOE");
            }
            yetty_yplatform_coro_yield();
            continue;
        }
        have += read_res.value;
    }
    /* Drain any excess to keep the stream aligned. */
    for (;;) {
        uint8_t scratch[256];
        struct yetty_ycore_size_result drain_res =
            yetty_ywire_wire_statemachine_read(sm, scratch, sizeof(scratch));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, drain_res, "terminal_content_inset: sm drain");
        if (drain_res.value == 0) {
            if (yetty_ywire_wire_statemachine_at_end(sm)) {
                break;
            }
            yetty_yplatform_coro_yield();
        }
    }
    if (msg.magic != YETTY_CONTENT_INSET_MAGIC) {
        return YETTY_ERR(yetty_ycore_void, "terminal_content_inset: bad payload magic");
    }
    return terminal_content_inset_apply(terminal, &msg);
}

static struct yetty_ycore_void_result terminal_content_inset_process_input(
    void *userdata, struct yetty_ywire_wire_statemachine *sm)
{
    /* userdata is &terminal->inset_handler_self (a distinct pointer so the SM
     * spawns a coroutine for THIS fn rather than sharing the reinject coro,
     * which the bare-terminal userdata already owns). Recover the terminal. */
    struct yetty_yterminal_terminal *terminal = *(struct yetty_yterminal_terminal **)userdata;
    for (;;) {
        struct yetty_ycore_void_result envelope_res =
            terminal_content_inset_consume_envelope(terminal, sm);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, envelope_res, "terminal_content_inset_process_input");
        yetty_yplatform_coro_yield();
    }
}

/*-----------------------------------------------------------------------
 * Pane-wide input subscription — YETTY_OSC_CS_CLIENT_INPUT_SUB.
 *
 * Bits honoured here. RESIZE: a client that lays out in logical pixels
 * asks to be told the pane size AND the display's HiDPI factor, without
 * opting into mouse or key forwarding. KEY_FANOUT: the client asks that
 * keystrokes be consumed and fanned out as structured
 * CLIENT_INPUT_FIGURE_KEY envelopes once one of its figures is
 * click-focused (without the opt-in, keys stay on the raw PTY channel).
 * MOUSE_CLICK / MOUSE_MOVE / MOUSE_WHEEL: pane-wide mouse forwarding —
 * events that no yscene figure owns are emitted as SC_CLIENT_INPUT_MOUSE
 * (figure_id 0, pane-local pixels); this is the input path for a pure
 * drawable-tree surface, which has no figures for the DEC-mode hit-test.
 * Each envelope declares the full desired subscription state. The classic
 * figure-tagged mouse/?1502 path stays driven by the DEC private modes,
 * which is where every existing figure subscriber sets them.
 *----------------------------------------------------------------------*/

/* Read one subscription envelope off the SM and latch the geometry bit.
 * Emits the current size immediately on the rising edge so the client can
 * lay out without waiting for the next pane resize. */
static struct yetty_ycore_void_result terminal_client_sub_consume_envelope(
    struct yetty_yterminal_terminal *terminal, struct yetty_ywire_wire_statemachine *sm)
{
    struct yetty_client_input_sub msg;
    uint8_t *cursor = (uint8_t *)&msg;
    size_t have = 0;
    while (have < sizeof(msg)) {
        struct yetty_ycore_size_result read_res =
            yetty_ywire_wire_statemachine_read(sm, cursor + have, sizeof(msg) - have);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, read_res, "terminal_client_sub: sm read");
        if (read_res.value == 0) {
            if (yetty_ywire_wire_statemachine_at_end(sm)) {
                if (have == 0) {
                    return YETTY_OK_VOID(); /* empty envelope: nothing to do */
                }
                return YETTY_ERR(yetty_ycore_void, "terminal_client_sub: short payload at EOE");
            }
            yetty_yplatform_coro_yield();
            continue;
        }
        have += read_res.value;
    }
    /* Drain any excess to keep the stream aligned. */
    for (;;) {
        uint8_t scratch[256];
        struct yetty_ycore_size_result drain_res =
            yetty_ywire_wire_statemachine_read(sm, scratch, sizeof(scratch));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, drain_res, "terminal_client_sub: sm drain");
        if (drain_res.value == 0) {
            if (yetty_ywire_wire_statemachine_at_end(sm)) {
                break;
            }
            yetty_yplatform_coro_yield();
        }
    }
    if (msg.magic != YETTY_CLIENT_INPUT_SUB_MAGIC) {
        return YETTY_ERR(yetty_ycore_void, "terminal_client_sub: bad payload magic");
    }
    int want_resize = (msg.flags & YETTY_CLIENT_INPUT_SUB_RESIZE) != 0;
    int was_subscribed = terminal->resize_subscribed;
    terminal->resize_subscribed = want_resize;
    terminal->figure_key_optin = (msg.flags & YETTY_CLIENT_INPUT_SUB_KEY_FANOUT) != 0;
    terminal->pane_mouse_click_subscribed = (msg.flags & YETTY_CLIENT_INPUT_SUB_MOUSE_CLICK) != 0;
    terminal->pane_mouse_move_subscribed = (msg.flags & YETTY_CLIENT_INPUT_SUB_MOUSE_MOVE) != 0;
    terminal->pane_mouse_wheel_subscribed = (msg.flags & YETTY_CLIENT_INPUT_SUB_MOUSE_WHEEL) != 0;
    /* The subscription doubles as the RPC-less client-presence marker for
     * the exit-window input barrier: a DCS-only client (ygui2) opens no
     * dynamic channel, so without this the barrier would disarm on the
     * same feed that processed its HOLD. flags=0 (the framework's detach,
     * sent AFTER it stopped reading stdin) marks the client gone. */
    if (terminal->channel_host) {
        yetty_ywire_connection_input_barrier_client_present(terminal->channel_host,
                                                            msg.flags != 0u);
    }
    ydebug("terminal: client sub flags=0x%x resize=%d figure_key=%d pane_mouse=%d/%d/%d", msg.flags,
           want_resize, terminal->figure_key_optin, terminal->pane_mouse_click_subscribed,
           terminal->pane_mouse_move_subscribed, terminal->pane_mouse_wheel_subscribed);
    if (want_resize && !was_subscribed && terminal->applied_w > 0.0f &&
        terminal->applied_h > 0.0f) {
        /* Pane-wide subscription → pane-wide envelope. */
        struct yetty_ycore_void_result rr =
            terminal_emit_pane_resize(terminal, terminal->applied_w, terminal->applied_h);
        if (YETTY_IS_ERR(rr)) {
            yetty_ycore_error_destroy(rr.error);
        }
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result terminal_client_sub_process_input(
    void *userdata, struct yetty_ywire_wire_statemachine *sm)
{
    struct yetty_yterminal_terminal *terminal = *(struct yetty_yterminal_terminal **)userdata;
    for (;;) {
        struct yetty_ycore_void_result envelope_res =
            terminal_client_sub_consume_envelope(terminal, sm);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, envelope_res, "terminal_client_sub_process_input");
        yetty_yplatform_coro_yield();
    }
}

/* Read one content-rect envelope off the SM: exactly one yetty_content_rect,
 * draining any trailing bytes a future revision adds. */
static struct yetty_ycore_void_result terminal_content_rect_consume_envelope(
    struct yetty_yterminal_terminal *terminal, struct yetty_ywire_wire_statemachine *sm)
{
    struct yetty_content_rect msg;
    uint8_t *cursor = (uint8_t *)&msg;
    size_t have = 0;
    while (have < sizeof(msg)) {
        struct yetty_ycore_size_result read_res =
            yetty_ywire_wire_statemachine_read(sm, cursor + have, sizeof(msg) - have);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, read_res, "terminal_content_rect: sm read");
        if (read_res.value == 0) {
            if (yetty_ywire_wire_statemachine_at_end(sm)) {
                if (have == 0) {
                    return YETTY_OK_VOID(); /* empty envelope: nothing to do */
                }
                return YETTY_ERR(yetty_ycore_void, "terminal_content_rect: short payload at EOE");
            }
            yetty_yplatform_coro_yield();
            continue;
        }
        have += read_res.value;
    }
    /* Drain any excess to keep the stream aligned. */
    for (;;) {
        uint8_t scratch[256];
        struct yetty_ycore_size_result drain_res =
            yetty_ywire_wire_statemachine_read(sm, scratch, sizeof(scratch));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, drain_res, "terminal_content_rect: sm drain");
        if (drain_res.value == 0) {
            if (yetty_ywire_wire_statemachine_at_end(sm)) {
                break;
            }
            yetty_yplatform_coro_yield();
        }
    }
    if (msg.magic != YETTY_CONTENT_RECT_MAGIC) {
        return YETTY_ERR(yetty_ycore_void, "terminal_content_rect: bad payload magic");
    }
    return terminal_content_spec_apply(terminal, msg.x, msg.y, msg.width, msg.height);
}

static struct yetty_ycore_void_result terminal_content_rect_process_input(
    void *userdata, struct yetty_ywire_wire_statemachine *sm)
{
    /* userdata is &terminal->content_rect_handler_self — same distinct-pointer
     * coroutine trick as the inset handler above. */
    struct yetty_yterminal_terminal *terminal = *(struct yetty_yterminal_terminal **)userdata;
    for (;;) {
        struct yetty_ycore_void_result envelope_res =
            terminal_content_rect_consume_envelope(terminal, sm);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, envelope_res, "terminal_content_rect_process_input");
        yetty_yplatform_coro_yield();
    }
}

/* Shader-effect OSC codes (mirror the poc protocol):
 *   ESC ] 66666{7,8,9} ; INDEX:P0:P1:P2:P3:P4:P5 BEL
 * 666667 = pre-effect (glyph, not yet applied), 666668 = post-color effect,
 * 666669 = coordinate distortion (complex pass). INDEX 0 disables the class. */
enum {
    YETTY_OSC_CS_EFFECT_PRE = 666667,
    YETTY_OSC_CS_EFFECT_POST = 666668,
    YETTY_OSC_CS_EFFECT_COORD = 666669,
};

/* Read one effect envelope's text body, parse "INDEX:P0:...:P5", and apply. */
static struct yetty_ycore_void_result terminal_effect_consume_envelope(
    struct yetty_yterminal_terminal *terminal, struct yetty_ywire_wire_statemachine *sm)
{
    int code = yetty_ywire_wire_statemachine_code(sm);
    char payload[128];
    size_t len = 0;
    for (;;) {
        uint8_t *dst = (uint8_t *)payload + len;
        size_t room = (len < sizeof(payload) - 1) ? sizeof(payload) - 1 - len : 0;
        uint8_t scratch[64];
        struct yetty_ycore_size_result read_res =
            room ? yetty_ywire_wire_statemachine_read(sm, dst, room)
                 : yetty_ywire_wire_statemachine_read(sm, scratch, sizeof(scratch));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, read_res, "terminal_effect: sm read");
        if (read_res.value == 0) {
            if (yetty_ywire_wire_statemachine_at_end(sm)) {
                break;
            }
            yetty_yplatform_coro_yield();
            continue;
        }
        if (room) {
            len += read_res.value;
        }
    }
    payload[len] = '\0';
    ydebug("terminal_effect: code=%d raw_body_len=%zu body='%s'", code, len, payload);

    /* Parse index + up to 6 colon-separated float params (missing → 0). */
    uint32_t index = 0;
    float params[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    {
        char *end = NULL;
        index = (uint32_t)strtoul(payload, &end, 10);
        for (int i = 0; i < 6 && end && *end == ':'; i++) {
            params[i] = strtof(end + 1, &end);
        }
    }

    if (code == YETTY_OSC_CS_EFFECT_POST) {
        struct yetty_ycore_void_result sr =
            yetty_yvterm_vterm_set_post_effect(terminal->grid, index, params[0], params[1],
                                               params[2], params[3], params[4], params[5]);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "terminal_effect: set_post_effect");
        ydebug("terminal_effect: post-effect index=%u p0=%.3f", index, (double)params[0]);
    } else if (code == YETTY_OSC_CS_EFFECT_COORD) {
        struct yetty_ycore_void_result sr =
            yetty_yvterm_vterm_set_coord_effect(terminal->grid, index, params[0], params[1],
                                                params[2], params[3], params[4], params[5]);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "terminal_effect: set_coord_effect");
        ydebug("terminal_effect: coord-effect index=%u p0=%.3f", index, (double)params[0]);
    } else {
        /* Pre-effect (glyph substitution) not yet applied. */
        ydebug("terminal_effect: OSC %d index=%u (pre-effect not yet applied)", code, index);
    }
    terminal->context.yetty_context.event_loop->ops->request_render(
        terminal->context.yetty_context.event_loop);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result terminal_effect_process_input(
    void *userdata, struct yetty_ywire_wire_statemachine *sm)
{
    struct yetty_yterminal_terminal *terminal = *(struct yetty_yterminal_terminal **)userdata;
    for (;;) {
        struct yetty_ycore_void_result r = terminal_effect_consume_envelope(terminal, sm);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "terminal_effect_process_input");
        yetty_yplatform_coro_yield();
    }
}

enum { TERMINAL_YDRAW_GROUP_DEPTH_MAX = 8 };

/* Bounds on wire-supplied vertical extents. RESERVE_MAX_PX caps the declared
 * viewport (and any content bottom) so the reserve row count stays exactly
 * representable and the newline advance stays finite; RESERVE_MAX_ROWS is a
 * second bound after the cell division (a 1px cell height must not turn a
 * legal pixel span into millions of rows). */
enum {
    TERMINAL_YDRAW_RESERVE_MAX_PX = 1 << 24,
    TERMINAL_YDRAW_RESERVE_MAX_ROWS = 1 << 16,
};

/* Shared ingest bookkeeping for one envelope's worth of inbound rich
 * content. Two producers feed it: the wire-streamed YDRAW_BIN path
 * (terminal_ydraw_consume_bin) and the in-process serialized path used by
 * the terminal-side file renderer (yetty_yterminal_mime_ingest_serialized). */
struct terminal_ydraw_ingest_state {
    uint32_t cursor_row;
    struct yetty_ycore_pixel_size text_cell;
    /* The grid's committed rich density product (logical → current-fb:
     * display density x structural cell zoom), snapshotted with the cell
     * metrics so this envelope's reserve/span conversions use exactly
     * the value retirement judges the same content by. */
    float rich_density;
    /* Bottom extent of this envelope's drawn content (px, envelope-local,
     * from the insert row's top). After ingestion the cursor is advanced
     * past it so the next envelope's content (the next plot, the next PDF
     * page, …) lands below instead of on top. Covers complexes, SDF prims,
     * and text alike — ycat ships one envelope per PDF page with y=0
     * origin, so without this the pages would all stack at the same row. */
    float content_bottom_px;
    uint32_t ingested_records;
    uint32_t ingested_complexes;
    /* Nonzero while walking a REOPENED group's replacement body: replacement
     * content keeps the block's original reservation, so its bounds must not
     * feed the reserve computation. */
    uint32_t reopen_depth;
    /* Set once the envelope produced NEW rolling content (anonymous records
     * or a newly created group). A mutation-only envelope (reopens/deletes)
     * reserves no rows and never advances the cursor. */
    int new_content;
    /* GROUP nesting bound for the nested-body walk. */
    uint32_t group_depth;
    /* Folded key of each open scope: scope_keys[0] is the root (empty path);
     * scope_keys[d] is the key of the group open at depth d. A group / delete /
     * update target key at the current scope is
     * yetty_yvterm_group_key_fold(scope_keys[group_depth], local_id). This is
     * how nested group-id PATHS collapse into the 64-bit binding key. */
    uint64_t scope_keys[TERMINAL_YDRAW_GROUP_DEPTH_MAX + 1u];
    /* CMD_NODE_ID latch: the id assigned to the NEXT COMPLEX record (how a
     * complex carries its own id — see cmds.h). Survives interleaved
     * primitives (a plot's emitter surrounds its complex with label prims),
     * is consumed by the first complex, and is dropped at a GROUP
     * declaration or the batch end. */
    uint32_t pending_node_id;
    int pending_node_id_valid;
    /* CMD_PATH latch: the ABSOLUTE ancestor-scope key for the NEXT
     * update/delete (the command's own id is the final path component).
     * Consumed by exactly one command; any other record drops it. */
    uint64_t pending_path_key;
    int pending_path_valid;
    /* CMD_RESERVE: declared insertion span in pixels (0 = derive from the
     * content bottom as always). Content taller than a declared span never
     * extends the reservation — the projection clips it. */
    float declared_span_px;
};

/* Defined below — mutually recursive with terminal_ydraw_ingest_record via
 * nested GROUP bodies. */
static void terminal_ydraw_walk_nested(struct yetty_yterminal_terminal *terminal,
                                       struct terminal_ydraw_ingest_state *state,
                                       const uint8_t *bytes, size_t byte_count);
static void terminal_ydraw_route_delete(struct yetty_yterminal_terminal *terminal,
                                        struct terminal_ydraw_ingest_state *state, uint32_t id);

/* Anchor rich content on the cursor's current visible line in yvterm's OWN
 * grid. The grid owns a primitive/complex list per line on its scroll
 * ring, so whatever sits at that line scrolls with the text for free — no
 * separate figure, no rolling-row bookkeeping. Complexes render via vterm's
 * rich pass; raw SDF / text records are stored on the line for the SDF pass. */
static struct yetty_ycore_void_result terminal_ydraw_ingest_begin(
    struct yetty_yterminal_terminal *terminal, struct terminal_ydraw_ingest_state *state)
{
    memset(state, 0, sizeof(*state));
    uint32_t cursor_col = 0, cursor_visible = 0;
    struct yetty_ycore_void_result cursor_res =
        yetty_yvterm_vterm_cursor(terminal->grid, &state->cursor_row, &cursor_col, &cursor_visible);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, cursor_res, "terminal_ydraw ingest: cursor");
    struct pixel_size_result text_cell_res = yetty_yvterm_vterm_cell_size(terminal->grid);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, text_cell_res, "terminal_ydraw ingest: cell size");
    state->text_cell = text_cell_res.value;
    state->rich_density = 1.0f;
    struct yetty_ycore_void_result density_res =
        yetty_yvterm_vterm_rich_density(terminal->grid, &state->rich_density);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, density_res, "terminal_ydraw ingest: rich density");
    return YETTY_OK_VOID();
}

/* Complex-tier classifier for the drawable paths: SDF is probed FIRST with
 * the HAS_ID bit masked — an id-carrying SDF prim has its top bit set and
 * would otherwise be mistaken for a complex (garbage bounds, doomed factory
 * calls, a phantom retained envelope). */
static int terminal_ydraw_record_is_complex(uint32_t type)
{
    if (yetty_ysdf_primitive_size(type & ~YETTY_YDRAW_HAS_ID_FLAG) > 0u) {
        return 0;
    }
    return yetty_ydraw_is_complex(type);
}

/* Effective bottom (px) of one insert's supplied subtree. This is the
 * pre-scan that makes a REPLACING insert command-local and atomic: computed
 * BEFORE any mutation, so a replacement that cannot fit its target's row
 * span is skipped whole and the previous content stays intact. Structural
 * markers carry no geometry; nested GROUP bodies recurse. Parse trouble just
 * ends the scan — framing is the walk's concern. */
static float terminal_ydraw_subtree_bottom_px(struct yetty_yterminal_terminal *terminal,
                                              const uint8_t *bytes, size_t byte_count,
                                              uint32_t depth)
{
    float bottom = 0.0f;
    if (depth > TERMINAL_YDRAW_GROUP_DEPTH_MAX) {
        return bottom;
    }
    while (byte_count >= sizeof(uint32_t)) {
        struct yetty_ydraw_command command;
        struct yetty_ycore_size_result step = yetty_ydraw_drawable_command_parse(
            terminal->ydraw_registry, bytes, (uint32_t)byte_count, &command);
        if (YETTY_IS_ERR(step)) {
            yetty_ycore_error_destroy(step.error);
            break;
        }
        if (step.value == 0 || step.value > byte_count || (step.value % sizeof(uint32_t)) != 0) {
            break;
        }
        if (command.kind == YETTY_YDRAW_COMMAND_ADD && command.entry.data) {
            const uint32_t *data = command.entry.data;
            if (data[0] == YETTY_YDRAW_CMD_GROUP && step.value >= 12u) {
                uint32_t payload_size = data[2];
                size_t payload_limit = step.value - 12u;
                float nested = terminal_ydraw_subtree_bottom_px(
                    terminal, (const uint8_t *)(data + 3),
                    payload_size <= payload_limit ? payload_size : payload_limit, depth + 1u);
                if (nested > bottom) {
                    bottom = nested;
                }
            } else if (data[0] != YETTY_YDRAW_RESOURCE_FONT && data[0] != YETTY_YDRAW_CMD_ZERO &&
                       data[0] != YETTY_YDRAW_CMD_GROUP_REF && data[0] != YETTY_YDRAW_CMD_PAINT_Z &&
                       data[0] != YETTY_YDRAW_CMD_PAINT_Z_END &&
                       data[0] != YETTY_YDRAW_CMD_NODE_ID) {
                struct rectangle_result aabb;
                int have_aabb = 0;
                if (terminal_ydraw_record_is_complex(data[0])) {
                    aabb = yetty_ydraw_complex_record_aabb(data);
                    have_aabb = 1;
                } else if (command.entry.ops && command.entry.ops->aabb) {
                    aabb = command.entry.ops->aabb(data);
                    have_aabb = 1;
                }
                if (have_aabb) {
                    if (YETTY_IS_OK(aabb)) {
                        if (aabb.value.max.y > bottom) {
                            bottom = aabb.value.max.y;
                        }
                    } else {
                        yetty_ycore_error_destroy(aabb.error);
                    }
                }
            }
        }
        bytes += step.value;
        byte_count -= step.value;
    }
    return bottom;
}

/* Place one ADD record (complex or raw SDF/text/font) on the anchor line.
 * Per-record failures are absorbed (skip the record, keep the envelope). */
static void terminal_ydraw_ingest_record(struct yetty_yterminal_terminal *terminal,
                                         struct terminal_ydraw_ingest_state *state,
                                         const struct yetty_ydraw_drawable_list_entry *entry,
                                         size_t size)
{
    const uint32_t *data = entry->data;
    /* Structural commands dispatch by EXACT type ahead of the drawable
     * paths — CMD_GROUP (0x80000002) would otherwise pass the is_complex
     * range test and be mistaken for a figure envelope (garbage bounds, a
     * phantom retained envelope, doomed materialize retries). */
    if (data[0] == YETTY_YDRAW_CMD_NODE_ID && size >= 8u) {
        state->pending_node_id = data[1];
        state->pending_node_id_valid = 1;
        state->ingested_records++;
        return;
    }
    if (data[0] == YETTY_YDRAW_CMD_PATH && size >= 12u) {
        /* Fold the absolute path (outermost first, from ROOT) into the
         * ancestor-scope key for the NEXT update/delete. */
        uint32_t path_count = data[1];
        if (path_count == 0u || path_count > YETTY_YDRAW_CMD_PATH_MAX_IDS ||
            (size_t)(8u + path_count * 4u) > size) {
            return; /* malformed — command-local skip */
        }
        uint64_t key = YETTY_YVTERM_GROUP_KEY_ROOT;
        for (uint32_t index = 0; index < path_count; ++index) {
            key = yetty_yvterm_group_key_fold(key, data[2u + index]);
        }
        state->pending_path_key = key;
        state->pending_path_valid = 1;
        ydebug("ydraw ingest: CMD_PATH depth=%u key=%016llx", path_count, (unsigned long long)key);
        state->ingested_records++;
        return;
    }
    /* The path latch survives INTO a GROUP declaration (the addressed-
     * insert form below); any other record drops it — it addresses the
     * NEXT command only. */
    int path_latch_valid = state->pending_path_valid;
    uint64_t path_latch_key = state->pending_path_key;
    state->pending_path_valid = 0;
    if (data[0] == YETTY_YDRAW_CMD_GROUP) {
        /* A pending node id does not cross a structural GROUP declaration. */
        state->pending_node_id_valid = 0;
        if (size < 12u) {
            return;
        }
        uint32_t group_id = data[1];
        uint32_t payload_size = data[2];
        if (state->group_depth >= TERMINAL_YDRAW_GROUP_DEPTH_MAX) {
            ydebug("ydraw ingest: GROUP id=%u nested too deep — skipped", group_id);
            return;
        }
        /* Ancestor scope: a CMD_PATH latch at top level addresses the GROUP
         * at any depth — the addressed-insert form (E5's reopen half): the
         * command's own id is the final path component, and a LIVE key
         * reopens in place exactly like a depth-1 reopen. A fresh key under
         * a latched path is the missing-descendant CREATE, whose placement
         * rules (join the parent's insertion) are not implemented yet —
         * skipped explicitly rather than misplaced. */
        uint64_t group_scope = state->scope_keys[state->group_depth];
        int used_path_latch = 0;
        if (state->group_depth == 0 && path_latch_valid) {
            group_scope = path_latch_key;
            used_path_latch = 1;
        }
        uint64_t group_key = yetty_yvterm_group_key_fold(group_scope, group_id);
        if (used_path_latch) {
            uint32_t live_probe = 0;
            struct yetty_ycore_uint32_result probe_res =
                yetty_yvterm_vterm_rich_group_query(terminal->grid, group_key, &live_probe);
            if (YETTY_IS_ERR(probe_res)) {
                yetty_ycore_error_destroy(probe_res.error);
                return;
            }
            if (!probe_res.value) {
                ydebug("ydraw ingest: GROUP id=%u at path — create-at-path not supported, skipped",
                       group_id);
                return;
            }
        }
        /* Command-local atomicity for a REPLACING insert: if the path is
         * live, the supplied subtree must fit the target's existing row span
         * — checked BEFORE any mutation, so an oversized replacement skips
         * whole and the previous content stays intact. */
        uint32_t live_span = 0;
        struct yetty_ycore_uint32_result live_res =
            yetty_yvterm_vterm_rich_group_query(terminal->grid, group_key, &live_span);
        if (YETTY_IS_ERR(live_res)) {
            yetty_ycore_error_destroy(live_res.error);
        } else if (live_res.value && state->text_cell.height > 0) {
            float span_px = (float)(live_span ? live_span : 1u) * (float)state->text_cell.height;
            /* Producer-logical bottom vs framebuffer span — scale by the
             * committed rich density product (same unit rule as the
             * reserve path). */
            float density = state->rich_density > 0.0f ? state->rich_density : 1.0f;
            float replacement_bottom =
                density * terminal_ydraw_subtree_bottom_px(
                              terminal, (const uint8_t *)(data + 3),
                              payload_size <= (size - 12u) ? payload_size : (size - 12u),
                              state->group_depth);
            if (replacement_bottom > span_px) {
                ydebug("ydraw ingest: GROUP id=%u replacement exceeds row span — skipped",
                       group_id);
                return;
            }
        }
        struct yetty_ycore_uint32_result open_res =
            yetty_yvterm_vterm_rich_group_open(terminal->grid, state->cursor_row, group_key);
        if (YETTY_IS_ERR(open_res)) {
            ydebug("ydraw ingest: GROUP id=%u open failed: %s", group_id, open_res.error.msg);
            yetty_ycore_error_destroy(open_res.error);
            return;
        }
        int reopened = open_res.value != 0;
        state->ingested_records++;
        if (reopened) {
            state->reopen_depth++;
        } else {
            state->new_content = 1;
        }
        state->scope_keys[state->group_depth + 1u] = group_key;
        state->group_depth++;
        terminal_ydraw_walk_nested(terminal, state, (const uint8_t *)(data + 3),
                                   payload_size <= (size - 12u) ? payload_size : (size - 12u));
        state->group_depth--;
        if (reopened) {
            state->reopen_depth--;
        }
        struct yetty_ycore_void_result close_res =
            yetty_yvterm_vterm_rich_group_close(terminal->grid);
        if (YETTY_IS_ERR(close_res)) {
            yetty_ycore_error_destroy(close_res.error);
        }
        return;
    }
    if (data[0] == YETTY_YDRAW_CMD_ZERO && size >= 8u && data[1] == 0u) {
        if (state->group_depth > 0u) {
            /* A full clear inside an open group scope would wipe the very
             * block the scope appends into — command-local skip. */
            ydebug("ydraw ingest: CMD_ZERO nested in a GROUP — skipped");
            state->ingested_records++;
            return;
        }
        /* Full-canvas clear embedded in the body (ygui-style full redraw:
         * one CMD_ZERO + the new frame). Rings, cache and archive watermark
         * — same reach as the YDRAW_CLEAR envelope. */
        struct yetty_ycore_void_result zero_res = yetty_yvterm_vterm_clear_rich_all(terminal->grid);
        if (YETTY_IS_ERR(zero_res)) {
            yetty_ycore_error_destroy(zero_res.error);
        }
        state->ingested_records++;
        return;
    }
    if (data[0] == YETTY_YDRAW_CMD_GROUP_REF) {
        /* Shared/multi-parent references would turn the group tree into a
         * graph; explicitly unsupported rather than silently ignored. */
        ydebug("ydraw ingest: CMD_GROUP_REF unsupported — skipped");
        state->ingested_records++;
        return;
    }
    if (data[0] == YETTY_YDRAW_CMD_RESERVE && size >= 8u) {
        /* Declared viewport span: fixes the batch's reservation regardless of
         * how tall the content is. Clamped — the wire word is an arbitrary
         * u32, and an unbounded value would round-trip through float above
         * integer precision and turn the reserve advance into a runaway
         * newline loop. RESERVE only DECLARES the span; it does not mark new
         * content — a batch whose only novelty is a RESERVE is mutation-only
         * and takes no placement (the contract's new content is new roots +
         * anonymous drawables). */
        uint32_t declared_px = data[1];
        if (declared_px > TERMINAL_YDRAW_RESERVE_MAX_PX) {
            ydebug("ydraw ingest: RESERVE %upx clamped to %upx", declared_px,
                   (uint32_t)TERMINAL_YDRAW_RESERVE_MAX_PX);
            declared_px = TERMINAL_YDRAW_RESERVE_MAX_PX;
        }
        state->declared_span_px = (float)declared_px;
        state->ingested_records++;
        return;
    }
    if (data[0] == YETTY_YDRAW_CMD_PAINT_Z && size >= 8u) {
        /* Open an explicit paint-z scope: the records that follow (a
         * complex and its label/legend prims) stack at this depth. */
        int32_t z;
        memcpy(&z, &data[1], sizeof(z));
        struct yetty_ycore_void_result push_res =
            yetty_yvterm_vterm_rich_push_paint_z(terminal->grid, z);
        if (YETTY_IS_ERR(push_res)) {
            yetty_ycore_error_destroy(push_res.error);
        }
        state->ingested_records++;
        return;
    }
    if (data[0] == YETTY_YDRAW_CMD_PAINT_Z_END && size >= 8u) {
        struct yetty_ycore_void_result pop_res =
            yetty_yvterm_vterm_rich_pop_paint_z(terminal->grid);
        if (YETTY_IS_ERR(pop_res)) {
            yetty_ycore_error_destroy(pop_res.error);
        }
        state->ingested_records++;
        return;
    }
    /* new_content is set below only AFTER a record actually lands (append /
     * attach success) and only for placement-bearing records — a failed
     * append, a failed factory, or a FONT resource (glyph bytes, no drawn
     * geometry) must not mark the batch as new content, or finalization
     * would reserve rows for nothing (the one-row floor keys off this). */
    /* Resolve the record's effective AABB once: it feeds BOTH the batch's
     * content-bottom (reservation) and the record's stored content-space
     * extent (per-node scroll retirement footprints — needed for
     * replacement content too, hence not reopen-gated). The FONT resource
     * ships glyph bytes, not drawn geometry — skipped. Complexes carry
     * their bounds at a fixed payload offset; SDF / text records expose
     * them via the entry ops aabb. */
    float extent_top_px = 0.0f;
    float extent_bottom_px = 0.0f;
    int extent_valid = 0;
    if (data[0] != YETTY_YDRAW_RESOURCE_FONT) {
        struct rectangle_result aabb;
        int have_aabb = 0;
        if (terminal_ydraw_record_is_complex(data[0])) {
            aabb = yetty_ydraw_complex_record_aabb(data);
            have_aabb = 1;
        } else if (entry->ops->aabb) {
            aabb = entry->ops->aabb(data);
            have_aabb = 1;
        }
        if (have_aabb) {
            if (YETTY_IS_OK(aabb)) {
                if (isfinite(aabb.value.min.y) && isfinite(aabb.value.max.y) &&
                    aabb.value.max.y >= aabb.value.min.y) {
                    extent_top_px = aabb.value.min.y;
                    extent_bottom_px = aabb.value.max.y;
                    extent_valid = 1;
                }
                if (state->reopen_depth == 0 && aabb.value.max.y > state->content_bottom_px) {
                    state->content_bottom_px = aabb.value.max.y;
                }
            } else {
                yetty_ycore_error_destroy(aabb.error);
            }
        }
    }
    if (terminal_ydraw_record_is_complex(data[0]) && terminal->complex_factory) {
        /* Retain the creating wire envelope on the line FIRST (verbatim, in
         * the same arena the SDF records use — the SDF pass skips
         * complex-type records). When the line ages past the scrollback
         * hot window the figure runtime is destroyed and this envelope is
         * what re-materializes it on scroll-back. Best-effort: a figure
         * without a retained envelope still displays, it just cannot be
         * rebuilt after eviction. */
        struct yetty_ycore_uint32_result envelope_res =
            extent_valid
                ? yetty_yvterm_vterm_append_primitive_extent(
                      terminal->grid, state->cursor_row, data, (uint32_t)(size / sizeof(uint32_t)),
                      extent_top_px, extent_bottom_px)
                : yetty_yvterm_vterm_append_primitive(terminal->grid, state->cursor_row, data,
                                                      (uint32_t)(size / sizeof(uint32_t)));
        if (YETTY_IS_ERR(envelope_res)) {
            yetty_ycore_error_destroy(envelope_res.error);
        }
        /* Mint the figure instance and hand it to the line; the grid owns it
         * and vterm's rich pass draws it at the line's pixel origin. */
        struct yetty_ydraw_complex_ptr_result ir = yetty_ydraw_complex_factory_create_instance(
            terminal->complex_factory, data, size, /*rolling_row=*/0u);
        if (YETTY_IS_OK(ir)) {
            struct yetty_ycore_uint32_result at =
                yetty_yvterm_vterm_attach_complex(terminal->grid, state->cursor_row, ir.value);
            if (YETTY_IS_ERR(at)) {
                ydebug("ydraw ingest: attach_complex type=0x%08x FAILED: %s", data[0],
                       at.error.msg);
                yetty_ydraw_complex_destroy(ir.value);
                yetty_ycore_error_destroy(at.error);
            } else {
                state->ingested_complexes++;
                if (state->reopen_depth == 0) {
                    state->new_content = 1; /* runtime landed — placement-bearing */
                }
                /* An id-bearing complex (CMD_NODE_ID prefix) becomes the
                 * addressable COMPLEX node at its path: the enclosing group
                 * path folded with its OWN id. An anonymous complex is not
                 * addressable (drawable-use-cases.md §2). */
                if (state->pending_node_id_valid) {
                    uint64_t node_key = yetty_yvterm_group_key_fold(
                        state->scope_keys[state->group_depth], state->pending_node_id);
                    ydebug("ydraw ingest: complex node id=%u key=%016llx", state->pending_node_id,
                           (unsigned long long)node_key);
                    state->pending_node_id_valid = 0; /* consumed */
                    struct yetty_ycore_void_result bind_res =
                        yetty_yvterm_vterm_rich_update_bind(terminal->grid, node_key);
                    if (YETTY_IS_ERR(bind_res)) {
                        ydebug("ydraw ingest: complex node bind FAILED: %s", bind_res.error.msg);
                        yetty_ycore_error_destroy(bind_res.error);
                    }
                }
            }
        } else {
            ydebug("ydraw ingest: create_instance type=0x%08x FAILED: %s", data[0], ir.error.msg);
            yetty_ycore_error_destroy(ir.error);
        }
    } else {
        /* Raw SDF prim / glyph / text-drawable-list / font record: store the
         * whole wire record on the line for the SDF render pass to consume. */
        struct yetty_ycore_uint32_result ap =
            extent_valid
                ? yetty_yvterm_vterm_append_primitive_extent(
                      terminal->grid, state->cursor_row, data, (uint32_t)(size / sizeof(uint32_t)),
                      extent_top_px, extent_bottom_px)
                : yetty_yvterm_vterm_append_primitive(terminal->grid, state->cursor_row, data,
                                                      (uint32_t)(size / sizeof(uint32_t)));
        if (YETTY_IS_ERR(ap)) {
            yetty_ycore_error_destroy(ap.error); /* skip one bad record, keep parsing */
        } else if (state->reopen_depth == 0 && data[0] != YETTY_YDRAW_RESOURCE_FONT) {
            state->new_content = 1; /* stored, drawable — placement-bearing */
        }
    }
    state->ingested_records++;
}

/* Vterm entry points declared ahead of the regenerated api header so the
 * first codegen pass resolves them. */
struct yetty_ycore_void_result yetty_yvterm_vterm_rich_update_extent_refresh(
    struct yetty_yclass_object *obj, uint64_t update_key, float content_top_px,
    float content_bottom_px);
struct yetty_ycore_void_result yetty_yvterm_vterm_rich_update_paint_z(
    struct yetty_yclass_object *obj, uint64_t update_key, int32_t *out_paint_z);
struct yetty_ycore_void_result yetty_yvterm_vterm_rich_group_reserve(
    struct yetty_yclass_object *obj, uint64_t group_key, uint32_t extra_records,
    uint32_t extra_words);

/* One staged chrome replacement record: a validated view into the emitted
 * drawable list, collected BEFORE any store mutation. */
struct terminal_rechrome_staged_record {
    const uint32_t *words;
    uint32_t word_count;
    float extent_top_px;
    float extent_bottom_px;
    int extent_valid;
};

/* Generous DoS ceiling on one chrome replacement's emitted bytes. This is
 * NOT a contract narrowing — real chrome is a few KB; anything near this
 * bound means a broken emit_chrome. The record COUNT is unbounded (the
 * staging array sizes itself from the parsed stream). */
enum { TERMINAL_RECHROME_MAX_BYTES = 1u << 20 };

/* Parse the emitted chrome stream once: validate that it holds ONLY plain
 * prim records (the figure never nests groups or complexes in its own
 * chrome), count records + words, and — when `staged` is non-NULL — fill
 * one staged view per record. Returns 1 on a fully valid stream. */
static int terminal_rechrome_scan(struct yetty_yterminal_terminal *terminal, const uint8_t *bytes,
                                  size_t byte_count, struct terminal_rechrome_staged_record *staged,
                                  uint32_t *out_record_count, uint32_t *out_word_total)
{
    if (!bytes && byte_count != 0) {
        return 0;
    }
    uint32_t record_count = 0;
    uint32_t word_total = 0;
    while (bytes && byte_count >= 2u * sizeof(uint32_t)) {
        struct yetty_ydraw_command command;
        struct yetty_ycore_size_result step = yetty_ydraw_drawable_command_parse(
            terminal->ydraw_registry, bytes, (uint32_t)byte_count, &command);
        if (YETTY_IS_ERR(step)) {
            yetty_ycore_error_destroy(step.error);
            return 0;
        }
        if (step.value == 0 || step.value > byte_count) {
            return 0;
        }
        if (command.kind != YETTY_YDRAW_COMMAND_ADD || !command.entry.data || !command.entry.ops) {
            return 0;
        }
        if (staged) {
            struct terminal_rechrome_staged_record *slot = &staged[record_count];
            slot->words = command.entry.data;
            slot->word_count = (uint32_t)(step.value / sizeof(uint32_t));
            slot->extent_valid = 0;
            slot->extent_top_px = 0.0f;
            slot->extent_bottom_px = 0.0f;
            if (command.entry.ops->aabb) {
                struct rectangle_result aabb = command.entry.ops->aabb(slot->words);
                if (YETTY_IS_OK(aabb)) {
                    if (isfinite(aabb.value.min.y) && isfinite(aabb.value.max.y) &&
                        aabb.value.max.y >= aabb.value.min.y) {
                        slot->extent_top_px = aabb.value.min.y;
                        slot->extent_bottom_px = aabb.value.max.y;
                        slot->extent_valid = 1;
                    }
                } else {
                    yetty_ycore_error_destroy(aabb.error);
                }
            }
        }
        record_count++;
        word_total += (uint32_t)(step.value / sizeof(uint32_t));
        bytes += step.value;
        byte_count -= step.value;
    }
    if (byte_count != 0) {
        /* A ragged tail is NOT a valid stream — accepting it would
         * silently truncate the replacement ("fully valid" contract). */
        return 0;
    }
    *out_record_count = record_count;
    *out_word_total = word_total;
    return 1;
}

/* Locally rebuild a self-owned figure's chrome group after an update
 * invalidated its layout (geometry / axis-range op). The figure re-emits
 * its label prims for its CURRENT retained state (ops->emit_chrome) and
 * the group named in its record is reopened IN PLACE — the same
 * exact-subtree replacement a wire reopen performs, only no bytes ever
 * crossed the wire.
 *
 * ATOMIC against the old chrome: the emitted stream is fully parsed and
 * validated, the block's record/arena capacity AND the group's
 * replacement-ordinal headroom are RESERVED, the figure's original
 * paint-z is resolved and its ambient scope is pushed — every fallible
 * step happens while the old chrome is still intact. After the reopen the
 * staged appends are infallible by reservation. Returns 1 only on a
 * complete commit; the caller keeps `chrome_dirty` set otherwise so a
 * later update retries against the intact old chrome. */
static int terminal_ydraw_local_rechrome(struct yetty_yterminal_terminal *terminal,
                                         struct terminal_ydraw_ingest_state *state,
                                         struct yetty_ydraw_complex *target, uint64_t update_key,
                                         uint64_t parent_scope)
{
    uint64_t chrome_key = yetty_yvterm_group_key_fold(parent_scope, target->chrome_group_id);
    /* Only REPLACE a chrome group the creation envelope established —
     * a create here would open a stray block mid-update. */
    struct yetty_ycore_uint32_result live_res =
        yetty_yvterm_vterm_rich_group_query(terminal->grid, chrome_key, NULL);
    if (YETTY_IS_ERR(live_res)) {
        yetty_ycore_error_destroy(live_res.error);
        return 0;
    }
    if (!live_res.value) {
        ydebug("ydraw ingest: local rechrome skipped — chrome group %u not live",
               target->chrome_group_id);
        return 0;
    }
    struct yetty_ydraw_drawable_list_result list_res =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    if (YETTY_IS_ERR(list_res)) {
        yetty_ycore_error_destroy(list_res.error);
        return 0;
    }
    struct yetty_ydraw_drawable_list *chrome_list = list_res.value;
    struct yetty_ycore_void_result emit_res = target->ops->emit_chrome(target, chrome_list);
    if (YETTY_IS_ERR(emit_res)) {
        ydebug("ydraw ingest: local rechrome emit failed: %s", emit_res.error.msg);
        yetty_ycore_error_destroy(emit_res.error);
        yetty_ydraw_drawable_list_destroy(chrome_list);
        return 0;
    }

    /* STAGE: size from the parsed stream itself — no fixed record cap
     * narrowing the emit_chrome contract. Pass 1 validates + counts;
     * pass 2 fills the exactly-sized staging array. */
    const uint8_t *bytes = yetty_ydraw_drawable_list_data(chrome_list);
    size_t byte_count = yetty_ydraw_drawable_list_size(chrome_list);
    if (byte_count > (size_t)TERMINAL_RECHROME_MAX_BYTES) {
        ydebug("ydraw ingest: local rechrome aborted — emitted chrome exceeds %u bytes",
               (unsigned)TERMINAL_RECHROME_MAX_BYTES);
        yetty_ydraw_drawable_list_destroy(chrome_list);
        return 0;
    }
    uint32_t staged_count = 0;
    uint32_t staged_words_total = 0;
    if (!terminal_rechrome_scan(terminal, bytes, byte_count, NULL, &staged_count,
                                &staged_words_total)) {
        ydebug("ydraw ingest: local rechrome aborted — unexpected emitted content");
        yetty_ydraw_drawable_list_destroy(chrome_list);
        return 0;
    }
    struct terminal_rechrome_staged_record *staged = NULL;
    if (staged_count) {
        staged = calloc(staged_count, sizeof(*staged));
        if (!staged) {
            yetty_ydraw_drawable_list_destroy(chrome_list);
            return 0;
        }
        uint32_t refill_count = 0;
        uint32_t refill_words = 0;
        if (!terminal_rechrome_scan(terminal, bytes, byte_count, staged, &refill_count,
                                    &refill_words) ||
            refill_count != staged_count) {
            free(staged);
            yetty_ydraw_drawable_list_destroy(chrome_list);
            return 0;
        }
    }

    /* RESERVE: block capacity + replacement-ordinal headroom — after
     * this the commit below cannot fail. */
    struct yetty_ycore_void_result reserve_res = yetty_yvterm_vterm_rich_group_reserve(
        terminal->grid, chrome_key, staged_count, staged_words_total);
    if (YETTY_IS_ERR(reserve_res)) {
        ydebug("ydraw ingest: local rechrome reserve failed: %s", reserve_res.error.msg);
        yetty_ycore_error_destroy(reserve_res.error);
        free(staged);
        yetty_ydraw_drawable_list_destroy(chrome_list);
        return 0;
    }

    /* The figure's original depth — replacement chrome MUST keep it. An
     * unresolvable z or a full ambient stack aborts here, before the
     * destructive reopen, leaving the old chrome standing. */
    int32_t figure_paint_z = 0;
    struct yetty_ycore_void_result z_res =
        yetty_yvterm_vterm_rich_update_paint_z(terminal->grid, update_key, &figure_paint_z);
    if (YETTY_IS_ERR(z_res)) {
        ydebug("ydraw ingest: local rechrome aborted — paint-z unresolved: %s", z_res.error.msg);
        yetty_ycore_error_destroy(z_res.error);
        free(staged);
        yetty_ydraw_drawable_list_destroy(chrome_list);
        return 0;
    }
    struct yetty_ycore_void_result push_res =
        yetty_yvterm_vterm_rich_push_paint_z(terminal->grid, figure_paint_z);
    if (YETTY_IS_ERR(push_res)) {
        ydebug("ydraw ingest: local rechrome aborted — paint-z scope: %s", push_res.error.msg);
        yetty_ycore_error_destroy(push_res.error);
        /* The rejected push still counted into the store's overflow
         * bookkeeping — issue ITS matching pop, or the envelope's next
         * wire PAINT_Z_END would consume the leaked overflow instead of
         * closing its real scope (mis-stamping every record after it). */
        struct yetty_ycore_void_result balance_res =
            yetty_yvterm_vterm_rich_pop_paint_z(terminal->grid);
        if (YETTY_IS_ERR(balance_res)) {
            yetty_ycore_error_destroy(balance_res.error);
        }
        free(staged);
        yetty_ydraw_drawable_list_destroy(chrome_list);
        return 0;
    }

    /* COMMIT: exact-subtree replacement + staged appends (infallible by
     * reservation; the defensive absorb below covers nothing reachable). */
    struct yetty_ycore_uint32_result open_res =
        yetty_yvterm_vterm_rich_group_open(terminal->grid, state->cursor_row, chrome_key);
    if (YETTY_IS_ERR(open_res)) {
        ydebug("ydraw ingest: local rechrome reopen failed: %s", open_res.error.msg);
        yetty_ycore_error_destroy(open_res.error);
        struct yetty_ycore_void_result unwind_res =
            yetty_yvterm_vterm_rich_pop_paint_z(terminal->grid);
        if (YETTY_IS_ERR(unwind_res)) {
            yetty_ycore_error_destroy(unwind_res.error);
        }
        free(staged);
        yetty_ydraw_drawable_list_destroy(chrome_list);
        return 0;
    }
    int complete = 1;
    for (uint32_t index = 0; index < staged_count; index++) {
        const struct terminal_rechrome_staged_record *slot = &staged[index];
        struct yetty_ycore_uint32_result append_res =
            slot->extent_valid
                ? yetty_yvterm_vterm_append_primitive_extent(
                      terminal->grid, state->cursor_row, slot->words, slot->word_count,
                      slot->extent_top_px, slot->extent_bottom_px)
                : yetty_yvterm_vterm_append_primitive(terminal->grid, state->cursor_row,
                                                      slot->words, slot->word_count);
        if (YETTY_IS_ERR(append_res)) {
            ydebug("ydraw ingest: local rechrome append failed: %s", append_res.error.msg);
            yetty_ycore_error_destroy(append_res.error);
            complete = 0;
        }
    }
    struct yetty_ycore_void_result pop_res = yetty_yvterm_vterm_rich_pop_paint_z(terminal->grid);
    if (YETTY_IS_ERR(pop_res)) {
        yetty_ycore_error_destroy(pop_res.error);
    }
    struct yetty_ycore_void_result close_res = yetty_yvterm_vterm_rich_group_close(terminal->grid);
    if (YETTY_IS_ERR(close_res)) {
        yetty_ycore_error_destroy(close_res.error);
    }
    free(staged);
    yetty_ydraw_drawable_list_destroy(chrome_list);
    if (complete) {
        ydebug("ydraw ingest: chrome group %u locally rebuilt (%u records, z=%d)",
               target->chrome_group_id, staged_count, figure_paint_z);
    }
    return complete;
}

/* Remember a figure whose local chrome replacement failed transiently so
 * the envelope-close sweep retries it. Deduplicated; a full queue falls
 * back to the figure's own chrome_dirty retry-on-next-update. */
static void terminal_rechrome_retry_queue(struct yetty_yterminal_terminal *terminal,
                                          uint64_t update_key, uint64_t parent_scope)
{
    for (uint32_t index = 0; index < terminal->rechrome_retry_count; ++index) {
        if (terminal->rechrome_retry[index].update_key == update_key) {
            return;
        }
    }
    uint32_t capacity = sizeof(terminal->rechrome_retry) / sizeof(terminal->rechrome_retry[0]);
    if (terminal->rechrome_retry_count >= capacity) {
        ydebug("ydraw ingest: rechrome retry queue full — figure keeps chrome_dirty");
        return;
    }
    terminal->rechrome_retry[terminal->rechrome_retry_count].update_key = update_key;
    terminal->rechrome_retry[terminal->rechrome_retry_count].parent_scope = parent_scope;
    terminal->rechrome_retry_count++;
}

/* Envelope-close retry sweep: attempt every queued chrome replacement
 * again. Entries drop on success, on a vanished/clean target, or stay
 * queued for the next envelope — convergence no longer depends on the
 * SAME figure receiving another update. */
static void terminal_rechrome_retry_sweep(struct yetty_yterminal_terminal *terminal,
                                          struct terminal_ydraw_ingest_state *state)
{
    uint32_t write_index = 0;
    for (uint32_t index = 0; index < terminal->rechrome_retry_count; ++index) {
        uint64_t update_key = terminal->rechrome_retry[index].update_key;
        uint64_t parent_scope = terminal->rechrome_retry[index].parent_scope;
        struct yetty_ydraw_complex *target = NULL;
        struct yetty_ycore_void_result target_res =
            yetty_yvterm_vterm_rich_update_target(terminal->grid, update_key, &target);
        if (YETTY_IS_ERR(target_res)) {
            yetty_ycore_error_destroy(target_res.error);
            continue; /* binding gone — drop */
        }
        if (!target || !target->chrome_dirty || !target->ops->emit_chrome ||
            target->chrome_group_id == 0u) {
            continue; /* vanished or already clean — drop */
        }
        if (terminal_ydraw_local_rechrome(terminal, state, target, update_key, parent_scope)) {
            target->chrome_dirty = 0;
            continue; /* converged — drop */
        }
        terminal->rechrome_retry[write_index].update_key = update_key;
        terminal->rechrome_retry[write_index].parent_scope = parent_scope;
        write_index++;
    }
    terminal->rechrome_retry_count = write_index;
}

/* Route one CMD_UPDATE command to the live figure registered under its
 * target id (live plot samples, chunked video NALs). The payload's first
 * u32 is the figure-defined target_field; the rest is the body. Failures
 * are absorbed like any other per-record problem. */
static void terminal_ydraw_route_update(struct yetty_yterminal_terminal *terminal,
                                        struct terminal_ydraw_ingest_state *state,
                                        const struct yetty_ydraw_command_update *update)
{
    state->ingested_records++;
    if (update->size < sizeof(uint32_t)) {
        return; /* no target_field word — nothing to dispatch */
    }
    /* Ancestor scope: an explicit CMD_PATH latch wins (absolute, any depth);
     * otherwise the ambient open-group scope (root at top level). The wire
     * id is the FINAL path component either way. */
    uint64_t update_scope =
        state->pending_path_valid ? state->pending_path_key : state->scope_keys[state->group_depth];
    state->pending_path_valid = 0; /* consumed */
    uint64_t update_key = yetty_yvterm_group_key_fold(update_scope, update->id);
    struct yetty_ydraw_complex *target = NULL;
    struct yetty_ycore_void_result target_res =
        yetty_yvterm_vterm_rich_update_target(terminal->grid, update_key, &target);
    if (YETTY_IS_ERR(target_res)) {
        yetty_ycore_error_destroy(target_res.error);
        return;
    }
    if (!target || !target->ops || !target->ops->update) {
        /* Not a complex — a GROUP node at this path receives a STATE-FIELD
         * write instead (what update means depends on the node's kind). The
         * only field today is the translation offset: [f32 x][f32 y],
         * absolute. Anything unresolvable stays a graceful no-op. */
        uint32_t state_field;
        memcpy(&state_field, update->data, sizeof(state_field));
        if (state_field == YETTY_YDRAW_GROUP_FIELD_CLIP &&
            update->size >= sizeof(uint32_t) + 4u * sizeof(float)) {
            float clip_rect[4];
            memcpy(clip_rect, update->data + sizeof(uint32_t), sizeof(clip_rect));
            struct yetty_ycore_void_result clip_res = yetty_yvterm_vterm_rich_group_clip_set(
                terminal->grid, update_key, clip_rect[0], clip_rect[1], clip_rect[2], clip_rect[3]);
            if (YETTY_IS_ERR(clip_res)) {
                ydebug("ydraw ingest: group clip id=%u: %s", update->id, clip_res.error.msg);
                yetty_ycore_error_destroy(clip_res.error);
            }
            return;
        }
        if (state_field == YETTY_YDRAW_GROUP_FIELD_OFFSET &&
            update->size >= sizeof(uint32_t) + 2u * sizeof(float)) {
            float offset_pair[2];
            memcpy(offset_pair, update->data + sizeof(uint32_t), sizeof(offset_pair));
            struct yetty_ycore_void_result offset_res = yetty_yvterm_vterm_rich_group_offset_set(
                terminal->grid, update_key, offset_pair[0], offset_pair[1]);
            if (YETTY_IS_ERR(offset_res)) {
                ydebug("ydraw ingest: group offset id=%u: %s", update->id, offset_res.error.msg);
                yetty_ycore_error_destroy(offset_res.error);
            }
            return;
        }
        ydebug("ydraw ingest: CMD_UPDATE id=%u has no live updatable target", update->id);
        return;
    }
    uint32_t target_field;
    memcpy(&target_field, update->data, sizeof(target_field));
    struct yetty_ycore_void_result update_res = target->ops->update(
        target, target_field, update->data + sizeof(uint32_t), update->size - sizeof(uint32_t));
    if (YETTY_IS_ERR(update_res)) {
        ydebug("ydraw ingest: CMD_UPDATE id=%u failed: %s", update->id, update_res.error.msg);
        yetty_ycore_error_destroy(update_res.error);
        return;
    }
    /* Retain the accepted update so the figure re-materializes with it after
     * hot-tier eviction (live-first budget — see the grid). The payload tail
     * is journaled as whole words; a ragged byte tail cannot round-trip the
     * word-aligned wire framing and is not journaled. */
    uint32_t payload_bytes = update->size - (uint32_t)sizeof(uint32_t);
    if ((payload_bytes % sizeof(uint32_t)) == 0u) {
        struct yetty_ycore_void_result journal_res = yetty_yvterm_vterm_rich_update_journal(
            terminal->grid, update_key, target_field,
            (const uint32_t *)(update->data + sizeof(uint32_t)),
            payload_bytes / (uint32_t)sizeof(uint32_t));
        if (YETTY_IS_ERR(journal_res)) {
            yetty_ycore_error_destroy(journal_res.error);
        }
    } else {
        /* Ragged byte tail: applied live but not journalable — poison the
         * record's journal so a PARTIAL history can never replay as if it
         * were the complete current state. */
        struct yetty_ycore_void_result poison_res =
            yetty_yvterm_vterm_rich_update_journal_poison(terminal->grid, update_key);
        if (YETTY_IS_ERR(poison_res)) {
            yetty_ycore_error_destroy(poison_res.error);
        }
    }
    /* Keep the retained scroll-retirement footprint in step with the
     * runtime: a geometry/range op moves the drawable's effective AABB,
     * and retirement (plus the ancestor group's subtree union) must
     * judge what is actually drawn. The block's immutable row span never
     * changes; a same-value write for data-only updates is harmless. */
    if (isfinite(target->bounds.min.y) && isfinite(target->bounds.max.y) &&
        target->bounds.max.y >= target->bounds.min.y) {
        struct yetty_ycore_void_result extent_res = yetty_yvterm_vterm_rich_update_extent_refresh(
            terminal->grid, update_key, target->bounds.min.y, target->bounds.max.y);
        if (YETTY_IS_ERR(extent_res)) {
            yetty_ycore_error_destroy(extent_res.error);
        }
    }
    /* A geometry / axis-range op invalidated the figure's chrome layout:
     * regenerate the chrome group LOCALLY from the figure's retained
     * state. The wire carried only the tiny op — the labels never
     * re-ship. chrome_dirty clears ONLY on a complete commit; a staged
     * or partial failure keeps it set AND queues the figure for the
     * envelope-close retry sweep — a figure that goes quiet after this
     * one op still converges. */
    if (target->chrome_dirty && target->ops->emit_chrome && target->chrome_group_id != 0u) {
        if (terminal_ydraw_local_rechrome(terminal, state, target, update_key, update_scope)) {
            target->chrome_dirty = 0;
        } else {
            terminal_rechrome_retry_queue(terminal, update_key, update_scope);
        }
    }
}

/* Route one CMD_DELETE to the grid's live group index. Unknown/sealed ids
 * are logged and absorbed (fire-and-forget wire; a later ACK extension
 * surfaces them). */
static void terminal_ydraw_route_delete(struct yetty_yterminal_terminal *terminal,
                                        struct terminal_ydraw_ingest_state *state, uint32_t id)
{
    state->ingested_records++;
    uint64_t delete_scope =
        state->pending_path_valid ? state->pending_path_key : state->scope_keys[state->group_depth];
    state->pending_path_valid = 0; /* consumed */
    uint64_t delete_key = yetty_yvterm_group_key_fold(delete_scope, id);
    struct yetty_ycore_void_result delete_res =
        yetty_yvterm_vterm_rich_group_delete(terminal->grid, delete_key);
    if (YETTY_IS_ERR(delete_res)) {
        ydebug("ydraw ingest: CMD_DELETE id=%u: %s", id, delete_res.error.msg);
        yetty_ycore_error_destroy(delete_res.error);
    }
}

/* Walk a nested GROUP body: records back to back, parsed with the same
 * registry stride machinery the wire iterator uses. ADDs (and nested
 * GROUPs, recursively through ingest_record) apply under the open group
 * scope; DELETE/UPDATE route like top-level commands. Malformed tails are
 * dropped from the first bad stride. */
static void terminal_ydraw_walk_nested(struct yetty_yterminal_terminal *terminal,
                                       struct terminal_ydraw_ingest_state *state,
                                       const uint8_t *bytes, size_t byte_count)
{
    while (byte_count >= sizeof(uint32_t)) {
        struct yetty_ydraw_command command;
        struct yetty_ycore_size_result step = yetty_ydraw_drawable_command_parse(
            terminal->ydraw_registry, bytes, (uint32_t)byte_count, &command);
        if (YETTY_IS_ERR(step)) {
            yetty_ycore_error_destroy(step.error);
            return;
        }
        if (step.value == 0 || step.value > byte_count) {
            return;
        }
        if (command.kind == YETTY_YDRAW_COMMAND_DELETE) {
            terminal_ydraw_route_delete(terminal, state, command.id);
        } else if (command.kind == YETTY_YDRAW_COMMAND_UPDATE) {
            terminal_ydraw_route_update(terminal, state, &command.update);
        } else if (command.kind == YETTY_YDRAW_COMMAND_ADD && command.entry.data &&
                   command.entry.ops) {
            terminal_ydraw_ingest_record(terminal, state, &command.entry, step.value);
        }
        bytes += step.value;
        byte_count -= step.value;
    }
}

static void terminal_ydraw_ingest_finish(struct yetty_yterminal_terminal *terminal,
                                         struct terminal_ydraw_ingest_state *state, int ok)
{
    ydebug("ydraw ingest: %u records (%u complexes) bottom=%.0fpx ok=%d", state->ingested_records,
           state->ingested_complexes, state->content_bottom_px, ok);

    /* Retry chrome replacements that failed transiently in an EARLIER
     * envelope — every envelope boundary is a convergence point, not just
     * the failed figure's own next update. */
    if (terminal->rechrome_retry_count) {
        terminal_rechrome_retry_sweep(terminal, state);
    }

    /* Reserve vertical space for this envelope's content by advancing the
     * libvterm cursor (newlines drive normal scrollback + rolling_row
     * bookkeeping). Only the receiver knows the cell height, so this row
     * count cannot be computed by the producer. The bounds are checked here
     * because both inputs are wire-controlled: content_bottom_px accumulates
     * producer AABB floats (can be NaN/inf), and the declared span is an
     * arbitrary u32 (clamped at parse, re-clamped here). */
    uint32_t reserve_rows = 0u;
    float reserve_bottom_px =
        state->declared_span_px > 0.0f ? state->declared_span_px : state->content_bottom_px;
    if (ok && state->new_content && state->text_cell.height > 0) {
        if (!isfinite(reserve_bottom_px) || reserve_bottom_px < 0.0f) {
            reserve_bottom_px = 0.0f;
        }
        /* The wire authors spans/extents in PRODUCER-LOGICAL pixels; the
         * cell height is CURRENT framebuffer pixels — scale by the grid's
         * committed rich density product (display density x structural
         * cell zoom) BEFORE the row conversion, the same value the
         * retirement math judges this content by (at scale 2 an unscaled
         * RESERVE(300) would reserve about half the needed rows). */
        reserve_bottom_px *= state->rich_density > 0.0f ? state->rich_density : 1.0f;
        if (reserve_bottom_px > (float)TERMINAL_YDRAW_RESERVE_MAX_PX) {
            reserve_bottom_px = (float)TERMINAL_YDRAW_RESERVE_MAX_PX;
        }
        /* Ceil the pixel bottom BEFORE the integer division — truncating
         * first under-reserves fractional bounds (a 10.1px bottom on a 10px
         * cell needs TWO rows), and fractional AABBs are normal for strokes
         * and transformed geometry. */
        uint32_t cell_h = (uint32_t)state->text_cell.height;
        uint32_t reserve_px = (uint32_t)ceilf(reserve_bottom_px);
        reserve_rows = (reserve_px + cell_h - 1u) / cell_h;
        if (reserve_rows == 0u) {
            /* A successful provisional insertion with no visible extent (an
             * empty addressable group, GROUP(7){}) still takes its minimum
             * ONE-row placement — one row of surface, one cursor advance. */
            reserve_rows = 1u;
        }
        if (reserve_rows > TERMINAL_YDRAW_RESERVE_MAX_ROWS) {
            reserve_rows = TERMINAL_YDRAW_RESERVE_MAX_ROWS;
        }
        /* Declare the insertion's span BEFORE the advance: the advance can
         * scroll, and the lifecycle paths (sealing, line recycling) must see
         * the block's true coverage — with span still unset a full-height
         * insertion would be sealed or destroyed during its own creation.
         * The returned advance is clamped on the alternate screen (no
         * history: the reserve must never scroll away its own content — the
         * cursor stops on the last row instead of below the block). */
        uint32_t advance_rows = reserve_rows;
        struct yetty_ycore_uint32_result declare_res =
            yetty_yvterm_vterm_rich_span_declare(terminal->grid, reserve_rows);
        if (YETTY_IS_ERR(declare_res)) {
            yetty_ycore_error_destroy(declare_res.error);
        } else {
            advance_rows = declare_res.value;
        }
        /* Chunked advance with handle re-homing: an advance deeper than the
         * ring's history capacity must not recycle the line carrying the
         * provisional block's handle before relocation. A FAILED advance
         * (re-home allocation, tracking invariant) means the declared
         * placement can no longer be realized — finalizing the full span
         * against a partial advance would let later text start inside the
         * claimed insertion, so the provisional insertion is ABORTED whole
         * (command-local: previous content is untouched, this batch's new
         * content is discarded). */
        struct yetty_ycore_void_result advance_res =
            yetty_yvterm_vterm_rich_reserve_advance(terminal->grid, advance_rows);
        if (YETTY_IS_ERR(advance_res)) {
            ydebug("ydraw ingest: reserve advance failed (%s) — aborting the insertion",
                   advance_res.error.msg);
            yetty_ycore_error_destroy(advance_res.error);
            struct yetty_ycore_void_result abort_res =
                yetty_yvterm_vterm_rich_batch_abort(terminal->grid);
            if (YETTY_IS_ERR(abort_res)) {
                yetty_ycore_error_destroy(abort_res.error);
            }
            reserve_rows = 0u; /* nothing to finalize */
        }
    }

    /* Finalize the batch UNCONDITIONALLY: relocation re-homes a placed block
     * onto its bottom-owner line (derived from its own timeline anchors, so
     * a clamped advance cannot skew it) and, placement or not, closes the
     * provisional insertion — the next envelope must start fresh. */
    struct yetty_ycore_void_result relocate_res =
        yetty_yvterm_vterm_relocate_rich_to_bottom(terminal->grid, reserve_rows);
    if (YETTY_IS_ERR(relocate_res)) {
        yetty_ycore_error_destroy(relocate_res.error);
    }

    /* Drop any paint-z scope left open by an unbalanced envelope, so it
     * cannot bleed onto the next envelope's records. */
    struct yetty_ycore_void_result reset_res =
        yetty_yvterm_vterm_rich_reset_paint_z(terminal->grid);
    if (YETTY_IS_ERR(reset_res)) {
        yetty_ycore_error_destroy(reset_res.error);
    }
}

/* Apply one complete buffered YDRAW body — the wire-order, nontransactional
 * batch of the contract (drawable-use-cases.md §4): commands execute in
 * declared order through the shared record path (anonymous adds, GROUP
 * scopes, DELETE/UPDATE routing); each insert is command-local (a replacing
 * insert pre-scans its subtree against the target's row span before any
 * mutation); a failed command never rolls back earlier ones. Framing trouble
 * drops the malformed tail (the walk stops where no boundary is
 * recoverable). Finalization runs AFTER the commands: one cursor advance by
 * the batch's insertion row span, then the resulting scroll/sealing. */
static struct yetty_ycore_void_result terminal_ydraw_apply_body(
    struct yetty_yterminal_terminal *terminal, const uint8_t *bytes, size_t byte_count)
{
    struct terminal_ydraw_ingest_state state;
    struct yetty_ycore_void_result begin_res = terminal_ydraw_ingest_begin(terminal, &state);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, begin_res, "terminal_ydraw_apply_body: begin");
    terminal_ydraw_walk_nested(terminal, &state, bytes, byte_count);
    terminal_ydraw_ingest_finish(terminal, &state, 1);
    return YETTY_OK_VOID();
}

/* Ingest one YDRAW_BIN envelope into yvterm's own grid: the whole decoded
 * body is buffered, structurally validated, then applied — SDF shapes,
 * text-drawable-lists (with wire-shipped fonts), complexes, and the
 * GROUP/DELETE/UPDATE commands alike. ycat/ypdf/markdown flow through
 * here. The leading 24-byte serialized-envelope header (shared with the
 * in-process MIME path) is skipped when present. */
static struct yetty_ycore_void_result terminal_ydraw_consume_bin(
    struct yetty_yterminal_terminal *terminal, struct yetty_ywire_wire_statemachine *sm)
{
    enum { YDRAW_BODY_INITIAL_CAPACITY = 64u * 1024u, YDRAW_SERIALIZED_HEADER_BYTES = 24 };
    /* Hard ceiling on one buffered envelope: a child emitting an unbounded
     * or unterminated rich DCS stream must not exhaust terminal memory. */
    const size_t YDRAW_BODY_MAX_BYTES = 256u * 1024u * 1024u;
    size_t capacity = YDRAW_BODY_INITIAL_CAPACITY;
    size_t length = 0;
    uint8_t *buffer = malloc(capacity);
    if (!buffer) {
        return YETTY_ERR(yetty_ycore_void, "terminal_ydraw: body buffer alloc");
    }
    for (;;) {
        if (length == capacity) {
            if (capacity >= YDRAW_BODY_MAX_BYTES) {
                free(buffer);
                return YETTY_ERR(yetty_ycore_void, "terminal_ydraw: envelope exceeds the body cap");
            }
            size_t new_capacity = capacity * 2u;
            uint8_t *grown = realloc(buffer, new_capacity);
            if (!grown) {
                free(buffer);
                return YETTY_ERR(yetty_ycore_void, "terminal_ydraw: body buffer grow");
            }
            buffer = grown;
            capacity = new_capacity;
        }
        struct yetty_ycore_size_result read_res =
            yetty_ywire_wire_statemachine_read(sm, buffer + length, capacity - length);
        if (YETTY_IS_ERR(read_res)) {
            free(buffer);
            return YETTY_ERR(yetty_ycore_void, "terminal_ydraw: body read", read_res);
        }
        if (read_res.value == 0) {
            break; /* envelope end */
        }
        length += read_res.value;
    }
    const uint8_t *body = buffer;
    size_t body_length = length;
    if (length >= YDRAW_SERIALIZED_HEADER_BYTES) {
        uint32_t magic = 0;
        memcpy(&magic, buffer, sizeof(magic));
        if (magic == 0x31425059u /* 'YPB1' */) {
            uint32_t declared = 0;
            memcpy(&declared, buffer + 20, sizeof(declared));
            body = buffer + YDRAW_SERIALIZED_HEADER_BYTES;
            body_length = length - YDRAW_SERIALIZED_HEADER_BYTES;
            if (declared < body_length) {
                body_length = declared;
            }
        }
    }
    struct yetty_ycore_void_result apply_res =
        terminal_ydraw_apply_body(terminal, body, body_length);
    free(buffer);
    return apply_res;
}

/* Ingest a serialized drawable-list blob already sitting in memory — the
 * terminal-side file renderer (terminal-mime.c) produces these by running
 * ysvg/ymarkdown/ypdf/… locally and serializing the resulting drawable
 * list. Same 24-byte framed header the wire iterator consumes (magic +
 * scene bounds + byte_count), then a plain command walk. */
struct yetty_ycore_void_result yetty_yterminal_mime_ingest_serialized(
    struct yetty_yterminal_terminal *terminal, const uint8_t *bytes, size_t len)
{
    enum { SERIALIZED_HEADER_BYTES = 24 };
    if (!terminal || !bytes) {
        return YETTY_ERR(yetty_ycore_void, "mime ingest: NULL arg");
    }
    if (len < SERIALIZED_HEADER_BYTES) {
        return YETTY_ERR(yetty_ycore_void, "mime ingest: blob shorter than envelope header");
    }
    uint32_t magic = 0;
    memcpy(&magic, bytes, sizeof(magic));
    if (magic != 0x31425059u /* 'YPB1' */) {
        return YETTY_ERR(yetty_ycore_void, "mime ingest: bad envelope magic");
    }
    uint32_t byte_count = 0;
    memcpy(&byte_count, bytes + 20, sizeof(byte_count));
    size_t body_len = len - SERIALIZED_HEADER_BYTES;
    if (byte_count < body_len) {
        body_len = byte_count;
    }
    /* One shared buffered path with the wire ingest: validate the complete
     * body, then apply. */
    return terminal_ydraw_apply_body(terminal, bytes + SERIALIZED_HEADER_BYTES, body_len);
}

/*===========================================================================
 * Headless ingest harness (#728) — the producer→terminal acceptance seam.
 *
 * A terminal slice with ONLY the ydraw ingest surface wired: the content
 * grid figure (CPU model — GPU init is a guarded no-op without a
 * runtime), the default drawable-list registry, and an EMPTY complex
 * factory registry the test populates with its own instrumented concrete
 * factory. No PTY, no event loop, no fonts, no wire statemachine.
 *
 * Tests feed the EXACT serialized envelope bytes a producer ships
 * (yetty_ydraw_drawable_list_serialize output — what ygui2's
 * framework_ship encodes into the DCS payload) through
 * yetty_yterminal_mime_ingest_serialized(), which runs the SAME
 * terminal_ydraw_apply_body pipeline as the PTY wire path: record walk,
 * scope folding, NODE_ID binding, CMD_PATH'd update routing, journal,
 * extent refresh and receiver-local chrome replacement.
 *=========================================================================*/

struct yetty_yterminal_terminal_result yetty_yterminal_ingest_harness_open(uint32_t cols,
                                                                           uint32_t rows)
{
    struct yetty_yclass_ctx terminal_ctx = {0};
    struct yetty_yclass_object_ptr_result object_res =
        yetty_yterminal_terminal_create(&terminal_ctx);
    YETTY_RETURN_IF_ERR(yetty_yterminal_terminal, object_res, "ingest harness: terminal object");
    struct yetty_yclass_object *terminal_object = object_res.value;
    struct yetty_yterminal_terminal_ptr_result terminal_res =
        yetty_yterminal_terminal_from(terminal_object);
    if (YETTY_IS_ERR(terminal_res)) {
        struct yetty_ycore_void_result free_res = yetty_yclass_object_free(terminal_object);
        if (YETTY_IS_ERR(free_res)) {
            yetty_ycore_error_destroy(free_res.error);
        }
        return YETTY_ERR(yetty_yterminal_terminal, "ingest harness: from_obj", terminal_res);
    }
    struct yetty_yterminal_terminal *terminal = terminal_res.value;
    /* The view vtable and emit yface, as terminal_open installs them —
     * the zoom/resize contract test dispatches REAL view events at the
     * harness and captures the resulting host→client envelopes. */
    terminal->view.ops = &terminal_view_ops;
    terminal->view.id = yetty_yui_view_next_id();
    struct yetty_yface_ptr_result yface_res = yetty_yface_create();
    if (YETTY_IS_ERR(yface_res)) {
        struct yetty_ycore_void_result free_res = yetty_yclass_object_free(terminal_object);
        if (YETTY_IS_ERR(free_res)) {
            yetty_ycore_error_destroy(free_res.error);
        }
        return YETTY_ERR(yetty_yterminal_terminal, "ingest harness: emit yface", yface_res);
    }
    terminal->emit_yface = yface_res.value;
    terminal->cols = cols;
    terminal->rows = rows;
    terminal->layout_content_scale = 1.0f;

    struct yetty_ydraw_drawable_list_registry_ptr_result registry_res =
        yetty_ydraw_drawable_list_registry_create_default();
    if (YETTY_IS_ERR(registry_res)) {
        struct yetty_ycore_void_result yface_destroy_res =
            yetty_yface_destroy(terminal->emit_yface);
        if (YETTY_IS_ERR(yface_destroy_res)) {
            yetty_ycore_error_destroy(yface_destroy_res.error);
        }
        terminal->emit_yface = NULL;
        struct yetty_ycore_void_result free_res = yetty_yclass_object_free(terminal_object);
        if (YETTY_IS_ERR(free_res)) {
            yetty_ycore_error_destroy(free_res.error);
        }
        return YETTY_ERR(yetty_yterminal_terminal, "ingest harness: registry", registry_res);
    }
    terminal->ydraw_registry = registry_res.value;

    struct yetty_ydraw_complex_factory_ptr_result factory_res =
        yetty_ydraw_complex_factory_create(NULL, NULL, (WGPUTextureFormat)0, NULL, NULL);
    if (YETTY_IS_ERR(factory_res)) {
        struct yetty_ycore_void_result yface_destroy_res =
            yetty_yface_destroy(terminal->emit_yface);
        if (YETTY_IS_ERR(yface_destroy_res)) {
            yetty_ycore_error_destroy(yface_destroy_res.error);
        }
        terminal->emit_yface = NULL;
        yetty_ydraw_drawable_list_registry_destroy(terminal->ydraw_registry);
        struct yetty_ycore_void_result free_res = yetty_yclass_object_free(terminal_object);
        if (YETTY_IS_ERR(free_res)) {
            yetty_ycore_error_destroy(free_res.error);
        }
        return YETTY_ERR(yetty_yterminal_terminal, "ingest harness: complex factory", factory_res);
    }
    terminal->complex_factory = factory_res.value;

    /* The CPU content model: GPU init inside is a guarded no-op with a
     * NULL context; the figure exists as the ingest target. The terminal
     * object itself is the sink host, as in the real wiring. */
    struct yetty_yclass_object_ptr_result grid_res =
        yetty_yvterm_vterm_figure_create(cols, rows, NULL, terminal_object);
    if (YETTY_IS_ERR(grid_res)) {
        struct yetty_ycore_void_result yface_destroy_res =
            yetty_yface_destroy(terminal->emit_yface);
        if (YETTY_IS_ERR(yface_destroy_res)) {
            yetty_ycore_error_destroy(yface_destroy_res.error);
        }
        terminal->emit_yface = NULL;
        yetty_ydraw_complex_factory_destroy(terminal->complex_factory);
        yetty_ydraw_drawable_list_registry_destroy(terminal->ydraw_registry);
        struct yetty_ycore_void_result free_res = yetty_yclass_object_free(terminal_object);
        if (YETTY_IS_ERR(free_res)) {
            yetty_ycore_error_destroy(free_res.error);
        }
        return YETTY_ERR(yetty_yterminal_terminal, "ingest harness: grid figure", grid_res);
    }
    terminal->grid = grid_res.value;
    return YETTY_OK(yetty_yterminal_terminal, terminal);
}

/* The harness's complex-factory registry — the test registers its
 * instrumented concrete factory here before feeding envelopes. */
struct yetty_ydraw_complex_factory *yetty_yterminal_ingest_harness_factory(
    struct yetty_yterminal_terminal *terminal)
{
    return terminal ? terminal->complex_factory : NULL;
}

/* The harness's content grid figure (yvterm:vterm object) — the
 * assertion surface; its composed model is reachable through
 * yetty_yvterm_vterm_grid_object(). Borrowed. */
struct yetty_yclass_object *yetty_yterminal_ingest_harness_grid(
    struct yetty_yterminal_terminal *terminal)
{
    return terminal ? terminal->grid : NULL;
}

/* Subscribe the harness terminal to the pane-wide resize family — what a
 * real client's CLIENT_INPUT_SUB envelope with the RESIZE bit latches.
 * The zoom/geometry regression drives the production view-event path and
 * captures the resulting envelope through the harness PTY below. */
struct yetty_ycore_void_result yetty_yterminal_ingest_harness_subscribe_pane(
    struct yetty_yterminal_terminal *terminal)
{
    if (!terminal) {
        return YETTY_ERR(yetty_ycore_void, "ingest harness subscribe: NULL terminal");
    }
    terminal->resize_subscribed = 1;
    return YETTY_OK_VOID();
}

/* Install a (test-owned, borrowed) PTY backend on the harness terminal —
 * the capture point for host→client OSC envelopes (resize) and the
 * resize sink the zoom path drives. The caller keeps ownership and must
 * outlive the harness. */
struct yetty_ycore_void_result yetty_yterminal_ingest_harness_set_pty(
    struct yetty_yterminal_terminal *terminal, struct yetty_platform_pty *pty)
{
    if (!terminal) {
        return YETTY_ERR(yetty_ycore_void, "ingest harness set_pty: NULL terminal");
    }
    terminal->context.pty = pty;
    return YETTY_OK_VOID();
}

/* Set the harness's committed content scale (HiDPI density): both halves
 * of the unit rule together — the terminal's layout scale and the grid's
 * rich density product (reserve/span conversion AND per-node retirement
 * read the latter). Producer envelopes stay logical; the receiver
 * converts. The harness's cell metrics are FIXED (no font re-derive), so
 * the product is set to the bare density — the production transition
 * (yetty_yvterm_vterm_set_content_scale) instead rebases the structural
 * baseline because its cells rescale in the same geometry pass. */
struct yetty_ycore_void_result yetty_yterminal_ingest_harness_set_scale(
    struct yetty_yterminal_terminal *terminal, float content_scale)
{
    if (!terminal) {
        return YETTY_ERR(yetty_ycore_void, "ingest harness set_scale: NULL terminal");
    }
    if (!(isfinite(content_scale) && content_scale > 0.0f)) {
        return YETTY_ERR(yetty_ycore_void, "ingest harness set_scale: must be positive finite");
    }
    terminal->layout_content_scale = content_scale;
    if (terminal->grid) {
        struct yetty_yclass_object_ptr_result grid_obj_res =
            yetty_yvterm_vterm_grid_object(terminal->grid);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, grid_obj_res, "ingest harness set_scale: grid");
        struct yetty_ycore_void_result density_res =
            yetty_yvterm_grid_set_rich_density(grid_obj_res.value, content_scale);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, density_res, "ingest harness set_scale: density");
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yterminal_ingest_harness_close(
    struct yetty_yterminal_terminal *terminal)
{
    if (!terminal) {
        return YETTY_ERR(yetty_ycore_void, "ingest harness close: NULL terminal");
    }
    /* Best-effort teardown, mirroring terminal_destroy's ordering: the
     * grid figure first (it owns runtimes minted by the factory), then
     * the factory, then the registry (OWNED here, unlike the borrowed
     * framework instance the real terminal uses). */
    struct yetty_ycore_void_result first_err = YETTY_OK_VOID();
    bool have_err = false;
    if (terminal->grid) {
        struct yetty_ycore_void_result grid_destroy_res = yetty_yfigure_destroy(terminal->grid);
        if (YETTY_IS_ERR(grid_destroy_res)) {
            first_err = grid_destroy_res;
            have_err = true;
        }
        terminal->grid = NULL;
    }
    if (terminal->complex_factory) {
        yetty_ydraw_complex_factory_destroy(terminal->complex_factory);
        terminal->complex_factory = NULL;
    }
    if (terminal->ydraw_registry) {
        yetty_ydraw_drawable_list_registry_destroy(terminal->ydraw_registry);
        terminal->ydraw_registry = NULL;
    }
    if (terminal->emit_yface) {
        struct yetty_ycore_void_result yface_destroy_res =
            yetty_yface_destroy(terminal->emit_yface);
        terminal->emit_yface = NULL;
        if (YETTY_IS_ERR(yface_destroy_res)) {
            if (!have_err) {
                first_err = yface_destroy_res;
                have_err = true;
            } else {
                yetty_ycore_error_destroy(yface_destroy_res.error);
            }
        }
    }
    struct yetty_yclass_object_ptr_result object_res = yetty_yterminal_terminal_to(terminal);
    if (YETTY_IS_OK(object_res)) {
        struct yetty_ycore_void_result free_res = yetty_yclass_object_free(object_res.value);
        if (YETTY_IS_ERR(free_res)) {
            if (!have_err) {
                first_err = free_res;
                have_err = true;
            } else {
                yetty_ycore_error_destroy(free_res.error);
            }
        }
    } else if (!have_err) {
        first_err = (struct yetty_ycore_void_result){.ok = 0, .error = object_res.error};
        have_err = true;
    } else {
        yetty_ycore_error_destroy(object_res.error);
    }
    if (have_err) {
        return YETTY_ERR(yetty_ycore_void, "ingest harness close", first_err);
    }
    return YETTY_OK_VOID();
}

struct yetty_yterminal_mime_env yetty_yterminal_mime_env_get(
    struct yetty_yterminal_terminal *terminal)
{
    struct yetty_yterminal_mime_env env = {0};
    if (!terminal) {
        return env;
    }
    env.config = terminal->context.yetty_context.runtime->config;
    env.cols = terminal->cols;
    env.rows = terminal->rows;
    struct pixel_size_result cell_res = yetty_yvterm_vterm_cell_size(terminal->grid);
    if (YETTY_IS_OK(cell_res)) {
        env.cell_width = (uint32_t)cell_res.value.width;
        env.cell_height = (uint32_t)cell_res.value.height;
    } else {
        yetty_ycore_error_destroy(cell_res.error);
    }
    return env;
}

void yetty_yterminal_mime_request_render(struct yetty_yterminal_terminal *terminal)
{
    terminal->context.yetty_context.event_loop->ops->request_render(
        terminal->context.yetty_context.event_loop);
}

/* Wire handler for the YDRAW_* DCS codes: CLEAR drops all anchored rich
 * content, BIN/OVERLAY stream records into the model. Loops across envelopes
 * (one persistent coroutine), draining CLEAR's empty body and yielding between
 * envelopes so the SM can advance. */
static struct yetty_ycore_void_result terminal_ydraw_process_input(
    void *userdata, struct yetty_ywire_wire_statemachine *sm)
{
    struct yetty_yterminal_terminal *terminal = *(struct yetty_yterminal_terminal **)userdata;
    for (;;) {
        int code = yetty_ywire_wire_statemachine_code(sm);
        if (code == YETTY_DCS_YDRAW_CLEAR) {
            struct yetty_ycore_void_result clear_res =
                yetty_yvterm_vterm_clear_rich_all(terminal->grid);
            if (YETTY_IS_ERR(clear_res)) {
                yetty_ycore_error_destroy(clear_res.error);
            }
            /* CLEAR has no body; drain the terminator so the SM advances. */
            while (!yetty_ywire_wire_statemachine_at_end(sm)) {
                uint8_t drain[16];
                struct yetty_ycore_size_result drain_res =
                    yetty_ywire_wire_statemachine_read(sm, drain, sizeof(drain));
                YETTY_RETURN_IF_ERR(yetty_ycore_void, drain_res, "terminal_ydraw: clear drain");
                if (drain_res.value == 0 && !yetty_ywire_wire_statemachine_at_end(sm)) {
                    yetty_yplatform_coro_yield();
                }
            }
        } else {
            struct yetty_ycore_void_result bin_res = terminal_ydraw_consume_bin(terminal, sm);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, bin_res, "terminal_ydraw: consume bin");
        }
        terminal->context.yetty_context.event_loop->ops->request_render(
            terminal->context.yetty_context.event_loop);
        yetty_yplatform_coro_yield();
    }
}

/* yetty_yterminal_mouse_sub_fn impl — fired by the text-layer when libvterm
 * flips DEC mode 1500/1501. Latch state on the terminal, and on the
 * rising edge ship the current pane pixel size to the client via
 * YETTY_OSC_SC_CLIENT_INPUT_FIGURE_RESIZE — over telnet/guest transports
 * TIOCGWINSZ carries no pixels, so this OSC is the client's only size cue
 * (without it a ygui client renders at its 800x600 default). */
YETTY_ANNOTATE("override@ytermsink:sink:mouse_sub")
static struct yetty_ycore_void_result terminal_sink_mouse_sub(struct yetty_yclass_object *obj,
                                                              int click_enabled, int move_enabled,
                                                              int key_enabled)
{
    struct yetty_yterminal_terminal_ptr_result terminal_res = yetty_yterminal_terminal_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, terminal_res, "terminal sink mouse_sub: from_obj");
    struct yetty_yterminal_terminal *terminal = terminal_res.value;
    int was_subscribed = terminal->mouse_click_subscribed || terminal->mouse_move_subscribed;
    terminal->mouse_click_subscribed = click_enabled;
    terminal->mouse_move_subscribed = move_enabled;
    terminal->key_subscribed = key_enabled;
    ydebug("terminal: mouse_sub click=%d move=%d key=%d", click_enabled, move_enabled, key_enabled);

    if (!was_subscribed && (click_enabled || move_enabled) && terminal->applied_w > 0.0f &&
        terminal->applied_h > 0.0f) {
        struct yetty_ycore_void_result rr = terminal_emit_card_resize(
            terminal, terminal->focused_figure_id, terminal->applied_w, terminal->applied_h);
        if (YETTY_IS_ERR(rr)) {
            yetty_ycore_error_destroy(rr.error);
        }
    }
    /* Subscription drop = the figure no longer wants client input. The
     * figure itself may persist in the compositor (apps that exit with
     * keep_visible=true), but routing keystrokes to it after this point
     * would mean writing OSC envelopes targeted at a non-listening
     * figure to the PTY slave — the cooked-mode tty driver echoes the
     * printable bytes back, libvterm prints them as plain text, and the
     * user sees base64 garbage at the shell prompt. */
    if (!click_enabled && !move_enabled) {
        terminal->focused_figure_id = 0;
    }
    return YETTY_OK_VOID();
}

/* yetty_yterminal_clipboard_write_fn impl — the child emitted OSC 52 to set the
 * clipboard. libvterm already base64-decoded the payload, so hand the plain text
 * to the platform clipboard, reusing the same hop as mouse-selection copy. The
 * platform exposes a single clipboard, so the primary-selection target is routed
 * there too rather than dropped; `clipboard` is kept for a future
 * PRIMARY-capable backend. */
YETTY_ANNOTATE("override@ytermsink:sink:clipboard_write")
static struct yetty_ycore_void_result terminal_sink_clipboard_write(struct yetty_yclass_object *obj,
                                                                    const char *text, size_t len,
                                                                    int clipboard)
{
    struct yetty_yterminal_terminal_ptr_result terminal_res = yetty_yterminal_terminal_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, terminal_res, "terminal sink clipboard_write: from_obj");
    struct yetty_yterminal_terminal *terminal = terminal_res.value;
    struct yetty_yclass_object *clip = terminal->context.yetty_context.runtime->clipboard;
    if (!clip) {
        ydebug("terminal sink clipboard_write: no clipboard");
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result sr = yetty_yplatform_clipboard_set_text(clip, text, len);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr,
                        "terminal sink clipboard_write: clipboard set_text failed");
    yinfo("terminal: OSC 52 set %zu bytes to %s", len, clipboard ? "clipboard" : "primary");
    return YETTY_OK_VOID();
}

/* yetty_yterminal_sixel_write_fn impl — the child emitted a sixel image
 * (`DCS <params> q <data> ST`). Decode the payload to RGBA, package it as one
 * yimage prim in a fresh drawable list, and feed it through the same serialized
 * ingest path a ycat image envelope takes, so it anchors and scrolls with the
 * text. Downscaled (aspect-preserving) to the pane width when wider. Built only
 * when the ysixel decoder is compiled in; otherwise the DCS is silently
 * dropped (the callback is never registered). */
YETTY_ANNOTATE("override@ytermsink:sink:sixel_write")
static struct yetty_ycore_void_result terminal_sink_sixel_write(struct yetty_yclass_object *obj,
                                                                const char *data, size_t len)
{
#if defined(YETTY_HAS_YSIXEL) && YETTY_HAS_YSIXEL
    struct yetty_yterminal_terminal_ptr_result terminal_res = yetty_yterminal_terminal_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, terminal_res, "terminal sink sixel_write: from_obj");
    struct yetty_yterminal_terminal *terminal = terminal_res.value;
    struct yetty_yterminal_mime_env env = yetty_yterminal_mime_env_get(terminal);
    struct yetty_ysixel_render_config sixel_config = {
        .max_width_px = (float)(env.cols * env.cell_width),
    };
    struct yetty_ydraw_drawable_list_result render_res =
        yetty_ysixel_render(data, len, &sixel_config);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, render_res, "terminal: sixel decode/render");

    const uint8_t *serialized = NULL;
    size_t serialized_len = yetty_ydraw_drawable_list_serialize(render_res.value, &serialized);
    if (serialized_len == 0 || !serialized) {
        yetty_ydraw_drawable_list_destroy(render_res.value);
        return YETTY_ERR(yetty_ycore_void, "terminal: sixel serialize produced no bytes");
    }
    struct yetty_ycore_void_result ingest_res =
        yetty_yterminal_mime_ingest_serialized(terminal, serialized, serialized_len);
    yetty_ydraw_drawable_list_destroy(render_res.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ingest_res, "terminal: sixel ingest");
    yinfo("terminal: sixel image %zu bytes decoded and anchored", len);
    return YETTY_OK_VOID();
#else
    (void)obj;
    (void)data;
    (void)len;
    return YETTY_OK_VOID();
#endif
}

/* Shared body for "the content layer needs a render frame": kick the event
 * loop. Used by both the sink override below and the yrdawn callback (which
 * needs a plain (void*) signature). */
static struct yetty_ycore_void_result terminal_request_render_impl(
    struct yetty_yterminal_terminal *terminal)
{
    ydebug("terminal request_render: calling request_render");
    terminal->context.yetty_context.event_loop->ops->request_render(
        terminal->context.yetty_context.event_loop);
    return YETTY_OK_VOID();
}

/* yrdawn passes this as a plain (fn, userdata) callback (it has its own
 * callback type), so the void* form stays. */
static struct yetty_ycore_void_result terminal_request_render_callback(void *userdata)
{
    return terminal_request_render_impl((struct yetty_yterminal_terminal *)userdata);
}

YETTY_ANNOTATE("override@ytermsink:sink:request_render")
static struct yetty_ycore_void_result terminal_sink_request_render(struct yetty_yclass_object *obj)
{
    struct yetty_yterminal_terminal_ptr_result terminal_res = yetty_yterminal_terminal_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, terminal_res, "terminal sink request_render: from_obj");
    return terminal_request_render_impl(terminal_res.value);
}

/* set_title: the running program set the terminal title (OSC 0/2). Store it
 * (truncating to the local buffer) and kick a frame so yui's titlebar sync
 * picks the new label up even when no visible cell changed. */
YETTY_ANNOTATE("override@ytermsink:sink:set_title")
static struct yetty_ycore_void_result terminal_sink_set_title(struct yetty_yclass_object *obj,
                                                              const char *title, size_t len)
{
    struct yetty_yterminal_terminal_ptr_result terminal_res = yetty_yterminal_terminal_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, terminal_res, "terminal sink set_title: from_obj");
    struct yetty_yterminal_terminal *terminal = terminal_res.value;
    size_t take = len < sizeof(terminal->title) - 1 ? len : sizeof(terminal->title) - 1;
    if (take > 0 && title) {
        memcpy(terminal->title, title, take);
    } else {
        take = 0;
    }
    terminal->title[take] = 0;
    ydebug("terminal set_title: \"%s\"", terminal->title);
    return terminal_request_render_impl(terminal);
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

/* Push the current (anchor, head) into the content layer (it forwards to its
 * text grid + ydraw canvas). Called on every drag tick. */
static struct yetty_ycore_void_result terminal_push_selection(
    struct yetty_yterminal_terminal *terminal)
{
    struct yetty_ycore_void_result r = yetty_yvterm_vterm_set_selection(
        terminal->grid, terminal->sel_active, terminal->sel_anchor_row, terminal->sel_anchor_col,
        terminal->sel_head_row, terminal->sel_head_col);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "terminal_push_selection: set_selection failed");
    terminal->context.yetty_context.event_loop->ops->request_render(
        terminal->context.yetty_context.event_loop);
    return YETTY_OK_VOID();
}

/* Translate a pane-local pixel coordinate to (row, col) in the visible
 * grid. col is allowed to reach `cols` (one past the last cell) to encode
 * "past EOL" — needed for "drag past the right edge selects to end of
 * line". row is clamped to [0, rows-1]; rows below the grid clamp to the
 * last row, matching how xterm extends selection downward. */
static struct yetty_ycore_void_result terminal_cell_from_local(
    const struct yetty_yterminal_terminal *terminal, float lx, float ly, uint32_t *out_row,
    uint32_t *out_col)
{
    float cell_w = 10.0f;
    float cell_h = 20.0f;
    struct pixel_size_result cell_res = yetty_yvterm_vterm_cell_size(terminal->grid);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, cell_res, "terminal_cell_from_local: cell size");
    struct yetty_ycore_pixel_size cell = cell_res.value;
    if (cell.width > 0.0f) {
        cell_w = cell.width;
    }
    if (cell.height > 0.0f) {
        cell_h = cell.height;
    }
    /* The grid renders on the resolved content rect (a client reservation
     * may offset it inside the pane) — shift into content-rect space so
     * cell math lands on the rows the user actually sees. */
    float content_x = 0.0f, content_y = 0.0f;
    struct yetty_ycore_void_result content_rect_res =
        yetty_yvterm_vterm_get_content_rect(terminal->grid, &content_x, &content_y, NULL, NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, content_rect_res,
                        "terminal_cell_from_local: content rect");
    lx -= content_x;
    ly -= content_y;
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
    return YETTY_OK_VOID();
}

/* Collect the content layer's selection text (it concatenates its text grid +
 * ydraw glyph contributions in reading order). */
static struct yetty_ycore_void_result terminal_collect_selection_text(
    struct yetty_yterminal_terminal *terminal, struct yetty_ycore_buffer *out)
{
    if (!terminal->sel_active) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result r = yetty_yvterm_vterm_get_selection_text(terminal->grid, out);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "grid selection text");
    return YETTY_OK_VOID();
}

/* Ctrl+Shift+C — extract the selection, set it on the system clipboard,
 * leave the highlight in place so the user can verify. No-op when nothing
 * is selected or no clipboard manager exists (headless). */
static struct yetty_ycore_void_result terminal_copy_selection(
    struct yetty_yterminal_terminal *terminal)
{
    if (!terminal->sel_active) {
        return YETTY_OK_VOID();
    }
    struct yetty_yclass_object *clipboard = terminal->context.yetty_context.runtime->clipboard;
    if (!clipboard) {
        ydebug("terminal_copy_selection: no clipboard");
        return YETTY_OK_VOID();
    }

    struct yetty_ycore_buffer_result br = yetty_ycore_buffer_create(256);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "terminal_copy_selection: buffer_create failed");
    struct yetty_ycore_buffer buf = br.value;

    struct yetty_ycore_void_result cr = terminal_collect_selection_text(terminal, &buf);
    if (YETTY_IS_ERR(cr)) {
        yetty_ycore_buffer_destroy(&buf);
        return YETTY_ERR(yetty_ycore_void, "terminal_copy_selection: collect_selection_text failed",
                         cr);
    }

    if (buf.size > 0) {
        struct yetty_ycore_void_result sr =
            yetty_yplatform_clipboard_set_text(clipboard, (const char *)buf.data, buf.size);
        if (YETTY_IS_ERR(sr)) {
            yetty_ycore_buffer_destroy(&buf);
            return YETTY_ERR(yetty_ycore_void, "terminal_copy_selection: clipboard set_text failed",
                             sr);
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
    struct yetty_yterminal_terminal *terminal)
{
    struct yetty_yclass_object *clipboard = terminal->context.yetty_context.runtime->clipboard;
    if (!clipboard) {
        ydebug("terminal_paste_clipboard: no clipboard");
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result r = yetty_yplatform_clipboard_request_paste(clipboard);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r,
                        "terminal_paste_clipboard: clipboard request_paste failed");
    return YETTY_OK_VOID();
}

/* Clear any active selection and tell the layers to drop their highlight. */
static struct yetty_ycore_void_result terminal_clear_selection(
    struct yetty_yterminal_terminal *terminal)
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
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "terminal_clear_selection: push_selection failed");
    return YETTY_OK_VOID();
}

/* Event handler - only for PTY poll events registered directly with event loop
 */
static struct yetty_ycore_int_result terminal_event_handler(
    struct yetty_yevent_event_listener *listener, const struct yetty_yui_event *event)
{
    struct yetty_yterminal_terminal *terminal =
        container_of(listener, struct yetty_yterminal_terminal, listener);

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
static struct yetty_ycore_void_result terminal_render_frame(
    struct yetty_yterminal_terminal *terminal, struct yetty_ydraw_target *target, int force_redraw)
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
    /* One render walk: the content (text grid + ydraw canvas) is the lowest-z
     * child of the root container, with producer figures (ymgui, yrdawn, yplot,
     * shader-glyph, …) stacked above. The container paints children back-to-
     * front in a single pass, each into big_target with LoadOp_Load. The grid
     * figure force-repaints the content every walk so figures composited above
     * always sit on freshly-drawn pixels — so force_redraw needs no separate
     * handling here. */
    (void)force_redraw;
    if (!terminal->root_container_obj) {
        return YETTY_ERR(yetty_ycore_void, "terminal_render_frame: no root container to render");
    }
    /* Publish the current scroll position to the figure container so inline
     * cards (ygui/ymgui/…) re-anchor and slide with the surrounding text. The
     * top visible content row is the scrollback view_top when active, else the
     * live anchor; cards offset from the row they were created at. */
    {
        int view_active = 0;
        uint64_t view_top = 0;
        struct yetty_ycore_void_result view_res =
            yetty_yvterm_vterm_get_view(terminal->grid, &view_active, &view_top);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, view_res, "terminal_render_frame: get_view");
        uint64_t content_root_row = view_top;
        if (!view_active) {
            struct yetty_ycore_uint64_result live_res = terminal_live_anchor(terminal);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, live_res, "terminal_render_frame: live anchor");
            content_root_row = live_res.value;
        }
        struct pixel_size_result cell_res = yetty_yvterm_vterm_cell_size(terminal->grid);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, cell_res, "terminal_render_frame: cell size");
        struct yetty_ycore_void_result ctx_res = yetty_yfigure_container_set_scroll_context(
            terminal->root_container_obj, content_root_row, (float)cell_res.value.height);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, ctx_res, "terminal_render_frame: scroll context");
    }
    {
        struct yetty_yfigure_figure_ptr_result rf_res =
            yetty_yfigure_container_as_figure(terminal->root_container_obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rf_res, "terminal_render_frame: root as_figure");
        struct yetty_yfigure_figure *rf = rf_res.value;
        struct yetty_ycore_void_result render_res =
            yetty_yfigure_render((struct yetty_yclass_object *)rf - 1, target);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, render_res,
                            "terminal_render_frame: root container render");
        struct yetty_ycore_void_result dirty_clear_res =
            yetty_yfigure_figure_dirty_set((struct yetty_yclass_object *)(rf)-1, 0);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dirty_clear_res,
                            "terminal_render_frame: clear container dirty");
    }
    ytime_report(layers);

    ydebug("terminal_render_frame: done (root container single pass, no blend)");
    ytime_report(frame_render);
    return YETTY_OK_VOID();
}

/* Drive the wire state machine — pulls PTY bytes (sync-read backends) and
 * dispatches to registered layers. */
static struct yetty_ycore_void_result terminal_read_pty(struct yetty_yterminal_terminal *terminal)
{
    struct yetty_ycore_void_result r = yetty_ywire_wire_statemachine_process(terminal->sm);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "terminal_read_pty: wire_statemachine_process failed");
    /* Same as the async pipe-read path: the content grid aggregates its
     * sub-renderers' dirty bits via is_dirty; its own figure dirty is never
     * set. */
    struct yetty_ycore_int_result dirty_res = yetty_yvterm_vterm_is_dirty(terminal->grid);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dirty_res, "terminal_read_pty: grid is_dirty");
    if (dirty_res.value) {
        terminal->context.yetty_context.event_loop->ops->request_render(
            terminal->context.yetty_context.event_loop);
    }
    return YETTY_OK_VOID();
}

/* Terminal creation/destruction */

struct yetty_yterminal_terminal_result yetty_yterminal_terminal_open(
    struct yetty_ycore_grid_size grid_size, const struct yetty_context *yetty_context)
{
    struct yetty_yterminal_terminal *terminal;
    uint32_t cols = grid_size.cols;
    uint32_t rows = grid_size.rows;

    ydebug("terminal_open: cols=%u rows=%u", cols, rows);

    /* Allocate the terminal AS a yclass object (yterminal:terminal) so it is
     * the object a connecting tool receives as its session root. The struct
     * is the class data slice; consumers keep holding the data pointer
     * (view embedded first, unchanged), while the RPC layer reaches the
     * object via yetty_yterminal_terminal_to(). object_alloc zero-fills the
     * slice, same as the former calloc. */
    struct yetty_yclass_ctx terminal_ctx = {0};
    struct yetty_yclass_object_ptr_result object_res =
        yetty_yterminal_terminal_create(&terminal_ctx);
    if (YETTY_IS_ERR(object_res)) {
        return YETTY_ERR(yetty_yterminal_terminal, "failed to allocate terminal object",
                         object_res);
    }
    struct yetty_yclass_object *terminal_object = object_res.value;
    struct yetty_yterminal_terminal_ptr_result terminal_data_res =
        yetty_yterminal_terminal_from(terminal_object);
    if (YETTY_IS_ERR(terminal_data_res)) {
        struct yetty_ycore_void_result free_res = yetty_yclass_object_free(terminal_object);
        if (YETTY_IS_ERR(free_res)) {
            yetty_ycore_error_destroy(free_res.error);
        }
        return YETTY_ERR(yetty_yterminal_terminal, "terminal_open: from_obj", terminal_data_res);
    }
    terminal = terminal_data_res.value;

    /* Initialize view base */
    terminal->view.ops = &terminal_view_ops;
    terminal->view.id = yetty_yui_view_next_id();

    terminal->cols = cols;
    terminal->rows = rows;
    terminal->context.yetty_context = *yetty_context;
    /* Record the density the cell metrics are about to be derived at
     * (vterm bakes font_size = config * content_scale at creation).
     * apply_pane_geometry rescales the stride if this ever diverges
     * from the runtime's live value. */
    terminal->layout_content_scale = 1.0f;
    if (yetty_context->runtime) {
        float creation_scale = yetty_context->runtime->gpu.app_gpu_context.content_scale;
        if (creation_scale > 0.0f) {
            terminal->layout_content_scale = creation_scale;
        }
    }

    /* Validate event loop from context */
    if (!yetty_context->event_loop) {
        ydebug("terminal_create: no event_loop in context");
        struct yetty_ycore_void_result cleanup_free = yetty_yclass_object_free(terminal_object);
        if (YETTY_IS_ERR(cleanup_free)) {
            yetty_ycore_error_destroy(cleanup_free.error);
        }
        return YETTY_ERR(yetty_yterminal_terminal, "no event_loop in context");
    }
    ydebug("terminal_create: using event_loop at %p",
           (void *)terminal->context.yetty_context.event_loop);

    /* Set up listener for PTY poll events */
    terminal->listener.handler = terminal_event_handler;

    /* PTY factory is required — every supported platform installs one at
     * startup. A missing factory means yetty_context was constructed wrong. */
    struct yetty_yplatform_pty_factory *pty_factory = yetty_context->pty_factory;
    if (!pty_factory || !pty_factory->ops || !pty_factory->ops->create_pty) {
        struct yetty_ycore_void_result cleanup_free = yetty_yclass_object_free(terminal_object);
        if (YETTY_IS_ERR(cleanup_free)) {
            yetty_ycore_error_destroy(cleanup_free.error);
        }
        return YETTY_ERR(
            yetty_yterminal_terminal,
            "terminal_create: yetty_context.pty_factory is NULL or has no create_pty op");
    }

    /* Create PTY */
    struct yetty_yplatform_pty_ptr_result pty_res =
        pty_factory->ops->create_pty(pty_factory, terminal->context.yetty_context.event_loop);
    YETTY_RETURN_IF_ERR(yetty_yterminal_terminal, pty_res,
                        "terminal_create: pty_factory create_pty failed");
    terminal->context.pty = pty_res.value;
    ydebug("terminal_create: PTY created at %p", (void *)terminal->context.pty);

    /* Create wire state machine — owns the PTY pointer, the decode stack,
     * and the per-OSC-code layer registry. */
    struct yetty_ywire_wire_statemachine_ptr_result sm_res =
        yetty_ywire_wire_statemachine_create(terminal->context.pty);
    YETTY_RETURN_IF_ERR(yetty_yterminal_terminal, sm_res,
                        "terminal_create: wire_statemachine_create failed");
    terminal->sm = sm_res.value;
    ydebug("terminal_create: wire state machine created");

    /* Long-lived yface for emit_*. One per terminal — out_buf is cleared
     * after every send so it stays at the steady-state high-water mark
     * rather than growing per-event. */
    struct yetty_yface_ptr_result yr = yetty_yface_create();
    YETTY_RETURN_IF_ERR(yetty_yterminal_terminal, yr,
                        "terminal_create: emit_yface yetty_yface_create failed");
    terminal->emit_yface = yr.value;

    /* Register PTY pipe with the event loop for async-delivery backends.
     * Sync-read backends (e.g. memory-pty) legitimately return NULL here;
     * the SM pulls bytes itself via pty->ops->read on each process(). */
    struct yetty_platform_pty_pipe_source *pipe_source =
        terminal->context.pty->ops->pipe_source(terminal->context.pty);
    if (pipe_source) {
        struct yetty_yevent_pipe_id_result pipe_res =
            terminal->context.yetty_context.event_loop->ops->register_pty_pipe(
                terminal->context.yetty_context.event_loop, pipe_source, terminal_pty_pipe_alloc,
                terminal_pty_pipe_read, terminal);
        YETTY_RETURN_IF_ERR(yetty_yterminal_terminal, pipe_res,
                            "terminal_create: event_loop register_pty_pipe failed");
        terminal->pty_pipe_id = pipe_res.value;
        ydebug("terminal_create: PTY pipe registered");
    }

    /* Create the content grid — the yvterm:grid figure for this pane. It owns
     * the libvterm text grid and the ydraw rich-content canvas, drives both
     * render passes, and routes all the text<->ydraw plumbing (scroll, cursor,
     * alt-screen, clear, selection, view-top) internally. It talks back to the
     * terminal (PTY write, render, mouse subscription, clipboard, sixel) by
     * dispatching the ytermsink:sink methods on the terminal object we pass as
     * its host. It is seated as the lowest-z child of the root container below. */
    struct yetty_yclass_object_ptr_result grid_res =
        yetty_yvterm_vterm_figure_create(cols, rows, yetty_context, terminal_object);
    YETTY_RETURN_IF_ERR(yetty_yterminal_terminal, grid_res,
                        "terminal_create: grid figure create failed");
    terminal->grid = grid_res.value;
    struct pixel_size_result content_cell_res = yetty_yvterm_vterm_cell_size(terminal->grid);
    YETTY_RETURN_IF_ERR(yetty_yterminal_terminal, content_cell_res,
                        "terminal_create: content cell size");
    struct yetty_ycore_pixel_size content_cell = content_cell_res.value;
    ydebug("terminal_create: content grid figure created");

    /* Register the grid's wire handlers: text grid as the default
     * (raw-passthrough) sink, ydraw canvas for the YDRAW DCS codes. */
    struct yetty_ycore_void_result rr =
        yetty_yvterm_vterm_register_wire(terminal->grid, terminal->sm);
    YETTY_RETURN_IF_ERR(yetty_yterminal_terminal, rr, "terminal_create: grid register_wire failed");
    ydebug("terminal_create: content grid wire handlers registered");

    /* Push the real cell+pixel dims down to the PTY before any child process
     * can read TIOCGWINSZ. The PTY's create_pty path forks at 80x24 with
     * ws_xpixel/ws_ypixel=0; without this catch-up, every client that needs
     * the pane pixel area (ymgui demo, GPU clients) sees zero and guesses. */
    if (terminal->context.pty->ops->resize) {
        struct yetty_ycore_void_result pr = terminal->context.pty->ops->resize(
            terminal->context.pty, cols, rows, cols * (uint32_t)content_cell.width,
            rows * (uint32_t)content_cell.height);
        YETTY_RETURN_IF_ERR(yetty_yterminal_terminal, pr,
                            "terminal_create: initial pty resize with pixel dims failed");
    }

    /* Shader-glyph is a figure owned by the content grid's text layer; it
     * renders it as a pass inside the grid's render. Not handled here. */

    /* ymgui has moved out of the layer stack. ImGui frames now flow as
     * yetty_ymgui_figure children of the root container, created and driven
     * through the yclass-RPC figure-mutation slots. Mouse / focus routing
     * scans the root container, the figures paint themselves, and
     * ymgui-layer.c is gone. */

    /* yrdawn now flows through the figure tree like ymgui — the factory
     * args wired into yframework_register_figure_factories below carry the
     * emit/request_render callbacks, and a create_child of kind YRDAWN mints
     * each remote canvas. yrdawn-layer.c is gone. */

    /* Root container — positioned-figure root of the rendering stack. Owned
     * directly by the terminal (it is not the content layer). The render loop
     * calls root_container->ops->render after the content layer; producers
     * mutate it through the yclass-RPC DCS server registered below.
     *
     * Default MSDF font: ygui-emitting subprocesses (ygreeter, ytop, …)
     * ship widget labels as TEXT_DRAWABLE_LIST records; each scene figure
     * the container mints needs a font at slot 0 to expand them into
     * renderable glyphs. Font load failure is non-fatal — labels won't
     * render, but the terminal stays up. */
    {
        struct yetty_yconfig_config *config = yetty_context->runtime->config;
        const char *fonts_dir = config->ops->get_string(config, "paths/fonts", "");
        const char *shaders_dir = config->ops->get_string(config, "paths/shaders", "");
        const char *font_family = "DejaVuSansMNerdFontMono";
        const char *cache_dir = config->ops->get_string(config, "paths/cache", "");
        char cdb_path[768];
        char shader_path[768];
        struct yetty_ycore_void_result cdb_res = yetty_yfont_msdf_resolve_cdb(
            yetty_context->runtime->gpu.msdf_generator, fonts_dir, cache_dir, font_family,
            "-Regular", cdb_path, sizeof(cdb_path));
        if (YETTY_IS_ERR(cdb_res)) {
            ywarn("terminal_create: root font '%s' has no usable CDB: %s", font_family,
                  cdb_res.error.msg);
            yetty_ycore_error_destroy(cdb_res.error);
        } else {
            snprintf(shader_path, sizeof(shader_path), "%s/msdf-font.wgsl", shaders_dir);
            struct yetty_font_font_result font_res =
                yetty_yfont_msdf_font_create(cdb_path, shader_path, "terminal_root");
            if (YETTY_IS_OK(font_res)) {
                terminal->compositor_font = font_res.value;
                struct yetty_ycore_void_result load_res =
                    terminal->compositor_font->ops->load_basic_latin(terminal->compositor_font);
                if (YETTY_IS_ERR(load_res)) {
                    ywarn("terminal_create: root font load_basic_latin: %s", load_res.error.msg);
                    yetty_ycore_error_destroy(load_res.error);
                }
                ydebug("terminal_create: root default font ready (%s)", cdb_path);
            } else {
                ywarn("terminal_create: root font load failed (%s): %s", cdb_path,
                      font_res.error.msg);
                yetty_ycore_error_destroy(font_res.error);
            }
        }
    }

    /* Complex-prim factory — handles yplot/yimage/yshadertoy/ymesh/yvideo
     * prims that arrive embedded in inbound YDRAW_BIN figure payloads. Each
     * concrete factory (yplot_factory_create etc.) builds its own
     * pipeline lazily on the first create_instance call. */
    {
        struct yetty_ydraw_complex_factory_ptr_result ffr = yetty_ydraw_complex_factory_create(
            yetty_context->runtime->gpu.device, yetty_context->runtime->gpu.queue,
            yetty_context->runtime->gpu.surface_format, yetty_context->runtime->gpu.allocator,
            yetty_context->event_loop);
        YETTY_RETURN_IF_ERR(yetty_yterminal_terminal, ffr,
                            "terminal_create: raw_complex_factory create");
        terminal->complex_factory = ffr.value;
        struct yetty_ydraw_concrete_factory *yplot_f = yetty_yplot_factory_create();
        if (yplot_f) {
            yplot_f->destroy = yetty_yplot_factory_destroy;
            struct yetty_ycore_void_result rr =
                yetty_ydraw_complex_factory_register(terminal->complex_factory, yplot_f);
            if (YETTY_IS_ERR(rr)) {
                yetty_ycore_error_destroy(rr.error);
                yetty_yplot_factory_destroy(yplot_f);
            }
        }
        struct yetty_ydraw_concrete_factory *yimage_f = yetty_yimage_factory_create();
        if (yimage_f) {
            yimage_f->destroy = yetty_yimage_factory_destroy;
            struct yetty_ycore_void_result rr =
                yetty_ydraw_complex_factory_register(terminal->complex_factory, yimage_f);
            if (YETTY_IS_ERR(rr)) {
                yetty_ycore_error_destroy(rr.error);
                yetty_yimage_factory_destroy(yimage_f);
            }
        }
        struct yetty_ydraw_concrete_factory *yshadertoy_f = yetty_yshadertoy_prim_factory_create();
        if (yshadertoy_f) {
            yshadertoy_f->destroy = yetty_yshadertoy_prim_factory_destroy;
            struct yetty_ycore_void_result rr =
                yetty_ydraw_complex_factory_register(terminal->complex_factory, yshadertoy_f);
            if (YETTY_IS_ERR(rr)) {
                yetty_ycore_error_destroy(rr.error);
                yetty_yshadertoy_prim_factory_destroy(yshadertoy_f);
            }
        }
#if defined(YETTY_HAS_YMESH) && YETTY_HAS_YMESH
        struct yetty_ydraw_concrete_factory *ymesh_f = yetty_ymesh_factory_create();
        if (ymesh_f) {
            ymesh_f->destroy = yetty_ymesh_factory_destroy;
            struct yetty_ycore_void_result rr =
                yetty_ydraw_complex_factory_register(terminal->complex_factory, ymesh_f);
            if (YETTY_IS_ERR(rr)) {
                yetty_ycore_error_destroy(rr.error);
                yetty_ymesh_factory_destroy(ymesh_f);
            }
        }
#endif
#if defined(YETTY_HAS_YVIDEO) && YETTY_HAS_YVIDEO
        struct yetty_ydraw_concrete_factory *yvideo_f = yetty_yvideo_factory_create();
        if (yvideo_f) {
            yvideo_f->destroy = yetty_yvideo_factory_destroy;
            struct yetty_ycore_void_result rr =
                yetty_ydraw_complex_factory_register(terminal->complex_factory, yvideo_f);
            if (YETTY_IS_ERR(rr)) {
                yetty_ycore_error_destroy(rr.error);
                yetty_yvideo_factory_destroy(yvideo_f);
            }
        }
#endif
    }

    /* Drawable-list registry for ingesting inbound YDRAW_BIN record streams —
     * borrowed from the framework's shared instance (immutable after
     * framework create), so the iterator can step SDF primitives,
     * cmd/font/text-list records, and complex records alike. */
    terminal->ydraw_registry = terminal->context.yetty_context.runtime->drawable_registry;
    if (!terminal->ydraw_registry) {
        return YETTY_ERR(yetty_yterminal_terminal,
                         "terminal_create: runtime has no drawable registry");
    }

    terminal->figure_args.default_font = terminal->compositor_font;
    terminal->figure_args.complex_factory = terminal->complex_factory;

    struct yetty_yfigure_registry_ptr_result reg_res = yetty_yfigure_registry_create();
    YETTY_RETURN_IF_ERR(yetty_yterminal_terminal, reg_res,
                        "terminal_create: figure registry create failed");
    terminal->figure_registry = reg_res.value;
    {
        /* The "ygrid" kind token is the legacy chrome-surface kind: absolute
         * coordinates (client widgets emit at their absolute rect), minted
         * as a retained scene. The token stays on the wire for compat. */
        terminal->figure_args.absolute_coords = 1;
        struct yetty_ycore_void_result rf = yetty_yscene_register_factory_for_kind(
            terminal->figure_registry, yetty_yfigure_kind_token("ygrid"), &terminal->figure_args);
        YETTY_RETURN_IF_ERR(yetty_yterminal_terminal, rf,
                            "terminal_create: yscene chrome-kind register");
        /* "yscroll" is the CONTENT kind (#685 Phase 2): LOCAL coordinates,
         * suited to producer content — content is emitted in document
         * coords, the figure is sized to the viewport and scrolled natively
         * via set_child_scroll (content = set_content_size so records past
         * the viewport are NOT clipped). ychromium's web page and every
         * ygui producer widget (plot / image / video content, shipped as
         * complex records in the child body) mint this kind. The default
         * "ygrid" kind is absolute (chrome/widgets) and cannot scroll
         * content taller than its rect.
         *
         * The old "yplot"/"yimage"/"yvideo"/"yzoo"/"yjungle" alias kinds
         * are RETIRED — nothing mints them. */
        terminal->yscene_factory_args.complex_factory = terminal->complex_factory;
        terminal->yscene_factory_args.default_font = terminal->compositor_font;
        struct yetty_ycore_void_result kr = yetty_yscene_register_factory_for_kind(
            terminal->figure_registry, yetty_yfigure_kind_token("yscroll"),
            &terminal->yscene_factory_args);
        YETTY_RETURN_IF_ERR(yetty_yterminal_terminal, kr,
                            "terminal_create: yscene yscroll register");

        /* "yscene" — the retained scene graph (#691), the canonical kind.
         * Local coordinates, same args bundle as the content kinds. */
        {
            struct yetty_ycore_void_result scene_reg_res = yetty_yscene_register_factory(
                terminal->figure_registry, &terminal->yscene_factory_args);
            YETTY_RETURN_IF_ERR(yetty_yterminal_terminal, scene_reg_res,
                                "terminal_create: yscene register_factory");
        }

        /* Framework-owned figure kinds (ymgui, yrdawn; ygui as it
         * migrates). yrdawn's emit_osc / request_render are
         * terminal-scoped — install them on the framework's per-kind
         * bundle before registration so the freshly-minted yrdawn
         * factory captures them. */
        /* Best-effort: yrdawn factory args are absent on builds
         * compiled without the yrdawn server (webasm). The error
         * carries the reason (NULL framework vs feature disabled);
         * we just skip the callback install in that case. */
        struct yetty_yrdawn_factory_args_ptr_result yr =
            yetty_yframework_factory_args_yrdawn(yetty_context->runtime);
        if (YETTY_IS_OK(yr)) {
            struct yetty_yrdawn_factory_args *yrdawn_args = yr.value;
            yrdawn_args->emit_osc_fn = terminal_layer_emit_osc;
            yrdawn_args->emit_osc_user = terminal;
            yrdawn_args->request_render_fn = terminal_request_render_callback;
            yrdawn_args->request_render_user = terminal;
        } else {
            yetty_ycore_error_destroy(yr.error);
        }

        struct yetty_ycore_void_result fr = yetty_yframework_register_figure_factories(
            yetty_context->runtime, terminal->figure_registry, yetty_context);
        YETTY_RETURN_IF_ERR(yetty_yterminal_terminal, fr,
                            "terminal_create: framework register_figure_factories");
    }

    struct yetty_ycore_rectangle root_rect = {
        .min = {.x = 0.0f, .y = 0.0f},
        .max = {.x = (float)cols * content_cell.width, .y = (float)rows * content_cell.height},
    };
    /* yclass-uniform construction: same call shape on both sides of
     * an RPC session. Local mint here (no session set) — the codegen
     * factory allocates the yclass object and runs the constructor
     * slot (sets the ops vtable). We then wire the per-instance
     * runtime state (rect, context, registry) via the setters. */
    struct yetty_yclass_ctx yclass_ctx = {0};
    struct yetty_yclass_object_ptr_result obj_res = yetty_yfigure_container_create(&yclass_ctx);
    YETTY_RETURN_IF_ERR(yetty_yterminal_terminal, obj_res,
                        "terminal_create: root_container create failed");
    terminal->root_container_obj = obj_res.value;
    yetty_yfigure_container_set_context(terminal->root_container_obj, yetty_context);
    yetty_yfigure_container_set_registry(terminal->root_container_obj, terminal->figure_registry);
    yetty_yfigure_container_set_rect(terminal->root_container_obj, root_rect);
    ydebug("terminal_create: root container ready");

    /* Seat the content grid (created above) as the lowest-z child of the root
     * container so it renders through the figure path, beneath every producer
     * figure, instead of a bespoke pre-container pass. The container owns the
     * figure; the terminal borrows terminal->grid. */
    {
        struct yetty_yfigure_figure_ptr_result grid_figure_res =
            yetty_yvterm_vterm_as_figure(terminal->grid);
        YETTY_RETURN_IF_ERR(yetty_yterminal_terminal, grid_figure_res,
                            "terminal_create: grid as figure failed");
        struct yetty_ycore_void_result add_res = yetty_yfigure_container_add_child(
            terminal->root_container_obj, grid_figure_res.value, YETTY_YTERMINAL_GRID_FIGURE_ID);
        YETTY_RETURN_IF_ERR(yetty_yterminal_terminal, add_res,
                            "terminal_create: add grid figure to root container failed");
        /* The content grid is the host's own structural figure, not part of the
         * producer-managed set. Protect it so a client's CLEAR_ALL (e.g. an app
         * dropping its figures at exit) and a full-screen erase / reset cannot
         * destroy the text layer along with the compositor children. */
        struct yetty_ycore_void_result protect_res = yetty_yfigure_container_protect_child(
            terminal->root_container_obj, YETTY_YTERMINAL_GRID_FIGURE_ID);
        YETTY_RETURN_IF_ERR(yetty_yterminal_terminal, protect_res,
                            "terminal_create: protect grid figure failed");
    }

    /* Rich content (ycat/ypdf/markdown/plots) is owned by yvterm's own grid —
     * complexes + raw SDF/text records are anchored per line on its scroll ring
     * and drawn by vterm's render. No separate figure surface. */

    /* On a full-screen erase / reset (CSI 2J/3J or RIS — e.g. `clear`, Ctrl-L,
     * `reset`) the content grid wipes the text grid and ydraw canvas, but the
     * positioned compositor figures live in the root container, which it does
     * not own. Hook into the same clear path to wipe them too. */
    struct yetty_ycore_void_result clear_hook_res = yetty_yvterm_vterm_set_clear_hook(
        terminal->grid, terminal_clear_figures_callback, terminal);
    YETTY_RETURN_IF_ERR(yetty_yterminal_terminal, clear_hook_res,
                        "terminal_create: set clear hook failed");

    /* On a terminal reset (RIS via `reset`, or DECSTR) drop the client-input
     * subscriptions the way libvterm drops its DEC mouse modes — otherwise a
     * client that died without unsubscribing leaves the pane spraying mouse
     * envelopes at the shell with no user-reachable cure. */
    struct yetty_ycore_void_result reset_hook_res = yetty_yvterm_vterm_set_reset_hook(
        terminal->grid, terminal_reset_subscriptions_callback, terminal);
    YETTY_RETURN_IF_ERR(yetty_yterminal_terminal, reset_hook_res,
                        "terminal_create: set reset hook failed");

    /* Figure re-materialization for the tiered scroll buffer: lines aging past
     * the scrollback hot window lose their figure runtimes but keep the
     * creating wire envelopes; this hook replays an envelope through the
     * complex factory when such a line scrolls back into view. */
    struct yetty_ycore_void_result materialize_res = yetty_yvterm_vterm_set_materialize(
        terminal->grid, terminal_materialize_figure_callback, terminal);
    YETTY_RETURN_IF_ERR(yetty_yterminal_terminal, materialize_res,
                        "terminal_create: set materialize hook failed");

    /* Client-input reinject — pane-wide subscribers bounce unconsumed
     * mouse events back here for default handling (wheel → scrollback).
     * Same DCS transport as every client→server yface emit. */
    rr = yetty_ywire_wire_statemachine_register(terminal->sm, YETTY_YWIRE_ENVELOPE_DCS,
                                                YETTY_OSC_CS_CLIENT_INPUT_REINJECT, /*has_args=*/1,
                                                terminal_reinject_process_input, terminal);
    YETTY_RETURN_IF_ERR(yetty_yterminal_terminal, rr,
                        "terminal_create: register client-input reinject");
    ydebug("terminal_create: client-input reinject registered for DCS %d",
           YETTY_OSC_CS_CLIENT_INPUT_REINJECT);

    /* Exit-window input HOLD — a client arms the host input barrier at the
     * start of its teardown (no payload; safe replacement for the removed
     * handback). Distinct userdata for its own handler coroutine. */
    terminal->input_hold_handler_self = terminal;
    rr = yetty_ywire_wire_statemachine_register(
        terminal->sm, YETTY_YWIRE_ENVELOPE_DCS, YETTY_OSC_CS_CLIENT_INPUT_HOLD, /*has_args=*/1,
        terminal_input_hold_process_input, &terminal->input_hold_handler_self);
    YETTY_RETURN_IF_ERR(yetty_yterminal_terminal, rr,
                        "terminal_create: register client-input hold");
    ydebug("terminal_create: client-input hold registered for DCS %d",
           YETTY_OSC_CS_CLIENT_INPUT_HOLD);

    /* Content inset — a client reserves a band of the pane for its own
     * overlay; we shrink the libvterm surface to the inset content rect.
     * Same DCS transport as every client→server yface emit (has_args=1).
     * Distinct userdata (&inset_handler_self) so the SM gives this its own
     * handler coroutine instead of sharing the reinject one (both would
     * otherwise key off the bare-terminal pointer). */
    terminal->inset_handler_self = terminal;
    rr = yetty_ywire_wire_statemachine_register(
        terminal->sm, YETTY_YWIRE_ENVELOPE_DCS, YETTY_OSC_CS_CONTENT_INSET, /*has_args=*/1,
        terminal_content_inset_process_input, &terminal->inset_handler_self);
    YETTY_RETURN_IF_ERR(yetty_yterminal_terminal, rr, "terminal_create: register content inset");
    ydebug("terminal_create: content inset registered for DCS %d", YETTY_OSC_CS_CONTENT_INSET);

    /* Pane-wide subscription — today only the geometry bit is honoured: a
     * logical-pixel client asks for size + content_scale without taking over
     * mouse or key routing. */
    terminal->client_sub_handler_self = terminal;
    rr = yetty_ywire_wire_statemachine_register(
        terminal->sm, YETTY_YWIRE_ENVELOPE_DCS, YETTY_OSC_CS_CLIENT_INPUT_SUB, /*has_args=*/1,
        terminal_client_sub_process_input, &terminal->client_sub_handler_self);
    YETTY_RETURN_IF_ERR(yetty_yterminal_terminal, rr, "terminal_create: register client sub");
    ydebug("terminal_create: client input sub registered for DCS %d",
           YETTY_OSC_CS_CLIENT_INPUT_SUB);

    /* Content rect — the inset's generalisation: the client places the text
     * surface on an explicit (edge-anchored) rect inside the pane. */
    terminal->content_rect_handler_self = terminal;
    rr = yetty_ywire_wire_statemachine_register(
        terminal->sm, YETTY_YWIRE_ENVELOPE_DCS, YETTY_OSC_CS_CONTENT_RECT, /*has_args=*/1,
        terminal_content_rect_process_input, &terminal->content_rect_handler_self);
    YETTY_RETURN_IF_ERR(yetty_yterminal_terminal, rr, "terminal_create: register content rect");
    ydebug("terminal_create: content rect registered for DCS %d", YETTY_OSC_CS_CONTENT_RECT);

    /* Anchored rich content (yplot/yimage/SDF drawings) arrives as YDRAW DCS
     * envelopes from producers (yplot, ycat, …). One handler coroutine (distinct
     * userdata) serves CLEAR + BIN + OVERLAY, parsing records into the content
     * grid's per-line rich model, which the grid figure then renders. */
    terminal->ydraw_handler_self = terminal;
    {
        const int ydraw_codes[] = {YETTY_DCS_YDRAW_CLEAR, YETTY_DCS_YDRAW_BIN,
                                   YETTY_DCS_YDRAW_OVERLAY};
        for (size_t i = 0; i < sizeof(ydraw_codes) / sizeof(ydraw_codes[0]); ++i) {
            rr = yetty_ywire_wire_statemachine_register(
                terminal->sm, YETTY_YWIRE_ENVELOPE_DCS, ydraw_codes[i], /*has_args=*/1,
                terminal_ydraw_process_input, &terminal->ydraw_handler_self);
            YETTY_RETURN_IF_ERR(yetty_yterminal_terminal, rr,
                                "terminal_create: register ydraw handler");
        }
    }
    ydebug("terminal_create: ydraw handlers registered (CLEAR/BIN/OVERLAY)");

    /* Raw-file envelopes (YETTY_DCS_MIME_FILE): the payload is an unrendered
     * file; the terminal detects the type (ymime) and runs the renderer
     * itself. Policy (per-type enable + size caps) and dispatch live in
     * terminal-mime.c. */
    terminal->mime_handler_self = terminal;
    rr = yetty_ywire_wire_statemachine_register(
        terminal->sm, YETTY_YWIRE_ENVELOPE_DCS, YETTY_DCS_MIME_FILE, /*has_args=*/1,
        yetty_yterminal_mime_process_input, &terminal->mime_handler_self);
    YETTY_RETURN_IF_ERR(yetty_yterminal_terminal, rr, "terminal_create: register mime handler");
    ydebug("terminal_create: mime-file handler registered for DCS %d", YETTY_DCS_MIME_FILE);

    /* Shader-effect OSC handlers (pre/post/coord). Each code gets a distinct
     * self-pointer so the SM spawns an independent coroutine per class. */
    {
        const int effect_codes[3] = {YETTY_OSC_CS_EFFECT_PRE, YETTY_OSC_CS_EFFECT_POST,
                                     YETTY_OSC_CS_EFFECT_COORD};
        for (size_t i = 0; i < 3; ++i) {
            terminal->effect_handler_self[i] = terminal;
            rr = yetty_ywire_wire_statemachine_register(
                terminal->sm, YETTY_YWIRE_ENVELOPE_OSC, effect_codes[i], /*has_args=*/0,
                terminal_effect_process_input, &terminal->effect_handler_self[i]);
            YETTY_RETURN_IF_ERR(yetty_yterminal_terminal, rr,
                                "terminal_create: register effect handler");
        }
    }
    ydebug("terminal_create: effect OSC handlers registered (666667/8/9)");

    /* This process is about to act as a yclass RPC / remote-object
     * server, so bring up the per-module discovery hooks explicitly.
     * The accessor lookups feed yetty_yclass_by_name()'s registry-miss
     * path (server CREATE / GET_CLASS) and the skel lookups feed wire
     * method dispatch. These were formerly installed by load-time
     * constructors in each module's generated rpc.gen.c; registering
     * them here keeps the side effect out of the linker and off every
     * process that never serves objects. Conditionally-linked modules
     * (ymgui, yrdawn) are registered from
     * yetty_yframework_register_figure_factories above, under the same
     * feature guards that gate their linkage. */
    {
        struct yetty_ycore_void_result reg_r = yetty_yfigure_register();
        YETTY_RETURN_IF_ERR(yetty_yterminal_terminal, reg_r, "terminal_create: yfigure_register");
        reg_r = yetty_yscene_register();
        YETTY_RETURN_IF_ERR(yetty_yterminal_terminal, reg_r, "terminal_create: yscene_register");
        reg_r = yetty_yvterm_register();
        YETTY_RETURN_IF_ERR(yetty_yterminal_terminal, reg_r, "terminal_create: yvterm_register");
        /* The session-root facade — its figure_root_container skel must be
         * dispatchable for a connecting tool to navigate to the container. */
        reg_r = yetty_yterminal_register();
        YETTY_RETURN_IF_ERR(yetty_yterminal_terminal, reg_r, "terminal_create: yterminal_register");
    }

    /* yclass RPC over DCS — subprocess clients (ygui, future widgets)
     * make yclass calls into the in-terminal yfigure tree by shipping
     * DCS envelopes on YETTY_DCS_YCLASS_RPC. The handler reads one
     * request envelope, dispatches via yetty_yclass_rpc_dispatch_one,
     * and ships the response back through the PTY master so the
     * client sees it on its stdin (where its own SM decodes the DCS
     * reply). One-line attach — the same wire_statemachine handles
     * yface OSC envelopes and yclass DCS envelopes in parallel.
     *
     * compressed=0 keeps tiny calls fast (b64-only); large frames
     * (apply_child_body buffers carrying a full figure body) benefit from
     * compressed=1 but the call-time pick lives at the client transport.
     * The server agrees on a fixed setting for simplicity; switch to a
     * per-envelope sniff if asymmetric compression turns out to matter. */
    {
        struct yetty_yclass_rpc_dcs_server_ptr_result dr =
            yetty_yclass_rpc_dcs_server_attach(terminal->sm, YETTY_DCS_YCLASS_RPC, /*compressed=*/0,
                                               terminal_dcs_emit_response, terminal);
        YETTY_RETURN_IF_ERR(yetty_yterminal_terminal, dr,
                            "terminal_create: dcs_server_attach for YCLASS_RPC");
        terminal->dcs_rpc_server = dr.value;
    }
    ydebug("terminal_create: yclass-rpc DCS handler registered (code=%d)", YETTY_DCS_YCLASS_RPC);

    /* Host-side connection layer — the acceptor of dynamic ywire channels
     * (DCS YETTY_DCS_YWIRE_CHANNEL) opened by in-pane clients. Rides the same
     * statemachine and the same PTY-master writer as the RPC server. */
    {
        struct yetty_ywire_connection_ptr_result hr = yetty_ywire_connection_attach(
            terminal->sm, terminal_dcs_emit_response, terminal, /*compressed=*/0);
        YETTY_RETURN_IF_ERR(yetty_yterminal_terminal, hr,
                            "terminal_create: connection_attach for YWIRE_CHANNEL");
        terminal->channel_host = hr.value;
    }
    /* Serve RPC on this connection: every dynamic channel a client opens is
     * accepted and served as its OWN independent RPC session (the SSH model)
     * — several clients on this one PTY no longer share/tear a single RPC
     * lane. One call; the accept plumbing lives inside yclass. (The legacy
     * DCS YCLASS_RPC server above stays until every client opens a dynamic
     * channel instead — issue #676.) */
    {
        struct yetty_ycore_void_result sr =
            yetty_yclass_rpc_serve_connection(terminal->channel_host);
        YETTY_RETURN_IF_ERR(yetty_yterminal_terminal, sr, "terminal_create: rpc_serve_connection");
    }
    ydebug("terminal_create: ywire channel host + RPC serve registered (code=%d)",
           YETTY_DCS_YWIRE_CHANNEL);

    /* Publish THIS TERMINAL as the session root: a connecting tool receives
     * the terminal object (yterminal:terminal) and navigates to the figure
     * container via yetty_yterminal_figure_root_container — so each
     * terminal's session reaches ITS OWN container instead of a process-
     * global that the last pane would overwrite. rpc_init first so handle
     * minting starts at 1 (0 is the invalid sentinel). */
    {
        struct yetty_ycore_void_result rpc_init_r = yetty_yclass_rpc_init();
        YETTY_RETURN_IF_ERR(yetty_yterminal_terminal, rpc_init_r, "terminal_create: rpc_init");

        struct yetty_yclass_handle_result root_r = yetty_yclass_rpc_set_root(terminal_object);
        YETTY_RETURN_IF_ERR(yetty_yterminal_terminal, root_r, "terminal_create: rpc_set_root");
        ydebug("terminal_create: yterminal:terminal root published handle=%llu",
               (unsigned long long)root_r.value);
    }

    /* The shader-glyph figure is created, rendered, and destroyed entirely by
     * the text-layer (it scans the cell buffer and renders as a second pass in
     * text_layer_render) — it is NOT a compositor child, so nothing to wire
     * here. The compositor's children remain the real figures (yplot, …). */

    return YETTY_OK(yetty_yterminal_terminal, terminal);
}

struct yetty_ycore_void_result yetty_yterminal_terminal_destroy(
    struct yetty_yterminal_terminal *terminal)
{
    if (!terminal) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yterminal_terminal_destroy: NULL terminal");
    }

    /* Best-effort cleanup: every step must run so resources are released.
     * Stash the first error and keep going; surface it at the end. */
    struct yetty_ycore_void_result first_err = YETTY_OK_VOID();
    bool have_err = false;
    ydebug("terminal_destroy: starting");

    /* Destroy root container (owned directly). It cascades to its child
     * figures — including the content grid, whose destroy slot tears down its
     * own text grid + ydraw canvas. The shader-glyph figure is owned by the
     * text layer and rides that teardown (it is not a separate container
     * child). Children hold borrowed refs to the compositor_font, so the font
     * is only safe to free after the container cascade completes; the registry
     * holds borrowed refs to the same font (factory user-data) — destroy
     * registry next. */
    terminal->grid = NULL; /* container owns it; drop the borrowed handle */
    if (terminal->root_container_obj) {
        struct yetty_yfigure_figure_ptr_result rf_res =
            yetty_yfigure_container_as_figure(terminal->root_container_obj);
        if (YETTY_IS_ERR(rf_res)) {
            yerror("terminal_destroy: root_container as_figure failed: %s", rf_res.error.msg);
            if (!have_err) {
                first_err = YETTY_ERR(yetty_ycore_void, "terminal_destroy: root as_figure", rf_res);
                have_err = true;
            } else {
                yetty_ycore_error_destroy(rf_res.error);
            }
        } else {
            struct yetty_ycore_void_result r =
                yetty_yfigure_destroy((struct yetty_yclass_object *)rf_res.value - 1);
            if (YETTY_IS_ERR(r)) {
                yerror("terminal_destroy: root_container destroy failed: %s", r.error.msg);
                if (!have_err) {
                    first_err = r;
                    have_err = true;
                } else {
                    yetty_ycore_error_destroy(r.error);
                }
            }
        }
        terminal->root_container_obj = NULL;
    }
    if (terminal->figure_registry) {
        struct yetty_ycore_void_result r =
            yetty_yfigure_registry_destroy(terminal->figure_registry);
        if (YETTY_IS_ERR(r)) {
            yetty_ycore_error_destroy(r.error);
        }
        terminal->figure_registry = NULL;
    }
    /* The complex factory outlives the registry — every scene the
     * registry minted borrowed our factory pointer, and they must be
     * gone (via root_container destroy above) before we tear it down. */
    if (terminal->complex_factory) {
        yetty_ydraw_complex_factory_destroy(terminal->complex_factory);
        terminal->complex_factory = NULL;
    }
    /* ydraw_registry is borrowed from the framework — not ours to destroy. */
    terminal->ydraw_registry = NULL;
    if (terminal->compositor_font) {
        terminal->compositor_font->ops->destroy(terminal->compositor_font);
        terminal->compositor_font = NULL;
    }

    /* Tear the barrier-expiry timer down BEFORE the connection and PTY it
     * releases into — its callback touches channel_host and the pane PTY, so it
     * must be stopped + deregistered + destroyed while both still exist.
     * Best-effort: run every step, stash the first error. */
    if (terminal->input_barrier_timer_ready) {
        struct yetty_yevent_event_loop *loop = terminal->context.yetty_context.event_loop;
        if (loop) {
            struct yetty_ycore_void_result stop_res =
                loop->ops->stop_timer(loop, terminal->input_barrier_timer);
            if (YETTY_IS_ERR(stop_res)) {
                if (!have_err) {
                    first_err = stop_res;
                    have_err = true;
                } else {
                    yetty_ycore_error_destroy(stop_res.error);
                }
            }
            struct yetty_ycore_void_result dereg_res = loop->ops->deregister_timer_listener(
                loop, terminal->input_barrier_timer, &terminal->input_barrier_listener);
            if (YETTY_IS_ERR(dereg_res)) {
                if (!have_err) {
                    first_err = dereg_res;
                    have_err = true;
                } else {
                    yetty_ycore_error_destroy(dereg_res.error);
                }
            }
            struct yetty_ycore_void_result destroy_res =
                loop->ops->destroy_timer(loop, terminal->input_barrier_timer);
            if (YETTY_IS_ERR(destroy_res)) {
                if (!have_err) {
                    first_err = destroy_res;
                    have_err = true;
                } else {
                    yetty_ycore_error_destroy(destroy_res.error);
                }
            }
        }
        terminal->input_barrier_timer_ready = 0;
    }

    /* Destroy wire state machine BEFORE the PTY — the SM holds a
     * non-owning pointer to the PTY and must not outlive it. */
    ydebug("terminal_destroy: destroying wire state machine");
    {
        struct yetty_ycore_void_result r = yetty_ywire_wire_statemachine_destroy(terminal->sm);
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

    /* The DCS RPC server state was borrowed by the SM's handler — only
     * safe to free now that the SM is destroyed. */
    yetty_yclass_rpc_dcs_server_destroy(terminal->dcs_rpc_server);
    terminal->dcs_rpc_server = NULL;

    /* Same borrow contract for the channel host (attach-mode connection —
     * it does not own the SM, so destroying it here only frees channels). */
    {
        struct yetty_ycore_void_result r = yetty_ywire_connection_destroy(terminal->channel_host);
        if (YETTY_IS_ERR(r)) {
            yerror("terminal_destroy: channel host destroy failed: %s", r.error.msg);
            if (!have_err) {
                first_err = r;
                have_err = true;
            } else {
                yetty_ycore_error_destroy(r.error);
            }
        }
        terminal->channel_host = NULL;
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

    /* The terminal struct is the data slice of the yterminal:terminal yclass
     * object — free the OBJECT (its allocation base), not the data pointer. */
    ydebug("terminal_destroy: freeing terminal object");
    struct yetty_yclass_object_ptr_result terminal_object_res =
        yetty_yterminal_terminal_to(terminal);
    if (YETTY_IS_OK(terminal_object_res)) {
        struct yetty_ycore_void_result object_free_res =
            yetty_yclass_object_free(terminal_object_res.value);
        if (YETTY_IS_ERR(object_free_res)) {
            if (!have_err) {
                first_err = object_free_res;
                have_err = true;
            } else {
                yetty_ycore_error_destroy(object_free_res.error);
            }
        }
    } else if (!have_err) {
        first_err = YETTY_ERR(yetty_ycore_void, "terminal_destroy: to_obj", terminal_object_res);
        have_err = true;
    } else {
        yetty_ycore_error_destroy(terminal_object_res.error);
    }
    ydebug("terminal_destroy: done");

    if (have_err) {
        return YETTY_ERR(yetty_ycore_void,
                         "yetty_yterminal_terminal_destroy: one or more cleanup steps failed",
                         first_err);
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yterminal_terminal_resize_grid(
    struct yetty_yterminal_terminal *terminal, struct yetty_ycore_grid_size grid_size,
    struct yetty_ycore_pixel_size cell_size)
{
    if (!terminal) {
        return YETTY_ERR(yetty_ycore_void,
                         "yetty_yterminal_terminal_resize_grid: terminal is NULL");
    }

    terminal->cols = grid_size.cols;
    terminal->rows = grid_size.rows;

    if (terminal->grid) {
        struct yetty_ycore_void_result r =
            yetty_yvterm_vterm_resize(terminal->grid, grid_size, cell_size);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r,
                            "yetty_yterminal_terminal_resize_grid: grid resize failed");
    }
    /* Push the new grid+pixel dims down to the PTY so a SIGWINCH fires
     * in the child and ws_xpixel/ws_ypixel from TIOCGWINSZ stays
     * current. Without this the layout-driven first resize (e.g. 80x24
     * default → 212x60 after pane layout) never reaches the inferior
     * and clients that need the actual pane pixel area get a stale
     * answer for the lifetime of the process. */
    if (terminal->context.pty && terminal->context.pty->ops && terminal->context.pty->ops->resize) {
        struct yetty_ycore_void_result pr = terminal->context.pty->ops->resize(
            terminal->context.pty, grid_size.cols, grid_size.rows,
            grid_size.cols * (uint32_t)cell_size.width,
            grid_size.rows * (uint32_t)cell_size.height);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, pr,
                            "yetty_yterminal_terminal_resize_grid: pty resize failed");
    }
    /* Root container has no grid-resize concept: each figure tracks
     * its own rect via wire SET_CHILD_RECT records. On terminal resize
     * the producer re-emits the layout in the new dims; the root
     * container's own rect gets refreshed at viewport-offset push time.
     *
     * The rect spans the WHOLE pane, not the grid's pixel size — with a
     * content reservation active the grid covers only part of the pane
     * (vterm's render slot places it on the resolved content rect), and
     * client overlay figures live in the remainder. Before the first
     * layout applied_w/h is 0; the grid px size is the pane then. */
    if (terminal->root_container_obj) {
        struct yetty_ycore_rectangle new_rect = {
            .min = {.x = 0.0f, .y = 0.0f},
            .max = {.x = terminal->applied_w > 0.0f ? terminal->applied_w
                                                    : (float)grid_size.cols * cell_size.width,
                    .y = terminal->applied_h > 0.0f ? terminal->applied_h
                                                    : (float)grid_size.rows * cell_size.height},
        };
        struct yetty_yfigure_figure_ptr_result rf_res =
            yetty_yfigure_container_as_figure(terminal->root_container_obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rf_res, "resize_grid: root as_figure");
        struct yetty_yfigure_figure *rf = rf_res.value;
        {
            struct yetty_ycore_void_result drop_r =
                yetty_yfigure_figure_rect_set((struct yetty_yclass_object *)(rf)-1, new_rect);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, drop_r, "drop: yetty_yfigure_figure_rect_set");
        }
        {
            struct yetty_ycore_void_result drop_r =
                yetty_yfigure_figure_dirty_set((struct yetty_yclass_object *)(rf)-1, 1);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, drop_r, "drop: yetty_yfigure_figure_dirty_set");
        }

        /* The content grid figure spans the whole pane — keep its rect in
         * lockstep with the container so its render/clip bounds track resizes. */
        if (terminal->grid) {
            struct yetty_ycore_void_result grid_rect_res =
                yetty_yfigure_figure_rect_set(terminal->grid, new_rect);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, grid_rect_res,
                                "yetty_yterminal_terminal_resize_grid: grid figure rect_set");
        }
    }
    /* The shader-glyph figure tracks the resize through the content grid's
     * resize above (it owns the text layer) — no separate push needed. */
    return YETTY_OK_VOID();
}

/* Resolve the client's content spec (edge-anchored; see yetty_content_rect)
 * against a live pane size into the effective content rect, clamped into the
 * pane and never below `min_w` x `min_h` (one cell). The all-zero spec
 * resolves to the full pane. */
static void terminal_resolve_content_rect(const struct yetty_yterminal_terminal *terminal,
                                          float pane_w, float pane_h, float min_w, float min_h,
                                          float *out_x, float *out_y, float *out_w, float *out_h)
{
    float origin_x = terminal->content_spec_x;
    float origin_y = terminal->content_spec_y;
    if (origin_x > pane_w - min_w) {
        origin_x = pane_w - min_w > 0.0f ? pane_w - min_w : 0.0f;
    }
    if (origin_y > pane_h - min_h) {
        origin_y = pane_h - min_h > 0.0f ? pane_h - min_h : 0.0f;
    }
    float width = terminal->content_spec_w > 0.0f ? terminal->content_spec_w
                                                  : pane_w - origin_x + terminal->content_spec_w;
    float height = terminal->content_spec_h > 0.0f ? terminal->content_spec_h
                                                   : pane_h - origin_y + terminal->content_spec_h;
    if (width > pane_w - origin_x) {
        width = pane_w - origin_x;
    }
    if (height > pane_h - origin_y) {
        height = pane_h - origin_y;
    }
    if (width < min_w) {
        width = min_w;
    }
    if (height < min_h) {
        height = min_h;
    }
    *out_x = origin_x;
    *out_y = origin_y;
    *out_w = width;
    *out_h = height;
}

/* Derive the text grid from a pane pixel size, honoring the content rect a
 * client reserved (YETTY_OSC_CS_CONTENT_RECT / _INSET), and drive the resize.
 * The grid is sized to exactly fill the content rect — cols*cell_w ==
 * content_w and rows*cell_h == content_h — so the grid figure's content-rect
 * placement in vterm.c lines up with the libvterm surface to the pixel.
 * Shared by the RESIZE event and the content OSC handlers so all paths
 * reflow identically. */
static struct yetty_ycore_void_result terminal_apply_pane_geometry(
    struct yetty_yterminal_terminal *terminal, float pane_w, float pane_h)
{
    if (!terminal->grid || pane_w <= 0.0f || pane_h <= 0.0f) {
        return YETTY_OK_VOID();
    }
    struct pixel_size_result cell_res = yetty_yvterm_vterm_cell_size(terminal->grid);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, cell_res, "terminal_apply_pane_geometry: cell size");
    struct yetty_ycore_pixel_size cell = cell_res.value;

    float cell_w_target = cell.width > 0 ? cell.width : 10.0f;
    float cell_h_target = cell.height > 0 ? cell.height : 20.0f;

    /* Track runtime content-scale changes (a yvnc viewer's display density
     * arriving via resize): rescale the cell stride by the ratio between
     * the live scale and the scale the current metrics were derived at.
     * Same mechanism as the structural zoom — grid, PTY and glyph
     * rendering all follow the cell size, and MSDF glyphs stay crisp. */
    if (terminal->context.yetty_context.runtime) {
        float live_scale =
            terminal->context.yetty_context.runtime->gpu.app_gpu_context.content_scale;
        if (live_scale > 0.0f && terminal->layout_content_scale > 0.0f &&
            live_scale != terminal->layout_content_scale) {
            float density_ratio = live_scale / terminal->layout_content_scale;
            cell_w_target *= density_ratio;
            cell_h_target *= density_ratio;
            /* Keep the structural-zoom reference in the new density too. */
            if (terminal->cell_zoom > 0.0f) {
                terminal->zoom_base_cell.width *= density_ratio;
                terminal->zoom_base_cell.height *= density_ratio;
            }
            terminal->layout_content_scale = live_scale;
            /* The retained side transitions ATOMICALLY with the layout
             * scale: renderer density + grid rich density move together
             * and the structural-zoom baseline rebases, so retained
             * logical content neither mis-retires nor misclassifies the
             * density-driven cell growth as cell zoom. */
            if (terminal->grid) {
                struct yetty_ycore_void_result scale_res =
                    yetty_yvterm_vterm_set_content_scale(terminal->grid, live_scale);
                if (YETTY_IS_ERR(scale_res)) {
                    ywarn("terminal: density transition: %s", scale_res.error.msg);
                    yetty_ycore_error_destroy(scale_res.error);
                }
            }
        }
    }

    /* Snap the cell stride to whole pixels. The font already reports a
     * snapped cell, but the density ratio above (or a legacy fractional
     * metric) can reintroduce fractions, and a fractional stride places
     * every column/row at a different subpixel phase — the same glyph then
     * samples its distance field at a different offset in each cell and
     * stems come out alternately crisp and blurry. */
    float cell_w_snapped = fmaxf(1.0f, roundf(cell_w_target));
    float cell_h_snapped = fmaxf(1.0f, roundf(cell_h_target));

    /* Resolve the reservation; never let the content rect collapse below a
     * single cell in either axis (a client could reserve more than the pane). */
    float content_x = 0.0f, content_y = 0.0f, content_w = 0.0f, content_h = 0.0f;
    terminal_resolve_content_rect(terminal, pane_w, pane_h, cell_w_snapped, cell_h_snapped,
                                  &content_x, &content_y, &content_w, &content_h);

    /* Whole cells only — the text shader maps grid pixels 1:1 across the
     * content rect, so the rect must equal cols*cell exactly; stretching
     * the cell to cover the remainder (the old behaviour) made the stride
     * fractional again. The sub-cell remainder strip at the right/bottom
     * edge stays pane background. */
    uint32_t new_cols = (uint32_t)(content_w / cell_w_snapped);
    uint32_t new_rows = (uint32_t)(content_h / cell_h_snapped);
    if (new_cols == 0) {
        new_cols = 1;
    }
    if (new_rows == 0) {
        new_rows = 1;
    }
    struct yetty_ycore_pixel_size new_cell = {
        .width = cell_w_snapped,
        .height = cell_h_snapped,
    };
    struct yetty_ycore_void_result rgr = yetty_yterminal_terminal_resize_grid(
        terminal, (struct yetty_ycore_grid_size){.cols = new_cols, .rows = new_rows}, new_cell);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rgr, "terminal_apply_pane_geometry: resize_grid");

    /* Hand the renderer the resolved rect: the grid figure spans the whole
     * pane; vterm's render slot places the text surface on this rect. The
     * rect is clipped to the exact grid extent so grid pixels stay 1:1 with
     * framebuffer pixels. */
    struct yetty_ycore_void_result content_rect_res = yetty_yvterm_vterm_set_content_rect(
        terminal->grid, content_x, content_y, (float)new_cols * cell_w_snapped,
        (float)new_rows * cell_h_snapped);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, content_rect_res,
                        "terminal_apply_pane_geometry: set content rect");

    /* Push the FULL pane pixel size to a subscribed figure client so it can
     * place its overlay in the reserved band — it needs the whole pane, not
     * the inset content rect. Telnet/guest clients have no TIOCGWINSZ pixels,
     * so this OSC is their only resize signal. Best-effort (drop the error). */
    if (terminal->mouse_move_subscribed || terminal->mouse_click_subscribed) {
        /* DEC figure path: figure-tagged envelope. */
        struct yetty_ycore_void_result er =
            terminal_emit_card_resize(terminal, terminal->focused_figure_id, pane_w, pane_h);
        if (YETTY_IS_ERR(er)) {
            yetty_ycore_error_destroy(er.error);
        }
    }
    if (terminal->resize_subscribed || terminal->pane_mouse_click_subscribed ||
        terminal->pane_mouse_move_subscribed || terminal->pane_mouse_wheel_subscribed) {
        /* Pane-wide subscription: pane-wide envelope. */
        struct yetty_ycore_void_result er = terminal_emit_pane_resize(terminal, pane_w, pane_h);
        if (YETTY_IS_ERR(er)) {
            yetty_ycore_error_destroy(er.error);
        }
    }
    return YETTY_OK_VOID();
}

/* Terminal state */

uint32_t yetty_yterminal_terminal_get_cols(const struct yetty_yterminal_terminal *terminal)
{
    return terminal ? terminal->cols : 0;
}

uint32_t yetty_yterminal_terminal_get_rows(const struct yetty_yterminal_terminal *terminal)
{
    return terminal ? terminal->rows : 0;
}

/*=============================================================================
 * View interface implementation
 *===========================================================================*/

struct yetty_yui_view *yetty_yterminal_terminal_as_view(struct yetty_yterminal_terminal *terminal)
{
    return terminal ? &terminal->view : NULL;
}

struct yetty_yterminal_terminal *yetty_yterminal_terminal_from_view(struct yetty_yui_view *view)
{
    /* terminal_view_ops is a file-local static const; identity-compare
     * the view's ops pointer against it to decide whether this view is
     * one of ours. Other view kinds (VNC, ydvnc) use different ops
     * tables and the compare fails — caller treats as "no terminal". */
    if (!view || view->ops != &terminal_view_ops) {
        return NULL;
    }
    return container_of(view, struct yetty_yterminal_terminal, view);
}

struct yetty_ywire_wire_statemachine *yetty_yterminal_terminal_wire_sm(
    struct yetty_yterminal_terminal *terminal)
{
    return terminal ? terminal->sm : NULL;
}

struct yetty_ywire_connection *yetty_yterminal_terminal_channel_host(
    struct yetty_yterminal_terminal *terminal)
{
    return terminal ? terminal->channel_host : NULL;
}

static struct yetty_ycore_void_result terminal_view_destroy(struct yetty_yui_view *view)
{
    struct yetty_yterminal_terminal *terminal =
        container_of(view, struct yetty_yterminal_terminal, view);
    return yetty_yterminal_terminal_destroy(terminal);
}

static struct yetty_ycore_void_result terminal_view_render(struct yetty_yui_view *view,
                                                           struct yetty_ydraw_target *render_target,
                                                           int force_redraw)
{
    struct yetty_yterminal_terminal *terminal =
        container_of(view, struct yetty_yterminal_terminal, view);

    return terminal_render_frame(terminal, render_target, force_redraw);
}

static struct yetty_ycore_void_result terminal_view_set_bounds(struct yetty_yui_view *view,
                                                               struct yetty_yui_rect bounds)
{
    struct yetty_yterminal_terminal *terminal =
        container_of(view, struct yetty_yterminal_terminal, view);

    /* Detect a real pixel-size change against the size we last resized to.
     * view->bounds is already overwritten with the new bounds by the
     * generic wrapper before we run, so compare against applied_w/h. */
    int changed = (terminal->applied_w != bounds.w) || (terminal->applied_h != bounds.h);
    view->bounds = bounds;

    ydebug("terminal_view_set_bounds: %.0fx%.0f at (%.0f,%.0f)", bounds.w, bounds.h, bounds.x,
           bounds.y);

    /* Compositor draws figures at absolute target coords, but the wire
     * producers (ygreeter, ygui) emit pane-local rects. Push the pane
     * origin into the compositor so it can shift incoming rects, keeping
     * the rendered pixels aligned with the mouse coords the input
     * pipeline subtracts (bounds.x/y) on the way down to the producer.
     *
     * bounds is in FRAMEBUFFER px (yui composes its chrome in fb). Divide by
     * layout_content_scale so the offset is in LOGICAL — client-authored ygui
     * figures ship their CREATE_CHILD rect in LOGICAL, and the container
     * stores rect_local + viewport_offset on each child. Storing them in a
     * single unit system means the ygrid scissor (rect * content_scale) reaches
     * the right fb region on HiDPI. On non-HiDPI (scale == 1.0) fb == logical
     * and this divide is a no-op, matching pre-HiDPI behaviour. */
    float offset_scale =
        terminal->layout_content_scale > 0.0f ? terminal->layout_content_scale : 1.0f;
    if (terminal->root_container_obj) {
        yetty_yfigure_container_set_viewport_offset(
            terminal->root_container_obj, bounds.x / offset_scale, bounds.y / offset_scale);
    }

    /* Actually resize the terminal when the pixel size changes. The
     * split-drag path reaches a pane through set_bounds (only the full-
     * window resize goes through a RESIZE event), so this used to just
     * store the bounds and resize nothing — a pane shrunk/grown by a
     * splitter kept its old grid + cell metrics, rendering glyphs
     * squashed until a full-window repaint snapped them back, and a
     * client program (ygreeter, nvim) never saw the new size. Drive the
     * exact same resize the event path performs. */
    if (changed && bounds.w > 0.0f && bounds.h > 0.0f) {
        terminal->applied_w = bounds.w;
        terminal->applied_h = bounds.h;
        struct yetty_yui_event re = {.type = YETTY_YCORE_RESIZE};
        re.resize.width = bounds.w;
        re.resize.height = bounds.h;
        struct yetty_ycore_int_result rr = terminal_view_on_event(view, &re);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "terminal_view_set_bounds: resize");
    }
    return YETTY_OK_VOID();
}

static const char *terminal_view_title(const struct yetty_yui_view *view)
{
    /* view is the first member of the terminal struct — direct cast. */
    const struct yetty_yterminal_terminal *terminal = (const struct yetty_yterminal_terminal *)view;
    return terminal->title[0] ? terminal->title : NULL;
}

static struct yetty_ycore_int_result terminal_view_on_event(struct yetty_yui_view *view,
                                                            const struct yetty_yui_event *event)
{
    struct yetty_yterminal_terminal *terminal =
        container_of(view, struct yetty_yterminal_terminal, view);

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
         * takes positive lines = older content (up).
         *
         * Suppressed when a hosted app subscribed for keyboard (DEC ?1502):
         * that app owns these keys and scrolls its own content with them,
         * exactly like a full-screen program owns PageUp/PageDown. */
        if (!terminal->key_subscribed && (event->key.key == 266 /* GLFW_KEY_PAGE_UP */ ||
                                          event->key.key == 267 /* GLFW_KEY_PAGE_DOWN */)) {
            int page = (int)terminal->rows;
            if (page < 1) {
                page = 1;
            }
            struct yetty_ycore_void_result sr =
                terminal_scrollback_apply(terminal, event->key.key == 266 ? +page : -page);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, sr,
                                "terminal_view_on_event: scrollback_apply failed");
            return YETTY_OK(yetty_ycore_int, 1);
        }
        /* Up / Down nudge the scrollback view one line at a time, but ONLY once
         * already in scrollback (entered via wheel-up or PageUp). At the live
         * prompt the arrows belong to the shell (command history) / focused
         * app, so they are left to fall through there. Matches less/tmux copy
         * mode. Handled before the any-key-exits rule so they keep scrolling
         * instead of dropping back to live. */
        if (!terminal->key_subscribed && terminal_scrollback_is_active(terminal) &&
            (event->key.key == 265 /* GLFW_KEY_UP */ ||
             event->key.key == 264 /* GLFW_KEY_DOWN */)) {
            struct yetty_ycore_void_result sr =
                terminal_scrollback_apply(terminal, event->key.key == 265 ? +1 : -1);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, sr,
                                "terminal_view_on_event: scrollback line nudge failed");
            return YETTY_OK(yetty_ycore_int, 1);
        }
        /* In scrollback view, Enter exits and is consumed (matches tmux copy
     * mode). Other keys also exit scrollback before falling through to
     * normal dispatch — this means typing while in scrollback returns to
     * live and delivers the keystroke to the shell, which is what users
     * expect when they meant to interact with the prompt. */
        /* A bare modifier press (Ctrl/Shift/Alt/Super, GLFW 340-347) is not a
         * real keystroke — it carries no shell input and is typically the
         * start of a chord like Ctrl+wheel (non-intrusive zoom). It must NOT
         * exit scrollback, or holding Ctrl to zoom would snap back to live. */
        bool is_bare_modifier = (event->key.key >= 340 && event->key.key <= 347);
        if (terminal_scrollback_is_active(terminal) && !is_bare_modifier) {
            int is_enter = (event->key.key == 257); /* GLFW_KEY_ENTER */
            struct yetty_ycore_void_result xr = terminal_scrollback_exit(terminal);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, xr,
                                "terminal_view_on_event: scrollback_exit failed");
            if (is_enter) {
                return YETTY_OK(yetty_ycore_int, 1);
            }
        }
        /* Structured keyboard fan-out to a click-focused ymgui figure (opt-in
         * via TERM_INPUT_SUB): route the keystroke to it as an OSC envelope and
         * DO NOT also feed libvterm. Skipped when the app subscribed for
         * keyboard via DEC ?1502 — such an app consumes keystrokes from its
         * own PTY (libvterm on_key below), the same channel a full-screen
         * program reads, so forwarding a structured copy here would double it. */
        if (!terminal->key_subscribed && terminal->figure_key_optin) {
            struct yetty_ycore_int_result ckr = terminal_emit_figure_key(
                terminal, YETTY_YMGUI_INPUT_KEY_DOWN, event->key.key, event->key.mods, 0);
            if (YETTY_IS_ERR(ckr)) {
                return YETTY_ERR(yetty_ycore_int,
                                 "terminal_view_on_event: emit_card_key(KEY_DOWN) failed", ckr);
            }
            if (ckr.value) {
                return YETTY_OK(yetty_ycore_int, 1);
            }
        }
        struct yetty_ycore_int_result on_key_res =
            yetty_yvterm_vterm_on_key(terminal->grid, event->key.key, event->key.mods);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, on_key_res, "terminal_view_on_event: on_key");
        if (on_key_res.value) {
            return YETTY_OK(yetty_ycore_int, 1);
        }
        return YETTY_OK(yetty_ycore_int, 1);

    case YETTY_YCORE_KEY_UP: {
        ydebug("terminal: KEY_UP key=%d mods=%d", event->key.key, event->key.mods);
        /* Same opt-in gate as KEY_DOWN/CHAR: without it, apps that never asked
         * for structured keys would still receive stray KEY_UP envelopes (and
         * ?1502 subscribers would see inconsistent phases). */
        if (!terminal->key_subscribed && terminal->figure_key_optin) {
            struct yetty_ycore_int_result ckr = terminal_emit_figure_key(
                terminal, YETTY_YMGUI_INPUT_KEY_UP, event->key.key, event->key.mods, 0);
            if (YETTY_IS_ERR(ckr)) {
                return YETTY_ERR(yetty_ycore_int,
                                 "terminal_view_on_event: emit_card_key(KEY_UP) failed", ckr);
            }
            if (ckr.value) {
                return YETTY_OK(yetty_ycore_int, 1);
            }
        }
        return YETTY_OK(yetty_ycore_int, 0);
    }

    case YETTY_YCORE_CHAR:
        ydebug("terminal: CHAR codepoint=U+%04X mods=%d", event->chr.codepoint, event->chr.mods);
        if (terminal_scrollback_is_active(terminal)) {
            struct yetty_ycore_void_result xr = terminal_scrollback_exit(terminal);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, xr,
                                "terminal_view_on_event: scrollback_exit failed");
        }
        /* See KEY_DOWN: a click-focused figure consumes the codepoint — but only
         * when the pane app did NOT subscribe for keyboard (DEC ?1502). A
         * key-subscribed app reads every keystroke from its own PTY (on_char
         * below), so routing a structured copy to the focused figure here would
         * steal printable keys from the app the moment a figure gained focus. */
        if (!terminal->key_subscribed && terminal->figure_key_optin) {
            struct yetty_ycore_int_result ckr = terminal_emit_figure_key(
                terminal, YETTY_YMGUI_INPUT_KEY_CHAR, -1, event->chr.mods, event->chr.codepoint);
            if (YETTY_IS_ERR(ckr)) {
                return YETTY_ERR(yetty_ycore_int,
                                 "terminal_view_on_event: emit_card_key(CHAR) failed", ckr);
            }
            if (ckr.value) {
                return YETTY_OK(yetty_ycore_int, 1);
            }
        }
        struct yetty_ycore_int_result on_char_res =
            yetty_yvterm_vterm_on_char(terminal->grid, event->chr.codepoint, event->chr.mods);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, on_char_res, "terminal_view_on_event: on_char");
        if (on_char_res.value) {
            return YETTY_OK(yetty_ycore_int, 1);
        }
        return YETTY_OK(yetty_ycore_int, 1);

    case YETTY_YCORE_RESIZE: {
        float width = event->resize.width;
        float height = event->resize.height;
        ydebug("terminal: RESIZE %.0fx%.0f", width, height);

        if (width <= 0 || height <= 0) {
            return YETTY_OK(yetty_ycore_int, 1);
        }

        /* Grid + cell stride: rows are derived from the desired target
         * stride (the font's natural cell height, or a sensible fallback),
         * then cell_w/cell_h are re-derived as content-rect / rows so the
         * canvas's `cols * cell_w x rows * cell_h` equals the content rect
         * (pane minus reserved insets) exactly. Without that, the shader's
         * NDC-to-pixel map shifts / scales by a few px against the
         * framebuffer and primitives pinned to the bottom edge end up cut
         * or stretched. The shared helper applies the content insets and
         * pushes the new grid into both the layers and the PTY. */
        struct yetty_ycore_void_result gr = terminal_apply_pane_geometry(terminal, width, height);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, gr,
                            "terminal_view_on_event: apply_pane_geometry failed");
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

        float view_w = terminal->view.bounds.w;
        float view_h = terminal->view.bounds.h;
        if (view_w <= 0.0f || view_h <= 0.0f) {
            ydebug("terminal: ZOOM_CELL_SIZE skipped, zero view bounds");
            return YETTY_OK(yetty_ycore_int, 1);
        }

        if (terminal->grid) {
            /* Seed the zoom base from the live cell on first use, so the
             * accumulated factor always applies to the same reference. */
            if (terminal->cell_zoom <= 0.0f) {
                struct pixel_size_result cell_res = yetty_yvterm_vterm_cell_size(terminal->grid);
                YETTY_RETURN_IF_ERR(yetty_ycore_int, cell_res,
                                    "terminal_view_on_event: zoom cell size");
                terminal->zoom_base_cell = cell_res.value;
                if (terminal->zoom_base_cell.width <= 0.0f) {
                    terminal->zoom_base_cell.width = 10.0f;
                }
                if (terminal->zoom_base_cell.height <= 0.0f) {
                    terminal->zoom_base_cell.height = 20.0f;
                }
                terminal->cell_zoom = 1.0f;
            }
            if (event->zoom_cell_size.reset) {
                terminal->cell_zoom = 1.0f;
            } else {
                float factor = 1.0f + delta;
                if (factor < 0.5f) {
                    factor = 0.5f;
                }
                if (factor > 3.0f) {
                    factor = 3.0f;
                }
                terminal->cell_zoom *= factor;
                if (terminal->cell_zoom < 0.25f) {
                    terminal->cell_zoom = 0.25f;
                }
                if (terminal->cell_zoom > 8.0f) {
                    terminal->cell_zoom = 8.0f;
                }
            }
            ydebug("terminal: ZOOM_CELL_SIZE delta=%.3f zoom=%.3f", delta, terminal->cell_zoom);
            /* Whole-pixel cell stride, whole cells, rect == grid extent —
             * same rules as terminal_apply_pane_geometry (a fractional
             * stride breaks the shared subpixel phase of the glyph grid).
             * Derived from the fixed base so per-tick rounding never feeds
             * back into the next tick. */
            float cell_w_target =
                fmaxf(1.0f, roundf(terminal->zoom_base_cell.width * terminal->cell_zoom));
            float cell_h_target =
                fmaxf(1.0f, roundf(terminal->zoom_base_cell.height * terminal->cell_zoom));
            float content_x = 0.0f, content_y = 0.0f, content_w = 0.0f, content_h = 0.0f;
            terminal_resolve_content_rect(terminal, view_w, view_h, cell_w_target, cell_h_target,
                                          &content_x, &content_y, &content_w, &content_h);
            uint32_t new_cols = (uint32_t)(content_w / cell_w_target);
            uint32_t new_rows = (uint32_t)(content_h / cell_h_target);
            if (new_cols == 0) {
                new_cols = 1;
            }
            if (new_rows == 0) {
                new_rows = 1;
            }
            struct yetty_ycore_pixel_size new_cell = {
                .width = cell_w_target,
                .height = cell_h_target,
            };
            struct yetty_ycore_void_result rgr = yetty_yterminal_terminal_resize_grid(
                terminal, (struct yetty_ycore_grid_size){.cols = new_cols, .rows = new_rows},
                new_cell);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, rgr,
                                "terminal_view_on_event: terminal_resize_grid (zoom) failed");
            struct yetty_ycore_void_result content_rect_res = yetty_yvterm_vterm_set_content_rect(
                terminal->grid, content_x, content_y, (float)new_cols * cell_w_target,
                (float)new_rows * cell_h_target);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, content_rect_res,
                                "terminal_view_on_event: zoom content rect");
            /* NO second PTY resize here: terminal_resize_grid above owns
             * that operation (one TIOCSWINSZ/SIGWINCH, one NAWS, one peer
             * callback per transition), and a redundant call was also a
             * second failure point BETWEEN the committed rich scale and
             * the client envelope below — failing there stranded ygui2
             * on its old viewport and input divisor. */
            /* The committed rich projection JUST changed — publish it to
             * the pane-wide subscription set (same set the ordinary
             * geometry path serves) in the SAME transition: an attached
             * drawable-tree client must reflow to the new logical
             * viewport the moment its retained content starts rendering
             * at the new scale, or it magnifies/clips with a stale input
             * divisor instead of reflowing. */
            if (terminal->resize_subscribed || terminal->pane_mouse_click_subscribed ||
                terminal->pane_mouse_move_subscribed || terminal->pane_mouse_wheel_subscribed) {
                struct yetty_ycore_void_result er =
                    terminal_emit_pane_resize(terminal, view_w, view_h);
                if (YETTY_IS_ERR(er)) {
                    yetty_ycore_error_destroy(er.error);
                }
            }
        }

        if (terminal->context.yetty_context.event_loop) {
            terminal->context.yetty_context.event_loop->ops->request_render(
                terminal->context.yetty_context.event_loop);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }

    case YETTY_YCORE_ZOOM_VISUAL_APPLY: {
        float scale = event->zoom_visual_apply.scale;
        float ox = event->zoom_visual_apply.offset_x;
        float oy = event->zoom_visual_apply.offset_y;
        if (terminal->grid) {
            struct yetty_ycore_void_result vr =
                yetty_yvterm_vterm_set_visual_zoom(terminal->grid, scale, ox, oy);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, vr,
                                "terminal_view_on_event: grid set_visual_zoom failed");
        }
        /* The shader-glyph figure picks up the zoom through the content grid's
         * set_visual_zoom above (it owns the text layer) — no separate push. */
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
                struct yetty_ycore_size_result wr = terminal_pty_write_raw(terminal, text, len);
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
   * the ymgui-layer's card registry. Each emit carries a figure_id and
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

        /* Cell-precise selection. Left-button drag starts/extends/finalises
         * a selection when nothing else owns the click (no figure-aware
         * subscriber, no scrollback view). Anchor is the cell under
         * MOUSE_DOWN; head tracks MOUSE_DRAG. */
        float lx_sel = event->mouse.x - view->bounds.x;
        float ly_sel = event->mouse.y - view->bounds.y;
        int in_bounds = !(lx_sel < 0.0f || ly_sel < 0.0f || lx_sel >= view->bounds.w ||
                          ly_sel >= view->bounds.h);
        int term_owns_click = !terminal->mouse_click_subscribed &&
                              !terminal->pane_mouse_click_subscribed &&
                              !terminal_scrollback_is_active(terminal);

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
                struct yetty_ycore_void_result cell_res =
                    terminal_cell_from_local(terminal, lx_sel, ly_sel, &r, &c);
                YETTY_RETURN_IF_ERR(yetty_ycore_int, cell_res,
                                    "terminal_view_on_event: cell from local (down)");

                /* Multi-click: a press within 0.4s of the previous one on the
                 * same cell escalates single → word (double) → line (triple),
                 * then wraps back to single. Mirrors xterm. */
                double now = yetty_yplatform_ytime_monotonic_sec();
                if (now - terminal->sel_last_click_sec <= 0.4 &&
                    r == terminal->sel_last_click_row && c == terminal->sel_last_click_col) {
                    terminal->sel_click_count++;
                    if (terminal->sel_click_count > 3) {
                        terminal->sel_click_count = 1;
                    }
                } else {
                    terminal->sel_click_count = 1;
                }
                terminal->sel_last_click_sec = now;
                terminal->sel_last_click_row = r;
                terminal->sel_last_click_col = c;

                if (terminal->sel_click_count == 2) {
                    /* Word select: snap the anchor/head to the word boundaries
                     * around the clicked cell. Not a drag — finalised here. */
                    uint32_t sc = c, ec = c;
                    struct yetty_ycore_void_result wb_res =
                        yetty_yvterm_vterm_word_bounds(terminal->grid, r, c, &sc, &ec);
                    YETTY_RETURN_IF_ERR(yetty_ycore_int, wb_res,
                                        "terminal_view_on_event: word bounds");
                    terminal->sel_anchor_row = r;
                    terminal->sel_anchor_col = sc;
                    terminal->sel_head_row = r;
                    terminal->sel_head_col = ec;
                    terminal->sel_active = 1;
                    terminal->sel_dragging = 0;
                } else if (terminal->sel_click_count == 3) {
                    /* Line select: the whole visible row (trailing blanks stream
                     * as nothing in get_selection_text). */
                    terminal->sel_anchor_row = r;
                    terminal->sel_anchor_col = 0;
                    terminal->sel_head_row = r;
                    terminal->sel_head_col = terminal->cols ? terminal->cols - 1u : 0u;
                    terminal->sel_active = 1;
                    terminal->sel_dragging = 0;
                } else {
                    /* Single click: anchor a fresh drag selection. */
                    terminal->sel_anchor_row = r;
                    terminal->sel_anchor_col = c;
                    terminal->sel_head_row = r;
                    terminal->sel_head_col = c;
                    terminal->sel_active = 1;
                    terminal->sel_dragging = 1;
                }
                struct yetty_ycore_void_result psr = terminal_push_selection(terminal);
                YETTY_RETURN_IF_ERR(yetty_ycore_int, psr,
                                    "terminal_view_on_event: push_selection (anchor) failed");
                /* Word/line selections are complete on the press — auto-copy now
                 * (honouring the same config as drag auto-copy). */
                if (terminal->sel_click_count >= 2) {
                    struct yetty_yconfig_config *cfg =
                        terminal->context.yetty_context.runtime->config;
                    int auto_copy = 1;
                    if (cfg && cfg->ops && cfg->ops->get_bool) {
                        auto_copy = cfg->ops->get_bool(cfg, "terminal/selection/auto-copy", 1);
                    }
                    if (auto_copy) {
                        struct yetty_ycore_void_result cs = terminal_copy_selection(terminal);
                        YETTY_RETURN_IF_ERR(yetty_ycore_int, cs,
                                            "terminal_view_on_event: word/line auto-copy failed");
                    }
                }
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
                        terminal->context.yetty_context.runtime->config;
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

        if (!terminal->mouse_click_subscribed && !terminal->pane_mouse_click_subscribed) {
            return YETTY_OK(yetty_ycore_int, 0);
        }
        float lx = event->mouse.x - view->bounds.x;
        float ly = event->mouse.y - view->bounds.y;
        int click_in_bounds =
            !(lx < 0.0f || ly < 0.0f || lx >= view->bounds.w || ly >= view->bounds.h);
        int btn = event->mouse.button;
        int press = (event->type == YETTY_YCORE_MOUSE_DOWN) ? 1 : 0;
        if (press && !click_in_bounds) {
            return YETTY_OK(yetty_ycore_int, 0); /* capture never starts outside */
        }
        int capture_held = terminal->mouse_buttons_held != 0;
        if (press) {
            terminal->mouse_buttons_held |= (1 << btn);
        } else {
            terminal->mouse_buttons_held &= ~(1 << btn);
        }
        int release_ends_capture = !press && terminal->mouse_buttons_held == 0;

        /* CAPTURED events (any button already held) route to the OWNER
         * recorded at the starting press — in or out of bounds — so a
         * figure keeps its release beyond the pane edge and a pane
         * background drag cannot switch to a figure it crosses. */
        if (capture_held && terminal->mouse_capture_figure_id != 0 &&
            terminal->mouse_click_subscribed) {
            struct yetty_yfigure_hit_result hit_res = terminal_resolve_figure_hit(
                terminal, event->mouse.x, event->mouse.y, terminal->mouse_capture_figure_id);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, hit_res,
                                "terminal_view_on_event: resolve hit (captured button)");
            struct yetty_ycore_void_result mr = terminal_emit_card_mouse_button(
                terminal, terminal->mouse_capture_figure_id, hit_res.value.local_x,
                hit_res.value.local_y, btn, press, 0.0f, event->mouse.mods);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, mr,
                                "terminal_view_on_event: emit captured figure button failed");
            if (release_ends_capture) {
                terminal->mouse_capture_figure_id = 0;
                terminal->mouse_capture_pane = 0;
            }
            return YETTY_OK(yetty_ycore_int, 1);
        }
        if (capture_held && terminal->mouse_capture_pane && terminal->pane_mouse_click_subscribed) {
            struct yetty_ycore_void_result mr = terminal_emit_card_mouse_button(
                terminal, 0u, lx, ly, btn, press, 0.0f, event->mouse.mods);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, mr,
                                "terminal_view_on_event: emit captured pane button failed");
            if (release_ends_capture) {
                terminal->mouse_capture_figure_id = 0;
                terminal->mouse_capture_pane = 0;
            }
            return YETTY_OK(yetty_ycore_int, 1);
        }
        if (release_ends_capture) {
            terminal->mouse_capture_figure_id = 0;
            terminal->mouse_capture_pane = 0;
        }
        if (!click_in_bounds) {
            return YETTY_OK(yetty_ycore_int, 0); /* unowned out-of-bounds event */
        }
        uint32_t owning_figure_id = 0;

        /* Figure-aware path. Focus tracking is click-only: a press
         * updates focused_figure_id (and emits an SC_FOCUS transition
         * if it changed); a release routes to whatever was focused.
         *
         * Hit-testing runs in WINDOW coords (not pane-local lx/ly).
         * The compositor stores figure rects with viewport_offset
         * already added — i.e. in window coords. Passing pane-local
         * cursor here would subtract bounds.y twice (once in lx/ly
         * derivation, once in hit_visit's local = cursor - rect.min)
         * and the engine would receive a click offset by exactly the
         * chrome height. */
        if (terminal->mouse_click_subscribed) {
            uint32_t focused = terminal->focused_figure_id;
            struct yetty_yfigure_hit hit;
            if (press) {
                struct yetty_yfigure_hit_result hit_res =
                    terminal_resolve_figure_hit(terminal, event->mouse.x, event->mouse.y, 0);
                YETTY_RETURN_IF_ERR(yetty_ycore_int, hit_res,
                                    "terminal_view_on_event: resolve hit (press)");
                hit = hit_res.value;
                if (hit.figure_id != focused) {
                    if (focused != 0) {
                        struct yetty_ycore_void_result lr =
                            terminal_emit_card_focus(terminal, focused, 0);
                        YETTY_RETURN_IF_ERR(yetty_ycore_int, lr,
                                            "terminal_view_on_event: emit focus lost");
                    }
                    terminal->focused_figure_id = hit.figure_id;
                    if (hit.figure_id != 0) {
                        struct yetty_ycore_void_result gr =
                            terminal_emit_card_focus(terminal, hit.figure_id, 1);
                        YETTY_RETURN_IF_ERR(yetty_ycore_int, gr,
                                            "terminal_view_on_event: emit focus gained");
                    }
                }
            } else {
                struct yetty_yfigure_hit_result hit_res =
                    terminal_resolve_figure_hit(terminal, event->mouse.x, event->mouse.y, focused);
                YETTY_RETURN_IF_ERR(yetty_ycore_int, hit_res,
                                    "terminal_view_on_event: resolve hit (release)");
                hit = hit_res.value;
            }
            if (hit.figure_id != 0) {
                owning_figure_id = hit.figure_id;
                struct yetty_ycore_void_result mr = terminal_emit_card_mouse_button(
                    terminal, hit.figure_id, hit.local_x, hit.local_y, btn, press, 0.0f,
                    event->mouse.mods);
                YETTY_RETURN_IF_ERR(yetty_ycore_int, mr,
                                    "terminal_view_on_event: emit_card_mouse_button failed");
            }
        }

        /* Pane-wide fallback: no yscene figure owns the click and the client
         * subscribed pane mouse — a drawable-tree surface has no figures, so
         * this is its only click path. Pane-local pixels, figure_id 0. */
        int pane_owns_click = 0;
        if (owning_figure_id == 0 && terminal->pane_mouse_click_subscribed) {
            pane_owns_click = 1;
            struct yetty_ycore_void_result mouse_res = terminal_emit_card_mouse_button(
                terminal, 0u, lx, ly, btn, press, 0.0f, event->mouse.mods);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, mouse_res,
                                "terminal_view_on_event: emit pane mouse_button failed");
        }

        /* A press that STARTED capture records its owner for the drag. */
        if (press && !capture_held) {
            terminal->mouse_capture_figure_id = owning_figure_id;
            terminal->mouse_capture_pane = owning_figure_id == 0 && pane_owns_click;
        }

        return YETTY_OK(yetty_ycore_int, 1);
    }

    case YETTY_YCORE_MOUSE_MOVE:
    case YETTY_YCORE_MOUSE_DRAG: {
        ydebug("terminal: MOUSE_MOVE win=(%.1f,%.1f) move_sub=%d", event->mouse.x, event->mouse.y,
               terminal->mouse_move_subscribed);

        /* Feed the live pointer (pane-local pixels) to the text renderer so
         * mouse-following shader effects track it. Independent of libvterm
         * mouse-mode subscription — effects work even when the app isn't
         * reading the mouse. The setter only forces a repaint when a coord
         * effect is active. */
        if (terminal->grid) {
            struct yetty_ycore_void_result mouse_res = yetty_yvterm_vterm_set_mouse(
                terminal->grid, event->mouse.x - view->bounds.x, event->mouse.y - view->bounds.y);
            if (YETTY_IS_ERR(mouse_res)) {
                yetty_ycore_error_destroy(mouse_res.error);
            }
        }

        /* Extend an in-flight selection. The platform's GLFW dispatcher
         * never synthesises MOUSE_DRAG — it only emits MOUSE_MOVE — so we
         * key off our own sel_dragging flag (set on MOUSE_DOWN, cleared on
         * MOUSE_UP) rather than the event type. Treat any cursor move
         * while we're dragging as drag extension. */
        if (terminal->sel_dragging) {
            float lx_sel = event->mouse.x - view->bounds.x;
            float ly_sel = event->mouse.y - view->bounds.y;
            uint32_t r, c;
            struct yetty_ycore_void_result cell_res =
                terminal_cell_from_local(terminal, lx_sel, ly_sel, &r, &c);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, cell_res,
                                "terminal_view_on_event: cell from local (drag)");
            if (r != terminal->sel_head_row || c != terminal->sel_head_col) {
                terminal->sel_head_row = r;
                terminal->sel_head_col = c;
                struct yetty_ycore_void_result psr = terminal_push_selection(terminal);
                YETTY_RETURN_IF_ERR(yetty_ycore_int, psr,
                                    "terminal_view_on_event: push_selection (drag) failed");
            }
            return YETTY_OK(yetty_ycore_int, 1);
        }

        if (!terminal->mouse_move_subscribed && !terminal->pane_mouse_move_subscribed) {
            return YETTY_OK(yetty_ycore_int, 0);
        }
        float lx = event->mouse.x - view->bounds.x;
        float ly = event->mouse.y - view->bounds.y;
        int move_in_bounds =
            !(lx < 0.0f || ly < 0.0f || lx >= view->bounds.w || ly >= view->bounds.h);

        /* CAPTURED motion routes to the drag's recorded OWNER, in or out of
         * bounds (desktop drag semantics) — never re-hit-tested, so a pane
         * background drag cannot hand itself to a figure it crosses. */
        if (terminal->mouse_buttons_held && terminal->mouse_capture_figure_id != 0 &&
            terminal->mouse_move_subscribed) {
            struct yetty_yfigure_hit_result hit_res = terminal_resolve_figure_hit(
                terminal, event->mouse.x, event->mouse.y, terminal->mouse_capture_figure_id);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, hit_res,
                                "terminal_view_on_event: resolve hit (captured move)");
            struct yetty_ycore_void_result mr = terminal_emit_card_mouse_move(
                terminal, terminal->mouse_capture_figure_id, hit_res.value.local_x,
                hit_res.value.local_y, terminal->mouse_buttons_held);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, mr,
                                "terminal_view_on_event: emit captured figure move failed");
            return YETTY_OK(yetty_ycore_int, 1);
        }
        if (terminal->mouse_buttons_held && terminal->mouse_capture_pane &&
            terminal->pane_mouse_move_subscribed) {
            struct yetty_ycore_void_result mr =
                terminal_emit_card_mouse_move(terminal, 0u, lx, ly, terminal->mouse_buttons_held);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, mr,
                                "terminal_view_on_event: emit captured pane move failed");
            return YETTY_OK(yetty_ycore_int, 1);
        }
        if (!move_in_bounds) {
            return YETTY_OK(yetty_ycore_int, 0); /* unowned out-of-bounds motion */
        }

        /* Hover (no capture). Figure hit first, then the pane fallback. */
        uint32_t move_owner_figure_id = 0;
        if (terminal->mouse_move_subscribed) {
            struct yetty_yfigure_hit_result hit_res =
                terminal_resolve_figure_hit(terminal, event->mouse.x, event->mouse.y, 0);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, hit_res,
                                "terminal_view_on_event: resolve hit (move)");
            struct yetty_yfigure_hit hit = hit_res.value;
            if (hit.figure_id != 0) {
                move_owner_figure_id = hit.figure_id;
                struct yetty_ycore_void_result mr =
                    terminal_emit_card_mouse_move(terminal, hit.figure_id, hit.local_x, hit.local_y,
                                                  terminal->mouse_buttons_held);
                YETTY_RETURN_IF_ERR(yetty_ycore_int, mr,
                                    "terminal_view_on_event: emit_card_mouse_move failed");
            }
        }

        /* Pane-wide fallback (see MOUSE_DOWN): unowned motion goes to a
         * pane-subscribed client with pane-local pixels. */
        if (move_owner_figure_id == 0 && terminal->pane_mouse_move_subscribed) {
            struct yetty_ycore_void_result mr =
                terminal_emit_card_mouse_move(terminal, 0u, lx, ly, terminal->mouse_buttons_held);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, mr,
                                "terminal_view_on_event: emit pane mouse_move failed");
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
         * if a figure is under the cursor, the wheel goes outbound; else a
         * pane-wide wheel subscription claims it (a fullscreen drawable app
         * scrolls its own content — same philosophy as ?1502 claiming the
         * scroll keys); else scrollback. */
        int wheel_mods = event->mouse_scroll.mods;
        if (!terminal_scrollback_is_active(terminal) && terminal->mouse_click_subscribed) {
            /* Window coords, same reason as MOUSE_DOWN. */
            struct yetty_yfigure_hit_result hit_res = terminal_resolve_figure_hit(
                terminal, event->mouse_scroll.x, event->mouse_scroll.y, 0);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, hit_res,
                                "terminal_view_on_event: resolve hit (scroll)");
            struct yetty_yfigure_hit hit = hit_res.value;
            if (hit.figure_id != 0) {
                struct yetty_ycore_void_result mr = terminal_emit_card_mouse_button(
                    terminal, hit.figure_id, hit.local_x, hit.local_y, 0, 0, event->mouse_scroll.dy,
                    wheel_mods);
                YETTY_RETURN_IF_ERR(
                    yetty_ycore_int, mr,
                    "terminal_view_on_event: emit_card_mouse_button (wheel) failed");
                return YETTY_OK(yetty_ycore_int, 1);
            }
        }
        if (!terminal_scrollback_is_active(terminal) && terminal->pane_mouse_wheel_subscribed) {
            struct yetty_ycore_void_result mr = terminal_emit_card_mouse_button(
                terminal, 0u, lx, ly, 0, 0, event->mouse_scroll.dy, wheel_mods);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, mr,
                                "terminal_view_on_event: emit pane wheel failed");
            return YETTY_OK(yetty_ycore_int, 1);
        }

        /* Modifier'd wheels (Ctrl / Ctrl-Shift) belong to the app-level
         * zoom gestures when no subscribed figure claimed them above —
         * report unconsumed so yetty.c's fallback (visual / cell zoom)
         * fires; scrollback must not eat them. Plain wheels reach yetty.c
         * only modifier-free, so this branch never starves scrollback. */
        if (wheel_mods & YETTY_MOD_CONTROL) {
            return YETTY_OK(yetty_ycore_int, 0);
        }

        int lines = (int)(event->mouse_scroll.dy * YETTY_YTERMINAL_WHEEL_LINES_PER_TICK);
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

/*=============================================================================
 * yclass wire surface — the terminal as the session root object.
 *===========================================================================*/

/* figure_root_container: navigate from the terminal (the session root a
 * connecting tool receives) to its root figure container. Object-returning
 * wire slot — a remote tool receives a session-bound container proxy; a
 * local caller receives the real container object. */
YETTY_ANNOTATE("virtual@yterminal:terminal:figure_root_container")
static struct yetty_yclass_object_ptr_result terminal_figure_root_container(
    struct yetty_yclass_object *obj)
{
    struct yetty_yterminal_terminal_ptr_result terminal_res = yetty_yterminal_terminal_from(obj);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, terminal_res,
                        "yterminal figure_root_container: object");
    struct yetty_yterminal_terminal *terminal = terminal_res.value;
    if (!terminal->root_container_obj) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yterminal figure_root_container: no root container");
    }
    return YETTY_OK(yetty_yclass_object_ptr, terminal->root_container_obj);
}

#include "yetty/gen/impl/yterminal/terminal.c"
