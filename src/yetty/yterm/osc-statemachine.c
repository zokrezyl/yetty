/*
 * osc-statemachine.c — streaming OSC state machine with cursor-driven
 * decode and pause/resume.
 *
 * See osc-statemachine.h for the wire shape and the cursor / pause model.
 *
 * Internal layout:
 *
 *   ring buffer (power-of-2 cap, head/tail with mask, doubling realloc)
 *      ├─ write_pos: monotonic counter, producer appends here (feed)
 *      └─ read_pos:  monotonic counter, SM consumes from here (step)
 *
 *   envelope state machine — same five states as yface, plus a cursor
 *   into the ring. State + b64 carry + LZ4F context + args buffer all
 *   live on the SM struct so step() can be paused at any byte boundary
 *   and resumed later.
 *
 *   payload codec — selected per registered OSC code: NONE / B64 / B64_LZ4.
 *   Decoded bytes are streamed through on_payload() in batches of up to
 *   the LZ4F output scratch (16 KB). Each batch checks the callback's
 *   pause return value; if pause hits we drain the current LZ4 inner-loop
 *   to keep input/output state consistent, then exit step().
 */

#include <yetty/yterm/osc-statemachine.h>

#include <lz4frame.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/ytrace/ytrace.h>

/*===========================================================================
 * Internal types
 *=========================================================================*/

#define OSC_SM_RING_INITIAL_CAP 4096u /* must be power of 2 */
#define OSC_SM_BODY_BATCH 256u        /* b64 chars per body batch */
#define OSC_SM_LZ4_OUT_CAP (16u * 1024u)
#define OSC_SM_ARGS_B64_MAX 1024u
#define OSC_SM_ARGS_RAW_MAX (OSC_SM_ARGS_B64_MAX * 3u / 4u)
#define OSC_SM_HANDLERS_INITIAL_CAP 4u

enum scan_state {
    SCAN_RAW = 0,
    SCAN_AFTER_ESC,
    SCAN_OSC_CODE,
    SCAN_OSC_ARGS,
    SCAN_OSC_BODY,
    SCAN_OSC_BODY_ESC,
};

struct osc_sm_handler {
    int code;
    enum yetty_yterm_osc_codec codec;
    yetty_yterm_osc_on_begin_cb on_begin;
    yetty_yterm_osc_on_payload_cb on_payload;
    yetty_yterm_osc_on_end_cb on_end;
    void *user;
};

struct yetty_yterm_osc_sm {
    /* Ring buffer. cap is a power of 2; both indices are monotonic
     * absolute counters so we never confuse "empty" and "full". */
    uint8_t *buf;
    size_t cap;
    size_t read_pos;
    size_t write_pos;

    /* Envelope state machine. */
    enum scan_state state;
    int code;

    /* Args slot — accumulated b64, decoded once at the second `;`. */
    char args_b64[OSC_SM_ARGS_B64_MAX];
    size_t args_b64_len;
    uint8_t args_decoded[OSC_SM_ARGS_RAW_MAX];
    size_t args_decoded_len;

    /* Resolved handler for the current envelope. NULL means no registered
     * handler — body bytes are dropped. */
    const struct osc_sm_handler *current_handler;

    /* b64 streaming decode carry — 0..3 chars not yet a complete quartet. */
    uint8_t b64_carry[4];
    uint8_t b64_carry_n;

    /* LZ4F decompression — allocated lazily on first compressed body,
     * reset between envelopes. */
    LZ4F_decompressionContext_t lz4_ctx;

    /* Pause flag — set when on_payload returns 1, cleared at top of step(). */
    int paused;

    /* Per-code handler registry. */
    struct osc_sm_handler *handlers;
    size_t handler_count;
    size_t handler_cap;

    /* Out-of-envelope handler. */
    yetty_yterm_osc_on_raw_cb on_raw;
    void *raw_user;
};

/*===========================================================================
 * Ring buffer helpers
 *=========================================================================*/

