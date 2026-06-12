/* GENERATED — do not edit. */
#include "yetty/ymap/map.h"
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h> /* container_of */
#include <yetty/ytrace/ytrace.h>
#include <stdint.h>
#include <stdlib.h> /* malloc/free for buffer-arg marshalling */
#include <string.h>

struct yetty_ycore_void_result yetty_ymap_configure(struct yetty_yclass_ctx *ctx,
                                                    struct yetty_yclass_object *obj,
                                                    double latitude, double longitude,
                                                    uint32_t zoom, uint32_t width_px,
                                                    uint32_t height_px)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ymap", (yetty_yclass_method_id_t)yetty_ymap_configure);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ymap_configure: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ymap_configure: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_ymap_configure: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_ymap_configure: dispatch_lookup failed");
    return ((yetty_ymap_configure_fn)dispatch_impl_r.value)(ctx, obj, latitude, longitude, zoom,
                                                            width_px, height_px);
}

struct yetty_ycore_void_result yetty_ymap_set_provider(struct yetty_yclass_ctx *ctx,
                                                       struct yetty_yclass_object *obj,
                                                       const char *name)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ymap", (yetty_yclass_method_id_t)yetty_ymap_set_provider);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ymap_set_provider: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ymap_set_provider: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_ymap_set_provider: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_ymap_set_provider: dispatch_lookup failed");
    return ((yetty_ymap_set_provider_fn)dispatch_impl_r.value)(ctx, obj, name);
}

struct yetty_ycore_void_result yetty_ymap_set_custom_provider(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj, const char *url_template,
    int is_vector, const char *file_extension, uint32_t max_zoom, const char *attribution)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ymap", (yetty_yclass_method_id_t)yetty_ymap_set_custom_provider);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_ymap_set_custom_provider: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ymap_set_custom_provider: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_ymap_set_custom_provider: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_ymap_set_custom_provider: dispatch_lookup failed");
    return ((yetty_ymap_set_custom_provider_fn)dispatch_impl_r.value)(
        ctx, obj, url_template, is_vector, file_extension, max_zoom, attribution);
}

struct yetty_ycore_void_result yetty_ymap_set_center(struct yetty_yclass_ctx *ctx,
                                                     struct yetty_yclass_object *obj,
                                                     double latitude, double longitude)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ymap", (yetty_yclass_method_id_t)yetty_ymap_set_center);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ymap_set_center: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ymap_set_center: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_ymap_set_center: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_ymap_set_center: dispatch_lookup failed");
    return ((yetty_ymap_set_center_fn)dispatch_impl_r.value)(ctx, obj, latitude, longitude);
}

struct yetty_ycore_void_result yetty_ymap_set_zoom(struct yetty_yclass_ctx *ctx,
                                                   struct yetty_yclass_object *obj, uint32_t zoom)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ymap", (yetty_yclass_method_id_t)yetty_ymap_set_zoom);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ymap_set_zoom: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ymap_set_zoom: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_ymap_set_zoom: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_ymap_set_zoom: dispatch_lookup failed");
    return ((yetty_ymap_set_zoom_fn)dispatch_impl_r.value)(ctx, obj, zoom);
}

struct yetty_ycore_void_result yetty_ymap_set_viewport(struct yetty_yclass_ctx *ctx,
                                                       struct yetty_yclass_object *obj,
                                                       uint32_t width_px, uint32_t height_px)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ymap", (yetty_yclass_method_id_t)yetty_ymap_set_viewport);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ymap_set_viewport: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ymap_set_viewport: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_ymap_set_viewport: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_ymap_set_viewport: dispatch_lookup failed");
    return ((yetty_ymap_set_viewport_fn)dispatch_impl_r.value)(ctx, obj, width_px, height_px);
}

struct yetty_ycore_void_result yetty_ymap_pan_by_pixels(struct yetty_yclass_ctx *ctx,
                                                        struct yetty_yclass_object *obj,
                                                        double delta_x, double delta_y)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ymap", (yetty_yclass_method_id_t)yetty_ymap_pan_by_pixels);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ymap_pan_by_pixels: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ymap_pan_by_pixels: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_ymap_pan_by_pixels: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_ymap_pan_by_pixels: dispatch_lookup failed");
    return ((yetty_ymap_pan_by_pixels_fn)dispatch_impl_r.value)(ctx, obj, delta_x, delta_y);
}

