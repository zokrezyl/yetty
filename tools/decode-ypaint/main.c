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
#include <yetty/ysdf/types.gen.h>

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

    int text_spans = 0;
    int qpy_only = 0;
    int short_qpy = 0;
    int prim_index = 0;

    while (p + 8 <= end) {
        uint32_t t;
        memcpy(&t, p, 4);
        size_t psize;
        int is_sdf = ((t & 0xF0000000u) == 0x10000000u);
        if (is_sdf) {
            uint32_t wc = yetty_ysdf_word_count((enum yetty_ysdf_type)t);
            if (wc == 0) {
                fprintf(stderr,
                        "  prim #%d @off=%zu: unknown SDF type 0x%08x — skipping rest\n",
                        prim_index, (size_t)(p - in->data), t);
                break;
            }
            psize = (size_t)wc * 4u;
        } else if (t == 0x40000002 || t == 0x40000001 || t == 0 ||
                   (t & 0xF0000000) == 0x80000000) {
            uint32_t payload_size;
            memcpy(&payload_size, p + 4, 4);
            psize = 8u + payload_size;
        } else {
            fprintf(stderr,
                    "  prim #%d @off=%zu: unknown prim type 0x%08x — skipping rest\n",
                    prim_index, (size_t)(p - in->data), t);
            break;
        }
        if (psize == 0 || p + psize > end) {
            fprintf(stderr,
                    "  prim #%d @off=%zu: truncated (size %zu, remaining %zu)\n",
                    prim_index, (size_t)(p - in->data), psize, (size_t)(end - p));
            break;
        }

        /* One-line dump per SDF prim. Geometry layout (after type/z_order/
         * fill/stroke/stroke_width) starts at word[5]. For the 2D box-ish
         * primitives (BOX, ROUNDED_BOX, LINEAR/RADIAL_GRADIENT_BOX, CIRCLE,
         * ELLIPSE, CAPSULE, …) word[5..6] are (cx, cy). For BOX/ROUNDED_BOX
         * specifically word[7..8] are (hw, hh) — so we can recover the
         * (x, y, w, h) the producer asked for. CIRCLE has radius at [7].
         * Anything else prints just (cx, cy). */
        if (is_sdf) {
            const uint32_t *w = (const uint32_t *)p;
            float cx, cy;
            memcpy(&cx, &w[5], 4);
            memcpy(&cy, &w[6], 4);
            if (t == YETTY_YSDF_BOX || t == YETTY_YSDF_ROUNDED_BOX ||
                t == YETTY_YSDF_LINEAR_GRADIENT_BOX ||
                t == YETTY_YSDF_RADIAL_GRADIENT_BOX) {
                float hw, hh;
                memcpy(&hw, &w[7], 4);
                memcpy(&hh, &w[8], 4);
                const char *name =
                    (t == YETTY_YSDF_BOX) ? "BOX" :
                    (t == YETTY_YSDF_ROUNDED_BOX) ? "ROUNDED_BOX" :
                    (t == YETTY_YSDF_LINEAR_GRADIENT_BOX) ? "LINEAR_GRADIENT_BOX" :
                                                            "RADIAL_GRADIENT_BOX";
                uint32_t fill, stroke;
                memcpy(&fill, &w[2], 4);
                memcpy(&stroke, &w[3], 4);
                fprintf(stderr,
                        "  prim #%d %s rect=(%.1f,%.1f)..(%.1f,%.1f) "
                        "hw=%.1f hh=%.1f fill=0x%08x stroke=0x%08x\n",
                        prim_index, name, cx - hw, cy - hh, cx + hw, cy + hh, hw, hh,
                        fill, stroke);
            } else if (t == YETTY_YSDF_CIRCLE) {
                float r;
                memcpy(&r, &w[7], 4);
                fprintf(stderr, "  prim #%d CIRCLE c=(%.1f,%.1f) r=%.1f\n",
                        prim_index, cx, cy, r);
            } else {
                fprintf(stderr,
                        "  prim #%d SDF type=0x%08x c=(%.1f,%.1f) words=%zu\n",
                        prim_index, t, cx, cy, psize / 4);
            }
        } else if ((t & 0xF0000000u) == 0x80000000u) {
            fprintf(stderr,
                    "  prim #%d COMPLEX type=0x%08x payload=%zu B\n",
                    prim_index, t, psize - 8);
        } else if (t == 0) {
            fprintf(stderr, "  prim #%d CMD_ZERO (clear+home)\n", prim_index);
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
        prim_index++;
    }
    fprintf(stderr,
            "  prim summary: %d prims total; %d TEXT_SPANs; "
            "qpy-only=%d; short(<=2)-with-qpy=%d\n",
            prim_index, text_spans, qpy_only, short_qpy);
    return 0;
}

static int run(FILE *f, const char *label)
{
    /* Read everything until EOF. Stdin can't seek, so use a growable
     * buffer for both stdin and regular files (one path, no fseek). */
    size_t cap = 1u << 14;
    size_t n = 0;
    char *buf = malloc(cap + 1);
    if (!buf) { fprintf(stderr, "alloc failed\n"); return 1; }
    for (;;) {
        if (n == cap) {
            cap *= 2;
            char *grown = realloc(buf, cap + 1);
            if (!grown) { free(buf); fprintf(stderr, "realloc failed\n"); return 1; }
            buf = grown;
        }
        size_t r = fread(buf + n, 1, cap - n, f);
        n += r;
        if (r == 0) {
            if (ferror(f)) {
                fprintf(stderr, "read error on %s\n", label);
                free(buf);
                return 1;
            }
            break;
        }
    }
    buf[n] = 0;
    fprintf(stderr, "read %zu B from %s\n", n, label);

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

static void print_help(const char *prog)
{
    fprintf(stderr,
            "usage: %s [FILE | -]\n"
            "       %s -h | --help\n"
            "\n"
            "Decode the OSC envelopes emitted by a ypaint producer\n"
            "(ycat, ygreeter, ygui apps, ...).\n"
            "\n"
            "With no argument or with `-`, reads from standard input — useful\n"
            "for piping a live producer straight into the decoder:\n"
            "\n"
            "    ./ygreeter | %s\n"
            "    cat capture.bin | %s -\n"
            "    %s capture.bin\n"
            "\n"
            "All diagnostic output goes to stderr, so this tool can be chained\n"
            "with others (e.g. `grep`, `tee`) on stdout without interference.\n"
            "\n"
            "For each OSC envelope decode-ypaint prints:\n"
            "  - the OSC code\n"
            "  - the args meta (magic, version, compression flag, raw size)\n"
            "  - the decoded payload size + first 16 bytes (so the ypaint\n"
            "    magic `YPB1` is directly visible)\n"
            "  - a summary of TEXT_SPAN primitives carried in the payload\n",
            prog, prog, prog, prog, prog);
}

int main(int argc, char **argv)
{
    const char *path = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            print_help(argv[0]);
            return 0;
        }
        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            fprintf(stderr, "unknown flag: %s\n", argv[i]);
            print_help(argv[0]);
            return 1;
        }
        if (path) {
            fprintf(stderr, "too many positional arguments\n");
            print_help(argv[0]);
            return 1;
        }
        path = argv[i];
    }

    FILE *f;
    const char *label;
    if (!path || !strcmp(path, "-")) {
        f = stdin;
        label = "<stdin>";
    } else {
        f = fopen(path, "rb");
        if (!f) { perror(path); return 1; }
        label = path;
    }
    int rc = run(f, label);
    if (f != stdin) {
        fclose(f);
    }
    return rc;
}
