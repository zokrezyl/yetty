/*
 * daemon.c — the ymux daemon/server role: class@ymux:daemon (#695 phase 5).
 * There is ONE `ymux` executable; "daemon" is the persistent server role it
 * forks into — one binary, no separate daemon product.
 *
 * Owns the listening ipc socket, the session (panes + attachments +
 * controller policy), one PTY per pane (via the host spawn seam — the
 * daemon role wires a real forkpty, tests wire a memory pair), and
 * the per-connection frame plumbing. Single-threaded and non-blocking:
 * step() pumps accept → client frames → PTY output → projections → tx
 * flush once; the run loop (or a test) calls it repeatedly.
 *
 * Data flow (the tmux shape): client INPUT_* verbs → session (permission
 * gate) → pane engine encodes → engine host output → PTY write. PTY reads
 * → pane feed → per-attachment projector → PAINT frames. Clients never
 * see VT bytes or emulator state — only paint-format words.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/yplatform/ipc-socket.h>
#include <yetty/yplatform/pty.h>
#include <yetty/yterminal/client-input.h>
#include <yetty/yterminal/dcs-codes.h>
#include <yetty/ytrace/ytrace.h>
#include <yetty/ywire/channel.h>
#include <yetty/ywire/connection.h>
#include <yetty/ywire/wire-statemachine.h>

#include <yetty/api/ymux/attachment.h>
#include <yetty/api/ymux/engine.h>
#include <yetty/api/ymux/history.h>
#include <yetty/api/ymux/pane.h>
#include <yetty/api/ymux/projector.h>
#include <yetty/api/ymux/session.h>
#include <yetty/api/ymux/vtsink.h>

#include "op-stream.h"
#include "proto.h"
#include "rpc-lane.h"
#include "tx-queue.h"

/* Forward decls for same-module session methods newly added this codegen run
 * (their generated header is regenerated in the same pass; signatures match). */
struct yetty_ycore_void_result yetty_ymux_session_input_mouse_move(struct yetty_yclass_object *obj,
                                                                   uint32_t attachment_id,
                                                                   uint32_t row, uint32_t col,
                                                                   int mods);
struct yetty_ycore_void_result yetty_ymux_session_input_mouse_button(
    struct yetty_yclass_object *obj, uint32_t attachment_id, int button, int pressed, int mods);
struct yetty_ycore_void_result yetty_ymux_session_input_paste(struct yetty_yclass_object *obj,
                                                              uint32_t attachment_id,
                                                              const char *text, size_t len);

enum {
    YMUX_DAEMON_MAX_CONNECTIONS = 32,
    YMUX_DAEMON_MAX_SESSIONS = 16,
    YMUX_DAEMON_SESSION_NAME_MAX = 63,
    YMUX_DAEMON_MAX_PTYS = 64,
    YMUX_DAEMON_RX_CAP = 1u << 18,
    YMUX_DAEMON_TX_CAP = 1u << 21,
    YMUX_DAEMON_PTY_CHUNK = 16384,
    /* Per-pane, per-step ceiling on PTY intake, in chunks. A flooding
     * application (`find /`, `seq`) refills the PTY as fast as it is read;
     * an unbounded drain loop spins here for the flood's whole duration and
     * daemon_step_project never runs — the attached screen freezes until
     * the flood ends. Bound the intake so every step still projects; the
     * engine coalesces whatever queues up (tmux bounds its per-pass reads
     * the same way). 4 x 16 KiB ~ a few milliseconds of engine feeding. */
    YMUX_DAEMON_PTY_STEP_CHUNKS = 4,
    YMUX_DAEMON_PAINT_CAP = 1u << 20,
    /* Backpressure window: skip projecting for an attachment while more
     * than this many generations are unacked — the projector's shadow
     * diff coalesces the skipped frames into one delta later. */
    YMUX_DAEMON_MAX_UNACKED = 2,
    /* Slow-client recovery high-water mark: when a connection's undrained tx
     * backlog crosses this, the queued (stateful VT) stream is treated as
     * obsolete — #699 recovery discards it and forces a fresh complete redraw
     * rather than skip-and-continue a stateful stream. Below TX_CAP so recovery
     * fires before an outright overflow-close. */
    YMUX_DAEMON_TX_RECOVER_HIGH_WATER = (1u << 21) * 3 / 4,
    /* Give up (close, as tmux detaches a hopeless client) after this many
     * consecutive recoveries that never drain. */
    YMUX_DAEMON_SLOW_RECOVER_LIMIT = 4,
    /* Per-pane cap on concurrently-proxied ygui/ygreeter RPC channels. A single
     * figure-surface app opens one dynamic RPC channel (plus a few for its own
     * sub-figures); a small table covers it. */
    YMUX_DAEMON_PANE_RPC_CHANNELS = 16,
    /* Ceiling on the per-pane PTY output backlog (application input + terminal
     * replies). Crossing it is genuine overload — reported as an error rather
     * than dropping the tail and continuing a corrupted input stream. */
    YMUX_DAEMON_PANE_OUT_CAP = 1u << 18,
};

typedef struct yetty_yplatform_pty_ptr_result (*yetty_ymux_daemon_spawn_fn)(uint32_t rows,
                                                                            uint32_t cols,
                                                                            void *userdata);

/* The PTY seam: the daemon model never forks or opens devices itself. */
struct YETTY_ANNOTATE("expose") yetty_ymux_daemon_host {
    yetty_ymux_daemon_spawn_fn spawn;
    void *userdata;
};

/* One tmux-style NAMED session ("0", "1", … when unnamed; new-session -s
 * gives explicit names). The server starts with none; new-session
 * creates them, kill-session destroys them. */
struct daemon_session_entry {
    struct yetty_yclass_object *session; /* owned; NULL = free slot */
    char name[YMUX_DAEMON_SESSION_NAME_MAX + 1];
    uint64_t created_stamp; /* monotonic counter — "most recent" order */
};

struct daemon_pane_pty {
    struct yetty_platform_pty *pty;      /* owned; NULL = free slot */
    struct yetty_ymux_daemon *daemon;    /* backpointer for effect routing */
    struct yetty_yclass_object *session; /* owning session (pane ids are
                                          * per-session — the pair keys
                                          * this table) */
    uint32_t pane_id;
    uint32_t pty_rows;
    uint32_t pty_cols;
    /* Bounded FIFO for engine output (the application's input encodings + DA/DSR
     * replies) that a non-blocking PTY write could not accept yet. Drained on
     * each step; never silently dropped (overflow is a loud error, not data
     * loss followed by more bytes). */
    uint8_t *out_queue;
    size_t out_queue_len;
    size_t out_queue_cap;
    /* Non-scrolling figure-surface proxy (#695: ygreeter/ygui). The pane app's
     * yclass-RPC arrives as DCS 800000/800001 envelopes the engine captures via
     * host.rich (daemon_pane_rich reconstructs them). They are fed to this
     * dedicated ywire connection — separate from the text engine, so the text
     * path is untouched — which forwards each RPC channel's bytes to the
     * controlling client (RPC_RELAY); the client pipes them to yetty, which
     * serves + renders the figure. Responses ride back through rpc_writer ->
     * daemon_pane_output -> the app's PTY. */
    struct yetty_ywire_wire_statemachine *rpc_sm;  /* owned; decodes the envelopes */
    struct yetty_ywire_connection *rpc_connection; /* owned; forwards the channels */
    void *rpc_forward_state; /* owned; the forward primitive's per-connection state */
    /* The controller attachment the live proxied RPC channels are bound to. When
     * it changes (takeover) or drops (detach), the channels are closed toward the
     * pane app so it observes the loss rather than blocking on a reply that the
     * old controller can no longer deliver. 0 = no channels / unbound. */
    uint32_t rpc_controller;
    /* channel_id -> channel map for writing client-relayed responses back. A
     * ygui app opens a handful of channels; a small fixed table suffices. */
    struct {
        uint32_t channel_id;
        struct yetty_ywire_channel *channel;
    } rpc_channels[YMUX_DAEMON_PANE_RPC_CHANNELS];
    uint32_t rpc_channel_count;
};

struct daemon_connection {
    yetty_ipc_socket_t socket;           /* NULL = free slot */
    struct yetty_yclass_object *session; /* attached session (borrowed) */
    uint32_t attachment_id;              /* 0 = connected, not attached */
    uint32_t pane_id;
    uint32_t capabilities; /* client terminfo caps (YMUX_TERM_CAP_*) */
    uint64_t sent_generation;
    uint64_t acked_generation;
    /* Fault injection (tests only): fail the next N vtsink lane enqueues, so
     * the feed-failure recovery path is exercisable — the exactness of the
     * can_fit pre-check otherwise makes a black-box hit unreliable. */
    uint32_t fail_next_vtsink_tx;

    /* Chrome input seat (review #16): CONSUMED overlay events arrive here
     * COMPLETE (owned payloads, no truncation) and the daemon-role chrome
     * consumer drains them — scroll-mode: arrow keys move this attachment's
     * scrollback view. Bounded ring; a full ring refuses the frame (the
     * bridge does not pop the scene event until acceptance). */
    struct {
        uint32_t input_class;
        uint32_t byte_len;
        uint8_t *bytes; /* owned */
    } chrome_queue[8];
    uint32_t chrome_queue_head;
    uint32_t chrome_queue_count;
    uint64_t chrome_intake_count;
    uint32_t chrome_intake_class;
    /* Highest overlay-input sequence APPLIED on this connection (review
     * #19): a resend with sequence <= this re-ACKs without re-applying —
     * duplicate suppression for the sender's lost-ACK retry path. */
    /* Per-connection overlay-input applied high-water: dedups a same-connection
     * lost-ACK resend (a fresh connection starts at 0 and applies its events
     * fresh — reconnect is a NEW process, not an in-process resume, so there
     * is no cross-connection replay to alias; cycle-22 P0). */
    uint32_t overlay_applied_seq;
    /* Test seam (review #19 negative paths): refuse the next N overlay
     * events with a sequence-bearing NACK. */
    uint32_t refuse_next_overlay;
    /* STREAMING copy-mode key decoder state (review #19): an escape
     * sequence split across overlay frames accumulates here — CSI with
     * parameters (modified arrows) and SS3 application-cursor forms both
     * decode; a literal per-frame scan consumed split arrows without
     * moving. */
    uint8_t copy_key_pending[16];
    uint32_t copy_key_pending_len;
    /* COPY-MODE state (review #17): the daemon-role chrome consumer is a
     * real interactive mode — arrows move the copy cursor (scrolling at
     * the top edge), space anchors, enter copies the span to the daemon
     * paste buffer, q exits (CHROME_RELEASE to the client). */
    int copy_cursor_row;
    int copy_cursor_col;
    int copy_anchor_row;
    int copy_anchor_col;
    int copy_selecting;
    /* The last CONSUMED event's complete payload (owned) — the payload-
     * identity observation point for the seat (review #17). */
    uint8_t *chrome_last_bytes;
    uint32_t chrome_last_len;
    uint32_t chrome_last_class;
    /* Last PANE_MODES bitmask pushed (bit0 = app mouse); pushed on change so
     * the bridge can gate drag-selection ownership (review #13). */
    uint32_t sent_pane_modes;
    int sent_pane_modes_valid;
    /* #699.2 vtsink lane — the client published a hosted ymux:vtsink and we
     * originate typed feed() calls on this ASYNC session instead of enqueuing
     * YMUX_PROTO_VT frames. The session owns the lane transport; the proxy is
     * a bare handle wrapper (free()). All NULL until VTSINK_PUBLISH. */
    struct yetty_yclass_rpc_session *vtsink_session;
    struct yetty_yclass_transport *vtsink_lane; /* borrowed view for push_rx */
    struct yetty_yclass_object *vtsink_proxy;
    uint8_t *rx;
    size_t rx_len;
    uint8_t *tx;
    size_t tx_len;
    /* Bytes of the LEADING tx frame already written to the socket. tx[0] always
     * holds a valid frame header (daemon_step_tx reclaims only fully-sent
     * frames), so tx_sent==0 means tx[0] is at a clean boundary and tx_sent>0
     * means the leading frame is mid-flight. This is the exact boundary tracker
     * slow-client recovery needs — a boolean "residual bytes exist" would stay
     * true across whole trailing frames and starve recovery. */
    size_t tx_sent;
    /* Monotonic total bytes ever drained to the socket. The delta between two
     * recovery observations is the true "is the socket progressing" signal —
     * independent of tx_len, which stays high while a large leading frame is
     * mid-flight. */
    uint64_t tx_total_sent;
    uint64_t slow_last_total_sent;
    /* Consecutive slow-client recoveries WITHOUT any socket drain since the last
     * one; after a bound the connection is closed (a hopeless client, as tmux).
     * A client that keeps draining — even a huge single frame — is never counted
     * hopeless. */
    uint32_t slow_recover_count;
    int want_close;
};

struct YETTY_ANNOTATE("class@ymux:daemon") yetty_ymux_daemon {
    yetty_ipc_socket_t listener;
    char socket_path[YETTY_IPC_SOCKET_PATH_MAX];
    struct daemon_session_entry sessions[YMUX_DAEMON_MAX_SESSIONS];
    uint64_t session_stamp; /* mint for created_stamp / recency */
    uint32_t next_auto_name;
    struct yetty_ymux_daemon_host host;
    struct daemon_connection connections[YMUX_DAEMON_MAX_CONNECTIONS];
    struct daemon_pane_pty ptys[YMUX_DAEMON_MAX_PTYS];
    uint32_t default_rows;
    uint32_t default_cols;
    int shutdown_requested;
    /* The copy-mode PASTE BUFFER (tmux-style most-recent buffer): filled by
     * enter in copy-mode, written to the pane by the paste verb. GROWABLE
     * (review #19) up to a hard cap; crossing the cap sets the truncation
     * flag instead of silently dropping the tail. */
    char *paste_buffer;
    uint32_t paste_buffer_len;
    uint32_t paste_buffer_cap;
    int paste_truncated;
};

/* Provided by the generated impl glue (foot include). */
struct yetty_yclass_ptr_result yetty_ymux_daemon_class_get(void);
struct yetty_ymux_daemon_ptr_result yetty_ymux_daemon_from(struct yetty_yclass_object *obj);
YETTY_YRESULT_DECLARE(yetty_ymux_daemon_ptr, struct yetty_ymux_daemon *);

/*===========================================================================
 * Small helpers.
 *=========================================================================*/

/* Pane ids are per-session: the (session, pane_id) pair keys the table. */
static struct daemon_pane_pty *daemon_find_pty(struct yetty_ymux_daemon *daemon,
                                               struct yetty_yclass_object *session,
                                               uint32_t pane_id)
{
    for (uint32_t index = 0; index < YMUX_DAEMON_MAX_PTYS; ++index) {
        if (daemon->ptys[index].pty && daemon->ptys[index].session == session &&
            daemon->ptys[index].pane_id == pane_id) {
            return &daemon->ptys[index];
        }
    }
    return NULL;
}

/*--- Named sessions (tmux model) -----------------------------------*/

static struct daemon_session_entry *daemon_session_find(struct yetty_ymux_daemon *daemon,
                                                        const char *name)
{
    if (!name || !name[0]) {
        return NULL;
    }
    for (uint32_t index = 0; index < YMUX_DAEMON_MAX_SESSIONS; ++index) {
        if (daemon->sessions[index].session && strcmp(daemon->sessions[index].name, name) == 0) {
            return &daemon->sessions[index];
        }
    }
    return NULL;
}

/* tmux "most recently created/used" — the default target when a command
 * names no session. */
static struct daemon_session_entry *daemon_session_most_recent(struct yetty_ymux_daemon *daemon)
{
    struct daemon_session_entry *best = NULL;
    for (uint32_t index = 0; index < YMUX_DAEMON_MAX_SESSIONS; ++index) {
        if (daemon->sessions[index].session &&
            (!best || daemon->sessions[index].created_stamp > best->created_stamp)) {
            best = &daemon->sessions[index];
        }
    }
    return best;
}

/* Resolve a command's session target: explicit name, or most recent when
 * the name is empty (tmux semantics). */
static struct daemon_session_entry *daemon_session_target(struct yetty_ymux_daemon *daemon,
                                                          const char *name)
{
    if (name && name[0]) {
        return daemon_session_find(daemon, name);
    }
    return daemon_session_most_recent(daemon);
}

static struct yetty_ycore_void_result daemon_enqueue(struct daemon_connection *connection,
                                                     uint32_t type, const void *payload,
                                                     size_t payload_len);

/* Presentation effects (#695, exactly-once): produced by the pane's
 * engine during a feed; enqueued to live attachments immediately and
 * never replayed — a reconnecting client does not see old bells,
 * titles, or clipboard writes. Bell/title fan out to every attachment
 * of the pane; clipboard goes to the CONTROLLER only. */
static void daemon_effect_broadcast(struct yetty_ymux_daemon *daemon,
                                    struct yetty_yclass_object *session, uint32_t pane_id,
                                    uint32_t type, const void *payload, size_t payload_len,
                                    int controller_only)
{
    uint32_t controller = 0;
    struct yetty_ycore_uint32_result controller_res = yetty_ymux_session_controller(session);
    if (YETTY_IS_OK(controller_res)) {
        controller = controller_res.value;
    } else {
        yetty_ycore_error_destroy(controller_res.error);
    }
    for (uint32_t index = 0; index < YMUX_DAEMON_MAX_CONNECTIONS; ++index) {
        struct daemon_connection *connection = &daemon->connections[index];
        if (!connection->socket || connection->attachment_id == 0 ||
            connection->session != session || connection->pane_id != pane_id) {
            continue;
        }
        if (controller_only && connection->attachment_id != controller) {
            continue;
        }
        struct yetty_ycore_void_result enqueue_res =
            daemon_enqueue(connection, type, payload, payload_len);
        if (YETTY_IS_ERR(enqueue_res)) {
            yetty_ycore_error_destroy(enqueue_res.error);
        }
    }
}

