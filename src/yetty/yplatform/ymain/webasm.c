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
 *
 * Every failure branch reports to the hosting page (fatal-report → the
 * terminal.html abort dialog) in addition to stderr: on webasm stderr is
 * only the browser console, and a bring-up failure would otherwise leave
 * the user staring at a dead canvas that still claims to be loading.
 */

#include <stdio.h>

#include <yetty/ycore/result.h>
#include <yetty/yclass/class.h>
#include <yetty/yplatform/fatal-report.h>

/* Generated platform + app symbols (mirrors ymain/glfw.c). */
struct yetty_ycore_void_result yetty_yplatform_register(void);
struct yetty_yclass_object_ptr_result yetty_yplatform_webasm_platform_create(
    struct yetty_yclass_ctx *ctx);
struct yetty_ycore_void_result yetty_yplatform_platform_run(struct yetty_yclass_object *obj,
                                                            struct yetty_yclass_object *app,
                                                            int argc, char **argv);
struct yetty_ycore_void_result yetty_yapp_register(void);
struct yetty_yclass_object_ptr_result yetty_yapp_create_app(struct yetty_yclass_ctx *ctx);

/* Print the chain to stderr AND hand it to the hosting page, then free it. */
static void report_bootstrap_failure(const char *stage, struct yetty_ycore_error error)
{
    yetty_ycore_error_print(stderr, stage, error);

    char chain_buf[768];
    yetty_ycore_error_snprint(chain_buf, sizeof(chain_buf), error);
    char page_message[896];
    snprintf(page_message, sizeof(page_message), "%s: %s", stage, chain_buf);
    yetty_yplatform_fatal_report(page_message);

    yetty_ycore_error_destroy(error);
}

int main(int argc, char **argv)
{
    struct yetty_ycore_void_result reg = yetty_yplatform_register();
    if (YETTY_IS_ERR(reg)) {
        report_bootstrap_failure("yetty: platform register", reg.error);
        return 1;
    }
    struct yetty_ycore_void_result yapp_reg = yetty_yapp_register();
    if (YETTY_IS_ERR(yapp_reg)) {
        report_bootstrap_failure("yetty: yapp register", yapp_reg.error);
        return 1;
    }
    /* The concrete app registers its own app class inside create_app. */
    struct yetty_yclass_object_ptr_result app_res = yetty_yapp_create_app(NULL);
    if (YETTY_IS_ERR(app_res)) {
        report_bootstrap_failure("yetty: app create", app_res.error);
        return 1;
    }

    struct yetty_yclass_object_ptr_result platform_res =
        yetty_yplatform_webasm_platform_create(NULL);
    if (YETTY_IS_ERR(platform_res)) {
        report_bootstrap_failure("yetty: platform create", platform_res.error);
        return 1;
    }

    /* Single step — run() calls init(app, argc, argv) internally, then drives
     * the app. */
    struct yetty_ycore_void_result run_res =
        yetty_yplatform_platform_run(platform_res.value, app_res.value, argc, argv);
    if (YETTY_IS_ERR(run_res)) {
        report_bootstrap_failure("yetty: platform run", run_res.error);
        return 1;
    }

    /* main() returns; the browser keeps the wasm runtime alive to service the
     * emscripten main loop. */
    return 0;
}
