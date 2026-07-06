/*
 * ylexbor-js-web — WebAPI bindings on top of QuickJS for the bits
 * github / gitlab / any modern SPA needs to even *boot*:
 *
 *   fetch / Response / XMLHttpRequest          — request/response fetch
 *                                                 layer; async on the
 *                                                 worker pool when the
 *                                                 host provides one,
 *                                                 sync fallback otherwise
 *   setTimeout / setInterval / clearTimeout    — min-heap timer queue
 *   queueMicrotask / requestAnimationFrame     — onto the same queue
 *   window === globalThis (already set by DOM)
 *   navigator.{userAgent, language, languages,
 *              cookieEnabled, onLine, platform,
 *              hardwareConcurrency, maxTouchPoints}
 *   location.{href, origin, protocol, host, hostname,
 *             pathname, search, hash, port}
 *   history.{pushState, replaceState, back, forward, go, length, state}
 *   localStorage / sessionStorage              — in-memory map with
 *                                                 the standard 4 ops
 *   matchMedia(q)                              — returns an EventTarget
 *                                                 with matches=false
 *   getComputedStyle(el)                       — reads the inline style
 *                                                 attribute (close enough
 *                                                 for feature checks)
 *   document.cookie (get/set)                  — in-memory string
 *   atob / btoa                                — needed by analytics
 *   structuredClone                            — JSON round-trip impl
 *   AbortController / AbortSignal              — minimal stubs
 *   Worker / SharedWorker / ServiceWorker      — constructor stubs that
 *                                                 do nothing
 *   navigator.serviceWorker.{register, ...}    — Promise-resolving stubs
 *   crypto.{getRandomValues, randomUUID,
 *          subtle.{digest, importKey, sign,
 *                  verify, encrypt, decrypt,
 *                  generateKey}}              — getRandomValues real,
 *                                                 subtle stubs reject
 *   IndexedDB stubs                            — open returns a never-
 *                                                 resolving request
 *   CustomEvent / MessageEvent classes
 *
 * Most of this is "good enough to not throw on existence checks". The
 * goal is for boilerplate-detection libraries to stop bailing out of
 * the SPA boot path so the actual rendering code keeps running.
 *
 * Compile-out: YETTY_HAVE_QUICKJS=0 → all stubs.
 */

#include "ybrowser-internal.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef YETTY_HAVE_QUICKJS
#define YETTY_HAVE_QUICKJS 0
#endif
#ifndef YETTY_HAVE_CURL
#define YETTY_HAVE_CURL 0
#endif

/* URL resolution and HTTP fetch are independent of QuickJS — image
 * loading and external-stylesheet fetching need them whether or not a
 * JS runtime is compiled in. Compile them unconditionally; the
 * QuickJS-gated section below is just the JS-side bindings. */

#if YETTY_HAVE_CURL
#include <curl/curl.h>
#endif

#include <yetty/yplatform/yworkpool.h>
#include <yetty/ytrace/ytrace.h>

/* ===========================================================================
 * URL resolution + sync HTTP fetch.
 * ===========================================================================*/

char *yetty_ylexbor_resolve_url_against(const char *base_url, const char *href)
{
    if (!href) {
        return NULL;
    }
    if (strncmp(href, "http://", 7) == 0 || strncmp(href, "https://", 8) == 0 ||
        strncmp(href, "data:", 5) == 0 || strncmp(href, "file://", 7) == 0) {
        return strdup(href);
    }
    if (!base_url) {
        return strdup(href); /* best effort */
    }

    /* Two cases: absolute path (starts with /) or relative. */
    if (href[0] == '/' && href[1] == '/') {
        /* protocol-relative — splice the base's protocol. */
        const char *p = strstr(base_url, "://");
        size_t plen = p ? (size_t)(p - base_url) : 4;
        size_t hl = strlen(href);
        char *out = malloc(plen + 1 + hl + 1);
        memcpy(out, base_url, plen);
        out[plen] = ':';
        memcpy(out + plen + 1, href, hl + 1);
        return out;
    }
    if (href[0] == '/') {
        /* absolute path — splice scheme://host/ */
        const char *p = strstr(base_url, "://");
        if (!p) {
            return strdup(href);
        }
        const char *slash = strchr(p + 3, '/');
        size_t prefix = slash ? (size_t)(slash - base_url) : strlen(base_url);
        size_t hl = strlen(href);
        char *out = malloc(prefix + hl + 1);
        memcpy(out, base_url, prefix);
        memcpy(out + prefix, href, hl + 1);
        return out;
    }
    /* relative — strip last path segment of base_url, append href */
    size_t blen = strlen(base_url);
    const char *base_end = base_url + blen;
    const char *slash = NULL;
    const char *p = strstr(base_url, "://");
    const char *path_start = p ? strchr(p + 3, '/') : base_url;
    if (path_start) {
        for (const char *q = base_end; q > path_start; q--) {
            if (*(q - 1) == '/') {
                slash = q - 1;
                break;
            }
        }
    }
    size_t prefix = slash ? (size_t)(slash - base_url + 1) : blen;
    size_t hl = strlen(href);
    char *out = malloc(prefix + hl + 1);
    memcpy(out, base_url, prefix);
    memcpy(out + prefix, href, hl + 1);
    return out;
}

char *yetty_ylexbor_resolve_url(struct yetty_ylexbor *r, const char *href)
{
    return yetty_ylexbor_resolve_url_against(r ? r->base_url : NULL, href);
}

#if YETTY_HAVE_CURL

struct fetch_transfer {
    char *data;
    size_t size, cap;
    /* Cancellation: when cancel_generation is non-NULL and its current
	 * value no longer matches generation, the transfer aborts — the
	 * progress callback catches stalls in DNS/connect/TLS/headers, the
	 * write callback catches streaming bodies at the next chunk. The
	 * pointed-to counter is bumped by the engine on navigation; atomic
	 * because workers poll while the loop thread increments. */
    uint64_t generation;
    const _Atomic uint64_t *cancel_generation;
    /* Response headers we care about, captured by fetch_header_cb. */
    char cache_control[160];
    char etag[256];
    char last_modified[128];
    char vary[128];
};

static int fetch_transfer_is_stale(const struct fetch_transfer *transfer)
{
    return transfer->cancel_generation != NULL &&
           atomic_load_explicit(transfer->cancel_generation, memory_order_acquire) !=
               transfer->generation;
}

/* Hard ceiling on a single fetched response. Without it, an endpoint that
 * streams without end (SSE / long-poll / analytics beacon) or ships a
 * gzip decompression bomb (curl auto-inflates under ACCEPT_ENCODING) drives
 * the doubling buffer below to many GB — a real CNN/github load grew it past
 * 25 GB and OOM-killed the process. 64 MiB is far above any HTML / CSS / JS /
 * image a page legitimately serves, so this only ever trips on a runaway. */
#define FETCH_MAX_RESPONSE (64u * 1024u * 1024u)

static size_t fetch_write_cb(char *chunk, size_t chunk_size, size_t chunk_count, void *userdata)
{
    struct fetch_transfer *transfer = userdata;
    size_t add = chunk_size * chunk_count;
    /* Stale transfer (the page navigated away) — abort now instead of
	 * downloading the rest just to throw it away. Returning a short
	 * count fails the easy handle with CURLE_WRITE_ERROR. */
    if (fetch_transfer_is_stale(transfer)) {
        return 0;
    }
    if (transfer->size + add > FETCH_MAX_RESPONSE) {
        return 0;
    }
    if (transfer->size + add + 1 > transfer->cap) {
        size_t new_cap = transfer->cap ? transfer->cap * 2 : 16384;
        while (new_cap < transfer->size + add + 1) {
            new_cap *= 2;
        }
        char *grown = realloc(transfer->data, new_cap);
        if (!grown) {
            return 0;
        }
        transfer->data = grown;
        transfer->cap = new_cap;
    }
    memcpy(transfer->data + transfer->size, chunk, add);
    transfer->size += add;
    return add;
}

/* Capture the response headers the cache policy needs (Cache-Control).
 * Content-Type comes from CURLINFO_CONTENT_TYPE after the transfer. */
/* Copy one response header's trimmed value into `dst` when `line` is that
 * header. Returns 1 on capture. */
static int header_capture(const char *line, size_t len, const char *name, size_t name_len,
                          char *dst, size_t dst_size)
{
    if (len <= name_len || strncasecmp(line, name, name_len) != 0) {
        return 0;
    }
    const char *value = line + name_len;
    size_t value_len = len - name_len;
    while (value_len > 0 && (*value == ' ' || *value == '\t')) {
        value++;
        value_len--;
    }
    while (value_len > 0 && (value[value_len - 1] == '\r' || value[value_len - 1] == '\n')) {
        value_len--;
    }
    if (value_len >= dst_size) {
        value_len = dst_size - 1;
    }
    memcpy(dst, value, value_len);
    dst[value_len] = '\0';
    return 1;
}

static size_t fetch_header_cb(char *line, size_t line_size, size_t line_count, void *userdata)
{
    struct fetch_transfer *transfer = userdata;
    size_t len = line_size * line_count;
    (void)(header_capture(line, len, "cache-control:", 14, transfer->cache_control,
                          sizeof(transfer->cache_control)) ||
           header_capture(line, len, "etag:", 5, transfer->etag, sizeof(transfer->etag)) ||
           header_capture(line, len, "last-modified:", 14, transfer->last_modified,
                          sizeof(transfer->last_modified)) ||
           header_capture(line, len, "vary:", 5, transfer->vary, sizeof(transfer->vary)));
    return len;
}

/* Cancellation for the phases the write callback never sees: a transfer
 * stuck in DNS/connect/TLS/headers (or a body-less response) gets progress
 * ticks but no write callbacks, so a navigation would otherwise keep it
 * alive until the timeout. Non-zero return aborts with
 * CURLE_ABORTED_BY_CALLBACK. */
static int fetch_progress_cb(void *userdata, curl_off_t download_total, curl_off_t download_now,
                             curl_off_t upload_total, curl_off_t upload_now)
{
    (void)download_total;
    (void)download_now;
    (void)upload_total;
    (void)upload_now;
    const struct fetch_transfer *transfer = userdata;
    return fetch_transfer_is_stale(transfer) ? 1 : 0;
}

#include <pthread.h>

/* Network loader — the owner of everything fetch-related that outlives a
 * single request. The share handle is the workhorse: the image fetch
 * paths make ~30 curl_easy_init() calls to the same CDN host
 * (upload.wikimedia.org); without a share each call does a fresh TCP+TLS
 * handshake (~100ms × 30 = ~3s wasted). With it, the second through Nth
 * easy handle reuse the live connection from the pool. Thread-safe via
 * libcurl's CURL_LOCK_DATA_* lock callbacks — fetches also run on image
 * worker threads and the JS thread. The Alt-Svc cache path lives here
 * too so BOTH the sequential and the curl_multi paths get HTTP/3
 * upgrades, and so no racy static path buffer is needed. */
/* One cached resource. `bytes` is the wire body; `pixels` is the decoded
 * RGBA decoration published by the image pipeline so back/forward repaints
 * skip both the network AND the decode. */
struct loader_cache_entry {
    char *url;
    /* Request kind the response was negotiated for. Part of the key: the
	 * same URL fetched as STYLE vs IMAGE carries different Accept /
	 * Sec-Fetch-Dest headers, and Vary-sensitive CDNs serve different
	 * representations — never hand one kind's bytes to another. */
    enum yetty_ybrowser_request_kind kind;
    char *bytes;
    size_t bytes_len;
    long status;
    char *content_type;
    char *effective_url;
    uint32_t *pixels;
    int pixels_width, pixels_height;
    time_t expires_at;       /* wall clock; entry is stale past this */
    uint64_t last_used_tick; /* LRU ordinal */
    size_t charge;           /* bytes accounted against the budget */
    /* Validators — a stale entry with either one becomes a conditional
	 * refetch; a 304 re-serves the stored body with a refreshed TTL. */
    char *etag;
    char *last_modified;
};

/* Snapshot of a stale entry's validators, filled by the lookup so the
 * fetch can go conditional. */
struct loader_cache_revalidation {
    int available;
    char etag[256];
    char last_modified[128];
};

/* Default freshness when the server sends no max-age: a session-cache
 * TTL. Long enough that every same-site navigation in a browsing burst
 * hits, short enough that a long-running instance doesn't serve
 * yesterday's stylesheet. */
#define LOADER_CACHE_DEFAULT_TTL_SECONDS 300

/* Total budget for bytes + pixels across all entries. */
#define LOADER_CACHE_BUDGET (64u * 1024u * 1024u)

struct yetty_ybrowser_loader {
    CURLSH *share; /* NULL when curl_share_init failed — fetches still work */
    pthread_mutex_t share_locks[CURL_LOCK_DATA_LAST];
    /* $XDG_CACHE_HOME/yetty/altsvc-cache (or $HOME/.cache/...). Empty
	 * string when no cache dir could be derived. */
    char altsvc_path[1024];

    /* Resource cache — shared by every engine on this loader, touched
	 * from the loop thread and the image/fetch workers alike. */
    pthread_mutex_t cache_mutex;
    struct loader_cache_entry *cache_entries;
    int cache_count, cache_cap;
    size_t cache_charge;
    uint64_t cache_tick;

    /* Single-flight coalescing: concurrent misses on the same (url, kind)
	 * used to run duplicate transfers, first store wins. Followers now
	 * wait for the leader and re-read the cache. Bounded — when the table
	 * is full, extra requests just fetch (correct, merely unshared). */
    pthread_mutex_t inflight_mutex;
    pthread_cond_t inflight_cond;
    struct {
        char url[512];
        int kind;
        int active;
    } inflight[16];

    /* Central transfer scheduler: ONE curl_multi on its own thread
	 * carries every easy handle. Submitting threads (loop thread, image/
	 * fetch/nav workers) configure the handle, enqueue, and sleep until
	 * completion — so all traffic multiplexes on shared H2 connections,
	 * connection counts stay bounded process-wide, and no thread ever
	 * owns a private network loop. */
    pthread_t scheduler_thread;
    int scheduler_running;
    int scheduler_shutdown; /* scheduler_mutex held */
    int scheduler_active;   /* handles inside the multi; scheduler_mutex held */
    CURLM *scheduler_multi;
    pthread_mutex_t scheduler_mutex;
    pthread_cond_t scheduler_cond; /* broadcast per completion */
    struct loader_scheduled_transfer *scheduler_pending;
};

/* One transfer parked on the scheduler. Lives on the submitting thread's
 * stack — valid because that thread blocks until done is set. */
struct loader_scheduled_transfer {
    CURL *easy;
    CURLcode result;
    int done;
    struct loader_scheduled_transfer *next;
};

/* SCHEDULER THREAD — the only place that touches scheduler_multi. */
static void *loader_scheduler_main(void *loader_ptr)
{
    struct yetty_ybrowser_loader *loader = loader_ptr;
    CURLM *multi = loader->scheduler_multi;
    for (;;) {
        pthread_mutex_lock(&loader->scheduler_mutex);
        while (loader->scheduler_pending) {
            struct loader_scheduled_transfer *node = loader->scheduler_pending;
            loader->scheduler_pending = node->next;
            node->next = NULL;
            curl_easy_setopt(node->easy, CURLOPT_PRIVATE, node);
            if (curl_multi_add_handle(multi, node->easy) == CURLM_OK) {
                loader->scheduler_active++;
            } else {
                node->result = CURLE_FAILED_INIT;
                node->done = 1;
                pthread_cond_broadcast(&loader->scheduler_cond);
            }
        }
        int shutdown_now = loader->scheduler_shutdown && loader->scheduler_active == 0;
        pthread_mutex_unlock(&loader->scheduler_mutex);
        if (shutdown_now) {
            break;
        }
        int running_handles = 0;
        curl_multi_perform(multi, &running_handles);
        int msgs_left = 0;
        CURLMsg *message;
        while ((message = curl_multi_info_read(multi, &msgs_left)) != NULL) {
            if (message->msg != CURLMSG_DONE) {
                continue;
            }
            struct loader_scheduled_transfer *node = NULL;
            curl_easy_getinfo(message->easy_handle, CURLINFO_PRIVATE, &node);
            CURLcode transfer_result = message->data.result;
            curl_multi_remove_handle(multi, message->easy_handle);
            pthread_mutex_lock(&loader->scheduler_mutex);
            loader->scheduler_active--;
            if (node) {
                node->result = transfer_result;
                node->done = 1;
            }
            pthread_cond_broadcast(&loader->scheduler_cond);
            pthread_mutex_unlock(&loader->scheduler_mutex);
        }
        int numfds = 0;
        /* Sleeps until network activity or curl_multi_wakeup (submission /
		 * shutdown). */
        curl_multi_poll(multi, NULL, 0, 250, &numfds);
    }
    return NULL;
}

/* Enqueue a configured easy handle onto the scheduler. Pair with
 * loader_scheduler_wait. */
static void loader_scheduler_submit(struct yetty_ybrowser_loader *loader,
                                    struct loader_scheduled_transfer *node)
{
    pthread_mutex_lock(&loader->scheduler_mutex);
    node->next = loader->scheduler_pending;
    loader->scheduler_pending = node;
    pthread_mutex_unlock(&loader->scheduler_mutex);
    curl_multi_wakeup(loader->scheduler_multi);
}

static CURLcode loader_scheduler_wait(struct yetty_ybrowser_loader *loader,
                                      struct loader_scheduled_transfer *node)
{
    pthread_mutex_lock(&loader->scheduler_mutex);
    while (!node->done) {
        pthread_cond_wait(&loader->scheduler_cond, &loader->scheduler_mutex);
    }
    pthread_mutex_unlock(&loader->scheduler_mutex);
    return node->result;
}

/* Run one configured easy handle: through the central scheduler when it
 * is up (the norm), else a plain blocking perform. */
static CURLcode loader_perform(struct yetty_ybrowser_loader *loader, CURL *easy)
{
    if (!loader || !loader->scheduler_running) {
        return curl_easy_perform(easy);
    }
    struct loader_scheduled_transfer node = {.easy = easy};
    loader_scheduler_submit(loader, &node);
    return loader_scheduler_wait(loader, &node);
}

static void loader_cache_entry_free(struct loader_cache_entry *entry)
{
    free(entry->url);
    free(entry->bytes);
    free(entry->content_type);
    free(entry->effective_url);
    free(entry->pixels);
    free(entry->etag);
    free(entry->last_modified);
    memset(entry, 0, sizeof(*entry));
}

/* cache_mutex held. Returns the entry index or -1. */
static int loader_cache_find(struct yetty_ybrowser_loader *loader, const char *url,
                             enum yetty_ybrowser_request_kind kind)
{
    for (int i = 0; i < loader->cache_count; i++) {
        if (loader->cache_entries[i].url && loader->cache_entries[i].kind == kind &&
            strcmp(loader->cache_entries[i].url, url) == 0) {
            return i;
        }
    }
    return -1;
}

