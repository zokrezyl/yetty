/*
 * ylexbor-js-web — WebAPI bindings on top of QuickJS for the bits
 * github / gitlab / any modern SPA needs to even *boot*:
 *
 *   fetch / Response / XMLHttpRequest          — sync libcurl behind a
 *                                                 Promise (good enough
 *                                                 to wake an SPA up;
 *                                                 swap to async libcurl
 *                                                 multi later)
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

#include <yetty/ytrace/ytrace.h>

/* ===========================================================================
 * URL resolution + sync HTTP fetch.
 * ===========================================================================*/

char *yetty_ylexbor_resolve_url(struct yetty_ylexbor *r, const char *href)
{
    if (!href) {
        return NULL;
    }
    if (strncmp(href, "http://", 7) == 0 || strncmp(href, "https://", 8) == 0 ||
        strncmp(href, "data:", 5) == 0 || strncmp(href, "file://", 7) == 0) {
        return strdup(href);
    }
    if (!r->base_url) {
        return strdup(href); /* best effort */
    }

    /* Two cases: absolute path (starts with /) or relative. */
    if (href[0] == '/' && href[1] == '/') {
        /* protocol-relative — splice the base's protocol. */
        const char *p = strstr(r->base_url, "://");
        size_t plen = p ? (size_t)(p - r->base_url) : 4;
        size_t hl = strlen(href);
        char *out = malloc(plen + 1 + hl + 1);
        memcpy(out, r->base_url, plen);
        out[plen] = ':';
        memcpy(out + plen + 1, href, hl + 1);
        return out;
    }
    if (href[0] == '/') {
        /* absolute path — splice scheme://host/ */
        const char *p = strstr(r->base_url, "://");
        if (!p) {
            return strdup(href);
        }
        const char *slash = strchr(p + 3, '/');
        size_t prefix = slash ? (size_t)(slash - r->base_url) : strlen(r->base_url);
        size_t hl = strlen(href);
        char *out = malloc(prefix + hl + 1);
        memcpy(out, r->base_url, prefix);
        memcpy(out + prefix, href, hl + 1);
        return out;
    }
    /* relative — strip last path segment of base_url, append href */
    size_t blen = strlen(r->base_url);
    const char *base_end = r->base_url + blen;
    const char *slash = NULL;
    const char *p = strstr(r->base_url, "://");
    const char *path_start = p ? strchr(p + 3, '/') : r->base_url;
    if (path_start) {
        for (const char *q = base_end; q > path_start; q--) {
            if (*(q - 1) == '/') {
                slash = q - 1;
                break;
            }
        }
    }
    size_t prefix = slash ? (size_t)(slash - r->base_url + 1) : blen;
    size_t hl = strlen(href);
    char *out = malloc(prefix + hl + 1);
    memcpy(out, r->base_url, prefix);
    memcpy(out + prefix, href, hl + 1);
    return out;
}

#if YETTY_HAVE_CURL

struct fetch_buf {
    char *data;
    size_t size, cap;
};

/* Hard ceiling on a single fetched response. Without it, an endpoint that
 * streams without end (SSE / long-poll / analytics beacon) or ships a
 * gzip decompression bomb (curl auto-inflates under ACCEPT_ENCODING) drives
 * the doubling buffer below to many GB — a real CNN/github load grew it past
 * 25 GB and OOM-killed the process. 64 MiB is far above any HTML / CSS / JS /
 * image a page legitimately serves, so this only ever trips on a runaway. */
#define FETCH_MAX_RESPONSE (64u * 1024u * 1024u)

static size_t fetch_write_cb(char *p, size_t sz, size_t n, void *ud)
{
    struct fetch_buf *b = ud;
    size_t add = sz * n;
    /* Abort the transfer once the cap is exceeded. Returning a short count
	 * makes curl fail the easy handle with CURLE_WRITE_ERROR; the caller
	 * treats it as a failed fetch and moves on. */
    if (b->size + add > FETCH_MAX_RESPONSE) {
        return 0;
    }
    if (b->size + add + 1 > b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 16384;
        while (nc < b->size + add + 1) {
            nc *= 2;
        }
        char *np = realloc(b->data, nc);
        if (!np) {
            return 0;
        }
        b->data = np;
        b->cap = nc;
    }
    memcpy(b->data + b->size, p, add);
    b->size += add;
    return add;
}

char *yetty_ylexbor_http_get_referer(const char *url, const char *referer, size_t *out_len,
                                     long *out_status);

