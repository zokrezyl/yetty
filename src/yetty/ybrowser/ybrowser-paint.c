/*
 * ylexbor-paint — emit ypaint primitives from a laid-out box vector.
 *
 * Every YL_BOX_BLOCK with non-zero alpha background → ysdf box.
 * Every YL_BOX_INLINE_TEXT → TEXT_SPAN flyweight prim.
 * Every YL_BOX_INLINE_IMAGE → yimage prim with decoded pixels (or grey
 *                             placeholder when the fetch/decode fails).
 *
 * Image decoding: per-document cache keyed by the resolved absolute
 * URL of `<img src>`. Cache hits skip the fetch + decode. The same
 * decoded pixel buffer is re-serialized into a fresh yimage prim each
 * paint call, since ypaint takes ownership of the prim bytes via
 * add_prim — we keep the cache copy alive for repaints.
 *
 * Color packing matches what ypaint's shader expects: low byte = R,
 * high byte = A. Same convention ynetsurf-plotters.c uses.
 */

#include "ybrowser-internal.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if YETTY_HAVE_LIBPNG
#include <png.h>
#endif
#include <stb_image.h>
#if YETTY_HAVE_TURBOJPEG
#include <turbojpeg.h>
#endif

#include <yetty/ypaint-core/buffer.h>
#include <yetty/ysdf/types.gen.h>
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/yimage/yimage-gen.h>
#include <yetty/ytrace/ytrace.h>

static uint32_t pack_rgba(struct yetty_ylexbor_color c)
{
    if (c.a == 0) {
        return 0;
    }
    return ((uint32_t)c.r) | ((uint32_t)c.g << 8) | ((uint32_t)c.b << 16) | ((uint32_t)c.a << 24);
}

#if YETTY_HAVE_LIBPNG
/* Decode a PNG byte stream into RGBA8 via libpng's simplified API
 * (png_image_*). Allocates *out_pixels (caller frees with free()).
 * Returns 1 on success, 0 on failure.
 *
 * libpng handles the full PNG feature surface — interlaced, palette,
 * 16-bit channel, color-profile-tagged — that stb_image's PNG decoder
 * either fails or mishandles on real-world payloads. The simplified
 * API normalizes everything to PNG_FORMAT_RGBA (matching the wire
 * format we feed yimage). */
static int decode_png(const uint8_t *bytes, size_t len, uint32_t **out_pixels, int *out_w,
                      int *out_h)
{
    png_image image = {0};
    image.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&image, bytes, len)) {
        png_image_free(&image);
        return 0;
    }
    image.format = PNG_FORMAT_RGBA; /* normalize all variants to 8-bit RGBA */
    size_t total = (size_t)PNG_IMAGE_SIZE(image);
    if (total == 0 || image.width == 0 || image.height == 0) {
        png_image_free(&image);
        return 0;
    }
    uint32_t *pixels = malloc(total);
    if (!pixels) {
        png_image_free(&image);
        return 0;
    }
    if (!png_image_finish_read(&image, NULL, pixels,
                               /*row_stride=*/0, NULL)) {
        free(pixels);
        png_image_free(&image);
        return 0;
    }
    *out_pixels = pixels;
    *out_w = (int)image.width;
    *out_h = (int)image.height;
    png_image_free(&image);
    return 1;
}
#endif

#if YETTY_HAVE_TURBOJPEG
/* Decode a JPEG byte stream into RGBA8 via libjpeg-turbo. Same
 * malloc/free contract as decode_png. We use TJPF_RGBA so the byte
 * order matches what yimage stores (little-endian RGBA). */
static int decode_jpeg(const uint8_t *bytes, size_t len, uint32_t **out_pixels, int *out_w,
                       int *out_h)
{
    tjhandle d = tjInitDecompress();
    if (!d) {
        return 0;
    }
    int w = 0, h = 0, subsamp = 0, cs = 0;
    if (tjDecompressHeader3(d, bytes, (unsigned long)len, &w, &h, &subsamp, &cs) != 0 || w <= 0 ||
        h <= 0) {
        tjDestroy(d);
        return 0;
    }
    uint32_t *pixels = malloc((size_t)w * (size_t)h * 4);
    if (!pixels) {
        tjDestroy(d);
        return 0;
    }
    if (tjDecompress2(d, bytes, (unsigned long)len, (unsigned char *)pixels, w, /*pitch=*/0, h,
                      TJPF_RGBA, TJFLAG_FASTDCT) != 0) {
        free(pixels);
        tjDestroy(d);
        return 0;
    }
    tjDestroy(d);
    *out_pixels = pixels;
    *out_w = w;
    *out_h = h;
    return 1;
}
#endif

