/* GENERATED — do not edit. */
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>  /* container_of, buffer */
#include <yetty/ytrace/ytrace.h>
#include <stdbool.h>
#include <stddef.h>  /* NULL, size_t */
#include <stdint.h>
#include <stdio.h>  /* stderr */
#include <stdlib.h>  /* calloc/free for proxy + buffer marshalling */
#include <string.h>  /* memcpy/strcmp/strlen */

struct yetty_ycore_const_char_ptr_result;
struct yetty_ycore_int_result;
struct yetty_ycore_void_result;
struct yetty_ydraw_drawable_list_result;
struct yetty_ycore_void_result yetty_ymap_configure(struct yetty_yclass_object * obj, double latitude, double longitude, uint32_t zoom, uint32_t width_px, uint32_t height_px);
struct yetty_ycore_void_result yetty_ymap_set_provider(struct yetty_yclass_object * obj, const char * name);
struct yetty_ycore_void_result yetty_ymap_set_custom_provider(struct yetty_yclass_object * obj, const char * url_template, int is_vector, const char * file_extension, uint32_t max_zoom, const char * attribution);
struct yetty_ycore_void_result yetty_ymap_set_center(struct yetty_yclass_object * obj, double latitude, double longitude);
struct yetty_ycore_void_result yetty_ymap_set_zoom(struct yetty_yclass_object * obj, uint32_t zoom);
struct yetty_ycore_void_result yetty_ymap_set_viewport(struct yetty_yclass_object * obj, uint32_t width_px, uint32_t height_px);
struct yetty_ycore_void_result yetty_ymap_pan_by_pixels(struct yetty_yclass_object * obj, double delta_x, double delta_y);
struct yetty_ycore_int_result yetty_ymap_zoom_by_at(struct yetty_yclass_object * obj, int32_t step, double anchor_x, double anchor_y);
struct yetty_ycore_int_result yetty_ymap_get_zoom(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_ymap_geolocate(struct yetty_yclass_object * obj);
struct yetty_ycore_const_char_ptr_result yetty_ymap_attribution(struct yetty_yclass_object * obj);
struct yetty_ycore_int_result yetty_ymap_is_vector(struct yetty_yclass_object * obj);
struct yetty_ydraw_drawable_list_result yetty_ymap_render(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_ymap_destroy(struct yetty_yclass_object * obj);
typedef struct yetty_ycore_void_result (*yetty_ymap_configure_fn)(struct yetty_yclass_object *, double, double, uint32_t, uint32_t, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_ymap_set_provider_fn)(struct yetty_yclass_object *, const char *);
typedef struct yetty_ycore_void_result (*yetty_ymap_set_custom_provider_fn)(struct yetty_yclass_object *, const char *, int, const char *, uint32_t, const char *);
typedef struct yetty_ycore_void_result (*yetty_ymap_set_center_fn)(struct yetty_yclass_object *, double, double);
typedef struct yetty_ycore_void_result (*yetty_ymap_set_zoom_fn)(struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_ymap_set_viewport_fn)(struct yetty_yclass_object *, uint32_t, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_ymap_pan_by_pixels_fn)(struct yetty_yclass_object *, double, double);
typedef struct yetty_ycore_int_result (*yetty_ymap_zoom_by_at_fn)(struct yetty_yclass_object *, int32_t, double, double);
typedef struct yetty_ycore_int_result (*yetty_ymap_get_zoom_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ymap_geolocate_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_ymap_attribution_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_int_result (*yetty_ymap_is_vector_fn)(struct yetty_yclass_object *);
typedef struct yetty_ydraw_drawable_list_result (*yetty_ymap_render_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ymap_destroy_fn)(struct yetty_yclass_object *);

[[maybe_unused]]
static yetty_ymap_configure_fn yetty_ymap_map_yetty_ymap_configure_check = map_configure;
[[maybe_unused]]
static yetty_ymap_set_provider_fn yetty_ymap_map_yetty_ymap_set_provider_check = map_set_provider;
[[maybe_unused]]
static yetty_ymap_set_custom_provider_fn yetty_ymap_map_yetty_ymap_set_custom_provider_check = map_set_custom_provider;
[[maybe_unused]]
static yetty_ymap_set_center_fn yetty_ymap_map_yetty_ymap_set_center_check = map_set_center;
[[maybe_unused]]
static yetty_ymap_set_zoom_fn yetty_ymap_map_yetty_ymap_set_zoom_check = map_set_zoom;
[[maybe_unused]]
static yetty_ymap_set_viewport_fn yetty_ymap_map_yetty_ymap_set_viewport_check = map_set_viewport;
[[maybe_unused]]
static yetty_ymap_pan_by_pixels_fn yetty_ymap_map_yetty_ymap_pan_by_pixels_check = map_pan_by_pixels;
[[maybe_unused]]
static yetty_ymap_zoom_by_at_fn yetty_ymap_map_yetty_ymap_zoom_by_at_check = map_zoom_by_at;
[[maybe_unused]]
static yetty_ymap_get_zoom_fn yetty_ymap_map_yetty_ymap_get_zoom_check = map_get_zoom;
[[maybe_unused]]
static yetty_ymap_geolocate_fn yetty_ymap_map_yetty_ymap_geolocate_check = map_geolocate;
[[maybe_unused]]
static yetty_ymap_attribution_fn yetty_ymap_map_yetty_ymap_attribution_check = map_attribution;
[[maybe_unused]]
static yetty_ymap_is_vector_fn yetty_ymap_map_yetty_ymap_is_vector_check = map_is_vector;
[[maybe_unused]]
static yetty_ymap_render_fn yetty_ymap_map_yetty_ymap_render_check = map_render;
[[maybe_unused]]
static yetty_ymap_destroy_fn yetty_ymap_map_yetty_ymap_destroy_check = map_destroy;

struct yetty_yclass_ptr_result yetty_ymap_map_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) return YETTY_OK(yetty_yclass_ptr, cls);
    ydebug("registering class=yetty_ymap_map");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ymap_map",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ymap_map),
        .data_align = _Alignof(struct yetty_ymap_map),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ymap", "configure", (yetty_yclass_method_id_t)yetty_ymap_configure, (yetty_yclass_impl_t)map_configure},
        {"yetty_ymap", "set_provider", (yetty_yclass_method_id_t)yetty_ymap_set_provider, (yetty_yclass_impl_t)map_set_provider},
        {"yetty_ymap", "set_custom_provider", (yetty_yclass_method_id_t)yetty_ymap_set_custom_provider, (yetty_yclass_impl_t)map_set_custom_provider},
        {"yetty_ymap", "set_center", (yetty_yclass_method_id_t)yetty_ymap_set_center, (yetty_yclass_impl_t)map_set_center},
        {"yetty_ymap", "set_zoom", (yetty_yclass_method_id_t)yetty_ymap_set_zoom, (yetty_yclass_impl_t)map_set_zoom},
        {"yetty_ymap", "set_viewport", (yetty_yclass_method_id_t)yetty_ymap_set_viewport, (yetty_yclass_impl_t)map_set_viewport},
        {"yetty_ymap", "pan_by_pixels", (yetty_yclass_method_id_t)yetty_ymap_pan_by_pixels, (yetty_yclass_impl_t)map_pan_by_pixels},
        {"yetty_ymap", "zoom_by_at", (yetty_yclass_method_id_t)yetty_ymap_zoom_by_at, (yetty_yclass_impl_t)map_zoom_by_at},
        {"yetty_ymap", "get_zoom", (yetty_yclass_method_id_t)yetty_ymap_get_zoom, (yetty_yclass_impl_t)map_get_zoom},
        {"yetty_ymap", "geolocate", (yetty_yclass_method_id_t)yetty_ymap_geolocate, (yetty_yclass_impl_t)map_geolocate},
        {"yetty_ymap", "attribution", (yetty_yclass_method_id_t)yetty_ymap_attribution, (yetty_yclass_impl_t)map_attribution},
        {"yetty_ymap", "is_vector", (yetty_yclass_method_id_t)yetty_ymap_is_vector, (yetty_yclass_impl_t)map_is_vector},
        {"yetty_ymap", "render", (yetty_yclass_method_id_t)yetty_ymap_render, (yetty_yclass_impl_t)map_render},
        {"yetty_ymap", "destroy", (yetty_yclass_method_id_t)yetty_ymap_destroy, (yetty_yclass_impl_t)map_destroy},
    };
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                              NULL, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ymap_map_class_get: class_register failed: %s", register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ymap_map_class_get: class_register failed", register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ymap_map_ptr_result yetty_ymap_map_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ymap_map_class_get();
    if (YETTY_IS_ERR(class_r))
        return YETTY_ERR(yetty_ymap_map_ptr, "yetty_ymap_map_from: class accessor", class_r);
    struct yetty_yclass_void_ptr_result slice_r =
        yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r))
        return YETTY_ERR(yetty_ymap_map_ptr, "yetty_ymap_map_from: object_data", slice_r);
    return YETTY_OK(yetty_ymap_map_ptr, (struct yetty_ymap_map *)slice_r.value);
}