char *yetty_ylexbor_http_get(const char *url, size_t *out_len, long *out_status)
{
    return yetty_ylexbor_http_get_referer(url, NULL, out_len, out_status);
}

/* Process-wide shared connection / DNS / TLS-session cache. The image
 * fetch loop in ybrowser-paint.c makes ~30 sequential curl_easy_init()
 * calls to the same CDN host (upload.wikimedia.org); without a share
 * handle each call does a fresh TCP+TLS handshake (~100ms × 30 = ~3s
 * wasted). With the share, the second through Nth easy handle reuse
 * the live connection from the pool, dropping the per-image cost to
 * just the transfer time. Equivalent of Chrome's connection pool but
 * single-threaded sequential — true parallelism would need curl_multi
 * (deferred). Thread-safe via libcurl's CURL_LOCK_DATA_* — we add the
 * per-data-type lock callbacks because curl can be called from the
 * JS-worker thread for fetch()/XHR. */
static CURLSH *g_curl_share = NULL;
#include <pthread.h>
static pthread_once_t g_curl_share_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t g_curl_share_locks[CURL_LOCK_DATA_LAST];

static void share_lock_cb(CURL *h, curl_lock_data data, curl_lock_access access, void *ud)
{
    (void)h;
    (void)access;
    (void)ud;
    if (data < CURL_LOCK_DATA_LAST) {
        pthread_mutex_lock(&g_curl_share_locks[data]);
    }
}
static void share_unlock_cb(CURL *h, curl_lock_data data, void *ud)
{
    (void)h;
    (void)ud;
    if (data < CURL_LOCK_DATA_LAST) {
        pthread_mutex_unlock(&g_curl_share_locks[data]);
    }
}
static void share_init_once(void)
{
    for (int i = 0; i < CURL_LOCK_DATA_LAST; i++) {
        pthread_mutex_init(&g_curl_share_locks[i], NULL);
    }
    g_curl_share = curl_share_init();
    if (!g_curl_share) {
        return;
    }
    curl_share_setopt(g_curl_share, CURLSHOPT_LOCKFUNC, share_lock_cb);
    curl_share_setopt(g_curl_share, CURLSHOPT_UNLOCKFUNC, share_unlock_cb);
    /* Share the four caches that matter for sequential same-host
	 * fetches. CONNECT is the big one — keeps the TCP+TLS connection
	 * alive across easy handle lifetimes. DNS and SSL_SESSION are
	 * smaller wins but free. */
    curl_share_setopt(g_curl_share, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
    curl_share_setopt(g_curl_share, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
#ifdef CURL_LOCK_DATA_CONNECT
    curl_share_setopt(g_curl_share, CURLSHOPT_SHARE, CURL_LOCK_DATA_CONNECT);
#endif
    curl_share_setopt(g_curl_share, CURLSHOPT_SHARE, CURL_LOCK_DATA_COOKIE);
}

void *yetty_ylexbor_curl_share(void)
{
    pthread_once(&g_curl_share_once, share_init_once);
    return g_curl_share;
}

char *yetty_ylexbor_http_get_referer(const char *url, const char *referer, size_t *out_len,
                                     long *out_status)
{
    if (!url) {
        if (out_len) {
            *out_len = 0;
        }
        if (out_status) {
            *out_status = 0;
        }
        return NULL;
    }
    /* file:// — handle directly since libcurl is often built without
	 * the FILE protocol. Used heavily by the WPT integration runner
	 * to load `<script src=...>` siblings of the test page. */
    if (strncmp(url, "file://", 7) == 0) {
        const char *path = url + 7;
        FILE *f = fopen(path, "rb");
        if (!f) {
            if (out_len) {
                *out_len = 0;
            }
            if (out_status) {
                *out_status = 0;
            }
            return NULL;
        }
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz < 0) {
            fclose(f);
            if (out_status) {
                *out_status = 0;
            }
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
    CURL *c = curl_easy_init();
    if (!c) {
        return NULL;
    }
    /* Attach the process-wide share handle so this easy handle reuses
	 * the cached TCP+TLS connection, DNS resolution, and TLS session
	 * from previous calls. First-touch initialization is one-shot via
	 * pthread_once. */
    pthread_once(&g_curl_share_once, share_init_once);
    if (g_curl_share) {
        curl_easy_setopt(c, CURLOPT_SHARE, g_curl_share);
    }
    struct fetch_buf b = {0};
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 10L);

    /* HTTP version negotiation. Letting libcurl pick (the default) gives
	 * HTTP/2 over TLS via ALPN — fast on its own. For HTTP/3 we use the
	 * Alt-Svc cache: servers advertise H3 via the Alt-Svc response
	 * header on HTTP/2, libcurl persists the advert, and switches to
	 * H3 on the NEXT request to that origin. No timeout penalty for
	 * non-H3 hosts — unlike CURL_HTTP_VERSION_3 which forces H3 and
	 * cannot downgrade. Alt-Svc cache file lives next to the user's
	 * runtime dir; persists across runs so warm sessions skip the
	 * H2-discovery round-trip. */
#ifdef CURLOPT_ALTSVC
    {
        curl_version_info_data *vi = curl_version_info(CURLVERSION_NOW);
        (void)vi;
#ifdef CURL_VERSION_HTTP3
        if (vi && (vi->features & CURL_VERSION_HTTP3)) {
            /* Cache file: $XDG_CACHE_HOME/yetty/altsvc-cache or
			 * $HOME/.cache/yetty/altsvc-cache. Created on first
			 * advert, ignored on read failure. */
            static char altsvc_path[1024];
            if (altsvc_path[0] == '\0') {
                const char *xdg = getenv("XDG_CACHE_HOME");
                const char *home = getenv("HOME");
                if (xdg && *xdg) {
                    snprintf(altsvc_path, sizeof(altsvc_path), "%s/yetty/altsvc-cache", xdg);
                } else if (home && *home) {
                    snprintf(altsvc_path, sizeof(altsvc_path), "%s/.cache/yetty/altsvc-cache",
                             home);
                }
            }
            if (altsvc_path[0]) {
                curl_easy_setopt(c, CURLOPT_ALTSVC, altsvc_path);
                /* Restrict to h2→h3 and h3→h2 upgrades. */
#ifdef CURLALTSVC_H1
                curl_easy_setopt(c, CURLOPT_ALTSVC_CTRL,
                                 (long)(CURLALTSVC_H1 | CURLALTSVC_H2 | CURLALTSVC_H3));
#endif
            }
        }
#endif
    }
#endif
    /* Send a standard Chrome User-Agent so CDNs treat us as a real
	 * browser. Wikimedia's bot-throttling, news.google.com's image
	 * gate, and several CloudFlare WAFs all behave very differently
	 * for unidentified vs browser UAs. The env var lets ops override
	 * for cases where bot-identifying UAs are required by ToS. */
    const char *ua = getenv("YETTY_USER_AGENT");
    if (!ua || !*ua) {
        ua = "Mozilla/5.0 (X11; Linux x86_64) "
             "AppleWebKit/537.36 (KHTML, like Gecko) "
             "Chrome/120.0.0.0 Safari/537.36";
    }
    curl_easy_setopt(c, CURLOPT_USERAGENT, ua);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, fetch_write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &b);
    /* Browser-like header set: image-prioritised Accept, sec-fetch
	 * hints (Chrome/Firefox always send these), and a Referer when
	 * the caller knows the document URL — gstatic / Cloudflare WAF
	 * / many CDN image endpoints 404 or 403 on fetches that lack
	 * it. */
    struct curl_slist *headers = NULL;
    /* IMPORTANT: do NOT advertise webp/avif/apng — content-negotiating
	 * CDNs (notably Wikimedia) will serve those formats over PNG/JPEG
	 * when the URL itself ends in .png. We can't decode webp/avif/apng
	 * (no libwebp / libavif / libapng linked), so claiming support
	 * means every Wikimedia thumb comes back as a webp blob and our
	 * decoder rejects it. Restrict to formats we actually handle. */
    headers = curl_slist_append(
        headers, "Accept: image/png,image/jpeg,image/gif,image/svg+xml,image/*;q=0.5,*/*;q=0.1");
    headers = curl_slist_append(headers, "Accept-Language: en-US,en;q=0.9");
    headers = curl_slist_append(headers, "Sec-Fetch-Dest: image");
    headers = curl_slist_append(headers, "Sec-Fetch-Mode: no-cors");
    headers = curl_slist_append(headers, "Sec-Fetch-Site: cross-site");
    if (referer && *referer) {
        curl_easy_setopt(c, CURLOPT_REFERER, referer);
    }
    if (headers) {
        curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
    }
    double t_req = yetty_ylexbor_prof_now_ms();
    CURLcode rc = curl_easy_perform(c);
    long status = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    yetty_ylexbor_prof("    HTTP %.0fms status=%ld bytes=%zu rc=%d %.90s",
                       yetty_ylexbor_prof_now_ms() - t_req, status, b.size, (int)rc, url);
    if (headers) {
        curl_slist_free_all(headers);
    }
    curl_easy_cleanup(c);
    if (rc != CURLE_OK) {
        free(b.data);
        return NULL;
    }
    if (out_len) {
        *out_len = b.size;
    }
    if (out_status) {
        *out_status = status;
    }
    if (b.data) {
        b.data[b.size] = 0;
    }
    return b.data;
}

/* Configure one easy handle inside http_get_many with the same options
 * the sequential path uses. Extracted to avoid copy-paste drift. The
 * caller adds the curl_slist headers; we set everything else. */
static void multi_configure_easy(CURL *c, const char *url, const char *referer, struct fetch_buf *b,
                                 struct curl_slist *headers)
{
    pthread_once(&g_curl_share_once, share_init_once);
    if (g_curl_share) {
        curl_easy_setopt(c, CURLOPT_SHARE, g_curl_share);
    }
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 10L);
    const char *ua = getenv("YETTY_USER_AGENT");
    if (!ua || !*ua) {
        ua = "Mozilla/5.0 (X11; Linux x86_64) "
             "AppleWebKit/537.36 (KHTML, like Gecko) "
             "Chrome/120.0.0.0 Safari/537.36";
    }
    curl_easy_setopt(c, CURLOPT_USERAGENT, ua);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, fetch_write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, b);
    if (referer && *referer) {
        curl_easy_setopt(c, CURLOPT_REFERER, referer);
    }
    if (headers) {
        curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
    }
}

