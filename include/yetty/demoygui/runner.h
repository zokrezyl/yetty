/* GENERATED — do not edit. */
/* Public interface for regular class(es) `app` (module: demoygui).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_DEMOYGUI_RUNNER_H
#define YETTY_YCLASSGEN_DEMOYGUI_RUNNER_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * yclass app wrapper. The standalone bring-up is a yapp:app subclass: the
 * shared glfw_platform brings up the window/surface/GPU/channels and drives the
 * run override below. The heavy per-demo state stays in `struct demo_runner`
 * (the build_fn API the numbered demos depend on); the app data block just
 * embeds it. demo_runner_run creates the app, stamps name/build/enable_chrome
 * onto the embedded runner, then runs the platform sequence.
 *
 * yclass: the only hand-written file is this annotated runner.c; runner.gen.c is
 * #included at the foot.
 */
struct yetty_yclass_ptr_result yetty_demoygui_app_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_demoygui_app;
struct yetty_demoygui_app_ptr_result {
    int ok;
    union {
        struct yetty_demoygui_app *value;
        struct yetty_ycore_error error;
    };
};
struct yetty_demoygui_app_ptr_result yetty_demoygui_app_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_demoygui_app_to(struct yetty_demoygui_app *data);

struct yetty_yclass_object_ptr_result yetty_demoygui_app_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_demoygui_register(void);

#ifdef __cplusplus
}
#endif

#endif
