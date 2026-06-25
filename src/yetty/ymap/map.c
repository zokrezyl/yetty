/*
 * map.c — yclass class `ymap:map`: a slippy-map client/model.
 *
 * `map` is a client-side object: it holds a view (center lat/lon, zoom,
 * viewport pixels) over a named TILE PROVIDER and renders the visible map
 * to a ydraw drawable list via the ymap engine — raster providers
 * composite PNG/JPEG tiles into one yimage prim, vector providers
 * (shortbread MVT) emit native SDF/MSDF drawables. It does not display
 * itself: one-shot callers ship the list into the scrolling layer
 * (YDRAW_BIN, ycat-style), interactive frontends ship it to a server
 * figure via yview and drive pan/zoom by calling the navigation methods
 * and re-rendering.
 *
 * Being a yclass class, `make codegen` emits the public header (map.h),
 * the method dispatch, model.yaml, and the FFI / host-language binding
 * surface — Python or Lua get the same pan/zoom/render verbs the C tool
 * uses. Slots are `local@`: render returns a pointer (in-process only),
 * and the navigation math is trivial — a remote frontend re-creates the
 * model locally and ships drawable lists instead of proxying calls.
 *
 * Providers are a built-in registry (keyless, attribution-required tile
 * servers — OSM raster + vector, OpenTopoMap, CyclOSM, HOT, CARTO
 * basemaps, NASA GIBS + Sentinel-2 satellite) plus a custom slot for
 * arbitrary printf-style XYZ templates. Every provider carries its
 * required attribution line; frontends MUST display it.
 */
/* This TU deliberately does NOT include its own generated public header
 * `yetty/ymap/map.h` — that header is a downstream artifact for other
 * modules. The foundational types this TU and the appended map.gen.c need
 * are pulled in directly here, and this TU declares its own
 * `yetty_ymap_map_ptr_result` below (the same one map.h publishes). */
#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#include <yetty/yface/yface.h>
#include <yetty/ymap/engine.h>
#include <yetty/yplatform/paths.h>
#include <yetty/yterminal/dcs-codes.h>
#include <yetty/ytrace/ytrace.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Viewport sanity bounds (the engine enforces its own as well). */
enum {
    YMAP_MIN_VIEWPORT_PX = 64,
    YMAP_MAX_VIEWPORT_PX = 4096,
    YMAP_DEFAULT_ZOOM = 13,
    YMAP_DEFAULT_VIEWPORT_W = 640,
    YMAP_DEFAULT_VIEWPORT_H = 400,
};

/*=============================================================================
 * Provider registry
 *===========================================================================*/

struct ymap_provider {
    const char *name;
    int vector; /* 1 = shortbread MVT through the vector renderer */
    /* printf-style template; %u slots in z, x, y order. POSIX positional
     * specifiers (%1$u/%3$u/%2$u) reorder for z/y/x servers (WMTS). */
    const char *url_template;
    const char *file_extension;
    uint32_t max_zoom;
    const char *attribution;
};

/* The built-in providers. All keyless; all require the listed attribution
 * to be displayed with the map. Returned table is NULL-name terminated. */
