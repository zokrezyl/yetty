/*
 * yetty/yface/yface.c — bidirectional OSC stream:
 *   bytes  --> [LZ4F_compress*] -->  streaming base64 encode  -->  out_buf
 *   chars  --> streaming base64 decode --> [LZ4F_decompress]  -->  in_buf
 *
 * The LZ4 step is gated by a per-envelope `compressed` flag carried as a
 * single character right after the first ';' — '0' = raw b64, '1' = LZ4F.
 *
 * The LZ4 contexts hold the dictionary / partial-block state; the b64
 * encoder/decoder hold the 0..2 / 0..3 byte carry that bridges chunked
 * input. Together that means start/write/finish can be called with
 * arbitrarily small chunks and produce identical output to a one-shot
 * encode of the concatenated input.
 *
 * On top of the chunked codec there's an envelope scanner driven by
 * yetty_yface_feed_bytes() — finds \e]…\e\\ in a raw byte stream, drives
 * the decode for each envelope's body, and fires the user's on_osc
 * callback with the decoded payload. Bytes outside any envelope go to
 * on_raw so consumers can keep their keyboard / CSI handling.
 */

#include <yetty/yface/yface.h>

#include <lz4frame.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*===========================================================================
 * Internal state
 *=========================================================================*/

#define ENC_SCRATCH_CAP_DEFAULT (64 * 1024) /* one LZ4 block worth */

/* Stream scanner state — drives feed_bytes(). */
enum yface_scan_state {
    YFACE_SCAN_RAW = 0,      /* bytes outside an envelope → on_raw */
    YFACE_SCAN_AFTER_ESC,    /* saw ESC; deciding if this opens an OSC */
    YFACE_SCAN_OSC_CODE,     /* reading decimal vendor code */
    YFACE_SCAN_OSC_ARGS,     /* collecting b64-args until 2nd ';' */
    YFACE_SCAN_OSC_BODY,     /* feeding payload b64 through codec */
    YFACE_SCAN_OSC_BODY_ESC, /* saw ESC inside body — could be ST */
};

#define YFACE_ARGS_MAX_B64 1024 /* 768 bytes raw — enough for any meta */
#define YFACE_ARGS_MAX_RAW (YFACE_ARGS_MAX_B64 * 3 / 4)

struct yetty_yface {
    struct yetty_ycore_buffer in_buf;
    struct yetty_ycore_buffer out_buf;

    /* Outgoing — LZ4F + streaming b64 encode */
    LZ4F_compressionContext_t enc_ctx;
    uint8_t *enc_scratch;
    size_t enc_scratch_cap;
    int enc_active;
    int enc_compressed;
    /* b64 carry: 0..2 input bytes that didn't form a complete triple yet. */
    uint8_t enc_b64_carry[2];
    uint8_t enc_b64_carry_n;

    /* Incoming — streaming b64 decode + (optional) LZ4F decompress */
    LZ4F_decompressionContext_t dec_ctx;
    int dec_active;
    int dec_compressed;
    /* b64 carry: 0..3 chars that didn't form a complete quartet yet. */
    char dec_b64_carry[4];
    uint8_t dec_b64_carry_n;

    /* Stream scanner state. */
    enum yface_scan_state scan_state;
    int scan_osc_code;
    /* Raw b64 chars of the args slot — drained on second ';' into
     * scan_args_decoded. */
    char scan_args_b64[YFACE_ARGS_MAX_B64];
    size_t scan_args_b64_len;
    /* Decoded args bytes — handed to on_osc. */
    uint8_t scan_args_decoded[YFACE_ARGS_MAX_RAW];
    size_t scan_args_decoded_len;
    yetty_yface_msg_cb on_osc;
    yetty_yface_raw_cb on_raw;
    void *handler_user;
};

/*===========================================================================
 * Streaming base64 — encoder
 *
 * Standard "+/=" alphabet. Holds 0..2 bytes of input between calls. Each
 * call drains as many complete triples as possible into the destination
 * buffer; the leftover (size%3) goes into carry. encode_flush emits the
 * final partial as 2 or 3 chars + '=' padding.
 *=========================================================================*/

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

