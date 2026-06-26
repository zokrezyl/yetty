/* GENERATED — do not edit. */
/* Public interface for regular class(es) `editor` (module: yai).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YAI_EDITOR_H
#define YETTY_YCLASSGEN_YAI_EDITOR_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yai_app;

/* The class@ annotation needs a struct to sit on; the base carries no
 * per-instance state (the line buffer is in struct yai_app, mode scratch
 * is in the subclass). */
struct yetty_yclass_ptr_result yetty_yai_editor_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yai_editor;
struct yetty_yai_editor_ptr_result {
    int ok;
    union {
        struct yetty_yai_editor *value;
        struct yetty_ycore_error error;
    };
};
struct yetty_yai_editor_ptr_result yetty_yai_editor_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yai_editor_to(struct yetty_yai_editor *data);

struct yetty_ycore_int_result yetty_yai_feed_byte(struct yetty_yclass_object * obj, struct yai_app * app, int byte);

typedef struct yetty_ycore_int_result (*yetty_yai_feed_byte_fn)(struct yetty_yclass_object *, struct yai_app *, int);

struct yetty_yclass_object_ptr_result yetty_yai_editor_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_yai_register(void);

#ifdef __cplusplus
}
#endif

#endif
