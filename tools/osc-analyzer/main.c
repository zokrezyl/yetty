/*
 * osc-analyzer — diagnostic tool for the NEW figure-tree wire format.
 *
 * Wire shape (after the b64 / lz4f decode that yface does):
 *   sequence of records, each:
 *     u32 length          payload bytes that follow this header
 *     u32 id              0 = admin / self-target; else routes to child
 *     bytes payload[length]
 *
 * id=0 records carry a u32 admin_op as the first 4 bytes of payload,
 * followed by op-specific body. id!=0 records' payload is the figure-
 * kind's own format (SDF/glyph stream for ygrid; FRAME/TEX struct for
 * ymgui; nested record stream for inner containers).
 *
 * Two modes — same as decode-ydraw:
 *
 *   Capture mode (default):
 *     osc-analyzer [FILE | -]
 *
 *   Interpose mode:
 *     APP | osc-analyzer --interpose -o decoded.log | RECEIVER
 *
 * Decoded output goes to stderr by default; pass -o FILE to redirect.
 *
 * What this prints:
 *   - envelope: OSC code, args meta (magic / compression flag / raw size),
 *     payload first bytes
 *   - records: length, id, hint of payload kind
 *   - admin records: sub-cmd (CLEAR_ALL / CREATE_CHILD / DELETE_CHILD /
 *     SET_RECT / SET_CHILD_RECT) with decoded fields
 *   - routed records (id != 0): payload first u32 to hint at kind:
 *       SDF prim stream  -> walk through with decode-ydraw-style walker
 *       ymgui FRAME/TEX  -> magic-decoded fields
 *       container body   -> recursively walk records
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <yetty/ycore/util.h>
#include <yetty/ycore/types.h>
#include <yetty/yface/yface.h>
#include <yetty/yplatform/tty.h>
#include <yetty/ysdf/types.gen.h>
#include <yetty/ydraw-core/cmds.h>
#include <yetty/ydraw-core/font-prim.h>
#include <yetty/ydraw-core/text-span-prim.h>
#include <yetty/ydraw-core/figure-types.h>
#include <yetty/yfigure/wire.h>
#include <yetty/ymgui/wire.h>

static FILE *g_out = NULL;

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

static void ind(int depth)
{
    for (int i = 0; i < depth; i++) fputs("  ", g_out);
}

/*=========================================================================
 * Inner-prim walker — for routed records whose payload is a flat stream
 * of SDF / glyph / TEXT_SPAN / FONT records (what ygrid figures receive).
 * Lifted in spirit from decode-ydraw's walk_prims; trimmed because the
 * new outer wire has its own framing (no CMD_GROUP recursion expected
 * inside an ygrid payload — those moved up to the container level).
 *=======================================================================*/

