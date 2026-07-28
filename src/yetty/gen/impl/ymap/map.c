/* GENERATED — do not edit. */
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h> /* container_of, buffer */
#include <yetty/ytrace/ytrace.h>
#include <stdbool.h>
#include <stddef.h> /* NULL, size_t */
#include <stdint.h>
#include <stdio.h>  /* stderr */
#include <stdlib.h> /* calloc/free for proxy + buffer marshalling */
#include <string.h> /* memcpy/strcmp/strlen */

struct yetty_ycore_const_char_ptr_result;
struct yetty_ycore_int_result;
struct yetty_ycore_void_result;
struct yetty_ydraw_drawable_list_result;
struct yetty_ycore_void_result yetty_ymap_configure(struct yetty_yclass_object *obj,
                                                    double latitude, double longitude,
                                                    uint32_t zoom, uint32_t width_px,
                                                    uint32_t height_px);
struct yetty_ycore_void_result yetty_ymap_set_provider(struct yetty_yclass_object *obj,
                                                       const char *name);
struct yetty_ycore_void_result yetty_ymap_set_custom_provider(
    struct yetty_yclass_object *obj, const char *url_template, int is_vector,
    const char *file_extension, uint32_t max_zoom, const char *attribution);
struct yetty_ycore_void_result yetty_ymap_set_center(struct yetty_yclass_object *obj,
                                                     double latitude, double longitude);
struct yetty_ycore_void_result yetty_ymap_set_zoom(struct yetty_yclass_object *obj, uint32_t zoom);
struct yetty_ycore_void_result yetty_ymap_set_viewport(struct yetty_yclass_object *obj,
                                                       uint32_t width_px, uint32_t height_px);
struct yetty_ycore_void_result yetty_ymap_pan_by_pixels(struct yetty_yclass_object *obj,
                                                        double delta_x, double delta_y);
struct yetty_ycore_int_result yetty_ymap_zoom_by_at(struct yetty_yclass_object *obj, int32_t step,
                                                    double anchor_x, double anchor_y);
