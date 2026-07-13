/*
 * mime.c - file-type detection + DCS file-envelope prologue codec.
 *
 * See <yetty/ymime/mime.h> for the API contract and the prologue wire
 * layout. Everything here is pure classification / byte packing — no
 * allocation, no IO, no external libraries.
 */

#include <yetty/ymime/mime.h>

#include <string.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

/*=============================================================================
 * Type names
 *===========================================================================*/

const char *yetty_ymime_type_name(enum yetty_ymime_type type)
{
    switch (type) {
    case YETTY_YMIME_TYPE_TEXT:
        return "text";
    case YETTY_YMIME_TYPE_MARKDOWN:
        return "markdown";
    case YETTY_YMIME_TYPE_PDF:
        return "pdf";
    case YETTY_YMIME_TYPE_IMAGE:
        return "image";
    case YETTY_YMIME_TYPE_SVG:
        return "svg";
    case YETTY_YMIME_TYPE_VIDEO:
        return "video";
    case YETTY_YMIME_TYPE_MUSIC:
        return "music";
    case YETTY_YMIME_TYPE_CIRCUIT:
        return "circuit";
    case YETTY_YMIME_TYPE_MESH:
        return "mesh";
    case YETTY_YMIME_TYPE_UNKNOWN:
    default:
        return "unknown";
    }
}

const char *yetty_ymime_type_canonical_mime(enum yetty_ymime_type type)
{
    switch (type) {
    case YETTY_YMIME_TYPE_TEXT:
        return "text/plain";
    case YETTY_YMIME_TYPE_MARKDOWN:
        return "text/markdown";
    case YETTY_YMIME_TYPE_PDF:
        return "application/pdf";
    case YETTY_YMIME_TYPE_IMAGE:
        return "image/*";
    case YETTY_YMIME_TYPE_SVG:
        return "image/svg+xml";
    case YETTY_YMIME_TYPE_VIDEO:
        return "video/h264";
    case YETTY_YMIME_TYPE_MUSIC:
        return "text/x-lilypond"; /* de-facto editor string, no IANA MIME */
    case YETTY_YMIME_TYPE_CIRCUIT:
        return "text/x-ycircuit"; /* yetty's own DSL */
    case YETTY_YMIME_TYPE_MESH:
        return "model/gltf-binary";
    case YETTY_YMIME_TYPE_UNKNOWN:
    default:
        return NULL;
    }
}

/*=============================================================================
 * MIME → type
 *===========================================================================*/

enum yetty_ymime_type yetty_ymime_type_from_mime(const char *mime)
{
    if (!mime || !*mime) {
        return YETTY_YMIME_TYPE_UNKNOWN;
    }
    if (strcmp(mime, "application/pdf") == 0) {
        return YETTY_YMIME_TYPE_PDF;
    }
    if (strcmp(mime, "text/markdown") == 0 || strcmp(mime, "text/x-markdown") == 0) {
        return YETTY_YMIME_TYPE_MARKDOWN;
    }
    /* SVG before the generic image/ prefix so it routes to the vector
     * renderer, not the raster decoder. */
    if (strcmp(mime, "image/svg+xml") == 0 || strcmp(mime, "image/svg") == 0) {
        return YETTY_YMIME_TYPE_SVG;
    }
    if (strncmp(mime, "image/", 6) == 0) {
        return YETTY_YMIME_TYPE_IMAGE;
    }
    if (strcmp(mime, "video/h264") == 0 || strcmp(mime, "video/H264") == 0 ||
        strcmp(mime, "video/avc") == 0 || strcmp(mime, "video/x-h264") == 0) {
        return YETTY_YMIME_TYPE_VIDEO;
    }
    if (strcmp(mime, "text/x-lilypond") == 0) {
        return YETTY_YMIME_TYPE_MUSIC;
    }
    if (strcmp(mime, "text/x-ycircuit") == 0) {
        return YETTY_YMIME_TYPE_CIRCUIT;
    }
    if (strcmp(mime, "model/gltf-binary") == 0) {
        return YETTY_YMIME_TYPE_MESH;
    }
    if (strncmp(mime, "text/", 5) == 0) {
        return YETTY_YMIME_TYPE_TEXT;
    }
    return YETTY_YMIME_TYPE_UNKNOWN;
}

