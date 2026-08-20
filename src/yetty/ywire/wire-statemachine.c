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
#include <yetty/ycore/result.h>

#include <errno.h>
#include <lz4frame.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
typedef long long ssize_t;
#define write(fd, buf, n) _write((fd), (buf), (unsigned int)(n))
#else
#include <unistd.h>
#endif

#include <yetty/ycore/terminal-detect.h>
#include <yetty/ycore/types.h>
#include <yetty/yplatform/pty.h>
#include <yetty/yplatform/ycoroutine.h>
#include <yetty/ytrace/ytrace.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

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

/* Ingest-side tmux-passthrough UNWRAP state. Runs over raw bytes BEFORE
 * they reach the ring/framer: strips `ESC P tmux; … ESC \` wrappers and
 * un-doubles the ESCs inside, so a wrapped envelope parses identically to
 * a bare one. Real tmux already unwraps the through-tmux case; this covers
 * same-process loopback emits (a GUI app under tmux that wraps and then
 * feeds its own SM) and direct pipes. Bare input passes through untouched. */
enum unwrap_state {
    UNWRAP_NORMAL = 0, /* passthrough; watching for ESC */
    UNWRAP_AFTER_ESC,  /* saw ESC; could begin ESC P tmux; */
    UNWRAP_MATCH_TMUX, /* saw ESC P; matching the literal "tmux;" */
    UNWRAP_INNER,      /* inside the wrapper payload; un-doubling ESCs */
    UNWRAP_INNER_ESC,  /* saw ESC inside payload; ESC→one ESC, \ →end */
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
 * is created on first registration and torn down at SM destroy.
 *
 * `has_args` selects the wire shape this (kind, code) uses — see the
 * header. The scanner reads it after the first `;` to decide whether
 * to enter SCAN_OSC_ARGS (consume args between `;`s, decode_args_slot)
 * or jump straight to SCAN_OSC_BODY with an empty args slot. */
struct wire_handler {
    enum yetty_ywire_envelope_kind kind;
    int code;
    int has_args;
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

YETTY_YRESULT_DECLARE(yetty_ywire_handler_coro_ptr, struct handler_coro *);

/* Buffered-handler wrapper. Owned by the SM; freed at destroy. Used by
 * the register_buffered / set_envelope_default_buffered / set_default_buffered
 * sugar layer to drain bytes into a heap buffer and fire a single
 * envelope-level callback. */
struct buffered_handler {
    /* Exactly one of these two is non-NULL: env_cb for envelope sinks,
     * raw_cb for the default-sink path. */
    yetty_ywire_envelope_cb env_cb;
    yetty_ywire_raw_cb raw_cb;
    void *userdata;
    struct yetty_ycore_buffer body; /* envelope body accumulator */
};

struct yetty_ywire_wire_statemachine {
    /* Non-owning. */
    struct yetty_platform_pty *pty;

    /* Input ring (power-of-2 cap; monotonic positions; mask on access). */
    uint8_t *ring;
    size_t ring_cap;
    size_t read_pos;
    size_t write_pos;

    /* Ingest-side tmux-unwrap state (persists across feeds/reads). */
    enum unwrap_state uw_state;
    size_t uw_match; /* chars of "tmux;" matched in UNWRAP_MATCH_TMUX */

    /* Framer state. */
    enum scan_state state;
    enum yetty_ywire_envelope_kind current_kind;
    int current_code;
    struct wire_handler *current_handler; /* NULL if no handler for (kind, code) */

    /* Raw code-section bytes (the decimal code digits) captured as they
     * arrive in SCAN_OSC_CODE. Kept so that a foreign (non-yetty) OSC/DCS
     * control string — XTGETTCAP (ESC P +q …), DECRQSS (ESC P $q …), a
     * standard OSC whose code has no yetty handler — can be re-emitted
     * byte-identical when the framer decides to pass it through to the
     * terminal. Digits only; the introducer (ESC + kind) and the
     * separator / foreign byte are reconstructed at passthrough time. */
    uint8_t code_raw[16];
    size_t code_raw_len;

    /* Default sink for SCAN_RAW. */
    yetty_ywire_process_input_fn default_fn;
    void *default_userdata;

    /* Catch-all envelope handler — fires for any (kind, code) with no
     * specific entry in handlers[]. NULL means "drain & drop".
     * `envelope_default_has_args` is propagated into the synthetic
     * handler slot used at dispatch. */
    yetty_ywire_process_input_fn envelope_default_fn;
    void *envelope_default_userdata;
    int envelope_default_has_args;
    /* Synthetic handler entry that points at the catch-all. Kept on the
     * SM so the dispatcher can drive it through the same code path as
     * regular handlers (one per-userdata coro etc.). Refreshed
     * per-envelope before dispatch. */
    struct wire_handler envelope_default_handler_slot;

    /* (kind, code) → handler registry (linear scan; small N). */
    struct wire_handler *handlers;
    size_t handler_count;
    size_t handler_cap;

    /* Owned buffered-handler wrappers (env_cb / raw_cb). One per
     * register_buffered + set_envelope_default_buffered +
     * set_default_buffered call. Freed at destroy. */
    struct buffered_handler **buffered;
    size_t buffered_count;
    size_t buffered_cap;

    /* === Encoder state — for streaming writes === */
    LZ4F_compressionContext_t enc_ctx;
    uint8_t *enc_scratch;
    size_t enc_scratch_cap;
    int enc_active;
    int enc_compressed;
    int enc_tmux_wrap; /* this envelope is wrapped in ESC P tmux; … ESC \ */
    uint8_t enc_b64_carry[2];
    uint8_t enc_b64_carry_n;
    struct yetty_ycore_buffer *enc_out_buf; /* borrowed during write */

    /* tmux passthrough — cached once per SM. True only when running inside
     * tmux (TERM_PROGRAM=tmux; see yetty_ywire_tmux_passthrough_active). In
     * that case tmux parses/
     * re-renders pane output and will NOT forward our OSC/DCS envelopes to
     * the outer terminal — except the `ESC P tmux; <payload, every ESC
     * doubled> ESC \` wrapper, which it unwraps verbatim (with
     * `allow-passthrough on`). We wrap every emitted envelope here so
     * producers need no per-call-site logic; the receive side
     * (ingest_unwrap) strips it back. Directly under yetty (no tmux) this is
     * false and envelopes go out bare. */
    int tmux_wrap;
    int tmux_wrap_known;

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

