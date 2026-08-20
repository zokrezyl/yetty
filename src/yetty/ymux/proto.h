/*
 * proto.h — the ymux client⇄daemon socket frame protocol, shared between
 * daemon.c (server) and client.c (client). Module-private to ymux:
 * a wire contract, not a public C API.
 *
 * Transport: a byte stream (unix domain socket / named pipe via the
 * yplatform ipc-socket seam). Every message is one frame:
 *
 *   u32 magic  = YMUX_PROTO_MAGIC
 *   u32 type   = one of YMUX_PROTO_*
 *   u32 length = payload byte count (may be 0)
 *   length bytes of payload
 *
 * All integers little-endian. Payload layouts are u32 words except where a
 * byte string is stated.
 */
#ifndef YETTY_YMUX_PROTO_H
#define YETTY_YMUX_PROTO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Overlay-first input routing classes (#699.4, review #11). */
enum {
    YMUX_INPUT_CLASS_POINTER = 1,
    YMUX_INPUT_CLASS_KEY = 2,
    YMUX_INPUT_CLASS_PASTE = 3,
};

enum {
    YMUX_PROTO_MAGIC = 0x594D5846, /* "YMXF" */
    YMUX_PROTO_VERSION = 9,        /* bumped: ATTACH may append the client's
                                    * TERMINAL STRINGS ([term_len u32]
                                    * [features_len u32][term][features]) after
                                    * the token — the daemon resolves the
                                    * capability profile through the tmux
                                    * terminfo/features state model instead of
                                    * trusting the bitmask alone.
                                    * Previously 8: TRANSACTION is the rich body VERBATIM
                             * (the paint count prefix is gone from the wire).
                             * v7: the semantic cell PAINT path is gone
                             * entirely (#699.3) — no PAINT frames, and the
                             * TRANSACTION frame is rich-only (a nonzero paint
                             * word count is refused). Text is delivered
                             * EXCLUSIVELY as typed ordered vtsink feed() calls
                             * over the VTSINK_RPC lane (#699.2): the client
                             * publishes its hosted sink (VTSINK_PUBLISH) after
                             * every WELCOME and ACKs the APPLIED generation.
                             * word[7]=capabilities, word[4]=cell height.
                             * A stale older peer refuses attach on version. */
    YMUX_PROTO_HEADER_WORDS = 3,
    YMUX_PROTO_TOKEN_MAX = 63,
    YMUX_PROTO_MAX_PAYLOAD = 4u << 20,
};

/* Client terminfo capability bitmask (attach word[7]) — what the yscene client
 * terminal grid advertises so the daemon's #699 VT projector matches tmux for
 * THAT capability profile. Grows as more capability-gated emission is ported
 * (BCE, extended underline, hyperlinks, alt-charset). */
