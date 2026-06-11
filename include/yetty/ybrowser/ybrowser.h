#ifndef YETTY_YLEXBOR_H
#define YETTY_YLEXBOR_H

/*
 * ylexbor — permissively-licensed in-process HTML/CSS renderer for
 * yetty. Sits on top of lexbor (Apache-2.0): HTML parser, CSS parser,
 * selectors, computed-style cascade. Layout + paint live here.
 *
 * Status: MVP. Implements:
 *   - HTML5 parsing (via lexbor)
 *   - CSS parsing + selector matching + computed style (via lexbor)
 *   - Naive block-flow layout: vertical stacking of block-level elements,
 *     line-by-line text wrapping inside inline content
 *   - Painting: emit ydraw primitives (boxes + TEXT_DRAWABLE_LIST) into a buffer
 *
 * Explicitly NOT implemented (TODO, separate work):
 *   - Float layout
 *   - Flexbox / Grid / Tables (treated as plain block)
 *   - Background images, gradients, borders with radii
 *   - z-index / stacking contexts
 *   - position: absolute/fixed/sticky
 *   - Form input rendering (rendered as plain text)
 *   - JavaScript / DOM events
 *   - Image fetching (external resources skipped)
 *   - Animations / transitions
 *
 * License note: this module is BSL-1.1 (yetty's main license) — unlike
 * src/yetty/ynetsurf which is GPL-2.0 due to NetSurf core. ylexbor uses
 * only Apache-2.0 lexbor and yetty's own code, so it can be linked
 * directly into the main yetty binary.
 */

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

struct yetty_ylexbor_config {
    int viewport_width;      /* px */
    int viewport_height;     /* px (unused by current layout — content height is computed) */
    float default_font_size; /* px; falls back to 16 if 0 */
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

/* Resize the viewport — invalidates the layout. */
struct yetty_ycore_void_result yetty_ylexbor_set_viewport(struct yetty_ylexbor *r, int width,
                                                          int height);

/* Drain the laid-out document into a ydraw buffer. Caller owns buf;
 * this function appends primitives. */
struct yetty_ycore_void_result yetty_ylexbor_render(struct yetty_ylexbor *r,
                                                    struct yetty_ydraw_drawable_list *buf);

/* Total content height after layout, in px. Useful for scrollbars. */
int yetty_ylexbor_content_height(const struct yetty_ylexbor *r);

/* Dispatch a synthetic 'click' event to JS handlers attached at (x,y).
 * Returns 1 if any handler fired. Caller should check
 * yetty_ylexbor_dom_dirty() afterwards and call _relayout() if set. */
int yetty_ylexbor_dispatch_click(struct yetty_ylexbor *r, float x, float y);

/* If a laid-out box at document coords (x, y) sits inside an <a>/<area>
 * with an href, return that href resolved against the document base URL
 * (caller frees). Returns NULL when there is no link there, or for an
 * in-page "#fragment" target. Works without JavaScript — it reads the
 * DOM/box tree directly — so plain hyperlinks are navigable. */
char *yetty_ylexbor_link_at(struct yetty_ylexbor *r, float x, float y);

/* True iff JS mutated the DOM since the last relayout. Cleared by
 * yetty_ylexbor_relayout. */
int yetty_ylexbor_dom_dirty(const struct yetty_ylexbor *r);

/* Re-run box-build + layout. Cheap-ish (~ms for small docs). Called by
 * the host after a JS turn that mutated the DOM, or after a viewport
 * resize. Render() reads the same boxes. */
struct yetty_ycore_void_result yetty_ylexbor_relayout(struct yetty_ylexbor *r);

/* Set the base URL the loaded document was fetched from. Used to
 * resolve relative src= on external <script> and fetch() calls.
 * Optional — if not set, only absolute URLs work. */
struct yetty_ycore_void_result yetty_ylexbor_set_base_url(struct yetty_ylexbor *r, const char *url);

/* Process-wide libcurl share handle (CURLSH*, returned as void* to keep
 * the public header free of <curl/curl.h>). Use it from external
 * fetchers that go through their own `curl_easy_init()` so the
 * connection they open stays warm for subsequent yetty fetches:
 *     curl_easy_setopt(c, CURLOPT_SHARE, yetty_ylexbor_curl_share());
 * Without this, the page fetch in tools/ybrowser/main.c opens a TLS
 * connection to e.g. en.wikipedia.org and immediately discards it —
 * then load_external_stylesheets has to re-handshake to the same host
 * for the CSS fetch (~100ms wasted). Returns NULL when libcurl isn't
 * compiled in. */
void *yetty_ylexbor_curl_share(void);

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

/* Fetch + decode at most ONE <img> whose URL isn't cached yet (the first
 * in document order), blocking only for that single image. Returns 1 if it
 * loaded one (the host should re-render to show it and call again for the
 * next), 0 when no images are pending. Only meaningful with defer mode on;
 * call it once per frame from the host loop to stream a page's images in
 * without ever blocking for the whole set at once. */
int yetty_ylexbor_fetch_one_pending_image(struct yetty_ylexbor *r);

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
 * pool (parallel fetch + decode). Returns the number of jobs submitted this
 * call (0 if async isn't enabled or nothing is pending). Safe to call every
 * frame — already-cached / in-flight images are skipped. */
int yetty_ylexbor_start_image_fetch(struct yetty_ylexbor *r);

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

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YLEXBOR_H */