    /* === Rolling envelope traffic stats ===
     *
     * Per-envelope accumulator: bytes_pushed counts every byte that
     * crosses out of the decoder into out_carry while state is one of
     * the OSC_BODY* values. Reset (and committed when terminator_seen
     * was set) in envelope_reset — the single chokepoint hit on every
     * envelope-end transition (successful or aborted alike).
     *
     * stats_buckets is a 60-second ring indexed by epoch_second % 60.
     * Each bucket stamps its own epoch_second so the snapshot can
     * skip stale slots without an extra "is this fresh" flag.
     *
     * stats_last_logged_second is the heartbeat watermark — every time
     * a commit lands in a strictly newer second than this, we emit one
     * ydebug line with the snapshot, then update the watermark. */
    uint64_t stats_current_envelope_bytes;
    struct {
        int64_t epoch_second; /* 0 = unused */
        uint64_t count;
        uint64_t bytes;
    } stats_buckets[60];
    int64_t stats_last_logged_second;

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
    /* Stats hook — every byte that crosses into out_carry during an
     * envelope dispatch is a decoded body byte the handler will read.
     * Raw-passthrough paths (SCAN_RAW, ESC-pair fallback in
     * SCAN_AFTER_ESC) also land here, but they're filtered out by the
     * state check — they're not envelope traffic. */
    if (sm->state == SCAN_OSC_BODY || sm->state == SCAN_OSC_BODY_ESC) {
        sm->stats_current_envelope_bytes += n;
    }
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Rolling envelope traffic stats — bucket maintenance + heartbeat
 *=========================================================================*/

static int64_t stats_now_seconds(void)
{
    /* Direct monotonic-clock call so ywire stays free of any yplatform
     * link dep (small tools — yecho, yvideo, ycat — pull ywire in
     * standalone). 1-second resolution is enough for the bucket grid. */
#ifdef _WIN32
    return (int64_t)(GetTickCount64() / 1000ULL);
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (int64_t)ts.tv_sec;
#endif
}

/* Forward — defined below; stats_commit_envelope uses it for the
 * per-second ydebug heartbeat. */
static struct yetty_ywire_stats_snapshot stats_snapshot_inline(
    const struct yetty_ywire_wire_statemachine *sm);

static void stats_commit_envelope(struct yetty_ywire_wire_statemachine *sm)
{
    int64_t now_s = stats_now_seconds();
    size_t idx = (size_t)((now_s % 60 + 60) % 60); /* defensive on negative */
    if (sm->stats_buckets[idx].epoch_second != now_s) {
        sm->stats_buckets[idx].epoch_second = now_s;
        sm->stats_buckets[idx].count = 0;
        sm->stats_buckets[idx].bytes = 0;
    }
    sm->stats_buckets[idx].count += 1;
    sm->stats_buckets[idx].bytes += sm->stats_current_envelope_bytes;

    if (now_s > sm->stats_last_logged_second) {
        struct yetty_ywire_stats_snapshot s = stats_snapshot_inline(sm);
        uint64_t avg_1 = s.count_1s ? s.bytes_1s / s.count_1s : 0;
        uint64_t avg_10 = s.count_10s ? s.bytes_10s / s.count_10s : 0;
        uint64_t avg_60 = s.count_60s ? s.bytes_60s / s.count_60s : 0;
        ydebug("ywire stats sm=%p | 1s: %llu env / %llu B (avg %llu) | "
               "10s: %llu env / %llu B (avg %llu) | "
               "60s: %llu env / %llu B (avg %llu)",
               (void *)sm, (unsigned long long)s.count_1s, (unsigned long long)s.bytes_1s,
               (unsigned long long)avg_1, (unsigned long long)s.count_10s,
               (unsigned long long)s.bytes_10s, (unsigned long long)avg_10,
               (unsigned long long)s.count_60s, (unsigned long long)s.bytes_60s,
               (unsigned long long)avg_60);
        sm->stats_last_logged_second = now_s;
    }
}

static struct yetty_ywire_stats_snapshot stats_snapshot_inline(
    const struct yetty_ywire_wire_statemachine *sm)
{
    struct yetty_ywire_stats_snapshot snap = {0};
    if (!sm) {
        return snap;
    }
    int64_t now_s = stats_now_seconds();
    for (int i = 0; i < 60; i++) {
        int64_t age = now_s - sm->stats_buckets[i].epoch_second;
        if (sm->stats_buckets[i].epoch_second == 0 || age < 0 || age >= 60) {
            continue;
        }
        if (age < 1) {
            snap.count_1s += sm->stats_buckets[i].count;
            snap.bytes_1s += sm->stats_buckets[i].bytes;
        }
        if (age < 10) {
            snap.count_10s += sm->stats_buckets[i].count;
            snap.bytes_10s += sm->stats_buckets[i].bytes;
        }
        snap.count_60s += sm->stats_buckets[i].count;
        snap.bytes_60s += sm->stats_buckets[i].bytes;
    }
    return snap;
}

struct yetty_ywire_stats_snapshot yetty_ywire_wire_statemachine_stats_snapshot(
    const struct yetty_ywire_wire_statemachine *sm)
{
    return stats_snapshot_inline(sm);
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
        size_t r = LZ4F_decompress(sm->lz4_ctx, out, &out_left, decoded + in_pos, &in_left, NULL);
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

        if (hit_terminator) {
            /* The terminator bytes are already consumed (read_pos advanced
             * past them in the batch loop). Record that the frame is
             * terminated NOW, before decoding the payload: a payload decode
             * error (e.g. a corrupt LZ4 stream) must not lose the framing
             * boundary. If it did, the recovery drain-and-drop would keep
             * scanning past this frame and swallow the FOLLOWING envelope. */
            sm->terminator_seen = 1;
            if (!sm->lz4_mode) {
                sm->lz4_drain_done = 1;
            }
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

YETTY_EXTERNAL_CALLBACK
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

static struct yetty_ywire_handler_coro_ptr_result get_or_spawn_handler_coro(
    struct yetty_ywire_wire_statemachine *sm, yetty_ywire_process_input_fn fn, void *userdata)
{
    struct handler_coro *existing = find_handler_coro(sm, userdata);
    if (existing) {
        return YETTY_OK(yetty_ywire_handler_coro_ptr, existing);
    }
    if (!fn || !userdata) {
        return YETTY_ERR(yetty_ywire_handler_coro_ptr,
                         "wire_sm: get_or_spawn: fn and userdata must be non-NULL");
    }

    if (sm->handler_coro_count == sm->handler_coro_cap) {
        size_t nc = sm->handler_coro_cap ? sm->handler_coro_cap * 2 : 4;
        struct handler_coro **grown =
            realloc(sm->handler_coros, nc * sizeof(struct handler_coro *));
        if (!grown) {
            return YETTY_ERR(yetty_ywire_handler_coro_ptr,
                             "wire_sm: get_or_spawn: handler_coros realloc failed");
        }
        sm->handler_coros = grown;
        sm->handler_coro_cap = nc;
    }
    struct handler_coro *hc = calloc(1, sizeof(struct handler_coro));
    if (!hc) {
        return YETTY_ERR(yetty_ywire_handler_coro_ptr, "wire_sm: get_or_spawn: calloc failed");
    }
    hc->fn = fn;
    hc->userdata = userdata;
    hc->sm = sm;
    hc->result = YETTY_OK_VOID();
    struct yplatform_coro_ptr_result spawn_res =
        yetty_yplatform_coro_spawn(handler_coro_entry, hc, 1024 * 1024, "wire-handler");
    if (YETTY_IS_ERR(spawn_res)) {
        free(hc);
        return YETTY_ERR(yetty_ywire_handler_coro_ptr, "wire_sm: get_or_spawn: coro spawn failed",
                         spawn_res);
    }
    hc->coro = spawn_res.value;
    sm->handler_coros[sm->handler_coro_count++] = hc;
    return YETTY_OK(yetty_ywire_handler_coro_ptr, hc);
}

static struct yetty_ycore_void_result respawn_handler_coro(struct handler_coro *hc)
{
    if (!hc) {
        return YETTY_ERR(yetty_ycore_void, "wire_sm: respawn: hc is NULL");
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
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sp, "wire_sm: respawn: coro spawn failed");
    hc->coro = sp.value;
    return YETTY_OK_VOID();
}

static void envelope_reset(struct yetty_ywire_wire_statemachine *sm)
{
    /* Stats commit point — only successful envelopes (terminator seen)
     * land in the bucket. Aborted ones (mid-envelope errors,
     * resync-on-ESC) drop their accumulated byte count silently. */
    if (sm->terminator_seen) {
        stats_commit_envelope(sm);
    }
    sm->stats_current_envelope_bytes = 0;

    sm->args_b64_len = 0;
    sm->args_decoded_len = 0;
    sm->code_raw_len = 0;
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

/* A foreign (non-yetty) OSC/DCS control string has reached the framer:
 * its code section is not a yetty envelope — either the byte after the
 * code is non-numeric (XTGETTCAP `ESC P +q`, DECRQSS `ESC P $q`, an OSC
 * whose first code byte is not a digit) or the code parses cleanly but
 * no handler is registered for it (standard OSC 0/1/2/7/8/52/133, …).
 *
 * Re-emit the introducer (ESC + kind) + the code digits consumed so far
 * + `tail` (the non-numeric byte, or the code separator) into the raw
 * out-carry, then drop to SCAN_RAW. The remainder of the string (body +
 * ST/BEL terminator) then relays byte-identical to the terminal through
 * the normal raw path — a foreign control string is just `ESC <bytes>
 * ST`, which SCAN_RAW + SCAN_AFTER_ESC already pass through verbatim.
 * libvterm frames and answers (or gracefully ignores) it natively.
 *
 * envelope_reset() wipes out-carry as its last act, so it MUST run before
 * the re-emitted bytes are appended — hence the explicit ordering here. */
static struct yetty_ycore_void_result passthrough_foreign_control(
    struct yetty_ywire_wire_statemachine *sm, uint8_t tail)
{
    uint8_t relay[2 + sizeof(sm->code_raw) + 1];
    size_t relay_len = 0;
    relay[relay_len++] = '\033';
    relay[relay_len++] = (uint8_t)sm->current_kind;
    memcpy(relay + relay_len, sm->code_raw, sm->code_raw_len);
    relay_len += sm->code_raw_len;
    relay[relay_len++] = tail;

    sm->current_kind = WIRE_KIND_NONE;
    sm->current_code = 0;
    envelope_reset(sm);
    sm->state = SCAN_RAW;
    return out_carry_append(sm, relay, relay_len);
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

    /* The scanner coro is spawned lazily on first feed/process — emit-only
     * callers (yetty_ywire_emit, transient SM) never need it and skip
     * the 1MB stack alloc. */
    return YETTY_OK(yetty_ywire_wire_statemachine_ptr, sm);
}

/* Spawn the scanner coro if it isn't running yet. */
static struct yetty_ycore_void_result ensure_sm_coro(struct yetty_ywire_wire_statemachine *sm)
{
    if (sm->sm_coro) {
        return YETTY_OK_VOID();
    }
    struct yplatform_coro_ptr_result spawn_res =
        yetty_yplatform_coro_spawn(sm_coro_entry, sm, 1024 * 1024, "wire-sm");
    if (YETTY_IS_ERR(spawn_res)) {
        return YETTY_ERR(yetty_ycore_void, "wire_sm: sm_coro spawn", spawn_res);
    }
    sm->sm_coro = spawn_res.value;
    return YETTY_OK_VOID();
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
    if (sm->enc_ctx) {
        LZ4F_freeCompressionContext(sm->enc_ctx);
        sm->enc_ctx = NULL;
    }
    free(sm->enc_scratch);
    sm->enc_scratch = NULL;
    /* Free buffered-handler wrappers (the handler coros they back have
     * already been destroyed by the handler_coros loop above). */
    for (size_t i = 0; i < sm->buffered_count; i++) {
        if (sm->buffered[i]) {
            yetty_ycore_buffer_destroy(&sm->buffered[i]->body);
            free(sm->buffered[i]);
        }
    }
    free(sm->buffered);
    sm->buffered = NULL;
    sm->buffered_count = 0;
    sm->buffered_cap = 0;
    free(sm->ring);
    free(sm->handlers);
    free(sm);
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ywire_wire_statemachine_register(
    struct yetty_ywire_wire_statemachine *sm, enum yetty_ywire_envelope_kind kind, int code,
    int has_args, yetty_ywire_process_input_fn fn, void *userdata)
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
            sm->handlers[i].has_args = has_args ? 1 : 0;
            sm->handlers[i].fn = fn;
            sm->handlers[i].userdata = userdata;
            struct yetty_ywire_handler_coro_ptr_result coro_res =
                get_or_spawn_handler_coro(sm, fn, userdata);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, coro_res, "wire_sm: handler coro spawn failed");
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
        .kind = kind, .code = code, .has_args = has_args ? 1 : 0, .fn = fn, .userdata = userdata};
    struct yetty_ywire_handler_coro_ptr_result coro_res =
        get_or_spawn_handler_coro(sm, fn, userdata);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, coro_res,
                        "wire_sm: handler coro spawn failed in register");
    return YETTY_OK_VOID();
}

/* Append raw bytes to the input ring, growing as needed. */
static struct yetty_ycore_void_result ring_put(struct yetty_ywire_wire_statemachine *sm,
                                               const uint8_t *bytes, size_t n)
{
    if (n == 0) {
        return YETTY_OK_VOID();
    }
    size_t avail = ring_avail(sm);
    if (avail + n > sm->ring_cap) {
        struct yetty_ycore_void_result r = ring_grow_to(sm, avail + n);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "wire_sm: ring_put grow");
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
    return YETTY_OK_VOID();
}

/* Run raw input through the tmux-passthrough UNWRAP state machine, then
 * append the result to the ring. Strips `ESC P tmux; … ESC \` wrappers and
 * un-doubles ESCs inside; bare input passes through unchanged. See the
 * unwrap_state enum. NORMAL/INNER bulk-copy runs up to the next ESC so the
 * common (unwrapped) path stays cheap. */
static struct yetty_ycore_void_result ingest_unwrap(struct yetty_ywire_wire_statemachine *sm,
                                                    const uint8_t *bytes, size_t n)
{
    static const char tmux_lit[] = "tmux;"; /* 5 chars, after ESC P */
    size_t i = 0;
    while (i < n) {
        switch (sm->uw_state) {
        case UNWRAP_NORMAL: {
            size_t j = i;
            while (j < n && bytes[j] != 0x1b) {
                j++;
            }
            struct yetty_ycore_void_result r = ring_put(sm, bytes + i, j - i);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "wire_sm: unwrap normal");
            if (j < n) {
                sm->uw_state = UNWRAP_AFTER_ESC;
                i = j + 1;
            } else {
                i = j;
            }
            break;
        }
        case UNWRAP_AFTER_ESC: {
            uint8_t c = bytes[i++];
            if (c == 'P') {
                sm->uw_state = UNWRAP_MATCH_TMUX;
                sm->uw_match = 0;
            } else {
                /* Not our wrapper opener — pass the held ESC through. If c
                 * is itself ESC, hold it again; else emit ESC + c. */
                uint8_t pair[2] = {0x1b, c};
                struct yetty_ycore_void_result r = ring_put(sm, pair, (c == 0x1b) ? 1 : 2);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "wire_sm: unwrap esc");
                sm->uw_state = (c == 0x1b) ? UNWRAP_AFTER_ESC : UNWRAP_NORMAL;
            }
            break;
        }
        case UNWRAP_MATCH_TMUX: {
            uint8_t c = bytes[i++];
            if (sm->uw_match < 5 && c == (uint8_t)tmux_lit[sm->uw_match]) {
                sm->uw_match++;
                if (sm->uw_match == 5) {
                    sm->uw_state = UNWRAP_INNER;
                }
            } else {
                /* Bare DCS (ESC P <code>…), not a wrapper. Flush ESC P plus
                 * the matched prefix, then reprocess the current byte. */
                uint8_t buf[8];
                size_t m = 0;
                buf[m++] = 0x1b;
                buf[m++] = 'P';
                for (size_t k = 0; k < sm->uw_match; k++) {
                    buf[m++] = (uint8_t)tmux_lit[k];
                }
                struct yetty_ycore_void_result r = ring_put(sm, buf, m);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "wire_sm: unwrap match flush");
                if (c == 0x1b) {
                    sm->uw_state = UNWRAP_AFTER_ESC;
                } else {
                    sm->uw_state = UNWRAP_NORMAL;
                    struct yetty_ycore_void_result r2 = ring_put(sm, &c, 1);
                    YETTY_RETURN_IF_ERR(yetty_ycore_void, r2, "wire_sm: unwrap match byte");
                }
            }
            break;
        }
        case UNWRAP_INNER: {
            size_t j = i;
            while (j < n && bytes[j] != 0x1b) {
                j++;
            }
            struct yetty_ycore_void_result r = ring_put(sm, bytes + i, j - i);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "wire_sm: unwrap inner");
            if (j < n) {
                sm->uw_state = UNWRAP_INNER_ESC;
                i = j + 1;
            } else {
                i = j;
            }
            break;
        }
        case UNWRAP_INNER_ESC: {
            uint8_t c = bytes[i++];
            if (c == 0x1b) {
                uint8_t esc = 0x1b; /* doubled ESC → single */
                struct yetty_ycore_void_result r = ring_put(sm, &esc, 1);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "wire_sm: unwrap inner dbl");
                sm->uw_state = UNWRAP_INNER;
            } else if (c == '\\') {
                sm->uw_state = UNWRAP_NORMAL; /* wrapper terminator — drop */
            } else {
                uint8_t pair[2] = {0x1b, c};
                struct yetty_ycore_void_result r = ring_put(sm, pair, 2);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "wire_sm: unwrap inner esc");
                sm->uw_state = UNWRAP_INNER;
            }
            break;
        }
        }
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
    {
        struct yetty_ycore_void_result r = ingest_unwrap(sm, (const uint8_t *)bytes, n);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "wire_sm: feed ingest");
    }

    /* Lazy-spawn the scanner — emit-only callers may have never
     * triggered it. */
    {
        struct yetty_ycore_void_result sr = ensure_sm_coro(sm);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "wire_sm: feed: ensure_sm_coro");
    }

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
        return YETTY_ERR(yetty_ycore_void,
                         "wire_sm: set_default: fn and userdata must be non-NULL");
    }
    sm->default_fn = fn;
    sm->default_userdata = userdata;
    struct yetty_ywire_handler_coro_ptr_result coro_res =
        get_or_spawn_handler_coro(sm, fn, userdata);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, coro_res, "wire_sm: default coro spawn failed");
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
    /* Read into a scratch buffer, then run the bytes through the tmux-unwrap
     * filter on their way into the ring (ring_put grows as needed). The
     * filter must see a contiguous run, so we can't read straight into the
     * (wrapping) ring anymore. */
    char buf[16384];
    struct yetty_ycore_size_result rr = sm->pty->ops->read(sm->pty, buf, sizeof(buf));
    if (YETTY_IS_ERR(rr)) {
        return YETTY_ERR(yetty_ycore_size, "wire_sm: pty read", rr);
    }
    if (rr.value > 0) {
        struct yetty_ycore_void_result r = ingest_unwrap(sm, (const uint8_t *)buf, rr.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_size, r, "wire_sm: pty ingest");
    }
    return YETTY_OK(yetty_ycore_size, rr.value);
}