/* Decode any other format (GIF, BMP, WebP via stb's loader, …) with
 * stb_image. Kept as a fallback so we don't lose support for the
 * formats libpng/turbojpeg don't handle. */
static int decode_other(const uint8_t *bytes, size_t len, uint32_t **out_pixels, int *out_w,
                        int *out_h)
{
    int w = 0, h = 0, channels = 0;
    stbi_uc *pixels = stbi_load_from_memory((const stbi_uc *)bytes, (int)len, &w, &h, &channels, 4);
    if (!pixels || w <= 0 || h <= 0) {
        if (pixels) {
            stbi_image_free(pixels);
        }
        return 0;
    }
    size_t npx = (size_t)w * (size_t)h;
    uint32_t *out = malloc(npx * sizeof(uint32_t));
    if (!out) {
        stbi_image_free(pixels);
        return 0;
    }
    memcpy(out, pixels, npx * sizeof(uint32_t));
    stbi_image_free(pixels);
    *out_pixels = out;
    *out_w = w;
    *out_h = h;
    return 1;
}

/* SVG sniff — parse width/height/viewBox from the XML header so the
 * placeholder we emit lands at the correct aspect ratio, even without
 * a real SVG rasterizer. We render a small grey/checker pattern as
 * the actual pixels so users can tell where SVGs would go. */
static float parse_svg_length(const char *s, size_t len)
{
    while (len > 0 && (*s == ' ' || *s == '\t')) {
        s++;
        len--;
    }
    if (len == 0) {
        return 0;
    }
    char buf[32];
    size_t n = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
    memcpy(buf, s, n);
    buf[n] = 0;
    return (float)atof(buf);
}

static int sniff_svg_dimensions(const uint8_t *bytes, size_t len, int *out_w, int *out_h)
{
    if (len < 4) {
        return 0;
    }
    const char *p = (const char *)bytes;
    const char *end = p + len;
    /* Skip leading XML declaration / DOCTYPE / whitespace; cap at
	 * the first 4 KB so we don't scan an entire file. */
    if (end - p > 4096) {
        end = p + 4096;
    }
    const char *svg = NULL;
    for (const char *q = p; q + 4 <= end; q++) {
        if (q[0] == '<' && q[1] == 's' && q[2] == 'v' && q[3] == 'g' &&
            (q[4] == ' ' || q[4] == '\t' || q[4] == '\n' || q[4] == '\r' || q[4] == '>')) {
            svg = q + 4;
            break;
        }
    }
    if (!svg) {
        return 0;
    }
    const char *gt = memchr(svg, '>', (size_t)(end - svg));
    if (!gt) {
        gt = end;
    }
    float w = 0, h = 0, vb_w = 0, vb_h = 0;
    /* width="..." */
    const char *q = svg;
    while (q < gt) {
        if (gt - q >= 7 && memcmp(q, "width=\"", 7) == 0) {
            const char *e2 = memchr(q + 7, '"', (size_t)(gt - q - 7));
            if (e2) {
                w = parse_svg_length(q + 7, e2 - q - 7);
            }
        }
        if (gt - q >= 8 && memcmp(q, "height=\"", 8) == 0) {
            const char *e2 = memchr(q + 8, '"', (size_t)(gt - q - 8));
            if (e2) {
                h = parse_svg_length(q + 8, e2 - q - 8);
            }
        }
        if (gt - q >= 9 && memcmp(q, "viewBox=\"", 9) == 0) {
            const char *e2 = memchr(q + 9, '"', (size_t)(gt - q - 9));
            if (e2) {
                /* "minx miny width height" — pick last 2. */
                char vb[64];
                size_t vlen = (size_t)(e2 - q - 9);
                if (vlen < sizeof(vb)) {
                    memcpy(vb, q + 9, vlen);
                    vb[vlen] = 0;
                    float a, b, c, d;
                    if (sscanf(vb, "%f %f %f %f", &a, &b, &c, &d) == 4) {
                        vb_w = c;
                        vb_h = d;
                    }
                }
            }
        }
        q++;
    }
    if (w <= 0) {
        w = vb_w;
    }
    if (h <= 0) {
        h = vb_h;
    }
    if (w > 0 && h > 0) {
        *out_w = (int)w;
        *out_h = (int)h;
        return 1;
    }
    return 0;
}

/* Render a tiny checker-pattern placeholder for SVGs we can't fully
 * rasterize — at least the user sees there's an image there at the
 * right aspect ratio rather than a blank slot. */
