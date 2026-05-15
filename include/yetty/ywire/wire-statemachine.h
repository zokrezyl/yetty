/*
 * yetty_ywire_wire_statemachine — OSC framer + decode stack + dispatcher.
 *
 * Wire shape:
 *
 *     ESC ] <decimal-code> ; <b64-args> ; <b64-payload> ESC \\
 *     ESC ] <decimal-code> ; <b64-args> ; <b64-payload> BEL
 *
 * Pipeline:
 *
 *   PTY  →  ring  →  framer  →  [b64]  →  [lz4]  →  read()  →  layer
 *
 * The SM owns the full decode stack. Layers register against codes and
 * pull decoded bytes via osc_statemachine_read(). Layers have zero
 * knowledge of b64 / lz4 — codec is fixed by protocol (args = b64,
 * payload = b64+lz4).
 *
 * Byte source: the SM holds the PTY pointer it was created with and
 * pulls bytes itself via pty->ops->read inside process(). There is no
 * external feed function. Async-delivery platforms (libuv) are expected
 * to buffer inside the PTY abstraction so ops->read returns when called.
 *
 * Mid-envelope return: the layer's process_input may return at any byte
 * boundary. The SM keeps current_layer + scan position + decode state
 * across calls; the next process() cycle re-dispatches the same layer
 * and the layer resumes pulling bytes seamlessly. This is what enables
 * scroll-yielding in ydraw-layer.
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
struct yetty_yrender_terminal_layer;

YETTY_YRESULT_DECLARE(yetty_ywire_wire_statemachine_ptr,
                     struct yetty_ywire_wire_statemachine *);

/*
 * Construct the SM. Stores `pty` as a non-owning pointer; the SM uses
 * it as its only byte source via pty->ops->read. Lazy init for the ring
 * and decode stack. Called once at terminal startup.
 */
struct yetty_ywire_wire_statemachine_ptr_result
yetty_ywire_wire_statemachine_create(struct yetty_platform_pty *pty);

/*
 * Destroy. Frees the ring, decode-stack state (b64 carry, LZ4F context,
 * args + output carry), and the per-code registry. Does NOT touch the
 * PTY (non-owning) or any registered layers (also non-owning).
 */
struct yetty_ycore_void_result yetty_ywire_wire_statemachine_destroy(
    struct yetty_ywire_wire_statemachine *osc_statemachine);

/*
 * Bind `layer` to OSC `code`. When the framer recognises ESC ] code ;
 * <args> ;, the SM sets current_layer = layer and calls
 * layer->ops->process_input(layer, osc_statemachine). Re-registering
 * the same code overwrites. No codec parameter — the decode pipeline
 * is fixed by protocol (args = b64, payload = b64 + lz4).
 */
struct yetty_ycore_void_result yetty_ywire_wire_statemachine_register(
    struct yetty_ywire_wire_statemachine *osc_statemachine, int code,
    struct yetty_yrender_terminal_layer *layer);

/*
 * Bind the layer that consumes bytes outside any OSC envelope (raw
 * passthrough — terminal text into vterm). Same dispatch contract as
 * register(), but its process_input fires while the framer is in
 * SCAN_RAW. There is no decode stack on this path; bytes the layer
 * reads via osc_statemachine_read are wire bytes verbatim.
 */
struct yetty_ycore_void_result yetty_ywire_wire_statemachine_set_default(
    struct yetty_ywire_wire_statemachine *osc_statemachine,
    struct yetty_yrender_terminal_layer *layer);

/*
 * Platform integration only — async-delivery PTY backends (libuv) push
 * bytes here from their read callback because the SM cannot pull them
 * synchronously. Sync-read backends never call this. Bytes go straight
 * into the SM's input ring; processing happens on the next process()
 * call.
 *
 * NOT a layer-facing API.
 */
struct yetty_ycore_void_result yetty_ywire_wire_statemachine_feed(
    struct yetty_ywire_wire_statemachine *osc_statemachine,
    const char *bytes, size_t n);

/*
 * Drive the SM. Reads PTY bytes into the ring (via pty->ops->read for
 * sync-read backends; otherwise the bytes are already there from
 * platform feed), advances the framer scanner, and calls
 * layer->ops->process_input zero or more times. Returns when the ring
 * is drained or a layer voluntarily returned mid-envelope.
 *
 * Called by the terminal's event loop whenever the PTY pipe signals
 * readability, and again after every render frame to drain residual
 * bytes that arrived while we were rendering.
 */
struct yetty_ycore_void_result yetty_ywire_wire_statemachine_process(
    struct yetty_ywire_wire_statemachine *osc_statemachine);

/*
 * Pull up to `n` DECODED bytes from the SM into `dst`. Returns the
 * number of bytes actually copied. The SM reads as much wire as needed,
 * runs the bytes through the decode stack (b64 + lz4 for payload, no
 * decode for raw), copies decoded bytes out, and stashes any decoder
 * excess in its output carry for the next call.
 *
 * Stops at the envelope terminator without consuming it: from then on
 * returns 0 with at_end() == true until the layer's process_input
 * returns.
 *
 * Called from inside layer->ops->process_input (and only there).
 */
struct yetty_ycore_size_result yetty_ywire_wire_statemachine_read(
    struct yetty_ywire_wire_statemachine *osc_statemachine, uint8_t *dst, size_t n);

/*
 * View into the decoded args slot for the envelope currently being
 * dispatched (filled when the framer crosses the second `;`). Returns
 * (NULL, 0) outside an OSC dispatch or before the args slot is ready.
 *
 * Args are tiny by protocol (e.g. yetty_yface_bin_meta is 32 B), so
 * the SM holds them in a small fixed buffer and exposes a pointer
 * view. Used by ydraw-layer / ymgui-layer to read meta headers.
 */
struct yetty_ywire_wire_statemachine_args {
    const uint8_t *bytes;
    size_t len;
};
struct yetty_ywire_wire_statemachine_args yetty_ywire_wire_statemachine_args(
    const struct yetty_ywire_wire_statemachine *osc_statemachine);

/*
 * True iff the framer has reached the envelope terminator (BEL or
 * ESC \\) for the envelope currently being dispatched. After this
 * flips, osc_statemachine_read returns 0 for the rest of this dispatch.
 * Layers use it as the "envelope ended, finalise" signal.
 *
 * Always 0 outside an OSC dispatch.
 */
int yetty_ywire_wire_statemachine_at_end(
    const struct yetty_ywire_wire_statemachine *osc_statemachine);

/*
 * Return the OSC code (e.g. 600001 for ydraw BIN) of the envelope
 * currently being dispatched. 0 outside any OSC dispatch.
 *
 * Used by layers that own multiple codes — ydraw-layer is registered
 * for CLEAR/BIN/YAML/OVERLAY and dispatches by this value.
 */
int yetty_ywire_wire_statemachine_code(
    const struct yetty_ywire_wire_statemachine *osc_statemachine);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YTERM_OSC_STATEMACHINE_H */
