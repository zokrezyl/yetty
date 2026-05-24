/*
 * wire-statemachine.c — envelope (OSC/DCS) framer + decode stack + dispatcher.
 *
 * Pipeline:
 *
 *   PTY → ring → framer → [b64 (args)] | [b64 → lz4 (payload)] → read() → handler
 *
 * SM owns the full decode stack. Handlers register (fn, userdata) against
 * a (kind, code) tuple and pull decoded bytes via _read. See header for
 * the contract.
 */
#include <yetty/ywire/wire-statemachine.h>

#include <lz4frame.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/yplatform/pty.h>
#include <yetty/yplatform/ycoroutine.h>
#include <yetty/ytrace/ytrace.h>

#define OSC_RING_INITIAL_CAP 4096u /* must be power of 2 */
#define OSC_HANDLERS_INITIAL_CAP 4u
#define OSC_ARGS_B64_MAX 1024u
#define OSC_ARGS_RAW_MAX (OSC_ARGS_B64_MAX * 3u / 4u)
#define OSC_OUT_CARRY_CAP (256u * 1024u) /* fits a 64KB LZ4F block + amplification */

/* Sentinel kind value used when no envelope is being dispatched. Picked
 * to be distinct from both ']' and 'P'. */
#define WIRE_KIND_NONE ((enum yetty_ywire_envelope_kind)0)

enum scan_state {
    SCAN_RAW = 0,      /* outside any envelope; default sink dispatch */
    SCAN_AFTER_ESC,    /* saw ESC in RAW; next byte decides */
    SCAN_OSC_CODE,     /* parsing decimal code digits */
    SCAN_OSC_ARGS,     /* between first `;` and second `;` (b64-args) */
    SCAN_OSC_BODY,     /* after second `;`; current handler dispatch */
    SCAN_OSC_BODY_ESC, /* saw ESC inside body; next byte decides terminator */
};

/* Envelope string terminators (ECMA-48 / xterm):
 *   BEL — single-byte terminator (OSC only; xterm extension)
 *   ST  — two-byte `ESC \` (standards-compliant 7-bit form; used by both
 *         OSC and DCS)
 * The framer accepts BEL only for OSC; DCS must use ST. */
enum osc_term {
    OSC_BEL = '\007',
    OSC_ST_ESC = '\033',
    OSC_ST_TAIL = '\\',
};

/* Registered handler entry. Indexed by (kind, code). The fn + userdata
 * pair survives for the lifetime of the SM; the per-handler coroutine
 * is created on first registration and torn down at SM destroy. */
struct wire_handler {
    enum yetty_ywire_envelope_kind kind;
    int code;
    yetty_ywire_process_input_fn fn;
    void *userdata;
};

/* Persistent per-handler coroutine. Deduped by `userdata`: if the same
 * userdata pointer is registered for multiple (kind, code) entries,
 * they all share ONE coro running ONE fn (the one supplied on first
 * registration; callers must be consistent). The coro's process_input
 * runs in a `for(;;)` loop, dispatching by reading the SM's current
 * code/kind on each envelope.
 *
 * Indirection via pointer-table — see the layer_coros comment further
 * below for why we don't store these by value. */
struct handler_coro {
    void *userdata;
    yetty_ywire_process_input_fn fn;
    struct yetty_ywire_wire_statemachine *sm;
    struct yetty_yplatform_coro *coro;
    struct yetty_ycore_void_result result;
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
    enum yetty_ywire_envelope_kind current_kind;
    int current_code;
    struct wire_handler *current_handler; /* NULL if no handler for (kind, code) */

    /* Default sink for SCAN_RAW. */
    yetty_ywire_process_input_fn default_fn;
    void *default_userdata;

    /* (kind, code) → handler registry (linear scan; small N). */
    struct wire_handler *handlers;
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

    /* Decoded-output carry: bytes the decoder produced but the handler
     * hasn't pulled yet. Drained first by _read. */
    uint8_t out_carry[OSC_OUT_CARRY_CAP];
    size_t out_carry_head;
    size_t out_carry_tail;

    /* True while a handler's fn is on the stack. */
    int dispatching;

    /* Scratch flag: scanner consumed the terminator while inside a
     * read(). Set inside read(), checked by process() after dispatch. */
    int terminator_seen;

    /* Persistent SM coroutine — the scanner runs here. Spawned at SM
     * create, destroyed at SM destroy. The PTY-byte-arrival path
     * (`wire_statemachine_feed`) resumes this coro after appending to
     * the ring. When the scanner runs out of work it yields back to
     * the event-loop caller of feed(). */
    struct yetty_yplatform_coro *sm_coro;

    /* Fatal result stash. The sm_coro entry function returns its
     * Result here on a fatal exit. */
    struct yetty_ycore_void_result sm_result;

    /* Per-handler coroutine table. One entry per unique registered
     * userdata pointer (default or any kind/code). Lookup is linear in
     * userdata; N is small.
     *
     * Array of POINTERS to heap-allocated handler_coro structs. The
     * struct itself must not be relocatable, because each spawned
     * coro stores its handler_coro pointer in its closure — a realloc
     * of the storage would silently free-after-use every prior
     * handler's coro state. Indirection costs one extra deref but
     * keeps the structs stable across grow. */
    struct handler_coro **handler_coros;
    size_t handler_coro_count;
    size_t handler_coro_cap;
};

/*===========================================================================
 * Ring helpers
 *=========================================================================*/