/* cache_mutex held. Drop the least-recently-used entries until the
 * charge fits the budget again. */
static void loader_cache_evict(struct yetty_ybrowser_loader *loader)
{
    while (loader->cache_charge > LOADER_CACHE_BUDGET && loader->cache_count > 0) {
        int oldest = 0;
        for (int i = 1; i < loader->cache_count; i++) {
            if (loader->cache_entries[i].last_used_tick <
                loader->cache_entries[oldest].last_used_tick) {
                oldest = i;
            }
        }
        loader->cache_charge -= loader->cache_entries[oldest].charge;
        loader_cache_entry_free(&loader->cache_entries[oldest]);
        loader->cache_entries[oldest] = loader->cache_entries[loader->cache_count - 1];
        memset(&loader->cache_entries[loader->cache_count - 1], 0,
               sizeof(loader->cache_entries[0]));
        loader->cache_count--;
    }
}

/* Only plain GETs of subresources are cacheable — API responses (XHR)
 * and document navigations are not. */
static int loader_cache_kind_ok(const struct yetty_ybrowser_request *request)
{
    if (request->body ||
        (request->method && *request->method && strcasecmp(request->method, "GET") != 0)) {
        return 0;
    }
    return request->kind == YETTY_YBROWSER_REQUEST_STYLE ||
           request->kind == YETTY_YBROWSER_REQUEST_SCRIPT ||
           request->kind == YETTY_YBROWSER_REQUEST_IMAGE;
}

/* cache_mutex held. Copy an entry's payload into `response`. Returns 1 on
 * success, 0 on OOM (response zeroed). */
static int loader_cache_entry_to_response(struct loader_cache_entry *entry,
                                          struct yetty_ybrowser_response *response)
{
    response->status = entry->status;
    response->body = malloc(entry->bytes_len + 1);
    if (!response->body) {
        memset(response, 0, sizeof(*response));
        return 0;
    }
    memcpy(response->body, entry->bytes, entry->bytes_len);
    response->body[entry->bytes_len] = '\0';
    response->body_len = entry->bytes_len;
    response->effective_url = entry->effective_url ? strdup(entry->effective_url) : NULL;
    response->content_type = entry->content_type ? strdup(entry->content_type) : NULL;
    if (entry->pixels && entry->pixels_width > 0 && entry->pixels_height > 0) {
        size_t pixel_bytes = (size_t)entry->pixels_width * (size_t)entry->pixels_height * 4;
        response->image_pixels = malloc(pixel_bytes);
        if (response->image_pixels) {
            memcpy(response->image_pixels, entry->pixels, pixel_bytes);
            response->image_width = entry->pixels_width;
            response->image_height = entry->pixels_height;
        }
    }
    return 1;
}

/* Parse Cache-Control into a TTL. Returns 0 when the response must not be
 * cached (no-store / no-cache / non-positive max-age), else 1 with the
 * TTL in *out_ttl_seconds. */
static int loader_cache_ttl(const char *cache_control, long *out_ttl_seconds)
{
    long ttl_seconds = LOADER_CACHE_DEFAULT_TTL_SECONDS;
    if (cache_control && *cache_control) {
        if (strstr(cache_control, "no-store") || strstr(cache_control, "no-cache")) {
            return 0;
        }
        const char *max_age = strstr(cache_control, "max-age=");
        if (max_age) {
            ttl_seconds = atol(max_age + 8);
            if (ttl_seconds <= 0) {
                return 0;
            }
        }
    }
    *out_ttl_seconds = ttl_seconds;
    return 1;
}

/* Vary policy: our requests hold Accept / Accept-Encoding / User-Agent
 * constant per request kind (and kind is part of the cache key), so those
 * dimensions are safe. Anything else — Cookie, Origin, a bare `*` — could
 * serve a wrong variant cross-context: don't cache it. */
static int loader_cache_vary_ok(const char *vary)
{
    if (!vary || !*vary) {
        return 1;
    }
    const char *cursor = vary;
    while (*cursor) {
        while (*cursor == ' ' || *cursor == '\t' || *cursor == ',') {
            cursor++;
        }
        if (!*cursor) {
            break;
        }
        size_t token_len = strcspn(cursor, ", \t");
        if (!((token_len == 6 && strncasecmp(cursor, "accept", 6) == 0) ||
              (token_len == 15 && strncasecmp(cursor, "accept-encoding", 15) == 0) ||
              (token_len == 10 && strncasecmp(cursor, "user-agent", 10) == 0))) {
            return 0;
        }
        cursor += token_len;
    }
    return 1;
}

/* Fresh-hit lookup: fills `response` with copies and returns 1, or returns
 * 0 on miss/stale. A stale entry that carries validators is KEPT and its
 * validators snapshot into *revalidation — the caller refetches
 * conditionally and a 304 re-serves the stored body. */
static int loader_cache_lookup(struct yetty_ybrowser_loader *loader,
                               const struct yetty_ybrowser_request *request,
                               struct yetty_ybrowser_response *response,
                               struct loader_cache_revalidation *revalidation)
{
    if (revalidation) {
        memset(revalidation, 0, sizeof(*revalidation));
    }
    if (!loader || !loader_cache_kind_ok(request)) {
        return 0;
    }
    int hit = 0;
    pthread_mutex_lock(&loader->cache_mutex);
    int idx = loader_cache_find(loader, request->url, request->kind);
    if (idx >= 0) {
        struct loader_cache_entry *entry = &loader->cache_entries[idx];
        if (entry->expires_at <= time(NULL)) {
            if (revalidation && (entry->etag || entry->last_modified)) {
                /* Stale but revalidatable — keep the body for a 304. */
                revalidation->available = 1;
                if (entry->etag) {
                    snprintf(revalidation->etag, sizeof(revalidation->etag), "%s", entry->etag);
                }
                if (entry->last_modified) {
                    snprintf(revalidation->last_modified, sizeof(revalidation->last_modified), "%s",
                             entry->last_modified);
                }
            } else {
                /* Stale, no validators — drop it; the caller re-fetches. */
                loader->cache_charge -= entry->charge;
                loader_cache_entry_free(entry);
                loader->cache_entries[idx] = loader->cache_entries[loader->cache_count - 1];
                memset(&loader->cache_entries[loader->cache_count - 1], 0,
                       sizeof(loader->cache_entries[0]));
                loader->cache_count--;
            }
        } else {
            entry->last_used_tick = ++loader->cache_tick;
            hit = loader_cache_entry_to_response(entry, response);
        }
    }
    pthread_mutex_unlock(&loader->cache_mutex);
    return hit;
}

/* A conditional refetch came back 304: refresh the stored entry's TTL and
 * serve its body. Returns 1 when served, 0 when the entry vanished (the
 * caller falls back to an unconditional refetch). */
static int loader_cache_serve_revalidated(struct yetty_ybrowser_loader *loader,
                                          const struct yetty_ybrowser_request *request,
                                          const struct fetch_transfer *transfer,
                                          struct yetty_ybrowser_response *response)
{
    if (!loader) {
        return 0;
    }
    long ttl_seconds = LOADER_CACHE_DEFAULT_TTL_SECONDS;
    (void)loader_cache_ttl(transfer->cache_control, &ttl_seconds);
    int served = 0;
    pthread_mutex_lock(&loader->cache_mutex);
    int idx = loader_cache_find(loader, request->url, request->kind);
    if (idx >= 0) {
        struct loader_cache_entry *entry = &loader->cache_entries[idx];
        entry->expires_at = time(NULL) + ttl_seconds;
        entry->last_used_tick = ++loader->cache_tick;
        served = loader_cache_entry_to_response(entry, response);
    }
    pthread_mutex_unlock(&loader->cache_mutex);
    return served;
}

/* Store a fresh 2xx response; the transfer carries the response headers
 * that drive cacheability (Cache-Control, Vary) and the validators. */
static void loader_cache_store(struct yetty_ybrowser_loader *loader,
                               const struct yetty_ybrowser_request *request,
                               const struct yetty_ybrowser_response *response,
                               const struct fetch_transfer *transfer)
{
    if (!loader || !loader_cache_kind_ok(request) || !response->body || response->status < 200 ||
        response->status >= 300) {
        return;
    }
    long ttl_seconds = LOADER_CACHE_DEFAULT_TTL_SECONDS;
    if (!loader_cache_ttl(transfer->cache_control, &ttl_seconds)) {
        return;
    }
    if (!loader_cache_vary_ok(transfer->vary)) {
        return;
    }
    if (response->body_len > LOADER_CACHE_BUDGET / 4) {
        return; /* one entry must not dominate the budget */
    }
    pthread_mutex_lock(&loader->cache_mutex);
    int existing = loader_cache_find(loader, request->url, request->kind);
    if (existing >= 0) {
        /* Replace — a revalidation-refetch that came back 200 supersedes
		 * the stale body (a raced same-URL store is byte-equal anyway). */
        loader->cache_charge -= loader->cache_entries[existing].charge;
        loader_cache_entry_free(&loader->cache_entries[existing]);
        loader->cache_entries[existing] = loader->cache_entries[loader->cache_count - 1];
        memset(&loader->cache_entries[loader->cache_count - 1], 0,
               sizeof(loader->cache_entries[0]));
        loader->cache_count--;
    }
    if (loader->cache_count == loader->cache_cap) {
        int new_cap = loader->cache_cap ? loader->cache_cap * 2 : 32;
        struct loader_cache_entry *entries =
            realloc(loader->cache_entries, (size_t)new_cap * sizeof(*entries));
        if (!entries) {
            pthread_mutex_unlock(&loader->cache_mutex);
            return;
        }
        loader->cache_entries = entries;
        loader->cache_cap = new_cap;
    }
    struct loader_cache_entry *entry = &loader->cache_entries[loader->cache_count];
    memset(entry, 0, sizeof(*entry));
    entry->url = strdup(request->url);
    entry->bytes = malloc(response->body_len + 1);
    if (!entry->url || !entry->bytes) {
        loader_cache_entry_free(entry);
        pthread_mutex_unlock(&loader->cache_mutex);
        return;
    }
    memcpy(entry->bytes, response->body, response->body_len);
    entry->bytes[response->body_len] = '\0';
    entry->bytes_len = response->body_len;
    entry->kind = request->kind;
    entry->status = response->status;
    entry->content_type = response->content_type ? strdup(response->content_type) : NULL;
    entry->effective_url = response->effective_url ? strdup(response->effective_url) : NULL;
    entry->etag = transfer->etag[0] ? strdup(transfer->etag) : NULL;
    entry->last_modified = transfer->last_modified[0] ? strdup(transfer->last_modified) : NULL;
    entry->expires_at = time(NULL) + ttl_seconds;
    entry->last_used_tick = ++loader->cache_tick;
    entry->charge = response->body_len;
    loader->cache_count++;
    loader->cache_charge += entry->charge;
    loader_cache_evict(loader);
    pthread_mutex_unlock(&loader->cache_mutex);
}

/* Publish decoded pixels onto an existing cache entry so the NEXT
 * navigation skips the decode as well as the network. Copies. */
void yetty_ybrowser_loader_cache_put_pixels(struct yetty_ybrowser_loader *loader, const char *url,
                                            const uint32_t *pixels, int width, int height)
{
    if (!loader || !url || !pixels || width <= 0 || height <= 0) {
        return;
    }
    size_t pixel_bytes = (size_t)width * (size_t)height * 4;
    if (pixel_bytes > LOADER_CACHE_BUDGET / 4) {
        return;
    }
    pthread_mutex_lock(&loader->cache_mutex);
    /* Pixels are the image pipeline's decoration — only IMAGE entries. */
    int idx = loader_cache_find(loader, url, YETTY_YBROWSER_REQUEST_IMAGE);
    if (idx >= 0 && !loader->cache_entries[idx].pixels) {
        struct loader_cache_entry *entry = &loader->cache_entries[idx];
        entry->pixels = malloc(pixel_bytes);
        if (entry->pixels) {
            memcpy(entry->pixels, pixels, pixel_bytes);
            entry->pixels_width = width;
            entry->pixels_height = height;
            entry->charge += pixel_bytes;
            loader->cache_charge += pixel_bytes;
            loader_cache_evict(loader);
        }
    }
    pthread_mutex_unlock(&loader->cache_mutex);
}

static void share_lock_cb(CURL *handle, curl_lock_data data, curl_lock_access access,
                          void *userdata)
{
    (void)handle;
    (void)access;
    struct yetty_ybrowser_loader *loader = userdata;
    if (data < CURL_LOCK_DATA_LAST) {
        pthread_mutex_lock(&loader->share_locks[data]);
    }
}
static void share_unlock_cb(CURL *handle, curl_lock_data data, void *userdata)
{
    (void)handle;
    struct yetty_ybrowser_loader *loader = userdata;
    if (data < CURL_LOCK_DATA_LAST) {
        pthread_mutex_unlock(&loader->share_locks[data]);
    }
}

