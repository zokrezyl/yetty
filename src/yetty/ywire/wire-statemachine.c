/*
 * wire-statemachine.c — OSC framer + decode stack + dispatcher.
 *
 * Pipeline:
 *
 *   PTY → ring → framer → [b64 (args)] | [b64 → lz4 (payload)] → read() → layer
 *
 * SM owns the full decode stack. Layers register against codes and pull
 * decoded bytes via osc_statemachine_read. See header for the contract.
 */
#include <yetty/ywire/wire-statemachine.h>

#include <lz4frame.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/yplatform/pty.h>
#include <yetty/yterm/terminal.h>
#include <yetty/ytrace/ytrace.h>

#define OSC_RING_INITIAL_CAP 4096u    /* must be power of 2 */
#define OSC_HANDLERS_INITIAL_CAP 4u
#define OSC_ARGS_B64_MAX 1024u
#define OSC_ARGS_RAW_MAX (OSC_ARGS_B64_MAX * 3u / 4u)
#define OSC_OUT_CARRY_CAP (256u * 1024u) /* fits a 64KB LZ4F block + amplification */

enum scan_state {
    SCAN_RAW = 0,        /* outside any envelope; default_layer dispatch */
    SCAN_AFTER_ESC,      /* saw ESC in RAW; next byte decides */
    SCAN_OSC_CODE,       /* parsing decimal code digits */
    SCAN_OSC_ARGS,       /* between first `;` and second `;` (b64-args) */
    SCAN_OSC_BODY,       /* after second `;`; current_layer dispatch */
    SCAN_OSC_BODY_ESC,   /* saw ESC inside body; next byte decides terminator */
};

struct osc_handler {
    int code;
    struct yetty_yrender_terminal_layer *layer;
};

struct yetty_ywire_wire_statemachine {
    /* Non-owning. */
    struct yetty_platform_pty *pty;

    /* Input ring (power-of-2 cap; monotonic positions; mask on access). */
    uint8_t *ring;
    size_t ring_cap;
    size_t read_pos;
    size_t write_pos;

    /* Framer state. */
    enum scan_state state;
    int current_code;
    struct yetty_yrender_terminal_layer *current_layer;

    /* Default sink for SCAN_RAW. */
    struct yetty_yrender_terminal_layer *default_layer;

    /* code → layer registry (linear scan; small N). */
    struct osc_handler *handlers;
    size_t handler_count;
    size_t handler_cap;

    /* Args slot. Raw b64 chars accumulate here until the second `;`,
     * then we b64-decode once into args_decoded. Tiny by protocol. */
    char args_b64[OSC_ARGS_B64_MAX];
    size_t args_b64_len;
    uint8_t args_decoded[OSC_ARGS_RAW_MAX];
    size_t args_decoded_len;

    /* Payload decode pipeline: streaming b64 then optional LZ4F.
     * lz4_mode is decided once the first chunk of b64-decoded bytes is
     * available, by sniffing the LZ4F frame magic (0x184D2204). YAML /
     * CLEAR payloads are b64-only and pass straight through. */
    uint8_t b64_carry[4];
    uint8_t b64_carry_n;
    LZ4F_decompressionContext_t lz4_ctx; /* lazy alloc, reset per envelope */
    int lz4_mode;                        /* 1 if first 4 decoded bytes were LZ4 magic */
    int lz4_mode_known;                  /* 0 until first decoded byte arrives */
    int lz4_drain_done;                  /* set once LZ4F drain has finished */
    int b64_eos;                         /* saw `=` in payload b64 stream */

    /* Decoded-output carry: bytes the decoder produced but the layer
     * hasn't pulled yet. Drained first by osc_statemachine_read. */
    uint8_t out_carry[OSC_OUT_CARRY_CAP];
    size_t out_carry_head;
    size_t out_carry_tail;

    /* True while a layer's process_input is on the stack. */
    int dispatching;

    /* Scratch flag: scanner consumed the terminator while inside a
     * read(). Set inside read(), checked by process() after dispatch. */
    int terminator_seen;
};

/*===========================================================================
 * Ring helpers
 *=========================================================================*/

static size_t ring_avail(const struct yetty_ywire_wire_statemachine *statemachine)
{
    return statemachine->write_pos - statemachine->read_pos;
}

static size_t round_pow2(size_t n)
{
    size_t r = 1;
    while (r < n) {
        r <<= 1;
    }
    return r;
}