static size_t ring_avail(const struct yetty_ywire_wire_statemachine *sm)
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

static struct yetty_ycore_void_result ring_grow_to(struct yetty_ywire_wire_statemachine *sm,
                                                   size_t new_min)
{
    size_t avail = ring_avail(sm);
    size_t new_cap = sm->ring_cap ? sm->ring_cap : OSC_RING_INITIAL_CAP;
    if (new_cap < new_min) {
        new_cap = round_pow2(new_min);
    }
    if (new_cap == sm->ring_cap && sm->ring) {
        return YETTY_OK_VOID();
    }
    uint8_t *nb = malloc(new_cap);
    if (!nb) {
        return YETTY_ERR(yetty_ycore_void, "wire_sm: ring grow malloc failed");
    }
    if (sm->ring && avail > 0) {
        size_t mask = sm->ring_cap - 1;
        size_t off = sm->read_pos & mask;
        size_t first = sm->ring_cap - off;
        if (first >= avail) {
            memcpy(nb, sm->ring + off, avail);
        } else {
            memcpy(nb, sm->ring + off, first);
            memcpy(nb + first, sm->ring, avail - first);
        }
    }
    free(sm->ring);
    sm->ring = nb;
    sm->ring_cap = new_cap;
    sm->read_pos = 0;
    sm->write_pos = avail;
    return YETTY_OK_VOID();
}

static uint8_t ring_at(const struct yetty_ywire_wire_statemachine *sm, size_t pos)
{
    return sm->ring[pos & (sm->ring_cap - 1)];
}

/*===========================================================================
 * Out-carry (decoded bytes produced but not yet pulled)
 *=========================================================================*/

static size_t out_carry_avail(const struct yetty_ywire_wire_statemachine *sm)
{
    return sm->out_carry_tail - sm->out_carry_head;
}

static void out_carry_reset(struct yetty_ywire_wire_statemachine *sm)
{
    sm->out_carry_head = 0;
    sm->out_carry_tail = 0;
}

static size_t out_carry_drain(struct yetty_ywire_wire_statemachine *sm, uint8_t *dst, size_t n)
{
    size_t have = out_carry_avail(sm);
    size_t take = have < n ? have : n;
    if (take == 0) {
        return 0;
    }
    memcpy(dst, sm->out_carry + sm->out_carry_head, take);
    sm->out_carry_head += take;
    if (sm->out_carry_head == sm->out_carry_tail) {
        out_carry_reset(sm);
    }
    return take;
}

/* Append `n` decoded bytes to the carry. */
static struct yetty_ycore_void_result out_carry_append(struct yetty_ywire_wire_statemachine *sm,
                                                       const uint8_t *src, size_t n)
{
    if (sm->out_carry_tail + n > sizeof(sm->out_carry)) {
        size_t live = out_carry_avail(sm);
        if (live > 0) {
            memmove(sm->out_carry, sm->out_carry + sm->out_carry_head, live);
        }
        sm->out_carry_head = 0;
        sm->out_carry_tail = live;
        if (live + n > sizeof(sm->out_carry)) {
            return YETTY_ERR(yetty_ycore_void, "wire_sm: out carry overflow");
        }
    }
    memcpy(sm->out_carry + sm->out_carry_tail, src, n);
    sm->out_carry_tail += n;
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
static void decode_args_slot(struct yetty_ywire_wire_statemachine *sm)
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
        char tail[4] = {
            sm->args_b64[pos],
            sm->args_b64[pos + 1],
            b64n - pos >= 3 ? sm->args_b64[pos + 2] : 'A',
            'A',
        };
        uint8_t triple[3];
        if (b64_decode_quartet(tail, triple)) {
            sm->args_decoded[sm->args_decoded_len++] = triple[0];
            if (b64n - pos >= 3) {
                sm->args_decoded[sm->args_decoded_len++] = triple[1];
            }
        }
    }
}

/*===========================================================================
 * Handler registry
 *=========================================================================*/

static struct wire_handler *find_handler(struct yetty_ywire_wire_statemachine *sm,
                                         enum yetty_ywire_envelope_kind kind, int code)
{
    for (size_t i = 0; i < sm->handler_count; i++) {
        if (sm->handlers[i].kind == kind && sm->handlers[i].code == code) {
            return &sm->handlers[i];
        }
    }
    return NULL;
}

/*===========================================================================
 * Body decode: pump payload bytes from the ring through b64 + lz4 into
 * out_carry. Stops at the body terminator. Returns OK; sm->terminator_seen
 * is set when the terminator is consumed.
 *=========================================================================*/

#define LZ4F_FRAME_MAGIC 0x184D2204u

