/* GENERATED — do not edit. */
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>
#include <stddef.h>  /* NULL, size_t */

struct yetty_ycore_const_char_ptr_result;
struct yetty_ycore_int_result;
struct yetty_ycore_void_result;
struct yetty_ydraw_drawable_list_result;
struct yetty_ycore_void_result yetty_ymap_configure(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, double latitude, double longitude, uint32_t zoom, uint32_t width_px, uint32_t height_px);
struct yetty_ycore_void_result yetty_ymap_set_provider(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, const char * name);
struct yetty_ycore_void_result yetty_ymap_set_custom_provider(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, const char * url_template, int is_vector, const char * file_extension, uint32_t max_zoom, const char * attribution);
struct yetty_ycore_void_result yetty_ymap_set_center(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, double latitude, double longitude);
struct yetty_ycore_void_result yetty_ymap_set_zoom(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, uint32_t zoom);
struct yetty_ycore_void_result yetty_ymap_set_viewport(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, uint32_t width_px, uint32_t height_px);
struct yetty_ycore_void_result yetty_ymap_pan_by_pixels(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, double delta_x, double delta_y);
struct yetty_ycore_int_result yetty_ymap_zoom_by_at(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, int32_t step, double anchor_x, double anchor_y);
struct yetty_ycore_int_result yetty_ymap_get_zoom(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_ymap_geolocate(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj);
struct yetty_ycore_const_char_ptr_result yetty_ymap_attribution(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj);
struct yetty_ycore_int_result yetty_ymap_is_vector(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj);
struct yetty_ydraw_drawable_list_result yetty_ymap_render(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_ymap_destroy(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj);
typedef struct yetty_ycore_void_result (*yetty_ymap_configure_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *, double, double, uint32_t, uint32_t, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_ymap_set_provider_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *, const char *);
typedef struct yetty_ycore_void_result (*yetty_ymap_set_custom_provider_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *, const char *, int, const char *, uint32_t, const char *);
typedef struct yetty_ycore_void_result (*yetty_ymap_set_center_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *, double, double);
typedef struct yetty_ycore_void_result (*yetty_ymap_set_zoom_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_ymap_set_viewport_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *, uint32_t, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_ymap_pan_by_pixels_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *, double, double);
typedef struct yetty_ycore_int_result (*yetty_ymap_zoom_by_at_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *, int32_t, double, double);
typedef struct yetty_ycore_int_result (*yetty_ymap_get_zoom_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ymap_geolocate_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_ymap_attribution_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_int_result (*yetty_ymap_is_vector_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ydraw_drawable_list_result (*yetty_ymap_render_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ymap_destroy_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *);

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