static struct yetty_ycore_void_result ring_grow_to(
    struct yetty_ywire_wire_statemachine *statemachine, size_t new_min)
{
    size_t avail = ring_avail(statemachine);
    size_t new_cap = statemachine->ring_cap ? statemachine->ring_cap : OSC_RING_INITIAL_CAP;
    if (new_cap < new_min) {
        new_cap = round_pow2(new_min);
    }
    if (new_cap == statemachine->ring_cap && statemachine->ring) {
        return YETTY_OK_VOID();
    }
    uint8_t *nb = malloc(new_cap);
    if (!nb) {
        return YETTY_ERR(yetty_ycore_void, "osc_statemachine: ring grow malloc failed");
    }
    if (statemachine->ring && avail > 0) {
        size_t mask = statemachine->ring_cap - 1;
        size_t off = statemachine->read_pos & mask;
        size_t first = statemachine->ring_cap - off;
        if (first >= avail) {
            memcpy(nb, statemachine->ring + off, avail);
        } else {
            memcpy(nb, statemachine->ring + off, first);
            memcpy(nb + first, statemachine->ring, avail - first);
        }
    }
    free(statemachine->ring);
    statemachine->ring = nb;
    statemachine->ring_cap = new_cap;
    statemachine->read_pos = 0;
    statemachine->write_pos = avail;
    return YETTY_OK_VOID();
}

static uint8_t ring_at(const struct yetty_ywire_wire_statemachine *statemachine, size_t pos)
{
    return statemachine->ring[pos & (statemachine->ring_cap - 1)];
}

/*===========================================================================
 * Out-carry (decoded bytes produced but not yet pulled)
 *=========================================================================*/

static size_t out_carry_avail(const struct yetty_ywire_wire_statemachine *statemachine)
{
    return statemachine->out_carry_tail - statemachine->out_carry_head;
}

static void out_carry_reset(struct yetty_ywire_wire_statemachine *statemachine)
{
    statemachine->out_carry_head = 0;
    statemachine->out_carry_tail = 0;
}

static size_t out_carry_drain(struct yetty_ywire_wire_statemachine *statemachine,
                              uint8_t *dst, size_t n)
{
    size_t have = out_carry_avail(statemachine);
    size_t take = have < n ? have : n;
    if (take == 0) {
        return 0;
    }
    memcpy(dst, statemachine->out_carry + statemachine->out_carry_head, take);
    statemachine->out_carry_head += take;
    if (statemachine->out_carry_head == statemachine->out_carry_tail) {
        out_carry_reset(statemachine);
    }
    return take;
}

/* Append `n` decoded bytes to the carry. Caller must have called
 * out_carry_avail and ensured space (carry is sized for one LZ4F block). */
static struct yetty_ycore_void_result out_carry_append(
    struct yetty_ywire_wire_statemachine *statemachine, const uint8_t *src, size_t n)
{
    if (statemachine->out_carry_tail + n > sizeof(statemachine->out_carry)) {
        /* Compact: shift live bytes to start. */
        size_t live = out_carry_avail(statemachine);
        if (live > 0) {
            memmove(statemachine->out_carry, statemachine->out_carry + statemachine->out_carry_head, live);
        }
        statemachine->out_carry_head = 0;
        statemachine->out_carry_tail = live;
        if (live + n > sizeof(statemachine->out_carry)) {
            return YETTY_ERR(yetty_ycore_void, "osc_statemachine: out carry overflow");
        }
    }
    memcpy(statemachine->out_carry + statemachine->out_carry_tail, src, n);
    statemachine->out_carry_tail += n;
    return YETTY_OK_VOID();
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

/* Decode the accumulated args_b64 into args_decoded (one shot). */
static void decode_args_slot(struct yetty_ywire_wire_statemachine *statemachine)
{
    statemachine->args_decoded_len = 0;
    size_t b64n = statemachine->args_b64_len;
    while (b64n > 0 && statemachine->args_b64[b64n - 1] == '=') {
        b64n--;
    }
    size_t pos = 0;
    while (pos + 4 <= b64n) {
        uint8_t triple[3];
        if (!b64_decode_quartet(statemachine->args_b64 + pos, triple)) {
            break;
        }
        if (statemachine->args_decoded_len + 3 > sizeof(statemachine->args_decoded)) {
            break;
        }
        memcpy(statemachine->args_decoded + statemachine->args_decoded_len, triple, 3);
        statemachine->args_decoded_len += 3;
        pos += 4;
    }
    if (b64n - pos >= 2 && statemachine->args_decoded_len + 2 <= sizeof(statemachine->args_decoded)) {
        char tail[4] = {
            statemachine->args_b64[pos], statemachine->args_b64[pos + 1],
            b64n - pos >= 3 ? statemachine->args_b64[pos + 2] : 'A', 'A',
        };
        uint8_t triple[3];
        if (b64_decode_quartet(tail, triple)) {
            statemachine->args_decoded[statemachine->args_decoded_len++] = triple[0];
            if (b64n - pos >= 3) {
                statemachine->args_decoded[statemachine->args_decoded_len++] = triple[1];
            }
        }
    }
}

/*===========================================================================
 * Handler registry
 *=========================================================================*/

static struct yetty_yrender_terminal_layer *find_layer(
    const struct yetty_ywire_wire_statemachine *statemachine, int code)
{
    for (size_t i = 0; i < statemachine->handler_count; i++) {
        if (statemachine->handlers[i].code == code) {
            return statemachine->handlers[i].layer;
        }
    }
    return NULL;
}

/*===========================================================================
 * Body decode: pump payload bytes from the ring through b64 + lz4 into
 * out_carry. Stops at the body terminator. Returns OK; statemachine->terminator_seen
 * is set when the terminator is consumed.
 *=========================================================================*/

#define LZ4F_FRAME_MAGIC 0x184D2204u

/* Pull ONE chunk of output out of LZ4F (no more input) into out_carry.
 * Sets lz4_drain_done when LZ4F has nothing left. The caller (body_pump)
 * loops on this so the layer can drain out_carry between iterations
 * — large frames decompress to many MB and would overflow the carry
 * if drained in one shot. */
static struct yetty_ycore_void_result lz4_drain_one(
    struct yetty_ywire_wire_statemachine *statemachine)
{
    if (!statemachine->lz4_ctx) {
        statemachine->lz4_drain_done = 1;
        return YETTY_OK_VOID();
    }
    /* Don't run drain if carry can't fit a full chunk — let the layer
     * pull bytes first. */
    size_t free = sizeof(statemachine->out_carry) - out_carry_avail(statemachine);
    if (free < 8192) {
        return YETTY_OK_VOID();
    }
    uint8_t out[8192];
    size_t in_left = 0;
    size_t out_left = sizeof(out);
    size_t r = LZ4F_decompress(statemachine->lz4_ctx, out, &out_left, NULL,
                               &in_left, NULL);
    if (LZ4F_isError(r)) {
        return YETTY_ERR(yetty_ycore_void, LZ4F_getErrorName(r));
    }
    if (out_left > 0) {
        struct yetty_ycore_void_result rr =
            out_carry_append(statemachine, out, out_left);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr,
                            "osc_statemachine: lz4 drain append");
    } else {
        statemachine->lz4_drain_done = 1;
    }
    return YETTY_OK_VOID();
}

