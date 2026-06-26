/*
 * decode-ydraw — diagnostic tool that takes the OSC stream emitted by a
 * ydraw producer (ycat, ygreeter, ygui apps) and decodes it via yface,
 * printing what's inside.
 *
 * Two modes:
 *
 *   Capture mode (default):
 *     decode-ydraw [FILE | -]    # parse every \eP…\e\\ envelope in FILE / stdin
 *
 *   Interpose mode (--interpose / -t):
 *     PRODUCER | decode-ydraw --interpose -o decoded.log | CONSUMER
 *       — bytes flow through unchanged on stdout (so the consumer still
 *         sees the real OSC stream), and the decoded view is teed into
 *         the -o file.
 *
 * Decoded output goes to stderr by default; pass -o FILE to redirect it
 * so the OSC stream on stdout stays clean for piping.
 *
 * What this prints for each envelope:
 *   - OSC code
 *   - args meta (magic / version / compressed / raw_size)
 *   - decoded payload size + first bytes (so the ydraw magic is visible)
 *   - every wire command in the payload:
 *       CMD_ZERO  (clear + cursor home)
 *       CMD_DELETE id=N
 *       CMD_GROUP id=N  (recurses into its payload as nested commands)
 *       FONT id=N name="…" ttf=N B
 *       TEXT_DRAWABLE_LIST @(x,y) font=N "text"
 *       SDF prims (BOX, ROUNDED_BOX, CIRCLE, …)
 *       COMPLEX (yplot, yimage, …)
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>

#include <yetty/ycore/util.h>
#include <yetty/ycore/types.h>
#include <yetty/yface/yface.h>
#include <yetty/yplatform/tty.h>
#include <yetty/ysdf/types.gen.h>
#include <yetty/ydraw-core/cmds.h>
#include <yetty/ydraw-core/font-resource.h>
#include <yetty/ydraw-core/text-drawable-list.h>
#include <yetty/ydraw-core/composite.h>

static FILE *g_out = NULL; /* set in main() — defaults to stderr */

static void out(const char *fmt, ...)
#ifndef _MSC_VER
    __attribute__((format(printf, 1, 2)))
#endif
    ;
static void out(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_out, fmt, ap);
    va_end(ap);
}

/*=========================================================================
 * Prim-stream walker — shared between the top-level envelope payload and
 * the CMD_GROUP recursive descent. `indent` controls the leading
 * whitespace so nested commands stay visually grouped under their parent.
 * Returns 0 on clean stop, -1 on a parse error (and prints which prim
 * was bad).
 *=======================================================================*/

static int walk_prims(const uint8_t *p, const uint8_t *end, int indent);

/* Print a SDF prim (one-line summary). `t` is the type word. */
static void print_sdf_prim(int idx, int indent, uint32_t t, const uint32_t *w, size_t words)
{
    float cx, cy;
    if (words >= 7) {
        memcpy(&cx, &w[5], 4);
        memcpy(&cy, &w[6], 4);
    } else {
        cx = cy = 0.0f;
    }
    const char *prefix = (indent == 0) ? "  " : "      ";
    if (t == YETTY_YSDF_BOX || t == YETTY_YSDF_ROUNDED_BOX || t == YETTY_YSDF_LINEAR_GRADIENT_BOX ||
        t == YETTY_YSDF_RADIAL_GRADIENT_BOX) {
        float hw = 0.0f, hh = 0.0f;
        if (words >= 9) {
            memcpy(&hw, &w[7], 4);
            memcpy(&hh, &w[8], 4);
        }
        uint32_t fill = 0, stroke = 0;
        if (words >= 4) {
            memcpy(&fill, &w[2], 4);
            memcpy(&stroke, &w[3], 4);
        }
        const char *name = (t == YETTY_YSDF_BOX)                   ? "BOX"
                           : (t == YETTY_YSDF_ROUNDED_BOX)         ? "ROUNDED_BOX"
                           : (t == YETTY_YSDF_LINEAR_GRADIENT_BOX) ? "LINEAR_GRADIENT_BOX"
                                                                   : "RADIAL_GRADIENT_BOX";
        out("%sprim #%d %s rect=(%.1f,%.1f)..(%.1f,%.1f) hw=%.1f hh=%.1f "
            "fill=0x%08x stroke=0x%08x\n",
            prefix, idx, name, cx - hw, cy - hh, cx + hw, cy + hh, hw, hh, fill, stroke);
    } else if (t == YETTY_YSDF_CIRCLE) {
        float r = 0.0f;
        if (words >= 8) {
            memcpy(&r, &w[7], 4);
        }
        out("%sprim #%d CIRCLE c=(%.1f,%.1f) r=%.1f\n", prefix, idx, cx, cy, r);
    } else {
        out("%sprim #%d SDF type=0x%08x c=(%.1f,%.1f) words=%zu\n", prefix, idx, t, cx, cy, words);
    }
}