void yetty_ylexbor_http_get_many(const char *const *urls, int n, const char *referer,
                                 int concurrency, char **out_bodies, size_t *out_lens,
                                 long *out_status)
{
    if (n <= 0) {
        return;
    }
    if (concurrency < 1) {
        concurrency = 1;
    }
    if (concurrency > n) {
        concurrency = n;
    }
    yetty_ylexbor_prof("    http_get_many START n=%d conc=%d", n, concurrency);
    double t_many = yetty_ylexbor_prof_now_ms();

    /* Build the shared header list once — same set used by the
	 * sequential path. curl_multi shares this slist among handles. */
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(
        headers, "Accept: image/png,image/jpeg,image/gif,image/svg+xml,image/*;q=0.5,*/*;q=0.1");
    headers = curl_slist_append(headers, "Accept-Language: en-US,en;q=0.9");
    headers = curl_slist_append(headers, "Sec-Fetch-Dest: image");
    headers = curl_slist_append(headers, "Sec-Fetch-Mode: no-cors");
    headers = curl_slist_append(headers, "Sec-Fetch-Site: cross-site");

    /* Per-request scratch — one CURL*, one fetch_buf, one slot index.
	 * Use a tag pointer (CURLOPT_PRIVATE) to recover the slot when a
	 * handle finishes. */
    struct slot {
        CURL *c;
        struct fetch_buf b;
        int idx;
        int started;
        int done;
    };
    struct slot *slots = calloc((size_t)n, sizeof(*slots));
    if (!slots) {
        curl_slist_free_all(headers);
        return;
    }
    for (int i = 0; i < n; i++) {
        slots[i].idx = i;
        out_bodies[i] = NULL;
        out_lens[i] = 0;
        out_status[i] = 0;
    }

    CURLM *mh = curl_multi_init();
    if (!mh) {
        curl_slist_free_all(headers);
        free(slots);
        return;
    }
    /* HTTP/2 multiplexing — when the server speaks H2, all in-flight
	 * requests to the same origin share ONE TCP+TLS connection. This
	 * is the big win vs sequential easy-handle calls. */
    curl_multi_setopt(mh, CURLMOPT_PIPELINING, (long)CURLPIPE_MULTIPLEX);
    curl_multi_setopt(mh, CURLMOPT_MAX_TOTAL_CONNECTIONS, (long)concurrency);
    curl_multi_setopt(mh, CURLMOPT_MAX_HOST_CONNECTIONS, (long)concurrency);

    /* Prime the first `concurrency` requests. */
    int next_to_start = 0;
    int running_handles = 0;
    while (next_to_start < n && next_to_start < concurrency) {
        struct slot *s = &slots[next_to_start];
        s->c = curl_easy_init();
        if (!s->c) {
            next_to_start++;
            continue;
        }
        multi_configure_easy(s->c, urls[next_to_start], referer, &s->b, headers);
        curl_easy_setopt(s->c, CURLOPT_PRIVATE, s);
        curl_multi_add_handle(mh, s->c);
        s->started = 1;
        next_to_start++;
    }

    do {
        int numfds = 0;
        curl_multi_perform(mh, &running_handles);
        curl_multi_wait(mh, NULL, 0, 1000, &numfds);

        /* Drain finished messages. */
        int msgs_left = 0;
        CURLMsg *m;
        while ((m = curl_multi_info_read(mh, &msgs_left)) != NULL) {
            if (m->msg != CURLMSG_DONE) {
                continue;
            }
            struct slot *s = NULL;
            curl_easy_getinfo(m->easy_handle, CURLINFO_PRIVATE, &s);
            if (!s) {
                continue;
            }
            long status = 0;
            curl_easy_getinfo(m->easy_handle, CURLINFO_RESPONSE_CODE, &status);
            if (m->data.result == CURLE_OK) {
                if (s->b.data) {
                    s->b.data[s->b.size] = 0;
                }
                out_bodies[s->idx] = s->b.data;
                out_lens[s->idx] = s->b.size;
                out_status[s->idx] = status;
            } else {
                free(s->b.data);
                out_bodies[s->idx] = NULL;
                out_lens[s->idx] = 0;
                out_status[s->idx] = status;
            }
            curl_multi_remove_handle(mh, m->easy_handle);
            curl_easy_cleanup(m->easy_handle);
            s->c = NULL;
            s->done = 1;

            /* Start the next pending request. */
            if (next_to_start < n) {
                struct slot *ns = &slots[next_to_start];
                ns->c = curl_easy_init();
                if (ns->c) {
                    multi_configure_easy(ns->c, urls[next_to_start], referer, &ns->b, headers);
                    curl_easy_setopt(ns->c, CURLOPT_PRIVATE, ns);
                    curl_multi_add_handle(mh, ns->c);
                    ns->started = 1;
                }
                next_to_start++;
            }
        }
    } while (running_handles > 0 || next_to_start < n);

    curl_multi_cleanup(mh);
    curl_slist_free_all(headers);
    free(slots);
    yetty_ylexbor_prof("    http_get_many DONE  %.0f ms (n=%d)",
                       yetty_ylexbor_prof_now_ms() - t_many, n);
}