static struct yetty_ycore_void_result lz4_drain_one(struct yetty_ywire_wire_statemachine *sm)
{
    if (!sm->lz4_ctx) {
        sm->lz4_drain_done = 1;
        return YETTY_OK_VOID();
    }
    size_t free = sizeof(sm->out_carry) - out_carry_avail(sm);
    if (free < 8192) {
        return YETTY_OK_VOID();
    }
    uint8_t out[8192];
    size_t in_left = 0;
    size_t out_left = sizeof(out);
    size_t r = LZ4F_decompress(sm->lz4_ctx, out, &out_left, NULL, &in_left, NULL);
    if (LZ4F_isError(r)) {
        return YETTY_ERR(yetty_ycore_void, LZ4F_getErrorName(r));
    }
    if (out_left > 0) {
        struct yetty_ycore_void_result rr = out_carry_append(sm, out, out_left);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "wire_sm: lz4 drain append");
    } else {
        sm->lz4_drain_done = 1;
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result push_decoded(struct yetty_ywire_wire_statemachine *sm,
                                                   const uint8_t *decoded, size_t n)
{
    if (n == 0) {
        return YETTY_OK_VOID();
    }
    if (!sm->lz4_mode_known && n >= 4) {
        uint32_t magic = (uint32_t)decoded[0] | ((uint32_t)decoded[1] << 8) |
                         ((uint32_t)decoded[2] << 16) | ((uint32_t)decoded[3] << 24);
        sm->lz4_mode = (magic == LZ4F_FRAME_MAGIC);
        sm->lz4_mode_known = 1;
    }
    if (!sm->lz4_mode_known) {
        sm->lz4_mode_known = 1;
    }

    if (!sm->lz4_mode) {
        return out_carry_append(sm, decoded, n);
    }

    if (!sm->lz4_ctx) {
        LZ4F_errorCode_t err = LZ4F_createDecompressionContext(&sm->lz4_ctx, LZ4F_VERSION);
        if (LZ4F_isError(err)) {
            return YETTY_ERR(yetty_ycore_void, LZ4F_getErrorName(err));
        }
    }
    size_t in_pos = 0;
    while (in_pos < n) {
        uint8_t out[OSC_OUT_CARRY_CAP];
        size_t in_left = n - in_pos;
        size_t out_left = sizeof(out);
        size_t r =
            LZ4F_decompress(sm->lz4_ctx, out, &out_left, decoded + in_pos, &in_left, NULL);
        if (LZ4F_isError(r)) {
            return YETTY_ERR(yetty_ycore_void, LZ4F_getErrorName(r));
        }
        if (out_left > 0) {
            struct yetty_ycore_void_result rr = out_carry_append(sm, out, out_left);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "wire_sm: out_carry append");
        }
        in_pos += in_left;
        if (in_left == 0 && out_left == 0) {
            break;
        }
    }
    return YETTY_OK_VOID();
}

/* Pump up to `want` decoded bytes into out_carry from the body. Stops
 * at the envelope terminator (BEL or ST). Advances read_pos past
 * consumed wire bytes. */
static struct yetty_ycore_void_result body_pump(struct yetty_ywire_wire_statemachine *sm,
                                                size_t want)
{
    const size_t max_per_pass = sizeof(sm->out_carry) / 2;
    if (want > max_per_pass) {
        want = max_per_pass;
    }
    while (out_carry_avail(sm) < want) {
        if (sm->terminator_seen) {
            if (sm->lz4_mode && !sm->lz4_drain_done) {
                struct yetty_ycore_void_result dr = lz4_drain_one(sm);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, dr, "wire_sm: lz4 drain step");
                if (sm->lz4_drain_done) {
                    return YETTY_OK_VOID();
                }
                if (out_carry_avail(sm) >= want) {
                    return YETTY_OK_VOID();
                }
                continue;
            }
            return YETTY_OK_VOID();
        }
        if (sm->read_pos >= sm->write_pos) {
            return YETTY_OK_VOID();
        }

        uint8_t batch[256];
        size_t batch_n = 0;
        int hit_terminator = 0;
        while (batch_n < sizeof(batch) && sm->read_pos < sm->write_pos) {
            uint8_t c = ring_at(sm, sm->read_pos);
            /* BEL terminates OSC only (xterm extension). DCS must use ST. */
            if (c == OSC_BEL && sm->current_kind == YETTY_YWIRE_ENVELOPE_OSC) {
                sm->read_pos++;
                hit_terminator = 1;
                break;
            }
            if (c == OSC_ST_ESC) {
                if (sm->read_pos + 1 >= sm->write_pos) {
                    goto out;
                }
                if (ring_at(sm, sm->read_pos + 1) == OSC_ST_TAIL) {
                    sm->read_pos += 2;
                    hit_terminator = 1;
                    break;
                }
                sm->read_pos++;
                continue;
            }
            batch[batch_n++] = c;
            sm->read_pos++;
        }
    out:
        if (batch_n == 0 && !hit_terminator) {
            return YETTY_OK_VOID();
        }

        char full[sizeof(batch) + 4];
        size_t full_n = 0;
        for (size_t i = 0; i < sm->b64_carry_n; i++) {
            full[full_n++] = (char)sm->b64_carry[i];
        }
        size_t valid_n = 0;
        while (valid_n < batch_n && batch[valid_n] != '=') {
            valid_n++;
        }
        if (valid_n < batch_n) {
            sm->b64_eos = 1;
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
        sm->b64_carry_n = (uint8_t)(full_n - quartets * 4);
        for (size_t i = 0; i < sm->b64_carry_n; i++) {
            sm->b64_carry[i] = (uint8_t)full[quartets * 4 + i];
        }

        struct yetty_ycore_void_result pr = push_decoded(sm, decoded, decoded_n);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, pr, "wire_sm: push_decoded");

        if (hit_terminator) {
            if (sm->b64_carry_n >= 2) {
                char tail[4] = {
                    (char)sm->b64_carry[0],
                    (char)sm->b64_carry[1],
                    sm->b64_carry_n >= 3 ? (char)sm->b64_carry[2] : 'A',
                    'A',
                };
                uint8_t triple[3];
                if (b64_decode_quartet(tail, triple)) {
                    uint8_t out_bytes[2] = {triple[0], triple[1]};
                    size_t out_n = sm->b64_carry_n >= 3 ? 2 : 1;
                    struct yetty_ycore_void_result fr = push_decoded(sm, out_bytes, out_n);
                    YETTY_RETURN_IF_ERR(yetty_ycore_void, fr, "wire_sm: flush b64 tail");
                }
            }
            sm->b64_carry_n = 0;
            sm->terminator_seen = 1;
            if (!sm->lz4_mode) {
                sm->lz4_drain_done = 1;
            }
        }
    }
    return YETTY_OK_VOID();
}