/* Print a TEXT_DRAWABLE_LIST prim. Returns the parsed text-len so the caller can
 * surface it in summary stats. */
static void print_text_span(int idx, int indent, const uint8_t *p, size_t psize)
{
    const char *prefix = (indent == 0) ? "  " : "      ";
    /* Minimal direct decode — the layout is in include/yetty/ydraw-core/text-drawable-list.h.
     * Header is 8 bytes (type + payload_size); payload starts with 32 bytes of fixed
     * fields then text_len + text bytes. */
    if (psize < 8u + 32u) {
        out("%sprim #%d TEXT_DRAWABLE_LIST truncated (%zu B)\n", prefix, idx, psize);
        return;
    }
    float x, y, font_size;
    int32_t font_id;
    uint32_t color, text_len;
    memcpy(&x, p + 8, 4);
    memcpy(&y, p + 12, 4);
    memcpy(&font_size, p + 16, 4);
    /* rotation at +20, layer at +28 — skip */
    memcpy(&color, p + 24, 4);
    memcpy(&font_id, p + 32, 4);
    memcpy(&text_len, p + 36, 4);
    /* text_len decides v1 (text immediately after 32B fixed) vs v2 (40B fixed
     * with char_spacing/word_spacing). We can't distinguish from the prim
     * alone reliably — try v1 first; if text wouldn't fit, fall back. */
    size_t text_off_v1 = 8u + 32u; /* type+psize + fixed */
    size_t text_off_v2 = 8u + 40u;
    size_t text_off = text_off_v1;
    if (text_off + text_len > psize && text_off_v2 + text_len <= psize) {
        text_off = text_off_v2;
    }
    if (text_off + text_len > psize) {
        out("%sprim #%d TEXT_DRAWABLE_LIST @(%.1f,%.1f) fs=%.1f font=%d color=0x%08x "
            "text_len=%u (truncated)\n",
            prefix, idx, x, y, font_size, font_id, color, text_len);
        return;
    }
    const char *text = (const char *)(p + text_off);
    /* Trim display to first 64 chars to keep one-line entries tidy. */
    char buf[128];
    uint32_t show = text_len < sizeof(buf) - 1 ? text_len : (uint32_t)(sizeof(buf) - 5);
    memcpy(buf, text, show);
    buf[show] = '\0';
    /* Replace newlines/control bytes with '?' so the line stays printable. */
    for (uint32_t i = 0; i < show; i++) {
        unsigned char c = (unsigned char)buf[i];
        if (c < 0x20 || c == 0x7f) {
            buf[i] = '?';
        }
    }
    const char *tail = (show < text_len) ? "..." : "";
    out("%sprim #%d TEXT_DRAWABLE_LIST @(%.1f,%.1f) fs=%.1f font=%d color=0x%08x "
        "text(%u)=\"%s%s\"\n",
        prefix, idx, x, y, font_size, font_id, color, text_len, buf, tail);
}

static void print_font(int idx, int indent, const uint8_t *p, size_t psize)
{
    const char *prefix = (indent == 0) ? "  " : "      ";
    if (psize < 8u + 8u) {
        out("%sprim #%d FONT truncated\n", prefix, idx);
        return;
    }
    int32_t font_id;
    uint32_t name_len;
    memcpy(&font_id, p + 8, 4);
    memcpy(&name_len, p + 12, 4);
    size_t name_off = 16;
    if (name_off + name_len > psize) {
        out("%sprim #%d FONT id=%d name_len=%u (truncated)\n", prefix, idx, font_id, name_len);
        return;
    }
    char nbuf[96];
    uint32_t nshow = name_len < sizeof(nbuf) - 1 ? name_len : (uint32_t)(sizeof(nbuf) - 5);
    memcpy(nbuf, p + name_off, nshow);
    nbuf[nshow] = '\0';
    const char *nt = nshow < name_len ? "..." : "";
    size_t ttf_off = name_off + name_len;
    /* pad up to 4-aligned */
    ttf_off = (ttf_off + 3u) & ~(size_t)3u;
    uint32_t ttf_len = 0;
    if (ttf_off + 4 <= psize) {
        memcpy(&ttf_len, p + ttf_off, 4);
    }
    out("%sprim #%d FONT id=%d name(%u)=\"%s%s\" ttf=%u B\n", prefix, idx, font_id, name_len, nbuf,
        nt, ttf_len);
}