struct yetty_yclass_object *yetty_ymap_map_to(struct yetty_ymap_map *data)
{
    if (!data)
        return NULL;
    struct yetty_yclass_ptr_result class_r = yetty_ymap_map_class_get();
    if (YETTY_IS_ERR(class_r)) {
        yetty_ycore_error_destroy(class_r.error);
        return NULL;
    }
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    if (YETTY_IS_ERR(offset_r)) {
        yetty_ycore_error_destroy(offset_r.error);
        return NULL;
    }
    return (struct yetty_yclass_object *)((char *)data - offset_r.value);
}


struct yetty_ycore_void_result yetty_ymap_configure(struct yetty_yclass_object * obj, double latitude, double longitude, uint32_t zoom, uint32_t width_px, uint32_t height_px)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ymap", (yetty_yclass_method_id_t)yetty_ymap_configure);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_ymap_configure: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_ymap_configure: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_ymap_configure: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_ymap_configure: dispatch_lookup failed");
    return ((yetty_ymap_configure_fn)dispatch_impl_r.value)(obj, latitude, longitude, zoom, width_px, height_px);
}

struct yetty_ycore_void_result yetty_ymap_set_provider(struct yetty_yclass_object * obj, const char * name)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ymap", (yetty_yclass_method_id_t)yetty_ymap_set_provider);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_ymap_set_provider: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_ymap_set_provider: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_ymap_set_provider: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_ymap_set_provider: dispatch_lookup failed");
    return ((yetty_ymap_set_provider_fn)dispatch_impl_r.value)(obj, name);
}