/* Push len bytes through the encoder. Carry is updated. */
static struct yetty_ycore_void_result b64_encode_push(struct yetty_yface *y, const uint8_t *src,
                                                      size_t len)
{
    /* Combine carry + new input into triples. */
    size_t i = 0;
    while (y->enc_b64_carry_n > 0 && i < len) {
        if (y->enc_b64_carry_n == 1 && i + 1 < len) {
            struct yetty_ycore_void_result r =
                b64_emit_triple(&y->out_buf, y->enc_b64_carry[0], src[i], src[i + 1]);
            if (!r.ok) {
                return r;
            }
            i += 2;
            y->enc_b64_carry_n = 0;
        } else if (y->enc_b64_carry_n == 2 && i < len) {
            struct yetty_ycore_void_result r =
                b64_emit_triple(&y->out_buf, y->enc_b64_carry[0], y->enc_b64_carry[1], src[i]);
            if (!r.ok) {
                return r;
            }
            i += 1;
            y->enc_b64_carry_n = 0;
        } else {
            /* Need more bytes to complete a triple. */
            break;
        }
    }
    /* Whole triples from src[i..]. */
    while (i + 3 <= len) {
        struct yetty_ycore_void_result r =
            b64_emit_triple(&y->out_buf, src[i], src[i + 1], src[i + 2]);
        if (!r.ok) {
            return r;
        }
        i += 3;
    }
    /* Leftover into carry. */
    while (i < len && y->enc_b64_carry_n < 2) {
        y->enc_b64_carry[y->enc_b64_carry_n++] = src[i++];
    }
    /* If we still have input here something went wrong with the carry math. */
    if (i != len) {
        return YETTY_ERR(yetty_ycore_void, "b64 carry overflow");
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result b64_encode_flush(struct yetty_yface *y)
{
    if (y->enc_b64_carry_n == 0) {
        return YETTY_OK_VOID();
    }

    char chars[4];
    if (y->enc_b64_carry_n == 1) {
        uint32_t v = (uint32_t)y->enc_b64_carry[0] << 16;
        chars[0] = b64_alpha[(v >> 18) & 0x3F];
        chars[1] = b64_alpha[(v >> 12) & 0x3F];
        chars[2] = '=';
        chars[3] = '=';
    } else { /* 2 */
        uint32_t v = ((uint32_t)y->enc_b64_carry[0] << 16) | ((uint32_t)y->enc_b64_carry[1] << 8);
        chars[0] = b64_alpha[(v >> 18) & 0x3F];
        chars[1] = b64_alpha[(v >> 12) & 0x3F];
        chars[2] = b64_alpha[(v >> 6) & 0x3F];
        chars[3] = '=';
    }
    y->enc_b64_carry_n = 0;
    return yetty_ycore_buffer_write(&y->out_buf, chars, 4);
}

/*===========================================================================
 * Streaming base64 — decoder
 *
 * Holds 0..3 input chars between calls; emits raw bytes as soon as a
 * complete quartet is formed. Stops at first '=' (RFC-4648 padding).
 *=========================================================================*/

static const signed char b64_table[256] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1, -1, 0,  1,  2,  3,  4,  5,  6,
    7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1,
    -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48,
    49, 50, 51, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};

/* Decode one quartet of 4 valid (non-pad) b64 chars into 3 raw bytes. */
static int b64_decode_quartet(const char *in, uint8_t out[3])
{
    int v0 = b64_table[(unsigned char)in[0]];
    int v1 = b64_table[(unsigned char)in[1]];
    int v2 = b64_table[(unsigned char)in[2]];
    int v3 = b64_table[(unsigned char)in[3]];
    if (v0 < 0 || v1 < 0 || v2 < 0 || v3 < 0) {
        return 0;
    }
    uint32_t v = ((uint32_t)v0 << 18) | ((uint32_t)v1 << 12) | ((uint32_t)v2 << 6) | (uint32_t)v3;
    out[0] = (uint8_t)((v >> 16) & 0xFF);
    out[1] = (uint8_t)((v >> 8) & 0xFF);
    out[2] = (uint8_t)(v & 0xFF);
    return 1;
}

/*===========================================================================
 * The LZ4-out → b64-out chain that both write paths feed
 *=========================================================================*/

static struct yetty_ycore_void_result emit_compressed_chunk(struct yetty_yface *y, size_t lz4_out_n)
{
    if (lz4_out_n == 0) {
        return YETTY_OK_VOID();
    }
    return b64_encode_push(y, y->enc_scratch, lz4_out_n);
}

/*===========================================================================
 * Outgoing API
 *=========================================================================*/

static struct yetty_ycore_void_result ensure_enc_scratch(struct yetty_yface *y, size_t need)
{
    if (need <= y->enc_scratch_cap) {
        return YETTY_OK_VOID();
    }
    size_t new_cap = y->enc_scratch_cap ? y->enc_scratch_cap : 1024;
    while (new_cap < need) {
        new_cap *= 2;
    }
    uint8_t *p = (uint8_t *)realloc(y->enc_scratch, new_cap);
    if (!p) {
        return YETTY_ERR(yetty_ycore_void, "scratch realloc failed");
    }
    y->enc_scratch = p;
    y->enc_scratch_cap = new_cap;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yface_start_write(struct yetty_yface *y, int osc_code,
                                                       int compressed, const void *args,
                                                       size_t args_len)
{
    if (!y) {
        return YETTY_ERR(yetty_ycore_void, "yface is NULL");
    }
    if (y->enc_active) {
        return YETTY_ERR(yetty_ycore_void, "yface: write already active");
    }

    /* Wire shape: "\e]<code>;<b64-args>;<b64-payload>\e\\". The args
     * slot is always present (empty when args_len==0) so receivers
     * always see the same `;…;` split. The compressed/raw distinction
     * is per-code and (for bin codes) lives inside the args meta — not
     * on the wire as a separate field. */
    char hdr[32];
    int n = snprintf(hdr, sizeof(hdr), "\033]%d;", osc_code);
    if (n <= 0 || (size_t)n >= sizeof(hdr)) {
        return YETTY_ERR(yetty_ycore_void, "yface: bad osc_code");
    }
    {
        struct yetty_ycore_void_result r = yetty_ycore_buffer_write(&y->out_buf, hdr, (size_t)n);
        if (!r.ok) {
            return r;
        }
    }

    /* Args slot — b64-encode any bytes, then close with ';'. Even for
     * args_len==0 we still write the closing ';' so the wire shape
     * stays uniform. */
    y->enc_b64_carry_n = 0;
    if (args && args_len > 0) {
        struct yetty_ycore_void_result r = b64_encode_push(y, (const uint8_t *)args, args_len);
        if (!r.ok) {
            return r;
        }
        r = b64_encode_flush(y);
        if (!r.ok) {
            return r;
        }
        y->enc_b64_carry_n = 0;
    }
    {
        struct yetty_ycore_void_result r = yetty_ycore_buffer_write(&y->out_buf, ";", 1);
        if (!r.ok) {
            return r;
        }
    }

    y->enc_compressed = compressed ? 1 : 0;

    if (!compressed) {
        y->enc_active = 1;
        return YETTY_OK_VOID();
    }

    /* Compressed path: LZ4F frame opening through b64. */
    {
        struct yetty_ycore_void_result r = ensure_enc_scratch(y, ENC_SCRATCH_CAP_DEFAULT);
        if (!r.ok) {
            return r;
        }
    }

    LZ4F_errorCode_t err = LZ4F_createCompressionContext(&y->enc_ctx, LZ4F_VERSION);
    if (LZ4F_isError(err)) {
        return YETTY_ERR(yetty_ycore_void, LZ4F_getErrorName(err));
    }

    LZ4F_preferences_t prefs = {0};
    /* Default block size (64 KB), no checksum — yface's out_buf is taken
     * as a unit and OSC framing already provides terminators. */
    size_t hn = LZ4F_compressBegin(y->enc_ctx, y->enc_scratch, y->enc_scratch_cap, &prefs);
    if (LZ4F_isError(hn)) {
        LZ4F_freeCompressionContext(y->enc_ctx);
        y->enc_ctx = NULL;
        return YETTY_ERR(yetty_ycore_void, LZ4F_getErrorName(hn));
    }
    {
        struct yetty_ycore_void_result r = emit_compressed_chunk(y, hn);
        if (!r.ok) {
            LZ4F_freeCompressionContext(y->enc_ctx);
            y->enc_ctx = NULL;
            return r;
        }
    }

    y->enc_active = 1;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yface_write(struct yetty_yface *y, const void *src, size_t len)
{
    if (!y) {
        return YETTY_ERR(yetty_ycore_void, "yface is NULL");
    }
    if (!y->enc_active) {
        return YETTY_ERR(yetty_ycore_void, "yface: write outside frame");
    }
    if (len == 0) {
        return YETTY_OK_VOID();
    }
    if (!src) {
        return YETTY_ERR(yetty_ycore_void, "src is NULL");
    }

    if (!y->enc_compressed) {
        /* Raw path: source bytes feed b64 directly. */
        return b64_encode_push(y, (const uint8_t *)src, len);
    }

    /* Compressed path: LZ4F frame chunk through b64. */
    size_t bound = LZ4F_compressBound(len, NULL);
    {
        struct yetty_ycore_void_result r = ensure_enc_scratch(y, bound);
        if (!r.ok) {
            return r;
        }
    }
    size_t out_n =
        LZ4F_compressUpdate(y->enc_ctx, y->enc_scratch, y->enc_scratch_cap, src, len, NULL);
    if (LZ4F_isError(out_n)) {
        return YETTY_ERR(yetty_ycore_void, LZ4F_getErrorName(out_n));
    }
    return emit_compressed_chunk(y, out_n);
}

struct yetty_ycore_void_result yetty_yface_finish_write(struct yetty_yface *y)
{
    if (!y) {
        return YETTY_ERR(yetty_ycore_void, "yface is NULL");
    }
    if (!y->enc_active) {
        return YETTY_ERR(yetty_ycore_void, "yface: no active write");
    }

    if (!y->enc_compressed) {
        struct yetty_ycore_void_result r = b64_encode_flush(y);
        if (!r.ok) {
            y->enc_active = 0;
            return r;
        }
        r = yetty_ycore_buffer_write(&y->out_buf, "\033\\", 2);
        y->enc_active = 0;
        return r;
    }

    /* Compressed: flush LZ4F frame footer through b64. */
    size_t bound = LZ4F_compressBound(0, NULL);
    {
        struct yetty_ycore_void_result r = ensure_enc_scratch(y, bound);
        if (!r.ok) {
            return r;
        }
    }
    size_t end_n = LZ4F_compressEnd(y->enc_ctx, y->enc_scratch, y->enc_scratch_cap, NULL);
    if (LZ4F_isError(end_n)) {
        LZ4F_freeCompressionContext(y->enc_ctx);
        y->enc_ctx = NULL;
        y->enc_active = 0;
        return YETTY_ERR(yetty_ycore_void, LZ4F_getErrorName(end_n));
    }
    {
        struct yetty_ycore_void_result r = emit_compressed_chunk(y, end_n);
        if (!r.ok) {
            goto cleanup;
        }
        r = b64_encode_flush(y);
        if (!r.ok) {
            goto cleanup;
        }
        r = yetty_ycore_buffer_write(&y->out_buf, "\033\\", 2);
        if (!r.ok) {
            goto cleanup;
        }
    }
    LZ4F_freeCompressionContext(y->enc_ctx);
    y->enc_ctx = NULL;
    y->enc_active = 0;
    return YETTY_OK_VOID();

cleanup:
    LZ4F_freeCompressionContext(y->enc_ctx);
    y->enc_ctx = NULL;
    y->enc_active = 0;
    return YETTY_ERR(yetty_ycore_void, "yface: finish_write failed");
}

/*===========================================================================
 * Incoming API
 *=========================================================================*/

struct yetty_ycore_void_result yetty_yface_start_read(struct yetty_yface *y, int compressed)
{
    if (!y) {
        return YETTY_ERR(yetty_ycore_void, "yface is NULL");
    }
    if (y->dec_active) {
        return YETTY_ERR(yetty_ycore_void, "yface: read already active");
    }

    yetty_ycore_buffer_clear(&y->in_buf);
    y->dec_b64_carry_n = 0;
    y->dec_compressed = compressed ? 1 : 0;

    if (compressed) {
        LZ4F_errorCode_t err = LZ4F_createDecompressionContext(&y->dec_ctx, LZ4F_VERSION);
        if (LZ4F_isError(err)) {
            return YETTY_ERR(yetty_ycore_void, LZ4F_getErrorName(err));
        }
    }

    y->dec_active = 1;
    return YETTY_OK_VOID();
}

/* Push raw bytes from b64 decode into LZ4F_decompress, append output to
 * in_buf. */
static struct yetty_ycore_void_result dec_feed_compressed(struct yetty_yface *y,
                                                          const uint8_t *bytes, size_t n)
{
    /* LZ4F_decompress consumes input and produces output; we may need to
     * loop until all input is consumed. The output is sized
     * incrementally: we ensure room, decompress, append. Reuse a small
     * stack scratch since LZ4F itself manages the dictionary. */
    uint8_t scratch[16 * 1024];
    size_t in_pos = 0;
    while (in_pos < n) {
        size_t in_left = n - in_pos;
        size_t out_left = sizeof(scratch);
        size_t r = LZ4F_decompress(y->dec_ctx, scratch, &out_left, bytes + in_pos, &in_left, NULL);
        if (LZ4F_isError(r)) {
            return YETTY_ERR(yetty_ycore_void, LZ4F_getErrorName(r));
        }
        if (out_left > 0) {
            struct yetty_ycore_void_result rr =
                yetty_ycore_buffer_write(&y->in_buf, scratch, out_left);
            if (!rr.ok) {
                return rr;
            }
        }
        in_pos += in_left;
        if (in_left == 0 && out_left == 0) {
            break; /* nothing happened */
        }
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yface_feed(struct yetty_yface *y, const char *b64, size_t n)
{
    if (!y) {
        return YETTY_ERR(yetty_ycore_void, "yface is NULL");
    }
    if (!y->dec_active) {
        return YETTY_ERR(yetty_ycore_void, "yface: feed outside read");
    }
    if (n == 0) {
        return YETTY_OK_VOID();
    }
    if (!b64) {
        return YETTY_ERR(yetty_ycore_void, "b64 is NULL");
    }

    /* Skip any chars after the first '=' — that's the end-of-stream marker. */
    size_t valid_n = 0;
    while (valid_n < n && b64[valid_n] != '=') {
        valid_n++;
    }
    /* (We accept '=' chars in carry too; b64_decode_quartet rejects them.) */

    /* Stream out complete quartets. Allocate a small temp byte buffer for
     * whatever we decode this call; feed it to LZ4F in one go. */
    /* Worst case: ((carry_n + valid_n) / 4) * 3 bytes. */
    size_t total_in = (size_t)y->dec_b64_carry_n + valid_n;
    size_t bytes_cap = (total_in / 4) * 3;
    if (bytes_cap == 0) {
        /* Just stash what we got into carry and return. */
        for (size_t i = 0; i < valid_n; i++) {
            if (y->dec_b64_carry_n >= 4) {
                return YETTY_ERR(yetty_ycore_void, "yface: b64 carry overflow");
            }
            y->dec_b64_carry[y->dec_b64_carry_n++] = b64[i];
        }
        return YETTY_OK_VOID();
    }

    uint8_t *bytes = (uint8_t *)malloc(bytes_cap);
    if (!bytes) {
        return YETTY_ERR(yetty_ycore_void, "yface: oom");
    }
    size_t bytes_n = 0;

    /* Drain carry first by combining with new chars to form quartets. */
    size_t pos = 0;
    while (y->dec_b64_carry_n > 0 && y->dec_b64_carry_n < 4 && pos < valid_n) {
        y->dec_b64_carry[y->dec_b64_carry_n++] = b64[pos++];
    }
    if (y->dec_b64_carry_n == 4) {
        uint8_t triple[3];
        if (b64_decode_quartet(y->dec_b64_carry, triple)) {
            memcpy(bytes + bytes_n, triple, 3);
            bytes_n += 3;
        } /* else silently drop — caller fed garbage */
        y->dec_b64_carry_n = 0;
    }

    /* Whole quartets straight from b64[pos..]. */
    while (pos + 4 <= valid_n) {
        uint8_t triple[3];
        if (b64_decode_quartet(b64 + pos, triple)) {
            memcpy(bytes + bytes_n, triple, 3);
            bytes_n += 3;
        }
        pos += 4;
    }

    /* Leftover (<=3 chars) into carry. */
    while (pos < valid_n) {
        if (y->dec_b64_carry_n >= 4) {
            break;
        }
        y->dec_b64_carry[y->dec_b64_carry_n++] = b64[pos++];
    }

    struct yetty_ycore_void_result r;
    if (y->dec_compressed) {
        r = dec_feed_compressed(y, bytes, bytes_n);
    } else {
        r = yetty_ycore_buffer_write(&y->in_buf, bytes, bytes_n);
    }
    free(bytes);
    return r;
}

struct yetty_ycore_void_result yetty_yface_finish_read(struct yetty_yface *y)
{
    if (!y) {
        return YETTY_ERR(yetty_ycore_void, "yface is NULL");
    }
    if (!y->dec_active) {
        return YETTY_OK_VOID();
    }

    /* Drain any 2- or 3-char tail in carry into 1 or 2 raw bytes. The b64
     * encoder pads 1- and 2-byte remainders with '=' chars; feed_bytes
     * strips '=' before quartet decode (so '=' would never appear in
     * carry), which leaves a 2/3-char remainder here as the *valid* tail
     * — not a truncation. Pad with 'A' (= 0) to form a full quartet and
     * keep only the meaningful (carry_n - 1) bytes of the decoded triple. */
    struct yetty_ycore_void_result tail_r = YETTY_OK_VOID();
    if (y->dec_b64_carry_n >= 2) {
        char tail[4] = {
            y->dec_b64_carry[0],
            y->dec_b64_carry[1],
            y->dec_b64_carry_n >= 3 ? y->dec_b64_carry[2] : 'A',
            'A',
        };
        uint8_t triple[3];
        if (b64_decode_quartet(tail, triple)) {
            size_t out_n = (size_t)y->dec_b64_carry_n - 1;
            if (y->dec_compressed) {
                tail_r = dec_feed_compressed(y, triple, out_n);
            } else {
                tail_r = yetty_ycore_buffer_write(&y->in_buf, triple, out_n);
            }
        }
    }
    y->dec_b64_carry_n = 0;
    if (y->dec_compressed && y->dec_ctx) {
        LZ4F_freeDecompressionContext(y->dec_ctx);
        y->dec_ctx = NULL;
    }
    y->dec_active = 0;
    return tail_r;
}

/*===========================================================================
 * Stream scanner — driven by yetty_yface_feed_bytes()
 *
 * Walks an arbitrary byte stream looking for \e]<code>;<flag>;<b64>\e\\
 * envelopes. Bytes outside an envelope are forwarded byte-for-byte through
 * on_raw. For each complete envelope we drive start_read / feed / finish_read
 * (so we share the codec with the explicit-body API), then fire on_osc with
 * the decoded payload pointing into in_buf.
 *
 * In-body bytes are batched into spans and handed to feed() in chunks — no
 * per-byte feed() calls.
 *=========================================================================*/

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

/* Push a contiguous span of body bytes through the active read codec. */
static struct yetty_ycore_void_result scan_feed_body(struct yetty_yface *y, const char *p, size_t n)
{
    if (n == 0) {
        return YETTY_OK_VOID();
    }
    return yetty_yface_feed(y, p, n);
}

/* Forward a contiguous span of out-of-envelope bytes to on_raw. */
static void scan_emit_raw(struct yetty_yface *y, const char *p, size_t n)
{
    if (n == 0 || !y->on_raw) {
        return;
    }
    y->on_raw(y->handler_user, p, n);
}

struct yetty_ycore_void_result yetty_yface_feed_bytes(struct yetty_yface *y, const char *bytes,
                                                      size_t n)
{
    if (!y) {
        return YETTY_ERR(yetty_ycore_void, "yface is NULL");
    }
    if (!bytes || n == 0) {
        return YETTY_OK_VOID();
    }

    /* span_start tracks the head of the current run we'll flush in one
     * shot — either to on_raw (in RAW state) or to feed() (in OSC_BODY).
     * Whenever the state changes we flush and reset. */
    size_t span_start = 0;
    size_t i = 0;

    while (i < n) {
        char c = bytes[i];

        switch (y->scan_state) {
        case YFACE_SCAN_RAW:
            if (c == '\033') {
                /* Flush any raw run up to here, then enter AFTER_ESC. */
                scan_emit_raw(y, bytes + span_start, i - span_start);
                y->scan_state = YFACE_SCAN_AFTER_ESC;
                i++;
            } else {
                i++;
            }
            break;

        case YFACE_SCAN_AFTER_ESC:
            if (c == ']') {
                y->scan_osc_code = 0;
                y->scan_args_b64_len = 0;
                y->scan_args_decoded_len = 0;
                y->scan_state = YFACE_SCAN_OSC_CODE;
                i++;
            } else {
                /* Not an OSC introducer — emit ESC + this byte as raw and
                 * resume scanning. */
                if (y->on_raw) {
                    char esc = '\033';
                    y->on_raw(y->handler_user, &esc, 1);
                }
                span_start = i; /* current byte goes back to RAW */
                y->scan_state = YFACE_SCAN_RAW;
            }
            break;

        case YFACE_SCAN_OSC_CODE:
            if (c >= '0' && c <= '9') {
                y->scan_osc_code = y->scan_osc_code * 10 + (c - '0');
                i++;
            } else if (c == ';') {
                /* End of code; switch to args slot. We don't open the
                 * payload codec yet — that happens at the second ';'
                 * once we've parsed args (which may carry the bin meta
                 * with the compressed flag). */
                y->scan_state = YFACE_SCAN_OSC_ARGS;
                i++;
            } else {
                /* Malformed — drop and resume RAW from next byte. */
                y->scan_state = YFACE_SCAN_RAW;
                span_start = i + 1;
                i++;
            }
            break;

        case YFACE_SCAN_OSC_ARGS:
            if (c == ';') {
                /* End of args slot. b64-decode whatever we accumulated
                 * (might be empty), then open the payload codec. */
                y->scan_args_decoded_len = 0;
                size_t b64n = y->scan_args_b64_len;
                /* Strip trailing '=' padding before quartet decode. */
                while (b64n > 0 && y->scan_args_b64[b64n - 1] == '=') {
                    b64n--;
                }
                size_t pos = 0;
                while (pos + 4 <= b64n) {
                    uint8_t triple[3];
                    if (!b64_decode_quartet(y->scan_args_b64 + pos, triple)) {
                        break;
                    }
                    if (y->scan_args_decoded_len + 3 > sizeof(y->scan_args_decoded)) {
                        break;
                    }
                    memcpy(y->scan_args_decoded + y->scan_args_decoded_len, triple, 3);
                    y->scan_args_decoded_len += 3;
                    pos += 4;
                }
                /* Tail of 2 or 3 valid chars decodes to 1 or 2 bytes. */
                if (b64n - pos >= 2 &&
                    y->scan_args_decoded_len + 2 <= sizeof(y->scan_args_decoded)) {
                    char tail[4] = {y->scan_args_b64[pos], y->scan_args_b64[pos + 1],
                                    b64n - pos >= 3 ? y->scan_args_b64[pos + 2] : 'A', 'A'};
                    uint8_t triple[3];
                    if (b64_decode_quartet(tail, triple)) {
                        y->scan_args_decoded[y->scan_args_decoded_len++] = triple[0];
                        if (b64n - pos >= 3) {
                            y->scan_args_decoded[y->scan_args_decoded_len++] = triple[1];
                        }
                    }
                }

                /* Determine compressed flag. For bin codes the meta
                 * carries it; otherwise default to uncompressed. */
                int compressed = 0;
                if (y->scan_args_decoded_len >= sizeof(struct yetty_yface_bin_meta)) {
                    const struct yetty_yface_bin_meta *m =
                        (const struct yetty_yface_bin_meta *)y->scan_args_decoded;
                    if (m->magic == YETTY_YFACE_BIN_MAGIC) {
                        compressed = (m->compressed != 0);
                    }
                }

                struct yetty_ycore_void_result r = yetty_yface_start_read(y, compressed);
                if (!r.ok) {
                    y->scan_state = YFACE_SCAN_RAW;
                    span_start = i + 1;
                    i++;
                    break;
                }
                y->scan_state = YFACE_SCAN_OSC_BODY;
                i++;
                span_start = i;
            } else {
                /* Accumulate b64 chars (ignore anything outside the b64
                 * alphabet — caller may have padded weirdly). */
                if (y->scan_args_b64_len < sizeof(y->scan_args_b64)) {
                    y->scan_args_b64[y->scan_args_b64_len++] = c;
                }
                i++;
            }
            break;

        case YFACE_SCAN_OSC_BODY:
            if (c == '\033') {
                /* Flush body run so far, then check for ST. */
                struct yetty_ycore_void_result r =
                    scan_feed_body(y, bytes + span_start, i - span_start);
                if (!r.ok) {
                    yetty_yface_finish_read(y);
                    y->scan_state = YFACE_SCAN_RAW;
                    span_start = i + 1;
                    i++;
                    break;
                }
                y->scan_state = YFACE_SCAN_OSC_BODY_ESC;
                i++;
            } else if (c == '\007') {
                /* BEL terminator (legacy OSC form). Flush, finalize, fire. */
                struct yetty_ycore_void_result r =
                    scan_feed_body(y, bytes + span_start, i - span_start);
                if (!r.ok) {
                    yetty_yface_finish_read(y);
                    y->scan_state = YFACE_SCAN_RAW;
                    span_start = i + 1;
                    i++;
                    break;
                }
                yetty_yface_finish_read(y);
                if (y->on_osc) {
                    y->on_osc(y->handler_user, y->scan_osc_code, y->scan_args_decoded,
                              y->scan_args_decoded_len, y->in_buf.data, y->in_buf.size);
                }
                y->scan_state = YFACE_SCAN_RAW;
                span_start = i + 1;
                i++;
            } else {
                i++;
            }
            break;

        case YFACE_SCAN_OSC_BODY_ESC:
            if (c == '\\') {
                /* ST — envelope complete. Finalize codec + fire callback. */
                yetty_yface_finish_read(y);
                if (y->on_osc) {
                    y->on_osc(y->handler_user, y->scan_osc_code, y->scan_args_decoded,
                              y->scan_args_decoded_len, y->in_buf.data, y->in_buf.size);
                }
                y->scan_state = YFACE_SCAN_RAW;
                span_start = i + 1;
                i++;
            } else {
                /* Lone ESC inside body — feed it and the current char as
                 * body bytes, stay in BODY. (We already flushed up to but
                 * not including the ESC; resume span at the ESC.) */
                struct yetty_ycore_void_result r = scan_feed_body(y, "\033", 1);
                if (!r.ok) {
                    yetty_yface_finish_read(y);
                    y->scan_state = YFACE_SCAN_RAW;
                    span_start = i;
                    break;
                }
                y->scan_state = YFACE_SCAN_OSC_BODY;
                span_start = i; /* current char joins next span */
            }
            break;
        }
    }

    /* End-of-buffer flush of any pending span. */
    if (y->scan_state == YFACE_SCAN_RAW) {
        scan_emit_raw(y, bytes + span_start, n - span_start);
    } else if (y->scan_state == YFACE_SCAN_OSC_BODY) {
        return scan_feed_body(y, bytes + span_start, n - span_start);
    }

    return YETTY_OK_VOID();
}

/*===========================================================================
 * Lifecycle + accessors
 *=========================================================================*/

struct yetty_yface_ptr_result yetty_yface_create(void)
{
    struct yetty_yface *y = calloc(1, sizeof(*y));
    if (!y) {
        return YETTY_ERR(yetty_yface_ptr, "yface alloc failed");
    }
    return YETTY_OK(yetty_yface_ptr, y);
}

void yetty_yface_destroy(struct yetty_yface *y)
{
    if (!y) {
        return;
    }
    if (y->enc_ctx) {
        LZ4F_freeCompressionContext(y->enc_ctx);
        y->enc_ctx = NULL;
    }
    if (y->dec_ctx) {
        LZ4F_freeDecompressionContext(y->dec_ctx);
        y->dec_ctx = NULL;
    }
    free(y->enc_scratch);
    yetty_ycore_buffer_destroy(&y->in_buf);
    yetty_ycore_buffer_destroy(&y->out_buf);
    free(y);
}

struct yetty_ycore_buffer *yetty_yface_in_buf(struct yetty_yface *y)
{
    return y ? &y->in_buf : NULL;
}

struct yetty_ycore_buffer *yetty_yface_out_buf(struct yetty_yface *y)
{
    return y ? &y->out_buf : NULL;
}

/*===========================================================================
 * One-shot helpers — see yface.h. Internal pattern: spin a transient
 * yface for the call, do the streaming op once, copy result, free.
 *=========================================================================*/

#ifdef _WIN32
#include <io.h>
typedef long long ssize_t;
#define write(fd, buf, n) _write((fd), (buf), (unsigned int)(n))
#else
#include <unistd.h>
#endif
#include <errno.h>

struct yetty_ycore_void_result yetty_yface_emit(int osc_code, int compressed, const void *args,
                                                size_t args_len, const void *body, size_t body_len,
                                                struct yetty_ycore_buffer *out_buf)
{
    if (!out_buf) {
        return YETTY_ERR(yetty_ycore_void, "out_buf is NULL");
    }

    struct yetty_yface_ptr_result yr = yetty_yface_create();
    if (YETTY_IS_ERR(yr)) {
        return YETTY_ERR(yetty_ycore_void, yr.error.msg);
    }
    struct yetty_yface *y = yr.value;

    struct yetty_ycore_void_result r;
    r = yetty_yface_start_write(y, osc_code, compressed, args, args_len);
    if (YETTY_IS_ERR(r)) {
        goto out;
    }
    if (body && body_len) {
        r = yetty_yface_write(y, body, body_len);
        if (YETTY_IS_ERR(r)) {
            goto out;
        }
    }
    r = yetty_yface_finish_write(y);
    if (YETTY_IS_ERR(r)) {
        goto out;
    }

    /* Append the assembled OSC sequence to the caller's buffer. */
    r = yetty_ycore_buffer_write(out_buf, y->out_buf.data, y->out_buf.size);

out:
    yetty_yface_destroy(y);
    return r;
}

struct yetty_ycore_void_result yetty_yface_emit_to_fd(int fd, int osc_code, int compressed,
                                                      const void *args, size_t args_len,
                                                      const void *body, size_t body_len)
{
    struct yetty_ycore_buffer buf = {0};
    struct yetty_ycore_void_result r =
        yetty_yface_emit(osc_code, compressed, args, args_len, body, body_len, &buf);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_buffer_destroy(&buf);
        return r;
    }
    /* Blocking write_all — caller asked for the convenience function. */
    size_t off = 0;
    while (off < buf.size) {
        ssize_t w = write(fd, buf.data + off, buf.size - off);
        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            yetty_ycore_buffer_destroy(&buf);
            return YETTY_ERR(yetty_ycore_void, "write failed");
        }
        off += (size_t)w;
    }
    yetty_ycore_buffer_destroy(&buf);
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yface_decode(const char *b64, size_t n, int compressed,
                                                  struct yetty_ycore_buffer *out_buf)
{
    if (!out_buf) {
        return YETTY_ERR(yetty_ycore_void, "out_buf is NULL");
    }

    struct yetty_yface_ptr_result yr = yetty_yface_create();
    if (YETTY_IS_ERR(yr)) {
        return YETTY_ERR(yetty_ycore_void, yr.error.msg);
    }
    struct yetty_yface *y = yr.value;

    struct yetty_ycore_void_result r;
    r = yetty_yface_start_read(y, compressed);
    if (YETTY_IS_ERR(r)) {
        goto out;
    }
    r = yetty_yface_feed(y, b64, n);
    if (YETTY_IS_ERR(r)) {
        goto out;
    }
    r = yetty_yface_finish_read(y);
    if (YETTY_IS_ERR(r)) {
        goto out;
    }

    r = yetty_ycore_buffer_write(out_buf, y->in_buf.data, y->in_buf.size);

out:
    yetty_yface_destroy(y);
    return r;
}