static void print_figure(int idx, int indent, uint32_t t, size_t psize)
{
    const char *prefix = (indent == 0) ? "  " : "      ";
    /* Type ids carry meaning — see yplot-gen.h, yimage-gen.h. We don't
     * need full coverage here; the type word is unique enough to identify
     * the producer. */
    const char *name = "COMPLEX";
    /* Best-effort known-id pretty names. */
    if (t == 0x80000003u) {
        name = "YPLOT";
    } else if (t == 0x80000004u) {
        name = "YIMAGE";
    } else if (t == 0x80000005u) {
        name = "YMESH";
    }
    out("%sprim #%d %s type=0x%08x payload=%zu B\n", prefix, idx, name, t, psize - 8);
}

/*-------------------------------------------------------------------------
 * Core walker. The wire layout is:
 *   - SDF prims (type in [0x10000000, 0x1FFFFFFF]): fixed size from
 *     ysdf_word_count
 *   - CMD_ZERO (type == 0): empty payload, 8 bytes total (FAM header)
 *   - CMD_DELETE (type == 0x80000001): type(4) + id(4) + payload_size=0(4) = 12 B
 *   - CMD_GROUP  (type == 0x80000002): type(4) + id(4) + payload_size(4)
 *                                       + payload[payload_size] — recurse
 *   - FONT       (type == 0x40000001): type(4) + payload_size(4) + payload
 *   - TEXT_DRAWABLE_LIST  (type == 0x40000002): same shape
 *   - Figures    (type >= 0x80000003): type(4) + payload_size(4) + payload
 *
 * The CMD_DELETE / CMD_GROUP types live in the [0x80000000+] range
 * because HAS_ID_FLAG is the top bit; they overlap visually with figure
 * type ids but the exact values are reserved.
 *-----------------------------------------------------------------------*/
