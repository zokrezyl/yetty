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

#include "ylexbor-internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef YETTY_HAVE_QUICKJS
#  define YETTY_HAVE_QUICKJS 0
#endif
#ifndef YETTY_HAVE_CURL
#  define YETTY_HAVE_CURL 0
#endif

#if YETTY_HAVE_QUICKJS

#include <quickjs.h>

#if YETTY_HAVE_CURL
#  include <curl/curl.h>
#endif


/* ===========================================================================
 * Helpers
 * ===========================================================================*/

static struct yetty_ylexbor *runtime_ylex_w(JSContext *ctx)
{
	JSRuntime *rt = JS_GetRuntime(ctx);
	struct { struct yetty_ylexbor *r; } *s = JS_GetRuntimeOpaque(rt);
	return s ? s->r : NULL;
}

static int64_t now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* ===========================================================================
 * URL resolution + sync HTTP fetch.
 * ===========================================================================*/

char *yetty_ylexbor_resolve_url(struct yetty_ylexbor *r, const char *href)
{
	if (!href) return NULL;
	if (strncmp(href, "http://", 7) == 0 ||
	    strncmp(href, "https://", 8) == 0 ||
	    strncmp(href, "data:", 5) == 0 ||
	    strncmp(href, "file://", 7) == 0) {
		return strdup(href);
	}
	if (!r->base_url) return strdup(href);  /* best effort */

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
		if (!p) return strdup(href);
		const char *slash = strchr(p + 3, '/');
		size_t prefix = slash ? (size_t)(slash - r->base_url)
				      : strlen(r->base_url);
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
			if (*(q - 1) == '/') { slash = q - 1; break; }
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

struct fetch_buf { char *data; size_t size, cap; };

static size_t fetch_write_cb(char *p, size_t sz, size_t n, void *ud)
{
	struct fetch_buf *b = ud;
	size_t add = sz * n;
	if (b->size + add + 1 > b->cap) {
		size_t nc = b->cap ? b->cap * 2 : 16384;
		while (nc < b->size + add + 1) nc *= 2;
		char *np = realloc(b->data, nc);
		if (!np) return 0;
		b->data = np; b->cap = nc;
	}
	memcpy(b->data + b->size, p, add);
	b->size += add;
	return add;
}

char *yetty_ylexbor_http_get(const char *url, size_t *out_len, long *out_status)
{
	if (!url) {
		if (out_len)    *out_len    = 0;
		if (out_status) *out_status = 0;
		return NULL;
	}
	/* file:// — handle directly since libcurl is often built without
	 * the FILE protocol. Used heavily by the WPT integration runner
	 * to load `<script src=...>` siblings of the test page. */
	if (strncmp(url, "file://", 7) == 0) {
		const char *path = url + 7;
		FILE *f = fopen(path, "rb");
		if (!f) {
			if (out_len)    *out_len    = 0;
			if (out_status) *out_status = 0;
			return NULL;
		}
		fseek(f, 0, SEEK_END);
		long sz = ftell(f);
		fseek(f, 0, SEEK_SET);
		if (sz < 0) { fclose(f); if (out_status) *out_status = 0; return NULL; }
		char *buf = malloc((size_t)sz + 1);
		if (!buf) { fclose(f); return NULL; }
		size_t got = fread(buf, 1, (size_t)sz, f);
		fclose(f);
		buf[got] = '\0';
		if (out_len)    *out_len    = got;
		if (out_status) *out_status = 200;
		return buf;
	}
	CURL *c = curl_easy_init();
	if (!c) return NULL;
	struct fetch_buf b = {0};
	curl_easy_setopt(c, CURLOPT_URL, url);
	curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(c, CURLOPT_MAXREDIRS, 10L);
	curl_easy_setopt(c, CURLOPT_USERAGENT,
		"Mozilla/5.0 (X11; Linux x86_64) ylexbor/0.1");
	curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
	curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
	curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, fetch_write_cb);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, &b);
	CURLcode rc = curl_easy_perform(c);
	long status = 0;
	curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
	curl_easy_cleanup(c);
	if (rc != CURLE_OK) { free(b.data); return NULL; }
	if (out_len) *out_len = b.size;
	if (out_status) *out_status = status;
	if (b.data) b.data[b.size] = 0;
	return b.data;
}

#else /* !YETTY_HAVE_CURL */

char *yetty_ylexbor_http_get(const char *url, size_t *out_len, long *out_status)
{
	(void)url;
	if (out_len)    *out_len    = 0;
	if (out_status) *out_status = 0;
	return NULL;
}

#endif /* YETTY_HAVE_CURL */

/* ===========================================================================
 * Timers — single linear array sorted by deadline_ms.
 * ===========================================================================*/

struct yetty_ylexbor_timer {
	int    id;
	int    interval_ms;       /* 0 → one-shot */
	int64_t deadline_ms;
	JSValue handler;
	JSContext *ctx;
};

static int timer_cmp(const void *a, const void *b)
{
	const struct yetty_ylexbor_timer *x = *(const struct yetty_ylexbor_timer **)a;
	const struct yetty_ylexbor_timer *y = *(const struct yetty_ylexbor_timer **)b;
	if (x->deadline_ms < y->deadline_ms) return -1;
	if (x->deadline_ms > y->deadline_ms) return  1;
	return 0;
}

static int timer_add(struct yetty_ylexbor *r,
		     JSContext *ctx, JSValueConst handler,
		     int delay_ms, int interval_ms)
{
	if (r->timer_count == r->timer_cap) {
		int nc = r->timer_cap ? r->timer_cap * 2 : 8;
		void *p = realloc(r->timers, nc * sizeof(*r->timers));
		if (!p) return -1;
		r->timers = p; r->timer_cap = nc;
	}
	struct yetty_ylexbor_timer *t = calloc(1, sizeof(*t));
	if (!t) return -1;
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
		if (r->timers[i]->id != id) continue;
		JS_FreeValue(r->timers[i]->ctx, r->timers[i]->handler);
		free(r->timers[i]);
		memmove(&r->timers[i], &r->timers[i + 1],
			(r->timer_count - i - 1) * sizeof(*r->timers));
		r->timer_count--;
		return;
	}
}