/*===========================================================================
 * sm_coro: the scanner loop
 *=========================================================================*/

YETTY_EXTERNAL_CALLBACK
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
                    /* Flush any pending raw-passthrough bytes to the sink
                     * before entering a new escape sequence. envelope_reset()
                     * wipes the out-carry at envelope start, so a foreign
                     * control string relayed immediately before a yetty
                     * envelope would otherwise lose its trailing ST. Only
                     * defer when a default sink coro exists to drain it — with
                     * no consumer the bytes cannot drain, so take the ESC
                     * transition (dropping them, as before) rather than spin. */
                    if (out_carry_avail(sm) == 0 || !find_handler_coro(sm, sm->default_userdata)) {
                        sm->read_pos++;
                        sm->state = SCAN_AFTER_ESC;
                        break;
                    }
                    /* else fall through to resume the default coro, which
                     * drains out_carry; the ESC is handled next iteration. */
                }
                struct handler_coro *hc = find_handler_coro(sm, sm->default_userdata);
                if (hc) {
                    if (!hc->coro || yetty_yplatform_coro_is_finished(hc->coro)) {
                        if (YETTY_IS_ERR(hc->result)) {
                            ywarn("wire: default coro had exited (%s); respawning",
                                  hc->result.error.msg);
                        }
                        struct yetty_ycore_void_result respawn_res = respawn_handler_coro(hc);
                        if (YETTY_IS_ERR(respawn_res)) {
                            sm->sm_result =
                                YETTY_ERR(yetty_ycore_void,
                                          "wire: respawn of dead default coro failed", respawn_res);
                            return;
                        }
                    }
                    yetty_yplatform_coro_resume(hc->coro);
                    if (yetty_yplatform_coro_is_finished(hc->coro)) {
                        if (YETTY_IS_ERR(hc->result)) {
                            ywarn("wire: default coro exited mid-stream: %s", hc->result.error.msg);
                        } else {
                            ywarn("wire: default coro returned cleanly "
                                  "(was supposed to loop forever)");
                        }
                    }
                    break;
                }
                /* No default input consumer registered. Raw bytes have nowhere
                 * to go, so drop up to the next ESC. But if the ring is already
                 * drained there is nothing to consume here: SCAN_RAW never
                 * drains out_carry (only _read() does), so looping while
                 * out_carry_avail() > 0 with no consumer would busy-loop
                 * forever. Yield so process() returns and any buffered raw
                 * bytes can be pulled via _read() (or dropped at destroy). */
                if (sm->read_pos >= sm->write_pos) {
                    goto wait_more;
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
                } else if (c == '\033') {
                    /* Doubled ESC (ESC ESC …): the first ESC is a stray/aborted
                     * sequence. Emit it as raw and re-hold THIS ESC (stay in
                     * SCAN_AFTER_ESC) so it can still introduce the following
                     * sequence — otherwise an envelope introduced right after a
                     * dangling ESC (e.g. a truncated prior frame) is swallowed
                     * as a raw ESC-pair and lost. */
                    uint8_t esc = '\033';
                    struct yetty_ycore_void_result r = out_carry_append(sm, &esc, 1);
                    if (YETTY_IS_ERR(r)) {
                        sm->sm_result = YETTY_ERR(yetty_ycore_void, "wire: raw esc", r);
                        return;
                    }
                    sm->read_pos++;
                    /* stay in SCAN_AFTER_ESC with this ESC held */
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
                    /* A bare ESC that is not ST starts a NEW escape sequence.
                     * The current envelope's code section is therefore
                     * truncated/malformed; abandon it and reprocess this ESC as
                     * a fresh introducer rather than swallowing it as a code
                     * byte — swallowing it would desync (and drop) the envelope
                     * that this ESC actually introduces. */
                    sm->read_pos++;
                    sm->state = SCAN_AFTER_ESC;
                    sm->current_kind = WIRE_KIND_NONE;
                    sm->current_code = 0;
                    envelope_reset(sm);
                    break;
                }
                sm->read_pos++;
                /* End-of-code separator: `;` for OSC, the DCS final byte
                 * for DCS. After it, the args/body section is identical
                 * for both kinds (the inner `;` between args and payload
                 * is the same). See the header for the DCS rationale. */
                int code_end = (sm->current_kind == YETTY_YWIRE_ENVELOPE_DCS)
                                   ? (c == YETTY_YWIRE_DCS_FINAL)
                                   : (c == ';');
                if (c >= '0' && c <= '9') {
                    if (sm->code_raw_len < sizeof(sm->code_raw)) {
                        sm->code_raw[sm->code_raw_len++] = c;
                    }
                    sm->current_code = sm->current_code * 10 + (int)(c - '0');
                } else if (code_end) {
                    /* Resolve handler at the code separator. The handler's
                     * has_args flag drives whether we consume an args
                     * slot (one more `;` later) or jump straight into
                     * the body. Unknown (kind, code) with no
                     * envelope_default falls back to has_args=0 — the
                     * body is drained-and-dropped without trying to
                     * find a second `;` inside it. */
                    sm->current_handler = find_handler(sm, sm->current_kind, sm->current_code);
                    if (!sm->current_handler && sm->envelope_default_fn) {
                        sm->envelope_default_handler_slot.kind = sm->current_kind;
                        sm->envelope_default_handler_slot.code = sm->current_code;
                        sm->envelope_default_handler_slot.has_args = sm->envelope_default_has_args;
                        sm->envelope_default_handler_slot.fn = sm->envelope_default_fn;
                        sm->envelope_default_handler_slot.userdata = sm->envelope_default_userdata;
                        sm->current_handler = &sm->envelope_default_handler_slot;
                    }
                    /* Test hook: OSC 99099 synthesises a multi-frame
                     * error chain so the post_fatal_error → ynotify
                     * path can be exercised end-to-end from a child
                     * process. Same intent as the old SCAN_OSC_ARGS
                     * hook; fires earlier now. */
                    if (sm->current_kind == YETTY_YWIRE_ENVELOPE_OSC && sm->current_code == 99099) {
                        struct yetty_ycore_void_result inner =
                            YETTY_ERR(yetty_ycore_void, "test trigger: inner cause");
                        struct yetty_ycore_void_result mid =
                            YETTY_ERR(yetty_ycore_void, "test trigger: middle wrap", inner);
                        sm->sm_result = YETTY_ERR(yetty_ycore_void,
                                                  "test trigger: synthetic OSC 99099 error", mid);
                        return;
                    }
                    if (!sm->current_handler) {
                        /* Numeric code, valid separator, but no registered
                         * handler and no catch-all: a foreign OSC/DCS
                         * (standard OSC 0/1/2/7/8/52/133, …) rather than a
                         * yetty envelope. Relay the whole string to the
                         * terminal untouched instead of draining-and-dropping
                         * it — libvterm handles or ignores it gracefully. */
                        struct yetty_ycore_void_result relay = passthrough_foreign_control(sm, c);
                        if (YETTY_IS_ERR(relay)) {
                            sm->sm_result = YETTY_ERR(yetty_ycore_void,
                                                      "wire: foreign envelope passthrough", relay);
                            return;
                        }
                    } else if (sm->current_handler->has_args) {
                        sm->state = SCAN_OSC_ARGS;
                    } else {
                        sm->state = SCAN_OSC_BODY;
                    }
                } else {
                    /* Not a yetty envelope: the byte after the code section
                     * is neither a digit nor the code separator (XTGETTCAP
                     * `+`, DECRQSS `$`, or a non-numeric OSC code byte).
                     * Re-emit the introducer + consumed bytes and relay the
                     * foreign control string through untouched rather than
                     * swallowing the introducer and leaking its tail as
                     * screen text. */
                    struct yetty_ycore_void_result relay = passthrough_foreign_control(sm, c);
                    if (YETTY_IS_ERR(relay)) {
                        sm->sm_result =
                            YETTY_ERR(yetty_ycore_void, "wire: foreign control passthrough", relay);
                        return;
                    }
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
                    /* Bare ESC (not ST) mid-args → new escape sequence. Abandon
                     * this truncated envelope and reprocess the ESC as a fresh
                     * introducer so the next envelope is not desynced. */
                    sm->read_pos++;
                    sm->state = SCAN_AFTER_ESC;
                    sm->current_kind = WIRE_KIND_NONE;
                    sm->current_code = 0;
                    envelope_reset(sm);
                    break;
                }
                sm->read_pos++;
                if (c == ';') {
                    /* Second `;` — args slot complete. Handler was
                     * resolved at the first `;`; only thing left is to
                     * decode the args buffer and step to the body. */
                    decode_args_slot(sm);
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
                        break;
                    }
                    /* Ran out of ring input before the terminator. Nothing in
                     * this drain-and-drop path consumes out_carry, so yield
                     * rather than spin on out_carry_avail() > 0 with no more
                     * input to make progress against. */
                    goto wait_more;
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
                              (unsigned)sm->current_kind, sm->current_code, hc->result.error.msg);
                    }
                    struct yetty_ycore_void_result respawn_res = respawn_handler_coro(hc);
                    if (YETTY_IS_ERR(respawn_res)) {
                        sm->sm_result =
                            YETTY_ERR(yetty_ycore_void, "wire: respawn of dead handler coro failed",
                                      respawn_res);
                        return;
                    }
                    sm->current_handler = NULL;
                    break;
                }

                yetty_yplatform_coro_resume(hc->coro);

                if (yetty_yplatform_coro_is_finished(hc->coro)) {
                    if (YETTY_IS_ERR(hc->result)) {
                        ywarn("wire: handler coro for kind=0x%02x code=%d exited mid-envelope: %s",
                              (unsigned)sm->current_kind, sm->current_code, hc->result.error.msg);
                    } else {
                        ywarn("wire: handler coro for kind=0x%02x code=%d returned cleanly "
                              "(process_input was supposed to loop forever)",
                              (unsigned)sm->current_kind, sm->current_code);
                    }
                    /* Respawn the coro NOW so the NEXT envelope gets a live
                     * handler. If we left it finished, the lazy respawn at the
                     * top of the next envelope's body would fire and drop that
                     * envelope's body — i.e. one valid envelope after every
                     * handler error would be silently lost. */
                    struct yetty_ycore_void_result respawn_res = respawn_handler_coro(hc);
                    if (YETTY_IS_ERR(respawn_res)) {
                        sm->sm_result =
                            YETTY_ERR(yetty_ycore_void,
                                      "wire: respawn after mid-envelope exit failed", respawn_res);
                        return;
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
    {
        struct yetty_ycore_void_result sr = ensure_sm_coro(sm);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "wire_sm: process: ensure_sm_coro");
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
            /* Keep pumping after the terminator while the LZ4F decoder still
             * holds decompressed tail bytes — reporting end-of-envelope with
             * bytes stuck in the decompressor silently truncated the last
             * record of every lz4 envelope whose final flush didn't coincide
             * with a carry drain. */
            if (!sm->terminator_seen || (sm->lz4_mode && !sm->lz4_drain_done)) {
                struct yetty_ycore_void_result r = body_pump(sm, n);
                YETTY_RETURN_IF_ERR(yetty_ycore_size, r, "wire_sm: body_pump");
                copied = out_carry_drain(sm, dst, n);
                if (copied > 0) {
                    return YETTY_OK(yetty_ycore_size, copied);
                }
            }
            if (sm->terminator_seen && out_carry_avail(sm) == 0 &&
                (!sm->lz4_mode || sm->lz4_drain_done)) {
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
    /* An lz4 envelope is not over until the decompressor's tail has been
     * drained into out_carry AND consumed — see the read() end condition. */
    if (sm->lz4_mode && !sm->lz4_drain_done) {
        return 0;
    }
    return sm->terminator_seen && out_carry_avail(sm) == 0;
}

int yetty_ywire_wire_statemachine_code(const struct yetty_ywire_wire_statemachine *sm)
{
    return sm ? sm->current_code : 0;
}

int yetty_ywire_wire_statemachine_idle(const struct yetty_ywire_wire_statemachine *sm)
{
    if (!sm) {
        return 1;
    }
    /* GROUND on both layers: the scanner outside any envelope (no partial
     * escape pending) AND the tmux-unwrap passthrough not inside a wrapper.
     * This is the framed boundary a teardown drain may stop reading at —
     * the message handler fires BEFORE the envelope's ST terminator bytes
     * are consumed, so "completion reached" alone still leaves frame tail
     * bytes queued ahead of any raw user input. */
    return sm->state == SCAN_RAW && sm->uw_state == UNWRAP_NORMAL;
}

enum yetty_ywire_envelope_kind yetty_ywire_wire_statemachine_kind(
    const struct yetty_ywire_wire_statemachine *sm)
{
    return sm ? sm->current_kind : WIRE_KIND_NONE;
}

/*===========================================================================
 * Streaming write — start_write / write / finish_write
 *
 * Mirrors the receive pipeline but in reverse:
 *
 *   src bytes → [LZ4F encode]? → streaming b64 → out_buf
 *
 * b64 carry holds 0..2 bytes between writes; LZ4F context spans the
 * envelope. Both reset per envelope via start_write / finish_write.
 *=========================================================================*/

#define ENC_SCRATCH_DEFAULT (64 * 1024) /* one LZ4 block worth */

int yetty_ywire_tmux_passthrough_active(void)
{
    /* "Am I running inside tmux, so my output must be passthrough-wrapped to
     * reach yetty?" — TRUE iff TERM_PROGRAM=tmux. tmux sets that for its own
     * panes; in yetty's model a tmux is hosted by yetty, so it re-renders
     * pane output and will NOT forward our OSC/DCS envelopes to the outer
     * terminal except the `ESC P tmux; <payload, every ESC doubled> ESC \`
     * wrapper (which it unwraps verbatim with `allow-passthrough on`).
     * Directly under yetty (TERM_PROGRAM=yetty) there is no multiplexer to
     * wrap around, so we emit bare envelopes. */
    return yetty_term_program_is_tmux();
}

/* tmux passthrough wrapping. Lazily decide (and cache) whether emitted
 * envelopes must be wrapped so they survive a tmux session. See the
 * tmux_wrap field comment on the SM struct. */
static int sm_tmux_wrap(struct yetty_ywire_wire_statemachine *sm)
{
    if (!sm->tmux_wrap_known) {
        sm->tmux_wrap = yetty_ywire_tmux_passthrough_active();
        sm->tmux_wrap_known = 1;
    }
    return sm->tmux_wrap;
}

struct yetty_ycore_void_result yetty_ywire_tmux_wrap(const char *seq, size_t len,
                                                     struct yetty_ycore_buffer *out_buf)
{
    if (!out_buf) {
        return YETTY_ERR(yetty_ycore_void, "ywire_tmux_wrap: out_buf is NULL");
    }
    if (!seq || len == 0) {
        return YETTY_OK_VOID();
    }
    /* Not under tmux → emit verbatim; the host terminal sees the raw
     * control sequence directly. */
    if (!yetty_ywire_tmux_passthrough_active()) {
        return yetty_ycore_buffer_write(out_buf, seq, len);
    }
    /* Under tmux → wrap in ESC P tmux; … ESC \ with every ESC in the body
     * doubled, so tmux (with allow-passthrough) re-emits the original
     * sequence verbatim to the outer terminal (yetty). This is the only
     * way a raw control sequence (e.g. the DEC ?1500/?1501 card-mouse
     * enable) survives a tmux session. */
    struct yetty_ycore_void_result r = yetty_ycore_buffer_write(out_buf, "\033Ptmux;", 7);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "ywire_tmux_wrap: prefix");
    size_t run_start = 0;
    for (size_t i = 0; i < len; i++) {
        if ((unsigned char)seq[i] == 0x1b) {
            if (i > run_start) {
                r = yetty_ycore_buffer_write(out_buf, seq + run_start, i - run_start);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "ywire_tmux_wrap: run");
            }
            r = yetty_ycore_buffer_write(out_buf, "\033\033", 2);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "ywire_tmux_wrap: esc");
            run_start = i + 1;
        }
    }
    if (len > run_start) {
        r = yetty_ycore_buffer_write(out_buf, seq + run_start, len - run_start);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "ywire_tmux_wrap: tail");
    }
    return yetty_ycore_buffer_write(out_buf, "\033\\", 2);
}