static void walk_inner_prims(const uint8_t *p, const uint8_t *end, int depth)
{
    int idx = 0;
    while (p + 4 <= end) {
        uint32_t t;
        memcpy(&t, p, 4);

        /* SDF prim — fixed stride from ysdf table. */
        uint32_t wc = yetty_ysdf_word_count((enum yetty_ysdf_type)t);
        if (wc > 0) {
            size_t psize = (size_t)wc * 4u;
            if (p + psize > end) {
                ind(depth);
                out("prim #%d SDF type=0x%08x TRUNCATED (need %zu)\n",
                    idx, t, psize);
                return;
            }
            float cx = 0, cy = 0;
            if (wc >= 7) { memcpy(&cx, &p[20], 4); memcpy(&cy, &p[24], 4); }
            const char *name =
                (t == YETTY_YSDF_BOX)                  ? "BOX" :
                (t == YETTY_YSDF_ROUNDED_BOX)          ? "ROUNDED_BOX" :
                (t == YETTY_YSDF_CIRCLE)               ? "CIRCLE" :
                (t == YETTY_YSDF_LINEAR_GRADIENT_BOX)  ? "LINEAR_GRAD_BOX" :
                (t == YETTY_YSDF_RADIAL_GRADIENT_BOX)  ? "RADIAL_GRAD_BOX" :
                                                          "SDF";
            ind(depth);
            out("prim #%d %s c=(%.1f,%.1f) bytes=%zu\n",
                idx, name, cx, cy, psize);
            p += psize;
            idx++;
            continue;
        }

        /* FAM record: type | payload_size | payload. */
        if (p + 8 > end) {
            ind(depth);
            out("prim #%d truncated FAM header (type=0x%08x)\n", idx, t);
            return;
        }
        uint32_t payload_size;
        memcpy(&payload_size, p + 4, 4);
        size_t psize = 8u + payload_size;
        if (p + psize > end) {
            ind(depth);
            out("prim #%d FAM type=0x%08x payload TRUNCATED (need %u, have %zu)\n",
                idx, t, payload_size, (size_t)(end - p) - 8);
            return;
        }

        if (t == YETTY_YDRAW_TYPE_FONT) {
            int32_t font_id = 0;
            uint32_t name_len = 0;
            if (payload_size >= 8) {
                memcpy(&font_id, p + 8, 4);
                memcpy(&name_len, p + 12, 4);
            }
            char nb[96] = {0};
            uint32_t show = name_len < sizeof(nb) - 1 ? name_len
                                                       : (uint32_t)(sizeof(nb) - 5);
            if (8u + 8u + show <= psize)
                memcpy(nb, p + 16, show);
            size_t ttf_off = (16u + name_len + 3u) & ~(size_t)3u;
            uint32_t ttf_len = 0;
            if (ttf_off + 4 <= psize) memcpy(&ttf_len, p + ttf_off, 4);
            ind(depth);
            out("prim #%d FONT id=%d name=\"%s\" ttf=%u B (record %zu B)\n",
                idx, font_id, nb, ttf_len, psize);
        } else if (t == YETTY_YDRAW_TYPE_TEXT_SPAN) {
            /* Fixed prefix: 32 B (v1) or 40 B (v2 with char/word spacing).
             * payload_size >= 40 → v2 producer (current). text follows
             * the fixed prefix immediately. */
            if (psize >= 8u + 32u + 8u) {
                float x, y, fs;
                int32_t fid;
                uint32_t color, tlen;
                memcpy(&x, p + 8, 4);
                memcpy(&y, p + 12, 4);
                memcpy(&fs, p + 16, 4);
                memcpy(&color, p + 24, 4);
                memcpy(&fid, p + 32, 4);
                memcpy(&tlen, p + 36, 4);
                /* Header byte count = 8 (type+psize) + (32 or 40). The
                 * fixed-prefix size is encoded in payload_size: v2 means
                 * payload_size >= 40, which leaves room for char_spacing
                 * (offset 32) + word_spacing (offset 36) inside the
                 * fixed prefix. Test for v2 by checking if those two
                 * fields would fit before tlen text bytes. */
                size_t text_off = (payload_size >= 40u) ? (8u + 40u)
                                                        : (8u + 32u);
                char tb[128] = {0};
                if (text_off + tlen <= psize) {
                    uint32_t show = tlen < sizeof(tb) - 1 ? tlen
                                                          : (uint32_t)(sizeof(tb) - 5);
                    memcpy(tb, p + text_off, show);
                    for (uint32_t i = 0; i < show; i++) {
                        unsigned char c = (unsigned char)tb[i];
                        if (c < 0x20 || c == 0x7f) tb[i] = '?';
                    }
                    if (show < tlen) {
                        tb[show] = '.'; tb[show+1] = '.'; tb[show+2] = '.';
                    }
                }
                ind(depth);
                out("prim #%d TEXT_SPAN @(%.1f,%.1f) fs=%.1f font=%d color=0x%08x text(%u)=\"%s\"\n",
                    idx, x, y, fs, fid, color, tlen, tb);
            } else {
                ind(depth);
                out("prim #%d TEXT_SPAN truncated\n", idx);
            }
        } else if (yetty_ydraw_is_figure(t)) {
            const char *name = "COMPLEX";
            if (t == 0x80000003u) name = "YPLOT";
            else if (t == 0x80000004u) name = "YIMAGE";
            else if (t == 0x80000005u) name = "YMESH";
            ind(depth);
            out("prim #%d %s type=0x%08x payload=%u B\n",
                idx, name, t, payload_size);
        } else {
            ind(depth);
            out("prim #%d UNKNOWN type=0x%08x payload=%u B\n",
                idx, t, payload_size);
        }
        p += psize;
        idx++;
    }
    if (p != end) {
        ind(depth);
        out("(trailing %zu bytes not parsed)\n", (size_t)(end - p));
    }
}

