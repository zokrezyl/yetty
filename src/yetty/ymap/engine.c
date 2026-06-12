/*
 * engine.c — raster tile download + composite → yimage prim.
 *
 * See include/yetty/ymap/engine.h for the contract. The module
 * is deliberately GPU-less: everything ends in the existing yimage wire
 * serializer, so the receiving terminal needs no ymap-specific
 * code at all.
 *
 * Pipeline:
 *   lat/lon/zoom → global pixel viewport (Web-Mercator slippy projection)
 *   → covering tile set → per-tile: disk cache hit, else libcurl GET
 *   → stb_image PNG decode → blit into one RGBA8 composite
 *   → yetty_yimage_uniforms_serialize → drawable list with one prim.
 *
 * Tile fetch is best-effort BY DESIGN: a tile that fails to download or
 * decode leaves its background-colored hole and the render proceeds — a
 * map with one missing tile is far more useful than no map. Every other
 * failure (bad config, allocation, serialization) bails immediately.
 */

#include <yetty/ymap/engine.h>

#include "tile-fetch.h"

#include <yetty/yimage/yimage-gen.h>
#include <yetty/yimage/yimage.h>
#include <yetty/yplatform/paths.h>
#include <yetty/ytrace/ytrace.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stb_image.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define OSM_TILE_SIZE 256u
#define OSM_DEFAULT_TILE_URL "https://tile.openstreetmap.org/%u/%u/%u.png"
#define OSM_MAX_VIEWPORT_PX 4096u
#define OSM_MAX_TILES_PER_RENDER 192u
/* Off-map / missing-tile fill: light warm gray, opaque (RGBA bytes
 * 0xe4,0xe4,0xe0,0xff as a little-endian packed word). */
#define OSM_BACKGROUND_PIXEL 0xffe0e4e4u

/*=============================================================================
 * Projection
 *===========================================================================*/

void yetty_ymap_lonlat_to_global_pixel(double longitude, double latitude, uint32_t zoom,
                                              double *out_pixel_x, double *out_pixel_y)
{
    double tile_count = (double)(1u << zoom);
    double tile_x = (longitude + 180.0) / 360.0 * tile_count;
    double latitude_rad = latitude * M_PI / 180.0;
    double tile_y = (1.0 - asinh(tan(latitude_rad)) / M_PI) / 2.0 * tile_count;
    if (out_pixel_x) {
        *out_pixel_x = tile_x * (double)OSM_TILE_SIZE;
    }
    if (out_pixel_y) {
        *out_pixel_y = tile_y * (double)OSM_TILE_SIZE;
    }
}

void yetty_ymap_global_pixel_to_lonlat(double pixel_x, double pixel_y, uint32_t zoom,
                                              double *out_longitude, double *out_latitude)
{
    double world_px = (double)(1u << zoom) * (double)OSM_TILE_SIZE;
    double normalized_x = pixel_x / world_px;
    double normalized_y = pixel_y / world_px;
    if (out_longitude) {
        *out_longitude = normalized_x * 360.0 - 180.0;
    }
    if (out_latitude) {
        *out_latitude = atan(sinh(M_PI * (1.0 - 2.0 * normalized_y))) * 180.0 / M_PI;
    }
}

/*=============================================================================
 * Composite render
 *===========================================================================*/

/* Blit a decoded tile into the composite at its viewport position,
 * clipping against the viewport edges. */
static void blit_tile(uint32_t *composite, uint32_t viewport_w, uint32_t viewport_h,
                      const uint32_t *tile_pixels, int tile_w, int tile_h, int64_t dest_x,
                      int64_t dest_y)
{
    int64_t src_x0 = dest_x < 0 ? -dest_x : 0;
    int64_t src_y0 = dest_y < 0 ? -dest_y : 0;
    int64_t copy_x0 = dest_x > 0 ? dest_x : 0;
    int64_t copy_y0 = dest_y > 0 ? dest_y : 0;
    int64_t copy_x1 = dest_x + tile_w < (int64_t)viewport_w ? dest_x + tile_w : (int64_t)viewport_w;
    int64_t copy_y1 = dest_y + tile_h < (int64_t)viewport_h ? dest_y + tile_h : (int64_t)viewport_h;
    if (copy_x1 <= copy_x0 || copy_y1 <= copy_y0) {
        return;
    }
    size_t row_words = (size_t)(copy_x1 - copy_x0);
    for (int64_t row = 0; row < copy_y1 - copy_y0; row++) {
        const uint32_t *src_row = tile_pixels + (src_y0 + row) * tile_w + src_x0;
        uint32_t *dst_row = composite + (copy_y0 + row) * viewport_w + copy_x0;
        memcpy(dst_row, src_row, row_words * sizeof(uint32_t));
    }
}

