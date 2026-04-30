/*
 * pdf-renderer.c - PDF → ypaint buffer.
 *
 * Pass 1 (scene bounds):
 *   Walk pages, read each MediaBox, compute max page width and accumulated
 *   height (with per-page margin). No content streams are touched.
 *
 * Create the ypaint buffer with those bounds.
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
#include <yetty/ypaint-core/buffer.h>
#include <yetty/yfont/font.h>
#include <yetty/yfont/raster-font.h>
#include <yetty/ysdf/types.gen.h>
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/ycore/map.h>
#include <yetty/ycore/types.h>
#include <yetty/ytrace.h>

#include <pdfio.h>

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define PAGE_MARGIN 20.0f
#define MAX_FONTS 32

/*=============================================================================
 * Font tracking
 *===========================================================================*/

struct ypdf_font_info {
    char tag[64];                     /* e.g. "/F1" or "F1" */
    int buffer_font_id;               /* yetty_ypaint_core_buffer font index */
    struct yetty_font_font *raw_font; /* non-atlas metrics source */
    bool is_identity_h;
    struct yetty_ycore_map to_unicode; /* CID → Unicode */
    bool to_unicode_init;
};

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
    pdfio_stream_t *stream = pdfioObjOpenStream(cmap_obj, true);
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
 * Font extraction
 *===========================================================================*/

