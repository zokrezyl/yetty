#ifndef YETTY_YOPENSTREET_OPENSTREET_H
#define YETTY_YOPENSTREET_OPENSTREET_H

/*
 * yopenstreet — OpenStreetMap snapshot rendering for the scrolling grid.
 *
 * Downloads the slippy-map raster tiles covering a lat/lon-centered
 * viewport, composites them into one RGBA8 image, and packs it as a
 * single yimage complex prim inside a drawable list — the same low-level
 * YDRAW_BIN path ycat's image handler uses, so the map anchors at the
 * cursor and scrolls with the terminal content (yvterm scrolling rich
 * grid). No GPU code of its own: the receiving side renders it through
 * the existing yimage factory.
 *
 * Tiles are fetched over HTTPS (libcurl) with an on-disk cache under
 * <yetty cache dir>/osm-tiles/<z>/<x>/<y>.png so repeated renders of the
 * same area do not re-hit the tile server.
 *
 * OpenStreetMap tile usage policy notes (https://operations.osmfoundation.org/policies/tiles/):
 *   - requests carry an identifying User-Agent;
 *   - the disk cache avoids repeat downloads;
 *   - displayed maps require the attribution "© OpenStreetMap
 *     contributors" — the yopenstreet tool prints it under the map; any
 *     other frontend embedding this library must do the equivalent.
 */

#include <stdint.h>
#include <stdio.h>

#include <yetty/ycore/result.h>
#include <yetty/ydraw-core/drawable-list.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Highest zoom the default openstreetmap.org raster servers carry. */
#define YETTY_YOPENSTREET_MAX_ZOOM 19u
/* Highest zoom of the shortbread vector tile set (z15+ is overzoom). */
#define YETTY_YOPENSTREET_MAX_VECTOR_ZOOM 14u

struct yetty_yopenstreet_config {
    double latitude;  /* map center, degrees (WGS84), -85.05..85.05 */
    double longitude; /* map center, degrees, -180..180 */
    uint32_t zoom;    /* slippy-map zoom level, 0..YETTY_YOPENSTREET_MAX_ZOOM */

    uint32_t width_px; /* viewport (and displayed) size in pixels */
    uint32_t height_px;

    /* printf-style template with three unsigned slots in z, x, y order
     * (e.g. "https://tile.openstreetmap.org/%u/%u/%u.png"). NULL → that
     * default. The string is handed to snprintf verbatim — it comes from
     * the local CLI user, not the wire. */
    const char *tile_url_template;

    /* Tile cache directory. NULL → <yetty cache dir>/osm-tiles. */
    const char *cache_dir;

    /* Tile file extension for cache names AND implied content type
     * (raster decodes via stb_image: png/jpg/jpeg all fine). NULL →
     * "png" for raster, "mvt" for vector. Satellite providers (NASA
     * GIBS, EOX Sentinel-2) serve jpeg. */
    const char *tile_file_extension;
};

/* Forward/inverse slippy projection: degrees ↔ global pixel coordinates
 * at `zoom` (256 px per tile, origin at the map's north-west corner).
 * Exposed for tools and tests — the interactive tool pans/zooms by
 * shifting the center in pixel space and projecting back. */
void yetty_yopenstreet_lonlat_to_global_pixel(double longitude, double latitude, uint32_t zoom,
                                              double *out_pixel_x, double *out_pixel_y);

void yetty_yopenstreet_global_pixel_to_lonlat(double pixel_x, double pixel_y, uint32_t zoom,
                                              double *out_longitude, double *out_latitude);

/* Geolocate this machine's public IP (one keyless HTTPS request to
 * ipinfo.io, short timeout). City-level accuracy at best — meant only to
 * pick a sensible default map center. Best-effort by contract: callers
 * fall back on error, the lookup must never gate a render. */
struct yetty_ycore_void_result yetty_yopenstreet_geolocate_public_ip(double *out_latitude,
                                                                     double *out_longitude);

/* Download + composite the configured viewport and return a fresh
 * drawable list holding ONE yimage prim. Caller frees the list with
 * yetty_ydraw_drawable_list_destroy. */
struct yetty_ydraw_drawable_list_result yetty_yopenstreet_render(
    const struct yetty_yopenstreet_config *config);

/* Vector variant: fetches Mapbox-Vector-Tile (shortbread schema) tiles
 * and emits the map as native drawables — SDF triangles/segments for
 * polygons and lines, MSDF text runs for labels — instead of pixels.
 * Crisp under cell zoom and far smaller on the wire. zoom is capped at
 * YETTY_YOPENSTREET_MAX_VECTOR_ZOOM; tile_url_template defaults to
 * vector.openstreetmap.org/shortbread_v1 (.mvt), the cache to
 * <yetty cache dir>/osm-vector-tiles. */
struct yetty_ydraw_drawable_list_result yetty_yopenstreet_render_vector(
    const struct yetty_yopenstreet_config *config);

/* OSC envelope (YETTY_DCS_YDRAW_BIN, same wire format as ycat / yimage).
 * Returns bytes written. */
struct yetty_ycore_size_result yetty_yopenstreet_osc_bin_emit(
    const struct yetty_ydraw_drawable_list *buffer, FILE *out);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YOPENSTREET_OPENSTREET_H */
