/* GENERATED — do not edit. */
/* Object API for regular class(es) `map` (implementation module: ymap).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YMAP_MAP_H
#define YETTY_YCLASSGEN_API_YMAP_MAP_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw-core/drawable-list.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ydraw_drawable_list;

struct yetty_yclass_ptr_result yetty_ymap_map_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ymap_map;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YMAP_MAP_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YMAP_MAP_PTR_RESULT
struct yetty_ymap_map_ptr_result {
    int ok;
    union {
        struct yetty_ymap_map *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ymap_map_ptr_result yetty_ymap_map_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ymap_map_to(struct yetty_ymap_map *data);

/* configure: set the whole view in one call. zoom is clamped to the
 * active provider's range; viewport to the engine's sanity bounds. */
struct yetty_ycore_void_result yetty_ymap_configure(struct yetty_yclass_object *obj,
                                                    double latitude, double longitude,
                                                    uint32_t zoom, uint32_t width_px,
                                                    uint32_t height_px);
/* set_provider: select a built-in provider by name (see
 * yetty_ymap_provider_info for enumeration). Clears any custom provider;
 * clamps the current zoom into the new provider's range. */
struct yetty_ycore_void_result yetty_ymap_set_provider(struct yetty_yclass_object *obj,
                                                       const char *name);
/* set_custom_provider: arbitrary XYZ tile server. `url_template` is
 * printf-style with three unsigned slots in z, x, y order (POSIX
 * positional %1$u.. reorder for z/y/x servers). `attribution` is what the
 * frontend must display; pass the provider's required credit line. */
struct yetty_ycore_void_result yetty_ymap_set_custom_provider(
    struct yetty_yclass_object *obj, const char *url_template, int is_vector,
    const char *file_extension, uint32_t max_zoom, const char *attribution);
/* set_center / set_zoom / set_viewport: individual view setters. */
struct yetty_ycore_void_result yetty_ymap_set_center(struct yetty_yclass_object *obj,
                                                     double latitude, double longitude);
struct yetty_ycore_void_result yetty_ymap_set_zoom(struct yetty_yclass_object *obj, uint32_t zoom);
struct yetty_ycore_void_result yetty_ymap_set_viewport(struct yetty_yclass_object *obj,
                                                       uint32_t width_px, uint32_t height_px);
/* pan_by_pixels: shift the view by a viewport-pixel delta — content
 * follows the pointer (drag right → map moves right → center moves
 * west). Latitude clamps so the viewport stays on the Mercator square. */
struct yetty_ycore_void_result yetty_ymap_pan_by_pixels(struct yetty_yclass_object *obj,
                                                        double delta_x, double delta_y);
/* zoom_by_at: zoom by `step` levels keeping the viewport pixel
 * (anchor_x, anchor_y) on the same lat/lon — the wheel-zoom feel.
 * Returns the (clamped) new zoom level. */
struct yetty_ycore_int_result yetty_ymap_zoom_by_at(struct yetty_yclass_object *obj, int32_t step,
                                                    double anchor_x, double anchor_y);
/* get_zoom: current zoom level (after clamping). */
struct yetty_ycore_int_result yetty_ymap_get_zoom(struct yetty_yclass_object *obj);
/* geolocate: center the view on this machine's public-IP location (one
 * HTTPS request, city-level accuracy, see ymap's geoip notes).
 * Best-effort: on error the view is unchanged and the caller decides. */
struct yetty_ycore_void_result yetty_ymap_geolocate(struct yetty_yclass_object *obj);
/* attribution: the credit line the frontend MUST display with the map. */
struct yetty_ycore_const_char_ptr_result yetty_ymap_attribution(struct yetty_yclass_object *obj);
/* is_vector: 1 when the active provider renders through the vector
 * (SDF/MSDF) pipeline — frontends use it to pick drag-render pacing. */
struct yetty_ycore_int_result yetty_ymap_is_vector(struct yetty_yclass_object *obj);
/* overlay_geojson: load a GeoJSON document (FeatureCollection / Feature /
 * bare geometry) as the map's data overlay, replacing any previous one.
 * Point/MultiPoint become markers (radius from a numeric `mag` property
 * when present), LineString/MultiLineString become tracks, Polygon outer
 * rings become outlines. */
struct yetty_ycore_void_result yetty_ymap_overlay_geojson(struct yetty_yclass_object *obj,
                                                          const char *geojson_text);
/* render: fetch the covering tiles (parallel, disk-cached) and emit the
 * visible map as a fresh ydraw drawable list (caller owns it). Pointer
 * return -> local-only. */
struct yetty_ydraw_drawable_list_result yetty_ymap_render(struct yetty_yclass_object *obj);
/* destroy: free owned strings + the yclass allocation. */
struct yetty_ycore_void_result yetty_ymap_destroy(struct yetty_yclass_object *obj);

struct yetty_yclass_object_ptr_result yetty_ymap_map_create(struct yetty_yclass_ctx *ctx);

/* Number of built-in providers. */
uint32_t yetty_ymap_provider_count(void);
/* Metadata of built-in provider `index` (0..count-1). Any out pointer may
 * be NULL. Strings are static — never freed by the caller. */
struct yetty_ycore_void_result yetty_ymap_provider_info(uint32_t index, const char **out_name,
                                                        const char **out_attribution,
                                                        uint32_t *out_max_zoom, int *out_is_vector);
/* One-shot helper — serialize a rendered drawable list as a YDRAW_BIN DCS
 * envelope (the ycat/scrolling path). Exposed as a free function for CLIs. */
struct yetty_ycore_void_result yetty_ymap_emit_osc(const struct yetty_ydraw_drawable_list *list,
                                                   int fd);

#ifdef __cplusplus
}
#endif

#endif