/* Push `n` bytes of b64-decoded payload through the per-envelope
 * pipeline (lz4 or passthrough) into out_carry. */
static struct yetty_ycore_void_result push_decoded(
    struct yetty_ywire_wire_statemachine *statemachine, const uint8_t *decoded, size_t n)
{
    if (n == 0) {
        return YETTY_OK_VOID();
    }
    /* Sniff lz4 magic on the first decoded byte. */
    if (!statemachine->lz4_mode_known && n >= 4) {
        uint32_t magic = (uint32_t)decoded[0] | ((uint32_t)decoded[1] << 8) |
                         ((uint32_t)decoded[2] << 16) | ((uint32_t)decoded[3] << 24);
        statemachine->lz4_mode = (magic == LZ4F_FRAME_MAGIC);
        statemachine->lz4_mode_known = 1;
    }
    if (!statemachine->lz4_mode_known) {
        /* Not enough bytes yet to decide — passthrough is wrong if it's
         * actually lz4'd. Hold the bytes in b64_carry path? Simpler: if
         * we got <4 bytes, just buffer; once the next chunk arrives
         * we'll decide. But the only way <4 happens here is if the
         * envelope body is genuinely tiny. Treat as passthrough — yaml
         * payloads are always larger than 4 bytes so this is a no-op
         * for compressed streams. */
        statemachine->lz4_mode_known = 1;
    }

    if (!statemachine->lz4_mode) {
        /* Passthrough: b64-decoded bytes are the final payload. */
        return out_carry_append(statemachine, decoded, n);
    }

    /* LZ4F frame mode. Lazy-alloc context. */
    if (!statemachine->lz4_ctx) {
        LZ4F_errorCode_t err =
            LZ4F_createDecompressionContext(&statemachine->lz4_ctx, LZ4F_VERSION);
        if (LZ4F_isError(err)) {
            return YETTY_ERR(yetty_ycore_void, LZ4F_getErrorName(err));
        }
    }
    size_t in_pos = 0;
    while (in_pos < n) {
        uint8_t out[OSC_OUT_CARRY_CAP];
        size_t in_left = n - in_pos;
        size_t out_left = sizeof(out);
        size_t r = LZ4F_decompress(statemachine->lz4_ctx, out, &out_left,
                                   decoded + in_pos, &in_left, NULL);
        if (LZ4F_isError(r)) {
            return YETTY_ERR(yetty_ycore_void, LZ4F_getErrorName(r));
        }
        if (out_left > 0) {
            struct yetty_ycore_void_result rr =
                out_carry_append(statemachine, out, out_left);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, rr,
                                "osc_statemachine: out_carry append");
        }
        in_pos += in_left;
        if (in_left == 0 && out_left == 0) {
            break;
        }
    }
    return YETTY_OK_VOID();
}

