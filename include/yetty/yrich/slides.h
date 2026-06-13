/* GENERATED — do not edit. */
/* Public interface for regular class(es) `shape, slides` (module: yrich).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), and any
 * `expose`d API. Public types come from `expose` annotations. */
#ifndef YETTY_YCLASSGEN_YRICH_SLIDES_H
#define YETTY_YCLASSGEN_YRICH_SLIDES_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

struct yetty_yclass_ptr_result yetty_yrich_shape_class_get(void);
struct yetty_yclass_ptr_result yetty_yrich_slides_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yrich_shape;
YETTY_YRESULT_DECLARE(yetty_yrich_shape_ptr, struct yetty_yrich_shape *);
struct yetty_yrich_shape_ptr_result yetty_yrich_shape_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object *yetty_yrich_shape_to(struct yetty_yrich_shape *data);
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
YETTY_YRESULT_DECLARE(yetty_yrich_slides_ptr, struct yetty_yrich_slides *);
struct yetty_yrich_slides_ptr_result yetty_yrich_slides_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object *yetty_yrich_slides_to(struct yetty_yrich_slides *data);

struct yetty_ycore_void_result;

struct yetty_ycore_void_result yetty_yrich_slides_set_current(struct yetty_yclass_ctx *ctx,
                                                              struct yetty_yclass_object *obj,
                                                              int32_t index);
struct yetty_ycore_void_result yetty_yrich_slides_next(struct yetty_yclass_ctx *ctx,
                                                       struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_slides_prev(struct yetty_yclass_ctx *ctx,
                                                       struct yetty_yclass_object *obj);

typedef struct yetty_ycore_void_result (*yetty_yrich_slides_set_current_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, int32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_slides_next_fn)(struct yetty_yclass_ctx *,
                                                                     struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_slides_prev_fn)(struct yetty_yclass_ctx *,
                                                                     struct yetty_yclass_object *);

struct yetty_yclass_object_ptr_result yetty_yrich_shape_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_yrich_slides_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_yrich_register(void);

struct yetty_yrich_slide;

/* Header-destined content for the generated slides.h (skipped by the real
 * build, which defines the enum in the #ifndef block above). The types
 * include carries the slide aggregate + its Result the slide-management
 * API above references. */
#include <yetty/yrich/yrich-types.h>

enum yetty_yrich_shape_kind {
    YETTY_YRICH_SHAPE_RECTANGLE = 0,
    YETTY_YRICH_SHAPE_ELLIPSE,
    YETTY_YRICH_SHAPE_TEXTBOX,
    YETTY_YRICH_SHAPE_LINE,
    YETTY_YRICH_SHAPE_ARROW,
    YETTY_YRICH_SHAPE_IMAGE,
};
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
struct yetty_yrich_slide *yetty_yrich_slides_slide_at(struct yetty_yclass_object *obj,
                                                      int32_t index);
int32_t yetty_yrich_slides_current(struct yetty_yclass_object *obj);
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

#endif
