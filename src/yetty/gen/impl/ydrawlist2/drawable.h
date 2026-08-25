/* GENERATED — do not edit. */
/* Public interface for regular class(es) `drawable, font, text` (module: ydrawlist2).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YDRAWLIST2_DRAWABLE_H
#define YETTY_YCLASSGEN_YDRAWLIST2_DRAWABLE_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ydraw_drawable_list;

/* The abstract drawable base. It has no state of its own — the member below
 * only keeps the struct non-empty for strict C; it is not a property and
 * never reaches the wire. */
struct yetty_yclass_ptr_result yetty_ydrawlist2_drawable_class_get(void);
/* A FONT resource record. The wire record carries an EXPLICIT i32 font_id
 * field — a plain property of this object, chosen by the user; Text records
 * reference it by the same int. v2 scope: installed-face references only
 * (no embedded TTF bytes yet). */
struct yetty_yclass_ptr_result yetty_ydrawlist2_font_class_get(void);
/* A TEXT run record: a UTF-8 span at (x, y) referencing a font by id
 * (-1 = the terminal's default face). Shaping and glyph resolution stay
 * server-side; @name shader glyphs are ordinary PUA codepoints in the body.
 * The body is inline — the cap bounds one RECORD, not the drawing. */
struct yetty_yclass_ptr_result yetty_ydrawlist2_text_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ydrawlist2_drawable;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YDRAWLIST2_DRAWABLE_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YDRAWLIST2_DRAWABLE_PTR_RESULT
struct yetty_ydrawlist2_drawable_ptr_result {
    int ok;
    union {
        struct yetty_ydrawlist2_drawable *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ydrawlist2_drawable_ptr_result yetty_ydrawlist2_drawable_from(
    struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ydrawlist2_drawable_to(
    struct yetty_ydrawlist2_drawable *data);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ydrawlist2_font;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YDRAWLIST2_FONT_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YDRAWLIST2_FONT_PTR_RESULT
struct yetty_ydrawlist2_font_ptr_result {
    int ok;
    union {
        struct yetty_ydrawlist2_font *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ydrawlist2_font_ptr_result yetty_ydrawlist2_font_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ydrawlist2_font_to(struct yetty_ydrawlist2_font *data);
struct yetty_ycore_int_result yetty_ydrawlist2_font_font_id_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ydrawlist2_font_font_id_set(struct yetty_yclass_object *obj,
                                                                 int32_t value);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ydrawlist2_text;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YDRAWLIST2_TEXT_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YDRAWLIST2_TEXT_PTR_RESULT
struct yetty_ydrawlist2_text_ptr_result {
    int ok;
    union {
        struct yetty_ydrawlist2_text *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ydrawlist2_text_ptr_result yetty_ydrawlist2_text_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ydrawlist2_text_to(struct yetty_ydrawlist2_text *data);
struct float_result yetty_ydrawlist2_text_x_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ydrawlist2_text_x_set(struct yetty_yclass_object *obj,
                                                           float value);
struct float_result yetty_ydrawlist2_text_y_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ydrawlist2_text_y_set(struct yetty_yclass_object *obj,
                                                           float value);
struct float_result yetty_ydrawlist2_text_font_size_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ydrawlist2_text_font_size_set(struct yetty_yclass_object *obj,
                                                                   float value);
struct uint32_result yetty_ydrawlist2_text_color_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ydrawlist2_text_color_set(struct yetty_yclass_object *obj,
                                                               uint32_t value);
struct uint32_result yetty_ydrawlist2_text_layer_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ydrawlist2_text_layer_set(struct yetty_yclass_object *obj,
                                                               uint32_t value);
struct yetty_ycore_int_result yetty_ydrawlist2_text_font_id_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ydrawlist2_text_font_id_set(struct yetty_yclass_object *obj,
                                                                 int32_t value);
struct float_result yetty_ydrawlist2_text_rotation_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ydrawlist2_text_rotation_set(struct yetty_yclass_object *obj,
                                                                  float value);

/* pack: append this drawable's record to `list`. The base impl is abstract —
 * every concrete drawable (font, text, the ysdf2 shapes) overrides it. */
struct yetty_ycore_void_result yetty_ydrawlist2_pack(struct yetty_yclass_object *obj,
                                                     struct yetty_ydraw_drawable_list *list);
/* set_name: the installed face name (e.g. "Emmentaler"). */
struct yetty_ycore_void_result yetty_ydrawlist2_set_name(struct yetty_yclass_object *obj,
                                                         const char *name);
/* set_body: the UTF-8 text of the run. Named `body` so the binding
 * generators treat it as the class's primary content (positional in the
 * generated constructors, like api_yplot's Function). */
struct yetty_ycore_void_result yetty_ydrawlist2_set_body(struct yetty_yclass_object *obj,
                                                         const char *body);
/* set_color: text color as "#RRGGBB" / "#RRGGBBAA" (the u32 `color`
 * property remains for numeric callers). */
struct yetty_ycore_void_result yetty_ydrawlist2_set_color(struct yetty_yclass_object *obj,
                                                          const char *color);

typedef struct yetty_ycore_void_result (*yetty_ydrawlist2_pack_fn)(
    struct yetty_yclass_object *, struct yetty_ydraw_drawable_list *);
typedef struct yetty_ycore_void_result (*yetty_ydrawlist2_set_name_fn)(struct yetty_yclass_object *,
                                                                       const char *);
typedef struct yetty_ycore_void_result (*yetty_ydrawlist2_set_body_fn)(struct yetty_yclass_object *,
                                                                       const char *);
typedef struct yetty_ycore_void_result (*yetty_ydrawlist2_set_color_fn)(
    struct yetty_yclass_object *, const char *);

struct yetty_yclass_object_ptr_result yetty_ydrawlist2_drawable_create(
    struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ydrawlist2_font_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ydrawlist2_text_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ydrawlist2_register(void);

#ifdef __cplusplus
}
#endif

#endif