/* Pump up to `want` decoded bytes into out_carry from the body. Stops
 * at the envelope terminator (BEL or ESC \\). Advances read_pos past
 * consumed wire bytes. */
static struct yetty_ycore_void_result body_pump(
    struct yetty_ywire_wire_statemachine *statemachine, size_t want)
{
    /* Cap the in-pass target at half the carry. The carry is bounded
     * (OSC_OUT_CARRY_CAP), but a single prim can be many MB (e.g. yimage's
     * 2.5 MB pixel payload). Without this cap, body_pump would loop trying
     * to satisfy `want = 2.5 MB`, eventually hitting out_carry_append's
     * overflow guard — surfaced as a stream-level error and a dropped
     * frame ("prim_iter: body pull" → "envelope truncated"). The iter
     * already loops calling statemachine_read until its own `filled ==
     * total_size`, so it's fine for body_pump to deliver progress in
     * carry-sized chunks; the caller drains and re-asks. */
    const size_t max_per_pass = sizeof(statemachine->out_carry) / 2;
    if (want > max_per_pass) {
        want = max_per_pass;
    }
    while (out_carry_avail(statemachine) < want) {
        if (statemachine->terminator_seen) {
            /* Bytes after terminator: drain LZ4F incrementally — one
             * chunk per call so the layer has a chance to drain the
             * carry between iterations. */
            if (statemachine->lz4_mode && !statemachine->lz4_drain_done) {
                struct yetty_ycore_void_result dr = lz4_drain_one(statemachine);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, dr,
                                    "osc_statemachine: lz4 drain step");
                if (statemachine->lz4_drain_done) {
                    return YETTY_OK_VOID();
                }
                /* If drain produced 0 bytes (carry full), exit so
                 * layer can drain. */
                if (out_carry_avail(statemachine) >= want) {
                    return YETTY_OK_VOID();
                }
                continue;
            }
            return YETTY_OK_VOID();
        }
        if (statemachine->read_pos >= statemachine->write_pos) {
            return YETTY_OK_VOID(); /* need more wire input */
        }

        /* Step 1: collect up to 256 wire b64 chars, stopping at terminator. */
        uint8_t batch[256];
        size_t batch_n = 0;
        int hit_terminator = 0;
        while (batch_n < sizeof(batch) && statemachine->read_pos < statemachine->write_pos) {
            uint8_t c = ring_at(statemachine, statemachine->read_pos);
            if (c == '\007') {
                statemachine->read_pos++;
                hit_terminator = 1;
                break;
            }
            if (c == '\033') {
                if (statemachine->read_pos + 1 >= statemachine->write_pos) {
                    /* Don't know yet — leave ESC for next pump. */
                    goto out;
                }
                if (ring_at(statemachine, statemachine->read_pos + 1) == '\\') {
                    statemachine->read_pos += 2;
                    hit_terminator = 1;
                    break;
                }
                /* Stray ESC inside body — skip it and continue. */
                statemachine->read_pos++;
                continue;
            }
            batch[batch_n++] = c;
            statemachine->read_pos++;
        }
out:
        if (batch_n == 0 && !hit_terminator) {
            return YETTY_OK_VOID();
        }

        /* Step 2: b64-decode batch with carry from previous call. */
        char full[sizeof(batch) + 4];
        size_t full_n = 0;
        for (size_t i = 0; i < statemachine->b64_carry_n; i++) {
            full[full_n++] = (char)statemachine->b64_carry[i];
        }
        size_t valid_n = 0;
        while (valid_n < batch_n && batch[valid_n] != '=') {
            valid_n++;
        }
        if (valid_n < batch_n) {
            statemachine->b64_eos = 1;
        }
        memcpy(full + full_n, batch, valid_n);
        full_n += valid_n;

        uint8_t decoded[(sizeof(batch) + 4) * 3 / 4 + 4];
        size_t decoded_n = 0;
        size_t quartets = full_n / 4;
        for (size_t q = 0; q < quartets; q++) {
            uint8_t triple[3];
            if (b64_decode_quartet(full + q * 4, triple)) {
                memcpy(decoded + decoded_n, triple, 3);
                decoded_n += 3;
            }
        }
        statemachine->b64_carry_n = (uint8_t)(full_n - quartets * 4);
        for (size_t i = 0; i < statemachine->b64_carry_n; i++) {
            statemachine->b64_carry[i] = (uint8_t)full[quartets * 4 + i];
        }