static const char b64_alpha[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static struct yetty_ycore_void_result b64_emit_triple(struct yetty_ycore_buffer *out, uint8_t a,
                                                      uint8_t b, uint8_t c)
{
    char chars[4];
    uint32_t v = ((uint32_t)a << 16) | ((uint32_t)b << 8) | (uint32_t)c;
    chars[0] = b64_alpha[(v >> 18) & 0x3F];
    chars[1] = b64_alpha[(v >> 12) & 0x3F];
    chars[2] = b64_alpha[(v >> 6) & 0x3F];
    chars[3] = b64_alpha[v & 0x3F];
    return yetty_ycore_buffer_write(out, chars, 4);
}

/* Push `len` bytes through the streaming b64 encoder into sm->enc_out_buf. */
static struct yetty_ycore_void_result b64_encode_push(struct yetty_ywire_wire_statemachine *sm,
                                                      const uint8_t *src, size_t len)
{
    size_t i = 0;
    /* Drain carry first: combine with new input to form full triples. */
    while (sm->enc_b64_carry_n > 0 && i < len) {
        if (sm->enc_b64_carry_n == 1 && i + 1 < len) {
            struct yetty_ycore_void_result r =
                b64_emit_triple(sm->enc_out_buf, sm->enc_b64_carry[0], src[i], src[i + 1]);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "wire_sm: b64 triple");
            i += 2;
            sm->enc_b64_carry_n = 0;
        } else if (sm->enc_b64_carry_n == 2 && i < len) {
            struct yetty_ycore_void_result r = b64_emit_triple(
                sm->enc_out_buf, sm->enc_b64_carry[0], sm->enc_b64_carry[1], src[i]);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "wire_sm: b64 triple");
            i += 1;
            sm->enc_b64_carry_n = 0;
        } else {
            break; /* need more bytes to complete a triple */
        }
    }
    /* Whole triples from src[i..]. */
    while (i + 3 <= len) {
        struct yetty_ycore_void_result r =
            b64_emit_triple(sm->enc_out_buf, src[i], src[i + 1], src[i + 2]);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "wire_sm: b64 triple");
        i += 3;
    }
    /* Leftover into carry. */
    while (i < len && sm->enc_b64_carry_n < 2) {
        sm->enc_b64_carry[sm->enc_b64_carry_n++] = src[i++];
    }
    if (i != len) {
        return YETTY_ERR(yetty_ycore_void, "wire_sm: b64 carry overflow");
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result b64_encode_flush(struct yetty_ywire_wire_statemachine *sm)
{
    if (sm->enc_b64_carry_n == 0) {
        return YETTY_OK_VOID();
    }
    char chars[4];
    if (sm->enc_b64_carry_n == 1) {
        uint32_t v = (uint32_t)sm->enc_b64_carry[0] << 16;
        chars[0] = b64_alpha[(v >> 18) & 0x3F];
        chars[1] = b64_alpha[(v >> 12) & 0x3F];
        chars[2] = '=';
        chars[3] = '=';
    } else { /* 2 */
        uint32_t v = ((uint32_t)sm->enc_b64_carry[0] << 16) | ((uint32_t)sm->enc_b64_carry[1] << 8);
        chars[0] = b64_alpha[(v >> 18) & 0x3F];
        chars[1] = b64_alpha[(v >> 12) & 0x3F];
        chars[2] = b64_alpha[(v >> 6) & 0x3F];
        chars[3] = '=';
    }
    sm->enc_b64_carry_n = 0;
    return yetty_ycore_buffer_write(sm->enc_out_buf, chars, 4);
}

static struct yetty_ycore_void_result ensure_enc_scratch(struct yetty_ywire_wire_statemachine *sm,
                                                         size_t need)
{
    if (need <= sm->enc_scratch_cap) {
        return YETTY_OK_VOID();
    }
    size_t new_cap = sm->enc_scratch_cap ? sm->enc_scratch_cap : 1024;
    while (new_cap < need) {
        new_cap *= 2;
    }
    uint8_t *p = realloc(sm->enc_scratch, new_cap);
    if (!p) {
        return YETTY_ERR(yetty_ycore_void, "wire_sm: enc scratch realloc");
    }
    sm->enc_scratch = p;
    sm->enc_scratch_cap = new_cap;
    return YETTY_OK_VOID();
}

/* Abandon the in-flight envelope after an encode failure so a long-lived SM
 * stays usable for the next start_write. The LZ4F context may be mid-frame
 * (undefined state after an error) — drop it; start_write lazily recreates
 * it. Partial envelope bytes already appended to the caller's out_buf are
 * the caller's to discard. */
static void enc_abort(struct yetty_ywire_wire_statemachine *sm)
{
    if (sm->enc_ctx) {
        LZ4F_freeCompressionContext(sm->enc_ctx);
        sm->enc_ctx = NULL;
    }
    sm->enc_active = 0;
    sm->enc_b64_carry_n = 0;
    sm->enc_out_buf = NULL;
}

/* Body of start_write — every encoder-state mutation happens in here; the
 * public wrapper aborts the envelope on any failure. */
static struct yetty_ycore_void_result start_write_engage(struct yetty_ywire_wire_statemachine *sm,
                                                         enum yetty_ywire_envelope_kind kind,
                                                         int code, int has_args, int compressed,
                                                         const void *args, size_t args_len,
                                                         struct yetty_ycore_buffer *out_buf)
{
    sm->enc_out_buf = out_buf;
    sm->enc_b64_carry_n = 0;
    sm->enc_compressed = compressed ? 1 : 0;
    sm->enc_tmux_wrap = sm_tmux_wrap(sm);

    /* tmux passthrough prefix. When wrapping, the whole envelope becomes the
     * data string of `ESC P tmux; … ESC \`, and every ESC inside it must be
     * doubled. Our envelope contains ESC only at the opener and terminator
     * (code/args/body are base64 + digits — no ESC), so doubling is just:
     * emit `ESC P tmux;` then one extra ESC here (the first of the doubled
     * opener pair; the hdr below supplies the second), and a doubled
     * terminator in finish_write. */
    if (sm->enc_tmux_wrap) {
        struct yetty_ycore_void_result r = yetty_ycore_buffer_write(out_buf, "\033Ptmux;\033", 8);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "wire_sm: start_write: tmux prefix");
    }

    /* Wire shape (sep = `;` for OSC, the DCS final byte for DCS — see
     * the header for why DCS needs a real final byte after the code):
     *   has_args=0 → "ESC <kind> <code> <sep> <b64+lz4 body> ESC \\"
     *   has_args=1 → "ESC <kind> <code> <sep> <b64 args> ; <b64+lz4 body> ESC \\"
     * Either way the prefix `ESC <kind> <code> <sep>` is written first;
     * the args section + closing `;` are only added when has_args. */
    char sep = (kind == YETTY_YWIRE_ENVELOPE_DCS) ? YETTY_YWIRE_DCS_FINAL : ';';
    char hdr[32];
    int n = snprintf(hdr, sizeof(hdr), "\033%c%d%c", (char)kind, code, sep);
    if (n <= 0 || (size_t)n >= sizeof(hdr)) {
        return YETTY_ERR(yetty_ycore_void, "wire_sm: start_write: bad code");
    }
    {
        struct yetty_ycore_void_result r = yetty_ycore_buffer_write(out_buf, hdr, (size_t)n);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "wire_sm: start_write: hdr");
    }
    if (has_args) {
        if (args && args_len > 0) {
            struct yetty_ycore_void_result r = b64_encode_push(sm, (const uint8_t *)args, args_len);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "wire_sm: start_write: args push");
            r = b64_encode_flush(sm);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "wire_sm: start_write: args flush");
            sm->enc_b64_carry_n = 0;
        }
        struct yetty_ycore_void_result r = yetty_ycore_buffer_write(out_buf, ";", 1);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "wire_sm: start_write: args close");
    }

    if (!compressed) {
        sm->enc_active = 1;
        return YETTY_OK_VOID();
    }

    /* Compressed: lazy-alloc LZ4F context + scratch, write frame header
     * through the b64 encoder. */
    {
        struct yetty_ycore_void_result r = ensure_enc_scratch(sm, ENC_SCRATCH_DEFAULT);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "wire_sm: start_write: scratch");
    }
    if (!sm->enc_ctx) {
        LZ4F_errorCode_t err = LZ4F_createCompressionContext(&sm->enc_ctx, LZ4F_VERSION);
        if (LZ4F_isError(err)) {
            return YETTY_ERR(yetty_ycore_void, LZ4F_getErrorName(err));
        }
    }
    LZ4F_preferences_t prefs = {0};
    size_t hn = LZ4F_compressBegin(sm->enc_ctx, sm->enc_scratch, sm->enc_scratch_cap, &prefs);
    if (LZ4F_isError(hn)) {
        return YETTY_ERR(yetty_ycore_void, LZ4F_getErrorName(hn));
    }
    if (hn > 0) {
        struct yetty_ycore_void_result r = b64_encode_push(sm, sm->enc_scratch, hn);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "wire_sm: start_write: lz4 frame header");
    }
    sm->enc_active = 1;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ywire_wire_statemachine_start_write(
    struct yetty_ywire_wire_statemachine *sm, enum yetty_ywire_envelope_kind kind, int code,
    int has_args, int compressed, const void *args, size_t args_len,
    struct yetty_ycore_buffer *out_buf)
{
    if (!sm) {
        return YETTY_ERR(yetty_ycore_void, "wire_sm: sm is NULL");
    }
    if (!out_buf) {
        return YETTY_ERR(yetty_ycore_void, "wire_sm: start_write: out_buf is NULL");
    }
    if (sm->enc_active) {
        return YETTY_ERR(yetty_ycore_void, "wire_sm: start_write: already active");
    }
    if (kind != YETTY_YWIRE_ENVELOPE_OSC && kind != YETTY_YWIRE_ENVELOPE_DCS) {
        return YETTY_ERR(yetty_ycore_void, "wire_sm: start_write: unknown envelope kind");
    }
    if (!has_args && (args || args_len)) {
        return YETTY_ERR(yetty_ycore_void,
                         "wire_sm: start_write: has_args=0 but args/args_len given");
    }
    struct yetty_ycore_void_result engage_res =
        start_write_engage(sm, kind, code, has_args, compressed, args, args_len, out_buf);
    if (YETTY_IS_ERR(engage_res)) {
        enc_abort(sm);
    }
    return engage_res;
}

