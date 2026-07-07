/*
 * yetty/yface/yface.c — semantic / message-type layer.
 *
 * Wire mechanics (envelope framing, b64, LZ4F) live in ywire; this
 * file is a thin wrapper that holds a ywire SM internally and provides
 * the higher-level API its consumers expect. The struct fields and
 * function names match the legacy yface shape so existing callers keep
 * compiling unchanged.
 *
 * Long-term direction: typed helpers for the structured message kinds
 * (mouse, resize, key, focus, …) move in here as separate emit/decode
 * pairs that wrap yetty_ywire_emit + yetty_ywire_decode. The codec
 * itself never reappears in this file.
 */

#include <yetty/yface/yface.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ywire/wire-statemachine.h>

struct yetty_yface {
    struct yetty_ycore_buffer in_buf;
    struct yetty_ycore_buffer out_buf;

    /* The wire machinery — owns framer, codec, encoder state. Created
     * lazily on first use that needs it (start_write or feed_bytes). */
    struct yetty_ywire_wire_statemachine *sm;

    /* Streaming-write state — what kind/code/compressed the caller
     * opened with. Tracked so finish_write doesn't need the caller to
     * repeat them. */
    int write_active;

    /* set_handlers callbacks. We register an envelope-default and a
     * raw-default with the SM that call these. */
    yetty_yface_msg_cb on_osc;
    yetty_yface_raw_cb on_raw;
    void *handler_user;
    int handlers_attached; /* SM-side buffered handlers installed */

    /* start_read / feed / finish_read state — accumulates b64 chars
     * between feed calls, decodes at finish_read via yetty_ywire_decode.
     * No streaming through the codec here; the API was rarely used and
     * keeping the local b64+lz4 state for one tool isn't worth the
     * duplication. */
    struct yetty_ycore_buffer dec_b64_accum;
    int dec_compressed;
    int dec_active;
};

/*---------------------------------------------------------------------------
 * Lifecycle
 *-------------------------------------------------------------------------*/

struct yetty_yface_ptr_result yetty_yface_create(void)
{
    struct yetty_yface *y = calloc(1, sizeof(*y));
    if (!y) {
        return YETTY_ERR(yetty_yface_ptr, "yface: calloc failed");
    }
    return YETTY_OK(yetty_yface_ptr, y);
}

struct yetty_ycore_void_result yetty_yface_destroy(struct yetty_yface *y)
{
    if (!y) {
        return YETTY_OK_VOID();
    }
    /* Best-effort teardown: run every step, stash the first error, free the
     * yface regardless, and surface the error at the end. */
    struct yetty_ycore_void_result first_err = YETTY_OK_VOID();
    if (y->sm) {
        struct yetty_ycore_void_result dr = yetty_ywire_wire_statemachine_destroy(y->sm);
        if (YETTY_IS_ERR(dr)) {
            first_err = YETTY_ERR(yetty_ycore_void, "yface: SM destroy", dr);
        }
        y->sm = NULL;
    }
    yetty_ycore_buffer_destroy(&y->in_buf);
    yetty_ycore_buffer_destroy(&y->out_buf);
    yetty_ycore_buffer_destroy(&y->dec_b64_accum);
    free(y);
    return first_err;
}

struct yetty_ycore_buffer *yetty_yface_in_buf(struct yetty_yface *y)
{
    return y ? &y->in_buf : NULL;
}

struct yetty_ycore_buffer *yetty_yface_out_buf(struct yetty_yface *y)
{
    return y ? &y->out_buf : NULL;
}

/*---------------------------------------------------------------------------
 * Lazy SM ownership
 *-------------------------------------------------------------------------*/

static struct yetty_ycore_void_result ensure_sm(struct yetty_yface *y)
{
    if (y->sm) {
        return YETTY_OK_VOID();
    }
    struct yetty_ywire_wire_statemachine_ptr_result sr = yetty_ywire_wire_statemachine_create(NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "yface: SM create");
    y->sm = sr.value;
    return YETTY_OK_VOID();
}