static int walk_prims(const uint8_t *p, const uint8_t *end, int indent)
{
    const char *prefix = (indent == 0) ? "  " : "      ";
    int idx = 0;
    while (p + 4 <= end) {
        uint32_t t;
        memcpy(&t, p, 4);
        size_t psize = 0;

        /* CMD_ZERO — empty FAM record (8 bytes). */
        if (t == YETTY_YDRAW_CMD_ZERO) {
            /* Some emitters use a bare 4-byte type word for CMD_ZERO; if a
             * second u32 follows and reads as 0 we consume the FAM header,
             * otherwise just the type word. */
            if (p + 8 <= end) {
                uint32_t psz;
                memcpy(&psz, p + 4, 4);
                psize = (psz == 0u) ? 8u : 8u;
            } else {
                psize = 4u;
            }
            out("%sprim #%d CMD_ZERO\n", prefix, idx);
            p += psize;
            idx++;
            continue;
        }

        /* CMD_DELETE — 12 bytes (type, id, payload_size=0). */
        if (t == YETTY_YDRAW_CMD_DELETE) {
            if (p + 12 > end) {
                fprintf(stderr, "%sprim #%d CMD_DELETE truncated\n", prefix, idx);
                return -1;
            }
            uint32_t id;
            memcpy(&id, p + 4, 4);
            out("%sprim #%d CMD_DELETE id=%u\n", prefix, idx, id);
            p += 12;
            idx++;
            continue;
        }

        /* CMD_GROUP — 12 + payload_size bytes; recurse into payload. */
        if (t == YETTY_YDRAW_CMD_GROUP) {
            if (p + 12 > end) {
                fprintf(stderr, "%sprim #%d CMD_GROUP header truncated\n", prefix, idx);
                return -1;
            }
            uint32_t id, plen;
            memcpy(&id, p + 4, 4);
            memcpy(&plen, p + 8, 4);
            /* Compare lengths, not pointers: `p + 12` is in-bounds (checked
             * above), so forming `p + 12 + plen` for a huge untrusted plen
             * would be undefined behaviour. */
            if (plen > (size_t)(end - p) - 12u) {
                fprintf(stderr, "%sprim #%d CMD_GROUP id=%u body truncated (%u B needed)\n", prefix,
                        idx, id, plen);
                return -1;
            }
            out("%sprim #%d CMD_GROUP id=%u (payload %u B) {\n", prefix, idx, id, plen);
            walk_prims(p + 12, p + 12 + plen, indent + 1);
            out("%s} end GROUP id=%u\n", prefix, id);
            p += 12 + plen;
            idx++;
            continue;
        }

        /* SDF prim — actual values live near the top of the u32 space
         * (YETTY_YSDF_CIRCLE = 0x7FFFFFFFu and friends, see
         * include/yetty/ysdf/types.gen.h). The earlier comment in
         * cmds.h about [0x10000000, 0x1FFFFFFF] is stale — trust the
         * generator's word_count: a non-zero return means the type is a
         * recognised SDF and we know its fixed stride. */
        {
            uint32_t wc = yetty_ysdf_word_count((enum yetty_ysdf_type)t);
            if (wc > 0) {
                psize = (size_t)wc * 4u;
                if (p + psize > end) {
                    fprintf(stderr, "%sprim #%d SDF type 0x%08x truncated (%zu B needed)\n", prefix,
                            idx, t, psize);
                    return -1;
                }
                print_sdf_prim(idx, indent, t, (const uint32_t *)p, psize / 4);
                p += psize;
                idx++;
                continue;
            }
        }

        /* Drawable-list entry FAM (FONT, TEXT_DRAWABLE_LIST) and figures: type(4) + payload_size(4) + payload */
        if (p + 8 > end) {
            fprintf(stderr, "%sprim #%d FAM header truncated for type 0x%08x\n", prefix, idx, t);
            return -1;
        }
        uint32_t payload_size;
        memcpy(&payload_size, p + 4, 4);
        psize = 8u + payload_size;
        /* `p + 8` is in-bounds (checked above); compare the payload length
         * against the remaining bytes rather than forming `p + psize`, which
         * is undefined behaviour for a huge untrusted payload_size. */
        if (payload_size > (size_t)(end - p) - 8u) {
            fprintf(stderr,
                    "%sprim #%d FAM type 0x%08x payload truncated "
                    "(%u B needed, %zu remaining)\n",
                    prefix, idx, t, payload_size, (size_t)(end - p));
            return -1;
        }

        if (t == YETTY_YDRAW_RESOURCE_FONT) {
            print_font(idx, indent, p, psize);
        } else if (t == YETTY_YDRAW_TYPE_TEXT_DRAWABLE_LIST) {
            print_text_span(idx, indent, p, psize);
        } else if (yetty_ydraw_is_composite(t)) {
            print_figure(idx, indent, t, psize);
        } else {
            out("%sprim #%d UNKNOWN type=0x%08x payload=%u B\n", prefix, idx, t, payload_size);
        }
        p += psize;
        idx++;
    }
    return 0;
}

