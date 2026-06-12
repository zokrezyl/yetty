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
#define OSM_USER_AGENT "yetty-ymap/0.1 (+https://github.com/zokrezyl/yetty)"
#define OSM_FETCH_TIMEOUT_SECONDS 20L

/* Newest HTTP version this libcurl can attempt. yetty's vendored curl is
 * built with ngtcp2/nghttp3 (HTTP/3 + QUIC) on Linux; curl never tries
 * h3 unless asked, so ask — CURL_HTTP_VERSION_3 races QUIC against
 * TCP+h2/h1.1 and falls back automatically (curl >= 8.4 semantics). On
 * builds without the QUIC stack the default stands: h2 via TLS ALPN. */
static long osm_preferred_http_version(void)
{
    static long preferred_version; /* 0 = not probed yet */
    if (preferred_version == 0) {
#ifdef CURL_VERSION_HTTP3
        const curl_version_info_data *info = curl_version_info(CURLVERSION_NOW);
        if (info && (info->features & CURL_VERSION_HTTP3)) {
            preferred_version = CURL_HTTP_VERSION_3;
            yinfo("ymap: libcurl %s has HTTP/3 — racing QUIC with TCP fallback", info->version);
        } else {
            preferred_version = CURL_HTTP_VERSION_2TLS;
        }
#else
        preferred_version = CURL_HTTP_VERSION_2TLS;
#endif
    }
    return preferred_version;
}

/*=============================================================================
 * curl lifecycle
 *===========================================================================*/

CURL *yetty_ymap_curl_acquire(void)
{
    /* curl global init is once-per-process. */
    static int curl_initialized = 0;
    if (!curl_initialized) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        curl_initialized = 1;
    }
    return curl_easy_init();
}

void yetty_ymap_curl_release(CURL *curl_handle)
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
        return YETTY_ERR(yetty_ycore_void, "ymap: fopen for read failed");
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return YETTY_ERR(yetty_ycore_void, "ymap: fseek failed");
    }
    long file_size = ftell(file);
    if (file_size <= 0) {
        fclose(file);
        return YETTY_ERR(yetty_ycore_void, "ymap: empty or unreadable file");
    }
    rewind(file);
    uint8_t *bytes = malloc((size_t)file_size);
    if (!bytes) {
        fclose(file);
        return YETTY_ERR(yetty_ycore_void, "ymap: file buffer alloc failed");
    }
    size_t bytes_read = fread(bytes, 1, (size_t)file_size, file);
    fclose(file);
    if (bytes_read != (size_t)file_size) {
        free(bytes);
        return YETTY_ERR(yetty_ycore_void, "ymap: short read");
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
        return YETTY_ERR(yetty_ycore_void, "ymap: fopen for write failed");
    }
    size_t bytes_written = fwrite(bytes, 1, len, file);
    fclose(file);
    if (bytes_written != len) {
        return YETTY_ERR(yetty_ycore_void, "ymap: short write");
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
    curl_easy_setopt(curl_handle, CURLOPT_HTTP_VERSION, osm_preferred_http_version());
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
        return YETTY_ERR(yetty_ycore_void, "ymap: HTTP error status");
    }
    if (body.len == 0) {
        free(body.data);
        return YETTY_ERR(yetty_ycore_void, "ymap: empty HTTP body");
    }
    *out_bytes = body.data;
    *out_len = body.len;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ymap_http_get(CURL *curl_handle, const char *url,
                                                   long timeout_seconds, uint8_t **out_bytes,
                                                   size_t *out_len)
{
    if (!curl_handle || !url || !out_bytes || !out_len) {
        return YETTY_ERR(yetty_ycore_void, "ymap http_get: NULL argument");
    }
    return fetch_url(curl_handle, url, timeout_seconds, out_bytes, out_len);
}

/*=============================================================================
 * Tile fetch with disk cache
 *===========================================================================*/