struct yetty_ycore_int_result yetty_ymap_get_zoom(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ymap_geolocate(struct yetty_yclass_object *obj);
struct yetty_ycore_const_char_ptr_result yetty_ymap_attribution(struct yetty_yclass_object *obj);
struct yetty_ycore_int_result yetty_ymap_is_vector(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ymap_overlay_geojson(struct yetty_yclass_object *obj,
                                                          const char *geojson_text);
struct yetty_ydraw_drawable_list_result yetty_ymap_render(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ymap_destroy(struct yetty_yclass_object *obj);
typedef struct yetty_ycore_void_result (*yetty_ymap_configure_fn)(struct yetty_yclass_object *,
                                                                  double, double, uint32_t,
                                                                  uint32_t, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_ymap_set_provider_fn)(struct yetty_yclass_object *,
                                                                     const char *);
typedef struct yetty_ycore_void_result (*yetty_ymap_set_custom_provider_fn)(
    struct yetty_yclass_object *, const char *, int, const char *, uint32_t, const char *);
typedef struct yetty_ycore_void_result (*yetty_ymap_set_center_fn)(struct yetty_yclass_object *,
                                                                   double, double);
typedef struct yetty_ycore_void_result (*yetty_ymap_set_zoom_fn)(struct yetty_yclass_object *,
                                                                 uint32_t);
typedef struct yetty_ycore_void_result (*yetty_ymap_set_viewport_fn)(struct yetty_yclass_object *,
                                                                     uint32_t, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_ymap_pan_by_pixels_fn)(struct yetty_yclass_object *,
                                                                      double, double);
typedef struct yetty_ycore_int_result (*yetty_ymap_zoom_by_at_fn)(struct yetty_yclass_object *,
                                                                  int32_t, double, double);
typedef struct yetty_ycore_int_result (*yetty_ymap_get_zoom_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ymap_geolocate_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_ymap_attribution_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_int_result (*yetty_ymap_is_vector_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ymap_overlay_geojson_fn)(
    struct yetty_yclass_object *, const char *);
typedef struct yetty_ydraw_drawable_list_result (*yetty_ymap_render_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ymap_destroy_fn)(struct yetty_yclass_object *);

YETTY_MAYBE_UNUSED
static yetty_ymap_configure_fn yetty_ymap_map_yetty_ymap_configure_check = map_configure;
YETTY_MAYBE_UNUSED
static yetty_ymap_set_provider_fn yetty_ymap_map_yetty_ymap_set_provider_check = map_set_provider;
YETTY_MAYBE_UNUSED
static yetty_ymap_set_custom_provider_fn yetty_ymap_map_yetty_ymap_set_custom_provider_check =
    map_set_custom_provider;
YETTY_MAYBE_UNUSED
static yetty_ymap_set_center_fn yetty_ymap_map_yetty_ymap_set_center_check = map_set_center;
YETTY_MAYBE_UNUSED
static yetty_ymap_set_zoom_fn yetty_ymap_map_yetty_ymap_set_zoom_check = map_set_zoom;
YETTY_MAYBE_UNUSED
static yetty_ymap_set_viewport_fn yetty_ymap_map_yetty_ymap_set_viewport_check = map_set_viewport;
YETTY_MAYBE_UNUSED
static yetty_ymap_pan_by_pixels_fn yetty_ymap_map_yetty_ymap_pan_by_pixels_check =
    map_pan_by_pixels;
YETTY_MAYBE_UNUSED
static yetty_ymap_zoom_by_at_fn yetty_ymap_map_yetty_ymap_zoom_by_at_check = map_zoom_by_at;
YETTY_MAYBE_UNUSED
static yetty_ymap_get_zoom_fn yetty_ymap_map_yetty_ymap_get_zoom_check = map_get_zoom;
YETTY_MAYBE_UNUSED
static yetty_ymap_geolocate_fn yetty_ymap_map_yetty_ymap_geolocate_check = map_geolocate;
YETTY_MAYBE_UNUSED
static yetty_ymap_attribution_fn yetty_ymap_map_yetty_ymap_attribution_check = map_attribution;
YETTY_MAYBE_UNUSED
static yetty_ymap_is_vector_fn yetty_ymap_map_yetty_ymap_is_vector_check = map_is_vector;
YETTY_MAYBE_UNUSED
static yetty_ymap_overlay_geojson_fn yetty_ymap_map_yetty_ymap_overlay_geojson_check =
    map_overlay_geojson;
YETTY_MAYBE_UNUSED
static yetty_ymap_render_fn yetty_ymap_map_yetty_ymap_render_check = map_render;
YETTY_MAYBE_UNUSED
static yetty_ymap_destroy_fn yetty_ymap_map_yetty_ymap_destroy_check = map_destroy;

struct yetty_yclass_ptr_result yetty_ymap_map_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ymap_map");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ymap_map",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ymap_map),
        .data_align = _Alignof(struct yetty_ymap_map),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ymap", "configure", (yetty_yclass_method_id_t)yetty_ymap_configure,
         (yetty_yclass_impl_t)map_configure},
        {"yetty_ymap", "set_provider", (yetty_yclass_method_id_t)yetty_ymap_set_provider,
         (yetty_yclass_impl_t)map_set_provider},
        {"yetty_ymap", "set_custom_provider",
         (yetty_yclass_method_id_t)yetty_ymap_set_custom_provider,
         (yetty_yclass_impl_t)map_set_custom_provider},
        {"yetty_ymap", "set_center", (yetty_yclass_method_id_t)yetty_ymap_set_center,
         (yetty_yclass_impl_t)map_set_center},
        {"yetty_ymap", "set_zoom", (yetty_yclass_method_id_t)yetty_ymap_set_zoom,
         (yetty_yclass_impl_t)map_set_zoom},
        {"yetty_ymap", "set_viewport", (yetty_yclass_method_id_t)yetty_ymap_set_viewport,
         (yetty_yclass_impl_t)map_set_viewport},
        {"yetty_ymap", "pan_by_pixels", (yetty_yclass_method_id_t)yetty_ymap_pan_by_pixels,
         (yetty_yclass_impl_t)map_pan_by_pixels},
        {"yetty_ymap", "zoom_by_at", (yetty_yclass_method_id_t)yetty_ymap_zoom_by_at,
         (yetty_yclass_impl_t)map_zoom_by_at},
        {"yetty_ymap", "get_zoom", (yetty_yclass_method_id_t)yetty_ymap_get_zoom,
         (yetty_yclass_impl_t)map_get_zoom},
        {"yetty_ymap", "geolocate", (yetty_yclass_method_id_t)yetty_ymap_geolocate,
         (yetty_yclass_impl_t)map_geolocate},
        {"yetty_ymap", "attribution", (yetty_yclass_method_id_t)yetty_ymap_attribution,
         (yetty_yclass_impl_t)map_attribution},
        {"yetty_ymap", "is_vector", (yetty_yclass_method_id_t)yetty_ymap_is_vector,
         (yetty_yclass_impl_t)map_is_vector},
        {"yetty_ymap", "overlay_geojson", (yetty_yclass_method_id_t)yetty_ymap_overlay_geojson,
         (yetty_yclass_impl_t)map_overlay_geojson},
        {"yetty_ymap", "render", (yetty_yclass_method_id_t)yetty_ymap_render,
         (yetty_yclass_impl_t)map_render},
        {"yetty_ymap", "destroy", (yetty_yclass_method_id_t)yetty_ymap_destroy,
         (yetty_yclass_impl_t)map_destroy},
    };
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]), NULL, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ymap_map_class_get: class_register failed: %s", register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ymap_map_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ymap_map_ptr_result yetty_ymap_map_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ymap_map_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ymap_map_ptr, "yetty_ymap_map_from: class accessor", class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ymap_map_ptr, "yetty_ymap_map_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_ymap_map_ptr, (struct yetty_ymap_map *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ymap_map_to(struct yetty_ymap_map *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_ymap_map_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r, "yetty_ymap_map_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_ymap_map_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct yetty_yclass_object_ptr_result yetty_ymap_map_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ymap_map_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ymap_map");
    if (ctx && ctx->session) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ymap_map_create: remote create unsupported for a split-mode class; "
                         "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ymap_map_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ymap_map_create: class accessor failed",
                         class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) {
        return alloc_r;
    }
    return alloc_r;
}