struct yetty_ybrowser_loader_ptr_result yetty_ybrowser_loader_create(void)
{
    struct yetty_ybrowser_loader *loader = calloc(1, sizeof(*loader));
    if (loader == NULL) {
        return YETTY_ERR(yetty_ybrowser_loader_ptr, "loader alloc");
    }
    for (int i = 0; i < CURL_LOCK_DATA_LAST; i++) {
        pthread_mutex_init(&loader->share_locks[i], NULL);
    }
    pthread_mutex_init(&loader->cache_mutex, NULL);
    pthread_mutex_init(&loader->inflight_mutex, NULL);
    pthread_cond_init(&loader->inflight_cond, NULL);
    loader->share = curl_share_init();
    if (loader->share) {
        curl_share_setopt(loader->share, CURLSHOPT_LOCKFUNC, share_lock_cb);
        curl_share_setopt(loader->share, CURLSHOPT_UNLOCKFUNC, share_unlock_cb);
        curl_share_setopt(loader->share, CURLSHOPT_USERDATA, loader);
        /* Share the four caches that matter for repeated same-host
		 * fetches. CONNECT is the big one — keeps the TCP+TLS connection
		 * alive across easy handle lifetimes. DNS and SSL_SESSION are
		 * smaller wins but free. */
        curl_share_setopt(loader->share, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
        curl_share_setopt(loader->share, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
#ifdef CURL_LOCK_DATA_CONNECT
        curl_share_setopt(loader->share, CURLSHOPT_SHARE, CURL_LOCK_DATA_CONNECT);
#endif
        curl_share_setopt(loader->share, CURLSHOPT_SHARE, CURL_LOCK_DATA_COOKIE);
    }
    /* Alt-Svc cache file — lets a second visit to an H3-capable origin
	 * skip the H2 round and go straight to HTTP/3. Only when this build
	 * of libcurl actually speaks H3. */
#if defined(CURLOPT_ALTSVC) && defined(CURL_VERSION_HTTP3)
    {
        curl_version_info_data *version_info = curl_version_info(CURLVERSION_NOW);
        if (version_info && (version_info->features & CURL_VERSION_HTTP3)) {
            const char *xdg = getenv("XDG_CACHE_HOME");
            const char *home = getenv("HOME");
            if (xdg && *xdg) {
                snprintf(loader->altsvc_path, sizeof(loader->altsvc_path), "%s/yetty/altsvc-cache",
                         xdg);
            } else if (home && *home) {
                snprintf(loader->altsvc_path, sizeof(loader->altsvc_path),
                         "%s/.cache/yetty/altsvc-cache", home);
            }
        }
    }
#endif
    /* Central scheduler thread — every transfer this loader runs goes
	 * through this ONE multi: same-origin requests multiplex on shared
	 * H2 connections, per-host and total connection counts are bounded
	 * globally, and workers wait instead of running private loops. */
    loader->scheduler_multi = curl_multi_init();
    if (loader->scheduler_multi) {
        curl_multi_setopt(loader->scheduler_multi, CURLMOPT_PIPELINING, (long)CURLPIPE_MULTIPLEX);
        curl_multi_setopt(loader->scheduler_multi, CURLMOPT_MAX_HOST_CONNECTIONS, 6L);
        curl_multi_setopt(loader->scheduler_multi, CURLMOPT_MAX_TOTAL_CONNECTIONS, 32L);
        pthread_mutex_init(&loader->scheduler_mutex, NULL);
        pthread_cond_init(&loader->scheduler_cond, NULL);
        if (pthread_create(&loader->scheduler_thread, NULL, loader_scheduler_main, loader) == 0) {
            loader->scheduler_running = 1;
        } else {
            pthread_mutex_destroy(&loader->scheduler_mutex);
            pthread_cond_destroy(&loader->scheduler_cond);
            curl_multi_cleanup(loader->scheduler_multi);
            loader->scheduler_multi = NULL;
        }
    }
    return YETTY_OK(yetty_ybrowser_loader_ptr, loader);
}

struct yetty_ycore_void_result yetty_ybrowser_loader_destroy(struct yetty_ybrowser_loader *loader)
{
    if (loader == NULL) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result res = YETTY_OK_VOID();
    if (loader->scheduler_running) {
        pthread_mutex_lock(&loader->scheduler_mutex);
        loader->scheduler_shutdown = 1;
        pthread_mutex_unlock(&loader->scheduler_mutex);
        curl_multi_wakeup(loader->scheduler_multi);
        pthread_join(loader->scheduler_thread, NULL);
        curl_multi_cleanup(loader->scheduler_multi);
        pthread_mutex_destroy(&loader->scheduler_mutex);
        pthread_cond_destroy(&loader->scheduler_cond);
        loader->scheduler_running = 0;
    }
    if (loader->share) {
        CURLSHcode code = curl_share_cleanup(loader->share);
        if (code != CURLSHE_OK) {
            res = YETTY_ERR(yetty_ycore_void, "curl_share_cleanup failed");
        }
    }
    for (int i = 0; i < CURL_LOCK_DATA_LAST; i++) {
        pthread_mutex_destroy(&loader->share_locks[i]);
    }
    for (int i = 0; i < loader->cache_count; i++) {
        loader_cache_entry_free(&loader->cache_entries[i]);
    }
    free(loader->cache_entries);
    pthread_mutex_destroy(&loader->cache_mutex);
    pthread_mutex_destroy(&loader->inflight_mutex);
    pthread_cond_destroy(&loader->inflight_cond);
    free(loader);
    return res;
}

void *yetty_ybrowser_loader_curl_share(struct yetty_ybrowser_loader *loader)
{
    return loader ? loader->share : NULL;
}

/* Apply the loader-owned bits (share handle, Alt-Svc cache) to one easy
 * handle. Shared by the sequential and curl_multi paths so both get
 * connection reuse AND HTTP/3 upgrades. */
static void loader_configure_easy(struct yetty_ybrowser_loader *loader, CURL *easy)
{
    if (!loader) {
        return;
    }
    if (loader->share) {
        curl_easy_setopt(easy, CURLOPT_SHARE, loader->share);
    }
#ifdef CURLOPT_ALTSVC
    if (loader->altsvc_path[0]) {
        curl_easy_setopt(easy, CURLOPT_ALTSVC, loader->altsvc_path);
        /* Permit h1/h2/h3 alternates. */
#ifdef CURLALTSVC_H1
        curl_easy_setopt(easy, CURLOPT_ALTSVC_CTRL,
                         (long)(CURLALTSVC_H1 | CURLALTSVC_H2 | CURLALTSVC_H3));
#endif
    }
#endif
}

/* file:// — read directly since libcurl is often built without the FILE
 * protocol. Used heavily by the WPT integration runner to load
 * `<script src=...>` siblings of the test page, and by fetch_many for
 * local stylesheet links. Returns a malloc'd NUL-terminated buffer;
 * *out_status is 200 on success, 0 on failure. */
static char *fetch_local_file(const char *url, size_t *out_len, long *out_status)
{
    if (out_len) {
        *out_len = 0;
    }
    if (out_status) {
        *out_status = 0;
    }
    const char *path = url + 7; /* past "file://" */
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) {
        fclose(f);
        return NULL;
    }
    char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[got] = '\0';
    if (out_len) {
        *out_len = got;
    }
    if (out_status) {
        *out_status = 200;
    }
    return buf;
}

/* ===========================================================================
 * Request → response. One configuration path for the sequential and the
 * curl_multi fetchers, with headers derived from what the resource IS
 * (document / stylesheet / script / image / API call) — content-
 * negotiating CDNs and WAFs key on exactly these hints, and claiming
 * "image" for a stylesheet gets the wrong (or no) representation.
 * ===========================================================================*/

/* Scheme-less host span of a URL — for the Sec-Fetch-Site hint. */
static int url_host_span(const char *url, const char **out_start, size_t *out_len)
{
    const char *scheme_end = strstr(url, "://");
    if (!scheme_end) {
        return 0;
    }
    const char *host = scheme_end + 3;
    size_t len = strcspn(host, "/?#");
    *out_start = host;
    *out_len = len;
    return len > 0;
}

/* We don't model registrable domains, so a subdomain reports cross-site
 * where Chrome would say same-site; servers treat both as "not
 * same-origin", which is the distinction the gatekeepers key on. */
static const char *sec_fetch_site_for(const char *url, const char *referer)
{
    if (!referer || !*referer) {
        return "none";
    }
    const char *url_host = NULL, *referer_host = NULL;
    size_t url_host_len = 0, referer_host_len = 0;
    if (!url_host_span(url, &url_host, &url_host_len) ||
        !url_host_span(referer, &referer_host, &referer_host_len)) {
        return "cross-site";
    }
    if (url_host_len == referer_host_len &&
        strncasecmp(url_host, referer_host, url_host_len) == 0) {
        return "same-origin";
    }
    return "cross-site";
}

static struct curl_slist *build_request_headers(const struct yetty_ybrowser_request *request)
{
    struct curl_slist *headers = NULL;
    switch (request->kind) {
    case YETTY_YBROWSER_REQUEST_DOCUMENT:
        /* Wikipedia's caching tier serves the lightweight bot variant of
		 * pages to requests without a browser-shape Accept header. */
        headers = curl_slist_append(
            headers, "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8");
        headers = curl_slist_append(headers, "Sec-Fetch-Dest: document");
        headers = curl_slist_append(headers, "Sec-Fetch-Mode: navigate");
        headers = curl_slist_append(headers, "Sec-Fetch-User: ?1");
        headers = curl_slist_append(headers, "Upgrade-Insecure-Requests: 1");
        break;
    case YETTY_YBROWSER_REQUEST_STYLE:
        headers = curl_slist_append(headers, "Accept: text/css,*/*;q=0.1");
        headers = curl_slist_append(headers, "Sec-Fetch-Dest: style");
        headers = curl_slist_append(headers, "Sec-Fetch-Mode: no-cors");
        break;
    case YETTY_YBROWSER_REQUEST_SCRIPT:
        headers = curl_slist_append(headers, "Accept: */*");
        headers = curl_slist_append(headers, "Sec-Fetch-Dest: script");
        headers = curl_slist_append(headers, "Sec-Fetch-Mode: no-cors");
        break;
    case YETTY_YBROWSER_REQUEST_IMAGE:
        /* IMPORTANT: do NOT advertise avif/apng — content-negotiating
		 * CDNs (notably Wikimedia) will serve those formats over
		 * PNG/JPEG when the URL itself ends in .png, and we can't
		 * decode them. Restrict to formats the decoders handle. */
        headers = curl_slist_append(
            headers,
            "Accept: image/png,image/jpeg,image/gif,image/svg+xml,image/*;q=0.5,*/*;q=0.1");
        headers = curl_slist_append(headers, "Sec-Fetch-Dest: image");
        headers = curl_slist_append(headers, "Sec-Fetch-Mode: no-cors");
        break;
    case YETTY_YBROWSER_REQUEST_XHR:
        headers = curl_slist_append(headers, "Accept: */*");
        headers = curl_slist_append(headers, "Sec-Fetch-Dest: empty");
        headers = curl_slist_append(headers, "Sec-Fetch-Mode: cors");
        break;
    }
    headers = curl_slist_append(headers, "Accept-Language: en-US,en;q=0.9");
    {
        char site_header[96];
        snprintf(site_header, sizeof(site_header), "Sec-Fetch-Site: %s",
                 sec_fetch_site_for(request->url, request->referer));
        headers = curl_slist_append(headers, site_header);
    }
    /* Caller-supplied headers last (fetch()/XHR: Authorization,
	 * Content-Type, X-*). */
    for (int i = 0; i < request->extra_header_count; i++) {
        if (request->extra_headers[i] && request->extra_headers[i][0]) {
            headers = curl_slist_append(headers, request->extra_headers[i]);
        }
    }
    return headers;
}

/* Everything below the loader bits that both fetch paths share. */
static void fetch_configure_easy(struct yetty_ybrowser_loader *loader, CURL *easy,
                                 const struct yetty_ybrowser_request *request,
                                 struct fetch_transfer *transfer, struct curl_slist *headers)
{
    loader_configure_easy(loader, easy);
    curl_easy_setopt(easy, CURLOPT_URL, request->url);
    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(easy, CURLOPT_MAXREDIRS, 10L);
    /* Send a standard Chrome User-Agent so CDNs treat us as a real
	 * browser. Wikimedia's bot-throttling, news.google.com's image
	 * gate, and several CloudFlare WAFs all behave very differently
	 * for unidentified vs browser UAs. The env var lets ops override
	 * for cases where bot-identifying UAs are required by ToS. */
    const char *user_agent = getenv("YETTY_USER_AGENT");
    if (!user_agent || !*user_agent) {
        user_agent = "Mozilla/5.0 (X11; Linux x86_64) "
                     "AppleWebKit/537.36 (KHTML, like Gecko) "
                     "Chrome/120.0.0.0 Safari/537.36";
    }
    curl_easy_setopt(easy, CURLOPT_USERAGENT, user_agent);
    curl_easy_setopt(easy, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT, 10L);
    /* Fail dead connections fast: under 1 KB/s for 15 s means the peer
	 * is gone; don't sit out the full 30 s wall-clock cap. */
    curl_easy_setopt(easy, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(easy, CURLOPT_LOW_SPEED_TIME, 15L);
    /* Bigger receive buffer = fewer write-callback invocations on image
	 * transfers (default is 16 KB). */
    curl_easy_setopt(easy, CURLOPT_BUFFERSIZE, 262144L);
    /* Library used from worker threads — no signal-based timeouts. */
    curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
    /* Reject oversized Content-Length before the transfer starts; the
	 * write callback still guards chunked/streamed responses. */
    curl_easy_setopt(easy, CURLOPT_MAXFILESIZE_LARGE, (curl_off_t)FETCH_MAX_RESPONSE);
    curl_easy_setopt(easy, CURLOPT_ACCEPT_ENCODING, "");
    /* Enable the cookie engine (empty COOKIEFILE = no file, just parse)
	 * for SUBRESOURCE and API fetches — session-gated image CDNs and
	 * affinity-routed sheets need the cookies the document's redirect
	 * chain set (shared across handles via CURL_LOCK_DATA_COOKIE on the
	 * loader). Deliberately NOT for DOCUMENT navigations: cookie-capable
	 * page requests make variant-sniffing sites serve their JS-heavy SPA
	 * shell (news.google.com drops from 1.7 MB of static article markup
	 * to a 650 KB shell our JS can't boot), while the cookie-less page
	 * fetch gets the static variant this engine renders well. Revisit
	 * when the JS surface can boot those shells. */
    if (request->kind != YETTY_YBROWSER_REQUEST_DOCUMENT) {
        curl_easy_setopt(easy, CURLOPT_COOKIEFILE, "");
    }
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, fetch_write_cb);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, transfer);
    curl_easy_setopt(easy, CURLOPT_HEADERFUNCTION, fetch_header_cb);
    curl_easy_setopt(easy, CURLOPT_HEADERDATA, transfer);
    /* Progress ticks fire during resolve/connect/TLS — the only chance
	 * to cancel a transfer that never delivers body bytes. */
    curl_easy_setopt(easy, CURLOPT_XFERINFOFUNCTION, fetch_progress_cb);
    curl_easy_setopt(easy, CURLOPT_XFERINFODATA, transfer);
    curl_easy_setopt(easy, CURLOPT_NOPROGRESS, 0L);
    if (request->referer && *request->referer) {
        curl_easy_setopt(easy, CURLOPT_REFERER, request->referer);
    }
    if (headers) {
        curl_easy_setopt(easy, CURLOPT_HTTPHEADER, headers);
    }
    /* Method + body (fetch()/XHR). COPYPOSTFIELDS keeps its own copy so
	 * async jobs don't have to keep the caller's buffer alive. */
    if (request->body && request->body_len > 0) {
        curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)request->body_len);
        curl_easy_setopt(easy, CURLOPT_COPYPOSTFIELDS, request->body);
    }
    if (request->method && *request->method && strcasecmp(request->method, "GET") != 0 &&
        !(strcasecmp(request->method, "POST") == 0 && request->body)) {
        curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, request->method);
    }
}

/* Move the transfer result into the response. Consumes the transfer's
 * buffer on success; frees it on failure. */
static void fetch_finish_response(CURL *easy, CURLcode curl_code, struct fetch_transfer *transfer,
                                  struct yetty_ybrowser_response *response)
{
    long status = 0;
    curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &status);
    long wire_version = 0;
    if (curl_easy_getinfo(easy, CURLINFO_HTTP_VERSION, &wire_version) == CURLE_OK) {
        switch (wire_version) {
        case CURL_HTTP_VERSION_1_0:
            response->http_version = 10;
            break;
        case CURL_HTTP_VERSION_1_1:
            response->http_version = 11;
            break;
        case CURL_HTTP_VERSION_2_0:
            response->http_version = 20;
            break;
#ifdef CURL_VERSION_HTTP3
        case CURL_HTTP_VERSION_3:
            response->http_version = 30;
            break;
#endif
        default:
            response->http_version = 0;
            break;
        }
    }
    if (curl_code != CURLE_OK) {
        free(transfer->data);
        transfer->data = NULL;
        response->status = 0;
        return;
    }
    response->status = status;
    if (transfer->data) {
        transfer->data[transfer->size] = 0;
    }
    response->body = transfer->data;
    response->body_len = transfer->size;
    transfer->data = NULL;
    char *effective_url = NULL;
    if (curl_easy_getinfo(easy, CURLINFO_EFFECTIVE_URL, &effective_url) == CURLE_OK &&
        effective_url && *effective_url) {
        response->effective_url = strdup(effective_url);
    }
    char *content_type = NULL;
    if (curl_easy_getinfo(easy, CURLINFO_CONTENT_TYPE, &content_type) == CURLE_OK && content_type &&
        *content_type) {
        response->content_type = strdup(content_type);
    }
}

/* Single-flight coalescing. Claim (url, kind) before fetching; concurrent
 * fetchers of the same resource wait for the leader and re-read the cache.
 * Returns the claimed slot index, or -1 when coalescing is unavailable
 * (table full / oversized url) — the caller just fetches unshared. */
static int loader_inflight_claim(struct yetty_ybrowser_loader *loader,
                                 const struct yetty_ybrowser_request *request)
{
    if (!loader || !loader_cache_kind_ok(request) ||
        strlen(request->url) >= sizeof(loader->inflight[0].url)) {
        return -1;
    }
    pthread_mutex_lock(&loader->inflight_mutex);
    for (;;) {
        int free_slot = -1;
        int busy_slot = -1;
        for (int i = 0; i < (int)(sizeof(loader->inflight) / sizeof(loader->inflight[0])); i++) {
            if (!loader->inflight[i].active) {
                free_slot = i;
            } else if (loader->inflight[i].kind == (int)request->kind &&
                       strcmp(loader->inflight[i].url, request->url) == 0) {
                busy_slot = i;
                break;
            }
        }
        if (busy_slot >= 0) {
            /* A leader is already transferring this resource — wait for it
			 * to finish, then let the caller re-read the cache. */
            pthread_cond_wait(&loader->inflight_cond, &loader->inflight_mutex);
            continue;
        }
        if (free_slot < 0) {
            pthread_mutex_unlock(&loader->inflight_mutex);
            return -1;
        }
        snprintf(loader->inflight[free_slot].url, sizeof(loader->inflight[free_slot].url), "%s",
                 request->url);
        loader->inflight[free_slot].kind = (int)request->kind;
        loader->inflight[free_slot].active = 1;
        pthread_mutex_unlock(&loader->inflight_mutex);
        return free_slot;
    }
}

static void loader_inflight_release(struct yetty_ybrowser_loader *loader, int slot)
{
    if (!loader || slot < 0) {
        return;
    }
    pthread_mutex_lock(&loader->inflight_mutex);
    loader->inflight[slot].active = 0;
    loader->inflight[slot].url[0] = '\0';
    pthread_cond_broadcast(&loader->inflight_cond);
    pthread_mutex_unlock(&loader->inflight_mutex);
}

/* One transfer attempt: conditional headers when revalidating, 304 served
 * from the store. The caller handles the rare 304-but-entry-evicted race
 * with one unconditional retry. */
static struct yetty_ycore_void_result fetch_transfer_once(
    struct yetty_ybrowser_loader *loader, const struct yetty_ybrowser_request *request,
    struct yetty_ybrowser_response *response, const struct loader_cache_revalidation *revalidation)
{
    CURL *easy = curl_easy_init();
    if (!easy) {
        return YETTY_ERR(yetty_ycore_void, "ybrowser_fetch: curl_easy_init");
    }
    struct fetch_transfer transfer = {0};
    transfer.generation = request->generation;
    transfer.cancel_generation = request->cancel_generation;
    struct curl_slist *headers = build_request_headers(request);
    if (revalidation && revalidation->available) {
        char conditional[320];
        if (revalidation->etag[0]) {
            snprintf(conditional, sizeof(conditional), "If-None-Match: %s", revalidation->etag);
            headers = curl_slist_append(headers, conditional);
        }
        if (revalidation->last_modified[0]) {
            snprintf(conditional, sizeof(conditional), "If-Modified-Since: %s",
                     revalidation->last_modified);
            headers = curl_slist_append(headers, conditional);
        }
    }
    fetch_configure_easy(loader, easy, request, &transfer, headers);
    double t_request = yetty_ylexbor_prof_now_ms();
    CURLcode curl_code = loader_perform(loader, easy);
    fetch_finish_response(easy, curl_code, &transfer, response);
    yetty_ylexbor_prof("    HTTP %.0fms status=%ld bytes=%zu rc=%d kind=%d http=%d %.90s",
                       yetty_ylexbor_prof_now_ms() - t_request, response->status,
                       response->body_len, (int)curl_code, (int)request->kind,
                       response->http_version, request->url);
    int served_304 = 0;
    if (response->status == 304 && revalidation && revalidation->available) {
        struct yetty_ybrowser_response revalidated = {0};
        if (loader_cache_serve_revalidated(loader, request, &transfer, &revalidated)) {
            yetty_ybrowser_response_dispose(response);
            *response = revalidated;
            served_304 = 1;
        }
    }
    if (!served_304) {
        loader_cache_store(loader, request, response, &transfer);
    }
    curl_slist_free_all(headers);
    curl_easy_cleanup(easy);
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ybrowser_fetch(struct yetty_ybrowser_loader *loader,
                                                    const struct yetty_ybrowser_request *request,
                                                    struct yetty_ybrowser_response *response)
{
    if (!request || !request->url || !response) {
        return YETTY_ERR(yetty_ycore_void, "ybrowser_fetch: null request/response");
    }
    memset(response, 0, sizeof(*response));
    if (strncmp(request->url, "file://", 7) == 0) {
        response->body = fetch_local_file(request->url, &response->body_len, &response->status);
        return YETTY_OK_VOID();
    }
    struct loader_cache_revalidation revalidation;
    if (loader_cache_lookup(loader, request, response, &revalidation)) {
        yetty_ylexbor_prof("    HTTP cache-hit bytes=%zu %.90s", response->body_len, request->url);
        return YETTY_OK_VOID();
    }
    /* Coalesce concurrent same-resource misses: followers block until the
	 * leader finishes, then hit the cache it populated. */
    int inflight_slot = loader_inflight_claim(loader, request);
    if (inflight_slot >= 0 && loader_cache_lookup(loader, request, response, &revalidation)) {
        loader_inflight_release(loader, inflight_slot);
        yetty_ylexbor_prof("    HTTP coalesced bytes=%zu %.90s", response->body_len, request->url);
        return YETTY_OK_VOID();
    }

    struct yetty_ycore_void_result fetch_res =
        fetch_transfer_once(loader, request, response, &revalidation);
    if (YETTY_IS_OK(fetch_res) && response->status == 304) {
        /* Revalidation raced an eviction — one unconditional retry. */
        yetty_ybrowser_response_dispose(response);
        fetch_res = fetch_transfer_once(loader, request, response, NULL);
    }
    loader_inflight_release(loader, inflight_slot);
    return fetch_res;
}

void yetty_ybrowser_response_dispose(struct yetty_ybrowser_response *response)
{
    if (!response) {
        return;
    }
    free(response->body);
    free(response->effective_url);
    free(response->content_type);
    free(response->image_pixels);
    memset(response, 0, sizeof(*response));
}

struct yetty_ycore_void_result yetty_ybrowser_fetch_many(
    struct yetty_ybrowser_loader *loader, const struct yetty_ybrowser_request *requests, int count,
    struct yetty_ybrowser_response *responses, int host_connection_cap)
{
    if (count <= 0) {
        return YETTY_OK_VOID();
    }
    if (!requests || !responses) {
        return YETTY_ERR(yetty_ycore_void, "ybrowser_fetch_many: null arrays");
    }
    /* Per-host / total connection limits live on the loader's central
	 * scheduler now — the historical per-call cap is only used by the
	 * no-scheduler fallback ordering (which is sequential anyway). */
    (void)host_connection_cap;
    yetty_ylexbor_prof("    fetch_many START n=%d", count);
    double t_many = yetty_ylexbor_prof_now_ms();
    memset(responses, 0, (size_t)count * sizeof(*responses));