/*---------------------------------------------------------------------------
 * Outgoing — streaming
 *-------------------------------------------------------------------------*/

struct yetty_ycore_void_result yetty_yface_start_write(struct yetty_yface *y,
                                                       enum yetty_ywire_envelope_kind kind,
                                                       int wire_code, int compressed,
                                                       const void *args, size_t args_len)
{
    if (!y) {
        return YETTY_ERR(yetty_ycore_void, "yface: yface is NULL");
    }
    if (y->write_active) {
        return YETTY_ERR(yetty_ycore_void, "yface: write already active");
    }
    {
        struct yetty_ycore_void_result r = ensure_sm(y);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "yface: start_write: ensure_sm");
    }
    struct yetty_ycore_void_result r = yetty_ywire_wire_statemachine_start_write(
        y->sm, kind, wire_code, /*has_args=*/1, compressed, args, args_len, &y->out_buf);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "yface: start_write");
    y->write_active = 1;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yface_write(struct yetty_yface *y, const void *src, size_t len)
{
    if (!y) {
        return YETTY_ERR(yetty_ycore_void, "yface: yface is NULL");
    }
    if (!y->write_active) {
        return YETTY_ERR(yetty_ycore_void, "yface: write outside frame");
    }
    struct yetty_ycore_void_result r = yetty_ywire_wire_statemachine_write(y->sm, src, len);
    if (YETTY_IS_ERR(r)) {
        /* The SM aborted the envelope and reset its encoder — mirror that
         * here so the yface can start a fresh write. */
        y->write_active = 0;
    }
    return r;
}

struct yetty_ycore_void_result yetty_yface_finish_write(struct yetty_yface *y)
{
    if (!y) {
        return YETTY_ERR(yetty_ycore_void, "yface: yface is NULL");
    }
    if (!y->write_active) {
        return YETTY_ERR(yetty_ycore_void, "yface: no active write");
    }
    struct yetty_ycore_void_result r = yetty_ywire_wire_statemachine_finish_write(y->sm);
    y->write_active = 0;
    return r;
}

/*---------------------------------------------------------------------------
 * Incoming — body-only streaming (start_read / feed / finish_read).
 *
 * Buffer the b64 chars between feed() calls; decode once via
 * yetty_ywire_decode in finish_read. Functionally equivalent to the
 * previous streaming implementation; trades incremental decode for
 * one shared codec.
 *-------------------------------------------------------------------------*/

