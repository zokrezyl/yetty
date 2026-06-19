/* GENERATED — do not edit. */
/* Public interface for regular class(es) `paragraph, inline_image, ydoc` (module: yrich).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YRICH_YDOC_H
#define YETTY_YCLASSGEN_YRICH_YDOC_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yrich/yrich-types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yclass_ptr_result yetty_yrich_paragraph_class_get(void);
struct yetty_yclass_ptr_result yetty_yrich_inline_image_class_get(void);
struct yetty_yclass_ptr_result yetty_yrich_ydoc_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yrich_paragraph;
struct yetty_yrich_paragraph_ptr_result {
    int ok;
    union {
        struct yetty_yrich_paragraph *value;
        struct yetty_ycore_error error;
    };
};
struct yetty_yrich_paragraph_ptr_result yetty_yrich_paragraph_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object *yetty_yrich_paragraph_to(struct yetty_yrich_paragraph *data);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yrich_inline_image;
struct yetty_yrich_inline_image_ptr_result {
    int ok;
    union {
        struct yetty_yrich_inline_image *value;
        struct yetty_ycore_error error;
    };
};
struct yetty_yrich_inline_image_ptr_result yetty_yrich_inline_image_from(
    struct yetty_yclass_object *obj);
struct yetty_yclass_object *yetty_yrich_inline_image_to(struct yetty_yrich_inline_image *data);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yrich_ydoc;
struct yetty_yrich_ydoc_ptr_result {
    int ok;
    union {
        struct yetty_yrich_ydoc *value;
        struct yetty_ycore_error error;
    };
};
struct yetty_yrich_ydoc_ptr_result yetty_yrich_ydoc_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object *yetty_yrich_ydoc_to(struct yetty_yrich_ydoc *data);

struct yetty_ycore_void_result yetty_yrich_ydoc_toggle_format(struct yetty_yclass_object *obj,
                                                              uint32_t format_flag);
/* Text colour — selection-scoped like toggle_format, absolute (no toggle). */
struct yetty_ycore_void_result yetty_yrich_ydoc_set_text_color(struct yetty_yclass_object *obj,
                                                               uint32_t color);
/* Paragraph alignment (enum yetty_yrich_halign). */
struct yetty_ycore_void_result yetty_yrich_ydoc_set_alignment(struct yetty_yclass_object *obj,
                                                              uint32_t halign);
/* Heading levels — 0 = normal text, 1..3 = headings (size + bold base). */
struct yetty_ycore_void_result yetty_yrich_ydoc_set_heading(struct yetty_yclass_object *obj,
                                                            uint32_t level);
struct yetty_ycore_void_result yetty_yrich_ydoc_change_font_size(struct yetty_yclass_object *obj,
                                                                 float delta);

typedef struct yetty_ycore_void_result (*yetty_yrich_ydoc_toggle_format_fn)(
    struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_ydoc_set_text_color_fn)(
    struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_ydoc_set_alignment_fn)(
    struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_ydoc_set_heading_fn)(
    struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_ydoc_change_font_size_fn)(
    struct yetty_yclass_object *, float);

struct yetty_yclass_object_ptr_result yetty_yrich_paragraph_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_yrich_inline_image_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_yrich_ydoc_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_yrich_register(void);

struct yetty_ycore_void_result yetty_yrich_paragraph_set_text(struct yetty_yclass_object *obj,
                                                              const char *text, size_t len);
/* Style setters — the data slice is private to this TU; the yaml loader and
 * future bindings reach the style through these. */
struct yetty_ycore_void_result yetty_yrich_paragraph_set_font_size(struct yetty_yclass_object *obj,
                                                                   float font_size);
struct yetty_ycore_void_result yetty_yrich_paragraph_set_color(struct yetty_yclass_object *obj,
                                                               uint32_t color);
struct yetty_ycore_void_result yetty_yrich_paragraph_set_format(struct yetty_yclass_object *obj,
                                                                uint32_t format);
struct yetty_ycore_void_result yetty_yrich_ydoc_set_page_width(struct yetty_yclass_object *obj,
                                                               float width);
struct yetty_ycore_void_result yetty_yrich_ydoc_set_margin(struct yetty_yclass_object *obj,
                                                           float margin);
struct yetty_yclass_object_ptr_result yetty_yrich_ydoc_add_paragraph(
    struct yetty_yclass_object *obj, const char *text, size_t text_len);
struct yetty_yclass_object *yetty_yrich_ydoc_paragraph_at(struct yetty_yclass_object *obj,
                                                          int32_t index);
struct yetty_yclass_object_ptr_result yetty_yrich_ydoc_insert_image(struct yetty_yclass_object *obj,
                                                                    int32_t paragraph_index,
                                                                    float width, float height);
/* Selected text as a fresh heap string (caller frees). NULL when the
 * selection is empty or not a text selection. */
char *yetty_yrich_ydoc_selection_text(struct yetty_yclass_object *obj);
/* Drop every element and start over with one empty paragraph (File > New).
 * The undo history is cleared — its ops reference dead elements. */
struct yetty_ycore_void_result yetty_yrich_ydoc_clear(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_ydoc_set_source_path(struct yetty_yclass_object *obj,
                                                                const char *path);
const char *yetty_yrich_ydoc_source_path(struct yetty_yclass_object *obj);
struct yetty_ycore_float_result yetty_yrich_ydoc_page_width(struct yetty_yclass_object *obj);
struct yetty_ycore_float_result yetty_yrich_ydoc_margin(struct yetty_yclass_object *obj);
struct yetty_ycore_size_result yetty_yrich_ydoc_paragraph_count(struct yetty_yclass_object *obj);
const char *yetty_yrich_paragraph_text(struct yetty_yclass_object *obj);
struct yetty_ycore_size_result yetty_yrich_paragraph_text_len(struct yetty_yclass_object *obj);
struct yetty_ycore_float_result yetty_yrich_paragraph_font_size(struct yetty_yclass_object *obj);
struct yetty_ycore_uint32_result yetty_yrich_paragraph_color(struct yetty_yclass_object *obj);
struct yetty_ycore_uint32_result yetty_yrich_paragraph_format(struct yetty_yclass_object *obj);
struct yetty_ycore_uint32_result yetty_yrich_paragraph_alignment(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_paragraph_set_alignment(struct yetty_yclass_object *obj,
                                                                   uint32_t halign);
struct yetty_ycore_size_result yetty_yrich_paragraph_run_count(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_paragraph_run_get(struct yetty_yclass_object *obj,
                                                             size_t index, int32_t *out_start,
                                                             int32_t *out_end, uint32_t *out_format,
                                                             uint32_t *out_color);
/* Append one styled run — loader-side; ranges must arrive sorted and
 * non-overlapping (the writer emits them that way). */
struct yetty_ycore_void_result yetty_yrich_paragraph_add_run(struct yetty_yclass_object *obj,
                                                             int32_t start, int32_t end,
                                                             uint32_t format, uint32_t color);

#ifdef __cplusplus
}
#endif

#endif
