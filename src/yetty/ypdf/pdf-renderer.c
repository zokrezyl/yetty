/*
 * pdf-renderer.c - PDF → ydraw buffer.
 *
 * Pass 1 (scene bounds):
 *   Walk pages, read each MediaBox, compute max page width and accumulated
 *   height (with per-page margin). No content streams are touched.
 *
 * Create the ydraw buffer with those bounds.
 *
 * Pass 2 (emission):
 *   Per page, extract embedded TTF fonts, parse the ToUnicode CMap, then
 *   run the content parser with callbacks that:
 *     - translate text-space Y into flipped screen Y, CID-remap Identity-H
 *       text, add a text span to the buffer, measure width via
 *       yetty_font_raster_font and return the advance in text-space units
 *     - emit axis-aligned rectangles as Box + 4× Segment (stroke case)
 *     - emit line segments as Segment
 */

#include <yetty/ypdf/ypdf.h>
#include <yetty/ypdf/pdf-content-parser.h>
#include <yetty/ydraw-core/draw-list.h>
#include <yetty/yfont/font.h>
#include <yetty/yfont/raster-font.h>
#include <yetty/ysdf/types.gen.h>
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/ycore/map.h>
#include <yetty/ycore/types.h>
#include <yetty/ytrace/ytrace.h>

#include <pdfio.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#if YETTY_HAS_FONTCONFIG
#include <fontconfig/fontconfig.h>
#endif

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PAGE_MARGIN 20.0f
#define MAX_FONTS 32

/* Uniform page zoom. The PDF is emitted at 1 point = 1 ydraw unit, so the
 * on-screen font size equals the PDF's point size. Scaling every emitted
 * coordinate, font size and the page bounds by this factor enlarges the whole
 * page — text included — without reflowing, which is the only way to grow PDF
 * text. 1.5 = 50% larger. Adjust this one knob to change the zoom. */
#define YPDF_RENDER_SCALE 1.5f

/*=============================================================================
 * Font tracking
 *===========================================================================*/

struct yetty_ypdf_font_info {
    char tag[64];                      /* e.g. "/F1" or "F1" */
    int buffer_font_id;                /* yetty_ydraw_draw_list font index.
                                         * Legacy single-buffer mode: assigned
                                         * once at first add, stable across
                                         * pages. Streaming mode: envelope-
                                         * local, rewritten per page. */
    struct yetty_yfont_font *raw_font; /* non-atlas metrics source */
    bool is_identity_h;
    struct yetty_ycore_map to_unicode; /* CID → Unicode */
    bool to_unicode_init;
    /* Streaming mode only: FNV1a64 of the TTF bytes (and its 16-hex form),
     * plus a flag tracking whether the TTF bytes have already been put on
     * the wire in any prior envelope. Unused in legacy mode. */
    uint64_t hash;
    char hex[17];
    bool globally_emitted;
};

/* FNV1a64 of a byte buffer. Sender-side mirror of the receiver's hash
 * (scrolling-canvas.c) — must stay bit-identical so the receiver's
 * disk-cached CDB lookup hits on subsequent envelopes. */
static uint64_t fnv1a64(const uint8_t *data, size_t len)
{
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < len; i++) {
        h ^= data[i];
        h *= 1099511628211ULL;
    }
    return h;
}

/* MediaBox is an inheritable attribute (PDF spec 14.2): pages without an
 * explicit MediaBox pick one up via their /Parent chain on the Pages
 * tree. pdfioDictGetRect doesn't walk parents — we do it here. Returns
 * true on success; *out filled with the resolved box. */
static bool resolve_media_box(pdfio_obj_t *page_obj, pdfio_rect_t *out)
{
    if (!page_obj) {
        return false;
    }
    pdfio_obj_t *cur = page_obj;
    /* Cap the walk so a cyclic /Parent (malformed PDF) can't spin
     * forever. PDF page trees are typically shallow; 64 is generous. */
    for (int depth = 0; depth < 64 && cur; depth++) {
        pdfio_dict_t *d = pdfioObjGetDict(cur);
        if (d && pdfioDictGetRect(d, "MediaBox", out)) {
            return true;
        }
        if (!d) {
            return false;
        }
        cur = pdfioDictGetObj(d, "Parent");
    }
    return false;
}

/*=============================================================================
 * Colour helpers
 *===========================================================================*/

static uint8_t clamp_byte(float f)
{
    if (f < 0.0f) {
        return 0;
    }
    if (f > 1.0f) {
        return 255;
    }
    return (uint8_t)(f * 255.0f);
}

static uint32_t rgb_to_abgr(float r, float g, float b)
{
    /* ABGR little-endian: 0xAA BB GG RR stored as (A<<24)|(B<<16)|(G<<8)|R */
    return 0xFF000000u | ((uint32_t)clamp_byte(b) << 16) | ((uint32_t)clamp_byte(g) << 8) |
           (uint32_t)clamp_byte(r);
}

/*=============================================================================
 * ToUnicode CMap parser
 *===========================================================================*/