static const struct ymap_provider *ymap_provider_table(void)
{
    static const struct ymap_provider providers[] = {
        {"osm", 0, "https://tile.openstreetmap.org/%u/%u/%u.png", "png", 19,
         "(C) OpenStreetMap contributors"},
        {"osm-vector", 1, "https://vector.openstreetmap.org/shortbread_v1/%u/%u/%u.mvt", "mvt", 14,
         "(C) OpenStreetMap contributors"},
        {"opentopomap", 0, "https://tile.opentopomap.org/%u/%u/%u.png", "png", 17,
         "(C) OpenStreetMap contributors, SRTM | style (C) OpenTopoMap (CC-BY-SA)"},
        {"cyclosm", 0, "https://a.tile-cyclosm.openstreetmap.fr/cyclosm/%u/%u/%u.png", "png", 19,
         "(C) OpenStreetMap contributors | style CyclOSM"},
        {"osm-hot", 0, "https://tile-c.openstreetmap.fr/hot/%u/%u/%u.png", "png", 19,
         "(C) OpenStreetMap contributors | tiles Humanitarian OpenStreetMap Team"},
        {"osmfr", 0, "https://tile-c.openstreetmap.fr/osmfr/%u/%u/%u.png", "png", 20,
         "(C) OpenStreetMap contributors | tiles OSM France"},
        {"carto-light", 0, "https://basemaps.cartocdn.com/light_all/%u/%u/%u.png", "png", 20,
         "(C) OpenStreetMap contributors (C) CARTO"},
        {"carto-dark", 0, "https://basemaps.cartocdn.com/dark_all/%u/%u/%u.png", "png", 20,
         "(C) OpenStreetMap contributors (C) CARTO"},
        {"carto-voyager", 0, "https://basemaps.cartocdn.com/rastertiles/voyager/%u/%u/%u.png",
         "png", 20, "(C) OpenStreetMap contributors (C) CARTO"},
        {"gibs-bluemarble", 0,
         "https://gibs.earthdata.nasa.gov/wmts/epsg3857/best/"
         "BlueMarble_ShadedRelief_Bathymetry/default/GoogleMapsCompatible_Level8/%1$u/%3$u/%2$u"
         ".jpeg",
         "jpeg", 8, "Imagery courtesy NASA Global Imagery Browse Services (GIBS)"},
        {"s2cloudless", 0,
         "https://tiles.maps.eox.at/wmts/1.0.0/s2cloudless-2020_3857/default/g/%1$u/%3$u/%2$u.jpg",
         "jpg", 15, "Sentinel-2 cloudless by EOX IT Services (CC-BY-NC-SA 4.0), Copernicus data"},
        {NULL, 0, NULL, NULL, 0, NULL},
    };
    return providers;
}

static const struct ymap_provider *ymap_provider_by_name(const char *name)
{
    if (!name) {
        return NULL;
    }
    const struct ymap_provider *providers = ymap_provider_table();
    for (size_t i = 0; providers[i].name; i++) {
        if (strcmp(providers[i].name, name) == 0) {
            return &providers[i];
        }
    }
    return NULL;
}

/*=============================================================================
 * Class data
 *===========================================================================*/

struct YETTY_ANNOTATE("class@ymap:map") YETTY_ANNOTATE("include@yetty/ydraw-core/drawable-list.h")
    yetty_ymap_map {
    double latitude;
    double longitude;
    uint32_t zoom;
    uint32_t width_px;
    uint32_t height_px;

    /* Active provider: a row of the built-in table, or the custom slot
     * below when `custom_url_template` is set. */
    const struct ymap_provider *provider;
    char *custom_url_template; /* owned; NULL = built-in provider */
    char *custom_attribution;  /* owned */
    char custom_extension[16];
    uint32_t custom_max_zoom;
    int custom_vector;
};

/* Result wrapper for the map handle. Declared here (not pulled from map.h,
 * which this TU does not include) so the appended map.gen.c — which defines
 * yetty_ymap_map_from() returning it — has the type in scope. */
YETTY_YRESULT_DECLARE(yetty_ymap_map_ptr, struct yetty_ymap_map *);

/* Defined in the appended map.gen.c (foot of this TU). */
struct yetty_yclass_ptr_result yetty_ymap_map_class_get(void);
struct yetty_ymap_map_ptr_result yetty_ymap_map_from(struct yetty_yclass_object *obj);

static struct yetty_yclass_void_ptr_result map_from_obj(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ymap_map_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, class_r, "map_from_obj: class_get");
    return yetty_yclass_object_data(obj, class_r.value);
}

/* Resolve the active provider fields regardless of built-in vs custom. */
static void map_active_provider(const struct yetty_ymap_map *map, const char **out_template,
                                const char **out_extension, uint32_t *out_max_zoom, int *out_vector,
                                const char **out_attribution)
{
    if (map->custom_url_template) {
        *out_template = map->custom_url_template;
        *out_extension = map->custom_extension[0] ? map->custom_extension : NULL;
        *out_max_zoom = map->custom_max_zoom;
        *out_vector = map->custom_vector;
        *out_attribution = map->custom_attribution ? map->custom_attribution : "";
        return;
    }
    const struct ymap_provider *provider =
        map->provider ? map->provider : &ymap_provider_table()[0];
    *out_template = provider->url_template;
    *out_extension = provider->file_extension;
    *out_max_zoom = provider->max_zoom;
    *out_vector = provider->vector;
    *out_attribution = provider->attribution;
}

static uint32_t map_clamp_zoom(const struct yetty_ymap_map *map, int64_t zoom)
{
    const char *url_template;
    const char *extension;
    uint32_t max_zoom;
    int vector;
    const char *attribution;
    map_active_provider(map, &url_template, &extension, &max_zoom, &vector, &attribution);
    if (zoom < 0) {
        return 0;
    }
    if (zoom > (int64_t)max_zoom) {
        return max_zoom;
    }
    return (uint32_t)zoom;
}

