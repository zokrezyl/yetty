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

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YTERM_OSC_STATEMACHINE_H */
