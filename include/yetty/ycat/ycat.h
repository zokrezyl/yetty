#ifndef YETTY_YCAT_YCAT_H
#define YETTY_YCAT_YCAT_H

/*
 * ycat - MIME-dispatched cat.
 *
 * Detects the type of a byte buffer (libmagic + extension fallback) and
 * dispatches to a handler that turns the bytes into a ydraw-core buffer.
 * The caller then either:
 *   - base64-encodes the ydraw primitives and emits an OSC 666674 sequence
 *     (yetty_ycat_osc_bin_emit), so a running yetty terminal picks it up
 *     and routes to its ydraw scrolling layer, or
 *   - for plain-text / unknown input, passes the bytes through unchanged.
 *
 * The library is pure C. The tool in tools/ycat drives it.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <yetty/ycore/result.h>
#include <yetty/ydraw-core/drawable-list.h>

#ifdef __cplusplus
extern "C" {
#endif

/*=============================================================================
 * Detected file type
 *===========================================================================*/

enum yetty_ycat_type {
    YETTY_YCAT_TYPE_UNKNOWN = 0,
    YETTY_YCAT_TYPE_TEXT,
    YETTY_YCAT_TYPE_MARKDOWN,
    YETTY_YCAT_TYPE_PDF,
    YETTY_YCAT_TYPE_IMAGE,
    YETTY_YCAT_TYPE_SVG,
    YETTY_YCAT_TYPE_MERMAID,
    YETTY_YCAT_TYPE_VIDEO,     /* H.264 Annex-B (#198 item 6) */
    YETTY_YCAT_TYPE_LOTTIE,    /* Lottie (Bodymovin) JSON — single-frame render */
    YETTY_YCAT_TYPE_MUSIC,     /* LilyPond score (.ly) — engraved via ymusic */
    YETTY_YCAT_TYPE_SHADERTOY, /* WGSL mainImage shader — animated yshadertoy prim */
    YETTY_YCAT_TYPE_CIRCUIT,   /* circuit schematic DSL — drawn via ycircuit */
    YETTY_YCAT_TYPE_CHART,     /* CSV/JSON/YAML chart data — drawn via ychart */
    YETTY_YCAT_TYPE_DOCX,      /* Word OOXML — rendered via ymsoffice */
    YETTY_YCAT_TYPE_XLSX,      /* Excel OOXML — rendered via ymsoffice */
    YETTY_YCAT_TYPE_PPTX,      /* PowerPoint OOXML — rendered via ymsoffice */
    YETTY_YCAT_TYPE_PLOT,      /* numeric series (.npy / data columns) — drawn via yplot */
};

const char *yetty_ycat_type_name(enum yetty_ycat_type type);
enum yetty_ycat_type yetty_ycat_type_from_name(const char *name);

/*=============================================================================
 * Detection
 *===========================================================================*/

/* Detect type from MIME string. */
enum yetty_ycat_type yetty_ycat_type_from_mime(const char *mime);

/* Detect type from file extension (e.g. ".md"). Case-insensitive. */
enum yetty_ycat_type yetty_ycat_type_from_extension(const char *ext);

/* Detect from bytes (optionally with a path for extension fallback).
 * Uses libmagic when compiled with YETTY_YCAT_HAS_LIBMAGIC, otherwise
 * extension-only. path may be NULL for stdin. */
enum yetty_ycat_type yetty_ycat_detect(const uint8_t *bytes, size_t len, const char *path);

/*=============================================================================
 * Rendering
 *===========================================================================*/

struct yetty_ycat_config {
    uint32_t cell_width;
    uint32_t cell_height;
    uint32_t width_cells;
    uint32_t height_cells;
};

/* Handler signature: bytes+len (and optionally a path for formats that need
 * one, e.g. PDF via pdfio) → fresh ydraw-core buffer. Returned buffer
 * ownership is transferred to the caller.
 *
 * path_hint may be NULL (for stdin / URL). If the handler needs a real file
 * and path_hint is NULL, it is expected to spill to a temp file. */
typedef struct yetty_ydraw_drawable_list_result (*yetty_ycat_handler_fn)(
    const uint8_t *bytes, size_t len, const char *path_hint,
    const struct yetty_ycat_config *config);

