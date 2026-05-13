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
    if (strcasecmp(noleading, "pdf") == 0) {
        return YETTY_YCAT_TYPE_PDF;
    }
    if (strcasecmp(noleading, "svg") == 0) {
        return YETTY_YCAT_TYPE_SVG;
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
    if (!mgc_path && YETTY_YCAT_MAGIC_MGC_PATH[0]) {
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

enum yetty_ycat_type yetty_ycat_detect(const uint8_t *bytes, size_t len, const char *path)
{
    /* Extension first on types libmagic generalises away (markdown and
	 * most source files → text/plain). */
    enum yetty_ycat_type by_ext = yetty_ycat_type_from_extension(path_extension(path));
    if (by_ext == YETTY_YCAT_TYPE_MARKDOWN || by_ext == YETTY_YCAT_TYPE_PDF ||
        by_ext == YETTY_YCAT_TYPE_SVG) {
        return by_ext;
    }

    enum yetty_ycat_type by_magic = detect_via_libmagic(bytes, len);
    if (by_magic != YETTY_YCAT_TYPE_UNKNOWN && by_magic != YETTY_YCAT_TYPE_TEXT) {
        return by_magic;
    }

    if (by_ext != YETTY_YCAT_TYPE_UNKNOWN) {
        return by_ext;
    }
    return by_magic;
}