/*=============================================================================
 * Extension → type
 *===========================================================================*/

enum yetty_ymime_type yetty_ymime_type_from_extension(const char *path_or_extension)
{
    if (!path_or_extension || !*path_or_extension) {
        return YETTY_YMIME_TYPE_UNKNOWN;
    }
    /* Reduce a path to its extension; accept a bare "md" / ".md" too. */
    const char *extension = strrchr(path_or_extension, '.');
    if (!extension) {
        extension = path_or_extension;
        const char *slash = strrchr(path_or_extension, '/');
        if (slash) {
            /* Path with no dot in the basename → no extension. */
            return YETTY_YMIME_TYPE_UNKNOWN;
        }
    } else {
        const char *slash = strrchr(path_or_extension, '/');
        if (slash && slash > extension) {
            return YETTY_YMIME_TYPE_UNKNOWN;
        }
        extension++;
    }
    if (!*extension) {
        return YETTY_YMIME_TYPE_UNKNOWN;
    }

    if (strcasecmp(extension, "md") == 0 || strcasecmp(extension, "markdown") == 0 ||
        strcasecmp(extension, "mdown") == 0 || strcasecmp(extension, "mkd") == 0) {
        return YETTY_YMIME_TYPE_MARKDOWN;
    }
    if (strcasecmp(extension, "pdf") == 0) {
        return YETTY_YMIME_TYPE_PDF;
    }
    if (strcasecmp(extension, "svg") == 0) {
        return YETTY_YMIME_TYPE_SVG;
    }
    if (strcasecmp(extension, "h264") == 0 || strcasecmp(extension, "264") == 0 ||
        strcasecmp(extension, "avc") == 0 || strcasecmp(extension, "x264") == 0) {
        return YETTY_YMIME_TYPE_VIDEO;
    }
    /* LilyPond scores and their include files. */
    if (strcasecmp(extension, "ly") == 0 || strcasecmp(extension, "ily") == 0) {
        return YETTY_YMIME_TYPE_MUSIC;
    }
    /* ycircuit schematic DSL. */
    if (strcasecmp(extension, "circuit") == 0 || strcasecmp(extension, "yct") == 0) {
        return YETTY_YMIME_TYPE_CIRCUIT;
    }
    /* glTF binary container. */
    if (strcasecmp(extension, "glb") == 0) {
        return YETTY_YMIME_TYPE_MESH;
    }
    if (strcasecmp(extension, "txt") == 0) {
        return YETTY_YMIME_TYPE_TEXT;
    }
    /* The stb_image raster set. */
    if (strcasecmp(extension, "png") == 0 || strcasecmp(extension, "jpg") == 0 ||
        strcasecmp(extension, "jpeg") == 0 || strcasecmp(extension, "gif") == 0 ||
        strcasecmp(extension, "bmp") == 0 || strcasecmp(extension, "tga") == 0 ||
        strcasecmp(extension, "psd") == 0 || strcasecmp(extension, "hdr") == 0 ||
        strcasecmp(extension, "pic") == 0 || strcasecmp(extension, "ppm") == 0 ||
        strcasecmp(extension, "pgm") == 0) {
        return YETTY_YMIME_TYPE_IMAGE;
    }
    return YETTY_YMIME_TYPE_UNKNOWN;
}

/*=============================================================================
 * Content sniffers
 *===========================================================================*/

/* PDF: "%PDF-" within the first 1 KiB (the spec allows junk before the
 * header; real-world files keep it in the first kilobyte). */
static int looks_like_pdf(const uint8_t *bytes, size_t len)
{
    static const char marker[] = "%PDF-";
    const size_t marker_len = sizeof(marker) - 1u;
    size_t scan = len < 1024u ? len : 1024u;
    if (scan < marker_len) {
        return 0;
    }
    for (size_t i = 0; i + marker_len <= scan; i++) {
        if (memcmp(bytes + i, marker, marker_len) == 0) {
            return 1;
        }
    }
    return 0;
}

/* SVG: after optional BOM/whitespace/comments the document opens with
 * "<svg", or opens with an XML/DOCTYPE prelude and contains "<svg"
 * within the sniff window. */
