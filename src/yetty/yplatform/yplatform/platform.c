/*
 * yplatform/yplatform/platform.c — platform lifecycle abstraction, base class.
 *
 * A `platform` owns the host integration for one yetty process. It has exactly
 * two virtual methods, both implemented by each platform subclass
 * (glfw_platform, webasm_platform, ios_platform, android_platform):
 *
 *   platform_init(argc, argv) — create the config (passing argc/argv through to
 *                   yconfig_create), the window object, the clipboard object and
 *                   the cross-thread channels.
 *   platform_run(argc, argv)  — call platform_init(argc, argv), then start /
 *                   drive the OS event loop.
 *
 * The per-platform main (ymain/<platform>) drives the whole startup with a
 * single call: platform_run(argc, argv). run() calls init() itself, so main
 * never has to sequence the two. It has no accessors and is never reached into
 * by other code; everything each platform needs is held privately in its own
 * subclass data slice.
 *
 * Both slots are local@ — a platform is a single process-local object, never
 * proxied over RPC.
 *
 * NOT WIRED IN: no CMake entry and codegen has not been run; the existing
 * yinit/ymain bootstrap stays until this replaces it. The platform.gen.c
 * included at the foot does not exist yet.
 */

#include <stddef.h>

#include <yetty/ycore/result.h>
#include <yetty/yclass/class.h>

/* Abstract base — owns no state; each subclass holds its own privately. */
struct [[clang::annotate("class@yplatform:platform")]] yetty_yplatform_platform {
    char reserved;
};

/* Result wrapper + codegen accessor/downcast forward-decls (this TU does not
 * include its own generated header). */
YETTY_YRESULT_DECLARE(yetty_yplatform_platform_ptr, struct yetty_yplatform_platform *);
struct yetty_yclass_ptr_result yetty_yplatform_platform_class_get(void);
struct yetty_yplatform_platform_ptr_result yetty_yplatform_platform_from(
    struct yetty_yclass_object *obj);

/*===========================================================================
 * The two virtual slots. Each platform subclass overrides both; the base
 * defaults make a missing override a loud error.
 *=========================================================================*/

[[clang::annotate("virtual@yplatform:platform:platform_init")]]
[[clang::annotate("local@yplatform:platform_init")]]
static struct yetty_ycore_void_result platform_default_init(struct yetty_yclass_object *obj,
                                                            struct yetty_yclass_object *app, int argc,
                                                            char **argv)
{
    (void)obj;
    (void)app;
    (void)argc;
    (void)argv;
    return YETTY_ERR(yetty_ycore_void, "platform_init: not implemented by base platform class");
}

[[clang::annotate("virtual@yplatform:platform:platform_run")]]
[[clang::annotate("local@yplatform:platform_run")]]
static struct yetty_ycore_void_result platform_default_run(struct yetty_yclass_object *obj,
                                                           struct yetty_yclass_object *app, int argc,
                                                           char **argv)
{
    (void)obj;
    (void)app;
    (void)argc;
    (void)argv;
    return YETTY_ERR(yetty_ycore_void, "platform_run: not implemented by base platform class");
}

#include "platform.gen.c"