/*=============================================================================
 * Method slots
 *===========================================================================*/

/* configure: set the whole view in one call. zoom is clamped to the
 * active provider's range; viewport to the engine's sanity bounds. */
YETTY_ANNOTATE("virtual@ymap:map:configure")
YETTY_ANNOTATE("local@ymap:configure")
static struct yetty_ycore_void_result
    map_configure(struct yetty_yclass_object *obj, double latitude, double longitude, uint32_t zoom,
                  uint32_t width_px, uint32_t height_px)
{
    struct yetty_yclass_void_ptr_result map_r = map_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, map_r, "ymap configure: from_obj");
    struct yetty_ymap_map *map = map_r.value;
    if (latitude < -85.06 || latitude > 85.06 || longitude < -180.0 || longitude > 180.0) {
        return YETTY_ERR(yetty_ycore_void, "ymap configure: lat/lon out of range");
    }
    if (width_px < YMAP_MIN_VIEWPORT_PX || width_px > YMAP_MAX_VIEWPORT_PX ||
        height_px < YMAP_MIN_VIEWPORT_PX || height_px > YMAP_MAX_VIEWPORT_PX) {
        return YETTY_ERR(yetty_ycore_void, "ymap configure: viewport out of range");
    }
    map->latitude = latitude;
    map->longitude = longitude;
    map->zoom = map_clamp_zoom(map, (int64_t)zoom);
    map->width_px = width_px;
    map->height_px = height_px;
    return YETTY_OK_VOID();
}

/* set_provider: select a built-in provider by name (see
 * yetty_ymap_provider_info for enumeration). Clears any custom provider;
 * clamps the current zoom into the new provider's range. */
YETTY_ANNOTATE("virtual@ymap:map:set_provider")
YETTY_ANNOTATE("local@ymap:set_provider")
static struct yetty_ycore_void_result
    map_set_provider(struct yetty_yclass_object *obj, const char *name)
{
    struct yetty_yclass_void_ptr_result map_r = map_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, map_r, "ymap set_provider: from_obj");
    struct yetty_ymap_map *map = map_r.value;
    const struct ymap_provider *provider = ymap_provider_by_name(name);
    if (!provider) {
        return YETTY_ERR(yetty_ycore_void, "ymap set_provider: unknown provider name");
    }
    free(map->custom_url_template);
    free(map->custom_attribution);
    map->custom_url_template = NULL;
    map->custom_attribution = NULL;
    map->provider = provider;
    map->zoom = map_clamp_zoom(map, (int64_t)map->zoom);
    return YETTY_OK_VOID();
}

/* set_custom_provider: arbitrary XYZ tile server. `url_template` is
 * printf-style with three unsigned slots in z, x, y order (POSIX
 * positional %1$u.. reorder for z/y/x servers). `attribution` is what the
 * frontend must display; pass the provider's required credit line. */
YETTY_ANNOTATE("virtual@ymap:map:set_custom_provider")
YETTY_ANNOTATE("local@ymap:set_custom_provider")
static struct yetty_ycore_void_result
    map_set_custom_provider(struct yetty_yclass_object *obj, const char *url_template,
                            int is_vector, const char *file_extension, uint32_t max_zoom,
                            const char *attribution)
{
    struct yetty_yclass_void_ptr_result map_r = map_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, map_r, "ymap set_custom_provider: from_obj");
    struct yetty_ymap_map *map = map_r.value;
    if (!url_template || !url_template[0]) {
        return YETTY_ERR(yetty_ycore_void, "ymap set_custom_provider: empty url_template");
    }
    char *template_copy = strdup(url_template);
    char *attribution_copy = strdup(attribution ? attribution : "");
    if (!template_copy || !attribution_copy) {
        free(template_copy);
        free(attribution_copy);
        return YETTY_ERR(yetty_ycore_void, "ymap set_custom_provider: alloc failed");
    }
    free(map->custom_url_template);
    free(map->custom_attribution);
    map->custom_url_template = template_copy;
    map->custom_attribution = attribution_copy;
    snprintf(map->custom_extension, sizeof(map->custom_extension), "%s",
             (file_extension && file_extension[0]) ? file_extension : (is_vector ? "mvt" : "png"));
    map->custom_max_zoom = max_zoom ? max_zoom : (is_vector ? 14u : 19u);
    map->custom_vector = is_vector ? 1 : 0;
    map->zoom = map_clamp_zoom(map, (int64_t)map->zoom);
    return YETTY_OK_VOID();
}