static int looks_like_svg(const uint8_t *bytes, size_t len)
{
    size_t scan = len < YETTY_YMIME_SNIFF_WINDOW ? len : YETTY_YMIME_SNIFF_WINDOW;
    size_t pos = 0;
    /* UTF-8 BOM */
    if (scan >= 3u && bytes[0] == 0xEFu && bytes[1] == 0xBBu && bytes[2] == 0xBFu) {
        pos = 3;
    }
    while (pos < scan && (bytes[pos] == ' ' || bytes[pos] == '\t' || bytes[pos] == '\r' ||
                          bytes[pos] == '\n')) {
        pos++;
    }
    if (pos >= scan || bytes[pos] != '<') {
        return 0;
    }
    if (pos + 4u <= scan && memcmp(bytes + pos, "<svg", 4) == 0) {
        return 1;
    }
    /* XML prelude / doctype / comment — any of these may precede the root
     * element; require an actual "<svg" later in the window. */
    if (pos + 2u <= scan && (bytes[pos + 1] == '?' || bytes[pos + 1] == '!')) {
        for (size_t i = pos; i + 4u <= scan; i++) {
            if (memcmp(bytes + i, "<svg", 4) == 0) {
                return 1;
            }
        }
    }
    return 0;
}

/* Raster image magic bytes — the formats stb_image decodes and that have
 * an unambiguous signature. (TGA/PIC/PNM have weak or absent magics; they
 * are reachable via extension or MIME hint only.) */
