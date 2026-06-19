/*
 * yetty/app.c — yclass class `yetty:app`: the yetty terminal as a yapp.
 *
 * Subclass of yapp:app. This is the program-specific half of the startup: the
 * platform brings up config / window / clipboard / GPU surface / channels and
 * hands the assembled runtime to run, which builds the PTY factory, the framework
 * and the terminal, runs the terminal loop, and tears down. The same platform
 * bootstrap drives every standalone app — only this app object differs.
 *
 * yclass: the only hand-written file is this annotated .c; app.gen.c is #included
 * at the foot. Both slots are app overrides of the yapp:app virtuals.
 */

#include <stddef.h>

#include <yetty/ycore/result.h>
#include <yetty/yclass/class.h>
#include <yetty/yapp/app.h>
#include <yetty/yevent/event.h>
#include <yetty/yframework/yframework.h>
#include <yetty/yetty/yetty.h>
#include <yetty/yinit/yinit.h>
#include <yetty/yplatform/platform-input-pipe.h>
#include <yetty/yplatform/pty.h>
#include <yetty/ytrace/ytrace.h>

struct [[clang::annotate("class@yetty:app")]] [[clang::annotate("parent@yapp:app")]]
yetty_yetty_app {
    char reserved;
};

/* Result wrapper + codegen accessor/downcast forward-decls (this TU does not
 * include its own generated header). */
YETTY_YRESULT_DECLARE(yetty_yetty_app_ptr, struct yetty_yetty_app *);
struct yetty_yclass_ptr_result yetty_yetty_app_class_get(void);
struct yetty_yetty_app_ptr_result yetty_yetty_app_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yetty_app_create(struct yetty_yclass_ctx *ctx);

[[clang::annotate("override@yapp:app:init")]]
static struct yetty_ycore_void_result yetty_app_init(struct yetty_yclass_object *obj,
                                                     struct yetty_yinit_runtime *rt)
{
    (void)obj;
    (void)rt;
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yapp:app:run")]]
static struct yetty_ycore_void_result yetty_app_run(struct yetty_yclass_object *obj,
                                                    struct yetty_yinit_runtime *rt)
{
    (void)obj;

    struct yetty_yplatform_pty_factory_ptr_result pty_res =
        yetty_yplatform_pty_factory_create(rt->config, NULL);
    if (!YETTY_IS_OK(pty_res)) {
        return YETTY_ERR(yetty_ycore_void, "yetty:app: pty_factory_create failed", pty_res);
    }
    struct yetty_yplatform_pty_factory *pty_factory = pty_res.value;

    struct yetty_yframework_ptr_result framework_res = yetty_yframework_create(rt);
    if (!YETTY_IS_OK(framework_res)) {
        pty_factory->ops->destroy(pty_factory);
        return YETTY_ERR(yetty_ycore_void, "yetty:app: yframework_create failed", framework_res);
    }
    struct yetty_yframework *framework = framework_res.value;

    struct yetty_yetty_yetty_result yetty_res = yetty_create(framework, pty_factory);
    if (!YETTY_IS_OK(yetty_res)) {
        (void)yetty_yframework_destroy(framework);
        pty_factory->ops->destroy(pty_factory);
        return YETTY_ERR(yetty_ycore_void, "yetty:app: yetty_create failed", yetty_res);
    }
    struct yetty_yetty_yetty *yetty = yetty_res.value;

    /* First frame at the live framebuffer size, not the config default. */
    struct yetty_yui_event resize = {
        .type = YETTY_YCORE_RESIZE,
        .resize = {.width = (float)rt->surface_width, .height = (float)rt->surface_height},
    };
    rt->platform_input_pipe->ops->write(rt->platform_input_pipe, &resize, sizeof(resize));

    struct yetty_ycore_void_result run_res = yetty_run(yetty);

    (void)yetty_destroy(yetty);
    (void)yetty_yframework_destroy(framework);
    pty_factory->ops->destroy(pty_factory);
    return run_res;
}

struct yetty_yclass_object_ptr_result yetty_yapp_create_app(struct yetty_yclass_ctx *ctx)
{
    return yetty_yetty_app_create(ctx);
}

/* Strong override of the weak yapp default: install the yetty app's
 * accessor-lookup hook. Keeps yetty's startup identical to before the shared
 * entry was generalized. */
struct yetty_ycore_void_result yetty_yetty_register(void);

struct yetty_ycore_void_result yetty_yapp_register_app(void)
{
    return yetty_yetty_register();
}

#include "app.gen.c"