static struct yetty_ycore_void_result daemon_pane_bell(void *userdata)
{
    struct daemon_pane_pty *slot = userdata;
    daemon_effect_broadcast(slot->daemon, slot->session, slot->pane_id, YMUX_PROTO_EFFECT_BELL,
                            NULL, 0, 0);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result daemon_pane_title(const char *title, size_t len,
                                                        void *userdata)
{
    struct daemon_pane_pty *slot = userdata;
    daemon_effect_broadcast(slot->daemon, slot->session, slot->pane_id, YMUX_PROTO_EFFECT_TITLE,
                            title, len, 0);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result daemon_pane_clipboard(const char *text, size_t len,
                                                            int clipboard, void *userdata)
{
    struct daemon_pane_pty *slot = userdata;
    uint32_t target = clipboard ? 1u : 0u;
    size_t payload_len = sizeof(uint32_t) + len;
    uint8_t *payload = malloc(payload_len);
    if (!payload) {
        return YETTY_ERR(yetty_ycore_void, "ymux daemon: clipboard payload alloc");
    }
    memcpy(payload, &target, sizeof(uint32_t));
    if (len) {
        memcpy(payload + sizeof(uint32_t), text, len);
    }
    daemon_effect_broadcast(slot->daemon, slot->session, slot->pane_id, YMUX_PROTO_EFFECT_CLIPBOARD,
                            payload, payload_len,
                            /*controller_only=*/1);
    free(payload);
    return YETTY_OK_VOID();
}

/* Append bytes to the pane's PTY output backlog (bounded). Overflow is a loud
 * error — data loss followed by more input bytes would corrupt the stream. */
static struct yetty_ycore_void_result daemon_pane_enqueue_output(struct daemon_pane_pty *slot,
                                                                 const uint8_t *bytes, size_t len)
{
    if (len == 0) {
        return YETTY_OK_VOID();
    }
    size_t needed = slot->out_queue_len + len;
    if (needed > YMUX_DAEMON_PANE_OUT_CAP) {
        return YETTY_ERR(yetty_ycore_void, "ymux daemon: pane PTY output backlog overflow");
    }
    if (needed > slot->out_queue_cap) {
        size_t new_cap = slot->out_queue_cap ? slot->out_queue_cap * 2 : 4096;
        while (new_cap < needed) {
            new_cap *= 2;
        }
        if (new_cap > YMUX_DAEMON_PANE_OUT_CAP) {
            new_cap = YMUX_DAEMON_PANE_OUT_CAP;
        }
        uint8_t *grown = realloc(slot->out_queue, new_cap);
        if (!grown) {
            return YETTY_ERR(yetty_ycore_void, "ymux daemon: pane PTY output backlog alloc");
        }
        slot->out_queue = grown;
        slot->out_queue_cap = new_cap;
    }
    memcpy(slot->out_queue + slot->out_queue_len, bytes, len);
    slot->out_queue_len = needed;
    return YETTY_OK_VOID();
}

/* Drain as much of the queued PTY output as a non-blocking write accepts,
 * keeping the unwritten remainder in order for the next attempt. */
static void daemon_pane_drain_output(struct daemon_pane_pty *slot)
{
    if (!slot->pty || slot->out_queue_len == 0) {
        return;
    }
    size_t written = 0;
    while (written < slot->out_queue_len) {
        struct yetty_ycore_size_result write_res = slot->pty->ops->write(
            slot->pty, (const char *)(slot->out_queue + written), slot->out_queue_len - written);
        if (YETTY_IS_ERR(write_res)) {
            yetty_ycore_error_destroy(write_res.error);
            break;
        }
        if (write_res.value == 0) {
            break; /* would block — retry next step */
        }
        written += write_res.value;
    }
    if (written > 0) {
        slot->out_queue_len -= written;
        memmove(slot->out_queue, slot->out_queue + written, slot->out_queue_len);
    }
}

/* Engine host output → PTY write (input encodings, DA/DSR responses). A
 * non-blocking write that can't take everything queues the remainder instead of
 * dropping it; ordering is preserved by flushing the backlog first. */
static struct yetty_ycore_void_result daemon_pane_output(const char *bytes, size_t len,
                                                         void *userdata)
{
    struct daemon_pane_pty *slot = userdata;
    if (!slot->pty) {
        return YETTY_OK_VOID();
    }
    daemon_pane_drain_output(slot);
    size_t written = 0;
    /* Write directly only when the backlog is empty — otherwise the new bytes
     * must stay behind the queued ones to preserve order. */
    if (slot->out_queue_len == 0) {
        while (written < len) {
            struct yetty_ycore_size_result write_res =
                slot->pty->ops->write(slot->pty, bytes + written, len - written);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, write_res, "ymux daemon: pty write");
            if (write_res.value == 0) {
                break; /* would block — queue the tail below */
            }
            written += write_res.value;
        }
    }
    return daemon_pane_enqueue_output(slot, (const uint8_t *)bytes + written, len - written);
}

/*===========================================================================
 * Non-scrolling figure-surface RPC proxy (#695: ygui/ygreeter). See the
 * daemon_pane_pty comment: the pane app's yclass-RPC (DCS 800000/800001) is
 * tunnelled through the daemon role to the controlling client, which pipes it
 * to yetty where the figure is served + rendered. The daemon is a transparent
 * relay — it never decodes an RPC call.
 *=========================================================================*/

/* Emit side of the pane's forward connection: yetty's replies (relayed back
 * from the client) reach the pane app as terminal INPUT via the PTY-output
 * path — the app's own ywire connection reads them off its stdin. */
YETTY_EXTERNAL_CALLBACK
static struct yetty_ycore_void_result daemon_pane_rpc_writer(const uint8_t *bytes, size_t n,
                                                             void *user)
{
    struct daemon_pane_pty *slot = user;
    return daemon_pane_output((const char *)bytes, n, slot);
}

/* Remember channel_id -> channel so a client-relayed response writes back to the
 * right proxied channel. Idempotent (re-seen channel just refreshes). */
static void daemon_pane_rpc_track(struct daemon_pane_pty *slot, uint32_t channel_id,
                                  struct yetty_ywire_channel *channel)
{
    for (uint32_t index = 0; index < slot->rpc_channel_count; ++index) {
        if (slot->rpc_channels[index].channel_id == channel_id) {
            slot->rpc_channels[index].channel = channel;
            return;
        }
    }
    if (slot->rpc_channel_count < YMUX_DAEMON_PANE_RPC_CHANNELS) {
        slot->rpc_channels[slot->rpc_channel_count].channel_id = channel_id;
        slot->rpc_channels[slot->rpc_channel_count].channel = channel;
        slot->rpc_channel_count++;
    }
}

/* rpc_forward_connection callback: one run of the pane app's RPC request bytes
 * on a proxied channel. Relay to the CONTROLLING client (which opens a matching
 * channel to yetty and pipes them), tagged with the channel id for the return
 * path. Sending to the controller only keeps one figure + one response stream. */
YETTY_EXTERNAL_CALLBACK
static void daemon_pane_rpc_forward(void *userdata, struct yetty_ywire_channel *channel,
                                    const uint8_t *bytes, size_t n)
{
    struct daemon_pane_pty *slot = userdata;
    if (!slot || !channel || !bytes || n == 0) {
        return;
    }
    /* Bind ownership synchronously, at the moment the first request is actually
     * relayed — NOT in the pre-step controller check, which runs before this
     * PTY output is parsed. A channel tracked mid-step with rpc_controller still
     * unset would be invisible to the reverse-close on a following detach and
     * would strand. Resolve the current controller now: with none attached there
     * is no one to serve/render the figure, so refuse the channel rather than
     * track it unowned. */
    uint32_t controller = 0;
    struct yetty_ycore_uint32_result controller_res = yetty_ymux_session_controller(slot->session);
    if (YETTY_IS_OK(controller_res)) {
        controller = controller_res.value;
    } else {
        yetty_ycore_error_destroy(controller_res.error);
    }
    if (controller == 0) {
        struct yetty_ycore_void_result close_res = yetty_ywire_channel_close(channel);
        if (YETTY_IS_ERR(close_res)) {
            yetty_ycore_error_destroy(close_res.error);
        }
        return;
    }
    slot->rpc_controller = controller; /* the owner of the live channels */
    uint32_t channel_id = yetty_ywire_channel_id(channel);
    daemon_pane_rpc_track(slot, channel_id, channel);
    size_t payload_len = sizeof(uint32_t) + n; /* [channel_id u32][raw request bytes] */
    uint8_t *payload = malloc(payload_len);
    if (!payload) {
        return;
    }
    memcpy(payload, &channel_id, sizeof(uint32_t));
    memcpy(payload + sizeof(uint32_t), bytes, n);
    daemon_effect_broadcast(slot->daemon, slot->session, slot->pane_id, YMUX_PROTO_RPC_RELAY,
                            payload, payload_len, /*controller_only=*/1);
    free(payload);
}

/* rpc_forward_connection close callback: the pane app closed a proxied RPC
 * channel. Drop the daemon-side pairing and tell the controlling client to tear
 * down its matching upstream channel, so neither side leaks a channel nor
 * misroutes a late response into a reused slot. */
YETTY_EXTERNAL_CALLBACK
static void daemon_pane_rpc_close(void *userdata, struct yetty_ywire_channel *channel)
{
    struct daemon_pane_pty *slot = userdata;
    if (!slot || !channel) {
        return;
    }
    uint32_t channel_id = yetty_ywire_channel_id(channel);
    int removed = 0;
    for (uint32_t index = 0; index < slot->rpc_channel_count; ++index) {
        if (slot->rpc_channels[index].channel_id == channel_id) {
            slot->rpc_channels[index] = slot->rpc_channels[--slot->rpc_channel_count];
            removed = 1;
            break;
        }
    }
    /* Only tell the controller when WE observed the close first (pane-initiated).
     * If the controller already asked us to close (mapping already gone), the
     * broadcast would be a spurious echo. */
    if (removed) {
        daemon_effect_broadcast(slot->daemon, slot->session, slot->pane_id,
                                YMUX_PROTO_RPC_RELAY_CLOSE, &channel_id, sizeof(channel_id),
                                /*controller_only=*/1);
    }
}

/* Enqueue RPC_RELAY_CLOSE for one channel to a SPECIFIC attachment (not the
 * current controller). Used to tell the OUTGOING controller to retire its
 * upstream Yetty channel on takeover/detach — daemon_effect_broadcast targets
 * the current controller, which is now the wrong (or a NULL) one. */
static void daemon_notify_attachment_relay_close(struct daemon_pane_pty *slot,
                                                 uint32_t attachment_id, uint32_t channel_id)
{
    if (attachment_id == 0) {
        return;
    }
    for (uint32_t index = 0; index < YMUX_DAEMON_MAX_CONNECTIONS; ++index) {
        struct daemon_connection *connection = &slot->daemon->connections[index];
        if (!connection->socket || connection->attachment_id != attachment_id ||
            connection->session != slot->session || connection->pane_id != slot->pane_id) {
            continue;
        }
        struct yetty_ycore_void_result enqueue_res =
            daemon_enqueue(connection, YMUX_PROTO_RPC_RELAY_CLOSE, &channel_id, sizeof(channel_id));
        if (YETTY_IS_ERR(enqueue_res)) {
            yetty_ycore_error_destroy(enqueue_res.error);
        }
        break;
    }
}

/* Close every live proxied RPC channel toward the pane app (best-effort). Both
 * ends are cancelled: each channel is closed toward the pane app AND an
 * RPC_RELAY_CLOSE is sent to `owner_attachment` (the controller that opened the
 * upstream channel) so it retires its Yetty-side channel too. Sending happens
 * before the mapping is dropped (channel_id still known); the close event's own
 * callback then sees no mapping and does not re-broadcast. */
static void daemon_pane_close_all_rpc_channels(struct daemon_pane_pty *slot,
                                               uint32_t owner_attachment)
{
    while (slot->rpc_channel_count > 0) {
        uint32_t channel_id = slot->rpc_channels[slot->rpc_channel_count - 1].channel_id;
        struct yetty_ywire_channel *channel =
            slot->rpc_channels[slot->rpc_channel_count - 1].channel;
        slot->rpc_channel_count--;
        daemon_notify_attachment_relay_close(slot, owner_attachment, channel_id);
        if (channel) {
            struct yetty_ycore_void_result close_res = yetty_ywire_channel_close(channel);
            if (YETTY_IS_ERR(close_res)) {
                yetty_ycore_error_destroy(close_res.error);
            }
        }
    }
}

/* Bind the pane's live proxied RPC channels to the current session controller.
 * If the controller CHANGED (takeover) or DROPPED (detach -> 0) while channels
 * are live, close them toward the pane app: the old controller can no longer
 * deliver a response and the new one never saw the request, so the app must
 * observe the loss rather than deadlock. Called once per pump step per pane. */
static void daemon_pane_rpc_check_controller(struct daemon_pane_pty *slot)
{
    uint32_t controller = 0;
    struct yetty_ycore_uint32_result controller_res = yetty_ymux_session_controller(slot->session);
    if (YETTY_IS_OK(controller_res)) {
        controller = controller_res.value;
    } else {
        yetty_ycore_error_destroy(controller_res.error);
    }
    if (slot->rpc_channel_count == 0) {
        slot->rpc_controller = controller; /* rebind when the next channel opens */
        return;
    }
    if (slot->rpc_controller == 0) {
        slot->rpc_controller = controller; /* first binding for these channels */
        return;
    }
    if (controller != slot->rpc_controller) {
        /* Cancel both ends: close toward the pane app AND tell the OUTGOING
         * controller (slot->rpc_controller, the channels' owner) to retire its
         * upstream Yetty channels — otherwise those leak with late responses
         * refused for lack of controller. */
        daemon_pane_close_all_rpc_channels(slot, slot->rpc_controller);
        slot->rpc_controller = controller;
    }
}

/* Engine host.rich for a pane. The ymux engine captures every yetty-vendor DCS
 * envelope (`ESC P <code> y <body> ST`) and, for codes other than the scrolling
 * card (600001, handled in pane_rich), calls this. The yclass-RPC codes
 * (800000/800001) carry an in-pane ygui/ygreeter figure built over RPC:
 * reconstruct the envelope byte-exact (the body is base64, no embedded ESC) and
 * feed it to the pane's dedicated forward connection, which tunnels the RPC to
 * the client. Any other code is not ours — ignore it. */
YETTY_EXTERNAL_CALLBACK
static struct yetty_ycore_void_result daemon_pane_rich(uint32_t code, const char *payload,
                                                       size_t len, uint64_t logical_line_id,
                                                       uint32_t logical_cell_offset, void *userdata)
{
    (void)logical_line_id;
    (void)logical_cell_offset;
    struct daemon_pane_pty *slot = userdata;
    if ((code != YETTY_DCS_YCLASS_RPC && code != YETTY_DCS_YWIRE_CHANNEL) || !slot ||
        !slot->rpc_sm) {
        return YETTY_OK_VOID();
    }
    char intro[32];
    int intro_len = snprintf(intro, sizeof(intro), "\x1bP%uy", code);
    if (intro_len < 0 || (size_t)intro_len >= sizeof(intro)) {
        return YETTY_ERR(yetty_ycore_void, "ymux daemon_pane_rich: bad code");
    }
    struct yetty_ycore_void_result feed_res =
        yetty_ywire_wire_statemachine_feed(slot->rpc_sm, intro, (size_t)intro_len);
    if (YETTY_IS_OK(feed_res) && len > 0) {
        feed_res = yetty_ywire_wire_statemachine_feed(slot->rpc_sm, payload, len);
    }
    if (YETTY_IS_OK(feed_res)) {
        feed_res = yetty_ywire_wire_statemachine_feed(slot->rpc_sm, "\x1b\\", 2);
    }
    YETTY_RETURN_IF_ERR(yetty_ycore_void, feed_res, "ymux daemon_pane_rich: sm feed");
    return YETTY_OK_VOID();
}

/* Stand up the per-pane figure-surface RPC-forward connection. Non-fatal: a
 * failure just means ygui figures won't render in this pane; text is unaffected.
 * Called once when a pane's PTY slot is created. */
static void daemon_pane_rpc_setup(struct daemon_pane_pty *slot)
{
    struct yetty_ywire_wire_statemachine_ptr_result sm_res =
        yetty_ywire_wire_statemachine_create(NULL);
    if (YETTY_IS_ERR(sm_res)) {
        yetty_ycore_error_destroy(sm_res.error);
        return;
    }
    slot->rpc_sm = sm_res.value;
    struct yetty_ywire_connection_ptr_result conn_res =
        yetty_ywire_connection_attach(slot->rpc_sm, daemon_pane_rpc_writer, slot, /*compressed=*/0);
    if (YETTY_IS_ERR(conn_res)) {
        yetty_ycore_error_destroy(conn_res.error);
        struct yetty_ycore_void_result sm_destroy =
            yetty_ywire_wire_statemachine_destroy(slot->rpc_sm);
        if (YETTY_IS_ERR(sm_destroy)) {
            yetty_ycore_error_destroy(sm_destroy.error);
        }
        slot->rpc_sm = NULL;
        return;
    }
    slot->rpc_connection = conn_res.value;
    struct yetty_ycore_void_result fwd_res =
        yetty_yclass_rpc_forward_connection(slot->rpc_connection, daemon_pane_rpc_forward,
                                            daemon_pane_rpc_close, slot, &slot->rpc_forward_state);
    if (YETTY_IS_ERR(fwd_res)) {
        yetty_ycore_error_destroy(fwd_res.error);
    }
}

/* Tear down the forward connection when a pane's PTY slot is freed. */
static void daemon_pane_rpc_teardown(struct daemon_pane_pty *slot)
{
    if (slot->rpc_connection) {
        struct yetty_ycore_void_result conn_res =
            yetty_ywire_connection_destroy(slot->rpc_connection);
        if (YETTY_IS_ERR(conn_res)) {
            yetty_ycore_error_destroy(conn_res.error);
        }
        slot->rpc_connection = NULL;
    }
    if (slot->rpc_sm) {
        struct yetty_ycore_void_result sm_res = yetty_ywire_wire_statemachine_destroy(slot->rpc_sm);
        if (YETTY_IS_ERR(sm_res)) {
            yetty_ycore_error_destroy(sm_res.error);
        }
        slot->rpc_sm = NULL;
    }
    /* Frees the forward primitive's per-connection config AND any per-channel
     * forward states still live at teardown — connection destruction above fired
     * no CLOSE events for them, so a plain free() would leak them. */
    yetty_yclass_rpc_forward_connection_destroy(slot->rpc_forward_state);
    slot->rpc_forward_state = NULL;
    slot->rpc_channel_count = 0;
}

/* Queue one frame on a connection (header + payload into the tx buffer).
 * A connection too slow to drain its window is closed, as in tmux. */
static struct yetty_ycore_void_result daemon_enqueue(struct daemon_connection *connection,
                                                     uint32_t type, const void *payload,
                                                     size_t payload_len)
{
    size_t frame_len = YMUX_PROTO_HEADER_WORDS * sizeof(uint32_t) + payload_len;
    if (connection->tx_len + frame_len > YMUX_DAEMON_TX_CAP) {
        connection->want_close = 1;
        return YETTY_ERR(yetty_ycore_void, "ymux daemon: tx overflow, closing connection");
    }
    uint32_t header[YMUX_PROTO_HEADER_WORDS] = {YMUX_PROTO_MAGIC, type, (uint32_t)payload_len};
    memcpy(connection->tx + connection->tx_len, header, sizeof(header));
    connection->tx_len += sizeof(header);
    if (payload_len) {
        memcpy(connection->tx + connection->tx_len, payload, payload_len);
        connection->tx_len += payload_len;
    }
    return YETTY_OK_VOID();
}

/* Whether a frame of exactly `payload_len` bytes would fit the fixed tx buffer
 * right now. The projector uses this to pre-check a fully-built frame before
 * enqueuing, so an oversized redraw defers (invalidate + full redraw next step)
 * instead of overflow-closing a client that is in fact draining. Unlike a
 * fixed reserve, it accounts for the frame's EXACT size (header + payload). */
static int daemon_tx_can_fit(const struct daemon_connection *connection, size_t payload_len)
{
    size_t frame_len = YMUX_PROTO_HEADER_WORDS * sizeof(uint32_t) + payload_len;
    return connection->tx_len + frame_len <= YMUX_DAEMON_TX_CAP;
}

static void daemon_enqueue_refuse(struct daemon_connection *connection, uint32_t reason)
{
    struct yetty_ycore_void_result enqueue_res =
        daemon_enqueue(connection, YMUX_PROTO_REFUSE, &reason, sizeof(reason));
    if (YETTY_IS_ERR(enqueue_res)) {
        yetty_ycore_error_destroy(enqueue_res.error);
    }
}

/* Outbound leg of the vtsink lane: wrap the session's request bytes into a
 * VTSINK_RPC frame toward this connection. The enqueue Result PROPAGATES
 * (review #10): a failure fails the typed feed() call synchronously, so the
 * projector shadow/generation state is never committed for bytes that never
 * reached the queue. */
static struct yetty_ycore_void_result daemon_vtsink_lane_tx(const uint8_t *bytes, size_t len,
                                                            void *userdata)
{
    struct daemon_connection *connection = userdata;
    if (connection->fail_next_vtsink_tx > 0) {
        --connection->fail_next_vtsink_tx;
        return YETTY_ERR(yetty_ycore_void, "ymux daemon: vtsink lane tx fault injected");
    }
    struct yetty_ycore_void_result enqueue_res =
        daemon_enqueue(connection, YMUX_PROTO_VTSINK_RPC, bytes, len);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, enqueue_res, "ymux daemon: vtsink lane tx enqueue");
    return YETTY_OK_VOID();
}