static int looks_like_raster_image(const uint8_t *bytes, size_t len)
{
    if (len >= 8u) {
        static const uint8_t png_magic[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
        if (memcmp(bytes, png_magic, sizeof(png_magic)) == 0) {
            return 1;
        }
    }
    if (len >= 3u && bytes[0] == 0xFFu && bytes[1] == 0xD8u && bytes[2] == 0xFFu) {
        return 1; /* JPEG */
    }
    if (len >= 6u && (memcmp(bytes, "GIF87a", 6) == 0 || memcmp(bytes, "GIF89a", 6) == 0)) {
        return 1;
    }
    if (len >= 12u && memcmp(bytes, "RIFF", 4) == 0 && memcmp(bytes + 8, "WEBP", 4) == 0) {
        return 1;
    }
    if (len >= 2u && bytes[0] == 'B' && bytes[1] == 'M') {
        return 1; /* BMP */
    }
    if (len >= 11u && memcmp(bytes, "#?RADIANCE", 10) == 0) {
        return 1; /* HDR */
    }
    return 0;
}

/* H.264 Annex-B: a 00 00 (00) 01 start code in the first 64 bytes followed
 * by a NAL type in {7=SPS, 8=PPS, 5=IDR}. */
static int looks_like_h264_annex_b(const uint8_t *bytes, size_t len)
{
    if (len < 5u) {
        return 0;
    }
    size_t scan = len < 64u ? len : 64u;
    for (size_t i = 0u; i + 4u < scan; i++) {
        const uint8_t *probe = bytes + i;
        size_t nal_offset;
        if (probe[0] == 0u && probe[1] == 0u && probe[2] == 0u && probe[3] == 1u) {
            nal_offset = 4u;
        } else if (probe[0] == 0u && probe[1] == 0u && probe[2] == 1u) {
            nal_offset = 3u;
        } else {
            continue;
        }
        if (i + nal_offset >= scan) {
            return 0;
        }
        uint8_t nal_type = bytes[i + nal_offset] & 0x1fu;
        if (nal_type == 7u || nal_type == 8u || nal_type == 5u) {
            return 1;
        }
    }
    return 0;
}

/* glTF binary container: ASCII "glTF" magic at offset 0. */
static int looks_like_glb(const uint8_t *bytes, size_t len)
{
    return len >= 4u && memcmp(bytes, "glTF", 4) == 0;
}

/* LilyPond has no magic, but its leading commands are distinctive. */
static int looks_like_lilypond(const uint8_t *bytes, size_t len)
{
    size_t scan = len < YETTY_YMIME_SNIFF_WINDOW ? len : YETTY_YMIME_SNIFF_WINDOW;
    static const char *const markers[] = {"\\relative",  "\\version", "\\score",
                                          "\\new Staff", "\\clef",    "\\time"};
    for (size_t marker = 0; marker < sizeof(markers) / sizeof(markers[0]); marker++) {
        size_t needle_len = strlen(markers[marker]);
        if (needle_len > scan) {
            continue;
        }
        for (size_t i = 0; i + needle_len <= scan; i++) {
            if (memcmp(bytes + i, markers[marker], needle_len) == 0) {
                return 1;
            }
        }
    }
    return 0;
}

/* The ycircuit DSL opens with a required keyword: the first non-blank,
 * non-comment line starts with `circuit`. */
static int looks_like_ycircuit(const uint8_t *bytes, size_t len)
{
    size_t scan = len < YETTY_YMIME_SNIFF_WINDOW ? len : YETTY_YMIME_SNIFF_WINDOW;
    size_t pos = 0;
    while (pos < scan) {
        while (pos < scan && (bytes[pos] == ' ' || bytes[pos] == '\t' || bytes[pos] == '\r')) {
            pos++;
        }
        if (pos < scan && bytes[pos] == '\n') {
            pos++;
            continue;
        }
        if (pos < scan && bytes[pos] == '#') {
            while (pos < scan && bytes[pos] != '\n') {
                pos++;
            }
            continue;
        }
        static const char keyword[] = "circuit";
        size_t keyword_len = sizeof(keyword) - 1;
        if (pos + keyword_len > scan || memcmp(bytes + pos, keyword, keyword_len) != 0) {
            return 0;
        }
        size_t after = pos + keyword_len;
        return after >= len || bytes[after] == ' ' || bytes[after] == '\t' ||
               bytes[after] == '\r' || bytes[after] == '\n';
    }
    return 0;
}

enum yetty_ymime_type yetty_ymime_sniff(const uint8_t *bytes, size_t len)
{
    if (!bytes || len == 0u) {
        return YETTY_YMIME_TYPE_UNKNOWN;
    }
    if (looks_like_pdf(bytes, len)) {
        return YETTY_YMIME_TYPE_PDF;
    }
    if (looks_like_raster_image(bytes, len)) {
        return YETTY_YMIME_TYPE_IMAGE;
    }
    if (looks_like_glb(bytes, len)) {
        return YETTY_YMIME_TYPE_MESH;
    }
    if (looks_like_svg(bytes, len)) {
        return YETTY_YMIME_TYPE_SVG;
    }
    if (looks_like_h264_annex_b(bytes, len)) {
        return YETTY_YMIME_TYPE_VIDEO;
    }
    if (looks_like_ycircuit(bytes, len)) {
        return YETTY_YMIME_TYPE_CIRCUIT;
    }
    if (looks_like_lilypond(bytes, len)) {
        return YETTY_YMIME_TYPE_MUSIC;
    }
    return YETTY_YMIME_TYPE_UNKNOWN;
}

/*=============================================================================
 * Combined
 *===========================================================================*/

enum yetty_ymime_type yetty_ymime_detect(const char *mime_hint, const char *name_hint,
                                         const uint8_t *bytes, size_t len)
{
    /* A specific MIME hint wins — the sender knows best for the types
     * content sniffing cannot resolve (markdown vs plain text). A bare
     * text-family hint is kept only as the last fallback. */
    enum yetty_ymime_type by_mime = yetty_ymime_type_from_mime(mime_hint);
    if (by_mime != YETTY_YMIME_TYPE_UNKNOWN && by_mime != YETTY_YMIME_TYPE_TEXT) {
        return by_mime;
    }
    enum yetty_ymime_type by_extension = yetty_ymime_type_from_extension(name_hint);
    if (by_extension != YETTY_YMIME_TYPE_UNKNOWN && by_extension != YETTY_YMIME_TYPE_TEXT) {
        return by_extension;
    }
    enum yetty_ymime_type by_sniff = yetty_ymime_sniff(bytes, len);
    if (by_sniff != YETTY_YMIME_TYPE_UNKNOWN) {
        return by_sniff;
    }
    if (by_mime == YETTY_YMIME_TYPE_TEXT || by_extension == YETTY_YMIME_TYPE_TEXT) {
        return YETTY_YMIME_TYPE_TEXT;
    }
    return YETTY_YMIME_TYPE_UNKNOWN;
}

/*=============================================================================
 * Prologue codec
 *===========================================================================*/

static void put_uint16_le(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)(value >> 8);
}

