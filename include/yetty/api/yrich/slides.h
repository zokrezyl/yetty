/* GENERATED — do not edit. */
/* Object API for regular class(es) `shape, slides` (implementation module: yrich).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YRICH_SLIDES_H
#define YETTY_YCLASSGEN_API_YRICH_SLIDES_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yrich/yrich-types.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YRICH_SHAPE_KIND
#define YETTY_YCLASSGEN_TYPE_YETTY_YRICH_SHAPE_KIND
/* Shape kinds — exposed in the generated slides.h. */
enum yetty_yrich_shape_kind {
    YETTY_YRICH_SHAPE_RECTANGLE = 0,
    YETTY_YRICH_SHAPE_ELLIPSE = 1,
    YETTY_YRICH_SHAPE_TEXTBOX = 2,
    YETTY_YRICH_SHAPE_LINE = 3,
    YETTY_YRICH_SHAPE_ARROW = 4,
    YETTY_YRICH_SHAPE_IMAGE = 5,
};
#endif

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yrich_shape;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YRICH_SHAPE_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YRICH_SHAPE_PTR_RESULT
struct yetty_yrich_shape_ptr_result {
    int ok;
    union {
        struct yetty_yrich_shape *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_yrich_shape_ptr_result yetty_yrich_shape_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yrich_shape_to(struct yetty_yrich_shape *data);
struct uint32_result yetty_yrich_shape_fill_color_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_shape_fill_color_set(struct yetty_yclass_object *obj,
                                                                uint32_t value);
struct uint32_result yetty_yrich_shape_stroke_color_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_shape_stroke_color_set(struct yetty_yclass_object *obj,
                                                                  uint32_t value);
struct float_result yetty_yrich_shape_stroke_width_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_shape_stroke_width_set(struct yetty_yclass_object *obj,
                                                                  float value);
struct float_result yetty_yrich_shape_rotation_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_shape_rotation_set(struct yetty_yclass_object *obj,
                                                              float value);
struct float_result yetty_yrich_shape_corner_radius_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_shape_corner_radius_set(struct yetty_yclass_object *obj,
                                                                   float value);
struct uint32_result yetty_yrich_shape_text_align_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_shape_text_align_set(struct yetty_yclass_object *obj,
                                                                uint32_t value);
struct uint32_result yetty_yrich_shape_text_valign_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_shape_text_valign_set(struct yetty_yclass_object *obj,
                                                                 uint32_t value);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yrich_slides;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YRICH_SLIDES_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YRICH_SLIDES_PTR_RESULT
struct yetty_yrich_slides_ptr_result {
    int ok;
    union {
        struct yetty_yrich_slides *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_yrich_slides_ptr_result yetty_yrich_slides_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yrich_slides_to(struct yetty_yrich_slides *data);

struct yetty_ycore_void_result yetty_yrich_slides_set_current(struct yetty_yclass_object *obj,
                                                              int32_t index);
struct yetty_ycore_void_result yetty_yrich_slides_next(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_slides_prev(struct yetty_yclass_object *obj);

struct yetty_yclass_object_ptr_result yetty_yrich_shape_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_yrich_slides_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_yrich_shape_set_text(struct yetty_yclass_object *obj,
                                                          const char *text, size_t len);
struct yetty_ycore_void_result yetty_yrich_shape_set_font_size(struct yetty_yclass_object *obj,
                                                               float font_size);
struct yetty_ycore_void_result yetty_yrich_shape_set_text_color(struct yetty_yclass_object *obj,
                                                                uint32_t color);
struct yetty_ycore_void_result yetty_yrich_shape_set_image_source(struct yetty_yclass_object *obj,
                                                                  const char *source);
struct yetty_ycore_void_result yetty_yrich_slides_set_slide_size(struct yetty_yclass_object *obj,
                                                                 float width, float height);
struct yetty_yrich_slide_ptr_result yetty_yrich_slides_add_slide(struct yetty_yclass_object *obj);
struct yetty_yrich_slide_ptr_result yetty_yrich_slides_slide_at(struct yetty_yclass_object *obj,
                                                                int32_t index);
struct yetty_ycore_int_result yetty_yrich_slides_current(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yrich_slides_add_rectangle(
    struct yetty_yclass_object *obj, float x, float y, float width, float height);
struct yetty_yclass_object_ptr_result yetty_yrich_slides_add_ellipse(
    struct yetty_yclass_object *obj, float x, float y, float width, float height);
struct yetty_yclass_object_ptr_result yetty_yrich_slides_add_textbox(
    struct yetty_yclass_object *obj, float x, float y, float width, float height, const char *text,
    size_t text_len);
struct yetty_yclass_object_ptr_result yetty_yrich_slides_add_line(struct yetty_yclass_object *obj,
                                                                  float x1, float y1, float x2,
                                                                  float y2);
struct yetty_yclass_object_ptr_result yetty_yrich_slides_add_image(struct yetty_yclass_object *obj,
                                                                   float x, float y, float width,
                                                                   float height);

#ifdef __cplusplus
}
#endif

#endif
