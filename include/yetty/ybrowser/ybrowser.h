#ifndef YETTY_YLEXBOR_H
#define YETTY_YLEXBOR_H

/*
 * ylexbor — permissively-licensed in-process HTML/CSS renderer for
 * yetty. Sits on top of lexbor (Apache-2.0) for HTML parsing and on
 * libcss for the CSS cascade. Layout + paint live here.
 *
 * Implements:
 *   - HTML5 parsing (via lexbor); CSS cascade incl. @import (via libcss)
 *   - Block flow with floats/clear, flexbox (iterative flexible-length
 *     resolution, per-line wrap), reduced grid (templates, spans,
 *     stretch, occupancy placement), simple tables
 *   - position: relative/absolute/fixed incl. static-position fallback
 *   - Images (PNG/JPEG/GIF/WebP/BMP via decoders, SVG as merged vector
 *     scenes through ysvg) with sync/batch/worker-pool fetch paths
 *   - JavaScript (QuickJS): DOM, timers, async fetch()/XHR, storage
 *   - Painting: ydraw primitives (boxes + TEXT_DRAWABLE_LIST) into a buffer
 *
 * Known gaps (tracked in the correctness-audit issue):
 *   - custom properties are text-substituted, not cascade-correct
 *   - grid intrinsic track sizing; flex reverse/order/align-self;
 *     column wrap; automatic content-based flex minimums
 *   - background images, gradients, border radii at paint
 *   - z-index stacking contexts (two-pass positioned-subtree paint only)
 *   - animations / transitions
 *
 * License note: this module is BSL-1.1 (yetty's main license) — unlike
 * src/yetty/ynetsurf which is GPL-2.0 due to NetSurf core. ylexbor uses
 * only Apache-2.0 lexbor and yetty's own code, so it can be linked
 * directly into the main yetty binary.
 */

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>

struct yetty_ydraw_drawable_list;

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ylexbor;
YETTY_YRESULT_DECLARE(yetty_ylexbor_ptr, struct yetty_ylexbor *);

/* Network loader — owns everything fetch-related that outlives a single
 * request: the libcurl share handle (connection pool + DNS + TLS-session
 * caches), its locks, and the Alt-Svc (HTTP/3 upgrade) cache path. Create
 * ONE per process and hand it to every engine via the config below so the
 * page fetch, stylesheet fetches, and image fetches all reuse the same
 * warm connections. An engine created without one makes its own (private
 * connection pool — correct, just no cross-engine reuse). Destroy only
 * after every engine using it is gone. */
struct yetty_ybrowser_loader;
YETTY_YRESULT_DECLARE(yetty_ybrowser_loader_ptr, struct yetty_ybrowser_loader *);

struct yetty_ybrowser_loader_ptr_result yetty_ybrowser_loader_create(void);
struct yetty_ycore_void_result yetty_ybrowser_loader_destroy(struct yetty_ybrowser_loader *loader);

/* ---- Request / response objects ------------------------------------------
 * A fetch is described by a request object and produces a response object.
 * The request's `kind` drives the wire shape (Accept / Sec-Fetch-* headers)
 * — content-negotiating CDNs and WAFs serve different representations per
 * kind, so a stylesheet fetched "as an image" gets the wrong bytes. The
 * response accumulates decorations (decoded image pixels today) on its way
 * to the render end of the pipeline. */

enum yetty_ybrowser_request_kind {
    YETTY_YBROWSER_REQUEST_DOCUMENT, /* top-level page navigation */
    YETTY_YBROWSER_REQUEST_STYLE,    /* <link rel=stylesheet> */
    YETTY_YBROWSER_REQUEST_SCRIPT,   /* external <script src> */
    YETTY_YBROWSER_REQUEST_IMAGE,    /* <img> / background image */
    YETTY_YBROWSER_REQUEST_XHR,      /* JS fetch() / XMLHttpRequest */
};

struct yetty_ybrowser_request {
    const char *url; /* required; borrowed */
    enum yetty_ybrowser_request_kind kind;
    const char *referer; /* document URL, or NULL */

    /* fetch()/XHR extras — leave NULL/0 for a plain GET. */
    const char *method; /* "POST", "PUT", …; NULL/"GET" for GET */
    const char *body;   /* request body; copied by the fetcher */
    size_t body_len;
    const char *const *extra_headers; /* "Name: value" strings */
    int extra_header_count;