static int find_font_idx(const struct ypdf_font_info *fonts, size_t count, const char *tag)
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
                                                         struct yetty_ypaint_core_buffer *buffer,
                                                         struct ypdf_font_info *fonts,
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
                    pdfio_dict_t *cid_dict = pdfioObjGetDict(cid_font_obj);
                    if (cid_dict) {
                        font_desc_obj = pdfioDictGetObj(cid_dict, "FontDescriptor");
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

        pdfio_obj_t *font_file_obj = pdfioDictGetObj(font_desc_dict, "FontFile2");
        if (!font_file_obj) {
            font_file_obj = pdfioDictGetObj(font_desc_dict, "FontFile3");
        }
        if (!font_file_obj) {
            continue;
        }

        pdfio_stream_t *ff_stream = pdfioObjOpenStream(font_file_obj, true);
        if (!ff_stream) {
            continue;
        }

        uint8_t *bytes = NULL;
        size_t sz = 0, cap = 0;
        uint8_t chunk[8192];
        ssize_t rd;
        while ((rd = pdfioStreamRead(ff_stream, chunk, sizeof(chunk))) > 0) {
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
        pdfioStreamClose(ff_stream);
        if (!bytes || sz == 0) {
            free(bytes);
            continue;
        }

        /* Store TTF in buffer. */
        struct yetty_ycore_buffer ttf_buf = {bytes, sz, sz};
        struct yetty_ycore_int_result id_res =
            yetty_ypaint_core_buffer_add_font(buffer, &ttf_buf, tag);

        int buf_font_id = -1;
        if (YETTY_IS_OK(id_res)) {
            buf_font_id = id_res.value;
        }

        /* Metrics-only font for measurement. */
        struct yetty_font_font_result ff_res =
            yetty_font_raster_font_create_from_data(bytes, sz, tag, NULL, 32.0f);

        free(bytes);

        if (YETTY_IS_ERR(ff_res)) {
            ywarn("ypdf: raster_font from TTF '%s' failed: %s", tag, ff_res.error.msg);
            continue;
        }

        struct ypdf_font_info *fi_out = &fonts[*font_count];
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

/*=============================================================================
 * Render context (shared by the three callbacks via user_data)
 *===========================================================================*/

struct render_ctx {
    struct yetty_ypaint_core_buffer *buffer;
    struct ypdf_font_info *fonts;
    size_t font_count;
    float y_offset;
    float page_height;
};

/*=============================================================================
 * Callbacks
 *===========================================================================*/

static struct float_result text_emit_cb(void *ud, const char *text, size_t text_len, float pos_x,
                                        float pos_y, float effective_size, float rotation_radians,
                                        const struct yetty_ypdf_text_state *state)
{

    struct render_ctx *c = (struct render_ctx *)ud;
    float sx = pos_x;
    float sy = c->y_offset + (c->page_height - pos_y);

    int font_idx = find_font_idx(c->fonts, c->font_count, state->font_name);
    struct ypdf_font_info *fi = (font_idx >= 0) ? &c->fonts[font_idx] : NULL;

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
    (void)yetty_ypaint_core_buffer_add_text(
        c->buffer, sx, sy, &tb, effective_size, color, 0, font_id,
        (fabsf(rotation_radians) > 0.001f) ? -rotation_radians : 0.0f);

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
    (void)effective_size;

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

    struct render_ctx *c = (struct render_ctx *)ud;
    float rx = x;
    float ry = c->y_offset + (c->page_height - y - h);

    if (mode == YETTY_YPDF_PAINT_FILL || mode == YETTY_YPDF_PAINT_FILL_AND_STROKE) {
        uint32_t fc = rgb_to_abgr(fr, fg, fb);
        struct yetty_ysdf_box geom = {
            .center_x = rx + w * 0.5f,
            .center_y = ry + h * 0.5f,
            .half_width = w * 0.5f,
            .half_height = h * 0.5f,
            .corner_radius = 0.0f,
        };
        yetty_ysdf_add_box(c->buffer, 0, fc, 0, 0.0f, &geom);
    }
    if (mode == YETTY_YPDF_PAINT_STROKE || mode == YETTY_YPDF_PAINT_FILL_AND_STROKE) {
        uint32_t sc = rgb_to_abgr(sr, sg, sb);
        struct yetty_ysdf_segment sides[4] = {
            {rx, ry, rx + w, ry},
            {rx + w, ry, rx + w, ry + h},
            {rx + w, ry + h, rx, ry + h},
            {rx, ry + h, rx, ry},
        };
        for (int i = 0; i < 4; i++) {
            yetty_ysdf_add_segment(c->buffer, 0, 0, sc, line_width, &sides[i]);
        }
    }
}

YETTY_EXTERNAL_CALLBACK
static void line_paint_cb(void *ud, float x0, float y0, float x1, float y1, float r, float g,
                          float b, float line_width)
{

    struct render_ctx *c = (struct render_ctx *)ud;
    uint32_t color = rgb_to_abgr(r, g, b);
    struct yetty_ysdf_segment geom = {
        .start_x = x0,
        .start_y = c->y_offset + (c->page_height - y0),
        .end_x = x1,
        .end_y = c->y_offset + (c->page_height - y1),
    };
    yetty_ysdf_add_segment(c->buffer, 0, 0, color, line_width, &geom);
}

/*=============================================================================
 * Public entry point
 *===========================================================================*/

struct yetty_ypdf_render_result yetty_ypdf_render_pdf(pdfio_file_t *pdf)
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
        pdfio_dict_t *pd = pdfioObjGetDict(page_obj);
        pdfio_rect_t mb = {0};
        if (!pdfioDictGetRect(pd, "MediaBox", &mb)) {
            mb.x1 = 0.0;
            mb.y1 = 0.0;
            mb.x2 = 612.0;
            mb.y2 = 792.0;
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

    struct yetty_ypaint_core_buffer_config cfg = {
        .scene_min_x = 0.0f,
        .scene_min_y = 0.0f,
        .scene_max_x = max_width,
        .scene_max_y = total_height,
    };
    struct yetty_ypaint_core_buffer_result br = yetty_ypaint_core_buffer_create(&cfg);
    if (YETTY_IS_ERR(br)) {
        return YETTY_ERR(yetty_ypdf_render, br.error.msg);
    }
    struct yetty_ypaint_core_buffer *buffer = br.value;

    /* ---------- Pass 2: emission ---------- */
    struct ypdf_font_info fonts[MAX_FONTS];
    memset(fonts, 0, sizeof(fonts));
    size_t font_count = 0;

    struct render_ctx ctx = {
        .buffer = buffer,
        .fonts = fonts,
        .font_count = 0,
        .y_offset = 0.0f,
        .page_height = 0.0f,
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
        pdfio_dict_t *pd = pdfioObjGetDict(page_obj);
        pdfio_rect_t mb = {0};
        if (!pdfioDictGetRect(pd, "MediaBox", &mb)) {
            mb.x1 = 0.0;
            mb.y1 = 0.0;
            mb.x2 = 612.0;
            mb.y2 = 792.0;
        }
        float ph = (float)(mb.y2 - mb.y1);

        struct yetty_ycore_void_result fr =
            extract_page_fonts(page_obj, buffer, fonts, &font_count);
        (void)fr; /* per-font failures already logged inside */

        struct yetty_ypdf_content_parser_ptr_result pr = yetty_ypdf_content_parser_create(&cb);
        if (YETTY_IS_ERR(pr)) {
            yetty_ypaint_core_buffer_destroy(buffer);
            return YETTY_ERR(yetty_ypdf_render, pr.error.msg);
        }
        yetty_ypdf_content_parser_set_page_height(pr.value, ph);

        ctx.font_count = font_count;
        ctx.y_offset = y_offset;
        ctx.page_height = ph;

        size_t num_streams = pdfioPageGetNumStreams(page_obj);
        for (size_t s = 0; s < num_streams; s++) {
            pdfio_stream_t *stream = pdfioPageOpenStream(page_obj, s, true);
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