/* set_center / set_zoom / set_viewport: individual view setters. */
YETTY_ANNOTATE("virtual@ymap:map:set_center")
YETTY_ANNOTATE("local@ymap:set_center")
static struct yetty_ycore_void_result
    map_set_center(struct yetty_yclass_object *obj, double latitude, double longitude)
{
    struct yetty_yclass_void_ptr_result map_r = map_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, map_r, "ymap set_center: from_obj");
    struct yetty_ymap_map *map = map_r.value;
    if (latitude < -85.06 || latitude > 85.06 || longitude < -180.0 || longitude > 180.0) {
        return YETTY_ERR(yetty_ycore_void, "ymap set_center: lat/lon out of range");
    }
    map->latitude = latitude;
    map->longitude = longitude;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("virtual@ymap:map:set_zoom")
YETTY_ANNOTATE("local@ymap:set_zoom")
static struct yetty_ycore_void_result map_set_zoom(struct yetty_yclass_object *obj, uint32_t zoom)
{
    struct yetty_yclass_void_ptr_result map_r = map_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, map_r, "ymap set_zoom: from_obj");
    struct yetty_ymap_map *map = map_r.value;
    map->zoom = map_clamp_zoom(map, (int64_t)zoom);
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("virtual@ymap:map:set_viewport")
YETTY_ANNOTATE("local@ymap:set_viewport")
static struct yetty_ycore_void_result
    map_set_viewport(struct yetty_yclass_object *obj, uint32_t width_px, uint32_t height_px)
{
    struct yetty_yclass_void_ptr_result map_r = map_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, map_r, "ymap set_viewport: from_obj");
    struct yetty_ymap_map *map = map_r.value;
    if (width_px < YMAP_MIN_VIEWPORT_PX || width_px > YMAP_MAX_VIEWPORT_PX ||
        height_px < YMAP_MIN_VIEWPORT_PX || height_px > YMAP_MAX_VIEWPORT_PX) {
        return YETTY_ERR(yetty_ycore_void, "ymap set_viewport: viewport out of range");
    }
    map->width_px = width_px;
    map->height_px = height_px;
    return YETTY_OK_VOID();
}

/* pan_by_pixels: shift the view by a viewport-pixel delta — content
 * follows the pointer (drag right → map moves right → center moves
 * west). Latitude clamps so the viewport stays on the Mercator square. */
YETTY_ANNOTATE("virtual@ymap:map:pan_by_pixels")
YETTY_ANNOTATE("local@ymap:pan_by_pixels")
static struct yetty_ycore_void_result
    map_pan_by_pixels(struct yetty_yclass_object *obj, double delta_x, double delta_y)
{
    struct yetty_yclass_void_ptr_result map_r = map_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, map_r, "ymap pan_by_pixels: from_obj");
    struct yetty_ymap_map *map = map_r.value;

    double center_x = 0.0;
    double center_y = 0.0;
    yetty_ymap_lonlat_to_global_pixel(map->longitude, map->latitude, map->zoom, &center_x,
                                      &center_y);
    center_x -= delta_x;
    center_y -= delta_y;
    double world_px = (double)(1u << map->zoom) * 256.0;
    double half_h = (double)map->height_px / 2.0;
    if (center_y < half_h) {
        center_y = half_h;
    }
    if (center_y > world_px - half_h) {
        center_y = world_px - half_h;
    }
    yetty_ymap_global_pixel_to_lonlat(center_x, center_y, map->zoom, &map->longitude,
                                      &map->latitude);
    return YETTY_OK_VOID();
}

/* zoom_by_at: zoom by `step` levels keeping the viewport pixel
 * (anchor_x, anchor_y) on the same lat/lon — the wheel-zoom feel.
 * Returns the (clamped) new zoom level. */