static int decode_envelope(struct yetty_yface *y, const char *body, size_t body_len)
{
    /* body has the DCS shape "<code> y <b64-args> ; <b64-payload>", where
     * 'y' is the final byte (YETTY_YWIRE_DCS_FINAL) that separates the
     * decimal code from the args/payload section. */
    size_t code_len = 0;
    while (code_len < body_len && body[code_len] >= '0' && body[code_len] <= '9') {
        code_len++;
    }
    if (code_len == 0 || code_len > 16 || code_len >= body_len) {
        out("  bad code length %zu\n", code_len);
        return -1;
    }
    if (body[code_len] != 'y') {
        out("  expected DCS final byte 'y' after code, got 0x%02x\n",
            (unsigned char)body[code_len]);
        return -1;
    }
    char code_str[20] = {0};
    memcpy(code_str, body, code_len);
    int osc_code = atoi(code_str);

    const char *after_code = body + code_len + 1;
    size_t after_code_len = body_len - code_len - 1;
    const char *semi2 = memchr(after_code, ';', after_code_len);
    if (!semi2) {
        out("  no second ;\n");
        return -1;
    }
    size_t args_len = semi2 - after_code;
    const char *payload = semi2 + 1;
    size_t payload_len = after_code_len - args_len - 1;

    out("  osc code: %d  (args b64=%zu  payload b64=%zu)\n", osc_code, args_len, payload_len);

    /* Decode args. */
    int compressed = 0;
    if (args_len > 0) {
        char meta_raw[64] = {0};
        size_t mlen = yetty_ycore_base64_decode(after_code, args_len, meta_raw, sizeof(meta_raw));
        out("  args decoded: %zu bytes\n", mlen);
        if (mlen >= sizeof(struct yetty_yface_bin_meta)) {
            const struct yetty_yface_bin_meta *m = (const struct yetty_yface_bin_meta *)meta_raw;
            const char *magic_ok = (m->magic == YETTY_YFACE_BIN_MAGIC) ? "OK" : "MISMATCH";
            out("  meta: magic=0x%08x [%s]  version=%u  "
                "compressed=%u  algo=%u  raw_size=%llu\n",
                m->magic, magic_ok, m->version, m->compressed, m->compression_algo,
                (unsigned long long)m->raw_size);
            compressed = (m->compressed != 0);
        }
    } else {
        out("  args empty (no meta)\n");
    }

    /* Decode payload through yface. */
    if (payload_len == 0) {
        out("  payload empty (clear/no-body envelope)\n");
        return 0;
    }
    struct yetty_ycore_void_result r = yetty_yface_start_read(y, compressed);
    if (!r.ok) {
        out("  start_read failed: %s\n", r.error.msg);
        return -1;
    }
    r = yetty_yface_feed(y, payload, payload_len);
    if (!r.ok) {
        out("  feed failed: %s\n", r.error.msg);
        yetty_yface_finish_read(y);
        return -1;
    }
    r = yetty_yface_finish_read(y);
    if (!r.ok) {
        out("  finish_read failed: %s\n", r.error.msg);
        return -1;
    }

    struct yetty_ycore_buffer *in = yetty_yface_in_buf(y);
    out("  payload decoded: %zu bytes; first 16:", in->size);
    for (size_t i = 0; i < in->size && i < 16; i++) {
        out(" %02x", (unsigned char)in->data[i]);
    }
    out("\n");

    /* Walk the framed payload past the 4B magic + 16B scene bounds +
     * 4B byte_count. */
    if (in->size < 24 || memcmp(in->data, "YPB1", 4) != 0) {
        out("  (no YPB1 frame — done)\n");
        return 0;
    }
    float bnd[4];
    memcpy(bnd, in->data + 4, 16);
    uint32_t byte_count;
    memcpy(&byte_count, in->data + 20, 4);
    out("  scene_bounds=(%.1f,%.1f)..(%.1f,%.1f)  byte_count=%u\n", bnd[0], bnd[1], bnd[2], bnd[3],
        byte_count);
    if ((size_t)byte_count + 24 > in->size) {
        out("  byte_count overflows buffer — stopping\n");
        return 0;
    }
    walk_prims(in->data + 24, in->data + 24 + byte_count, 0);
    return 0;
}

/*=========================================================================
 * Envelope splitter — handles both file/stdin batch mode and the
 * incremental feed used by interpose mode.
 *=======================================================================*/

struct splitter {
    char *buf;
    size_t cap;
    size_t len;
    int env_count;
    int err_count;
    struct yetty_yface *yface;
};

static void splitter_init(struct splitter *s, struct yetty_yface *y)
{
    s->buf = NULL;
    s->cap = 0;
    s->len = 0;
    s->env_count = 0;
    s->err_count = 0;
    s->yface = y;
}

static void splitter_destroy(struct splitter *s)
{
    free(s->buf);
}

static int splitter_grow(struct splitter *s, size_t need)
{
    if (need <= s->cap) {
        return 0;
    }
    size_t newcap = s->cap ? s->cap : (1u << 14);
    while (newcap < need) {
        newcap *= 2;
    }
    char *g = realloc(s->buf, newcap);
    if (!g) {
        return -1;
    }
    s->buf = g;
    s->cap = newcap;
    return 0;
}