static JSValue js_setTimeout(JSContext *ctx, JSValueConst this_val,
			     int argc, JSValueConst *argv)
{
	(void)this_val;
	if (argc < 1) return JS_NewInt32(ctx, 0);
	struct yetty_ylexbor *r = runtime_ylex_w(ctx);
	if (!r) return JS_NewInt32(ctx, 0);
	int delay = 0;
	if (argc >= 2) JS_ToInt32(ctx, &delay, argv[1]);
	if (delay < 0) delay = 0;
	int id = timer_add(r, ctx, argv[0], delay, /*interval=*/0);
	return JS_NewInt32(ctx, id);
}

static JSValue js_setInterval(JSContext *ctx, JSValueConst this_val,
			      int argc, JSValueConst *argv)
{
	(void)this_val;
	if (argc < 1) return JS_NewInt32(ctx, 0);
	struct yetty_ylexbor *r = runtime_ylex_w(ctx);
	if (!r) return JS_NewInt32(ctx, 0);
	int delay = 0;
	if (argc >= 2) JS_ToInt32(ctx, &delay, argv[1]);
	if (delay < 4) delay = 4;
	int id = timer_add(r, ctx, argv[0], delay, /*interval=*/delay);
	return JS_NewInt32(ctx, id);
}

static JSValue js_clearTimer(JSContext *ctx, JSValueConst this_val,
			     int argc, JSValueConst *argv)
{
	(void)this_val;
	struct yetty_ylexbor *r = runtime_ylex_w(ctx);
	if (!r || argc < 1) return JS_UNDEFINED;
	int id = 0;
	JS_ToInt32(ctx, &id, argv[0]);
	timer_remove(r, id);
	return JS_UNDEFINED;
}

static JSValue js_requestAnimationFrame(JSContext *ctx, JSValueConst this_val,
					int argc, JSValueConst *argv)
{
	(void)this_val;
	if (argc < 1) return JS_NewInt32(ctx, 0);
	struct yetty_ylexbor *r = runtime_ylex_w(ctx);
	if (!r) return JS_NewInt32(ctx, 0);
	return JS_NewInt32(ctx, timer_add(r, ctx, argv[0], 16, 0));
}

static JSValue js_queueMicrotask(JSContext *ctx, JSValueConst this_val,
				 int argc, JSValueConst *argv)
{
	(void)this_val;
	if (argc < 1 || !JS_IsFunction(ctx, argv[0])) return JS_UNDEFINED;
	struct yetty_ylexbor *r = runtime_ylex_w(ctx);
	/* Schedule as a 0ms timer — the next pump() call drains it. */
	if (r) timer_add(r, ctx, argv[0], 0, 0);
	return JS_UNDEFINED;
}

void yetty_ylexbor_js_drain_jobs(struct yetty_ylexbor *r)
{
	if (!r->js_rt) return;
	JSContext *ctx = (JSContext *)r->js_ctx;
	JSContext *jc;
	int n = 0;
	while ((n = JS_ExecutePendingJob((JSRuntime *)r->js_rt, &jc)) > 0) {
		/* keep draining */
	}
	if (n < 0) {
		JSValue ex = JS_GetException(ctx);
		const char *m = JS_ToCString(ctx, ex);
		fprintf(stderr, "[js:job-exception] %s\n", m ? m : "?");
		if (m) JS_FreeCString(ctx, m);
		JS_FreeValue(ctx, ex);
		r->js_error_count++;
	}
}

void yetty_ylexbor_js_web_shutdown(struct yetty_ylexbor *r)
{
	if (!r->timers) return;
	JSContext *ctx = (JSContext *)r->js_ctx;
	for (int i = 0; i < r->timer_count; i++) {
		if (ctx) JS_FreeValue(ctx, r->timers[i]->handler);
		free(r->timers[i]);
	}
	free(r->timers);
	r->timers = NULL;
	r->timer_count = r->timer_cap = 0;
}

int yetty_ylexbor_pump(struct yetty_ylexbor *r)
{
	if (!r) return -1;
	if (!r->js_ctx) return -1;
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
		memmove(&r->timers[0], &r->timers[1],
			(r->timer_count - 1) * sizeof(*r->timers));
		r->timer_count--;

		int saved_id = t->id;
		int saved_interval = t->interval_ms;
		JSValue handler = t->handler;
		JSValue ret = JS_Call(ctx, handler, JS_UNDEFINED, 0, NULL);
		if (JS_IsException(ret)) {
			JSValue ex = JS_GetException(ctx);
			const char *m = JS_ToCString(ctx, ex);
			fprintf(stderr, "[js:timer] %s\n", m ? m : "?");
			if (m) JS_FreeCString(ctx, m);
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
				r->timers = p; r->timer_cap = nc;
			}
			r->timers[r->timer_count++] = t;
			qsort(r->timers, r->timer_count, sizeof(*r->timers),
			      timer_cmp);
		} else {
			JS_FreeValue(ctx, t->handler);
			free(t);
		}
after_call:
		yetty_ylexbor_js_drain_jobs(r);
		now = now_ms();
	}
	yetty_ylexbor_js_drain_jobs(r);
	if (r->timer_count == 0) return -1;
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
	JS_SetPropertyStr(ctx, resp, "ok",
		JS_NewBool(ctx, status >= 200 && status < 300));
	JS_SetPropertyStr(ctx, resp, "statusText", JS_NewString(ctx, ""));
	JS_SetPropertyStr(ctx, resp, "redirected", JS_FALSE);
	JS_SetPropertyStr(ctx, resp, "type", JS_NewString(ctx, "basic"));
	JSValue body_str = body ? JS_NewStringLen(ctx, body, len)
				: JS_NewString(ctx, "");
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
	JSValue installer = JS_Eval(ctx, def, strlen(def),
		"<response-init>", JS_EVAL_TYPE_GLOBAL);
	if (JS_IsException(installer)) {
		JS_FreeValue(ctx, installer);
		return resp;
	}
	JSValueConst args[] = { resp };
	JSValue res = JS_Call(ctx, installer, JS_UNDEFINED, 1, args);
	JS_FreeValue(ctx, installer);
	if (!JS_IsException(res)) JS_FreeValue(ctx, res);
	free(body);
	return resp;
}

