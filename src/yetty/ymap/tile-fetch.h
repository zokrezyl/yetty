#ifndef YETTY_YMAP_TILE_FETCH_H
#define YETTY_YMAP_TILE_FETCH_H

/*
 * tile-fetch.h — module-internal tile download + disk cache, shared by
 * the raster (openstreet.c) and vector (vector-render.c) paths. Not a
 * public API; lives next to the sources like scrolling-grid.h does.
 */

#include <stddef.h>
#include <stdint.h>

#include <curl/curl.h>

#include <yetty/ycore/result.h>

/* curl_global_init guard + easy-handle mint. One handle per render,
 * reused across its tiles (connection keep-alive). */
CURL *yetty_ymap_curl_acquire(void);
void yetty_ymap_curl_release(CURL *curl_handle);

/* Fetch one tile: disk cache at
 * <cache_root>/<zoom>/<x>/<y>.<cache_file_extension> first, else HTTP GET
 * of the printf-style `url_template` (three unsigned slots, z/x/y order)
 * with a best-effort cache write-back. Caller frees *out_bytes. */
struct yetty_ycore_void_result yetty_ymap_tile_fetch(
    CURL *curl_handle, const char *url_template, const char *cache_root,
    const char *cache_file_extension, uint32_t zoom, uint32_t tile_x, uint32_t tile_y,
    uint8_t **out_bytes, size_t *out_len);

/* Plain GET into a fresh buffer (no cache) — geoip lookup and other
 * one-shot requests. timeout_seconds <= 0 falls back to the tile
 * default. Caller frees *out_bytes. */
struct yetty_ycore_void_result yetty_ymap_http_get(CURL *curl_handle, const char *url,
                                                          long timeout_seconds, uint8_t **out_bytes,
                                                          size_t *out_len);

/* One slot of a batch fetch. Caller fills tile_x / tile_y; bytes/len are
 * the result (heap, caller frees) — NULL/0 when that tile failed. */
struct yetty_ymap_tile_request {
    uint32_t tile_x;
    uint32_t tile_y;
    uint8_t *bytes;
    size_t len;
};

/* Fetch a whole tile set: cache hits fill synchronously, the misses
 * download IN PARALLEL via curl-multi (HTTP/2 multiplexing, capped at 2
 * connections per host per the OSM tile usage policy). Per-tile failures
 * are best-effort — the slot stays NULL and the call still returns OK;
 * only setup-level failures error. */
struct yetty_ycore_void_result yetty_ymap_tiles_fetch(
    const char *url_template, const char *cache_root, const char *cache_file_extension,
    uint32_t zoom, struct yetty_ymap_tile_request *requests, uint32_t request_count);

#endif /* YETTY_YMAP_TILE_FETCH_H */