        /* Step 3: push the decoded chunk through the pipeline. */
        struct yetty_ycore_void_result pr = push_decoded(statemachine, decoded, decoded_n);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, pr, "osc_statemachine: push_decoded");

        /* Step 4: terminator just hit — flush trailing b64 carry (1-3
         * chars left over that decode to 1-2 bytes), then let the outer
         * loop drive incremental lz4 drain on subsequent iterations. */
        if (hit_terminator) {
            if (statemachine->b64_carry_n >= 2) {
                char tail[4] = {
                    (char)statemachine->b64_carry[0],
                    (char)statemachine->b64_carry[1],
                    statemachine->b64_carry_n >= 3 ? (char)statemachine->b64_carry[2] : 'A',
                    'A',
                };
                uint8_t triple[3];
                if (b64_decode_quartet(tail, triple)) {
                    uint8_t out_bytes[2] = {triple[0], triple[1]};
                    size_t out_n = statemachine->b64_carry_n >= 3 ? 2 : 1;
                    struct yetty_ycore_void_result fr =
                        push_decoded(statemachine, out_bytes, out_n);
                    YETTY_RETURN_IF_ERR(yetty_ycore_void, fr,
                                        "osc_statemachine: flush b64 tail");
                }
            }
            statemachine->b64_carry_n = 0;
            statemachine->terminator_seen = 1;
            if (!statemachine->lz4_mode) {
                statemachine->lz4_drain_done = 1;
            }
        }
    }
    return YETTY_OK_VOID();
}

/* Pump raw (non-decoded) bytes from the ring into out_carry up to `want`
 * decoded bytes available — for SCAN_RAW. Stops at the next ESC. */
static struct yetty_ycore_void_result raw_pump(
    struct yetty_ywire_wire_statemachine *statemachine, size_t want)
{
    while (out_carry_avail(statemachine) < want && statemachine->read_pos < statemachine->write_pos) {
        uint8_t c = ring_at(statemachine, statemachine->read_pos);
        if (c == '\033') {
            return YETTY_OK_VOID(); /* leave ESC for the framer */
        }
        struct yetty_ycore_void_result r = out_carry_append(statemachine, &c, 1);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "osc_statemachine: raw out_carry");
        statemachine->read_pos++;
    }
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Dispatch
 *=========================================================================*/

static struct yetty_ycore_void_result dispatch(
    struct yetty_ywire_wire_statemachine *statemachine, struct yetty_yrender_terminal_layer *layer)
{
    if (!layer || !layer->ops || !layer->ops->process_input) {
        return YETTY_OK_VOID();
    }
    statemachine->dispatching = 1;
    struct yetty_ycore_void_result r = layer->ops->process_input(layer, statemachine);
    statemachine->dispatching = 0;
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "osc_statemachine: layer process_input failed");
    return YETTY_OK_VOID();
}

/* Reset per-envelope state at the start of a new envelope. */
static void envelope_reset(struct yetty_ywire_wire_statemachine *statemachine)
{
    statemachine->args_b64_len = 0;
    statemachine->args_decoded_len = 0;
    statemachine->b64_carry_n = 0;
    statemachine->b64_eos = 0;
    statemachine->terminator_seen = 0;
    statemachine->lz4_mode = 0;
    statemachine->lz4_mode_known = 0;
    statemachine->lz4_drain_done = 0;
    out_carry_reset(statemachine);
    if (statemachine->lz4_ctx) {
        LZ4F_resetDecompressionContext(statemachine->lz4_ctx);
    }
}

/*===========================================================================
 * Public API
 *=========================================================================*/

struct yetty_ywire_wire_statemachine_ptr_result yetty_ywire_wire_statemachine_create(
    struct yetty_platform_pty *pty)
{
    struct yetty_ywire_wire_statemachine *statemachine =
        calloc(1, sizeof(struct yetty_ywire_wire_statemachine));
    if (!statemachine) {
        return YETTY_ERR(yetty_ywire_wire_statemachine_ptr, "osc_statemachine: calloc failed");
    }
    statemachine->pty = pty;
    statemachine->state = SCAN_RAW;
    return YETTY_OK(yetty_ywire_wire_statemachine_ptr, statemachine);
}

