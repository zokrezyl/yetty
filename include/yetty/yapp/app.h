/* GENERATED — do not edit. */
/* Public interface for regular class(es) `app` (module: yapp).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YAPP_APP_H
#define YETTY_YCLASSGEN_YAPP_APP_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yinit_runtime;

struct yetty_yclass_ptr_result yetty_yapp_app_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yapp_app;
struct yetty_yapp_app_ptr_result {
    int ok;
    union {
        struct yetty_yapp_app *value;
        struct yetty_ycore_error error;
    };
};
struct yetty_yapp_app_ptr_result yetty_yapp_app_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object *yetty_yapp_app_to(struct yetty_yapp_app *data);

struct yetty_ycore_void_result yetty_yapp_init(struct yetty_yclass_object *app,
                                               struct yetty_yinit_runtime *runtime);
struct yetty_ycore_void_result yetty_yapp_run(struct yetty_yclass_object *app,
                                              struct yetty_yinit_runtime *runtime);

typedef struct yetty_ycore_void_result (*yetty_yapp_init_fn)(struct yetty_yclass_object *,
                                                             struct yetty_yinit_runtime *);
typedef struct yetty_ycore_void_result (*yetty_yapp_run_fn)(struct yetty_yclass_object *,
                                                            struct yetty_yinit_runtime *);

struct yetty_yclass_object_ptr_result yetty_yapp_app_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_yapp_register(void);

struct yetty_yclass_object_ptr_result yetty_yapp_create_app(struct yetty_yclass_ctx *ctx);
/*
 * Concrete-app by-name registration hook. The shared platform entry calls this
 * after registering the platform and base-app classes; the final app module may
 * provide a strong override that installs its own yclass accessor-lookup hook
 * (needed only when the app object is proxied over RPC). Standalone apps that
 * only dispatch locally inherit this no-op default — their app class registers
 * itself lazily on first create.
 */
struct yetty_ycore_void_result yetty_yapp_register_app(void);

#ifdef __cplusplus
}
#endif

#endif