static void daemon_vtsink_teardown(struct daemon_connection *connection)
{
    free(connection->vtsink_proxy);
    connection->vtsink_proxy = NULL;
    if (connection->vtsink_session) {
        /* Destroys the session AND the lane transport it owns; still-pending
         * pipelined completions are surfaced/destroyed by the session. */
        struct yetty_ycore_void_result session_res =
            yetty_yclass_rpc_session_destroy(connection->vtsink_session);
        if (YETTY_IS_ERR(session_res)) {
            yetty_ycore_error_destroy(session_res.error);
        }
        connection->vtsink_session = NULL;
    }
    connection->vtsink_lane = NULL;
}

static void daemon_connection_reset(struct daemon_connection *connection)
{
    if (connection->socket) {
        yetty_platform_socket_close(connection->socket);
    }
    daemon_vtsink_teardown(connection);
    free(connection->rx);
    free(connection->tx);
    for (uint32_t index = 0; index < connection->chrome_queue_count; ++index) {
        uint32_t slot = (connection->chrome_queue_head + index) % 8;
        free(connection->chrome_queue[slot].bytes);
    }
    free(connection->chrome_last_bytes);
    memset(connection, 0, sizeof(*connection));
}

/* Push the pane's canonical engine geometry into its PTY when it drifted
 * (attach-as-controller and controller RESIZE both change it). */
static void daemon_sync_pty_size(struct yetty_ymux_daemon *daemon,
                                 struct yetty_yclass_object *session, uint32_t pane_id)
{
    struct daemon_pane_pty *slot = daemon_find_pty(daemon, session, pane_id);
    if (!slot) {
        return;
    }
    struct yetty_yclass_object_ptr_result pane_res = yetty_ymux_session_pane(session, pane_id);
    if (YETTY_IS_ERR(pane_res)) {
        yetty_ycore_error_destroy(pane_res.error);
        return;
    }
    struct yetty_yclass_object_ptr_result engine_res = yetty_ymux_pane_engine(pane_res.value);
    if (YETTY_IS_ERR(engine_res)) {
        yetty_ycore_error_destroy(engine_res.error);
        return;
    }
    uint32_t rows = 0, cols = 0;
    struct yetty_ycore_void_result dims_res =
        yetty_ymux_engine_dims(engine_res.value, &rows, &cols);
    if (YETTY_IS_ERR(dims_res)) {
        yetty_ycore_error_destroy(dims_res.error);
        return;
    }
    if (rows == slot->pty_rows && cols == slot->pty_cols) {
        return;
    }
    struct yetty_ycore_void_result resize_res = slot->pty->ops->resize(slot->pty, cols, rows, 0, 0);
    if (YETTY_IS_ERR(resize_res)) {
        yetty_ycore_error_destroy(resize_res.error);
        return;
    }
    slot->pty_rows = rows;
    slot->pty_cols = cols;
}

/*===========================================================================
 * Frame dispatch.
 *=========================================================================*/

/* Deliver the reverse RPC_RELAY_CLOSE to a departing controller and close the
 * pane-side channels, WHILE the connection still carries its identity. The
 * explicit-detach and forced-detach paths null out attachment_id/pane_id/session
 * synchronously in the frame handler; if relay cleanup only ran afterwards (the
 * per-pump daemon_pane_rpc_check_controller) it could no longer resolve the
 * outgoing attachment to notify it, leaking that client's upstream Yetty
 * channels. Call this before clearing the connection's identity. */
static void daemon_release_connection_relays(struct yetty_ymux_daemon *daemon,
                                             struct daemon_connection *connection)
{
    if (!connection->session || connection->attachment_id == 0) {
        return;
    }
    struct daemon_pane_pty *slot =
        daemon_find_pty(daemon, connection->session, connection->pane_id);
    if (slot && slot->rpc_channel_count > 0 && slot->rpc_controller == connection->attachment_id) {
        daemon_pane_close_all_rpc_channels(slot, connection->attachment_id);
        slot->rpc_controller = 0;
    }
}

static void daemon_close_pane(struct yetty_ymux_daemon *daemon, struct yetty_yclass_object *session,
                              uint32_t pane_id)
{
    /* Reverse-close the controller's relays before the PTY (and its channel map)
     * is torn down, so the controller retires its upstream Yetty channels. */
    struct daemon_pane_pty *pane_slot = daemon_find_pty(daemon, session, pane_id);
    if (pane_slot && pane_slot->rpc_channel_count > 0) {
        daemon_pane_close_all_rpc_channels(pane_slot, pane_slot->rpc_controller);
        pane_slot->rpc_controller = 0;
    }
    /* Tell every attached client, drop their attachment state, close the
     * session pane (which detaches server-side), then the PTY. */
    for (uint32_t index = 0; index < YMUX_DAEMON_MAX_CONNECTIONS; ++index) {
        struct daemon_connection *connection = &daemon->connections[index];
        if (!connection->socket || connection->attachment_id == 0 ||
            connection->session != session || connection->pane_id != pane_id) {
            continue;
        }
        struct yetty_ycore_void_result enqueue_res =
            daemon_enqueue(connection, YMUX_PROTO_PANE_EXIT, &pane_id, sizeof(pane_id));
        if (YETTY_IS_ERR(enqueue_res)) {
            yetty_ycore_error_destroy(enqueue_res.error);
        }
        connection->attachment_id = 0;
        connection->pane_id = 0;
        connection->session = NULL;
    }
    struct yetty_ycore_void_result close_res = yetty_ymux_session_pane_close(session, pane_id);
    if (YETTY_IS_ERR(close_res)) {
        yetty_ycore_error_destroy(close_res.error);
    }
    struct daemon_pane_pty *slot = daemon_find_pty(daemon, session, pane_id);
    if (slot) {
        daemon_pane_rpc_teardown(slot);
        struct yetty_ycore_void_result destroy_res = slot->pty->ops->destroy(slot->pty);
        if (YETTY_IS_ERR(destroy_res)) {
            yetty_ycore_error_destroy(destroy_res.error);
        }
        free(slot->out_queue);
        memset(slot, 0, sizeof(*slot));
    }
}

static void daemon_handle_attach(struct yetty_ymux_daemon *daemon,
                                 struct daemon_connection *connection, const uint8_t *payload,
                                 size_t payload_len)
{
    if (payload_len < 8 * sizeof(uint32_t)) {
        daemon_enqueue_refuse(connection, YMUX_PROTO_REFUSE_BAD_FRAME);
        return;
    }
    uint32_t words[8];
    memcpy(words, payload, sizeof(words));
    uint32_t version = words[0];
    uint32_t pane_id = words[1];
    uint32_t view_rows = words[2];
    uint32_t view_cols = words[3];
    uint32_t cell_pixel_height = words[4];
    uint32_t name_len = words[5];
    uint32_t token_len = words[6];
    uint32_t capabilities = words[7];
    if (version != YMUX_PROTO_VERSION) {
        daemon_enqueue_refuse(connection, YMUX_PROTO_REFUSE_VERSION);
        return;
    }
    if (name_len > YMUX_DAEMON_SESSION_NAME_MAX || token_len > YMUX_PROTO_TOKEN_MAX ||
        8 * sizeof(uint32_t) + name_len + token_len > payload_len) {
        daemon_enqueue_refuse(connection, YMUX_PROTO_REFUSE_BAD_FRAME);
        return;
    }
    char session_name[YMUX_DAEMON_SESSION_NAME_MAX + 1] = {0};
    memcpy(session_name, payload + 8 * sizeof(uint32_t), name_len);
    char token[YMUX_PROTO_TOKEN_MAX + 1] = {0};
    memcpy(token, payload + 8 * sizeof(uint32_t) + name_len, token_len);
    /* Terminal-strings tail (proto 9): resolve the capability profile via
     * the tmux terminfo/features state model when the client names its
     * terminal; absent (both lengths zero / no tail) = bitmask-only. */
    char client_term[64] = {0};
    char client_features[128] = {0};
    {
        size_t tail = 8 * sizeof(uint32_t) + name_len + token_len;
        if (tail + 2 * sizeof(uint32_t) <= payload_len) {
            uint32_t term_len = 0;
            uint32_t features_len = 0;
            memcpy(&term_len, payload + tail, sizeof(uint32_t));
            memcpy(&features_len, payload + tail + sizeof(uint32_t), sizeof(uint32_t));
            if (term_len < sizeof(client_term) && features_len < sizeof(client_features) &&
                tail + 2 * sizeof(uint32_t) + term_len + features_len <= payload_len) {
                memcpy(client_term, payload + tail + 2 * sizeof(uint32_t), term_len);
                memcpy(client_features, payload + tail + 2 * sizeof(uint32_t) + term_len,
                       features_len);
            }
        }
    }

    /* tmux attach-session: the target session must EXIST (empty name =
     * most recent); attach never creates sessions. */
    struct daemon_session_entry *entry = daemon_session_target(daemon, session_name);
    if (!entry) {
        daemon_enqueue_refuse(connection, YMUX_PROTO_REFUSE_UNKNOWN_PANE);
        return;
    }

    if (connection->attachment_id != 0 && connection->session) {
        /* Re-attach on a live connection: detach the old view first. */
        struct yetty_ycore_void_result detach_res =
            yetty_ymux_session_detach(connection->session, connection->attachment_id);
        if (YETTY_IS_ERR(detach_res)) {
            yetty_ycore_error_destroy(detach_res.error);
        }
        connection->attachment_id = 0;
        connection->pane_id = 0;
        connection->session = NULL;
    }

    if (pane_id == 0) {
        struct yetty_ycore_uint32_result active_res =
            yetty_ymux_session_active_pane(entry->session);
        if (YETTY_IS_OK(active_res)) {
            pane_id = active_res.value;
        } else {
            yetty_ycore_error_destroy(active_res.error);
        }
    }
    if (pane_id == 0) {
        daemon_enqueue_refuse(connection, YMUX_PROTO_REFUSE_UNKNOWN_PANE);
        return;
    }

    struct yetty_ycore_uint32_result attach_res = yetty_ymux_session_attach(
        entry->session, pane_id, view_rows, view_cols, token[0] ? token : NULL);
    if (YETTY_IS_ERR(attach_res)) {
        yetty_ycore_error_destroy(attach_res.error);
        daemon_enqueue_refuse(connection, YMUX_PROTO_REFUSE_UNKNOWN_PANE);
        return;
    }
    connection->session = entry->session;
    connection->attachment_id = attach_res.value;
    connection->pane_id = pane_id;
    connection->capabilities = capabilities;
    connection->sent_generation = 0;
    connection->acked_generation = 0;
    connection->overlay_applied_seq = 0;            /* fresh connection: apply events fresh */
    entry->created_stamp = ++daemon->session_stamp; /* recency: last attached */
    daemon_sync_pty_size(daemon, entry->session, pane_id);

    /* Tell this attachment's VT projector the client's terminfo capability
     * profile so its #699 redraw emits colours the client can render (truecolor
     * passthrough vs an RGB->256 downgrade). Per-attachment: each client may
     * differ. */
    struct yetty_yclass_object_ptr_result projector_res =
        yetty_ymux_session_projector(entry->session, attach_res.value);
    if (YETTY_IS_OK(projector_res)) {
        struct yetty_ycore_void_result caps_res =
            yetty_ymux_projector_set_capabilities(projector_res.value, capabilities);
        if (YETTY_IS_ERR(caps_res)) {
            yetty_ycore_error_destroy(caps_res.error);
        }
        if (client_term[0]) {
            /* The named terminal REFINES the profile through the state
             * model (tty-features resolution) — the mask keeps only the
             * mode bits (VT_TEXT / ATTACH_PREAMBLE). */
            struct yetty_ycore_void_result term_res = yetty_ymux_projector_set_terminal(
                projector_res.value, client_term, client_features);
            if (YETTY_IS_ERR(term_res)) {
                yetty_ycore_error_destroy(term_res.error);
            }
        }
    } else {
        yetty_ycore_error_destroy(projector_res.error);
    }

    uint32_t permissions = 0;
    struct yetty_ycore_uint32_result permissions_res =
        yetty_ymux_session_permissions(entry->session, attach_res.value);
    if (YETTY_IS_OK(permissions_res)) {
        permissions = permissions_res.value;
    } else {
        yetty_ycore_error_destroy(permissions_res.error);
    }
    uint32_t canonical_rows = 0, canonical_cols = 0;
    struct yetty_yclass_object_ptr_result pane_obj_res =
        yetty_ymux_session_pane(entry->session, pane_id);
    if (YETTY_IS_OK(pane_obj_res)) {
        struct yetty_yclass_object_ptr_result engine_res =
            yetty_ymux_pane_engine(pane_obj_res.value);
        if (YETTY_IS_OK(engine_res)) {
            struct yetty_ycore_void_result dims_res =
                yetty_ymux_engine_dims(engine_res.value, &canonical_rows, &canonical_cols);
            if (YETTY_IS_ERR(dims_res)) {
                yetty_ycore_error_destroy(dims_res.error);
            }
            /* Rich figures (ycat) are rendered at a fixed 16px producer cell but
             * the client draws each history row at its own cell height. Tell the
             * engine the real client cell height so row reservation reserves the
             * figure's exact pixel span (no dead gap before the prompt). */
            if (cell_pixel_height > 0) {
                struct yetty_ycore_void_result cell_res =
                    yetty_ymux_engine_set_cell_height(engine_res.value, cell_pixel_height);
                if (YETTY_IS_ERR(cell_res)) {
                    yetty_ycore_error_destroy(cell_res.error);
                }
            }
        } else {
            yetty_ycore_error_destroy(engine_res.error);
        }
    } else {
        yetty_ycore_error_destroy(pane_obj_res.error);
    }
    uint32_t welcome[5] = {attach_res.value, pane_id, permissions, canonical_rows, canonical_cols};
    struct yetty_ycore_void_result enqueue_res =
        daemon_enqueue(connection, YMUX_PROTO_WELCOME, welcome, sizeof(welcome));
    if (YETTY_IS_ERR(enqueue_res)) {
        yetty_ycore_error_destroy(enqueue_res.error);
    }
}

/* Reply to a session-management verb (status 0 = ok + text). */
static void daemon_session_reply(struct daemon_connection *connection, uint32_t status,
                                 const char *text)
{
    size_t text_len = text ? strlen(text) : 0;
    uint8_t payload[512];
    if (sizeof(uint32_t) + text_len > sizeof(payload)) {
        text_len = sizeof(payload) - sizeof(uint32_t);
    }
    memcpy(payload, &status, sizeof(uint32_t));
    if (text_len) {
        memcpy(payload + sizeof(uint32_t), text, text_len);
    }
    struct yetty_ycore_void_result enqueue_res =
        daemon_enqueue(connection, YMUX_PROTO_SESSION_REPLY, payload, sizeof(uint32_t) + text_len);
    if (YETTY_IS_ERR(enqueue_res)) {
        yetty_ycore_error_destroy(enqueue_res.error);
    }
}

/* new-session: create a NAMED session with its initial shell pane (tmux
 * creates the first window immediately). Duplicate names error like
 * tmux ("duplicate session: <name>"). Empty name auto-numbers. */