/* Body of write — the public wrapper aborts the envelope on any failure. */
static struct yetty_ycore_void_result write_engage(struct yetty_ywire_wire_statemachine *sm,
                                                   const void *src, size_t len)
{
    if (!sm->enc_compressed) {
        return b64_encode_push(sm, (const uint8_t *)src, len);
    }
    size_t bound = LZ4F_compressBound(len, NULL);
    {
        struct yetty_ycore_void_result r = ensure_enc_scratch(sm, bound);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "wire_sm: write: scratch");
    }
    size_t out_n =
        LZ4F_compressUpdate(sm->enc_ctx, sm->enc_scratch, sm->enc_scratch_cap, src, len, NULL);
    if (LZ4F_isError(out_n)) {
        return YETTY_ERR(yetty_ycore_void, LZ4F_getErrorName(out_n));
    }
    if (out_n == 0) {
        return YETTY_OK_VOID();
    }
    return b64_encode_push(sm, sm->enc_scratch, out_n);
}

struct yetty_ycore_void_result yetty_ywire_wire_statemachine_write(
    struct yetty_ywire_wire_statemachine *sm, const void *src, size_t len)
{
    if (!sm) {
        return YETTY_ERR(yetty_ycore_void, "wire_sm: sm is NULL");
    }
    if (!sm->enc_active) {
        return YETTY_ERR(yetty_ycore_void, "wire_sm: write outside frame");
    }
    if (len == 0) {
        return YETTY_OK_VOID();
    }
    if (!src) {
        return YETTY_ERR(yetty_ycore_void, "wire_sm: write: src is NULL");
    }
    struct yetty_ycore_void_result write_res = write_engage(sm, src, len);
    if (YETTY_IS_ERR(write_res)) {
        enc_abort(sm);
    }
    return write_res;
}