struct yetty_ycore_void_result yetty_ymap_set_custom_provider(struct yetty_yclass_object * obj, const char * url_template, int is_vector, const char * file_extension, uint32_t max_zoom, const char * attribution)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ymap", (yetty_yclass_method_id_t)yetty_ymap_set_custom_provider);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_ymap_set_custom_provider: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_ymap_set_custom_provider: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_ymap_set_custom_provider: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_ymap_set_custom_provider: dispatch_lookup failed");
    return ((yetty_ymap_set_custom_provider_fn)dispatch_impl_r.value)(obj, url_template, is_vector, file_extension, max_zoom, attribution);
}

struct yetty_ycore_void_result yetty_ymap_set_center(struct yetty_yclass_object * obj, double latitude, double longitude)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ymap", (yetty_yclass_method_id_t)yetty_ymap_set_center);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_ymap_set_center: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_ymap_set_center: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_ymap_set_center: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_ymap_set_center: dispatch_lookup failed");
    return ((yetty_ymap_set_center_fn)dispatch_impl_r.value)(obj, latitude, longitude);
}

struct yetty_ycore_void_result yetty_ymap_set_zoom(struct yetty_yclass_object * obj, uint32_t zoom)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ymap", (yetty_yclass_method_id_t)yetty_ymap_set_zoom);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_ymap_set_zoom: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_ymap_set_zoom: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_ymap_set_zoom: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_ymap_set_zoom: dispatch_lookup failed");
    return ((yetty_ymap_set_zoom_fn)dispatch_impl_r.value)(obj, zoom);
}

struct yetty_ycore_void_result yetty_ymap_set_viewport(struct yetty_yclass_object * obj, uint32_t width_px, uint32_t height_px)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ymap", (yetty_yclass_method_id_t)yetty_ymap_set_viewport);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_ymap_set_viewport: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_ymap_set_viewport: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_ymap_set_viewport: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_ymap_set_viewport: dispatch_lookup failed");
    return ((yetty_ymap_set_viewport_fn)dispatch_impl_r.value)(obj, width_px, height_px);
}

struct yetty_ycore_void_result yetty_ymap_pan_by_pixels(struct yetty_yclass_object * obj, double delta_x, double delta_y)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ymap", (yetty_yclass_method_id_t)yetty_ymap_pan_by_pixels);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_ymap_pan_by_pixels: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_ymap_pan_by_pixels: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_ymap_pan_by_pixels: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_ymap_pan_by_pixels: dispatch_lookup failed");
    return ((yetty_ymap_pan_by_pixels_fn)dispatch_impl_r.value)(obj, delta_x, delta_y);
}