static int decode_svg_placeholder(const uint8_t *bytes, size_t len, uint32_t **out_pixels,
                                  int *out_w, int *out_h)
{
    int w = 0, h = 0;
    if (!sniff_svg_dimensions(bytes, len, &w, &h)) {
        w = 24;
        h = 24;
    }
    /* Cap the placeholder to keep memory + GPU upload sensible. */
    if (w > 256) {
        h = (int)((float)h * 256.0f / (float)w);
        w = 256;
    }
    if (h > 256) {
        w = (int)((float)w * 256.0f / (float)h);
        h = 256;
    }
    if (w <= 0) {
        w = 1;
    }
    if (h <= 0) {
        h = 1;
    }
    uint32_t *px = malloc((size_t)w * (size_t)h * 4);
    if (!px) {
        return 0;
    }
    uint32_t a = 0xFFD0D0D0u, b = 0xFFE8E8E8u;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int cx = x / 8, cy = y / 8;
            px[y * w + x] = ((cx ^ cy) & 1) ? a : b;
        }
    }
    *out_pixels = px;
    *out_w = w;
    *out_h = h;
    return 1;
}

/* Identify image format by magic bytes and dispatch to the right
 * decoder. Returns 1 on success. */
static int decode_image(const uint8_t *bytes, size_t len, uint32_t **out_pixels, int *out_w,
                        int *out_h)
{
    if (len >= 8 && bytes[0] == 0x89 && bytes[1] == 'P' && bytes[2] == 'N' && bytes[3] == 'G' &&
        bytes[4] == 0x0D && bytes[5] == 0x0A && bytes[6] == 0x1A && bytes[7] == 0x0A) {
#if YETTY_HAVE_LIBPNG
        if (decode_png(bytes, len, out_pixels, out_w, out_h)) {
            return 1;
        }
#endif
        /* PNG header but libpng absent or failed — fall back
		 * to stb (which tolerates some PNGs libpng rejects
		 * strictly, and is the only PNG path when libpng was
		 * not built in). */
        return decode_other(bytes, len, out_pixels, out_w, out_h);
    }
    if (len >= 3 && bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF) {
#if YETTY_HAVE_TURBOJPEG
        if (decode_jpeg(bytes, len, out_pixels, out_w, out_h)) {
            return 1;
        }
#endif
        return decode_other(bytes, len, out_pixels, out_w, out_h);
    }
    /* SVG / XML payloads — no rasterizer linked, so emit a sized
	 * checker-pattern placeholder. The geometry is sniffed from the
	 * SVG XML so the box ends up at the right aspect ratio. */
    if (len >= 5 && (memcmp(bytes, "<?xml", 5) == 0 || memcmp(bytes, "<svg ", 5) == 0 ||
                     memcmp(bytes, "<svg>", 5) == 0)) {
        return decode_svg_placeholder(bytes, len, out_pixels, out_w, out_h);
    }
    if (len >= 1 && bytes[0] == '<') {
        /* Generic XML/HTML — treat as SVG-ish. */
        return decode_svg_placeholder(bytes, len, out_pixels, out_w, out_h);
    }
    return decode_other(bytes, len, out_pixels, out_w, out_h);
}

/* Base64 decoder for data: URIs. Tolerates whitespace and `=` padding.
 * Returns decoded length, 0 on bad input. `out` must be at least
 * (in_len * 3 / 4 + 4) bytes. */
static size_t b64_decode(const char *in, size_t in_len, uint8_t *out)
{
    static const int8_t T[256] = {
        [0 ... 255] = -1, ['A'] = 0,  ['B'] = 1,  ['C'] = 2,  ['D'] = 3,  ['E'] = 4,  ['F'] = 5,
        ['G'] = 6,        ['H'] = 7,  ['I'] = 8,  ['J'] = 9,  ['K'] = 10, ['L'] = 11, ['M'] = 12,
        ['N'] = 13,       ['O'] = 14, ['P'] = 15, ['Q'] = 16, ['R'] = 17, ['S'] = 18, ['T'] = 19,
        ['U'] = 20,       ['V'] = 21, ['W'] = 22, ['X'] = 23, ['Y'] = 24, ['Z'] = 25, ['a'] = 26,
        ['b'] = 27,       ['c'] = 28, ['d'] = 29, ['e'] = 30, ['f'] = 31, ['g'] = 32, ['h'] = 33,
        ['i'] = 34,       ['j'] = 35, ['k'] = 36, ['l'] = 37, ['m'] = 38, ['n'] = 39, ['o'] = 40,
        ['p'] = 41,       ['q'] = 42, ['r'] = 43, ['s'] = 44, ['t'] = 45, ['u'] = 46, ['v'] = 47,
        ['w'] = 48,       ['x'] = 49, ['y'] = 50, ['z'] = 51, ['0'] = 52, ['1'] = 53, ['2'] = 54,
        ['3'] = 55,       ['4'] = 56, ['5'] = 57, ['6'] = 58, ['7'] = 59, ['8'] = 60, ['9'] = 61,
        ['+'] = 62,       ['/'] = 63, ['-'] = 62, ['_'] = 63, /* URL-safe variant */
    };
    size_t n = 0;
    uint32_t bits = 0;
    int have = 0;
    for (size_t i = 0; i < in_len; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '=') {
            continue;
        }
        int v = T[c];
        if (v < 0) {
            return 0;
        }
        bits = (bits << 6) | (uint32_t)v;
        have += 6;
        if (have >= 8) {
            have -= 8;
            out[n++] = (uint8_t)((bits >> have) & 0xFF);
        }
    }
    return n;
}