/* Forward decls. A class tagged platform@<x> is registered only on
 * that platform: its accessor/skel decls and its registration entry
 * are wrapped in #ifdef YETTY_PLATFORM_<X>, where CMake compiles the
 * class .c. A cross-platform class is a plain strong ref, defined in
 * the same library and pulled in when register() is. Submodule
 * registers are chained as strong externs (always co-linked). */
struct yetty_yclass_ptr_result yetty_ymap_map_class_get(void);
struct yetty_ycore_void_result yetty_ymap_register(void);

/* ---- ymap: class name -> accessor (lazy) ---------------------- */

static struct yetty_yclass_ptr_result yetty_ymap_accessor_lookup(const char *name)
{
    if (strcmp(name, "yetty_ymap_map") == 0) {
        return yetty_ymap_map_class_get();
    }
    /* "Not mine": OK with NULL value -- yetty_yclass_by_name walks to next hook. */
    return YETTY_OK(yetty_yclass_ptr, NULL);
}

/* ---- ymap: explicit yclass-RPC hook registration ------------- */

struct yetty_ycore_void_result yetty_ymap_register(void)
{
    static bool registered = false;
    if (registered) {
        return YETTY_OK_VOID();
    }

    struct yetty_ycore_void_result add_accessor_r =
        yetty_yclass_add_accessor_lookup(yetty_ymap_accessor_lookup);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_accessor_r,
                        "yetty_ymap_register: add_accessor_lookup");
    registered = true;
    return YETTY_OK_VOID();
}
