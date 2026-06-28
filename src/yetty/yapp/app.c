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

struct YETTY_ANNOTATE("class@yapp:app") yetty_yapp_app {
    char reserved;
};

YETTY_YRESULT_DECLARE(yetty_yapp_app_ptr, struct yetty_yapp_app *);
struct yetty_yclass_ptr_result yetty_yapp_app_class_get(void);
struct yetty_yapp_app_ptr_result yetty_yapp_app_from(struct yetty_yclass_object *obj);

YETTY_ANNOTATE("virtual@yapp:app:init")
YETTY_ANNOTATE("local@yapp:init")
static struct yetty_ycore_void_result yapp_default_init(struct yetty_yclass_object *app,
                                                        struct yetty_yclass_object *platform)
{
    (void)app;
    (void)platform;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("virtual@yapp:app:run")
YETTY_ANNOTATE("local@yapp:run")
static struct yetty_ycore_void_result yapp_default_run(struct yetty_yclass_object *app,
                                                       struct yetty_yclass_object *platform)
{
    (void)app;
    (void)platform;
    return YETTY_ERR(yetty_ycore_void, "yapp:app:run not implemented");
}

/*
 * Ask the app to end its run loop. The base default is a no-op (an app with no
 * event loop has nothing to stop); a concrete app overrides this to stop its
 * loop so run() returns. Local — an app quits itself in-process.
 */
YETTY_ANNOTATE("virtual@yapp:app:quit")
YETTY_ANNOTATE("local@yapp:quit")
static struct yetty_ycore_void_result yapp_default_quit(struct yetty_yclass_object *app)
{
    (void)app;
    return YETTY_OK_VOID();
}

/*
 * App-injection point. The generic platform bootstrap (ymain/<plat>.c) knows
 * only yapp:app, so it calls this fixed-name forwarder; the concrete app module
 * linked into the binary defines it to build — and, when the app is wire-proxied,
 * register — its own app class. Declaration only: there is no default. A binary
 * that links a platform entry without defining this fails to link, by design.
 */
YETTY_ANNOTATE("expose")
struct yetty_yclass_object_ptr_result yetty_yapp_create_app(struct yetty_yclass_ctx *ctx);

#include "app.gen.c"