/*=========================================================================
 * Record-stream walker — the NEW outer wire format.
 *=======================================================================*/

static const char *admin_op_name(uint32_t op)
{
    switch (op) {
    case YETTY_YFIGURE_ADMIN_CLEAR_ALL:       return "CLEAR_ALL";
    case YETTY_YFIGURE_ADMIN_CREATE_CHILD:    return "CREATE_CHILD";
    case YETTY_YFIGURE_ADMIN_DELETE_CHILD:    return "DELETE_CHILD";
    case YETTY_YFIGURE_ADMIN_SET_CHILD_RECT:  return "SET_CHILD_RECT";
    case YETTY_YFIGURE_ADMIN_SET_RECT:        return "SET_RECT";
    default:                                   return "UNKNOWN";
    }
}

static const char *kind_name(uint32_t kind)
{
    switch (kind) {
    case YETTY_YFIGURE_KIND_CONTAINER: return "CONTAINER";
    case YETTY_YFIGURE_KIND_YGRID:     return "YGRID";
    case YETTY_YFIGURE_KIND_YMGUI:     return "YMGUI";
    case YETTY_YFIGURE_KIND_YRDAWN:    return "YRDAWN";
    case YETTY_YFIGURE_KIND_YPLOT:     return "YPLOT";
    case YETTY_YFIGURE_KIND_YIMAGE:    return "YIMAGE";
    case YETTY_YFIGURE_KIND_YVIDEO:    return "YVIDEO";
    case YETTY_YFIGURE_KIND_YZOO:      return "YZOO";
    case YETTY_YFIGURE_KIND_YJUNGLE:   return "YJUNGLE";
    default:                            return "UNKNOWN_KIND";
    }
}

static void walk_records(const uint8_t *bytes, size_t bytes_len, int depth);

/* Decide what a routed record's payload IS by peeking its first u32,
 * then print accordingly. */
static void walk_routed_payload(uint32_t id, const uint8_t *payload, size_t plen,
                                int depth)
{
    if (plen == 0) {
        ind(depth);
        out("(empty payload for id=%u)\n", id);
        return;
    }
    uint32_t first = 0;
    memcpy(&first, payload, plen >= 4 ? 4 : plen);

    /* ymgui FRAME / TEX magic? */
    if (first == YMGUI_WIRE_MAGIC_FRAME) {
        if (plen >= sizeof(struct yetty_ymgui_wire_frame)) {
            const struct yetty_ymgui_wire_frame *fh =
                (const struct yetty_ymgui_wire_frame *)payload;
            ind(depth);
            out("ymgui FRAME version=%u total=%u figure_id=%u cmd_lists=%u "
                "display=(%.1fx%.1f)\n",
                fh->version, fh->total_size, fh->figure_id, fh->cmd_list_count,
                fh->display_size_x, fh->display_size_y);
        } else {
            ind(depth);
            out("ymgui FRAME (truncated, %zu B)\n", plen);
        }
        return;
    }
    if (first == YMGUI_WIRE_MAGIC_TEX) {
        if (plen >= sizeof(struct yetty_ymgui_wire_tex)) {
            const struct yetty_ymgui_wire_tex *th =
                (const struct yetty_ymgui_wire_tex *)payload;
            ind(depth);
            out("ymgui TEX version=%u total=%u figure_id=%u tex_id=%u "
                "format=%u %ux%u\n",
                th->version, th->total_size, th->figure_id, th->tex_id,
                th->format, th->width, th->height);
        } else {
            ind(depth);
            out("ymgui TEX (truncated, %zu B)\n", plen);
        }
        return;
    }

    /* Looks like a record stream? First u32 is "length"; if it's small
     * and the second u32 is a plausible id (or 0), recurse. */
    if (plen >= 8 && first <= plen - 8) {
        ind(depth);
        out("(payload looks like nested record stream)\n");
        walk_records(payload, plen, depth + 1);
        return;
    }

    /* Otherwise treat as a flat prim stream (ygrid body). */
    ind(depth);
    out("(payload as prim stream:)\n");
    walk_inner_prims(payload, payload + plen, depth + 1);
}