YETTY_ANNOTATE("virtual@ymap:map:zoom_by_at")
YETTY_ANNOTATE("local@ymap:zoom_by_at")
static struct yetty_ycore_int_result
    map_zoom_by_at(struct yetty_yclass_object *obj, int32_t step, double anchor_x, double anchor_y)
{
    struct yetty_yclass_void_ptr_result map_r = map_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, map_r, "ymap zoom_by_at: from_obj");
    struct yetty_ymap_map *map = map_r.value;

    uint32_t new_zoom = map_clamp_zoom(map, (int64_t)map->zoom + step);
    if (new_zoom == map->zoom) {
        return YETTY_OK(yetty_ycore_int, (int)map->zoom);
    }

    double center_x = 0.0;
    double center_y = 0.0;
    yetty_ymap_lonlat_to_global_pixel(map->longitude, map->latitude, map->zoom, &center_x,
                                      &center_y);
    double origin_x = center_x - (double)map->width_px / 2.0;
    double origin_y = center_y - (double)map->height_px / 2.0;
    double anchor_gpx_x = origin_x + anchor_x;
    double anchor_gpx_y = origin_y + anchor_y;

    double scale = (new_zoom > map->zoom) ? (double)(1u << (new_zoom - map->zoom))
                                          : 1.0 / (double)(1u << (map->zoom - new_zoom));
    double new_center_x = anchor_gpx_x * scale - anchor_x + (double)map->width_px / 2.0;
    double new_center_y = anchor_gpx_y * scale - anchor_y + (double)map->height_px / 2.0;

    map->zoom = new_zoom;
    yetty_ymap_global_pixel_to_lonlat(new_center_x, new_center_y, map->zoom, &map->longitude,
                                      &map->latitude);
    return YETTY_OK(yetty_ycore_int, (int)map->zoom);
}

/* get_zoom: current zoom level (after clamping). */
YETTY_ANNOTATE("virtual@ymap:map:get_zoom")
YETTY_ANNOTATE("local@ymap:get_zoom")
static struct yetty_ycore_int_result map_get_zoom(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_void_ptr_result map_r = map_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, map_r, "ymap get_zoom: from_obj");
    struct yetty_ymap_map *map = map_r.value;
    return YETTY_OK(yetty_ycore_int, (int)map->zoom);
}

/* geolocate: center the view on this machine's public-IP location (one
 * HTTPS request, city-level accuracy, see ymap's geoip notes).
 * Best-effort: on error the view is unchanged and the caller decides. */
YETTY_ANNOTATE("virtual@ymap:map:geolocate")
YETTY_ANNOTATE("local@ymap:geolocate")
static struct yetty_ycore_void_result map_geolocate(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_void_ptr_result map_r = map_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, map_r, "ymap geolocate: from_obj");
    struct yetty_ymap_map *map = map_r.value;
    double latitude = 0.0;
    double longitude = 0.0;
    struct yetty_ycore_void_result geo_res = yetty_ymap_geolocate_public_ip(&latitude, &longitude);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, geo_res, "ymap geolocate: lookup");
    map->latitude = latitude;
    map->longitude = longitude;
    return YETTY_OK_VOID();
}

/* attribution: the credit line the frontend MUST display with the map. */
YETTY_ANNOTATE("virtual@ymap:map:attribution")
YETTY_ANNOTATE("local@ymap:attribution")
static struct yetty_ycore_const_char_ptr_result map_attribution(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_void_ptr_result map_r = map_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, map_r, "ymap attribution: from_obj");
    struct yetty_ymap_map *map = map_r.value;
    const char *url_template;
    const char *extension;
    uint32_t max_zoom;
    int vector;
    const char *attribution;
    map_active_provider(map, &url_template, &extension, &max_zoom, &vector, &attribution);
    return YETTY_OK(yetty_ycore_const_char_ptr, attribution);
}

/* is_vector: 1 when the active provider renders through the vector
 * (SDF/MSDF) pipeline — frontends use it to pick drag-render pacing. */
YETTY_ANNOTATE("virtual@ymap:map:is_vector")
YETTY_ANNOTATE("local@ymap:is_vector")
static struct yetty_ycore_int_result map_is_vector(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_void_ptr_result map_r = map_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, map_r, "ymap is_vector: from_obj");
    struct yetty_ymap_map *map = map_r.value;
    const char *url_template;
    const char *extension;
    uint32_t max_zoom;
    int vector;
    const char *attribution;
    map_active_provider(map, &url_template, &extension, &max_zoom, &vector, &attribution);
    return YETTY_OK(yetty_ycore_int, vector);
}

/* render: fetch the covering tiles (parallel, disk-cached) and emit the
 * visible map as a fresh ydraw drawable list (caller owns it). Pointer
 * return -> local-only. */