static int read_hex4(const char *s, size_t len, size_t *pos, uint32_t *out)
{
    while (*pos < len && s[*pos] != '<') {
        (*pos)++;
    }
    if (*pos >= len) {
        return -1;
    }
    (*pos)++;
    size_t start = *pos;
    while (*pos < len && s[*pos] != '>') {
        (*pos)++;
    }
    uint32_t v = 0;
    for (size_t i = start; i < *pos; i++) {
        char c = s[i];
        v <<= 4;
        if (c >= '0' && c <= '9') {
            v |= (uint32_t)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            v |= (uint32_t)(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            v |= (uint32_t)(c - 'A' + 10);
        }
    }
    if (*pos < len) {
        (*pos)++;
    }
    *out = v;
    return 0;
}

static const char *str_find(const char *hay, size_t hay_len, const char *needle, size_t start)
{
    size_t nl = strlen(needle);
    if (start >= hay_len || nl > hay_len - start) {
        return NULL;
    }
    for (size_t i = start; i + nl <= hay_len; i++) {
        if (memcmp(hay + i, needle, nl) == 0) {
            return hay + i;
        }
    }
    return NULL;
}

static void parse_to_unicode_cmap(pdfio_obj_t *cmap_obj, struct yetty_ycore_map *map)
{
    struct _pdfio_stream_s *stream = pdfioObjOpenStream(cmap_obj, true);
    if (!stream) {
        return;
    }

    char *data = NULL;
    size_t data_size = 0;
    size_t data_cap = 0;
    uint8_t buf[4096];
    ssize_t n;
    while ((n = pdfioStreamRead(stream, buf, sizeof(buf))) > 0) {
        if (data_size + (size_t)n > data_cap) {
            size_t new_cap = data_cap ? data_cap * 2 : 8192;
            while (new_cap < data_size + (size_t)n) {
                new_cap *= 2;
            }
            char *nd = realloc(data, new_cap);
            if (!nd) {
                free(data);
                pdfioStreamClose(stream);
                return;
            }
            data = nd;
            data_cap = new_cap;
        }
        memcpy(data + data_size, buf, (size_t)n);
        data_size += (size_t)n;
    }
    pdfioStreamClose(stream);
    if (!data) {
        return;
    }

    size_t pos = 0;
    while (pos < data_size) {
        const char *bfchar = str_find(data, data_size, "beginbfchar", pos);
        const char *bfrange = str_find(data, data_size, "beginbfrange", pos);
        if (!bfchar && !bfrange) {
            break;
        }

        if (bfchar && (!bfrange || bfchar < bfrange)) {
            pos = (size_t)(bfchar - data) + 11;
            const char *end_p = str_find(data, data_size, "endbfchar", pos);
            size_t end_pos = end_p ? (size_t)(end_p - data) : data_size;
            while (pos < end_pos) {
                uint32_t cid, uni;
                if (read_hex4(data, end_pos, &pos, &cid) < 0) {
                    break;
                }
                if (read_hex4(data, end_pos, &pos, &uni) < 0) {
                    break;
                }
                yetty_ycore_map_put(map, cid, uni);
            }
            pos = end_pos + 9;
        } else {
            pos = (size_t)(bfrange - data) + 12;
            const char *end_p = str_find(data, data_size, "endbfrange", pos);
            size_t end_pos = end_p ? (size_t)(end_p - data) : data_size;
            while (pos < end_pos) {
                uint32_t start_cid, end_cid, start_uni;
                if (read_hex4(data, end_pos, &pos, &start_cid) < 0) {
                    break;
                }
                if (read_hex4(data, end_pos, &pos, &end_cid) < 0) {
                    break;
                }
                if (read_hex4(data, end_pos, &pos, &start_uni) < 0) {
                    break;
                }
                for (uint32_t c = start_cid; c <= end_cid; c++) {
                    yetty_ycore_map_put(map, c, start_uni + (c - start_cid));
                }
            }
            pos = end_pos + 10;
        }
    }
    free(data);
}

/*=============================================================================
 * CID → Unicode remap for Identity-H fonts
 *===========================================================================*/

/* Remap 2-byte CIDs encoded as WinAnsi-decoded bytes back through the
 * ToUnicode CMap. decoded has been WinAnsi-expanded to UTF-8 by the parser;
 * we undo that to recover the raw bytes, pair them into 16-bit CIDs, then
 * write a fresh UTF-8 string into out. Returns output byte length. */
static size_t remap_cid_text(const char *decoded, size_t decoded_len,
                             const struct yetty_ycore_map *to_unicode, char *out, size_t out_cap)
{
    /* Decode UTF-8 → single-byte values. Non-Latin-1 fallback to 0. */
    uint8_t raw_stack[256];
    uint8_t *raw = raw_stack;
    size_t raw_count = 0;
    size_t raw_cap = sizeof(raw_stack);
    uint8_t *raw_heap = NULL;

    const uint8_t *p = (const uint8_t *)decoded;
    const uint8_t *end = p + decoded_len;
    while (p < end) {
        uint32_t cp = 0;
        uint8_t b = *p;
        if ((b & 0x80) == 0) {
            cp = b;
            p += 1;
        } else if ((b & 0xE0) == 0xC0 && p + 1 < end) {
            cp = ((uint32_t)(b & 0x1F) << 6) | (p[1] & 0x3F);
            p += 2;
        } else if ((b & 0xF0) == 0xE0 && p + 2 < end) {
            cp = ((uint32_t)(b & 0x0F) << 12) | ((uint32_t)(p[1] & 0x3F) << 6) | (p[2] & 0x3F);
            p += 3;
        } else {
            cp = b;
            p += 1;
        }

        if (raw_count + 1 > raw_cap) {
            size_t nc = raw_cap * 2;
            uint8_t *nh = realloc(raw_heap, nc);
            if (!nh) {
                break;
            }
            if (!raw_heap) {
                memcpy(nh, raw_stack, raw_count);
            }
            raw_heap = nh;
            raw = raw_heap;
            raw_cap = nc;
        }
        raw[raw_count++] = (cp < 0x100) ? (uint8_t)cp : 0;
    }

    /* Pair bytes as 16-bit CIDs, look up in map, encode UTF-8. */
    size_t out_pos = 0;
    for (size_t j = 0; j + 1 < raw_count; j += 2) {
        uint32_t cid = ((uint32_t)raw[j] << 8) | raw[j + 1];
        uint32_t uni = cid;
        const uint32_t *lookup = yetty_ycore_map_get(to_unicode, cid);
        if (lookup) {
            uni = *lookup;
        }

        uint8_t enc[4];
        size_t enc_len;
        if (uni < 0x80) {
            enc[0] = (uint8_t)uni;
            enc_len = 1;
        } else if (uni < 0x800) {
            enc[0] = (uint8_t)(0xC0 | (uni >> 6));
            enc[1] = (uint8_t)(0x80 | (uni & 0x3F));
            enc_len = 2;
        } else if (uni < 0x10000) {
            enc[0] = (uint8_t)(0xE0 | (uni >> 12));
            enc[1] = (uint8_t)(0x80 | ((uni >> 6) & 0x3F));
            enc[2] = (uint8_t)(0x80 | (uni & 0x3F));
            enc_len = 3;
        } else {
            enc[0] = (uint8_t)(0xF0 | (uni >> 18));
            enc[1] = (uint8_t)(0x80 | ((uni >> 12) & 0x3F));
            enc[2] = (uint8_t)(0x80 | ((uni >> 6) & 0x3F));
            enc[3] = (uint8_t)(0x80 | (uni & 0x3F));
            enc_len = 4;
        }
        if (out_pos + enc_len > out_cap) {
            break;
        }
        memcpy(out + out_pos, enc, enc_len);
        out_pos += enc_len;
    }

    free(raw_heap);
    return out_pos;
}

/*=============================================================================
 * TTF hmtx patcher
 *
 * Many PDF subsetters strip or uniformise the embedded TTF's hmtx table —
 * the authoritative widths live in the PDF font dict (/Widths for simple
 * TTF, /W for CIDFontType2). FreeType reads advances out of hmtx, so a
 * stripped hmtx propagates uniform stride into both the producer's
 * measure_text and the receiver's CDB. We rewrite hmtx in-place from the
 * PDF widths before the font bytes are handed to add_font / raster_font.
 *
 * No checksum recomputation: FreeType's normal load path doesn't validate
 * head.checkSumAdjustment or per-table checksums.
 *
 * CFF (FontFile3) is left untouched — its advances live inside the
 * CharString program, which is a much bigger patcher.
 *===========================================================================*/

#define TTF_TAG(a, b, c, d)                                                                        \
    (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))

static uint16_t read_u16_be(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t read_u32_be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void write_u16_be(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFF);
}

static bool find_ttf_table(const uint8_t *ttf, size_t ttf_size, uint32_t tag, size_t *off,
                           size_t *len)
{
    if (ttf_size < 12) {
        return false;
    }
    uint16_t num_tables = read_u16_be(ttf + 4);
    if (12 + (size_t)num_tables * 16 > ttf_size) {
        return false;
    }
    for (uint16_t i = 0; i < num_tables; i++) {
        const uint8_t *e = ttf + 12 + (size_t)i * 16;
        if (read_u32_be(e) == tag) {
            uint32_t toff = read_u32_be(e + 8);
            uint32_t tlen = read_u32_be(e + 12);
            if ((size_t)toff + tlen > ttf_size) {
                return false;
            }
            *off = toff;
            *len = tlen;
            return true;
        }
    }
    return false;
}

/* round(width_pdf * units_per_em / 1000) clamped to uint16. */
static uint16_t pdf_width_to_fu(double w_pdf, uint16_t units_per_em)
{
    double w_fu = (w_pdf * (double)units_per_em) / 1000.0;
    if (w_fu < 0.0) {
        w_fu = 0.0;
    }
    if (w_fu > 65535.0) {
        w_fu = 65535.0;
    }
    return (uint16_t)(w_fu + 0.5);
}

/* Resolve dict[key] as an array, falling back to the indirect-object form. */
static pdfio_array_t *dict_get_array(pdfio_dict_t *d, const char *key)
{
    pdfio_array_t *a = pdfioDictGetArray(d, key);
    if (a) {
        return a;
    }
    pdfio_obj_t *o = pdfioDictGetObj(d, key);
    return o ? pdfioObjGetArray(o) : NULL;
}

/* PDF "WinAnsiEncoding" maps bytes 0x80-0x9F to specific Unicode chars
 * that don't match Unicode 0x80-0x9F (which are control chars). Ditto a
 * couple stragglers in 0xA0-0xFF. PDF spec Appendix D, Table D.2.
 *
 * Without this remap, the hmtx patcher looks up FT_Get_Char_Index(face, 0x95)
 * — Unicode U+0095 is a control char that the font doesn't have a glyph
 * for — and silently fails to patch the bullet's advance. The substituted
 * font's intrinsic bullet width (which is *different* from what the PDF
 * declares in /Widths) leaks through, and every char on every line that
 * contains a bullet drifts.
 *
 * Only the 0x80-0x9F range and a couple of 0xA0-range exceptions need
 * remapping; 0x00-0x7F and most of 0xA0-0xFF are direct-mapped to the
 * same Unicode codepoints. */
static uint32_t winansi_to_unicode(uint8_t b)
{
    /* Direct-mapped: 0x00-0x7F is ASCII, most of 0xA0-0xFF is Latin-1. */
    if (b < 0x80) {
        return b;
    }
    /* Sparse exceptions: 0x80-0x9F + a few high-range gaps. Anything not
     * listed falls through to direct (b). */
    switch (b) {
    case 0x80:
        return 0x20AC; /* Euro */
    case 0x82:
        return 0x201A; /* single low-9 quote */
    case 0x83:
        return 0x0192; /* florin */
    case 0x84:
        return 0x201E; /* double low-9 quote */
    case 0x85:
        return 0x2026; /* ellipsis */
    case 0x86:
        return 0x2020; /* dagger */
    case 0x87:
        return 0x2021; /* double dagger */
    case 0x88:
        return 0x02C6; /* circumflex */
    case 0x89:
        return 0x2030; /* per mille */
    case 0x8A:
        return 0x0160; /* S caron */
    case 0x8B:
        return 0x2039; /* single left guillemet */
    case 0x8C:
        return 0x0152; /* OE ligature */
    case 0x8E:
        return 0x017D; /* Z caron */
    case 0x91:
        return 0x2018; /* left single quote */
    case 0x92:
        return 0x2019; /* right single quote */
    case 0x93:
        return 0x201C; /* left double quote */
    case 0x94:
        return 0x201D; /* right double quote */
    case 0x95:
        return 0x2022; /* bullet */
    case 0x96:
        return 0x2013; /* en dash */
    case 0x97:
        return 0x2014; /* em dash */
    case 0x98:
        return 0x02DC; /* small tilde */
    case 0x99:
        return 0x2122; /* trademark */
    case 0x9A:
        return 0x0161; /* s caron */
    case 0x9B:
        return 0x203A; /* single right guillemet */
    case 0x9C:
        return 0x0153; /* oe ligature */
    case 0x9E:
        return 0x017E; /* z caron */
    case 0x9F:
        return 0x0178; /* Y diaeresis */
    default:
        return b; /* 0xA0-0xFF mostly Latin-1 direct-map */
    }
}

