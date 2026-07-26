/* GENERATED — do not edit. */
/* Object API for regular class(es) `app` (implementation module: yapp).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YAPP_APP_H
#define YETTY_YCLASSGEN_API_YAPP_APP_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif



/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yapp_app;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YAPP_APP_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YAPP_APP_PTR_RESULT
struct yetty_yapp_app_ptr_result {
    int ok;
    union {
        struct yetty_yapp_app *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_yapp_app_ptr_result yetty_yapp_app_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yapp_app_to(struct yetty_yapp_app *data);

struct yetty_ycore_void_result yetty_yapp_init(struct yetty_yclass_object * app, struct yetty_yclass_object * platform);
struct yetty_ycore_void_result yetty_yapp_run(struct yetty_yclass_object * app, struct yetty_yclass_object * platform);
/*
 * Ask the app to end its run loop. The base default is a no-op (an app with no
 * event loop has nothing to stop); a concrete app overrides this to stop its
 * loop so run() returns. Local — an app quits itself in-process.
 */
struct yetty_ycore_void_result yetty_yapp_quit(struct yetty_yclass_object * app);

struct yetty_yclass_object_ptr_result yetty_yapp_app_create(struct yetty_yclass_ctx *ctx);



/*
 * App-injection point. The generic platform bootstrap (ymain/<plat>.c) knows
 * only yapp:app, so it calls this fixed-name forwarder; the concrete app module
 * linked into the binary defines it to build — and, when the app is wire-proxied,
 * register — its own app class. Declaration only: there is no default. A binary
 * that links a platform entry without defining this fails to link, by design.
 */
struct yetty_yclass_object_ptr_result yetty_yapp_create_app(struct yetty_yclass_ctx *ctx);

#ifdef __cplusplus
}
#endif

#endif