static void walk_records(const uint8_t *bytes, size_t bytes_len, int depth)
{
    size_t off = 0;
    int idx = 0;
    while (off < bytes_len) {
        if (bytes_len - off < 8) {
            ind(depth);
            out("record #%d header TRUNCATED (only %zu bytes left)\n",
                idx, bytes_len - off);
            return;
        }
        uint32_t length, id;
        memcpy(&length, bytes + off + 0, 4);
        memcpy(&id, bytes + off + 4, 4);
        off += 8;

        if (length > bytes_len - off) {
            ind(depth);
            out("record #%d length=%u id=%u OVERRUNS buffer (only %zu left)\n",
                idx, length, id, bytes_len - off);
            return;
        }

        if (id == 0) {
            /* Admin record. */
            uint32_t op = 0;
            if (length >= 4) memcpy(&op, bytes + off, 4);
            ind(depth);
            out("record #%d ADMIN op=%s(%u) length=%u\n",
                idx, admin_op_name(op), op, length);
            const uint8_t *body = bytes + off + 4;
            size_t blen = length >= 4 ? length - 4 : 0;
            switch (op) {
            case YETTY_YFIGURE_ADMIN_CLEAR_ALL:
                break;
            case YETTY_YFIGURE_ADMIN_CREATE_CHILD: {
                if (blen < 28) {
                    ind(depth + 1);
                    out("(CREATE_CHILD header truncated)\n");
                    break;
                }
                uint32_t child_id, kind, init_n;
                float r[4];
                memcpy(&child_id, body + 0, 4);
                memcpy(&kind, body + 4, 4);
                memcpy(r, body + 8, 16);
                memcpy(&init_n, body + 24, 4);
                ind(depth + 1);
                out("child_id=%u kind=%s(%u) rect=(%.1f,%.1f)..(%.1f,%.1f) init=%u B\n",
                    child_id, kind_name(kind), kind, r[0], r[1], r[2], r[3], init_n);
                if (init_n > 0 && 28 + init_n <= blen) {
                    walk_routed_payload(child_id, body + 28, init_n, depth + 1);
                }
                break;
            }
            case YETTY_YFIGURE_ADMIN_DELETE_CHILD: {
                if (blen >= 4) {
                    uint32_t cid; memcpy(&cid, body, 4);
                    ind(depth + 1);
                    out("child_id=%u\n", cid);
                }
                break;
            }
            case YETTY_YFIGURE_ADMIN_SET_CHILD_RECT: {
                if (blen >= 20) {
                    uint32_t cid; float r[4];
                    memcpy(&cid, body + 0, 4);
                    memcpy(r, body + 4, 16);
                    ind(depth + 1);
                    out("child_id=%u rect=(%.1f,%.1f)..(%.1f,%.1f)\n",
                        cid, r[0], r[1], r[2], r[3]);
                }
                break;
            }
            case YETTY_YFIGURE_ADMIN_SET_RECT: {
                if (blen >= 16) {
                    float r[4]; memcpy(r, body, 16);
                    ind(depth + 1);
                    out("rect=(%.1f,%.1f)..(%.1f,%.1f)\n",
                        r[0], r[1], r[2], r[3]);
                }
                break;
            }
            default:
                break;
            }
        } else {
            ind(depth);
            out("record #%d ROUTED id=%u length=%u\n", idx, id, length);
            walk_routed_payload(id, bytes + off, length, depth + 1);
        }
        off += length;
        idx++;
    }
}

