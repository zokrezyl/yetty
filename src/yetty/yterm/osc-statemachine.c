/*
 * osc-statemachine.c — streaming OSC state machine with cursor-driven
 * decode and pause/resume. See osc-statemachine.h for the model.
 */

#include <yetty/yterm/osc-statemachine.h>

#include <lz4frame.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/yplatform/pty.h>
#include <yetty/ytrace/ytrace.h>

/*===========================================================================
 * Internal types
 *=========================================================================*/

#define OSC_SM_RING_INITIAL_CAP 4096u /* must be power of 2 */
#define OSC_SM_BODY_BATCH 256u
#define OSC_SM_LZ4_OUT_CAP (16u * 1024u)
#define OSC_SM_ARGS_B64_MAX 1024u
#define OSC_SM_ARGS_RAW_MAX (OSC_SM_ARGS_B64_MAX * 3u / 4u)
#define OSC_SM_HANDLERS_INITIAL_CAP 4u
#define OSC_SM_PAYLOAD_INITIAL_CAP 4096u

enum scan_state {
    SCAN_RAW = 0,
    SCAN_AFTER_ESC,
    SCAN_OSC_CODE,
    SCAN_OSC_ARGS,
    SCAN_OSC_BODY,
    SCAN_OSC_BODY_ESC,
};

enum view_kind {
    VIEW_NONE = 0,
    VIEW_ENVELOPE,
    VIEW_RAW,
};

struct osc_sm_handler {
    int code;
    enum yetty_yterm_osc_codec codec;
    yetty_yterm_osc_on_envelope_cb on_envelope;
    void *user;
};

struct yetty_yterm_osc_sm {
    /* Ring buffer (power-of-2 cap, monotonic indices, masked addressing). */
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

    /* Resolved handler for the current envelope. */
    const struct osc_sm_handler *current_handler;

    /* b64 streaming carry. */
    uint8_t b64_carry[4];
    uint8_t b64_carry_n;

    /* LZ4F decompression — lazy alloc, reset between envelopes. */
    LZ4F_decompressionContext_t lz4_ctx;

    /* Buffered envelope payload — accumulated across body batches and
     * surfaced to on_envelope at the terminator. */
    uint8_t *payload_buf;
    size_t payload_len;
    size_t payload_cap;