    /* Cancellation. When cancel_generation is non-NULL, the transfer
	 * aborts at the next progress tick or received chunk once
	 * *cancel_generation no longer equals generation (the engine bumps
	 * its counter on navigation). Atomic because workers read it while
	 * the loop thread increments. NULL → not cancellable. */
    uint64_t generation;
    const _Atomic uint64_t *cancel_generation;
};

struct yetty_ybrowser_response {
    long status; /* HTTP status; 200 for file://; 0 on transport failure */
    char *body;  /* malloc'd, NUL-terminated; NULL on failure */
    size_t body_len;
    char *effective_url; /* post-redirect URL; may be NULL */
    char *content_type;  /* Content-Type header value; may be NULL */
    int http_version;    /* negotiated wire version ×10 (11, 20, 30); 0 unknown */

    /* Decorations — attached by pipeline stages after the transfer,
	 * consumed at render time. */
    uint32_t *image_pixels; /* RGBA8, row-major */
    int image_width, image_height;
};

/* Free every owned member and zero the struct. Safe on NULL and on an
 * already-disposed response. */
void yetty_ybrowser_response_dispose(struct yetty_ybrowser_response *response);

/* Synchronous single fetch. The result is an error only for setup-level
 * failures (bad arguments, out of memory, curl init); network and HTTP
 * failures are data — inspect response->status / ->body. */
struct yetty_ycore_void_result yetty_ybrowser_fetch(struct yetty_ybrowser_loader *loader,
                                                    const struct yetty_ybrowser_request *request,
                                                    struct yetty_ybrowser_response *response);

/* Parallel batch fetch through one curl_multi: requests to the same
 * H2 origin multiplex over a single connection; `host_connection_cap`
 * bounds CONNECTIONS per host (matters for H1-only origins), not
 * streams. Failed slots come back zeroed (status 0, NULL body). */
struct yetty_ycore_void_result yetty_ybrowser_fetch_many(
    struct yetty_ybrowser_loader *loader, const struct yetty_ybrowser_request *requests, int count,
    struct yetty_ybrowser_response *responses, int host_connection_cap);

struct yetty_ylexbor_config {
    int viewport_width;      /* px */
    int viewport_height;     /* px (unused by current layout — content height is computed) */
    float default_font_size; /* px; falls back to 16 if 0 */
    /* Optional shared network loader (see above). NULL → the engine
	 * creates and owns a private one. Borrowed, never freed by the
	 * engine when supplied. */
    struct yetty_ybrowser_loader *loader;
};

struct yetty_ylexbor_ptr_result yetty_ylexbor_create(const struct yetty_ylexbor_config *cfg);
struct yetty_ycore_void_result yetty_ylexbor_destroy(struct yetty_ylexbor *r);

/* Set / replace the document. UTF-8 HTML; safe to call repeatedly to
 * replace previous content. */
struct yetty_ycore_void_result yetty_ylexbor_load_html(struct yetty_ylexbor *r, const char *html,
                                                       size_t html_len);

/* Append additional CSS that takes precedence over the document's own
 * <style>/<link> rules (origin = author). Handy for injecting a
 * theme. */
struct yetty_ycore_void_result yetty_ylexbor_add_css(struct yetty_ylexbor *r, const char *css,
                                                     size_t css_len);

/* Same, with the stylesheet's own absolute URL — @import rules inside the
 * sheet resolve against it (falling back to the document base when NULL).
 * The engine fetches, parses and cascades imported sheets recursively,
 * imported rules preceding the importing sheet's rules in source order. */
struct yetty_ycore_void_result yetty_ylexbor_add_css_from(struct yetty_ylexbor *r, const char *css,
                                                          size_t css_len, const char *sheet_url);

/* Resize the viewport — invalidates the layout. */
struct yetty_ycore_void_result yetty_ylexbor_set_viewport(struct yetty_ylexbor *r, int width,
                                                          int height);

/* Drain the laid-out document into a ydraw buffer. Caller owns buf;
 * this function appends primitives. */
struct yetty_ycore_void_result yetty_ylexbor_render(struct yetty_ylexbor *r,
                                                    struct yetty_ydraw_drawable_list *buf);