#else /* !YETTY_HAVE_CURL */

char *yetty_ylexbor_http_get(const char *url, size_t *out_len, long *out_status)
{
    (void)url;
    if (out_len) {
        *out_len = 0;
    }
    if (out_status) {
        *out_status = 0;
    }
    return NULL;
}

void yetty_ylexbor_http_get_many(const char *const *urls, int n, const char *referer,
                                 int concurrency, char **out_bodies, size_t *out_lens,
                                 long *out_status)
{
    (void)urls;
    (void)referer;
    (void)concurrency;
    for (int i = 0; i < n; i++) {
        out_bodies[i] = NULL;
        out_lens[i] = 0;
        out_status[i] = 0;
    }
}

void *yetty_ylexbor_curl_share(void)
{
    return NULL;
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
        JSValue err = JS_NewError(ctx);
        JS_SetPropertyStr(ctx, err, "message", JS_NewString(ctx, "fetch: missing url"));
        JS_Call(ctx, resolving_funcs[1], JS_UNDEFINED, 1, (JSValueConst[]){err});
        JS_FreeValue(ctx, err);
        goto out;
    }
    const char *url_arg = JS_ToCString(ctx, argv[0]);
    if (!url_arg) {
        goto out;
    }

    char *url = yetty_ylexbor_resolve_url(r, url_arg);
    JS_FreeCString(ctx, url_arg);
    if (!url) {
        JSValue err = JS_NewError(ctx);
        JS_Call(ctx, resolving_funcs[1], JS_UNDEFINED, 1, (JSValueConst[]){err});
        JS_FreeValue(ctx, err);
        goto out;
    }

    size_t blen = 0;
    long status = 0;
    char *body = yetty_ylexbor_http_get(url, &blen, &status);
    free(url);

    if (!body) {
        JSValue err = JS_NewError(ctx);
        JS_SetPropertyStr(ctx, err, "message", JS_NewString(ctx, "fetch: network error"));
        JS_Call(ctx, resolving_funcs[1], JS_UNDEFINED, 1, (JSValueConst[]){err});
        JS_FreeValue(ctx, err);
        goto out;
    }
    JSValue resp = make_response(ctx, body, blen, status);
    JS_Call(ctx, resolving_funcs[0], JS_UNDEFINED, 1, (JSValueConst[]){resp});
    JS_FreeValue(ctx, resp);

