/*
 * drawable.c — the drawable family of the v2 ydraw client interface:
 * `ydrawlist2:drawable` (abstract base), `ydrawlist2:font` (FONT record),
 * `ydrawlist2:text` (TEXT record). The paint-prefix mid-base lives in
 * shape.c (cross-module parent headers resolve by class name).
 *
 * A drawable is the typed spelling of one ydraw-list record. It carries the
 * record's data as class state; the virtual `pack` slot appends that record
 * to a drawable list. Packing is IMMEDIATE and the object is plain reusable
 * data afterwards — there is no retained scene, no hidden state, and pack
 * manages nothing (font ids are user-chosen record fields, never assigned
 * here). The 28 SDF geometry classes live in the generated `ysdf2` module
 * and derive `shape`.
 *
 * All in-module classes of the drawable hierarchy live in THIS translation
 * unit: the codegen wires every same-module override of the `pack` slot in
 * the slot owner's impl glue, so the override impls must be visible here.
 *
 * Every slot is `local@` — a drawable is an in-process emitter, never
 * proxied over RPC; the model still records the methods so the binding
 * generators can emit them.
 *
 * The version-2 modules (`ydrawlist2`, `ysdf2`) run alongside the plain-C
 * producer surface during the migration; the `2` suffixes drop in the
 * step-2 cleanup once the tools have moved over (epic #712).
 */
#include <yetty/yclass/class.h>
#include <yetty/ycore/ffi-annotations.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw-list/drawable-list.h>

#include "list2-internal.h"

#include <stdio.h>
#include <string.h>

enum {
    YDRAWLIST2_FONT_NAME_MAX = 128,
    YDRAWLIST2_TEXT_BODY_MAX = 2048,
};

/* The abstract drawable base. It has no state of its own — the member below
 * only keeps the struct non-empty for strict C; it is not a property and
 * never reaches the wire. */
struct YETTY_ANNOTATE("class@ydrawlist2:drawable") yetty_ydrawlist2_drawable {
    uint32_t base_reserved;
};

/* Result wrappers declared here (this TU does not include its own generated
 * headers). The guard defines keep the impl glue's guarded re-emission of
 * the same structs out of this TU — under C23, a redefinition whose member
 * points at an annotate-attributed tag trips clang's tag-compatibility
 * check (-Wodr). */
YETTY_YRESULT_DECLARE(yetty_ydrawlist2_drawable_ptr, struct yetty_ydrawlist2_drawable *);
#define YETTY_YCLASSGEN_TYPE_YETTY_YDRAWLIST2_DRAWABLE_PTR_RESULT

/*=============================================================================
 * drawable slots
 *===========================================================================*/

/* pack: append this drawable's record to `list`. The base impl is abstract —
 * every concrete drawable (font, text, the ysdf2 shapes) overrides it. */
YETTY_ANNOTATE("virtual@ydrawlist2:drawable:pack")
YETTY_ANNOTATE("local@ydrawlist2:pack")
static struct yetty_ycore_void_result drawable_pack(struct yetty_yclass_object *obj,
                                                    struct yetty_ydraw_drawable_list *list)
{
    (void)obj;
    (void)list;
    return YETTY_ERR(yetty_ycore_void,
                     "ydrawlist2 pack: abstract drawable — use a concrete drawable class");
}

/* A FONT resource record. The wire record carries an EXPLICIT i32 font_id
 * field — a plain property of this object, chosen by the user; Text records
 * reference it by the same int. v2 scope: installed-face references only
 * (no embedded TTF bytes yet). */
struct YETTY_ANNOTATE("class@ydrawlist2:font") YETTY_ANNOTATE("parent@ydrawlist2:drawable")
    yetty_ydrawlist2_font {
    YETTY_ANNOTATE("property") int32_t font_id;
    char name[YDRAWLIST2_FONT_NAME_MAX]; /* installed face name; inline, no dtor */
};

YETTY_YRESULT_DECLARE(yetty_ydrawlist2_font_ptr, struct yetty_ydrawlist2_font *);
#define YETTY_YCLASSGEN_TYPE_YETTY_YDRAWLIST2_FONT_PTR_RESULT

struct yetty_yclass_ptr_result yetty_ydrawlist2_font_class_get(void);

static struct yetty_yclass_void_ptr_result font_from_obj(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ydrawlist2_font_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, class_r, "font_from_obj: class_get");
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, slice_r, "font_from_obj: object_data");
    return slice_r;
}

/*=============================================================================
 * font slots
 *===========================================================================*/

/* set_name: the installed face name (e.g. "Emmentaler"). */
YETTY_ANNOTATE("virtual@ydrawlist2:font:set_name")
YETTY_ANNOTATE("local@ydrawlist2:set_name")
static struct yetty_ycore_void_result font_set_name(struct yetty_yclass_object *obj,
                                                    const char *name YETTY_ANNOT_CSTRING)
{
    struct yetty_yclass_void_ptr_result font_r = font_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, font_r, "ydrawlist2 set_name: object");
    struct yetty_ydrawlist2_font *font = (struct yetty_ydrawlist2_font *)font_r.value;
    if (!name) {
        font->name[0] = '\0';
        return YETTY_OK_VOID();
    }
    if (strlen(name) >= sizeof(font->name)) {
        return YETTY_ERR(yetty_ycore_void, "ydrawlist2 set_name: font name too long");
    }
    snprintf(font->name, sizeof(font->name), "%s", name);
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@ydrawlist2:drawable:pack")
static struct yetty_ycore_void_result font_pack(struct yetty_yclass_object *obj,
                                                struct yetty_ydraw_drawable_list *list)
{
    struct yetty_yclass_void_ptr_result font_r = font_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, font_r, "ydrawlist2 font pack: object");
    struct yetty_ydrawlist2_font *font = (struct yetty_ydrawlist2_font *)font_r.value;
    if (font->name[0] == '\0') {
        return YETTY_ERR(yetty_ycore_void, "ydrawlist2 font pack: name not set");
    }
    struct yetty_ycore_void_result pack_r =
        yetty_ydraw_drawable_list_add_font_with_id(list, font->font_id, font->name, NULL, 0);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, pack_r, "ydrawlist2 font pack: add_font_with_id");
    return YETTY_OK_VOID();
}

