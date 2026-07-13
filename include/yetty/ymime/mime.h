#ifndef YETTY_YMIME_MIME_H
#define YETTY_YMIME_MIME_H

/*
 * ymime - file-type detection + the DCS file-envelope prologue codec.
 *
 * Shared by the terminal (receive side of YETTY_DCS_MIME_FILE) and thin
 * clients (ycat --raw). Pure classification over a byte window plus two
 * optional hints — a declared MIME string and a filename. Deliberately
 * self-contained: no libmagic, no renderer-module can_parse callbacks, so
 * the terminal can link it without dragging in client-side handler stacks.
 * (ycat's own detect.c keeps its wider type set and libmagic; the two may
 * be unified later with ycat delegating to this module.)
 *
 * Detection order in yetty_ymime_detect:
 *   1. declared MIME hint      — the sender knows best for types content
 *                                sniffing cannot resolve (markdown vs text)
 *   2. filename-extension hint
 *   3. content sniffers        — magic bytes / distinctive syntax
 * A hint that only says "text" does not shadow a more specific sniff.
 *
 * The prologue is the variable-length header at the front of the FIRST
 * chunk's decompressed payload of a YETTY_DCS_MIME_FILE envelope (the
 * fixed 32-byte yetty_yface_file_meta rides in the args slot; strings do
 * not fit there). Layout, little-endian, fields contiguous:
 *
 *   u16 prologue_size   total bytes including this field; decoders skip
 *                       forward to prologue_size, so fields can be
 *                       appended without a version bump
 *   u8  mime_len        declared MIME string (may be 0)
 *   u8  name_len        filename hint for extension detection (may be 0)
 *   u16 args_len        render-flag string, same syntax the renderers
 *                       take ("--font-size=…") (may be 0)
 *
 * File bytes start at offset prologue_size.
 */

#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

/*=============================================================================
 * Detected type
 *===========================================================================*/

enum yetty_ymime_type {
    YETTY_YMIME_TYPE_UNKNOWN = 0,
    YETTY_YMIME_TYPE_TEXT,
    YETTY_YMIME_TYPE_MARKDOWN,
    YETTY_YMIME_TYPE_PDF,
    YETTY_YMIME_TYPE_IMAGE, /* raster family (png/jpeg/gif/… — stb set) */
    YETTY_YMIME_TYPE_SVG,
    YETTY_YMIME_TYPE_VIDEO,   /* H.264 Annex-B */
    YETTY_YMIME_TYPE_MUSIC,   /* LilyPond score (engraved via ymusic) */
    YETTY_YMIME_TYPE_CIRCUIT, /* ycircuit schematic DSL */
    YETTY_YMIME_TYPE_MESH,    /* glTF binary (GLB) — rendered via ymesh */
    YETTY_YMIME_TYPE_DOCX,    /* Word OOXML — rendered via ymsoffice */
    YETTY_YMIME_TYPE_XLSX,    /* Excel OOXML — rendered via ymsoffice */
    YETTY_YMIME_TYPE_PPTX,    /* PowerPoint OOXML — rendered via ymsoffice */
};

/* Short type name — the per-type config key segment (mime/types/<name>/…)
 * and the log token. "svg", "markdown", "pdf", "image", "video", "music",
 * "circuit", "mesh", "text", "unknown". */
const char *yetty_ymime_type_name(enum yetty_ymime_type type);

/* Canonical MIME family string for a type — what a sender puts in the
 * prologue hint when it detected locally (the raster family maps to the
 * wildcard "image" family string; the receiver maps any image/ prefix back
 * to IMAGE). NULL for UNKNOWN. */
const char *yetty_ymime_type_canonical_mime(enum yetty_ymime_type type);

/*=============================================================================
 * Detection
 *===========================================================================*/

/* Classify a MIME string. Unrecognized → UNKNOWN. */
enum yetty_ymime_type yetty_ymime_type_from_mime(const char *mime);

/* Classify by filename extension. Accepts a bare extension (".md" / "md")
 * or a full path/filename. Case-insensitive. */
enum yetty_ymime_type yetty_ymime_type_from_extension(const char *path_or_extension);

/* Content sniff over the leading bytes. YETTY_YMIME_SNIFF_WINDOW bytes are
 * sufficient; extra bytes are ignored. */
#define YETTY_YMIME_SNIFF_WINDOW 4096u
enum yetty_ymime_type yetty_ymime_sniff(const uint8_t *bytes, size_t len);

/* Combined detection: MIME hint, then extension hint, then sniff (see the
 * header comment for the exact precedence). Any argument may be NULL/0. */
enum yetty_ymime_type yetty_ymime_detect(const char *mime_hint, const char *name_hint,
                                         const uint8_t *bytes, size_t len);

/*=============================================================================
 * File-envelope prologue codec
 *===========================================================================*/

/* Hard bound on the whole prologue; a decoder rejects anything larger, so
 * a receiver can size its sniff-window buffer as
 * YETTY_YMIME_PROLOGUE_MAX + YETTY_YMIME_SNIFF_WINDOW. */
#define YETTY_YMIME_PROLOGUE_MAX 4096u

/* Decoded prologue. String views point INTO the caller's buffer (not
 * NUL-terminated); lengths are authoritative. On encode, NULL pointers
 * with zero lengths are valid empty fields. */
struct yetty_ymime_prologue {
    const char *mime;
    size_t mime_len; /* <= 255 */
    const char *name;
    size_t name_len; /* <= 255 */
    const char *args;
    size_t args_len;
};

/* Encode into dst. Returns the encoded size. */
struct yetty_ycore_size_result yetty_ymime_prologue_encode(
    const struct yetty_ymime_prologue *prologue, uint8_t *dst, size_t dst_cap);

/* Decode from the head of src. Returns the total prologue size (the offset
 * at which the file bytes start); fails on a truncated or oversized
 * prologue. Unknown trailing fields inside prologue_size are skipped. */
struct yetty_ycore_size_result yetty_ymime_prologue_decode(const uint8_t *src, size_t len,
                                                           struct yetty_ymime_prologue *out);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YMIME_MIME_H */
