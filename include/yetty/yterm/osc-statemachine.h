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
 *   producer side  ─── feed / read_pty ───▶  ring buffer
 *                                                │
 *                                         cursor (read_pos)
 *                                                │
 *   consumer side  ◀── process()  ── envelope SM + b64 + LZ4F decode
 *                                                │
 *                                          on_envelope(user, sm)
 *                                          on_raw(user, sm)
 *
 * The cursor (read_pos) marks where the SM is currently processing. Bytes
 * between the cursor and the producer's write_pos are pending input. The
 * ring buffer grows on overflow (doubling realloc) so the producer never
 * blocks while the consumer is stalled. State (b64 carry, LZ4F context,
 * envelope state, accumulated payload) lives on the SM struct so the SM
 * can be paused at any byte boundary and resumed later (the streaming
 * payload hook returning 1 sets the pause).
 *
 * Codec is per-OSC-code:
 *   YETTY_YTERM_OSC_CODEC_NONE      raw bytes pass straight to the consumer
 *   YETTY_YTERM_OSC_CODEC_B64       streaming b64 decode
 *   YETTY_YTERM_OSC_CODEC_B64_LZ4   streaming b64 decode + LZ4F
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
struct yetty_platform_pty;

YETTY_YRESULT_DECLARE(yetty_yterm_osc_sm_ptr, struct yetty_yterm_osc_sm *);

enum yetty_yterm_osc_codec {
    YETTY_YTERM_OSC_CODEC_NONE = 0,    /* body bytes pass through verbatim */
    YETTY_YTERM_OSC_CODEC_B64 = 1,     /* streaming b64 decode */
    YETTY_YTERM_OSC_CODEC_B64_LZ4 = 2, /* streaming b64 decode + LZ4F */
};

/* View into the SM's current envelope. Valid only while an on_envelope
 * callback is on the stack. Pulled via yetty_yterm_osc_sm_envelope(). */
struct yetty_yterm_osc_sm_envelope {
    int code;
    const uint8_t *args;
    size_t args_len;
    const uint8_t *payload;
    size_t payload_len;
};

/* View into the SM's current out-of-envelope byte span. Valid only while
 * an on_raw callback is on the stack. Pulled via yetty_yterm_osc_sm_raw(). */
struct yetty_yterm_osc_sm_raw {
    const char *bytes;
    size_t len;
};

YETTY_YRESULT_DECLARE(yetty_yterm_osc_sm_envelope, struct yetty_yterm_osc_sm_envelope);
YETTY_YRESULT_DECLARE(yetty_yterm_osc_sm_raw, struct yetty_yterm_osc_sm_raw);

/* Fired once per complete envelope at the terminator. Pull args/payload
 * via yetty_yterm_osc_sm_envelope(sm). */
typedef struct yetty_ycore_void_result (*yetty_yterm_osc_on_envelope_cb)(
    void *user, struct yetty_yterm_osc_sm *sm);

/* Fired for byte spans outside an OSC envelope. Pull bytes via
 * yetty_yterm_osc_sm_raw(sm). */
typedef struct yetty_ycore_void_result (*yetty_yterm_osc_on_raw_cb)(
    void *user, struct yetty_yterm_osc_sm *sm);

/*-----------------------------------------------------------------------------
 * Lifecycle
 *---------------------------------------------------------------------------*/

struct yetty_yterm_osc_sm_ptr_result yetty_yterm_osc_sm_create(void);

struct yetty_ycore_void_result yetty_yterm_osc_sm_destroy(struct yetty_yterm_osc_sm *sm);

/*-----------------------------------------------------------------------------
 * Registration
 *---------------------------------------------------------------------------*/

/* Register a handler for one OSC code. Re-registering overwrites. */
struct yetty_ycore_void_result yetty_yterm_osc_sm_register(
    struct yetty_yterm_osc_sm *sm, int code, enum yetty_yterm_osc_codec codec,
    yetty_yterm_osc_on_envelope_cb on_envelope, void *user);

/* Set the handler for bytes outside any OSC envelope. */
struct yetty_ycore_void_result yetty_yterm_osc_sm_set_raw_handler(
    struct yetty_yterm_osc_sm *sm, yetty_yterm_osc_on_raw_cb on_raw, void *user);

/*-----------------------------------------------------------------------------
 * Input
 *---------------------------------------------------------------------------*/

/* Append bytes to the input ring. Grows on overflow. Doesn't process.
 * For pty input, prefer read_pty() — it avoids the intermediate copy. */
struct yetty_ycore_void_result yetty_yterm_osc_sm_feed(struct yetty_yterm_osc_sm *sm,
                                                       const char *bytes, size_t n);

/* Read directly from `pty` into the ring's writable region — zero-copy.
 * Returns the number of bytes read; 0 on EAGAIN/EOF. Doesn't process. */
struct yetty_ycore_size_result yetty_yterm_osc_sm_read_pty(struct yetty_yterm_osc_sm *sm,
                                                           struct yetty_platform_pty *pty);

/*-----------------------------------------------------------------------------
 * Drive
 *---------------------------------------------------------------------------*/

/* Advance the cursor through pending input. Returns when the cursor
 * catches the producer (no more bytes). */
struct yetty_ycore_void_result yetty_yterm_osc_sm_process(struct yetty_yterm_osc_sm *sm);

/*-----------------------------------------------------------------------------
 * Pull-mode access — only valid inside on_envelope / on_raw callbacks.
 * Errors when called outside a callback (no current view).
 *---------------------------------------------------------------------------*/

struct yetty_yterm_osc_sm_envelope_result yetty_yterm_osc_sm_envelope(
    const struct yetty_yterm_osc_sm *sm);

struct yetty_yterm_osc_sm_raw_result yetty_yterm_osc_sm_raw(
    const struct yetty_yterm_osc_sm *sm);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YTERM_OSC_STATEMACHINE_H */