/* Outcome of an incremental (image-delta) repaint attempt — see
 * yetty_ylexbor_paint_image_deltas. */
struct yetty_ylexbor_incremental_result {
    /* 1 = `buf` holds only the changed image groups (CMD_GROUP records, NO
     *     leading CMD_ZERO). Ship it straight to the existing page figure so
     *     the receiver replaces just those entities; the rest of the page is
     *     untouched.
     * 0 = the relayout shifted page geometry (or no full render has run yet),
     *     so a delta would be wrong. `buf` is left empty; the caller must run
     *     its normal full render_doc path instead. */
    int is_delta;
    /* When is_delta: how many image groups were re-emitted into `buf`. 0 means
     * layout was stable but no image content actually changed — nothing to
     * ship. */
    int changed_groups;
};

/* Incremental repaint for streamed images. Relayouts, then — if the layout is
 * byte-for-byte the same as the last full render (images landed but nothing
 * moved) — appends a CMD_GROUP(id){…} for each image whose pixels changed
 * since it was last emitted, translated by (offset_x, offset_y) so the delta
 * lands at the same screen coords the full render produced (pass the page
 * embed's content offset). If the layout changed, returns is_delta = 0 and
 * writes nothing; the caller falls back to a full render. Caller owns `buf`. */
struct yetty_ylexbor_incremental_result yetty_ylexbor_paint_image_deltas(
    struct yetty_ylexbor *r, struct yetty_ydraw_drawable_list *buf, float offset_x, float offset_y);

/* Total content height after layout, in px. Useful for scrollbars. */
int yetty_ylexbor_content_height(const struct yetty_ylexbor *r);

/* Dispatch a synthetic 'click' event to JS handlers attached at (x,y).
 * Returns 1 if any handler fired. Caller should check
 * yetty_ylexbor_dom_dirty() afterwards and call _relayout() if set. */
int yetty_ylexbor_dispatch_click(struct yetty_ylexbor *r, float x, float y);

/* Page text input. A pointer click focuses the clicked text field (inside
 * dispatch_click); the host then routes keystrokes here rather than to its own
 * address bar. has_page_focus reports whether a page field currently holds
 * focus; dispatch_text inserts typed UTF-8 (fires the page's input events);
 * dispatch_key delivers an editing/navigation key by DOM key name
 * ("Backspace", "Enter", …) and returns whether it was consumed. */
int yetty_ylexbor_has_page_focus(struct yetty_ylexbor *r);
void yetty_ylexbor_dispatch_text(struct yetty_ylexbor *r, const char *utf8);
int yetty_ylexbor_dispatch_key(struct yetty_ylexbor *r, const char *key_name);

/* If a laid-out box at document coords (x, y) sits inside an <a>/<area>
 * with an href, return that href resolved against the document base URL
 * (caller frees). Returns NULL when there is no link there, or for an
 * in-page "#fragment" target. Works without JavaScript — it reads the
 * DOM/box tree directly — so plain hyperlinks are navigable. */
char *yetty_ylexbor_link_at(struct yetty_ylexbor *r, float x, float y);

/* First non-empty attribute `name` on the deepest element at document
 * coords (x, y) or its ancestors — malloc'd copy (caller frees), NULL
 * when absent. With a non-NULL `contains`, values lacking that substring
 * are skipped AND each ancestor's subtree is searched too: the payload
 * often sits on a sibling of the hit (e.g. gnews's article-URL jslog
 * lives on a separate overlay <a> inside the same story card as the
 * clicked headline). Generic companion to link_at for callers needing
 * element context around a hit. */
char *yetty_ylexbor_ancestor_attr_at(struct yetty_ylexbor *r, float x, float y, const char *name,
                                     const char *contains);

/* True iff JS mutated the DOM since the last relayout. Cleared by
 * yetty_ylexbor_relayout. */
int yetty_ylexbor_dom_dirty(const struct yetty_ylexbor *r);

/* A GPU repaint is owed. Set on every DOM/style mutation and, unlike dom_dirty,
 * NOT cleared by a geometry getter's forced layout flush — so the render loop
 * still repaints after JS that mutates then measures in one turn (e.g. an
 * overlay positioning itself then reading getBoundingClientRect). The host
 * consults this each tick and calls yetty_ylexbor_mark_painted() once it ships
 * the frame. */