struct yetty_ycore_void_result yetty_ywire_wire_statemachine_destroy(
    struct yetty_ywire_wire_statemachine *statemachine)
{
    if (!statemachine) {
        return YETTY_ERR(yetty_ycore_void, "osc_statemachine: statemachine is NULL");
    }
    if (statemachine->lz4_ctx) {
        LZ4F_freeDecompressionContext(statemachine->lz4_ctx);
    }
    free(statemachine->ring);
    free(statemachine->handlers);
    free(statemachine);
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ywire_wire_statemachine_register(
    struct yetty_ywire_wire_statemachine *statemachine, int code,
    struct yetty_yrender_terminal_layer *layer)
{
    if (!statemachine) {
        return YETTY_ERR(yetty_ycore_void, "osc_statemachine: statemachine is NULL");
    }
    for (size_t i = 0; i < statemachine->handler_count; i++) {
        if (statemachine->handlers[i].code == code) {
            statemachine->handlers[i].layer = layer;
            return YETTY_OK_VOID();
        }
    }
    if (statemachine->handler_count == statemachine->handler_cap) {
        size_t nc = statemachine->handler_cap ? statemachine->handler_cap * 2 : OSC_HANDLERS_INITIAL_CAP;
        struct osc_handler *nh = realloc(statemachine->handlers, nc * sizeof(struct osc_handler));
        if (!nh) {
            return YETTY_ERR(yetty_ycore_void, "osc_statemachine: handler realloc failed");
        }
        statemachine->handlers = nh;
        statemachine->handler_cap = nc;
    }
    statemachine->handlers[statemachine->handler_count++] =
        (struct osc_handler){.code = code, .layer = layer};
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ywire_wire_statemachine_feed(
    struct yetty_ywire_wire_statemachine *statemachine, const char *bytes, size_t n)
{
    if (!statemachine) {
        return YETTY_ERR(yetty_ycore_void, "osc_statemachine: statemachine is NULL");
    }
    if (!bytes || n == 0) {
        return YETTY_OK_VOID();
    }
    size_t avail = ring_avail(statemachine);
    if (avail + n > statemachine->ring_cap) {
        struct yetty_ycore_void_result r = ring_grow_to(statemachine, avail + n);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "osc_statemachine: feed grow");
    }
    size_t mask = statemachine->ring_cap - 1;
    size_t off = statemachine->write_pos & mask;
    size_t first = statemachine->ring_cap - off;
    if (first >= n) {
        memcpy(statemachine->ring + off, bytes, n);
    } else {
        memcpy(statemachine->ring + off, bytes, first);
        memcpy(statemachine->ring, bytes + first, n - first);
    }
    statemachine->write_pos += n;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ywire_wire_statemachine_set_default(
    struct yetty_ywire_wire_statemachine *statemachine,
    struct yetty_yrender_terminal_layer *layer)
{
    if (!statemachine) {
        return YETTY_ERR(yetty_ycore_void, "osc_statemachine: statemachine is NULL");
    }
    statemachine->default_layer = layer;
    return YETTY_OK_VOID();
}

/* Read PTY bytes into the ring's writable region (zero-copy). */
static struct yetty_ycore_size_result pull_from_pty(
    struct yetty_ywire_wire_statemachine *statemachine)
{
    if (!statemachine->pty || !statemachine->pty->ops || !statemachine->pty->ops->read) {
        return YETTY_ERR(yetty_ycore_size, "osc_statemachine: pty has no read op");
    }
    if (!statemachine->ring) {
        struct yetty_ycore_void_result r = ring_grow_to(statemachine, OSC_RING_INITIAL_CAP);
        YETTY_RETURN_IF_ERR(yetty_ycore_size, r, "osc_statemachine: ring init");
    }
    if (ring_avail(statemachine) == statemachine->ring_cap) {
        struct yetty_ycore_void_result r = ring_grow_to(statemachine, statemachine->ring_cap * 2);
        YETTY_RETURN_IF_ERR(yetty_ycore_size, r, "osc_statemachine: ring grow");
    }
    size_t mask = statemachine->ring_cap - 1;
    size_t off = statemachine->write_pos & mask;
    size_t free_to_end = statemachine->ring_cap - off;
    size_t free_total = statemachine->ring_cap - ring_avail(statemachine);
    size_t max_n = free_to_end < free_total ? free_to_end : free_total;
    if (max_n == 0) {
        return YETTY_OK(yetty_ycore_size, 0);
    }
    struct yetty_ycore_size_result rr =
        statemachine->pty->ops->read(statemachine->pty, (char *)statemachine->ring + off, max_n);
    if (YETTY_IS_ERR(rr)) {
        return YETTY_ERR(yetty_ycore_size, "osc_statemachine: pty read", rr);
    }
    statemachine->write_pos += rr.value;
    return YETTY_OK(yetty_ycore_size, rr.value);
}

struct yetty_ycore_void_result yetty_ywire_wire_statemachine_process(
    struct yetty_ywire_wire_statemachine *statemachine)
{
    if (!statemachine) {
        return YETTY_ERR(yetty_ycore_void, "osc_statemachine: statemachine is NULL");
    }

    /* Pump fresh PTY bytes into the ring (sync-read backends only).
     * libuv-backed PTYs return an error from ops->read; bytes arrive
     * through _feed in that case, so we silently ignore the failure. */
    if (statemachine->pty) {
        for (;;) {
            struct yetty_ycore_size_result rr = pull_from_pty(statemachine);
            if (YETTY_IS_ERR(rr)) {
                yetty_ycore_error_destroy(rr.error);
                break;
            }
            if (rr.value == 0) {
                break;
            }
        }
    }