/* Append the envelope terminator. Plain: ESC \. Under tmux wrapping: the
 * envelope's own terminator ESC \ with its ESC doubled (ESC ESC \) followed
 * by the tmux wrapper's own terminator ESC \. */
static struct yetty_ycore_void_result write_terminator(struct yetty_ywire_wire_statemachine *sm)
{
    if (sm->enc_tmux_wrap) {
        return yetty_ycore_buffer_write(sm->enc_out_buf, "\033\033\\\033\\", 5);
    }
    return yetty_ycore_buffer_write(sm->enc_out_buf, "\033\\", 2);
}

/* Body of finish_write — the public wrapper aborts the envelope on any
 * failure and closes it out on success. */
static struct yetty_ycore_void_result finish_write_engage(struct yetty_ywire_wire_statemachine *sm)
{
    if (!sm->enc_compressed) {
        struct yetty_ycore_void_result r = b64_encode_flush(sm);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "wire_sm: finish_write: b64 flush");
        return write_terminator(sm);
    }
    /* Compressed: flush LZ4F footer through b64, then ST. */
    size_t bound = LZ4F_compressBound(0, NULL);
    {
        struct yetty_ycore_void_result r = ensure_enc_scratch(sm, bound);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "wire_sm: finish_write: scratch");
    }
    size_t end_n = LZ4F_compressEnd(sm->enc_ctx, sm->enc_scratch, sm->enc_scratch_cap, NULL);
    if (LZ4F_isError(end_n)) {
        return YETTY_ERR(yetty_ycore_void, LZ4F_getErrorName(end_n));
    }
    if (end_n > 0) {
        struct yetty_ycore_void_result r = b64_encode_push(sm, sm->enc_scratch, end_n);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "wire_sm: finish_write: lz4 footer");
    }
    struct yetty_ycore_void_result r = b64_encode_flush(sm);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "wire_sm: finish_write: b64 flush");
    return write_terminator(sm);
}

