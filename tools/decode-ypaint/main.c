/*
 * decode-ypaint — diagnostic tool that takes the OSC stream emitted by ycat
 * (or any other ypaint-bin emitter) and decodes it via yface, printing what's
 * inside.
 *
 * Usage:
 *   decode-ypaint <file>     # parse and decode every \e]…\e\\ envelope in file
 *
 * Goal: when ycat output looks broken (server side does nothing), run
 * decode-ypaint on the captured bytes to confirm the wire format is valid
 * end-to-end (envelope, args meta, b64+LZ4F payload, ypaint magic).
 *
 * For each envelope it prints:
 *   - osc code
 *   - decoded args meta (magic / version / compressed / raw_size)
 *   - decoded payload size + first bytes (so the ypaint magic is visible)
 *
 * The tool uses yface for the decode side — same code path the receiver
 * uses, so a clean run here means the wire is fine and bugs (if any) live
 * in the consumer.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <yetty/ycore/util.h>
#include <yetty/ycore/types.h>
#include <yetty/yface/yface.h>

static int decode_envelope(struct yetty_yface *y,
                           const char *body, size_t body_len)
{
    /* body has the shape "<code>;<b64-args>;<b64-payload>". */
    const char *semi1 = memchr(body, ';', body_len);
    if (!semi1) { fprintf(stderr, "no first ;\n"); return -1; }
    size_t code_len = semi1 - body;
    if (code_len == 0 || code_len > 16) {
        fprintf(stderr, "bad code length %zu\n", code_len); return -1;
    }
    char code_str[20] = {0};
    memcpy(code_str, body, code_len);
    int osc_code = atoi(code_str);

    const char *after_code = semi1 + 1;
    size_t after_code_len = body_len - code_len - 1;
    const char *semi2 = memchr(after_code, ';', after_code_len);
    if (!semi2) { fprintf(stderr, "no second ;\n"); return -1; }
    size_t args_len = semi2 - after_code;
    const char *payload = semi2 + 1;
    size_t payload_len = after_code_len - args_len - 1;

    fprintf(stderr,
            "  osc code: %d  (args b64=%zu  payload b64=%zu)\n",
            osc_code, args_len, payload_len);

    /* Decode args. */
    int compressed = 0;
    if (args_len > 0) {
        char meta_raw[64] = {0};
        size_t mlen = yetty_ycore_base64_decode(after_code, args_len,
                                                meta_raw, sizeof(meta_raw));
        fprintf(stderr, "  args decoded: %zu bytes\n", mlen);
        if (mlen >= sizeof(struct yetty_yface_bin_meta)) {
            const struct yetty_yface_bin_meta *m =
                (const struct yetty_yface_bin_meta *)meta_raw;
            const char *magic_ok =
                (m->magic == YETTY_YFACE_BIN_MAGIC) ? "OK" : "MISMATCH";
            fprintf(stderr,
                    "  meta: magic=0x%08x [%s]  version=%u  "
                    "compressed=%u  algo=%u  raw_size=%llu\n",
                    m->magic, magic_ok, m->version,
                    m->compressed, m->compression_algo,
                    (unsigned long long)m->raw_size);
            compressed = (m->compressed != 0);
        }
    } else {
        fprintf(stderr, "  args empty (no meta)\n");
    }

    /* Decode payload through yface. */
    if (payload_len == 0) {
        fprintf(stderr, "  payload empty (clear/no-body envelope)\n");
        return 0;
    }
    struct yetty_ycore_void_result r = yetty_yface_start_read(y, compressed);
    if (!r.ok) {
        fprintf(stderr, "  start_read failed: %s\n", r.error.msg);
        return -1;
    }
    r = yetty_yface_feed(y, payload, payload_len);
    if (!r.ok) {
        fprintf(stderr, "  feed failed: %s\n", r.error.msg);
        yetty_yface_finish_read(y);
        return -1;
    }
    r = yetty_yface_finish_read(y);
    if (!r.ok) {
        fprintf(stderr, "  finish_read failed: %s\n", r.error.msg);
        return -1;
    }

    struct yetty_ycore_buffer *in = yetty_yface_in_buf(y);
    fprintf(stderr, "  payload decoded: %zu bytes; first 16:", in->size);
    for (size_t i = 0; i < in->size && i < 16; i++)
        fprintf(stderr, " %02x", (unsigned char)in->data[i]);
    fprintf(stderr, "\n");

    /* Inspect TEXT_SPAN prims — look for any text content that's ONLY
	 * descender letters (q, p, y) on its own. Walk the framed payload
	 * past the 4B magic + 16B scene bounds + 4B byte_count. */
    if (in->size < 24 || memcmp(in->data, "YPB1", 4) != 0) {
        return 0;
    }
    uint32_t byte_count;
    memcpy(&byte_count, in->data + 20, 4);
    if (byte_count + 24 > in->size) {
        return 0;
    }
    const uint8_t *p = in->data + 24;
    const uint8_t *end = p + byte_count;

    /* SDF prim word counts — from src/yetty/ysdf/funcs.gen.c. Sized to
	 * cover the prims ybrowser emits (CIRCLE/BOX/SEGMENT, plus extended
	 * shapes if a future producer adds them). */
    static const int8_t sdf_words[64] = {
        /* 0x10000000.. */ 8, 10, 9, 11, -1, -1, 9, 11,
        /*               */ 13, 9, 8, 8, 10, 10, 11, 8,
        /*               */ 10, 9, 10, 9, 9, -1, -1, -1,
        /*               */ -1, -1, -1, -1, 8, 9, 9, -1,
        /*               */ -1, -1, -1, -1, -1, -1, -1, -1,
        /*               */ -1, -1, -1, -1, -1, -1, -1, -1,
        /*               */ -1, -1, -1, -1, -1, -1, -1, -1,
        /*               */ -1, -1, -1, -1, -1, -1, -1, -1,
    };

    int text_spans = 0;
    int qpy_only = 0;
    int short_qpy = 0;

    while (p + 8 <= end) {
        uint32_t t;
        memcpy(&t, p, 4);
        size_t psize;
        if ((t & 0xFFFFFF00) == 0x10000000) {
            uint8_t idx = (uint8_t)(t & 0xFF);
            if (idx >= 64 || sdf_words[idx] <= 0) {
                fprintf(stderr, "  unknown SDF prim type 0x%08x at offset %zu\n", t,
                        (size_t)(p - in->data));
                break;
            }
            psize = (size_t)sdf_words[idx] * 4;
        } else if (t == 0x40000002 || t == 0x40000001 || t == 0 ||
                   (t & 0xF0000000) == 0x80000000) {
            uint32_t payload_size;
            memcpy(&payload_size, p + 4, 4);
            psize = 8u + payload_size;
        } else {
            fprintf(stderr, "  unknown prim type 0x%08x at offset %zu\n", t,
                    (size_t)(p - in->data));
            break;
        }
        if (psize == 0 || p + psize > end) {
            fprintf(stderr, "  truncated prim at offset %zu (size %zu, remaining %zu)\n",
                    (size_t)(p - in->data), psize, (size_t)(end - p));
            break;
        }
        if (t == 0x40000002 && psize >= 48) {
            text_spans++;
            float x, y;
            uint32_t tl;
            memcpy(&x, p + 8, 4);
            memcpy(&y, p + 12, 4);
            memcpy(&tl, p + 36, 4);
            if (48u + tl <= psize) {
                const char *text = (const char *)p + 48;
                /* Check 1: text is EXCLUSIVELY p/g/q/y letters (any case). */
                int all_qpy = (tl > 0);
                for (uint32_t k = 0; k < tl; k++) {
                    char c = text[k];
                    if (c != 'p' && c != 'g' && c != 'q' && c != 'y' &&
                        c != 'P' && c != 'G' && c != 'Q' && c != 'Y') {
                        all_qpy = 0;
                        break;
                    }
                }
                if (all_qpy) {
                    qpy_only++;
                    if (qpy_only <= 25) {
                        fprintf(stderr, "  QPY-ONLY span @(%.1f,%.1f) tl=%u text='%.*s'\n",
                                x, y, tl, (int)tl, text);
                    }
                }
                /* Check 2: short fragment (<=2 chars) containing any p/g/q/y. */
                if (tl <= 2) {
                    int has_qpy = 0;
                    for (uint32_t k = 0; k < tl; k++) {
                        char c = text[k];
                        if (c == 'p' || c == 'g' || c == 'q' || c == 'y' ||
                            c == 'P' || c == 'G' || c == 'Q' || c == 'Y') {
                            has_qpy = 1;
                            break;
                        }
                    }
                    if (has_qpy) {
                        short_qpy++;
                        if (short_qpy <= 25) {
                            fprintf(stderr,
                                    "  SHORT-QPY span @(%.1f,%.1f) tl=%u text='%.*s'\n",
                                    x, y, tl, (int)tl, text);
                        }
                    }
                }
            }
        }
        p += psize;
    }
    fprintf(stderr, "  prim summary: %d TEXT_SPANs; qpy-only=%d; short(<=2)-with-qpy=%d\n",
            text_spans, qpy_only, short_qpy);
    return 0;
}

