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
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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
        /* Absolute path. A file:// document has no server root — approximate
		 * one: YBROWSER_FILE_ROOT names the directory the "site" is served
		 * from (the WPT runner sets it to the checkout root so
		 * /css/support/*.css resolves like it does on a real wptserve). */
        if (strncmp(base_url, "file://", 7) == 0) {
            const char *file_root = getenv("YBROWSER_FILE_ROOT");
            if (file_root && *file_root) {
                size_t root_len = strlen(file_root);
                while (root_len > 1 && file_root[root_len - 1] == '/') {
                    root_len--;
                }
                size_t hl = strlen(href);
                char *out = malloc(7 + root_len + hl + 1);
                if (!out) {
                    return NULL;
                }
                memcpy(out, "file://", 7);
                memcpy(out + 7, file_root, root_len);
                memcpy(out + 7 + root_len, href, hl + 1);
                return out;
            }
        }
        /* splice scheme://host/ */
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
    char age_header[32];
    char expires_header[80];
    char date_header[80];
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
           header_capture(line, len, "vary:", 5, transfer->vary, sizeof(transfer->vary)) ||
           header_capture(line, len, "age:", 4, transfer->age_header,
                          sizeof(transfer->age_header)) ||
           header_capture(line, len, "expires:", 8, transfer->expires_header,
                          sizeof(transfer->expires_header)) ||
           header_capture(line, len, "date:", 5, transfer->date_header,
                          sizeof(transfer->date_header)));
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

    /* Persistent Netscape cookie jar ($XDG_CACHE_HOME/yetty/cookies.txt or
	 * $HOME/.cache/yetty/cookies.txt). Loaded ONCE into the shared jar at loader
	 * create and written back ONCE at destroy, so consent/session cookies
	 * (YouTube SOCS, login state) survive a restart instead of forcing the
	 * consent wall every launch. Empty when no cache dir could be derived. */
    char cookie_path[1024];

    /* Cookies a page set via document.cookie, mirrored here (separate from
	 * curl's jar) so the next same-origin DOCUMENT navigation carries them —
	 * consent/session cookies (YouTube SOCS) are written this way then the page
	 * reloads. Host-scoped to avoid leaking cross-site. Guarded by cache_mutex;
	 * freed on loader destroy. */
    char *page_cookies;
    char *page_cookie_host;

    /* Disk tier — persists cache entries across restarts. The memory
	 * LRU below stays authoritative within a session; disk hits are
	 * promoted into it and flow through the same freshness /
	 * revalidation branches. */
    struct yetty_ybrowser_disk_cache disk_cache;

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

/* Plain GETs of documents and subresources are cacheable per HTTP
 * caching semantics (our DOCUMENT requests also run without the cookie
 * engine, so they carry no credentialed variance; Cookie-varying
 * responses are rejected by the Vary guard regardless). API traffic
 * (XHR/fetch bodies, non-GET methods) stays uncached — fetch()'s own
 * cache modes are not modelled. */
