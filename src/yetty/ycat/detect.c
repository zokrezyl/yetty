/*
 * detect.c - file-type detection.
 *
 * libmagic is used when available (YETTY_YCAT_HAS_LIBMAGIC), otherwise a
 * filename-extension fallback runs alone. Even with libmagic the extension
 * is consulted afterwards when libmagic only returns a generic "text/plain"
 * — libmagic doesn't have a stable markdown magic.
 *
 * The libmagic cookie is a per-process singleton. A small helper wraps
 * lazy init; no teardown on exit is required because the process dies
 * immediately after.
 */

#include <yetty/ycat/ycat.h>

#ifdef YETTY_YCAT_HAS_DIAGRAM
#include <yetty/ydiagram/mermaid-parser.h>
#endif
#ifdef YETTY_YCAT_HAS_LOTTIE
#include <yetty/ylottie/ylottie.h>
#endif
#include <yetty/ytrace/ytrace.h>

#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#else
#include <strings.h>
#endif

#ifdef YETTY_YCAT_HAS_LIBMAGIC
#include <magic.h>
#endif

/*=============================================================================
 * MIME → type
 *===========================================================================*/

enum yetty_ycat_type yetty_ycat_type_from_mime(const char *mime)
{
    if (!mime || !*mime) {
        return YETTY_YCAT_TYPE_UNKNOWN;
    }

    if (strcmp(mime, "application/pdf") == 0) {
        return YETTY_YCAT_TYPE_PDF;
    }
    if (strcmp(mime, "text/markdown") == 0 || strcmp(mime, "text/x-markdown") == 0) {
        return YETTY_YCAT_TYPE_MARKDOWN;
    }
    /* Mermaid has no IANA-registered MIME; honour the de-facto strings
     * used by various editors and the Mermaid CLI. */
    if (strcmp(mime, "text/vnd.mermaid") == 0 || strcmp(mime, "text/x-mermaid") == 0 ||
        strcmp(mime, "application/vnd.mermaid") == 0) {
        return YETTY_YCAT_TYPE_MERMAID;
    }
    /* LilyPond has no IANA MIME either; honour the de-facto editor string. */
    if (strcmp(mime, "text/x-lilypond") == 0) {
        return YETTY_YCAT_TYPE_MUSIC;
    }
    /* Check SVG before the generic "image/" prefix so it routes to the
     * vector handler instead of the raster decoder. */
    if (strcmp(mime, "image/svg+xml") == 0 || strcmp(mime, "image/svg") == 0) {
        return YETTY_YCAT_TYPE_SVG;
    }
    /* libmagic with MAGIC_MIME_TYPE returns "image/png", "image/jpeg", etc.
     * stb_image decodes png/jpg/gif/bmp/tga/psd/hdr/pic/pnm; the handler
     * surfaces stb's own error if the subtype isn't supported. */
    if (strncmp(mime, "image/", 6) == 0) {
        return YETTY_YCAT_TYPE_IMAGE;
    }
    /* H.264 Annex-B raw bitstream — libmagic doesn't have a stable
     * mapping for naked H.264, but if a sender hand-sets video/h264 we
     * route it. The bytes sniff below also catches Annex-B without any
     * MIME hint. (We don't currently demux .mp4 in ycat — that needs a
     * minimp4 reader on the sender side first.) */
    if (strcmp(mime, "video/h264") == 0 || strcmp(mime, "video/H264") == 0 ||
        strcmp(mime, "video/avc") == 0 || strcmp(mime, "video/x-h264") == 0) {
        return YETTY_YCAT_TYPE_VIDEO;
    }
    if (strncmp(mime, "text/", 5) == 0) {
        return YETTY_YCAT_TYPE_TEXT;
    }
    return YETTY_YCAT_TYPE_UNKNOWN;
}

/*=============================================================================
 * Extension → type
 *===========================================================================*/

static const char *path_extension(const char *path)
{
    if (!path) {
        return NULL;
    }
    const char *dot = strrchr(path, '.');
    const char *slash = strrchr(path, '/');
    if (!dot || (slash && slash > dot)) {
        return NULL;
    }
    return dot;
}

enum yetty_ycat_type yetty_ycat_type_from_extension(const char *ext)
{
    if (!ext) {
        return YETTY_YCAT_TYPE_UNKNOWN;
    }
    const char *noleading = (*ext == '.') ? ext + 1 : ext;
    if (!*noleading) {
        return YETTY_YCAT_TYPE_UNKNOWN;
    }

