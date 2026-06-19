/* GENERATED — do not edit. */
/* Public interface for regular class(es) `platform` (module: yplatform).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YPLATFORM_YPLATFORM_PLATFORM_H
#define YETTY_YCLASSGEN_YPLATFORM_YPLATFORM_PLATFORM_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Abstract base — owns no state; each subclass holds its own privately. */
struct yetty_yclass_ptr_result yetty_yplatform_platform_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yplatform_platform;
struct yetty_yplatform_platform_ptr_result {
    int ok;
    union {
        struct yetty_yplatform_platform *value;
        struct yetty_ycore_error error;
    };
};
struct yetty_yplatform_platform_ptr_result yetty_yplatform_platform_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object *yetty_yplatform_platform_to(struct yetty_yplatform_platform *data);

struct yetty_ycore_void_result yetty_yplatform_platform_init(struct yetty_yclass_object * obj, struct yetty_yclass_object * app, int argc, char ** argv);
struct yetty_ycore_void_result yetty_yplatform_platform_run(struct yetty_yclass_object * obj, struct yetty_yclass_object * app, int argc, char ** argv);

typedef struct yetty_ycore_void_result (*yetty_yplatform_platform_init_fn)(struct yetty_yclass_object *, struct yetty_yclass_object *, int, char **);
typedef struct yetty_ycore_void_result (*yetty_yplatform_platform_run_fn)(struct yetty_yclass_object *, struct yetty_yclass_object *, int, char **);

struct yetty_yclass_object_ptr_result yetty_yplatform_platform_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_yplatform_register(void);

#ifdef __cplusplus
}
#endif

#endif