enum yetty_ymux_term_cap {
    YMUX_TERM_CAP_TRUECOLOR = 1u << 0, /* RGB SGR passthrough (\e[38;2;R;G;Bm);
                                        * when absent the projector downgrades a
                                        * true-colour cell to the nearest 256
                                        * palette index (setaf/setab). */
    YMUX_TERM_CAP_VT_TEXT = 1u << 1,   /* the client renders text from the VT byte
                                        * stream (its own libvterm grid) fed by
                                        * vtsink feed() calls; flow control keys
                                        * to the feed generation the client ACKs
                                        * after applying. */
    /* Renderer capability bits (review #13): the attach negotiates the tty
     * PROFILE — absent bits take tmux's documented fallbacks in the emitter
     * (ECH -> spaces, regions/IL/DL -> redraw, exotic pen -> dropped). The
     * xterm-256color truecolor profile sets ECH|ILDL|DECSTBM|BCE. */
    YMUX_TERM_CAP_ECH = 1u << 3,
    YMUX_TERM_CAP_ILDL = 1u << 4,
    YMUX_TERM_CAP_DECSTBM = 1u << 5,
    YMUX_TERM_CAP_BCE = 1u << 6,
    YMUX_TERM_CAP_EXTENDED_UNDERLINE = 1u << 7,
    YMUX_TERM_CAP_UNDERLINE_COLOUR = 1u << 8,
    YMUX_TERM_CAP_HYPERLINK = 1u << 9,
    YMUX_TERM_CAP_SYNC = 1u << 11,            /* synchronized output (DEC 2026) — set
                                    * RESPONSE-DRIVEN when the client's
                                    * DECRPM ?2026 report says the mode is
                                    * recognized (review #19). */
    YMUX_TERM_CAP_ATTACH_PREAMBLE = 1u << 10, /* emit tmux's attach setup
                                               * (modes, paste, theme query,
                                               * region, clear) before the
                                               * first full redraw */
    YMUX_TERM_CAP_MARGINS = 1u << 12,         /* DECLRMM/DECSLRM horizontal margins:
                                               * the attach preamble enables ?69h and
                                               * establishes full-screen margins */
    /* The terminal endpoint's ONE deterministic identity (review #14): the
     * yscene grid's libvterm supports the full xterm-256color operation set,
     * so production attach AND the differential driver advertise exactly
     * this — the oracle's TERM=xterm-256color is the same profile. */
    YMUX_TERM_CAPS_XTERM_256COLOR = YMUX_TERM_CAP_TRUECOLOR | YMUX_TERM_CAP_ECH |
                                    YMUX_TERM_CAP_ILDL | YMUX_TERM_CAP_DECSTBM | YMUX_TERM_CAP_BCE,
    YMUX_TERM_CAP_RESOURCE_REF = 1u << 2, /* the client caches rich creation
                                           * payloads by content hash, so the
                                           * projector may REFERENCE a payload it
                                           * already sent (YMUX_RICH_FLAG_RESOURCE
                                           * _REF) instead of re-sending the
                                           * heavy bytes every full frame. */
};