    while (statemachine->read_pos < statemachine->write_pos || out_carry_avail(statemachine) > 0) {
        switch (statemachine->state) {
        case SCAN_RAW: {
            if (statemachine->default_layer) {
                size_t before_rp = statemachine->read_pos;
                size_t before_oc = out_carry_avail(statemachine);
                struct yetty_ycore_void_result r = dispatch(statemachine, statemachine->default_layer);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "osc_statemachine: default dispatch");
                if (statemachine->read_pos == before_rp && out_carry_avail(statemachine) == before_oc) {
                    /* Layer made no progress — done for this cycle, OR
                     * we're parked at an ESC and need to advance the
                     * scanner. */
                    if (statemachine->read_pos < statemachine->write_pos &&
                        ring_at(statemachine, statemachine->read_pos) == '\033') {
                        statemachine->read_pos++;
                        statemachine->state = SCAN_AFTER_ESC;
                        break;
                    }
                    return YETTY_OK_VOID();
                }
            } else {
                /* No default sink — just skip. */
                while (statemachine->read_pos < statemachine->write_pos &&
                       ring_at(statemachine, statemachine->read_pos) != '\033') {
                    statemachine->read_pos++;
                }
                if (statemachine->read_pos < statemachine->write_pos) {
                    statemachine->read_pos++;
                    statemachine->state = SCAN_AFTER_ESC;
                }
            }
            break;
        }

        case SCAN_AFTER_ESC: {
            if (statemachine->read_pos >= statemachine->write_pos) {
                return YETTY_OK_VOID();
            }
            uint8_t c = ring_at(statemachine, statemachine->read_pos);
            if (c == ']') {
                statemachine->read_pos++;
                statemachine->current_code = 0;
                envelope_reset(statemachine);
                statemachine->state = SCAN_OSC_CODE;
            } else {
                /* Not OSC opener — emit ESC + this byte to default
                 * via out_carry so vterm sees the regular escape. */
                uint8_t emit[2] = {'\033', c};
                struct yetty_ycore_void_result r = out_carry_append(statemachine, emit, 2);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "osc_statemachine: raw esc-pair");
                statemachine->read_pos++;
                statemachine->state = SCAN_RAW;
            }
            break;
        }

        case SCAN_OSC_CODE: {
            if (statemachine->read_pos >= statemachine->write_pos) {
                return YETTY_OK_VOID();
            }
            uint8_t c = ring_at(statemachine, statemachine->read_pos++);
            if (c >= '0' && c <= '9') {
                statemachine->current_code = statemachine->current_code * 10 + (int)(c - '0');
            } else if (c == ';') {
                statemachine->state = SCAN_OSC_ARGS;
            } else {
                ywarn("osc_statemachine: malformed OSC code byte=0x%02x", (unsigned)c);
                statemachine->state = SCAN_RAW;
            }
            break;
        }

        case SCAN_OSC_ARGS: {
            if (statemachine->read_pos >= statemachine->write_pos) {
                return YETTY_OK_VOID();
            }
            uint8_t c = ring_at(statemachine, statemachine->read_pos++);
            if (c == ';') {
                decode_args_slot(statemachine);
                statemachine->current_layer = find_layer(statemachine, statemachine->current_code);
                statemachine->state = SCAN_OSC_BODY;
            } else if (statemachine->args_b64_len < sizeof(statemachine->args_b64)) {
                statemachine->args_b64[statemachine->args_b64_len++] = (char)c;
            }
            break;
        }