struct yetty_ycore_void_result yetty_ymap_tile_fetch(CURL *curl_handle, const char *url_template,
                                                     const char *cache_root,
                                                     const char *cache_file_extension,
                                                     uint32_t zoom, uint32_t tile_x,
                                                     uint32_t tile_y, uint8_t **out_bytes,
                                                     size_t *out_len)
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
        ywarn("ymap: unreadable cache entry %s; re-downloading", cache_path);
    }

    char url[1024];
    snprintf(url, sizeof(url), url_template, zoom, tile_x, tile_y);
    struct yetty_ycore_void_result fetch_res = fetch_url(curl_handle, url, 0, out_bytes, out_len);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, fetch_res, "ymap: tile download failed");

    /* Cache write is best-effort — a read-only cache dir must not fail
     * the render. */
    char cache_tile_dir[1024];
    snprintf(cache_tile_dir, sizeof(cache_tile_dir), "%s/%u/%u", cache_root, zoom, tile_x);
    yetty_yplatform_mkdir_p(cache_tile_dir);
    struct yetty_ycore_void_result write_res = write_entire_file(cache_path, *out_bytes, *out_len);
    if (YETTY_IS_ERR(write_res)) {
        ywarn("ymap: cache write failed for %s: %s", cache_path, write_res.error.msg);
        yetty_ycore_error_destroy(write_res.error);
    }
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Parallel batch fetch (curl-multi)
 *
 * Cache hits are read synchronously up front; the remaining tiles
 * download concurrently. Parallelism is bounded politely: the OSM tile
 * usage policy allows at most two simultaneous connections per host, so
 * the multi handle is capped there and HTTP/2 multiplexing carries the
 * rest of the concurrency on those two connections.
 *===========================================================================*/

#define OSM_MAX_HOST_CONNECTIONS 2L
#define OSM_MAX_TOTAL_CONNECTIONS 4L

struct osm_tile_transfer {
    struct yetty_ymap_tile_request *request;
    struct osm_growable_buffer body;
    char url[1024];
};

