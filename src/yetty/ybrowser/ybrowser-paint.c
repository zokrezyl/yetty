/*
 * ylexbor-paint — emit ydraw primitives from a laid-out box vector.
 *
 * Every YL_BOX_BLOCK with non-zero alpha background → ysdf box.
 * Every YL_BOX_INLINE_TEXT → TEXT_DRAWABLE_LIST drawable-list entry prim.
 * Every YL_BOX_INLINE_IMAGE → yimage prim with decoded pixels (or grey
 *                             placeholder when the fetch/decode fails).
 *
 * Image decoding: per-document cache keyed by the resolved absolute
 * URL of `<img src>`. Cache hits skip the fetch + decode. The same
 * decoded pixel buffer is re-serialized into a fresh yimage prim each
 * paint call, since ydraw takes ownership of the prim bytes via
 * add_prim — we keep the cache copy alive for repaints.
 *
 * Color packing matches what ydraw's shader expects: low byte = R,
 * high byte = A. Same convention ynetsurf-plotters.c uses.
 */

#include "ybrowser-internal.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#if YETTY_HAVE_LIBPNG
#include <png.h>
#endif
#include <stb_image.h>
#if YETTY_HAVE_TURBOJPEG
#include <turbojpeg.h>
#endif
#if YETTY_HAVE_LIBWEBP
#include <webp/decode.h>
#endif

#include <yetty/ycore/util.h>
#include <yetty/yplatform/yworkpool.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/ysdf/types.gen.h>
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/ysdf/merge.h>
#include <yetty/ysvg/ysvg.h>
#include <yetty/yimage/yimage-gen.h>
#include <yetty/ytrace/ytrace.h>

#include <lexbor/html/serialize.h>

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

/* SVG — real vector rendering through ysvg. A source renders ONCE into a
 * drawable list; paint MERGES that scene into the page's output under the
 * image box's transform (yetty_ydraw_drawable_list_merge_transformed), so
 * the primitives stay vectors end to end — no rasterization, no checker
 * placeholders. */
static int bytes_look_like_svg(const uint8_t *bytes, size_t len)
{
    /* The document element must be <svg> — skip whitespace, comments and
	 * the <!DOCTYPE>/<?xml?> prologue, then check the first real tag. */
    size_t scan = len < 1024 ? len : 1024;
    size_t i = 0;
    while (i < scan) {
        while (i < scan && (bytes[i] == ' ' || bytes[i] == '\t' || bytes[i] == '\r' ||
                            bytes[i] == '\n' || bytes[i] == '\f')) {
            i++;
        }
        if (i >= scan || bytes[i] != '<') {
            return 0;
        }
        if (i + 4 <= scan && memcmp(bytes + i, "<!--", 4) == 0) {
            const uint8_t *close = NULL;
            for (size_t j = i + 4; j + 3 <= scan; j++) {
                if (memcmp(bytes + j, "-->", 3) == 0) {
                    close = bytes + j;
                    break;
                }
            }
            if (!close) {
                return 0;
            }
            i = (size_t)(close - bytes) + 3;
            continue;
        }
        if (i + 1 < scan && (bytes[i + 1] == '!' || bytes[i + 1] == '?')) {
            size_t j = i + 2;
            while (j < scan && bytes[j] != '>') {
                j++;
            }
            if (j >= scan) {
                return 0;
            }
            i = j + 1;
            continue;
        }
        return i + 4 < scan && (bytes[i + 1] | 0x20) == 's' && (bytes[i + 2] | 0x20) == 'v' &&
               (bytes[i + 3] | 0x20) == 'g' &&
               (bytes[i + 4] == '>' || bytes[i + 4] == ' ' || bytes[i + 4] == '\t' ||
                bytes[i + 4] == '\r' || bytes[i + 4] == '\n' || bytes[i + 4] == '/');
    }
    return 0;
}

void yetty_ylexbor_svg_parse_preserve_aspect(const char *bytes, size_t len, float *out_align_x,
                                             float *out_align_y, uint8_t *out_mode)
{
    *out_align_x = 0.5f;
    *out_align_y = 0.5f;
    *out_mode = 0; /* meet */
    if (!bytes || len == 0) {
        return;
    }
    /* Scope the scan to the root <svg ...> start tag. */
    const char *root = NULL;
    for (size_t i = 0; i + 4 < len; i++) {
        if (bytes[i] == '<' && (bytes[i + 1] | 0x20) == 's' && (bytes[i + 2] | 0x20) == 'v' &&
            (bytes[i + 3] | 0x20) == 'g') {
            root = bytes + i;
            break;
        }
    }
    if (!root) {
        return;
    }
    const char *tag_end = memchr(root, '>', (size_t)(bytes + len - root));
    if (!tag_end) {
        return;
    }
    static const char attr_name[] = "preserveAspectRatio";
    const char *attr = NULL;
    for (const char *scan = root; scan + sizeof(attr_name) - 1 < tag_end; scan++) {
        if (strncmp(scan, attr_name, sizeof(attr_name) - 1) == 0) {
            attr = scan + sizeof(attr_name) - 1;
            break;
        }
    }
    if (!attr) {
        return;
    }
    while (attr < tag_end && (*attr == ' ' || *attr == '=' || *attr == '"' || *attr == '\'')) {
        attr++;
    }
    /* Optional "defer" prefix (image-only, ignored). */
    if (strncmp(attr, "defer", 5) == 0) {
        attr += 5;
        while (attr < tag_end && *attr == ' ') {
            attr++;
        }
    }
    if (strncmp(attr, "none", 4) == 0) {
        *out_mode = 2;
        return;
    }
    /* x(Min|Mid|Max)Y(Min|Mid|Max) — the discriminating letter is the
	 * second of each keyword: i / i / a for Min / Mid / Max is ambiguous,
	 * so use the THIRD letter: n / d / x. */
    if ((*attr | 0x20) == 'x' && attr + 8 < tag_end) {
        char x_third = (char)(attr[3] | 0x20);
        *out_align_x = x_third == 'n' ? 0.0f : (x_third == 'x' ? 1.0f : 0.5f);
        char y_third = (char)(attr[7] | 0x20);
        *out_align_y = y_third == 'n' ? 0.0f : (y_third == 'x' ? 1.0f : 0.5f);
        attr += 8;
    }
    while (attr < tag_end && (*attr == ' ' || *attr == '"' || *attr == '\'')) {
        attr++;
    }
    if (attr + 5 <= tag_end && strncmp(attr, "slice", 5) == 0) {
        *out_mode = 1;
    }
}

/* ysvg <image> resolver: href resolved against the document base, bytes
 * through the shared loader (cache + cancellation), pixels from the
 * per-document image cache — borrowed, valid for the render call. */
YETTY_EXTERNAL_CALLBACK
static int svg_image_resolver(void *userdata, const char *href, const uint32_t **out_pixels,
                              int *out_width, int *out_height)
{
    struct yetty_ylexbor *r = userdata;
    if (!r || !href) {
        return 0;
    }
    char *absolute = yetty_ylexbor_resolve_url(r, href);
    if (!absolute) {
        return 0;
    }
    struct yetty_ylexbor_img_cache_entry *cached = yetty_ylexbor_img_cache_get_or_load(r, absolute);
    free(absolute);
    if (!cached || cached->failed || !cached->pixels) {
        return 0;
    }
    *out_pixels = cached->pixels;
    *out_width = cached->w;
    *out_height = cached->h;
    return 1;
}

int yetty_ylexbor_svg_scene_render(struct yetty_ylexbor *r, const char *bytes, size_t len,
                                   float default_font_px,
                                   struct yetty_ydraw_drawable_list **out_scene, float *out_min_x,
                                   float *out_min_y, float *out_w, float *out_h,
                                   struct yetty_ysvg_link_region **out_links,
                                   size_t *out_link_count)
{
    if (out_links) {
        *out_links = NULL;
    }
    if (out_link_count) {
        *out_link_count = 0;
    }
    /* The cell size only seeds ysvg's default font-size fallback — the CSS
	 * initial font-size (16px) is the right analogue in a page context. */
    struct yetty_ysvg_render_config config = {
        .cell_width = 8,
        .cell_height = default_font_px > 0.0f ? default_font_px : 16,
        .width_cells = 32,
        .height_cells = 8,
    };
    if (r) {
        /* <image href> content flows through the document's base URL,
		 * loader and image cache. */
        config.image_resolver.resolve = svg_image_resolver;
        config.image_resolver.userdata = r;
    }
    config.collect_link_regions = out_links != NULL;
    struct yetty_ysvg_render_result render_res = yetty_ysvg_render(bytes, len, NULL, 0, &config);
    if (YETTY_IS_ERR(render_res)) {
        yetty_ycore_error_destroy(render_res.error);
        return 0;
    }
    struct yetty_ydraw_drawable_list *scene = render_res.value.buffer;
    if (!scene) {
        yetty_ysvg_links_free(render_res.value.links, render_res.value.link_count);
        return 0;
    }
    float min_x = yetty_ydraw_drawable_list_scene_min_x(scene);
    float min_y = yetty_ydraw_drawable_list_scene_min_y(scene);
    float scene_w = render_res.value.scene_width;
    float scene_h = render_res.value.scene_height;
    if (scene_w <= 0.0f) {
        scene_w = yetty_ydraw_drawable_list_scene_max_x(scene) - min_x;
    }
    if (scene_h <= 0.0f) {
        scene_h = yetty_ydraw_drawable_list_scene_max_y(scene) - min_y;
    }
    if (scene_w <= 0.0f || scene_h <= 0.0f) {
        yetty_ysvg_links_free(render_res.value.links, render_res.value.link_count);
        yetty_ydraw_drawable_list_destroy(scene);
        return 0;
    }
    *out_scene = scene;
    *out_min_x = min_x;
    *out_min_y = min_y;
    *out_w = scene_w;
    *out_h = scene_h;
    if (out_links) {
        *out_links = render_res.value.links;
        *out_link_count = render_res.value.link_count;
    } else {
        yetty_ysvg_links_free(render_res.value.links, render_res.value.link_count);
    }
    return 1;
}