static void daemon_handle_session_new(struct yetty_ymux_daemon *daemon,
                                      struct daemon_connection *connection, const uint8_t *payload,
                                      size_t payload_len)
{
    if (payload_len < 3 * sizeof(uint32_t)) {
        daemon_enqueue_refuse(connection, YMUX_PROTO_REFUSE_BAD_FRAME);
        return;
    }
    uint32_t words[3];
    memcpy(words, payload, sizeof(words));
    uint32_t rows = words[0] ? words[0] : daemon->default_rows;
    uint32_t cols = words[1] ? words[1] : daemon->default_cols;
    uint32_t name_len = words[2];
    if (name_len > YMUX_DAEMON_SESSION_NAME_MAX || 3 * sizeof(uint32_t) + name_len > payload_len) {
        daemon_enqueue_refuse(connection, YMUX_PROTO_REFUSE_BAD_FRAME);
        return;
    }
    char name[YMUX_DAEMON_SESSION_NAME_MAX + 1] = {0};
    memcpy(name, payload + 3 * sizeof(uint32_t), name_len);
    if (!name[0]) {
        snprintf(name, sizeof(name), "%u", daemon->next_auto_name);
    }
    if (daemon_session_find(daemon, name)) {
        char message[128];
        snprintf(message, sizeof(message), "duplicate session: %s", name);
        daemon_session_reply(connection, 1, message);
        return;
    }
    struct daemon_session_entry *entry = NULL;
    for (uint32_t index = 0; index < YMUX_DAEMON_MAX_SESSIONS; ++index) {
        if (!daemon->sessions[index].session) {
            entry = &daemon->sessions[index];
            break;
        }
    }
    struct daemon_pane_pty *slot = NULL;
    for (uint32_t index = 0; index < YMUX_DAEMON_MAX_PTYS; ++index) {
        if (!daemon->ptys[index].pty) {
            slot = &daemon->ptys[index];
            break;
        }
    }
    if (!entry || !slot || !daemon->host.spawn) {
        daemon_session_reply(connection, 1, "create session failed: server full");
        return;
    }
    struct yetty_yclass_object_ptr_result session_res = yetty_ymux_session_make();
    if (YETTY_IS_ERR(session_res)) {
        yetty_ycore_error_destroy(session_res.error);
        daemon_session_reply(connection, 1, "create session failed");
        return;
    }
    struct yetty_yplatform_pty_ptr_result pty_res =
        daemon->host.spawn(rows, cols, daemon->host.userdata);
    if (YETTY_IS_ERR(pty_res)) {
        yetty_ycore_error_destroy(pty_res.error);
        struct yetty_ycore_void_result dispose_res = yetty_ymux_session_dispose(session_res.value);
        if (YETTY_IS_ERR(dispose_res)) {
            yetty_ycore_error_destroy(dispose_res.error);
        }
        daemon_session_reply(connection, 1, "create session failed: pty spawn");
        return;
    }
    struct yetty_ymux_engine_host engine_host = {
        .output = daemon_pane_output,
        .clipboard = daemon_pane_clipboard,
        .bell = daemon_pane_bell,
        .title = daemon_pane_title,
        .rich = daemon_pane_rich, /* tunnel ygui/ygreeter figure-surface RPC to the client */
        .userdata = slot,
    };
    struct yetty_ycore_uint32_result pane_res = yetty_ymux_session_pane_create(
        session_res.value, rows, cols, /*hot_rows=*/1024, /*total_row_cap=*/0, &engine_host);
    if (YETTY_IS_ERR(pane_res)) {
        yetty_ycore_error_destroy(pane_res.error);
        struct yetty_ycore_void_result destroy_res = pty_res.value->ops->destroy(pty_res.value);
        if (YETTY_IS_ERR(destroy_res)) {
            yetty_ycore_error_destroy(destroy_res.error);
        }
        struct yetty_ycore_void_result dispose_res = yetty_ymux_session_dispose(session_res.value);
        if (YETTY_IS_ERR(dispose_res)) {
            yetty_ycore_error_destroy(dispose_res.error);
        }
        daemon_session_reply(connection, 1, "create session failed: pane");
        return;
    }
    slot->pty = pty_res.value;
    slot->daemon = daemon;
    slot->session = session_res.value;
    slot->pane_id = pane_res.value;
    slot->pty_rows = 0; /* force the first size sync */
    slot->pty_cols = 0;
    daemon_pane_rpc_setup(slot);
    entry->session = session_res.value;
    snprintf(entry->name, sizeof(entry->name), "%s", name);
    entry->created_stamp = ++daemon->session_stamp;
    if (strspn(name, "0123456789") == strlen(name)) {
        uint32_t numeric = (uint32_t)strtoul(name, NULL, 10);
        if (numeric >= daemon->next_auto_name) {
            daemon->next_auto_name = numeric + 1;
        }
    }
    daemon_sync_pty_size(daemon, entry->session, pane_res.value);
    daemon_session_reply(connection, 0, name);
}

/* Destroy one session: panes+PTYs die, its clients detach (tmux
 * kill-session). */
static void daemon_session_destroy_entry(struct yetty_ymux_daemon *daemon,
                                         struct daemon_session_entry *entry)
{
    for (uint32_t index = 0; index < YMUX_DAEMON_MAX_PTYS; ++index) {
        struct daemon_pane_pty *slot = &daemon->ptys[index];
        if (slot->pty && slot->session == entry->session) {
            /* Reverse-close the controller's relays BEFORE the channel map is
             * destroyed — kill-session/session-destroy reaches this path, not
             * daemon_close_pane, so without this the controller's upstream Yetty
             * channels leak. Runs while the controller connection identity is
             * still intact (cleared in the connection loop below). */
            if (slot->rpc_channel_count > 0) {
                daemon_pane_close_all_rpc_channels(slot, slot->rpc_controller);
                slot->rpc_controller = 0;
            }
            daemon_pane_rpc_teardown(slot);
            struct yetty_ycore_void_result destroy_res = slot->pty->ops->destroy(slot->pty);
            if (YETTY_IS_ERR(destroy_res)) {
                yetty_ycore_error_destroy(destroy_res.error);
            }
            free(slot->out_queue);
            memset(slot, 0, sizeof(*slot));
        }
    }
    for (uint32_t index = 0; index < YMUX_DAEMON_MAX_CONNECTIONS; ++index) {
        struct daemon_connection *connection = &daemon->connections[index];
        if (connection->socket && connection->session == entry->session) {
            uint32_t pane_id = connection->pane_id;
            struct yetty_ycore_void_result enqueue_res =
                daemon_enqueue(connection, YMUX_PROTO_PANE_EXIT, &pane_id, sizeof(pane_id));
            if (YETTY_IS_ERR(enqueue_res)) {
                yetty_ycore_error_destroy(enqueue_res.error);
            }
            connection->attachment_id = 0;
            connection->pane_id = 0;
            connection->session = NULL;
        }
    }
    struct yetty_ycore_void_result dispose_res = yetty_ymux_session_dispose(entry->session);
    if (YETTY_IS_ERR(dispose_res)) {
        yetty_ycore_error_destroy(dispose_res.error);
    }
    memset(entry, 0, sizeof(*entry));

    /* tmux: the server exits when its last session dies. */
    if (!daemon_session_most_recent(daemon)) {
        daemon->shutdown_requested = 1;
    }
}

static void daemon_handle_session_verb(struct yetty_ymux_daemon *daemon,
                                       struct daemon_connection *connection, uint32_t type,
                                       const uint8_t *payload, size_t payload_len)
{
    switch (type) {
    case YMUX_PROTO_SESSION_LIST: {
        /* tmux `ls` shape: "<name>: 1 windows (created ...)" — v1 keeps
         * the name + attachment count, no timestamps (no wall clock in
         * the daemon model). */
        char text[512];
        size_t used = 0;
        int any = 0;
        for (uint32_t index = 0; index < YMUX_DAEMON_MAX_SESSIONS; ++index) {
            struct daemon_session_entry *entry = &daemon->sessions[index];
            if (!entry->session) {
                continue;
            }
            uint32_t attached = 0;
            for (uint32_t conn = 0; conn < YMUX_DAEMON_MAX_CONNECTIONS; ++conn) {
                if (daemon->connections[conn].socket &&
                    daemon->connections[conn].session == entry->session &&
                    daemon->connections[conn].attachment_id != 0) {
                    ++attached;
                }
            }
            int wrote = snprintf(text + used, sizeof(text) - used, "%s: 1 windows%s\n", entry->name,
                                 attached ? " (attached)" : "");
            if (wrote < 0 || (size_t)wrote >= sizeof(text) - used) {
                break;
            }
            used += (size_t)wrote;
            any = 1;
        }
        if (!any) {
            daemon_session_reply(connection, 1, "no server running on this socket");
            break;
        }
        daemon_session_reply(connection, 0, text);
        break;
    }
    case YMUX_PROTO_SESSION_HAS: {
        char name[YMUX_DAEMON_SESSION_NAME_MAX + 1] = {0};
        size_t name_len =
            payload_len < YMUX_DAEMON_SESSION_NAME_MAX ? payload_len : YMUX_DAEMON_SESSION_NAME_MAX;
        memcpy(name, payload, name_len);
        struct daemon_session_entry *entry = daemon_session_target(daemon, name);
        if (entry) {
            daemon_session_reply(connection, 0, "");
        } else {
            char message[128];
            snprintf(message, sizeof(message), "can't find session: %s", name);
            daemon_session_reply(connection, 1, message);
        }
        break;
    }
    case YMUX_PROTO_SESSION_KILL: {
        char name[YMUX_DAEMON_SESSION_NAME_MAX + 1] = {0};
        size_t name_len =
            payload_len < YMUX_DAEMON_SESSION_NAME_MAX ? payload_len : YMUX_DAEMON_SESSION_NAME_MAX;
        memcpy(name, payload, name_len);
        struct daemon_session_entry *entry = daemon_session_target(daemon, name);
        if (!entry) {
            char message[128];
            snprintf(message, sizeof(message), "can't find session: %s", name);
            daemon_session_reply(connection, 1, message);
            break;
        }
        daemon_session_destroy_entry(daemon, entry);
        daemon_session_reply(connection, 0, "");
        break;
    }
    case YMUX_PROTO_SESSION_RENAME: {
        if (payload_len < sizeof(uint32_t)) {
            daemon_enqueue_refuse(connection, YMUX_PROTO_REFUSE_BAD_FRAME);
            break;
        }
        uint32_t old_len;
        memcpy(&old_len, payload, sizeof(uint32_t));
        if (old_len > YMUX_DAEMON_SESSION_NAME_MAX || sizeof(uint32_t) + old_len > payload_len) {
            daemon_enqueue_refuse(connection, YMUX_PROTO_REFUSE_BAD_FRAME);
            break;
        }
        char old_name[YMUX_DAEMON_SESSION_NAME_MAX + 1] = {0};
        memcpy(old_name, payload + sizeof(uint32_t), old_len);
        size_t new_len = payload_len - sizeof(uint32_t) - old_len;
        if (new_len > YMUX_DAEMON_SESSION_NAME_MAX) {
            new_len = YMUX_DAEMON_SESSION_NAME_MAX;
        }
        char new_name[YMUX_DAEMON_SESSION_NAME_MAX + 1] = {0};
        memcpy(new_name, payload + sizeof(uint32_t) + old_len, new_len);
        if (!new_name[0]) {
            daemon_session_reply(connection, 1, "empty session name");
            break;
        }
        struct daemon_session_entry *entry = daemon_session_target(daemon, old_name);
        if (!entry) {
            char message[128];
            snprintf(message, sizeof(message), "can't find session: %s", old_name);
            daemon_session_reply(connection, 1, message);
            break;
        }
        if (daemon_session_find(daemon, new_name)) {
            char message[128];
            snprintf(message, sizeof(message), "duplicate session: %s", new_name);
            daemon_session_reply(connection, 1, message);
            break;
        }
        snprintf(entry->name, sizeof(entry->name), "%s", new_name);
        daemon_session_reply(connection, 0, "");
        break;
    }
    case YMUX_PROTO_SESSION_DETACH: {
        /* tmux detach-client -s: every client of the session detaches
         * (their bridges see PANE_EXIT and return to the shell). */
        char name[YMUX_DAEMON_SESSION_NAME_MAX + 1] = {0};
        size_t name_len =
            payload_len < YMUX_DAEMON_SESSION_NAME_MAX ? payload_len : YMUX_DAEMON_SESSION_NAME_MAX;
        memcpy(name, payload, name_len);
        struct daemon_session_entry *entry = daemon_session_target(daemon, name);
        if (!entry) {
            char message[128];
            snprintf(message, sizeof(message), "can't find session: %s", name);
            daemon_session_reply(connection, 1, message);
            break;
        }
        for (uint32_t conn = 0; conn < YMUX_DAEMON_MAX_CONNECTIONS; ++conn) {
            struct daemon_connection *victim = &daemon->connections[conn];
            if (!victim->socket || victim->session != entry->session ||
                victim->attachment_id == 0) {
                continue;
            }
            daemon_release_connection_relays(daemon, victim);
            struct yetty_ycore_void_result detach_res =
                yetty_ymux_session_detach(entry->session, victim->attachment_id);
            if (YETTY_IS_ERR(detach_res)) {
                yetty_ycore_error_destroy(detach_res.error);
            }
            uint32_t pane_id = victim->pane_id;
            struct yetty_ycore_void_result enqueue_res =
                daemon_enqueue(victim, YMUX_PROTO_PANE_EXIT, &pane_id, sizeof(pane_id));
            if (YETTY_IS_ERR(enqueue_res)) {
                yetty_ycore_error_destroy(enqueue_res.error);
            }
            victim->attachment_id = 0;
            victim->pane_id = 0;
            victim->session = NULL;
        }
        daemon_session_reply(connection, 0, "");
        break;
    }
    case YMUX_PROTO_SESSION_SEND_KEYS: {
        /* tmux send-keys: feed the session's active pane engine directly
         * — no attachment, no permission gate (the command line already
         * reached the server's socket). */
        if (payload_len < sizeof(uint32_t)) {
            daemon_enqueue_refuse(connection, YMUX_PROTO_REFUSE_BAD_FRAME);
            break;
        }
        uint32_t name_len;
        memcpy(&name_len, payload, sizeof(uint32_t));
        if (name_len > YMUX_DAEMON_SESSION_NAME_MAX || sizeof(uint32_t) + name_len > payload_len) {
            daemon_enqueue_refuse(connection, YMUX_PROTO_REFUSE_BAD_FRAME);
            break;
        }
        char name[YMUX_DAEMON_SESSION_NAME_MAX + 1] = {0};
        memcpy(name, payload + sizeof(uint32_t), name_len);
        struct daemon_session_entry *entry = daemon_session_target(daemon, name);
        if (!entry) {
            char message[128];
            snprintf(message, sizeof(message), "can't find session: %s", name);
            daemon_session_reply(connection, 1, message);
            break;
        }
        struct yetty_ycore_uint32_result pane_res = yetty_ymux_session_active_pane(entry->session);
        if (YETTY_IS_ERR(pane_res) || pane_res.value == 0) {
            if (YETTY_IS_ERR(pane_res)) {
                yetty_ycore_error_destroy(pane_res.error);
            }
            daemon_session_reply(connection, 1, "session has no panes");
            break;
        }
        struct yetty_yclass_object_ptr_result pane_obj_res =
            yetty_ymux_session_pane(entry->session, pane_res.value);
        if (YETTY_IS_ERR(pane_obj_res)) {
            yetty_ycore_error_destroy(pane_obj_res.error);
            daemon_session_reply(connection, 1, "session has no panes");
            break;
        }
        struct yetty_yclass_object_ptr_result engine_res =
            yetty_ymux_pane_engine(pane_obj_res.value);
        if (YETTY_IS_ERR(engine_res)) {
            yetty_ycore_error_destroy(engine_res.error);
            daemon_session_reply(connection, 1, "session has no panes");
            break;
        }
        size_t pair_bytes = payload_len - sizeof(uint32_t) - name_len;
        size_t pair_count = pair_bytes / (2 * sizeof(uint32_t));
        const uint8_t *pairs = payload + sizeof(uint32_t) + name_len;
        for (size_t pair = 0; pair < pair_count; ++pair) {
            uint32_t kind, value;
            memcpy(&kind, pairs + pair * 2 * sizeof(uint32_t), sizeof(uint32_t));
            memcpy(&value, pairs + pair * 2 * sizeof(uint32_t) + sizeof(uint32_t),
                   sizeof(uint32_t));
            struct yetty_ycore_void_result input_res =
                kind == 0 ? yetty_ymux_engine_input_char(engine_res.value, value, 0)
                          : yetty_ymux_engine_input_key(engine_res.value, (int)value, 0);
            if (YETTY_IS_ERR(input_res)) {
                yetty_ycore_error_destroy(input_res.error);
            }
        }
        daemon_session_reply(connection, 0, "");
        break;
    }
    default:
        daemon_enqueue_refuse(connection, YMUX_PROTO_REFUSE_BAD_FRAME);
        break;
    }
}

/* Authorization for the figure-surface proxy back-channels (RPC_RELAY responses
 * + FIGURE_INPUT). The daemon only ever forwards a pane app's RPC requests to
 * the CONTROLLER (daemon_pane_rpc_forward, controller_only=1), so responses and
 * figure input must be accepted ONLY from that same controller, and only when it
 * holds INPUT permission — otherwise a read-only viewer could inject bytes into
 * the pane application's live RPC channel or PTY. */
static int daemon_relay_authorized(struct daemon_connection *connection)
{
    if (!connection->session || connection->attachment_id == 0) {
        return 0;
    }
    struct yetty_ycore_uint32_result controller_res =
        yetty_ymux_session_controller(connection->session);
    if (YETTY_IS_ERR(controller_res)) {
        yetty_ycore_error_destroy(controller_res.error);
        return 0;
    }
    if (connection->attachment_id != controller_res.value) {
        return 0;
    }
    struct yetty_ycore_uint32_result perm_res =
        yetty_ymux_session_permissions(connection->session, connection->attachment_id);
    if (YETTY_IS_ERR(perm_res)) {
        yetty_ycore_error_destroy(perm_res.error);
        return 0;
    }
    return (perm_res.value & YETTY_YMUX_PERMISSION_INPUT) != 0;
}

static int daemon_recover_slow_client(struct daemon_connection *connection,
                                      struct yetty_yclass_object *projector);

/* Move this connection's attachment view by `delta` rows (negative = into
 * scrollback), clamped to the pane timeline; at/past live top -> follow. */
static void daemon_connection_scroll(struct daemon_connection *connection, int32_t delta)
{
    struct yetty_yclass_object_ptr_result attachment_res =
        yetty_ymux_session_attachment(connection->session, connection->attachment_id);
    struct yetty_yclass_object_ptr_result pane_res =
        yetty_ymux_session_pane(connection->session, connection->pane_id);
    if (YETTY_IS_ERR(attachment_res) || YETTY_IS_ERR(pane_res)) {
        if (YETTY_IS_ERR(attachment_res)) {
            yetty_ycore_error_destroy(attachment_res.error);
        }
        if (YETTY_IS_ERR(pane_res)) {
            yetty_ycore_error_destroy(pane_res.error);
        }
        return;
    }
    uint64_t floor_idx = 0, live_top = 0;
    struct yetty_ycore_void_result timeline_res =
        yetty_ymux_pane_timeline(pane_res.value, &floor_idx, &live_top);
    if (YETTY_IS_ERR(timeline_res)) {
        yetty_ycore_error_destroy(timeline_res.error);
        return;
    }
    struct yetty_ycore_uint64_result view_top_res =
        yetty_ymux_attachment_view_top(attachment_res.value);
    if (YETTY_IS_ERR(view_top_res)) {
        yetty_ycore_error_destroy(view_top_res.error);
        return;
    }
    int64_t target = (int64_t)view_top_res.value + delta;
    if (target < (int64_t)floor_idx) {
        target = (int64_t)floor_idx;
    }
    struct yetty_ycore_void_result move_res;
    if (target >= (int64_t)live_top) {
        move_res = yetty_ymux_attachment_follow(attachment_res.value);
    } else {
        move_res = yetty_ymux_attachment_anchor(attachment_res.value, (uint64_t)target);
    }
    if (YETTY_IS_ERR(move_res)) {
        yetty_ycore_error_destroy(move_res.error);
    }
}

/* Copy the selected span (anchor..cursor, row-major, LIVE screen rows) into
 * the daemon paste buffer. */
/* Append into the growable paste buffer (hard cap; past it the truncation
 * FLAG is set — never a silent stop mid-selection). */