int yetty_ylexbor_needs_paint(const struct yetty_ylexbor *r);
void yetty_ylexbor_mark_painted(struct yetty_ylexbor *r);

/* Returns any JS-initiated navigation target (location.assign/replace/reload or
 * `location.href = …`), transferring ownership; NULL when none is pending. The
 * host polls this each tick and drives a real navigate() with the result, then
 * frees it. This is what lets e.g. YouTube's consent flow reload into the site
 * after saving the choice. */
char *yetty_ylexbor_take_pending_navigation(struct yetty_ylexbor *r);

/* Companion to the above for form submissions: hands the host the HTTP method
 * and request body that accompany the pending navigation. Call right after
 * take_pending_navigation() returns non-NULL. *out_method is "POST"/etc. (NULL
 * for a GET), *out_body is the form-encoded body (NULL if none); both transfer
 * ownership. This is what makes the consent "Accept all" form actually POST to
 * the save endpoint, set the cookie, and navigate the returned page. */
void yetty_ylexbor_take_pending_nav_post(struct yetty_ylexbor *r, char **out_method,
                                         char **out_body, size_t *out_body_len, bool *out_reload);

/* Re-run box-build + layout. Cheap-ish (~ms for small docs). Called by
 * the host after a JS turn that mutated the DOM, or after a viewport
 * resize. Render() reads the same boxes. */
struct yetty_ycore_void_result yetty_ylexbor_relayout(struct yetty_ylexbor *r);

/* Set the base URL the loaded document was fetched from. Used to
 * resolve relative src= on external <script> and fetch() calls.
 * Optional — if not set, only absolute URLs work. */
struct yetty_ycore_void_result yetty_ylexbor_set_base_url(struct yetty_ylexbor *r, const char *url);

/* The loader's libcurl share handle (CURLSH*, returned as void* to keep
 * the public header free of <curl/curl.h>). Use it from external
 * fetchers that go through their own `curl_easy_init()` so the
 * connection they open stays warm for subsequent engine fetches:
 *     curl_easy_setopt(c, CURLOPT_SHARE, yetty_ybrowser_loader_curl_share(loader));
 * Without this, the page fetch in tools/ybrowser/main.c opens a TLS
 * connection to e.g. en.wikipedia.org and immediately discards it —
 * then load_external_stylesheets has to re-handshake to the same host
 * for the CSS fetch (~100ms wasted). Returns NULL when libcurl isn't
 * compiled in or loader is NULL. */
void *yetty_ybrowser_loader_curl_share(struct yetty_ybrowser_loader *loader);

/* Run any pending timers whose deadline has elapsed and drain Promise
 * microtasks. Returns milliseconds until the next timer fires (-1 if
 * none). The host calls this from its event loop. */
int yetty_ylexbor_pump_timers(struct yetty_ylexbor *r);

/* Set the per-glyph advance (fraction of font-size) the layout uses to
 * estimate text width and to position styled inline fragments on a line.
 * It must match the advance of the font the host renders with, or colored
 * link / bold / italic runs drift out of alignment. An interactive host
 * rendering with a monospace MSDF font passes that font's advance (~0.6);
 * pass 0 to keep the 0.55 default. */
void yetty_ylexbor_set_glyph_advance_ratio(struct yetty_ylexbor *r, float ratio);

/* Image loading mode. By default _render() fetches every <img> URL over
 * the network synchronously inside the paint pass — fine for a one-shot
 * render, but it blocks the caller's thread for as long as the HTTP takes
 * (seconds on an image-heavy page), which freezes an interactive host's
 * event loop. Set `on` to 1 to defer: paint draws placeholders for images
 * that aren't cached yet and returns immediately; the host then loads
 * images off the critical path with yetty_ylexbor_fetch_one_pending_image
 * and re-renders. `data:` URIs are unaffected (decoded inline). */
void yetty_ylexbor_set_defer_image_fetch(struct yetty_ylexbor *r, int on);

/* Script execution mode. By default yetty_ylexbor_load_html() runs every inline
 * + external <script> synchronously during the load, so the FIRST paint waits
 * for all of them (seconds of blank screen on a script-heavy page — 75 chunks
 * on github). Set `on` to 1 to DEFER: load_html parses HTML, applies CSS, and
 * lays out WITHOUT running scripts, so the host can paint the initial
 * HTML+CSS content immediately (progressive rendering, like any browser's
 * first-contentful-paint). The host then calls
 * yetty_ylexbor_run_deferred_scripts() once, after that first paint, to run the
 * scripts and relayout. */
