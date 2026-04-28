#ifndef YETTY_YRICH_YSLIDES_H
#define YETTY_YRICH_YSLIDES_H

/*
 * yslides — slideshow/presentation document.
 *
 * A slideshow is a list of slides; each slide owns a flat z-ordered list of
 * shapes. Shapes are concrete element subclasses (rectangle, ellipse, line,
 * text-box, image).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>
#include <yetty/yrich/yrich-document.h>
#include <yetty/yrich/yrich-element.h>
#include <yetty/yrich/yrich-types.h>

#ifdef __cplusplus
extern "C" {
#endif

enum yetty_yrich_shape_kind {
    YETTY_YRICH_SHAPE_RECTANGLE = 0,
    YETTY_YRICH_SHAPE_ELLIPSE,
    YETTY_YRICH_SHAPE_TEXTBOX,
    YETTY_YRICH_SHAPE_LINE,
    YETTY_YRICH_SHAPE_ARROW,
    YETTY_YRICH_SHAPE_IMAGE,
};

/*=============================================================================
 * Shape — common base for all slide elements.
 *===========================================================================*/

struct yetty_yrich_shape {
    struct yetty_yrich_element base;

    uint32_t kind;
    struct yetty_yrich_rect bounds;

    uint32_t fill_color;
    uint32_t stroke_color;
    float stroke_width;
    float rotation; /* degrees */
    float corner_radius;

    /* Text-box only — kept here so a single Shape allocation covers all
	 * shape kinds without needing per-kind subtypes. */
    char *text; /* owned, may be NULL */
    size_t text_len;
    struct yetty_yrich_text_style text_style;
    uint32_t text_align;  /* enum yetty_yrich_halign */
    uint32_t text_valign; /* enum yetty_yrich_valign */

    bool editing;
    int32_t cursor_pos;
    int32_t sel_start;
    int32_t sel_end;

    /* Image only. */
    char *image_source; /* owned base64/path, may be NULL */
    bool preserve_aspect;
};

YETTY_YRESULT_DECLARE(yetty_yrich_shape_ptr, struct yetty_yrich_shape *);

struct yetty_yrich_shape_ptr_result yetty_yrich_shape_create(yetty_yrich_element_id id,
                                                             uint32_t kind,
                                                             struct yetty_yrich_rect bounds);

void yetty_yrich_shape_set_text(struct yetty_yrich_shape *s, const char *text, size_t len);

/*=============================================================================
 * Slide — owns a flat list of shapes (aliasing pointers; the document owns).
 *===========================================================================*/

struct yetty_yrich_slide {
    int32_t index;
    uint32_t bg_color;

    struct yetty_yrich_shape **shapes; /* alias pointers; document owns */
    size_t shape_count;
    size_t shape_capacity;
};

/*=============================================================================
 * Slideshow document
 *===========================================================================*/

struct yetty_yrich_slides {
    struct yetty_yrich_document base;

    float slide_width;
    float slide_height;

    struct yetty_yrich_slide **slides;
    size_t slide_count;
    size_t slide_capacity;

    int32_t current_slide;

    struct yetty_yrich_shape *editing_shape; /* aliases */

    bool dragging;
    float drag_start_x, drag_start_y;
    float drag_offset_x, drag_offset_y;

    bool presentation_mode;
};

YETTY_YRESULT_DECLARE(yetty_yrich_slides_ptr, struct yetty_yrich_slides *);

struct yetty_yrich_slides_ptr_result yetty_yrich_slides_create(void);

void yetty_yrich_slides_set_slide_size(struct yetty_yrich_slides *s, float w, float h);

/*=============================================================================
 * Slide management
 *===========================================================================*/

struct yetty_yrich_slide *yetty_yrich_slides_add_slide(struct yetty_yrich_slides *s);

struct yetty_yrich_slide *yetty_yrich_slides_slide_at(const struct yetty_yrich_slides *s,
                                                      int32_t index);

void yetty_yrich_slides_set_current(struct yetty_yrich_slides *s, int32_t index);

void yetty_yrich_slides_next(struct yetty_yrich_slides *s);
void yetty_yrich_slides_prev(struct yetty_yrich_slides *s);

/*=============================================================================
 * Shape creation on current slide. The slideshow takes ownership of the new
 * shape (via the document's element list); the returned pointer aliases.
 *===========================================================================*/

struct yetty_yrich_shape_ptr_result yetty_yrich_slides_add_rectangle(struct yetty_yrich_slides *s,
                                                                     float x, float y, float w,
                                                                     float h);

struct yetty_yrich_shape_ptr_result yetty_yrich_slides_add_ellipse(struct yetty_yrich_slides *s,
                                                                   float x, float y, float w,
                                                                   float h);

struct yetty_yrich_shape_ptr_result yetty_yrich_slides_add_textbox(struct yetty_yrich_slides *s,
                                                                   float x, float y, float w,
                                                                   float h, const char *text,
                                                                   size_t text_len);

struct yetty_yrich_shape_ptr_result yetty_yrich_slides_add_line(struct yetty_yrich_slides *s,
                                                                float x1, float y1, float x2,
                                                                float y2);

struct yetty_yrich_shape_ptr_result yetty_yrich_slides_add_image(struct yetty_yrich_slides *s,
                                                                 float x, float y, float w,
                                                                 float h);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YRICH_YSLIDES_H */