struct yetty_ycore_int_result yetty_ymap_zoom_by_at(struct yetty_yclass_object * obj, int32_t step, double anchor_x, double anchor_y)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ymap", (yetty_yclass_method_id_t)yetty_ymap_zoom_by_at);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_int, "yetty_ymap_zoom_by_at: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_int, "yetty_ymap_zoom_by_at: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, object_class_r, "yetty_ymap_zoom_by_at: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, dispatch_impl_r, "yetty_ymap_zoom_by_at: dispatch_lookup failed");
    return ((yetty_ymap_zoom_by_at_fn)dispatch_impl_r.value)(obj, step, anchor_x, anchor_y);
}

struct yetty_ycore_int_result yetty_ymap_get_zoom(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ymap", (yetty_yclass_method_id_t)yetty_ymap_get_zoom);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_int, "yetty_ymap_get_zoom: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_int, "yetty_ymap_get_zoom: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, object_class_r, "yetty_ymap_get_zoom: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, dispatch_impl_r, "yetty_ymap_get_zoom: dispatch_lookup failed");
    return ((yetty_ymap_get_zoom_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_void_result yetty_ymap_geolocate(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ymap", (yetty_yclass_method_id_t)yetty_ymap_geolocate);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_ymap_geolocate: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_ymap_geolocate: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_ymap_geolocate: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_ymap_geolocate: dispatch_lookup failed");
    return ((yetty_ymap_geolocate_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_const_char_ptr_result yetty_ymap_attribution(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ymap", (yetty_yclass_method_id_t)yetty_ymap_attribution);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_ymap_attribution: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_ymap_attribution: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, object_class_r, "yetty_ymap_attribution: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, dispatch_impl_r, "yetty_ymap_attribution: dispatch_lookup failed");
    return ((yetty_ymap_attribution_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_int_result yetty_ymap_is_vector(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ymap", (yetty_yclass_method_id_t)yetty_ymap_is_vector);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_int, "yetty_ymap_is_vector: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_int, "yetty_ymap_is_vector: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, object_class_r, "yetty_ymap_is_vector: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, dispatch_impl_r, "yetty_ymap_is_vector: dispatch_lookup failed");
    return ((yetty_ymap_is_vector_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ydraw_drawable_list_result yetty_ymap_render(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ymap", (yetty_yclass_method_id_t)yetty_ymap_render);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ydraw_drawable_list, "yetty_ymap_render: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ydraw_drawable_list, "yetty_ymap_render: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ydraw_drawable_list, object_class_r, "yetty_ymap_render: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ydraw_drawable_list, dispatch_impl_r, "yetty_ymap_render: dispatch_lookup failed");
    return ((yetty_ymap_render_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_void_result yetty_ymap_destroy(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ymap", (yetty_yclass_method_id_t)yetty_ymap_destroy);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_ymap_destroy: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_ymap_destroy: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_ymap_destroy: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_ymap_destroy: dispatch_lookup failed");
    return ((yetty_ymap_destroy_fn)dispatch_impl_r.value)(obj);
}

struct yetty_yclass_object_ptr_result yetty_ymap_map_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ymap_map_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ymap_map");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ymap_map_class_get();
    if (YETTY_IS_ERR(class_accessor_r))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ymap_map_create: class accessor failed", class_accessor_r);
    const struct yetty_yclass *klass = class_accessor_r.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result alloc_r =
            yetty_yclass_object_alloc(klass);
        if (YETTY_IS_ERR(alloc_r)) return alloc_r;
        return alloc_r;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result translate_class_r =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ymap_map");
        if (YETTY_IS_ERR(translate_class_r)) {
            yetty_ycore_error_print(stderr,
                "yetty_ymap_map_create: translate_class (degraded — will lazy-resolve)",
                translate_class_r.error);
            yetty_ycore_error_destroy(translate_class_r.error);
        }
    }

    uint64_t handle = 0;
    const char *class_name = "yetty_ymap_map";
    struct yetty_ycore_size_result create_call_r = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, class_name, strlen(class_name), &handle,
        sizeof(handle));
    if (YETTY_IS_ERR(create_call_r))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ymap_map_create: CREATE call failed", create_call_r);
    if (create_call_r.value != sizeof(handle) || !handle)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ymap_map_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yetty/yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *proxy = calloc(1, sizeof(*proxy));
    if (!proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ymap_map_create: calloc(proxy) failed");
    proxy->header.klass = klass;
    /* Link the session onto the proxy so its methods marshal over it — they
     * read obj->session instead of taking a ctx argument. */
    proxy->header.session = ctx->session;
    proxy->handle = handle;
    return YETTY_OK(yetty_yclass_object_ptr, &proxy->header);
}