    struct fetch_slot {
        CURL *easy;
        struct fetch_transfer transfer;
        struct curl_slist *headers;
        struct loader_cache_revalidation revalidation;
        struct loader_scheduled_transfer node;
        int submitted;
        int idx;
    };
    struct fetch_slot *slots = calloc((size_t)count, sizeof(*slots));
    if (!slots) {
        return YETTY_ERR(yetty_ycore_void, "ybrowser_fetch_many: slots alloc");
    }
    int use_scheduler = loader && loader->scheduler_running;

    /* Configure + submit every transfer up front — the scheduler
	 * multiplexes the lot on shared connections. */
    for (int i = 0; i < count; i++) {
        const struct yetty_ybrowser_request *request = &requests[i];
        if (!request->url) {
            continue;
        }
        /* file:// — libcurl is often built without the FILE protocol;
		 * serve those slots directly (WPT pages link local sheets). */
        if (strncmp(request->url, "file://", 7) == 0) {
            responses[i].body =
                fetch_local_file(request->url, &responses[i].body_len, &responses[i].status);
            continue;
        }
        struct fetch_slot *slot = &slots[i];
        if (loader_cache_lookup(loader, request, &responses[i], &slot->revalidation)) {
            continue;
        }
        slot->idx = i;
        slot->easy = curl_easy_init();
        if (!slot->easy) {
            continue; /* slot stays failed (NULL body, status 0) */
        }
        slot->transfer.generation = request->generation;
        slot->transfer.cancel_generation = request->cancel_generation;
        slot->headers = build_request_headers(request);
        if (slot->revalidation.available) {
            char conditional[320];
            if (slot->revalidation.etag[0]) {
                snprintf(conditional, sizeof(conditional), "If-None-Match: %s",
                         slot->revalidation.etag);
                slot->headers = curl_slist_append(slot->headers, conditional);
            }
            if (slot->revalidation.last_modified[0]) {
                snprintf(conditional, sizeof(conditional), "If-Modified-Since: %s",
                         slot->revalidation.last_modified);
                slot->headers = curl_slist_append(slot->headers, conditional);
            }
        }
        fetch_configure_easy(loader, slot->easy, request, &slot->transfer, slot->headers);
        if (use_scheduler) {
            slot->node.easy = slot->easy;
            loader_scheduler_submit(loader, &slot->node);
            slot->submitted = 1;
        }
    }

    /* Collect completions in slot order (the scheduler runs them all
	 * concurrently; order here only sequences the bookkeeping). The
	 * no-scheduler fallback performs each transfer inline. */
    for (int i = 0; i < count; i++) {
        struct fetch_slot *slot = &slots[i];
        if (!slot->easy) {
            continue;
        }
        CURLcode transfer_code = slot->submitted ? loader_scheduler_wait(loader, &slot->node)
                                                 : curl_easy_perform(slot->easy);
        fetch_finish_response(slot->easy, transfer_code, &slot->transfer, &responses[slot->idx]);
        yetty_ylexbor_prof("      done status=%ld bytes=%zu http=%d %.60s",
                           responses[slot->idx].status, responses[slot->idx].body_len,
                           responses[slot->idx].http_version, requests[slot->idx].url);
        if (responses[slot->idx].status == 304 && slot->revalidation.available) {
            /* Conditional refetch validated — serve the stored body. */
            struct yetty_ybrowser_response revalidated = {0};
            if (loader_cache_serve_revalidated(loader, &requests[slot->idx], &slot->transfer,
                                               &revalidated)) {
                yetty_ybrowser_response_dispose(&responses[slot->idx]);
                responses[slot->idx] = revalidated;
            }
        } else {
            loader_cache_store(loader, &requests[slot->idx], &responses[slot->idx],
                               &slot->transfer);
        }
        curl_easy_cleanup(slot->easy);
        slot->easy = NULL;
        curl_slist_free_all(slot->headers);
        slot->headers = NULL;
    }
    free(slots);
    yetty_ylexbor_prof("    fetch_many DONE  %.0f ms (n=%d)", yetty_ylexbor_prof_now_ms() - t_many,
                       count);
    return YETTY_OK_VOID();
}

#else /* !YETTY_HAVE_CURL */

void yetty_ybrowser_response_dispose(struct yetty_ybrowser_response *response)
{
    if (!response) {
        return;
    }
    free(response->body);
    free(response->effective_url);
    free(response->content_type);
    free(response->image_pixels);
    memset(response, 0, sizeof(*response));
}

struct yetty_ycore_void_result yetty_ybrowser_fetch(struct yetty_ybrowser_loader *loader,
                                                    const struct yetty_ybrowser_request *request,
                                                    struct yetty_ybrowser_response *response)
{
    (void)loader;
    (void)request;
    if (response) {
        memset(response, 0, sizeof(*response));
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ybrowser_fetch_many(
    struct yetty_ybrowser_loader *loader, const struct yetty_ybrowser_request *requests, int count,
    struct yetty_ybrowser_response *responses, int host_connection_cap)
{
    (void)loader;
    (void)requests;
    (void)host_connection_cap;
    if (responses && count > 0) {
        memset(responses, 0, (size_t)count * sizeof(*responses));
    }
    return YETTY_OK_VOID();
}

/* Without libcurl there is nothing to own, but the lifecycle must still
 * work so hosts can be compiled either way. */
struct yetty_ybrowser_loader {
    int unused;
};

struct yetty_ybrowser_loader_ptr_result yetty_ybrowser_loader_create(void)
{
    struct yetty_ybrowser_loader *loader = calloc(1, sizeof(*loader));
    if (loader == NULL) {
        return YETTY_ERR(yetty_ybrowser_loader_ptr, "loader alloc");
    }
    return YETTY_OK(yetty_ybrowser_loader_ptr, loader);
}

struct yetty_ycore_void_result yetty_ybrowser_loader_destroy(struct yetty_ybrowser_loader *loader)
{
    free(loader);
    return YETTY_OK_VOID();
}

void *yetty_ybrowser_loader_curl_share(struct yetty_ybrowser_loader *loader)
{
    (void)loader;
    return NULL;
}

void yetty_ybrowser_loader_cache_put_pixels(struct yetty_ybrowser_loader *loader, const char *url,
                                            const uint32_t *pixels, int width, int height)
{
    (void)loader;
    (void)url;
    (void)pixels;
    (void)width;
    (void)height;
}

#endif /* YETTY_HAVE_CURL */

/* ===========================================================================
 * Everything below this point is the JS-side bindings (fetch/XHR/
 * timers/window/etc.). Gated on QuickJS being compiled in.
 * ===========================================================================*/

#if YETTY_HAVE_QUICKJS

#include <quickjs.h>

static struct yetty_ylexbor *runtime_ylex_w(JSContext *ctx)
{
    JSRuntime *rt = JS_GetRuntime(ctx);
    struct {
        struct yetty_ylexbor *r;
    } *s = JS_GetRuntimeOpaque(rt);
    return s ? s->r : NULL;
}

static int64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* ===========================================================================
 * Timers — single linear array sorted by deadline_ms.
 * ===========================================================================*/

struct yetty_ylexbor_timer {
    int id;
    int interval_ms; /* 0 → one-shot */
    int64_t deadline_ms;
    JSValue handler;
    JSContext *ctx;
};

static int timer_cmp(const void *a, const void *b)
{
    const struct yetty_ylexbor_timer *x = *(const struct yetty_ylexbor_timer **)a;
    const struct yetty_ylexbor_timer *y = *(const struct yetty_ylexbor_timer **)b;
    if (x->deadline_ms < y->deadline_ms) {
        return -1;
    }
    if (x->deadline_ms > y->deadline_ms) {
        return 1;
    }
    return 0;
}

static int timer_add(struct yetty_ylexbor *r, JSContext *ctx, JSValueConst handler, int delay_ms,
                     int interval_ms)
{
    if (r->timer_count == r->timer_cap) {
        int nc = r->timer_cap ? r->timer_cap * 2 : 8;
        void *p = realloc(r->timers, nc * sizeof(*r->timers));
        if (!p) {
            return -1;
        }
        r->timers = p;
        r->timer_cap = nc;
    }
    struct yetty_ylexbor_timer *t = calloc(1, sizeof(*t));
    if (!t) {
        return -1;
    }
    t->id = ++r->next_timer_id;
    t->ctx = ctx;
    t->handler = JS_DupValue(ctx, handler);
    t->deadline_ms = now_ms() + delay_ms;
    t->interval_ms = interval_ms;
    r->timers[r->timer_count++] = t;
    qsort(r->timers, r->timer_count, sizeof(*r->timers), timer_cmp);
    return t->id;
}

static void timer_remove(struct yetty_ylexbor *r, int id)
{
    for (int i = 0; i < r->timer_count; i++) {
        if (r->timers[i]->id != id) {
            continue;
        }
        JS_FreeValue(r->timers[i]->ctx, r->timers[i]->handler);
        free(r->timers[i]);
        memmove(&r->timers[i], &r->timers[i + 1], (r->timer_count - i - 1) * sizeof(*r->timers));
        r->timer_count--;
        return;
    }
}

static JSValue js_setTimeout(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) {
        return JS_NewInt32(ctx, 0);
    }
    struct yetty_ylexbor *r = runtime_ylex_w(ctx);
    if (!r) {
        return JS_NewInt32(ctx, 0);
    }
    int delay = 0;
    if (argc >= 2) {
        JS_ToInt32(ctx, &delay, argv[1]);
    }
    if (delay < 0) {
        delay = 0;
    }
    int id = timer_add(r, ctx, argv[0], delay, /*interval=*/0);
    return JS_NewInt32(ctx, id);
}

static JSValue js_setInterval(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) {
        return JS_NewInt32(ctx, 0);
    }
    struct yetty_ylexbor *r = runtime_ylex_w(ctx);
    if (!r) {
        return JS_NewInt32(ctx, 0);
    }
    int delay = 0;
    if (argc >= 2) {
        JS_ToInt32(ctx, &delay, argv[1]);
    }
    if (delay < 4) {
        delay = 4;
    }
    int id = timer_add(r, ctx, argv[0], delay, /*interval=*/delay);
    return JS_NewInt32(ctx, id);
}

static JSValue js_clearTimer(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    struct yetty_ylexbor *r = runtime_ylex_w(ctx);
    if (!r || argc < 1) {
        return JS_UNDEFINED;
    }
    int id = 0;
    JS_ToInt32(ctx, &id, argv[0]);
    timer_remove(r, id);
    return JS_UNDEFINED;
}

static JSValue js_requestAnimationFrame(JSContext *ctx, JSValueConst this_val, int argc,
                                        JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) {
        return JS_NewInt32(ctx, 0);
    }
    struct yetty_ylexbor *r = runtime_ylex_w(ctx);
    if (!r) {
        return JS_NewInt32(ctx, 0);
    }
    return JS_NewInt32(ctx, timer_add(r, ctx, argv[0], 16, 0));
}

static JSValue js_queueMicrotask(JSContext *ctx, JSValueConst this_val, int argc,
                                 JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1 || !JS_IsFunction(ctx, argv[0])) {
        return JS_UNDEFINED;
    }
    struct yetty_ylexbor *r = runtime_ylex_w(ctx);
    /* Schedule as a 0ms timer — the next pump() call drains it. */
    if (r) {
        timer_add(r, ctx, argv[0], 0, 0);
    }
    return JS_UNDEFINED;
}

void yetty_ylexbor_js_drain_jobs(struct yetty_ylexbor *r)
{
    if (!r->js_rt) {
        return;
    }
    JSContext *ctx = (JSContext *)r->js_ctx;
    JSContext *jc;
    int n = 0;
    while ((n = JS_ExecutePendingJob((JSRuntime *)r->js_rt, &jc)) > 0) {
        /* keep draining */
    }
    if (n < 0) {
        JSValue ex = JS_GetException(ctx);
        const char *m = JS_ToCString(ctx, ex);
        ydebug("js job-exception: %s", m ? m : "?");
        if (m) {
            JS_FreeCString(ctx, m);
        }
        JS_FreeValue(ctx, ex);
        r->js_error_count++;
    }
}

void yetty_ylexbor_js_web_shutdown(struct yetty_ylexbor *r)
{
    if (!r->timers) {
        return;
    }
    JSContext *ctx = (JSContext *)r->js_ctx;
    for (int i = 0; i < r->timer_count; i++) {
        if (ctx) {
            JS_FreeValue(ctx, r->timers[i]->handler);
        }
        free(r->timers[i]);
    }
    free(r->timers);
    r->timers = NULL;
    r->timer_count = r->timer_cap = 0;
}

int yetty_ylexbor_pump(struct yetty_ylexbor *r)
{
    if (!r) {
        return -1;
    }
    if (!r->js_ctx) {
        return -1;
    }
    JSContext *ctx = (JSContext *)r->js_ctx;

    int64_t now = now_ms();
    while (r->timer_count > 0 && r->timers[0]->deadline_ms <= now) {
        /* Pop the timer out of the array BEFORE invoking its
		 * handler. If the handler calls clearTimeout/clearInterval
		 * on its own id (idiomatic for "fire 3 times then stop"),
		 * timer_remove walks r->timers and our hot pointer would
		 * become a UAF. Owning `t` locally — and reinserting only
		 * if it's still a live interval — sidesteps that. */
        struct yetty_ylexbor_timer *t = r->timers[0];
        memmove(&r->timers[0], &r->timers[1], (r->timer_count - 1) * sizeof(*r->timers));
        r->timer_count--;

        int saved_id = t->id;
        int saved_interval = t->interval_ms;
        JSValue handler = t->handler;
        JSValue ret = JS_Call(ctx, handler, JS_UNDEFINED, 0, NULL);
        if (JS_IsException(ret)) {
            JSValue ex = JS_GetException(ctx);
            const char *m = JS_ToCString(ctx, ex);
            ydebug("js timer: %s", m ? m : "?");
            if (m) {
                JS_FreeCString(ctx, m);
            }
            JS_FreeValue(ctx, ex);
            r->js_error_count++;
        }
        JS_FreeValue(ctx, ret);

        /* Did the handler clearInterval its own id? If yes,
		 * timer_remove ran but didn't find us in the array
		 * (we're already popped) — the *next* clearTimeout on
		 * the same id would be a no-op, but the handler ref we
		 * held has not been freed. We free it here. */
        int still_alive = saved_interval > 0;
        if (still_alive) {
            /* If the handler also explicitly cleared the
			 * timer, timer_remove doesn't see it (popped),
			 * so we need a separate sentinel. We use a
			 * `_disarmed` flag set by timer_remove — but we
			 * don't have one yet. Rely on this convention:
			 * our handlers either call clear (cancel) OR let
			 * the interval re-fire. clearInterval is a
			 * cooperative drop. To be safe against handlers
			 * that do BOTH (spec-illegal but seen in real
			 * code), we check `r->timer_count` for any entry
			 * with the same id and skip reinsert. */
            for (int i = 0; i < r->timer_count; i++) {
                if (r->timers[i]->id == saved_id) {
                    still_alive = 0;
                    break;
                }
            }
        }

        if (still_alive) {
            t->deadline_ms = now + t->interval_ms;
            /* Re-insert in deadline order. */
            if (r->timer_count == r->timer_cap) {
                int nc = r->timer_cap ? r->timer_cap * 2 : 8;
                void *p = realloc(r->timers, nc * sizeof(*r->timers));
                if (!p) {
                    JS_FreeValue(ctx, t->handler);
                    free(t);
                    goto after_call;
                }
                r->timers = p;
                r->timer_cap = nc;
            }
            r->timers[r->timer_count++] = t;
            qsort(r->timers, r->timer_count, sizeof(*r->timers), timer_cmp);
        } else {
            JS_FreeValue(ctx, t->handler);
            free(t);
        }
    after_call:
        yetty_ylexbor_js_drain_jobs(r);
        now = now_ms();
    }
    yetty_ylexbor_js_drain_jobs(r);
    if (r->timer_count == 0) {
        return -1;
    }
    int64_t delta = r->timers[0]->deadline_ms - now;
    return delta < 0 ? 0 : (int)delta;
}

/* ===========================================================================
 * fetch() — synchronous libcurl behind an immediately-resolved Promise.
 *
 * `fetch(url)` parses the url, runs the request, and returns a
 * Promise that resolves to a Response object with .text(), .json(),
 * .arrayBuffer(), .ok, .status. Anything that errors rejects.
 * ===========================================================================*/

static JSValue make_response(JSContext *ctx, char *body, size_t len, long status)
{
    JSValue resp = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, resp, "status", JS_NewInt32(ctx, (int32_t)status));
    JS_SetPropertyStr(ctx, resp, "ok", JS_NewBool(ctx, status >= 200 && status < 300));
    JS_SetPropertyStr(ctx, resp, "statusText", JS_NewString(ctx, ""));
    JS_SetPropertyStr(ctx, resp, "redirected", JS_FALSE);
    JS_SetPropertyStr(ctx, resp, "type", JS_NewString(ctx, "basic"));
    JSValue body_str = body ? JS_NewStringLen(ctx, body, len) : JS_NewString(ctx, "");
    JS_SetPropertyStr(ctx, resp, "_body", body_str);
    /* Methods — closures-over-_body. */
    const char *def =
        "r => { "
        "r.text = () => Promise.resolve(r._body); "
        "r.json = () => Promise.resolve(JSON.parse(r._body)); "
        "r.arrayBuffer = () => { "
        "  const a = new ArrayBuffer(r._body.length); "
        "  const v = new Uint8Array(a); "
        "  for (let i = 0; i < r._body.length; i++) v[i] = r._body.charCodeAt(i) & 0xff; "
        "  return Promise.resolve(a); "
        "}; "
        "r.headers = { get: ()=>null, has: ()=>false, "
        "  forEach: ()=>{}, entries: function*(){} }; "
        "r.clone = () => r; "
        "return r; }";
    JSValue installer = JS_Eval(ctx, def, strlen(def), "<response-init>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(installer)) {
        JS_FreeValue(ctx, installer);
        return resp;
    }
    JSValueConst args[] = {resp};
    JSValue res = JS_Call(ctx, installer, JS_UNDEFINED, 1, args);
    JS_FreeValue(ctx, installer);
    if (!JS_IsException(res)) {
        JS_FreeValue(ctx, res);
    }
    free(body);
    return resp;
}

/* Owned copies of everything a fetch() call needs off the JS thread. */
struct js_fetch_params {
    char *url;
    char *method;
    char *body;
    size_t body_len;
    char **header_lines;
    int header_count;
};

static void js_fetch_params_free(struct js_fetch_params *params)
{
    free(params->url);
    free(params->method);
    free(params->body);
    for (int i = 0; i < params->header_count; i++) {
        free(params->header_lines[i]);
    }
    free(params->header_lines);
    memset(params, 0, sizeof(*params));
}

