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
#ifdef YETTY_YCAT_HAS_YCHART
#include <yetty/ychart/data-parser.h>
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
    /* OOXML container MIMEs (libmagic with a current magic DB reports the
     * full vnd strings; older DBs fall back to application/zip, which the
     * content sniffer resolves). */
    if (strcmp(mime, "application/vnd.openxmlformats-officedocument.wordprocessingml.document") ==
        0) {
        return YETTY_YCAT_TYPE_DOCX;
    }
    if (strcmp(mime, "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet") == 0) {
        return YETTY_YCAT_TYPE_XLSX;
    }
    if (strcmp(mime, "application/vnd.openxmlformats-officedocument.presentationml.presentation") ==
        0) {
        return YETTY_YCAT_TYPE_PPTX;
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
    /* WGSL shader text — rendered as an animated yshadertoy prim. */
    if (strcasecmp(noleading, "wgsl") == 0) {
        return YETTY_YCAT_TYPE_SHADERTOY;
    }
    /* ycircuit schematic DSL. */
    if (strcasecmp(noleading, "circuit") == 0 || strcasecmp(noleading, "yct") == 0) {
        return YETTY_YCAT_TYPE_CIRCUIT;
    }
    /* ychart data files. Generic .json/.yaml are NOT mapped here — only
     * the explicit chart extensions, so structured data files are not
     * hijacked (content with a `#ychart` directive or a chart key is still
     * sniffed; bare tabular .csv/.tsv is claimed by the content sniff). */
    if (strcasecmp(noleading, "chart") == 0 || strcasecmp(noleading, "ychart") == 0) {
        return YETTY_YCAT_TYPE_CHART;
    }
    /* NumPy arrays — rendered as a line plot via yplot. */
    if (strcasecmp(noleading, "npy") == 0) {
        return YETTY_YCAT_TYPE_PLOT;
    }
    /* OOXML documents (plus the macro-enabled variants — same container). */
    if (strcasecmp(noleading, "docx") == 0 || strcasecmp(noleading, "docm") == 0) {
        return YETTY_YCAT_TYPE_DOCX;
    }
    if (strcasecmp(noleading, "xlsx") == 0 || strcasecmp(noleading, "xlsm") == 0) {
        return YETTY_YCAT_TYPE_XLSX;
    }
    if (strcasecmp(noleading, "pptx") == 0 || strcasecmp(noleading, "pptm") == 0 ||
        strcasecmp(noleading, "ppsx") == 0) {
        return YETTY_YCAT_TYPE_PPTX;
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

#ifdef YETTY_YCAT_HAS_YSHADERTOY
/* WGSL has no libmagic signature; the Shadertoy contract makes the sniff
 * trivial — a `fn mainImage(` definition somewhere in the first few KB.
 * Covers piped stdin with no .wgsl extension. Only compiled in when the
 * shadertoy handler is, so a handler-less build never classifies text as
 * SHADERTOY. */
static int looks_like_wgsl_main_image(const uint8_t *bytes, size_t len)
{
    if (!bytes || len == 0u) {
        return 0;
    }
    size_t scan = len < 4096u ? len : 4096u;
    static const char marker[] = "fn mainImage(";
    size_t needle_len = sizeof(marker) - 1u;
    if (needle_len > scan) {
        return 0;
    }
    for (size_t i = 0; i + needle_len <= scan; i++) {
        if (memcmp(bytes + i, marker, needle_len) == 0) {
            return 1;
        }
    }
    return 0;
}
#endif

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

#ifdef YETTY_YCAT_HAS_YCHART
/* Bare tabular data sniff: a headered CSV/TSV with no chart marker. The
 * shape test is deliberately strict so prose with commas is never claimed:
 * the first line needs >= 2 delimited fields, the next lines (up to four
 * checked) must repeat the same field count, and the second line must
 * carry at least one numeric field. */
static int looks_like_bare_table(const char *text, size_t len)
{
    size_t scan = len < 4096u ? len : 4096u;
    char delimiter = 0;
    size_t line_index = 0;
    size_t expected_fields = 0;
    int numeric_seen = 0;

    size_t pos = 0;
    while (pos < scan && line_index < 4) {
        size_t line_start = pos;
        while (pos < scan && text[pos] != '\n') {
            pos++;
        }
        size_t line_end = pos;
        if (pos < scan) {
            pos++;
        }
        while (line_end > line_start && (text[line_end - 1] == '\r' || text[line_end - 1] == ' ')) {
            line_end--;
        }
        if (line_end == line_start) {
            if (line_index == 0) {
                continue; /* skip leading blank lines */
            }
            break; /* blank line ends the table prefix */
        }
        if (text[line_start] == '#') {
            continue;
        }

        if (line_index == 0) {
            /* Delimiter from the header line. */
            for (size_t i = line_start; i < line_end; i++) {
                if (text[i] == ',') {
                    delimiter = ',';
                    break;
                }
                if (text[i] == '\t') {
                    delimiter = '\t';
                    break;
                }
            }
            if (delimiter == 0) {
                return 0;
            }
        }

        size_t field_count = 1;
        size_t field_start = line_start;
        for (size_t i = line_start; i <= line_end; i++) {
            if (i == line_end || text[i] == delimiter) {
                if (line_index >= 1 && !numeric_seen) {
                    char field[64];
                    size_t field_len = i - field_start;
                    if (field_len > 0 && field_len < sizeof(field)) {
                        memcpy(field, text + field_start, field_len);
                        field[field_len] = '\0';
                        char *end = NULL;
                        strtod(field, &end);
                        if (end != field && end && *end == '\0') {
                            numeric_seen = 1;
                        }
                    }
                }
                if (i < line_end) {
                    field_count++;
                }
                field_start = i + 1;
            }
        }
        if (line_index == 0) {
            if (field_count < 2) {
                return 0;
            }
            expected_fields = field_count;
        } else if (field_count != expected_fields) {
            return 0;
        }
        line_index++;
    }

    return line_index >= 2 && numeric_seen;
}
#endif

#ifdef YETTY_YCAT_HAS_YCIRCUIT
/* The ycircuit DSL has no libmagic signature, but (like mermaid's `graph`)
 * it opens with a required keyword: the first non-blank, non-comment line
 * starts with `circuit`. Only compiled in when the circuit handler is, so a
 * handler-less build never classifies text as CIRCUIT. */
static int looks_like_ycircuit(const uint8_t *bytes, size_t len)
{
    if (!bytes || len == 0) {
        return 0;
    }
    size_t scan = len < 4096u ? len : 4096u;
    size_t pos = 0;
    while (pos < scan) {
        /* Skip leading blanks on the line. */
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
        /* First content line: must be `circuit` followed by EOL/blank. */
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
#endif

#ifdef YETTY_YCAT_HAS_YMSOFFICE
/* OOXML sniff: a ZIP local-header magic plus one of the three telltale
 * part names somewhere in the archive (entry names are stored verbatim in
 * both the local headers and the central directory). */
static int buffer_contains(const uint8_t *bytes, size_t len, const char *needle)
{
    size_t needle_len = strlen(needle);
    if (needle_len > len) {
        return 0;
    }
    for (size_t i = 0; i + needle_len <= len; i++) {
        if (memcmp(bytes + i, needle, needle_len) == 0) {
            return 1;
        }
    }
    return 0;
}

static enum yetty_ycat_type sniff_ooxml(const uint8_t *bytes, size_t len)
{
    if (!bytes || len < 4 || bytes[0] != 'P' || bytes[1] != 'K' || bytes[2] != 0x03 ||
        bytes[3] != 0x04) {
        return YETTY_YCAT_TYPE_UNKNOWN;
    }
    if (buffer_contains(bytes, len, "word/document.xml")) {
        return YETTY_YCAT_TYPE_DOCX;
    }
    if (buffer_contains(bytes, len, "xl/workbook.xml")) {
        return YETTY_YCAT_TYPE_XLSX;
    }
    if (buffer_contains(bytes, len, "ppt/presentation.xml")) {
        return YETTY_YCAT_TYPE_PPTX;
    }
    return YETTY_YCAT_TYPE_UNKNOWN;
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
        by_ext == YETTY_YCAT_TYPE_MUSIC || by_ext == YETTY_YCAT_TYPE_SHADERTOY ||
        by_ext == YETTY_YCAT_TYPE_CIRCUIT || by_ext == YETTY_YCAT_TYPE_CHART ||
        by_ext == YETTY_YCAT_TYPE_DOCX || by_ext == YETTY_YCAT_TYPE_XLSX ||
        by_ext == YETTY_YCAT_TYPE_PPTX) {
        return by_ext;
    }

    enum yetty_ycat_type by_magic = detect_via_libmagic(bytes, len);
    if (by_magic != YETTY_YCAT_TYPE_UNKNOWN && by_magic != YETTY_YCAT_TYPE_TEXT) {
        return by_magic;
    }

#ifdef YETTY_YCAT_HAS_YMSOFFICE
    /* OOXML sniff — catches piped/renamed files where libmagic only says
     * application/zip. */
    {
        enum yetty_ycat_type by_ooxml = sniff_ooxml(bytes, len);
        if (by_ooxml != YETTY_YCAT_TYPE_UNKNOWN) {
            return by_ooxml;
        }
    }
#endif

    /* H.264 Annex-B sniff — runs whenever libmagic gave us text/plain
     * or unknown. The 00 00 (00) 01 prefix is rare in plain text, so
     * false positives are vanishingly unlikely. */
    if (looks_like_h264_annex_b(bytes, len)) {
        return YETTY_YCAT_TYPE_VIDEO;
    }

#ifdef YETTY_YCAT_HAS_YPLOT
    /* NumPy .npy magic — rendered as a line plot via yplot. */
    if (bytes && len >= 6 && memcmp(bytes, "\x93NUMPY", 6) == 0) {
        return YETTY_YCAT_TYPE_PLOT;
    }
#endif

#ifdef YETTY_YCAT_HAS_YCHART
    /* ychart has no libmagic signature; the sniff is conservative — only a
     * `#ychart` directive line or JSON/YAML with a top-level chart key is
     * claimed, so a plain JSON/YAML data file is left as text. Runs before
     * the mermaid sniff (a chart marker never looks like `graph`/`flowchart`). */
    if (bytes && len > 0 && yetty_ychart_can_parse((const char *)bytes, len)) {
        return YETTY_YCAT_TYPE_CHART;
    }
    /* Bare tabular data (headered CSV/TSV): consistent delimiter counts on
     * the first rows and a numeric second row. ychart derives a default
     * column chart from it, so `ycat data.csv` shows a figure, not text. */
    if (bytes && len > 0 && looks_like_bare_table((const char *)bytes, len)) {
        return YETTY_YCAT_TYPE_CHART;
    }
#endif

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

#ifdef YETTY_YCAT_HAS_YSHADERTOY
    /* WGSL mainImage sniff — covers piped stdin with no .wgsl extension. */
    if (looks_like_wgsl_main_image(bytes, len)) {
        return YETTY_YCAT_TYPE_SHADERTOY;
    }
#endif

#ifdef YETTY_YCAT_HAS_YCIRCUIT
    /* ycircuit sniff — covers piped stdin with no .circuit extension. */
    if (looks_like_ycircuit(bytes, len)) {
        return YETTY_YCAT_TYPE_CIRCUIT;
    }
#endif

    if (by_ext != YETTY_YCAT_TYPE_UNKNOWN) {
        return by_ext;
    }
    return by_magic;
}