static void tile_transfer_configure(CURL *easy_handle, struct osm_tile_transfer *transfer)
{
    curl_easy_setopt(easy_handle, CURLOPT_URL, transfer->url);
    curl_easy_setopt(easy_handle, CURLOPT_HTTP_VERSION, osm_preferred_http_version());
    curl_easy_setopt(easy_handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(easy_handle, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(easy_handle, CURLOPT_TIMEOUT, OSM_FETCH_TIMEOUT_SECONDS);
    curl_easy_setopt(easy_handle, CURLOPT_USERAGENT, OSM_USER_AGENT);
    curl_easy_setopt(easy_handle, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(easy_handle, CURLOPT_WRITEFUNCTION, osm_curl_write_callback);
    curl_easy_setopt(easy_handle, CURLOPT_WRITEDATA, &transfer->body);
    curl_easy_setopt(easy_handle, CURLOPT_PRIVATE, transfer);
}

/* Completion of one transfer: success keeps the body in the request slot
 * and writes the cache back; failure logs and leaves the slot NULL. */
static void tile_transfer_finish(CURL *easy_handle, CURLcode curl_result, const char *cache_root,
                                 const char *cache_file_extension, uint32_t zoom)
{
    struct osm_tile_transfer *transfer = NULL;
    curl_easy_getinfo(easy_handle, CURLINFO_PRIVATE, &transfer);
    if (!transfer) {
        return;
    }
    long http_status = 0;
    curl_easy_getinfo(easy_handle, CURLINFO_RESPONSE_CODE, &http_status);
    long negotiated_version = 0;
    curl_easy_getinfo(easy_handle, CURLINFO_HTTP_VERSION, &negotiated_version);
    ydebug("ymap: tile %u/%u done, HTTP version code %ld (30=h3 20=h2)", transfer->request->tile_x,
           transfer->request->tile_y, negotiated_version);

    if (curl_result != CURLE_OK || http_status >= 400 || transfer->body.len == 0) {
        ywarn("ymap: tile %u/%u/%u download failed (%s, HTTP %ld)", zoom, transfer->request->tile_x,
              transfer->request->tile_y, curl_easy_strerror(curl_result), http_status);
        free(transfer->body.data);
        transfer->body.data = NULL;
        return;
    }

    transfer->request->bytes = transfer->body.data;
    transfer->request->len = transfer->body.len;
    transfer->body.data = NULL;

    /* Best-effort cache write-back, same as the serial path. */
    char cache_tile_dir[1024];
    char cache_path[1280];
    snprintf(cache_tile_dir, sizeof(cache_tile_dir), "%s/%u/%u", cache_root, zoom,
             transfer->request->tile_x);
    snprintf(cache_path, sizeof(cache_path), "%s/%u.%s", cache_tile_dir, transfer->request->tile_y,
             cache_file_extension);
    yetty_yplatform_mkdir_p(cache_tile_dir);
    struct yetty_ycore_void_result write_res =
        write_entire_file(cache_path, transfer->request->bytes, transfer->request->len);
    if (YETTY_IS_ERR(write_res)) {
        ywarn("ymap: cache write failed for %s: %s", cache_path, write_res.error.msg);
        yetty_ycore_error_destroy(write_res.error);
    }
}

struct yetty_ycore_void_result yetty_ymap_tiles_fetch(
    const char *url_template, const char *cache_root, const char *cache_file_extension,
    uint32_t zoom, struct yetty_ymap_tile_request *requests, uint32_t request_count)
{
    if (!url_template || !cache_root || !cache_file_extension || (!requests && request_count)) {
        return YETTY_ERR(yetty_ycore_void, "ymap tiles_fetch: NULL argument");
    }
    if (request_count == 0) {
        return YETTY_OK_VOID();
    }

    /* Pass 1: serve cache hits, collect the misses. */
    uint32_t miss_count = 0;
    for (uint32_t i = 0; i < request_count; i++) {
        struct yetty_ymap_tile_request *request = &requests[i];
        request->bytes = NULL;
        request->len = 0;
        char cache_path[1280];
        snprintf(cache_path, sizeof(cache_path), "%s/%u/%u/%u.%s", cache_root, zoom,
                 request->tile_x, request->tile_y, cache_file_extension);
        if (yetty_yplatform_file_is_regular(cache_path)) {
            struct yetty_ycore_void_result read_res =
                read_entire_file(cache_path, &request->bytes, &request->len);
            if (YETTY_IS_OK(read_res)) {
                continue;
            }
            yetty_ycore_error_destroy(read_res.error);
            ywarn("ymap: unreadable cache entry %s; re-downloading", cache_path);
        }
        miss_count++;
    }
    if (miss_count == 0) {
        return YETTY_OK_VOID();
    }

    /* curl global init guard (shared with the easy-handle path). */
    CURL *probe_handle = yetty_ymap_curl_acquire();
    if (!probe_handle) {
        return YETTY_ERR(yetty_ycore_void, "ymap tiles_fetch: curl init failed");
    }
    yetty_ymap_curl_release(probe_handle);

    CURLM *multi_handle = curl_multi_init();
    if (!multi_handle) {
        return YETTY_ERR(yetty_ycore_void, "ymap tiles_fetch: curl_multi_init failed");
    }
    curl_multi_setopt(multi_handle, CURLMOPT_MAX_HOST_CONNECTIONS, OSM_MAX_HOST_CONNECTIONS);
    curl_multi_setopt(multi_handle, CURLMOPT_MAX_TOTAL_CONNECTIONS, OSM_MAX_TOTAL_CONNECTIONS);
    curl_multi_setopt(multi_handle, CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);

    struct osm_tile_transfer *transfers = calloc(miss_count, sizeof(struct osm_tile_transfer));
    CURL **easy_handles = calloc(miss_count, sizeof(CURL *));
    if (!transfers || !easy_handles) {
        free(transfers);
        free(easy_handles);
        curl_multi_cleanup(multi_handle);
        return YETTY_ERR(yetty_ycore_void, "ymap tiles_fetch: transfer alloc failed");
    }

    uint32_t transfer_count = 0;
    for (uint32_t i = 0; i < request_count; i++) {
        if (requests[i].bytes) {
            continue; /* cache hit */
        }
        struct osm_tile_transfer *transfer = &transfers[transfer_count];
        transfer->request = &requests[i];
        snprintf(transfer->url, sizeof(transfer->url), url_template, zoom, requests[i].tile_x,
                 requests[i].tile_y);
        CURL *easy_handle = curl_easy_init();
        if (!easy_handle) {
            ywarn("ymap: easy handle alloc failed for tile %u/%u — skipped", requests[i].tile_x,
                  requests[i].tile_y);
            continue;
        }
        tile_transfer_configure(easy_handle, transfer);
        curl_multi_add_handle(multi_handle, easy_handle);
        easy_handles[transfer_count] = easy_handle;
        transfer_count++;
    }

    int still_running = 0;
    do {
        CURLMcode multi_result = curl_multi_perform(multi_handle, &still_running);
        if (multi_result != CURLM_OK) {
            ywarn("ymap: curl_multi_perform: %s", curl_multi_strerror(multi_result));
            break;
        }
        if (still_running) {
            curl_multi_poll(multi_handle, NULL, 0, 1000, NULL);
        }
        /* Drain completions as they arrive so cache write-back overlaps
         * the remaining transfers. */
        int messages_left = 0;
        CURLMsg *message;
        while ((message = curl_multi_info_read(multi_handle, &messages_left))) {
            if (message->msg == CURLMSG_DONE) {
                tile_transfer_finish(message->easy_handle, message->data.result, cache_root,
                                     cache_file_extension, zoom);
                curl_multi_remove_handle(multi_handle, message->easy_handle);
            }
        }
    } while (still_running);

    for (uint32_t i = 0; i < transfer_count; i++) {
        if (easy_handles[i]) {
            curl_easy_cleanup(easy_handles[i]);
        }
        free(transfers[i].body.data); /* leftovers of aborted transfers */
    }
    free(easy_handles);
    free(transfers);
    curl_multi_cleanup(multi_handle);
    return YETTY_OK_VOID();
}