static size_t ring_avail(const struct yetty_yterm_osc_sm *sm)
{
    return sm->write_pos - sm->read_pos;
}

/* Round up to next power of 2 (n >= 1). */
static size_t round_pow2(size_t n)
{
    size_t r = 1;
    while (r < n) {
        r <<= 1;
    }
    return r;
}

/* Grow the ring to at least new_min capacity. The unprocessed range
 * [read_pos, write_pos) is repacked at offset 0 of the new buffer; both
 * counters are renormalised. */
static struct yetty_ycore_void_result ring_grow_to(struct yetty_yterm_osc_sm *sm, size_t new_min)
{
    size_t avail = ring_avail(sm);
    size_t new_cap = sm->cap ? sm->cap : OSC_SM_RING_INITIAL_CAP;
    if (new_cap < new_min) {
        new_cap = round_pow2(new_min);
    }
    if (new_cap == sm->cap && sm->buf) {
        return YETTY_OK_VOID();
    }

    uint8_t *nb = malloc(new_cap);
    if (!nb) {
        return YETTY_ERR(yetty_ycore_void, "osc_sm: ring grow malloc failed");
    }
    if (sm->buf && avail > 0) {
        size_t mask = sm->cap - 1;
        size_t off = sm->read_pos & mask;
        size_t first = sm->cap - off;
        if (first >= avail) {
            memcpy(nb, sm->buf + off, avail);
        } else {
            memcpy(nb, sm->buf + off, first);
            memcpy(nb + first, sm->buf, avail - first);
        }
    }
    free(sm->buf);
    sm->buf = nb;
    sm->cap = new_cap;
    sm->read_pos = 0;
    sm->write_pos = avail;
    return YETTY_OK_VOID();
}

static uint8_t ring_at(const struct yetty_yterm_osc_sm *sm, size_t pos)
{
    return sm->buf[pos & (sm->cap - 1)];
}

/* Copy `n` bytes from the ring starting at `start` into `dst`. */
static void ring_read_into(const struct yetty_yterm_osc_sm *sm, size_t start, size_t n, uint8_t *dst)
{
    if (n == 0) {
        return;
    }
    size_t mask = sm->cap - 1;
    size_t off = start & mask;
    size_t first = sm->cap - off;
    if (first >= n) {
        memcpy(dst, sm->buf + off, n);
    } else {
        memcpy(dst, sm->buf + off, first);
        memcpy(dst + first, sm->buf, n - first);
    }
}

/* Find the first byte in [start, end) equal to a or b. Returns the index
 * of the match, or end if not found. */
static size_t ring_find2(const struct yetty_yterm_osc_sm *sm, size_t start, size_t end, uint8_t a,
                         uint8_t b)
{
    for (size_t i = start; i < end; i++) {
        uint8_t c = ring_at(sm, i);
        if (c == a || c == b) {
            return i;
        }
    }
    return end;
}

/*===========================================================================
 * Streaming base64 — decoder
 *=========================================================================*/

static int b64_decode_char(char c, uint8_t *out)
{
    if (c >= 'A' && c <= 'Z') {
        *out = (uint8_t)(c - 'A');
    } else if (c >= 'a' && c <= 'z') {
        *out = (uint8_t)(c - 'a' + 26);
    } else if (c >= '0' && c <= '9') {
        *out = (uint8_t)(c - '0' + 52);
    } else if (c == '+') {
        *out = 62;
    } else if (c == '/') {
        *out = 63;
    } else {
        return 0;
    }
    return 1;
}

static int b64_decode_quartet(const char chars[4], uint8_t triple[3])
{
    uint8_t v[4];
    for (int i = 0; i < 4; i++) {
        if (!b64_decode_char(chars[i], &v[i])) {
            return 0;
        }
    }
    triple[0] = (uint8_t)((v[0] << 2) | (v[1] >> 4));
    triple[1] = (uint8_t)((v[1] << 4) | (v[2] >> 2));
    triple[2] = (uint8_t)((v[2] << 6) | v[3]);
    return 1;
}

