/*
 * yplatform/ywindow/ios-tvos.c — iOS / tvOS window subclass.
 *
 * On iOS/tvOS the window is a UIView backed by a CAMetalLayer that UIKit
 * creates and owns; yetty never opens or closes it. The view-controller
 * bootstrap (yinit/ios-tvos.m) computes the framebuffer size and content
 * scale from the layer and pushes them in through
 * yetty_yplatform_ios_window_set_metrics. The size/scale queries return those
 * cached values; the live resize path updates them through the same setter.
 * This subclass keeps no native handle: its own methods need only the cached
 * metrics, and the CAMetalLayer required to build the WebGPU surface is owned
 * by the bootstrap, not stored or exposed here.
 *
 * This file is intentionally plain C (no Obj-C) so the yclass generator can
 * parse it; all UIKit / CAMetalLayer interaction stays in the Obj-C bootstrap,
 * which only calls the C-ABI setters declared here.
 */

#include <stddef.h>

#include <yetty/ycore/result.h>
#include <yetty/yclass/class.h>

/* Own result wrapper + codegen accessor/downcast forward-decls. */
YETTY_YRESULT_DECLARE(yetty_yplatform_ios_window_ptr, struct yetty_yplatform_ios_window *);
struct yetty_yclass_ptr_result yetty_yplatform_ios_window_class_get(void);
struct yetty_yplatform_ios_window_ptr_result yetty_yplatform_ios_window_from(
    struct yetty_yclass_object *obj);

/* Cached metrics pushed from the UIKit bootstrap. framebuffer_* is in physical
 * pixels; content_scale is UIScreen.nativeScale. Logical size is derived as
 * framebuffer / scale. */
struct YETTY_ANNOTATE("class@yplatform:ios_window") YETTY_ANNOTATE("platform@ios")
    YETTY_ANNOTATE("parent@yplatform:window") yetty_yplatform_ios_window {
    int framebuffer_width;
    int framebuffer_height;
    float content_scale;
};

YETTY_ANNOTATE("override@yplatform:ios_window:window_open")
static struct yetty_ycore_void_result ios_window_open(struct yetty_yclass_object *obj, int width,
                                                      int height, const char *title)
{
    /* UIKit owns the view/layer; there is nothing to open. The bootstrap binds
     * the native handle + metrics through the exposed setters. */
    (void)obj;
    (void)width;
    (void)height;
    (void)title;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@yplatform:ios_window:window_get_size")
static struct yetty_ycore_void_result ios_window_get_size(struct yetty_yclass_object *obj,
                                                          int *width, int *height)
{
    struct yetty_yplatform_ios_window_ptr_result data = yetty_yplatform_ios_window_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data, "window_get_size: data_get");
    float scale = data.value->content_scale > 0.0f ? data.value->content_scale : 1.0f;
    if (width) {
        *width = (int)((float)data.value->framebuffer_width / scale);
    }
    if (height) {
        *height = (int)((float)data.value->framebuffer_height / scale);
    }
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@yplatform:ios_window:window_get_framebuffer_size")
static struct yetty_ycore_void_result ios_window_get_framebuffer_size(
    struct yetty_yclass_object *obj, int *width, int *height)
{
    struct yetty_yplatform_ios_window_ptr_result data = yetty_yplatform_ios_window_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data, "window_get_framebuffer_size: data_get");
    if (width) {
        *width = data.value->framebuffer_width;
    }
    if (height) {
        *height = data.value->framebuffer_height;
    }
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@yplatform:ios_window:window_get_content_scale")
static struct yetty_ycore_void_result ios_window_get_content_scale(struct yetty_yclass_object *obj,
                                                                   float *xscale, float *yscale)
{
    struct yetty_yplatform_ios_window_ptr_result data = yetty_yplatform_ios_window_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data, "window_get_content_scale: data_get");
    float scale = data.value->content_scale > 0.0f ? data.value->content_scale : 1.0f;
    if (xscale) {
        *xscale = scale;
    }
    if (yscale) {
        *yscale = scale;
    }
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@yplatform:ios_window:window_should_close")
static struct yetty_ycore_int_result ios_window_should_close(struct yetty_yclass_object *obj)
{
    /* iOS/tvOS apps are terminated by the OS, never by a window-close request. */
    (void)obj;
    return YETTY_OK(yetty_ycore_int, 0);
}

YETTY_ANNOTATE("override@yplatform:ios_window:window_set_title")
static struct yetty_ycore_void_result ios_window_set_title(struct yetty_yclass_object *obj,
                                                           const char *title)
{
    /* No window title concept on iOS/tvOS. */
    (void)obj;
    (void)title;
    return YETTY_OK_VOID();
}

/* Push framebuffer metrics from the UIKit bootstrap / layout-resize callback. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yplatform_ios_window_set_metrics(
    struct yetty_yclass_object *obj, int framebuffer_width, int framebuffer_height,
    float content_scale)
{
    struct yetty_yplatform_ios_window_ptr_result data = yetty_yplatform_ios_window_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data, "ios_window_set_metrics: data_get");
    data.value->framebuffer_width = framebuffer_width;
    data.value->framebuffer_height = framebuffer_height;
    data.value->content_scale = content_scale;
    return YETTY_OK_VOID();
}

#include "ios-tvos.gen.c"