YETTY_ANNOTATE("virtual@ymap:map:render")
YETTY_ANNOTATE("local@ymap:render")
static struct yetty_ydraw_drawable_list_result map_render(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_void_ptr_result map_r = map_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ydraw_drawable_list, map_r, "ymap render: from_obj");
    struct yetty_ymap_map *map = map_r.value;

    const char *url_template;
    const char *extension;
    uint32_t max_zoom;
    int vector;
    const char *attribution;
    map_active_provider(map, &url_template, &extension, &max_zoom, &vector, &attribution);

    /* Per-provider cache subtree so styles never collide. The custom
     * provider caches under "custom" — switch templates and stale tiles
     * may serve; pass a fresh name to avoid that. */
    const char *provider_name =
        map->custom_url_template ? "custom" : (map->provider ? map->provider->name : "osm");
    char cache_dir[1024];
    snprintf(cache_dir, sizeof(cache_dir), "%s/ymap/%s", yetty_yplatform_get_cache_dir(),
             provider_name);

    struct yetty_ymap_config engine_config = {
        .latitude = map->latitude,
        .longitude = map->longitude,
        .zoom = map->zoom,
        .width_px = map->width_px,
        .height_px = map->height_px,
        .tile_url_template = url_template,
        .cache_dir = cache_dir,
        .tile_file_extension = extension,
    };
    return vector ? yetty_ymap_render_vector(&engine_config)
                  : yetty_ymap_render_raster(&engine_config);
}

/* destroy: free owned strings + the yclass allocation. */
YETTY_ANNOTATE("virtual@ymap:map:destroy")
YETTY_ANNOTATE("local@ymap:destroy")
static struct yetty_ycore_void_result map_destroy(struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_OK_VOID();
    }
    struct yetty_yclass_void_ptr_result map_r = map_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, map_r, "ymap destroy: from_obj");
    struct yetty_ymap_map *map = map_r.value;
    free(map->custom_url_template);
    free(map->custom_attribution);
    return yetty_yclass_object_free(obj);
}

/*=============================================================================
 * Provider enumeration — free functions for frontends and bindings.
 *===========================================================================*/

/* Number of built-in providers. */
YETTY_ANNOTATE("expose")
uint32_t yetty_ymap_provider_count(void)
{
    const struct ymap_provider *providers = ymap_provider_table();
    uint32_t count = 0;
    while (providers[count].name) {
        count++;
    }
    return count;
}

/* Metadata of built-in provider `index` (0..count-1). Any out pointer may
 * be NULL. Strings are static — never freed by the caller. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymap_provider_info(uint32_t index, const char **out_name,
                                                        const char **out_attribution,
                                                        uint32_t *out_max_zoom, int *out_is_vector)
{
    const struct ymap_provider *providers = ymap_provider_table();
    if (index >= yetty_ymap_provider_count()) {
        return YETTY_ERR(yetty_ycore_void, "ymap provider_info: index out of range");
    }
    if (out_name) {
        *out_name = providers[index].name;
    }
    if (out_attribution) {
        *out_attribution = providers[index].attribution;
    }
    if (out_max_zoom) {
        *out_max_zoom = providers[index].max_zoom;
    }
    if (out_is_vector) {
        *out_is_vector = providers[index].vector;
    }
    return YETTY_OK_VOID();
}

/* One-shot helper — serialize a rendered drawable list as a YDRAW_BIN OSC
 * envelope (the ycat/scrolling path). Exposed as a free function for CLIs. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymap_emit_osc(const struct yetty_ydraw_drawable_list *list,
                                                   int fd)
{
    if (!list) {
        return YETTY_ERR(yetty_ycore_void, "ymap emit_osc: NULL list");
    }
    const uint8_t *raw = NULL;
    size_t raw_size =
        yetty_ydraw_drawable_list_serialize((struct yetty_ydraw_drawable_list *)list, &raw);
    if (raw_size == 0 || !raw) {
        return YETTY_ERR(yetty_ycore_void, "ymap emit_osc: empty serialize");
    }
    struct yetty_yface_bin_meta meta = {
        .magic = YETTY_YFACE_BIN_MAGIC,
        .version = YETTY_YFACE_BIN_VERSION,
        .compressed = YETTY_YFACE_COMP_LZ4F,
        .compression_algo = 0,
        .raw_size = raw_size,
        .reserved = {0, 0},
    };
    struct yetty_ycore_void_result emit_result = yetty_yface_emit_to_fd(
        fd, YETTY_DCS_YDRAW_BIN, /*compressed=*/1, &meta, sizeof(meta), raw, raw_size);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, emit_result, "ymap emit_osc: yface_emit_to_fd");
    return YETTY_OK_VOID();
}

#include "map.gen.c"