static void patch_simple_widths(uint8_t *ttf_data, size_t ttf_size, size_t hmtx_off,
                                uint16_t gid_limit, uint16_t units_per_em,
                                pdfio_dict_t *font_obj_dict)
{
    pdfio_array_t *widths = dict_get_array(font_obj_dict, "Widths");
    if (!widths) {
        return;
    }
    size_t widths_len = pdfioArrayGetSize(widths);
    if (widths_len == 0) {
        return;
    }
    int first_char = (int)pdfioDictGetNumber(font_obj_dict, "FirstChar");
    if (first_char < 0) {
        first_char = 0;
    }
    /* Most non-CID PDF fonts use WinAnsiEncoding; treat as default when
     * Encoding is missing. MacRomanEncoding / StandardEncoding are rare;
     * those would need separate tables. The downstream is just the hmtx
     * patcher — getting the codepoint wrong silently skips the patch
     * (FT_Get_Char_Index returns 0 for U+0095 in any font), so the
     * worst case for an unknown encoding is "behaves like before this
     * fix" (drift on those few chars). */
    const char *encoding = pdfioDictGetName(font_obj_dict, "Encoding");
    int is_winansi = !encoding || strcmp(encoding, "WinAnsiEncoding") == 0 ||
                     strcmp(encoding, "/WinAnsiEncoding") == 0;

    /* Char code → glyph index lookup needs FreeType's cmap parser; rolling
     * our own would mean handling cmap formats 0/4/6/12 and platform/encoding
     * preference. FreeType is already a yfont dependency. */
    FT_Library lib;
    if (FT_Init_FreeType(&lib) != 0) {
        return;
    }
    FT_Face face;
    if (FT_New_Memory_Face(lib, ttf_data, (FT_Long)ttf_size, 0, &face) != 0) {
        FT_Done_FreeType(lib);
        return;
    }

    int patched = 0;
    for (size_t i = 0; i < widths_len; i++) {
        double w_pdf = pdfioArrayGetNumber(widths, i);
        /* PDF subsetters often emit 0 in /Widths for char codes the document
         * doesn't actually use. We're patching a SUBSTITUTED font (the user
         * doesn't have the original), and that substitute already has correct
         * default widths for those glyphs. Overwriting with 0 here would
         * produce zero-advance glyphs the moment any text accidentally uses
         * one (e.g. a font shared across italic + body, where the body span
         * happens to reach a char the italic dict marked 0). Skip and let the
         * substitute's intrinsic width through. */
        if (w_pdf == 0.0) {
            continue;
        }
        long pdf_byte = first_char + (long)i;
        /* Translate PDF byte → Unicode codepoint via the encoding. For
         * WinAnsi this fixes 0x80-0x9F; for ASCII bytes it's identity. */
        FT_ULong code;
        if (is_winansi && pdf_byte >= 0 && pdf_byte <= 0xFF) {
            code = (FT_ULong)winansi_to_unicode((uint8_t)pdf_byte);
        } else {
            code = (FT_ULong)pdf_byte;
        }
        FT_UInt gid = FT_Get_Char_Index(face, code);
        if (gid == 0 || gid >= gid_limit) {
            continue;
        }
        uint16_t w_fu = pdf_width_to_fu(w_pdf, units_per_em);
        write_u16_be(ttf_data + hmtx_off + (size_t)gid * 4, w_fu);
        patched++;
    }

    FT_Done_Face(face);
    FT_Done_FreeType(lib);
    ydebug("ypdf: hmtx patcher (simple): %d/%zu glyph advances rewritten", patched, widths_len);
}

/* Walk the sparse /W array. Two formats per entry:
 *   c [w0 w1 ...]   — widths for c, c+1, c+2, ...
 *   c1 c2 w         — single width w for CIDs c1..c2 inclusive
 * Identity-H mapping: CID == glyph index. */
static void patch_cid_widths(uint8_t *ttf_data, size_t hmtx_off, uint16_t gid_limit,
                             uint16_t units_per_em, pdfio_dict_t *cid_font_dict)
{
    pdfio_array_t *w_arr = dict_get_array(cid_font_dict, "W");
    if (!w_arr) {
        return;
    }
    size_t w_len = pdfioArrayGetSize(w_arr);
    int patched = 0;
    size_t i = 0;
    while (i < w_len) {
        if (pdfioArrayGetType(w_arr, i) != PDFIO_VALTYPE_NUMBER || i + 1 >= w_len) {
            break;
        }
        long c = (long)pdfioArrayGetNumber(w_arr, i);
        pdfio_valtype_t t1 = pdfioArrayGetType(w_arr, i + 1);

        if (t1 == PDFIO_VALTYPE_ARRAY) {
            pdfio_array_t *inner = pdfioArrayGetArray(w_arr, i + 1);
            size_t inner_len = inner ? pdfioArrayGetSize(inner) : 0;
            for (size_t j = 0; j < inner_len; j++) {
                long cid = c + (long)j;
                if (cid < 0 || cid >= gid_limit) {
                    continue;
                }
                double w_pdf = pdfioArrayGetNumber(inner, j);
                /* Same reasoning as patch_simple_widths: 0 in /W is "char
                 * not used by this PDF", not "render with zero advance".
                 * Don't clobber the substituted font's intrinsic width. */
                if (w_pdf == 0.0) {
                    continue;
                }
                uint16_t w_fu = pdf_width_to_fu(w_pdf, units_per_em);
                write_u16_be(ttf_data + hmtx_off + (size_t)cid * 4, w_fu);
                patched++;
            }
            i += 2;
        } else if (t1 == PDFIO_VALTYPE_NUMBER && i + 2 < w_len &&
                   pdfioArrayGetType(w_arr, i + 2) == PDFIO_VALTYPE_NUMBER) {
            long c2 = (long)pdfioArrayGetNumber(w_arr, i + 1);
            double w_pdf = pdfioArrayGetNumber(w_arr, i + 2);
            if (w_pdf == 0.0) {
                i += 3;
                continue;
            }
            uint16_t w_fu = pdf_width_to_fu(w_pdf, units_per_em);
            for (long cid = c; cid <= c2; cid++) {
                if (cid < 0 || cid >= gid_limit) {
                    continue;
                }
                write_u16_be(ttf_data + hmtx_off + (size_t)cid * 4, w_fu);
                patched++;
            }
            i += 3;
        } else {
            break;
        }
    }
    ydebug("ypdf: hmtx patcher (CID): %d glyph advances rewritten", patched);
}

/* Rewrite the in-memory TTF's hmtx table from the PDF font dict's /Widths
 * (simple) or descendant CIDFont's /W (Type0). cid_font_dict is NULL for
 * simple fonts. Caller must have verified the FontFile is FontFile2 (TTF). */
