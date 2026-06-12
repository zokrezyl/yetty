/*
 * tile-fetch.c — tile download + disk cache (see tile-fetch.h).
 *
 * Extracted from openstreet.c when the vector path landed so both the
 * raster PNG and vector MVT fetches share one implementation. OSM tile
 * policy: identifying User-Agent on every request, disk cache to avoid
 * repeat downloads.
 */

#include "tile-fetch.h"

#include <yetty/yplatform/fs.h>
#include <yetty/ytrace/ytrace.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* OSM tile policy requires an identifying User-Agent. */
#define OSM_USER_AGENT "yetty-yopenstreet/0.1 (+https://github.com/zokrezyl/yetty)"
#define OSM_FETCH_TIMEOUT_SECONDS 20L

/*=============================================================================
 * curl lifecycle
 *===========================================================================*/

CURL *yetty_yopenstreet_curl_acquire(void)
{
    /* curl global init is once-per-process. */
    static int curl_initialized = 0;
    if (!curl_initialized) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        curl_initialized = 1;
    }
    return curl_easy_init();
}

void yetty_yopenstreet_curl_release(CURL *curl_handle)
{
    if (curl_handle) {
        curl_easy_cleanup(curl_handle);
    }
}

/*=============================================================================
 * File helpers
 *===========================================================================*/

static struct yetty_ycore_void_result read_entire_file(const char *path, uint8_t **out_bytes,
                                                       size_t *out_len)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        return YETTY_ERR(yetty_ycore_void, "yopenstreet: fopen for read failed");
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return YETTY_ERR(yetty_ycore_void, "yopenstreet: fseek failed");
    }
    long file_size = ftell(file);
    if (file_size <= 0) {
        fclose(file);
        return YETTY_ERR(yetty_ycore_void, "yopenstreet: empty or unreadable file");
    }
    rewind(file);
    uint8_t *bytes = malloc((size_t)file_size);
    if (!bytes) {
        fclose(file);
        return YETTY_ERR(yetty_ycore_void, "yopenstreet: file buffer alloc failed");
    }
    size_t bytes_read = fread(bytes, 1, (size_t)file_size, file);
    fclose(file);
    if (bytes_read != (size_t)file_size) {
        free(bytes);
        return YETTY_ERR(yetty_ycore_void, "yopenstreet: short read");
    }
    *out_bytes = bytes;
    *out_len = (size_t)file_size;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result write_entire_file(const char *path, const uint8_t *bytes,
                                                        size_t len)
{
    FILE *file = fopen(path, "wb");
    if (!file) {
        return YETTY_ERR(yetty_ycore_void, "yopenstreet: fopen for write failed");
    }
    size_t bytes_written = fwrite(bytes, 1, len, file);
    fclose(file);
    if (bytes_written != len) {
        return YETTY_ERR(yetty_ycore_void, "yopenstreet: short write");
    }
    return YETTY_OK_VOID();
}

/*=============================================================================
 * HTTP fetch
 *===========================================================================*/

struct osm_growable_buffer {
    uint8_t *data;
    size_t len;
    size_t cap;
};

static size_t osm_curl_write_callback(char *chunk, size_t member_size, size_t member_count,
                                      void *user_data)
{
    struct osm_growable_buffer *buffer = user_data;
    size_t total = member_size * member_count;
    if (buffer->len + total > buffer->cap) {
        size_t new_cap = buffer->cap ? buffer->cap * 2 : 65536;
        while (new_cap < buffer->len + total) {
            new_cap *= 2;
        }
        uint8_t *new_data = realloc(buffer->data, new_cap);
        if (!new_data) {
            return 0; /* abort the transfer */
        }
        buffer->data = new_data;
        buffer->cap = new_cap;
    }
    memcpy(buffer->data + buffer->len, chunk, total);
    buffer->len += total;
    return total;
}

static struct yetty_ycore_void_result fetch_url(CURL *curl_handle, const char *url,
                                                long timeout_seconds, uint8_t **out_bytes,
                                                size_t *out_len)
{
    struct osm_growable_buffer body = {0};
    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl_handle, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT,
                     timeout_seconds > 0 ? timeout_seconds : OSM_FETCH_TIMEOUT_SECONDS);
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, OSM_USER_AGENT);
    /* Empty string = accept every encoding curl supports and decode
     * transparently — vector tile servers gzip their protobuf. */
    curl_easy_setopt(curl_handle, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, osm_curl_write_callback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &body);

    CURLcode curl_result = curl_easy_perform(curl_handle);
    if (curl_result != CURLE_OK) {
        free(body.data);
        return YETTY_ERR(yetty_ycore_void, curl_easy_strerror(curl_result));
    }
    long http_status = 0;
    curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &http_status);
    if (http_status >= 400) {
        free(body.data);
        return YETTY_ERR(yetty_ycore_void, "yopenstreet: HTTP error status");
    }
    if (body.len == 0) {
        free(body.data);
        return YETTY_ERR(yetty_ycore_void, "yopenstreet: empty HTTP body");
    }
    *out_bytes = body.data;
    *out_len = body.len;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yopenstreet_http_get(CURL *curl_handle, const char *url,
                                                          long timeout_seconds, uint8_t **out_bytes,
                                                          size_t *out_len)
{
    if (!curl_handle || !url || !out_bytes || !out_len) {
        return YETTY_ERR(yetty_ycore_void, "yopenstreet http_get: NULL argument");
    }
    return fetch_url(curl_handle, url, timeout_seconds, out_bytes, out_len);
}

/*=============================================================================
 * Tile fetch with disk cache
 *===========================================================================*/

struct yetty_ycore_void_result yetty_yopenstreet_tile_fetch(
    CURL *curl_handle, const char *url_template, const char *cache_root,
    const char *cache_file_extension, uint32_t zoom, uint32_t tile_x, uint32_t tile_y,
    uint8_t **out_bytes, size_t *out_len)
{
    char cache_path[1024];
    snprintf(cache_path, sizeof(cache_path), "%s/%u/%u/%u.%s", cache_root, zoom, tile_x, tile_y,
             cache_file_extension);

    if (yetty_yplatform_file_is_regular(cache_path)) {
        struct yetty_ycore_void_result read_res = read_entire_file(cache_path, out_bytes, out_len);
        if (YETTY_IS_OK(read_res)) {
            return YETTY_OK_VOID();
        }
        /* Corrupt / unreadable cache entry — fall through to re-download. */
        yetty_ycore_error_destroy(read_res.error);
        ywarn("yopenstreet: unreadable cache entry %s; re-downloading", cache_path);
    }

    char url[1024];
    snprintf(url, sizeof(url), url_template, zoom, tile_x, tile_y);
    struct yetty_ycore_void_result fetch_res = fetch_url(curl_handle, url, 0, out_bytes, out_len);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, fetch_res, "yopenstreet: tile download failed");

    /* Cache write is best-effort — a read-only cache dir must not fail
     * the render. */
    char cache_tile_dir[1024];
    snprintf(cache_tile_dir, sizeof(cache_tile_dir), "%s/%u/%u", cache_root, zoom, tile_x);
    yetty_yplatform_mkdir_p(cache_tile_dir);
    struct yetty_ycore_void_result write_res = write_entire_file(cache_path, *out_bytes, *out_len);
    if (YETTY_IS_ERR(write_res)) {
        ywarn("yopenstreet: cache write failed for %s: %s", cache_path, write_res.error.msg);
        yetty_ycore_error_destroy(write_res.error);
    }
    return YETTY_OK_VOID();
}