void yetty_ylexbor_set_defer_scripts(struct yetty_ylexbor *r, int on);

/* Run the scripts that yetty_ylexbor_load_html() skipped under defer-scripts
 * mode, then re-resolve the box tree + layout from the (now script-mutated)
 * DOM. Call once after the initial paint. No-op (still relayouts) if defer mode
 * was off. After it returns the host should repaint. */
struct yetty_ycore_void_result yetty_ylexbor_run_deferred_scripts(struct yetty_ylexbor *r);

/* ===========================================================================
 * DevTools JavaScript console.
 *
 * The engine records every page console.{log,info,debug,warn,error} call and
 * every REPL evaluation into a bounded ring buffer. A host UI (the standalone
 * ybrowser DevTools panel) reads it back to render a live console, and feeds
 * typed expressions to yetty_ylexbor_eval_js() to evaluate them in the page's
 * own JS context (same globals, DOM and state the page's scripts see).
 * ===========================================================================*/

/* Severity / origin of a console line. LOG..ERROR mirror the console.* levels;
 * INPUT is a REPL expression the user typed; RESULT is its evaluated value. */
enum yetty_ylexbor_console_level {
    YETTY_YLEXBOR_CONSOLE_LOG = 0,
    YETTY_YLEXBOR_CONSOLE_INFO,
    YETTY_YLEXBOR_CONSOLE_DEBUG,
    YETTY_YLEXBOR_CONSOLE_WARN,
    YETTY_YLEXBOR_CONSOLE_ERROR,
    YETTY_YLEXBOR_CONSOLE_INPUT,
    YETTY_YLEXBOR_CONSOLE_RESULT,
};

/* Borrowed view of one console line. `text` is owned by the engine and stays
 * valid until the next clear / engine destroy — copy it if you need to keep it. */
struct yetty_ylexbor_console_view {
    int level; /* enum yetty_ylexbor_console_level */
    const char *text;
};

/* Monotonic count of console lines ever recorded (never decreases, survives a
 * clear). A UI polls this to notice new output without rescanning the ring. */
uint64_t yetty_ylexbor_console_total(const struct yetty_ylexbor *r);

/* Number of console lines currently retained (0..YETTY_YLEXBOR_CONSOLE_CAP). */
int yetty_ylexbor_console_count(const struct yetty_ylexbor *r);

/* Fetch a retained console line. `index` is 0 = oldest retained .. count-1 =
 * newest. Out-of-range returns {LOG, NULL}. */
struct yetty_ylexbor_console_view yetty_ylexbor_console_at(const struct yetty_ylexbor *r,
                                                           int index);

/* Drop all retained console lines (the "clear console" action). Leaves
 * console_total unchanged so the monotonic counter stays honest. */
void yetty_ylexbor_console_clear(struct yetty_ylexbor *r);

/* Evaluate `src` as top-level JavaScript in the page's context (initialising
 * the JS runtime if the page had no scripts). The typed source is recorded as
 * an INPUT line and the outcome as a RESULT (or ERROR) line, so a console UI
 * only has to re-read the ring. Also returns the displayed value as a freshly
 * allocated string (caller frees). Errors are reported as a value string, not
 * a Result error; the Result errors only for a NULL argument or OOM. When
 * QuickJS is compiled out this returns a short "JavaScript is not available"
 * string. */
struct yetty_ycore_char_ptr_result yetty_ylexbor_eval_js(struct yetty_ylexbor *r, const char *src);

/* ---------------------------------------------------------------------------
 * DOM inspector — walk the parsed document tree for the DevTools Elements view.
 * ------------------------------------------------------------------------- */

/* Called once per shown DOM node during a walk, in document (pre-order) order.
 * `depth` is 0 for the <html> root; a node's children are reported at depth+1
 * (contiguous — every reported node's parent was reported at depth-1).
 * `has_children` is 1 if the node has any child worth showing (so a UI knows to
 * make it an expandable branch). `label` is a display-ready string: elements as
 * `<div id="x" class="y">`, text nodes as a quoted, whitespace-collapsed
 * snippet. Return non-zero to stop the walk early (e.g. after a node cap). */