enum yetty_ymux_proto_type {
    /* client → daemon */
    YMUX_PROTO_ATTACH = 1,              /* version, pane_id (0 = default), view_rows,
                                * view_cols, cell_pixel_height, session_name_len,
                                * token_len, capabilities (YMUX_TERM_CAP_*),
                                * session_name bytes, token bytes. Session
                                * name empty = the most recent session
                                * (tmux attach-session without -t); the
                                * session must exist. */
    YMUX_PROTO_DETACH = 2,              /* (empty) — connection stays open */
    YMUX_PROTO_INPUT_CHAR = 3,          /* codepoint, mods */
    YMUX_PROTO_INPUT_KEY = 4,           /* key, mods */
    YMUX_PROTO_RESIZE = 5,              /* rows, cols */
    YMUX_PROTO_ACK = 6,                 /* generation lo, hi */
    YMUX_PROTO_TAKEOVER = 7,            /* (empty) */
    YMUX_PROTO_SHUTDOWN = 8,            /* (empty) — daemon exits its run loop */
    YMUX_PROTO_SCROLL = 9,              /* delta rows as i32 (negative = into
                                * history; reaching the live top resumes
                                * follow-live) */
    YMUX_PROTO_RESYNC = 17,             /* (empty) — the client's terminal-byte stream is
                                * unusable (a dropped/failed VT frame): invalidate
                                * this attachment's projector so the next
                                * projection is a fresh COMPLETE redraw. */
    YMUX_PROTO_INPUT_MOUSE = 18,        /* kind, a, b, mods: kind 0 = move (a=row,
                                  * b=col), kind 1 = button (a=button, b=pressed).
                                  * Permission-gated; the engine encodes per the
                                  * application's active mouse mode (or drops). */
    YMUX_PROTO_INPUT_PASTE = 19,        /* text bytes — bracketed-paste content; the
                                  * engine wraps it per the app's paste mode. */
    YMUX_PROTO_RPC_RELAY = 20,          /* [channel_id u32][raw ywire-RPC frame bytes] —
                                  * a transparent proxy of an in-pane ygui/ygreeter
                                  * yclass-RPC channel. BIDIRECTIONAL: daemon->client
                                  * carries the pane app's request bytes; client->
                                  * daemon carries yetty's response bytes. The client
                                  * pipes them to a dynamic channel on its own yetty
                                  * connection so yetty serves + renders the figure. */
    YMUX_PROTO_RPC_RELAY_CLOSE = 21,    /* [channel_id u32] — the proxied channel closed
                                      * on the sender's side; drop the pairing. */
    YMUX_PROTO_FIGURE_INPUT = 22,       /* [wire_code u32][raw client-input body] — input
                                      * (CLIENT_INPUT_FIGURE_MOUSE/KEY) yetty routed to a
                                      * PROXIED figure (ygui/ygreeter). client->daemon; the
                                      * daemon re-emits it as an OSC envelope on the pane
                                      * PTY so the app's ywire connection feeds its ygui. */
    YMUX_PROTO_VTSINK_PUBLISH = 23,     /* client->daemon, after WELCOME: [handle u64]
                                      * [feed_rid u32] — the client hosts a ymux:vtsink
                                      * object (#699.2) and publishes its RPC handle plus
                                      * the locally-resolved `feed` slot id. The daemon
                                      * builds an ASYNC yclass-RPC session over the vtsink
                                      * lane, seeds the slot id (zero admin round-trips),
                                      * and thereafter delivers terminal bytes as typed
                                      * ordered feed() calls instead of YMUX_PROTO_VT. */
    YMUX_PROTO_PANE_MODES = 26,         /* daemon->client, on change: [modes u32] — bit0 =
                                      * the app subscribes MOUSE (selection ownership
                                      * defers to the pane; review #13). */
    YMUX_PROTO_CHROME_RELEASE = 31,     /* daemon->client: copy-mode exited ('q') —
                                      * the bridge releases chrome input focus. */
    YMUX_PROTO_PASTE_BUFFER = 32,       /* client->daemon (verb): write the daemon's
                                      * copy-mode paste buffer into the pane. */
    YMUX_PROTO_OVERLAY_INPUT_ACK = 30,  /* daemon->client: [seq u32] — the overlay
                                        * event with this sequence is ACCEPTED
                                        * (owned by the daemon seat). The bridge
                                        * pops its source event only now. */
    YMUX_PROTO_OVERLAY_INPUT_NACK = 33, /* daemon->client: [seq u32][reason u32] —
                                         * the event with this sequence was REFUSED
                                         * (queue full / alloc). The sender still
                                         * owns it and resends the SAME sequence
                                         * (review #19). */
    YMUX_PROTO_TTY_RESPONSE = 29,       /* client->daemon: RAW terminal-response bytes
                                      * the per-attachment renderer's terminal
                                      * produced (answers to the RENDERER's own
                                      * queries — DA/DSR/CPR/...). Routed to THAT
                                      * attachment's response parser; never keyboard
                                      * input, never the application PTY. */
    YMUX_PROTO_OVERLAY_INPUT = 28,      /* client->daemon: one CONSUMED overlay input
                                      * event drained from the overlay scene's queue —
                                      * [class u32][payload bytes]. The daemon owns the
                                      * chrome (it publishes the overlay content), so
                                      * chrome interaction logic consumes here. */
    YMUX_PROTO_RECOVER = 27,            /* client->daemon (ops/debug): force slow-client
                                      * recovery NOW on every attached connection —
                                      * queued terminal frames drop, the vtsink epoch
                                      * resets. Drives attach-level reset-ordering
                                      * verification against a LIVE daemon. */
    YMUX_PROTO_VTSINK_RESET = 25,       /* daemon->client: the terminal-RPC EPOCH is
                                      * cancelled (slow-client recovery discarded the
                                      * queued feed stream). The client destroys its
                                      * hosted sink + lane state and RE-PUBLISHES; the
                                      * fresh epoch opens with a complete redraw. */
    YMUX_PROTO_VTSINK_RPC = 24,         /* BIDIRECTIONAL raw yclass-RPC frame bytes for the
                                      * vtsink lane: daemon->client carries feed() request
                                      * frames (the client dispatches and replies), client->
                                      * daemon carries the response frames (the daemon's
                                      * session pump completes the pipelined calls). One
                                      * lane per connection — no channel id. CONTROL-CLASS
                                      * in the tx-queue: never compaction-discarded (a
                                      * dropped response would desync the completion FIFO).
                                      * The ONE discard path is the slow-client EPOCH
                                      * RESET (VTSINK_RESET): the whole session is torn
                                      * down and re-published, so the FIFO cannot desync.
                                      * Ordinary throttling stays at the ack window. */