/* Registry lookup. Returns NULL for types with no handler (TEXT / UNKNOWN). */
yetty_ycat_handler_fn yetty_ycat_get_handler(enum yetty_ycat_type type);

/* Register / override a handler at runtime (used to plug in later types
 * like tree-sitter syntax highlighting). Returns 0 on success, -1 on a bad
 * type argument. */
int yetty_ycat_register_handler(enum yetty_ycat_type type, yetty_ycat_handler_fn fn);

/* One-shot: detect → render. */
struct yetty_ydraw_drawable_list_result yetty_ycat_render(const uint8_t *bytes, size_t len,
                                                          const char *path_hint,
                                                          const struct yetty_ycat_config *config);

/*=============================================================================
 * Streaming handlers — for types whose natural unit is smaller than the
 * whole document (PDF: per page, markdown: per screen-height tile, …).
 *
 * The handler emits one or more ydraw envelopes by invoking the supplied
 * emit callback. Each envelope is independent; coordinates inside it are
 * envelope-local (origin at y=0), and the receiver scrolls by the
 * envelope's scene height between calls.
 *
 * Single-shot handlers (image, svg, mermaid) keep using
 * yetty_ycat_handler_fn — the dispatch path picks streaming first and
 * falls back to single-shot if no streaming handler is registered.
 *===========================================================================*/

/* emit one envelope. The envelope is borrowed for the call's duration; the
 * handler must not retain the pointer. Return an error to abort the
 * streaming render. */
typedef struct yetty_ycore_void_result (*yetty_ycat_emit_fn)(
    void *user_data, const struct yetty_ydraw_drawable_list *envelope);

typedef struct yetty_ycore_void_result (*yetty_ycat_handler_streaming_fn)(
    const uint8_t *bytes, size_t len, const char *path_hint, const struct yetty_ycat_config *config,
    yetty_ycat_emit_fn emit, void *emit_user_data);

yetty_ycat_handler_streaming_fn yetty_ycat_get_handler_streaming(enum yetty_ycat_type type);

int yetty_ycat_register_handler_streaming(enum yetty_ycat_type type,
                                          yetty_ycat_handler_streaming_fn fn);

/*=============================================================================
 * Tree-sitter direct access — two emitters, shared parser+color-map.
 *===========================================================================*/

/* Pick a tree-sitter grammar name from mime or path (extension). NULL if
 * neither yields a supported grammar. */
const char *yetty_ycat_grammar_lookup(const char *mime, const char *path);

/* Parse bytes with `grammar_name`, emit coloured spans into a fresh ydraw
 * buffer. Useful when targeting a yetty terminal (via the OSC envelope). */
struct yetty_ydraw_drawable_list_result yetty_ycat_ts_render(
    const uint8_t *bytes, size_t len, const char *grammar_name,
    const struct yetty_ycat_config *config);

/* Parse bytes with `grammar_name`, emit 24-bit SGR-coloured source text to
 * `out`. Works on any terminal. Returns 0 on success, -1 on failure. */
int yetty_ycat_ts_emit_sgr(const uint8_t *bytes, size_t len, const char *grammar_name, FILE *out);

/*=============================================================================
 * URL fetching
 *===========================================================================*/

/* Return 1 if s starts with "http://" or "https://". */
int yetty_ycat_is_url(const char *s);

/* Fetch url into *out / *out_len (malloc'd, caller frees). On success,
 * optionally stores the Content-Type MIME (before ';') in content_type_out
 * (caller must free if non-NULL). Returns 0 on success, -1 on error. */
int yetty_ycat_fetch_url(const char *url, uint8_t **out, size_t *out_len, char **content_type_out);

/*=============================================================================
 * OSC emission
 *===========================================================================*/

/* Emit an OSC 666674 (YDRAW_SCROLL) sequence wrapping the buffer's primitive
 * bytes (base64-encoded, `--bin` format) to `out`. Returns number of bytes
 * written, 0 on failure. */
struct yetty_ycore_size_result yetty_ycat_osc_bin_emit(
    const struct yetty_ydraw_drawable_list *buffer, FILE *out);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YCAT_YCAT_H */