typedef int (*yetty_ylexbor_dom_visit_fn)(void *user, int depth, int has_children,
                                          const char *label);

/* Visit every shown node of the parsed document. No-op if nothing is parsed. */
void yetty_ylexbor_dom_walk(struct yetty_ylexbor *r, yetty_ylexbor_dom_visit_fn visit, void *user);

/* Fetch + decode at most ONE <img> whose URL isn't cached yet (the first
 * in document order), blocking only for that single image. Returns 1 if it
 * loaded one (the host should re-render to show it and call again for the
 * next), 0 when no images are pending. Only meaningful with defer mode on;
 * call it once per frame from the host loop to stream a page's images in
 * without ever blocking for the whole set at once. */
int yetty_ylexbor_fetch_one_pending_image(struct yetty_ylexbor *r);

/* Batch variant: fetch up to `max_count` pending images in one parallel
 * HTTP/2-multiplexed batch. Returns how many were processed (0 = page
 * fully loaded). Prefer this over the one-at-a-time call when the host
 * pumps per frame. */
int yetty_ylexbor_fetch_pending_images(struct yetty_ylexbor *r, int max_count);

struct yetty_yplatform_yworkpool;

/* Enable ASYNC parallel image fetching. `pool` is a worker pool created on the
 * host's event loop; once set, yetty_ylexbor_start_image_fetch submits each
 * pending <img> as a background fetch+decode job (parallel, non-blocking)
 * instead of blocking the caller. `on_ready(user)` is invoked on the loop
 * thread each time a fetch completes so the host can repaint. Pass pool=NULL to
 * disable (falls back to the synchronous one-at-a-time path). The pool and
 * `user` must outlive the engine, OR the engine outlive all in-flight jobs —
 * the engine defers its own teardown until in-flight jobs drain, so calling
 * yetty_ylexbor_destroy while fetches are running is safe. */
void yetty_ylexbor_set_async_image_fetch(struct yetty_ylexbor *r,
                                         struct yetty_yplatform_yworkpool *pool,
                                         void (*on_ready)(void *user), void *user);

/* Submit every not-yet-fetched <img> in the laid-out document to the async
 * pool (parallel fetch + decode). On success the result value is the number of
 * jobs submitted this call (0 if async isn't enabled or nothing is pending).
 * Returns an error if submitting a job to the work pool fails. Safe to call
 * every frame — already-cached / in-flight images are skipped. */
struct yetty_ycore_int_result yetty_ylexbor_start_image_fetch(struct yetty_ylexbor *r);

/* Number of async image fetch+decode jobs currently in flight (submitted but
 * not yet folded in). The host keeps its event loop ticking while this is > 0
 * so completions repaint promptly instead of waiting for an unrelated wake. */
int yetty_ylexbor_images_in_flight(const struct yetty_ylexbor *r);

/* Load-timeline profiler. When the YBROWSER_PROFILE env var is set, prof()
 * prints a timestamped event line to stderr; prof_now_ms() returns the
 * monotonic clock (ms) used to measure per-step durations. Exposed so the host
 * tool can profile the pieces outside the engine (HTML fetch, window/GPU
 * startup, first render). Near-zero overhead (one getenv) when off. */
void yetty_ylexbor_prof(const char *fmt, ...);
double yetty_ylexbor_prof_now_ms(void);

/* ===========================================================================
 * Test-only inspection.
 *
 * Probe the post-layout box vector by index. Returns 0 + fills the
 * out args on success, non-zero if the index is out of range. Used by
 * test/ut/ybrowser to pin layout positions for regression coverage —
 * not intended for production callers (the box vector representation
 * is internal and may change). `tag_out` is filled with the lowercased
 * element local name (e.g. "div", "p"); set to "" for anonymous boxes.
 * ===========================================================================*/
int yetty_ylexbor_test_box_count(const struct yetty_ylexbor *r);
int yetty_ylexbor_test_box_at(const struct yetty_ylexbor *r, int index, float *x, float *y,
                              float *w, float *h, char *tag_out, int tag_cap);

/* Test-only: the element's offsetLeft/offsetTop per CSSOM-View — border-edge
 * position relative to the nearest positioned ancestor's padding edge, or
 * absolute document coordinates when the offsetParent is the body. This is
 * what check-layout-th.js data-offset-x/-y assert against. */