    /* Session management (tmux command semantics — the `ymux` CLI maps
     * new-session / list-sessions / has-session / kill-session /
     * rename-session / send-keys onto these). All are answered with a
     * SESSION_REPLY frame. Names are ≤63 byte strings. */
    YMUX_PROTO_SESSION_NEW = 10,    /* rows, cols, name_len, name bytes —
                                     * empty name = auto-number ("0","1",…);
                                     * errors when the name exists (tmux
                                     * "duplicate session") */
    YMUX_PROTO_SESSION_LIST = 11,   /* (empty) — reply text: one session
                                     * per line, tmux ls style */
    YMUX_PROTO_SESSION_HAS = 12,    /* name bytes */
    YMUX_PROTO_SESSION_KILL = 13,   /* name bytes (empty = most recent) */
    YMUX_PROTO_SESSION_RENAME = 14, /* old_len, old bytes, new bytes */
    YMUX_PROTO_SESSION_DETACH = 15, /* name bytes — detach every client of
                                     * the session (tmux detach-client -s) */
    YMUX_PROTO_SESSION_SEND_KEYS =
        16, /* name_len, name bytes, then (kind, value) u32 pairs: kind 0 =
             * codepoint via input_char, kind 1 = YETTY_YMUX_KEY_* via
             * input_key (tmux send-keys — no attachment needed) */

    /* daemon → client */
    YMUX_PROTO_WELCOME = 100, /* attachment_id, pane_id, permissions,
                                 * canonical rows, canonical cols */
    /* 101 was YMUX_PROTO_PAINT — the semantic cell-paint frame, retired by
     * #699.3 (text rides the vtsink lane; the surface class is gone). Do not
     * reuse the value while v5 peers may roam. */
    YMUX_PROTO_REFUSE = 102,      /* reason code (YMUX_PROTO_REFUSE_*) */
    YMUX_PROTO_PANE_EXIT = 103,   /* pane_id — the application ended */
    YMUX_PROTO_TRANSACTION = 104, /* one atomic RICH content transaction: the payload
              * is the rich-format body verbatim (#699.3 — the retired
              * semantic paint half has no wire representation). */

    /* Presentation effects — exactly-once (never replayed on reconnect).
     * Bell and title go to every attachment of the pane; clipboard goes
     * to the CONTROLLER only. */
    YMUX_PROTO_EFFECT_BELL = 105,      /* (empty) */
    YMUX_PROTO_EFFECT_TITLE = 106,     /* title bytes */
    YMUX_PROTO_EFFECT_CLIPBOARD = 107, /* target u32 (1 clipboard,
                                        * 0 primary), then text bytes */
    YMUX_PROTO_SESSION_REPLY = 108     /* status u32 (0 ok, else errno-ish),
                                        * then reply text bytes */
    /* 109 was YMUX_PROTO_VT — the bespoke framed VT byte path, retired by
     * #699.2: terminal bytes now ride the VTSINK_RPC lane as typed ordered
     * feed() calls. Do not reuse the value while v5 peers may roam. */
};

enum yetty_ymux_proto_refuse {
    YMUX_PROTO_REFUSE_VERSION = 1,
    YMUX_PROTO_REFUSE_UNKNOWN_PANE = 2,
    YMUX_PROTO_REFUSE_FULL = 3,
    YMUX_PROTO_REFUSE_NOT_PERMITTED = 4,
    YMUX_PROTO_REFUSE_BAD_FRAME = 5,
    YMUX_PROTO_REFUSE_NOT_ATTACHED = 6
};

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YMUX_PROTO_H */