/* When `bytes` is an SVG document, render it into the cache entry's vector
 * scene (w/h become the scene extent so layout keeps the aspect ratio) and
 * return 1 — the caller then skips the raster decode. Returns 0 for
 * rasters. A source that IS SVG but fails to render marks the entry failed
 * (grey placeholder at paint) and still returns 1. */
static int img_cache_entry_fill_svg(struct yetty_ylexbor *r,
                                    struct yetty_ylexbor_img_cache_entry *entry, const char *bytes,
                                    size_t len)
{
    if (!bytes || len == 0 || !bytes_look_like_svg((const uint8_t *)bytes, len)) {
        return 0;
    }
    yetty_ylexbor_svg_parse_preserve_aspect(bytes, len, &entry->svg_par_align_x,
                                            &entry->svg_par_align_y, &entry->svg_par_mode);
    if (!yetty_ylexbor_svg_scene_render(r, bytes, len, 0.0f, &entry->svg_scene, &entry->svg_min_x,
                                        &entry->svg_min_y, &entry->svg_w, &entry->svg_h, NULL,
                                        NULL)) {
        entry->failed = 1;
        return 1;
    }
    entry->w = (int)(entry->svg_w + 0.5f);
    entry->h = (int)(entry->svg_h + 0.5f);
    if (entry->w <= 0) {
        entry->w = 1;
    }
    if (entry->h <= 0) {
        entry->h = 1;
    }
    return 1;
}

#if YETTY_HAVE_LIBWEBP
/* Decode a WebP (lossy VP8 / lossless VP8L) byte stream into RGBA8 via
 * libwebp. Allocates *out_pixels (caller frees with free()). Returns 1 on
 * success, 0 on failure. WebP is now the default image format across the
 * modern web — Google News serves every article thumbnail as WebP and
 * ignores the Accept header, so without this path those images can only
 * render as placeholders. */
static int decode_webp(const uint8_t *bytes, size_t len, uint32_t **out_pixels, int *out_w,
                       int *out_h)
{
    int width = 0, height = 0;
    if (!WebPGetInfo(bytes, len, &width, &height) || width <= 0 || height <= 0) {
        return 0;
    }
    /* libwebp writes bytes in R,G,B,A memory order — identical to the
	 * libpng/turbojpeg paths above, so the packed uint32 layout matches. */
    uint8_t *rgba = WebPDecodeRGBA(bytes, len, &width, &height);
    if (!rgba) {
        return 0;
    }
    size_t pixel_count = (size_t)width * (size_t)height;
    uint32_t *pixels = malloc(pixel_count * sizeof(uint32_t));
    if (!pixels) {
        WebPFree(rgba);
        return 0;
    }
    memcpy(pixels, rgba, pixel_count * sizeof(uint32_t));
    WebPFree(rgba);
    *out_pixels = pixels;
    *out_w = width;
    *out_h = height;
    return 1;
}
#endif

/* Identify image format by magic bytes and dispatch to the right
 * decoder. Returns 1 on success. */
static int decode_image(const uint8_t *bytes, size_t len, uint32_t **out_pixels, int *out_w,
                        int *out_h)
{
    /* WebP: "RIFF"<u32 size>"WEBP". Check before the generic stb fallback,
	 * which cannot decode it. */
    if (len >= 12 && memcmp(bytes, "RIFF", 4) == 0 && memcmp(bytes + 8, "WEBP", 4) == 0) {
#if YETTY_HAVE_LIBWEBP
        if (decode_webp(bytes, len, out_pixels, out_w, out_h)) {
            return 1;
        }
#endif
        return decode_other(bytes, len, out_pixels, out_w, out_h);
    }
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
    /* Markup payloads never raster-decode: SVG sources take the vector
	 * path (img_cache_entry_fill_svg, checked by every cache-fill site
	 * BEFORE this decoder), so reaching here with markup means an HTML
	 * error page or unrenderable XML — a failed image. */
    if (len >= 1 && bytes[0] == '<') {
        return 0;
    }
    return decode_other(bytes, len, out_pixels, out_w, out_h);
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
        size_t cap = plen * 3 / 4 + 4;
        uint8_t *buf = malloc(cap);
        if (!buf) {
            return NULL;
        }
        size_t n = yetty_ycore_base64_decode(payload, plen, (char *)buf, cap);
        if (n == 0) {
            free(buf);
            return NULL;
        }
        *out_len = n;
        return (char *)buf;
    }
    /* Percent-encoded text payload — the standard way to inline SVG
	 * (`data:image/svg+xml,%3Csvg...`). Decode %XX so the downstream
	 * SVG sniff sees real `<svg` markup; every other byte (including
	 * `+`, which is literal in data: URIs) passes through unchanged. */
    char *buf = malloc(plen + 1);
    if (!buf) {
        return NULL;
    }
    size_t out = 0;
    for (size_t i = 0; i < plen;) {
        if (payload[i] == '%' && i + 2 < plen + 1 && isxdigit((unsigned char)payload[i + 1]) &&
            isxdigit((unsigned char)payload[i + 2])) {
            char hex[3] = {payload[i + 1], payload[i + 2], 0};
            buf[out++] = (char)strtol(hex, NULL, 16);
            i += 3;
        } else {
            buf[out++] = payload[i++];
        }
    }
    buf[out] = 0;
    *out_len = out;
    return buf;
}

/* 2xx acceptance — one rule for every image path (a 203 or 206 image is
 * as decodable as a 200; anything else is a failure). */
static int image_response_ok(const struct yetty_ybrowser_response *response)
{
    return response->body != NULL && response->body_len > 0 && response->status >= 200 &&
           response->status < 300;
}

/* The image decoration stage: decode a fetched response's bytes and
 * attach the pixels to the response object. The render side consumes
 * the decorated response (pixels move into the per-document cache).
 * A response that already carries pixels (loader cache hit) skips the
 * decode; a fresh decode is published back so the NEXT document skips
 * it too. */