/* Pump raw (non-decoded) bytes from the ring into out_carry up to `want`
 * decoded bytes available — for SCAN_RAW. Stops at the next ESC. */
static struct yetty_ycore_void_result raw_pump(struct yetty_ywire_wire_statemachine *sm,
                                               size_t want)
{
    while (out_carry_avail(sm) < want && sm->read_pos < sm->write_pos) {
        uint8_t c = ring_at(sm, sm->read_pos);
        if (c == '\033') {
            return YETTY_OK_VOID();
        }
        struct yetty_ycore_void_result r = out_carry_append(sm, &c, 1);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "wire_sm: raw out_carry");
        sm->read_pos++;
    }
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Coroutines — one per unique userdata pointer. Deduped by userdata so a
 * single consumer registered as default AND for multiple (kind, code)
 * tuples gets ONE persistent coro.
 *=========================================================================*/

static void sm_coro_entry(void *arg);

YETTY_EXTERNAL_CALLBACK
static void handler_coro_entry(void *arg)
{
    struct handler_coro *hc = arg;
    hc->sm->dispatching = 1;
    hc->result = hc->fn(hc->userdata, hc->sm);
    hc->sm->dispatching = 0;
}

static struct handler_coro *find_handler_coro(struct yetty_ywire_wire_statemachine *sm,
                                              void *userdata)
{
    if (!userdata) {
        return NULL;
    }
    for (size_t i = 0; i < sm->handler_coro_count; i++) {
        if (sm->handler_coros[i]->userdata == userdata) {
            return sm->handler_coros[i];
        }
    }
    return NULL;
}

static struct handler_coro *get_or_spawn_handler_coro(struct yetty_ywire_wire_statemachine *sm,
                                                     yetty_ywire_process_input_fn fn,
                                                     void *userdata)
{
    struct handler_coro *existing = find_handler_coro(sm, userdata);
    if (existing) {
        return existing;
    }
    if (!fn || !userdata) {
        return NULL;
    }

    if (sm->handler_coro_count == sm->handler_coro_cap) {
        size_t nc = sm->handler_coro_cap ? sm->handler_coro_cap * 2 : 4;
        struct handler_coro **grown =
            realloc(sm->handler_coros, nc * sizeof(struct handler_coro *));
        if (!grown) {
            return NULL;
        }
        sm->handler_coros = grown;
        sm->handler_coro_cap = nc;
    }
    struct handler_coro *hc = calloc(1, sizeof(struct handler_coro));
    if (!hc) {
        return NULL;
    }
    hc->fn = fn;
    hc->userdata = userdata;
    hc->sm = sm;
    hc->result = YETTY_OK_VOID();
    struct yplatform_coro_ptr_result spawn_res =
        yetty_yplatform_coro_spawn(handler_coro_entry, hc, 1024 * 1024, "wire-handler");
    if (YETTY_IS_ERR(spawn_res)) {
        yetty_ycore_error_destroy(spawn_res.error);
        free(hc);
        return NULL;
    }
    hc->coro = spawn_res.value;
    sm->handler_coros[sm->handler_coro_count++] = hc;
    return hc;
}

static int respawn_handler_coro(struct handler_coro *hc)
{
    if (!hc) {
        return 0;
    }
    if (YETTY_IS_ERR(hc->result)) {
        yetty_ycore_error_destroy(hc->result.error);
    }
    hc->result = YETTY_OK_VOID();
    if (hc->coro) {
        yetty_yplatform_coro_destroy(hc->coro);
        hc->coro = NULL;
    }
    struct yplatform_coro_ptr_result sp =
        yetty_yplatform_coro_spawn(handler_coro_entry, hc, 1024 * 1024, "wire-handler");
    if (YETTY_IS_ERR(sp)) {
        yetty_ycore_error_destroy(sp.error);
        return 0;
    }
    hc->coro = sp.value;
    return 1;
}

static void envelope_reset(struct yetty_ywire_wire_statemachine *sm)
{
    sm->args_b64_len = 0;
    sm->args_decoded_len = 0;
    sm->b64_carry_n = 0;
    sm->b64_eos = 0;
    sm->terminator_seen = 0;
    sm->lz4_mode = 0;
    sm->lz4_mode_known = 0;
    sm->lz4_drain_done = 0;
    out_carry_reset(sm);
    if (sm->lz4_ctx) {
        LZ4F_resetDecompressionContext(sm->lz4_ctx);
    }
}

/*===========================================================================
 * Public API
 *=========================================================================*/

