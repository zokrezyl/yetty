/*
 * yplatform/ymain/glfw.c — GLFW desktop entry (Linux / macOS / Windows).
 *
 * The whole bootstrap is: register the platform classes, create the
 * glfw_platform, then platform_run(argc, argv). run() calls init() itself, so
 * main has a single step. argc/argv flow through run → init → yconfig_create.
 */

#include <stdio.h>
#include <stdlib.h>

#include <yetty/ycore/result.h>
#include <yetty/yclass/class.h>
#include <yetty/yplatform/compat.h> /* setenv shim on Windows MSVC */

/* Generated platform + app symbols. */
struct yetty_ycore_void_result yetty_yplatform_register(void);
struct yetty_yclass_object_ptr_result yetty_yplatform_glfw_platform_create(
    struct yetty_yclass_ctx *ctx);
struct yetty_ycore_void_result yetty_yplatform_platform_run(struct yetty_yclass_object *obj,
                                                            struct yetty_yclass_object *app,
                                                            int argc, char **argv);
struct yetty_ycore_void_result yetty_yapp_register(void);
struct yetty_yclass_object_ptr_result yetty_yapp_create_app(struct yetty_yclass_ctx *ctx);

int main(int argc, char **argv)
{
    /* Tell descendant tools their host terminal is yetty (inherited by forks). */
    setenv("TERM_PROGRAM", "yetty", 1);

    struct yetty_ycore_void_result reg = yetty_yplatform_register();
    if (YETTY_IS_ERR(reg)) {
        yetty_ycore_error_print(stderr, "yetty: platform register", reg.error);
        yetty_ycore_error_destroy(reg.error);
        return 1;
    }
    struct yetty_ycore_void_result yapp_reg = yetty_yapp_register();
    if (YETTY_IS_ERR(yapp_reg)) {
        yetty_ycore_error_print(stderr, "yetty: yapp register", yapp_reg.error);
        yetty_ycore_error_destroy(yapp_reg.error);
        return 1;
    }
    /* The concrete program is injected by the final app module, which also
     * registers its own app class inside create_app. The platform entry only
     * knows yapp:app. */
    struct yetty_yclass_object_ptr_result app_res = yetty_yapp_create_app(NULL);
    if (YETTY_IS_ERR(app_res)) {
        yetty_ycore_error_print(stderr, "yetty: app create", app_res.error);
        yetty_ycore_error_destroy(app_res.error);
        return 1;
    }

    struct yetty_yclass_object_ptr_result platform_res = yetty_yplatform_glfw_platform_create(NULL);
    if (YETTY_IS_ERR(platform_res)) {
        yetty_ycore_error_print(stderr, "yetty: platform create", platform_res.error);
        yetty_ycore_error_destroy(platform_res.error);
        return 1;
    }

    /* Single step — run() calls init(app, argc, argv) internally. */
    struct yetty_ycore_void_result run_res =
        yetty_yplatform_platform_run(platform_res.value, app_res.value, argc, argv);
    if (YETTY_IS_ERR(run_res)) {
        yetty_ycore_error_print(stderr, "yetty: platform run", run_res.error);
        yetty_ycore_error_destroy(run_res.error);
        return 1;
    }

    return 0;
}