static struct yetty_ycore_void_result validate_config(const struct yetty_ymap_config *config)
{
    if (!config) {
        return YETTY_ERR(yetty_ycore_void, "ymap: config is NULL");
    }
    if (config->zoom > YETTY_YMAP_MAX_ZOOM) {
        return YETTY_ERR(yetty_ycore_void, "ymap: zoom exceeds maximum (19)");
    }
    if (config->width_px == 0 || config->height_px == 0 || config->width_px > OSM_MAX_VIEWPORT_PX ||
        config->height_px > OSM_MAX_VIEWPORT_PX) {
        return YETTY_ERR(yetty_ycore_void, "ymap: viewport size out of range (1..4096 px)");
    }
    if (config->latitude < -85.06 || config->latitude > 85.06) {
        return YETTY_ERR(yetty_ycore_void, "ymap: latitude outside Web-Mercator range");
    }
    if (config->longitude < -180.0 || config->longitude > 180.0) {
        return YETTY_ERR(yetty_ycore_void, "ymap: longitude out of range");
    }
    return YETTY_OK_VOID();
}

struct yetty_ydraw_drawable_list_result yetty_ymap_render_raster(
    const struct yetty_ymap_config *config)
{
    struct yetty_ycore_void_result valid_res = validate_config(config);
    YETTY_RETURN_IF_ERR(yetty_ydraw_drawable_list, valid_res, "ymap: invalid config");

    const char *url_template =
        config->tile_url_template ? config->tile_url_template : OSM_DEFAULT_TILE_URL;
    char default_cache_root[1024];
    const char *cache_root = config->cache_dir;
    if (!cache_root) {
        snprintf(default_cache_root, sizeof(default_cache_root), "%s/osm-tiles",
                 yetty_yplatform_get_cache_dir());
        cache_root = default_cache_root;
    }

    /* Viewport in global pixel space, centered on lat/lon. */
    double center_pixel_x = 0.0;
    double center_pixel_y = 0.0;
    yetty_ymap_lonlat_to_global_pixel(config->longitude, config->latitude, config->zoom,
                                             &center_pixel_x, &center_pixel_y);
    int64_t origin_x = (int64_t)llround(center_pixel_x) - (int64_t)config->width_px / 2;
    int64_t origin_y = (int64_t)llround(center_pixel_y) - (int64_t)config->height_px / 2;

    /* Covering tile index range (y clamped to the map, x wraps). */
    int64_t tile_count = (int64_t)1 << config->zoom;
    int64_t tile_x_first =
        origin_x >= 0 ? origin_x / OSM_TILE_SIZE : (origin_x - (OSM_TILE_SIZE - 1)) / OSM_TILE_SIZE;
    int64_t tile_y_first =
        origin_y >= 0 ? origin_y / OSM_TILE_SIZE : (origin_y - (OSM_TILE_SIZE - 1)) / OSM_TILE_SIZE;
    int64_t tile_x_last = (origin_x + (int64_t)config->width_px - 1) / (int64_t)OSM_TILE_SIZE;
    int64_t tile_y_last = (origin_y + (int64_t)config->height_px - 1) / (int64_t)OSM_TILE_SIZE;

    int64_t tiles_total = (tile_x_last - tile_x_first + 1) * (tile_y_last - tile_y_first + 1);
    if (tiles_total > (int64_t)OSM_MAX_TILES_PER_RENDER) {
        return YETTY_ERR(yetty_ydraw_drawable_list,
                         "ymap: viewport covers too many tiles — reduce size or zoom out");
    }

    size_t pixel_count = (size_t)config->width_px * (size_t)config->height_px;
    uint32_t *composite = malloc(pixel_count * sizeof(uint32_t));
    if (!composite) {
        return YETTY_ERR(yetty_ydraw_drawable_list, "ymap: composite alloc failed");
    }
    for (size_t i = 0; i < pixel_count; i++) {
        composite[i] = OSM_BACKGROUND_PIXEL;
    }

    /* Collect the covering tile set (wrapped duplicates deduped), fetch
     * everything in one parallel batch, then decode + blit. */
    struct osm_blit_slot {
        int64_t tile_x; /* unwrapped — blit position */
        int64_t tile_y;
        uint32_t request_index;
    };
    struct osm_blit_slot *slots = malloc((size_t)tiles_total * sizeof(struct osm_blit_slot));
    struct yetty_ymap_tile_request *requests =
        calloc((size_t)tiles_total, sizeof(struct yetty_ymap_tile_request));
    if (!slots || !requests) {
        free(slots);
        free(requests);
        free(composite);
        return YETTY_ERR(yetty_ydraw_drawable_list, "ymap: tile set alloc failed");
    }
    uint32_t slot_count = 0;
    uint32_t request_count = 0;
    for (int64_t tile_y = tile_y_first; tile_y <= tile_y_last; tile_y++) {
        if (tile_y < 0 || tile_y >= tile_count) {
            continue; /* north/south of the map — background stays */
        }
        for (int64_t tile_x = tile_x_first; tile_x <= tile_x_last; tile_x++) {
            /* Longitude wraps. */
            uint32_t wrapped_x = (uint32_t)(((tile_x % tile_count) + tile_count) % tile_count);
            uint32_t request_index = request_count;
            for (uint32_t i = 0; i < request_count; i++) {
                if (requests[i].tile_x == wrapped_x && requests[i].tile_y == (uint32_t)tile_y) {
                    request_index = i;
                    break;
                }
            }
            if (request_index == request_count) {
                requests[request_count].tile_x = wrapped_x;
                requests[request_count].tile_y = (uint32_t)tile_y;
                request_count++;
            }
            slots[slot_count++] = (struct osm_blit_slot){
                .tile_x = tile_x, .tile_y = tile_y, .request_index = request_index};
        }
    }

    const char *tile_extension = config->tile_file_extension ? config->tile_file_extension : "png";
    struct yetty_ycore_void_result fetch_res = yetty_ymap_tiles_fetch(
        url_template, cache_root, tile_extension, config->zoom, requests, request_count);
    if (YETTY_IS_ERR(fetch_res)) {
        free(slots);
        free(requests);
        free(composite);
        return YETTY_ERR(yetty_ydraw_drawable_list, "ymap: batch fetch", fetch_res);
    }

    uint32_t tiles_blitted = 0;
    for (uint32_t i = 0; i < slot_count; i++) {
        const struct osm_blit_slot *slot = &slots[i];
        const struct yetty_ymap_tile_request *request = &requests[slot->request_index];
        if (!request->bytes) {
            continue; /* best-effort: background hole stays */
        }
        int tile_w = 0;
        int tile_h = 0;
        int channels = 0;
        stbi_uc *tile_pixels = stbi_load_from_memory(request->bytes, (int)request->len, &tile_w,
                                                     &tile_h, &channels, 4);
        if (!tile_pixels) {
            ywarn("ymap: tile %u/%u/%lld decode failed: %s", config->zoom, request->tile_x,
                  (long long)slot->tile_y, stbi_failure_reason());
            continue;
        }
        blit_tile(composite, config->width_px, config->height_px, (const uint32_t *)tile_pixels,
                  tile_w, tile_h, slot->tile_x * (int64_t)OSM_TILE_SIZE - origin_x,
                  slot->tile_y * (int64_t)OSM_TILE_SIZE - origin_y);
        stbi_image_free(tile_pixels);
        tiles_blitted++;
    }
    for (uint32_t i = 0; i < request_count; i++) {
        free(requests[i].bytes);
    }
    free(requests);
    free(slots);

    if (tiles_blitted == 0) {
        free(composite);
        return YETTY_ERR(yetty_ydraw_drawable_list,
                         "ymap: no tile could be fetched — offline and cold cache?");
    }
    ydebug("ymap: composited %u/%lld tiles for %ux%u px at z%u", tiles_blitted,
           (long long)tiles_total, config->width_px, config->height_px, config->zoom);

    /* Pack as ONE yimage prim — identical path to yetty_yimage_render,
     * minus the decode (we already hold raw RGBA8). */
    struct yetty_yimage_uniforms uniforms = {0};
    uniforms.bounds_x = 0.0f;
    uniforms.bounds_y = 0.0f;
    uniforms.bounds_w = (float)config->width_px;
    uniforms.bounds_h = (float)config->height_px;
    uniforms.image_w = config->width_px;
    uniforms.image_h = config->height_px;
    struct yetty_yimage_buffers buffers = {
        .pixels = composite,
        .pixels_len = pixel_count,
    };

    size_t required = yetty_yimage_uniforms_serialized_size(&uniforms, &buffers);
    uint8_t *prim_buf = malloc(required);
    if (!prim_buf) {
        free(composite);
        return YETTY_ERR(yetty_ydraw_drawable_list, "ymap: prim alloc failed");
    }
    struct yetty_ycore_size_result serialize_res =
        yetty_yimage_uniforms_serialize(&uniforms, &buffers, prim_buf, required);
    free(composite);
    if (YETTY_IS_ERR(serialize_res)) {
        free(prim_buf);
        return YETTY_ERR(yetty_ydraw_drawable_list, "ymap: serialize failed", serialize_res);
    }

    struct yetty_ydraw_drawable_list_config list_config = {
        .scene_min_x = 0.0f,
        .scene_min_y = 0.0f,
        .scene_max_x = (float)config->width_px,
        .scene_max_y = (float)config->height_px,
    };
    struct yetty_ydraw_drawable_list_result list_res =
        yetty_ydraw_drawable_list_config_buffer_create(&list_config);
    if (YETTY_IS_ERR(list_res)) {
        free(prim_buf);
        return YETTY_ERR(yetty_ydraw_drawable_list, "ymap: list create failed", list_res);
    }
    struct yetty_ydraw_id_result add_res =
        yetty_ydraw_drawable_list_add_prim(list_res.value, prim_buf, required);
    free(prim_buf);
    if (YETTY_IS_ERR(add_res)) {
        yetty_ydraw_drawable_list_destroy(list_res.value);
        return YETTY_ERR(yetty_ydraw_drawable_list, "ymap: add_prim failed", add_res);
    }
    return YETTY_OK(yetty_ydraw_drawable_list, list_res.value);
}