struct yetty_ywire_wire_statemachine_ptr_result yetty_ywire_wire_statemachine_create(
    struct yetty_platform_pty *pty)
{
    struct yetty_ywire_wire_statemachine *sm =
        calloc(1, sizeof(struct yetty_ywire_wire_statemachine));
    if (!sm) {
        return YETTY_ERR(yetty_ywire_wire_statemachine_ptr, "wire_sm: calloc failed");
    }
    sm->pty = pty;
    sm->state = SCAN_RAW;
    sm->current_kind = WIRE_KIND_NONE;

    struct yplatform_coro_ptr_result spawn_res =
        yetty_yplatform_coro_spawn(sm_coro_entry, sm, 1024 * 1024, "wire-sm");
    if (YETTY_IS_ERR(spawn_res)) {
        free(sm);
        return YETTY_ERR(yetty_ywire_wire_statemachine_ptr, "wire_sm: sm_coro spawn",
                         spawn_res);
    }
    sm->sm_coro = spawn_res.value;

    return YETTY_OK(yetty_ywire_wire_statemachine_ptr, sm);
}

struct yetty_ycore_void_result yetty_ywire_wire_statemachine_destroy(
    struct yetty_ywire_wire_statemachine *sm)
{
    if (!sm) {
        return YETTY_ERR(yetty_ycore_void, "wire_sm: sm is NULL");
    }
    for (size_t i = 0; i < sm->handler_coro_count; i++) {
        if (!sm->handler_coros[i]) {
            continue;
        }
        if (sm->handler_coros[i]->coro) {
            yetty_yplatform_coro_destroy(sm->handler_coros[i]->coro);
            sm->handler_coros[i]->coro = NULL;
        }
        free(sm->handler_coros[i]);
    }
    free(sm->handler_coros);
    sm->handler_coros = NULL;
    sm->handler_coro_count = 0;
    sm->handler_coro_cap = 0;

