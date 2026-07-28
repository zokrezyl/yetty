/* GENERATED — do not edit. */
/* Public interface for regular class(es) `app` (module: yhello).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YHELLO_MAIN_H
#define YETTY_YCLASSGEN_YHELLO_MAIN_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * yclass app wrapper. The standalone window host is a yapp:app subclass; the
 * heavy per-run state stays in `struct app` (shared with client mode), which the
 * data block just embeds. codegen sees this class because the Makefile passes
 * YETTY_YHELLO_HAS_STANDALONE via YCLASS_DEFINES; main.gen.c is #included at the
 * foot, inside the same guard, so reduced builds never compile it.
 */
struct yetty_yclass_ptr_result yetty_yhello_app_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yhello_app;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YHELLO_APP_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YHELLO_APP_PTR_RESULT
struct yetty_yhello_app_ptr_result {
    int ok;
    union {
        struct yetty_yhello_app *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_yhello_app_ptr_result yetty_yhello_app_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yhello_app_to(struct yetty_yhello_app *data);

struct yetty_yclass_object_ptr_result yetty_yhello_app_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_yhello_register(void);

#ifdef __cplusplus
}
#endif

#endif