/* Decode a `data:[<mime>][;base64],<payload>` URI. Caller frees. NULL
 * on malformed input. Sites use `data:image/png;base64,...` or
 * `data:image/svg+xml;...` heavily for icons / sprites; libcurl rejects
 * these so we have to handle them ourselves. */
static char *data_uri_decode(const char *url, size_t *out_len)
{
    if (strncmp(url, "data:", 5) != 0) {
        return NULL;
    }
    const char *p = url + 5;
    const char *comma = strchr(p, ',');
    if (!comma) {
        return NULL;
    }
    int is_b64 = 0;
    for (const char *s = p; s + 7 <= comma; s++) {
        if (strncmp(s, ";base64", 7) == 0) {
            is_b64 = 1;
            break;
        }
    }
    const char *payload = comma + 1;
    size_t plen = strlen(payload);
    if (is_b64) {
        uint8_t *buf = malloc(plen * 3 / 4 + 4);
        if (!buf) {
            return NULL;
        }
        size_t n = b64_decode(payload, plen, buf);
        if (n == 0) {
            free(buf);
            return NULL;
        }
        *out_len = n;
        return (char *)buf;
    }
    /* URL-encoded text payload (e.g. inline SVG). Pass through —
	 * full %XX decoding isn't worth the complexity for a path
	 * stb/libpng can't decode anyway. */
    char *buf = malloc(plen + 1);
    if (!buf) {
        return NULL;
    }
    memcpy(buf, payload, plen);
    buf[plen] = 0;
    *out_len = plen;
    return buf;
}

/* Look up `url` in the document's image cache. Returns the cache slot
 * (existing or freshly allocated) or NULL on alloc failure. The slot
 * may have failed=1 to indicate a previous fetch/decode error — caller
 * should fall through to the placeholder path in that case.
 * Exposed (non-static) so ylexbor-box.c can pre-decode at box-build
 * time to set the image's natural pixel dimensions on the box. */