/*===========================================================================
 * Handler registry
 *=========================================================================*/

static const struct osc_sm_handler *find_handler(const struct yetty_yterm_osc_sm *sm, int code)
{
    for (size_t i = 0; i < sm->handler_count; i++) {
        if (sm->handlers[i].code == code) {
            return &sm->handlers[i];
        }
    }
    return NULL;
}

/*===========================================================================
 * Envelope helpers
 *=========================================================================*/

/* Decode the accumulated args_b64 into args_decoded. Truncates if the args
 * slot is malformed (mirrors yface's tolerant behaviour). */
static void decode_args(struct yetty_yterm_osc_sm *sm)
{
    sm->args_decoded_len = 0;
    size_t b64n = sm->args_b64_len;
    while (b64n > 0 && sm->args_b64[b64n - 1] == '=') {
        b64n--;
    }
    size_t pos = 0;
    while (pos + 4 <= b64n) {
        uint8_t triple[3];
        if (!b64_decode_quartet(sm->args_b64 + pos, triple)) {
            break;
        }
        if (sm->args_decoded_len + 3 > sizeof(sm->args_decoded)) {
            break;
        }
        memcpy(sm->args_decoded + sm->args_decoded_len, triple, 3);
        sm->args_decoded_len += 3;
        pos += 4;
    }
    /* Tail of 2 or 3 valid chars decodes to 1 or 2 bytes (b64 padding). */
    if (b64n - pos >= 2 && sm->args_decoded_len + 2 <= sizeof(sm->args_decoded)) {
        char tail[4] = {sm->args_b64[pos], sm->args_b64[pos + 1],
                        b64n - pos >= 3 ? sm->args_b64[pos + 2] : 'A', 'A'};
        uint8_t triple[3];
        if (b64_decode_quartet(tail, triple)) {
            sm->args_decoded[sm->args_decoded_len++] = triple[0];
            if (b64n - pos >= 3) {
                sm->args_decoded[sm->args_decoded_len++] = triple[1];
            }
        }
    }
}

/* Open the codec for the current envelope based on the handler's mode.
 * Lazily allocates the LZ4 context on first compressed body; resets it
 * between envelopes. */