/* Parse the fetch() init object: method, body (string), headers (plain
 * object of name → value). Anything else (FormData, streams, signals)
 * is ignored — existence is enough for the SPA boot paths we serve. */
static void js_fetch_parse_init(JSContext *ctx, JSValueConst init, struct js_fetch_params *params)
{
    if (!JS_IsObject(init)) {
        return;
    }
    JSValue method_val = JS_GetPropertyStr(ctx, init, "method");
    if (JS_IsString(method_val)) {
        const char *method = JS_ToCString(ctx, method_val);
        if (method) {
            params->method = strdup(method);
            JS_FreeCString(ctx, method);
        }
    }
    JS_FreeValue(ctx, method_val);

    JSValue body_val = JS_GetPropertyStr(ctx, init, "body");
    if (JS_IsString(body_val)) {
        size_t body_len = 0;
        const char *body = JS_ToCStringLen(ctx, &body_len, body_val);
        if (body) {
            params->body = malloc(body_len + 1);
            if (params->body) {
                memcpy(params->body, body, body_len);
                params->body[body_len] = '\0';
                params->body_len = body_len;
            }
            JS_FreeCString(ctx, body);
        }
    }
    JS_FreeValue(ctx, body_val);

    JSValue headers_val = JS_GetPropertyStr(ctx, init, "headers");
    if (JS_IsObject(headers_val)) {
        JSPropertyEnum *props = NULL;
        uint32_t prop_count = 0;
        if (JS_GetOwnPropertyNames(ctx, &props, &prop_count, headers_val,
                                   JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) == 0) {
            params->header_lines =
                calloc(prop_count ? prop_count : 1, sizeof(*params->header_lines));
            for (uint32_t i = 0; params->header_lines && i < prop_count; i++) {
                const char *name = JS_AtomToCString(ctx, props[i].atom);
                JSValue value_val = JS_GetProperty(ctx, headers_val, props[i].atom);
                const char *value = JS_ToCString(ctx, value_val);
                if (name && value) {
                    size_t line_len = strlen(name) + 2 + strlen(value) + 1;
                    char *line = malloc(line_len);
                    if (line) {
                        snprintf(line, line_len, "%s: %s", name, value);
                        params->header_lines[params->header_count++] = line;
                    }
                }
                if (name) {
                    JS_FreeCString(ctx, name);
                }
                if (value) {
                    JS_FreeCString(ctx, value);
                }
                JS_FreeValue(ctx, value_val);
            }
            JS_FreePropertyEnum(ctx, props, prop_count);
        }
    }
    JS_FreeValue(ctx, headers_val);
}

/* Deliver a completed fetch to JS: resolve with a Response (transfers
 * body ownership to make_response) or reject with a network error. */
static void js_fetch_deliver(JSContext *ctx, struct yetty_ybrowser_response *response,
                             JSValueConst resolve_func, JSValueConst reject_func)
{
    if (response->body || response->status != 0) {
        JSValue response_obj =
            make_response(ctx, response->body, response->body_len, response->status);
        response->body = NULL; /* ownership moved to make_response */
        JS_Call(ctx, resolve_func, JS_UNDEFINED, 1, (JSValueConst[]){response_obj});
        JS_FreeValue(ctx, response_obj);
    } else {
        JSValue error_obj = JS_NewError(ctx);
        JS_SetPropertyStr(ctx, error_obj, "message", JS_NewString(ctx, "fetch: network error"));
        JS_Call(ctx, reject_func, JS_UNDEFINED, 1, (JSValueConst[]){error_obj});
        JS_FreeValue(ctx, error_obj);
    }
}

/* Async fetch job — the network transfer runs on a worker thread; the
 * promise resolves back on the loop thread. Same lifetime discipline as
 * the image jobs in ybrowser-paint.c: the engine defers teardown until
 * every job drains, so `r` (and its js context) outlive the job. */
struct js_fetch_job {
    struct yetty_ylexbor *r;
    uint64_t generation;
    struct yetty_ybrowser_loader *loader;
    struct js_fetch_params params; /* owned */
    char *referer;                 /* owned copy of base_url */
    struct yetty_ybrowser_response response;
    JSContext *ctx;
    JSValue resolve_func, reject_func; /* dup'd refs, freed in done() */
};

/* WORKER THREAD — touches only the job's own copies, never the engine
 * (except the read-only cancel counter, which the deferred teardown
 * keeps alive for the job's whole life). */
static void js_fetch_job_run(void *job_ptr)
{
    struct js_fetch_job *job = job_ptr;
    struct yetty_ybrowser_request request = {
        .url = job->params.url,
        .kind = YETTY_YBROWSER_REQUEST_XHR,
        .referer = job->referer,
        .method = job->params.method,
        .body = job->params.body,
        .body_len = job->params.body_len,
        .extra_headers = (const char *const *)job->params.header_lines,
        .extra_header_count = job->params.header_count,
        .generation = job->generation,
        .cancel_generation = &job->r->fetch_generation,
    };
    struct yetty_ycore_void_result fetch_res =
        yetty_ybrowser_fetch(job->loader, &request, &job->response);
    if (YETTY_IS_ERR(fetch_res)) {
        yetty_ycore_error_destroy(fetch_res.error);
    }
}

/* LOOP THREAD. Resolve/reject the promise and drain microtasks so .then
 * chains run now, then release the job. Signature dictated by the work
 * pool (void (*)(void *)) — absorb inner Results at this boundary. */
YETTY_EXTERNAL_CALLBACK
static void js_fetch_job_done(void *job_ptr)
{
    struct js_fetch_job *job = job_ptr;
    struct yetty_ylexbor *r = job->r;
    r->img_jobs_in_flight--;

    int stale = (job->generation != r->fetch_generation) || r->destroy_pending;
    if (!stale && r->js_ctx) {
        js_fetch_deliver(job->ctx, &job->response, job->resolve_func, job->reject_func);
        yetty_ylexbor_js_drain_jobs(r);
        if (r->on_resource_ready) {
            r->on_resource_ready(r->resource_ready_user);
        }
    }
    /* The context is still alive even on the deferred-teardown path —
	 * _yetty_ylexbor_destroy_now only runs below, after the last job. */
    JS_FreeValue(job->ctx, job->resolve_func);
    JS_FreeValue(job->ctx, job->reject_func);
    yetty_ybrowser_response_dispose(&job->response);
    js_fetch_params_free(&job->params);
    free(job->referer);
    free(job);

    if (r->destroy_pending && r->img_jobs_in_flight == 0) {
        struct yetty_ycore_void_result destroy_res = _yetty_ylexbor_destroy_now(r);
        if (YETTY_IS_ERR(destroy_res)) {
            ydebug("js_fetch_job_done: deferred destroy failed: %s", destroy_res.error.msg);
            yetty_ycore_error_destroy(destroy_res.error);
        }
    }
}

static JSValue js_fetch(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    struct yetty_ylexbor *r = runtime_ylex_w(ctx);

    JSValue resolving_funcs[2];
    JSValue promise = JS_NewPromiseCapability(ctx, resolving_funcs);
    if (JS_IsException(promise)) {
        return promise;
    }

    if (argc < 1 || !r) {
        JSValue error_obj = JS_NewError(ctx);
        JS_SetPropertyStr(ctx, error_obj, "message", JS_NewString(ctx, "fetch: missing url"));
        JS_Call(ctx, resolving_funcs[1], JS_UNDEFINED, 1, (JSValueConst[]){error_obj});
        JS_FreeValue(ctx, error_obj);
        goto out;
    }
    const char *url_arg = JS_ToCString(ctx, argv[0]);
    if (!url_arg) {
        goto out;
    }
    struct js_fetch_params params = {0};
    params.url = yetty_ylexbor_resolve_url(r, url_arg);
    JS_FreeCString(ctx, url_arg);
    if (!params.url) {
        JSValue error_obj = JS_NewError(ctx);
        JS_Call(ctx, resolving_funcs[1], JS_UNDEFINED, 1, (JSValueConst[]){error_obj});
        JS_FreeValue(ctx, error_obj);
        goto out;
    }
    if (argc >= 2) {
        js_fetch_parse_init(ctx, argv[1], &params);
    }

    /* Async path: hand the transfer to the worker pool and resolve from
	 * its done() on the loop thread — three concurrent fetch()es overlap
	 * on the wire and the UI pump keeps ticking. Without a pool (one-shot
	 * render, in-yetty client) fall back to the synchronous fetch. */
    if (r->img_pool) {
        struct js_fetch_job *job = calloc(1, sizeof(*job));
        if (job) {
            job->r = r;
            job->generation = r->fetch_generation;
            job->loader = r->loader;
            job->params = params; /* ownership moves to the job */
            job->referer = r->base_url ? strdup(r->base_url) : NULL;
            job->ctx = ctx;
            job->resolve_func = JS_DupValue(ctx, resolving_funcs[0]);
            job->reject_func = JS_DupValue(ctx, resolving_funcs[1]);
            struct yetty_yplatform_yworkpool_job pool_job = {
                .run = js_fetch_job_run,
                .done = js_fetch_job_done,
                .ctx = job,
            };
            struct yetty_ycore_void_result submit_res =
                yetty_yplatform_yworkpool_submit(r->img_pool, pool_job);
            if (YETTY_IS_OK(submit_res)) {
                r->img_jobs_in_flight++;
                goto out;
            }
            yetty_ycore_error_destroy(submit_res.error);
            JS_FreeValue(ctx, job->resolve_func);
            JS_FreeValue(ctx, job->reject_func);
            params = job->params; /* take ownership back for the sync path */
            free(job->referer);
            free(job);
        }
    }

    /* Synchronous path. */
    {
        struct yetty_ybrowser_request request = {
            .url = params.url,
            .kind = YETTY_YBROWSER_REQUEST_XHR,
            .referer = r->base_url,
            .method = params.method,
            .body = params.body,
            .body_len = params.body_len,
            .extra_headers = (const char *const *)params.header_lines,
            .extra_header_count = params.header_count,
        };
        struct yetty_ybrowser_response response = {0};
        struct yetty_ycore_void_result fetch_res =
            yetty_ybrowser_fetch(r->loader, &request, &response);
        if (YETTY_IS_ERR(fetch_res)) {
            yetty_ycore_error_destroy(fetch_res.error);
        }
        js_fetch_deliver(ctx, &response, resolving_funcs[0], resolving_funcs[1]);
        yetty_ybrowser_response_dispose(&response);
        js_fetch_params_free(&params);
    }

out:
    JS_FreeValue(ctx, resolving_funcs[0]);
    JS_FreeValue(ctx, resolving_funcs[1]);
    return promise;
}

/* ===========================================================================
 * Storage — in-memory backing for localStorage / sessionStorage.
 *
 * One engine-owned map per kind (see yetty_ylexbor.web_local_storage /
 * .web_session_storage) so two documents never see each other's keys.
 * Crude string→string. "Storage event" dispatch and quota are TODO;
 * this just satisfies feature-detection. Freed by the engine destroy.
 * ===========================================================================*/

static int kv_find(const struct yetty_ylexbor_kv_store *store, const char *key)
{
    for (int i = 0; i < store->count; i++) {
        if (strcmp(store->items[i].key, key) == 0) {
            return i;
        }
    }
    return -1;
}

static JSValue storage_get_item(JSContext *ctx, struct yetty_ylexbor_kv_store *store, int argc,
                                JSValueConst *argv)
{
    if (!store || argc < 1) {
        return JS_NULL;
    }
    const char *key = JS_ToCString(ctx, argv[0]);
    if (!key) {
        return JS_NULL;
    }
    int i = kv_find(store, key);
    JS_FreeCString(ctx, key);
    return i < 0 ? JS_NULL : JS_NewString(ctx, store->items[i].value);
}

static JSValue storage_set_item(JSContext *ctx, struct yetty_ylexbor_kv_store *store, int argc,
                                JSValueConst *argv)
{
    if (!store || argc < 2) {
        return JS_UNDEFINED;
    }
    const char *key = JS_ToCString(ctx, argv[0]);
    const char *value = JS_ToCString(ctx, argv[1]);
    if (!key || !value) {
        if (key) {
            JS_FreeCString(ctx, key);
        }
        if (value) {
            JS_FreeCString(ctx, value);
        }
        return JS_UNDEFINED;
    }
    int i = kv_find(store, key);
    if (i >= 0) {
        free(store->items[i].value);
        store->items[i].value = strdup(value);
    } else {
        if (store->count == store->cap) {
            int new_cap = store->cap ? store->cap * 2 : 16;
            struct yetty_ylexbor_kv_entry *items =
                realloc(store->items, (size_t)new_cap * sizeof(*items));
            if (!items) {
                JS_FreeCString(ctx, key);
                JS_FreeCString(ctx, value);
                return JS_UNDEFINED;
            }
            store->items = items;
            store->cap = new_cap;
        }
        store->items[store->count].key = strdup(key);
        store->items[store->count].value = strdup(value);
        store->count++;
    }
    JS_FreeCString(ctx, key);
    JS_FreeCString(ctx, value);
    return JS_UNDEFINED;
}

static JSValue storage_remove_item(JSContext *ctx, struct yetty_ylexbor_kv_store *store, int argc,
                                   JSValueConst *argv)
{
    if (!store || argc < 1) {
        return JS_UNDEFINED;
    }
    const char *key = JS_ToCString(ctx, argv[0]);
    if (!key) {
        return JS_UNDEFINED;
    }
    int i = kv_find(store, key);
    JS_FreeCString(ctx, key);
    if (i < 0) {
        return JS_UNDEFINED;
    }
    free(store->items[i].key);
    free(store->items[i].value);
    memmove(&store->items[i], &store->items[i + 1],
            (size_t)(store->count - i - 1) * sizeof(store->items[0]));
    store->count--;
    return JS_UNDEFINED;
}

static JSValue storage_clear(struct yetty_ylexbor_kv_store *store)
{
    if (!store) {
        return JS_UNDEFINED;
    }
    for (int i = 0; i < store->count; i++) {
        free(store->items[i].key);
        free(store->items[i].value);
    }
    store->count = 0;
    return JS_UNDEFINED;
}

static JSValue storage_key(JSContext *ctx, struct yetty_ylexbor_kv_store *store, int argc,
                           JSValueConst *argv)
{
    if (!store || argc < 1) {
        return JS_NULL;
    }
    int i = 0;
    JS_ToInt32(ctx, &i, argv[0]);
    if (i < 0 || i >= store->count) {
        return JS_NULL;
    }
    return JS_NewString(ctx, store->items[i].key);
}

/* Thin JS-callback wrappers routing to the right engine-owned store. */
static struct yetty_ylexbor_kv_store *local_store(JSContext *ctx)
{
    struct yetty_ylexbor *r = runtime_ylex_w(ctx);
    return r ? &r->web_local_storage : NULL;
}

static struct yetty_ylexbor_kv_store *session_store(JSContext *ctx)
{
    struct yetty_ylexbor *r = runtime_ylex_w(ctx);
    return r ? &r->web_session_storage : NULL;
}

#define DEFINE_STORAGE(NAME, STORE_FOR_CTX)                                                        \
    static JSValue js_##NAME##_getItem(JSContext *ctx, JSValueConst self, int argc,                \
                                       JSValueConst *argv)                                         \
    {                                                                                              \
        (void)self;                                                                                \
        return storage_get_item(ctx, STORE_FOR_CTX(ctx), argc, argv);                              \
    }                                                                                              \
    static JSValue js_##NAME##_setItem(JSContext *ctx, JSValueConst self, int argc,                \
                                       JSValueConst *argv)                                         \
    {                                                                                              \
        (void)self;                                                                                \
        return storage_set_item(ctx, STORE_FOR_CTX(ctx), argc, argv);                              \
    }                                                                                              \
    static JSValue js_##NAME##_removeItem(JSContext *ctx, JSValueConst self, int argc,             \
                                          JSValueConst *argv)                                      \
    {                                                                                              \
        (void)self;                                                                                \
        return storage_remove_item(ctx, STORE_FOR_CTX(ctx), argc, argv);                           \
    }                                                                                              \
    static JSValue js_##NAME##_clear(JSContext *ctx, JSValueConst self, int argc,                  \
                                     JSValueConst *argv)                                           \
    {                                                                                              \
        (void)self;                                                                                \
        (void)argc;                                                                                \
        (void)argv;                                                                                \
        return storage_clear(STORE_FOR_CTX(ctx));                                                  \
    }                                                                                              \
    static JSValue js_##NAME##_key(JSContext *ctx, JSValueConst self, int argc,                    \
                                   JSValueConst *argv)                                             \
    {                                                                                              \
        (void)self;                                                                                \
        return storage_key(ctx, STORE_FOR_CTX(ctx), argc, argv);                                   \
    }

DEFINE_STORAGE(local, local_store)
DEFINE_STORAGE(session, session_store)

/* ===========================================================================
 * Cookie store — backs document.cookie. Engine-owned so tabs/documents
 * never leak cookies into each other; freed by the engine destroy.
 * ===========================================================================*/

static JSValue js_doc_cookie_get(JSContext *ctx, JSValueConst this_val)
{
    (void)this_val;
    struct yetty_ylexbor *r = runtime_ylex_w(ctx);
    const char *cookie = r && r->web_cookie_string ? r->web_cookie_string : "";
    return JS_NewString(ctx, cookie);
}
static JSValue js_doc_cookie_set(JSContext *ctx, JSValueConst this_val, JSValueConst val)
{
    (void)this_val;
    struct yetty_ylexbor *r = runtime_ylex_w(ctx);
    if (!r) {
        return JS_UNDEFINED;
    }
    const char *s = JS_ToCString(ctx, val);
    if (!s) {
        return JS_UNDEFINED;
    }
    /* Append (real cookie semantics is way more complicated). */
    if (!r->web_cookie_string) {
        r->web_cookie_string = strdup(s);
    } else {
        size_t old_len = strlen(r->web_cookie_string), add_len = strlen(s);
        char *p = realloc(r->web_cookie_string, old_len + add_len + 3);
        if (p) {
            strcat(p, "; ");
            strcat(p, s);
            r->web_cookie_string = p;
        }
    }
    JS_FreeCString(ctx, s);
    return JS_UNDEFINED;
}

/* ===========================================================================
 * matchMedia / getComputedStyle — feature-detection-friendly minimal
 * shapes.
 * ===========================================================================*/

static JSValue js_matchMedia(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    const char *def = "({ matches: false, media: '', "
                      "   addEventListener: ()=>{}, removeEventListener: ()=>{}, "
                      "   addListener: ()=>{}, removeListener: ()=>{}, "
                      "   dispatchEvent: ()=>false, onchange: null })";
    return JS_Eval(ctx, def, strlen(def), "<matchMedia>", JS_EVAL_TYPE_GLOBAL);
}