static int run(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return 1; }
    fseek(f, 0, SEEK_END);
    size_t n = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(n + 1);
    if (!buf) { fclose(f); return 1; }
    if (fread(buf, 1, n, f) != n) {
        fprintf(stderr, "short read\n");
        free(buf); fclose(f); return 1;
    }
    buf[n] = 0;
    fclose(f);

    /* One yface for all envelopes — same pattern as the receiver. */
    struct yetty_yface_ptr_result yr = yetty_yface_create();
    if (!yr.ok) { fprintf(stderr, "yface_create failed\n"); free(buf); return 1; }
    struct yetty_yface *y = yr.value;

    /* Walk the byte buffer looking for \e]…\e\\ envelopes. */
    size_t pos = 0;
    int count = 0, errors = 0;
    while (pos + 1 < n) {
        if (buf[pos] != '\033' || buf[pos + 1] != ']') { pos++; continue; }
        size_t open = pos + 2;
        /* Find ESC \ */
        size_t close = open;
        while (close + 1 < n) {
            if (buf[close] == '\033' && buf[close + 1] == '\\') break;
            close++;
        }
        if (close + 1 >= n) {
            fprintf(stderr, "  unterminated envelope at byte %zu\n", pos);
            break;
        }
        size_t body_len = close - open;
        fprintf(stderr, "envelope #%d at byte %zu (body %zu B):\n",
                count, pos, body_len);
        if (decode_envelope(y, buf + open, body_len) < 0)
            errors++;
        count++;
        pos = close + 2;
    }
    fprintf(stderr, "\nfound %d envelope(s), %d error(s)\n", count, errors);

    yetty_yface_destroy(y);
    free(buf);
    return errors ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <ycat-output-file>\n", argv[0]);
        return 1;
    }
    return run(argv[1]);
}