/* Feed more bytes; consume any complete envelopes that have arrived.
 * Bytes before the first envelope are silently kept as-is (the stream
 * may carry non-ydraw output — yetty receives a mix of OSC + regular
 * terminal text). On exit `s->buf` retains only the unconsumed tail. */
static void splitter_consume(struct splitter *s)
{
    size_t pos = 0;
    while (pos + 1 < s->len) {
        if (s->buf[pos] != '\033' || s->buf[pos + 1] != 'P') {
            pos++;
            continue;
        }
        /* Look for ESC \ terminator. If we don't see it yet, stop —
         * more bytes may complete the envelope on the next read. */
        size_t open = pos + 2;
        size_t close = open;
        int found = 0;
        while (close + 1 < s->len) {
            if (s->buf[close] == '\033' && s->buf[close + 1] == '\\') {
                found = 1;
                break;
            }
            close++;
        }
        if (!found) {
            break;
        }

        size_t body_len = close - open;
        out("envelope #%d at byte %zu (body %zu B):\n", s->env_count, pos, body_len);
        if (decode_envelope(s->yface, s->buf + open, body_len) < 0) {
            s->err_count++;
        }
        s->env_count++;
        pos = close + 2;
    }
    /* Drop consumed bytes; keep the tail. */
    if (pos > 0) {
        memmove(s->buf, s->buf + pos, s->len - pos);
        s->len -= pos;
    }
}

/*=========================================================================
 * Capture mode — read everything from a file/stdin, decode, summarize.
 *=======================================================================*/

static int run_capture(FILE *f, const char *label)
{
    struct yetty_yface_ptr_result yr = yetty_yface_create();
    if (!yr.ok) {
        fprintf(stderr, "yface_create failed\n");
        return 1;
    }
    struct yetty_yface *y = yr.value;

    struct splitter s;
    splitter_init(&s, y);

    char chunk[1u << 14];
    for (;;) {
        size_t r = fread(chunk, 1, sizeof(chunk), f);
        if (r == 0) {
            if (ferror(f)) {
                fprintf(stderr, "read error on %s\n", label);
                splitter_destroy(&s);
                yetty_yface_destroy(y);
                return 1;
            }
            break;
        }
        if (splitter_grow(&s, s.len + r) < 0) {
            fprintf(stderr, "splitter grow failed\n");
            splitter_destroy(&s);
            yetty_yface_destroy(y);
            return 1;
        }
        memcpy(s.buf + s.len, chunk, r);
        s.len += r;
        splitter_consume(&s);
    }
    out("\nread from %s: found %d envelope(s), %d error(s); %zu B unparsed tail\n", label,
        s.env_count, s.err_count, s.len);

    int rc = s.err_count ? 1 : 0;
    splitter_destroy(&s);
    yetty_yface_destroy(y);
    return rc;
}

/*=========================================================================
 * Interpose mode — pipe stdin straight to stdout, parse a copy of the
 * bytes through the splitter into the decoded output file.
 *=======================================================================*/

static int run_interpose(void)
{
    struct yetty_yface_ptr_result yr = yetty_yface_create();
    if (!yr.ok) {
        fprintf(stderr, "yface_create failed\n");
        return 1;
    }
    struct yetty_yface *y = yr.value;

    struct splitter s;
    splitter_init(&s, y);

    /* OSC byte streams are binary — keep CR/LF unchanged on Windows. */
    yetty_yplatform_tty_binary_io();
    /* Unbuffered passthrough so the downstream consumer sees bytes as
     * they arrive (matches the original raw write(STDOUT_FILENO, …)). */
    setvbuf(stdout, NULL, _IONBF, 0);

    char chunk[1u << 13];
    int rc = 0;
    for (;;) {
        size_t r = fread(chunk, 1, sizeof(chunk), stdin);
        if (r == 0) {
            if (ferror(stdin) && !feof(stdin)) {
                fprintf(stderr, "interpose: stdin read failed\n");
                rc = 1;
            }
            break;
        }
        /* Pass-through to stdout first so the downstream consumer sees
         * the real bytes even if our decode-side has trouble. */
        if (fwrite(chunk, 1, r, stdout) != r) {
            fprintf(stderr, "interpose: stdout write failed\n");
            rc = 1;
            break;
        }
        /* Feed the splitter — best-effort; failure to grow only loses
         * decoded output, not the passthrough. */
        if (splitter_grow(&s, s.len + r) == 0) {
            memcpy(s.buf + s.len, chunk, r);
            s.len += r;
            splitter_consume(&s);
            fflush(g_out);
        }
    }
    out("\ninterpose closed: %d envelope(s), %d error(s); %zu B unparsed tail\n", s.env_count,
        s.err_count, s.len);

    splitter_destroy(&s);
    yetty_yface_destroy(y);
    return rc;
}

