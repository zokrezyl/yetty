/*
 * yplatform/ymain/webasm.c — WebAssembly entry (the main yetty target).
 *
 * Same linear shape as the desktop glfw entry: register the platform + base-app
 * classes, create the concrete app through the yetty_yapp_create_app() injection
 * point, create the webasm_platform, then platform_run(platform, app).
 * run() calls init() and then drives the app (init/run). The browser owns the
 * event loop (the app's run() registers emscripten_set_main_loop and hands
 * back), so platform_run returns and main() returns; the browser keeps the wasm
 * runtime alive to service frames.
 */

#include <stdio.h>

#include <yetty/ycore/result.h>
#include <yetty/yclass/class.h>

/* Generated platform + app symbols (mirrors ymain/glfw.c). */
struct yetty_ycore_void_result yetty_yplatform_register(void);
struct yetty_yclass_object_ptr_result yetty_yplatform_webasm_platform_create(
    struct yetty_yclass_ctx *ctx);
struct yetty_ycore_void_result yetty_yplatform_platform_run(struct yetty_yclass_object *obj,
                                                            struct yetty_yclass_object *app,
                                                            int argc, char **argv);
struct yetty_ycore_void_result yetty_yapp_register(void);
struct yetty_yclass_object_ptr_result yetty_yapp_create_app(struct yetty_yclass_ctx *ctx);

int main(int argc, char **argv)
{
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
    /* The concrete app registers its own app class inside create_app. */
    struct yetty_yclass_object_ptr_result app_res = yetty_yapp_create_app(NULL);
    if (YETTY_IS_ERR(app_res)) {
        yetty_ycore_error_print(stderr, "yetty: app create", app_res.error);
        yetty_ycore_error_destroy(app_res.error);
        return 1;
    }

    struct yetty_yclass_object_ptr_result platform_res =
        yetty_yplatform_webasm_platform_create(NULL);
    if (YETTY_IS_ERR(platform_res)) {
        yetty_ycore_error_print(stderr, "yetty: platform create", platform_res.error);
        yetty_ycore_error_destroy(platform_res.error);
        return 1;
    }

    /* Single step — run() calls init(app, argc, argv) internally, then drives
     * the app. */
    struct yetty_ycore_void_result run_res =
        yetty_yplatform_platform_run(platform_res.value, app_res.value, argc, argv);
    if (YETTY_IS_ERR(run_res)) {
        yetty_ycore_error_print(stderr, "yetty: platform run", run_res.error);
        yetty_ycore_error_destroy(run_res.error);
        return 1;
    }

    /* main() returns; the browser keeps the wasm runtime alive to service the
     * emscripten main loop. */
    return 0;
}