static int loader_cache_kind_ok(const struct yetty_ybrowser_request *request)
{
    if (request->body ||
        (request->method && *request->method && strcasecmp(request->method, "GET") != 0)) {
        return 0;
    }
    return request->kind == YETTY_YBROWSER_REQUEST_DOCUMENT ||
           request->kind == YETTY_YBROWSER_REQUEST_STYLE ||
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

/* Token match inside a Cache-Control value: directives are comma-separated
 * and a naive strstr("max-age=") also matches "s-maxage=" — walk tokens. */
static const char *cache_control_token(const char *cache_control, const char *name)
{
    size_t name_len = strlen(name);
    const char *cursor = cache_control;
    while (cursor && *cursor) {
        while (*cursor == ' ' || *cursor == '\t' || *cursor == ',') {
            cursor++;
        }
        if (!*cursor) {
            break;
        }
        size_t token_len = strcspn(cursor, ",");
        size_t bare_len = strcspn(cursor, ",= \t");
        if (bare_len == name_len && strncasecmp(cursor, name, name_len) == 0) {
            return cursor;
        }
        cursor += token_len;
    }
    return NULL;
}

/* Freshness policy from the response headers. This is a PRIVATE (per-user)
 * cache, so `private` is storable and `s-maxage` is a shared-cache
 * directive we deliberately ignore. Freshness precedence: max-age (minus
 * Age), Expires relative to Date, the Last-Modified heuristic (a tenth of
 * the resource's age, one-day cap), then the session default. `must-revalidate` needs
 * no special casing: a stale entry is only ever served through a
 * successful conditional revalidation, never as-is.
 *
 * Returns:
 *   -1  do not store (no-store)
 *    0  store but immediately stale — serve only via revalidation
 *       (no-cache, or freshness that has already expired per Age/Expires)
 *    1  fresh for *out_ttl_seconds */
static int loader_cache_ttl(const char *cache_control, const char *age_header,
                            const char *expires_header, const char *date_header,
                            const char *last_modified_header, long *out_ttl_seconds)
{
    long ttl_seconds = LOADER_CACHE_DEFAULT_TTL_SECONDS;
    int have_explicit_freshness = 0;
    if (cache_control && *cache_control) {
        if (cache_control_token(cache_control, "no-store")) {
            return -1;
        }
        if (cache_control_token(cache_control, "no-cache")) {
            *out_ttl_seconds = 0;
            return 0;
        }
        const char *max_age = cache_control_token(cache_control, "max-age");
        if (max_age) {
            const char *value = strchr(max_age, '=');
            ttl_seconds = value ? atol(value + 1) : 0;
            have_explicit_freshness = 1;
        }
    }
    if (!have_explicit_freshness && expires_header && *expires_header) {
        time_t expires_at = curl_getdate(expires_header, NULL);
        if (expires_at != (time_t)-1) {
            /* Relative to the origin's Date when present (clock-skew
			 * safe), else our own clock. */
            time_t origin_now = (time_t)-1;
            if (date_header && *date_header) {
                origin_now = curl_getdate(date_header, NULL);
            }
            if (origin_now == (time_t)-1) {
                origin_now = time(NULL);
            }
            ttl_seconds = (long)difftime(expires_at, origin_now);
            have_explicit_freshness = 1;
        }
    }
    if (!have_explicit_freshness && last_modified_header && *last_modified_header) {
        /* Heuristic freshness: no explicit lifetime, but a Last-Modified —
		 * use a tenth of the resource's age, capped at a day. */
        time_t last_modified_at = curl_getdate(last_modified_header, NULL);
        if (last_modified_at != (time_t)-1) {
            long resource_age = (long)difftime(time(NULL), last_modified_at);
            if (resource_age > 0) {
                ttl_seconds = resource_age / 10;
                if (ttl_seconds > 24l * 3600l) {
                    ttl_seconds = 24l * 3600l;
                }
                have_explicit_freshness = 1;
            }
        }
    }
    if (have_explicit_freshness && age_header && *age_header) {
        long age_seconds = atol(age_header);
        if (age_seconds > 0) {
            ttl_seconds -= age_seconds;
        }
    }
    if (ttl_seconds <= 0) {
        *out_ttl_seconds = 0;
        return have_explicit_freshness ? 0 : -1;
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

/* Insert a disk-tier entry into the memory LRU. Caller holds cache_mutex
 * and has verified the key is absent. Returns the new index, or -1. */
static int loader_cache_promote_locked(struct yetty_ybrowser_loader *loader, const char *url,
                                       enum yetty_ybrowser_request_kind kind, const char *body,
                                       size_t body_len,
                                       const struct yetty_ybrowser_disk_cache_meta *meta)
{
    if (loader->cache_count == loader->cache_cap) {
        int new_cap = loader->cache_cap ? loader->cache_cap * 2 : 32;
        struct loader_cache_entry *entries =
            realloc(loader->cache_entries, (size_t)new_cap * sizeof(*entries));
        if (!entries) {
            return -1;
        }
        loader->cache_entries = entries;
        loader->cache_cap = new_cap;
    }
    struct loader_cache_entry *entry = &loader->cache_entries[loader->cache_count];
    memset(entry, 0, sizeof(*entry));
    entry->url = strdup(url);
    entry->bytes = malloc(body_len + 1);
    if (!entry->url || !entry->bytes) {
        loader_cache_entry_free(entry);
        memset(entry, 0, sizeof(*entry));
        return -1;
    }
    memcpy(entry->bytes, body, body_len);
    entry->bytes[body_len] = '\0';
    entry->bytes_len = body_len;
    entry->kind = kind;
    entry->status = meta->status;
    entry->content_type = meta->content_type[0] ? strdup(meta->content_type) : NULL;
    entry->effective_url = meta->effective_url[0] ? strdup(meta->effective_url) : NULL;
    entry->etag = meta->etag[0] ? strdup(meta->etag) : NULL;
    entry->last_modified = meta->last_modified[0] ? strdup(meta->last_modified) : NULL;
    entry->expires_at = meta->expires_at;
    entry->last_used_tick = ++loader->cache_tick;
    entry->charge = body_len;
    int idx = loader->cache_count;
    loader->cache_count++;
    loader->cache_charge += entry->charge;
    loader_cache_evict(loader);
    /* Eviction may have moved or dropped the new entry — re-resolve. */
    return idx < loader->cache_count && loader->cache_entries[idx].url &&
                   strcmp(loader->cache_entries[idx].url, url) == 0
               ? idx
               : loader_cache_find(loader, url, kind);
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
    if (idx < 0) {
        /* Disk tier: promote a persisted entry — fresh OR merely
		 * revalidatable — into the memory LRU, then let the shared
		 * branch below decide (serve, go conditional, or drop). The
		 * mutex is released around the file IO; a racing promotion of
		 * the same URL resolves by re-running the find. */
        pthread_mutex_unlock(&loader->cache_mutex);
        struct yetty_ybrowser_disk_cache_meta disk_meta;
        char *disk_body = NULL;
        size_t disk_body_len = 0;
        int disk_hit =
            yetty_ybrowser_disk_cache_load(&loader->disk_cache, request->url, (int)request->kind,
                                           &disk_meta, &disk_body, &disk_body_len);
        pthread_mutex_lock(&loader->cache_mutex);
        if (disk_hit) {
            idx = loader_cache_find(loader, request->url, request->kind);
            if (idx < 0 && disk_body_len <= LOADER_CACHE_BUDGET / 4) {
                idx = loader_cache_promote_locked(loader, request->url, request->kind, disk_body,
                                                  disk_body_len, &disk_meta);
            }
            free(disk_body);
        }
    }
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
    if (loader_cache_ttl(transfer->cache_control, transfer->age_header, transfer->expires_header,
                         transfer->date_header, transfer->last_modified, &ttl_seconds) <= 0) {
        ttl_seconds = 0; /* stays revalidate-on-every-use */
    }
    int served = 0;
    struct yetty_ybrowser_disk_cache_meta disk_meta = {0};
    char *disk_body = NULL;
    size_t disk_body_len = 0;
    pthread_mutex_lock(&loader->cache_mutex);
    int idx = loader_cache_find(loader, request->url, request->kind);
    if (idx >= 0) {
        struct loader_cache_entry *entry = &loader->cache_entries[idx];
        entry->expires_at = time(NULL) + ttl_seconds;
        entry->last_used_tick = ++loader->cache_tick;
        served = loader_cache_entry_to_response(entry, response);
        if (served) {
            /* Snapshot for the disk refresh outside the lock — a 304
			 * must extend the persisted entry's freshness too, or every
			 * cold start repeats the conditional round-trip. */
            disk_meta.status = entry->status;
            disk_meta.expires_at = entry->expires_at;
            disk_meta.stored_at = time(NULL);
            if (entry->etag) {
                snprintf(disk_meta.etag, sizeof(disk_meta.etag), "%s", entry->etag);
            }
            if (entry->last_modified) {
                snprintf(disk_meta.last_modified, sizeof(disk_meta.last_modified), "%s",
                         entry->last_modified);
            }
            if (entry->content_type) {
                snprintf(disk_meta.content_type, sizeof(disk_meta.content_type), "%s",
                         entry->content_type);
            }
            if (entry->effective_url) {
                snprintf(disk_meta.effective_url, sizeof(disk_meta.effective_url), "%s",
                         entry->effective_url);
            }
            disk_body = malloc(entry->bytes_len);
            if (disk_body) {
                memcpy(disk_body, entry->bytes, entry->bytes_len);
                disk_body_len = entry->bytes_len;
            }
        }
    }
    pthread_mutex_unlock(&loader->cache_mutex);
    if (disk_body) {
        yetty_ybrowser_disk_cache_store(&loader->disk_cache, request->url, (int)request->kind,
                                        &disk_meta, disk_body, disk_body_len);
        free(disk_body);
    }
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
    int freshness =
        loader_cache_ttl(transfer->cache_control, transfer->age_header, transfer->expires_header,
                         transfer->date_header, transfer->last_modified, &ttl_seconds);
    if (freshness < 0) {
        return; /* no-store */
    }
    if (freshness == 0 && !transfer->etag[0] && !transfer->last_modified[0]) {
        return; /* immediately stale and nothing to revalidate with */
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

    /* Persist. Same policy decisions as the memory tier (this runs after
	 * the no-store / Vary / size gates above); the disk entry carries the
	 * absolute expiry so a cold start resumes with identical freshness or
	 * revalidation behavior. */
    {
        struct yetty_ybrowser_disk_cache_meta disk_meta = {0};
        disk_meta.status = response->status;
        disk_meta.expires_at = time(NULL) + (freshness > 0 ? ttl_seconds : 0);
        disk_meta.stored_at = time(NULL);
        snprintf(disk_meta.etag, sizeof(disk_meta.etag), "%s", transfer->etag);
        snprintf(disk_meta.last_modified, sizeof(disk_meta.last_modified), "%s",
                 transfer->last_modified);
        if (response->content_type) {
            snprintf(disk_meta.content_type, sizeof(disk_meta.content_type), "%s",
                     response->content_type);
        }
        if (response->effective_url) {
            snprintf(disk_meta.effective_url, sizeof(disk_meta.effective_url), "%s",
                     response->effective_url);
        }
        yetty_ybrowser_disk_cache_store(&loader->disk_cache, request->url, (int)request->kind,
                                        &disk_meta, response->body, response->body_len);
    }
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
    /* Disk cache tier — best-effort: a failure here (no writable cache
	 * dir, mutex init) degrades to memory-only caching, never breaks
	 * fetching. */
    {
        struct yetty_ycore_void_result disk_res =
            yetty_ybrowser_disk_cache_init(&loader->disk_cache);
        if (YETTY_IS_ERR(disk_res)) {
            yetty_ycore_error_destroy(disk_res.error);
        }
    }

    /* Persistent cookie jar: derive $XDG_CACHE_HOME/yetty/cookies.txt (or under
	 * $HOME/.cache), ensure the directory exists, and load any saved cookies
	 * into the shared jar ONCE so consent/session state survives a restart. The
	 * jar is written back at loader destroy. Best-effort: if the dir can't be
	 * made or the file doesn't exist yet, we simply start cookieless. */
    {
        const char *xdg = getenv("XDG_CACHE_HOME");
        const char *home = getenv("HOME");
        char dir[1024] = {0};
        if (xdg && *xdg) {
            snprintf(dir, sizeof(dir), "%s/yetty", xdg);
        } else if (home && *home) {
            snprintf(dir, sizeof(dir), "%s/.cache/yetty", home);
        }
        if (dir[0]) {
            /* Create the parent chain (…/.cache then …/.cache/yetty). */
            char parent[1024];
            snprintf(parent, sizeof(parent), "%s", dir);
            char *slash = strrchr(parent, '/');
            if (slash && slash != parent) {
                *slash = '\0';
                (void)mkdir(parent, 0755);
            }
            if (mkdir(dir, 0700) == 0 || errno == EEXIST) {
                snprintf(loader->cookie_path, sizeof(loader->cookie_path), "%s/cookies.txt", dir);
                if (loader->share) {
                    CURL *primer = curl_easy_init();
                    if (primer) {
                        curl_easy_setopt(primer, CURLOPT_SHARE, loader->share);
                        curl_easy_setopt(primer, CURLOPT_COOKIEFILE, loader->cookie_path);
                        /* Force the read now so the cookies land in the shared
						 * jar before the first real transfer. */
                        curl_easy_setopt(primer, CURLOPT_COOKIELIST, "RELOAD");
                        curl_easy_cleanup(primer);
                    }
                }
            }
        }
    }

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
    /* Persist the shared cookie jar to disk before tearing the share down, so
	 * consent/session cookies set this run are there on the next launch. The
	 * flush handle joins the share (COOKIEFILE="" enables the engine against the
	 * shared jar without re-reading the file), points COOKIEJAR at our path, and
	 * both the explicit FLUSH and the cleanup write the Netscape jar. */
    if (loader->share && loader->cookie_path[0]) {
        CURL *flush = curl_easy_init();
        if (flush) {
            curl_easy_setopt(flush, CURLOPT_SHARE, loader->share);
            curl_easy_setopt(flush, CURLOPT_COOKIEFILE, "");
            curl_easy_setopt(flush, CURLOPT_COOKIEJAR, loader->cookie_path);
            curl_easy_setopt(flush, CURLOPT_COOKIELIST, "FLUSH");
            curl_easy_cleanup(flush);
        }
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
    free(loader->page_cookies);
    free(loader->page_cookie_host);
    pthread_mutex_destroy(&loader->cache_mutex);
    pthread_mutex_destroy(&loader->inflight_mutex);
    yetty_ybrowser_disk_cache_shutdown(&loader->disk_cache);
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
    /* Enable the cookie engine (empty COOKIEFILE = no file, just parse) for
     * ALL requests, including DOCUMENT navigations. The page redirect chain
     * sets consent/visitor/session cookies (e.g. YouTube SOCS, YSC,
     * VISITOR_INFO1_LIVE) that the document and its subresources need; without
     * them sites serve a consent-walled or logged-out-empty variant (the
     * YouTube homepage feed is empty cookieless). The jar is shared across
     * handles via CURL_LOCK_DATA_COOKIE on the loader, so cookies set on the
     * page request are visible to subresource/API fetches too. */
    curl_easy_setopt(easy, CURLOPT_COOKIEFILE, "");
    /* A same-origin DOCUMENT navigation also carries cookies the previous page
	 * set via document.cookie (mirrored on the loader): that is how a consent
	 * reload sends its freshly-written SOCS. Host-scoped so a page's cookies
	 * never travel to a different site. curl copies the string in setopt, so it
	 * is safe to unlock right after. */
    if (request->kind == YETTY_YBROWSER_REQUEST_DOCUMENT) {
        /* Persist the jar to disk when this navigation's easy handle is cleaned
		 * up (right after the transfer, not at process exit), so consent/session
		 * cookies set on the page — the reload after "Accept all" writes SOCS —
		 * survive even a later crash or hard kill. Navigations are infrequent
		 * and effectively serial, so the per-nav write is cheap. */
        if (loader->cookie_path[0]) {
            curl_easy_setopt(easy, CURLOPT_COOKIEJAR, loader->cookie_path);
        }
        pthread_mutex_lock(&loader->cache_mutex);
        if (loader->page_cookies && loader->page_cookie_host) {
            const char *req_host = NULL;
            size_t req_host_len = 0;
            if (url_host_span(request->url, &req_host, &req_host_len) &&
                req_host_len == strlen(loader->page_cookie_host) &&
                strncmp(req_host, loader->page_cookie_host, req_host_len) == 0) {
                curl_easy_setopt(easy, CURLOPT_COOKIE, loader->page_cookies);
            }
        }
        pthread_mutex_unlock(&loader->cache_mutex);
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
    yetty_ylexbor_js_update_stack_top(r);
    JSContext *ctx = (JSContext *)r->js_ctx;

    /* Dynamically-inserted external <script>s fetch + execute here — after
	 * the inserting script's turn ended, before timers (a loaded script may
	 * schedule the timers this same pump then fires). */
    int scripts_executed = yetty_ylexbor_js_run_pending_scripts(r);

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
        /* Executed or still-queued dynamic scripts want another tick soon —
		 * returning -1 ("no timers") would let a boot loop stop before a
		 * loader chain finishes. */
        return (scripts_executed > 0 || r->pending_script_count > 0) ? 0 : -1;
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
        /* A Headers instance keeps its name→value pairs in an internal map
         * object exposed as `headerMap` (its own props are accessor methods,
         * not the header entries); a plain object is iterated directly. */
        JSValue header_map = JS_GetPropertyStr(ctx, headers_val, "headerMap");
        JSValueConst header_source = JS_IsObject(header_map) ? header_map : headers_val;
        JSPropertyEnum *props = NULL;
        uint32_t prop_count = 0;
        if (JS_GetOwnPropertyNames(ctx, &props, &prop_count, header_source,
                                   JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) == 0) {
            params->header_lines =
                calloc(prop_count ? prop_count : 1, sizeof(*params->header_lines));
            for (uint32_t i = 0; params->header_lines && i < prop_count; i++) {
                const char *name = JS_AtomToCString(ctx, props[i].atom);
                JSValue value_val = JS_GetProperty(ctx, header_source, props[i].atom);
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
        JS_FreeValue(ctx, header_map);
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
    ydebug("js fetch: %s %s", job->params.method ? job->params.method : "GET",
           job->params.url ? job->params.url : "(null)");
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

    /* A navigation/reload runs load_html, which SYNCHRONOUSLY frees this
	 * engine's JS context (js_destroy) and bumps fetch_generation. A deferred
	 * full-destroy (destroy_pending) instead keeps the context pinned alive
	 * until in-flight jobs drain. So a generation mismatch means our JSContext
	 * is already gone: JS_FreeContext/JS_FreeRuntime freed every object in it,
	 * including the promise resolve/reject refs this job dup'd. Touching them
	 * now is a use-after-free on a dangling context that corrupts the allocator
	 * and crashes later (seen while parsing the reloaded page's scripts;
	 * YouTube fires dozens of icon fetch()es that outlive its consent reload).
	 * On reload we must NOT deliver AND must NOT free those refs. */
    int reloaded = (job->generation != r->fetch_generation);
    int stale = reloaded || r->destroy_pending;
    if (!stale && r->js_ctx) {
        yetty_ylexbor_js_update_stack_top(r);
        js_fetch_deliver(job->ctx, &job->response, job->resolve_func, job->reject_func);
        yetty_ylexbor_js_drain_jobs(r);
        if (r->on_resource_ready) {
            r->on_resource_ready(r->resource_ready_user);
        }
    }
    /* The context is still alive even on the deferred-teardown path —
	 * _yetty_ylexbor_destroy_now only runs below, after the last job. */
    if (!reloaded) {
        /* Context still alive: a live engine, or the deferred-teardown path
		 * (_yetty_ylexbor_destroy_now only runs below, after the last job). */
        JS_FreeValue(job->ctx, job->resolve_func);
        JS_FreeValue(job->ctx, job->reject_func);
    }
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

/* ===========================================================================
 * Dynamically-inserted external <script> as a worker-pool job — the same
 * lifecycle as js_fetch_job: run() fetches on a worker thread (generation-
 * cancellable), done() delivers on the loop thread where the generation
 * guard decides whether the element/context are still the live document's.
 * Submitted from yetty_ylexbor_js_queue_script when a pool is available;
 * without a pool the pump's small per-tick batch handles the queue instead
 * (same degraded mode the image path uses).
 * ===========================================================================*/
struct js_script_job {
    struct yetty_ylexbor *r;
    uint64_t generation;
    struct yetty_ybrowser_loader *loader;
    char *url;                  /* owned */
    char *referer;              /* owned */
    lxb_dom_element_t *element; /* weak — document-owned; generation-guarded */
    struct yetty_ybrowser_response response;
};

/* WORKER THREAD. */
static void js_script_job_run(void *job_ptr)
{
    struct js_script_job *job = job_ptr;
    ydebug("dynamic script fetch %.140s", job->url);
    struct yetty_ybrowser_request request = {
        .url = job->url,
        .kind = YETTY_YBROWSER_REQUEST_SCRIPT,
        .referer = job->referer,
        .generation = job->generation,
        .cancel_generation = &job->r->fetch_generation,
    };
    struct yetty_ycore_void_result fetch_res =
        yetty_ybrowser_fetch(job->loader, &request, &job->response);
    if (YETTY_IS_ERR(fetch_res)) {
        yetty_ycore_error_destroy(fetch_res.error);
    }
}

/* LOOP THREAD. Evaluate the script and fire load/error on its element.
 * Signature dictated by the work pool (void (*)(void *)) — absorb inner
 * Results at this boundary. */
YETTY_EXTERNAL_CALLBACK
static void js_script_job_done(void *job_ptr)
{
    struct js_script_job *job = job_ptr;
    struct yetty_ylexbor *r = job->r;
    r->img_jobs_in_flight--;

    /* Same staleness rules as js_fetch_job_done: a navigation freed the
	 * context this job was minted for — the element pointer and JSContext
	 * are the OLD document's, touch neither. */
    int reloaded = (job->generation != r->fetch_generation);
    int stale = reloaded || r->destroy_pending;
    if (!stale && r->js_ctx) {
        if (job->response.body && job->response.status >= 200 && job->response.status < 300) {
            yetty_ylexbor_js_eval_script_body(r, job->response.body, job->response.body_len,
                                              job->url);
            yetty_ylexbor_js_fire_element_event(r, job->element, "load");
        } else {
            ydebug("dynamic script %.140s status=%ld", job->url, job->response.status);
            yetty_ylexbor_js_fire_element_event(r, job->element, "error");
        }
        yetty_ylexbor_js_drain_jobs(r);
        if (r->on_resource_ready) {
            r->on_resource_ready(r->resource_ready_user);
        }
    }
    yetty_ybrowser_response_dispose(&job->response);
    free(job->url);
    free(job->referer);
    free(job);

    if (r->destroy_pending && r->img_jobs_in_flight == 0) {
        struct yetty_ycore_void_result destroy_res = _yetty_ylexbor_destroy_now(r);
        if (YETTY_IS_ERR(destroy_res)) {
            ydebug("js_script_job_done: deferred destroy failed: %s", destroy_res.error.msg);
            yetty_ycore_error_destroy(destroy_res.error);
        }
    }
}

int yetty_ylexbor_js_submit_script_job(struct yetty_ylexbor *r, lxb_dom_element_t *element,
                                       char *url)
{
    if (r == NULL || r->img_pool == NULL || r->loader == NULL) {
        return 0;
    }
    struct js_script_job *job = calloc(1, sizeof(*job));
    if (job == NULL) {
        return 0;
    }
    job->r = r;
    job->generation = r->fetch_generation;
    job->loader = r->loader;
    job->url = url; /* ownership moves to the job on successful submit */
    job->referer = r->base_url ? strdup(r->base_url) : NULL;
    job->element = element;
    struct yetty_yplatform_yworkpool_job pool_job = {
        .run = js_script_job_run,
        .done = js_script_job_done,
        .ctx = job,
    };
    struct yetty_ycore_void_result submit_res =
        yetty_yplatform_yworkpool_submit(r->img_pool, pool_job);
    if (YETTY_IS_ERR(submit_res)) {
        yetty_ycore_error_destroy(submit_res.error);
        free(job->referer);
        free(job); /* url stays with the caller on failure */
        return 0;
    }
    r->img_jobs_in_flight++;
    return 1;
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
    /* The first argument is a URL string OR a Request object. A Request
     * carries the URL in .url (plus .method/.headers/.body); stringifying it
     * directly would yield "[object Object]". Read .url when it's a string. */
    JSValue request_url_val = JS_UNDEFINED;
    bool arg0_is_request = false;
    const char *url_arg = NULL;
    if (JS_IsObject(argv[0]) && !JS_IsString(argv[0])) {
        JSValue url_prop = JS_GetPropertyStr(ctx, argv[0], "url");
        if (JS_IsString(url_prop)) {
            request_url_val = url_prop;
            url_arg = JS_ToCString(ctx, request_url_val);
            arg0_is_request = true;
        } else {
            JS_FreeValue(ctx, url_prop);
        }
    }
    if (!arg0_is_request) {
        url_arg = JS_ToCString(ctx, argv[0]);
    }
    if (!url_arg) {
        JS_FreeValue(ctx, request_url_val);
        goto out;
    }
    struct js_fetch_params params = {0};
    params.url = yetty_ylexbor_resolve_url(r, url_arg);
    JS_FreeCString(ctx, url_arg);
    JS_FreeValue(ctx, request_url_val);
    if (!params.url) {
        JSValue error_obj = JS_NewError(ctx);
        JS_Call(ctx, resolving_funcs[1], JS_UNDEFINED, 1, (JSValueConst[]){error_obj});
        JS_FreeValue(ctx, error_obj);
        goto out;
    }
    /* Init precedence: an explicit init object wins; otherwise a Request
     * object supplies its own method/headers/body. */
    if (argc >= 2 && JS_IsObject(argv[1])) {
        js_fetch_parse_init(ctx, argv[1], &params);
    } else if (arg0_is_request) {
        js_fetch_parse_init(ctx, argv[0], &params);
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

/* Mirror the page's accumulated document.cookie onto the loader so the next
 * same-origin DOCUMENT navigation can send it: document.cookie and curl's jar
 * are separate stores, and sites set consent/session cookies via document.cookie
 * then reload — YouTube writes SOCS this way; without this the reload is
 * cookieless and the consent wall returns. Host-scoped so it never leaks to a
 * different site. Safe from the JS thread (a plain mutexed string copy — no curl
 * handle, so it can't disturb the loader's in-flight transfers). */
static void cookie_bridge_to_jar(struct yetty_ylexbor *r, const char *cookie)
{
    (void)cookie;
    if (!r || !r->loader || !r->base_url || !r->web_cookie_string) {
        return;
    }
    const char *host = NULL;
    size_t host_len = 0;
    if (!url_host_span(r->base_url, &host, &host_len) || host_len == 0) {
        return;
    }
    struct yetty_ybrowser_loader *loader = r->loader;
    char *cookies_copy = strdup(r->web_cookie_string);
    char *host_copy = strndup(host, host_len);
    if (!cookies_copy || !host_copy) {
        free(cookies_copy);
        free(host_copy);
        return;
    }
    pthread_mutex_lock(&loader->cache_mutex);
    free(loader->page_cookies);
    free(loader->page_cookie_host);
    loader->page_cookies = cookies_copy;
    loader->page_cookie_host = host_copy;
    pthread_mutex_unlock(&loader->cache_mutex);
}

/* Case-insensitive "does hay start with prefix". */
static bool cookie_ci_prefix(const char *hay, const char *prefix)
{
    while (*prefix) {
        if (tolower((unsigned char)*hay) != tolower((unsigned char)*prefix)) {
            return false;
        }
        hay++;
        prefix++;
    }
    return true;
}

/* Case-insensitive search for `needle` anywhere in `hay`. */
static const char *cookie_ci_find(const char *hay, const char *needle)
{
    for (; *hay; hay++) {
        if (cookie_ci_prefix(hay, needle)) {
            return hay;
        }
    }
    return NULL;
}

/* Apply one `document.cookie = "..."` write with real (if simplified) cookie
 * semantics. Only the leading `name=value` is the cookie; everything after the
 * first ';' is attributes. An Expires in the past or Max-Age <= 0 DELETES the
 * named cookie; otherwise the cookie is SET, replacing any existing cookie of
 * the same name — never accumulating duplicates and never storing
 * expires/domain/path/secure as if they were cookies. The store is the
 * "name=value; name2=value2" string in r->web_cookie_string, which is both what
 * document.cookie returns and what is mirrored onto the loader for the next
 * navigation. (The previous code appended the whole raw declaration, so the
 * store filled with attribute-junk and stale duplicates, and a page that reads
 * document.cookie back to confirm its consent write — YouTube does — saw a
 * garbled string and re-showed the consent wall.) */
static void web_cookie_apply(struct yetty_ylexbor *r, const char *decl)
{
    if (!decl) {
        return;
    }
    while (*decl == ' ' || *decl == '\t') {
        decl++;
    }
    const char *semi = strchr(decl, ';');
    size_t nv_len = semi ? (size_t)(semi - decl) : strlen(decl);
    while (nv_len > 0 && (decl[nv_len - 1] == ' ' || decl[nv_len - 1] == '\t')) {
        nv_len--;
    }
    const char *eq = memchr(decl, '=', nv_len);
    if (!eq) {
        return; /* not a name=value pair — ignore */
    }
    size_t name_len = (size_t)(eq - decl);
    while (name_len > 0 && (decl[name_len - 1] == ' ' || decl[name_len - 1] == '\t')) {
        name_len--;
    }
    if (name_len == 0) {
        return;
    }
    const char *value = eq + 1;
    size_t value_len = (size_t)(decl + nv_len - value);

    /* Deletion? Expires in the past or Max-Age <= 0. */
    bool del = false;
    if (semi) {
        const char *attrs = semi + 1;
        const char *ma = cookie_ci_find(attrs, "max-age");
        if (ma) {
            const char *q = ma + 7;
            while (*q == ' ' || *q == '\t') {
                q++;
            }
            if (*q == '=') {
                if (strtol(q + 1, NULL, 10) <= 0) {
                    del = true;
                }
            }
        }
        const char *ex = cookie_ci_find(attrs, "expires");
        if (ex) {
            const char *q = ex + 7;
            while (*q == ' ' || *q == '\t') {
                q++;
            }
            if (*q == '=') {
                time_t t = curl_getdate(q + 1, NULL);
                if (t != (time_t)-1 && t <= time(NULL)) {
                    del = true;
                }
            }
        }
    }

    /* Rebuild the store: keep every existing pair whose name differs from this
	 * one, then append the fresh pair (unless deleting). */
    const char *old = r->web_cookie_string ? r->web_cookie_string : "";
    size_t need = strlen(old) + name_len + value_len + 4;
    char *out = malloc(need);
    if (!out) {
        return;
    }
    size_t out_len = 0;
    const char *p = old;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == ';') {
            p++;
        }
        if (!*p) {
            break;
        }
        const char *pair = p;
        const char *pair_end = strchr(p, ';');
        size_t pair_len = pair_end ? (size_t)(pair_end - p) : strlen(p);
        p = pair_end ? pair_end : p + pair_len;
        /* Compare this pair's name to the incoming name. */
        const char *peq = memchr(pair, '=', pair_len);
        size_t pn_len = peq ? (size_t)(peq - pair) : pair_len;
        if (pn_len == name_len && strncmp(pair, decl, name_len) == 0) {
            continue; /* drop the stale copy */
        }
        if (out_len) {
            memcpy(out + out_len, "; ", 2);
            out_len += 2;
        }
        memcpy(out + out_len, pair, pair_len);
        out_len += pair_len;
    }
    if (!del) {
        if (out_len) {
            memcpy(out + out_len, "; ", 2);
            out_len += 2;
        }
        memcpy(out + out_len, decl, name_len);
        out_len += name_len;
        out[out_len++] = '=';
        memcpy(out + out_len, value, value_len);
        out_len += value_len;
    }
    out[out_len] = '\0';
    free(r->web_cookie_string);
    r->web_cookie_string = out;
}

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
    web_cookie_apply(r, s);
    cookie_bridge_to_jar(r, s);
    JS_FreeCString(ctx, s);
    return JS_UNDEFINED;
}

/* ===========================================================================
 * matchMedia / getComputedStyle — feature-detection-friendly minimal
 * shapes.
 * ===========================================================================*/

/* -------- matchMedia: a minimal but STRICT media-query evaluator --------
 * We evaluate width/height range features and the screen/all media types
 * against the live viewport. Anything we do not model (color-scheme, resolution,
 * orientation, hover, non-px units, MQ4 `<` / `>` range syntax) makes the query
 * NON-matching rather than being guessed at. Invalid syntax (unbalanced parens,
 * dangling not/only, two media types) is likewise a non-match. The CSS cascade
 * itself uses libcss's own evaluator against the same viewport. */

/* Parse a media-feature length in [s,e): optional spaces, a non-negative
 * number, optional spaces, then "px" — or a unitless 0. The whole range must be
 * consumed; any other unit (em/rem/vw/%) or trailing garbage is rejected so an
 * unsupported unit is NOT silently treated as pixels. */
static bool media_parse_px(const char *s, const char *e, float *out)
{
    while (s < e && isspace((unsigned char)*s)) {
        s++;
    }
    const char *num_start = s;
    bool any_digit = false;
    while (s < e && isdigit((unsigned char)*s)) {
        s++;
        any_digit = true;
    }
    if (s < e && *s == '.') {
        s++;
        while (s < e && isdigit((unsigned char)*s)) {
            s++;
            any_digit = true;
        }
    }
    /* Require at least one digit so ".", ".px", and "px" are rejected rather
     * than parsed as 0. Accepts 0, 0.0, .5px, 1.5px. */
    if (!any_digit) {
        return false;
    }
    size_t nlen = (size_t)(s - num_start);
    if (nlen >= 64) {
        return false;
    }
    char numbuf[64];
    memcpy(numbuf, num_start, nlen);
    numbuf[nlen] = '\0';
    float value = (float)atof(numbuf);
    while (s < e && isspace((unsigned char)*s)) {
        s++;
    }
    if (s == e) {
        /* unitless — only 0 is a valid length */
        if (value != 0.0f) {
            return false;
        }
        *out = 0.0f;
        return true;
    }
    if (e - s >= 2 && s[0] == 'p' && s[1] == 'x') {
        s += 2;
        while (s < e && isspace((unsigned char)*s)) {
            s++;
        }
        if (s != e) {
            return false;
        }
        *out = value;
        return true;
    }
    return false;
}

/* Evaluate one "(name: value)" feature (inner = text between the parens). Sets
 * *invalid on a structural error: no colon (bare/range syntax), a value that is
 * not a valid px length, or a feature we do not model. */
static bool media_eval_feature(const char *inner, size_t len, int vw, int vh, bool *invalid)
{
    const char *e = inner + len;
    const char *colon = memchr(inner, ':', len);
    if (!colon) {
        *invalid = true; /* range syntax or a boolean feature we don't support */
        return false;
    }
    const char *name_s = inner, *name_e = colon;
    while (name_s < name_e && isspace((unsigned char)*name_s)) {
        name_s++;
    }
    while (name_e > name_s && isspace((unsigned char)name_e[-1])) {
        name_e--;
    }
    size_t nlen = (size_t)(name_e - name_s);
    float px = 0.0f;
    if (!media_parse_px(colon + 1, e, &px)) {
        *invalid = true;
        return false;
    }
    /* Compare against the parsed float directly so a fractional threshold like
     * (min-width: 800.9px) does not round down to 800 and mis-match at 800px. */
    if (nlen == 9 && !memcmp(name_s, "min-width", 9)) {
        return (float)vw >= px;
    }
    if (nlen == 9 && !memcmp(name_s, "max-width", 9)) {
        return (float)vw <= px;
    }
    if (nlen == 5 && !memcmp(name_s, "width", 5)) {
        return (float)vw == px;
    }
    if (nlen == 10 && !memcmp(name_s, "min-height", 10)) {
        return (float)vh >= px;
    }
    if (nlen == 10 && !memcmp(name_s, "max-height", 10)) {
        return (float)vh <= px;
    }
    if (nlen == 6 && !memcmp(name_s, "height", 6)) {
        return (float)vh == px;
    }
    *invalid = true; /* feature we don't model */
    return false;
}

/* Evaluate one comma-separated media-query part. Grammar handled:
 *   [only|not]? <type>? [and (feature)]*  |  (feature) [and (feature)]*
 * Matching media types: screen, all. print/speech/unknown do not match. `not`
 * negates a well-formed result. A structural error (unbalanced parens, unknown
 * feature, bad unit, two media types, dangling not/only) is a non-match
 * regardless of `not`. */
static bool media_query_part_matches(const char *part, size_t len, int vw, int vh)
{
    /* Reject an overlong part rather than truncating — a truncated matching
     * prefix could turn an invalid/non-matching query into a matching one. */
    char buf[512];
    if (len >= sizeof(buf)) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        buf[i] = (char)tolower((unsigned char)part[i]);
    }
    buf[len] = '\0';

    char *s = buf, *e = buf + strlen(buf);
    while (s < e && isspace((unsigned char)*s)) {
        s++;
    }
    while (e > s && isspace((unsigned char)e[-1])) {
        e--;
    }
    *e = '\0';
    if (s == e) {
        return true; /* empty query == all */
    }

    int depth = 0;
    for (char *p = s; p < e; p++) {
        if (*p == '(') {
            depth++;
        } else if (*p == ')') {
            if (--depth < 0) {
                return false; /* unbalanced */
            }
        }
    }
    if (depth != 0) {
        return false;
    }

    bool negate = false;
    if ((size_t)(e - s) > 3 && !memcmp(s, "not", 3) && isspace((unsigned char)s[3])) {
        negate = true;
        s += 3;
    } else if ((size_t)(e - s) > 4 && !memcmp(s, "only", 4) && isspace((unsigned char)s[4])) {
        s += 4;
    }
    while (s < e && isspace((unsigned char)*s)) {
        s++;
    }
    if (s == e) {
        return false; /* dangling not/only */
    }

    /* Conjunction grammar enforced with a small state machine:
     *   EXPECT_PRIMARY : first token — a media type OR a feature;
     *   EXPECT_AND     : after a type/feature — only `and` or end is valid;
     *   EXPECT_FEATURE : after `and` — only a feature is valid (no type, no
     *                    repeated `and`, no end).
     * Rejects missing / leading / trailing / repeated `and` and bare adjacency. */
    enum { EXPECT_PRIMARY, EXPECT_AND, EXPECT_FEATURE } state = EXPECT_PRIMARY;
    bool result = true;
    char *p = s;
    while (p < e) {
        while (p < e && isspace((unsigned char)*p)) {
            p++;
        }
        if (p >= e) {
            break;
        }
        if (*p == '(') {
            if (state != EXPECT_PRIMARY && state != EXPECT_FEATURE) {
                return false; /* feature with no preceding `and` */
            }
            char *q = p;
            int d = 0;
            for (; q < e; q++) {
                if (*q == '(') {
                    d++;
                } else if (*q == ')') {
                    if (--d == 0) {
                        q++;
                        break;
                    }
                }
            }
            bool invalid = false;
            bool feature_matches =
                media_eval_feature(p + 1, (size_t)((q - 1) - (p + 1)), vw, vh, &invalid);
            if (invalid) {
                return false;
            }
            result = result && feature_matches;
            state = EXPECT_AND;
            p = q;
        } else {
            char *q = p;
            while (q < e && !isspace((unsigned char)*q) && *q != '(') {
                q++;
            }
            size_t tlen = (size_t)(q - p);
            if (tlen == 3 && !memcmp(p, "and", 3)) {
                if (state != EXPECT_AND) {
                    return false; /* leading or repeated `and` */
                }
                state = EXPECT_FEATURE;
            } else {
                /* a bare media type — only valid as the first primary token */
                if (state != EXPECT_PRIMARY) {
                    return false; /* type after `and`, or a second media type */
                }
                bool type_ok =
                    (tlen == 6 && !memcmp(p, "screen", 6)) || (tlen == 3 && !memcmp(p, "all", 3));
                result = result && type_ok;
                state = EXPECT_AND;
            }
            p = q;
        }
    }
    if (state == EXPECT_FEATURE) {
        return false; /* trailing `and` */
    }
    return negate ? !result : result;
}

static bool media_query_matches(const char *query, int vw, int vh)
{
    if (query == NULL) {
        return false;
    }
    /* Always evaluate at least one part so an empty query string (a valid list
     * that means `all`) matches rather than being skipped by a `while (*part)`. */
    const char *part = query;
    for (;;) {
        const char *comma = strchr(part, ',');
        size_t len = comma ? (size_t)(comma - part) : strlen(part);
        if (media_query_part_matches(part, len, vw, vh)) {
            return true; /* comma = OR */
        }
        if (!comma) {
            break;
        }
        part = comma + 1;
    }
    return false;
}

/* Live MediaQueryList.matches: re-evaluates the stored `media` string against
 * the CURRENT viewport on every read, so a set_viewport() is reflected on the
 * same object without recreating it. */
static JSValue js_mql_matches_get(JSContext *ctx, JSValueConst this_val)
{
    JSValue mediav = JS_GetPropertyStr(ctx, this_val, "media");
    const char *query = JS_ToCString(ctx, mediav);
    struct yetty_ylexbor *r = runtime_ylex_w(ctx);
    int vw = r ? r->viewport_w : 1024;
    int vh = r ? r->viewport_h : 768;
    bool matches = query ? media_query_matches(query, vw, vh) : false;
    if (query) {
        JS_FreeCString(ctx, query);
    }
    JS_FreeValue(ctx, mediav);
    return JS_NewBool(ctx, matches);
}

static void define_getter(JSContext *ctx, JSValue obj, const char *name,
                          JSValue (*getter)(JSContext *, JSValueConst));

static JSValue js_matchMedia(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    const char *query = (argc >= 1) ? JS_ToCString(ctx, argv[0]) : NULL;
    /* MediaQueryList with a LIVE `matches` getter — re-reads the stored query
     * against the current r->viewport_w/h on each access, so a set_viewport() is
     * reflected on the same object. Change-event dispatch (onchange /
     * addEventListener('change')) is not yet wired; listeners stay no-ops. */
    const char *def = "({ media:'', addEventListener:()=>{}, removeEventListener:()=>{}, "
                      "addListener:()=>{}, removeListener:()=>{}, dispatchEvent:()=>false, "
                      "onchange:null })";
    JSValue mql = JS_Eval(ctx, def, strlen(def), "<matchMedia>", JS_EVAL_TYPE_GLOBAL);
    if (!JS_IsException(mql)) {
        JS_SetPropertyStr(ctx, mql, "media", JS_NewString(ctx, query ? query : ""));
        define_getter(ctx, mql, "matches", js_mql_matches_get);
    }
    if (query) {
        JS_FreeCString(ctx, query);
    }
    return mql;
}

/* Resolved-value computed style lives in ybrowser-js-dom.c, where the laid-out
 * box vector is reachable. It installs the inline-style declaration as the
 * result's prototype, so callers still get getPropertyValue/setProperty/cssText
 * plus every inline property, and layers the box-derived resolved values on top. */
JSValue yetty_ylexbor_js_getComputedStyle(JSContext *ctx, JSValueConst this_val, int argc,
                                          JSValueConst *argv);

static JSValue js_getComputedStyle(JSContext *ctx, JSValueConst this_val, int argc,
                                   JSValueConst *argv)
{
    return yetty_ylexbor_js_getComputedStyle(ctx, this_val, argc, argv);
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

/* __ybIngestCSS(cssText): push a CSS string into the live libcss cascade.
 * Bridges CONSTRUCTABLE STYLESHEETS — `new CSSStyleSheet().replaceSync(css)` +
 * `document.adoptedStyleSheets = [sheet]` — which modern CSS-in-JS (YouTube's
 * kevlar: 16 adoptedStyleSheets uses in base.js) applies per component. Our
 * CSSStyleSheet was a no-op stub, so those rules — including the consent
 * lightbox's `:host{position:fixed;inset:0}` modal styling — never reached the
 * cascade and the lightbox collapsed to height 0 (in-flow, not a modal). Shady
 * DOM scopes these rules by class (`.style-scope.<tag>`), so a document-level
 * ingest is correct — the selectors self-scope. Marks the tree dirty so the next
 * relayout applies the new rules. */
static JSValue js_ingest_css(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) {
        return JS_UNDEFINED;
    }
    struct yetty_ylexbor *r = runtime_ylex_w(ctx);
    if (r == NULL) {
        return JS_UNDEFINED;
    }
    size_t len = 0;
    const char *css = JS_ToCStringLen(ctx, &len, argv[0]);
    if (css != NULL) {
        struct yetty_ycore_void_result res = yetty_ylexbor_add_css(r, css, len);
        if (YETTY_IS_ERR(res)) {
            yetty_ycore_error_destroy(res.error);
        }
        r->dom_dirty = 1;
        JS_FreeCString(ctx, css);
    }
    return JS_UNDEFINED;
}

/* window.innerWidth/innerHeight (and outerWidth/Height, which for a chromeless
 * embedded viewport are identical) report the LIVE engine viewport, so a later
 * yetty_ylexbor_set_viewport() is reflected without re-running any prelude. This
 * keeps the JS window viewport coherent with the libcss/media viewport and
 * documentElement.clientWidth — all derived from the one r->viewport_w/h. */
static JSValue js_window_inner_width_get(JSContext *ctx, JSValueConst this_val)
{
    (void)this_val;
    struct yetty_ylexbor *r = runtime_ylex_w(ctx);
    return JS_NewInt32(ctx, r ? r->viewport_w : 1024);
}

static JSValue js_window_inner_height_get(JSContext *ctx, JSValueConst this_val)
{
    (void)this_val;
    struct yetty_ylexbor *r = runtime_ylex_w(ctx);
    return JS_NewInt32(ctx, r ? r->viewport_h : 768);
}

/* Define an accessor (getter-only) property on obj. globalThis rejects
 * JS_SetPropertyFunctionList (see install_global_fn), so live window/screen
 * metrics are installed one at a time this way. */
static void define_getter(JSContext *ctx, JSValue obj, const char *name,
                          JSValue (*getter)(JSContext *, JSValueConst))
{
    JSAtom atom = JS_NewAtom(ctx, name);
    /* JS_NewCFunction2 reinterprets the callback per its cproto (JS_CFUNC_getter
     * => JSValue(*)(JSContext*, JSValueConst)); cast through the generic type. */
    JSValue getter_fn = JS_NewCFunction2(ctx, (JSCFunction *)getter, name, 0, JS_CFUNC_getter, 0);
    JS_DefinePropertyGetSet(ctx, obj, atom, getter_fn, JS_UNDEFINED,
                            JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
    JS_FreeAtom(ctx, atom);
}

/* location.assign/replace/reload and `location.href = …` all funnel here:
 * record the resolved target so the host can drive a real navigate() on the
 * next frame (a plain page has no other way to reload — YouTube's consent flow
 * calls location.reload() after saving the choice, and the old no-op stub left
 * it stuck on "Saving your choice" forever). */
static void location_request_navigation(JSContext *ctx, struct yetty_ylexbor *r, const char *url)
{
    (void)ctx;
    if (!r || !url || !*url) {
        return;
    }
    char *resolved = yetty_ylexbor_resolve_url(r, url);
    free(r->pending_navigation);
    r->pending_navigation = resolved ? resolved : strdup(url);
    r->pending_nav_reload = false;
}

static JSValue js_location_assign(JSContext *ctx, JSValueConst this_val, int argc,
                                  JSValueConst *argv)
{
    struct yetty_ylexbor *r = runtime_ylex_w(ctx);
    if (argc >= 1) {
        const char *url = JS_ToCString(ctx, argv[0]);
        if (url) {
            location_request_navigation(ctx, r, url);
            JS_SetPropertyStr(
                ctx, (JSValue)this_val, "__hrefValue",
                JS_NewString(ctx, (r && r->pending_navigation) ? r->pending_navigation : url));
            JS_FreeCString(ctx, url);
        }
    }
    return JS_UNDEFINED;
}

static JSValue js_location_reload(JSContext *ctx, JSValueConst this_val, int argc,
                                  JSValueConst *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    struct yetty_ylexbor *r = runtime_ylex_w(ctx);
    if (r && r->base_url) {
        free(r->pending_navigation);
        r->pending_navigation = strdup(r->base_url);
        /* A reload must re-fetch, not replay a cached response: the consent
		 * flow reloads after writing SOCS and the cached consent-walled page
		 * would otherwise loop forever. */
        r->pending_nav_reload = true;
    }
    return JS_UNDEFINED;
}

/* Form submission as a real navigating request. __ybFormNavigate(action,
 * method, body) resolves the action against the base URL and records it as the
 * pending navigation together with the HTTP method and form-encoded body, so
 * the host issues a genuine POST (or GET), follows redirects, and loads the
 * resulting document — replacing the page. This is what makes a consent
 * "Accept all" (POST to consent.google.com/save) set the cookie and land back
 * on the site, instead of a background fetch that saves the cookie but never
 * navigates. */
static JSValue js_form_navigate(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    struct yetty_ylexbor *r = runtime_ylex_w(ctx);
    if (!r || argc < 1) {
        return JS_UNDEFINED;
    }
    const char *action = JS_ToCString(ctx, argv[0]);
    if (!action) {
        return JS_UNDEFINED;
    }
    ydebug("form_navigate: action=%s argc=%d", action, argc);
    char *resolved = yetty_ylexbor_resolve_url(r, action);
    free(r->pending_navigation);
    r->pending_navigation = resolved ? resolved : strdup(action);
    r->pending_nav_reload = false;
    JS_FreeCString(ctx, action);

    free(r->pending_nav_method);
    r->pending_nav_method = NULL;
    free(r->pending_nav_body);
    r->pending_nav_body = NULL;
    r->pending_nav_body_len = 0;

    if (argc >= 2 && JS_IsString(argv[1])) {
        const char *method = JS_ToCString(ctx, argv[1]);
        if (method && *method) {
            r->pending_nav_method = strdup(method);
        }
        JS_FreeCString(ctx, method);
    }
    if (argc >= 3 && JS_IsString(argv[2])) {
        size_t body_len = 0;
        const char *body = JS_ToCStringLen(ctx, &body_len, argv[2]);
        if (body) {
            char *copy = malloc(body_len + 1);
            if (copy) {
                memcpy(copy, body, body_len);
                copy[body_len] = '\0';
                r->pending_nav_body = copy;
                r->pending_nav_body_len = body_len;
            }
            JS_FreeCString(ctx, body);
        }
    }
    return JS_UNDEFINED;
}

static JSValue js_location_href_get(JSContext *ctx, JSValueConst this_val)
{
    return JS_GetPropertyStr(ctx, this_val, "__hrefValue");
}

static JSValue js_location_href_set(JSContext *ctx, JSValueConst this_val, JSValueConst val)
{
    struct yetty_ylexbor *r = runtime_ylex_w(ctx);
    const char *url = JS_ToCString(ctx, val);
    if (url) {
        location_request_navigation(ctx, r, url);
        JS_SetPropertyStr(
            ctx, (JSValue)this_val, "__hrefValue",
            JS_NewString(ctx, (r && r->pending_navigation) ? r->pending_navigation : url));
        JS_FreeCString(ctx, url);
    }
    return JS_UNDEFINED;
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

    /* Live window viewport metrics (replaces the old hardcoded 1024/768 in the
     * stubs prelude) — always reflect r->viewport_w/h. */
    define_getter(ctx, global, "innerWidth", js_window_inner_width_get);
    define_getter(ctx, global, "innerHeight", js_window_inner_height_get);
    define_getter(ctx, global, "outerWidth", js_window_inner_width_get);
    define_getter(ctx, global, "outerHeight", js_window_inner_height_get);

    /* screen — no separate physical screen for a chromeless embed, so track the
     * viewport too (coherent with innerWidth rather than a stale 1024/768). */
    JSValue screen = JS_NewObject(ctx);
    define_getter(ctx, screen, "width", js_window_inner_width_get);
    define_getter(ctx, screen, "height", js_window_inner_height_get);
    define_getter(ctx, screen, "availWidth", js_window_inner_width_get);
    define_getter(ctx, screen, "availHeight", js_window_inner_height_get);
    JS_SetPropertyStr(ctx, screen, "colorDepth", JS_NewInt32(ctx, 24));
    JS_SetPropertyStr(ctx, screen, "pixelDepth", JS_NewInt32(ctx, 24));
    JS_SetPropertyStr(ctx, global, "screen", screen);

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
    /* Backing store for the href accessor defined below. */
    JS_SetPropertyStr(ctx, loc, "__hrefValue", JS_NewString(ctx, href));
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
    JS_SetPropertyStr(ctx, loc, "assign", JS_NewCFunction(ctx, js_location_assign, "assign", 1));
    JS_SetPropertyStr(ctx, loc, "replace", JS_NewCFunction(ctx, js_location_assign, "replace", 1));
    JS_SetPropertyStr(ctx, loc, "reload", JS_NewCFunction(ctx, js_location_reload, "reload", 0));
    /* href is an accessor: reads return the stored value; writes record a
	 * navigation for the host to act on. */
    {
        JSAtom href_atom = JS_NewAtom(ctx, "href");
        JSValue href_get = JS_NewCFunction2(ctx, (JSCFunction *)js_location_href_get, "href", 0,
                                            JS_CFUNC_getter, 0);
        JSValue href_set = JS_NewCFunction2(ctx, (JSCFunction *)js_location_href_set, "href", 1,
                                            JS_CFUNC_setter, 0);
        JS_DefinePropertyGetSet(ctx, loc, href_atom, href_get, href_set,
                                JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
        JS_FreeAtom(ctx, href_atom);
    }
    {
        const char *tostr = "l => { l.toString = () => l.href; return l; }";
        JSValue li = JS_Eval(ctx, tostr, strlen(tostr), "<loc>", JS_EVAL_TYPE_GLOBAL);
        if (!JS_IsException(li)) {
            JSValue r2 = JS_Call(ctx, li, JS_UNDEFINED, 1, (JSValueConst[]){loc});
            JS_FreeValue(ctx, r2);
        }
        JS_FreeValue(ctx, li);
    }
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
        /* document.readyState starts at "loading": page scripts run while the
		 * document is still loading, and frameworks branch on this — e.g. a
		 * boot pass that runs immediately when readyState is already
		 * "complete" but otherwise DEFERS to a readystatechange listener so
		 * it sees the fully-built app. The script runner advances the state
		 * to interactive/complete (firing readystatechange) after the script
		 * pass — see yetty_ylexbor_js_run_all_scripts. */
        JS_SetPropertyStr(ctx, doc, "readyState", JS_NewString(ctx, "loading"));
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
    /* Constructable-stylesheet bridge (see js_ingest_css). */
    install_global_fn(ctx, global, "__ybIngestCSS", js_ingest_css, 1);
    /* Form-submission navigation bridge (see js_form_navigate). */
    install_global_fn(ctx, global, "__ybFormNavigate", js_form_navigate, 3);

    /* Misc stubs — feature detection rarely actually USES these,
	 * just checks they exist. */
    const char *stubs =
        /* Shady-DOM composition control. Some apps (YouTube's Polymer build)
         * set window.ShadyDOM = {force:true, preferPerformance:true,
         * noPatch:true}. Those two performance flags tell Shady-DOM to skip
         * physically composing each shadow tree into the light DOM, on the
         * assumption that a native shadow-DOM renderer will draw the shadow
         * trees. This engine renders the native tree only, so without physical
         * composition the stamped content is invisible. Intercept the
         * assignment and clear both flags so Shady-DOM composes shadow content
         * into the native DOM (its classic non-shadow-browser mode). */
        "(function(){ var shadyOpts; Object.defineProperty(globalThis,'ShadyDOM',{"
        "  configurable:true,"
        "  get:function(){ return shadyOpts; },"
        "  set:function(v){ if(v && typeof v==='object'){ v.preferPerformance=false; } "
        "    shadyOpts=v; } }); })();"
        "globalThis.Worker          = function(){ this.postMessage = ()=>{}; "
        "this.terminate=()=>{}; this.addEventListener=()=>{}; };"
        "globalThis.SharedWorker    = function(){ this.port = { postMessage:()=>{}, "
        "addEventListener:()=>{} }; };"
        "globalThis.BroadcastChannel= function(){ this.postMessage = ()=>{}; this.close=()=>{}; "
        "this.addEventListener=()=>{}; };"
        /* AbortSignal must carry throwIfAborted() (the kevlar app reads
		 * signal.throwIfAborted and threw "cannot read property 'throwIfAborted'
		 * of undefined" without it) plus the static abort/timeout/any factories,
		 * each returning a signal with the same method surface. */
        "globalThis.AbortSignal = (function(){"
        "  function make(){ return { aborted:false, reason:undefined, onabort:null,"
        "    throwIfAborted:function(){ if(this.aborted) throw (this.reason||new "
        "Error('aborted')); },"
        "    addEventListener:function(){}, removeEventListener:function(){},"
        "    dispatchEvent:function(){ return false; } }; }"
        "  var S=function(){ return make(); };"
        "  S.abort=function(r){ var s=make(); s.aborted=true; s.reason=r; return s; };"
        "  S.timeout=function(){ return make(); };"
        "  S.any=function(){ return make(); };"
        "  S._make=make; return S; })();"
        "globalThis.AbortController = function(){ this.signal = globalThis.AbortSignal._make();"
        "  this.abort = function(r){ this.signal.aborted=true; this.signal.reason=r;"
        "    if(typeof this.signal.onabort==='function'){ try{ "
        "this.signal.onabort({type:'abort'}); }"
        "    catch(e){} } }; };"
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
        "this.cancelable=!!(init&&init.cancelable); this.composed=!!(init&&init.composed); "
        "this.defaultPrevented=false; "
        /* Empty until dispatchEvent installs the real path — matching the
		 * spec's "composedPath() returns [] outside dispatch". */
        "this.composedPath=()=>this.__composedPath||[]; "
        "this.preventDefault=()=>{this.defaultPrevented=true;}; "
        /* Propagation flags read by the dispatchEvent bubbling walk. */
        "this.stopPropagation=()=>{this.__propagationStopped=true;}; "
        "this.stopImmediatePropagation=()=>{this.__propagationStopped=true;"
        "this.__immediateStopped=true;}; };"
        /* CustomEvent/MessageEvent must CAPTURE the base Event constructor at
		 * definition time. Shadow-DOM polyfills wrap globalThis.Event with a
		 * function that ignores `this` and returns a fresh object — a dynamic
		 * `globalThis.Event.call(this, …)` inside these subclasses then
		 * initializes nothing, leaving every instance without a .type, and
		 * every dispatched event silently matches zero listeners. */
        "globalThis.CustomEvent = (function(BaseEvent){ return function(t, init){ "
        "BaseEvent.call(this,t,init); "
        "this.detail = init? init.detail : null; }; })(globalThis.Event);"
        "globalThis.MessageEvent = (function(BaseEvent){ return function(t, init){ "
        "BaseEvent.call(this,t,init); "
        "this.data = init?init.data:null; this.origin = init?init.origin||'':''; }; "
        "})(globalThis.Event);"
        /* MutationObserver is a real implementation installed by dom_install
		 * (js_dom_install runs first); do NOT stub it here or the working one
		 * gets clobbered by a no-op. */
        /* A real-enough IntersectionObserver. Lazy-loaders gate content on
			 * isIntersecting=true — most importantly YouTube thumbnails: yt-image sets
			 * the <img> src only from an IntersectionObserver "viewport entered"
			 * callback (jet()->eLO()->observe()); a no-op stub leaves every thumbnail
			 * blank. We report isIntersecting=true ONLY for elements that have a real
			 * laid-out size AND fall inside the viewport (+rootMargin), re-checking a
			 * few times to catch elements laid out after observe(), then giving up.
			 * Requiring real size is what keeps off-screen / not-yet-laid-out lazy
			 * content (e.g. GitHub's below-the-fold images) from all reporting visible
			 * and flooding the loader. */
        "globalThis.IntersectionObserver = function(cb, opts){ var self=this; var margin=0;"
        "  try{ if(opts&&opts.rootMargin){ var mm=(''+opts.rootMargin).match(/-?\\d+/);"
        "    if(mm) margin=Math.abs(parseInt(mm[0],10)); } }catch(e){}"
        "  var watched=[]; var ticking=false;"
        "  var rectOf=function(el){ try{ return el.getBoundingClientRect(); }"
        "    catch(e){ return {top:0,left:0,right:0,bottom:0,width:0,height:0}; } };"
        "  var fire=function(el, rc, vis){ var vw=window.innerWidth||1280, "
        "vh=window.innerHeight||800;"
        "    cb([{ target: el, isIntersecting: vis, intersectionRatio: vis?1:0,"
        "          boundingClientRect: rc, intersectionRect: rc,"
        "          rootBounds: {top:0,left:0,right:vw,bottom:vh,width:vw,height:vh}, time: 0 }], "
        "self); };"
        "  var check=function(){ ticking=false; var vw=window.innerWidth||1280, "
        "vh=window.innerHeight||800;"
        "    for(var i=watched.length-1;i>=0;i--){ var w=watched[i]; w.n++;"
        "      var rc=rectOf(w.el);"
        "      var vis=(rc.width>0)&&(rc.height>0)&&(rc.bottom>=-margin)&&(rc.top<=vh+margin)"
        "        &&(rc.right>=-margin)&&(rc.left<=vw+margin);"
        "      if(vis){ fire(w.el, rc, true); watched.splice(i,1); }"
        "      else if(w.n>=8){ watched.splice(i,1); } }"
        "    if(watched.length && !ticking){ ticking=true; setTimeout(check, 250); } };"
        "  this.observe=function(el){ if(!el) return; watched.push({el:el, n:0});"
        "    if(!ticking){ ticking=true; setTimeout(check, 50); } };"
        "  this.unobserve=function(el){ for(var i=0;i<watched.length;i++){ if(watched[i].el===el){ "
        "watched.splice(i,1); break; } } };"
        "  this.disconnect=function(){ watched.length=0; };"
        "  this.takeRecords=function(){ return []; }; };"
        /* Real ResizeObserver delivery. Responsive components (YouTube's
		 * ytd-rich-grid-renderer) read clientWidth at dataChanged — before the
		 * first layout gives a nonzero box — and depend on a post-layout resize
		 * notification to retry. A no-op stub strands them at containerWidth=0
		 * forever. Delivery is async (a re-check timer, never synchronous from
		 * observe(), never re-entering the layout pass), compares each target's
		 * border box against its last-reported size, batches all changed targets
		 * into one callback per observer, and isolates callback exceptions. This
		 * mirrors the IntersectionObserver re-check loop above. */
        "globalThis.ResizeObserver = function(cb){"
        "  var self=this; var watched=[]; var ticking=false;"
        "  var boxOf=function(el){ try{ var r=el.getBoundingClientRect(); return "
        "{w:r.width,h:r.height}; }"
        "    catch(e){ return {w:0,h:0}; } };"
        "  var check=function(){ ticking=false; var changed=[];"
        "    for(var i=0;i<watched.length;i++){ var wt=watched[i]; var b=boxOf(wt.el);"
        "      if(b.w!==wt.w || b.h!==wt.h){ wt.w=b.w; wt.h=b.h;"
        "        if(globalThis.__RO_DEBUG){try{console.log('[RO] fire <'+(wt.el&&wt.el.tagName)+'> "
        "'+b.w+'x'+b.h);}catch(_){}}"
        "        changed.push({target: wt.el,"
        "          contentRect:{x:0,y:0,top:0,left:0,width:b.w,height:b.h,right:b.w,bottom:b.h},"
        "          borderBoxSize:[{inlineSize:b.w,blockSize:b.h}],"
        "          contentBoxSize:[{inlineSize:b.w,blockSize:b.h}]}); } }"
        "    if(changed.length){ try{ cb(changed, self); }"
        "      catch(e){ try{console.error('[RO] callback', (e&&e.name||'Error')+': "
        "'+(e&&e.message));}catch(_){}} }"
        "    if(watched.length && !ticking){ ticking=true; setTimeout(check, 300); } };"
        "  this.observe=function(el){ if(!el) return;"
        "    for(var i=0;i<watched.length;i++){ if(watched[i].el===el) return; }"
        "    watched.push({el:el, w:-1, h:-1});"
        "    if(globalThis.__RO_DEBUG){try{var dr=boxOf(el);console.log('[RO] observe "
        "<'+(el&&el.tagName)+'> '+dr.w+'x'+dr.h);}catch(_){}}"
        "    if(!ticking){ ticking=true; setTimeout(check, 50); } };"
        "  this.unobserve=function(el){ for(var i=0;i<watched.length;i++){ if(watched[i].el===el){ "
        "watched.splice(i,1); break; } } };"
        "  this.disconnect=function(){ watched.length=0; }; };"
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
        /* innerWidth/innerHeight/outerWidth/outerHeight and screen.{width,height,
         * availWidth,availHeight} are installed from C as LIVE getters over the
         * engine viewport (r->viewport_w/h) in yetty_ylexbor_js_web_install — do
         * NOT assign them here or a later set_viewport() would be masked by these
         * stale constants (and the assignment would clobber the getters). */
        /* EventTarget — base class many libs `class X extends
		 * EventTarget` against. Mirrors addEventListener etc. */
        /* dom_install already wired a real EventTarget whose prototype carries
		 * the native addEventListener/dispatchEvent the Shady-DOM polyfill
		 * captures — keep it if present; only fall back to this generic stub
		 * when the DOM bindings are absent. */
        "globalThis.EventTarget = globalThis.EventTarget || function(){"
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
        /* Element/Node/Document are wired by dom_install with real prototype
		 * chains (Element.prototype -> Node.prototype -> EventTarget.prototype);
		 * keep those, fall back to bare stubs only without the DOM bindings. */
        "globalThis.Element     = globalThis.Element  || function(){};"
        "globalThis.Node        = globalThis.Node     || function(){};"
        "globalThis.Document    = globalThis.Document || function(){};"
        "globalThis.HTMLDocument= function(){};"
        /* Chain HTMLElement onto the real Element.prototype so element methods
		 * and the interface hierarchy are inherited by `class X extends
		 * HTMLElement` custom elements. */
        "try{ Object.setPrototypeOf(globalThis.HTMLElement.prototype, "
        "globalThis.Element.prototype); "
        "}catch(e){}"
        /* Canvas 2D context stub — the web-animations polyfill creates a
		 * <canvas> and calls getContext('2d') to normalise CSS colors; without
		 * it the whole polyfill threw "not a function". Non-drawing stub: it
		 * stores fillStyle and no-ops the drawing surface. */
        "try{ if(globalThis.Element && !globalThis.Element.prototype.getContext){"
        "  globalThis.Element.prototype.getContext = function(){ var fs='#000000';"
        "    return { canvas:this, get fillStyle(){return fs;}, set fillStyle(v){fs=v;},"
        "      strokeStyle:'#000', globalAlpha:1, lineWidth:1, font:'10px sans-serif',"
        "      fillRect:function(){}, clearRect:function(){}, strokeRect:function(){},"
        "      beginPath:function(){}, closePath:function(){}, moveTo:function(){}, "
        "lineTo:function(){},"
        "      arc:function(){}, rect:function(){}, fill:function(){}, stroke:function(){}, "
        "clip:function(){},"
        "      save:function(){}, restore:function(){}, scale:function(){}, rotate:function(){},"
        "      translate:function(){}, transform:function(){}, setTransform:function(){}, "
        "drawImage:function(){},"
        "      putImageData:function(){}, fillText:function(){}, strokeText:function(){},"
        "      createLinearGradient:function(){return {addColorStop:function(){}};},"
        "      createRadialGradient:function(){return {addColorStop:function(){}};},"
        "      createPattern:function(){return {};},"
        "      getImageData:function(x,y,w,h){w=w||1;h=h||1;"
        "        return {data:new Uint8ClampedArray(w*h*4), width:w, height:h};},"
        "      measureText:function(t){return {width:(t?String(t).length:0)*6};} };"
        "  }; } }catch(e){}"
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
        /* MessageChannel — a REAL entangled port pair with async delivery.
		 * Task schedulers (e.g. YouTube's scheduler.js) pump their macrotask
		 * queue through a MessageChannel port; a silent no-op stub swallows
		 * every scheduled task — fetch continuations, SPA navigation — with
		 * no error anywhere. Delivery order: onmessage first, then
		 * addEventListener('message') listeners. Messages posted before the
		 * port is started queue up; assigning onmessage auto-starts (spec). */
        "(function(){"
        "function deliver(port){"
        "  if(!port._started||port._closed)return;"
        "  while(port._queue.length){"
        "    var data=port._queue.shift();"
        "    var ev={type:'message',data:data,origin:'',ports:[],source:null,"
        "      target:port,currentTarget:port,bubbles:false,cancelable:false,"
        "      stopPropagation:function(){},stopImmediatePropagation:function(){},"
        "      preventDefault:function(){}};"
        "    try{ if(typeof port._onmessage==='function') port._onmessage(ev); }"
        "    catch(e){ try{console.error('MessagePort onmessage:',e&&e.message);}catch(_){}}"
        "    for(var i=0;i<port._listeners.length;i++){"
        "      try{ port._listeners[i].call(port,ev); }"
        "      catch(e){ try{console.error('MessagePort listener:',e&&e.message);}catch(_){}}"
        "    }"
        "  }"
        "}"
        "function makePort(){"
        "  var port={_peer:null,_queue:[],_listeners:[],_onmessage:null,"
        "    _started:false,_closed:false,onmessageerror:null,"
        "    start:function(){ this._started=true; var p=this;"
        "      setTimeout(function(){deliver(p);},0); },"
        "    close:function(){ this._closed=true; },"
        "    addEventListener:function(t,f){"
        "      if(t==='message'&&typeof f==='function') this._listeners.push(f); },"
        "    removeEventListener:function(t,f){"
        "      var i=this._listeners.indexOf(f); if(i>=0)this._listeners.splice(i,1); },"
        "    postMessage:function(data){"
        "      var peer=this._peer;"
        "      if(!peer||peer._closed)return;"
        "      peer._queue.push(data);"
        "      setTimeout(function(){deliver(peer);},0); }};"
        "  Object.defineProperty(port,'onmessage',{"
        "    get:function(){return this._onmessage;},"
        "    set:function(f){ this._onmessage=f; this.start(); }});"
        "  return port;"
        "}"
        "globalThis.MessageChannel=function(){"
        "  this.port1=makePort(); this.port2=makePort();"
        "  this.port1._peer=this.port2; this.port2._peer=this.port1;"
        "};"
        "})();"
        "globalThis.MessagePort   = function(){};"
        /* window.postMessage — async 'message' event at the window itself
		 * (the same-window mailbox pattern; also a scheduler macrotask
		 * primitive). window.onmessage fires first, then the registered
		 * 'message' listeners via the normal window dispatch path. */
        "globalThis.postMessage = function(data,origin){"
        "  setTimeout(function(){"
        "    var ev={type:'message',data:data,"
        "      origin:(globalThis.location&&globalThis.location.origin)||'',"
        "      source:globalThis,ports:[],bubbles:false,cancelable:false,"
        "      target:globalThis,currentTarget:globalThis,"
        "      stopPropagation:function(){},stopImmediatePropagation:function(){},"
        "      preventDefault:function(){}};"
        "    try{ if(typeof globalThis.onmessage==='function') globalThis.onmessage(ev); }"
        "    catch(e){ try{console.error('window.onmessage:',e&&e.message);}catch(_){}}"
        "    try{ globalThis.dispatchEvent(ev); }catch(e){}"
        "  },0);"
        "};"
        "globalThis.Worklet       = function(){ this.addModule=()=>Promise.resolve(); };"
        "globalThis.LinkPreloadManager = function(){};"
        /* NodeFilter constants — the Shady-DOM (webcomponents) polyfill and DOM
		 * traversal reference them; absence was a hard ReferenceError. */
        "globalThis.NodeFilter    = { SHOW_ALL:0xFFFFFFFF, SHOW_ELEMENT:1, SHOW_ATTRIBUTE:2, "
        "SHOW_TEXT:4, SHOW_CDATA_SECTION:8, SHOW_ENTITY_REFERENCE:16, SHOW_ENTITY:32, "
        "SHOW_PROCESSING_INSTRUCTION:64, SHOW_COMMENT:128, SHOW_DOCUMENT:256, "
        "SHOW_DOCUMENT_TYPE:512, SHOW_DOCUMENT_FRAGMENT:1024, SHOW_NOTATION:2048, "
        "FILTER_ACCEPT:1, FILTER_REJECT:2, FILTER_SKIP:3 };"
        /* A REAL TreeWalker/NodeIterator over the live DOM (node wrappers expose
		 * firstChild/nextSibling/parentNode/nodeType). The Shady-DOM polyfill
		 * calls document.createTreeWalker(document, SHOW_ALL, …) to scan the tree
		 * — a no-op walker made it patch nothing and the app crashed. */
        "(function(){"
        "function accept(node,show,filter){var nt=node.nodeType||0;"
        "if(nt>=1&&nt<=32&&!(((show>>>0))&(1<<(nt-1))))return 3;"
        "if(!filter)return 1;var fn=(typeof filter==='function')?filter:(filter&&filter.acceptNode?"
        "function(n){return filter.acceptNode(n);}:null);"
        "if(!fn)return 1;var v=fn(node);return (typeof v==='number')?v:1;}"
        "function TW(root,show,filter){this.root=root;this.whatToShow=(show>>>0)||0xFFFFFFFF;"
        "this.filter=filter||null;this.currentNode=root;}"
        "TW.prototype.nextNode=function(){var node=this.currentNode,r=1;"
        "for(;;){while(r!==2&&node&&node.firstChild){node=node.firstChild;"
        "r=accept(node,this.whatToShow,this.filter);"
        "if(r===1){this.currentNode=node;return node;}}"
        "var nx=null,n=node;while(n&&n!==this.root){if(n.nextSibling){nx=n.nextSibling;break;}"
        "n=n.parentNode;}if(!nx)return null;node=nx;r=accept(node,this.whatToShow,this.filter);"
        "if(r===1){this.currentNode=node;return node;}}};"
        "TW.prototype.parentNode=function(){var n=this.currentNode;"
        "while(n&&n!==this.root){n=n.parentNode;"
        "if(n&&accept(n,this.whatToShow,this.filter)===1){this.currentNode=n;return n;}}return "
        "null;};"
        "TW.prototype.firstChild=function(){var n=this.currentNode&&this.currentNode.firstChild;"
        "while(n){var r=accept(n,this.whatToShow,this.filter);"
        "if(r===1){this.currentNode=n;return n;}if(r===3&&n.firstChild){n=n.firstChild;continue;}"
        "n=n.nextSibling;}return null;};"
        "TW.prototype.nextSibling=function(){var n=this.currentNode&&this.currentNode.nextSibling;"
        "while(n){if(accept(n,this.whatToShow,this.filter)===1){this.currentNode=n;return n;}"
        "n=n.nextSibling;}return null;};"
        "TW.prototype.previousNode=function(){return null;};"
        "TW.prototype.lastChild=function(){return null;};"
        "TW.prototype.previousSibling=function(){return null;};"
        "globalThis.TreeWalker=TW;"
        "function NI(root,show,filter){this._tw=new TW(root,show,filter);this.root=root;"
        "this.referenceNode=root;this.whatToShow=(show>>>0)||0xFFFFFFFF;this."
        "pointerBeforeReferenceNode=true;}"
        "NI.prototype.nextNode=function(){var "
        "n=this._tw.nextNode();if(n)this.referenceNode=n;return n;};"
        "NI.prototype.previousNode=function(){return null;};NI.prototype.detach=function(){};"
        "globalThis.NodeIterator=NI;"
        "if(typeof document!=='undefined'){"
        "document.createTreeWalker=function(root,show,filter){"
        "return new TW(root,show===undefined?0xFFFFFFFF:show,filter);};"
        "document.createNodeIterator=function(root,show,filter){"
        "return new NI(root,show===undefined?0xFFFFFFFF:show,filter);};}"
        "})();"
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
        /* DocumentFragment is installed by the DOM layer with a real prototype
		 * chained on Node.prototype (so fragments are not `instanceof Element`);
		 * keep that if present and only stub it when the DOM layer is absent. */
        "globalThis.DocumentFragment  = globalThis.DocumentFragment || function(){};"
        "globalThis.Text              = function(){};"
        "globalThis.Comment           = function(){};"
        /* Rest of the Node hierarchy. The Shady-DOM polyfill and kevlar iterate
		 * a list of these constructors patching `.prototype.insertBefore`; a
		 * missing one (CharacterData was first) threw "cannot read property
		 * 'prototype' of undefined". Chain the character-data types onto the real
		 * Node.prototype so the patched methods reach them. */
        "globalThis.CharacterData     = function(){};"
        "globalThis.ProcessingInstruction = function(){};"
        "globalThis.DocumentType      = function(){};"
        "globalThis.CDATASection      = function(){};"
        "globalThis.HTMLSlotElement   = function(){};"
        "globalThis.StaticRange       = function(){};"
        "globalThis.DOMImplementation = function(){};"
        "try{ if(globalThis.Node){"
        "  Object.setPrototypeOf(globalThis.CharacterData.prototype, globalThis.Node.prototype);"
        "  Object.setPrototypeOf(globalThis.Text.prototype, globalThis.CharacterData.prototype);"
        "  Object.setPrototypeOf(globalThis.Comment.prototype, globalThis.CharacterData.prototype);"
        "  Object.setPrototypeOf(globalThis.DocumentFragment.prototype, globalThis.Node.prototype);"
        "} }catch(e){}"
        /* nodeType / document-position constants. The spec exposes these on both
         * the Node interface object AND Node.prototype (so `Node.ELEMENT_NODE`
         * and `someNode.ELEMENT_NODE` both resolve). Without them every
         * `node.nodeType === Node.ELEMENT_NODE` comparison in web code tests
         * against undefined and silently takes the wrong branch — this is what
         * broke YouTube's Shady-DOM polyfill wholesale (its ShadowRoot.nodeType
         * getter returns Node.DOCUMENT_FRAGMENT_NODE, which was undefined). */
        "try{ if(globalThis.Node){ var NT={"
        "ELEMENT_NODE:1,ATTRIBUTE_NODE:2,TEXT_NODE:3,CDATA_SECTION_NODE:4,"
        "ENTITY_REFERENCE_NODE:5,ENTITY_NODE:6,PROCESSING_INSTRUCTION_NODE:7,"
        "COMMENT_NODE:8,DOCUMENT_NODE:9,DOCUMENT_TYPE_NODE:10,"
        "DOCUMENT_FRAGMENT_NODE:11,NOTATION_NODE:12,"
        "DOCUMENT_POSITION_DISCONNECTED:1,DOCUMENT_POSITION_PRECEDING:2,"
        "DOCUMENT_POSITION_FOLLOWING:4,DOCUMENT_POSITION_CONTAINS:8,"
        "DOCUMENT_POSITION_CONTAINED_BY:16,DOCUMENT_POSITION_IMPLEMENTATION_SPECIFIC:32};"
        "for(var ntk in NT){ try{ globalThis.Node[ntk]=NT[ntk];"
        "globalThis.Node.prototype[ntk]=NT[ntk]; }catch(e){} } } }catch(e){}"
        /* Native-ish Shadow DOM. Providing Element.prototype.attachShadow +
         * Node.prototype.getRootNode makes the Shady-DOM polyfill's feature
         * probe (`w.Rb=!(!attachShadow||!getRootNode)`, `inUse=force||!Rb`)
         * report native support, so it stays dormant instead of patching the
         * whole DOM (which it can't fully drive against this engine). The
         * shadow root is a light view over the host: mutations delegate to the
         * host element, so stamped template content lands in the host's child
         * list and the layout engine renders it. Slot distribution is not
         * modeled — the composed tree is approximated by the host's children. */
        "try{ if(globalThis.Element && globalThis.Node){"
        "  globalThis.Element.prototype.attachShadow=function(init){"
        "    var host=this;"
        "    if(host.__shadowRoot) return host.__shadowRoot;"
        "    var root={host:host, mode:(init&&init.mode)||'open', nodeType:11,"
        "      ownerDocument:(typeof document!=='undefined'?document:null),"
        "      appendChild:function(n){return host.appendChild(n);},"
        "      insertBefore:function(n,r){return host.insertBefore(n,r);},"
        "      removeChild:function(n){return host.removeChild(n);},"
        "      replaceChild:function(a,b){return host.replaceChild(a,b);},"
        "      append:function(){return host.append.apply(host,arguments);},"
        "      prepend:function(){return host.prepend.apply(host,arguments);},"
        "      querySelector:function(s){return host.querySelector(s);},"
        "      querySelectorAll:function(s){return host.querySelectorAll(s);},"
        "      getElementById:function(id){return host.querySelector('#'+id);},"
        "      addEventListener:function(){return host.addEventListener.apply(host,arguments);},"
        "      removeEventListener:function(){return "
        "host.removeEventListener.apply(host,arguments);},"
        "      dispatchEvent:function(e){return host.dispatchEvent(e);},"
        "      cloneNode:function(d){return host.cloneNode(d);},"
        "      contains:function(n){return host.contains?host.contains(n):false;},"
        "      getRootNode:function(){return this;}};"
        "    Object.defineProperty(root,'childNodes',{get:function(){return host.childNodes;}});"
        "    Object.defineProperty(root,'children',{get:function(){return host.children;}});"
        "    Object.defineProperty(root,'firstChild',{get:function(){return host.firstChild;}});"
        "    Object.defineProperty(root,'lastChild',{get:function(){return host.lastChild;}});"
        "    Object.defineProperty(root,'firstElementChild',{get:function(){return "
        "host.firstElementChild;}});"
        "    Object.defineProperty(root,'innerHTML',{get:function(){return "
        "host.innerHTML;},set:function(v){host.innerHTML=v;}});"
        "    Object.defineProperty(root,'textContent',{get:function(){return "
        "host.textContent;},set:function(v){host.textContent=v;}});"
        "    Object.defineProperty(root,'activeElement',{get:function(){return null;}});"
        "    "
        "try{Object.defineProperty(host,'shadowRoot',{value:root,configurable:true});}catch(e){"
        "host.shadowRoot=root;}"
        "    host.__shadowRoot=root;"
        "    return root;"
        "  };"
        "  if(!globalThis.Node.prototype.getRootNode){"
        "    globalThis.Node.prototype.getRootNode=function(opts){var "
        "n=this;while(n&&n.parentNode){n=n.parentNode;}return n||this;};"
        "  }"
        "} }catch(e){}"
        "globalThis.Attr              = function(){};"
        "globalThis.NodeList          = function(){};"
        "globalThis.HTMLCollection    = function(){};"
        "globalThis.DOMTokenList      = function(){};"
        "globalThis.NamedNodeMap      = function(){};"
        "globalThis.CSSStyleDeclaration= function(){};"
        /* Constructable stylesheet: `new CSSStyleSheet(); s.replaceSync(css);
			 * document.adoptedStyleSheets=[s]`. replace/replaceSync push the CSS
			 * into the cascade via __ybIngestCSS (see the C side). Without this the
			 * sheet was inert and adopted component styles never applied. */
        "globalThis.CSSStyleSheet     = function(){ this._css=''; this.cssRules=[]; this.rules=[]; "
        "};"
        /* Methods on the PROTOTYPE — frameworks feature-detect
			 * `CSSStyleSheet.prototype.replaceSync` before using constructable
			 * sheets; instance-only methods fail that check and the site silently
			 * falls back to a path that never applies the styles here. */
        "globalThis.CSSStyleSheet.prototype.replaceSync=function(t){ this._css=''+t;"
        "  try{globalThis.__ybIngestCSS(this._css);}catch(e){} return this; };"
        "globalThis.CSSStyleSheet.prototype.replace=function(t){ this._css=''+t;"
        "  try{globalThis.__ybIngestCSS(this._css);}catch(e){} return Promise.resolve(this); };"
        "globalThis.CSSStyleSheet.prototype.insertRule=function(rule){"
        "  try{globalThis.__ybIngestCSS(''+rule);}catch(e){} return 0; };"
        "globalThis.CSSStyleSheet.prototype.deleteRule=function(){};"
        /* adoptedStyleSheets: a settable array. The CSS is already ingested at
			 * replaceSync time (class-scoped, so document-level is correct), so the
			 * assignment itself only needs to not throw and stay iterable for the
			 * `[...document.adoptedStyleSheets, sheet]` idiom. */
        "try{ if(globalThis.document && globalThis.document.adoptedStyleSheets===undefined)"
        "  globalThis.document.adoptedStyleSheets=[]; }catch(e){}"
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
        /* Window IS an EventTarget, and the Shady-DOM polyfill captures the
		 * native event methods off Window.prototype then reads them back as
		 * window.__shady_native_addEventListener — so window must inherit from
		 * Window.prototype, which must own addEventListener/etc. */
        "try{"
        "  globalThis.Window.prototype.addEventListener = globalThis.addEventListener;"
        "  globalThis.Window.prototype.removeEventListener = globalThis.removeEventListener;"
        "  globalThis.Window.prototype.dispatchEvent = globalThis.dispatchEvent;"
        "  if(globalThis.EventTarget)"
        "    Object.setPrototypeOf(globalThis.Window.prototype, globalThis.EventTarget.prototype);"
        "  Object.setPrototypeOf(globalThis, globalThis.Window.prototype);"
        "}catch(e){}"
        "globalThis.WindowProxy       = function(){};"
        "globalThis.Headers           = function(init){ const m={}; this.headerMap=m; "
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
        /* connectedCallback fires once per element, only while it is in the
		 * document tree. Kept separate from upgrade() so an element created
		 * detached and inserted later connects at insertion time, matching the
		 * custom-element-reaction lifecycle every web-component framework
		 * relies on. */
        "  const connectIfNeeded=(el)=>{ if(el.__ceConnected)return;"
        "    if(el.isConnected!==true)return; el.__ceConnected=true;"
        "    try{ if(typeof el.connectedCallback==='function') el.connectedCallback(); }"
        "    catch(e){ try{console.error('ce connect <'+(el&&el.tagName)+'>', "
        "(e&&e.name||'Error')+': '+(e&&e.message),"
        "      '|', (e&&e.stack||'').split('\\n').slice(0,3).join(' <- '));}catch(_){}} };"
        "  const upgrade=(el,c)=>{ try{ if(el.__ceUpgraded)return; el.__ceUpgraded=true;"
        "    Object.setPrototypeOf(el,c.prototype);"
        /* Run the real constructor with `el` as `this` (brands private fields)
		 * via the construction stack; super() returns `el`. */
        "    globalThis.__ceStack.push(el);"
        "    try{ Reflect.construct(c,[],c); }"
        "    finally{ if(globalThis.__ceStack[globalThis.__ceStack.length-1]===el)"
        "             globalThis.__ceStack.pop(); }"
        /* Upgrade-algorithm step: enqueue attributeChangedCallback for each
			 * PRE-EXISTING attribute named in observedAttributes. Parse-time
			 * attributes never fired a reaction (the element was not yet upgraded),
			 * so frameworks that deserialize attribute -> property here (Polymer:
			 * `modal` attr -> modal=true -> withBackdrop -> backdrop) would silently
			 * drop that config. Runs after the constructor (property system ready)
			 * and before connectedCallback, per the spec order. */
        "    try{ var oa=c.observedAttributes;"
        "      if(oa&&oa.length&&typeof el.attributeChangedCallback==='function'){"
        "        for(var ai=0;ai<oa.length;ai++){ var an=oa[ai];"
        "          if(el.hasAttribute&&el.hasAttribute(an)){"
        "            try{ el.attributeChangedCallback(an,null,el.getAttribute(an),null); "
        "}catch(e){}"
        "          } } } }catch(e){}"
        "    connectIfNeeded(el);"
        /* Reconcile own data properties that shadow a prototype accessor. A value
			 * written to the raw element before its class prototype was installed
			 * becomes an own DATA property; after setPrototypeOf it shadows the class's
			 * forwarding/data accessor, so later reads and writes never reach the
			 * component instance. YouTube's Wiz yt-formatted-string is a wrapper whose
			 * `text` accessor forwards to an inner instance (inst.text); a `text` bound
			 * before upgrade lands as an own data property that shadows it, so inst.text
			 * stays undefined and the node renders blank (the consent dialog title and
			 * Reject/Accept labels). The prototype and instance now exist, so route each
			 * such value back through the accessor's setter (the standard pre-upgrade
			 * property reconciliation). */
        "    try{ var reconcileProto=Object.getPrototypeOf(el);"
        "      Object.getOwnPropertyNames(el).forEach(function(propName){"
        "        if(propName.charAt(0)==='_')return;"
        "        var ownDesc=Object.getOwnPropertyDescriptor(el,propName);"
        "        if(!ownDesc||!('value' in "
        "ownDesc)||!ownDesc.writable||!ownDesc.configurable)return;"
        "        var accessorDesc=null, protoCursor=reconcileProto;"
        "        while(protoCursor){ "
        "accessorDesc=Object.getOwnPropertyDescriptor(protoCursor,propName);"
        "          if(accessorDesc)break; protoCursor=Object.getPrototypeOf(protoCursor); }"
        "        if(accessorDesc && typeof accessorDesc.set==='function'){"
        "          var stashedValue=el[propName];"
        "          try{ delete el[propName]; el[propName]=stashedValue; }catch(reassignErr){} }"
        "      }); }catch(reconcileErr){}"
        "  }catch(e){ try{console.error('ce upgrade <'+(el&&el.tagName)+'>',"
        "    (e&&e.name||'Error')+': '+(e&&e.message), '|', "
        "(e&&e.stack||'').split('\\n').slice(0,3).join(' <- "
        "'));}catch(_){}} };"
        /* Upgrade a raw defined element, or connect one already upgraded. */
        "  const upgradeOrConnect=(el)=>{ if(!el||el.nodeType!==1)return;"
        "    var tag=el.tagName?(''+el.tagName).toLowerCase():''; if(tag.indexOf('-')<=0)return;"
        "    var c=m[tag]; if(!c)return;"
        "    if(!el.__ceUpgraded) upgrade(el,c); else connectIfNeeded(el); };"
        /* Collect matching elements by a manual deep walk. querySelectorAll skips
			 * DocumentFragment children, but a framework stamps a component's
			 * template into a fragment that becomes the host's child; a renderer
			 * defined AFTER it was stamped must still be upgraded retroactively, and
			 * an insertion of fragment-nested content must connect its descendants.
			 * Descend into element (1) and document-fragment (11) children plus any
			 * shadow root; `tag==='*'` matches every element. A <template>'s content
			 * is reached via `.content`, not childNodes, so inert template contents
			 * are correctly left un-upgraded. */
        "  const deepEls=(root,tag)=>{ var out=[], seen=new Set(), st=[root];"
        "    while(st.length){ var el=st.pop(); if(!el||seen.has(el))continue; seen.add(el);"
        "      if(el.nodeType===1 && el.tagName && (tag==='*' || "
        "(''+el.tagName).toLowerCase()===tag)) out.push(el);"
        "      var kids=el.childNodes; if(kids){ for(var i=0;i<kids.length;i++){ var k=kids[i];"
        "        if(k&&(k.nodeType===1||k.nodeType===11)) st.push(k); } }"
        "      if(el.shadowRoot&&el.shadowRoot.childNodes){ var sr=el.shadowRoot.childNodes;"
        "        for(var j=0;j<sr.length;j++){ var s=sr[j]; "
        "if(s&&(s.nodeType===1||s.nodeType===11)) st.push(s); } } }"
        "    return out; };"
        "  this.define=(n,c)=>{ m[n]=c; chain();"
        /* Arm synchronous custom-element reactions in the DOM insertion paths
			 * the first time any element is defined. */
        "    try{ globalThis.__ceActivate && globalThis.__ceActivate(); }catch(e){}"
        /* Upgrade + connect any element of this tag already in the tree.
		 * attachShadow delegates to the host, so shadow-stamped content is
		 * the host's real children and querySelectorAll reaches it. */
        "    try{ var els=deepEls(document.documentElement,n); for(var i=0;i<els.length;i++)"
        "      upgradeOrConnect(els[i]);"
        "    }catch(e){}"
        "    if(defers[n]){ defers[n].forEach(r=>r()); delete defers[n]; } };"
        "  this.get=n=>m[n];"
        /* Deliver an observed-attribute change to an already-upgraded custom
			 * element (fired from setAttribute/removeAttribute in the DOM layer).
			 * Native custom elements get attributeChangedCallback on every runtime
			 * observed-attribute change; a framework toggling one (Polymer clearing
			 * a bound disable-upgrade so an icon can finally upgrade) depends on it.
			 * Gated on __ceUpgraded + observedAttributes so plain and un-upgraded
			 * elements pay nothing. */
        "  this.__attributeChanged=(el,name,oldVal,newVal)=>{ try{"
        "    if(!el||el.nodeType!==1||!el.__ceUpgraded)return;"
        "    var c=m[(''+el.tagName).toLowerCase()]; if(!c)return;"
        "    var oa=c.observedAttributes; if(!oa||oa.indexOf(name)<0)return;"
        "    if(typeof el.attributeChangedCallback==='function')"
        "      el.attributeChangedCallback(name,oldVal,newVal,null);"
        "  }catch(e){} };"
        "  this.whenDefined=n=>{ if(m[n])return Promise.resolve(m[n]);"
        "    return new Promise(res=>{ (defers[n]=defers[n]||[]).push(()=>res(m[n])); }); };"
        "  this.upgrade=root=>{ var base=root||document.documentElement;"
        "    for(var n in m){ try{ var els=deepEls(base,n);"
        "    for(var i=0;i<els.length;i++) upgradeOrConnect(els[i]); }catch(e){} } };"
        /* Connect every defined custom element in a freshly-inserted subtree,
		 * in tree order. Driven by the reaction observer below on each
		 * insertion. */
        /* Upgrade a single freshly-created element (document.createElement of a
			 * defined custom tag). Runs the constructor + observed-attribute
			 * reactions synchronously so the returned element already carries its
			 * prototype and methods, as the spec's "create an element" requires;
			 * connectedCallback still waits for insertion (connectIfNeeded is a
			 * no-op while detached). Without this a manager that does
			 * createElement(tag) then calls a method on the result (iron-overlay
			 * creating its backdrop and calling backdrop.prepare()) hits undefined. */
        /* createElement of a defined custom tag: install the prototype so the
			 * returned object exposes its methods immediately (callers routinely
			 * invoke a method on the result — iron-overlay creating its backdrop
			 * then calling backdrop.prepare()). We do NOT run the constructor here:
			 * legacy Polymer runs its full property init in connectedCallback, and
			 * running the constructor now would make it run TWICE (once here, once at
			 * connect), the second pass re-creating __data and discarding any property
			 * the caller set on the detached element (e.g. backdrop.opened=true, which
			 * then self-removes). Deferring the constructor to insertion keeps a single
			 * init; properties set pre-connect are preserved via Polymer's __dataProto.
			 * __ceUpgraded is left unset so the normal connect path upgrades fully. */
        /* createElement of a defined custom tag. We must expose the element's
			 * methods immediately (callers invoke them on the detached result — the
			 * iron-overlay manager creates its backdrop then calls backdrop.prepare()),
			 * but we must NOT construct the instance here: lazy component systems
			 * (Polymer under ShadyDOM, YouTube's Wiz wrapper) construct the instance
			 * again in connectedCallback, and that second construction re-creates the
			 * element's data store, discarding any property set on the detached element
			 * (backdrop.opened=true -> reverts to the false default -> the backdrop
			 * self-removes on attach). So: (1) finalize the CLASS once — a throwaway
			 * construction publishes the methods + property accessors onto the shared
			 * prototype; (2) give the returned element that prototype WITHOUT
			 * constructing it. Its pre-connect property writes land in Polymer's
			 * __dataProto and are applied by the single connect-time construction. */
        /* createElement of a defined custom tag: construct the element in place
			 * (constructor + observed-attribute reactions via upgradeOrConnect), so
			 * the returned object exposes its methods — callers invoke them on the
			 * detached result, e.g. iron-overlay creating its backdrop then calling
			 * backdrop.prepare(). connectedCallback is NOT fired here: it must wait
			 * for real insertion. An earlier "eager connect" that fired it on the
			 * detached element poisoned every framework attach-state flag — legacy
			 * Polymer's connectedCallback unconditionally sets isAttached=true, and
			 * YouTube's page-manager attachPage() skips its appendChild for a page
			 * whose isAttached is already set, so the created ytd-browse was never
			 * inserted and the whole page stayed blank. Pre-connect property writes
			 * survive via the shadowed-accessor reconciliation in upgrade(). */
        "  this.__upgradeOne=(el)=>{ try{ upgradeOrConnect(el); }catch(e){} };"
        "  this.__connectSubtree=(node)=>{ try{"
        "    var all=deepEls(node,'*'); for(var i=0;i<all.length;i++) upgradeOrConnect(all[i]);"
        "  }catch(e){ try{console.error('ce subtree <'+(node&&(node.tagName||node.nodeName))+'> "
        "nt='+(node&&node.nodeType),"
        "    (e&&e.name||'Error')+': '+(e&&e.message), '|', "
        "(e&&e.stack||'').split('\\n').slice(0,3).join(' <- '));}catch(_){}} };"
        "  this.__disconnectSubtree=(node)=>{ try{"
        "    var visit=(el)=>{ if(el&&el.nodeType===1&&el.__ceConnected){ el.__ceConnected=false;"
        "      try{ if(typeof el.disconnectedCallback==='function') el.disconnectedCallback(); }"
        "      catch(e){} } };"
        "    var all=deepEls(node,'*'); for(var i=0;i<all.length;i++) visit(all[i]);"
        "  }catch(e){} }; };"
        "globalThis.customElements = new globalThis.CustomElementRegistry();"
        /* Custom-element reactions are driven synchronously from the DOM
		 * insertion primitives (see ce_react_connect in the dom layer): on every
		 * insertion into the live document the inserted subtree is handed to
		 * __connectSubtree, so connectedCallback fires in-line with template
		 * stamping -- the ordering Polymer/Lit/Stencil require. */
        "globalThis.Image       = function(){ this.src=''; this.onload=null; this.onerror=null; "
        "this.addEventListener=()=>{}; };"
        /* Audio: no playback, but the constructor must EXIST. YouTube's
			 * masthead render constructs `new Audio(...)` mid-stamp; with the
			 * global missing the ReferenceError was swallowed by the page's
			 * component-wrapper error handling and the masthead (and everything
			 * inside it: search box, topbar buttons, consent renderer) silently
			 * froze at the server-rendered skeleton. */
        "globalThis.Audio       = function(src){ this.src=src||''; this.volume=1; "
        "this.muted=false; "
        "this.paused=true; this.currentTime=0; this.duration=NaN; this.autoplay=false; "
        "this.loop=false; this.preload='auto'; this.onload=null; this.onerror=null; "
        "this.play=()=>Promise.resolve(); this.pause=()=>{}; this.load=()=>{}; "
        "this.canPlayType=()=>''; this.addEventListener=()=>{}; this.removeEventListener=()=>{}; "
        "this.dispatchEvent=()=>false; };"
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
        /* navigator.sendBeacon: fire-and-forget POST that MUST return true. Its
			 * absence made callers throw `sendBeacon is not a function`; when that
			 * throw happens inside a framework's swallow-all wrapper (e.g. YouTube's
			 * consent-save handler) the operation silently "fails" — the consent
			 * lightbox showed "An error occurred while saving your choice" and never
			 * dismissed, covering the page. sendBeacon does not expose the response
			 * to the caller, so best-effort POST + unconditional true is spec-faithful. */
        "if(globalThis.navigator && !globalThis.navigator.sendBeacon){"
        "  globalThis.navigator.sendBeacon = function(url, data){"
        "    try{ globalThis.fetch(url, { method:'POST', keepalive:true,"
        "      body:(data===undefined||data===null)?undefined:"
        "        (typeof data==='string'?data:data) }); }catch(e){}"
        "    return true; }; }"
        /* HTMLFormElement.submit / requestSubmit + the native submit-button
			 * default action all funnel through __ybSubmitForm, which serializes
			 * the successful controls and hands the host a real NAVIGATING request
			 * via __ybFormNavigate (POST body or GET query). A prior version did a
			 * background fetch() that captured the Set-Cookie but never navigated —
			 * so a consent "Accept all" (POST to consent.google.com/save, 302 back
			 * to the site) saved the cookie yet left the page unchanged. Navigating
			 * for real loads the redirected document, so the choice takes effect. */
        "(function(){ try{"
        "  var submitter=null;"
        "  globalThis.__ybSubmitForm=function(form, btn){ try{"
        "    if(!form) return;"
        "    var action=(form.getAttribute&&form.getAttribute('action'))||globalThis.location.href;"
        "    if(!action) action=globalThis.location.href;"
        "    var method=((form.getAttribute&&form.getAttribute('method'))||'GET').toUpperCase();"
        "    var ctrls=form.querySelectorAll?form.querySelectorAll('input,select,textarea'):[];"
        "    var parts=[];"
        "    var add=function(n,v){ parts.push(encodeURIComponent(n)+'='+encodeURIComponent(v)); };"
        "    for(var i=0;i<ctrls.length;i++){ var c=ctrls[i];"
        "      var name=c.getAttribute&&c.getAttribute('name'); if(!name)continue;"
        "      if(c.disabled)continue;"
        "      var tag=(c.tagName||'').toLowerCase();"
        "      var type=((c.getAttribute&&c.getAttribute('type'))||'').toLowerCase();"
        /* skip unchecked checkbox/radio and non-submitting button types */
        "      if(tag==='input'&&(type==='checkbox'||type==='radio')&&!c.checked)continue;"
        "      "
        "if(tag==='input'&&(type==='submit'||type==='button'||type==='image'||type==='reset'||type="
        "=='file'))continue;"
        "      if(tag==='button')continue;"
        "      var "
        "val=(c.value!==undefined&&c.value!==null)?c.value:((c.getAttribute&&c.getAttribute('value'"
        "))||'');"
        "      add(name,val); }"
        /* the activated submit button contributes its own name=value */
        "    if(btn){ var bn=btn.getAttribute&&btn.getAttribute('name');"
        "      if(bn){ var "
        "bv=(btn.value!==undefined&&btn.value!==null)?btn.value:((btn.getAttribute&&btn."
        "getAttribute('value'))||''); add(bn,bv); } }"
        "    var body=parts.join('&');"
        "    if(method==='GET'){ "
        "globalThis.__ybFormNavigate(action.split('#')[0]+(action.indexOf('?')<0?'?':'&')+body,'"
        "GET',''); }"
        "    else{ globalThis.__ybFormNavigate(action,method,body); }"
        "  }catch(e){ try{ console.error('__ybSubmitForm: '+(e&&e.message?e.message:e)); "
        "}catch(x){} } };"
        "  var ep=Object.getPrototypeOf(document.createElement('form'));"
        "  if(ep){"
        "    ep.submit=function(){ globalThis.__ybSubmitForm(this,null); };"
        "    ep.requestSubmit=function(b){ globalThis.__ybSubmitForm(this,b||null); }; } "
        "}catch(e){} })();"
        /* Page text input. The host has no way to type into a page <input> —
		 * key_cb/char_cb only fed the address bar. These helpers give the
		 * standalone shell a focused-element model + text/edit dispatch so a
		 * click focuses a field (YouTube's search box) and typed characters land
		 * in it, firing the `input` events the page's suggestion/search logic
		 * listens for, and Enter submits. Caret is end-of-value (covers the
		 * dominant "type a query" case; full caret editing is future work). */
        "(function(){"
        "  var isTextField=function(e){ if(!e||e.nodeType!==1)return false;"
        "    var tag=(e.tagName||'').toLowerCase();"
        "    if(tag==='textarea')return true;"
        "    if(e.getAttribute&&e.getAttribute('contenteditable')==='true')return true;"
        "    if(tag!=='input')return false;"
        "    var t=((e.getAttribute&&e.getAttribute('type'))||'text').toLowerCase();"
        "    return ['text','search','email','url','tel','password','number',''].indexOf(t)>=0; };"
        "  var fire=function(e,type,bubbles){ try{ var ev=new Event(type,{bubbles:!!bubbles});"
        "    e.dispatchEvent(ev); }catch(x){} };"
        /* Called from dispatch_click. Focus the nearest text field ancestor of
		 * the clicked element, or blur the current one. Returns true if a field
		 * took focus. */
        "  globalThis.__ybFocusHit=function(el){ var e=el, hops=0, field=null;"
        /* Walk up from the clicked element. At each level, take the element
			 * itself if it is a text field, else the FIRST text field inside it.
			 * The descendant search is what makes a click on a wrapped input work:
			 * Material/outlined text fields (Google sign-in) layer a label/notch
			 * overlay over the <input>, so the click target is the wrapper, not the
			 * input. Bounded to a few hops so we don't grab an unrelated input from
			 * a distant ancestor. */
        "    while(e&&e.nodeType===1&&hops<6){"
        "      if(isTextField(e)){ field=e; break; }"
        "      if(e.querySelector){ var q=e.querySelector('input,textarea');"
        "        if(q&&isTextField(q)){ field=q; break; } }"
        "      e=e.parentNode; hops++; }"
        "    if(field){"
        "      if(globalThis.__ybPageFocus!==field){"
        "        if(globalThis.__ybPageFocus){ fire(globalThis.__ybPageFocus,'blur',false); }"
        "        globalThis.__ybPageFocus=field;"
        "        try{ if(typeof field.focus==='function')field.focus(); }catch(x){}"
        "        fire(field,'focus',false); fire(field,'focusin',true); }"
        "      return true; }"
        "    if(globalThis.__ybPageFocus){ fire(globalThis.__ybPageFocus,'blur',false);"
        "      fire(globalThis.__ybPageFocus,'focusout',true); globalThis.__ybPageFocus=null; }"
        "    return false; };"
        "  globalThis.__ybHasPageFocus=function(){ var e=globalThis.__ybPageFocus;"
        "    return !!(e && e.isConnected!==false); };"
        /* Insert typed text at the end of the focused field's value. */
        "  globalThis.__ybInsertText=function(text){ var e=globalThis.__ybPageFocus;"
        "    if(!e||!text)return; var tag=(e.tagName||'').toLowerCase();"
        "    try{ if(tag==='input'||tag==='textarea'){ e.value=(e.value||'')+text; }"
        "      else{ e.appendChild(document.createTextNode(text)); } }catch(x){}"
        "    fire(e,'beforeinput',true); fire(e,'input',true); };"
        /* Editing / navigation keys. keyName is the DOM key name. */
        "  globalThis.__ybEditKey=function(keyName){ var e=globalThis.__ybPageFocus;"
        "    if(!e)return false; var tag=(e.tagName||'').toLowerCase();"
        "    var isField=(tag==='input'||tag==='textarea');"
        "    try{ var kd=new Event('keydown',{bubbles:true}); kd.key=keyName;"
        "      kd.code=keyName; e.dispatchEvent(kd); }catch(x){}"
        "    if(keyName==='Backspace'){ if(isField){ var v=e.value||''; e.value=v.slice(0,-1);"
        "        fire(e,'beforeinput',true); fire(e,'input',true); } }"
        "    else if(keyName==='Enter'){"
        "      var f=e; while(f&&(f.tagName||'').toLowerCase()!=='form')f=f.parentNode;"
        "      if(f&&globalThis.__ybSubmitForm){ globalThis.__ybSubmitForm(f,null); }"
        "      fire(e,'change',true); }"
        "    try{ var ku=new Event('keyup',{bubbles:true}); ku.key=keyName; e.dispatchEvent(ku); "
        "}catch(x){}"
        "    return true; };"
        "})();"
        "globalThis.HTMLCanvasElement = function(){};"
        "globalThis.OffscreenCanvas = function(){ this.getContext = () => null; };"
        /* requestIdleCallback must hand the callback an IdleDeadline; code that
         * reads `deadline.timeRemaining()` / `.didTimeout` throws on undefined,
         * which silently kills idle-scheduled work (framework hydration/render
         * chunks are frequently idle-scheduled). */
        "globalThis.requestIdleCallback = (cb) => setTimeout(function(){"
        "  cb({didTimeout:false, timeRemaining:function(){return 50;}}); }, 1);"
        "globalThis.cancelIdleCallback = clearTimeout;"
        /* rAF callbacks receive a DOMHighResTimeStamp; without it, `t - last`
         * math yields NaN and breaks scheduler loops. Wrap the native rAF to
         * inject one. */
        "if(globalThis.requestAnimationFrame){ var nativeRaf=globalThis.requestAnimationFrame;"
        "  globalThis.requestAnimationFrame = (cb) => nativeRaf(function(){"
        "    cb((globalThis.performance&&performance.now)?performance.now():0); }); }"
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

    /* Intl polyfill. QuickJS-ng ships no Internationalization API (it needs
	 * ICU), so `Intl` is undefined and any bundle that touches it dies with a
	 * ReferenceError — on nytimes that killed the MAIN bundle. This provides a
	 * functional en-US approximation of the common surface (DateTimeFormat,
	 * NumberFormat, Collator, PluralRules, RelativeTimeFormat, ListFormat,
	 * Locale, Segmenter) so such bundles run. Not locale-accurate. */
    static const char *intl_polyfill =
        "if (typeof globalThis.Intl === 'undefined') { (function(){"
        "  var I = {};"
        "  var MON=['January','February','March','April','May','June','July','August',"
        "           'September','October','November','December'];"
        "  var DAY=['Sunday','Monday','Tuesday','Wednesday','Thursday','Friday','Saturday'];"
        "  function pad(n){ return (n<10?'0':'')+n; }"
        "  I.getCanonicalLocales=function(l){ if(l==null)return[]; "
        "    return Array.isArray(l)?l.map(String):[String(l)]; };"
        "  I.supportedValuesOf=function(){ return []; };"
        /* DateTimeFormat */
        "  function DTF(loc,opt){ if(!(this instanceof DTF))return new DTF(loc,opt);"
        "    this._o=opt||{}; this._l=Array.isArray(loc)?loc[0]:(loc||'en-US'); }"
        "  DTF.prototype.resolvedOptions=function(){ var o=this._o; return {locale:this._l||'en-US',"
        "    calendar:'gregory',numberingSystem:'latn',timeZone:o.timeZone||'UTC',year:o.year,"
        "    month:o.month,day:o.day,hour:o.hour,minute:o.minute,second:o.second}; };"
        "  DTF.prototype.format=function(d){ d=(d==null)?new Date():(d instanceof Date?d:new Date(d));"
        "    if(isNaN(d.getTime()))return'Invalid Date'; var o=this._o;"
        "    var y=d.getFullYear(),mo=d.getMonth(),da=d.getDate(),h=d.getHours(),"
        "        mi=d.getMinutes(),se=d.getSeconds();"
        "    var hasDate=!!(o.year||o.month||o.day||o.dateStyle||o.weekday);"
        "    var hasTime=(o.hour!=null||o.minute!=null||o.second!=null||o.timeStyle!=null);"
        "    if(!hasDate&&!hasTime){ return MON[mo]+' '+da+', '+y; }"
        "    var out=[];"
        "    if(o.weekday){ out.push(DAY[d.getDay()]); }"
        "    if(hasDate){ var mn=(o.month==='short')?MON[mo].slice(0,3)"
        "      :((o.month==='numeric')?(mo+1):((o.month==='2-digit')?pad(mo+1):MON[mo]));"
        "      var dp=(o.day==='2-digit')?pad(da):da; out.push(mn+' '+dp+(o.year?', '+y:'')); }"
        "    if(hasTime){ var hr=(o.hour12===false)?h:((h%12)||12);"
        "      var t=hr+':'+pad(mi)+(o.second!=null?':'+pad(se):'');"
        "      if(o.hour12!==false)t+=' '+(h<12?'AM':'PM'); out.push(t); }"
        "    return out.join(o.weekday?', ':' '); };"
        "  DTF.prototype.formatToParts=function(d){ return [{type:'literal',value:this.format(d)}]; };"
        "  DTF.prototype.formatRange=function(a,b){ return this.format(a)+' \\u2013 '+this.format(b); };"
        "  DTF.prototype.formatRangeToParts=function(a,b){"
        "    return [{type:'literal',value:this.formatRange(a,b)}]; };"
        "  DTF.supportedLocalesOf=function(l){ return I.getCanonicalLocales(l); };"
        "  I.DateTimeFormat=DTF;"
        /* NumberFormat */
        "  function NF(loc,opt){ if(!(this instanceof NF))return new NF(loc,opt);"
        "    this._o=opt||{}; this._l=Array.isArray(loc)?loc[0]:(loc||'en-US'); }"
        "  NF.prototype.resolvedOptions=function(){ return Object.assign({locale:this._l||'en-US',"
        "    numberingSystem:'latn',style:'decimal'},this._o); };"
        "  NF.prototype.format=function(n){ n=Number(n); if(!isFinite(n))return String(n); var o=this._o;"
        "    if(o.style==='percent')n=n*100; var neg=n<0; n=Math.abs(n);"
        "    var maxF=(o.maximumFractionDigits!=null)?o.maximumFractionDigits"
        "      :((o.style==='currency')?2:((o.minimumFractionDigits!=null)?o.minimumFractionDigits:3));"
        "    var minF=(o.minimumFractionDigits!=null)?o.minimumFractionDigits"
        "      :((o.style==='currency')?2:0);"
        "    var fixed=n.toFixed(Math.min(20,Math.max(minF,maxF)));"
        "    var sp=fixed.split('.'); var ip=sp[0]; var fp=sp[1]||'';"
        "    while(fp.length>minF && fp.charAt(fp.length-1)==='0'){ fp=fp.slice(0,-1); }"
        "    if(o.useGrouping!==false){ ip=ip.replace(/\\B(?=(\\d{3})+(?!\\d))/g,','); }"
        "    var res=ip+(fp?('.'+fp):'');"
        "    if(o.style==='percent')res+='%';"
        "    if(o.style==='currency')res=((!o.currency||o.currency==='USD')?'$':(o.currency+'\\u00a0'))+res;"
        "    return (neg?'-':'')+res; };"
        "  NF.prototype.formatToParts=function(n){ return [{type:'literal',value:this.format(n)}]; };"
        "  NF.prototype.formatRange=function(a,b){ return this.format(a)+'\\u2013'+this.format(b); };"
        "  NF.supportedLocalesOf=function(l){ return I.getCanonicalLocales(l); };"
        "  I.NumberFormat=NF;"
        /* Collator */
        "  function COL(l,o){ if(!(this instanceof COL))return new COL(l,o); }"
        "  COL.prototype.compare=function(a,b){ a=String(a); b=String(b);"
        "    return a<b?-1:(a>b?1:0); };"
        "  COL.prototype.resolvedOptions=function(){ return {locale:'en-US'}; };"
        "  COL.supportedLocalesOf=function(l){ return I.getCanonicalLocales(l); };"
        "  I.Collator=COL;"
        /* PluralRules */
        "  function PR(l,o){ if(!(this instanceof PR))return new PR(l,o); this._o=o||{}; }"
        "  PR.prototype.select=function(n){ n=Number(n);"
        "    if(this._o.type==='ordinal'){ var a=n%10,b=n%100;"
        "      if(a===1&&b!==11)return'one'; if(a===2&&b!==12)return'two';"
        "      if(a===3&&b!==13)return'few'; return'other'; }"
        "    return n===1?'one':'other'; };"
        "  PR.prototype.resolvedOptions=function(){ return {locale:'en-US',type:this._o.type||'cardinal'}; };"
        "  PR.supportedLocalesOf=function(l){ return I.getCanonicalLocales(l); };"
        "  I.PluralRules=PR;"
        /* RelativeTimeFormat */
        "  function RTF(l,o){ if(!(this instanceof RTF))return new RTF(l,o); this._o=o||{}; }"
        "  RTF.prototype.format=function(v,unit){ v=Number(v); var u=String(unit).replace(/s$/,'');"
        "    var p=Math.abs(v)===1?u:u+'s'; if(v===0)return'now';"
        "    return v<0?(Math.abs(v)+' '+p+' ago'):('in '+v+' '+p); };"
        "  RTF.prototype.formatToParts=function(v,unit){"
        "    return [{type:'literal',value:this.format(v,unit)}]; };"
        "  RTF.prototype.resolvedOptions=function(){ return {locale:'en-US',"
        "    numeric:this._o.numeric||'always',style:this._o.style||'long'}; };"
        "  RTF.supportedLocalesOf=function(l){ return I.getCanonicalLocales(l); };"
        "  I.RelativeTimeFormat=RTF;"
        /* ListFormat */
        "  function LF(l,o){ if(!(this instanceof LF))return new LF(l,o); this._o=o||{}; }"
        "  LF.prototype.format=function(arr){ arr=Array.from(arr||[]); if(arr.length===0)return'';"
        "    if(arr.length===1)return String(arr[0]);"
        "    var conj=(this._o.type==='disjunction')?'or':'and';"
        "    return arr.slice(0,-1).join(', ')+(arr.length>2?', ':' ')+conj+' '+arr[arr.length-1]; };"
        "  LF.prototype.formatToParts=function(arr){ return [{type:'element',value:this.format(arr)}]; };"
        "  LF.prototype.resolvedOptions=function(){ return {locale:'en-US',"
        "    type:this._o.type||'conjunction',style:this._o.style||'long'}; };"
        "  LF.supportedLocalesOf=function(l){ return I.getCanonicalLocales(l); };"
        "  I.ListFormat=LF;"
        /* Locale */
        "  function LOC(tag,o){ if(!(this instanceof LOC))return new LOC(tag,o);"
        "    tag=String(tag||'en-US'); var p=tag.split('-'); this.baseName=tag;"
        "    this.language=p[0]||'en'; this.region=p[1]; this.maximize=function(){return this;};"
        "    this.minimize=function(){return this;}; this.toString=function(){return tag;}; }"
        "  I.Locale=LOC;"
        /* Segmenter */
        "  function SEG(l,o){ if(!(this instanceof SEG))return new SEG(l,o); this._o=o||{}; }"
        "  SEG.prototype.segment=function(s){ s=String(s); var g=this._o.granularity||'grapheme';"
        "    var arr=(g==='word')?s.split(/(\\s+)/).filter(function(x){return x.length;})"
        "      :((g==='sentence')?[s]:Array.from(s));"
        "    var segs=arr.map(function(x,i){return {segment:x,index:i,input:s,"
        "      isWordLike:/\\w/.test(x)};});"
        "    segs[Symbol.iterator]=function(){ var i=0; return {next:function(){"
        "      return i<segs.length?{value:segs[i++],done:false}:{value:undefined,done:true}; }}; };"
        "    return segs; };"
        "  SEG.prototype.resolvedOptions=function(){ return {locale:'en-US',"
        "    granularity:this._o.granularity||'grapheme'}; };"
        "  I.Segmenter=SEG;"
        "  globalThis.Intl=I;"
        "})(); }";
    JSValue intl_v =
        JS_Eval(ctx, intl_polyfill, strlen(intl_polyfill), "<intl-polyfill>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(intl_v)) {
        JSValue ex = JS_GetException(ctx);
        const char *m = JS_ToCString(ctx, ex);
        ydebug("js intl-polyfill: %s", m ? m : "?");
        if (m) {
            JS_FreeCString(ctx, m);
        }
        JS_FreeValue(ctx, ex);
    }
    JS_FreeValue(ctx, intl_v);

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
