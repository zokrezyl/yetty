/*
 * yapp/app.c — abstract app base class.
 *
 * A platform creates one concrete app through the weak
 * yetty_yapp_create_app() injection point, then drives it through the
 * yapp:app virtual methods below. Platform code never names a concrete app.
 */

#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>

#include <stddef.h>

struct [[clang::annotate("class@yapp:app")]] yetty_yapp_app {
    char reserved;
};

YETTY_YRESULT_DECLARE(yetty_yapp_app_ptr, struct yetty_yapp_app *);
struct yetty_yclass_ptr_result yetty_yapp_app_class_get(void);
struct yetty_yapp_app_ptr_result yetty_yapp_app_from(struct yetty_yclass_object *obj);

[[clang::annotate("virtual@yapp:app:init")]] [[clang::annotate("local@yapp:init")]]
static struct yetty_ycore_void_result yapp_default_init(struct yetty_yclass_object *app,
                                                        struct yetty_yclass_object *platform)
{
    (void)app;
    (void)platform;
    return YETTY_OK_VOID();
}

[[clang::annotate("virtual@yapp:app:run")]] [[clang::annotate("local@yapp:run")]]
static struct yetty_ycore_void_result yapp_default_run(struct yetty_yclass_object *app,
                                                       struct yetty_yclass_object *platform)
{
    (void)app;
    (void)platform;
    return YETTY_ERR(yetty_ycore_void, "yapp:app:run not implemented");
}

[[clang::annotate("expose")]]
__attribute__((weak)) struct yetty_yclass_object_ptr_result yetty_yapp_create_app(
    struct yetty_yclass_ctx *ctx)
{
    (void)ctx;
    return YETTY_ERR(yetty_yclass_object_ptr,
                     "yetty_yapp_create_app: no app implementation linked");
}

/*
 * Concrete-app by-name registration hook. The shared platform entry calls this
 * after registering the platform and base-app classes; the final app module may
 * provide a strong override that installs its own yclass accessor-lookup hook
 * (needed only when the app object is proxied over RPC). Standalone apps that
 * only dispatch locally inherit this no-op default — their app class registers
 * itself lazily on first create.
 */
[[clang::annotate("expose")]]
__attribute__((weak)) struct yetty_ycore_void_result yetty_yapp_register_app(void)
{
    return YETTY_OK_VOID();
}

#include "app.gen.c"
