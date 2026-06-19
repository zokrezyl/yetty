/*
 * yplatform/ywindow/webasm.c — WebAssembly canvas window subclass.
 *
 * Delegates to the existing emscripten canvas helpers in
 * yplatform/window/webasm.c. The canvas is created during page / bootstrap
 * setup, so window_open is a no-op; this subclass stores no native handle —
 * emscripten addresses the canvas globally, not through a handle.
 */

#include <stddef.h>

#include <yetty/ycore/result.h>
#include <yetty/yclass/class.h>

/* Existing emscripten canvas backend (yplatform/window/webasm.c). */
void yetty_yplatform_webasm_get_window_size(int *width, int *height);
void yetty_yplatform_webasm_get_framebuffer_size(int *width, int *height);
void yetty_yplatform_webasm_get_content_scale(float *xscale, float *yscale);
int yetty_yplatform_webasm_should_close(void);
void yetty_yplatform_webasm_set_title(const char *title);
void yetty_yplatform_webasm_destroy_window(void);

/* Own result wrapper + codegen accessor/downcast forward-decls. */
YETTY_YRESULT_DECLARE(yetty_yplatform_webasm_window_ptr, struct yetty_yplatform_webasm_window *);
struct yetty_yclass_ptr_result yetty_yplatform_webasm_window_class_get(void);
struct yetty_yplatform_webasm_window_ptr_result yetty_yplatform_webasm_window_from(
    struct yetty_yclass_object *obj);

/* No webasm-specific state — the canvas is global to the emscripten runtime. */
struct [[clang::annotate("class@yplatform:webasm_window")]] [[clang::annotate(
    "platform@webasm")]] [[clang::annotate("parent@yplatform:window")]]
yetty_yplatform_webasm_window {
    char reserved;
};

[[clang::annotate("override@yplatform:webasm_window:window_open")]]
static struct yetty_ycore_void_result webasm_window_open(struct yetty_yclass_object *obj, int width,
                                                         int height, const char *title)
{
    /* The canvas already exists (created from the page during bootstrap); there
     * is nothing to open here. */
    (void)obj;
    (void)width;
    (void)height;
    (void)title;
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yplatform:webasm_window:window_destroy")]]
static struct yetty_ycore_void_result webasm_window_destroy(struct yetty_yclass_object *obj)
{
    (void)obj;
    yetty_yplatform_webasm_destroy_window();
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yplatform:webasm_window:window_get_size")]]
static struct yetty_ycore_void_result webasm_window_get_size(struct yetty_yclass_object *obj,
                                                             int *width, int *height)
{
    (void)obj;
    yetty_yplatform_webasm_get_window_size(width, height);
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yplatform:webasm_window:window_get_framebuffer_size")]]
static struct yetty_ycore_void_result webasm_window_get_framebuffer_size(
    struct yetty_yclass_object *obj, int *width, int *height)
{
    (void)obj;
    yetty_yplatform_webasm_get_framebuffer_size(width, height);
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yplatform:webasm_window:window_get_content_scale")]]
static struct yetty_ycore_void_result webasm_window_get_content_scale(
    struct yetty_yclass_object *obj, float *xscale, float *yscale)
{
    (void)obj;
    yetty_yplatform_webasm_get_content_scale(xscale, yscale);
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yplatform:webasm_window:window_should_close")]]
static struct yetty_ycore_int_result webasm_window_should_close(struct yetty_yclass_object *obj)
{
    (void)obj;
    return YETTY_OK(yetty_ycore_int, yetty_yplatform_webasm_should_close());
}

[[clang::annotate("override@yplatform:webasm_window:window_set_title")]]
static struct yetty_ycore_void_result webasm_window_set_title(struct yetty_yclass_object *obj,
                                                              const char *title)
{
    (void)obj;
    yetty_yplatform_webasm_set_title(title);
    return YETTY_OK_VOID();
}

#include "webasm.gen.c"