static void patch_ttf_with_pdf_widths(uint8_t *ttf_data, size_t ttf_size,
                                      pdfio_dict_t *font_obj_dict, pdfio_dict_t *cid_font_dict)
{
    if (!ttf_data || ttf_size < 12) {
        return;
    }
    /* Sniff the font flavour. TrueType: 0x00010000 or 'true'. CFF: 'OTTO'. */
    uint32_t magic = read_u32_be(ttf_data);
    if (magic != 0x00010000u && magic != TTF_TAG('t', 'r', 'u', 'e')) {
        return;
    }

    size_t head_off, head_len, hhea_off, hhea_len, hmtx_off, hmtx_len, maxp_off, maxp_len;
    if (!find_ttf_table(ttf_data, ttf_size, TTF_TAG('h', 'e', 'a', 'd'), &head_off, &head_len) ||
        !find_ttf_table(ttf_data, ttf_size, TTF_TAG('h', 'h', 'e', 'a'), &hhea_off, &hhea_len) ||
        !find_ttf_table(ttf_data, ttf_size, TTF_TAG('h', 'm', 't', 'x'), &hmtx_off, &hmtx_len) ||
        !find_ttf_table(ttf_data, ttf_size, TTF_TAG('m', 'a', 'x', 'p'), &maxp_off, &maxp_len)) {
        return;
    }
    if (head_len < 20 || hhea_len < 36 || maxp_len < 6) {
        return;
    }
    uint16_t units_per_em = read_u16_be(ttf_data + head_off + 18);
    uint16_t num_h_metrics = read_u16_be(ttf_data + hhea_off + 34);
    uint16_t num_glyphs = read_u16_be(ttf_data + maxp_off + 4);
    if (units_per_em == 0 || num_h_metrics == 0) {
        return;
    }
    if (hmtx_off + (size_t)num_h_metrics * 4 > ttf_size) {
        return;
    }

    /* Per the OpenType spec, glyphs at index >= numberOfHMetrics share the
     * advance from the LAST hmtx record (numberOfHMetrics-1). If the
     * subsetter set numberOfHMetrics < num_glyphs and we patched that
     * shared record, we'd silently change every glyph that aliases it —
     * worse than leaving the table alone. So when the table is collapsed,
     * we only allow patching the strictly-private records [0 .. nHM-2].
     * Properly fixing the collapsed case requires expanding hmtx and
     * shifting downstream tables — out of scope per the issue. */
    uint16_t patch_limit = num_h_metrics;
    if (num_h_metrics < num_glyphs && patch_limit > 0) {
        patch_limit -= 1;
    }
    ydebug("ypdf: hmtx patcher: upem=%u numberOfHMetrics=%u num_glyphs=%u patch_limit=%u (cid=%d)",
           units_per_em, num_h_metrics, num_glyphs, patch_limit, cid_font_dict ? 1 : 0);
    if (patch_limit == 0) {
        return;
    }

    if (cid_font_dict) {
        patch_cid_widths(ttf_data, hmtx_off, patch_limit, units_per_em, cid_font_dict);
    } else {
        patch_simple_widths(ttf_data, ttf_size, hmtx_off, patch_limit, units_per_em, font_obj_dict);
    }
}

/*=============================================================================
 * Font extraction
 *===========================================================================*/

/* PDF "BaseFont" names sometimes carry a 6-letter random subset prefix
 * followed by '+', e.g. "ABCDEF+Arial-BoldMT". Strip that for fontconfig
 * lookup — we want the underlying family. */
static const char *strip_pdf_subset_prefix(const char *name)
{
    if (!name || !*name) {
        return name;
    }
    /* Skip leading '/' if pdfio leaked it. */
    if (name[0] == '/') {
        name++;
    }
    /* "AAAAAA+Foo" → "Foo". The prefix is exactly 6 uppercase letters. */
    if (name[0] && name[1] && name[2] && name[3] && name[4] && name[5] && name[6] == '+') {
        int all_upper = 1;
        for (int i = 0; i < 6; i++) {
            if (name[i] < 'A' || name[i] > 'Z') {
                all_upper = 0;
                break;
            }
        }
        if (all_upper) {
            name += 7;
        }
    }
    return name;
}

/* Resolve a PDF font name (e.g. "Arial-BoldMT") to an absolute TTF path
 * via fontconfig. Returns malloc'd string on hit, NULL on miss / no
 * fontconfig at build time. Used to substitute system fonts when the PDF
 * doesn't embed FontFile2/FontFile3 (very common for Arial / Times /
 * Helvetica references in older PDFs). */
static char *resolve_pdf_font_via_fontconfig(const char *base_font)
{
#if YETTY_HAS_FONTCONFIG
    const char *name = strip_pdf_subset_prefix(base_font);
    if (!name || !*name) {
        return NULL;
    }
    static int s_fc_inited = 0;
    if (!s_fc_inited) {
        if (!FcInit()) {
            return NULL;
        }
        s_fc_inited = 1;
    }
    /* PDF font names use '-' to glue family/style ("Arial-BoldMT"). For
     * fontconfig, ',' or ' ' work better — try the raw name first, then
     * a sanitised variant if needed. */
    FcPattern *pat = FcNameParse((const FcChar8 *)name);
    if (!pat) {
        return NULL;
    }
    FcConfigSubstitute(NULL, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);

    FcResult result;
    FcPattern *match = FcFontMatch(NULL, pat, &result);
    char *out = NULL;
    if (match) {
        FcChar8 *file = NULL;
        if (FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch && file) {
            out = strdup((const char *)file);
        }
        FcPatternDestroy(match);
    }
    FcPatternDestroy(pat);
    return out;
#else
    (void)base_font;
    return NULL;
#endif
}

