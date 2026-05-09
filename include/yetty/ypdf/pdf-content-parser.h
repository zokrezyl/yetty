#ifndef YETTY_YPDF_PDF_CONTENT_PARSER_H
#define YETTY_YPDF_PDF_CONTENT_PARSER_H

/*
 * PDF content stream parser.
 *
 * Drives a stateful interpreter over PDF graphics/text operators and emits
 * three kinds of events through caller-supplied callbacks sharing one
 * user_data pointer:
 *   - text_emit   : each decoded text fragment with its effective size,
 *                   rotation, and text state; callback returns the horizontal
 *                   advance (in text-space units) so the parser can update the
 *                   text matrix for the next fragment.
 *   - rect_paint  : an axis-aligned rectangle painted via re + S/f/B.
 *   - line_paint  : a single line segment from a general path (m/l/h/S).
 *
 * Callers set page_height before parsing each page; PDF user space has its
 * origin at the bottom-left, screen conventions are top-down.
 */

#include <stddef.h>
#include <stdint.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

/* Forward declare pdfio stream (C API). */
struct _pdfio_stream_s;

#ifdef __cplusplus
extern "C" {
#endif

/*=============================================================================
 * Types exposed to callbacks
 *===========================================================================*/

/* Text state per PDF spec 9.3 (snapshot passed to the text callback).
 * fill_r/g/b are pulled from the graphics state — PDF text colour is the
 * non-stroking colour set via rg / RG / g / G / k / sc / scn. Without this
 * the renderer can't tell black-on-white body text from coloured runs. */
struct yetty_ypdf_text_state {
    float char_spacing;           /* Tc */
    float word_spacing;           /* Tw */
    float horizontal_scaling;     /* Tz — percentage (default 100) */
    float leading;                /* TL */
    float font_size;              /* Tfs from Tf */
    float rise;                   /* Ts */
    const char *font_name;        /* PDF font tag, e.g. "/F1" */
    float fill_r, fill_g, fill_b; /* non-stroking RGB, [0..1] */
};

enum yetty_ypdf_paint_mode {
    YETTY_YPDF_PAINT_STROKE = 0,
    YETTY_YPDF_PAINT_FILL = 1,
    YETTY_YPDF_PAINT_FILL_AND_STROKE = 2,
};

/*=============================================================================
 * Callback signatures
 *===========================================================================*/

/* Text emission. text points to UTF-8 bytes (not null-terminated). Returns
 * the advance to apply to the text matrix, in text-space units (i.e. divided
 * by font_size if you measured pixels at that size). */
typedef struct float_result (*yetty_ypdf_text_emit_fn)(void *user_data, const char *text,
                                                       size_t text_len, float pos_x, float pos_y,
                                                       float effective_size, float rotation_radians,
                                                       const struct yetty_ypdf_text_state *state);

typedef void (*yetty_ypdf_rect_paint_fn)(void *user_data, float x, float y, float w, float h,
                                         enum yetty_ypdf_paint_mode mode, float stroke_r,
                                         float stroke_g, float stroke_b, float fill_r, float fill_g,
                                         float fill_b, float line_width);

typedef void (*yetty_ypdf_line_paint_fn)(void *user_data, float x0, float y0, float x1, float y1,
                                         float r, float g, float b, float line_width);

struct yetty_ypdf_content_parser_callbacks {
    yetty_ypdf_text_emit_fn text_emit;
    yetty_ypdf_rect_paint_fn rect_paint;
    yetty_ypdf_line_paint_fn line_paint;
    void *user_data;
};

/*=============================================================================
 * Parser lifecycle
 *===========================================================================*/

struct yetty_ypdf_content_parser;

YETTY_YRESULT_DECLARE(yetty_ypdf_content_parser_ptr, struct yetty_ypdf_content_parser *);

struct yetty_ypdf_content_parser_ptr_result
yetty_ypdf_content_parser_callbacks_content_parser_create(
    const struct yetty_ypdf_content_parser_callbacks *cb);

void yetty_ypdf_content_parser_destroy(struct yetty_ypdf_content_parser *p);

/* Set page height (for callers that need to y-flip in callbacks). */
void yetty_ypdf_content_parser_set_page_height(struct yetty_ypdf_content_parser *p, float h);

/* Parse a single PDF content stream. May be called multiple times on one
 * parser — state persists only for the lifetime of the current page's
 * streams (caller resets state between pages by creating a new parser). */
struct yetty_ycore_void_result yetty_ypdf_content_parser_parse_stream(
    struct yetty_ypdf_content_parser *p, struct _pdfio_stream_s *stream);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPDF_PDF_CONTENT_PARSER_H */