    if (strcasecmp(noleading, "md") == 0 || strcasecmp(noleading, "markdown") == 0 ||
        strcasecmp(noleading, "mdown") == 0 || strcasecmp(noleading, "mkd") == 0) {
        return YETTY_YCAT_TYPE_MARKDOWN;
    }
    if (strcasecmp(noleading, "mmd") == 0 || strcasecmp(noleading, "mermaid") == 0) {
        return YETTY_YCAT_TYPE_MERMAID;
    }
    if (strcasecmp(noleading, "pdf") == 0) {
        return YETTY_YCAT_TYPE_PDF;
    }
    if (strcasecmp(noleading, "svg") == 0) {
        return YETTY_YCAT_TYPE_SVG;
    }
    /* dotLottie containers are zips we don't unpack, but a raw Bodymovin JSON
     * is often given the .lottie name — route it and let the handler parse. */
    if (strcasecmp(noleading, "lottie") == 0) {
        return YETTY_YCAT_TYPE_LOTTIE;
    }
    /* Raw H.264 Annex-B common extensions. (.mp4 / .mov / .m4v need a
     * demuxer we don't ship here yet — leave them unmapped for now.) */
    if (strcasecmp(noleading, "h264") == 0 || strcasecmp(noleading, "264") == 0 ||
        strcasecmp(noleading, "avc") == 0 || strcasecmp(noleading, "x264") == 0) {
        return YETTY_YCAT_TYPE_VIDEO;
    }
    /* LilyPond scores and their include files. */
    if (strcasecmp(noleading, "ly") == 0 || strcasecmp(noleading, "ily") == 0) {
        return YETTY_YCAT_TYPE_MUSIC;
    }
    if (strcasecmp(noleading, "txt") == 0) {
        return YETTY_YCAT_TYPE_TEXT;
    }
    /* Image extensions stb_image can decode. */
    if (strcasecmp(noleading, "png") == 0 || strcasecmp(noleading, "jpg") == 0 ||
        strcasecmp(noleading, "jpeg") == 0 || strcasecmp(noleading, "gif") == 0 ||
        strcasecmp(noleading, "bmp") == 0 || strcasecmp(noleading, "tga") == 0 ||
        strcasecmp(noleading, "psd") == 0 || strcasecmp(noleading, "hdr") == 0 ||
        strcasecmp(noleading, "pic") == 0 || strcasecmp(noleading, "ppm") == 0 ||
        strcasecmp(noleading, "pgm") == 0) {
        return YETTY_YCAT_TYPE_IMAGE;
    }
    return YETTY_YCAT_TYPE_UNKNOWN;
}

/*=============================================================================
 * libmagic wrapper (optional)
 *===========================================================================*/

#ifdef YETTY_YCAT_HAS_LIBMAGIC

static magic_t magic_cookie;
static int magic_attempted = 0;

static magic_t get_magic_cookie(void)
{
    if (magic_attempted) {
        return magic_cookie;
    }
    magic_attempted = 1;

    magic_cookie = magic_open(MAGIC_MIME_TYPE | MAGIC_NO_CHECK_COMPRESS);
    if (!magic_cookie) {
        return NULL;
    }

    /* YCAT_MAGIC_MGC env override, then compiled-in path, then system
	 * default. */
    const char *mgc_path = getenv("YCAT_MAGIC_MGC");
#ifdef YETTY_YCAT_MAGIC_MGC_PATH
    if (!mgc_path) {
        mgc_path = YETTY_YCAT_MAGIC_MGC_PATH;
    }
#endif
    if (magic_load(magic_cookie, mgc_path) != 0) {
        if (magic_load(magic_cookie, NULL) != 0) {
            ydebug("libmagic load failed: %s", magic_error(magic_cookie));
            magic_close(magic_cookie);
            magic_cookie = NULL;
            return NULL;
        }
    }
    return magic_cookie;
}

static enum yetty_ycat_type detect_via_libmagic(const uint8_t *bytes, size_t len)
{
    magic_t m = get_magic_cookie();
    if (!m) {
        return YETTY_YCAT_TYPE_UNKNOWN;
    }
    const char *mime = magic_buffer(m, bytes, len);
    if (!mime) {
        return YETTY_YCAT_TYPE_UNKNOWN;
    }
    return yetty_ycat_type_from_mime(mime);
}

#else /* !YETTY_YCAT_HAS_LIBMAGIC */

static enum yetty_ycat_type detect_via_libmagic(const uint8_t *bytes, size_t len)
{
    (void)bytes;
    (void)len;
    return YETTY_YCAT_TYPE_UNKNOWN;
}