static int decorate_image_response(struct yetty_ybrowser_loader *loader, const char *url,
                                   struct yetty_ybrowser_response *response)
{
    if (response->image_pixels) {
        return 1; /* decoration arrived with the cache hit */
    }
    uint32_t *pixels = NULL;
    int width = 0, height = 0;
    if (!decode_image((const uint8_t *)response->body, response->body_len, &pixels, &width,
                      &height)) {
        return 0;
    }
    response->image_pixels = pixels;
    response->image_width = width;
    response->image_height = height;
    yetty_ybrowser_loader_cache_put_pixels(loader, url, pixels, width, height);
    return 1;
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
    /* Defer mode: never hit the network from the paint thread. Leave the
     * URL pending (no cache entry) so the host can load it later via
     * yetty_ylexbor_fetch_one_pending_image. `data:` URIs carry their
     * bytes inline, so decode those regardless. */
    if (r->defer_image_fetch && strncmp(url, "data:", 5) != 0) {
        return NULL;
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

    struct yetty_ybrowser_response response = {0};
    if (strncmp(url, "data:", 5) == 0) {
        response.body = data_uri_decode(url, &response.body_len);
        response.status = response.body ? 200 : 0;
    } else {
        /* Pass the document's base URL as Referer — CDNs route
		 * image-fetch authorisation through this. Without it,
		 * gstatic's faviconV2 endpoints return 404, several
		 * image hot-link blockers return 403, and Cloudflare's
		 * "verifying you are human" page returns 503. */
        struct yetty_ybrowser_request request = {
            .url = url,
            .kind = YETTY_YBROWSER_REQUEST_IMAGE,
            .referer = r->base_url,
        };
        struct yetty_ycore_void_result fetch_res =
            yetty_ybrowser_fetch(r->loader, &request, &response);
        if (YETTY_IS_ERR(fetch_res)) {
            yetty_ycore_error_destroy(fetch_res.error);
        }
    }
    if (!image_response_ok(&response)) {
        ydebug("img FETCH FAIL status=%ld len=%zu url=%s", response.status, response.body_len, url);
        yetty_ybrowser_response_dispose(&response);
        e->failed = 1;
        return e;
    }
    const char *bytes = response.body;
    size_t blen = response.body_len;

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
    (void)fmt; /* consumed only by the ydebug() calls below */

    /* Vector source? Render through ysvg and keep the drawable list —
	 * paint merges it under the box transform, no rasterization. */
    if (img_cache_entry_fill_svg(r, e, response.body, response.body_len)) {
        ydebug("img SVG %s wh=%dx%d url=%s", e->failed ? "FAIL" : "OK", e->w, e->h, url);
        yetty_ybrowser_response_dispose(&response);
        return e;
    }

    if (!decorate_image_response(r->loader, url, &response)) {
        ydebug("img DECODE FAIL fmt=%s len=%zu first8=%02x%02x%02x%02x%02x%02x%02x%02x url=%s", fmt,
               blen, (uint8_t)bytes[0], (uint8_t)bytes[1], (uint8_t)bytes[2], (uint8_t)bytes[3],
               blen >= 5 ? (uint8_t)bytes[4] : 0, blen >= 6 ? (uint8_t)bytes[5] : 0,
               blen >= 7 ? (uint8_t)bytes[6] : 0, blen >= 8 ? (uint8_t)bytes[7] : 0, url);
        yetty_ybrowser_response_dispose(&response);
        e->failed = 1;
        return e;
    }
    ydebug("img DECODE OK   fmt=%s wh=%dx%d url=%s", fmt, response.image_width,
           response.image_height, url);
    e->pixels = response.image_pixels;
    e->w = response.image_width;
    e->h = response.image_height;
    response.image_pixels = NULL; /* ownership moved into the cache */
    yetty_ybrowser_response_dispose(&response);
    return e;
}

void yetty_ylexbor_set_defer_image_fetch(struct yetty_ylexbor *r, int on)
{
    if (r) {
        r->defer_image_fetch = on ? 1 : 0;
    }
}

/* Fetch up to `max_count` pending <img> URLs in ONE parallel batch and
 * fold them into the cache. Returns how many URLs were processed. The
 * in-yetty client (no worker pool) calls this once per pump — batching
 * turns its former one-blocking-RTT-per-frame trickle into a handful of
 * multiplexed transfers per frame. */
int yetty_ylexbor_fetch_pending_images(struct yetty_ylexbor *r, int max_count)
{
    if (r == NULL || max_count <= 0) {
        return 0;
    }
    char **urls = calloc((size_t)max_count, sizeof(*urls));
    struct yetty_ybrowser_request *fetch_requests =
        calloc((size_t)max_count, sizeof(*fetch_requests));
    struct yetty_ybrowser_response *fetch_responses =
        calloc((size_t)max_count, sizeof(*fetch_responses));
    if (!urls || !fetch_requests || !fetch_responses) {
        free(urls);
        free(fetch_requests);
        free(fetch_responses);
        return 0;
    }
    int pending = 0;
    for (uint32_t i = 0; i < r->boxes.size && pending < max_count; i++) {
        struct yetty_ylexbor_box *b = &r->boxes.data[i];
        if (b->kind != YL_BOX_INLINE_IMAGE || b->element == NULL) {
            continue;
        }
        char *url = yetty_ylexbor_img_pick_url(r, b->element);
        if (url == NULL) {
            continue;
        }
        if (strncmp(url, "data:", 5) == 0) {
            free(url); /* inline bytes — no network, handled during paint */
            continue;
        }
        int already = 0;
        for (int k = 0; !already && k < r->img_cache_count; k++) {
            if (r->img_cache[k].url && strcmp(r->img_cache[k].url, url) == 0) {
                already = 1;
            }
        }
        for (int k = 0; !already && k < pending; k++) {
            if (strcmp(urls[k], url) == 0) {
                already = 1;
            }
        }
        if (already) {
            free(url);
            continue;
        }
        urls[pending] = url;
        fetch_requests[pending].url = url;
        fetch_requests[pending].kind = YETTY_YBROWSER_REQUEST_IMAGE;
        fetch_requests[pending].referer = r->base_url;
        pending++;
    }
    if (pending > 0) {
        struct yetty_ycore_void_result many_res = yetty_ybrowser_fetch_many(
            r->loader, fetch_requests, pending, fetch_responses, /*host_connection_cap=*/4);
        if (YETTY_IS_ERR(many_res)) {
            yetty_ycore_error_destroy(many_res.error);
        }
        for (int k = 0; k < pending; k++) {
            struct yetty_ybrowser_response *response = &fetch_responses[k];
            if (r->img_cache_count == r->img_cache_cap) {
                int new_cap = r->img_cache_cap ? r->img_cache_cap * 2 : 8;
                struct yetty_ylexbor_img_cache_entry *grown =
                    realloc(r->img_cache, (size_t)new_cap * sizeof(*grown));
                if (!grown) {
                    yetty_ybrowser_response_dispose(response);
                    continue;
                }
                r->img_cache = grown;
                r->img_cache_cap = new_cap;
            }
            struct yetty_ylexbor_img_cache_entry *e = &r->img_cache[r->img_cache_count++];
            memset(e, 0, sizeof(*e));
            e->url = urls[k]; /* transfer ownership */
            urls[k] = NULL;
            if (!image_response_ok(response)) {
                yetty_ybrowser_response_dispose(response);
                e->failed = 1;
                continue;
            }
            if (img_cache_entry_fill_svg(r, e, response->body, response->body_len)) {
                yetty_ybrowser_response_dispose(response);
                continue;
            }
            if (!decorate_image_response(r->loader, e->url, response)) {
                yetty_ybrowser_response_dispose(response);
                e->failed = 1;
                continue;
            }
            e->pixels = response->image_pixels;
            e->w = response->image_width;
            e->h = response->image_height;
            response->image_pixels = NULL; /* moved into the cache */
            yetty_ybrowser_response_dispose(response);
        }
    }
    for (int k = 0; k < pending; k++) {
        free(urls[k]);
    }
    free(urls);
    free(fetch_requests);
    free(fetch_responses);
    return pending;
}

int yetty_ylexbor_fetch_one_pending_image(struct yetty_ylexbor *r)
{
    return yetty_ylexbor_fetch_pending_images(r, 1);
}

/* ===========================================================================
 * Async parallel image fetching (worker pool). Each pending <img> becomes a
 * job: fetch+decode on a worker thread (the pool runs several at once, sharing
 * libcurl's HTTP/2 connection pool), then fold the pixels into the cache on the
 * loop thread and notify the host to repaint. Nothing blocks the UI thread.
 * ===========================================================================*/

struct ylexbor_img_job {
    struct yetty_ylexbor *r;
    uint64_t generation;                  /* engine fetch_generation at submit; stale if changed */
    struct yetty_ybrowser_loader *loader; /* borrowed from the engine; the engine
                                           * outlives every job (destroy defers) */
    const _Atomic uint64_t *cancel_generation; /* &engine->fetch_generation — valid for
                                                * the job's whole life (teardown defers) */
    char *url;                                 /* owned */
    char *base_url;                            /* owned copy — read on the worker thread */
    /* Filled by run() on the worker thread: */
    uint32_t *pixels;
    int w, h;
    int failed;
    /* SVG sources: the worker carries the raw bytes back instead of
	 * decoding — the ysvg render happens in done() on the LOOP thread
	 * (the css/lwc internals under ysvg are not audited for concurrent
	 * worker use). */
    char *svg_source; /* owned */
    size_t svg_source_len;
};

/* WORKER THREAD. Touches ONLY the job's own copies — never the engine, which
 * may be torn down (deferred) before done() runs. */
static void img_job_run(void *ctx)
{
    struct ylexbor_img_job *job = ctx;
    /* cancel_generation points into the engine, but reading it here is
	 * safe: the engine defers its teardown until every job has drained,
	 * so the pointer outlives the job. A navigation bumps the counter
	 * and this transfer aborts at its next progress tick or chunk. */
    struct yetty_ybrowser_request request = {
        .url = job->url,
        .kind = YETTY_YBROWSER_REQUEST_IMAGE,
        .referer = job->base_url,
        .generation = job->generation,
        .cancel_generation = job->cancel_generation,
    };
    struct yetty_ybrowser_response response = {0};
    struct yetty_ycore_void_result fetch_res =
        yetty_ybrowser_fetch(job->loader, &request, &response);
    if (YETTY_IS_ERR(fetch_res)) {
        yetty_ycore_error_destroy(fetch_res.error);
    }
    if (!image_response_ok(&response)) {
        yetty_ybrowser_response_dispose(&response);
        job->failed = 1;
        return;
    }
    if (response.body && response.body_len > 0 &&
        bytes_look_like_svg((const uint8_t *)response.body, response.body_len)) {
        job->svg_source = response.body;
        job->svg_source_len = response.body_len;
        response.body = NULL; /* ownership moved to the job */
        yetty_ybrowser_response_dispose(&response);
        return;
    }
    if (!decorate_image_response(job->loader, job->url, &response)) {
        yetty_ybrowser_response_dispose(&response);
        job->failed = 1;
        return;
    }
    job->pixels = response.image_pixels;
    job->w = response.image_width;
    job->h = response.image_height;
    response.image_pixels = NULL; /* ownership moved to the job */
    yetty_ybrowser_response_dispose(&response);
}

/* LOOP THREAD. Fold the result into the cache (if still valid) and notify the
 * host; handle deferred engine teardown. Signature is dictated by the work
 * pool's `done` callback (void (*)(void *)), so any inner Result has nowhere
 * to propagate to — absorb at this boundary. */
YETTY_EXTERNAL_CALLBACK
static void img_job_done(void *ctx)
{
    struct ylexbor_img_job *job = ctx;
    struct yetty_ylexbor *r = job->r;
    r->img_jobs_in_flight--;

    int stale = (job->generation != r->fetch_generation) || r->destroy_pending;
    if (!stale) {
        for (int i = 0; i < r->img_cache_count; i++) {
            if (r->img_cache[i].url && strcmp(r->img_cache[i].url, job->url) == 0) {
                struct yetty_ylexbor_img_cache_entry *e = &r->img_cache[i];
                e->loading = 0;
                if (job->svg_source) {
                    /* Loop thread — safe to run ysvg here. */
                    (void)img_cache_entry_fill_svg(r, e, job->svg_source, job->svg_source_len);
                } else if (job->failed || job->pixels == NULL) {
                    e->failed = 1;
                } else {
                    e->pixels = job->pixels;
                    e->w = job->w;
                    e->h = job->h;
                    job->pixels = NULL; /* ownership transferred to the cache */
                }
                break;
            }
        }
        if (r->on_resource_ready) {
            r->on_resource_ready(r->resource_ready_user);
        }
    }
    free(job->pixels); /* non-NULL only when stale / url vanished */
    free(job->svg_source);
    free(job->url);
    free(job->base_url);
    free(job);

    if (r->img_jobs_in_flight == 0 && !stale) {
        yetty_ylexbor_prof("    img fetch: all done (last image landed)");
    }

    if (r->destroy_pending && r->img_jobs_in_flight == 0) {
        struct yetty_ycore_void_result destroy_res = _yetty_ylexbor_destroy_now(r);
        if (YETTY_IS_ERR(destroy_res)) {
            ydebug("img_job_done: deferred destroy failed: %s", destroy_res.error.msg);
            yetty_ycore_error_destroy(destroy_res.error);
        }
    }
}

void yetty_ylexbor_set_async_image_fetch(struct yetty_ylexbor *r,
                                         struct yetty_yplatform_yworkpool *pool,
                                         void (*on_ready)(void *user), void *user)
{
    if (r == NULL) {
        return;
    }
    r->img_pool = pool;
    r->on_resource_ready = on_ready;
    r->resource_ready_user = user;
}

int yetty_ylexbor_images_in_flight(const struct yetty_ylexbor *r)
{
    return r ? r->img_jobs_in_flight : 0;
}

struct yetty_ycore_int_result yetty_ylexbor_start_image_fetch(struct yetty_ylexbor *r)
{
    if (r == NULL || r->img_pool == NULL) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    int submitted = 0;
    for (uint32_t i = 0; i < r->boxes.size; i++) {
        struct yetty_ylexbor_box *b = &r->boxes.data[i];
        if (b->kind != YL_BOX_INLINE_IMAGE || b->element == NULL) {
            continue;
        }
        char *url = yetty_ylexbor_img_pick_url(r, b->element);
        if (url == NULL) {
            continue;
        }
        if (strncmp(url, "data:", 5) == 0) {
            free(url); /* inline bytes, decoded during paint */
            continue;
        }
        int seen = 0;
        for (int k = 0; k < r->img_cache_count; k++) {
            if (r->img_cache[k].url && strcmp(r->img_cache[k].url, url) == 0) {
                seen = 1; /* already cached or a job is in flight */
                break;
            }
        }
        if (seen) {
            free(url);
            continue;
        }
        /* Reserve a `loading` cache slot so paint draws a placeholder and a
         * later start_image_fetch doesn't resubmit this url. */
        if (r->img_cache_count == r->img_cache_cap) {
            int nc = r->img_cache_cap ? r->img_cache_cap * 2 : 8;
            struct yetty_ylexbor_img_cache_entry *p =
                realloc(r->img_cache, (size_t)nc * sizeof(*p));
            if (!p) {
                free(url);
                break;
            }
            r->img_cache = p;
            r->img_cache_cap = nc;
        }
        struct yetty_ylexbor_img_cache_entry *e = &r->img_cache[r->img_cache_count];
        memset(e, 0, sizeof(*e));
        e->url = strdup(url);
        if (e->url == NULL) {
            free(url);
            break;
        }
        e->loading = 1;
        r->img_cache_count++;

        struct ylexbor_img_job *job = calloc(1, sizeof(*job));
        if (job == NULL) {
            free(url);
            break;
        }
        job->r = r;
        job->generation = r->fetch_generation;
        job->cancel_generation = &r->fetch_generation;
        job->loader = r->loader;
        job->url = url; /* ownership transferred to the job */
        job->base_url = r->base_url ? strdup(r->base_url) : NULL;

        struct yetty_yplatform_yworkpool_job wj = {
            .run = img_job_run,
            .done = img_job_done,
            .ctx = job,
        };
        struct yetty_ycore_void_result submit_res =
            yetty_yplatform_yworkpool_submit(r->img_pool, wj);
        if (YETTY_IS_ERR(submit_res)) {
            free(job->base_url);
            free(job->url);
            free(job);
            e->loading = 0;
            e->failed = 1;
            return YETTY_ERR(yetty_ycore_int, "yetty_ylexbor_start_image_fetch: submit job",
                             submit_res);
        }
        r->img_jobs_in_flight++;
        submitted++;
    }
    if (submitted > 0) {
        yetty_ylexbor_prof("    img fetch: +%d submitted, %d in flight", submitted,
                           r->img_jobs_in_flight);
    }
    return YETTY_OK(yetty_ycore_int, submitted);
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

/* Merge a rendered SVG scene into the page output inside the box rect,
 * honoring the root's preserveAspectRatio: `none` stretches per axis;
 * meet / slice pick the min / max uniform scale and distribute the
 * leftover by the alignment factors (0 / 0.5 / 1). A slice overflow is
 * not clipped — the merge path has no clip records (documented
 * limitation). Advances *z past the merged records so later page prims
 * stay on top. */
void yetty_ylexbor_svg_merge_transform(float min_x, float min_y, float scene_w, float scene_h,
                                       float par_align_x, float par_align_y, uint8_t par_mode,
                                       float box_x, float box_y, float box_w, float box_h,
                                       float *out_scale_x, float *out_scale_y, float *out_offset_x,
                                       float *out_offset_y)
{
    float scale_x = scene_w > 0.0f ? box_w / scene_w : 1.0f;
    float scale_y = scene_h > 0.0f ? box_h / scene_h : 1.0f;
    if (par_mode != 2) { /* meet / slice: uniform scale */
        float scale = par_mode == 1 ? (scale_x > scale_y ? scale_x : scale_y)
                                    : (scale_x < scale_y ? scale_x : scale_y);
        scale_x = scale;
        scale_y = scale;
    }
    *out_scale_x = scale_x;
    *out_scale_y = scale_y;
    *out_offset_x = box_x + (box_w - scene_w * scale_x) * par_align_x - min_x * scale_x;
    *out_offset_y = box_y + (box_h - scene_h * scale_y) * par_align_y - min_y * scale_y;
}

static void svg_scene_merge(struct yetty_ydraw_drawable_list *buf,
                            const struct yetty_ydraw_drawable_list *scene, float min_x, float min_y,
                            float scene_w, float scene_h, float box_x, float box_y, float box_w,
                            float box_h, float par_align_x, float par_align_y, uint8_t par_mode,
                            uint32_t *z)
{
    if (scene_w <= 0.0f || scene_h <= 0.0f || box_w <= 0.0f || box_h <= 0.0f) {
        return;
    }
    float scale_x, scale_y, offset_x, offset_y;
    yetty_ylexbor_svg_merge_transform(min_x, min_y, scene_w, scene_h, par_align_x, par_align_y,
                                      par_mode, box_x, box_y, box_w, box_h, &scale_x, &scale_y,
                                      &offset_x, &offset_y);
    struct yetty_ycore_int_result merge_res = yetty_ydraw_drawable_list_merge_transformed(
        buf, scene, offset_x, offset_y, scale_x, scale_y, *z);
    if (YETTY_IS_ERR(merge_res)) {
        ydebug("svg merge failed: %s", merge_res.error.msg);
        yetty_ycore_error_destroy(merge_res.error);
        return;
    }
    *z += (uint32_t)merge_res.value;
}

/* Serializer sink for inline <svg> subtrees — plain growable byte buffer,
 * so no lexbor mraw ownership is involved. */
struct svg_serialize_sink {
    char *data;
    size_t len, cap;
    int failed;
};

YETTY_EXTERNAL_CALLBACK
static lxb_status_t svg_serialize_append(const lxb_char_t *chunk, size_t chunk_len, void *ctx)
{
    struct svg_serialize_sink *sink = ctx;
    if (sink->failed) {
        return LXB_STATUS_ERROR;
    }
    if (sink->len + chunk_len + 1 > sink->cap) {
        size_t new_cap = sink->cap ? sink->cap * 2 : 1024;
        while (new_cap < sink->len + chunk_len + 1) {
            new_cap *= 2;
        }
        char *grown = realloc(sink->data, new_cap);
        if (!grown) {
            sink->failed = 1;
            return LXB_STATUS_ERROR;
        }
        sink->data = grown;
        sink->cap = new_cap;
    }
    memcpy(sink->data + sink->len, chunk, chunk_len);
    sink->len += chunk_len;
    sink->data[sink->len] = '\0';
    return LXB_STATUS_OK;
}

void yetty_ylexbor_svg_inline_cache_clear(struct yetty_ylexbor *r)
{
    if (!r) {
        return;
    }
    for (int i = 0; i < r->svg_inline_count; i++) {
        yetty_ydraw_drawable_list_destroy(r->svg_inline_cache[i].scene);
        yetty_ysvg_links_free(r->svg_inline_cache[i].links, r->svg_inline_cache[i].link_count);
    }
    free(r->svg_inline_cache);
    r->svg_inline_cache = NULL;
    r->svg_inline_count = 0;
    r->svg_inline_cap = 0;
}

uint32_t yetty_ylexbor_group_id_for(struct yetty_ylexbor *r, const void *element)
{
    if (!r || !element) {
        return 0;
    }
    for (int i = 0; i < r->group_id_count; i++) {
        if (r->group_ids[i].element == element) {
            return r->group_ids[i].gid;
        }
    }
    if (r->group_id_count == r->group_id_cap) {
        int new_cap = r->group_id_cap ? r->group_id_cap * 2 : 16;
        struct yl_group_id_entry *grown = realloc(r->group_ids, (size_t)new_cap * sizeof(*grown));
        if (!grown) {
            return 0; /* out of memory — caller emits the box ungrouped */
        }
        r->group_ids = grown;
        r->group_id_cap = new_cap;
    }
    /* 0 is reserved for the receiver's root entity — start ids at 1. */
    if (r->next_group_id == 0) {
        r->next_group_id = 1;
    }
    uint32_t gid = r->next_group_id++;
    r->group_ids[r->group_id_count].element = element;
    r->group_ids[r->group_id_count].gid = gid;
    r->group_ids[r->group_id_count].content_hash = 0;
    r->group_ids[r->group_id_count].content_hash_valid = 0;
    r->group_id_count++;
    return gid;
}

void yetty_ylexbor_group_ids_clear(struct yetty_ylexbor *r)
{
    if (!r) {
        return;
    }
    free(r->group_ids);
    r->group_ids = NULL;
    r->group_id_count = 0;
    r->group_id_cap = 0;
    r->have_full_render = 0;
    r->last_layout_hash = 0;
    /* next_group_id is intentionally left climbing — ids are never reused. */
}

/* 64-bit FNV-1a — used to fingerprint a group's prim bytes and the whole
 * page's box geometry so the incremental path can tell what actually changed
 * without a byte-for-byte compare. */
static uint64_t ylexbor_fnv1a64(const void *data, size_t len)
{
    const uint8_t *p = data;
    uint64_t hash = 1469598103934665603ULL;
    for (size_t i = 0; i < len; i++) {
        hash ^= p[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

/* Find the mutable group-id entry for `element`, or NULL if none has been
 * assigned yet (element boxes get their id lazily in the paint loop). */
static struct yl_group_id_entry *ylexbor_group_entry(struct yetty_ylexbor *r, const void *element)
{
    if (!r || !element) {
        return NULL;
    }
    for (int i = 0; i < r->group_id_count; i++) {
        if (r->group_ids[i].element == element) {
            return &r->group_ids[i];
        }
    }
    return NULL;
}

/* FNV-1a over every box's (kind, x, y, w, h). Two renders with an identical
 * hash laid out to identical geometry — the invariant that makes shipping
 * only image-pixel deltas safe (nothing else moved). */
static uint64_t ylexbor_layout_hash(const struct yetty_ylexbor *r)
{
    uint64_t hash = 1469598103934665603ULL;
    for (uint32_t i = 0; i < r->boxes.size; i++) {
        const struct yetty_ylexbor_box *b = &r->boxes.data[i];
        struct {
            int32_t kind;
            float x, y, w, h;
        } key = {(int32_t)b->kind, b->x, b->y, b->w, b->h};
        const uint8_t *p = (const uint8_t *)&key;
        for (size_t k = 0; k < sizeof(key); k++) {
            hash ^= p[k];
            hash *= 1099511628211ULL;
        }
    }
    return hash;
}

/* Rendered scene for an inline <svg> element, cached per element (one
 * ysvg parse+render per element per document — repaints just re-merge).
 * Returns the cache entry (scene NULL when serialization/render failed)
 * or NULL on allocation failure. */
struct yetty_ylexbor_svg_inline_entry *yetty_ylexbor_svg_inline_find(struct yetty_ylexbor *r,
                                                                     const void *element)
{
    for (int i = 0; i < r->svg_inline_count; i++) {
        if (r->svg_inline_cache[i].element == element) {
            return &r->svg_inline_cache[i];
        }
    }
    return NULL;
}

static struct yetty_ylexbor_svg_inline_entry *svg_inline_scene_get(struct yetty_ylexbor *r,
                                                                   lxb_dom_element_t *element,
                                                                   uint32_t inherited_rgba,
                                                                   float inherited_font_px)
{
    for (int i = 0; i < r->svg_inline_count; i++) {
        if (r->svg_inline_cache[i].element == (const void *)element) {
            return &r->svg_inline_cache[i];
        }
    }
    if (r->svg_inline_count == r->svg_inline_cap) {
        int new_cap = r->svg_inline_cap ? r->svg_inline_cap * 2 : 8;
        struct yetty_ylexbor_svg_inline_entry *grown =
            realloc(r->svg_inline_cache, (size_t)new_cap * sizeof(*grown));
        if (!grown) {
            return NULL;
        }
        r->svg_inline_cache = grown;
        r->svg_inline_cap = new_cap;
    }
    struct yetty_ylexbor_svg_inline_entry *entry = &r->svg_inline_cache[r->svg_inline_count++];
    memset(entry, 0, sizeof(*entry));
    entry->element = element;

    struct svg_serialize_sink sink = {0};
    lxb_status_t serialize_status =
        lxb_html_serialize_tree_cb(lxb_dom_interface_node(element), svg_serialize_append, &sink);
    if (serialize_status == LXB_STATUS_OK && !sink.failed && sink.data && sink.len > 0) {
        /* HTML → SVG inheritance: resolve `currentColor` against the
		 * element's computed CSS color (ysvg alone would default it to
		 * black). Textual substitution in the serialized subtree —
		 * "#rrggbb" (7 bytes) always fits where "currentColor" (12)
		 * stood, so the rewrite shrinks in place. */
        {
            char resolved_color[8];
            snprintf(resolved_color, sizeof(resolved_color), "#%02x%02x%02x",
                     (inherited_rgba >> 24) & 0xff, (inherited_rgba >> 16) & 0xff,
                     (inherited_rgba >> 8) & 0xff);
            size_t write_pos = 0;
            for (size_t read_pos = 0; read_pos < sink.len;) {
                if (read_pos + 12 <= sink.len &&
                    strncasecmp(sink.data + read_pos, "currentColor", 12) == 0) {
                    memcpy(sink.data + write_pos, resolved_color, 7);
                    write_pos += 7;
                    read_pos += 12;
                } else {
                    sink.data[write_pos++] = sink.data[read_pos++];
                }
            }
            sink.len = write_pos;
            sink.data[sink.len] = '\0';
            ydebug("svg inline: currentColor -> %s (inherited rgba %08x)", resolved_color,
                   inherited_rgba);
        }
        yetty_ylexbor_svg_parse_preserve_aspect(sink.data, sink.len, &entry->par_align_x,
                                                &entry->par_align_y, &entry->par_mode);
        (void)yetty_ylexbor_svg_scene_render(r, sink.data, sink.len, inherited_font_px,
                                             &entry->scene, &entry->min_x, &entry->min_y, &entry->w,
                                             &entry->h, &entry->links, &entry->link_count);
    }
    free(sink.data);
    return entry;
}

/* Emit the drawable prims for one YL_BOX_INLINE_IMAGE box into `buf`,
 * advancing `*z`. `ox`/`oy` translate every emitted coordinate: the full
 * paint passes (0, 0) (the embed adds the widget offset later), while the
 * incremental image-delta path passes the embed's (dx, dy) so a delta shipped
 * straight to the ygrid figure lands at the SAME coords the full render did.
 * Factored out of the paint loop so both paths emit byte-identical prims
 * inside a CMD_GROUP. `i` is a box index used only for debug logging. Handles
 * the three <img> shapes: inline/linked <svg> (vector merge), grey
 * placeholder (not yet loaded / failed), and a raster yimage prim. */
static void emit_image_box_prims(struct yetty_ylexbor *r, struct yetty_ylexbor_box *b,
                                 struct yetty_ydraw_drawable_list *buf, uint32_t *z, uint32_t i,
                                 float ox, float oy)
{
    const float px = b->x + ox;
    const float py = b->y + oy;
    /* Inline <svg>: serialize the element subtree, render it through ysvg
     * once (per-element cache), and merge the vector scene at the box rect. */
    if (b->element != NULL && b->element->node.local_name == LXB_TAG_SVG) {
        uint32_t inherited_rgba = ((uint32_t)b->fg.r << 24) | ((uint32_t)b->fg.g << 16) |
                                  ((uint32_t)b->fg.b << 8) | (uint32_t)b->fg.a;
        struct yetty_ylexbor_svg_inline_entry *inline_svg =
            svg_inline_scene_get(r, b->element, inherited_rgba, b->font_size);
        if (inline_svg && inline_svg->scene) {
            svg_scene_merge(buf, inline_svg->scene, inline_svg->min_x, inline_svg->min_y,
                            inline_svg->w, inline_svg->h, px, py, b->w, b->h,
                            inline_svg->par_align_x, inline_svg->par_align_y, inline_svg->par_mode,
                            z);
        }
        return;
    }
    char *url = yetty_ylexbor_img_pick_url(r, b->element);
    struct yetty_ylexbor_img_cache_entry *cached = NULL;
    if (url) {
        cached = yetty_ylexbor_img_cache_get_or_load(r, url);
    }
    free(url);

    /* Vector image — merge the rendered scene, keep it vector. */
    if (cached && cached->svg_scene) {
        svg_scene_merge(buf, cached->svg_scene, cached->svg_min_x, cached->svg_min_y, cached->svg_w,
                        cached->svg_h, px, py, b->w, b->h, cached->svg_par_align_x,
                        cached->svg_par_align_y, cached->svg_par_mode, z);
        return;
    }

    if (!cached || cached->failed || !cached->pixels) {
        /* Fetch/decode failed (or no src) — fall back to the grey placeholder
         * so at least the page geometry is preserved. */
        struct yetty_ysdf_box box = {
            .center_x = px + b->w * 0.5f,
            .center_y = py + b->h * 0.5f,
            .half_width = b->w * 0.5f,
            .half_height = b->h * 0.5f,
            .corner_radius = 0,
        };
        /* Drawable fills are ABGR (0xAABBGGRR) — this neutral grey was long
         * written as RGBA and rendered PINK. */
        (void)yetty_ydraw_drawable_list_add_cmd_add_box(buf, 0, (*z)++, 0xffc0c0c0u, 0, 0, &box);
        ydebug("paint image (placeholder) i=%u xy=%.0f,%.0f wh=%.0fx%.0f", i, b->x, b->y, b->w,
               b->h);
        return;
    }

    /* Ship pixels at DISPLAY resolution when the source is much larger than
     * its layout box. Full-res payloads ballooned a page's drawable list past
     * the 16 MiB RPC body cap (a 2000px hero in a 400px card is 25x the
     * bytes), freezing the in-yetty client on its first frame; a box-filter
     * downscale is also what the GPU sampling would show anyway. The result
     * caches on the entry per display size. */
    const uint32_t *ship_pixels = cached->pixels;
    int ship_w = cached->w;
    int ship_h = cached->h;
    {
        int target_w = (int)(b->w > 0 ? b->w : (float)cached->w);
        int target_h = (int)(b->h > 0 ? b->h : (float)cached->h);
        if (target_w > cached->w) {
            target_w = cached->w;
        }
        if (target_h > cached->h) {
            target_h = cached->h;
        }
        /* Wire-size ceiling: even at layout size, one hero screenshot is
         * megabytes of RGBA; half a megapixel is indistinguishable on a
         * terminal cell grid and keeps a whole page's envelope under the RPC
         * cap. */
        if (target_w > 0 && target_h > 0 && (long)target_w * (long)target_h > 262144l) {
            double shrink = sqrt(262144.0 / ((double)target_w * (double)target_h));
            target_w = (int)((double)target_w * shrink);
            target_h = (int)((double)target_h * shrink);
            if (target_w < 1) {
                target_w = 1;
            }
            if (target_h < 1) {
                target_h = 1;
            }
        }
        if (target_w > 0 && target_h > 0 &&
            (cached->w >= target_w * 2 || (cached->w > target_w && cached->h > target_h))) {
            if (cached->scaled_pixels &&
                (cached->scaled_w != target_w || cached->scaled_h != target_h)) {
                free(cached->scaled_pixels);
                cached->scaled_pixels = NULL;
            }
            if (!cached->scaled_pixels) {
                uint32_t *scaled = malloc((size_t)target_w * (size_t)target_h * sizeof(uint32_t));
                if (scaled) {
                    for (int out_y = 0; out_y < target_h; out_y++) {
                        int src_y0 = out_y * cached->h / target_h;
                        int src_y1 = (out_y + 1) * cached->h / target_h;
                        if (src_y1 <= src_y0) {
                            src_y1 = src_y0 + 1;
                        }
                        for (int out_x = 0; out_x < target_w; out_x++) {
                            int src_x0 = out_x * cached->w / target_w;
                            int src_x1 = (out_x + 1) * cached->w / target_w;
                            if (src_x1 <= src_x0) {
                                src_x1 = src_x0 + 1;
                            }
                            uint32_t acc_r = 0, acc_g = 0, acc_b = 0, acc_a = 0, n = 0;
                            for (int sy = src_y0; sy < src_y1; sy++) {
                                const uint32_t *row = cached->pixels + (size_t)sy * cached->w;
                                for (int sx = src_x0; sx < src_x1; sx++) {
                                    uint32_t px = row[sx];
                                    acc_r += px & 0xffu;
                                    acc_g += (px >> 8) & 0xffu;
                                    acc_b += (px >> 16) & 0xffu;
                                    acc_a += (px >> 24) & 0xffu;
                                    n++;
                                }
                            }
                            scaled[(size_t)out_y * target_w + out_x] =
                                (acc_r / n) | ((acc_g / n) << 8) | ((acc_b / n) << 16) |
                                ((acc_a / n) << 24);
                        }
                    }
                    cached->scaled_pixels = scaled;
                    cached->scaled_w = target_w;
                    cached->scaled_h = target_h;
                }
            }
            if (cached->scaled_pixels) {
                ship_pixels = cached->scaled_pixels;
                ship_w = cached->scaled_w;
                ship_h = cached->scaled_h;
            }
        }
    }

    /* Build a yimage prim sized to the box's allocated geometry — the decoded
     * pixels carry the source dimensions, the bounds carry where to draw. The
     * GPU does the resampling at sample time. */
    struct yetty_yimage_uniforms u = {
        .bounds_x = px,
        .bounds_y = py,
        .bounds_w = b->w > 0 ? b->w : (float)cached->w,
        .bounds_h = b->h > 0 ? b->h : (float)cached->h,
        .image_w = (uint32_t)ship_w,
        .image_h = (uint32_t)ship_h,
    };
    struct yetty_yimage_buffers bufs = {
        .pixels = ship_pixels,
        .pixels_len = (size_t)ship_w * (size_t)ship_h,
    };
    size_t need = yetty_yimage_uniforms_serialized_size(&u, &bufs);
    uint8_t *prim = malloc(need);
    if (!prim) {
        return;
    }
    struct yetty_ycore_size_result ser = yetty_yimage_uniforms_serialize(&u, &bufs, prim, need);
    if (YETTY_IS_ERR(ser)) {
        free(prim);
        return;
    }
    (void)yetty_ydraw_drawable_list_add_prim(buf, prim, need);
    free(prim);
    (*z)++;
    ydebug("paint image i=%u xy=%.0f,%.0f wh=%.0fx%.0f src=%dx%d", i, b->x, b->y, b->w, b->h,
           cached->w, cached->h);
}

struct yetty_ycore_void_result yetty_ylexbor_paint(struct yetty_ylexbor *r,
                                                   struct yetty_ydraw_drawable_list *buf)
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

    /* Parallel image prefetch. Before the synchronous per-box paint
	 * loop kicks in (which does decode + cache-populate on first hit
	 * per image), batch-fetch every <img> URL on the page in parallel
	 * via curl_multi. With HTTP/2 multiplexing this drops Wikipedia's
	 * ~34 image fetches from 34×sequential RTTs to 1 connection per
	 * origin × multiplexed streams — closer to what Chrome does.
	 * Skips URLs already in the cache (re-paints) and `data:` URIs
	 * (handled inline by img_cache_get_or_load). */
    /* Skipped in defer mode — an interactive host streams images in one
     * at a time off the render thread, so this batch fetch must never
     * block the paint pass. */
    if (!r->defer_image_fetch) {
        uint32_t img_count = 0;
        for (uint32_t i = 0; i < r->boxes.size; i++) {
            if (r->boxes.data[i].kind == YL_BOX_INLINE_IMAGE) {
                img_count++;
            }
        }
        if (img_count > 0) {
            char **urls = calloc(img_count, sizeof(*urls));
            struct yetty_ybrowser_request *fetch_requests =
                calloc(img_count, sizeof(*fetch_requests));
            struct yetty_ybrowser_response *fetch_responses =
                calloc(img_count, sizeof(*fetch_responses));
            int n = 0;
            if (urls && fetch_requests && fetch_responses) {
                for (uint32_t i = 0; i < r->boxes.size && n < (int)img_count; i++) {
                    struct yetty_ylexbor_box *b = &r->boxes.data[i];
                    if (b->kind != YL_BOX_INLINE_IMAGE || !b->element) {
                        continue;
                    }
                    char *u = yetty_ylexbor_img_pick_url(r, b->element);
                    if (!u) {
                        continue;
                    }
                    /* Skip data: URIs and already-cached entries. */
                    int already = 0;
                    if (strncmp(u, "data:", 5) == 0) {
                        already = 1;
                    }
                    for (int k = 0; !already && k < r->img_cache_count; k++) {
                        if (r->img_cache[k].url && strcmp(r->img_cache[k].url, u) == 0) {
                            already = 1;
                            break;
                        }
                    }
                    /* De-dup within this batch — multiple <img> can
					 * share the same src. */
                    for (int k = 0; !already && k < n; k++) {
                        if (strcmp(urls[k], u) == 0) {
                            already = 1;
                            break;
                        }
                    }
                    if (already) {
                        free(u);
                        continue;
                    }
                    urls[n] = u; /* transferred — freed below */
                    n++;
                }
                if (n > 0) {
                    for (int k = 0; k < n; k++) {
                        fetch_requests[k].url = urls[k];
                        fetch_requests[k].kind = YETTY_YBROWSER_REQUEST_IMAGE;
                        fetch_requests[k].referer = r->base_url;
                    }
                    struct yetty_ycore_void_result many_res =
                        yetty_ybrowser_fetch_many(r->loader, fetch_requests, n, fetch_responses,
                                                  /*host_connection_cap=*/8);
                    if (YETTY_IS_ERR(many_res)) {
                        yetty_ycore_error_destroy(many_res.error);
                    }
                    /* Insert fetched bytes into the cache, decoding
					 * each in turn. Decode is fast enough that
					 * parallelising it via worker threads turned
					 * out not to pay back the pthread_create
					 * overhead — measured 0.77s vs 0.80s on the
					 * Photography page; reverted. */
                    for (int k = 0; k < n; k++) {
                        struct yetty_ybrowser_response *response = &fetch_responses[k];
                        if (r->img_cache_count == r->img_cache_cap) {
                            int nc = r->img_cache_cap ? r->img_cache_cap * 2 : 8;
                            struct yetty_ylexbor_img_cache_entry *p =
                                realloc(r->img_cache, (size_t)nc * sizeof(*p));
                            if (!p) {
                                yetty_ybrowser_response_dispose(response);
                                continue;
                            }
                            r->img_cache = p;
                            r->img_cache_cap = nc;
                        }
                        struct yetty_ylexbor_img_cache_entry *e =
                            &r->img_cache[r->img_cache_count++];
                        memset(e, 0, sizeof(*e));
                        e->url = urls[k]; /* transfer ownership */
                        urls[k] = NULL;
                        if (!image_response_ok(response)) {
                            yetty_ybrowser_response_dispose(response);
                            e->failed = 1;
                            continue;
                        }
                        if (img_cache_entry_fill_svg(r, e, response->body, response->body_len)) {
                            yetty_ybrowser_response_dispose(response);
                            continue;
                        }
                        if (!decorate_image_response(r->loader, e->url, response)) {
                            yetty_ybrowser_response_dispose(response);
                            e->failed = 1;
                            continue;
                        }
                        e->pixels = response->image_pixels;
                        e->w = response->image_width;
                        e->h = response->image_height;
                        response->image_pixels = NULL; /* moved into the cache */
                        yetty_ybrowser_response_dispose(response);
                    }
                }
            }
            for (int k = 0; k < n; k++) {
                free(urls[k]);
            }
            free(urls);
            free(fetch_requests);
            free(fetch_responses);
        }
    }

    ydebug("paint total boxes=%u", r->boxes.size);
    for (uint32_t i = 0; i < r->boxes.size; i++) {
        struct yetty_ylexbor_box *b = &r->boxes.data[i];
        if (yetty_ylexbor_box_clipped_out(r, i)) {
            /* Wholly outside an overflow:hidden ancestor — clipped away. */
            continue;
        }
        if (b->opacity < 0.02f) {
            /* Effectively transparent (opacity:0, folded down the subtree at
			 * box-build) — the box keeps its laid-out space but paints
			 * nothing. This is how JS carousels / tab-panels hide the
			 * inactive slide in place; painting it produced the Google News
			 * weather-widget overlap (the hidden warning panel drawn on top
			 * of the visible forecast). We do not alpha-composite, so any
			 * partially-transparent box (>= 0.02) still paints fully. */
            continue;
        }
        if (b->vis_hidden) {
            /* visibility:hidden|collapse — the box keeps its laid-out
			 * space (siblings are already placed around it) but paints
			 * no background, border, or glyphs. A visibility:visible
			 * descendant is a separate box with its own cleared flag,
			 * so it still paints. */
            continue;
        }
        /* A box with h=0 but a visible border is normal — that's
		 * how `<hr>` renders (border-top: 1px on a content-less
		 * block). Don't skip it; the border paint below produces
		 * the visible 1px line. Same logic for w=0 vertical
		 * separators (rare in HTML but legal CSS). */
        bool has_visible_border = b->kind == YL_BOX_BLOCK && b->border_color.a != 0 &&
                                  (b->border_top > 0 || b->border_right > 0 ||
                                   b->border_bottom > 0 || b->border_left > 0);
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
                (void)yetty_ydraw_drawable_list_add_cmd_add_box(buf, 0, z++, pack_rgba(b->bg), 0, 0,
                                                                &box);
            }

            /* P2.10 background-image yimage emission removed: ylexbor
			 * has none, and we're trying to match its visual baseline
			 * since the user reports ylexbor renders correctly. */
            /* P1.4 list-item marker emission removed: ylexbor has no
			 * markers at all and the user reports its rendering as
			 * correct. */
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
                    (void)yetty_ydraw_drawable_list_add_cmd_add_box(buf, 0, z++, bc, 0, 0, &bx);
                }
                if (b->border_bottom > 0) {
                    struct yetty_ysdf_box bx = {
                        .center_x = b->x + b->w * 0.5f,
                        .center_y = b->y + b->h - b->border_bottom * 0.5f,
                        .half_width = b->w * 0.5f,
                        .half_height = b->border_bottom * 0.5f,
                    };
                    (void)yetty_ydraw_drawable_list_add_cmd_add_box(buf, 0, z++, bc, 0, 0, &bx);
                }
                if (b->border_left > 0) {
                    struct yetty_ysdf_box bx = {
                        .center_x = b->x + b->border_left * 0.5f,
                        .center_y = b->y + b->h * 0.5f,
                        .half_width = b->border_left * 0.5f,
                        .half_height = b->h * 0.5f,
                    };
                    (void)yetty_ydraw_drawable_list_add_cmd_add_box(buf, 0, z++, bc, 0, 0, &bx);
                }
                if (b->border_right > 0) {
                    struct yetty_ysdf_box bx = {
                        .center_x = b->x + b->w - b->border_right * 0.5f,
                        .center_y = b->y + b->h * 0.5f,
                        .half_width = b->border_right * 0.5f,
                        .half_height = b->h * 0.5f,
                    };
                    (void)yetty_ydraw_drawable_list_add_cmd_add_box(buf, 0, z++, bc, 0, 0, &bx);
                }
            }
            break;
        }