/* Read entire file into a malloc'd buffer. NULL on error. */
static uint8_t *load_font_file(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long sz = ftell(f);
    if (sz <= 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    uint8_t *buf = malloc((size_t)sz);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *out_len = (size_t)sz;
    return buf;
}

static int find_font_idx(const struct yetty_ypdf_font_info *fonts, size_t count, const char *tag)
{
    for (size_t i = 0; i < count; i++) {
        if (strcmp(fonts[i].tag, tag) == 0) {
            return (int)i;
        }
        /* Match with/without leading slash. */
        if (tag[0] == '/' && strcmp(fonts[i].tag, tag + 1) == 0) {
            return (int)i;
        }
        if (tag[0] != '/' && fonts[i].tag[0] == '/' && strcmp(fonts[i].tag + 1, tag) == 0) {
            return (int)i;
        }
    }
    return -1;
}

/* Per-font failures are intentionally absorbed (logged + continue). The
 * Result return is structural (this function calls Result-returning
 * APIs); it always reports OK. */
static struct yetty_ycore_void_result extract_page_fonts(pdfio_obj_t *page_obj,
                                                         struct yetty_ydraw_draw_list *buffer,
                                                         struct yetty_ypdf_font_info *fonts,
                                                         size_t *font_count)
{
    pdfio_dict_t *page_dict = pdfioObjGetDict(page_obj);
    if (!page_dict) {
        return YETTY_OK_VOID();
    }

    pdfio_dict_t *resources = pdfioDictGetDict(page_dict, "Resources");
    if (!resources) {
        pdfio_obj_t *ro = pdfioDictGetObj(page_dict, "Resources");
        if (ro) {
            resources = pdfioObjGetDict(ro);
        }
    }
    if (!resources) {
        return YETTY_OK_VOID();
    }

    pdfio_dict_t *font_dict = pdfioDictGetDict(resources, "Font");
    if (!font_dict) {
        pdfio_obj_t *fo = pdfioDictGetObj(resources, "Font");
        if (fo) {
            font_dict = pdfioObjGetDict(fo);
        }
    }
    if (!font_dict) {
        return YETTY_OK_VOID();
    }

    size_t n = pdfioDictGetNumPairs(font_dict);
    for (size_t fi = 0; fi < n; fi++) {
        const char *tag = pdfioDictGetKey(font_dict, fi);
        if (!tag) {
            continue;
        }
        if (find_font_idx(fonts, *font_count, tag) >= 0) {
            continue;
        }
        if (*font_count >= MAX_FONTS) {
            ywarn("ypdf: MAX_FONTS reached, skipping %s", tag);
            continue;
        }

        pdfio_obj_t *font_obj = pdfioDictGetObj(font_dict, tag);
        if (!font_obj) {
            continue;
        }
        pdfio_dict_t *font_obj_dict = pdfioObjGetDict(font_obj);
        if (!font_obj_dict) {
            continue;
        }

        pdfio_obj_t *font_desc_obj = pdfioDictGetObj(font_obj_dict, "FontDescriptor");
        bool is_identity_h = false;
        pdfio_obj_t *to_unicode_obj = pdfioDictGetObj(font_obj_dict, "ToUnicode");
        const char *encoding = pdfioDictGetName(font_obj_dict, "Encoding");
        if (encoding &&
            (strcmp(encoding, "Identity-H") == 0 || strcmp(encoding, "/Identity-H") == 0)) {
            is_identity_h = true;
        }

        /* Kept around for the hmtx patcher: for Type0 fonts the
         * authoritative widths live in the descendant CIDFont's /W. */
        pdfio_dict_t *cid_font_dict = NULL;
        if (!font_desc_obj) {
            pdfio_array_t *desc = pdfioDictGetArray(font_obj_dict, "DescendantFonts");
            if (!desc) {
                pdfio_obj_t *dfo = pdfioDictGetObj(font_obj_dict, "DescendantFonts");
                if (dfo) {
                    desc = pdfioObjGetArray(dfo);
                }
            }
            if (desc && pdfioArrayGetSize(desc) > 0) {
                pdfio_obj_t *cid_font_obj = pdfioArrayGetObj(desc, 0);
                if (cid_font_obj) {
                    cid_font_dict = pdfioObjGetDict(cid_font_obj);
                    if (cid_font_dict) {
                        font_desc_obj = pdfioDictGetObj(cid_font_dict, "FontDescriptor");
                    }
                }
            }
        }
        if (!font_desc_obj) {
            continue;
        }

        pdfio_dict_t *font_desc_dict = pdfioObjGetDict(font_desc_obj);
        if (!font_desc_dict) {
            continue;
        }

        bool is_truetype = true;
        pdfio_obj_t *font_file_obj = pdfioDictGetObj(font_desc_dict, "FontFile2");
        if (!font_file_obj) {
            font_file_obj = pdfioDictGetObj(font_desc_dict, "FontFile3");
            is_truetype = false;
        }

        /* No FontFile in the descriptor means the PDF references this font
         * by name and expects the reader to provide it from the system —
         * standard practice for "core 14" fonts (Times, Helvetica, Arial,
         * etc.) and any other font the producer assumed would be locally
         * installed. Fall back to fontconfig: substitute a system TTF and
         * proceed exactly as if the font were embedded.
         *
         * Without this, ypdf returns NULL for every text_emit_cb's
         * measure_text path — the parser then doesn't advance the text
         * matrix between Tj calls and every glyph stacks at the same
         * coordinate, producing the "complete chaos" overlap pattern
         * seen with sample.pdf and other PDFs that don't embed fonts. */
        uint8_t *substituted_bytes = NULL;
        size_t substituted_sz = 0;
        if (!font_file_obj) {
            const char *base_font = pdfioDictGetName(font_obj_dict, "BaseFont");
            if (!base_font) {
                base_font = pdfioDictGetName(font_desc_dict, "FontName");
            }
            if (base_font) {
                char *path = resolve_pdf_font_via_fontconfig(base_font);
                if (path) {
                    substituted_bytes = load_font_file(path, &substituted_sz);
                    if (substituted_bytes) {
                        is_truetype = true;
                        ydebug("ypdf: substituting system font for '%s' from %s "
                               "(%zu bytes)",
                               base_font, path, substituted_sz);
                    } else {
                        ywarn("ypdf: fontconfig found '%s' at %s but read failed", base_font, path);
                    }
                    free(path);
                } else {
                    ywarn("ypdf: no FontFile and fontconfig miss for '%s' — "
                          "spans using this font will fall back to default and "
                          "may overlap (no measure_text)",
                          base_font);
                }
            }
        }
        if (!font_file_obj && !substituted_bytes) {
            continue;
        }

        struct _pdfio_stream_s *ff_stream = NULL;
        if (font_file_obj) {
            ff_stream = pdfioObjOpenStream(font_file_obj, true);
            if (!ff_stream) {
                free(substituted_bytes);
                continue;
            }
        }

        uint8_t *bytes = substituted_bytes;
        size_t sz = substituted_sz, cap = substituted_sz;
        uint8_t chunk[8192];
        ssize_t rd;
        while (ff_stream && (rd = pdfioStreamRead(ff_stream, chunk, sizeof(chunk))) > 0) {
            if (sz + (size_t)rd > cap) {
                size_t nc = cap ? cap * 2 : 16384;
                while (nc < sz + (size_t)rd) {
                    nc *= 2;
                }
                uint8_t *nb = realloc(bytes, nc);
                if (!nb) {
                    free(bytes);
                    bytes = NULL;
                    break;
                }
                bytes = nb;
                cap = nc;
            }
            memcpy(bytes + sz, chunk, (size_t)rd);
            sz += (size_t)rd;
        }
        if (ff_stream) {
            pdfioStreamClose(ff_stream);
        }
        if (!bytes || sz == 0) {
            free(bytes);
            continue;
        }

        /* Many subsetters strip the TTF's hmtx. Rewrite it from the PDF's
         * authoritative widths before any consumer (buffer or raster_font)
         * sees the bytes; both downstream paths copy out of `bytes`. */
        if (is_truetype) {
            patch_ttf_with_pdf_widths(bytes, sz, font_obj_dict, cid_font_dict);
        }

        /* Store TTF in buffer. */
        struct yetty_ycore_buffer ttf_buf = {bytes, sz, sz};
        struct yetty_ycore_int_result id_res =
            yetty_ydraw_draw_list_add_font(buffer, &ttf_buf, tag);

        int buf_font_id = -1;
        if (YETTY_IS_OK(id_res)) {
            buf_font_id = id_res.value;
        }

        /* Metrics-only font for measurement. */
        struct yetty_font_font_result ff_res =
            yetty_yfont_raster_font_create_from_data(bytes, sz, tag, NULL, 32.0f);

        free(bytes);

        if (YETTY_IS_ERR(ff_res)) {
            ywarn("ypdf: raster_font from TTF '%s' failed: %s", tag, ff_res.error.msg);
            continue;
        }

        struct yetty_ypdf_font_info *fi_out = &fonts[*font_count];
        memset(fi_out, 0, sizeof(*fi_out));
        strncpy(fi_out->tag, tag, sizeof(fi_out->tag) - 1);
        fi_out->buffer_font_id = buf_font_id;
        fi_out->raw_font = ff_res.value;
        fi_out->is_identity_h = is_identity_h;

        if (to_unicode_obj) {
            if (yetty_ycore_map_init(&fi_out->to_unicode, 1024) == 0) {
                fi_out->to_unicode_init = true;
                parse_to_unicode_cmap(to_unicode_obj, &fi_out->to_unicode);
                ydebug("ypdf: font '%s' ToUnicode: %u entries", tag, fi_out->to_unicode.count);
            }
        }

        (*font_count)++;
        ydebug("ypdf: extracted font '%s' (%zu bytes) identityH=%d", tag, sz, (int)is_identity_h);
    }
    return YETTY_OK_VOID();
}

/* Streaming variant: per page, populate the per-document fonts[] on first
 * encounter (load TTF, hash, build raster_font, parse ToUnicode), then
 * emit a FONT prim into THIS page's envelope buffer for every font this
 * page references. The prim is full-bytes on the first envelope that ships
 * a given font (hash recorded in fi->globally_emitted), and hash-ref on
 * all later envelopes referencing the same font.
 *
 * fi->buffer_font_id is rewritten per envelope to the envelope-local id
 * (TEXT_SPAN refs read this through find_font_idx in the callbacks).
 *
 * Per-font failures are absorbed (logged + continue), matching the legacy
 * extract_page_fonts. */
static struct yetty_ycore_void_result extract_and_emit_page_fonts_streaming(
    pdfio_obj_t *page_obj, struct yetty_ydraw_draw_list *buffer, struct yetty_ypdf_font_info *fonts,
    size_t *font_count)
{
    pdfio_dict_t *page_dict = pdfioObjGetDict(page_obj);
    if (!page_dict) {
        return YETTY_OK_VOID();
    }

    pdfio_dict_t *resources = pdfioDictGetDict(page_dict, "Resources");
    if (!resources) {
        pdfio_obj_t *ro = pdfioDictGetObj(page_dict, "Resources");
        if (ro) {
            resources = pdfioObjGetDict(ro);
        }
    }
    if (!resources) {
        return YETTY_OK_VOID();
    }

    pdfio_dict_t *font_dict = pdfioDictGetDict(resources, "Font");
    if (!font_dict) {
        pdfio_obj_t *fo = pdfioDictGetObj(resources, "Font");
        if (fo) {
            font_dict = pdfioObjGetDict(fo);
        }
    }
    if (!font_dict) {
        return YETTY_OK_VOID();
    }

    size_t n = pdfioDictGetNumPairs(font_dict);
    for (size_t fi_idx = 0; fi_idx < n; fi_idx++) {
        const char *tag = pdfioDictGetKey(font_dict, fi_idx);
        if (!tag) {
            continue;
        }

        int existing = find_font_idx(fonts, *font_count, tag);
        if (existing >= 0) {
            /* Already loaded on a prior page. Just emit a FONT prim
             * into THIS envelope so the receiver can rebuild its
             * envelope-local font_map. */
            struct yetty_ypdf_font_info *fi = &fonts[existing];
            if (fi->globally_emitted) {
                struct yetty_ycore_int_result id_res =
                    yetty_ydraw_draw_list_add_font_ref(buffer, fi->hex);
                if (YETTY_IS_OK(id_res)) {
                    fi->buffer_font_id = id_res.value;
                } else {
                    ywarn("ypdf: streaming hash-ref emit failed for '%s'", tag);
                    fi->buffer_font_id = -1;
                }
            } else {
                /* Edge case: previously seen tag but not yet emitted
                 * (load_font_file or raster_font failed last time). Re-attempt
                 * — but we don't have bytes any more. Best effort: skip. */
                fi->buffer_font_id = -1;
            }
            continue;
        }

        if (*font_count >= MAX_FONTS) {
            ywarn("ypdf: MAX_FONTS reached, skipping %s", tag);
            continue;
        }

        /* New font for the document: load bytes, hmtx-patch, hash, build
         * raster_font, parse ToUnicode. Then emit full FONT prim. */
        pdfio_obj_t *font_obj = pdfioDictGetObj(font_dict, tag);
        if (!font_obj) {
            continue;
        }
        pdfio_dict_t *font_obj_dict = pdfioObjGetDict(font_obj);
        if (!font_obj_dict) {
            continue;
        }

        pdfio_obj_t *font_desc_obj = pdfioDictGetObj(font_obj_dict, "FontDescriptor");
        bool is_identity_h = false;
        pdfio_obj_t *to_unicode_obj = pdfioDictGetObj(font_obj_dict, "ToUnicode");
        const char *encoding = pdfioDictGetName(font_obj_dict, "Encoding");
        if (encoding &&
            (strcmp(encoding, "Identity-H") == 0 || strcmp(encoding, "/Identity-H") == 0)) {
            is_identity_h = true;
        }

        pdfio_dict_t *cid_font_dict = NULL;
        if (!font_desc_obj) {
            pdfio_array_t *desc = pdfioDictGetArray(font_obj_dict, "DescendantFonts");
            if (!desc) {
                pdfio_obj_t *dfo = pdfioDictGetObj(font_obj_dict, "DescendantFonts");
                if (dfo) {
                    desc = pdfioObjGetArray(dfo);
                }
            }
            if (desc && pdfioArrayGetSize(desc) > 0) {
                pdfio_obj_t *cid_font_obj = pdfioArrayGetObj(desc, 0);
                if (cid_font_obj) {
                    cid_font_dict = pdfioObjGetDict(cid_font_obj);
                    if (cid_font_dict) {
                        font_desc_obj = pdfioDictGetObj(cid_font_dict, "FontDescriptor");
                    }
                }
            }
        }
        if (!font_desc_obj) {
            continue;
        }

        pdfio_dict_t *font_desc_dict = pdfioObjGetDict(font_desc_obj);
        if (!font_desc_dict) {
            continue;
        }

        bool is_truetype = true;
        pdfio_obj_t *font_file_obj = pdfioDictGetObj(font_desc_dict, "FontFile2");
        if (!font_file_obj) {
            font_file_obj = pdfioDictGetObj(font_desc_dict, "FontFile3");
            is_truetype = false;
        }

        uint8_t *substituted_bytes = NULL;
        size_t substituted_sz = 0;
        if (!font_file_obj) {
            const char *base_font = pdfioDictGetName(font_obj_dict, "BaseFont");
            if (!base_font) {
                base_font = pdfioDictGetName(font_desc_dict, "FontName");
            }
            if (base_font) {
                char *path = resolve_pdf_font_via_fontconfig(base_font);
                if (path) {
                    substituted_bytes = load_font_file(path, &substituted_sz);
                    if (substituted_bytes) {
                        is_truetype = true;
                    }
                    free(path);
                }
            }
        }
        if (!font_file_obj && !substituted_bytes) {
            continue;
        }

        struct _pdfio_stream_s *ff_stream = NULL;
        if (font_file_obj) {
            ff_stream = pdfioObjOpenStream(font_file_obj, true);
            if (!ff_stream) {
                free(substituted_bytes);
                continue;
            }
        }

        uint8_t *bytes = substituted_bytes;
        size_t sz = substituted_sz, cap = substituted_sz;
        uint8_t chunk[8192];
        ssize_t rd;
        while (ff_stream && (rd = pdfioStreamRead(ff_stream, chunk, sizeof(chunk))) > 0) {
            if (sz + (size_t)rd > cap) {
                size_t nc = cap ? cap * 2 : 16384;
                while (nc < sz + (size_t)rd) {
                    nc *= 2;
                }
                uint8_t *nb = realloc(bytes, nc);
                if (!nb) {
                    free(bytes);
                    bytes = NULL;
                    break;
                }
                bytes = nb;
                cap = nc;
            }
            memcpy(bytes + sz, chunk, (size_t)rd);
            sz += (size_t)rd;
        }
        if (ff_stream) {
            pdfioStreamClose(ff_stream);
        }
        if (!bytes || sz == 0) {
            free(bytes);
            continue;
        }

        if (is_truetype) {
            patch_ttf_with_pdf_widths(bytes, sz, font_obj_dict, cid_font_dict);
        }

        /* Hash the (possibly patched) bytes — same encoding the receiver
         * uses to key its on-disk CDB cache. */
        uint64_t h = fnv1a64(bytes, sz);
        char hex[17];
        snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)h);

        /* Full FONT prim with TTF bytes — this is the first time the
         * document ships this font. */
        struct yetty_ycore_buffer ttf_buf = {bytes, sz, sz};
        struct yetty_ycore_int_result id_res =
            yetty_ydraw_draw_list_add_font(buffer, &ttf_buf, tag);
        int buf_font_id = -1;
        if (YETTY_IS_OK(id_res)) {
            buf_font_id = id_res.value;
        }

        struct yetty_font_font_result ff_res =
            yetty_yfont_raster_font_create_from_data(bytes, sz, tag, NULL, 32.0f);

        free(bytes);

        if (YETTY_IS_ERR(ff_res)) {
            ywarn("ypdf: raster_font from TTF '%s' failed: %s", tag, ff_res.error.msg);
            continue;
        }

        struct yetty_ypdf_font_info *fi_out = &fonts[*font_count];
        memset(fi_out, 0, sizeof(*fi_out));
        strncpy(fi_out->tag, tag, sizeof(fi_out->tag) - 1);
        fi_out->buffer_font_id = buf_font_id;
        fi_out->raw_font = ff_res.value;
        fi_out->is_identity_h = is_identity_h;
        fi_out->hash = h;
        memcpy(fi_out->hex, hex, sizeof(fi_out->hex));
        fi_out->globally_emitted = (buf_font_id >= 0);

        if (to_unicode_obj) {
            if (yetty_ycore_map_init(&fi_out->to_unicode, 1024) == 0) {
                fi_out->to_unicode_init = true;
                parse_to_unicode_cmap(to_unicode_obj, &fi_out->to_unicode);
            }
        }

        (*font_count)++;
        ydebug("ypdf: streaming first-emit '%s' (%zu bytes) hash=%s", tag, sz, hex);
    }
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Render context (shared by the three callbacks via user_data)
 *===========================================================================*/