static uint16_t get_uint16_le(const uint8_t *src)
{
    return (uint16_t)(src[0] | ((uint16_t)src[1] << 8));
}

struct yetty_ycore_size_result yetty_ymime_prologue_encode(
    const struct yetty_ymime_prologue *prologue, uint8_t *dst, size_t dst_cap)
{
    if (!prologue || !dst) {
        return YETTY_ERR(yetty_ycore_size, "ymime prologue encode: NULL arg");
    }
    if (prologue->mime_len > 255u || prologue->name_len > 255u) {
        return YETTY_ERR(yetty_ycore_size, "ymime prologue encode: mime/name too long");
    }
    size_t total = 2u + 1u + prologue->mime_len + 1u + prologue->name_len + 2u +
                   prologue->args_len;
    if (total > YETTY_YMIME_PROLOGUE_MAX) {
        return YETTY_ERR(yetty_ycore_size, "ymime prologue encode: exceeds prologue max");
    }
    if (total > dst_cap) {
        return YETTY_ERR(yetty_ycore_size, "ymime prologue encode: dst too small");
    }
    uint8_t *cursor = dst;
    put_uint16_le(cursor, (uint16_t)total);
    cursor += 2;
    *cursor++ = (uint8_t)prologue->mime_len;
    if (prologue->mime_len > 0u) {
        memcpy(cursor, prologue->mime, prologue->mime_len);
        cursor += prologue->mime_len;
    }
    *cursor++ = (uint8_t)prologue->name_len;
    if (prologue->name_len > 0u) {
        memcpy(cursor, prologue->name, prologue->name_len);
        cursor += prologue->name_len;
    }
    put_uint16_le(cursor, (uint16_t)prologue->args_len);
    cursor += 2;
    if (prologue->args_len > 0u) {
        memcpy(cursor, prologue->args, prologue->args_len);
    }
    return YETTY_OK(yetty_ycore_size, total);
}

struct yetty_ycore_size_result yetty_ymime_prologue_decode(const uint8_t *src, size_t len,
                                                           struct yetty_ymime_prologue *out)
{
    if (!src || !out) {
        return YETTY_ERR(yetty_ycore_size, "ymime prologue decode: NULL arg");
    }
    if (len < 2u) {
        return YETTY_ERR(yetty_ycore_size, "ymime prologue decode: truncated size field");
    }
    size_t total = get_uint16_le(src);
    if (total < 2u || total > YETTY_YMIME_PROLOGUE_MAX) {
        return YETTY_ERR(yetty_ycore_size, "ymime prologue decode: bad prologue size");
    }
    if (total > len) {
        return YETTY_ERR(yetty_ycore_size, "ymime prologue decode: truncated prologue");
    }

    memset(out, 0, sizeof(*out));
    size_t pos = 2u;

    if (pos + 1u > total) {
        return YETTY_ERR(yetty_ycore_size, "ymime prologue decode: truncated mime length");
    }
    size_t mime_len = src[pos++];
    if (pos + mime_len > total) {
        return YETTY_ERR(yetty_ycore_size, "ymime prologue decode: truncated mime");
    }
    out->mime = (const char *)(src + pos);
    out->mime_len = mime_len;
    pos += mime_len;

    if (pos + 1u > total) {
        return YETTY_ERR(yetty_ycore_size, "ymime prologue decode: truncated name length");
    }
    size_t name_len = src[pos++];
    if (pos + name_len > total) {
        return YETTY_ERR(yetty_ycore_size, "ymime prologue decode: truncated name");
    }
    out->name = (const char *)(src + pos);
    out->name_len = name_len;
    pos += name_len;

    if (pos + 2u > total) {
        return YETTY_ERR(yetty_ycore_size, "ymime prologue decode: truncated args length");
    }
    size_t args_len = get_uint16_le(src + pos);
    pos += 2u;
    if (pos + args_len > total) {
        return YETTY_ERR(yetty_ycore_size, "ymime prologue decode: truncated args");
    }
    out->args = (const char *)(src + pos);
    out->args_len = args_len;

    /* Bytes between here and `total` are future fields — skipped. */
    return YETTY_OK(yetty_ycore_size, total);
}