/*=========================================================================
 * Envelope decode — same yface path as decode-ydraw, then walk records.
 *=======================================================================*/

static int decode_envelope(struct yetty_yface *y,
                           const char *body, size_t body_len)
{
    const char *semi1 = memchr(body, ';', body_len);
    if (!semi1) { out("  no first ;\n"); return -1; }
    size_t code_len = semi1 - body;
    if (code_len == 0 || code_len > 16) {
        out("  bad code length %zu\n", code_len); return -1;
    }
    char code_str[20] = {0};
    memcpy(code_str, body, code_len);
    int osc_code = atoi(code_str);

    const char *after_code = semi1 + 1;
    size_t after_code_len = body_len - code_len - 1;
    const char *semi2 = memchr(after_code, ';', after_code_len);
    if (!semi2) { out("  no second ;\n"); return -1; }
    size_t args_len = semi2 - after_code;
    const char *payload = semi2 + 1;
    size_t payload_len = after_code_len - args_len - 1;

    out("  osc code: %d  (args b64=%zu  payload b64=%zu)\n",
        osc_code, args_len, payload_len);

    int compressed = 0;
    if (args_len > 0) {
        char meta_raw[64] = {0};
        size_t mlen = yetty_ycore_base64_decode(after_code, args_len,
                                                meta_raw, sizeof(meta_raw));
        if (mlen >= sizeof(struct yetty_yface_bin_meta)) {
            const struct yetty_yface_bin_meta *m =
                (const struct yetty_yface_bin_meta *)meta_raw;
            const char *magic_ok =
                (m->magic == YETTY_YFACE_BIN_MAGIC) ? "OK" : "MISMATCH";
            out("  meta: magic=0x%08x [%s] version=%u compressed=%u raw_size=%llu\n",
                m->magic, magic_ok, m->version, m->compressed,
                (unsigned long long)m->raw_size);
            compressed = (m->compressed != 0);
        }
    }

    if (payload_len == 0) {
        out("  (no payload — envelope is admin/control with empty body)\n");
        return 0;
    }
    struct yetty_ycore_void_result r = yetty_yface_start_read(y, compressed);
    if (!r.ok) { out("  start_read failed: %s\n", r.error.msg); return -1; }
    r = yetty_yface_feed(y, payload, payload_len);
    if (!r.ok) { out("  feed failed: %s\n", r.error.msg);
                 yetty_yface_finish_read(y); return -1; }
    r = yetty_yface_finish_read(y);
    if (!r.ok) { out("  finish_read failed: %s\n", r.error.msg); return -1; }

    struct yetty_ycore_buffer *in = yetty_yface_in_buf(y);
    out("  payload decoded: %zu bytes; first 16:", in->size);
    for (size_t i = 0; i < in->size && i < 16; i++)
        out(" %02x", (unsigned char)in->data[i]);
    out("\n");

    walk_records(in->data, in->size, 1);
    return 0;
}

/*=========================================================================
 * Envelope splitter + driver — same shape as decode-ydraw.
 *=======================================================================*/

struct splitter {
    char *buf;
    size_t cap;
    size_t len;
    int env_count;
    int err_count;
    struct yetty_yface *yface;
};

static int splitter_grow(struct splitter *s, size_t need)
{
    if (need <= s->cap) return 0;
    size_t newcap = s->cap ? s->cap : (1u << 14);
    while (newcap < need) newcap *= 2;
    char *g = realloc(s->buf, newcap);
    if (!g) return -1;
    s->buf = g;
    s->cap = newcap;
    return 0;
}