int yetty_ylexbor_test_box_offset_at(const struct yetty_ylexbor *r, int index, float *offset_left,
                                     float *offset_top);

/* Box-kind constants returned in *kind_out by yetty_ylexbor_test_box_info_at.
 * Map directly to the internal yetty_ylexbor_box_kind enum. */
#define YETTY_YLEXBOR_BOX_KIND_BLOCK 0
#define YETTY_YLEXBOR_BOX_KIND_INLINE_TEXT 1
#define YETTY_YLEXBOR_BOX_KIND_INLINE_IMAGE 2

/* Test-only: introspect inline-text style + text content. Returns 0 +
 * fills the out args on success, non-zero on out-of-range. text_out is
 * filled with the box's UTF-8 text bytes (NUL-terminated, truncated to
 * text_cap-1). For YL_BOX_BLOCK / YL_BOX_INLINE_IMAGE boxes text_out is
 * empty and font_weight/italic/underline reflect the box's style snapshot.
 * Any out-pointer may be NULL — those fields are skipped. */
int yetty_ylexbor_test_box_info_at(const struct yetty_ylexbor *r, int index, int *kind_out,
                                   int *font_weight_out, int *italic_out, int *underline_out,
                                   char *text_out, int text_cap);

/* Test-only: fetch the box's `data-test` attribute (used by the Chrome
 * geometry oracle to key boxes by a stable name independent of DOM order).
 * Writes the NUL-terminated attribute value into out_buf (truncated to
 * cap-1) and returns 0 on success; returns non-zero if the index is out of
 * range or the box's element has no `data-test` attribute. out_buf is set
 * to "" on any non-success. */
int yetty_ylexbor_test_box_data_test_at(const struct yetty_ylexbor *r, int index, char *out_buf,
                                        int cap);

/* Test-only: size provenance — the short names of the layout decisions
 * that produced this box's final width and height ("css", "flex-shrink",
 * "abs-fit", ...). The pointers reference static storage; never freed.
 * Emitted by --dump-boxes so the anchor comparator can cluster geometry
 * divergences by the deciding engine branch. Returns 0 on success. */
int yetty_ylexbor_test_box_sources_at(const struct yetty_ylexbor *r, int index,
                                      const char **width_source_out,
                                      const char **height_source_out);

/* Short name for an enum yl_size_source value (internal enum; exposed for
 * the dump tool). */
const char *yetty_ylexbor_size_source_name(int source);

/* Test-only: read an arbitrary attribute (`attr`) off the box's element into
 * out_buf (NUL-terminated, truncated to cap-1). Returns 0 on success, non-zero
 * for anonymous boxes or a missing attribute. Used by the upstream-WPT runner
 * to read check-layout-th.js assertions (data-expected-width / -height /
 * data-offset-x / -y) directly off each box. */
int yetty_ylexbor_test_box_attr_at(const struct yetty_ylexbor *r, int index, const char *attr,
                                   char *out_buf, int cap);

/* Test-only: write an nth-of-type DOM path for the box's element into out_buf,
 * e.g. "html:1>body:1>div:2>main:1" (top-down, each segment
 * lowercase-tag ":" 1-based index among same-tag element siblings). Returns 0
 * on success, non-zero for anonymous/text boxes (no element). Used by the
 * Chrome geometry oracle to match ybrowser boxes to Chrome's
 * getBoundingClientRect by identical DOM path. */
int yetty_ylexbor_test_box_path_at(const struct yetty_ylexbor *r, int index, char *out_buf,
                                   int cap);

/* Serialize the current document's DOM tree to HTML, matching Chrome's
 * headless `--dump-dom`: the doctype (if present) as `<!DOCTYPE <name>>` on
 * its own line, then the post-script `<html>…</html>` serialization of the
 * root element. A doctype-less (quirks) document emits no prefix line.
 * Returns a heap-allocated, NUL-terminated buffer the caller must free(), or
 * NULL when there is no document element or on allocation failure. When
 * out_len is non-NULL it receives the byte length (excluding the terminating
 * NUL). Backs the `--dump-dom` CLI mode and DOM-parity tooling that diffs
 * ybrowser's DOM against Chrome's. */
char *yetty_ylexbor_serialize_dom(const struct yetty_ylexbor *engine, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YLEXBOR_H */