out:
    JS_FreeValue(ctx, resolving_funcs[0]);
    JS_FreeValue(ctx, resolving_funcs[1]);
    return promise;
}

/* ===========================================================================
 * Storage — in-memory backing for localStorage / sessionStorage.
 *
 * One global map per kind. Crude string→string. "Storage event"
 * dispatch and quota are TODO; this just satisfies feature-detection.
 * ===========================================================================*/

struct kvent {
    char *k, *v;
};
static struct kvent g_local[1024];
static int g_local_n = 0;
static struct kvent g_session[1024];
static int g_session_n = 0;

static int kv_find(struct kvent *arr, int n, const char *k)
{
    for (int i = 0; i < n; i++) {
        if (strcmp(arr[i].k, k) == 0) {
            return i;
        }
    }
    return -1;
}

#define DEFINE_STORAGE(NAME, MAP, MAP_N)                                                           \
    static JSValue js_##NAME##_getItem(JSContext *ctx, JSValueConst self, int argc,                \
                                       JSValueConst *argv)                                         \
    {                                                                                              \
        (void)self;                                                                                \
        if (argc < 1)                                                                              \
            return JS_NULL;                                                                        \
        const char *k = JS_ToCString(ctx, argv[0]);                                                \
        if (!k)                                                                                    \
            return JS_NULL;                                                                        \
        int i = kv_find(MAP, MAP_N, k);                                                            \
        JS_FreeCString(ctx, k);                                                                    \
        return i < 0 ? JS_NULL : JS_NewString(ctx, MAP[i].v);                                      \
    }                                                                                              \
    static JSValue js_##NAME##_setItem(JSContext *ctx, JSValueConst self, int argc,                \
                                       JSValueConst *argv)                                         \
    {                                                                                              \
        (void)self;                                                                                \
        if (argc < 2)                                                                              \
            return JS_UNDEFINED;                                                                   \
        const char *k = JS_ToCString(ctx, argv[0]);                                                \
        const char *v = JS_ToCString(ctx, argv[1]);                                                \
        if (!k || !v) {                                                                            \
            if (k)                                                                                 \
                JS_FreeCString(ctx, k);                                                            \
            if (v)                                                                                 \
                JS_FreeCString(ctx, v);                                                            \
            return JS_UNDEFINED;                                                                   \
        }                                                                                          \
        int i = kv_find(MAP, MAP_N, k);                                                            \
        if (i >= 0) {                                                                              \
            free(MAP[i].v);                                                                        \
            MAP[i].v = strdup(v);                                                                  \
        } else if (MAP_N < (int)(sizeof(MAP) / sizeof(MAP[0]))) {                                  \
            MAP[MAP_N].k = strdup(k);                                                              \
            MAP[MAP_N].v = strdup(v);                                                              \
            MAP_N++;                                                                               \
        }                                                                                          \
        JS_FreeCString(ctx, k);                                                                    \
        JS_FreeCString(ctx, v);                                                                    \
        return JS_UNDEFINED;                                                                       \
    }                                                                                              \
    static JSValue js_##NAME##_removeItem(JSContext *ctx, JSValueConst self, int argc,             \
                                          JSValueConst *argv)                                      \
    {                                                                                              \
        (void)self;                                                                                \
        if (argc < 1)                                                                              \
            return JS_UNDEFINED;                                                                   \
        const char *k = JS_ToCString(ctx, argv[0]);                                                \
        if (!k)                                                                                    \
            return JS_UNDEFINED;                                                                   \
        int i = kv_find(MAP, MAP_N, k);                                                            \
        JS_FreeCString(ctx, k);                                                                    \
        if (i < 0)                                                                                 \
            return JS_UNDEFINED;                                                                   \
        free(MAP[i].k);                                                                            \
        free(MAP[i].v);                                                                            \
        memmove(&MAP[i], &MAP[i + 1], (MAP_N - i - 1) * sizeof(MAP[0]));                           \
        MAP_N--;                                                                                   \
        return JS_UNDEFINED;                                                                       \
    }                                                                                              \
    static JSValue js_##NAME##_clear(JSContext *ctx, JSValueConst self, int argc,                  \
                                     JSValueConst *argv)                                           \
    {                                                                                              \
        (void)ctx;                                                                                 \
        (void)self;                                                                                \
        (void)argc;                                                                                \
        (void)argv;                                                                                \
        for (int i = 0; i < MAP_N; i++) {                                                          \
            free(MAP[i].k);                                                                        \
            free(MAP[i].v);                                                                        \
        }                                                                                          \
        MAP_N = 0;                                                                                 \
        return JS_UNDEFINED;                                                                       \
    }                                                                                              \
    static JSValue js_##NAME##_key(JSContext *ctx, JSValueConst self, int argc,                    \
                                   JSValueConst *argv)                                             \
    {                                                                                              \
        (void)self;                                                                                \
        if (argc < 1)                                                                              \
            return JS_NULL;                                                                        \
        int i = 0;                                                                                 \
        JS_ToInt32(ctx, &i, argv[0]);                                                              \
        if (i < 0 || i >= MAP_N)                                                                   \
            return JS_NULL;                                                                        \
        return JS_NewString(ctx, MAP[i].k);                                                        \
    }