        case YL_BOX_INLINE_IMAGE: {
            /* Wrap this image's prims in a stable CMD_GROUP so a landed image
			 * can later ship as a per-group delta (surgical replace on the
			 * ygrid receiver) instead of a full-page reship. Anonymous image
			 * boxes (no element) fall back to bare prims at the root entity —
			 * same bytes as before. The group's inner prims keep absolute page
			 * coords, so cell-append order matches the flat path: pixel-
			 * identical on a full render. */
            uint32_t image_gid = yetty_ylexbor_group_id_for(r, b->element);
            uint32_t group_marker = 0;
            bool grouped = false;
            if (image_gid != 0) {
                struct yetty_ydraw_id_result gm =
                    yetty_ydraw_drawable_list_begin_group(buf, image_gid);
                if (YETTY_IS_ERR(gm)) {
                    yetty_ycore_error_destroy(gm.error);
                } else {
                    group_marker = gm.value;
                    grouped = true;
                }
            }
            emit_image_box_prims(r, b, buf, &z, i, 0.0f, 0.0f);
            if (grouped) {
                struct yetty_ycore_void_result ge =
                    yetty_ydraw_drawable_list_end_group(buf, group_marker);
                if (YETTY_IS_ERR(ge)) {
                    yetty_ycore_error_destroy(ge.error);
                } else {
                    /* Fingerprint the group's just-emitted page-coord payload
					 * (everything after the 12-byte CMD_GROUP header) so a
					 * later incremental pass can tell whether this image's
					 * pixels changed without re-shipping it. */
                    const uint8_t *bytes = yetty_ydraw_drawable_list_data(buf);
                    size_t total = yetty_ydraw_drawable_list_size(buf);
                    struct yl_group_id_entry *entry = ylexbor_group_entry(r, b->element);
                    if (entry && bytes && total >= (size_t)group_marker + 12u) {
                        size_t payload_off = (size_t)group_marker + 12u;
                        entry->content_hash =
                            ylexbor_fnv1a64(bytes + payload_off, total - payload_off);
                        entry->content_hash_valid = 1;
                    }
                }
            }
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
                (void)n; /* consumed only by the ydebug() above */
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
            /* Plain text emission — match ylexbor's path exactly.
			 * word_spacing/char_spacing aren't routed through; the
			 * justify-fill code computes word_spacing but until we
			 * confirm it's not the source of the user-reported
			 * scattered glyphs we keep it dormant. */
            (void)yetty_ydraw_drawable_list_add_text(buf, b->x, baseline_y, &txt, b->font_size,
                                                     pack_rgba(b->fg), z++, /*font_id=*/-1,
                                                     /*rotation=*/0.0f);
            /* Text decorations are thin SDF rects spanning the run width,
			 * each at a different vertical band relative to the text box:
			 *   underline    — just below the baseline,
			 *   line-through — through the x-height midline,
			 *   overline     — above the cap line.
			 * State is threaded through layout per inline fragment; emit a
			 * rect for whichever flags are set. */
            if ((b->underline || b->line_through || b->overline) && b->w > 0) {
                float thickness = b->font_size * 0.06f;
                if (thickness < 1.0f) {
                    thickness = 1.0f;
                }
                const float half_w = b->w * 0.5f;
                const float center_x = b->x + half_w;
                const float half_t = thickness * 0.5f;
                if (b->underline) {
                    float underline_y = baseline_y + b->font_size * 0.12f;
                    struct yetty_ysdf_box ubx = {
                        .center_x = center_x,
                        .center_y = underline_y + half_t,
                        .half_width = half_w,
                        .half_height = half_t,
                    };
                    (void)yetty_ydraw_drawable_list_add_cmd_add_box(buf, 0, z++, pack_rgba(b->fg),
                                                                    0, 0, &ubx);
                }
                if (b->line_through) {
                    /* Strike through the visual middle of the glyphs (≈ half
					 * the cap height above the baseline). */
                    float strike_y = baseline_y - b->font_size * 0.28f;
                    struct yetty_ysdf_box sbx = {
                        .center_x = center_x,
                        .center_y = strike_y,
                        .half_width = half_w,
                        .half_height = half_t,
                    };
                    (void)yetty_ydraw_drawable_list_add_cmd_add_box(buf, 0, z++, pack_rgba(b->fg),
                                                                    0, 0, &sbx);
                }
                if (b->overline) {
                    /* Above the cap line, near the top of the line box. */
                    float overline_y = b->y + b->font_size * 0.08f;
                    struct yetty_ysdf_box obx = {
                        .center_x = center_x,
                        .center_y = overline_y + half_t,
                        .half_width = half_w,
                        .half_height = half_t,
                    };
                    (void)yetty_ydraw_drawable_list_add_cmd_add_box(buf, 0, z++, pack_rgba(b->fg),
                                                                    0, 0, &obx);
                }
            }
            break;
        }
        }
    }

    /* Publish the scene rectangle. Use viewport_w as a floor for X so
	 * a page with no full-width background still produces a sensible
	 * scene width matching the requested viewport — and as a CEILING:
	 * a single overflowing prim (an unclipped carousel track, a nowrap
	 * marquee row — patterns real pages hide with overflow:hidden we
	 * don't model) would otherwise widen the scene past the viewport
	 * and the embed compresses the WHOLE page to fit, shrinking every
	 * glyph (github hydrated at 1640px painted a 3774px scene → 43%
	 * zoom soup). Cropping at the viewport edge is what overflow-x
	 * hidden shows. Height stays unclamped — vertical scroll is real. */
    float min_w = (float)r->viewport_w;
    if (scene_max_x < min_w) {
        scene_max_x = min_w;
    }
    yetty_ydraw_drawable_list_set_scene_bounds(buf, 0.0f, 0.0f, scene_max_x, scene_max_y);
    ydebug("paint scene bounds = (0,0)-(%.0f,%.0f)", scene_max_x, scene_max_y);

    /* This full paint established every group on the receiver at the current
     * geometry; snapshot the layout fingerprint so the incremental path can
     * detect a later relayout that shifted content (and defer to a full
     * render) versus one where only image pixels landed. */
    r->last_layout_hash = ylexbor_layout_hash(r);
    r->have_full_render = 1;

    return YETTY_OK_VOID();
}