#endif

/*=============================================================================
 * Combined
 *===========================================================================*/

/* H.264 Annex-B content sniff: at least one 00 00 (00) 01 prefix in the
 * first 64 bytes followed by a NAL type in {7=SPS, 8=PPS, 5=IDR}. We
 * scan up to 64 bytes — typical SPS/PPS NALs are well under that. */
static int looks_like_h264_annex_b(const uint8_t *bytes, size_t len)
{
    if (!bytes || len < 5u) {
        return 0;
    }
    size_t scan = len < 64u ? len : 64u;
    for (size_t i = 0u; i + 4u < scan; i++) {
        const uint8_t *p = bytes + i;
        size_t nal_off = 0u;
        if (p[0] == 0u && p[1] == 0u && p[2] == 0u && p[3] == 1u) {
            nal_off = 4u;
        } else if (p[0] == 0u && p[1] == 0u && p[2] == 1u) {
            nal_off = 3u;
        } else {
            continue;
        }
        if (i + nal_off >= scan) {
            return 0;
        }
        uint8_t nal_type = bytes[i + nal_off] & 0x1fu;
        if (nal_type == 7u || nal_type == 8u || nal_type == 5u) {
            return 1;
        }
    }
    return 0;
}

#ifdef YETTY_YCAT_HAS_YMUSIC
/* LilyPond has no libmagic signature, but its leading commands are
 * distinctive. Scan the first few KB for a telltale command — the same
 * markers the yless pager keys on. Only compiled in when the music
 * handler is, so a handler-less build never classifies text as MUSIC. */
static int looks_like_lilypond(const uint8_t *bytes, size_t len)
{
    if (!bytes || len == 0) {
        return 0;
    }
    size_t scan = len < 4096u ? len : 4096u;
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
#endif

enum yetty_ycat_type yetty_ycat_detect(const uint8_t *bytes, size_t len, const char *path)
{
    /* Extension first on types libmagic generalises away (markdown,
	 * mermaid, and most source files → text/plain). */
    enum yetty_ycat_type by_ext = yetty_ycat_type_from_extension(path_extension(path));
    if (by_ext == YETTY_YCAT_TYPE_MARKDOWN || by_ext == YETTY_YCAT_TYPE_PDF ||
        by_ext == YETTY_YCAT_TYPE_SVG || by_ext == YETTY_YCAT_TYPE_MERMAID ||
        by_ext == YETTY_YCAT_TYPE_VIDEO || by_ext == YETTY_YCAT_TYPE_LOTTIE ||
        by_ext == YETTY_YCAT_TYPE_MUSIC) {
        return by_ext;
    }

    enum yetty_ycat_type by_magic = detect_via_libmagic(bytes, len);
    if (by_magic != YETTY_YCAT_TYPE_UNKNOWN && by_magic != YETTY_YCAT_TYPE_TEXT) {
        return by_magic;
    }

    /* H.264 Annex-B sniff — runs whenever libmagic gave us text/plain
     * or unknown. The 00 00 (00) 01 prefix is rare in plain text, so
     * false positives are vanishingly unlikely. */
    if (looks_like_h264_annex_b(bytes, len)) {
        return YETTY_YCAT_TYPE_VIDEO;
    }

#ifdef YETTY_YCAT_HAS_DIAGRAM
    /* Mermaid has no libmagic signature, but the syntax is distinctive:
     * the first non-comment, non-blank line starts with `graph ` or
     * `flowchart `. yetty_ydiagram_mermaid_can_parse implements that
     * sniff and is cheap enough to run on every text/plain blob. */
    if (bytes && len > 0 && yetty_ydiagram_mermaid_can_parse((const char *)bytes, len)) {
        return YETTY_YCAT_TYPE_MERMAID;
    }
#endif

#ifdef YETTY_YCAT_HAS_LOTTIE
    /* Lottie has no libmagic signature and shares the .json extension with
     * unrelated data, so sniff for its tell-tale top-level shape. */
    if (bytes && len > 0 && yetty_ylottie_can_parse((const char *)bytes, len)) {
        return YETTY_YCAT_TYPE_LOTTIE;
    }
#endif

#ifdef YETTY_YCAT_HAS_YMUSIC
    /* LilyPond sniff — covers piped stdin with no .ly extension. */
    if (looks_like_lilypond(bytes, len)) {
        return YETTY_YCAT_TYPE_MUSIC;
    }
#endif

    if (by_ext != YETTY_YCAT_TYPE_UNKNOWN) {
        return by_ext;
    }
    return by_magic;
}