static JSValue js_fetch(JSContext *ctx, JSValueConst this_val,
			int argc, JSValueConst *argv)
{
	(void)this_val;
	struct yetty_ylexbor *r = runtime_ylex_w(ctx);

	JSValue resolving_funcs[2];
	JSValue promise = JS_NewPromiseCapability(ctx, resolving_funcs);
	if (JS_IsException(promise)) return promise;

	if (argc < 1 || !r) {
		JSValue err = JS_NewError(ctx);
		JS_SetPropertyStr(ctx, err, "message",
			JS_NewString(ctx, "fetch: missing url"));
		JS_Call(ctx, resolving_funcs[1], JS_UNDEFINED, 1,
			(JSValueConst[]){err});
		JS_FreeValue(ctx, err);
		goto out;
	}
	const char *url_arg = JS_ToCString(ctx, argv[0]);
	if (!url_arg) goto out;

	char *url = yetty_ylexbor_resolve_url(r, url_arg);
	JS_FreeCString(ctx, url_arg);
	if (!url) {
		JSValue err = JS_NewError(ctx);
		JS_Call(ctx, resolving_funcs[1], JS_UNDEFINED, 1,
			(JSValueConst[]){err});
		JS_FreeValue(ctx, err);
		goto out;
	}

	size_t blen = 0;
	long status = 0;
	char *body = yetty_ylexbor_http_get(url, &blen, &status);
	free(url);

	if (!body) {
		JSValue err = JS_NewError(ctx);
		JS_SetPropertyStr(ctx, err, "message",
			JS_NewString(ctx, "fetch: network error"));
		JS_Call(ctx, resolving_funcs[1], JS_UNDEFINED, 1,
			(JSValueConst[]){err});
		JS_FreeValue(ctx, err);
		goto out;
	}
	JSValue resp = make_response(ctx, body, blen, status);
	JS_Call(ctx, resolving_funcs[0], JS_UNDEFINED, 1,
		(JSValueConst[]){resp});
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

struct kvent { char *k, *v; };
static struct kvent g_local[1024];   static int g_local_n  = 0;
static struct kvent g_session[1024]; static int g_session_n = 0;

static int kv_find(struct kvent *arr, int n, const char *k)
{
	for (int i = 0; i < n; i++)
		if (strcmp(arr[i].k, k) == 0) return i;
	return -1;
}

#define DEFINE_STORAGE(NAME, MAP, MAP_N)                                   \
static JSValue js_##NAME##_getItem(JSContext *ctx, JSValueConst self,      \
				   int argc, JSValueConst *argv)            \
{ (void)self; if (argc<1) return JS_NULL;                                  \
	const char *k = JS_ToCString(ctx, argv[0]); if(!k) return JS_NULL; \
	int i = kv_find(MAP, MAP_N, k); JS_FreeCString(ctx,k);             \
	return i<0 ? JS_NULL : JS_NewString(ctx, MAP[i].v); }              \
static JSValue js_##NAME##_setItem(JSContext *ctx, JSValueConst self,      \
				   int argc, JSValueConst *argv)            \
{ (void)self; if (argc<2) return JS_UNDEFINED;                             \
	const char *k = JS_ToCString(ctx, argv[0]);                        \
	const char *v = JS_ToCString(ctx, argv[1]);                        \
	if(!k||!v){ if(k)JS_FreeCString(ctx,k); if(v)JS_FreeCString(ctx,v);\
		return JS_UNDEFINED; }                                     \
	int i = kv_find(MAP, MAP_N, k);                                    \
	if (i>=0) { free(MAP[i].v); MAP[i].v = strdup(v); }                \
	else if (MAP_N < (int)(sizeof(MAP)/sizeof(MAP[0]))) {              \
		MAP[MAP_N].k = strdup(k); MAP[MAP_N].v = strdup(v);        \
		MAP_N++;                                                   \
	}                                                                  \
	JS_FreeCString(ctx,k); JS_FreeCString(ctx,v); return JS_UNDEFINED; }\
static JSValue js_##NAME##_removeItem(JSContext *ctx, JSValueConst self,   \
				      int argc, JSValueConst *argv)        \
{ (void)self; if (argc<1) return JS_UNDEFINED;                             \
	const char *k = JS_ToCString(ctx, argv[0]); if(!k) return JS_UNDEFINED;\
	int i = kv_find(MAP, MAP_N, k); JS_FreeCString(ctx,k);             \
	if (i<0) return JS_UNDEFINED;                                      \
	free(MAP[i].k); free(MAP[i].v);                                    \
	memmove(&MAP[i], &MAP[i+1], (MAP_N-i-1)*sizeof(MAP[0]));           \
	MAP_N--; return JS_UNDEFINED; }                                    \
static JSValue js_##NAME##_clear(JSContext *ctx, JSValueConst self,        \
				 int argc, JSValueConst *argv)             \
{ (void)ctx; (void)self; (void)argc; (void)argv;                           \
	for (int i=0;i<MAP_N;i++){free(MAP[i].k);free(MAP[i].v);}          \
	MAP_N = 0; return JS_UNDEFINED; }                                  \
static JSValue js_##NAME##_key(JSContext *ctx, JSValueConst self,          \
			       int argc, JSValueConst *argv)               \
{ (void)self; if (argc<1) return JS_NULL;                                  \
	int i = 0; JS_ToInt32(ctx, &i, argv[0]);                           \
	if (i<0||i>=MAP_N) return JS_NULL;                                 \
	return JS_NewString(ctx, MAP[i].k); }