struct yetty_ycore_void_result yetty_ywire_wire_statemachine_finish_write(
    struct yetty_ywire_wire_statemachine *sm)
{
    if (!sm) {
        return YETTY_ERR(yetty_ycore_void, "wire_sm: sm is NULL");
    }
    if (!sm->enc_active) {
        return YETTY_ERR(yetty_ycore_void, "wire_sm: finish_write: no active write");
    }
    struct yetty_ycore_void_result finish_res = finish_write_engage(sm);
    if (YETTY_IS_ERR(finish_res)) {
        enc_abort(sm);
        return finish_res;
    }
    /* LZ4F_compressEnd returned the context to its post-create state, so it
     * is directly reusable — keep it (and the scratch) allocated so the next
     * envelope on this SM skips the per-frame context churn. Destroy frees
     * both. */
    sm->enc_active = 0;
    sm->enc_out_buf = NULL;
    return finish_res;
}

/*===========================================================================
 * One-shot helpers — internally use a transient SM
 *=========================================================================*/

struct yetty_ycore_void_result yetty_ywire_emit(enum yetty_ywire_envelope_kind kind, int code,
                                                int has_args, int compressed, const void *args,
                                                size_t args_len, const void *body, size_t body_len,
                                                struct yetty_ycore_buffer *out_buf)
{
    if (!out_buf) {
        return YETTY_ERR(yetty_ycore_void, "ywire_emit: out_buf is NULL");
    }
    struct yetty_ywire_wire_statemachine_ptr_result sr = yetty_ywire_wire_statemachine_create(NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "ywire_emit: SM create");
    struct yetty_ywire_wire_statemachine *sm = sr.value;

    struct yetty_ycore_void_result r = yetty_ywire_wire_statemachine_start_write(
        sm, kind, code, has_args, compressed, args, args_len, out_buf);
    if (YETTY_IS_OK(r) && body && body_len > 0) {
        r = yetty_ywire_wire_statemachine_write(sm, body, body_len);
    }
    if (YETTY_IS_OK(r)) {
        r = yetty_ywire_wire_statemachine_finish_write(sm);
    }
    struct yetty_ycore_void_result dr = yetty_ywire_wire_statemachine_destroy(sm);
    if (YETTY_IS_OK(r) && YETTY_IS_ERR(dr)) {
        return dr;
    }
    if (YETTY_IS_ERR(dr)) {
        yetty_ycore_error_destroy(dr.error);
    }
    return r;
}

struct yetty_ycore_void_result yetty_ywire_emit_to_fd(int fd, enum yetty_ywire_envelope_kind kind,
                                                      int code, int has_args, int compressed,
                                                      const void *args, size_t args_len,
                                                      const void *body, size_t body_len)
{
    struct yetty_ycore_buffer buf = {0};
    struct yetty_ycore_void_result r =
        yetty_ywire_emit(kind, code, has_args, compressed, args, args_len, body, body_len, &buf);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_buffer_destroy(&buf);
        return r;
    }
    size_t off = 0;
    while (off < buf.size) {
        ssize_t w = write(fd, buf.data + off, buf.size - off);
        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            yetty_ycore_buffer_destroy(&buf);
            return YETTY_ERR(yetty_ycore_void, "ywire_emit_to_fd: write failed");
        }
        off += (size_t)w;
    }
    yetty_ycore_buffer_destroy(&buf);
    return YETTY_OK_VOID();
}

/* One-shot body decode. Pipes b64 (optionally LZ4F-compressed) through
 * the SM's existing body-pump by routing the bytes through a synthetic
 * envelope. */
struct yetty_ycore_void_result yetty_ywire_decode(const char *b64, size_t n, int compressed,
                                                  struct yetty_ycore_buffer *out_buf)
{
    if (!out_buf) {
        return YETTY_ERR(yetty_ycore_void, "ywire_decode: out_buf is NULL");
    }
    if (n == 0) {
        return YETTY_OK_VOID();
    }
    if (!b64) {
        return YETTY_ERR(yetty_ycore_void, "ywire_decode: b64 is NULL");
    }

    /* Streaming b64 decode + optional LZ4F. Local state — no SM needed,
     * no envelope framing. */
    LZ4F_decompressionContext_t lz4_ctx = NULL;
    if (compressed) {
        LZ4F_errorCode_t err = LZ4F_createDecompressionContext(&lz4_ctx, LZ4F_VERSION);
        if (LZ4F_isError(err)) {
            return YETTY_ERR(yetty_ycore_void, LZ4F_getErrorName(err));
        }
    }

    /* Strip trailing `=` padding before quartet decode. */
    size_t valid_n = n;
    while (valid_n > 0 && b64[valid_n - 1] == '=') {
        valid_n--;
    }