struct yetty_ylexbor_incremental_result yetty_ylexbor_paint_image_deltas(
    struct yetty_ylexbor *r, struct yetty_ydraw_drawable_list *buf, float offset_x, float offset_y)
{
    struct yetty_ylexbor_incremental_result out = {0, 0};
    if (!r || !buf || !r->have_full_render) {
        return out; /* no baseline on the receiver — caller does a full render */
    }

    struct yetty_ycore_void_result relayout_res = yetty_ylexbor_relayout(r);
    if (YETTY_IS_ERR(relayout_res)) {
        yetty_ycore_error_destroy(relayout_res.error);
        return out; /* relayout failed — fall back to a full render */
    }

    /* The delta is only sound if the page laid out to exactly the same
     * geometry as the last full render: images landed but nothing shifted.
     * Any layout change means unshipped non-image content moved, so bail. */
    if (ylexbor_layout_hash(r) != r->last_layout_hash) {
        return out;
    }
    out.is_delta = 1;

    struct yetty_ydraw_drawable_list_result scratch_res =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    if (YETTY_IS_ERR(scratch_res)) {
        yetty_ycore_error_destroy(scratch_res.error);
        out.is_delta = 0;
        return out;
    }
    struct yetty_ydraw_drawable_list *scratch = scratch_res.value;