    if (sm->sm_coro) {
        yetty_yplatform_coro_destroy(sm->sm_coro);
        sm->sm_coro = NULL;
    }
    if (sm->lz4_ctx) {
        LZ4F_freeDecompressionContext(sm->lz4_ctx);
    }
    free(sm->ring);
    free(sm->handlers);
    free(sm);
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ywire_wire_statemachine_register(
    struct yetty_ywire_wire_statemachine *sm, enum yetty_ywire_envelope_kind kind, int code,
    yetty_ywire_process_input_fn fn, void *userdata)
{
    if (!sm) {
        return YETTY_ERR(yetty_ycore_void, "wire_sm: sm is NULL");
    }
    if (!fn || !userdata) {
        return YETTY_ERR(yetty_ycore_void, "wire_sm: register: fn and userdata must be non-NULL");
    }
    if (kind != YETTY_YWIRE_ENVELOPE_OSC && kind != YETTY_YWIRE_ENVELOPE_DCS) {
        return YETTY_ERR(yetty_ycore_void, "wire_sm: register: unknown envelope kind");
    }
    for (size_t i = 0; i < sm->handler_count; i++) {
        if (sm->handlers[i].kind == kind && sm->handlers[i].code == code) {
            sm->handlers[i].fn = fn;
            sm->handlers[i].userdata = userdata;
            if (!get_or_spawn_handler_coro(sm, fn, userdata)) {
                return YETTY_ERR(yetty_ycore_void, "wire_sm: handler coro spawn failed");
            }
            return YETTY_OK_VOID();
        }
    }
    if (sm->handler_count == sm->handler_cap) {
        size_t nc = sm->handler_cap ? sm->handler_cap * 2 : OSC_HANDLERS_INITIAL_CAP;
        struct wire_handler *nh = realloc(sm->handlers, nc * sizeof(struct wire_handler));
        if (!nh) {
            return YETTY_ERR(yetty_ycore_void, "wire_sm: handler realloc failed");
        }
        sm->handlers = nh;
        sm->handler_cap = nc;
    }
    sm->handlers[sm->handler_count++] = (struct wire_handler){
        .kind = kind, .code = code, .fn = fn, .userdata = userdata};
    if (!get_or_spawn_handler_coro(sm, fn, userdata)) {
        return YETTY_ERR(yetty_ycore_void, "wire_sm: handler coro spawn failed in register");
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ywire_wire_statemachine_feed(
    struct yetty_ywire_wire_statemachine *sm, const char *bytes, size_t n)
{
    if (!sm) {
        return YETTY_ERR(yetty_ycore_void, "wire_sm: sm is NULL");
    }
    if (!bytes || n == 0) {
        return YETTY_OK_VOID();
    }
    size_t avail = ring_avail(sm);
    if (avail + n > sm->ring_cap) {
        struct yetty_ycore_void_result r = ring_grow_to(sm, avail + n);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "wire_sm: feed grow");
    }
    size_t mask = sm->ring_cap - 1;
    size_t off = sm->write_pos & mask;
    size_t first = sm->ring_cap - off;
    if (first >= n) {
        memcpy(sm->ring + off, bytes, n);
    } else {
        memcpy(sm->ring + off, bytes, first);
        memcpy(sm->ring, bytes + first, n - first);
    }
    sm->write_pos += n;

    if (sm->sm_coro && !yetty_yplatform_coro_is_finished(sm->sm_coro)) {
        yetty_yplatform_coro_resume(sm->sm_coro);
        if (yetty_yplatform_coro_is_finished(sm->sm_coro)) {
            struct yetty_ycore_void_result r = sm->sm_result;
            yetty_yplatform_coro_destroy(sm->sm_coro);
            sm->sm_coro = NULL;

            sm->state = SCAN_RAW;
            sm->current_kind = WIRE_KIND_NONE;
            sm->current_code = 0;
            sm->args_b64_len = 0;
            envelope_reset(sm);
            struct yplatform_coro_ptr_result sp =
                yetty_yplatform_coro_spawn(sm_coro_entry, sm, 1024 * 1024, "wire-sm");
            if (YETTY_IS_OK(sp)) {
                sm->sm_coro = sp.value;
            }
            return r;
        }
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ywire_wire_statemachine_set_default(
    struct yetty_ywire_wire_statemachine *sm, yetty_ywire_process_input_fn fn, void *userdata)
{
    if (!sm) {
        return YETTY_ERR(yetty_ycore_void, "wire_sm: sm is NULL");
    }
    if (!fn || !userdata) {
        return YETTY_ERR(yetty_ycore_void, "wire_sm: set_default: fn and userdata must be non-NULL");
    }
    sm->default_fn = fn;
    sm->default_userdata = userdata;
    if (!get_or_spawn_handler_coro(sm, fn, userdata)) {
        return YETTY_ERR(yetty_ycore_void, "wire_sm: default coro spawn failed");
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_size_result pull_from_pty(struct yetty_ywire_wire_statemachine *sm)
{
    if (!sm->pty || !sm->pty->ops || !sm->pty->ops->read) {
        return YETTY_ERR(yetty_ycore_size, "wire_sm: pty has no read op");
    }
    if (!sm->ring) {
        struct yetty_ycore_void_result r = ring_grow_to(sm, OSC_RING_INITIAL_CAP);
        YETTY_RETURN_IF_ERR(yetty_ycore_size, r, "wire_sm: ring init");
    }
    if (ring_avail(sm) == sm->ring_cap) {
        struct yetty_ycore_void_result r = ring_grow_to(sm, sm->ring_cap * 2);
        YETTY_RETURN_IF_ERR(yetty_ycore_size, r, "wire_sm: ring grow");
    }
    size_t mask = sm->ring_cap - 1;
    size_t off = sm->write_pos & mask;
    size_t free_to_end = sm->ring_cap - off;
    size_t free_total = sm->ring_cap - ring_avail(sm);
    size_t max_n = free_to_end < free_total ? free_to_end : free_total;
    if (max_n == 0) {
        return YETTY_OK(yetty_ycore_size, 0);
    }
    struct yetty_ycore_size_result rr =
        sm->pty->ops->read(sm->pty, (char *)sm->ring + off, max_n);
    if (YETTY_IS_ERR(rr)) {
        return YETTY_ERR(yetty_ycore_size, "wire_sm: pty read", rr);
    }
    sm->write_pos += rr.value;
    return YETTY_OK(yetty_ycore_size, rr.value);
}

/*===========================================================================
 * sm_coro: the scanner loop
 *=========================================================================*/

YETTY_EXTERNAL_CALLBACK
static void sm_coro_entry(void *arg)
{
    struct yetty_ywire_wire_statemachine *sm = arg;

    for (;;) {
        if (sm->pty) {
            for (;;) {
                struct yetty_ycore_size_result rr = pull_from_pty(sm);
                if (YETTY_IS_ERR(rr)) {
                    yetty_ycore_error_destroy(rr.error);
                    break;
                }
                if (rr.value == 0) {
                    break;
                }
            }
        }

        while (sm->read_pos < sm->write_pos || out_carry_avail(sm) > 0) {
            switch (sm->state) {
            case SCAN_RAW: {
                if (sm->read_pos < sm->write_pos && ring_at(sm, sm->read_pos) == '\033') {
                    sm->read_pos++;
                    sm->state = SCAN_AFTER_ESC;
                    break;
                }
                struct handler_coro *hc = find_handler_coro(sm, sm->default_userdata);
                if (hc) {
                    if (!hc->coro || yetty_yplatform_coro_is_finished(hc->coro)) {
                        if (YETTY_IS_ERR(hc->result)) {
                            ywarn("wire: default coro had exited (%s); respawning",
                                  hc->result.error.msg);
                        }
                        if (!respawn_handler_coro(hc)) {
                            sm->sm_result = YETTY_ERR(
                                yetty_ycore_void, "wire: respawn of dead default coro failed");
                            return;
                        }
                    }
                    yetty_yplatform_coro_resume(hc->coro);
                    if (yetty_yplatform_coro_is_finished(hc->coro)) {
                        if (YETTY_IS_ERR(hc->result)) {
                            ywarn("wire: default coro exited mid-stream: %s",
                                  hc->result.error.msg);
                        } else {
                            ywarn("wire: default coro returned cleanly "
                                  "(was supposed to loop forever)");
                        }
                    }
                    break;
                }
                while (sm->read_pos < sm->write_pos && ring_at(sm, sm->read_pos) != '\033') {
                    sm->read_pos++;
                }
                break;
            }

            case SCAN_AFTER_ESC: {
                if (sm->read_pos >= sm->write_pos) {
                    goto wait_more;
                }
                uint8_t c = ring_at(sm, sm->read_pos);
                if (c == (uint8_t)YETTY_YWIRE_ENVELOPE_OSC ||
                    c == (uint8_t)YETTY_YWIRE_ENVELOPE_DCS) {
                    sm->read_pos++;
                    sm->current_kind = (enum yetty_ywire_envelope_kind)c;
                    sm->current_code = 0;
                    envelope_reset(sm);
                    sm->state = SCAN_OSC_CODE;
                } else {
                    uint8_t emit[2] = {'\033', c};
                    struct yetty_ycore_void_result r = out_carry_append(sm, emit, 2);
                    if (YETTY_IS_ERR(r)) {
                        sm->sm_result = YETTY_ERR(yetty_ycore_void, "wire: raw esc-pair", r);
                        return;
                    }
                    sm->read_pos++;
                    sm->state = SCAN_RAW;
                }
                break;
            }

            case SCAN_OSC_CODE: {
                if (sm->read_pos >= sm->write_pos) {
                    goto wait_more;
                }
                uint8_t c = ring_at(sm, sm->read_pos);
                /* BEL terminates OSC only. DCS must use ST. */
                if (c == OSC_BEL && sm->current_kind == YETTY_YWIRE_ENVELOPE_OSC) {
                    sm->read_pos++;
                    sm->state = SCAN_RAW;
                    sm->current_kind = WIRE_KIND_NONE;
                    sm->current_code = 0;
                    envelope_reset(sm);
                    break;
                }
                if (c == OSC_ST_ESC) {
                    if (sm->read_pos + 1 >= sm->write_pos) {
                        goto wait_more;
                    }
                    if (ring_at(sm, sm->read_pos + 1) == OSC_ST_TAIL) {
                        sm->read_pos += 2;
                        sm->state = SCAN_RAW;
                        sm->current_kind = WIRE_KIND_NONE;
                        sm->current_code = 0;
                        envelope_reset(sm);
                        break;
                    }
                }
                sm->read_pos++;
                if (c >= '0' && c <= '9') {
                    sm->current_code = sm->current_code * 10 + (int)(c - '0');
                } else if (c == ';') {
                    sm->state = SCAN_OSC_ARGS;
                } else {
                    ywarn("wire: malformed envelope code byte=0x%02x kind=0x%02x",
                          (unsigned)c, (unsigned)sm->current_kind);
                    sm->state = SCAN_RAW;
                    sm->current_kind = WIRE_KIND_NONE;
                }
                break;
            }

            case SCAN_OSC_ARGS: {
                if (sm->read_pos >= sm->write_pos) {
                    goto wait_more;
                }
                uint8_t c = ring_at(sm, sm->read_pos);
                if (c == OSC_BEL && sm->current_kind == YETTY_YWIRE_ENVELOPE_OSC) {
                    sm->read_pos++;
                    sm->state = SCAN_RAW;
                    sm->current_kind = WIRE_KIND_NONE;
                    sm->current_code = 0;
                    envelope_reset(sm);
                    break;
                }
                if (c == OSC_ST_ESC) {
                    if (sm->read_pos + 1 >= sm->write_pos) {
                        goto wait_more;
                    }
                    if (ring_at(sm, sm->read_pos + 1) == OSC_ST_TAIL) {
                        sm->read_pos += 2;
                        sm->state = SCAN_RAW;
                        sm->current_kind = WIRE_KIND_NONE;
                        sm->current_code = 0;
                        envelope_reset(sm);
                        break;
                    }
                }
                sm->read_pos++;
                if (c == ';') {
                    decode_args_slot(sm);
                    /* Test hook: OSC 99099 synthesises a multi-frame error
                     * chain so the post_fatal_error → ynotify path can be
                     * exercised end-to-end from a child process. */
                    if (sm->current_kind == YETTY_YWIRE_ENVELOPE_OSC &&
                        sm->current_code == 99099) {
                        struct yetty_ycore_void_result inner =
                            YETTY_ERR(yetty_ycore_void, "test trigger: inner cause");
                        struct yetty_ycore_void_result mid =
                            YETTY_ERR(yetty_ycore_void, "test trigger: middle wrap", inner);
                        sm->sm_result = YETTY_ERR(yetty_ycore_void,
                                                  "test trigger: synthetic OSC 99099 error",
                                                  mid);
                        return;
                    }
                    sm->current_handler = find_handler(sm, sm->current_kind, sm->current_code);
                    sm->state = SCAN_OSC_BODY;
                } else if (sm->args_b64_len < sizeof(sm->args_b64)) {
                    sm->args_b64[sm->args_b64_len++] = (char)c;
                }
                break;
            }

            case SCAN_OSC_BODY:
            case SCAN_OSC_BODY_ESC: {
                if (!sm->current_handler) {
                    while (!sm->terminator_seen && sm->read_pos < sm->write_pos) {
                        uint8_t c = ring_at(sm, sm->read_pos++);
                        if (c == OSC_BEL && sm->current_kind == YETTY_YWIRE_ENVELOPE_OSC) {
                            sm->terminator_seen = 1;
                            break;
                        }
                        if (c == OSC_ST_ESC) {
                            if (sm->read_pos >= sm->write_pos) {
                                sm->state = SCAN_OSC_BODY_ESC;
                                sm->read_pos--;
                                goto wait_more;
                            }
                            if (ring_at(sm, sm->read_pos) == OSC_ST_TAIL) {
                                sm->read_pos++;
                                sm->terminator_seen = 1;
                                break;
                            }
                        }
                    }
                    if (sm->terminator_seen) {
                        sm->state = SCAN_RAW;
                        sm->current_kind = WIRE_KIND_NONE;
                        sm->current_code = 0;
                        envelope_reset(sm);
                    }
                    break;
                }

                struct handler_coro *hc = find_handler_coro(sm, sm->current_handler->userdata);
                if (!hc) {
                    sm->sm_result = YETTY_ERR(yetty_ycore_void,
                                              "wire: no persistent coro for envelope handler");
                    return;
                }
                if (!hc->coro || yetty_yplatform_coro_is_finished(hc->coro)) {
                    if (YETTY_IS_ERR(hc->result)) {
                        ywarn("wire: handler coro for kind=0x%02x code=%d had exited (%s); "
                              "respawning and dropping current envelope body",
                              (unsigned)sm->current_kind, sm->current_code,
                              hc->result.error.msg);
                    }
                    if (!respawn_handler_coro(hc)) {
                        sm->sm_result = YETTY_ERR(yetty_ycore_void,
                                                  "wire: respawn of dead handler coro failed");
                        return;
                    }
                    sm->current_handler = NULL;
                    break;
                }

                yetty_yplatform_coro_resume(hc->coro);

                if (yetty_yplatform_coro_is_finished(hc->coro)) {
                    if (YETTY_IS_ERR(hc->result)) {
                        ywarn("wire: handler coro for kind=0x%02x code=%d exited mid-envelope: %s",
                              (unsigned)sm->current_kind, sm->current_code,
                              hc->result.error.msg);
                    } else {
                        ywarn("wire: handler coro for kind=0x%02x code=%d returned cleanly "
                              "(process_input was supposed to loop forever)",
                              (unsigned)sm->current_kind, sm->current_code);
                    }
                    sm->current_handler = NULL;
                    break;
                }

                if (sm->terminator_seen) {
                    sm->state = SCAN_RAW;
                    sm->current_handler = NULL;
                    sm->current_kind = WIRE_KIND_NONE;
                    sm->current_code = 0;
                    envelope_reset(sm);
                    break;
                }
                goto wait_more;
            }
            }
        }

    wait_more:
        yetty_yplatform_coro_yield();
    }
}

struct yetty_ycore_void_result yetty_ywire_wire_statemachine_process(
    struct yetty_ywire_wire_statemachine *sm)
{
    if (!sm) {
        return YETTY_ERR(yetty_ycore_void, "wire_sm: sm is NULL");
    }
    if (!sm->sm_coro) {
        return YETTY_ERR(yetty_ycore_void, "wire_sm: sm_coro not spawned");
    }
    yetty_yplatform_coro_resume(sm->sm_coro);
    if (yetty_yplatform_coro_is_finished(sm->sm_coro)) {
        struct yetty_ycore_void_result r = sm->sm_result;
        yetty_yplatform_coro_destroy(sm->sm_coro);
        sm->sm_coro = NULL;
        return r;
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_size_result yetty_ywire_wire_statemachine_read(
    struct yetty_ywire_wire_statemachine *sm, uint8_t *dst, size_t n)
{
    if (!sm) {
        return YETTY_ERR(yetty_ycore_size, "wire_sm: sm is NULL");
    }
    if (!dst || n == 0) {
        return YETTY_OK(yetty_ycore_size, 0);
    }

    for (;;) {
        size_t copied = 0;

        copied += out_carry_drain(sm, dst, n);
        if (copied > 0) {
            return YETTY_OK(yetty_ycore_size, copied);
        }

        if (sm->state == SCAN_OSC_BODY || sm->state == SCAN_OSC_BODY_ESC) {
            if (!sm->terminator_seen) {
                struct yetty_ycore_void_result r = body_pump(sm, n);
                YETTY_RETURN_IF_ERR(yetty_ycore_size, r, "wire_sm: body_pump");
                copied = out_carry_drain(sm, dst, n);
                if (copied > 0) {
                    return YETTY_OK(yetty_ycore_size, copied);
                }
            }
            if (sm->terminator_seen && out_carry_avail(sm) == 0) {
                return YETTY_OK(yetty_ycore_size, 0);
            }
        } else if (sm->state == SCAN_RAW) {
            struct yetty_ycore_void_result r = raw_pump(sm, n);
            YETTY_RETURN_IF_ERR(yetty_ycore_size, r, "wire_sm: raw_pump");
            copied = out_carry_drain(sm, dst, n);
            if (copied > 0) {
                return YETTY_OK(yetty_ycore_size, copied);
            }
        }
        yetty_yplatform_coro_yield();
    }
}

struct yetty_ywire_wire_statemachine_args yetty_ywire_wire_statemachine_args(
    const struct yetty_ywire_wire_statemachine *sm)
{
    struct yetty_ywire_wire_statemachine_args view = {NULL, 0};
    if (!sm || !sm->dispatching) {
        return view;
    }
    if (sm->state != SCAN_OSC_BODY && sm->state != SCAN_OSC_BODY_ESC) {
        return view;
    }
    view.bytes = sm->args_decoded;
    view.len = sm->args_decoded_len;
    return view;
}

int yetty_ywire_wire_statemachine_at_end(const struct yetty_ywire_wire_statemachine *sm)
{
    if (!sm || !sm->dispatching) {
        return 0;
    }
    return sm->terminator_seen;
}

int yetty_ywire_wire_statemachine_code(const struct yetty_ywire_wire_statemachine *sm)
{
    return sm ? sm->current_code : 0;
}

enum yetty_ywire_envelope_kind yetty_ywire_wire_statemachine_kind(
    const struct yetty_ywire_wire_statemachine *sm)
{
    return sm ? sm->current_kind : WIRE_KIND_NONE;
}