DEFINE_STORAGE(local, g_local, g_local_n)
DEFINE_STORAGE(session, g_session, g_session_n)

/* ===========================================================================
 * Cookie store — shared with document.cookie.
 * ===========================================================================*/

static char *g_cookie_string = NULL;

static JSValue js_doc_cookie_get(JSContext *ctx, JSValueConst this_val)
{
    (void)this_val;
    return JS_NewString(ctx, g_cookie_string ? g_cookie_string : "");
}
static JSValue js_doc_cookie_set(JSContext *ctx, JSValueConst this_val, JSValueConst val)
{
    (void)this_val;
    const char *s = JS_ToCString(ctx, val);
    if (!s) {
        return JS_UNDEFINED;
    }
    /* Append (real cookie semantics is way more complicated). */
    if (!g_cookie_string) {
        g_cookie_string = strdup(s);
    } else {
        size_t a = strlen(g_cookie_string), b = strlen(s);
        char *p = realloc(g_cookie_string, a + b + 3);
        if (p) {
            strcat(p, "; ");
            strcat(p, s);
            g_cookie_string = p;
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
	 * properties not set in the inline style get "". */
    return JS_DupValue(ctx, JS_GetPropertyStr(ctx, argv[0], "style"));
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
        "var URL_RE = new RegExp('^([a-zA-Z][a-zA-Z0-9+.-]*:)?(//(?:([^:@/?#]*)(?::([^@/?#]*))?@)?([^:/?#]*)(?::([0-9]*))?)?([^?#]*)([?][^#]*)?(#.*)?$');\n"
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
        "this.get = function (k) { for (var i = 0; i < pairs.length; i++) if (pairs[i][0] === k) return pairs[i][1]; return null; };\n"
        "this.getAll = function (k) { var r = []; for (var i = 0; i < pairs.length; i++) if (pairs[i][0] === k) r.push(pairs[i][1]); return r; };\n"
        "this.has = function (k) { for (var i = 0; i < pairs.length; i++) if (pairs[i][0] === k) return true; return false; };\n"
        "this.set = function (k, v) {\n"
        "v = String(v); var done = false;\n"
        "for (var i = 0; i < pairs.length; i++) {\n"
        "if (pairs[i][0] === k) { if (!done) { pairs[i][1] = v; done = true; } else { pairs.splice(i, 1); i--; } }\n"
        "}\n"
        "if (!done) pairs.push([k, v]); sync();\n"
        "};\n"
        "this.append = function (k, v) { pairs.push([k, String(v)]); sync(); };\n"
        "this.delete = function (k) { for (var i = 0; i < pairs.length; i++) { if (pairs[i][0] === k) { pairs.splice(i, 1); i--; } } sync(); };\n"
        "this.forEach = function (fn, thisArg) { for (var i = 0; i < pairs.length; i++) fn.call(thisArg, pairs[i][1], pairs[i][0], self); };\n"
        "this.keys = function* () { for (var i = 0; i < pairs.length; i++) yield pairs[i][0]; };\n"
        "this.values = function* () { for (var i = 0; i < pairs.length; i++) yield pairs[i][1]; };\n"
        "this.entries = function* () { for (var i = 0; i < pairs.length; i++) yield [pairs[i][0], pairs[i][1]]; };\n"
        "this[Symbol.iterator] = this.entries;\n"
        "this.sort = function () { pairs.sort(function (a, b) { return a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0; }); sync(); };\n"
        "this.toString = function () {\n"
        "return pairs.map(function (p) {\n"
        "return encodeURIComponent(p[0]) + '=' + encodeURIComponent(p[1]);\n"
        "}).join('&');\n"
        "};\n"
        "};\n"
        "G.URL = function (url, base) {\n"
        "var self = this;\n"
        "var P = { protocol: '', username: '', password: '', hostname: '', port: '', pathname: '', search: '', hash: '', authority: false };\n"
        "var spObj = null;\n"
        "function isSpecial() { return Object.prototype.hasOwnProperty.call(SPECIAL, P.protocol); }\n"
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
        "var ms = URL_RE.exec(input); P.pathname = ms[7] || '/'; P.search = ms[8] || ''; P.hash = ms[9] || '';\n"
        "} else if (input[0] === '?') {\n"
        "P.pathname = b.pathname; P.search = input; P.hash = '';\n"
        "} else if (input[0] === '#') {\n"
        "P.pathname = b.pathname; P.search = b.search; P.hash = input;\n"
        "} else {\n"
        "var basedir = b.pathname.slice(0, b.pathname.lastIndexOf('/') + 1) || '/';\n"
        "var mr = URL_RE.exec(input);\n"
        "P.pathname = removeDotSegments(basedir + (mr[7] || '')); P.search = mr[8] || ''; P.hash = mr[9] || '';\n"
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
        "set: function (v) { parseAbsolute(v); spObj = null; }, enumerable: true, configurable: true\n"
        "},\n"
        "protocol: {\n"
        "get: function () { return P.protocol; },\n"
        "set: function (v) { v = String(v); if (v && v.slice(-1) !== ':') v += ':'; P.protocol = v.toLowerCase(); }, enumerable: true, configurable: true\n"
        "},\n"
        "username: { get: function () { return P.username; }, set: function (v) { P.username = String(v); }, enumerable: true, configurable: true },\n"
        "password: { get: function () { return P.password; }, set: function (v) { P.password = String(v); }, enumerable: true, configurable: true },\n"
        "host: {\n"
        "get: function () { return P.hostname + (P.port ? ':' + P.port : ''); },\n"
        "set: function (v) { var mm = URL_RE.exec('//' + v); P.hostname = (mm[5] || '').toLowerCase(); P.port = mm[6] || ''; P.authority = true; }, enumerable: true, configurable: true\n"
        "},\n"
        "hostname: { get: function () { return P.hostname; }, set: function (v) { P.hostname = String(v).toLowerCase(); P.authority = true; }, enumerable: true, configurable: true },\n"
        "port: { get: function () { return P.port; }, set: function (v) { P.port = String(v); }, enumerable: true, configurable: true },\n"
        "pathname: {\n"
        "get: function () { return P.pathname; },\n"
        "set: function (v) { v = String(v); if (v && v[0] !== '/' && P.authority) v = '/' + v; P.pathname = v; }, enumerable: true, configurable: true\n"
        "},\n"
        "search: {\n"
        "get: function () { return P.search; },\n"
        "set: function (v) { v = String(v); if (v && v[0] !== '?') v = '?' + v; if (v === '?') v = ''; P.search = v; if (spObj) spObj._setFromString(v); }, enumerable: true, configurable: true\n"
        "},\n"
        "hash: {\n"
        "get: function () { return P.hash; },\n"
        "set: function (v) { v = String(v); if (v && v[0] !== '#') v = '#' + v; if (v === '#') v = ''; P.hash = v; }, enumerable: true, configurable: true\n"
        "},\n"
        "origin: {\n"
        "get: function () { if (isSpecial() && P.hostname) return P.protocol + '//' + P.hostname + (P.port ? ':' + P.port : ''); return 'null'; }, enumerable: true, configurable: true\n"
        "},\n"
        "searchParams: {\n"
        "get: function () { if (!spObj) { spObj = new G.URLSearchParams(P.search); spObj._url = self; } return spObj; }, enumerable: true, configurable: true\n"
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
        "    e&&e.message, '|', (e&&e.stack||'').split('\\n').slice(0,3).join(' <- '));}catch(_){}} };"
        "  this.define=(n,c)=>{ m[n]=c; chain();"
        "    try{ var els=document.querySelectorAll(n); for(var i=0;i<els.length;i++) upgrade(els[i],c);"
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
        "  this.send = (body) => { fetch(this._url).then(r => r.text()).then(t => {"
        "    this.status=200; this.statusText='OK'; this.responseText=t; this.response=t; "
        "this.readyState=4;"
        "    if (this.onreadystatechange) this.onreadystatechange();"
        "    if (this.onload) this.onload(); }).catch(() => {"
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
/* resolve_url + http_get + http_get_referer are defined unconditionally
 * above (above the QuickJS gate) — they're network helpers used by
 * image and external-stylesheet loading whether JS is on or off. */

#endif /* YETTY_HAVE_QUICKJS */