static void daemon_paste_append(struct yetty_ymux_daemon *daemon, const char *bytes, uint32_t len)
{
    enum { YMUX_DAEMON_PASTE_CAP = 16u << 20 };
    if (len == 0 || daemon->paste_truncated) {
        return;
    }
    size_t needed = (size_t)daemon->paste_buffer_len + len;
    if (needed > YMUX_DAEMON_PASTE_CAP) {
        daemon->paste_truncated = 1;
        ydebug("ymux copy: paste buffer cap reached — selection truncated");
        return;
    }
    if (needed > daemon->paste_buffer_cap) {
        size_t new_cap = daemon->paste_buffer_cap ? daemon->paste_buffer_cap : 8192;
        while (new_cap < needed) {
            new_cap *= 2;
        }
        char *grown = realloc(daemon->paste_buffer, new_cap);
        if (!grown) {
            daemon->paste_truncated = 1;
            return;
        }
        daemon->paste_buffer = grown;
        daemon->paste_buffer_cap = (uint32_t)new_cap;
    }
    memcpy(daemon->paste_buffer + daemon->paste_buffer_len, bytes, len);
    daemon->paste_buffer_len += len;
}

static uint32_t daemon_copy_encode_utf8(uint32_t codepoint, char encoded[4])
{
    uint32_t encoded_len = 0;
    if (codepoint < 0x80) {
        encoded[encoded_len++] = (char)codepoint;
    } else if (codepoint < 0x800) {
        encoded[encoded_len++] = (char)(0xC0 | (codepoint >> 6));
        encoded[encoded_len++] = (char)(0x80 | (codepoint & 0x3F));
    } else if (codepoint < 0x10000) {
        encoded[encoded_len++] = (char)(0xE0 | (codepoint >> 12));
        encoded[encoded_len++] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        encoded[encoded_len++] = (char)(0x80 | (codepoint & 0x3F));
    } else {
        encoded[encoded_len++] = (char)(0xF0 | (codepoint >> 18));
        encoded[encoded_len++] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
        encoded[encoded_len++] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        encoded[encoded_len++] = (char)(0x80 | (codepoint & 0x3F));
    }
    return encoded_len;
}

/* Copy the selection FROM THE DISPLAYED TIMELINE VIEW (review #19): rows
 * resolve through the attachment's view_top and the pane timeline — an
 * anchored-in-scrollback view copies the HISTORY rows on screen, never the
 * same-numbered live rows. Cluster-correct (combining marks appended, wide
 * continuations skipped), tmux-normalized (trailing blanks trimmed; wrapped
 * lines join without a newline). */
static void daemon_copy_selection(struct yetty_ymux_daemon *daemon,
                                  struct daemon_connection *connection)
{
    struct yetty_yclass_object_ptr_result pane_res =
        yetty_ymux_session_pane(connection->session, connection->pane_id);
    if (YETTY_IS_ERR(pane_res)) {
        yetty_ycore_error_destroy(pane_res.error);
        return;
    }
    uint64_t view_top = 0;
    {
        struct yetty_yclass_object_ptr_result attachment_res =
            yetty_ymux_session_attachment(connection->session, connection->attachment_id);
        if (YETTY_IS_OK(attachment_res)) {
            struct yetty_ycore_uint64_result view_top_res =
                yetty_ymux_attachment_view_top(attachment_res.value);
            if (YETTY_IS_OK(view_top_res)) {
                view_top = view_top_res.value;
            } else {
                yetty_ycore_error_destroy(view_top_res.error);
            }
        } else {
            yetty_ycore_error_destroy(attachment_res.error);
        }
    }
    int start_row = connection->copy_anchor_row;
    int start_col = connection->copy_anchor_col;
    int end_row = connection->copy_cursor_row;
    int end_col = connection->copy_cursor_col;
    if (end_row < start_row || (end_row == start_row && end_col < start_col)) {
        int swap_row = start_row;
        int swap_col = start_col;
        start_row = end_row;
        start_col = end_col;
        end_row = swap_row;
        end_col = swap_col;
    }
    daemon->paste_buffer_len = 0;
    daemon->paste_truncated = 0;
    for (int row = start_row; row <= end_row; ++row) {
        struct yetty_ymux_history_row_result row_res =
            yetty_ymux_pane_resolve_row(pane_res.value, view_top + (uint64_t)row);
        if (YETTY_IS_ERR(row_res)) {
            yetty_ycore_error_destroy(row_res.error);
            break; /* past the live bottom */
        }
        uint32_t cols = row_res.value.cols;
        const struct yetty_ymux_cell *cells = row_res.value.cells;
        uint32_t from_col = row == start_row ? (uint32_t)start_col : 0;
        uint32_t to_col = row == end_row ? (uint32_t)end_col : (cols ? cols - 1 : 0);
        uint32_t row_start_len = daemon->paste_buffer_len;
        uint32_t last_nonblank_len = daemon->paste_buffer_len;
        for (uint32_t col = from_col; col <= to_col && col < cols; ++col) {
            const struct yetty_ymux_cell *cell = &cells[col];
            if (cell->width == 0) {
                continue; /* wide-cell continuation: the lead cell carried it */
            }
            uint32_t codepoint = cell->codepoint ? cell->codepoint : (uint32_t)' ';
            char encoded[4];
            uint32_t encoded_len = daemon_copy_encode_utf8(codepoint, encoded);
            daemon_paste_append(daemon, encoded, encoded_len);
            for (uint32_t mark = 0;
                 mark < cell->mark_count && mark < (uint32_t)YETTY_YMUX_CELL_MAX_MARKS; ++mark) {
                if (cell->marks[mark] == 0) {
                    continue;
                }
                encoded_len = daemon_copy_encode_utf8(cell->marks[mark], encoded);
                daemon_paste_append(daemon, encoded, encoded_len);
            }
            if (codepoint != ' ' || cell->mark_count > 0) {
                last_nonblank_len = daemon->paste_buffer_len;
            }
        }
        /* tmux normalization: trailing blanks never enter the buffer. */
        if (last_nonblank_len >= row_start_len) {
            daemon->paste_buffer_len = last_nonblank_len;
        }
        if (row < end_row) {
            /* Wrapped lines JOIN: a next row that CONTINUES this logical
             * line gets no newline (tmux's line-join rule). */
            int next_continues = 0;
            struct yetty_ymux_history_row_result next_res =
                yetty_ymux_pane_resolve_row(pane_res.value, view_top + (uint64_t)row + 1);
            if (YETTY_IS_OK(next_res)) {
                next_continues = next_res.value.continuation ? 1 : 0;
            } else {
                yetty_ycore_error_destroy(next_res.error);
            }
            if (!next_continues) {
                daemon_paste_append(daemon, "\n", 1);
            }
        }
    }
}

/* One decoded copy-mode ARROW (CSI/SS3 final byte A-D; modifiers move the
 * same as plain arrows). */
static void daemon_copy_mode_arrow(struct daemon_connection *connection, uint8_t final_byte,
                                   uint32_t view_rows, uint32_t view_cols)
{
    if (final_byte == 'A') {
        if (connection->copy_cursor_row > 0) {
            --connection->copy_cursor_row;
        } else {
            daemon_connection_scroll(connection, -1);
        }
    } else if (final_byte == 'B') {
        if ((uint32_t)connection->copy_cursor_row + 1 < view_rows) {
            ++connection->copy_cursor_row;
        } else {
            daemon_connection_scroll(connection, 1);
        }
    } else if (final_byte == 'C') {
        if ((uint32_t)connection->copy_cursor_col + 1 < view_cols) {
            ++connection->copy_cursor_col;
        }
    } else if (final_byte == 'D' && connection->copy_cursor_col > 0) {
        --connection->copy_cursor_col;
    }
}

/* One plain (non-sequence) copy-mode key. */
static void daemon_copy_mode_plain_key(struct yetty_ymux_daemon *daemon,
                                       struct daemon_connection *connection, uint8_t byte)
{
    if (byte == ' ') {
        connection->copy_anchor_row = connection->copy_cursor_row;
        connection->copy_anchor_col = connection->copy_cursor_col;
        connection->copy_selecting = 1;
    } else if (byte == '\r' || byte == '\n') {
        if (connection->copy_selecting) {
            daemon_copy_selection(daemon, connection);
            connection->copy_selecting = 0;
        }
    } else if (byte == 'q') {
        connection->copy_selecting = 0;
        connection->copy_cursor_row = 0;
        connection->copy_cursor_col = 0;
        struct yetty_ycore_void_result release_res =
            daemon_enqueue(connection, YMUX_PROTO_CHROME_RELEASE, NULL, 0);
        if (YETTY_IS_ERR(release_res)) {
            yetty_ycore_error_destroy(release_res.error);
        }
    }
}

/* STREAMING copy-mode key decoder (review #19): bytes accumulate across
 * overlay frames, so an escape sequence split anywhere still decodes. CSI
 * with parameters (\e[1;5A modified arrows) and SS3 application-cursor
 * (\eOA) both resolve to arrows; unrecognized sequences are swallowed
 * whole, never half-consumed. */
static void daemon_copy_mode_key_byte(struct yetty_ymux_daemon *daemon,
                                      struct daemon_connection *connection, uint8_t byte,
                                      uint32_t view_rows, uint32_t view_cols)
{
    if (connection->copy_key_pending_len > 0) {
        if (connection->copy_key_pending_len < sizeof(connection->copy_key_pending)) {
            connection->copy_key_pending[connection->copy_key_pending_len++] = byte;
        } else {
            connection->copy_key_pending_len = 0; /* runaway — resync */
            return;
        }
        if (connection->copy_key_pending_len == 2) {
            if (byte == '[' || byte == 'O') {
                return; /* CSI / SS3 introducer — await the final */
            }
            /* ESC + literal: not a sequence — drop the ESC, replay the
             * byte as plain. */
            connection->copy_key_pending_len = 0;
            daemon_copy_mode_plain_key(daemon, connection, byte);
            return;
        }
        /* Inside CSI: parameter bytes continue the sequence. SS3 takes its
         * final immediately (this branch: length >= 3). */
        if (connection->copy_key_pending[1] == '[' &&
            ((byte >= '0' && byte <= '9') || byte == ';')) {
            return; /* parameter byte — keep accumulating */
        }
        uint8_t final_byte = byte;
        connection->copy_key_pending_len = 0;
        if (final_byte >= 'A' && final_byte <= 'D') {
            daemon_copy_mode_arrow(connection, final_byte, view_rows, view_cols);
        }
        return; /* other finals: swallowed whole */
    }
    if (byte == 0x1b) {
        connection->copy_key_pending[0] = 0x1b;
        connection->copy_key_pending_len = 1;
        return;
    }
    daemon_copy_mode_plain_key(daemon, connection, byte);
}

/* The daemon-role CHROME CONSUMER (review #16/#17) — COPY-MODE: a real
 * interactive mode. Arrows move the copy cursor (scrolling the view when
 * the cursor pushes past the top edge), SPACE anchors a selection, ENTER
 * copies the span to the daemon paste buffer, q exits and releases the
 * client's chrome focus (CHROME_RELEASE). Every event is drained COMPLETE
 * and freed. */
static void daemon_chrome_consume(struct yetty_ymux_daemon *daemon,
                                  struct daemon_connection *connection)
{
    /* The copy cursor moves within the VIEW — bound by the pane's actual
     * dims (an attachment's view can be smaller than the server default). */
    uint32_t view_rows = daemon->default_rows;
    uint32_t view_cols = daemon->default_cols;
    if (connection->session) {
        struct yetty_yclass_object_ptr_result pane_res =
            yetty_ymux_session_pane(connection->session, connection->pane_id);
        if (YETTY_IS_OK(pane_res)) {
            struct yetty_yclass_object_ptr_result engine_res =
                yetty_ymux_pane_engine(pane_res.value);
            if (YETTY_IS_OK(engine_res)) {
                struct yetty_ycore_void_result dims_res =
                    yetty_ymux_engine_dims(engine_res.value, &view_rows, &view_cols);
                if (YETTY_IS_ERR(dims_res)) {
                    yetty_ycore_error_destroy(dims_res.error);
                }
            } else {
                yetty_ycore_error_destroy(engine_res.error);
            }
        } else {
            yetty_ycore_error_destroy(pane_res.error);
        }
    }
    while (connection->chrome_queue_count > 0) {
        uint32_t slot = connection->chrome_queue_head;
        uint32_t input_class = connection->chrome_queue[slot].input_class;
        const uint8_t *bytes = connection->chrome_queue[slot].bytes;
        uint32_t byte_len = connection->chrome_queue[slot].byte_len;
        if (input_class == YMUX_INPUT_CLASS_KEY) {
            for (uint32_t offset = 0; offset < byte_len; ++offset) {
                daemon_copy_mode_key_byte(daemon, connection, bytes[offset], view_rows, view_cols);
            }
        }
        /* Retain the consumed payload for identity observation. */
        free(connection->chrome_last_bytes);
        connection->chrome_last_bytes = connection->chrome_queue[slot].bytes;
        connection->chrome_last_len = byte_len;
        connection->chrome_last_class = input_class;
        connection->chrome_queue[slot].bytes = NULL;
        connection->chrome_queue_head = (slot + 1) % 8;
        --connection->chrome_queue_count;
    }
}

/* Sequence-bearing overlay refusal (review #19): unlike the generic REFUSE
 * frame this names the refused event, so the sender resends the SAME
 * sequence immediately instead of waiting out its resend window. */
static void daemon_overlay_nack(struct daemon_connection *connection, uint32_t sequence,
                                uint32_t reason)
{
    uint32_t nack[2] = {sequence, reason};
    struct yetty_ycore_void_result nack_res = daemon_enqueue(
        connection, YMUX_PROTO_OVERLAY_INPUT_NACK, (const uint8_t *)nack, sizeof(nack));
    if (YETTY_IS_ERR(nack_res)) {
        yetty_ycore_error_destroy(nack_res.error);
    }
}