static JSValue js_getComputedStyle(JSContext *ctx, JSValueConst this_val, int argc,
                                   JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) {
        return JS_NULL;
    }
    /* Return the same `style` proxy we use for el.style — JS code
	 * that only reads inline-style values keeps working; reads of
	 * properties not set in the inline style get "". JS_GetPropertyStr
	 * already returns an owned reference we transfer to the caller;
	 * don't JS_DupValue it or the original reference leaks. */
    return JS_GetPropertyStr(ctx, argv[0], "style");
}

/* ===========================================================================
 * Crypto — getRandomValues real, the rest stubs that throw or return
 * never-resolving promises so feature detection succeeds.
 * ===========================================================================*/

static JSValue js_crypto_getRandomValues(JSContext *ctx, JSValueConst this_val, int argc,
                                         JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) {
        return JS_UNDEFINED;
    }
    /* TypedArray view: we just write to its underlying buffer via
	 * JS_GetTypedArrayBuffer (best-effort). */
    size_t bo = 0, blen = 0, bps = 0;
    JSValue ab = JS_GetTypedArrayBuffer(ctx, argv[0], &bo, &blen, &bps);
    if (JS_IsException(ab)) {
        return JS_DupValue(ctx, argv[0]);
    }
    size_t abl;
    uint8_t *data = JS_GetArrayBuffer(ctx, &abl, ab);
    if (data) {
        FILE *f = fopen("/dev/urandom", "rb");
        if (f) {
            (void)!fread(data + bo, 1, blen, f);
            fclose(f);
        }
    }
    JS_FreeValue(ctx, ab);
    return JS_DupValue(ctx, argv[0]);
}

static JSValue js_crypto_randomUUID(JSContext *ctx, JSValueConst this_val, int argc,
                                    JSValueConst *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    uint8_t b[16] = {0};
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) {
        (void)!fread(b, 1, 16, f);
        fclose(f);
    }
    b[6] = (b[6] & 0x0f) | 0x40;
    b[8] = (b[8] & 0x3f) | 0x80;
    char buf[37];
    snprintf(buf, sizeof(buf),
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", b[0], b[1],
             b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14],
             b[15]);
    return JS_NewString(ctx, buf);
}

/* ===========================================================================
 * Install everything onto globalThis.
 * ===========================================================================*/

/* globalThis is shared across the runtime; we cannot install funcs onto
 * it via JS_SetPropertyFunctionList (it auto-init aborts on properties
 * the global already owns, which on QuickJS-NG includes the timer-ish
 * names when std is loaded). Use per-name JS_SetPropertyStr instead —
 * that does a plain set which transparently overrides built-ins. */
static void install_global_fn(JSContext *ctx, JSValue global, const char *name, JSCFunction *fn,
                              int argc)
{
    JS_SetPropertyStr(ctx, global, name, JS_NewCFunction(ctx, fn, name, argc));
}

