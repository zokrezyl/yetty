/* GENERATED — do not edit. */
/* Object API for regular class(es) `ydraw_embed` (implementation module: ygui2).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YGUI2_WIDGETS_YDRAW_EMBED_H
#define YETTY_YCLASSGEN_API_YGUI2_WIDGETS_YDRAW_EMBED_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ydraw_drawable_list;

struct yetty_yclass_ptr_result yetty_ygui2_ydraw_embed_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ygui2_ydraw_embed;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YGUI2_YDRAW_EMBED_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YGUI2_YDRAW_EMBED_PTR_RESULT
struct yetty_ygui2_ydraw_embed_ptr_result {
    int ok;
    union {
        struct yetty_ygui2_ydraw_embed *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ygui2_ydraw_embed_ptr_result yetty_ygui2_ydraw_embed_from(
    struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ygui2_ydraw_embed_to(
    struct yetty_ygui2_ydraw_embed *data);

struct yetty_yclass_object_ptr_result yetty_ygui2_ydraw_embed_create(struct yetty_yclass_ctx *ctx);

/* Takes ownership of `buffer` (destroys the previous one). NULL clears.
 * The buffer is validated leaf-only; a rejected buffer is NOT adopted (the
 * caller keeps ownership) and the previous content stays. */
struct yetty_ycore_void_result yetty_ygui2_ydraw_embed_set_buffer(
    struct yetty_yclass_object *obj, struct yetty_ydraw_drawable_list *buffer);

#ifdef __cplusplus
}
#endif

#endif