struct yetty_ylexbor_img_cache_entry *yetty_ylexbor_img_cache_get_or_load(struct yetty_ylexbor *r,
                                                                          const char *url)
{
    for (int i = 0; i < r->img_cache_count; i++) {
        if (r->img_cache[i].url && strcmp(r->img_cache[i].url, url) == 0) {
            return &r->img_cache[i];
        }
    }
    if (r->img_cache_count == r->img_cache_cap) {
        int nc = r->img_cache_cap ? r->img_cache_cap * 2 : 8;
        struct yetty_ylexbor_img_cache_entry *p = realloc(r->img_cache, (size_t)nc * sizeof(*p));
        if (!p) {
            return NULL;
        }
        r->img_cache = p;
        r->img_cache_cap = nc;
    }
    struct yetty_ylexbor_img_cache_entry *e = &r->img_cache[r->img_cache_count++];
    memset(e, 0, sizeof(*e));
    e->url = strdup(url);
    if (!e->url) {
        r->img_cache_count--;
        return NULL;
    }

    long status = 0;
    size_t blen = 0;
    char *bytes = NULL;
    if (strncmp(url, "data:", 5) == 0) {
        bytes = data_uri_decode(url, &blen);
        status = bytes ? 200 : 0;
    } else {
        /* Pass the document's base URL as Referer — CDNs route
		 * image-fetch authorisation through this. Without it,
		 * gstatic's faviconV2 endpoints return 404, several
		 * image hot-link blockers return 403, and Cloudflare's
		 * "verifying you are human" page returns 503. */
        bytes = yetty_ylexbor_http_get_referer(url, r->base_url, &blen, &status);
    }
    if (!bytes || blen == 0 || (status != 0 && status != 200)) {
        ydebug("img FETCH FAIL status=%ld len=%zu url=%s", status, blen, url);
        free(bytes);
        e->failed = 1;
        return e;
    }

    /* Identify the format from the magic bytes for the log. */
    const char *fmt = "unknown";
    if (blen >= 8 && (uint8_t)bytes[0] == 0x89 && bytes[1] == 'P' && bytes[2] == 'N' &&
        bytes[3] == 'G') {
        fmt = "PNG";
    } else if (blen >= 3 && (uint8_t)bytes[0] == 0xFF && (uint8_t)bytes[1] == 0xD8 &&
               (uint8_t)bytes[2] == 0xFF) {
        fmt = "JPEG";
    } else if (blen >= 6 && bytes[0] == 'G' && bytes[1] == 'I' && bytes[2] == 'F' &&
               bytes[3] == '8') {
        fmt = "GIF";
    } else if (blen >= 12 && bytes[0] == 'R' && bytes[1] == 'I' && bytes[2] == 'F' &&
               bytes[3] == 'F' && bytes[8] == 'W' && bytes[9] == 'E' && bytes[10] == 'B' &&
               bytes[11] == 'P') {
        fmt = "WEBP";
    } else if (blen >= 4 && bytes[0] == '<') {
        fmt = "SVG/HTML";
    } else if (blen >= 5 && memcmp(bytes, "data:", 5) == 0) {
        fmt = "DATA-URI";
    }

    uint32_t *pixels = NULL;
    int w = 0, h = 0;
    int ok = decode_image((const uint8_t *)bytes, blen, &pixels, &w, &h);
    if (!ok) {
        ydebug("img DECODE FAIL fmt=%s len=%zu first8=%02x%02x%02x%02x%02x%02x%02x%02x url=%s", fmt,
               blen, (uint8_t)bytes[0], (uint8_t)bytes[1], (uint8_t)bytes[2], (uint8_t)bytes[3],
               blen >= 5 ? (uint8_t)bytes[4] : 0, blen >= 6 ? (uint8_t)bytes[5] : 0,
               blen >= 7 ? (uint8_t)bytes[6] : 0, blen >= 8 ? (uint8_t)bytes[7] : 0, url);
        free(bytes);
        e->failed = 1;
        return e;
    }
    free(bytes);
    ydebug("img DECODE OK   fmt=%s wh=%dx%d url=%s", fmt, w, h, url);
    e->pixels = pixels;
    e->w = w;
    e->h = h;
    return e;
}

/* Pick the best URL out of an `<img>`'s flock of source-ish attributes.
 *
 * Modern sites rarely put the real image URL in `src`. Common patterns:
 *
 *   <img src="data:image/gif;base64,..1px-placeholder.." data-src="real.png">
 *   <img loading="lazy" data-original="real.png">
 *   <img srcset="img-1x.png 1x, img-2x.png 2x"> with no `src` at all
 *   <picture><source srcset="..."><img src="fallback.png"></picture>
 *
 * We pick, in order:
 *   1. `data-src` / `data-original` / `data-lazy-src` (lazy-load attrs)
 *   2. first URL out of `srcset`
 *   3. plain `src`
 * but only swap when (1)/(2) actually exist; otherwise `src` stays.
 *
 * If `src` is a tiny data: URI (< 200 bytes — almost always a 1×1
 * placeholder) AND there's a non-empty `data-*` candidate, the lazy
 * attr wins regardless of order. */
static char *attr_strdup(lxb_dom_element_t *el, const char *name, size_t namelen)
{
    size_t l = 0;
    const lxb_char_t *v = lxb_dom_element_get_attribute(el, (const lxb_char_t *)name, namelen, &l);
    if (!v || l == 0) {
        return NULL;
    }
    return strndup((const char *)v, l);
}

char *yetty_ylexbor_img_pick_url(struct yetty_ylexbor *r, lxb_dom_element_t *el);