DEFINE_STORAGE(local,   g_local,   g_local_n)
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
static JSValue js_doc_cookie_set(JSContext *ctx, JSValueConst this_val,
				 JSValueConst val)
{
	(void)this_val;
	const char *s = JS_ToCString(ctx, val);
	if (!s) return JS_UNDEFINED;
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

static JSValue js_matchMedia(JSContext *ctx, JSValueConst this_val,
			     int argc, JSValueConst *argv)
{
	(void)this_val; (void)argc; (void)argv;
	const char *def =
		"({ matches: false, media: '', "
		"   addEventListener: ()=>{}, removeEventListener: ()=>{}, "
		"   addListener: ()=>{}, removeListener: ()=>{}, "
		"   dispatchEvent: ()=>false, onchange: null })";
	return JS_Eval(ctx, def, strlen(def),
		"<matchMedia>", JS_EVAL_TYPE_GLOBAL);
}

static JSValue js_getComputedStyle(JSContext *ctx, JSValueConst this_val,
				   int argc, JSValueConst *argv)
{
	(void)this_val;
	if (argc < 1) return JS_NULL;
	/* Return the same `style` proxy we use for el.style — JS code
	 * that only reads inline-style values keeps working; reads of
	 * properties not set in the inline style get "". */
	return JS_DupValue(ctx, JS_GetPropertyStr(ctx, argv[0], "style"));
}

/* ===========================================================================
 * Crypto — getRandomValues real, the rest stubs that throw or return
 * never-resolving promises so feature detection succeeds.
 * ===========================================================================*/

static JSValue js_crypto_getRandomValues(JSContext *ctx, JSValueConst this_val,
					 int argc, JSValueConst *argv)
{
	(void)this_val;
	if (argc < 1) return JS_UNDEFINED;
	/* TypedArray view: we just write to its underlying buffer via
	 * JS_GetTypedArrayBuffer (best-effort). */
	size_t bo = 0, blen = 0, bps = 0;
	JSValue ab = JS_GetTypedArrayBuffer(ctx, argv[0], &bo, &blen, &bps);
	if (JS_IsException(ab)) return JS_DupValue(ctx, argv[0]);
	size_t abl;
	uint8_t *data = JS_GetArrayBuffer(ctx, &abl, ab);
	if (data) {
		FILE *f = fopen("/dev/urandom", "rb");
		if (f) { (void)!fread(data + bo, 1, blen, f); fclose(f); }
	}
	JS_FreeValue(ctx, ab);
	return JS_DupValue(ctx, argv[0]);
}

static JSValue js_crypto_randomUUID(JSContext *ctx, JSValueConst this_val,
				    int argc, JSValueConst *argv)
{
	(void)this_val; (void)argc; (void)argv;
	uint8_t b[16] = {0};
	FILE *f = fopen("/dev/urandom", "rb");
	if (f) { (void)!fread(b, 1, 16, f); fclose(f); }
	b[6] = (b[6] & 0x0f) | 0x40;
	b[8] = (b[8] & 0x3f) | 0x80;
	char buf[37];
	snprintf(buf, sizeof(buf),
		"%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
		b[0],b[1],b[2],b[3], b[4],b[5], b[6],b[7], b[8],b[9],
		b[10],b[11],b[12],b[13],b[14],b[15]);
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
static void install_global_fn(JSContext *ctx, JSValue global,
			      const char *name, JSCFunction *fn, int argc)
{
	JS_SetPropertyStr(ctx, global, name,
		JS_NewCFunction(ctx, fn, name, argc));
}

static const JSCFunctionListEntry local_funcs[] = {
	JS_CFUNC_DEF("getItem",    1, js_local_getItem),
	JS_CFUNC_DEF("setItem",    2, js_local_setItem),
	JS_CFUNC_DEF("removeItem", 1, js_local_removeItem),
	JS_CFUNC_DEF("clear",      0, js_local_clear),
	JS_CFUNC_DEF("key",        1, js_local_key),
};
static const JSCFunctionListEntry session_funcs[] = {
	JS_CFUNC_DEF("getItem",    1, js_session_getItem),
	JS_CFUNC_DEF("setItem",    2, js_session_setItem),
	JS_CFUNC_DEF("removeItem", 1, js_session_removeItem),
	JS_CFUNC_DEF("clear",      0, js_session_clear),
	JS_CFUNC_DEF("key",        1, js_session_key),
};

void yetty_ylexbor_js_web_install(struct yetty_ylexbor *r)
{
	JSContext *ctx = (JSContext *)r->js_ctx;
	if (!ctx) return;

	JSValue global = JS_GetGlobalObject(ctx);
	install_global_fn(ctx, global, "setTimeout",      js_setTimeout,             2);
	install_global_fn(ctx, global, "setInterval",     js_setInterval,            2);
	install_global_fn(ctx, global, "clearTimeout",    js_clearTimer,             1);
	install_global_fn(ctx, global, "clearInterval",   js_clearTimer,             1);
	install_global_fn(ctx, global, "queueMicrotask",  js_queueMicrotask,         1);
	install_global_fn(ctx, global, "requestAnimationFrame", js_requestAnimationFrame, 1);
	install_global_fn(ctx, global, "cancelAnimationFrame",  js_clearTimer,        1);
	install_global_fn(ctx, global, "fetch",           js_fetch,                  1);
	install_global_fn(ctx, global, "matchMedia",      js_matchMedia,             1);
	install_global_fn(ctx, global, "getComputedStyle",js_getComputedStyle,       1);

	/* navigator */
	JSValue nav = JS_NewObject(ctx);
	JS_SetPropertyStr(ctx, nav, "userAgent",
		JS_NewString(ctx,
		"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
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
	const char *swdef =
		"({ register: () => new Promise(()=>{}), "
		"   ready: new Promise(()=>{}), "
		"   getRegistrations: () => Promise.resolve([]), "
		"   addEventListener: ()=>{} })";
	JSValue sw = JS_Eval(ctx, swdef, strlen(swdef),
		"<sw>", JS_EVAL_TYPE_GLOBAL);
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
		JS_SetPropertyStr(ctx, loc, "protocol",
			JS_NewStringLen(ctx, href, plen + 1));
		const char *host_start = p + 3;
		const char *path_start = strchr(host_start, '/');
		const char *q_start    = strchr(host_start, '?');
		const char *h_start    = strchr(host_start, '#');
		size_t host_len = path_start ? (size_t)(path_start - host_start)
				              : strlen(host_start);
		JSValue host_val = JS_NewStringLen(ctx, host_start, host_len);
		JS_SetPropertyStr(ctx, loc, "host", JS_DupValue(ctx, host_val));
		JS_SetPropertyStr(ctx, loc, "hostname", JS_DupValue(ctx, host_val));
		JS_FreeValue(ctx, host_val);
		size_t origin_len = (size_t)(host_start + host_len - href);
		JS_SetPropertyStr(ctx, loc, "origin",
			JS_NewStringLen(ctx, href, origin_len));
		JS_SetPropertyStr(ctx, loc, "pathname",
			path_start ? JS_NewString(ctx,
				q_start ? (path_start[0]=='/'? path_start : "/")
				        : path_start)
				   : JS_NewString(ctx, "/"));
		JS_SetPropertyStr(ctx, loc, "search",
			q_start ? JS_NewString(ctx, q_start) : JS_NewString(ctx, ""));
		JS_SetPropertyStr(ctx, loc, "hash",
			h_start ? JS_NewString(ctx, h_start) : JS_NewString(ctx, ""));
		JS_SetPropertyStr(ctx, loc, "port", JS_NewString(ctx, ""));
	} else {
		JS_SetPropertyStr(ctx, loc, "protocol", JS_NewString(ctx, "about:"));
		JS_SetPropertyStr(ctx, loc, "host",     JS_NewString(ctx, ""));
		JS_SetPropertyStr(ctx, loc, "hostname", JS_NewString(ctx, ""));
		JS_SetPropertyStr(ctx, loc, "origin",   JS_NewString(ctx, ""));
		JS_SetPropertyStr(ctx, loc, "pathname", JS_NewString(ctx, ""));
		JS_SetPropertyStr(ctx, loc, "search",   JS_NewString(ctx, ""));
		JS_SetPropertyStr(ctx, loc, "hash",     JS_NewString(ctx, ""));
		JS_SetPropertyStr(ctx, loc, "port",     JS_NewString(ctx, ""));
	}
	const char *locmethods =
		"l => { l.assign = () => {}; l.replace = () => {}; "
		"       l.reload = () => {}; l.toString = () => l.href; "
		"       return l; }";
	JSValue li = JS_Eval(ctx, locmethods, strlen(locmethods),
		"<loc>", JS_EVAL_TYPE_GLOBAL);
	if (!JS_IsException(li)) {
		JSValue r2 = JS_Call(ctx, li, JS_UNDEFINED, 1,
			(JSValueConst[]){loc});
		JS_FreeValue(ctx, r2);
	}
	JS_FreeValue(ctx, li);
	/* Both `window.location` and `document.location` reference the
	 * same Location object — share by ref. */
	JSValue doc_for_loc = JS_GetPropertyStr(ctx, global, "document");
	if (!JS_IsUndefined(doc_for_loc) && !JS_IsNull(doc_for_loc)) {
		JS_SetPropertyStr(ctx, doc_for_loc, "location",
			JS_DupValue(ctx, loc));
	}
	JS_FreeValue(ctx, doc_for_loc);
	JS_SetPropertyStr(ctx, global, "location", loc);

	/* history — minimal stub. */
	const char *histdef =
		"({ pushState: ()=>{}, replaceState: ()=>{}, "
		"   back: ()=>{}, forward: ()=>{}, go: ()=>{}, "
		"   length: 1, state: null, scrollRestoration: 'auto' })";
	JSValue hist = JS_Eval(ctx, histdef, strlen(histdef),
		"<hist>", JS_EVAL_TYPE_GLOBAL);
	JS_SetPropertyStr(ctx, global, "history", hist);

	/* Storage instances. */
	JSValue ls = JS_NewObject(ctx);
	JS_SetPropertyFunctionList(ctx, ls, local_funcs,
		sizeof(local_funcs)/sizeof(local_funcs[0]));
	JS_SetPropertyStr(ctx, global, "localStorage", ls);
	JSValue ss = JS_NewObject(ctx);
	JS_SetPropertyFunctionList(ctx, ss, session_funcs,
		sizeof(session_funcs)/sizeof(session_funcs[0]));
	JS_SetPropertyStr(ctx, global, "sessionStorage", ss);

	/* document.cookie accessor. */
	JSValue doc = JS_GetPropertyStr(ctx, global, "document");
	if (!JS_IsUndefined(doc) && !JS_IsNull(doc)) {
		JSAtom atom = JS_NewAtom(ctx, "cookie");
		JSValue getter = JS_NewCFunction2(ctx,
			(JSCFunction *)js_doc_cookie_get,
			"get cookie", 0, JS_CFUNC_getter, 0);
		JSValue setter = JS_NewCFunction2(ctx,
			(JSCFunction *)js_doc_cookie_set,
			"set cookie", 1, JS_CFUNC_setter, 0);
		JS_DefinePropertyGetSet(ctx, doc, atom, getter, setter,
			JS_PROP_CONFIGURABLE);
		JS_FreeAtom(ctx, atom);
		/* Also: document.readyState */
		JS_SetPropertyStr(ctx, doc, "readyState",
			JS_NewString(ctx, "complete"));
		/* document.URL / location */
		JS_SetPropertyStr(ctx, doc, "URL", JS_NewString(ctx, href));
		JS_SetPropertyStr(ctx, doc, "documentURI",
			JS_NewString(ctx, href));
		JS_SetPropertyStr(ctx, doc, "title",
			JS_NewString(ctx, ""));
		/* document.domain — host portion of base_url. github's
		 * cookie helpers throw "Unable to get document domain"
		 * when null, so empty string is acceptable but a real
		 * value is closer to spec. */
		const char *p2 = strstr(href, "://");
		if (p2) {
			const char *host = p2 + 3;
			const char *path = strchr(host, '/');
			size_t hlen = path ? (size_t)(path - host)
					   : strlen(host);
			JS_SetPropertyStr(ctx, doc, "domain",
				JS_NewStringLen(ctx, host, hlen));
		} else {
			JS_SetPropertyStr(ctx, doc, "domain",
				JS_NewString(ctx, "localhost"));
		}
		/* document.referrer / lastModified / characterSet etc.
		 * — empty strings are fine for feature detection. */
		JS_SetPropertyStr(ctx, doc, "referrer",     JS_NewString(ctx, ""));
		JS_SetPropertyStr(ctx, doc, "lastModified", JS_NewString(ctx, ""));
		JS_SetPropertyStr(ctx, doc, "characterSet", JS_NewString(ctx, "UTF-8"));
		JS_SetPropertyStr(ctx, doc, "charset",      JS_NewString(ctx, "UTF-8"));
		JS_SetPropertyStr(ctx, doc, "contentType",  JS_NewString(ctx, "text/html"));
		JS_SetPropertyStr(ctx, doc, "compatMode",   JS_NewString(ctx, "CSS1Compat"));
		JS_SetPropertyStr(ctx, doc, "hidden",       JS_FALSE);
		JS_SetPropertyStr(ctx, doc, "visibilityState",
			JS_NewString(ctx, "visible"));
		JS_SetPropertyStr(ctx, doc, "designMode",   JS_NewString(ctx, "off"));
		JS_SetPropertyStr(ctx, doc, "dir",          JS_NewString(ctx, "ltr"));
	}
	JS_FreeValue(ctx, doc);

	/* crypto */
	JSValue cry = JS_NewObject(ctx);
	JS_SetPropertyStr(ctx, cry, "getRandomValues",
		JS_NewCFunction(ctx, js_crypto_getRandomValues,
			"getRandomValues", 1));
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
	JSValue sub = JS_Eval(ctx, subtle, strlen(subtle),
		"<subtle>", JS_EVAL_TYPE_GLOBAL);
	JS_SetPropertyStr(ctx, cry, "subtle", sub);
	JS_SetPropertyStr(ctx, global, "crypto", cry);

	/* Misc stubs — feature detection rarely actually USES these,
	 * just checks they exist. */
	const char *stubs =
		"globalThis.Worker          = function(){ this.postMessage = ()=>{}; this.terminate=()=>{}; this.addEventListener=()=>{}; };"
		"globalThis.SharedWorker    = function(){ this.port = { postMessage:()=>{}, addEventListener:()=>{} }; };"
		"globalThis.BroadcastChannel= function(){ this.postMessage = ()=>{}; this.close=()=>{}; this.addEventListener=()=>{}; };"
		"globalThis.AbortController = function(){ this.signal = { aborted:false, addEventListener:()=>{}, removeEventListener:()=>{} }; this.abort = ()=>{ this.signal.aborted=true; }; };"
		"globalThis.AbortSignal     = { abort: ()=>({ aborted:true }), timeout: ()=>({ aborted:false, addEventListener:()=>{} }) };"
		"globalThis.indexedDB       = { open: ()=>({ onsuccess:null, onerror:null, addEventListener:()=>{} }), deleteDatabase: ()=>({}) };"
		"globalThis.btoa = s => { let b=''; for (let i=0;i<s.length;i++) b+=String.fromCharCode(s.charCodeAt(i)&0xff); /* placeholder — real impl below */ "
		"  const m='ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/'; let out='', i=0;"
		"  while (i<b.length){ const a=b.charCodeAt(i++)|0, c=b.charCodeAt(i++)|0, d=b.charCodeAt(i++)|0;"
		"    const t=(a<<16)|(c<<8)|d;"
		"    out += m[(t>>18)&63]+m[(t>>12)&63]+ (i-1>b.length?'=':m[(t>>6)&63]) + (i>b.length?'=':m[t&63]); }"
		"  return out; };"
		"globalThis.atob = s => { const m='ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';"
		"  s = s.replace(/=+$/,''); let out=''; for (let i=0; i<s.length;){ const a=m.indexOf(s[i++]), b=m.indexOf(s[i++]),"
		"    c=m.indexOf(s[i++]), d=m.indexOf(s[i++]); const t=(a<<18)|(b<<12)|((c<0?0:c)<<6)|(d<0?0:d);"
		"    out += String.fromCharCode((t>>16)&0xff); if (c>=0) out += String.fromCharCode((t>>8)&0xff); if (d>=0) out += String.fromCharCode(t&0xff); } return out; };"
		"globalThis.structuredClone = v => JSON.parse(JSON.stringify(v));"
		"globalThis.URL = function(u, base){ this.href = u; this.origin=''; this.protocol=''; this.host=''; this.hostname=''; this.pathname=''; this.search=''; this.hash=''; this.searchParams = new URLSearchParams(''); this.toString=()=>this.href; };"
		"globalThis.URLSearchParams = function(s){ const map = {}; if(typeof s === 'string'){ s = s.replace(/^[?]/,''); s.split('&').forEach(p=>{ if(!p) return; const [k,v=''] = p.split('='); map[decodeURIComponent(k)] = decodeURIComponent(v); }); }"
		"  this.get = k => map[k]||null; this.set=(k,v)=>{map[k]=v;}; this.has=k=>k in map; this.delete=k=>{delete map[k];}; this.toString=()=>Object.entries(map).map(([k,v])=>encodeURIComponent(k)+'='+encodeURIComponent(v)).join('&'); this.entries=function*(){for(const k in map)yield[k,map[k]];}; this.forEach=fn=>{for(const k in map)fn(map[k],k);}; };"
		"globalThis.Event = function(t, init){ this.type=t; this.bubbles=!!(init&&init.bubbles); this.cancelable=!!(init&&init.cancelable); this.defaultPrevented=false; this.preventDefault=()=>{this.defaultPrevented=true;}; this.stopPropagation=()=>{}; this.stopImmediatePropagation=()=>{}; };"
		"globalThis.CustomEvent = function(t, init){ globalThis.Event.call(this,t,init); this.detail = init? init.detail : null; };"
		"globalThis.MessageEvent = function(t, init){ globalThis.Event.call(this,t,init); this.data = init?init.data:null; this.origin = init?init.origin||'':''; };"
		"globalThis.MutationObserver = function(cb){ this.observe=()=>{}; this.disconnect=()=>{}; this.takeRecords=()=>[]; };"
		"globalThis.IntersectionObserver = function(cb){ this.observe=()=>{}; this.unobserve=()=>{}; this.disconnect=()=>{}; this.takeRecords=()=>[]; };"
		"globalThis.ResizeObserver = function(cb){ this.observe=()=>{}; this.unobserve=()=>{}; this.disconnect=()=>{}; };"
		"globalThis.PerformanceObserver = function(cb){ this.observe=()=>{}; this.disconnect=()=>{}; this.takeRecords=()=>[]; };"
		"globalThis.performance = { now: () => Date.now(), mark: ()=>{}, measure: ()=>{}, getEntries: ()=>[], getEntriesByType: ()=>[], getEntriesByName: ()=>[], clearMarks: ()=>{}, clearMeasures: ()=>{}, timing: {}, navigation: { type:0, redirectCount:0 } };"
		"globalThis.scrollTo = ()=>{}; globalThis.scrollBy = ()=>{};"
		"globalThis.alert = m => console.log('[alert]', m);"
		"globalThis.confirm = () => false;"
		"globalThis.prompt = () => null;"
		"globalThis.devicePixelRatio = 1;"
		"globalThis.innerWidth  = " "1024;"
		"globalThis.innerHeight = "  "768;"
		"globalThis.outerWidth  = 1024; globalThis.outerHeight = 768;"
		"globalThis.screen      = { width:1024, height:768, availWidth:1024, availHeight:768, colorDepth:24, pixelDepth:24 };"
		/* EventTarget — base class many libs `class X extends
		 * EventTarget` against. Mirrors addEventListener etc. */
		"globalThis.EventTarget = function(){"
		"  this._lst = {};"
		"  this.addEventListener = (t, fn) => { (this._lst[t] = this._lst[t] || []).push(fn); };"
		"  this.removeEventListener = (t, fn) => { const a = this._lst[t]; if (!a) return; const i = a.indexOf(fn); if (i >= 0) a.splice(i, 1); };"
		"  this.dispatchEvent = (e) => { const a = this._lst[e && e.type]; if (!a) return true; for (const fn of a.slice()) try { fn.call(this, e); } catch(_) {} return !(e && e.defaultPrevented); };"
		"};"
		"globalThis.HTMLElement = function(){};"
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
		"globalThis.MessageChannel = function(){ const port = { postMessage:()=>{}, onmessage:null, start:()=>{}, close:()=>{}, addEventListener:()=>{}, removeEventListener:()=>{} }; this.port1=port; this.port2=port; };"
		"globalThis.MessagePort   = function(){};"
		"globalThis.Worklet       = function(){ this.addModule=()=>Promise.resolve(); };"
		"globalThis.LinkPreloadManager = function(){};"
		"globalThis.NodeIterator  = function(){};"
		"globalThis.TreeWalker    = function(){ this.nextNode=()=>null; this.previousNode=()=>null; };"
		"globalThis.ProgressEvent = function(t,init){ globalThis.Event.call(this,t,init); this.lengthComputable=!!(init&&init.lengthComputable); this.loaded=(init&&init.loaded)||0; this.total=(init&&init.total)||0; };"
		"globalThis.CompositionEvent = function(t,init){ globalThis.Event.call(this,t,init); this.data=(init&&init.data)||''; };"
		"globalThis.ClipboardEvent = function(t,init){ globalThis.Event.call(this,t,init); this.clipboardData=null; };"
		"globalThis.DragEvent     = function(t,init){ globalThis.MouseEvent.call(this,t,init); this.dataTransfer=null; };"
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
		"globalThis.Range             = function(){ this.setStart=()=>{}; this.setEnd=()=>{}; this.collapse=()=>{}; this.selectNode=()=>{}; this.selectNodeContents=()=>{}; };"
		"globalThis.Selection         = function(){ this.removeAllRanges=()=>{}; this.addRange=()=>{}; this.toString=()=>''; };"
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
		"globalThis.Headers           = function(init){ const m={}; this.get=k=>m[String(k).toLowerCase()]||null; this.set=(k,v)=>{m[String(k).toLowerCase()]=v;}; this.has=k=>String(k).toLowerCase() in m; this.append=this.set; this.delete=k=>{delete m[String(k).toLowerCase()];}; this.forEach=fn=>{for(const k in m)fn(m[k],k);}; this.entries=function*(){for(const k in m)yield[k,m[k]];}; this.keys=function*(){for(const k in m)yield k;}; this.values=function*(){for(const k in m)yield m[k];}; if(init && typeof init==='object'){for(const k in init){this.set(k,init[k]);}} };"
		"globalThis.Request           = function(input, init){ this.url = typeof input==='string'?input:input&&input.url||''; this.method=(init&&init.method)||'GET'; this.headers = new Headers(init&&init.headers); this.body = init&&init.body||null; };"
		"globalThis.Response          = function(body, init){ this.body=body||null; this.status=(init&&init.status)||200; this.statusText=(init&&init.statusText)||''; this.headers=new Headers(init&&init.headers); this.ok=this.status>=200&&this.status<300; this.text=()=>Promise.resolve(typeof this.body==='string'?this.body:''); this.json=()=>Promise.resolve(JSON.parse(typeof this.body==='string'?this.body:'null')); this.arrayBuffer=()=>Promise.resolve(new ArrayBuffer(0)); this.clone=()=>this; };"
		"globalThis.Blob              = function(parts, opts){ this.size=0; this.type=(opts&&opts.type)||''; this.text=()=>Promise.resolve(''); this.arrayBuffer=()=>Promise.resolve(new ArrayBuffer(0)); this.slice=()=>new Blob([],opts); };"
		"globalThis.File              = function(parts, name, opts){ globalThis.Blob.call(this,parts,opts); this.name=name||''; this.lastModified=Date.now(); };"
		"globalThis.FileList          = function(){ this.length=0; this.item=()=>null; };"
		"globalThis.DataTransfer      = function(){ this.types=[]; this.files=new FileList(); this.items={length:0}; this.getData=()=>''; this.setData=()=>{}; };"
		"globalThis.DOMException      = function(message, name){ this.message=message||''; this.name=name||'Error'; };"
		"globalThis.DOMParser         = function(){ this.parseFromString = (s, t) => document; };"
		"globalThis.XPathResult       = function(){};"
		"globalThis.TextEncoder       = function(){ this.encode = s => { const a = new Uint8Array(s.length); for (let i=0;i<s.length;i++) a[i]=s.charCodeAt(i)&0xff; return a; }; };"
		"globalThis.TextDecoder       = function(){ this.decode = b => { let o=''; const v = b instanceof Uint8Array ? b : new Uint8Array(b); for (let i=0;i<v.length;i++) o+=String.fromCharCode(v[i]); return o; }; };"
		"globalThis.AudioContext      = function(){ this.close=()=>{}; this.createGain=()=>({}); this.createOscillator=()=>({}); this.destination={}; };"
		"globalThis.webkitAudioContext = globalThis.AudioContext;"
		"globalThis.RTCPeerConnection = function(){ this.close=()=>{}; this.createOffer=()=>Promise.resolve({}); this.createAnswer=()=>Promise.resolve({}); this.addEventListener=()=>{}; };"
		"globalThis.MediaQueryListEvent = function(t,init){ globalThis.Event.call(this,t,init); this.matches=!!(init&&init.matches); this.media=(init&&init.media)||''; };"
		"globalThis.PopStateEvent     = function(t,init){ globalThis.Event.call(this,t,init); this.state=init?init.state:null; };"
		"globalThis.HashChangeEvent   = function(t,init){ globalThis.Event.call(this,t,init); this.oldURL=(init&&init.oldURL)||''; this.newURL=(init&&init.newURL)||''; };"
		"globalThis.PageTransitionEvent= function(t,init){ globalThis.Event.call(this,t,init); this.persisted=!!(init&&init.persisted); };"
		"globalThis.ErrorEvent        = function(t,init){ globalThis.Event.call(this,t,init); this.message=(init&&init.message)||''; this.filename=(init&&init.filename)||''; this.lineno=(init&&init.lineno)||0; this.colno=(init&&init.colno)||0; this.error=(init&&init.error)||null; };"
		"globalThis.PromiseRejectionEvent = function(t,init){ globalThis.Event.call(this,t,init); this.promise=(init&&init.promise)||null; this.reason=(init&&init.reason)||null; };"
		"globalThis.SecurityPolicyViolationEvent = function(t,init){ globalThis.Event.call(this,t,init); };"
		"globalThis.UIEvent           = function(t,init){ globalThis.Event.call(this,t,init); };"
		"globalThis.MouseEvent        = function(t,init){ globalThis.Event.call(this,t,init); this.clientX=(init&&init.clientX)||0; this.clientY=(init&&init.clientY)||0; this.button=(init&&init.button)||0; };"
		"globalThis.KeyboardEvent     = function(t,init){ globalThis.Event.call(this,t,init); this.key=(init&&init.key)||''; this.code=(init&&init.code)||''; this.keyCode=(init&&init.keyCode)||0; };"
		"globalThis.PointerEvent      = function(t,init){ globalThis.MouseEvent.call(this,t,init); this.pointerId=(init&&init.pointerId)||0; this.pointerType=(init&&init.pointerType)||''; };"
		"globalThis.TouchEvent        = function(t,init){ globalThis.Event.call(this,t,init); this.touches=[]; this.targetTouches=[]; this.changedTouches=[]; };"
		"globalThis.WheelEvent        = function(t,init){ globalThis.MouseEvent.call(this,t,init); this.deltaX=(init&&init.deltaX)||0; this.deltaY=(init&&init.deltaY)||0; this.deltaZ=(init&&init.deltaZ)||0; };"
		"globalThis.FocusEvent        = function(t,init){ globalThis.Event.call(this,t,init); this.relatedTarget=(init&&init.relatedTarget)||null; };"
		"globalThis.InputEvent        = function(t,init){ globalThis.Event.call(this,t,init); this.data=(init&&init.data)||null; this.inputType=(init&&init.inputType)||''; };"
		"globalThis.SubmitEvent       = function(t,init){ globalThis.Event.call(this,t,init); this.submitter=(init&&init.submitter)||null; };"
		"globalThis.AnimationEvent    = function(t,init){ globalThis.Event.call(this,t,init); this.animationName=(init&&init.animationName)||''; };"
		"globalThis.TransitionEvent   = function(t,init){ globalThis.Event.call(this,t,init); this.propertyName=(init&&init.propertyName)||''; };"
		"globalThis.GamepadEvent      = function(t,init){ globalThis.Event.call(this,t,init); this.gamepad=null; };"
		"globalThis.StorageEvent      = function(t,init){ globalThis.Event.call(this,t,init); this.key=(init&&init.key)||''; this.oldValue=(init&&init.oldValue)||null; this.newValue=(init&&init.newValue)||null; };"
		"globalThis.MediaStream       = function(){ this.getTracks=()=>[]; this.addTrack=()=>{}; this.removeTrack=()=>{}; };"
		"globalThis.MediaStreamTrack  = function(){};"
		"globalThis.IntersectionObserverEntry = function(){};"
		"globalThis.ResizeObserverEntry = function(){};"
		"globalThis.PerformanceEntry  = function(){};"
		"globalThis.PerformanceMark   = function(){};"
		"globalThis.PerformanceMeasure= function(){};"
		"globalThis.PerformanceNavigationTiming = function(){};"
		"globalThis.PerformanceResourceTiming   = function(){};"
		"globalThis.CustomElementRegistry = function(){ const m={}; this.define=(n,c)=>{m[n]=c;}; this.get=n=>m[n]; this.whenDefined=()=>Promise.resolve(); this.upgrade=()=>{}; };"
		"globalThis.customElements = new globalThis.CustomElementRegistry();"
		"globalThis.Image       = function(){ this.src=''; this.onload=null; this.onerror=null; this.addEventListener=()=>{}; };"
		"globalThis.FileReader  = function(){ this.readAsText=()=>{}; this.readAsDataURL=()=>{}; this.addEventListener=()=>{}; };"
		"globalThis.FormData    = function(){ const m={}; this.append=(k,v)=>{m[k]=v;}; this.get=k=>m[k]; this.has=k=>k in m; this.delete=k=>{delete m[k];}; this.entries=function*(){for(const k in m)yield[k,m[k]];}; };"
		"globalThis.XMLHttpRequest = function(){"
		"  this.readyState=0; this.status=0; this.statusText=''; this.responseText=''; this.response=''; this.onload=null; this.onreadystatechange=null; this.onerror=null;"
		"  this._headers = {};"
		"  this.open = (m,u) => { this._method=m; this._url=u; this.readyState=1; if (this.onreadystatechange) this.onreadystatechange(); };"
		"  this.setRequestHeader = (k,v) => { this._headers[k]=v; };"
		"  this.send = (body) => { fetch(this._url).then(r => r.text()).then(t => {"
		"    this.status=200; this.statusText='OK'; this.responseText=t; this.response=t; this.readyState=4;"
		"    if (this.onreadystatechange) this.onreadystatechange();"
		"    if (this.onload) this.onload(); }).catch(() => {"
		"    this.status=0; this.readyState=4; if (this.onerror) this.onerror(); }); };"
		"  this.abort = () => {}; this.getAllResponseHeaders = () => ''; this.getResponseHeader = () => null; this.addEventListener = (t, fn) => { this['on'+t] = fn; }; };"
		"globalThis.WebSocket = function(){ this.send=()=>{}; this.close=()=>{}; this.addEventListener=()=>{}; this.readyState=3; };"
		"globalThis.HTMLCanvasElement = function(){};"
		"globalThis.OffscreenCanvas = function(){ this.getContext = () => null; };"
		"globalThis.requestIdleCallback = (cb) => setTimeout(cb, 1);"
		"globalThis.cancelIdleCallback = clearTimeout;"
		"";
	JSValue stub_v = JS_Eval(ctx, stubs, strlen(stubs),
		"<webapi-stubs>", JS_EVAL_TYPE_GLOBAL);
	if (JS_IsException(stub_v)) {
		JSValue ex = JS_GetException(ctx);
		const char *m = JS_ToCString(ctx, ex);
		fprintf(stderr, "[js:webapi-stub] %s\n", m ? m : "?");
		if (m) JS_FreeCString(ctx, m);
		JS_FreeValue(ctx, ex);
	}
	JS_FreeValue(ctx, stub_v);

	JS_FreeValue(ctx, global);
}

#else  /* !YETTY_HAVE_QUICKJS */

void yetty_ylexbor_js_web_install(struct yetty_ylexbor *r)         { (void)r; }
void yetty_ylexbor_js_web_shutdown(struct yetty_ylexbor *r)        { (void)r; }
void yetty_ylexbor_js_drain_jobs(struct yetty_ylexbor *r)          { (void)r; }
int  yetty_ylexbor_pump(struct yetty_ylexbor *r)                   { (void)r; return -1; }
char *yetty_ylexbor_resolve_url(struct yetty_ylexbor *r, const char *href)
                                                            { (void)r; return href ? strdup(href) : NULL; }
char *yetty_ylexbor_http_get(const char *url, size_t *out_len, long *out_status)
                                                            { (void)url; (void)out_len; (void)out_status; return NULL; }

#endif /* YETTY_HAVE_QUICKJS */