static void daemon_handle_frame(struct yetty_ymux_daemon *daemon,
                                struct daemon_connection *connection, uint32_t type,
                                const uint8_t *payload, size_t payload_len)
{
    if (type == YMUX_PROTO_ATTACH) {
        daemon_handle_attach(daemon, connection, payload, payload_len);
        return;
    }
    if (type == YMUX_PROTO_SHUTDOWN) {
        daemon->shutdown_requested = 1;
        return;
    }
    if (type == YMUX_PROTO_OVERLAY_INPUT) {
        if (payload_len < 8) {
            daemon_enqueue_refuse(connection, YMUX_PROTO_REFUSE_BAD_FRAME);
            return;
        }
        uint32_t sequence = 0;
        memcpy(&sequence, payload, 4);
        /* IDEMPOTENT replay (review #19): the sender resends the same
         * sequence when the ACK was lost/refused. An already-applied
         * sequence re-ACKs WITHOUT re-applying — a duplicate arrow key
         * must not double-scroll. */
        if (sequence != 0 && sequence <= connection->overlay_applied_seq) {
            struct yetty_ycore_void_result dup_ack_res = daemon_enqueue(
                connection, YMUX_PROTO_OVERLAY_INPUT_ACK, (const uint8_t *)&sequence, 4);
            if (YETTY_IS_ERR(dup_ack_res)) {
                yetty_ycore_error_destroy(dup_ack_res.error);
            }
            return;
        }
        if (connection->chrome_queue_count == 8 || connection->refuse_next_overlay > 0) {
            if (connection->refuse_next_overlay > 0) {
                --connection->refuse_next_overlay; /* test seam */
            }
            daemon_overlay_nack(connection, sequence, YMUX_PROTO_REFUSE_FULL);
            return;
        }
        uint32_t event_len = (uint32_t)(payload_len - 8);
        uint8_t *event_bytes = NULL;
        if (event_len > 0) {
            event_bytes = malloc(event_len);
            if (!event_bytes) {
                daemon_overlay_nack(connection, sequence, YMUX_PROTO_REFUSE_FULL);
                return;
            }
            memcpy(event_bytes, payload + 8, event_len);
        }
        /* ACK FIRST (review #19): if the ACK cannot even be queued, the
         * event is NOT applied and NOT recorded — the sender's resend gets
         * a clean second attempt instead of a silently-consumed event
         * whose loss the sender can never observe. */
        struct yetty_ycore_void_result ack_res =
            daemon_enqueue(connection, YMUX_PROTO_OVERLAY_INPUT_ACK, (const uint8_t *)&sequence, 4);
        if (YETTY_IS_ERR(ack_res)) {
            yetty_ycore_error_destroy(ack_res.error);
            free(event_bytes);
            return;
        }
        uint32_t slot = (connection->chrome_queue_head + connection->chrome_queue_count) % 8;
        memcpy(&connection->chrome_queue[slot].input_class, payload + 4, 4);
        connection->chrome_queue[slot].byte_len = event_len;
        connection->chrome_queue[slot].bytes = event_bytes;
        ++connection->chrome_queue_count;
        ++connection->chrome_intake_count;
        connection->chrome_intake_class = connection->chrome_queue[slot].input_class;
        connection->overlay_applied_seq = sequence;
        daemon_chrome_consume(daemon, connection);
        return;
    }
    if (type == YMUX_PROTO_PASTE_BUFFER) {
        /* Paste the copy-mode buffer (tmux's most-recent-buffer model,
         * daemon-global) into the target pane through the ENGINE paste
         * path — bracketed-paste guards are mode-aware there. Target: the
         * requester's own attached pane when attached, else the first
         * live pane (control-client `ymux paste`). */
        if (daemon->paste_truncated) {
            /* A capped/OOM copy kept only a prefix — pasting it silently as
             * "success" would hand the pane partial data (review cycle 21).
             * Refuse the command and say exactly what happened. */
            char truncated_text[96];
            snprintf(truncated_text, sizeof(truncated_text),
                     "paste refused: selection truncated at %u byte(s) (16 MiB cap)",
                     daemon->paste_buffer_len);
            daemon_session_reply(connection, 1, truncated_text);
            return;
        }
        struct yetty_yclass_object *target_session = connection->session;
        uint32_t target_pane_id = connection->pane_id;
        if (!target_session) {
            for (size_t index = 0; index < YMUX_DAEMON_MAX_PTYS; ++index) {
                if (daemon->ptys[index].pty) {
                    target_session = daemon->ptys[index].session;
                    target_pane_id = daemon->ptys[index].pane_id;
                    break;
                }
            }
        }
        uint32_t pasted = 0;
        if (target_session && daemon->paste_buffer_len > 0) {
            struct yetty_yclass_object_ptr_result pane_res =
                yetty_ymux_session_pane(target_session, target_pane_id);
            if (YETTY_IS_ERR(pane_res)) {
                yetty_ycore_error_destroy(pane_res.error);
            } else {
                struct yetty_yclass_object_ptr_result engine_res =
                    yetty_ymux_pane_engine(pane_res.value);
                if (YETTY_IS_ERR(engine_res)) {
                    yetty_ycore_error_destroy(engine_res.error);
                } else {
                    struct yetty_ycore_void_result paste_res = yetty_ymux_engine_input_paste(
                        engine_res.value, daemon->paste_buffer, daemon->paste_buffer_len);
                    if (YETTY_IS_ERR(paste_res)) {
                        yetty_ycore_error_destroy(paste_res.error);
                    } else {
                        pasted = 1;
                    }
                }
            }
        }
        char ack_text[64];
        snprintf(ack_text, sizeof(ack_text), "pasted %u byte(s) to %u pane(s)",
                 daemon->paste_buffer_len, pasted);
        daemon_session_reply(connection, pasted > 0 ? 0 : 1, ack_text);
        return;
    }
    if (type == YMUX_PROTO_RECOVER) {
        /* Ops/debug: force the slow-client recovery path on every attached
         * connection — same code path the backlog high-water trips. The
         * requester gets an ACK carrying the recovered count, so a caller
         * (and the reset-ordering harness) can DETECT an ignored request. */
        uint32_t recovered = 0;
        for (size_t index = 0; index < YMUX_DAEMON_MAX_CONNECTIONS; ++index) {
            struct daemon_connection *attached = &daemon->connections[index];
            if (!attached->socket || attached->attachment_id == 0) {
                continue;
            }
            struct yetty_yclass_object_ptr_result projector_res =
                yetty_ymux_session_projector(attached->session, attached->attachment_id);
            if (YETTY_IS_ERR(projector_res)) {
                yetty_ycore_error_destroy(projector_res.error);
                continue;
            }
            if (daemon_recover_slow_client(attached, projector_res.value)) {
                ++recovered;
            }
        }
        char ack_text[64];
        snprintf(ack_text, sizeof(ack_text), "recovered %u client(s)", recovered);
        daemon_session_reply(connection, recovered > 0 ? 0 : 1, ack_text);
        return;
    }
    if (type == YMUX_PROTO_SESSION_NEW) {
        daemon_handle_session_new(daemon, connection, payload, payload_len);
        return;
    }
    if (type == YMUX_PROTO_SESSION_LIST || type == YMUX_PROTO_SESSION_HAS ||
        type == YMUX_PROTO_SESSION_KILL || type == YMUX_PROTO_SESSION_RENAME ||
        type == YMUX_PROTO_SESSION_DETACH || type == YMUX_PROTO_SESSION_SEND_KEYS) {
        daemon_handle_session_verb(daemon, connection, type, payload, payload_len);
        return;
    }
    if (connection->attachment_id == 0) {
        daemon_enqueue_refuse(connection, YMUX_PROTO_REFUSE_NOT_ATTACHED);
        return;
    }
    if (type == YMUX_PROTO_TTY_RESPONSE) {
        /* RAW renderer-terminal responses: consumed by THIS attachment's
         * response parser on its projector — never the application PTY. */
        struct yetty_yclass_object_ptr_result projector_res =
            yetty_ymux_session_projector(connection->session, connection->attachment_id);
        if (YETTY_IS_ERR(projector_res)) {
            yetty_ycore_error_destroy(projector_res.error);
            return;
        }
        struct yetty_ycore_void_result consume_res = yetty_ymux_projector_consume_tty_response(
            projector_res.value, payload, (uint32_t)payload_len);
        if (YETTY_IS_ERR(consume_res)) {
            yetty_ycore_error_destroy(consume_res.error);
        }
        return;
    }
    switch (type) {
    case YMUX_PROTO_DETACH: {
        daemon_release_connection_relays(daemon, connection);
        struct yetty_ycore_void_result detach_res =
            yetty_ymux_session_detach(connection->session, connection->attachment_id);
        if (YETTY_IS_ERR(detach_res)) {
            yetty_ycore_error_destroy(detach_res.error);
        }
        connection->attachment_id = 0;
        connection->pane_id = 0;
        connection->session = NULL;
        break;
    }
    case YMUX_PROTO_INPUT_CHAR: {
        if (payload_len < 2 * sizeof(uint32_t)) {
            daemon_enqueue_refuse(connection, YMUX_PROTO_REFUSE_BAD_FRAME);
            break;
        }
        uint32_t words[2];
        memcpy(words, payload, sizeof(words));
        struct yetty_ycore_void_result input_res = yetty_ymux_session_input_char(
            connection->session, connection->attachment_id, words[0], (int)words[1]);
        if (YETTY_IS_ERR(input_res)) {
            yetty_ycore_error_destroy(input_res.error);
            daemon_enqueue_refuse(connection, YMUX_PROTO_REFUSE_NOT_PERMITTED);
        }
        break;
    }
    case YMUX_PROTO_INPUT_KEY: {
        if (payload_len < 2 * sizeof(uint32_t)) {
            daemon_enqueue_refuse(connection, YMUX_PROTO_REFUSE_BAD_FRAME);
            break;
        }
        uint32_t words[2];
        memcpy(words, payload, sizeof(words));
        /* Permission-gated, same as INPUT_CHAR — a read-only attachment must
         * not inject special keys into the application PTY. */
        struct yetty_ycore_void_result key_res = yetty_ymux_session_input_key(
            connection->session, connection->attachment_id, (int)words[0], (int)words[1]);
        if (YETTY_IS_ERR(key_res)) {
            char chain_buf[512];
            yetty_ycore_error_snprint(chain_buf, sizeof(chain_buf), key_res.error);
            yerror("ymux daemon: INPUT_KEY refused (attachment %u): %s", connection->attachment_id,
                   chain_buf);
            yetty_ycore_error_destroy(key_res.error);
            daemon_enqueue_refuse(connection, YMUX_PROTO_REFUSE_NOT_PERMITTED);
        }
        break;
    }
    case YMUX_PROTO_INPUT_MOUSE: {
        if (payload_len < 4 * sizeof(uint32_t)) {
            daemon_enqueue_refuse(connection, YMUX_PROTO_REFUSE_BAD_FRAME);
            break;
        }
        uint32_t words[4]; /* kind, a, b, mods */
        memcpy(words, payload, sizeof(words));
        struct yetty_ycore_void_result mouse_res;
        if (words[0] == 0) {
            mouse_res = yetty_ymux_session_input_mouse_move(
                connection->session, connection->attachment_id, words[1], words[2], (int)words[3]);
        } else {
            mouse_res = yetty_ymux_session_input_mouse_button(
                connection->session, connection->attachment_id, (int)words[1], (int)words[2],
                (int)words[3]);
        }
        if (YETTY_IS_ERR(mouse_res)) {
            char chain_buf[512];
            yetty_ycore_error_snprint(chain_buf, sizeof(chain_buf), mouse_res.error);
            yerror("ymux daemon: INPUT_MOUSE refused (attachment %u, kind %u): %s",
                   connection->attachment_id, words[0], chain_buf);
            yetty_ycore_error_destroy(mouse_res.error);
            daemon_enqueue_refuse(connection, YMUX_PROTO_REFUSE_NOT_PERMITTED);
        }
        break;
    }
    case YMUX_PROTO_INPUT_PASTE: {
        struct yetty_ycore_void_result paste_res = yetty_ymux_session_input_paste(
            connection->session, connection->attachment_id, (const char *)payload, payload_len);
        if (YETTY_IS_ERR(paste_res)) {
            char chain_buf[512];
            yetty_ycore_error_snprint(chain_buf, sizeof(chain_buf), paste_res.error);
            yerror("ymux daemon: INPUT_PASTE refused (attachment %u): %s",
                   connection->attachment_id, chain_buf);
            yetty_ycore_error_destroy(paste_res.error);
            daemon_enqueue_refuse(connection, YMUX_PROTO_REFUSE_NOT_PERMITTED);
        }
        break;
    }
    case YMUX_PROTO_RPC_RELAY: {
        /* Client relayed a figure-surface RPC RESPONSE from yetty for a proxied
         * channel: write it back to that channel so the app reads its reply.
         * Only the controller (with INPUT permission) may inject into the app's
         * live channel — a read-only viewer must not. */
        if (!daemon_relay_authorized(connection)) {
            daemon_enqueue_refuse(connection, YMUX_PROTO_REFUSE_NOT_PERMITTED);
            break;
        }
        if (payload_len < sizeof(uint32_t)) {
            daemon_enqueue_refuse(connection, YMUX_PROTO_REFUSE_BAD_FRAME);
            break;
        }
        uint32_t channel_id;
        memcpy(&channel_id, payload, sizeof(uint32_t));
        const uint8_t *bytes = payload + sizeof(uint32_t);
        size_t bytes_len = payload_len - sizeof(uint32_t);
        struct daemon_pane_pty *slot =
            daemon_find_pty(daemon, connection->session, connection->pane_id);
        struct yetty_ywire_channel *channel = NULL;
        if (slot) {
            for (uint32_t index = 0; index < slot->rpc_channel_count; ++index) {
                if (slot->rpc_channels[index].channel_id == channel_id) {
                    channel = slot->rpc_channels[index].channel;
                    break;
                }
            }
        }
        if (channel && bytes_len > 0) {
            struct yetty_ycore_size_result write_res =
                yetty_ywire_channel_write(channel, bytes, bytes_len);
            if (YETTY_IS_ERR(write_res)) {
                yetty_ycore_error_destroy(write_res.error);
            } else {
                struct yetty_ycore_void_result flush_res = yetty_ywire_channel_flush(channel);
                if (YETTY_IS_ERR(flush_res)) {
                    yetty_ycore_error_destroy(flush_res.error);
                }
            }
        }
        break;
    }
    case YMUX_PROTO_RPC_RELAY_CLOSE: {
        /* The client's upstream figure channel closed. Gate it like the other
         * relay verbs (only the controller with INPUT permission may tear down a
         * pane channel — otherwise a viewer could drop a live mapping and strand
         * an in-flight response), validate the length, and actually CLOSE the
         * pane app's ywire channel so a blocked app is released, not merely
         * forgotten. */
        if (!daemon_relay_authorized(connection)) {
            daemon_enqueue_refuse(connection, YMUX_PROTO_REFUSE_NOT_PERMITTED);
            break;
        }
        if (payload_len != sizeof(uint32_t)) {
            daemon_enqueue_refuse(connection, YMUX_PROTO_REFUSE_BAD_FRAME);
            break;
        }
        uint32_t channel_id;
        memcpy(&channel_id, payload, sizeof(uint32_t));
        struct daemon_pane_pty *slot =
            daemon_find_pty(daemon, connection->session, connection->pane_id);
        if (slot) {
            for (uint32_t index = 0; index < slot->rpc_channel_count; ++index) {
                if (slot->rpc_channels[index].channel_id != channel_id) {
                    continue;
                }
                struct yetty_ywire_channel *channel = slot->rpc_channels[index].channel;
                /* Remove the mapping FIRST so the close event's own callback
                 * (daemon_pane_rpc_close) doesn't re-close or re-broadcast. */
                slot->rpc_channels[index] = slot->rpc_channels[--slot->rpc_channel_count];
                if (channel) {
                    struct yetty_ycore_void_result close_res = yetty_ywire_channel_close(channel);
                    if (YETTY_IS_ERR(close_res)) {
                        yetty_ycore_error_destroy(close_res.error);
                    }
                }
                break;
            }
        }
        break;
    }
    case YMUX_PROTO_FIGURE_INPUT: {
        /* Input yetty routed to a PROXIED figure (ygui/ygreeter). Re-emit it as
         * an OSC envelope on the pane PTY so the app's own ywire connection
         * decodes it onto its INPUT channel and feeds its ygui framework. Gated
         * to the controller (INPUT permission); the wire code is whitelisted so a
         * client cannot inject an arbitrary vendor OSC into the app's PTY. */
        if (!daemon_relay_authorized(connection)) {
            daemon_enqueue_refuse(connection, YMUX_PROTO_REFUSE_NOT_PERMITTED);
            break;
        }
        if (payload_len < sizeof(uint32_t)) {
            daemon_enqueue_refuse(connection, YMUX_PROTO_REFUSE_BAD_FRAME);
            break;
        }
        uint32_t wire_code;
        memcpy(&wire_code, payload, sizeof(uint32_t));
        if (wire_code != (uint32_t)YETTY_OSC_SC_CLIENT_INPUT_FIGURE_MOUSE &&
            wire_code != (uint32_t)YETTY_OSC_SC_CLIENT_INPUT_FIGURE_KEY) {
            daemon_enqueue_refuse(connection, YMUX_PROTO_REFUSE_BAD_FRAME);
            break;
        }
        const uint8_t *body = payload + sizeof(uint32_t);
        size_t body_len = payload_len - sizeof(uint32_t);
        struct daemon_pane_pty *slot =
            daemon_find_pty(daemon, connection->session, connection->pane_id);
        if (!slot) {
            break;
        }
        struct yetty_ycore_buffer out = {0};
        struct yetty_ycore_void_result emit_res =
            yetty_ywire_emit(YETTY_YWIRE_ENVELOPE_OSC, (int)wire_code, /*has_args=*/1,
                             /*compressed=*/0, NULL, 0, body, body_len, &out);
        if (YETTY_IS_OK(emit_res) && out.size > 0) {
            struct yetty_ycore_void_result write_res =
                daemon_pane_output((const char *)out.data, out.size, slot);
            if (YETTY_IS_ERR(write_res)) {
                yetty_ycore_error_destroy(write_res.error);
            }
        } else if (YETTY_IS_ERR(emit_res)) {
            yetty_ycore_error_destroy(emit_res.error);
        }
        yetty_ycore_buffer_destroy(&out);
        break;
    }
    case YMUX_PROTO_RESIZE: {
        if (payload_len < 2 * sizeof(uint32_t)) {
            daemon_enqueue_refuse(connection, YMUX_PROTO_REFUSE_BAD_FRAME);
            break;
        }
        uint32_t words[2];
        memcpy(words, payload, sizeof(words));
        struct yetty_ycore_void_result resize_res = yetty_ymux_session_resize(
            connection->session, connection->attachment_id, words[0], words[1]);
        if (YETTY_IS_ERR(resize_res)) {
            yetty_ycore_error_destroy(resize_res.error);
        }
        daemon_sync_pty_size(daemon, connection->session, connection->pane_id);
        break;
    }
    case YMUX_PROTO_ACK: {
        if (payload_len < 2 * sizeof(uint32_t)) {
            daemon_enqueue_refuse(connection, YMUX_PROTO_REFUSE_BAD_FRAME);
            break;
        }
        uint32_t words[2];
        memcpy(words, payload, sizeof(words));
        uint64_t generation = (uint64_t)words[0] | ((uint64_t)words[1] << 32);
        connection->acked_generation = generation;
        struct yetty_yclass_object_ptr_result attachment_res =
            yetty_ymux_session_attachment(connection->session, connection->attachment_id);
        if (YETTY_IS_OK(attachment_res)) {
            struct yetty_ycore_void_result ack_res =
                yetty_ymux_attachment_ack(attachment_res.value, generation);
            if (YETTY_IS_ERR(ack_res)) {
                yetty_ycore_error_destroy(ack_res.error);
            }
        } else {
            yetty_ycore_error_destroy(attachment_res.error);
        }
        break;
    }
    case YMUX_PROTO_VTSINK_PUBLISH: {
        /* The client hosts a vtsink and hands us [handle u64][feed_rid u32]
         * (#699.2). Build the ASYNC feed session over the lane: zero admin
         * round-trips — the slot id is seeded, the handle wrapped directly. */
        if (payload_len < sizeof(uint64_t) + sizeof(uint32_t) || connection->vtsink_session) {
            daemon_enqueue_refuse(connection, YMUX_PROTO_REFUSE_BAD_FRAME);
            break;
        }
        uint64_t sink_handle = 0;
        uint32_t feed_rid = 0;
        memcpy(&sink_handle, payload, sizeof(uint64_t));
        memcpy(&feed_rid, payload + sizeof(uint64_t), sizeof(uint32_t));
        struct yetty_yclass_transport_ptr_result lane_res =
            yetty_ymux_rpc_lane_create(daemon_vtsink_lane_tx, connection);
        if (YETTY_IS_ERR(lane_res)) {
            yetty_ycore_error_destroy(lane_res.error);
            daemon_enqueue_refuse(connection, YMUX_PROTO_REFUSE_BAD_FRAME);
            break;
        }
        struct yetty_yclass_rpc_session_ptr_result session_res =
            yetty_yclass_rpc_session_create(lane_res.value);
        if (YETTY_IS_ERR(session_res)) {
            yetty_ycore_error_destroy(session_res.error);
            struct yetty_ycore_void_result lane_destroy_res =
                lane_res.value->ops->destroy(lane_res.value);
            if (YETTY_IS_ERR(lane_destroy_res)) {
                yetty_ycore_error_destroy(lane_destroy_res.error);
            }
            daemon_enqueue_refuse(connection, YMUX_PROTO_REFUSE_BAD_FRAME);
            break;
        }
        struct yetty_ycore_void_result seed_res = yetty_yclass_rpc_session_seed_remote_id_by_name(
            session_res.value, "yetty_ymux_feed", feed_rid);
        struct yetty_yclass_object_ptr_result proxy_res =
            YETTY_IS_OK(seed_res)
                ? yetty_yclass_object_proxy_create(session_res.value, sink_handle, NULL)
                : (struct yetty_yclass_object_ptr_result){0};
        if (YETTY_IS_ERR(seed_res) || YETTY_IS_ERR(proxy_res) || !proxy_res.value) {
            if (YETTY_IS_ERR(seed_res)) {
                yetty_ycore_error_destroy(seed_res.error);
            } else if (YETTY_IS_ERR(proxy_res)) {
                yetty_ycore_error_destroy(proxy_res.error);
            }
            struct yetty_ycore_void_result session_destroy_res =
                yetty_yclass_rpc_session_destroy(session_res.value);
            if (YETTY_IS_ERR(session_destroy_res)) {
                yetty_ycore_error_destroy(session_destroy_res.error);
            }
            daemon_enqueue_refuse(connection, YMUX_PROTO_REFUSE_BAD_FRAME);
            break;
        }
        connection->vtsink_session = session_res.value;
        connection->vtsink_lane = lane_res.value;
        connection->vtsink_proxy = proxy_res.value;
        /* Transport switch = receiver reset: any frame emitted on the legacy
         * path between WELCOME and this PUBLISH is not part of the lane stream.
         * Invalidate the projector so the lane opens with a fresh COMPLETE
         * redraw — the lane byte stream is self-contained from its first feed. */
        struct yetty_yclass_object_ptr_result publish_projector_res =
            yetty_ymux_session_projector(connection->session, connection->attachment_id);
        if (YETTY_IS_OK(publish_projector_res)) {
            struct yetty_ycore_void_result invalidate_res =
                yetty_ymux_projector_invalidate(publish_projector_res.value);
            if (YETTY_IS_ERR(invalidate_res)) {
                yetty_ycore_error_destroy(invalidate_res.error);
            }
            connection->sent_generation = 0;
            connection->acked_generation = 0;
        } else {
            yetty_ycore_error_destroy(publish_projector_res.error);
        }
        ydebug("ymux daemon: vtsink lane up (attachment %u, handle %llu, feed rid %u)",
               connection->attachment_id, (unsigned long long)sink_handle, feed_rid);
        break;
    }
    case YMUX_PROTO_VTSINK_RPC: {
        /* Response frames from the client's dispatch — complete the pipelined
         * feed() calls. Failures surface through the session's default sink
         * (log + destroy); flow control rides the separate applied-ACK. */
        if (!connection->vtsink_session || !connection->vtsink_lane) {
            daemon_enqueue_refuse(connection, YMUX_PROTO_REFUSE_BAD_FRAME);
            break;
        }
        struct yetty_ycore_void_result push_res =
            yetty_ymux_rpc_lane_push_rx(connection->vtsink_lane, payload, payload_len);
        if (YETTY_IS_ERR(push_res)) {
            yetty_ycore_error_destroy(push_res.error);
            break;
        }
        struct yetty_ycore_void_result pump_res =
            yetty_yclass_rpc_session_pump(connection->vtsink_session);
        if (YETTY_IS_ERR(pump_res)) {
            char chain_buf[512];
            yetty_ycore_error_snprint(chain_buf, sizeof(chain_buf), pump_res.error);
            yerror("ymux daemon: vtsink pump failed (attachment %u): %s", connection->attachment_id,
                   chain_buf);
            yetty_ycore_error_destroy(pump_res.error);
        }
        break;
    }
    case YMUX_PROTO_SCROLL: {
        if (payload_len < sizeof(uint32_t)) {
            daemon_enqueue_refuse(connection, YMUX_PROTO_REFUSE_BAD_FRAME);
            break;
        }
        int32_t delta;
        memcpy(&delta, payload, sizeof(delta));
        daemon_connection_scroll(connection, delta);
        break;
    }
    case YMUX_PROTO_RESYNC: {
        /* The client lost/failed a VT frame — its terminal stream is unusable.
         * Invalidate the projector so the next projection is a fresh COMPLETE
         * redraw (paint FULL + a VT reset + full redraw) rather than another
         * incremental delta on a desynced receiver. */
        struct yetty_yclass_object_ptr_result projector_res =
            yetty_ymux_session_projector(connection->session, connection->attachment_id);
        if (YETTY_IS_OK(projector_res)) {
            struct yetty_ycore_void_result invalidate_res =
                yetty_ymux_projector_invalidate(projector_res.value);
            if (YETTY_IS_ERR(invalidate_res)) {
                yetty_ycore_error_destroy(invalidate_res.error);
            }
            connection->sent_generation = 0;
            connection->acked_generation = 0;
        } else {
            yetty_ycore_error_destroy(projector_res.error);
        }
        break;
    }
    case YMUX_PROTO_TAKEOVER: {
        struct yetty_ycore_void_result takeover_res =
            yetty_ymux_session_takeover(connection->session, connection->attachment_id);
        if (YETTY_IS_ERR(takeover_res)) {
            yetty_ycore_error_destroy(takeover_res.error);
            daemon_enqueue_refuse(connection, YMUX_PROTO_REFUSE_NOT_PERMITTED);
            break;
        }
        daemon_sync_pty_size(daemon, connection->session, connection->pane_id);
        break;
    }
    default:
        daemon_enqueue_refuse(connection, YMUX_PROTO_REFUSE_BAD_FRAME);
        break;
    }
}