char *yetty_ylexbor_img_pick_url(struct yetty_ylexbor *r, lxb_dom_element_t *el)
{
    if (!el) {
        return NULL;
    }
    char *src = attr_strdup(el, "src", 3);
    char *lazy = attr_strdup(el, "data-src", 8);
    if (!lazy) {
        lazy = attr_strdup(el, "data-original", 13);
    }
    if (!lazy) {
        lazy = attr_strdup(el, "data-lazy-src", 13);
    }

    /* srcset — pick the first candidate URL. Per spec, srcset is a
	 * comma-separated list of `<url> <descriptor>` pairs, and the
	 * URL stops at the first whitespace. Sites embed bare commas
	 * inside the URL itself (Google's gstatic faviconV2 has
	 * `fallback_opts=TYPE,SIZE,URL`), so we MUST stop only at
	 * whitespace, never at `,`. */
    char *srcset_url = NULL;
    {
        size_t l = 0;
        const lxb_char_t *ss =
            lxb_dom_element_get_attribute(el, (const lxb_char_t *)"srcset", 6, &l);
        if (ss && l > 0) {
            const char *p = (const char *)ss;
            const char *end = p + l;
            while (p < end && (*p == ' ' || *p == '\t' || *p == '\n')) {
                p++;
            }
            const char *start = p;
            while (p < end && *p != ' ' && *p != '\t' && *p != '\n') {
                p++;
            }
            if (p > start) {
                srcset_url = strndup(start, p - start);
            }
        }
    }

    const char *pick = NULL;
    if (lazy) {
        pick = lazy;
    } else if (srcset_url) {
        pick = srcset_url;
    } else if (src) {
        pick = src;
    }

    /* If we picked plain `src` but it's a tiny data: URI placeholder,
	 * promote a lazy/srcset URL when available. */
    if (pick == src && src) {
        size_t srclen = strlen(src);
        if (strncmp(src, "data:", 5) == 0 && srclen < 200) {
            if (lazy) {
                pick = lazy;
            } else if (srcset_url) {
                pick = srcset_url;
            }
        }
    }

    char *abs = pick ? yetty_ylexbor_resolve_url(r, pick) : NULL;
    free(src);
    free(lazy);
    free(srcset_url);
    return abs;
}

struct yetty_ycore_void_result yetty_ylexbor_paint(struct yetty_ylexbor *r,
                                                   struct yetty_ypaint_core_buffer *buf)
{
    if (r == NULL || buf == NULL) {
        return YETTY_ERR(yetty_ycore_void, "ylexbor_paint: null");
    }

    uint32_t z = 0;
    /* Track the maximum X/Y extent of every emitted prim so we can
	 * set the buffer's scene bounds at the end. Without this the
	 * scene rectangle is left at (0,0)–(0,0), and the GPU pipeline
	 * culls every prim whose bounds fall outside that degenerate
	 * rectangle — yielding "no image displayed" even though the
	 * prims and their pixel data are present in the wire bytes. */
    float scene_max_x = 0.0f, scene_max_y = 0.0f;