struct yetty_ypdf_render_ctx {
    struct yetty_ydraw_draw_list *buffer;
    struct yetty_ypdf_font_info *fonts;
    size_t font_count;
    float y_offset;
    float page_height;
    float scale; /* uniform page zoom applied to every emitted coordinate + size */
};

/*=============================================================================
 * Callbacks
 *===========================================================================*/

static struct float_result text_emit_cb(void *ud, const char *text, size_t text_len, float pos_x,
                                        float pos_y, float effective_size, float rotation_radians,
                                        const struct yetty_ypdf_text_state *state)
{

    struct yetty_ypdf_render_ctx *c = (struct yetty_ypdf_render_ctx *)ud;
    float sx = pos_x * c->scale;
    float sy = (c->y_offset + (c->page_height - pos_y)) * c->scale;

    int font_idx = find_font_idx(c->fonts, c->font_count, state->font_name);
    struct yetty_ypdf_font_info *fi = (font_idx >= 0) ? &c->fonts[font_idx] : NULL;

    /* CID remap on Identity-H fonts with a ToUnicode map. */
    const char *emit_text_p = text;
    size_t emit_text_len = text_len;
    char remap_buf[4096];
    if (fi && fi->is_identity_h && fi->to_unicode_init && fi->to_unicode.count > 0) {
        emit_text_len =
            remap_cid_text(text, text_len, &fi->to_unicode, remap_buf, sizeof(remap_buf));
        emit_text_p = remap_buf;
    }

    /* Use the PDF's actual non-stroking colour. Hardcoding 0xFF000000
     * meant every glyph rendered black; on a black-background terminal
     * that's invisible. Auto-flip near-black to white so default body
     * text shows up on dark terminals — anything coloured (red/green/blue
     * highlights, syntax tags, etc.) passes through unchanged. */
    float fr = state->fill_r, fg = state->fill_g, fb = state->fill_b;
    float lum = 0.2126f * fr + 0.7152f * fg + 0.0722f * fb;
    if (lum < 0.05f) {
        fr = 1.0f;
        fg = 1.0f;
        fb = 1.0f;
    }
    uint32_t color = rgb_to_abgr(fr, fg, fb);

    struct yetty_ycore_buffer tb = {(uint8_t *)(uintptr_t)emit_text_p, emit_text_len,
                                    emit_text_len};
    int32_t font_id = fi ? (int32_t)fi->buffer_font_id : -1;
    /* Forward PDF text-state Tc/Tw to the canvas in display pixels.
     * effective_size is the apparent on-screen font size after the full
     * trm transform; state->font_size is the unscaled Tfs. The text
     * matrix already absorbed any non-uniform scale, so converting the
     * Tc/Tw (which are in Tfs units per the PDF spec) by effective_size
     * gives the on-screen pixel offsets the canvas should add per
     * codepoint and per ASCII space. Without this, lines that contain
     * spaces drift by ~1.2 px per space (mutool sees the actual on-page
     * positions; canvas would otherwise sum only font advances). */
    float disp_size = effective_size * c->scale;
    float h_scale_for_emit = state->horizontal_scaling / 100.0f;
    float emit_char_spacing = state->char_spacing * disp_size * h_scale_for_emit;
    float emit_word_spacing = state->word_spacing * disp_size * h_scale_for_emit;
    (void)yetty_ydraw_draw_list_add_text_full(
        c->buffer, sx, sy, &tb, disp_size, color, 0, font_id,
        (fabsf(rotation_radians) > 0.001f) ? -rotation_radians : 0.0f, emit_char_spacing,
        emit_word_spacing);

    /* Measure advance at the PDF text-state font size (Tfs), which matches
     * the units of the text matrix. See PDF spec 9.4.4. */
    float raw_advance;
    if (fi && fi->raw_font && fi->raw_font->ops && fi->raw_font->ops->measure_text) {
        struct float_result r = fi->raw_font->ops->measure_text(fi->raw_font, emit_text_p,
                                                                emit_text_len, state->font_size);
        if (YETTY_IS_ERR(r)) {
            return YETTY_ERR(float, r.error.msg);
        }
        raw_advance = r.value;
    } else {
        return YETTY_ERR(float, "no font for advance measurement");
    }

    /* Count codepoints / spaces for char/word spacing. */
    int num_cps = 0, num_spaces = 0;
    const uint8_t *p = (const uint8_t *)emit_text_p;
    const uint8_t *pe = p + emit_text_len;
    while (p < pe) {
        uint32_t cp = 0;
        uint8_t b = *p;
        if ((b & 0x80) == 0) {
            cp = b;
            p += 1;
        } else if ((b & 0xE0) == 0xC0) {
            cp = b & 0x1F;
            p += 1;
            if (p < pe) {
                cp = (cp << 6) | (*p & 0x3F);
                p += 1;
            }
        } else if ((b & 0xF0) == 0xE0) {
            p += 3;
        } else if ((b & 0xF8) == 0xF0) {
            p += 4;
        } else {
            p += 1;
        }
        num_cps++;
        if (cp == 0x20) {
            num_spaces++;
        }
    }

    float h_scale = state->horizontal_scaling / 100.0f;
    float advance =
        (raw_advance + num_cps * state->char_spacing + num_spaces * state->word_spacing) * h_scale;

    return YETTY_OK(float, advance);
}

