/*
 * yplatform/ywindow/android.c — Android window subclass.
 *
 * On Android the NDK delivers an ANativeWindow (via android_app); yetty never
 * opens or closes it. The NDK glue computes the framebuffer size and content
 * scale and pushes them in through yetty_yplatform_android_window_set_metrics;
 * the size/scale queries return those cached values.
 *
 * Plain C, no NDK headers — the ANativeWindow itself is owned by the glue, and
 * the metrics are passed in as plain values, so this TU parses cleanly under the
 * yclass generator and needs nothing from <android/native_window.h>.
 */

#include <stddef.h>

#include <yetty/ycore/result.h>
#include <yetty/yclass/class.h>

YETTY_YRESULT_DECLARE(yetty_yplatform_android_window_ptr, struct yetty_yplatform_android_window *);
struct yetty_yclass_ptr_result yetty_yplatform_android_window_class_get(void);
struct yetty_yplatform_android_window_ptr_result yetty_yplatform_android_window_from(
    struct yetty_yclass_object *obj);

/* Cached framebuffer metrics pushed from the NDK glue. */
struct [[clang::annotate("class@yplatform:android_window")]] [[clang::annotate("platform@android")]]
[[clang::annotate("parent@yplatform:window")]] yetty_yplatform_android_window {
    int framebuffer_width;
    int framebuffer_height;
    float content_scale;
};

[[clang::annotate("override@yplatform:android_window:window_open")]]
static struct yetty_ycore_void_result android_window_open(struct yetty_yclass_object *obj, int width,
                                                          int height, const char *title)
{
    /* The NDK owns the ANativeWindow; nothing to open. */
    (void)obj;
    (void)width;
    (void)height;
    (void)title;
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yplatform:android_window:window_get_size")]]
static struct yetty_ycore_void_result android_window_get_size(struct yetty_yclass_object *obj,
                                                              int *width, int *height)
{
    struct yetty_yplatform_android_window_ptr_result data = yetty_yplatform_android_window_from(obj);
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

[[clang::annotate("override@yplatform:android_window:window_get_framebuffer_size")]]
static struct yetty_ycore_void_result android_window_get_framebuffer_size(
    struct yetty_yclass_object *obj, int *width, int *height)
{
    struct yetty_yplatform_android_window_ptr_result data = yetty_yplatform_android_window_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data, "window_get_framebuffer_size: data_get");
    if (width) {
        *width = data.value->framebuffer_width;
    }
    if (height) {
        *height = data.value->framebuffer_height;
    }
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yplatform:android_window:window_get_content_scale")]]
static struct yetty_ycore_void_result android_window_get_content_scale(
    struct yetty_yclass_object *obj, float *xscale, float *yscale)
{
    struct yetty_yplatform_android_window_ptr_result data = yetty_yplatform_android_window_from(obj);
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

[[clang::annotate("override@yplatform:android_window:window_should_close")]]
static struct yetty_ycore_int_result android_window_should_close(struct yetty_yclass_object *obj)
{
    /* Android apps are terminated by the OS, never by a window-close request. */
    (void)obj;
    return YETTY_OK(yetty_ycore_int, 0);
}

[[clang::annotate("override@yplatform:android_window:window_set_title")]]
static struct yetty_ycore_void_result android_window_set_title(struct yetty_yclass_object *obj,
                                                               const char *title)
{
    /* No window title concept on Android. */
    (void)obj;
    (void)title;
    return YETTY_OK_VOID();
}

/* Push framebuffer metrics from the NDK glue / resize callback. */
[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_yplatform_android_window_set_metrics(
    struct yetty_yclass_object *obj, int framebuffer_width, int framebuffer_height,
    float content_scale)
{
    struct yetty_yplatform_android_window_ptr_result data = yetty_yplatform_android_window_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data, "android_window_set_metrics: data_get");
    data.value->framebuffer_width = framebuffer_width;
    data.value->framebuffer_height = framebuffer_height;
    data.value->content_scale = content_scale;
    return YETTY_OK_VOID();
}

#include "android.gen.c"