/* Parse complete frames out of the connection's rx buffer. */
static int daemon_drain_rx(struct yetty_ymux_daemon *daemon, struct daemon_connection *connection)
{
    int handled = 0;
    for (;;) {
        if (connection->rx_len < YMUX_PROTO_HEADER_WORDS * sizeof(uint32_t)) {
            break;
        }
        uint32_t header[YMUX_PROTO_HEADER_WORDS];
        memcpy(header, connection->rx, sizeof(header));
        if (header[0] != YMUX_PROTO_MAGIC || header[2] > YMUX_PROTO_MAX_PAYLOAD) {
            connection->want_close = 1;
            break;
        }
        size_t frame_len = sizeof(header) + header[2];
        if (connection->rx_len < frame_len) {
            break;
        }
        daemon_handle_frame(daemon, connection, header[1], connection->rx + sizeof(header),
                            header[2]);
        memmove(connection->rx, connection->rx + frame_len, connection->rx_len - frame_len);
        connection->rx_len -= frame_len;
        ++handled;
    }
    return handled;
}

/*===========================================================================
 * Lifecycle.
 *=========================================================================*/

YETTY_ANNOTATE("expose")
struct yetty_yclass_object_ptr_result yetty_ymux_daemon_make(
    const char *socket_path, uint32_t default_rows, uint32_t default_cols,
    const struct yetty_ymux_daemon_host *host)
{
    struct yetty_yclass_ptr_result class_res = yetty_ymux_daemon_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_res, "ymux daemon_make: class");
    struct yetty_yclass_object_ptr_result object_res = yetty_yclass_object_alloc(class_res.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, object_res, "ymux daemon_make: alloc");
    struct yetty_ymux_daemon_ptr_result daemon_res = yetty_ymux_daemon_from(object_res.value);
    if (YETTY_IS_ERR(daemon_res)) {
        struct yetty_ycore_void_result free_res = yetty_yclass_object_free(object_res.value);
        if (YETTY_IS_ERR(free_res)) {
            yetty_ycore_error_destroy(free_res.error);
        }
        return YETTY_ERR(yetty_yclass_object_ptr, "ymux daemon_make: from_obj", daemon_res);
    }
    struct yetty_ymux_daemon *daemon = daemon_res.value;
    daemon->default_rows = default_rows ? default_rows : 24;
    daemon->default_cols = default_cols ? default_cols : 80;
    if (host) {
        daemon->host = *host;
    }

    /* tmux model: the server starts with ZERO sessions — new-session
     * creates them. */
    if (socket_path) {
        yetty_platform_socket_unlink(socket_path);
    }
    struct yetty_ipc_socket_result listen_res =
        yetty_platform_socket_listen(socket_path, daemon->socket_path);
    if (YETTY_IS_ERR(listen_res)) {
        struct yetty_ycore_void_result free_res = yetty_yclass_object_free(object_res.value);
        if (YETTY_IS_ERR(free_res)) {
            yetty_ycore_error_destroy(free_res.error);
        }
        return YETTY_ERR(yetty_yclass_object_ptr, "ymux daemon_make: listen", listen_res);
    }
    daemon->listener = listen_res.value;
    return YETTY_OK(yetty_yclass_object_ptr, object_res.value);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_daemon_dispose(struct yetty_yclass_object *obj)
{
    struct yetty_ymux_daemon_ptr_result daemon_res = yetty_ymux_daemon_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, daemon_res, "ymux daemon_dispose: from_obj");
    struct yetty_ymux_daemon *daemon = daemon_res.value;
    for (uint32_t index = 0; index < YMUX_DAEMON_MAX_CONNECTIONS; ++index) {
        if (daemon->connections[index].socket) {
            daemon_connection_reset(&daemon->connections[index]);
        }
    }
    for (uint32_t index = 0; index < YMUX_DAEMON_MAX_PTYS; ++index) {
        if (daemon->ptys[index].pty) {
            daemon_pane_rpc_teardown(&daemon->ptys[index]);
            struct yetty_ycore_void_result destroy_res =
                daemon->ptys[index].pty->ops->destroy(daemon->ptys[index].pty);
            if (YETTY_IS_ERR(destroy_res)) {
                yetty_ycore_error_destroy(destroy_res.error);
            }
            free(daemon->ptys[index].out_queue);
            memset(&daemon->ptys[index], 0, sizeof(daemon->ptys[index]));
        }
    }
    free(daemon->paste_buffer);
    daemon->paste_buffer = NULL;
    daemon->paste_buffer_len = 0;
    daemon->paste_buffer_cap = 0;
    for (uint32_t index = 0; index < YMUX_DAEMON_MAX_SESSIONS; ++index) {
        if (daemon->sessions[index].session) {
            struct yetty_ycore_void_result session_res =
                yetty_ymux_session_dispose(daemon->sessions[index].session);
            if (YETTY_IS_ERR(session_res)) {
                yetty_ycore_error_destroy(session_res.error);
            }
            memset(&daemon->sessions[index], 0, sizeof(daemon->sessions[index]));
        }
    }
    if (daemon->listener) {
        yetty_platform_socket_close(daemon->listener);
        daemon->listener = NULL;
        yetty_platform_socket_unlink(daemon->socket_path);
    }
    return yetty_yclass_object_free(obj);
}

/*===========================================================================
 * The pump.
 *=========================================================================*/

static int daemon_step_accept(struct yetty_ymux_daemon *daemon)
{
    int accepted = 0;
    for (;;) {
        struct yetty_ipc_socket_result accept_res = yetty_platform_socket_accept(daemon->listener);
        if (YETTY_IS_ERR(accept_res)) {
            yetty_ycore_error_destroy(accept_res.error);
            break;
        }
        if (!accept_res.value) {
            break;
        }
        struct daemon_connection *slot = NULL;
        for (uint32_t index = 0; index < YMUX_DAEMON_MAX_CONNECTIONS; ++index) {
            if (!daemon->connections[index].socket) {
                slot = &daemon->connections[index];
                break;
            }
        }
        if (!slot) {
            yetty_platform_socket_close(accept_res.value);
            break;
        }
        memset(slot, 0, sizeof(*slot));
        slot->rx = malloc(YMUX_DAEMON_RX_CAP);
        slot->tx = malloc(YMUX_DAEMON_TX_CAP);
        if (!slot->rx || !slot->tx) {
            free(slot->rx);
            free(slot->tx);
            memset(slot, 0, sizeof(*slot));
            yetty_platform_socket_close(accept_res.value);
            break;
        }
        slot->socket = accept_res.value;
        ++accepted;
    }
    return accepted;
}

static int daemon_step_connections_rx(struct yetty_ymux_daemon *daemon)
{
    int events = 0;
    for (uint32_t index = 0; index < YMUX_DAEMON_MAX_CONNECTIONS; ++index) {
        struct daemon_connection *connection = &daemon->connections[index];
        if (!connection->socket) {
            continue;
        }
        while (yetty_platform_socket_has_data(connection->socket) &&
               connection->rx_len < YMUX_DAEMON_RX_CAP) {
            struct yetty_ycore_size_result recv_res =
                yetty_platform_socket_recv(connection->socket, connection->rx + connection->rx_len,
                                           YMUX_DAEMON_RX_CAP - connection->rx_len);
            if (YETTY_IS_ERR(recv_res)) {
                yetty_ycore_error_destroy(recv_res.error);
                connection->want_close = 1;
                break;
            }
            if (recv_res.value == 0) {
                /* Readable but zero bytes = peer closed. Attachment
                 * detaches; the pane lives on (that is the point). */
                connection->want_close = 1;
                break;
            }
            connection->rx_len += recv_res.value;
            ++events;
        }
        events += daemon_drain_rx(daemon, connection);
        if (connection->want_close) {
            if (connection->attachment_id != 0) {
                struct yetty_ycore_void_result detach_res =
                    yetty_ymux_session_detach(connection->session, connection->attachment_id);
                if (YETTY_IS_ERR(detach_res)) {
                    yetty_ycore_error_destroy(detach_res.error);
                }
            }
            daemon_connection_reset(connection);
            ++events;
        }
    }
    return events;
}

static int daemon_step_ptys(struct yetty_ymux_daemon *daemon)
{
    int events = 0;
    char chunk[YMUX_DAEMON_PTY_CHUNK];
    for (uint32_t index = 0; index < YMUX_DAEMON_MAX_PTYS; ++index) {
        struct daemon_pane_pty *slot = &daemon->ptys[index];
        if (!slot->pty) {
            continue;
        }
        /* Retire proxied RPC channels whose controller changed or dropped, so a
         * takeover/detach releases a pane app blocked on a figure RPC reply. */
        daemon_pane_rpc_check_controller(slot);
        /* Flush any PTY output backlog the write path could not accept before. */
        if (slot->out_queue_len > 0) {
            size_t before = slot->out_queue_len;
            daemon_pane_drain_output(slot);
            if (slot->out_queue_len < before) {
                ++events;
            }
        }
        struct yetty_yclass_object_ptr_result pane_res =
            yetty_ymux_session_pane(slot->session, slot->pane_id);
        if (YETTY_IS_ERR(pane_res)) {
            yetty_ycore_error_destroy(pane_res.error);
            continue;
        }
        for (uint32_t budget = 0; budget < YMUX_DAEMON_PTY_STEP_CHUNKS; ++budget) {
            struct yetty_ycore_size_result read_res =
                slot->pty->ops->read(slot->pty, chunk, sizeof(chunk));
            if (YETTY_IS_ERR(read_res)) {
                /* Non-blocking read with nothing pending surfaces as an
                 * error on some backends (fork pty EAGAIN) — treat as
                 * empty; a real hangup shows up via child_alive. */
                yetty_ycore_error_destroy(read_res.error);
                break;
            }
            if (read_res.value == 0) {
                break;
            }
            struct yetty_ycore_void_result feed_res =
                yetty_ymux_pane_feed(pane_res.value, chunk, read_res.value);
            if (YETTY_IS_ERR(feed_res)) {
                yetty_ycore_error_destroy(feed_res.error);
                break;
            }
            ++events;
            if (read_res.value < sizeof(chunk)) {
                break;
            }
        }
        if (slot->pty->ops->child_alive && !slot->pty->ops->child_alive(slot->pty)) {
            struct yetty_yclass_object *dead_session = slot->session;
            daemon_close_pane(daemon, dead_session, slot->pane_id);
            ++events;
            /* tmux: a session dies with its last pane (shell exit), and
             * the server dies with its last session — the destroy path
             * flags shutdown when nothing remains. */
            struct yetty_ycore_uint32_result remaining_res =
                yetty_ymux_session_active_pane(dead_session);
            if (YETTY_IS_OK(remaining_res) && remaining_res.value == 0) {
                for (uint32_t entry = 0; entry < YMUX_DAEMON_MAX_SESSIONS; ++entry) {
                    if (daemon->sessions[entry].session == dead_session) {
                        daemon_session_destroy_entry(daemon, &daemon->sessions[entry]);
                        break;
                    }
                }
            } else if (YETTY_IS_ERR(remaining_res)) {
                yetty_ycore_error_destroy(remaining_res.error);
            }
        }
    }
    return events;
}

/* Drop ONLY the obsolete terminal-redraw frames (VT/TRANSACTION/PAINT) from a
 * connection's tx backlog during slow-client recovery, keeping control/effect
 * frames and — critically — the figure-RPC relay frames: those are not a
 * recoverable stateful redraw, and a pane application is blocked waiting on the
 * relay ones. tx[0] always holds a valid frame header; the LEADING frame is kept
 * unconditionally when it is mid-flight (tx_sent>0) since its head is already on
 * the wire. Walk and compact. */
static void daemon_tx_drop_terminal_frames(struct daemon_connection *connection)
{
    struct yetty_ymux_tx_queue queue = {
        .buffer = connection->tx, .len = connection->tx_len, .sent = connection->tx_sent};
    yetty_ymux_tx_queue_drop_terminal_frames(&queue);
    connection->tx_len = queue.len;
}

/* #699 slow-client recovery. The client fell far enough behind that its queued
 * VT stream (a STATEFUL incremental redraw) is no longer usable — tmux does not
 * skip terminal bytes and keep going. Discard the obsolete queued TERMINAL
 * frames (keeping control/RPC/effect frames — see daemon_tx_drop_terminal_frames;
 * safe only at a clean frame boundary), invalidate the attachment's projector so
 * the next projection is a fresh COMPLETE redraw (paint FULL + a VT reset + full
 * redraw), and resync the ack window. Returns 1 when recovery ran. */
static int daemon_recover_slow_client(struct daemon_connection *connection,
                                      struct yetty_yclass_object *projector)
{
    /* tx[0] always holds a valid frame header now, so recovery can run at any
     * backlog: daemon_tx_drop_terminal_frames keeps the in-flight leading frame
     * (tx_sent>0) plus all control/RPC/effect frames and drops only obsolete
     * terminal redraw. This removes the old boolean that starved recovery. */
    daemon_tx_drop_terminal_frames(connection); /* incl. obsolete VTSINK_RPC feeds */
    /* Cancel the terminal-RPC EPOCH (review #12): the dropped feed requests
     * belonged to a session whose completion FIFO would now desync — destroy
     * it wholesale and tell the client to rebuild + re-publish its sink. The
     * projector invalidation below makes the fresh epoch open with a
     * complete redraw once the new PUBLISH seeds a session. */
    if (connection->vtsink_proxy) {
        daemon_vtsink_teardown(connection);
        struct yetty_ycore_void_result reset_res =
            daemon_enqueue(connection, YMUX_PROTO_VTSINK_RESET, NULL, 0);
        if (YETTY_IS_ERR(reset_res)) {
            yetty_ycore_error_destroy(reset_res.error);
        }
    }
    struct yetty_ycore_void_result invalidate_res = yetty_ymux_projector_invalidate(projector);
    if (YETTY_IS_ERR(invalidate_res)) {
        yetty_ycore_error_destroy(invalidate_res.error);
    }
    /* Resync the window so the fresh complete redraw is produced immediately. */
    connection->sent_generation = 0;
    connection->acked_generation = 0;
    return 1;
}