/* A TEXT run record: a UTF-8 span at (x, y) referencing a font by id
 * (-1 = the terminal's default face). Shaping and glyph resolution stay
 * server-side; @name shader glyphs are ordinary PUA codepoints in the body.
 * The body is inline — the cap bounds one RECORD, not the drawing. */
struct YETTY_ANNOTATE("class@ydrawlist2:text") YETTY_ANNOTATE("parent@ydrawlist2:drawable")
    yetty_ydrawlist2_text {
    YETTY_ANNOTATE("property") float x;
    YETTY_ANNOTATE("property") float y;
    YETTY_ANNOTATE("property") float font_size;
    YETTY_ANNOTATE("property") uint32_t color;
    YETTY_ANNOTATE("property") uint32_t layer;
    YETTY_ANNOTATE("property") int32_t font_id;
    YETTY_ANNOTATE("property") float rotation;
    uint32_t body_len;
    char body[YDRAWLIST2_TEXT_BODY_MAX]; /* UTF-8, inline, no dtor */
};

YETTY_YRESULT_DECLARE(yetty_ydrawlist2_text_ptr, struct yetty_ydrawlist2_text *);
#define YETTY_YCLASSGEN_TYPE_YETTY_YDRAWLIST2_TEXT_PTR_RESULT

struct yetty_yclass_ptr_result yetty_ydrawlist2_text_class_get(void);

static struct yetty_yclass_void_ptr_result text_from_obj(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ydrawlist2_text_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, class_r, "text_from_obj: class_get");
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, slice_r, "text_from_obj: object_data");
    return slice_r;
}

/*=============================================================================
 * text slots
 *===========================================================================*/

/* set_body: the UTF-8 text of the run. Named `body` so the binding
 * generators treat it as the class's primary content (positional in the
 * generated constructors, like api_yplot's Function). */
YETTY_ANNOTATE("virtual@ydrawlist2:text:set_body")
YETTY_ANNOTATE("primary@ydrawlist2:set_body")
YETTY_ANNOTATE("local@ydrawlist2:set_body")
static struct yetty_ycore_void_result text_set_body(struct yetty_yclass_object *obj,
                                                    const char *body YETTY_ANNOT_CSTRING)
{
    struct yetty_yclass_void_ptr_result text_r = text_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, text_r, "ydrawlist2 set_body: object");
    struct yetty_ydrawlist2_text *text = (struct yetty_ydrawlist2_text *)text_r.value;
    if (!body) {
        text->body[0] = '\0';
        text->body_len = 0;
        return YETTY_OK_VOID();
    }
    size_t body_len = strlen(body);
    if (body_len >= sizeof(text->body)) {
        return YETTY_ERR(yetty_ycore_void,
                         "ydrawlist2 set_body: text too long for one record — split the run");
    }
    memcpy(text->body, body, body_len + 1);
    text->body_len = (uint32_t)body_len;
    return YETTY_OK_VOID();
}

/* set_color: text color as "#RRGGBB" / "#RRGGBBAA" (the u32 `color`
 * property remains for numeric callers). */
YETTY_ANNOTATE("virtual@ydrawlist2:text:set_color")
YETTY_ANNOTATE("local@ydrawlist2:set_color")
static struct yetty_ycore_void_result text_set_color(struct yetty_yclass_object *obj,
                                                     const char *color YETTY_ANNOT_CSTRING)
{
    struct yetty_yclass_void_ptr_result text_r = text_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, text_r, "ydrawlist2 set_color: object");
    struct yetty_ycore_uint32_result word_r = ydrawlist2_color_parse(color);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, word_r, "ydrawlist2 set_color: color");
    ((struct yetty_ydrawlist2_text *)text_r.value)->color = word_r.value;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@ydrawlist2:drawable:pack")
static struct yetty_ycore_void_result text_pack(struct yetty_yclass_object *obj,
                                                struct yetty_ydraw_drawable_list *list)
{
    struct yetty_yclass_void_ptr_result text_r = text_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, text_r, "ydrawlist2 text pack: object");
    struct yetty_ydrawlist2_text *text = (struct yetty_ydrawlist2_text *)text_r.value;
    struct yetty_ycore_buffer body = {
        .data = (uint8_t *)text->body,
        .size = text->body_len,
        .capacity = text->body_len,
    };
    struct yetty_ycore_void_result pack_r =
        yetty_ydraw_drawable_list_add_text(list, text->x, text->y, &body, text->font_size,
                                           text->color, text->layer, text->font_id, text->rotation);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, pack_r, "ydrawlist2 text pack: add_text");
    return YETTY_OK_VOID();
}

#include "yetty/gen/impl/ydrawlist2/drawable.c"
