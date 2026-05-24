/*
 * yetty_ywire_wire_statemachine — envelope framer + decode stack + dispatcher.
 *
 * Two envelope kinds are recognised, both ECMA-48 control strings:
 *
 *   OSC: ESC ] <decimal-code> ; <b64-args> ; <b64-payload> (BEL | ESC \)
 *   DCS: ESC P <decimal-code> ; <b64-args> ; <b64-payload> ESC \
 *
 * The inside is identical between OSC and DCS — only the opener byte
 * (kind) differs. We pick a kind per producer envelope; the framer
 * dispatches a registered handler by (kind, code) pair.
 *
 * Pipeline:
 *
 *   PTY  →  ring  →  framer  →  [b64]  →  [lz4]  →  read()  →  handler
 *
 * Handlers register a (process_input_fn, userdata) pair against a
 * (kind, code) tuple. Handlers have zero knowledge of b64 / lz4 —
 * codec is fixed by protocol (args = b64, payload = b64+lz4).
 *
 * Byte source: the SM holds the PTY pointer it was created with and
 * pulls bytes itself via pty->ops->read inside process(). There is no
 * external feed function. Async-delivery platforms (libuv) are expected
 * to buffer inside the PTY abstraction so ops->read returns when called.
 *
 * Mid-envelope return: a handler's process_input_fn may return at any
 * byte boundary. The SM keeps current handler + scan position + decode
 * state across calls; the next process() cycle re-dispatches the same
 * handler and it resumes pulling bytes seamlessly.
 */
#ifndef YETTY_YTERM_OSC_STATEMACHINE_H
#define YETTY_YTERM_OSC_STATEMACHINE_H

#include <stddef.h>
#include <stdint.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ywire_wire_statemachine;
struct yetty_platform_pty;

YETTY_YRESULT_DECLARE(yetty_ywire_wire_statemachine_ptr, struct yetty_ywire_wire_statemachine *);

/*
 * Envelope kind. Value is the opener byte that follows ESC, which is
 * also what the framer looks for in SCAN_AFTER_ESC — the enum value
 * does double duty as the wire-byte literal.
 */
enum yetty_ywire_envelope_kind {
    YETTY_YWIRE_ENVELOPE_OSC = ']', /* ESC ]  …  (BEL | ST) */
    YETTY_YWIRE_ENVELOPE_DCS = 'P', /* ESC P  …  ST          */
};

/*
 * Handler callback. Called by the SM on its persistent per-handler
 * coroutine. Expected to loop forever (envelope after envelope, or raw
 * bytes forever for the default sink); a return is treated as fatal /
 * one-shot and surfaced. Inside the call, the handler pulls decoded
 * bytes via yetty_ywire_wire_statemachine_read and queries envelope
 * metadata via the accessors below.
 */
typedef struct yetty_ycore_void_result (*yetty_ywire_process_input_fn)(
    void *userdata, struct yetty_ywire_wire_statemachine *sm);

/*
 * Construct the SM. Stores `pty` as a non-owning pointer; the SM uses
 * it as its only byte source via pty->ops->read. Lazy init for the ring
 * and decode stack. Called once at terminal startup.
 */
struct yetty_ywire_wire_statemachine_ptr_result yetty_ywire_wire_statemachine_create(
    struct yetty_platform_pty *pty);

/*
 * Destroy. Frees the ring, decode-stack state (b64 carry, LZ4F context,
 * args + output carry), and the per-handler registry. Does NOT touch
 * the PTY (non-owning) or any registered handler userdata (also
 * non-owning).
 */
struct yetty_ycore_void_result yetty_ywire_wire_statemachine_destroy(
    struct yetty_ywire_wire_statemachine *sm);

/*
 * Bind (kind, code) → (fn, userdata). When the framer recognises
 * ESC <kind> <code> ; <args> ;, the SM dispatches fn(userdata, sm).
 * Re-registering the same (kind, code) overwrites. No codec parameter
 * — the decode pipeline is fixed by protocol (args = b64, payload =
 * b64 + lz4).
 *
 * Coroutine dedup: callers that re-use the same `userdata` pointer
 * across multiple registrations share one persistent coroutine — the
 * fn passed on the *first* registration is the one that's spawned, so
 * be consistent.
 */
struct yetty_ycore_void_result yetty_ywire_wire_statemachine_register(
    struct yetty_ywire_wire_statemachine *sm, enum yetty_ywire_envelope_kind kind, int code,
    yetty_ywire_process_input_fn fn, void *userdata);

/*
 * Bind the default sink for bytes outside any envelope (raw passthrough
 * — terminal text into vterm). Same dispatch contract as register(),
 * but fires while the framer is in SCAN_RAW. There is no decode stack
 * on this path; bytes the handler reads via _read are wire bytes
 * verbatim.
 */
struct yetty_ycore_void_result yetty_ywire_wire_statemachine_set_default(
    struct yetty_ywire_wire_statemachine *sm, yetty_ywire_process_input_fn fn, void *userdata);