    /* What the SM is currently delivering to a callback. Set just before
     * the callback fires; cleared right after. */
    enum view_kind view;
    int view_code;
    const uint8_t *view_args;
    size_t view_args_len;
    const uint8_t *view_payload;
    size_t view_payload_len;
    const char *view_raw;
    size_t view_raw_len;

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

static size_t round_pow2(size_t n)
{
    size_t r = 1;
    while (r < n) {
        r <<= 1;
    }
    return r;
}

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

static void ring_read_into(const struct yetty_yterm_osc_sm *sm, size_t start, size_t n,
                           uint8_t *dst)
{
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
 * b64
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
 * Handler registry / payload buffer
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

static struct yetty_ycore_void_result payload_reserve(struct yetty_yterm_osc_sm *sm, size_t extra)
{
    size_t need = sm->payload_len + extra;
    if (need <= sm->payload_cap) {
        return YETTY_OK_VOID();
    }
    size_t nc = sm->payload_cap ? sm->payload_cap : OSC_SM_PAYLOAD_INITIAL_CAP;
    while (nc < need) {
        nc *= 2;
    }
    uint8_t *nb = realloc(sm->payload_buf, nc);
    if (!nb) {
        return YETTY_ERR(yetty_ycore_void, "osc_sm: payload realloc failed");
    }
    sm->payload_buf = nb;
    sm->payload_cap = nc;
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Args + codec setup
 *=========================================================================*/

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

static struct yetty_ycore_void_result open_codec(struct yetty_yterm_osc_sm *sm)
{
    sm->b64_carry_n = 0;
    sm->payload_len = 0;
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
 * Body decode
 *=========================================================================*/

static struct yetty_ycore_void_result body_process_batch(struct yetty_yterm_osc_sm *sm,
                                                         size_t start, size_t n)
{
    if (n == 0 || !sm->current_handler) {
        return YETTY_OK_VOID();
    }
    if (n > OSC_SM_BODY_BATCH) {
        n = OSC_SM_BODY_BATCH;
    }

    uint8_t batch[OSC_SM_BODY_BATCH];
    ring_read_into(sm, start, n, batch);

    /* Combine carry + new chars; drop everything from first '=' (b64 EOS). */
    char full[OSC_SM_BODY_BATCH + 4];
    size_t full_n = 0;
    for (size_t i = 0; i < sm->b64_carry_n; i++) {
        full[full_n++] = (char)sm->b64_carry[i];
    }
    size_t valid_n = 0;
    while (valid_n < n && batch[valid_n] != '=') {
        valid_n++;
    }
    memcpy(full + full_n, batch, valid_n);
    full_n += valid_n;

    uint8_t decoded[(OSC_SM_BODY_BATCH + 4) * 3 / 4 + 4];
    size_t decoded_n = 0;
    size_t quartets = full_n / 4;
    for (size_t q = 0; q < quartets; q++) {
        uint8_t triple[3];
        if (b64_decode_quartet(full + q * 4, triple)) {
            memcpy(decoded + decoded_n, triple, 3);
            decoded_n += 3;
        }
    }

    sm->b64_carry_n = (uint8_t)(full_n - quartets * 4);
    for (size_t i = 0; i < sm->b64_carry_n; i++) {
        sm->b64_carry[i] = (uint8_t)full[quartets * 4 + i];
    }

    if (decoded_n == 0) {
        return YETTY_OK_VOID();
    }

    if (sm->current_handler->codec == YETTY_YTERM_OSC_CODEC_NONE ||
        sm->current_handler->codec == YETTY_YTERM_OSC_CODEC_B64) {
        struct yetty_ycore_void_result r = payload_reserve(sm, decoded_n);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "osc_sm: payload reserve");
        memcpy(sm->payload_buf + sm->payload_len, decoded, decoded_n);
        sm->payload_len += decoded_n;
        return YETTY_OK_VOID();
    }

    /* B64_LZ4 */
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
        if (out_left > 0) {
            struct yetty_ycore_void_result rr = payload_reserve(sm, out_left);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "osc_sm: payload reserve");
            memcpy(sm->payload_buf + sm->payload_len, out, out_left);
            sm->payload_len += out_left;
        }
        in_pos += in_left;
        if (in_left == 0 && out_left == 0) {
            break;
        }
    }
    return YETTY_OK_VOID();
}

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
        if (out_left > 0) {
            struct yetty_ycore_void_result rr = payload_reserve(sm, out_left);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "osc_sm: payload reserve");
            memcpy(sm->payload_buf + sm->payload_len, out, out_left);
            sm->payload_len += out_left;
        }
        if (out_left == 0) {
            break;
        }
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result dispatch_envelope(struct yetty_yterm_osc_sm *sm)
{
    if (!sm->current_handler || !sm->current_handler->on_envelope) {
        return YETTY_OK_VOID();
    }
    sm->view = VIEW_ENVELOPE;
    sm->view_code = sm->code;
    sm->view_args = sm->args_decoded;
    sm->view_args_len = sm->args_decoded_len;
    sm->view_payload = sm->payload_buf;
    sm->view_payload_len = sm->payload_len;
    struct yetty_ycore_void_result r =
        sm->current_handler->on_envelope(sm->current_handler->user, sm);
    sm->view = VIEW_NONE;
    sm->view_args = NULL;
    sm->view_args_len = 0;
    sm->view_payload = NULL;
    sm->view_payload_len = 0;
    if (YETTY_IS_ERR(r)) {
        return YETTY_ERR(yetty_ycore_void, "osc_sm: envelope handler failed", r);
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result dispatch_raw_range(struct yetty_yterm_osc_sm *sm,
                                                         size_t start, size_t end)
{
    if (!sm->on_raw || end <= start) {
        return YETTY_OK_VOID();
    }
    size_t mask = sm->cap - 1;
    size_t off = start & mask;
    size_t n = end - start;
    size_t first = sm->cap - off;

    sm->view = VIEW_RAW;
    sm->view_code = 0;

    if (first >= n) {
        sm->view_raw = (const char *)sm->buf + off;
        sm->view_raw_len = n;
        struct yetty_ycore_void_result r = sm->on_raw(sm->raw_user, sm);
        sm->view = VIEW_NONE;
        sm->view_raw = NULL;
        sm->view_raw_len = 0;
        if (YETTY_IS_ERR(r)) {
            return YETTY_ERR(yetty_ycore_void, "osc_sm: raw handler failed", r);
        }
        return YETTY_OK_VOID();
    }
    /* Wrap — deliver in two halves. */
    sm->view_raw = (const char *)sm->buf + off;
    sm->view_raw_len = first;
    struct yetty_ycore_void_result r1 = sm->on_raw(sm->raw_user, sm);
    if (YETTY_IS_ERR(r1)) {
        sm->view = VIEW_NONE;
        sm->view_raw = NULL;
        sm->view_raw_len = 0;
        return YETTY_ERR(yetty_ycore_void, "osc_sm: raw handler failed", r1);
    }
    sm->view_raw = (const char *)sm->buf;
    sm->view_raw_len = n - first;
    struct yetty_ycore_void_result r2 = sm->on_raw(sm->raw_user, sm);
    sm->view = VIEW_NONE;
    sm->view_raw = NULL;
    sm->view_raw_len = 0;
    if (YETTY_IS_ERR(r2)) {
        return YETTY_ERR(yetty_ycore_void, "osc_sm: raw handler failed", r2);
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result dispatch_raw_stack(struct yetty_yterm_osc_sm *sm,
                                                         const char *bytes, size_t n)
{
    if (!sm->on_raw || n == 0) {
        return YETTY_OK_VOID();
    }
    sm->view = VIEW_RAW;
    sm->view_code = 0;
    sm->view_raw = bytes;
    sm->view_raw_len = n;
    struct yetty_ycore_void_result r = sm->on_raw(sm->raw_user, sm);
    sm->view = VIEW_NONE;
    sm->view_raw = NULL;
    sm->view_raw_len = 0;
    if (YETTY_IS_ERR(r)) {
        return YETTY_ERR(yetty_ycore_void, "osc_sm: raw handler failed", r);
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
        return YETTY_ERR(yetty_yterm_osc_sm_ptr, "osc_sm: calloc failed");
    }
    sm->state = SCAN_RAW;
    return YETTY_OK(yetty_yterm_osc_sm_ptr, sm);
}

struct yetty_ycore_void_result yetty_yterm_osc_sm_destroy(struct yetty_yterm_osc_sm *sm)
{
    if (!sm) {
        return YETTY_ERR(yetty_ycore_void, "osc_sm: sm is NULL");
    }
    if (sm->lz4_ctx) {
        LZ4F_freeDecompressionContext(sm->lz4_ctx);
        sm->lz4_ctx = NULL;
    }
    free(sm->buf);
    free(sm->payload_buf);
    free(sm->handlers);
    free(sm);
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yterm_osc_sm_register(
    struct yetty_yterm_osc_sm *sm, int code, enum yetty_yterm_osc_codec codec,
    yetty_yterm_osc_on_envelope_cb on_envelope, void *user)
{
    if (!sm) {
        return YETTY_ERR(yetty_ycore_void, "osc_sm: sm is NULL");
    }
    for (size_t i = 0; i < sm->handler_count; i++) {
        if (sm->handlers[i].code == code) {
            sm->handlers[i].codec = codec;
            sm->handlers[i].on_envelope = on_envelope;
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
        .on_envelope = on_envelope,
        .user = user,
    };
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yterm_osc_sm_set_raw_handler(
    struct yetty_yterm_osc_sm *sm, yetty_yterm_osc_on_raw_cb on_raw, void *user)
{
    if (!sm) {
        return YETTY_ERR(yetty_ycore_void, "osc_sm: sm is NULL");
    }
    sm->on_raw = on_raw;
    sm->raw_user = user;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yterm_osc_sm_feed(struct yetty_yterm_osc_sm *sm,
                                                       const char *bytes, size_t n)
{
    if (!sm) {
        return YETTY_ERR(yetty_ycore_void, "osc_sm: sm is NULL");
    }
    if (!bytes || n == 0) {
        return YETTY_OK_VOID();
    }

    size_t avail = ring_avail(sm);
    if (avail + n > sm->cap) {
        struct yetty_ycore_void_result r = ring_grow_to(sm, avail + n);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "osc_sm: feed grow");
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

struct yetty_ycore_size_result yetty_yterm_osc_sm_read_pty(struct yetty_yterm_osc_sm *sm,
                                                           struct yetty_platform_pty *pty)
{
    if (!sm) {
        return YETTY_ERR(yetty_ycore_size, "osc_sm: sm is NULL");
    }
    if (!pty || !pty->ops || !pty->ops->read) {
        return YETTY_ERR(yetty_ycore_size, "osc_sm: pty has no read op");
    }

    if (!sm->buf) {
        struct yetty_ycore_void_result r = ring_grow_to(sm, OSC_SM_RING_INITIAL_CAP);
        YETTY_RETURN_IF_ERR(yetty_ycore_size, r, "osc_sm: ring init");
    }
    size_t avail = ring_avail(sm);
    if (avail == sm->cap) {
        struct yetty_ycore_void_result r = ring_grow_to(sm, sm->cap * 2);
        YETTY_RETURN_IF_ERR(yetty_ycore_size, r, "osc_sm: ring grow");
    }

    size_t mask = sm->cap - 1;
    size_t off = sm->write_pos & mask;
    size_t free_to_end = sm->cap - off;
    size_t free_total = sm->cap - ring_avail(sm);
    size_t max_n = free_to_end < free_total ? free_to_end : free_total;
    if (max_n == 0) {
        return YETTY_OK(yetty_ycore_size, 0);
    }

    struct yetty_ycore_size_result rr = pty->ops->read(pty, (char *)sm->buf + off, max_n);
    if (YETTY_IS_ERR(rr)) {
        return YETTY_ERR(yetty_ycore_size, "osc_sm: pty read", rr);
    }
    if (rr.value > 0) {
        sm->write_pos += rr.value;
    }
    return YETTY_OK(yetty_ycore_size, rr.value);
}

struct yetty_ycore_void_result yetty_yterm_osc_sm_process(struct yetty_yterm_osc_sm *sm)
{
    if (!sm) {
        return YETTY_ERR(yetty_ycore_void, "osc_sm: sm is NULL");
    }

    while (sm->read_pos < sm->write_pos) {
        switch (sm->state) {
        case SCAN_RAW: {
            size_t end = ring_find2(sm, sm->read_pos, sm->write_pos, '\033', '\033');
            if (end > sm->read_pos) {
                struct yetty_ycore_void_result r = dispatch_raw_range(sm, sm->read_pos, end);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "osc_sm: raw range");
                sm->read_pos = end;
            }
            if (sm->read_pos < sm->write_pos) {
                sm->read_pos++;
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
                char emit[2] = {'\033', (char)c};
                struct yetty_ycore_void_result r = dispatch_raw_stack(sm, emit, 2);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "osc_sm: raw esc-pair");
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
                sm->state = SCAN_OSC_BODY;
            } else if (sm->args_b64_len < sizeof(sm->args_b64)) {
                sm->args_b64[sm->args_b64_len++] = (char)c;
            }
            break;
        }

        case SCAN_OSC_BODY: {
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
                    sm->current_handler = NULL;
                    sm->read_pos = term;
                    sm->state = SCAN_RAW;
                    break;
                }
                sm->read_pos += batch_n;
            }

            if (term == sm->write_pos) {
                break; /* out of input within body */
            }
            if (term < batch_end) {
                uint8_t c = ring_at(sm, term);
                sm->read_pos = term + 1;
                if (c == '\007') {
                    struct yetty_ycore_void_result fr = finalize_body(sm);
                    if (YETTY_IS_ERR(fr)) {
                        yerror("osc_sm: finalize_body failed: %s", fr.error.msg);
                        yetty_ycore_error_destroy(fr.error);
                    } else {
                        struct yetty_ycore_void_result dr = dispatch_envelope(sm);
                        YETTY_RETURN_IF_ERR(yetty_ycore_void, dr, "osc_sm: dispatch");
                    }
                    sm->current_handler = NULL;
                    sm->state = SCAN_RAW;
                } else {
                    sm->state = SCAN_OSC_BODY_ESC;
                }
            }
            break;
        }

        case SCAN_OSC_BODY_ESC: {
            uint8_t c = ring_at(sm, sm->read_pos++);
            if (c == '\\') {
                struct yetty_ycore_void_result fr = finalize_body(sm);
                if (YETTY_IS_ERR(fr)) {
                    yerror("osc_sm: finalize_body failed: %s", fr.error.msg);
                    yetty_ycore_error_destroy(fr.error);
                } else {
                    struct yetty_ycore_void_result dr = dispatch_envelope(sm);
                    YETTY_RETURN_IF_ERR(yetty_ycore_void, dr, "osc_sm: dispatch");
                }
                sm->current_handler = NULL;
                sm->state = SCAN_RAW;
            } else {
                ywarn("osc_sm: ESC inside body not followed by '\\\\'; byte=0x%02x", (unsigned)c);
                sm->state = SCAN_OSC_BODY;
            }
            break;
        }
        }
    }

    return YETTY_OK_VOID();
}

struct yetty_yterm_osc_sm_envelope_result yetty_yterm_osc_sm_envelope(
    const struct yetty_yterm_osc_sm *sm)
{
    if (!sm) {
        return YETTY_ERR(yetty_yterm_osc_sm_envelope, "osc_sm: sm is NULL");
    }
    if (sm->view != VIEW_ENVELOPE) {
        return YETTY_ERR(yetty_yterm_osc_sm_envelope,
                         "osc_sm: no envelope in flight (called outside on_envelope?)");
    }
    struct yetty_yterm_osc_sm_envelope env = {
        .code = sm->view_code,
        .args = sm->view_args,
        .args_len = sm->view_args_len,
        .payload = sm->view_payload,
        .payload_len = sm->view_payload_len,
    };
    return YETTY_OK(yetty_yterm_osc_sm_envelope, env);
}

struct yetty_yterm_osc_sm_raw_result yetty_yterm_osc_sm_raw(const struct yetty_yterm_osc_sm *sm)
{
    if (!sm) {
        return YETTY_ERR(yetty_yterm_osc_sm_raw, "osc_sm: sm is NULL");
    }
    if (sm->view != VIEW_RAW) {
        return YETTY_ERR(yetty_yterm_osc_sm_raw,
                         "osc_sm: no raw span in flight (called outside on_raw?)");
    }
    struct yetty_yterm_osc_sm_raw raw = {
        .bytes = sm->view_raw,
        .len = sm->view_raw_len,
    };
    return YETTY_OK(yetty_yterm_osc_sm_raw, raw);
}