    uint8_t scratch[8192];
    size_t in_pos = 0;
    struct yetty_ycore_void_result r = YETTY_OK_VOID();
    while (in_pos + 4 <= valid_n) {
        uint8_t triples[3 * 256];
        size_t triples_n = 0;
        while (in_pos + 4 <= valid_n && triples_n + 3 <= sizeof(triples)) {
            uint8_t t[3];
            uint8_t v[4];
            int ok = 1;
            for (int i = 0; i < 4; i++) {
                if (!b64_decode_char(b64[in_pos + i], &v[i])) {
                    ok = 0;
                    break;
                }
            }
            if (!ok) {
                /* skip a single garbage byte */
                in_pos++;
                continue;
            }
            t[0] = (uint8_t)((v[0] << 2) | (v[1] >> 4));
            t[1] = (uint8_t)((v[1] << 4) | (v[2] >> 2));
            t[2] = (uint8_t)((v[2] << 6) | v[3]);
            memcpy(triples + triples_n, t, 3);
            triples_n += 3;
            in_pos += 4;
        }
        if (compressed) {
            size_t pos = 0;
            while (pos < triples_n) {
                size_t in_left = triples_n - pos;
                size_t out_left = sizeof(scratch);
                size_t lr =
                    LZ4F_decompress(lz4_ctx, scratch, &out_left, triples + pos, &in_left, NULL);
                if (LZ4F_isError(lr)) {
                    r = YETTY_ERR(yetty_ycore_void, LZ4F_getErrorName(lr));
                    goto out;
                }
                if (out_left > 0) {
                    r = yetty_ycore_buffer_write(out_buf, scratch, out_left);
                    if (YETTY_IS_ERR(r)) {
                        goto out;
                    }
                }
                pos += in_left;
                if (in_left == 0 && out_left == 0) {
                    break;
                }
            }
        } else {
            r = yetty_ycore_buffer_write(out_buf, triples, triples_n);
            if (YETTY_IS_ERR(r)) {
                goto out;
            }
        }
    }
    /* Tail of 2 or 3 valid chars decodes to 1 or 2 bytes. */
    if (valid_n - in_pos >= 2) {
        char tail[4] = {b64[in_pos], b64[in_pos + 1], valid_n - in_pos >= 3 ? b64[in_pos + 2] : 'A',
                        'A'};
        uint8_t v[4];
        int ok = 1;
        for (int i = 0; i < 4; i++) {
            if (!b64_decode_char(tail[i], &v[i])) {
                ok = 0;
                break;
            }
        }
        if (ok) {
            uint8_t t[3] = {(uint8_t)((v[0] << 2) | (v[1] >> 4)),
                            (uint8_t)((v[1] << 4) | (v[2] >> 2)), (uint8_t)((v[2] << 6) | v[3])};
            size_t out_n = (valid_n - in_pos >= 3) ? 2 : 1;
            if (compressed) {
                uint8_t lz4_scratch[8192];
                size_t pos = 0;
                while (pos < out_n) {
                    size_t in_left = out_n - pos;
                    size_t out_left = sizeof(lz4_scratch);
                    size_t lr =
                        LZ4F_decompress(lz4_ctx, lz4_scratch, &out_left, t + pos, &in_left, NULL);
                    if (LZ4F_isError(lr)) {
                        r = YETTY_ERR(yetty_ycore_void, LZ4F_getErrorName(lr));
                        goto out;
                    }
                    if (out_left > 0) {
                        r = yetty_ycore_buffer_write(out_buf, lz4_scratch, out_left);
                        if (YETTY_IS_ERR(r)) {
                            goto out;
                        }
                    }
                    pos += in_left;
                    if (in_left == 0 && out_left == 0) {
                        break;
                    }
                }
            } else {
                r = yetty_ycore_buffer_write(out_buf, t, out_n);
            }
        }
    }

out:
    if (lz4_ctx) {
        LZ4F_freeDecompressionContext(lz4_ctx);
    }
    return r;
}

/*===========================================================================
 * Envelope-default handler — fires for any (kind, code) without a
 * specific register() entry.
 *=========================================================================*/

struct yetty_ycore_void_result yetty_ywire_wire_statemachine_set_envelope_default(
    struct yetty_ywire_wire_statemachine *sm, int has_args, yetty_ywire_process_input_fn fn,
    void *userdata)
{
    if (!sm) {
        return YETTY_ERR(yetty_ycore_void, "wire_sm: sm is NULL");
    }
    if (!fn || !userdata) {
        return YETTY_ERR(yetty_ycore_void,
                         "wire_sm: set_envelope_default: fn and userdata must be non-NULL");
    }
    sm->envelope_default_fn = fn;
    sm->envelope_default_userdata = userdata;
    sm->envelope_default_has_args = has_args ? 1 : 0;
    struct yetty_ywire_handler_coro_ptr_result coro_res =
        get_or_spawn_handler_coro(sm, fn, userdata);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, coro_res, "wire_sm: envelope_default coro spawn failed");
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Buffered (push-callback) helpers
 *
 * Each one allocates an owned buffered_handler struct and registers a
 * synthetic pull-handler that drains the body / forwards raw bytes
 * and fires the user's typed callback.
 *=========================================================================*/

/* Body-buffered dispatcher: one envelope per outer iteration. Reads
 * decoded bytes into bh->body until EOE, fires env_cb, yields. */
static struct yetty_ycore_void_result envelope_buffered_dispatch(
    void *userdata, struct yetty_ywire_wire_statemachine *sm)
{
    struct buffered_handler *bh = userdata;
    uint8_t chunk[4096];
    for (;;) {
        yetty_ycore_buffer_clear(&bh->body);
        for (;;) {
            struct yetty_ycore_size_result rr =
                yetty_ywire_wire_statemachine_read(sm, chunk, sizeof(chunk));
            YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "buffered: SM read");
            if (rr.value == 0) {
                break;
            }
            struct yetty_ycore_void_result wr =
                yetty_ycore_buffer_write(&bh->body, chunk, rr.value);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, wr, "buffered: accumulate");
        }
        struct yetty_ywire_wire_statemachine_args a = yetty_ywire_wire_statemachine_args(sm);
        enum yetty_ywire_envelope_kind k = yetty_ywire_wire_statemachine_kind(sm);
        int c = yetty_ywire_wire_statemachine_code(sm);
        struct yetty_ycore_void_result cr =
            bh->env_cb(bh->userdata, k, c, a.bytes, a.len, bh->body.data, bh->body.size);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, cr, "buffered: env_cb");
        yetty_yplatform_coro_yield();
    }
}

/* Raw-buffered dispatcher: forwards runs of raw bytes via raw_cb. */
static struct yetty_ycore_void_result raw_buffered_dispatch(
    void *userdata, struct yetty_ywire_wire_statemachine *sm)
{
    struct buffered_handler *bh = userdata;
    uint8_t chunk[4096];
    for (;;) {
        struct yetty_ycore_size_result rr =
            yetty_ywire_wire_statemachine_read(sm, chunk, sizeof(chunk));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "raw_buffered: SM read");
        if (rr.value == 0) {
            yetty_yplatform_coro_yield();
            continue;
        }
        struct yetty_ycore_void_result cr = bh->raw_cb(bh->userdata, chunk, rr.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, cr, "raw_buffered: raw_cb");
    }
}

/* Allocate + park a buffered_handler in the SM's owned list. Caller
 * must check NULL return for OOM. */
static struct buffered_handler *new_buffered(struct yetty_ywire_wire_statemachine *sm)
{
    if (sm->buffered_count == sm->buffered_cap) {
        size_t nc = sm->buffered_cap ? sm->buffered_cap * 2 : 4;
        struct buffered_handler **grown =
            realloc(sm->buffered, nc * sizeof(struct buffered_handler *));
        if (!grown) {
            return NULL;
        }
        sm->buffered = grown;
        sm->buffered_cap = nc;
    }
    struct buffered_handler *bh = calloc(1, sizeof(struct buffered_handler));
    if (!bh) {
        return NULL;
    }
    sm->buffered[sm->buffered_count++] = bh;
    return bh;
}

struct yetty_ycore_void_result yetty_ywire_wire_statemachine_register_buffered(
    struct yetty_ywire_wire_statemachine *sm, enum yetty_ywire_envelope_kind kind, int code,
    int has_args, yetty_ywire_envelope_cb cb, void *userdata)
{
    if (!sm) {
        return YETTY_ERR(yetty_ycore_void, "wire_sm: sm is NULL");
    }
    if (!cb) {
        return YETTY_ERR(yetty_ycore_void, "wire_sm: register_buffered: cb is NULL");
    }
    struct buffered_handler *bh = new_buffered(sm);
    if (!bh) {
        return YETTY_ERR(yetty_ycore_void, "wire_sm: register_buffered: oom");
    }
    bh->env_cb = cb;
    bh->userdata = userdata;
    return yetty_ywire_wire_statemachine_register(sm, kind, code, has_args,
                                                  envelope_buffered_dispatch, bh);
}

struct yetty_ycore_void_result yetty_ywire_wire_statemachine_set_envelope_default_buffered(
    struct yetty_ywire_wire_statemachine *sm, int has_args, yetty_ywire_envelope_cb cb,
    void *userdata)
{
    if (!sm) {
        return YETTY_ERR(yetty_ycore_void, "wire_sm: sm is NULL");
    }
    if (!cb) {
        return YETTY_ERR(yetty_ycore_void, "wire_sm: envelope_default_buffered: cb is NULL");
    }
    struct buffered_handler *bh = new_buffered(sm);
    if (!bh) {
        return YETTY_ERR(yetty_ycore_void, "wire_sm: envelope_default_buffered: oom");
    }
    bh->env_cb = cb;
    bh->userdata = userdata;
    return yetty_ywire_wire_statemachine_set_envelope_default(sm, has_args,
                                                              envelope_buffered_dispatch, bh);
}

struct yetty_ycore_void_result yetty_ywire_wire_statemachine_set_default_buffered(
    struct yetty_ywire_wire_statemachine *sm, yetty_ywire_raw_cb cb, void *userdata)
{
    if (!sm) {
        return YETTY_ERR(yetty_ycore_void, "wire_sm: sm is NULL");
    }
    if (!cb) {
        return YETTY_ERR(yetty_ycore_void, "wire_sm: set_default_buffered: cb is NULL");
    }
    struct buffered_handler *bh = new_buffered(sm);
    if (!bh) {
        return YETTY_ERR(yetty_ycore_void, "wire_sm: set_default_buffered: oom");
    }
    bh->raw_cb = cb;
    bh->userdata = userdata;
    return yetty_ywire_wire_statemachine_set_default(sm, raw_buffered_dispatch, bh);
}