static void splitter_consume(struct splitter *s)
{
    size_t pos = 0;
    while (pos + 1 < s->len) {
        if (s->buf[pos] != '\033' || s->buf[pos + 1] != ']') {
            pos++;
            continue;
        }
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
        if (!found) break;

        size_t body_len = close - open;
        out("envelope #%d at byte %zu (body %zu B):\n",
            s->env_count, pos, body_len);
        if (decode_envelope(s->yface, s->buf + open, body_len) < 0) {
            s->err_count++;
        }
        s->env_count++;
        pos = close + 2;
    }
    if (pos > 0) {
        memmove(s->buf, s->buf + pos, s->len - pos);
        s->len -= pos;
    }
}

/* Raw mode — input is a contiguous {length,id}-record byte stream,
 * NOT OSC-framed. Used to inspect yui / in-process emitter output
 * that bypasses the OSC pipeline. */
static int run_raw(FILE *f, const char *label)
{
    size_t cap = 1u << 16;
    size_t len = 0;
    uint8_t *buf = (uint8_t *)malloc(cap);
    if (!buf) { fprintf(stderr, "raw: oom\n"); return 1; }
    uint8_t chunk[1u << 14];
    for (;;) {
        size_t r = fread(chunk, 1, sizeof(chunk), f);
        if (r == 0) {
            if (ferror(f)) {
                fprintf(stderr, "raw: read error on %s\n", label);
                free(buf);
                return 1;
            }
            break;
        }
        if (len + r > cap) {
            while (cap < len + r) cap *= 2;
            uint8_t *g = (uint8_t *)realloc(buf, cap);
            if (!g) { free(buf); fprintf(stderr, "raw: oom\n"); return 1; }
            buf = g;
        }
        memcpy(buf + len, chunk, r);
        len += r;
    }
    out("raw record stream from %s: %zu bytes\n", label, len);
    /* Multi-frame dumps from YUI_DUMP_RECORDS prefix each frame with
     * "===FRAME===\n" (12 bytes). Split on that marker and walk each
     * frame independently so a multi-frame capture is intelligible. */
    static const char marker[] = "===FRAME===\n";
    const size_t mlen = sizeof(marker) - 1;
    size_t off = 0;
    int frame = 0;
    while (off < len) {
        /* Find next marker (or end). */
        size_t next = len;
        for (size_t i = off; i + mlen <= len; i++) {
            if (memcmp(buf + i, marker, mlen) == 0) { next = i; break; }
        }
        if (next == off) {
            /* Marker at start: skip it, then look for next. */
            off += mlen;
            continue;
        }
        /* Strip leading marker if present (first frame). */
        size_t start = off;
        if (len - off >= mlen && memcmp(buf + off, marker, mlen) == 0) {
            start = off + mlen;
        }
        if (next > start) {
            out("\n=== frame %d (offset %zu, %zu bytes) ===\n",
                frame, start, next - start);
            walk_records(buf + start, next - start, 0);
            frame++;
        }
        off = next + ((next < len) ? mlen : 0);
    }
    if (frame == 0) {
        /* No marker — treat the whole buffer as one record stream. */
        walk_records(buf, len, 0);
    }
    free(buf);
    return 0;
}

static int run_capture(FILE *f, const char *label)
{
    struct yetty_yface_ptr_result yr = yetty_yface_create();
    if (!yr.ok) { fprintf(stderr, "yface_create failed\n"); return 1; }
    struct yetty_yface *y = yr.value;

    struct splitter s = {0};
    s.yface = y;

    char chunk[1u << 14];
    for (;;) {
        size_t r = fread(chunk, 1, sizeof(chunk), f);
        if (r == 0) {
            if (ferror(f)) {
                fprintf(stderr, "read error on %s\n", label);
                free(s.buf);
                yetty_yface_destroy(y);
                return 1;
            }
            break;
        }
        if (splitter_grow(&s, s.len + r) < 0) {
            fprintf(stderr, "splitter grow failed\n");
            free(s.buf);
            yetty_yface_destroy(y);
            return 1;
        }
        memcpy(s.buf + s.len, chunk, r);
        s.len += r;
        splitter_consume(&s);
    }
    out("\nread from %s: %d envelope(s), %d error(s); %zu B unparsed tail\n",
        label, s.env_count, s.err_count, s.len);

    int rc = s.err_count ? 1 : 0;
    free(s.buf);
    yetty_yface_destroy(y);
    return rc;
}