struct yetty_ycore_void_result yetty_yface_start_read(struct yetty_yface *y, int compressed)
{
    if (!y) {
        return YETTY_ERR(yetty_ycore_void, "yface: yface is NULL");
    }
    if (y->dec_active) {
        return YETTY_ERR(yetty_ycore_void, "yface: read already active");
    }
    yetty_ycore_buffer_clear(&y->in_buf);
    yetty_ycore_buffer_clear(&y->dec_b64_accum);
    y->dec_compressed = compressed ? 1 : 0;
    y->dec_active = 1;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yface_feed(struct yetty_yface *y, const char *b64, size_t n)
{
    if (!y) {
        return YETTY_ERR(yetty_ycore_void, "yface: yface is NULL");
    }
    if (!y->dec_active) {
        return YETTY_ERR(yetty_ycore_void, "yface: feed outside read");
    }
    if (n == 0) {
        return YETTY_OK_VOID();
    }
    if (!b64) {
        return YETTY_ERR(yetty_ycore_void, "yface: feed: b64 is NULL");
    }
    return yetty_ycore_buffer_write(&y->dec_b64_accum, b64, n);
}

struct yetty_ycore_void_result yetty_yface_finish_read(struct yetty_yface *y)
{
    if (!y) {
        return YETTY_ERR(yetty_ycore_void, "yface: yface is NULL");
    }
    if (!y->dec_active) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result r = yetty_ywire_decode(
        (const char *)y->dec_b64_accum.data, y->dec_b64_accum.size, y->dec_compressed, &y->in_buf);
    yetty_ycore_buffer_clear(&y->dec_b64_accum);
    y->dec_active = 0;
    return r;
}

/*---------------------------------------------------------------------------
 * Incoming — push-callback scanner (set_handlers + feed_bytes).
 *
 * Backed by a ywire SM. set_envelope_default_buffered fires the user's
 * on_osc once per envelope; set_default_buffered forwards raw bytes to
 * on_raw.
 *-------------------------------------------------------------------------*/

static struct yetty_ycore_void_result yface_on_envelope(void *userdata,
                                                        enum yetty_ywire_envelope_kind kind,
                                                        int code, const uint8_t *args,
                                                        size_t args_len, const uint8_t *payload,
                                                        size_t payload_len)
{
    (void)kind;
    struct yetty_yface *y = userdata;
    if (y->on_osc) {
        y->on_osc(y->handler_user, code, args, args_len, payload, payload_len);
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result yface_on_raw(void *userdata, const uint8_t *bytes, size_t n)
{
    struct yetty_yface *y = userdata;
    if (y->on_raw) {
        y->on_raw(y->handler_user, (const char *)bytes, n);
    }
    return YETTY_OK_VOID();
}

void yetty_yface_set_handlers(struct yetty_yface *y, yetty_yface_msg_cb on_osc,
                              yetty_yface_raw_cb on_raw, void *user)
{
    if (!y) {
        return;
    }
    y->on_osc = on_osc;
    y->on_raw = on_raw;
    y->handler_user = user;
}

static struct yetty_ycore_void_result attach_handlers(struct yetty_yface *y)
{
    if (y->handlers_attached) {
        return YETTY_OK_VOID();
    }
    {
        struct yetty_ycore_void_result r = ensure_sm(y);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "yface: attach: ensure_sm");
    }
    {
        struct yetty_ycore_void_result r =
            yetty_ywire_wire_statemachine_set_envelope_default_buffered(y->sm, /*has_args=*/1,
                                                                        yface_on_envelope, y);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "yface: attach: envelope_default");
    }
    {
        struct yetty_ycore_void_result r =
            yetty_ywire_wire_statemachine_set_default_buffered(y->sm, yface_on_raw, y);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "yface: attach: default");
    }
    y->handlers_attached = 1;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yface_feed_bytes(struct yetty_yface *y, const char *bytes,
                                                      size_t n)
{
    if (!y) {
        return YETTY_ERR(yetty_ycore_void, "yface: yface is NULL");
    }
    if (!bytes || n == 0) {
        return YETTY_OK_VOID();
    }
    {
        struct yetty_ycore_void_result r = attach_handlers(y);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "yface: feed_bytes: attach");
    }
    return yetty_ywire_wire_statemachine_feed(y->sm, bytes, n);
}

/*---------------------------------------------------------------------------
 * One-shot helpers — forward to ywire equivalents.
 *-------------------------------------------------------------------------*/

struct yetty_ycore_void_result yetty_yface_emit(int wire_code, int compressed, const void *args,
                                                size_t args_len, const void *body, size_t body_len,
                                                struct yetty_ycore_buffer *out_buf)
{
    return yetty_ywire_emit(YETTY_YWIRE_ENVELOPE_DCS, wire_code, /*has_args=*/1, compressed, args,
                            args_len, body, body_len, out_buf);
}

struct yetty_ycore_void_result yetty_yface_emit_to_fd(int fd, int wire_code, int compressed,
                                                      const void *args, size_t args_len,
                                                      const void *body, size_t body_len)
{
    return yetty_ywire_emit_to_fd(fd, YETTY_YWIRE_ENVELOPE_DCS, wire_code, /*has_args=*/1,
                                  compressed, args, args_len, body, body_len);
}

struct yetty_ycore_void_result yetty_yface_decode(const char *b64, size_t n, int compressed,
                                                  struct yetty_ycore_buffer *out_buf)
{
    return yetty_ywire_decode(b64, n, compressed, out_buf);
}