static int daemon_step_project(struct yetty_ymux_daemon *daemon)
{
    int events = 0;
    for (uint32_t index = 0; index < YMUX_DAEMON_MAX_CONNECTIONS; ++index) {
        struct daemon_connection *connection = &daemon->connections[index];
        if (!connection->socket || connection->attachment_id == 0) {
            continue;
        }
        /* Slow-client recovery: an undrained tx backlog past the high-water mark
         * means the client cannot keep up. Rather than skip-and-continue a
         * stateful VT stream, discard it and force a complete redraw. Pressure is
         * the UNSENT backlog (tx_len - tx_sent), NOT tx_len: a large leading frame
         * already drained to the socket but not yet reclaimed is not backlog. A
         * strike is counted only when the socket drained NOTHING since the last
         * recovery — so a client steadily draining even one huge frame is never
         * mistaken for hopeless; a genuinely stuck socket still trips the bound. */
        /* Push the pane-mode bitmask on change (selection ownership). */
        {
            struct yetty_yclass_object_ptr_result mode_pane_res =
                yetty_ymux_session_pane(connection->session, connection->pane_id);
            if (YETTY_IS_OK(mode_pane_res)) {
                struct yetty_yclass_object_ptr_result mode_engine_res =
                    yetty_ymux_pane_engine(mode_pane_res.value);
                if (YETTY_IS_OK(mode_engine_res)) {
                    uint32_t pane_modes =
                        yetty_ymux_engine_mouse_mode(mode_engine_res.value) ? 1u : 0u;
                    if (!connection->sent_pane_modes_valid ||
                        pane_modes != connection->sent_pane_modes) {
                        uint32_t payload = pane_modes;
                        struct yetty_ycore_void_result mode_send_res =
                            daemon_enqueue(connection, YMUX_PROTO_PANE_MODES,
                                           (const uint8_t *)&payload, sizeof(payload));
                        if (YETTY_IS_OK(mode_send_res)) {
                            connection->sent_pane_modes = pane_modes;
                            connection->sent_pane_modes_valid = 1;
                            ++events;
                        } else {
                            yetty_ycore_error_destroy(mode_send_res.error);
                        }
                    }
                } else {
                    yetty_ycore_error_destroy(mode_engine_res.error);
                }
            } else {
                yetty_ycore_error_destroy(mode_pane_res.error);
            }
        }
        size_t unsent = connection->tx_len - connection->tx_sent;
        if (unsent > YMUX_DAEMON_TX_RECOVER_HIGH_WATER) {
            struct yetty_yclass_object_ptr_result slow_projector_res =
                yetty_ymux_session_projector(connection->session, connection->attachment_id);
            if (YETTY_IS_OK(slow_projector_res)) {
                if (daemon_recover_slow_client(connection, slow_projector_res.value)) {
                    ++events;
                    uint64_t drained = connection->tx_total_sent - connection->slow_last_total_sent;
                    connection->slow_last_total_sent = connection->tx_total_sent;
                    int should_close = 0;
                    connection->slow_recover_count = yetty_ymux_tx_recovery_strike(
                        connection->slow_recover_count, drained, YMUX_DAEMON_SLOW_RECOVER_LIMIT,
                        &should_close);
                    if (should_close) {
                        connection->want_close = 1;
                    }
                }
            } else {
                yetty_ycore_error_destroy(slow_projector_res.error);
            }
            continue; /* the fresh complete redraw is produced next step */
        }
        if (connection->sent_generation > connection->acked_generation + YMUX_DAEMON_MAX_UNACKED) {
            continue; /* backpressure: coalesce into a later delta */
        }
        /* The client is keeping up again — clear the recovery strike count and
         * rebaseline the drain meter so a future backlog measures fresh progress. */
        connection->slow_recover_count = 0;
        connection->slow_last_total_sent = connection->tx_total_sent;
        struct yetty_yclass_object_ptr_result projector_res =
            yetty_ymux_session_projector(connection->session, connection->attachment_id);
        if (YETTY_IS_ERR(projector_res)) {
            yetty_ycore_error_destroy(projector_res.error);
            continue;
        }
        /* #699.3: text is delivered exclusively as vtsink feed() calls; the
         * TRANSACTION frame carries ONLY the rich body. */
        int is_vt_text = (connection->capabilities & YMUX_TERM_CAP_VT_TEXT) != 0;
        struct yetty_ycore_buffer_result rich_buffer_res =
            yetty_ycore_buffer_create(YMUX_DAEMON_PAINT_CAP);
        if (YETTY_IS_ERR(rich_buffer_res)) {
            yetty_ycore_error_destroy(rich_buffer_res.error);
            continue;
        }
        struct yetty_ycore_buffer rich_buffer = rich_buffer_res.value;
        struct yetty_ycore_int_result rich_res =
            yetty_ymux_projector_project_rich(projector_res.value, &rich_buffer);
        int rich_produced = 0;
        if (YETTY_IS_OK(rich_res)) {
            rich_produced = rich_res.value;
        } else {
            yetty_ycore_error_destroy(rich_res.error);
        }
        size_t rich_bytes = rich_produced ? rich_buffer.size : 0;

        /* Set when a produced frame does not fit the fixed tx buffer: the rest of
         * this step's projection is skipped and a complete redraw is scheduled,
         * rather than overflow-closing a draining client (see daemon_tx_can_fit). */
        int projection_deferred = 0;

        if (rich_bytes) {
            /* Rich-only TRANSACTION (#699.3 residue removal): the payload IS
             * the rich body — no retired paint half, no count prefix. */
            if (!daemon_tx_can_fit(connection, rich_bytes)) {
                /* Does not fit now. Defer WITHOUT advancing flow control past a
                 * delta we never sent: invalidate the projector (next projection
                 * is a full redraw), drop obsolete terminal frames to reclaim
                 * space, and resync the window. A client that never drains is
                 * still caught by the unsent-backlog strikes. */
                daemon_recover_slow_client(connection, projector_res.value);
                projection_deferred = 1;
            } else {
                struct yetty_ycore_void_result enqueue_res = daemon_enqueue(
                    connection, YMUX_PROTO_TRANSACTION, rich_buffer.data, rich_bytes);
                if (YETTY_IS_ERR(enqueue_res)) {
                    yetty_ycore_error_destroy(enqueue_res.error);
                }
            }
            ++events;
        }

        /* VT redraw for the yscene client terminal grid (#699): the tmux-style
         * projected redraw, delivered EXCLUSIVELY as typed ordered feed() calls
         * on the client's published vtsink (#699.2). Driven INDEPENDENTLY of
         * the semantic paint diff — project_vt has its own cell+cursor shadow,
         * so a cursor-only move still emits a frame; empty buffer = no change.
         * Gated on the lane being up: project_vt CONSUMES the delta, so
         * projecting before the client's VTSINK_PUBLISH would lose content —
         * until then the projector simply accumulates, and the PUBLISH-time
         * invalidation opens the lane with a complete redraw. Skipped when the
         * transaction above already deferred (buffer full). */
        if (!projection_deferred && is_vt_text && connection->vtsink_proxy) {
            struct yetty_ycore_buffer_result vt_buffer_res =
                yetty_ycore_buffer_create(YMUX_DAEMON_PAINT_CAP);
            if (YETTY_IS_OK(vt_buffer_res)) {
                struct yetty_ycore_buffer vt_buffer = vt_buffer_res.value;
                struct yetty_ycore_void_result vt_res =
                    yetty_ymux_projector_project_vt(projector_res.value, &vt_buffer);
                /* The enqueued frame is the ENCODED yclass feed() request —
                 * raw VT bytes plus the RPC envelope (call header + body
                 * framing: handle, generation, buffer length) — inside a
                 * VTSINK_RPC lane frame. The envelope bound is small and
                 * fixed; enqueue failure ALSO propagates through the lane, so
                 * this pre-check is an optimization, not the safety net. */
                enum { YMUX_DAEMON_VTSINK_RPC_ENVELOPE_MAX = 64 };
                if (YETTY_IS_OK(vt_res) && vt_buffer.size > 0 &&
                    !daemon_tx_can_fit(connection,
                                       vt_buffer.size + YMUX_DAEMON_VTSINK_RPC_ENVELOPE_MAX)) {
                    /* The frame does not fit — defer it exactly like the
                     * transaction above (invalidate + full redraw next step)
                     * instead of overflow-closing a draining client. */
                    daemon_recover_slow_client(connection, projector_res.value);
                } else if (YETTY_IS_OK(vt_res) && vt_buffer.size > 0) {
                    /* Mint the flow-control generation — the client ACKs it
                     * AFTER its dispatch applies the feed, so the ACK window
                     * tracks rendered state, not bytes received. */
                    uint64_t generation = 0;
                    struct yetty_yclass_object_ptr_result attachment_res =
                        yetty_ymux_session_attachment(connection->session,
                                                      connection->attachment_id);
                    if (YETTY_IS_OK(attachment_res)) {
                        struct yetty_ycore_uint64_result gen_res =
                            yetty_ymux_attachment_next_generation(attachment_res.value);
                        if (YETTY_IS_OK(gen_res)) {
                            generation = gen_res.value;
                        } else {
                            yetty_ycore_error_destroy(gen_res.error);
                        }
                    } else {
                        yetty_ycore_error_destroy(attachment_res.error);
                    }
                    struct yetty_ycore_buffer feed_bytes = {
                        .data = vt_buffer.data,
                        .size = vt_buffer.size,
                        .capacity = vt_buffer.size,
                    };
                    struct yetty_ycore_void_result feed_res =
                        yetty_ymux_feed(connection->vtsink_proxy, generation, feed_bytes);
                    if (YETTY_IS_ERR(feed_res)) {
                        /* The projection COMMITTED (op-consumed + shadow) but
                         * the bytes never reached the queue — without recovery
                         * the next projection sees no delta and the content is
                         * silently lost (review #11 P0). Same treatment as the
                         * can_fit defer: drop obsolete terminal frames,
                         * invalidate the projector (next projection = a fresh
                         * COMPLETE redraw), resync the ack window. The minted
                         * generation is never sent; the gap is harmless
                         * (generations are monotonic, the window resyncs). */
                        yetty_ycore_error_destroy(feed_res.error);
                        daemon_recover_slow_client(connection, projector_res.value);
                    } else {
                        connection->sent_generation = generation;
                        ++events;
                    }
                } else if (YETTY_IS_ERR(vt_res)) {
                    yetty_ycore_error_destroy(vt_res.error);
                }
                yetty_ycore_buffer_destroy(&vt_buffer);
            } else {
                yetty_ycore_error_destroy(vt_buffer_res.error);
            }
        }
        yetty_ycore_buffer_destroy(&rich_buffer);
    }
    return events;
}

static int daemon_step_tx(struct yetty_ymux_daemon *daemon)
{
    int events = 0;
    for (uint32_t index = 0; index < YMUX_DAEMON_MAX_CONNECTIONS; ++index) {
        struct daemon_connection *connection = &daemon->connections[index];
        if (!connection->socket || connection->tx_sent >= connection->tx_len) {
            continue;
        }
        struct yetty_ycore_size_result send_res =
            yetty_platform_socket_send(connection->socket, connection->tx + connection->tx_sent,
                                       connection->tx_len - connection->tx_sent);
        if (YETTY_IS_ERR(send_res)) {
            yetty_ycore_error_destroy(send_res.error);
            if (connection->attachment_id != 0) {
                struct yetty_ycore_void_result detach_res =
                    yetty_ymux_session_detach(connection->session, connection->attachment_id);
                if (YETTY_IS_ERR(detach_res)) {
                    yetty_ycore_error_destroy(detach_res.error);
                }
            }
            daemon_connection_reset(connection);
            continue;
        }
        if (send_res.value) {
            connection->tx_sent += send_res.value;
            connection->tx_total_sent += send_res.value; /* monotonic drain meter */
            /* Reclaim every FULLY-sent leading frame so tx[0] stays a valid frame
             * header and the buffer doesn't retain drained bytes; tx_sent then
             * points into the still-partial (or fresh) leading frame. */
            struct yetty_ymux_tx_queue queue = {
                .buffer = connection->tx, .len = connection->tx_len, .sent = connection->tx_sent};
            yetty_ymux_tx_queue_reclaim_sent(&queue);
            connection->tx_len = queue.len;
            connection->tx_sent = queue.sent;
            ++events;
        }
    }
    return events;
}

/* One non-blocking pump: accept, client frames, PTY output, projections,
 * tx flush. Returns the number of events processed (0 = quiescent — the
 * run loop may sleep/poll before the next step). */
/* The copy-mode paste buffer (copied out; returns the length). */
YETTY_ANNOTATE("expose")
struct yetty_ycore_uint32_result yetty_ymux_daemon_paste_buffer(struct yetty_yclass_object *obj,
                                                                uint8_t *out_bytes,
                                                                uint32_t out_capacity)
{
    struct yetty_ymux_daemon_ptr_result daemon_res = yetty_ymux_daemon_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, daemon_res, "ymux daemon paste_buffer");
    struct yetty_ymux_daemon *daemon = daemon_res.value;
    uint32_t copy_len =
        daemon->paste_buffer_len < out_capacity ? daemon->paste_buffer_len : out_capacity;
    if (copy_len > 0 && out_bytes) {
        memcpy(out_bytes, daemon->paste_buffer, copy_len);
    }
    return YETTY_OK(yetty_ycore_uint32, daemon->paste_buffer_len);
}

/* The chrome-intake seat: total CONSUMED overlay events received (across
 * connections), plus the most recent event's class — the observable end of
 * the overlay consumer route until interactive chrome lands. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_uint64_result yetty_ymux_daemon_chrome_intake(struct yetty_yclass_object *obj)
{
    struct yetty_ymux_daemon_ptr_result daemon_res = yetty_ymux_daemon_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint64, daemon_res, "ymux daemon chrome_intake");
    struct yetty_ymux_daemon *daemon = daemon_res.value;
    uint64_t total = 0;
    uint32_t last_class = 0;
    for (size_t index = 0; index < YMUX_DAEMON_MAX_CONNECTIONS; ++index) {
        if (daemon->connections[index].chrome_intake_count > 0) {
            total += daemon->connections[index].chrome_intake_count;
            last_class = daemon->connections[index].chrome_intake_class;
        }
    }
    return YETTY_OK(yetty_ycore_uint64, (total << 8) | last_class);
}

/* The last CONSUMED chrome event's payload — the seat's identity
 * observation: copies up to capacity, returns (class << 32) | length. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_uint64_result yetty_ymux_daemon_chrome_last_event(
    struct yetty_yclass_object *obj, uint8_t *out_bytes, uint32_t out_capacity)
{
    struct yetty_ymux_daemon_ptr_result daemon_res = yetty_ymux_daemon_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint64, daemon_res, "ymux daemon chrome_last_event");
    struct yetty_ymux_daemon *daemon = daemon_res.value;
    for (size_t index = 0; index < YMUX_DAEMON_MAX_CONNECTIONS; ++index) {
        struct daemon_connection *connection = &daemon->connections[index];
        if (!connection->socket || !connection->chrome_last_bytes) {
            continue;
        }
        uint32_t copy_len =
            connection->chrome_last_len < out_capacity ? connection->chrome_last_len : out_capacity;
        if (copy_len > 0 && out_bytes) {
            memcpy(out_bytes, connection->chrome_last_bytes, copy_len);
        }
        return YETTY_OK(yetty_ycore_uint64, ((uint64_t)connection->chrome_last_class << 32) |
                                                connection->chrome_last_len);
    }
    return YETTY_OK(yetty_ycore_uint64, 0);
}

/* Tests only: force slow-client recovery (epoch reset) on every attached
 * connection NOW — the high-water trigger needs megabytes of backlog that a
 * unit rig cannot accumulate through the ack window. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_daemon_force_recover(struct yetty_yclass_object *obj)
{
    struct yetty_ymux_daemon_ptr_result daemon_res = yetty_ymux_daemon_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, daemon_res, "ymux daemon force_recover");
    struct yetty_ymux_daemon *daemon = daemon_res.value;
    for (size_t index = 0; index < YMUX_DAEMON_MAX_CONNECTIONS; ++index) {
        struct daemon_connection *connection = &daemon->connections[index];
        if (!connection->socket || connection->attachment_id == 0) {
            continue;
        }
        struct yetty_yclass_object_ptr_result projector_res =
            yetty_ymux_session_projector(connection->session, connection->attachment_id);
        if (YETTY_IS_ERR(projector_res)) {
            yetty_ycore_error_destroy(projector_res.error);
            continue;
        }
        daemon_recover_slow_client(connection, projector_res.value);
    }
    return YETTY_OK_VOID();
}

/* Tests only: make the next `count` vtsink lane enqueues fail on every
 * connection, forcing the feed-failure recovery path. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_daemon_fail_next_vtsink_tx(
    struct yetty_yclass_object *obj, uint32_t count)
{
    struct yetty_ymux_daemon_ptr_result daemon_res = yetty_ymux_daemon_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, daemon_res, "ymux daemon fail_next_vtsink_tx");
    struct yetty_ymux_daemon *daemon = daemon_res.value;
    for (size_t index = 0; index < YMUX_DAEMON_MAX_CONNECTIONS; ++index) {
        if (daemon->connections[index].socket) {
            daemon->connections[index].fail_next_vtsink_tx = count;
        }
    }
    return YETTY_OK_VOID();
}

/* Test seam (review #21): mark the paste buffer TRUNCATED as if the copy hit
 * the 16 MiB cap — drives the paste-refusal boundary without building a
 * multi-megabyte selection. Hand-written, module-internal. */
struct yetty_ycore_void_result yetty_ymux_daemon_force_paste_truncated(
    struct yetty_yclass_object *obj)
{
    struct yetty_ymux_daemon_ptr_result daemon_res = yetty_ymux_daemon_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, daemon_res, "ymux daemon force_paste_truncated");
    daemon_res.value->paste_truncated = 1;
    return YETTY_OK_VOID();
}

/* Test seam (review #19): NACK-refuse the next `count` overlay events on
 * every attached connection — drives the refusal/retry negative paths.
 * Hand-written, module-internal. */
struct yetty_ycore_void_result yetty_ymux_daemon_refuse_next_overlay(
    struct yetty_yclass_object *obj, uint32_t count)
{
    struct yetty_ymux_daemon_ptr_result daemon_res = yetty_ymux_daemon_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, daemon_res, "ymux daemon refuse_next_overlay");
    struct yetty_ymux_daemon *daemon = daemon_res.value;
    for (size_t index = 0; index < YMUX_DAEMON_MAX_CONNECTIONS; ++index) {
        if (daemon->connections[index].socket) {
            daemon->connections[index].refuse_next_overlay = count;
        }
    }
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_ymux_daemon_step(struct yetty_yclass_object *obj)
{
    struct yetty_ymux_daemon_ptr_result daemon_res = yetty_ymux_daemon_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, daemon_res, "ymux daemon_step: from_obj");
    struct yetty_ymux_daemon *daemon = daemon_res.value;
    int events = 0;
    events += daemon_step_accept(daemon);
    events += daemon_step_connections_rx(daemon);
    events += daemon_step_ptys(daemon);
    events += daemon_step_project(daemon);
    events += daemon_step_tx(daemon);
    return YETTY_OK(yetty_ycore_int, events);
}

/*===========================================================================
 * Accessors.
 *=========================================================================*/

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_daemon_socket_path(struct yetty_yclass_object *obj,
                                                             char *out, size_t out_cap)
{
    struct yetty_ymux_daemon_ptr_result daemon_res = yetty_ymux_daemon_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, daemon_res, "ymux daemon_socket_path: from_obj");
    if (!out || out_cap == 0) {
        return YETTY_ERR(yetty_ycore_void, "ymux daemon_socket_path: no out buffer");
    }
    strncpy(out, daemon_res.value->socket_path, out_cap - 1);
    out[out_cap - 1] = 0;
    return YETTY_OK_VOID();
}

/* Borrowed — tests inspect controller policy through it. NULL-name (or
 * empty) resolves the most recent session, tmux-style. */
YETTY_ANNOTATE("expose")
struct yetty_yclass_object_ptr_result yetty_ymux_daemon_session(struct yetty_yclass_object *obj,
                                                                const char *name)
{
    struct yetty_ymux_daemon_ptr_result daemon_res = yetty_ymux_daemon_from(obj);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, daemon_res, "ymux daemon_session: from_obj");
    struct daemon_session_entry *entry = daemon_session_target(daemon_res.value, name);
    if (!entry) {
        return YETTY_ERR(yetty_yclass_object_ptr, "ymux daemon_session: no such session");
    }
    return YETTY_OK(yetty_yclass_object_ptr, entry->session);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_ymux_daemon_shutdown_requested(struct yetty_yclass_object *obj)
{
    struct yetty_ymux_daemon_ptr_result daemon_res = yetty_ymux_daemon_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, daemon_res, "ymux daemon_shutdown_requested: from_obj");
    return YETTY_OK(yetty_ycore_int, daemon_res.value->shutdown_requested);
}

#include "yetty/gen/impl/ymux/daemon.c"