        case SCAN_OSC_BODY:
        case SCAN_OSC_BODY_ESC: {
            if (!statemachine->current_layer) {
                /* No registered layer — drain body to terminator. */
                while (!statemachine->terminator_seen && statemachine->read_pos < statemachine->write_pos) {
                    uint8_t c = ring_at(statemachine, statemachine->read_pos++);
                    if (c == '\007') {
                        statemachine->terminator_seen = 1;
                        break;
                    }
                    if (c == '\033') {
                        if (statemachine->read_pos >= statemachine->write_pos) {
                            statemachine->state = SCAN_OSC_BODY_ESC;
                            statemachine->read_pos--; /* unread */
                            return YETTY_OK_VOID();
                        }
                        if (ring_at(statemachine, statemachine->read_pos) == '\\') {
                            statemachine->read_pos++;
                            statemachine->terminator_seen = 1;
                            break;
                        }
                    }
                }
                if (statemachine->terminator_seen) {
                    statemachine->state = SCAN_RAW;
                    statemachine->current_code = 0;
                    envelope_reset(statemachine);
                }
                break;
            }
            size_t before_rp = statemachine->read_pos;
            size_t before_oc = out_carry_avail(statemachine);
            struct yetty_ycore_void_result r = dispatch(statemachine, statemachine->current_layer);
            if (YETTY_IS_ERR(r)) {
                /* Layer failed mid-envelope (e.g. prim_iter saw a bad
                 * envelope magic). Without draining, the next dispatch on
                 * the next ring chunk hands the new (fresh) iter the
                 * MIDDLE of the failed envelope as if it were a new
                 * envelope header — its first 4 bytes look like garbage
                 * to the magic check, the iter errors again, repeat. The
                 * symptom is a stuck UI: ygreeter keeps emitting frames
                 * but none of them ever parse. Drain to the envelope
                 * terminator and reset state so the NEXT envelope on the
                 * wire gets a clean shot. */
                ywarn("osc_statemachine: body dispatch failed (%s) — draining envelope",
                      r.error.msg);
                yetty_ycore_error_destroy(r.error);
                while (!statemachine->terminator_seen &&
                       statemachine->read_pos < statemachine->write_pos) {
                    uint8_t c = ring_at(statemachine, statemachine->read_pos++);
                    if (c == '\007') {
                        statemachine->terminator_seen = 1;
                        break;
                    }
                    if (c == '\033' &&
                        statemachine->read_pos < statemachine->write_pos &&
                        ring_at(statemachine, statemachine->read_pos) == '\\') {
                        statemachine->read_pos++;
                        statemachine->terminator_seen = 1;
                        break;
                    }
                }
                statemachine->state = SCAN_RAW;
                statemachine->current_layer = NULL;
                statemachine->current_code = 0;
                envelope_reset(statemachine);
                break;
            }
            if (statemachine->terminator_seen) {
                statemachine->state = SCAN_RAW;
                statemachine->current_layer = NULL;
                statemachine->current_code = 0;
                envelope_reset(statemachine);
                break;
            }
            if (statemachine->read_pos == before_rp && out_carry_avail(statemachine) == before_oc) {
                /* No progress — exit, caller drives next cycle. */
                return YETTY_OK_VOID();
            }
            return YETTY_OK_VOID(); /* yield after each body dispatch */
        }
        }
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_size_result yetty_ywire_wire_statemachine_read(
    struct yetty_ywire_wire_statemachine *statemachine, uint8_t *dst, size_t n)
{
    if (!statemachine) {
        return YETTY_ERR(yetty_ycore_size, "osc_statemachine: statemachine is NULL");
    }
    if (!dst || n == 0) {
        return YETTY_OK(yetty_ycore_size, 0);
    }

    size_t copied = 0;

    /* Drain any decoded carry first. */
    copied += out_carry_drain(statemachine, dst, n);
    if (copied == n) {
        return YETTY_OK(yetty_ycore_size, copied);
    }

    /* Now pump: in body, run b64+lz4; in raw, passthrough. */
    if (statemachine->state == SCAN_OSC_BODY || statemachine->state == SCAN_OSC_BODY_ESC) {
        if (!statemachine->terminator_seen) {
            struct yetty_ycore_void_result r = body_pump(statemachine, n - copied);
            YETTY_RETURN_IF_ERR(yetty_ycore_size, r, "osc_statemachine: body_pump");
            copied += out_carry_drain(statemachine, dst + copied, n - copied);
        }
    } else if (statemachine->state == SCAN_RAW) {
        struct yetty_ycore_void_result r = raw_pump(statemachine, n - copied);
        YETTY_RETURN_IF_ERR(yetty_ycore_size, r, "osc_statemachine: raw_pump");
        copied += out_carry_drain(statemachine, dst + copied, n - copied);
    }
    return YETTY_OK(yetty_ycore_size, copied);
}

struct yetty_ywire_wire_statemachine_args yetty_ywire_wire_statemachine_args(
    const struct yetty_ywire_wire_statemachine *statemachine)
{
    struct yetty_ywire_wire_statemachine_args view = {NULL, 0};
    if (!statemachine || !statemachine->dispatching) {
        return view;
    }
    if (statemachine->state != SCAN_OSC_BODY && statemachine->state != SCAN_OSC_BODY_ESC) {
        return view;
    }
    view.bytes = statemachine->args_decoded;
    view.len = statemachine->args_decoded_len;
    return view;
}

int yetty_ywire_wire_statemachine_at_end(
    const struct yetty_ywire_wire_statemachine *statemachine)
{
    if (!statemachine || !statemachine->dispatching) {
        return 0;
    }
    return statemachine->terminator_seen;
}

int yetty_ywire_wire_statemachine_code(
    const struct yetty_ywire_wire_statemachine *statemachine)
{
    return statemachine ? statemachine->current_code : 0;
}
