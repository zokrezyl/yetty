/*
 * yplatform/ywindow/window.c — platform window abstraction, base class.
 *
 * A `window` wraps the platform's native window/surface object and exposes one
 * uniform contract the rest of yetty drives regardless of platform:
 *   - window_open / window_destroy           (lifecycle of the native object)
 *   - window_get_size / _get_framebuffer_size / _get_content_scale
 *   - window_should_close / window_set_title
 *
 * The concrete behaviour lives in the per-platform subclasses
 * (glfw_window, webasm_window, ios_window). This base introduces the virtual
 * slots and supplies defaults: teardown is a no-op (some platforms own the
 * native object themselves) and every query errors with "not implemented by
 * base" so a platform that forgets to override is caught loudly.
 *
 * The base holds NO native handle: each subclass that owns one (e.g. the
 * GLFWwindow*) keeps it private in its own data slice, never on the base and
 * never behind a public accessor. Nothing outside a subclass can read it.
 *
 * Every slot is marked `local@` — a window is inherently a process-local
 * object (you cannot meaningfully proxy a GLFWwindow over RPC), so the slots
 * are never wire-marshalled.
 *
 * NOT WIRED IN: this submodule has no CMake entry and codegen has not been
 * run, so the `window.gen.c` included at the foot does not exist yet. Review
 * the design first; wiring instructions live in the change summary.
 */

#include <stddef.h>

#include <yetty/ycore/result.h>
#include <yetty/yclass/class.h>

/* Base window data slice. The base is abstract — it owns no state (the native
 * handle, if any, is private to each subclass), so this slice is empty. */
struct YETTY_ANNOTATE("class@yplatform:window") yetty_yplatform_window {
    char reserved;
};

/* Result wrapper + codegen accessor/downcast forward-decls. This TU does not
 * include its own generated header (a downstream artifact that would redefine
 * the YETTY_YRESULT_DECLARE below); the generated window.h publishes the
 * identical declarations for consumers. */
YETTY_YRESULT_DECLARE(yetty_yplatform_window_ptr, struct yetty_yplatform_window *);
struct yetty_yclass_ptr_result yetty_yplatform_window_class_get(void);
struct yetty_yplatform_window_ptr_result yetty_yplatform_window_from(
    struct yetty_yclass_object *obj);

/*===========================================================================
 * Virtual slots introduced by the base window class. Each platform subclass
 * overrides the queries (and window_open); the defaults below turn a missing
 * override into a loud runtime error rather than silent wrong behaviour.
 *=========================================================================*/

YETTY_ANNOTATE("virtual@yplatform:window:window_open")
YETTY_ANNOTATE("local@yplatform:window_open")
static struct yetty_ycore_void_result
    window_default_open(struct yetty_yclass_object *obj, int width, int height, const char *title)
{
    (void)obj;
    (void)width;
    (void)height;
    (void)title;
    return YETTY_ERR(yetty_ycore_void, "window_open: not implemented by base window class");
}

YETTY_ANNOTATE("virtual@yplatform:window:window_destroy")
YETTY_ANNOTATE("local@yplatform:window_destroy")
static struct yetty_ycore_void_result window_default_destroy(struct yetty_yclass_object *obj)
{
    /* Teardown is optional — a platform whose native object is owned by the OS
     * (iOS UIView, webasm canvas) needs no work here. */
    (void)obj;
    return YETTY_OK_VOID();
}

/* Create the WebGPU surface for this window. `instance` is a WGPUInstance and
 * the result value is a WGPUSurface — both passed as void* so this abstraction
 * stays free of <webgpu/webgpu.h>; the subclass casts. The native handle stays
 * private to the subclass. */
YETTY_ANNOTATE("virtual@yplatform:window:window_create_surface")
YETTY_ANNOTATE("local@yplatform:window_create_surface")
static struct yetty_yclass_void_ptr_result
    window_default_create_surface(struct yetty_yclass_object *obj, void *instance)
{
    (void)obj;
    (void)instance;
    return YETTY_ERR(yetty_yclass_void_ptr,
                     "window_create_surface: not implemented by base window class");
}

YETTY_ANNOTATE("virtual@yplatform:window:window_get_size")
YETTY_ANNOTATE("local@yplatform:window_get_size")
static struct yetty_ycore_void_result
    window_default_get_size(struct yetty_yclass_object *obj, int *width, int *height)
{
    (void)obj;
    (void)width;
    (void)height;
    return YETTY_ERR(yetty_ycore_void, "window_get_size: not implemented by base window class");
}

YETTY_ANNOTATE("virtual@yplatform:window:window_get_framebuffer_size")
YETTY_ANNOTATE("local@yplatform:window_get_framebuffer_size")
static struct yetty_ycore_void_result
    window_default_get_framebuffer_size(struct yetty_yclass_object *obj, int *width, int *height)
{
    (void)obj;
    (void)width;
    (void)height;
    return YETTY_ERR(yetty_ycore_void,
                     "window_get_framebuffer_size: not implemented by base window class");
}

YETTY_ANNOTATE("virtual@yplatform:window:window_get_content_scale")
YETTY_ANNOTATE("local@yplatform:window_get_content_scale")
static struct yetty_ycore_void_result
    window_default_get_content_scale(struct yetty_yclass_object *obj, float *xscale, float *yscale)
{
    (void)obj;
    (void)xscale;
    (void)yscale;
    return YETTY_ERR(yetty_ycore_void,
                     "window_get_content_scale: not implemented by base window class");
}

YETTY_ANNOTATE("virtual@yplatform:window:window_should_close")
YETTY_ANNOTATE("local@yplatform:window_should_close")
static struct yetty_ycore_int_result window_default_should_close(struct yetty_yclass_object *obj)
{
    (void)obj;
    return YETTY_ERR(yetty_ycore_int, "window_should_close: not implemented by base window class");
}

YETTY_ANNOTATE("virtual@yplatform:window:window_set_title")
YETTY_ANNOTATE("local@yplatform:window_set_title")
static struct yetty_ycore_void_result
    window_default_set_title(struct yetty_yclass_object *obj, const char *title)
{
    (void)obj;
    (void)title;
    return YETTY_ERR(yetty_ycore_void, "window_set_title: not implemented by base window class");
}

#include "window.gen.c"