static int run_interpose(void)
{
    struct yetty_yface_ptr_result yr = yetty_yface_create();
    if (!yr.ok) { fprintf(stderr, "yface_create failed\n"); return 1; }
    struct yetty_yface *y = yr.value;

    struct splitter s = {0};
    s.yface = y;

    yetty_yplatform_tty_binary_io();
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
        if (fwrite(chunk, 1, r, stdout) != r) {
            fprintf(stderr, "interpose: stdout write failed\n");
            rc = 1;
            break;
        }
        if (splitter_grow(&s, s.len + r) == 0) {
            memcpy(s.buf + s.len, chunk, r);
            s.len += r;
            splitter_consume(&s);
            fflush(g_out);
        }
    }
    out("\ninterpose closed: %d envelope(s), %d error(s); %zu B tail\n",
        s.env_count, s.err_count, s.len);

    free(s.buf);
    yetty_yface_destroy(y);
    return rc;
}

static void print_help(const char *prog)
{
    fprintf(stderr,
            "usage: %s [FILE | -]            decode envelopes in FILE/stdin\n"
            "       %s --interpose [-o FILE]  pipe through, decode to FILE\n"
            "\n"
            "Decode the new figure-tree OSC wire (post-migration). For the\n"
            "legacy ydraw CMD_GROUP/UPDATE/DELETE format use decode-ydraw\n"
            "instead.\n"
            "\n"
            "Records: { length:u32 | id:u32 | payload[length] }.\n"
            "  id=0  -> admin: CLEAR_ALL / CREATE_CHILD / DELETE_CHILD /\n"
            "          SET_RECT / SET_CHILD_RECT (decoded inline)\n"
            "  id!=0 -> routed to child figure; payload printed as either\n"
            "          a ymgui FRAME/TEX, a nested record stream (container),\n"
            "          or a flat SDF/glyph stream (ygrid body)\n",
            prog, prog);
}

int main(int argc, char **argv)
{
    g_out = stderr;
    const char *path = NULL;
    const char *out_path = NULL;
    int interpose = 0;
    int raw_mode = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            print_help(argv[0]);
            return 0;
        }
        if (!strcmp(argv[i], "-t") || !strcmp(argv[i], "--interpose")) {
            interpose = 1;
            continue;
        }
        if (!strcmp(argv[i], "-r") || !strcmp(argv[i], "--raw")) {
            raw_mode = 1;
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
            return 1;
        }
        path = argv[i];
    }

    if (out_path) {
        FILE *f = fopen(out_path, "w");
        if (!f) { fprintf(stderr, "open %s: %s\n", out_path, strerror(errno)); return 1; }
        setvbuf(f, NULL, _IOLBF, 0);
        g_out = f;
    }

    int rc;
    if (interpose) {
        if (path) {
            fprintf(stderr, "%s: --interpose takes no FILE\n", argv[0]);
            return 1;
        }
        rc = run_interpose();
    } else {
        FILE *f;
        const char *label;
        if (!path || !strcmp(path, "-")) { f = stdin; label = "<stdin>"; }
        else {
            f = fopen(path, "rb");
            if (!f) { perror(path); return 1; }
            label = path;
        }
        rc = raw_mode ? run_raw(f, label) : run_capture(f, label);
        if (f != stdin) fclose(f);
    }

    if (g_out && g_out != stderr) fclose(g_out);
    return rc;
}