YETTY_EXTERNAL_CALLBACK
static void rect_paint_cb(void *ud, float x, float y, float w, float h,
                          enum yetty_ypdf_paint_mode mode, float sr, float sg, float sb, float fr,
                          float fg, float fb, float line_width)
{

    struct yetty_ypdf_render_ctx *c = (struct yetty_ypdf_render_ctx *)ud;
    float s = c->scale;
    float rx = x * s;
    float ry = (c->y_offset + (c->page_height - y - h)) * s;
    float w_s = w * s;
    float h_s = h * s;
    float lw_s = line_width * s;

    if (mode == YETTY_YPDF_PAINT_FILL || mode == YETTY_YPDF_PAINT_FILL_AND_STROKE) {
        uint32_t fc = rgb_to_abgr(fr, fg, fb);
        struct yetty_ysdf_box geom = {
            .center_x = rx + w_s * 0.5f,
            .center_y = ry + h_s * 0.5f,
            .half_width = w_s * 0.5f,
            .half_height = h_s * 0.5f,
            .corner_radius = 0.0f,
        };
        yetty_ydraw_draw_list_add_cmd_add_box(c->buffer, 0, 0, fc, 0, 0.0f, &geom);
    }
    if (mode == YETTY_YPDF_PAINT_STROKE || mode == YETTY_YPDF_PAINT_FILL_AND_STROKE) {
        uint32_t sc = rgb_to_abgr(sr, sg, sb);
        struct yetty_ysdf_segment sides[4] = {
            {rx, ry, rx + w_s, ry},
            {rx + w_s, ry, rx + w_s, ry + h_s},
            {rx + w_s, ry + h_s, rx, ry + h_s},
            {rx, ry + h_s, rx, ry},
        };
        for (int i = 0; i < 4; i++) {
            yetty_ydraw_draw_list_add_cmd_add_segment(c->buffer, 0, 0, 0, sc, lw_s, &sides[i]);
        }
    }
}

YETTY_EXTERNAL_CALLBACK
static void line_paint_cb(void *ud, float x0, float y0, float x1, float y1, float r, float g,
                          float b, float line_width)
{

    struct yetty_ypdf_render_ctx *c = (struct yetty_ypdf_render_ctx *)ud;
    float s = c->scale;
    uint32_t color = rgb_to_abgr(r, g, b);
    struct yetty_ysdf_segment geom = {
        .start_x = x0 * s,
        .start_y = (c->y_offset + (c->page_height - y0)) * s,
        .end_x = x1 * s,
        .end_y = (c->y_offset + (c->page_height - y1)) * s,
    };
    yetty_ydraw_draw_list_add_cmd_add_segment(c->buffer, 0, 0, 0, color, line_width * s, &geom);
}

/*=============================================================================
 * Public entry point
 *===========================================================================*/

struct yetty_ypdf_render_result yetty_ypdf_render_pdf(struct _pdfio_file_s *pdf)
{
    if (!pdf) {
        return YETTY_ERR(yetty_ypdf_render, "pdf is NULL");
    }

    int page_count = (int)pdfioFileGetNumPages(pdf);
    struct yetty_ypdf_render_output out = {0};
    out.page_count = page_count;
    if (page_count == 0) {
        return YETTY_ERR(yetty_ypdf_render, "pdf has no pages");
    }

    /* ---------- Pass 1: scene bounds from MediaBoxes ---------- */
    float max_width = 0.0f;
    float total_height = 0.0f;
    float first_page_height = 0.0f;

    for (int page = 0; page < page_count; page++) {
        pdfio_obj_t *page_obj = pdfioFileGetPage(pdf, (size_t)page);
        if (!page_obj) {
            continue;
        }
        pdfio_rect_t mb = {0};
        if (!resolve_media_box(page_obj, &mb)) {
            return YETTY_ERR(yetty_ypdf_render, "page is missing MediaBox");
        }
        float pw = (float)(mb.x2 - mb.x1);
        float ph = (float)(mb.y2 - mb.y1);
        if (page == 0) {
            first_page_height = ph;
        }
        if (pw > max_width) {
            max_width = pw;
        }
        total_height += ph;
        if (page < page_count - 1) {
            total_height += PAGE_MARGIN;
        }
    }

    struct yetty_ydraw_draw_list_config cfg = {
        .scene_min_x = 0.0f,
        .scene_min_y = 0.0f,
        .scene_max_x = max_width * YPDF_RENDER_SCALE,
        .scene_max_y = total_height * YPDF_RENDER_SCALE,
    };
    struct yetty_ydraw_draw_list_result br = yetty_ydraw_draw_list_config_buffer_create(&cfg);
    if (YETTY_IS_ERR(br)) {
        return YETTY_ERR(yetty_ypdf_render, br.error.msg);
    }
    struct yetty_ydraw_draw_list *buffer = br.value;

    /* ---------- Pass 2: emission ---------- */
    struct yetty_ypdf_font_info fonts[MAX_FONTS];
    memset(fonts, 0, sizeof(fonts));
    size_t font_count = 0;

    struct yetty_ypdf_render_ctx ctx = {
        .buffer = buffer,
        .fonts = fonts,
        .font_count = 0,
        .y_offset = 0.0f,
        .page_height = 0.0f,
        .scale = YPDF_RENDER_SCALE,
    };