/*
 * Platform integration only — async-delivery PTY backends (libuv) push
 * bytes here from their read callback because the SM cannot pull them
 * synchronously. Sync-read backends never call this. Bytes go straight
 * into the SM's input ring; processing happens on the next process()
 * call.
 *
 * NOT a handler-facing API.
 */
struct yetty_ycore_void_result yetty_ywire_wire_statemachine_feed(
    struct yetty_ywire_wire_statemachine *sm, const char *bytes, size_t n);

/*
 * Drive the SM. Reads PTY bytes into the ring (via pty->ops->read for
 * sync-read backends; otherwise the bytes are already there from
 * platform feed), advances the framer scanner, and resumes per-handler
 * coros zero or more times. Returns when the ring is drained or a
 * handler voluntarily returned mid-envelope.
 */
struct yetty_ycore_void_result yetty_ywire_wire_statemachine_process(
    struct yetty_ywire_wire_statemachine *sm);

/*
 * Pull up to `n` DECODED bytes from the SM into `dst`. Returns the
 * number of bytes actually copied. The SM reads as much wire as needed,
 * runs the bytes through the decode stack (b64 + lz4 for payload, no
 * decode for raw), copies decoded bytes out, and stashes any decoder
 * excess in its output carry for the next call.
 *
 * Stops at the envelope terminator without consuming it: from then on
 * returns 0 with at_end() == true until the handler returns.
 *
 * Called from inside a handler's process_input_fn (and only there).
 */
struct yetty_ycore_size_result yetty_ywire_wire_statemachine_read(
    struct yetty_ywire_wire_statemachine *sm, uint8_t *dst, size_t n);

/*
 * View into the decoded args slot for the envelope currently being
 * dispatched (filled when the framer crosses the second `;`). Returns
 * (NULL, 0) outside an envelope dispatch or before the args slot is
 * ready.
 *
 * Args are tiny by protocol (e.g. yetty_yface_bin_meta is 32 B), so
 * the SM holds them in a small fixed buffer and exposes a pointer
 * view.
 */
struct yetty_ywire_wire_statemachine_args {
    const uint8_t *bytes;
    size_t len;
};
struct yetty_ywire_wire_statemachine_args yetty_ywire_wire_statemachine_args(
    const struct yetty_ywire_wire_statemachine *sm);

/*
 * True iff the framer has reached the envelope terminator (BEL or ST)
 * for the envelope currently being dispatched. After this flips,
 * _read returns 0 for the rest of this dispatch.
 *
 * Always 0 outside an envelope dispatch.
 */
int yetty_ywire_wire_statemachine_at_end(const struct yetty_ywire_wire_statemachine *sm);

/*
 * Return the code (e.g. 600001 for ydraw BIN) of the envelope currently
 * being dispatched. 0 outside any envelope dispatch.
 *
 * Used by handlers that own multiple codes — ydraw-layer is registered
 * for CLEAR/BIN/OVERLAY and dispatches by this value.
 */
int yetty_ywire_wire_statemachine_code(const struct yetty_ywire_wire_statemachine *sm);

/*
 * Return the kind of the envelope currently being dispatched. Returns
 * a value not equal to either YETTY_YWIRE_ENVELOPE_OSC or
 * YETTY_YWIRE_ENVELOPE_DCS outside any envelope dispatch.
 */
enum yetty_ywire_envelope_kind yetty_ywire_wire_statemachine_kind(
    const struct yetty_ywire_wire_statemachine *sm);

/*===========================================================================
 * Rolling envelope traffic stats — count + decoded body bytes
 *
 * The SM keeps a 60-bucket ring (one bucket per epoch-second) and
 * updates it whenever an OSC/DCS envelope completes (terminator seen).
 * Each bucket records (count, decoded_body_bytes); the snapshot
 * accessor recomputes 1s / 10s / 60s windows from the ring on demand.
 *
 * "Decoded body bytes" = bytes that crossed out into out_carry for the
 * handler to read (post b64, post LZ4F decompression). Wire framing,
 * envelope kind/code, args slot, and the terminator are NOT counted.
 *
 * Idle terminals: stale buckets age out naturally — once a bucket's
 * epoch_second is older than the snapshot's window, it's skipped.
 *
 * The framer also emits one ydebug line per second of traffic — the
 * same snapshot, on a ~1Hz heartbeat. Silent when no envelopes flow.
 *=========================================================================*/

struct yetty_ywire_stats_snapshot {
    uint64_t count_1s;
    uint64_t count_10s;
    uint64_t count_60s;
    /* Decoded body bytes seen in the same windows. Average bytes per
     * envelope = bytes_Ns / max(1, count_Ns) — caller computes; we
     * don't divide here to keep the snapshot lossless. */
    uint64_t bytes_1s;
    uint64_t bytes_10s;
    uint64_t bytes_60s;
};

struct yetty_ywire_stats_snapshot yetty_ywire_wire_statemachine_stats_snapshot(
    const struct yetty_ywire_wire_statemachine *sm);