    ydebug("paint total boxes=%u", r->boxes.size);
    for (uint32_t i = 0; i < r->boxes.size; i++) {
        struct yetty_ylexbor_box *b = &r->boxes.data[i];
        /* A box with h=0 but a visible border is normal — that's
		 * how `<hr>` renders (border-top: 1px on a content-less
		 * block). Don't skip it; the border paint below produces
		 * the visible 1px line. Same logic for w=0 vertical
		 * separators (rare in HTML but legal CSS). */
        bool has_visible_border =
            b->kind == YL_BOX_BLOCK && b->border_color.a != 0 &&
            (b->border_top > 0 || b->border_right > 0 || b->border_bottom > 0 ||
             b->border_left > 0);
        if ((b->w <= 0 && !has_visible_border) || (b->h <= 0 && !has_visible_border)) {
            ydebug("paint skip  i=%u kind=%d xy=%.0f,%.0f wh=%.0fx%.0f", i, b->kind, b->x, b->y,
                   b->w, b->h);
            continue;
        }
        /* Grow scene extents to cover this box. We extend even
		 * for transparent / text boxes since they contribute glyph
		 * geometry; the GPU will simply skip empty regions. The
		 * border-only `<hr>` case adds border-top+border-bottom to
		 * the vertical extent so the painted 1px line isn't culled
		 * by the scene-bounds check. */
        float right = b->x + b->w;
        float bottom = b->y + b->h;
        if (has_visible_border) {
            float border_h_extent = b->border_top + b->border_bottom;
            float border_w_extent = b->border_left + b->border_right;
            if (b->y + border_h_extent > bottom) {
                bottom = b->y + border_h_extent;
            }
            if (b->x + border_w_extent > right) {
                right = b->x + border_w_extent;
            }
        }
        if (right > scene_max_x) {
            scene_max_x = right;
        }
        if (bottom > scene_max_y) {
            scene_max_y = bottom;
        }

        switch (b->kind) {
        case YL_BOX_BLOCK: {
            ydebug("paint block i=%u xy=%.0f,%.0f wh=%.0fx%.0f bg=%02x%02x%02x%02x "
                   "bw=%.1f/%.1f/%.1f/%.1f bc=%02x%02x%02x%02x",
                   i, b->x, b->y, b->w, b->h, b->bg.r, b->bg.g, b->bg.b, b->bg.a, b->border_top,
                   b->border_right, b->border_bottom, b->border_left, b->border_color.r,
                   b->border_color.g, b->border_color.b, b->border_color.a);
            /* Background fill (skip if transparent — most blocks). */
            if (b->bg.a != 0) {
                struct yetty_ysdf_box box = {
                    .center_x = b->x + b->w * 0.5f,
                    .center_y = b->y + b->h * 0.5f,
                    .half_width = b->w * 0.5f,
                    .half_height = b->h * 0.5f,
                    .corner_radius = b->border_radius,
                };
                (void)yetty_ysdf_add_box(buf, z++, pack_rgba(b->bg), 0, 0, &box);
            }
            /* Borders — render each present side as a thin ysdf
			 * rect of the border color. ysdf can't draw a
			 * stroked rounded box natively, so for now corner
			 * radius is honored on the bg fill but borders are
			 * straight rectangles. Good enough to read as a
			 * visible "card" outline. */
            if (b->border_color.a != 0 && (b->border_top > 0 || b->border_right > 0 ||
                                           b->border_bottom > 0 || b->border_left > 0)) {
                uint32_t bc = pack_rgba(b->border_color);
                if (b->border_top > 0) {
                    struct yetty_ysdf_box bx = {
                        .center_x = b->x + b->w * 0.5f,
                        .center_y = b->y + b->border_top * 0.5f,
                        .half_width = b->w * 0.5f,
                        .half_height = b->border_top * 0.5f,
                    };
                    (void)yetty_ysdf_add_box(buf, z++, bc, 0, 0, &bx);
                }
                if (b->border_bottom > 0) {
                    struct yetty_ysdf_box bx = {
                        .center_x = b->x + b->w * 0.5f,
                        .center_y = b->y + b->h - b->border_bottom * 0.5f,
                        .half_width = b->w * 0.5f,
                        .half_height = b->border_bottom * 0.5f,
                    };
                    (void)yetty_ysdf_add_box(buf, z++, bc, 0, 0, &bx);
                }
                if (b->border_left > 0) {
                    struct yetty_ysdf_box bx = {
                        .center_x = b->x + b->border_left * 0.5f,
                        .center_y = b->y + b->h * 0.5f,
                        .half_width = b->border_left * 0.5f,
                        .half_height = b->h * 0.5f,
                    };
                    (void)yetty_ysdf_add_box(buf, z++, bc, 0, 0, &bx);
                }
                if (b->border_right > 0) {
                    struct yetty_ysdf_box bx = {
                        .center_x = b->x + b->w - b->border_right * 0.5f,
                        .center_y = b->y + b->h * 0.5f,
                        .half_width = b->border_right * 0.5f,
                        .half_height = b->h * 0.5f,
                    };
                    (void)yetty_ysdf_add_box(buf, z++, bc, 0, 0, &bx);
                }
            }
            break;
        }

        case YL_BOX_INLINE_IMAGE: {
            char *url = yetty_ylexbor_img_pick_url(r, b->element);
            struct yetty_ylexbor_img_cache_entry *cached = NULL;
            if (url) {
                cached = yetty_ylexbor_img_cache_get_or_load(r, url);
            }
            free(url);

            if (!cached || cached->failed || !cached->pixels) {
                /* Fetch/decode failed (or no src) — fall back
				 * to the grey placeholder so at least the
				 * page geometry is preserved. */
                struct yetty_ysdf_box box = {
                    .center_x = b->x + b->w * 0.5f,
                    .center_y = b->y + b->h * 0.5f,
                    .half_width = b->w * 0.5f,
                    .half_height = b->h * 0.5f,
                    .corner_radius = 0,
                };
                (void)yetty_ysdf_add_box(buf, z++, 0xc0c0c0ffu, 0, 0, &box);
                ydebug("paint image (placeholder) i=%u xy=%.0f,%.0f wh=%.0fx%.0f", i, b->x, b->y,
                       b->w, b->h);
                break;
            }

            /* Build a yimage prim sized to the box's allocated
			 * geometry — the decoded pixels carry the source
			 * dimensions, the bounds carry where to draw. The
			 * GPU does the resampling at sample time. */
            struct yetty_yimage_uniforms u = {
                .bounds_x = b->x,
                .bounds_y = b->y,
                .bounds_w = b->w > 0 ? b->w : (float)cached->w,
                .bounds_h = b->h > 0 ? b->h : (float)cached->h,
                .image_w = (uint32_t)cached->w,
                .image_h = (uint32_t)cached->h,
            };
            struct yetty_yimage_buffers bufs = {
                .pixels = cached->pixels,
                .pixels_len = (size_t)cached->w * (size_t)cached->h,
            };
            size_t need = yetty_yimage_uniforms_serialized_size(&u, &bufs);
            uint8_t *prim = malloc(need);
            if (!prim) {
                break;
            }
            struct yetty_ycore_size_result ser =
                yetty_yimage_uniforms_serialize(&u, &bufs, prim, need);
            if (YETTY_IS_ERR(ser)) {
                free(prim);
                break;
            }
            (void)yetty_ypaint_core_buffer_add_prim(buf, prim, need);
            free(prim);
            z++;
            ydebug("paint image i=%u xy=%.0f,%.0f wh=%.0fx%.0f src=%dx%d", i, b->x, b->y, b->w,
                   b->h, cached->w, cached->h);
            break;
        }

        case YL_BOX_INLINE_TEXT: {
            if (b->text_len) {
                int n = b->text_len > 40 ? 40 : (int)b->text_len;
                ydebug("paint text  i=%u xy=%.0f,%.0f wh=%.0fx%.0f fg=%02x%02x%02x%02x w=%d%s%s "
                       "\"%.*s\"",
                       i, b->x, b->y, b->w, b->h, b->fg.r, b->fg.g, b->fg.b, b->fg.a,
                       b->font_weight, b->font_italic ? " i" : "", b->underline ? " u" : "", n,
                       b->text);
            }
            if (b->text == NULL || b->text_len == 0) {
                break;
            }
            struct yetty_ycore_buffer txt = {
                .data = (uint8_t *)b->text,
                .capacity = b->text_len,
                .size = b->text_len,
            };
            /* Baseline approximation: top + 0.8 * line height.
			 * Real metric needs FreeType ascent. */
            float baseline_y = b->y + b->font_size * 0.8f;
            (void)yetty_ypaint_core_buffer_add_text(buf, b->x, baseline_y, &txt, b->font_size,
                                                    pack_rgba(b->fg), z++, /*font_id=*/-1,
                                                    /*rotation=*/0.0f);
            /* Synthetic bold — we only have one font, so to make
			 * <strong>/<b>/font-weight:bold visibly thicker we
			 * draw the same run again offset by ~1px. Crude but
			 * it actually reads as bold on the screen. Drop when
			 * a real bold font is wired through font_id. */
            if (b->font_weight >= 600) {
                float ox = b->font_size * 0.05f;
                if (ox < 1.0f) {
                    ox = 1.0f;
                }
                (void)yetty_ypaint_core_buffer_add_text(buf, b->x + ox, baseline_y, &txt,
                                                        b->font_size, pack_rgba(b->fg), z++,
                                                        /*font_id=*/-1, /*rotation=*/0.0f);
            }
            if (b->underline && b->w > 0) {
                /* Thin SDF rect ~1px below baseline, in the run's
				 * foreground color. Matches the visual the user
				 * expects for an <a>. Thickness scales lightly
				 * with font size (max(1, font*0.06)) so it remains
				 * visible at large headings without overpowering
				 * normal text. */
                float thickness = b->font_size * 0.06f;
                if (thickness < 1.0f) {
                    thickness = 1.0f;
                }
                float underline_y = baseline_y + b->font_size * 0.12f;
                struct yetty_ysdf_box ubx = {
                    .center_x = b->x + b->w * 0.5f,
                    .center_y = underline_y + thickness * 0.5f,
                    .half_width = b->w * 0.5f,
                    .half_height = thickness * 0.5f,
                };
                (void)yetty_ysdf_add_box(buf, z++, pack_rgba(b->fg), 0, 0, &ubx);
            }
            break;
        }
        }
    }

    /* Publish the scene rectangle. Use viewport_w as a floor for X so
	 * a page with no full-width background still produces a sensible
	 * scene width matching the requested viewport. */
    float min_w = (float)r->viewport_w;
    if (scene_max_x < min_w) {
        scene_max_x = min_w;
    }
    yetty_ypaint_core_buffer_set_scene_bounds(buf, 0.0f, 0.0f, scene_max_x, scene_max_y);
    ydebug("paint scene bounds = (0,0)-(%.0f,%.0f)", scene_max_x, scene_max_y);

    return YETTY_OK_VOID();
}