static struct yetty_ycore_void_result open_codec(struct yetty_yterm_osc_sm *sm)
{
    sm->b64_carry_n = 0;
    if (!sm->current_handler) {
        return YETTY_OK_VOID();
    }
    if (sm->current_handler->codec == YETTY_YTERM_OSC_CODEC_B64_LZ4) {
        if (!sm->lz4_ctx) {
            LZ4F_errorCode_t err = LZ4F_createDecompressionContext(&sm->lz4_ctx, LZ4F_VERSION);
            if (LZ4F_isError(err)) {
                return YETTY_ERR(yetty_ycore_void, LZ4F_getErrorName(err));
            }
        } else {
            LZ4F_resetDecompressionContext(sm->lz4_ctx);
        }
    }
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Body batch processing
 *=========================================================================*/

/* Drive one batch of body b64 chars through the codec. The chars are read
 * out of the ring [start, start + n). If the consumer pauses inside this
 * batch (on_payload returns 1), we still drain the LZ4 inner loop so its
 * input/output state stays consistent — pause latency is bounded by one
 * batch's worth of decompression. */
static struct yetty_ycore_void_result body_process_batch(struct yetty_yterm_osc_sm *sm,
                                                         size_t start, size_t n)
{
    if (n == 0) {
        return YETTY_OK_VOID();
    }
    if (!sm->current_handler) {
        /* No registered handler — drop the body bytes. */
        return YETTY_OK_VOID();
    }

    if (n > OSC_SM_BODY_BATCH) {
        n = OSC_SM_BODY_BATCH;
    }
    char chars[OSC_SM_BODY_BATCH];
    {
        uint8_t scratch[OSC_SM_BODY_BATCH];
        ring_read_into(sm, start, n, scratch);
        memcpy(chars, scratch, n);
    }

    /* Combine carry (0..3 chars) with the new chars to form quartets. */
    char full[OSC_SM_BODY_BATCH + 4];
    size_t full_n = 0;
    for (size_t i = 0; i < sm->b64_carry_n; i++) {
        full[full_n++] = (char)sm->b64_carry[i];
    }
    /* Skip everything from the first '=' on — that's the b64 EOS pad. */
    size_t valid_n = 0;
    while (valid_n < n && chars[valid_n] != '=') {
        valid_n++;
    }
    memcpy(full + full_n, chars, valid_n);
    full_n += valid_n;

    /* Decode complete quartets into a transient buffer. */
    uint8_t decoded[(OSC_SM_BODY_BATCH + 4) * 3 / 4 + 4];
    size_t decoded_n = 0;
    size_t quartets = full_n / 4;
    for (size_t q = 0; q < quartets; q++) {
        uint8_t triple[3];
        if (b64_decode_quartet(full + q * 4, triple)) {
            memcpy(decoded + decoded_n, triple, 3);
            decoded_n += 3;
        }
        /* else: drop silently — caller fed garbage */
    }

    /* Stash leftover (<4 chars) back into carry. */
    sm->b64_carry_n = (uint8_t)(full_n - quartets * 4);
    for (size_t i = 0; i < sm->b64_carry_n; i++) {
        sm->b64_carry[i] = (uint8_t)full[quartets * 4 + i];
    }

    if (decoded_n == 0) {
        return YETTY_OK_VOID();
    }

    yetty_yterm_osc_on_payload_cb on_payload = sm->current_handler->on_payload;
    void *user = sm->current_handler->user;

    if (sm->current_handler->codec == YETTY_YTERM_OSC_CODEC_NONE ||
        sm->current_handler->codec == YETTY_YTERM_OSC_CODEC_B64) {
        if (on_payload) {
            int rc = on_payload(user, decoded, decoded_n);
            if (rc != 0) {
                sm->paused = 1;
            }
        }
        return YETTY_OK_VOID();
    }

    /* B64_LZ4: feed the decoded b64 bytes through LZ4F_decompress. */
    if (!sm->lz4_ctx) {
        return YETTY_ERR(yetty_ycore_void, "osc_sm: LZ4 ctx missing for compressed body");
    }
    uint8_t out[OSC_SM_LZ4_OUT_CAP];
    size_t in_pos = 0;
    while (in_pos < decoded_n) {
        size_t in_left = decoded_n - in_pos;
        size_t out_left = sizeof(out);
        size_t r = LZ4F_decompress(sm->lz4_ctx, out, &out_left, decoded + in_pos, &in_left, NULL);
        if (LZ4F_isError(r)) {
            return YETTY_ERR(yetty_ycore_void, LZ4F_getErrorName(r));
        }
        if (out_left > 0 && on_payload) {
            int rc = on_payload(user, out, out_left);
            if (rc != 0) {
                sm->paused = 1;
                /* Continue draining the LZ4 inner loop so its state stays
                 * coherent; outer step() exits after the loop. */
            }
        }
        in_pos += in_left;
        if (in_left == 0 && out_left == 0) {
            break; /* no progress — needs more input */
        }
    }
    return YETTY_OK_VOID();
}

/* End-of-body finalisation: drain any LZ4 output the decompressor still
 * holds and discard the b64 carry tail. */
static struct yetty_ycore_void_result finalize_body(struct yetty_yterm_osc_sm *sm)
{
    if (!sm->current_handler) {
        return YETTY_OK_VOID();
    }
    if (sm->current_handler->codec != YETTY_YTERM_OSC_CODEC_B64_LZ4 || !sm->lz4_ctx) {
        return YETTY_OK_VOID();
    }
    uint8_t out[OSC_SM_LZ4_OUT_CAP];
    for (;;) {
        size_t in_left = 0;
        size_t out_left = sizeof(out);
        size_t r = LZ4F_decompress(sm->lz4_ctx, out, &out_left, NULL, &in_left, NULL);
        if (LZ4F_isError(r)) {
            return YETTY_ERR(yetty_ycore_void, LZ4F_getErrorName(r));
        }
        if (out_left > 0 && sm->current_handler->on_payload) {
            int rc = sm->current_handler->on_payload(sm->current_handler->user, out, out_left);
            if (rc != 0) {
                sm->paused = 1;
            }
        }
        if (out_left == 0) {
            break;
        }
    }
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Public API
 *=========================================================================*/

struct yetty_yterm_osc_sm_ptr_result yetty_yterm_osc_sm_create(void)
{
    struct yetty_yterm_osc_sm *sm = calloc(1, sizeof(struct yetty_yterm_osc_sm));
    if (!sm) {
        return YETTY_ERR(yetty_yterm_osc_sm_ptr, "calloc failed");
    }
    sm->state = SCAN_RAW;
    /* Ring is allocated lazily on first feed — keeps create() cheap. */
    return YETTY_OK(yetty_yterm_osc_sm_ptr, sm);
}

void yetty_yterm_osc_sm_destroy(struct yetty_yterm_osc_sm *sm)
{
    if (!sm) {
        return;
    }
    if (sm->lz4_ctx) {
        LZ4F_freeDecompressionContext(sm->lz4_ctx);
        sm->lz4_ctx = NULL;
    }
    free(sm->buf);
    free(sm->handlers);
    free(sm);
}

struct yetty_ycore_void_result yetty_yterm_osc_sm_register(
    struct yetty_yterm_osc_sm *sm, int code, enum yetty_yterm_osc_codec codec,
    yetty_yterm_osc_on_begin_cb on_begin, yetty_yterm_osc_on_payload_cb on_payload,
    yetty_yterm_osc_on_end_cb on_end, void *user)
{
    if (!sm) {
        return YETTY_ERR(yetty_ycore_void, "sm is NULL");
    }

    for (size_t i = 0; i < sm->handler_count; i++) {
        if (sm->handlers[i].code == code) {
            sm->handlers[i].codec = codec;
            sm->handlers[i].on_begin = on_begin;
            sm->handlers[i].on_payload = on_payload;
            sm->handlers[i].on_end = on_end;
            sm->handlers[i].user = user;
            return YETTY_OK_VOID();
        }
    }

    if (sm->handler_count == sm->handler_cap) {
        size_t nc = sm->handler_cap ? sm->handler_cap * 2 : OSC_SM_HANDLERS_INITIAL_CAP;
        struct osc_sm_handler *nh =
            realloc(sm->handlers, nc * sizeof(struct osc_sm_handler));
        if (!nh) {
            return YETTY_ERR(yetty_ycore_void, "osc_sm: handler realloc failed");
        }
        sm->handlers = nh;
        sm->handler_cap = nc;
    }
    sm->handlers[sm->handler_count++] = (struct osc_sm_handler){
        .code = code,
        .codec = codec,
        .on_begin = on_begin,
        .on_payload = on_payload,
        .on_end = on_end,
        .user = user,
    };
    return YETTY_OK_VOID();
}

void yetty_yterm_osc_sm_set_raw_handler(struct yetty_yterm_osc_sm *sm,
                                        yetty_yterm_osc_on_raw_cb on_raw, void *user)
{
    if (!sm) {
        return;
    }
    sm->on_raw = on_raw;
    sm->raw_user = user;
}

struct yetty_ycore_void_result yetty_yterm_osc_sm_feed(struct yetty_yterm_osc_sm *sm,
                                                       const char *bytes, size_t n)
{
    if (!sm) {
        return YETTY_ERR(yetty_ycore_void, "sm is NULL");
    }
    if (!bytes || n == 0) {
        return YETTY_OK_VOID();
    }

    size_t avail = ring_avail(sm);
    if (avail + n > sm->cap) {
        struct yetty_ycore_void_result r = ring_grow_to(sm, avail + n);
        if (!r.ok) {
            return r;
        }
    }
    size_t mask = sm->cap - 1;
    size_t off = sm->write_pos & mask;
    size_t first = sm->cap - off;
    if (first >= n) {
        memcpy(sm->buf + off, bytes, n);
    } else {
        memcpy(sm->buf + off, bytes, first);
        memcpy(sm->buf, bytes + first, n - first);
    }
    sm->write_pos += n;
    return YETTY_OK_VOID();
}

/* Forward a span of out-of-envelope ring bytes to on_raw. The ring may
 * wrap inside the span, so split if needed. */
static void emit_raw_range(struct yetty_yterm_osc_sm *sm, size_t start, size_t end)
{
    if (!sm->on_raw || end <= start) {
        return;
    }
    size_t mask = sm->cap - 1;
    size_t off = start & mask;
    size_t n = end - start;
    size_t first = sm->cap - off;
    if (first >= n) {
        sm->on_raw(sm->raw_user, (const char *)sm->buf + off, n);
    } else {
        sm->on_raw(sm->raw_user, (const char *)sm->buf + off, first);
        sm->on_raw(sm->raw_user, (const char *)sm->buf, n - first);
    }
}

struct yetty_ycore_void_result yetty_yterm_osc_sm_step(struct yetty_yterm_osc_sm *sm)
{
    if (!sm) {
        return YETTY_ERR(yetty_ycore_void, "sm is NULL");
    }

