/* GENERATED — do not edit. */
/* Public interface for regular class(es) `drawable_list` (module: ydrawlist2).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YDRAWLIST2_LIST_H
#define YETTY_YCLASSGEN_YDRAWLIST2_LIST_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yclass_ptr_result yetty_ydrawlist2_drawable_list_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ydrawlist2_drawable_list;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YDRAWLIST2_DRAWABLE_LIST_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YDRAWLIST2_DRAWABLE_LIST_PTR_RESULT
struct yetty_ydrawlist2_drawable_list_ptr_result {
    int ok;
    union {
        struct yetty_ydrawlist2_drawable_list *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ydrawlist2_drawable_list_ptr_result yetty_ydrawlist2_drawable_list_from(
    struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ydrawlist2_drawable_list_to(
    struct yetty_ydrawlist2_drawable_list *data);

/* add: pack `drawable`'s record into this list, immediately, in call order. */
struct yetty_ycore_void_result yetty_ydrawlist2_add(struct yetty_yclass_object *obj,
                                                    struct yetty_yclass_object *drawable);
/* dcs_emit: serialize the list, wrap it in the YETTY_DCS_YDRAW_BIN envelope
 * and write it to stdout — the enclosing yetty renders it, anything else
 * discards it. Flushes so the envelope reaches the terminal immediately. */
struct yetty_ycore_void_result yetty_ydrawlist2_dcs_emit(struct yetty_yclass_object *obj);
/* destroy: release the wrapped list and the object. */
struct yetty_ycore_void_result yetty_ydrawlist2_destroy(struct yetty_yclass_object *obj);

typedef struct yetty_ycore_void_result (*yetty_ydrawlist2_add_fn)(struct yetty_yclass_object *,
                                                                  struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ydrawlist2_dcs_emit_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ydrawlist2_destroy_fn)(struct yetty_yclass_object *);

struct yetty_yclass_object_ptr_result yetty_ydrawlist2_drawable_list_create(
    struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ydrawlist2_register(void);

#ifdef __cplusplus
}
#endif

#endif