    for (uint32_t i = 0; i < r->boxes.size; i++) {
        struct yetty_ylexbor_box *b = &r->boxes.data[i];
        if (b->kind != YL_BOX_INLINE_IMAGE || b->element == NULL) {
            continue;
        }
        struct yl_group_id_entry *entry = ylexbor_group_entry(r, b->element);
        if (!entry) {
            /* An image the last full render never grouped (e.g. script added
             * it). It has no receiver entity to replace, so a delta can't
             * carry it — do a full render to establish it. */
            out.is_delta = 0;
            break;
        }

        /* Fingerprint the current page-coord prims (offset 0), matching the
         * hash domain the full render stored. */
        yetty_ydraw_drawable_list_clear(scratch);
        uint32_t scratch_z = 0;
        emit_image_box_prims(r, b, scratch, &scratch_z, i, 0.0f, 0.0f);
        const uint8_t *scratch_bytes = yetty_ydraw_drawable_list_data(scratch);
        size_t scratch_len = yetty_ydraw_drawable_list_size(scratch);
        uint64_t hash =
            ylexbor_fnv1a64(scratch_bytes ? scratch_bytes : (const uint8_t *)"", scratch_len);
        if (entry->content_hash_valid && hash == entry->content_hash) {
            continue; /* this image is unchanged — nothing to ship */
        }

        /* Changed: re-emit its group at the embed's offset so the delta lands
         * where the full render placed it, and refresh the fingerprint. */
        struct yetty_ydraw_id_result gm = yetty_ydraw_drawable_list_begin_group(buf, entry->gid);
        if (YETTY_IS_ERR(gm)) {
            yetty_ycore_error_destroy(gm.error);
            continue;
        }
        uint32_t delta_z = 0;
        emit_image_box_prims(r, b, buf, &delta_z, i, offset_x, offset_y);
        struct yetty_ycore_void_result ge = yetty_ydraw_drawable_list_end_group(buf, gm.value);
        if (YETTY_IS_ERR(ge)) {
            yetty_ycore_error_destroy(ge.error);
            continue;
        }
        entry->content_hash = hash;
        entry->content_hash_valid = 1;
        out.changed_groups++;
    }

    yetty_ydraw_drawable_list_destroy(scratch);
    return out;
}
