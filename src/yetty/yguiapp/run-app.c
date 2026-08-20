/*
 * yguiapp/run-app.c — dual-mode launcher for yclass yguiapp:app subclasses.
 *
 * Plain C (not a yclass file). Hosts a yguiapp:app subclass instance in one of
 * two modes, deciding by yetty_running_under_yetty():
 *
 *   STANDALONE  — register the platform + yapp classes, create the glfw_platform
 *                 and drive the app's run() override (the full window/GPU bring-up
 *                 in app.c).
 *   TERMINAL    — the GPU-free host in run.c, with the app's build virtual
 *                 dispatched through a thunk.
 *
 * Kept separate from run.c on purpose: this TU references the platform/GPU
 * bring-up symbols and the generated yguiapp:app dispatch glue, so anything
 * linking it pulls the whole standalone stack. Terminal-only tools link
 * yetty_yguiapp_terminal (run.c) and never see this file; demos and other
 * dual-mode yguiapp:app subclasses link yetty_yguiapp and route through
 * yetty_yguiapp_run_main.
 */

#include "yetty/yguiapp/run.h"

#include <yetty/ycore/result.h>
#include <yetty/ycore/terminal-detect.h>
#include <yetty/yclass/class.h>
#include <yetty/ytrace/ytrace.h>

#include <stdio.h>

/* Generated build-virtual stub (dispatches to the subclass override). */
struct yetty_ycore_void_result yetty_yguiapp_build(struct yetty_yclass_object *app,
                                                   struct yetty_yclass_object *root);

/* Platform bring-up symbols (provided by the executable's bootstrap sources). */
struct yetty_ycore_void_result yetty_yplatform_register(void);
struct yetty_ycore_void_result yetty_yapp_register(void);
struct yetty_yclass_object_ptr_result yetty_yplatform_glfw_platform_create(
    struct yetty_yclass_ctx *ctx);
struct yetty_ycore_void_result yetty_yplatform_platform_run(struct yetty_yclass_object *obj,
                                                            struct yetty_yclass_object *app,
                                                            int argc, char **argv);

/* Adapt the app object's build virtual to the terminal host's plain callback. */
static struct yetty_ycore_void_result yguiapp_app_build_thunk(struct yetty_yclass_object *body,
                                                              void *userdata)
{
    return yetty_yguiapp_build((struct yetty_yclass_object *)userdata, body);
}

struct yetty_ycore_void_result yetty_yguiapp_run_terminal(struct yetty_yclass_object *app)
{
    return yetty_yguiapp_run_terminal_build(yguiapp_app_build_thunk, app);
}

/*===========================================================================
 * Standalone host — drive the platform bring-up sequence directly.
 *=========================================================================*/
static int yguiapp_run_standalone(int argc, char **argv, const struct yetty_yclass *app_class)
{
    struct yetty_ycore_void_result platform_reg = yetty_yplatform_register();
    if (YETTY_IS_ERR(platform_reg)) {
        yetty_ycore_error_print(stderr, "yguiapp: platform register", platform_reg.error);
        yetty_ycore_error_destroy(platform_reg.error);
        return 1;
    }
    struct yetty_ycore_void_result yapp_reg = yetty_yapp_register();
    if (YETTY_IS_ERR(yapp_reg)) {
        yetty_ycore_error_print(stderr, "yguiapp: yapp register", yapp_reg.error);
        yetty_ycore_error_destroy(yapp_reg.error);
        return 1;
    }

    struct yetty_yclass_object_ptr_result app_res = yetty_yclass_object_alloc(app_class);
    if (YETTY_IS_ERR(app_res)) {
        yetty_ycore_error_print(stderr, "yguiapp: app alloc", app_res.error);
        yetty_ycore_error_destroy(app_res.error);
        return 1;
    }

    struct yetty_yclass_object_ptr_result platform_res = yetty_yplatform_glfw_platform_create(NULL);
    if (YETTY_IS_ERR(platform_res)) {
        yetty_ycore_error_print(stderr, "yguiapp: platform create", platform_res.error);
        yetty_ycore_error_destroy(platform_res.error);
        return 1;
    }

    /* Single step — run() calls init(app, argc, argv) internally. */
    struct yetty_ycore_void_result run_result =
        yetty_yplatform_platform_run(platform_res.value, app_res.value, argc, argv);
    if (YETTY_IS_ERR(run_result)) {
        yetty_ycore_error_print(stderr, "yguiapp: run", run_result.error);
        yetty_ycore_error_destroy(run_result.error);
        return 1;
    }
    return 0;
}

int yetty_yguiapp_run_main(int argc, char **argv, const struct yetty_yclass *app_class)
{
    ytrace_init();
    if (!app_class) {
        fprintf(stderr, "yguiapp: run_main: NULL app class\n");
        return 1;
    }

#ifdef YETTY_YGUI_HAS_UV
    if (yetty_running_under_yetty()) {
        /* Terminal mode runs inside a host yetty pane — no borderless OS window
         * to drag, so chrome is irrelevant here. */
        struct yetty_yclass_object_ptr_result app_res = yetty_yclass_object_alloc(app_class);
        if (YETTY_IS_ERR(app_res)) {
            yetty_ycore_error_print(stderr, "yguiapp: app alloc (terminal)", app_res.error);
            yetty_ycore_error_destroy(app_res.error);
            return 1;
        }
        struct yetty_ycore_void_result rt = yetty_yguiapp_run_terminal(app_res.value);
        if (YETTY_IS_ERR(rt)) {
            yetty_ycore_error_print(stderr, "yguiapp: run_terminal", rt.error);
            yetty_ycore_error_destroy(rt.error);
            return 1;
        }
        return 0;
    }
#endif

    return yguiapp_run_standalone(argc, argv, app_class);
}