    struct yetty_ypdf_content_parser_callbacks cb = {
        .text_emit = text_emit_cb,
        .rect_paint = rect_paint_cb,
        .line_paint = line_paint_cb,
        .user_data = &ctx,
    };

    float y_offset = 0.0f;
    for (int page = 0; page < page_count; page++) {
        pdfio_obj_t *page_obj = pdfioFileGetPage(pdf, (size_t)page);
        if (!page_obj) {
            continue;
        }
        pdfio_rect_t mb = {0};
        if (!resolve_media_box(page_obj, &mb)) {
            yetty_ydraw_draw_list_destroy(buffer);
            return YETTY_ERR(yetty_ypdf_render, "page is missing MediaBox");
        }
        float ph = (float)(mb.y2 - mb.y1);

        struct yetty_ycore_void_result fr =
            extract_page_fonts(page_obj, buffer, fonts, &font_count);
        (void)fr; /* per-font failures already logged inside */

        struct yetty_ypdf_content_parser_ptr_result pr =
            yetty_ypdf_content_parser_callbacks_content_parser_create(&cb);
        if (YETTY_IS_ERR(pr)) {
            yetty_ydraw_draw_list_destroy(buffer);
            return YETTY_ERR(yetty_ypdf_render, pr.error.msg);
        }
        yetty_ypdf_content_parser_set_page_height(pr.value, ph);

        ctx.font_count = font_count;
        ctx.y_offset = y_offset;
        ctx.page_height = ph;

        size_t num_streams = pdfioPageGetNumStreams(page_obj);
        for (size_t s = 0; s < num_streams; s++) {
            struct _pdfio_stream_s *stream = pdfioPageOpenStream(page_obj, s, true);
            if (!stream) {
                continue;
            }
            (void)yetty_ypdf_content_parser_parse_stream(pr.value, stream);
            pdfioStreamClose(stream);
        }

        yetty_ypdf_content_parser_destroy(pr.value);

        y_offset += ph;
        if (page < page_count - 1) {
            y_offset += PAGE_MARGIN;
        }
    }

    /* Cleanup per-font state. */
    for (size_t i = 0; i < font_count; i++) {
        if (fonts[i].raw_font && fonts[i].raw_font->ops && fonts[i].raw_font->ops->destroy) {
            fonts[i].raw_font->ops->destroy(fonts[i].raw_font);
        }
        if (fonts[i].to_unicode_init) {
            yetty_ycore_map_destroy(&fonts[i].to_unicode);
        }
    }

    out.buffer = buffer;
    out.total_height = y_offset;
    out.first_page_height = first_page_height;
    out.max_width = max_width;

    ydebug("ypdf: %d pages, %zu fonts, total_h=%.1f", page_count, font_count, y_offset);
    return YETTY_OK(yetty_ypdf_render, out);
}

/*=============================================================================
 * Streaming entry point — one envelope per page.
 *===========================================================================*/

struct yetty_ypdf_stream_render_result yetty_ypdf_render_pdf_streaming(
    struct _pdfio_file_s *pdf, yetty_ypdf_page_emit_fn on_page, void *user_data)
{
    if (!pdf) {
        return YETTY_ERR(yetty_ypdf_stream_render, "pdf is NULL");
    }
    if (!on_page) {
        return YETTY_ERR(yetty_ypdf_stream_render, "on_page is NULL");
    }

    int page_count = (int)pdfioFileGetNumPages(pdf);
    struct yetty_ypdf_stream_render_output out = {0};
    out.page_count = page_count;
    if (page_count == 0) {
        return YETTY_ERR(yetty_ypdf_stream_render, "pdf has no pages");
    }

    /* Per-document font state survives across envelopes. raw_font (metrics)
     * and ToUnicode maps are used by text_emit_cb on every page the font
     * is referenced; the globally_emitted flag suppresses re-shipping the
     * TTF bytes after the first envelope. */
    struct yetty_ypdf_font_info fonts[MAX_FONTS];
    memset(fonts, 0, sizeof(fonts));
    size_t font_count = 0;

    struct yetty_ypdf_render_ctx ctx = {0};
    struct yetty_ypdf_content_parser_callbacks cb = {
        .text_emit = text_emit_cb,
        .rect_paint = rect_paint_cb,
        .line_paint = line_paint_cb,
        .user_data = &ctx,
    };

    float first_page_height = 0.0f;
    float max_width = 0.0f;
    struct yetty_ycore_void_result emit_err = YETTY_OK_VOID();

    for (int page = 0; page < page_count; page++) {
        pdfio_obj_t *page_obj = pdfioFileGetPage(pdf, (size_t)page);
        if (!page_obj) {
            continue;
        }
        pdfio_rect_t mb = {0};
        if (!resolve_media_box(page_obj, &mb)) {
            emit_err = YETTY_ERR(yetty_ycore_void, "page is missing MediaBox");
            break;
        }
        float pw = (float)(mb.x2 - mb.x1);
        float ph = (float)(mb.y2 - mb.y1);
        if (page == 0) {
            first_page_height = ph;
        }
        if (pw > max_width) {
            max_width = pw;
        }

        /* Page envelope: scene spans the page plus a bottom margin so the
         * receiver scrolls a uniform gap between pages. Coordinates inside
         * are page-relative (origin top-left, y=0..ph) and scaled by
         * YPDF_RENDER_SCALE on emit, so the scene bounds scale to match. */
        struct yetty_ydraw_draw_list_config bcfg = {
            .scene_min_x = 0.0f,
            .scene_min_y = 0.0f,
            .scene_max_x = pw * YPDF_RENDER_SCALE,
            .scene_max_y = (ph + PAGE_MARGIN) * YPDF_RENDER_SCALE,
        };
        struct yetty_ydraw_draw_list_result br = yetty_ydraw_draw_list_config_buffer_create(&bcfg);
        if (YETTY_IS_ERR(br)) {
            emit_err = YETTY_ERR(yetty_ycore_void, "draw_list create failed", br);
            break;
        }
        struct yetty_ydraw_draw_list *page_buffer = br.value;

        /* Per-page font handling: load on first encounter, emit full TTF
         * on first envelope using the font, hash-ref on subsequent ones. */
        (void)extract_and_emit_page_fonts_streaming(page_obj, page_buffer, fonts, &font_count);

        struct yetty_ypdf_content_parser_ptr_result pr =
            yetty_ypdf_content_parser_callbacks_content_parser_create(&cb);
        if (YETTY_IS_ERR(pr)) {
            yetty_ydraw_draw_list_destroy(page_buffer);
            emit_err = YETTY_ERR(yetty_ycore_void, "content parser create", pr);
            break;
        }
        yetty_ypdf_content_parser_set_page_height(pr.value, ph);

        ctx.buffer = page_buffer;
        ctx.fonts = fonts;
        ctx.font_count = font_count;
        ctx.y_offset = 0.0f;
        ctx.page_height = ph;
        ctx.scale = YPDF_RENDER_SCALE;

        size_t num_streams = pdfioPageGetNumStreams(page_obj);
        for (size_t s = 0; s < num_streams; s++) {
            struct _pdfio_stream_s *stream = pdfioPageOpenStream(page_obj, s, true);
            if (!stream) {
                continue;
            }
            (void)yetty_ypdf_content_parser_parse_stream(pr.value, stream);
            pdfioStreamClose(stream);
        }

        yetty_ypdf_content_parser_destroy(pr.value);

        struct yetty_ycore_void_result er = on_page(user_data, page, page_count, page_buffer);
        yetty_ydraw_draw_list_destroy(page_buffer);
        if (YETTY_IS_ERR(er)) {
            emit_err = YETTY_ERR(yetty_ycore_void, "on_page callback failed", er);
            break;
        }
    }

    /* Cleanup per-font state. raw_font and ToUnicode survive for the whole
     * document; release them now that no more envelopes will be emitted. */
    for (size_t i = 0; i < font_count; i++) {
        if (fonts[i].raw_font && fonts[i].raw_font->ops && fonts[i].raw_font->ops->destroy) {
            fonts[i].raw_font->ops->destroy(fonts[i].raw_font);
        }
        if (fonts[i].to_unicode_init) {
            yetty_ycore_map_destroy(&fonts[i].to_unicode);
        }
    }

    if (YETTY_IS_ERR(emit_err)) {
        return YETTY_ERR(yetty_ypdf_stream_render, "streaming render aborted", emit_err);
    }

    out.first_page_height = first_page_height;
    out.max_width = max_width;
    ydebug("ypdf: streaming %d pages, %zu fonts", page_count, font_count);
    return YETTY_OK(yetty_ypdf_stream_render, out);
}