/*===========================================================================
 * Catch-all envelope handler
 *
 * Fires for any (kind, code) pair NOT covered by a specific register().
 * Used by callers that consume every envelope regardless of code
 * (yface's scanner, debug tools). Pull semantics — same contract as
 * register(): the fn must loop forever, may read decoded bytes via
 * _read, queries kind/code via the accessors.
 *=========================================================================*/
struct yetty_ycore_void_result yetty_ywire_wire_statemachine_set_envelope_default(
    struct yetty_ywire_wire_statemachine *sm, yetty_ywire_process_input_fn fn, void *userdata);

/*===========================================================================
 * Buffered (push-callback) reader — sugar over the pull API
 *
 * Each variant registers an internal pull-handler that drains the
 * envelope body into a heap buffer and fires the user's callback ONCE
 * per envelope with everything pre-decoded. For callers that don't
 * want to write a coroutine.
 *=========================================================================*/

/* args may be NULL/0 when the envelope has no args slot. */
typedef struct yetty_ycore_void_result (*yetty_ywire_envelope_cb)(
    void *userdata, enum yetty_ywire_envelope_kind kind, int code, const uint8_t *args,
    size_t args_len, const uint8_t *payload, size_t payload_len);

/* on_raw fires for runs of raw bytes outside any envelope. n is the
 * length of the contiguous run; the buffer is the SM's decode-stack
 * scratch and is only valid for the duration of the callback. */
typedef struct yetty_ycore_void_result (*yetty_ywire_raw_cb)(void *userdata, const uint8_t *bytes,
                                                             size_t n);

struct yetty_ycore_void_result yetty_ywire_wire_statemachine_register_buffered(
    struct yetty_ywire_wire_statemachine *sm, enum yetty_ywire_envelope_kind kind, int code,
    yetty_ywire_envelope_cb cb, void *userdata);

struct yetty_ycore_void_result yetty_ywire_wire_statemachine_set_envelope_default_buffered(
    struct yetty_ywire_wire_statemachine *sm, yetty_ywire_envelope_cb cb, void *userdata);

struct yetty_ycore_void_result yetty_ywire_wire_statemachine_set_default_buffered(
    struct yetty_ywire_wire_statemachine *sm, yetty_ywire_raw_cb cb, void *userdata);

/*===========================================================================
 * Streaming write — for producers that emit many envelopes
 *
 * The encoder state (b64 carry, LZ4F context) lives on the SM and is
 * re-used across envelopes. Bytes go into `out_buf` (caller-owned;
 * the SM borrows the pointer for the duration of the write). The same
 * SM can drive read + write in either direction independently — the
 * scanner state and the encoder state don't share buffers.
 *
 * Lifecycle: start_write → write* → finish_write. Only one write may
 * be active on a given SM at a time.
 *=========================================================================*/

/* Begin emitting an envelope. Appends "ESC <kind> <code> ; <b64-args> ;"
 * into out_buf, opens an LZ4F frame if compressed=1, and primes the
 * streaming b64 encoder for the body. */
struct yetty_ycore_void_result yetty_ywire_wire_statemachine_start_write(
    struct yetty_ywire_wire_statemachine *sm, enum yetty_ywire_envelope_kind kind, int code,
    int compressed, const void *args, size_t args_len, struct yetty_ycore_buffer *out_buf);

/* Push body bytes through the active write codec. Zero-byte calls are
 * a no-op; the encoder may emit zero output bytes (LZ4F batches up to
 * its block size). */
struct yetty_ycore_void_result yetty_ywire_wire_statemachine_write(
    struct yetty_ywire_wire_statemachine *sm, const void *src, size_t len);

/* Flush LZ4F (if active) + the b64 tail, then append the envelope
 * terminator (ESC \\). out_buf now holds a complete envelope. */
struct yetty_ycore_void_result yetty_ywire_wire_statemachine_finish_write(
    struct yetty_ywire_wire_statemachine *sm);

/*===========================================================================
 * One-shot helpers — no SM required, no streaming state retained
 *
 * Internally spin a transient SM, do the work, tear it down. Use the
 * streaming API above when emitting many envelopes back-to-back.
 *=========================================================================*/

/* Append a complete envelope ("ESC <kind> <code> ; <b64-args> ;
 * <b64+lz4 body> ESC \\") to out_buf. */
struct yetty_ycore_void_result yetty_ywire_emit(enum yetty_ywire_envelope_kind kind, int code,
                                                int compressed, const void *args, size_t args_len,
                                                const void *body, size_t body_len,
                                                struct yetty_ycore_buffer *out_buf);

/* Same, but blocking write(2) straight to fd. */
struct yetty_ycore_void_result yetty_ywire_emit_to_fd(int fd, enum yetty_ywire_envelope_kind kind,
                                                      int code, int compressed, const void *args,
                                                      size_t args_len, const void *body,
                                                      size_t body_len);

/* Decode a b64 (optionally LZ4F-compressed) body — the bytes between
 * the second `;` and the trailing terminator — into out_buf. */
struct yetty_ycore_void_result yetty_ywire_decode(const char *b64, size_t n, int compressed,
                                                  struct yetty_ycore_buffer *out_buf);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YTERM_OSC_STATEMACHINE_H */