struct yetty_ycore_int_result yetty_ymap_zoom_by_at(struct yetty_yclass_ctx *ctx,
                                                    struct yetty_yclass_object *obj, int32_t step,
                                                    double anchor_x, double anchor_y)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ymap", (yetty_yclass_method_id_t)yetty_ymap_zoom_by_at);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_int, "yetty_ymap_zoom_by_at: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ymap_zoom_by_at: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, object_class_r,
                        "yetty_ymap_zoom_by_at: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, dispatch_impl_r,
                        "yetty_ymap_zoom_by_at: dispatch_lookup failed");
    return ((yetty_ymap_zoom_by_at_fn)dispatch_impl_r.value)(ctx, obj, step, anchor_x, anchor_y);
}

struct yetty_ycore_int_result yetty_ymap_get_zoom(struct yetty_yclass_ctx *ctx,
                                                  struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ymap", (yetty_yclass_method_id_t)yetty_ymap_get_zoom);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_int, "yetty_ymap_get_zoom: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ymap_get_zoom: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, object_class_r,
                        "yetty_ymap_get_zoom: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, dispatch_impl_r,
                        "yetty_ymap_get_zoom: dispatch_lookup failed");
    return ((yetty_ymap_get_zoom_fn)dispatch_impl_r.value)(ctx, obj);
}

struct yetty_ycore_void_result yetty_ymap_geolocate(struct yetty_yclass_ctx *ctx,
                                                    struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ymap", (yetty_yclass_method_id_t)yetty_ymap_geolocate);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ymap_geolocate: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ymap_geolocate: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_ymap_geolocate: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_ymap_geolocate: dispatch_lookup failed");
    return ((yetty_ymap_geolocate_fn)dispatch_impl_r.value)(ctx, obj);
}

struct yetty_ycore_const_char_ptr_result yetty_ymap_attribution(struct yetty_yclass_ctx *ctx,
                                                                struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ymap", (yetty_yclass_method_id_t)yetty_ymap_attribution);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_const_char_ptr,
                             "yetty_ymap_attribution: method_slot_get failed", method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_ymap_attribution: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, object_class_r,
                        "yetty_ymap_attribution: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, dispatch_impl_r,
                        "yetty_ymap_attribution: dispatch_lookup failed");
    return ((yetty_ymap_attribution_fn)dispatch_impl_r.value)(ctx, obj);
}

struct yetty_ycore_int_result yetty_ymap_is_vector(struct yetty_yclass_ctx *ctx,
                                                   struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ymap", (yetty_yclass_method_id_t)yetty_ymap_is_vector);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_int, "yetty_ymap_is_vector: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ymap_is_vector: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, object_class_r,
                        "yetty_ymap_is_vector: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, dispatch_impl_r,
                        "yetty_ymap_is_vector: dispatch_lookup failed");
    return ((yetty_ymap_is_vector_fn)dispatch_impl_r.value)(ctx, obj);
}

struct yetty_ydraw_drawable_list_result yetty_ymap_render(struct yetty_yclass_ctx *ctx,
                                                          struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ymap", (yetty_yclass_method_id_t)yetty_ymap_render);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ydraw_drawable_list, "yetty_ymap_render: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ydraw_drawable_list, "yetty_ymap_render: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ydraw_drawable_list, object_class_r,
                        "yetty_ymap_render: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ydraw_drawable_list, dispatch_impl_r,
                        "yetty_ymap_render: dispatch_lookup failed");
    return ((yetty_ymap_render_fn)dispatch_impl_r.value)(ctx, obj);
}

struct yetty_ycore_void_result yetty_ymap_destroy(struct yetty_yclass_ctx *ctx,
                                                  struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ymap", (yetty_yclass_method_id_t)yetty_ymap_destroy);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ymap_destroy: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ymap_destroy: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_ymap_destroy: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_ymap_destroy: dispatch_lookup failed");
    return ((yetty_ymap_destroy_fn)dispatch_impl_r.value)(ctx, obj);
}
