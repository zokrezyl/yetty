/*
 * yetty_yterm_osc_sm - Streaming OSC state machine with cursor-driven
 *                      decode and pause/resume semantics.
 *
 * Wire shape (matches yface's existing protocol):
 *
 *     ESC ] <decimal-code> ; <b64-args> ; <b64-payload> ESC \\
 *     ESC ] <decimal-code> ; <b64-args> ; <b64-payload> BEL
 *
 * Two-buffer model:
 *
 *   producer side  ─── feed(bytes, n) ───▶  ring buffer
 *                                              │
 *                                       cursor (read_pos)
 *                                              │
 *   consumer side  ◀── step()  ── envelope SM + b64 + LZ4F decode
 *                                              │
 *                                       on_payload(decoded, n) → pause?
 *
 * The cursor (read_pos) marks where the SM is currently processing. Bytes
 * between the cursor and the producer's write_pos are pending input. The
 * ring buffer grows on overflow (doubling realloc) so the producer never
 * blocks while the consumer is paused. When the consumer pauses (typically
 * because a layer wants to render the partial state before continuing),
 * the cursor stays put; codec state (b64 carry, LZ4F context, envelope
 * state) lives on the SM struct so a later step() resumes byte-for-byte.
 *
 * Codec is per-OSC-code:
 *   YETTY_YTERM_OSC_CODEC_NONE      raw bytes pass straight to on_payload
 *   YETTY_YTERM_OSC_CODEC_B64       streaming b64 decode
 *   YETTY_YTERM_OSC_CODEC_B64_LZ4   streaming b64 decode + LZ4F decompress
 *
 * Out-of-envelope bytes (RAW state) go to the on_raw handler — same shape
 * as yface.
 */

#ifndef YETTY_YTERM_OSC_STATEMACHINE_H
#define YETTY_YTERM_OSC_STATEMACHINE_H

#include <stddef.h>
#include <stdint.h>
#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yterm_osc_sm;

YETTY_YRESULT_DECLARE(yetty_yterm_osc_sm_ptr, struct yetty_yterm_osc_sm *);

enum yetty_yterm_osc_codec {
    YETTY_YTERM_OSC_CODEC_NONE = 0,    /* body bytes pass through verbatim */
    YETTY_YTERM_OSC_CODEC_B64 = 1,     /* streaming b64 decode */
    YETTY_YTERM_OSC_CODEC_B64_LZ4 = 2, /* streaming b64 decode + LZ4F */
};

/* Fired at the second `;` (end of args slot), once for each OSC envelope.
 * `args` points into SM-owned scratch valid only for this call. */
typedef void (*yetty_yterm_osc_on_begin_cb)(void *user, int code, const uint8_t *args,
                                            size_t args_len);

/* Fired as decoded body bytes become available. Caller may consume in place
 * — the pointer is into SM-owned scratch valid only for this call. Return
 * 0 to keep going, 1 to pause: the SM stops advancing the cursor and step()
 * returns. The cursor is preserved at the byte boundary right after the
 * batch that produced this chunk; call step() again to resume. */
typedef int (*yetty_yterm_osc_on_payload_cb)(void *user, const uint8_t *decoded, size_t n);

/* Fired at the envelope terminator (BEL or ESC \\). The codec is finalised
 * (LZ4F context reset, b64 carry drained) before this fires. */
typedef void (*yetty_yterm_osc_on_end_cb)(void *user, int code);

/* Fired for any byte stream outside an OSC envelope. */
typedef void (*yetty_yterm_osc_on_raw_cb)(void *user, const char *bytes, size_t n);

struct yetty_yterm_osc_sm_ptr_result yetty_yterm_osc_sm_create(void);

void yetty_yterm_osc_sm_destroy(struct yetty_yterm_osc_sm *sm);

/* Register a handler for one OSC code. Subsequent envelopes whose decimal
 * code matches will route to this handler with the configured codec.
 * Re-registering the same code overwrites the previous handler. Pass NULL
 * for callbacks you don't care about. */
struct yetty_ycore_void_result yetty_yterm_osc_sm_register(
    struct yetty_yterm_osc_sm *sm, int code, enum yetty_yterm_osc_codec codec,
    yetty_yterm_osc_on_begin_cb on_begin, yetty_yterm_osc_on_payload_cb on_payload,
    yetty_yterm_osc_on_end_cb on_end, void *user);

/* Set the handler for bytes outside any OSC envelope. */
void yetty_yterm_osc_sm_set_raw_handler(struct yetty_yterm_osc_sm *sm,
                                        yetty_yterm_osc_on_raw_cb on_raw, void *user);

/* Append bytes to the input ring. Grows on overflow. Doesn't process. */
struct yetty_ycore_void_result yetty_yterm_osc_sm_feed(struct yetty_yterm_osc_sm *sm,
                                                       const char *bytes, size_t n);

/* Advance the cursor through pending input. Returns when:
 *   - the cursor catches the producer (no more bytes), or
 *   - an on_payload callback returned 1 (pause).
 *
 * Calling step() repeatedly is idempotent when no new input has arrived. */
struct yetty_ycore_void_result yetty_yterm_osc_sm_step(struct yetty_yterm_osc_sm *sm);

/* True iff the last step() returned because an on_payload callback paused.
 * Cleared on the next step(). Useful for debug / driver code. */
int yetty_yterm_osc_sm_paused(const struct yetty_yterm_osc_sm *sm);

/* Bytes currently sitting in the ring waiting to be processed. */
size_t yetty_yterm_osc_sm_pending(const struct yetty_yterm_osc_sm *sm);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YTERM_OSC_STATEMACHINE_H */
