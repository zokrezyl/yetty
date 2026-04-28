#ifndef YETTY_YPDF_YPDF_H
#define YETTY_YPDF_YPDF_H

/*
 * ypdf - render a PDF document into a ypaint buffer.
 *
 * The renderer:
 *   - walks pages once up-front to compute scene bounds from MediaBoxes
 *   - creates the ypaint buffer pre-configured with those bounds
 *   - extracts embedded TTF fonts (FontFile2 / FontFile3) and registers them
 *   - parses each content stream through the ypdf content parser, emitting
 *     text spans, axis-aligned box primitives (SDF type Box) and line segments
 *     (SDF type Segment) into the buffer
 *
 * The result carries the buffer ownership; caller frees it via
 * yetty_ypaint_core_buffer_destroy.
 */

#include <stddef.h>
#include <stdint.h>
#include <yetty/ycore/result.h>
#include <yetty/ypaint-core/buffer.h>

/* Forward declare pdfio file (C API). */
struct _pdfio_file_s;
typedef struct _pdfio_file_s pdfio_file_t;

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ypdf_render_output {
    struct yetty_ypaint_core_buffer *buffer;
    int page_count;
    float total_height;
    float first_page_height;
    float max_width;
};

YETTY_YRESULT_DECLARE(yetty_ypdf_render, struct yetty_ypdf_render_output);

/* Render pdf into a fresh ypaint buffer. On success the caller owns
 * result.value.buffer. */
struct yetty_ypdf_render_result yetty_ypdf_render_pdf(pdfio_file_t *pdf);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPDF_YPDF_H */