    sm->paused = 0;

    while (!sm->paused && sm->read_pos < sm->write_pos) {
        switch (sm->state) {
        case SCAN_RAW: {
            size_t end = ring_find2(sm, sm->read_pos, sm->write_pos, '\033', '\033');
            if (end > sm->read_pos) {
                emit_raw_range(sm, sm->read_pos, end);
                sm->read_pos = end;
            }
            if (sm->read_pos < sm->write_pos) {
                sm->read_pos++; /* consume ESC */
                sm->state = SCAN_AFTER_ESC;
            }
            break;
        }

        case SCAN_AFTER_ESC: {
            uint8_t c = ring_at(sm, sm->read_pos++);
            if (c == ']') {
                sm->code = 0;
                sm->args_b64_len = 0;
                sm->args_decoded_len = 0;
                sm->current_handler = NULL;
                sm->state = SCAN_OSC_CODE;
            } else {
                /* Not an OSC introducer — emit ESC + this byte raw, drop
                 * back to RAW. */
                if (sm->on_raw) {
                    char esc = '\033';
                    sm->on_raw(sm->raw_user, &esc, 1);
                    sm->on_raw(sm->raw_user, (const char *)&c, 1);
                }
                sm->state = SCAN_RAW;
            }
            break;
        }

        case SCAN_OSC_CODE: {
            uint8_t c = ring_at(sm, sm->read_pos++);
            if (c >= '0' && c <= '9') {
                sm->code = sm->code * 10 + (int)(c - '0');
            } else if (c == ';') {
                sm->state = SCAN_OSC_ARGS;
            } else {
                ywarn("osc_sm: malformed OSC, code byte=0x%02x", (unsigned)c);
                sm->state = SCAN_RAW;
            }
            break;
        }

        case SCAN_OSC_ARGS: {
            uint8_t c = ring_at(sm, sm->read_pos++);
            if (c == ';') {
                /* End of args slot. Decode args, resolve handler, open
                 * codec, fire on_begin. */
                decode_args(sm);
                sm->current_handler = find_handler(sm, sm->code);
                struct yetty_ycore_void_result r = open_codec(sm);
                if (YETTY_IS_ERR(r)) {
                    yerror("osc_sm: open_codec failed: %s", r.error.msg);
                    yetty_ycore_error_destroy(r.error);
                    sm->current_handler = NULL;
                    sm->state = SCAN_RAW;
                    break;
                }
                if (sm->current_handler && sm->current_handler->on_begin) {
                    sm->current_handler->on_begin(sm->current_handler->user, sm->code,
                                                  sm->args_decoded, sm->args_decoded_len);
                }
                sm->state = SCAN_OSC_BODY;
            } else {
                if (sm->args_b64_len < sizeof(sm->args_b64)) {
                    sm->args_b64[sm->args_b64_len++] = (char)c;
                }
                /* Overflow: silently truncate (yface mirrors this). */
            }
            break;
        }

        case SCAN_OSC_BODY: {
            /* Consume up to BATCH bytes ahead of the cursor, stopping at
             * the next ESC or BEL terminator within the window. */
            size_t batch_end = sm->read_pos + OSC_SM_BODY_BATCH;
            if (batch_end > sm->write_pos) {
                batch_end = sm->write_pos;
            }
            size_t term = ring_find2(sm, sm->read_pos, batch_end, '\033', '\007');
            size_t batch_n = term - sm->read_pos;

            if (batch_n > 0) {
                struct yetty_ycore_void_result r =
                    body_process_batch(sm, sm->read_pos, batch_n);
                if (YETTY_IS_ERR(r)) {
                    yerror("osc_sm: body batch failed: %s", r.error.msg);
                    yetty_ycore_error_destroy(r.error);
                    finalize_body(sm);
                    if (sm->current_handler && sm->current_handler->on_end) {
                        sm->current_handler->on_end(sm->current_handler->user, sm->code);
                    }
                    sm->current_handler = NULL;
                    sm->read_pos = term;
                    sm->state = SCAN_RAW;
                    break;
                }
                sm->read_pos += batch_n;
            }

            if (term == sm->write_pos) {
                /* Out of input within OSC_BODY — outer while exits. */
                break;
            }
            if (term < batch_end) {
                uint8_t c = ring_at(sm, term);
                sm->read_pos = term + 1;
                if (c == '\007') {
                    /* BEL terminator. */
                    struct yetty_ycore_void_result fr = finalize_body(sm);
                    if (YETTY_IS_ERR(fr)) {
                        yetty_ycore_error_destroy(fr.error);
                    }
                    if (sm->current_handler && sm->current_handler->on_end) {
                        sm->current_handler->on_end(sm->current_handler->user, sm->code);
                    }
                    sm->current_handler = NULL;
                    sm->state = SCAN_RAW;
                } else {
                    /* ESC — could be ST or ESC-as-data. */
                    sm->state = SCAN_OSC_BODY_ESC;
                }
            }
            /* else: hit BATCH cap without a terminator; loop and continue. */
            break;
        }

        case SCAN_OSC_BODY_ESC: {
            uint8_t c = ring_at(sm, sm->read_pos++);
            if (c == '\\') {
                /* ST — envelope terminator. */
                struct yetty_ycore_void_result fr = finalize_body(sm);
                if (YETTY_IS_ERR(fr)) {
                    yetty_ycore_error_destroy(fr.error);
                }
                if (sm->current_handler && sm->current_handler->on_end) {
                    sm->current_handler->on_end(sm->current_handler->user, sm->code);
                }
                sm->current_handler = NULL;
                sm->state = SCAN_RAW;
            } else {
                /* ESC was data inside the body. yface in the same case
                 * just feeds ESC and the byte through. b64 alphabet excludes
                 * both, so this only happens for malformed input — record
                 * via b64_carry overflow protection (drop quietly). */
                ywarn("osc_sm: ESC inside body not followed by '\\\\'; byte=0x%02x", (unsigned)c);
                sm->state = SCAN_OSC_BODY;
            }
            break;
        }
        }
    }

    return YETTY_OK_VOID();
}

int yetty_yterm_osc_sm_paused(const struct yetty_yterm_osc_sm *sm)
{
    return sm ? sm->paused : 0;
}

size_t yetty_yterm_osc_sm_pending(const struct yetty_yterm_osc_sm *sm)
{
    return sm ? ring_avail(sm) : 0;
}
