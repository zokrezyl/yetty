#ifndef YETTY_YPDF_YPDF_H
#define YETTY_YPDF_YPDF_H

/*
 * ypdf - render a PDF document into a ydraw buffer.
 *
 * The renderer:
 *   - walks pages once up-front to compute scene bounds from MediaBoxes
 *   - creates the ydraw buffer pre-configured with those bounds
 *   - extracts embedded TTF fonts (FontFile2 / FontFile3) and registers them
 *   - parses each content stream through the ypdf content parser, emitting
 *     text spans, axis-aligned box primitives (SDF type Box) and line segments
 *     (SDF type Segment) into the buffer
 *
 * The result carries the buffer ownership; caller frees it via
 * yetty_ydraw_draw_list_destroy.
 */

#include <stddef.h>
#include <stdint.h>
#include <yetty/ycore/result.h>
#include <yetty/ydraw-core/draw-list.h>

/* Forward declare pdfio file (C API). */
struct _pdfio_file_s;

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ypdf_render_output {
    struct yetty_ydraw_draw_list *buffer;
    int page_count;
    float total_height;
    float first_page_height;
    float max_width;
};

YETTY_YRESULT_DECLARE(yetty_ypdf_render, struct yetty_ypdf_render_output);

/* Render pdf into a fresh ydraw buffer. On success the caller owns
 * result.value.buffer. */
struct yetty_ypdf_render_result yetty_ypdf_render_pdf(struct _pdfio_file_s *pdf);

/*=============================================================================
 * Streaming render — one envelope per page.
 *
 * Per-page coordinates are envelope-relative (page origin at y=0); the
 * receiver scrolls by each envelope's scene_max_y, so stacking pages just
 * works without the producer maintaining absolute Y.
 *
 * Fonts are content-addressed by FNV1a64(TTF bytes). On the first envelope
 * that references a given font the full FONT prim (with TTF bytes) is
 * emitted; subsequent envelopes referencing the same font emit a hash-only
 * FONT prim (ttf_len = 0, name = 16-char hex). The receiver's on-disk MSDF
 * cache is keyed by the same hash, so byte payload is only ever sent once
 * per document.
 *
 * The callback is invoked synchronously per page. `envelope` is borrowed
 * for the duration of the call; the renderer destroys it right after.
 * Return an error result to abort the render.
 *===========================================================================*/

typedef struct yetty_ycore_void_result (*yetty_ypdf_page_emit_fn)(
    void *user_data, int page_index, int page_count,
    const struct yetty_ydraw_draw_list *envelope);

struct yetty_ypdf_stream_render_output {
    int page_count;
    float first_page_height;
    float max_width;
};

YETTY_YRESULT_DECLARE(yetty_ypdf_stream_render, struct yetty_ypdf_stream_render_output);

struct yetty_ypdf_stream_render_result yetty_ypdf_render_pdf_streaming(
    struct _pdfio_file_s *pdf, yetty_ypdf_page_emit_fn on_page, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPDF_YPDF_H */
