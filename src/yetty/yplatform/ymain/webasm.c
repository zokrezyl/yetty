/*
 * yplatform/ymain/webasm.c — WebAssembly entry.
 *
 * Same linear shape as desktop — the canvas exists at page load, so main()
 * registers the platform classes, creates the webasm_platform, then init()
 * then run(). The browser owns the event loop (emscripten drives it via the JS
 * asset preload + emscripten_set_main_loop), so platform_run hands back rather
 * than blocking; registering the emscripten loop is the app-worker seam below.
 *
 * NOT WIRED IN: forward-declared symbols, not in any CMake target; the existing
 * yinit/webasm path stays until this replaces it.
 *
 * SEAMS: argv (none on web anyway) and the app/render worker — how the app body
 * is invoked and how emscripten_set_main_loop is registered for it — are the
 * open platform→app seam, same as the other platforms.
 */

#include <stdio.h>

#include <yetty/ycore/result.h>
#include <yetty/yclass/class.h>

/* Generated platform symbols. */
struct yetty_ycore_void_result yetty_yplatform_register(void);
struct yetty_yclass_object_ptr_result yetty_yplatform_webasm_platform_create(
    struct yetty_yclass_ctx *ctx);
struct yetty_ycore_void_result yetty_yplatform_platform_run(struct yetty_yclass_object *obj,
                                                            int argc, char **argv);

int main(int argc, char **argv)
{
    struct yetty_ycore_void_result reg = yetty_yplatform_register();
    if (YETTY_IS_ERR(reg)) {
        yetty_ycore_error_print(stderr, "yetty: platform register", reg.error);
        yetty_ycore_error_destroy(reg.error);
        return 1;
    }

    struct yetty_yclass_object_ptr_result platform_res =
        yetty_yplatform_webasm_platform_create(NULL);
    if (YETTY_IS_ERR(platform_res)) {
        yetty_ycore_error_print(stderr, "yetty: platform create", platform_res.error);
        yetty_ycore_error_destroy(platform_res.error);
        return 1;
    }

    /* Single step — run() calls init(argc, argv) internally. */
    struct yetty_ycore_void_result run_res =
        yetty_yplatform_platform_run(platform_res.value, argc, argv);
    if (YETTY_IS_ERR(run_res)) {
        yetty_ycore_error_print(stderr, "yetty: platform run", run_res.error);
        yetty_ycore_error_destroy(run_res.error);
        return 1;
    }

    /* main() returns; the browser keeps the wasm runtime alive to service the
     * emscripten main loop. */
    return 0;
}