void yetty_ylexbor_js_web_install(struct yetty_ylexbor *r)
{
    JSContext *ctx = (JSContext *)r->js_ctx;
    if (!ctx) {
        return;
    }

    JSValue global = JS_GetGlobalObject(ctx);
    install_global_fn(ctx, global, "setTimeout", js_setTimeout, 2);
    install_global_fn(ctx, global, "setInterval", js_setInterval, 2);
    install_global_fn(ctx, global, "clearTimeout", js_clearTimer, 1);
    install_global_fn(ctx, global, "clearInterval", js_clearTimer, 1);
    install_global_fn(ctx, global, "queueMicrotask", js_queueMicrotask, 1);
    install_global_fn(ctx, global, "requestAnimationFrame", js_requestAnimationFrame, 1);
    install_global_fn(ctx, global, "cancelAnimationFrame", js_clearTimer, 1);
    install_global_fn(ctx, global, "fetch", js_fetch, 1);
    install_global_fn(ctx, global, "matchMedia", js_matchMedia, 1);
    install_global_fn(ctx, global, "getComputedStyle", js_getComputedStyle, 1);

    /* navigator */
    JSValue nav = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, nav, "userAgent",
                      JS_NewString(ctx, "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
                                        "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"));
    JS_SetPropertyStr(ctx, nav, "appName", JS_NewString(ctx, "Netscape"));
    JS_SetPropertyStr(ctx, nav, "appVersion", JS_NewString(ctx, "5.0"));
    JS_SetPropertyStr(ctx, nav, "platform", JS_NewString(ctx, "Linux x86_64"));
    JS_SetPropertyStr(ctx, nav, "language", JS_NewString(ctx, "en-US"));
    JSValue langs = JS_NewArray(ctx);
    JS_SetPropertyUint32(ctx, langs, 0, JS_NewString(ctx, "en-US"));
    JS_SetPropertyUint32(ctx, langs, 1, JS_NewString(ctx, "en"));
    JS_SetPropertyStr(ctx, nav, "languages", langs);
    JS_SetPropertyStr(ctx, nav, "cookieEnabled", JS_TRUE);
    JS_SetPropertyStr(ctx, nav, "onLine", JS_TRUE);
    JS_SetPropertyStr(ctx, nav, "hardwareConcurrency", JS_NewInt32(ctx, 4));
    JS_SetPropertyStr(ctx, nav, "maxTouchPoints", JS_NewInt32(ctx, 0));
    JS_SetPropertyStr(ctx, nav, "vendor", JS_NewString(ctx, ""));
    JS_SetPropertyStr(ctx, nav, "product", JS_NewString(ctx, "Gecko"));
    JS_SetPropertyStr(ctx, nav, "doNotTrack", JS_NULL);

    /* navigator.serviceWorker — register returns a never-resolving Promise
	 * to keep SPA boot paths happy without actually registering a worker. */
    const char *swdef = "({ register: () => new Promise(()=>{}), "
                        "   ready: new Promise(()=>{}), "
                        "   getRegistrations: () => Promise.resolve([]), "
                        "   addEventListener: ()=>{} })";
    JSValue sw = JS_Eval(ctx, swdef, strlen(swdef), "<sw>", JS_EVAL_TYPE_GLOBAL);
    JS_SetPropertyStr(ctx, nav, "serviceWorker", sw);
    JS_SetPropertyStr(ctx, global, "navigator", nav);

    /* location — populated from base_url. We use a plain object; full
	 * Location semantics (assign/replace/reload) can come later. */
    JSValue loc = JS_NewObject(ctx);
    const char *href = r->base_url ? r->base_url : "about:blank";
    JS_SetPropertyStr(ctx, loc, "href", JS_NewString(ctx, href));
    /* Crude parse — good enough for feature detection. */
    const char *p = strstr(href, "://");
    if (p) {
        size_t plen = (size_t)(p - href);
        JS_SetPropertyStr(ctx, loc, "protocol", JS_NewStringLen(ctx, href, plen + 1));
        const char *host_start = p + 3;
        const char *path_start = strchr(host_start, '/');
        const char *q_start = strchr(host_start, '?');
        const char *h_start = strchr(host_start, '#');
        size_t host_len = path_start ? (size_t)(path_start - host_start) : strlen(host_start);
        JSValue host_val = JS_NewStringLen(ctx, host_start, host_len);
        JS_SetPropertyStr(ctx, loc, "host", JS_DupValue(ctx, host_val));
        JS_SetPropertyStr(ctx, loc, "hostname", JS_DupValue(ctx, host_val));
        JS_FreeValue(ctx, host_val);
        size_t origin_len = (size_t)(host_start + host_len - href);
        JS_SetPropertyStr(ctx, loc, "origin", JS_NewStringLen(ctx, href, origin_len));
        JS_SetPropertyStr(
            ctx, loc, "pathname",
            path_start ? JS_NewString(ctx, q_start ? (path_start[0] == '/' ? path_start : "/")
                                                   : path_start)
                       : JS_NewString(ctx, "/"));
        JS_SetPropertyStr(ctx, loc, "search",
                          q_start ? JS_NewString(ctx, q_start) : JS_NewString(ctx, ""));
        JS_SetPropertyStr(ctx, loc, "hash",
                          h_start ? JS_NewString(ctx, h_start) : JS_NewString(ctx, ""));
        JS_SetPropertyStr(ctx, loc, "port", JS_NewString(ctx, ""));
    } else {
        JS_SetPropertyStr(ctx, loc, "protocol", JS_NewString(ctx, "about:"));
        JS_SetPropertyStr(ctx, loc, "host", JS_NewString(ctx, ""));
        JS_SetPropertyStr(ctx, loc, "hostname", JS_NewString(ctx, ""));
        JS_SetPropertyStr(ctx, loc, "origin", JS_NewString(ctx, ""));
        JS_SetPropertyStr(ctx, loc, "pathname", JS_NewString(ctx, ""));
        JS_SetPropertyStr(ctx, loc, "search", JS_NewString(ctx, ""));
        JS_SetPropertyStr(ctx, loc, "hash", JS_NewString(ctx, ""));
        JS_SetPropertyStr(ctx, loc, "port", JS_NewString(ctx, ""));
    }
    const char *locmethods = "l => { l.assign = () => {}; l.replace = () => {}; "
                             "       l.reload = () => {}; l.toString = () => l.href; "
                             "       return l; }";
    JSValue li = JS_Eval(ctx, locmethods, strlen(locmethods), "<loc>", JS_EVAL_TYPE_GLOBAL);
    if (!JS_IsException(li)) {
        JSValue r2 = JS_Call(ctx, li, JS_UNDEFINED, 1, (JSValueConst[]){loc});
        JS_FreeValue(ctx, r2);
    }
    JS_FreeValue(ctx, li);
    /* Both `window.location` and `document.location` reference the
	 * same Location object — share by ref. */
    JSValue doc_for_loc = JS_GetPropertyStr(ctx, global, "document");
    if (!JS_IsUndefined(doc_for_loc) && !JS_IsNull(doc_for_loc)) {
        JS_SetPropertyStr(ctx, doc_for_loc, "location", JS_DupValue(ctx, loc));
    }
    JS_FreeValue(ctx, doc_for_loc);
    JS_SetPropertyStr(ctx, global, "location", loc);

    /* history — minimal stub. */
    const char *histdef = "({ pushState: ()=>{}, replaceState: ()=>{}, "
                          "   back: ()=>{}, forward: ()=>{}, go: ()=>{}, "
                          "   length: 1, state: null, scrollRestoration: 'auto' })";
    JSValue hist = JS_Eval(ctx, histdef, strlen(histdef), "<hist>", JS_EVAL_TYPE_GLOBAL);
    JS_SetPropertyStr(ctx, global, "history", hist);

    /* Storage instances. */
    static const JSCFunctionListEntry local_funcs[] = {
        JS_CFUNC_DEF("getItem", 1, js_local_getItem),
        JS_CFUNC_DEF("setItem", 2, js_local_setItem),
        JS_CFUNC_DEF("removeItem", 1, js_local_removeItem),
        JS_CFUNC_DEF("clear", 0, js_local_clear),
        JS_CFUNC_DEF("key", 1, js_local_key),
    };
    static const JSCFunctionListEntry session_funcs[] = {
        JS_CFUNC_DEF("getItem", 1, js_session_getItem),
        JS_CFUNC_DEF("setItem", 2, js_session_setItem),
        JS_CFUNC_DEF("removeItem", 1, js_session_removeItem),
        JS_CFUNC_DEF("clear", 0, js_session_clear),
        JS_CFUNC_DEF("key", 1, js_session_key),
    };
    JSValue ls = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, ls, local_funcs, sizeof(local_funcs) / sizeof(local_funcs[0]));
    JS_SetPropertyStr(ctx, global, "localStorage", ls);
    JSValue ss = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, ss, session_funcs,
                               sizeof(session_funcs) / sizeof(session_funcs[0]));
    JS_SetPropertyStr(ctx, global, "sessionStorage", ss);

    /* document.cookie accessor. */
    JSValue doc = JS_GetPropertyStr(ctx, global, "document");
    if (!JS_IsUndefined(doc) && !JS_IsNull(doc)) {
        JSAtom atom = JS_NewAtom(ctx, "cookie");
        JSValue getter = JS_NewCFunction2(ctx, (JSCFunction *)js_doc_cookie_get, "get cookie", 0,
                                          JS_CFUNC_getter, 0);
        JSValue setter = JS_NewCFunction2(ctx, (JSCFunction *)js_doc_cookie_set, "set cookie", 1,
                                          JS_CFUNC_setter, 0);
        JS_DefinePropertyGetSet(ctx, doc, atom, getter, setter, JS_PROP_CONFIGURABLE);
        JS_FreeAtom(ctx, atom);
        /* Also: document.readyState */
        JS_SetPropertyStr(ctx, doc, "readyState", JS_NewString(ctx, "complete"));
        /* document.URL / location */
        JS_SetPropertyStr(ctx, doc, "URL", JS_NewString(ctx, href));
        JS_SetPropertyStr(ctx, doc, "documentURI", JS_NewString(ctx, href));
        JS_SetPropertyStr(ctx, doc, "title", JS_NewString(ctx, ""));
        /* document.domain — host portion of base_url. github's
		 * cookie helpers throw "Unable to get document domain"
		 * when null, so empty string is acceptable but a real
		 * value is closer to spec. */
        const char *p2 = strstr(href, "://");
        if (p2) {
            const char *host = p2 + 3;
            const char *path = strchr(host, '/');
            size_t hlen = path ? (size_t)(path - host) : strlen(host);
            JS_SetPropertyStr(ctx, doc, "domain", JS_NewStringLen(ctx, host, hlen));
        } else {
            JS_SetPropertyStr(ctx, doc, "domain", JS_NewString(ctx, "localhost"));
        }
        /* document.referrer / lastModified / characterSet etc.
		 * — empty strings are fine for feature detection. */
        JS_SetPropertyStr(ctx, doc, "referrer", JS_NewString(ctx, ""));
        JS_SetPropertyStr(ctx, doc, "lastModified", JS_NewString(ctx, ""));
        JS_SetPropertyStr(ctx, doc, "characterSet", JS_NewString(ctx, "UTF-8"));
        JS_SetPropertyStr(ctx, doc, "charset", JS_NewString(ctx, "UTF-8"));
        JS_SetPropertyStr(ctx, doc, "contentType", JS_NewString(ctx, "text/html"));
        JS_SetPropertyStr(ctx, doc, "compatMode", JS_NewString(ctx, "CSS1Compat"));
        JS_SetPropertyStr(ctx, doc, "hidden", JS_FALSE);
        JS_SetPropertyStr(ctx, doc, "visibilityState", JS_NewString(ctx, "visible"));
        JS_SetPropertyStr(ctx, doc, "designMode", JS_NewString(ctx, "off"));
        JS_SetPropertyStr(ctx, doc, "dir", JS_NewString(ctx, "ltr"));
    }
    JS_FreeValue(ctx, doc);

    /* crypto */
    JSValue cry = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, cry, "getRandomValues",
                      JS_NewCFunction(ctx, js_crypto_getRandomValues, "getRandomValues", 1));
    JS_SetPropertyStr(ctx, cry, "randomUUID",
                      JS_NewCFunction(ctx, js_crypto_randomUUID, "randomUUID", 0));
    /* subtle — present, every method returns a never-resolving Promise.
	 * Enough to pass `if (crypto.subtle)` checks; calls block forever. */
    const char *subtle =
        "({ digest: ()=>new Promise(()=>{}), "
        "   importKey: ()=>new Promise(()=>{}), "
        "   exportKey: ()=>new Promise(()=>{}), "
        "   generateKey: ()=>new Promise(()=>{}), "
        "   sign: ()=>new Promise(()=>{}), verify: ()=>new Promise(()=>{}), "
        "   encrypt: ()=>new Promise(()=>{}), decrypt: ()=>new Promise(()=>{}), "
        "   deriveKey: ()=>new Promise(()=>{}), deriveBits: ()=>new Promise(()=>{}), "
        "   wrapKey: ()=>new Promise(()=>{}), unwrapKey: ()=>new Promise(()=>{}) })";
    JSValue sub = JS_Eval(ctx, subtle, strlen(subtle), "<subtle>", JS_EVAL_TYPE_GLOBAL);
    JS_SetPropertyStr(ctx, cry, "subtle", sub);
    JS_SetPropertyStr(ctx, global, "crypto", cry);

    /* Misc stubs — feature detection rarely actually USES these,
	 * just checks they exist. */
    const char *stubs =
        "globalThis.Worker          = function(){ this.postMessage = ()=>{}; "
        "this.terminate=()=>{}; this.addEventListener=()=>{}; };"
        "globalThis.SharedWorker    = function(){ this.port = { postMessage:()=>{}, "
        "addEventListener:()=>{} }; };"
        "globalThis.BroadcastChannel= function(){ this.postMessage = ()=>{}; this.close=()=>{}; "
        "this.addEventListener=()=>{}; };"
        "globalThis.AbortController = function(){ this.signal = { aborted:false, "
        "addEventListener:()=>{}, removeEventListener:()=>{} }; this.abort = ()=>{ "
        "this.signal.aborted=true; }; };"
        "globalThis.AbortSignal     = { abort: ()=>({ aborted:true }), timeout: ()=>({ "
        "aborted:false, addEventListener:()=>{} }) };"
        "globalThis.indexedDB       = { open: ()=>({ onsuccess:null, onerror:null, "
        "addEventListener:()=>{} }), deleteDatabase: ()=>({}) };"
        "globalThis.btoa = s => { let b=''; for (let i=0;i<s.length;i++) "
        "b+=String.fromCharCode(s.charCodeAt(i)&0xff); /* placeholder — real impl below */ "
        "  const m='ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/'; let out='', "
        "i=0;"
        "  while (i<b.length){ const a=b.charCodeAt(i++)|0, c=b.charCodeAt(i++)|0, "
        "d=b.charCodeAt(i++)|0;"
        "    const t=(a<<16)|(c<<8)|d;"
        "    out += m[(t>>18)&63]+m[(t>>12)&63]+ (i-1>b.length?'=':m[(t>>6)&63]) + "
        "(i>b.length?'=':m[t&63]); }"
        "  return out; };"
        "globalThis.atob = s => { const "
        "m='ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';"
        "  s = s.replace(/=+$/,''); let out=''; for (let i=0; i<s.length;){ const "
        "a=m.indexOf(s[i++]), b=m.indexOf(s[i++]),"
        "    c=m.indexOf(s[i++]), d=m.indexOf(s[i++]); const "
        "t=(a<<18)|(b<<12)|((c<0?0:c)<<6)|(d<0?0:d);"
        "    out += String.fromCharCode((t>>16)&0xff); if (c>=0) out += "
        "String.fromCharCode((t>>8)&0xff); if (d>=0) out += String.fromCharCode(t&0xff); } return "
        "out; };"
        "globalThis.structuredClone = v => JSON.parse(JSON.stringify(v));"
        /* Conformant-enough URL + URLSearchParams. A non-parsing stub here made
         * Wikipedia's web2017-polyfills detect the native URL as broken, install
         * its own wrapper over our stub, then throw "not a function" inside its
         * tidy_instance() when our href getter returned a non-string. Passing the
         * polyfill's feature gate (search setter recomputes a normalized href)
         * makes it skip installation and use ours directly. Validated standalone. */
        "(function (G) {\n"
        "var SPECIAL = { 'http:': 80, 'https:': 443, 'ws:': 80, 'wss:': 443, 'ftp:': 21 };\n"
        "// Built from a backslash-free string so it embeds as a plain C string literal\n"
        "// with no escaping. '/' is not special in a regex, so it stays unescaped; we\n"
        "// use [0-9] for digits and [?] for a literal '?'.\n"
        "var URL_RE = new "
        "RegExp('^([a-zA-Z][a-zA-Z0-9+.-]*:)?(//(?:([^:@/?#]*)(?::([^@/?#]*))?@)?([^:/"
        "?#]*)(?::([0-9]*))?)?([^?#]*)([?][^#]*)?(#.*)?$');\n"
        "function removeDotSegments(path) {\n"
        "if (!path || path.indexOf('.') < 0) return path;\n"
        "var input = path.split('/');\n"
        "var out = [];\n"
        "for (var i = 0; i < input.length; i++) {\n"
        "var seg = input[i];\n"
        "if (seg === '.') { if (i === input.length - 1) out.push(''); continue; }\n"
        "if (seg === '..') {\n"
        "if (out.length > 1) out.pop();\n"
        "if (i === input.length - 1) out.push('');\n"
        "continue;\n"
        "}\n"
        "out.push(seg);\n"
        "}\n"
        "return out.join('/');\n"
        "}\n"
        "G.URLSearchParams = function (init) {\n"
        "var pairs = [];\n"
        "var self = this;\n"
        "this._url = null;\n"
        "function setFromString(s) {\n"
        "pairs.length = 0;\n"
        "s = String(s == null ? '' : s);\n"
        "if (s.charAt(0) === '?') s = s.slice(1);\n"
        "if (!s) return;\n"
        "s.split('&').forEach(function (p) {\n"
        "if (!p) return;\n"
        "var i = p.indexOf('=');\n"
        "var k = i < 0 ? p : p.slice(0, i);\n"
        "var v = i < 0 ? '' : p.slice(i + 1);\n"
        "pairs.push([\n"
        "decodeURIComponent(k.split('+').join(' ')),\n"
        "decodeURIComponent(v.split('+').join(' '))\n"
        "]);\n"
        "});\n"
        "}\n"
        "this._setFromString = setFromString;\n"
        "if (typeof init === 'string') {\n"
        "setFromString(init);\n"
        "} else if (init && typeof init === 'object') {\n"
        "if (Array.isArray(init)) {\n"
        "init.forEach(function (e) { pairs.push([String(e[0]), String(e[1])]); });\n"
        "} else if (typeof init.forEach === 'function') {\n"
        "init.forEach(function (v, k) { pairs.push([String(k), String(v)]); });\n"
        "} else {\n"
        "for (var k in init) {\n"
        "if (Object.prototype.hasOwnProperty.call(init, k)) pairs.push([k, String(init[k])]);\n"
        "}\n"
        "}\n"
        "}\n"
        "function sync() { if (self._url) self._url._setSearchFromParams(self.toString()); }\n"
        "this.get = function (k) { for (var i = 0; i < pairs.length; i++) if (pairs[i][0] === k) "
        "return pairs[i][1]; return null; };\n"
        "this.getAll = function (k) { var r = []; for (var i = 0; i < pairs.length; i++) if "
        "(pairs[i][0] === k) r.push(pairs[i][1]); return r; };\n"
        "this.has = function (k) { for (var i = 0; i < pairs.length; i++) if (pairs[i][0] === k) "
        "return true; return false; };\n"
        "this.set = function (k, v) {\n"
        "v = String(v); var done = false;\n"
        "for (var i = 0; i < pairs.length; i++) {\n"
        "if (pairs[i][0] === k) { if (!done) { pairs[i][1] = v; done = true; } else { "
        "pairs.splice(i, 1); i--; } }\n"
        "}\n"
        "if (!done) pairs.push([k, v]); sync();\n"
        "};\n"
        "this.append = function (k, v) { pairs.push([k, String(v)]); sync(); };\n"
        "this.delete = function (k) { for (var i = 0; i < pairs.length; i++) { if (pairs[i][0] === "
        "k) { pairs.splice(i, 1); i--; } } sync(); };\n"
        "this.forEach = function (fn, thisArg) { for (var i = 0; i < pairs.length; i++) "
        "fn.call(thisArg, pairs[i][1], pairs[i][0], self); };\n"
        "this.keys = function* () { for (var i = 0; i < pairs.length; i++) yield pairs[i][0]; };\n"
        "this.values = function* () { for (var i = 0; i < pairs.length; i++) yield pairs[i][1]; "
        "};\n"
        "this.entries = function* () { for (var i = 0; i < pairs.length; i++) yield [pairs[i][0], "
        "pairs[i][1]]; };\n"
        "this[Symbol.iterator] = this.entries;\n"
        "this.sort = function () { pairs.sort(function (a, b) { return a[0] < b[0] ? -1 : a[0] > "
        "b[0] ? 1 : 0; }); sync(); };\n"
        "this.toString = function () {\n"
        "return pairs.map(function (p) {\n"
        "return encodeURIComponent(p[0]) + '=' + encodeURIComponent(p[1]);\n"
        "}).join('&');\n"
        "};\n"
        "};\n"
        "G.URL = function (url, base) {\n"
        "var self = this;\n"
        "var P = { protocol: '', username: '', password: '', hostname: '', port: '', pathname: '', "
        "search: '', hash: '', authority: false };\n"
        "var spObj = null;\n"
        "function isSpecial() { return Object.prototype.hasOwnProperty.call(SPECIAL, P.protocol); "
        "}\n"
        "function applyMatch(m, intoPath) {\n"
        "P.protocol = (m[1] || '').toLowerCase();\n"
        "P.authority = m[2] != null;\n"
        "P.username = m[3] || '';\n"
        "P.password = m[4] || '';\n"
        "P.hostname = (m[5] || '').toLowerCase();\n"
        "P.port = m[6] || '';\n"
        "if (intoPath !== false) {\n"
        "P.pathname = m[7] || '';\n"
        "P.search = m[8] || '';\n"
        "P.hash = m[9] || '';\n"
        "}\n"
        "}\n"
        "function normalize() {\n"
        "if (P.authority && P.pathname === '' && isSpecial()) P.pathname = '/';\n"
        "if (P.authority) P.pathname = removeDotSegments(P.pathname);\n"
        "if (P.port && SPECIAL[P.protocol] === parseInt(P.port, 10)) P.port = '';\n"
        "}\n"
        "function parseAbsolute(input) {\n"
        "input = String(input).trim();\n"
        "var m = URL_RE.exec(input);\n"
        "if (!m || !m[1]) throw new TypeError('Invalid URL: ' + input);\n"
        "applyMatch(m, true);\n"
        "normalize();\n"
        "}\n"
        "function parseWithBase(input, baseStr) {\n"
        "input = String(input).trim();\n"
        "var m = URL_RE.exec(input);\n"
        "if (m && m[1]) { applyMatch(m, true); normalize(); return; }\n"
        "var b = new G.URL(baseStr);\n"
        "P.protocol = b.protocol;\n"
        "P.authority = true;\n"
        "P.username = ''; P.password = '';\n"
        "P.hostname = b.hostname; P.port = b.port;\n"
        "if (input === '') {\n"
        "P.pathname = b.pathname; P.search = b.search; P.hash = b.hash;\n"
        "} else if (input.slice(0, 2) === '//') {\n"
        "var ma = URL_RE.exec(P.protocol + input); applyMatch(ma, true); normalize(); return;\n"
        "} else if (input[0] === '/') {\n"
        "var ms = URL_RE.exec(input); P.pathname = ms[7] || '/'; P.search = ms[8] || ''; P.hash = "
        "ms[9] || '';\n"
        "} else if (input[0] === '?') {\n"
        "P.pathname = b.pathname; P.search = input; P.hash = '';\n"
        "} else if (input[0] === '#') {\n"
        "P.pathname = b.pathname; P.search = b.search; P.hash = input;\n"
        "} else {\n"
        "var basedir = b.pathname.slice(0, b.pathname.lastIndexOf('/') + 1) || '/';\n"
        "var mr = URL_RE.exec(input);\n"
        "P.pathname = removeDotSegments(basedir + (mr[7] || '')); P.search = mr[8] || ''; P.hash = "
        "mr[9] || '';\n"
        "}\n"
        "normalize();\n"
        "}\n"
        "function recompose() {\n"
        "var s = '';\n"
        "if (P.protocol) s += P.protocol;\n"
        "if (P.authority || P.hostname !== '') {\n"
        "s += '//';\n"
        "if (P.username) { s += P.username; if (P.password) s += ':' + P.password; s += '@'; }\n"
        "s += P.hostname;\n"
        "if (P.port) s += ':' + P.port;\n"
        "}\n"
        "s += P.pathname; s += P.search; s += P.hash;\n"
        "return s;\n"
        "}\n"
        "if (base !== undefined && base !== null && base !== '') parseWithBase(url, base);\n"
        "else parseAbsolute(url);\n"
        "this._setSearchFromParams = function (str) { P.search = str ? '?' + str : ''; };\n"
        "Object.defineProperties(this, {\n"
        "href: {\n"
        "get: function () { return recompose(); },\n"
        "set: function (v) { parseAbsolute(v); spObj = null; }, enumerable: true, configurable: "
        "true\n"
        "},\n"
        "protocol: {\n"
        "get: function () { return P.protocol; },\n"
        "set: function (v) { v = String(v); if (v && v.slice(-1) !== ':') v += ':'; P.protocol = "
        "v.toLowerCase(); }, enumerable: true, configurable: true\n"
        "},\n"
        "username: { get: function () { return P.username; }, set: function (v) { P.username = "
        "String(v); }, enumerable: true, configurable: true },\n"
        "password: { get: function () { return P.password; }, set: function (v) { P.password = "
        "String(v); }, enumerable: true, configurable: true },\n"
        "host: {\n"
        "get: function () { return P.hostname + (P.port ? ':' + P.port : ''); },\n"
        "set: function (v) { var mm = URL_RE.exec('//' + v); P.hostname = (mm[5] || "
        "'').toLowerCase(); P.port = mm[6] || ''; P.authority = true; }, enumerable: true, "
        "configurable: true\n"
        "},\n"
        "hostname: { get: function () { return P.hostname; }, set: function (v) { P.hostname = "
        "String(v).toLowerCase(); P.authority = true; }, enumerable: true, configurable: true },\n"
        "port: { get: function () { return P.port; }, set: function (v) { P.port = String(v); }, "
        "enumerable: true, configurable: true },\n"
        "pathname: {\n"
        "get: function () { return P.pathname; },\n"
        "set: function (v) { v = String(v); if (v && v[0] !== '/' && P.authority) v = '/' + v; "
        "P.pathname = v; }, enumerable: true, configurable: true\n"
        "},\n"
        "search: {\n"
        "get: function () { return P.search; },\n"
        "set: function (v) { v = String(v); if (v && v[0] !== '?') v = '?' + v; if (v === '?') v = "
        "''; P.search = v; if (spObj) spObj._setFromString(v); }, enumerable: true, configurable: "
        "true\n"
        "},\n"
        "hash: {\n"
        "get: function () { return P.hash; },\n"
        "set: function (v) { v = String(v); if (v && v[0] !== '#') v = '#' + v; if (v === '#') v = "
        "''; P.hash = v; }, enumerable: true, configurable: true\n"
        "},\n"
        "origin: {\n"
        "get: function () { if (isSpecial() && P.hostname) return P.protocol + '//' + P.hostname + "
        "(P.port ? ':' + P.port : ''); return 'null'; }, enumerable: true, configurable: true\n"
        "},\n"
        "searchParams: {\n"
        "get: function () { if (!spObj) { spObj = new G.URLSearchParams(P.search); spObj._url = "
        "self; } return spObj; }, enumerable: true, configurable: true\n"
        "}\n"
        "});\n"
        "this.toString = function () { return recompose(); };\n"
        "this.toJSON = function () { return recompose(); };\n"
        "};\n"
        "})(globalThis);\n"
        "globalThis.Event = function(t, init){ this.type=t; this.bubbles=!!(init&&init.bubbles); "
        "this.cancelable=!!(init&&init.cancelable); this.defaultPrevented=false; "
        "this.preventDefault=()=>{this.defaultPrevented=true;}; this.stopPropagation=()=>{}; "
        "this.stopImmediatePropagation=()=>{}; };"
        "globalThis.CustomEvent = function(t, init){ globalThis.Event.call(this,t,init); "
        "this.detail = init? init.detail : null; };"
        "globalThis.MessageEvent = function(t, init){ globalThis.Event.call(this,t,init); "
        "this.data = init?init.data:null; this.origin = init?init.origin||'':''; };"
        "globalThis.MutationObserver = function(cb){ this.observe=()=>{}; this.disconnect=()=>{}; "
        "this.takeRecords=()=>[]; };"
        "globalThis.IntersectionObserver = function(cb){ this.observe=()=>{}; "
        "this.unobserve=()=>{}; this.disconnect=()=>{}; this.takeRecords=()=>[]; };"
        "globalThis.ResizeObserver = function(cb){ this.observe=()=>{}; this.unobserve=()=>{}; "
        "this.disconnect=()=>{}; };"
        "globalThis.PerformanceObserver = function(cb){ this.observe=()=>{}; "
        "this.disconnect=()=>{}; this.takeRecords=()=>[]; };"
        "globalThis.performance = { now: () => Date.now(), mark: ()=>{}, measure: ()=>{}, "
        "getEntries: ()=>[], getEntriesByType: ()=>[], getEntriesByName: ()=>[], clearMarks: "
        "()=>{}, clearMeasures: ()=>{}, timing: {}, navigation: { type:0, redirectCount:0 } };"
        "globalThis.scrollTo = ()=>{}; globalThis.scrollBy = ()=>{};"
        "globalThis.alert = m => console.log('[alert]', m);"
        "globalThis.confirm = () => false;"
        "globalThis.prompt = () => null;"
        "globalThis.devicePixelRatio = 1;"
        "globalThis.innerWidth  = "
        "1024;"
        "globalThis.innerHeight = "
        "768;"
        "globalThis.outerWidth  = 1024; globalThis.outerHeight = 768;"
        "globalThis.screen      = { width:1024, height:768, availWidth:1024, availHeight:768, "
        "colorDepth:24, pixelDepth:24 };"
        /* EventTarget — base class many libs `class X extends
		 * EventTarget` against. Mirrors addEventListener etc. */
        "globalThis.EventTarget = function(){"
        "  this._lst = {};"
        "  this.addEventListener = (t, fn) => { (this._lst[t] = this._lst[t] || []).push(fn); };"
        "  this.removeEventListener = (t, fn) => { const a = this._lst[t]; if (!a) return; const i "
        "= a.indexOf(fn); if (i >= 0) a.splice(i, 1); };"
        "  this.dispatchEvent = (e) => { const a = this._lst[e && e.type]; if (!a) return true; "
        "for (const fn of a.slice()) try { fn.call(this, e); } catch(_) {} return !(e && "
        "e.defaultPrevented); };"
        "};"
        /* HTMLElement base for `class X extends HTMLElement`. During a custom-
		 * element upgrade the registry pushes the element being upgraded onto
		 * __ceStack; super() returns it so the subclass constructor (and its
		 * private #fields) brand the EXISTING element — skipping the
		 * constructor is what threw "invalid brand on object". A bare
		 * `new X()` (empty stack) makes a detached <div>. */
        "globalThis.__ceStack = [];"
        "globalThis.HTMLElement = function(){"
        "  if(globalThis.__ceStack.length) return globalThis.__ceStack.pop();"
        "  try{ return document.createElement('div'); }catch(e){ return this; } };"
        /* No-op custom-element lifecycle callbacks on the base prototype. Web
		 * components routinely call `super.connectedCallback()` (babel emits
		 * `_get(_getPrototypeOf(C.prototype),'connectedCallback',this).call(this)`);
		 * with no base callback that resolves to `undefined.call` →
		 * "cannot read property 'call' of undefined" (github's <tool-tip>). */
        "globalThis.HTMLElement.prototype.connectedCallback = function(){};"
        "globalThis.HTMLElement.prototype.disconnectedCallback = function(){};"
        "globalThis.HTMLElement.prototype.adoptedCallback = function(){};"
        "globalThis.HTMLElement.prototype.attributeChangedCallback = function(){};"
        "globalThis.Element     = function(){};"
        "globalThis.Node        = function(){};"
        "globalThis.Document    = function(){};"
        "globalThis.HTMLDocument= function(){};"
        "globalThis.HTMLAnchorElement = function(){};"
        "globalThis.HTMLImageElement  = function(){};"
        "globalThis.HTMLInputElement  = function(){};"
        "globalThis.HTMLFormElement   = function(){};"
        "globalThis.HTMLButtonElement = function(){};"
        "globalThis.HTMLDivElement    = function(){};"
        "globalThis.HTMLSpanElement   = function(){};"
        "globalThis.HTMLBodyElement   = function(){};"
        "globalThis.HTMLHtmlElement   = function(){};"
        "globalThis.HTMLHeadElement   = function(){};"
        "globalThis.HTMLMetaElement   = function(){};"
        "globalThis.HTMLLinkElement   = function(){};"
        "globalThis.HTMLScriptElement = function(){};"
        "globalThis.HTMLStyleElement  = function(){};"
        "globalThis.HTMLTemplateElement= function(){};"
        "globalThis.HTMLIFrameElement = function(){};"
        "globalThis.HTMLTableElement  = function(){};"
        "globalThis.HTMLTableRowElement = function(){};"
        "globalThis.HTMLTableCellElement= function(){};"
        "globalThis.HTMLSelectElement = function(){};"
        "globalThis.HTMLOptionElement = function(){};"
        "globalThis.HTMLOptGroupElement = function(){};"
        "globalThis.HTMLTextAreaElement = function(){};"
        "globalThis.HTMLLabelElement  = function(){};"
        "globalThis.HTMLLegendElement = function(){};"
        "globalThis.HTMLFieldSetElement = function(){};"
        "globalThis.HTMLDataListElement = function(){};"
        "globalThis.HTMLOutputElement = function(){};"
        "globalThis.HTMLProgressElement = function(){};"
        "globalThis.HTMLMeterElement  = function(){};"
        "globalThis.HTMLDetailsElement= function(){};"
        "globalThis.HTMLDialogElement = function(){};"
        "globalThis.HTMLPictureElement= function(){};"
        "globalThis.HTMLSourceElement = function(){};"
        "globalThis.HTMLVideoElement  = function(){};"
        "globalThis.HTMLAudioElement  = function(){};"
        "globalThis.HTMLMediaElement  = function(){};"
        "globalThis.HTMLTrackElement  = function(){};"
        "globalThis.HTMLEmbedElement  = function(){};"
        "globalThis.HTMLObjectElement = function(){};"
        "globalThis.HTMLParamElement  = function(){};"
        "globalThis.HTMLOptionsCollection= function(){};"
        "globalThis.HTMLAreaElement   = function(){};"
        "globalThis.HTMLMapElement    = function(){};"
        "globalThis.HTMLMenuElement   = function(){};"
        "globalThis.HTMLOListElement  = function(){};"
        "globalThis.HTMLUListElement  = function(){};"
        "globalThis.HTMLLIElement     = function(){};"
        "globalThis.HTMLDListElement  = function(){};"
        "globalThis.HTMLHRElement     = function(){};"
        "globalThis.HTMLBRElement     = function(){};"
        "globalThis.HTMLPreElement    = function(){};"
        "globalThis.HTMLQuoteElement  = function(){};"
        "globalThis.HTMLHeadingElement= function(){};"
        "globalThis.HTMLParagraphElement= function(){};"
        "globalThis.HTMLTableSectionElement= function(){};"
        "globalThis.HTMLTableColElement= function(){};"
        "globalThis.HTMLTableCaptionElement= function(){};"
        "globalThis.HTMLTimeElement   = function(){};"
        "globalThis.HTMLDataElement   = function(){};"
        "globalThis.HTMLUnknownElement= function(){};"
        "globalThis.MessageChannel = function(){ const port = { postMessage:()=>{}, "
        "onmessage:null, start:()=>{}, close:()=>{}, addEventListener:()=>{}, "
        "removeEventListener:()=>{} }; this.port1=port; this.port2=port; };"
        "globalThis.MessagePort   = function(){};"
        "globalThis.Worklet       = function(){ this.addModule=()=>Promise.resolve(); };"
        "globalThis.LinkPreloadManager = function(){};"
        "globalThis.NodeIterator  = function(){};"
        "globalThis.TreeWalker    = function(){ this.nextNode=()=>null; "
        "this.previousNode=()=>null; };"
        "globalThis.ProgressEvent = function(t,init){ globalThis.Event.call(this,t,init); "
        "this.lengthComputable=!!(init&&init.lengthComputable); "
        "this.loaded=(init&&init.loaded)||0; this.total=(init&&init.total)||0; };"
        "globalThis.CompositionEvent = function(t,init){ globalThis.Event.call(this,t,init); "
        "this.data=(init&&init.data)||''; };"
        "globalThis.ClipboardEvent = function(t,init){ globalThis.Event.call(this,t,init); "
        "this.clipboardData=null; };"
        "globalThis.DragEvent     = function(t,init){ globalThis.MouseEvent.call(this,t,init); "
        "this.dataTransfer=null; };"
        "globalThis.BeforeUnloadEvent = function(t,init){ globalThis.Event.call(this,t,init); };"
        "globalThis.AnimationFrameProvider = function(){};"
        "globalThis.PaymentRequest = function(){};"
        "globalThis.PushManager   = function(){};"
        "globalThis.Notification  = function(){};"
        "globalThis.Notification.permission = 'default';"
        "globalThis.Notification.requestPermission = ()=>Promise.resolve('default');"
        "globalThis.SVGElement        = function(){};"
        "globalThis.SVGSVGElement     = function(){};"
        "globalThis.MathMLElement     = function(){};"
        "globalThis.ShadowRoot        = function(){};"
        "globalThis.DocumentFragment  = function(){};"
        "globalThis.Text              = function(){};"
        "globalThis.Comment           = function(){};"
        "globalThis.Attr              = function(){};"
        "globalThis.NodeList          = function(){};"
        "globalThis.HTMLCollection    = function(){};"
        "globalThis.DOMTokenList      = function(){};"
        "globalThis.NamedNodeMap      = function(){};"
        "globalThis.CSSStyleDeclaration= function(){};"
        "globalThis.CSSStyleSheet     = function(){};"
        "globalThis.CSSRule           = function(){};"
        "globalThis.MediaQueryList    = function(){};"
        "globalThis.Range             = function(){ this.setStart=()=>{}; this.setEnd=()=>{}; "
        "this.collapse=()=>{}; this.selectNode=()=>{}; this.selectNodeContents=()=>{}; };"
        "globalThis.Selection         = function(){ this.removeAllRanges=()=>{}; "
        "this.addRange=()=>{}; this.toString=()=>''; };"
        /* Constructors that DOM-spec'd handlers reach for via instanceof
		 * checks. Every one we miss bails the boot path with a
		 * ReferenceError. */
        "globalThis.Location          = function(){};"
        "globalThis.History           = function(){};"
        "globalThis.Navigator         = function(){};"
        "globalThis.Screen            = function(){};"
        "globalThis.Storage           = function(){};"
        "globalThis.Window            = function(){};"
        "globalThis.WindowProxy       = function(){};"
        "globalThis.Headers           = function(init){ const m={}; "
        "this.get=k=>m[String(k).toLowerCase()]||null; "
        "this.set=(k,v)=>{m[String(k).toLowerCase()]=v;}; this.has=k=>String(k).toLowerCase() in "
        "m; this.append=this.set; this.delete=k=>{delete m[String(k).toLowerCase()];}; "
        "this.forEach=fn=>{for(const k in m)fn(m[k],k);}; this.entries=function*(){for(const k in "
        "m)yield[k,m[k]];}; this.keys=function*(){for(const k in m)yield k;}; "
        "this.values=function*(){for(const k in m)yield m[k];}; if(init && typeof "
        "init==='object'){for(const k in init){this.set(k,init[k]);}} };"
        "globalThis.Request           = function(input, init){ this.url = typeof "
        "input==='string'?input:input&&input.url||''; this.method=(init&&init.method)||'GET'; "
        "this.headers = new Headers(init&&init.headers); this.body = init&&init.body||null; };"
        "globalThis.Response          = function(body, init){ this.body=body||null; "
        "this.status=(init&&init.status)||200; this.statusText=(init&&init.statusText)||''; "
        "this.headers=new Headers(init&&init.headers); this.ok=this.status>=200&&this.status<300; "
        "this.text=()=>Promise.resolve(typeof this.body==='string'?this.body:''); "
        "this.json=()=>Promise.resolve(JSON.parse(typeof this.body==='string'?this.body:'null')); "
        "this.arrayBuffer=()=>Promise.resolve(new ArrayBuffer(0)); this.clone=()=>this; };"
        "globalThis.Blob              = function(parts, opts){ this.size=0; "
        "this.type=(opts&&opts.type)||''; this.text=()=>Promise.resolve(''); "
        "this.arrayBuffer=()=>Promise.resolve(new ArrayBuffer(0)); this.slice=()=>new "
        "Blob([],opts); };"
        "globalThis.File              = function(parts, name, opts){ "
        "globalThis.Blob.call(this,parts,opts); this.name=name||''; this.lastModified=Date.now(); "
        "};"
        "globalThis.FileList          = function(){ this.length=0; this.item=()=>null; };"
        "globalThis.DataTransfer      = function(){ this.types=[]; this.files=new FileList(); "
        "this.items={length:0}; this.getData=()=>''; this.setData=()=>{}; };"
        "globalThis.DOMException      = function(message, name){ this.message=message||''; "
        "this.name=name||'Error'; };"
        "globalThis.DOMParser         = function(){ this.parseFromString = (s, t) => document; };"
        "globalThis.XPathResult       = function(){};"
        "globalThis.TextEncoder       = function(){ this.encode = s => { const a = new "
        "Uint8Array(s.length); for (let i=0;i<s.length;i++) a[i]=s.charCodeAt(i)&0xff; return a; "
        "}; };"
        "globalThis.TextDecoder       = function(){ this.decode = b => { let o=''; const v = b "
        "instanceof Uint8Array ? b : new Uint8Array(b); for (let i=0;i<v.length;i++) "
        "o+=String.fromCharCode(v[i]); return o; }; };"
        "globalThis.AudioContext      = function(){ this.close=()=>{}; this.createGain=()=>({}); "
        "this.createOscillator=()=>({}); this.destination={}; };"
        "globalThis.webkitAudioContext = globalThis.AudioContext;"
        "globalThis.RTCPeerConnection = function(){ this.close=()=>{}; "
        "this.createOffer=()=>Promise.resolve({}); this.createAnswer=()=>Promise.resolve({}); "
        "this.addEventListener=()=>{}; };"
        "globalThis.MediaQueryListEvent = function(t,init){ globalThis.Event.call(this,t,init); "
        "this.matches=!!(init&&init.matches); this.media=(init&&init.media)||''; };"
        "globalThis.PopStateEvent     = function(t,init){ globalThis.Event.call(this,t,init); "
        "this.state=init?init.state:null; };"
        "globalThis.HashChangeEvent   = function(t,init){ globalThis.Event.call(this,t,init); "
        "this.oldURL=(init&&init.oldURL)||''; this.newURL=(init&&init.newURL)||''; };"
        "globalThis.PageTransitionEvent= function(t,init){ globalThis.Event.call(this,t,init); "
        "this.persisted=!!(init&&init.persisted); };"
        "globalThis.ErrorEvent        = function(t,init){ globalThis.Event.call(this,t,init); "
        "this.message=(init&&init.message)||''; this.filename=(init&&init.filename)||''; "
        "this.lineno=(init&&init.lineno)||0; this.colno=(init&&init.colno)||0; "
        "this.error=(init&&init.error)||null; };"
        "globalThis.PromiseRejectionEvent = function(t,init){ globalThis.Event.call(this,t,init); "
        "this.promise=(init&&init.promise)||null; this.reason=(init&&init.reason)||null; };"
        "globalThis.SecurityPolicyViolationEvent = function(t,init){ "
        "globalThis.Event.call(this,t,init); };"
        "globalThis.UIEvent           = function(t,init){ globalThis.Event.call(this,t,init); };"
        "globalThis.MouseEvent        = function(t,init){ globalThis.Event.call(this,t,init); "
        "this.clientX=(init&&init.clientX)||0; this.clientY=(init&&init.clientY)||0; "
        "this.button=(init&&init.button)||0; };"
        "globalThis.KeyboardEvent     = function(t,init){ globalThis.Event.call(this,t,init); "
        "this.key=(init&&init.key)||''; this.code=(init&&init.code)||''; "
        "this.keyCode=(init&&init.keyCode)||0; };"
        "globalThis.PointerEvent      = function(t,init){ globalThis.MouseEvent.call(this,t,init); "
        "this.pointerId=(init&&init.pointerId)||0; this.pointerType=(init&&init.pointerType)||''; "
        "};"
        "globalThis.TouchEvent        = function(t,init){ globalThis.Event.call(this,t,init); "
        "this.touches=[]; this.targetTouches=[]; this.changedTouches=[]; };"
        "globalThis.WheelEvent        = function(t,init){ globalThis.MouseEvent.call(this,t,init); "
        "this.deltaX=(init&&init.deltaX)||0; this.deltaY=(init&&init.deltaY)||0; "
        "this.deltaZ=(init&&init.deltaZ)||0; };"
        "globalThis.FocusEvent        = function(t,init){ globalThis.Event.call(this,t,init); "
        "this.relatedTarget=(init&&init.relatedTarget)||null; };"
        "globalThis.InputEvent        = function(t,init){ globalThis.Event.call(this,t,init); "
        "this.data=(init&&init.data)||null; this.inputType=(init&&init.inputType)||''; };"
        "globalThis.SubmitEvent       = function(t,init){ globalThis.Event.call(this,t,init); "
        "this.submitter=(init&&init.submitter)||null; };"
        "globalThis.AnimationEvent    = function(t,init){ globalThis.Event.call(this,t,init); "
        "this.animationName=(init&&init.animationName)||''; };"
        "globalThis.TransitionEvent   = function(t,init){ globalThis.Event.call(this,t,init); "
        "this.propertyName=(init&&init.propertyName)||''; };"
        "globalThis.GamepadEvent      = function(t,init){ globalThis.Event.call(this,t,init); "
        "this.gamepad=null; };"
        "globalThis.StorageEvent      = function(t,init){ globalThis.Event.call(this,t,init); "
        "this.key=(init&&init.key)||''; this.oldValue=(init&&init.oldValue)||null; "
        "this.newValue=(init&&init.newValue)||null; };"
        "globalThis.MediaStream       = function(){ this.getTracks=()=>[]; this.addTrack=()=>{}; "
        "this.removeTrack=()=>{}; };"
        "globalThis.MediaStreamTrack  = function(){};"
        "globalThis.IntersectionObserverEntry = function(){};"
        "globalThis.ResizeObserverEntry = function(){};"
        "globalThis.PerformanceEntry  = function(){};"
        "globalThis.PerformanceMark   = function(){};"
        "globalThis.PerformanceMeasure= function(){};"
        "globalThis.PerformanceNavigationTiming = function(){};"
        "globalThis.PerformanceResourceTiming   = function(){};"
        /* Custom Elements with a real upgrade reaction: define() splices each
		 * matching element's prototype onto the class and fires
		 * connectedCallback — that is what boots web-component-based UIs (e.g.
		 * GitHub's <react-app>). HTMLElement.prototype is lazily chained to the
		 * native element prototype so `class X extends HTMLElement` inherits
		 * appendChild/querySelector/etc.; the element's opaque DOM pointer is
		 * keyed by class id (unchanged by setPrototypeOf), so native methods
		 * keep resolving after the upgrade. */
        "globalThis.CustomElementRegistry = function(){"
        "  const m={}, defers={}; let chained=false;"
        "  const chain=()=>{ if(chained)return; chained=true; try{"
        "    var ep=Object.getPrototypeOf(document.createElement('div'));"
        "    if(ep && globalThis.HTMLElement && globalThis.HTMLElement.prototype)"
        "      Object.setPrototypeOf(globalThis.HTMLElement.prototype, ep);"
        "  }catch(e){} };"
        "  const upgrade=(el,c)=>{ try{ if(el.__ceUpgraded)return; el.__ceUpgraded=true;"
        "    Object.setPrototypeOf(el,c.prototype);"
        /* Run the real constructor with `el` as `this` (brands private fields)
		 * via the construction stack; super() returns `el`. */
        "    globalThis.__ceStack.push(el);"
        "    try{ Reflect.construct(c,[],c); }"
        "    finally{ if(globalThis.__ceStack[globalThis.__ceStack.length-1]===el)"
        "             globalThis.__ceStack.pop(); }"
        "    if(typeof el.connectedCallback==='function') el.connectedCallback();"
        "  }catch(e){ try{console.error('ce upgrade <'+(el&&el.tagName)+'>',"
        "    e&&e.message, '|', (e&&e.stack||'').split('\\n').slice(0,3).join(' <- "
        "'));}catch(_){}} };"
        "  this.define=(n,c)=>{ m[n]=c; chain();"
        "    try{ var els=document.querySelectorAll(n); for(var i=0;i<els.length;i++) "
        "upgrade(els[i],c);"
        "    }catch(e){}"
        "    if(defers[n]){ defers[n].forEach(r=>r()); delete defers[n]; } };"
        "  this.get=n=>m[n];"
        "  this.whenDefined=n=>{ if(m[n])return Promise.resolve(m[n]);"
        "    return new Promise(res=>{ (defers[n]=defers[n]||[]).push(()=>res(m[n])); }); };"
        "  this.upgrade=root=>{ for(var n in m){ try{ var els=(root||document).querySelectorAll(n);"
        "    for(var i=0;i<els.length;i++) upgrade(els[i],m[n]); }catch(e){} } }; };"
        "globalThis.customElements = new globalThis.CustomElementRegistry();"
        "globalThis.Image       = function(){ this.src=''; this.onload=null; this.onerror=null; "
        "this.addEventListener=()=>{}; };"
        "globalThis.FileReader  = function(){ this.readAsText=()=>{}; this.readAsDataURL=()=>{}; "
        "this.addEventListener=()=>{}; };"
        "globalThis.FormData    = function(){ const m={}; this.append=(k,v)=>{m[k]=v;}; "
        "this.get=k=>m[k]; this.has=k=>k in m; this.delete=k=>{delete m[k];}; "
        "this.entries=function*(){for(const k in m)yield[k,m[k]];}; };"
        "globalThis.XMLHttpRequest = function(){"
        "  this.readyState=0; this.status=0; this.statusText=''; this.responseText=''; "
        "this.response=''; this.onload=null; this.onreadystatechange=null; this.onerror=null;"
        "  this._headers = {};"
        "  this.open = (m,u) => { this._method=m; this._url=u; this.readyState=1; if "
        "(this.onreadystatechange) this.onreadystatechange(); };"
        "  this.setRequestHeader = (k,v) => { this._headers[k]=v; };"
        "  this.send = (body) => { fetch(this._url, { method: this._method || 'GET', "
        "headers: this._headers, body: (body === undefined || body === null) ? undefined : "
        "String(body) }).then(r => r.text().then(t => {"
        "    this.status=r.status; this.statusText=r.ok ? 'OK' : ''; this.responseText=t; "
        "this.response=t; this.readyState=4;"
        "    if (this.onreadystatechange) this.onreadystatechange();"
        "    if (this.onload) this.onload(); })).catch(() => {"
        "    this.status=0; this.readyState=4; if (this.onerror) this.onerror(); }); };"
        "  this.abort = () => {}; this.getAllResponseHeaders = () => ''; this.getResponseHeader = "
        "() => null; this.addEventListener = (t, fn) => { this['on'+t] = fn; }; };"
        "globalThis.WebSocket = function(){ this.send=()=>{}; this.close=()=>{}; "
        "this.addEventListener=()=>{}; this.readyState=3; };"
        "globalThis.HTMLCanvasElement = function(){};"
        "globalThis.OffscreenCanvas = function(){ this.getContext = () => null; };"
        "globalThis.requestIdleCallback = (cb) => setTimeout(cb, 1);"
        "globalThis.cancelIdleCallback = clearTimeout;"
        "";
    JSValue stub_v = JS_Eval(ctx, stubs, strlen(stubs), "<webapi-stubs>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(stub_v)) {
        JSValue ex = JS_GetException(ctx);
        const char *m = JS_ToCString(ctx, ex);
        ydebug("js webapi-stub: %s", m ? m : "?");
        if (m) {
            JS_FreeCString(ctx, m);
        }
        JS_FreeValue(ctx, ex);
    }
    JS_FreeValue(ctx, stub_v);

    JS_FreeValue(ctx, global);
}

#else /* !YETTY_HAVE_QUICKJS */

void yetty_ylexbor_js_web_install(struct yetty_ylexbor *r)
{
    (void)r;
}
void yetty_ylexbor_js_web_shutdown(struct yetty_ylexbor *r)
{
    (void)r;
}
void yetty_ylexbor_js_drain_jobs(struct yetty_ylexbor *r)
{
    (void)r;
}
int yetty_ylexbor_pump(struct yetty_ylexbor *r)
{
    (void)r;
    return -1;
}
/* resolve_url + the yetty_ybrowser_fetch layer are defined unconditionally
 * above (above the QuickJS gate) — they're network helpers used by
 * image and external-stylesheet loading whether JS is on or off. */

#endif /* YETTY_HAVE_QUICKJS */