static void print_help(const char *prog)
{
    fprintf(stderr,
            "usage: %s [FILE | -]                 # decode envelopes in FILE/stdin\n"
            "       %s --interpose [-o FILE]       # pipe through, decode to FILE\n"
            "       %s -h | --help\n"
            "\n"
            "Decode the OSC envelopes emitted by a ydraw producer (ycat,\n"
            "ygreeter, ygui apps, ...).\n"
            "\n"
            "Capture mode (default):\n"
            "  Reads from FILE or stdin and prints decoded envelopes.\n"
            "      ./ygreeter | %s\n"
            "      cat capture.bin | %s -\n"
            "      %s capture.bin\n"
            "\n"
            "Interpose mode (--interpose | -t):\n"
            "  Forwards stdin to stdout unchanged and tees a decoded view to\n"
            "  the file given by -o (or stderr if -o is omitted). Use this\n"
            "  when you want to watch the wire between an app and its\n"
            "  terminal without disturbing the stream:\n"
            "      ygreeter | %s -t -o /tmp/wire.log | yetty-receiver\n"
            "\n"
            "Options:\n"
            "  -o, --output FILE    write decoded output to FILE (default stderr)\n"
            "  -t, --interpose      enable interpose pipe mode\n"
            "  -h, --help           show this message\n"
            "\n"
            "Decoded output contains:\n"
            "  - OSC code\n"
            "  - args meta (magic, version, compression flag, raw size)\n"
            "  - decoded payload size + first 16 bytes (so the `YPB1` magic\n"
            "    is directly visible)\n"
            "  - every wire command in the envelope:\n"
            "      CMD_ZERO         clear + cursor-home\n"
            "      CMD_DELETE id=N  remove the named entity\n"
            "      CMD_GROUP id=N   open / re-open an entity; nested commands\n"
            "                       are indented under the GROUP\n"
            "      FONT             id, name, ttf length\n"
            "      TEXT_DRAWABLE_LIST        position, font, color, text content\n"
            "      SDF prims        BOX / ROUNDED_BOX / CIRCLE / …\n"
            "      COMPLEX          yplot / yimage / ymesh figures\n",
            prog, prog, prog, prog, prog, prog, prog);
}

int main(int argc, char **argv)
{
    g_out = stderr;
    const char *path = NULL;
    const char *out_path = NULL;
    int interpose = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            print_help(argv[0]);
            return 0;
        }
        if (!strcmp(argv[i], "-t") || !strcmp(argv[i], "--interpose")) {
            interpose = 1;
            continue;
        }
        if (!strcmp(argv[i], "-o") || !strcmp(argv[i], "--output")) {
            if (i + 1 >= argc) {
                fprintf(stderr, "%s: -o needs FILE\n", argv[0]);
                return 1;
            }
            out_path = argv[++i];
            continue;
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

    if (out_path) {
        FILE *f = fopen(out_path, "w");
        if (!f) {
            fprintf(stderr, "open %s for write: %s\n", out_path, strerror(errno));
            return 1;
        }
        setvbuf(f, NULL, _IOLBF, 0);
        g_out = f;
    }

    int rc;
    if (interpose) {
        if (path) {
            fprintf(stderr, "%s: --interpose takes no FILE arg (reads stdin)\n", argv[0]);
            return 1;
        }
        rc = run_interpose();
    } else {
        FILE *f;
        const char *label;
        if (!path || !strcmp(path, "-")) {
            f = stdin;
            label = "<stdin>";
        } else {
            f = fopen(path, "rb");
            if (!f) {
                perror(path);
                return 1;
            }
            label = path;
        }
        rc = run_capture(f, label);
        if (f != stdin) {
            fclose(f);
        }
    }

    if (g_out && g_out != stderr) {
        fclose(g_out);
    }
    return rc;
}
