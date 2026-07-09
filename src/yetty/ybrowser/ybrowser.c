/*
 * ylexbor — top-level lifecycle. Wires lexbor's HTML+CSS parsing to the
 * box-build → layout → paint pipeline implemented in the sibling files.
 */

#include "ybrowser-internal.h"
#include "ybrowser-libcss.h"

#include <stdlib.h>
#include <string.h>

#include <stdarg.h>
#include <stdio.h>
#include <time.h>
#include <lexbor/css/css.h>
#include <lexbor/style/style.h>
#include <lexbor/html/html.h>
#include <lexbor/dom/dom.h>
#include <lexbor/selectors/selectors.h>
#include <lexbor/tag/const.h>

#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/ytrace/ytrace.h>

/* ===========================================================================
 * Box vector — small dynamic array.
 * ===========================================================================*/

static struct yetty_ycore_void_result box_vec_reserve(struct yetty_ylexbor_box_vec *v,
                                                      uint32_t want)
{
    if (want <= v->cap) {
        return YETTY_OK_VOID();
    }
    uint32_t new_cap = v->cap ? v->cap * 2 : 16;
    while (new_cap < want) {
        new_cap *= 2;
    }
    void *p = realloc(v->data, new_cap * sizeof(*v->data));
    if (p == NULL) {
        return YETTY_ERR(yetty_ycore_void, "box vec OOM");
    }
    v->data = p;
    v->cap = new_cap;
    return YETTY_OK_VOID();
}

static void box_vec_clear(struct yetty_ylexbor_box_vec *v)
{
    /* Free any per-box heap (segments). wrap_inline_box frees segs as
	 * it consumes them, but on the re-layout / clear path we may be
	 * dropping the box vector with unwrapped INLINE_TEXT boxes still
	 * carrying their original seg arrays. */
    for (uint32_t i = 0; i < v->size; i++) {
        if (v->data[i].segs) {
            free(v->data[i].segs);
            v->data[i].segs = NULL;
            v->data[i].segs_count = 0;
        }
    }
    v->size = 0;
}

static void box_vec_destroy(struct yetty_ylexbor_box_vec *v)
{
    for (uint32_t i = 0; i < v->size; i++) {
        free(v->data[i].segs);
    }
    free(v->data);
    v->data = NULL;
    v->size = v->cap = 0;
}

static void kv_store_destroy(struct yetty_ylexbor_kv_store *store)
{
    for (int i = 0; i < store->count; i++) {
        free(store->items[i].key);
        free(store->items[i].value);
    }
    free(store->items);
    store->items = NULL;
    store->count = store->cap = 0;
}

/* ===========================================================================
 * Text arena
 * ===========================================================================*/

const char *yetty_ylexbor_arena_dup(struct yetty_ylexbor *r, const char *bytes, size_t len)
{
    if (len == 0) {
        return "";
    }
    /* Each fragment gets its own malloc so the returned pointer is
	 * stable for the document's lifetime. (The old realloc'd arena
	 * silently invalidated every previously-returned pointer when it
	 * grew — visible as random garbage characters in painted text on
	 * pages with many text nodes.) */
    char *out = malloc(len);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, bytes, len);
    if (r->text_chunks_count == r->text_chunks_cap) {
        size_t new_cap = r->text_chunks_cap ? r->text_chunks_cap * 2 : 256;
        char **p = realloc(r->text_chunks, new_cap * sizeof(*p));
        if (p == NULL) {
            free(out);
            return NULL;
        }
        r->text_chunks = p;
        r->text_chunks_cap = new_cap;
    }
    r->text_chunks[r->text_chunks_count++] = out;
    return out;
}

static void arena_reset(struct yetty_ylexbor *r)
{
    for (size_t i = 0; i < r->text_chunks_count; i++) {
        free(r->text_chunks[i]);
    }
    r->text_chunks_count = 0;
}

/* ===========================================================================
 * Naive text width — placeholder, will become FreeType-driven later.
 * Good enough for the same MVP layout shape ynetsurf uses.
 * ===========================================================================*/

float yetty_ylexbor_glyph_advance_ratio(const struct yetty_ylexbor *r)
{
    if (r != NULL && r->glyph_advance_ratio > 0.0f) {
        return r->glyph_advance_ratio;
    }
    return 0.55f;
}

float yetty_ylexbor_naive_text_width(const char *s, size_t len, float font_size,
                                     float advance_ratio)
{
    int n = 0;
    for (size_t i = 0; i < len;) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) {
            i += 1;
        } else if ((c & 0xE0) == 0xC0) {
            i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            i += 3;
        } else if ((c & 0xF8) == 0xF0) {
            i += 4;
        } else {
            i += 1;
        }
        n++;
    }
    if (advance_ratio <= 0.0f) {
        advance_ratio = 0.55f;
    }
    float per_glyph = font_size * advance_ratio;
    if (per_glyph < 1.0f) {
        per_glyph = 1.0f;
    }
    return n * per_glyph;
}

/* ===========================================================================
 * Public lifecycle
 * ===========================================================================*/

/* Unwind a partially-constructed engine on a create failure — releases
 * the private loader (if one was made) and the engine allocation. */
static void create_fail_cleanup(struct yetty_ylexbor *r)
{
    if (r->owns_loader) {
        struct yetty_ycore_void_result loader_res = yetty_ybrowser_loader_destroy(r->loader);
        if (YETTY_IS_ERR(loader_res)) {
            yetty_ycore_error_destroy(loader_res.error);
        }
    }
    free(r);
}

struct yetty_ylexbor_ptr_result yetty_ylexbor_create(const struct yetty_ylexbor_config *cfg)
{
    struct yetty_ylexbor *r = calloc(1, sizeof(*r));
    if (r == NULL) {
        return YETTY_ERR(yetty_ylexbor_ptr, "ylexbor alloc");
    }

    /* Network loader: borrow the host's (shared connection pool across
	 * engines) or create a private one. */
    if (cfg && cfg->loader) {
        r->loader = cfg->loader;
        r->owns_loader = 0;
    } else {
        struct yetty_ybrowser_loader_ptr_result loader_res = yetty_ybrowser_loader_create();
        if (YETTY_IS_ERR(loader_res)) {
            free(r);
            return YETTY_ERR(yetty_ylexbor_ptr, "ylexbor_create: loader", loader_res);
        }
        r->loader = loader_res.value;
        r->owns_loader = 1;
    }

    r->viewport_w = cfg && cfg->viewport_width > 0 ? cfg->viewport_width : 1024;
    r->viewport_h = cfg && cfg->viewport_height > 0 ? cfg->viewport_height : 768;
    r->default_font_size = cfg && cfg->default_font_size > 0 ? cfg->default_font_size : 16.0f;

    r->document = lxb_html_document_create();
    if (r->document == NULL) {
        create_fail_cleanup(r);
        return YETTY_ERR(yetty_ylexbor_ptr, "html_document_create");
    }
    /* Deliberately NOT calling lxb_style_init(): ybrowser cascades through
     * libcss, not lexbor. With the style subsystem enabled, lexbor eagerly
     * built and applied its own full cascade (element_styles_attach) for every
     * <style>/<link> during parsing and stylesheet attach — matching selectors
     * against every element — which dominated load time on large pages
     * (~60% on a news.google.com topic page) yet produced a result nothing
     * reads: box-build uses libcss_select, getComputedStyle/el.style read the
     * raw `style` attribute, querySelector uses its own selector engine, and
     * <style> text stays a DOM text node fed to libcss by the load walk. Leaving
     * the subsystem off also means <style>->stylesheet stays NULL, so
     * node_remove_safe's guard always takes the crash-free removal path. */

    /* libcss bridge — fatal init failure leaves r->libcss NULL and
     * the box pass falls back to lexbor's serialized-cascade path
     * (same code that ylexbor uses today). */
    if (yetty_ybrowser_libcss_init(r) != 0) {
        ydebug("ybrowser: libcss init failed, using lexbor cascade fallback");
    }

    return YETTY_OK(yetty_ylexbor_ptr, r);
}

/* The real teardown. Split out so the public destroy can DEFER it until any
 * in-flight async image-fetch jobs drain — their done() callback runs on the
 * loop thread and must never touch a freed engine. */
struct yetty_ycore_void_result _yetty_ylexbor_destroy_now(struct yetty_ylexbor *r)
{
    yetty_ylexbor_js_destroy(r);
    yetty_ybrowser_libcss_destroy(r);
    if (r->css_parser) {
        lxb_css_parser_destroy(r->css_parser, true);
    }
    if (r->document) {
        lxb_html_document_destroy(r->document);
    }
    box_vec_destroy(&r->boxes);
    arena_reset(r);
    yetty_ylexbor_css_media_map_end(r); /* no-op unless a scan was interrupted */
    yetty_ylexbor_grid_classes_free(r);
    free(r->text_chunks);
    free(r->base_url);
    kv_store_destroy(&r->web_local_storage);
    kv_store_destroy(&r->web_session_storage);
    free(r->web_cookie_string);
    if (r->owns_loader) {
        struct yetty_ycore_void_result loader_res = yetty_ybrowser_loader_destroy(r->loader);
        if (YETTY_IS_ERR(loader_res)) {
            yetty_ycore_error_destroy(loader_res.error);
        }
    }
    for (int i = 0; i < r->img_cache_count; i++) {
        free(r->img_cache[i].url);
        free(r->img_cache[i].pixels);
        free(r->img_cache[i].scaled_pixels);
        yetty_ydraw_drawable_list_destroy(r->img_cache[i].svg_scene);
    }
    free(r->img_cache);
    yetty_ylexbor_svg_inline_cache_clear(r);
    yetty_ylexbor_group_ids_clear(r);
    if (r->supp_selector_matcher) {
        lxb_selectors_destroy((lxb_selectors_t *)r->supp_selector_matcher, true);
        r->supp_selector_matcher = NULL;
    }
    yetty_ylexbor_css_vars_destroy(r);
    free(r);
    return YETTY_OK_VOID();
}

/* Load-timeline profiler. When YBROWSER_PROFILE is set in the environment,
 * print a timestamped line to stderr for each significant load event (phase
 * boundary, HTTP request, JS fetch). Absolute monotonic milliseconds — read
 * consecutive lines to see where the wall-clock goes. Thread-safe; a no-op
 * (just a getenv) when profiling is off. Intentionally getenv-per-call so
 * there's no file-scope cache variable. */
double yetty_ylexbor_prof_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1.0e6;
}

void yetty_ylexbor_prof(const char *fmt, ...)
{
    if (getenv("YBROWSER_PROFILE") == NULL) {
        return;
    }
    flockfile(stderr);
    fprintf(stderr, "[PROF %10.1f] ", yetty_ylexbor_prof_now_ms());
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    funlockfile(stderr);
}

struct yetty_ycore_void_result yetty_ylexbor_destroy(struct yetty_ylexbor *r)
{
    if (r == NULL) {
        return YETTY_OK_VOID();
    }
    /* Async fetches outstanding: defer the teardown. The last job's done()
     * (loop thread) will call _yetty_ylexbor_destroy_now once the count hits 0.
     * The caller must not touch `r` after this returns. */
    if (r->img_jobs_in_flight > 0) {
        r->destroy_pending = 1;
        return YETTY_OK_VOID();
    }
    return _yetty_ylexbor_destroy_now(r);
}

struct yetty_ycore_void_result yetty_ylexbor_set_base_url(struct yetty_ylexbor *r, const char *url)
{
    if (r == NULL) {
        return YETTY_ERR(yetty_ycore_void, "ylexbor_set_base_url: null r");
    }
    free(r->base_url);
    r->base_url = url ? strdup(url) : NULL;
    return YETTY_OK_VOID();
}

void yetty_ylexbor_set_glyph_advance_ratio(struct yetty_ylexbor *r, float ratio)
{
    if (r != NULL) {
        r->glyph_advance_ratio = ratio;
    }
}

int yetty_ylexbor_pump_timers(struct yetty_ylexbor *r)
{
    return yetty_ylexbor_pump(r);
}

/* Internal accessor — the WPT integration runner needs the raw
 * JSContext to read back results from globalThis.
 *
 * NOT part of the public ylexbor API; the test runner is the only
 * legitimate caller. Anyone else reaching for this should add a real
 * read-back function rather than poking JS state directly. */
void *yetty_ylexbor_internal_get_js_ctx(struct yetty_ylexbor *r)
{
    return r ? r->js_ctx : NULL;
}

/* One CSS source to apply. EITHER `inline_body` is set (an inline
 * <style> block — body is owned, will be freed after add_css) OR `url`
 * is set (external <link>; after the parallel-fetch step, the matching
 * slot in the response array carries the fetched body).
 * Document order across the two types is preserved by appending to a
 * single list during the DOM walk — CSS cascade specificity depends
 * on source order, so we must apply inline + external in the order
 * they appear. */
struct css_entry {
    int is_external;
    char *url;         /* owned when is_external — freed after fetch */
    char *inline_body; /* owned when !is_external — freed after add_css */
    size_t inline_len;
};

struct css_collect {
    struct css_entry *items;
    int count, cap;
};

static void css_collect_push(struct css_collect *cc, struct css_entry e)
{
    if (cc->count == cc->cap) {
        int nc = cc->cap ? cc->cap * 2 : 8;
        struct css_entry *p = realloc(cc->items, (size_t)nc * sizeof(*p));
        if (!p) {
            free(e.url);
            free(e.inline_body);
            return;
        }
        cc->items = p;
        cc->cap = nc;
    }
    cc->items[cc->count++] = e;
}

/* DOM walker — recursively appends <link rel=stylesheet> and <style>
 * to `cc->items` in document order. */
static void css_collect_walk(struct yetty_ylexbor *r, lxb_dom_node_t *node, struct css_collect *cc)
{
    for (lxb_dom_node_t *c = node->first_child; c != NULL; c = c->next) {
        if (c->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            lxb_dom_element_t *el = lxb_dom_interface_element(c);
            if (c->local_name == LXB_TAG_LINK) {
                size_t rl = 0;
                const lxb_char_t *rel =
                    lxb_dom_element_get_attribute(el, (const lxb_char_t *)"rel", 3, &rl);
                if (rel && rl == 10 && strncasecmp((const char *)rel, "stylesheet", 10) == 0) {
                    size_t hl = 0;
                    const lxb_char_t *href =
                        lxb_dom_element_get_attribute(el, (const lxb_char_t *)"href", 4, &hl);
                    if (href && hl > 0) {
                        char *h = malloc(hl + 1);
                        if (h) {
                            memcpy(h, href, hl);
                            h[hl] = '\0';
                            char *url = yetty_ylexbor_resolve_url(r, h);
                            free(h);
                            if (url) {
                                struct css_entry e = {.is_external = 1, .url = url};
                                css_collect_push(cc, e);
                            }
                        }
                    }
                }
            } else if (c->local_name == LXB_TAG_STYLE) {
                size_t total = 0;
                for (lxb_dom_node_t *t = c->first_child; t; t = t->next) {
                    if (t->type == LXB_DOM_NODE_TYPE_TEXT) {
                        total += lxb_dom_interface_text(t)->char_data.data.length;
                    }
                }
                if (total > 0) {
                    char *css = malloc(total + 1);
                    if (css) {
                        size_t off = 0;
                        for (lxb_dom_node_t *t = c->first_child; t; t = t->next) {
                            if (t->type != LXB_DOM_NODE_TYPE_TEXT) {
                                continue;
                            }
                            lxb_dom_text_t *tn = lxb_dom_interface_text(t);
                            size_t n = tn->char_data.data.length;
                            memcpy(css + off, tn->char_data.data.data, n);
                            off += n;
                        }
                        css[off] = '\0';
                        struct css_entry e = {
                            .is_external = 0, .inline_body = css, .inline_len = off};
                        css_collect_push(cc, e);
                    }
                }
            }
        }
        if (c->first_child) {
            css_collect_walk(r, c, cc);
        }
    }
}

/* Two-phase stylesheet load:
 *   1) DOM walk → collect <style> + <link rel=stylesheet> in order.
 *   2) Parallel-fetch every external URL via curl_multi.
 *   3) Apply each entry in collected order so CSS cascade specificity
 *      sees the same source ordering it would under sequential fetch.
 *
 * The big win is phase 2 — Wikipedia's two external sheets used to
 * fetch one after the other (~100ms RTT each = 200ms). Multiplexed
 * over a single HTTP/2 connection they finish in one RTT. */
static struct yetty_ycore_void_result load_external_stylesheets(struct yetty_ylexbor *r,
                                                                lxb_dom_node_t *node)
{
    struct css_collect cc = {0};
    css_collect_walk(r, node, &cc);
    if (cc.count == 0) {
        return YETTY_OK_VOID();
    }

    /* Count externals + allocate fetch I/O arrays. */
    int ext_n = 0;
    for (int i = 0; i < cc.count; i++) {
        if (cc.items[i].is_external) {
            ext_n++;
        }
    }
    struct yetty_ybrowser_request *fetch_requests = NULL;
    struct yetty_ybrowser_response *fetch_responses = NULL;
    int *slot_to_entry = NULL;
    if (ext_n > 0) {
        fetch_requests = calloc((size_t)ext_n, sizeof(*fetch_requests));
        fetch_responses = calloc((size_t)ext_n, sizeof(*fetch_responses));
        slot_to_entry = calloc((size_t)ext_n, sizeof(*slot_to_entry));
        if (!fetch_requests || !fetch_responses || !slot_to_entry) {
            free(fetch_requests);
            free(fetch_responses);
            free(slot_to_entry);
            fetch_requests = NULL;
            fetch_responses = NULL;
            slot_to_entry = NULL;
            ext_n = 0;
        } else {
            int j = 0;
            for (int i = 0; i < cc.count; i++) {
                if (cc.items[i].is_external) {
                    fetch_requests[j].url = cc.items[i].url;
                    fetch_requests[j].kind = YETTY_YBROWSER_REQUEST_STYLE;
                    fetch_requests[j].referer = r->base_url;
                    slot_to_entry[j] = i;
                    j++;
                }
            }
            struct yetty_ycore_void_result many_res = yetty_ybrowser_fetch_many(
                r->loader, fetch_requests, ext_n, fetch_responses, /*host_connection_cap=*/8);
            if (YETTY_IS_ERR(many_res)) {
                yetty_ycore_error_destroy(many_res.error);
            }
        }
    }

    /* Apply each entry in document order. The per-entry frees below must
	 * run for every entry, so on an add_css failure stash the first error
	 * and keep cleaning up rather than bailing mid-loop. */
    struct yetty_ycore_void_result apply_res = YETTY_OK_VOID();
    for (int i = 0; i < cc.count; i++) {
        struct css_entry *e = &cc.items[i];
        if (e->is_external) {
            /* Locate the matching slot. */
            int slot = -1;
            for (int s = 0; s < ext_n; s++) {
                if (slot_to_entry[s] == i) {
                    slot = s;
                    break;
                }
            }
            struct yetty_ybrowser_response *response = slot >= 0 ? &fetch_responses[slot] : NULL;
            if (response && response->body && response->status >= 200 && response->status < 300) {
                /* The sheet's own (post-redirect) URL anchors @import
				 * resolution inside it. */
                const char *sheet_url = response->effective_url ? response->effective_url : e->url;
                struct yetty_ycore_void_result ar =
                    yetty_ylexbor_add_css_from(r, response->body, response->body_len, sheet_url);
                if (YETTY_IS_ERR(ar)) {
                    if (YETTY_IS_OK(apply_res)) {
                        apply_res =
                            YETTY_ERR(yetty_ycore_void, "load_external_stylesheets: add_css", ar);
                    } else {
                        yetty_ycore_error_destroy(ar.error);
                    }
                } else {
                    r->css_sheets_loaded++;
                }
            } else {
                r->css_sheets_failed++;
            }
            if (response) {
                yetty_ybrowser_response_dispose(response);
            }
            free(e->url);
        } else {
            struct yetty_ycore_void_result ar =
                yetty_ylexbor_add_css(r, e->inline_body, e->inline_len);
            if (YETTY_IS_ERR(ar)) {
                if (YETTY_IS_OK(apply_res)) {
                    apply_res = YETTY_ERR(yetty_ycore_void,
                                          "load_external_stylesheets: add_css inline", ar);
                } else {
                    yetty_ycore_error_destroy(ar.error);
                }
            } else {
                r->css_sheets_inline++;
            }
            free(e->inline_body);
        }
    }
    free(fetch_requests);
    free(fetch_responses);
    free(slot_to_entry);
    free(cc.items);
    return apply_res;
}

struct yetty_ycore_void_result yetty_ylexbor_load_html(struct yetty_ylexbor *r, const char *html,
                                                       size_t html_len)
{
    if (r == NULL || html == NULL) {
        return YETTY_ERR(yetty_ycore_void, "ylexbor_load_html: null");
    }

    /* Replace the document — fresh parser state, drop any prior boxes. */

    /* The re-parse below frees every node of the old DOM and recycles the
	 * memory for the new one. The JS world is full of raw pointers into
	 * that old tree — wrapper opaques, the listener pool, timer callbacks
	 * closing over old elements — so it must die with the document. A
	 * surviving timer from the previous page would otherwise mutate
	 * recycled memory and corrupt the new DOM. Torn down while the old
	 * document is still alive so the job-drain inside can run safely;
	 * the next script run lazily re-creates the runtime against the new
	 * document. */
    yetty_ylexbor_js_destroy(r);

    box_vec_clear(&r->boxes);
    arena_reset(r);
    yetty_ylexbor_grid_classes_free(r);
    r->grid_content_max_px = 0.0f;
    r->content_height = 0;
    /* Invalidate any in-flight async image jobs from the previous document —
     * their done() will find a mismatched generation and discard. */
    r->fetch_generation++;
    /* Inline-<svg> scenes are keyed by element pointers that die with the
	 * old parse — drop them before the new document takes over. */
    yetty_ylexbor_svg_inline_cache_clear(r);
    /* Same for the element→group-id map (keyed by the same dying pointers).
	 * next_group_id keeps climbing so a new document never reuses an old id. */
    yetty_ylexbor_group_ids_clear(r);
    r->css_sheets_loaded = 0;
    r->css_sheets_failed = 0;
    r->css_sheets_inline = 0;

    yetty_ylexbor_prof("load_html START  html_bytes=%zu", html_len);
    double t_phase = yetty_ylexbor_prof_now_ms();

    /* New DOM coming — drop the cached document class set so the custom-
     * property scanner rebuilds it from this document. */
    yetty_ylexbor_css_vars_reset_doc_classes(r);

    lxb_status_t s = lxb_html_document_parse(r->document, (const lxb_char_t *)html, html_len);
    if (s != LXB_STATUS_OK) {
        return YETTY_ERR(yetty_ycore_void, "html_document_parse failed");
    }
    yetty_ylexbor_prof("  parse          %.0f ms", yetty_ylexbor_prof_now_ms() - t_phase);
    t_phase = yetty_ylexbor_prof_now_ms();

    /* MediaWiki pages get a small float-helper stylesheet so offline Wikipedia
	 * still lays out as paragraph-with-sidebar. Detect by the `mw-` class
	 * prefix — unique to MediaWiki output. We deliberately do NOT inject these
	 * generic-class floats (`.thumb`, `.infobox`) into ordinary sites: doing so
	 * floated e.g. a news card's `.thumb` out of flow. */
    {
        int has_mw = 0;
        if (html_len >= 3) {
            for (size_t i = 0; i + 3 <= html_len; i++) {
                if (html[i] == 'm' && html[i + 1] == 'w' && html[i + 2] == '-') {
                    has_mw = 1;
                    break;
                }
            }
        }
        if (has_mw) {
            (void)yetty_ybrowser_libcss_apply_wikipedia_quirks(r);
        }
    }

    /* Pull every external CSS referenced via <link rel=stylesheet>
	 * into the cascade. Done before scripts run so getComputedStyle
	 * reads make sense; done before box-build so colored backgrounds
	 * land on the boxes we paint. Skipped silently when libcurl is
	 * unavailable or a fetch errors. */
    struct yetty_ycore_void_result css_res =
        load_external_stylesheets(r, lxb_dom_interface_node(r->document));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, css_res, "load_html: load_external_stylesheets");
    yetty_ylexbor_prof("  external CSS   %.0f ms", yetty_ylexbor_prof_now_ms() - t_phase);
    t_phase = yetty_ylexbor_prof_now_ms();

    /* Run inline + external <script> blocks — UNLESS defer-scripts mode is on,
	 * in which case the host paints the initial HTML/CSS first and calls
	 * yetty_ylexbor_run_deferred_scripts() afterward (progressive rendering). */
    if (!r->defer_scripts && getenv("YBROWSER_NO_JS") == NULL) {
        (void)yetty_ylexbor_js_run_inline_scripts(r);
    }
    yetty_ylexbor_prof("  run scripts    %.0f ms", yetty_ylexbor_prof_now_ms() - t_phase);
    t_phase = yetty_ylexbor_prof_now_ms();

    ydebug("css sheets ext=%d inline=%d failed=%d customs=%d", r->css_sheets_loaded,
           r->css_sheets_inline, r->css_sheets_failed, r->customs.size);
    for (int i = 0; i < r->customs.size; i++) {
        ydebug("css   %s = %s", r->customs.data[i].name, r->customs.data[i].value);
    }

    struct yetty_ycore_void_result br = yetty_ylexbor_box_build(r);
    if (YETTY_IS_ERR(br)) {
        return br;
    }
    yetty_ylexbor_prof("  box-build      %.0f ms (boxes=%u)", yetty_ylexbor_prof_now_ms() - t_phase,
                       r->boxes.size);
    t_phase = yetty_ylexbor_prof_now_ms();

    struct yetty_ycore_void_result lr = yetty_ylexbor_layout(r);
    if (YETTY_IS_ERR(lr)) {
        return lr;
    }
    yetty_ylexbor_prof("  layout         %.0f ms", yetty_ylexbor_prof_now_ms() - t_phase);
    yetty_ylexbor_prof("load_html DONE");

    (void)box_vec_reserve; /* used by box-build */
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ylexbor_add_css_from(struct yetty_ylexbor *r, const char *css,
                                                          size_t css_len, const char *sheet_url)
{
    if (r == NULL || css == NULL) {
        return YETTY_ERR(yetty_ycore_void, "ylexbor_add_css: null");
    }

    /* Expand `flex: …` shorthands into longhands FIRST — libcss parses
	 * flex-grow/-shrink/-basis but not the shorthand, and stylesheets are
	 * where real pages set flex. The rewritten copy (when any expansion
	 * happened) feeds every consumer below: scanners, libcss, lexbor. */
    size_t expanded_len = 0;
    char *expanded_css = yetty_ylexbor_css_expand_flex(css, css_len, &expanded_len);
    if (expanded_css != NULL) {
        css = expanded_css;
        css_len = expanded_len;
    }

    /* Pre-scan for `:root { --x: y; }` etc. before lexbor parses,
	 * so var() lookups see the latest definitions. */
    yetty_ylexbor_css_vars_scan(r, css, css_len);
    /* Build the @media-active map once for this source so each per-declaration
     * scanner below tests media context in O(log n) instead of re-walking the
     * prefix (which is O(n^2) per sheet — the dominant cost on big pages). */
    yetty_ylexbor_css_media_map_begin(r, css, css_len);
    /* Also note any grid content-column cap (minmax(0, Nrem)) — applied as
     * a max-width on display:grid containers since we don't lay out grid
     * tracks. */
    yetty_ylexbor_css_scan_grid_content_width(r, css, css_len);
    yetty_ylexbor_css_scan_grid_templates(r, css, css_len);
    yetty_ylexbor_css_scan_grid_spans(r, css, css_len);
    yetty_ylexbor_css_scan_flex_gaps(r, css, css_len);
    yetty_ylexbor_css_scan_var_heights(r, css, css_len);
    yetty_ylexbor_css_scan_width_keywords(r, css, css_len);
    yetty_ylexbor_css_scan_calc_lengths(r, css, css_len);
    yetty_ylexbor_css_scan_aspect_ratios(r, css, css_len);
    yetty_ylexbor_css_scan_display_none(r, css, css_len);
    yetty_ylexbor_css_scan_line_clamps(r, css, css_len);
    yetty_ylexbor_css_scan_transforms(r, css, css_len);
    yetty_ylexbor_css_media_map_end(r);

    /* Push the CSS through libcss — this is the cascade box-build actually
     * reads; the sheet URL anchors @import resolution. We deliberately do NOT
     * also parse+attach a lexbor stylesheet (lxb_html_document_stylesheet_attach)
     * here: lexbor's cascade is never read (see the lxb_style_init note in
     * yetty_ylexbor_create) and applying it per element was the dominant load
     * cost on large pages. */
    (void)yetty_ybrowser_libcss_add_sheet(r, css, css_len, CSS_ORIGIN_AUTHOR, sheet_url);

    free(expanded_css);
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ylexbor_add_css(struct yetty_ylexbor *r, const char *css,
                                                     size_t css_len)
{
    /* No sheet URL — inline <style> and API callers; @import inside these
	 * resolves against the document base. */
    return yetty_ylexbor_add_css_from(r, css, css_len, NULL);
}

struct yetty_ycore_void_result yetty_ylexbor_set_viewport(struct yetty_ylexbor *r, int width,
                                                          int height)
{
    if (r == NULL) {
        return YETTY_ERR(yetty_ycore_void, "null");
    }
    r->viewport_w = width > 0 ? width : r->viewport_w;
    r->viewport_h = height > 0 ? height : r->viewport_h;
    if (r->boxes.size > 0) {
        struct yetty_ycore_void_result lr = yetty_ylexbor_layout(r);
        if (YETTY_IS_ERR(lr)) {
            return lr;
        }
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ylexbor_render(struct yetty_ylexbor *r,
                                                    struct yetty_ydraw_drawable_list *buf)
{
    if (r == NULL || buf == NULL) {
        return YETTY_ERR(yetty_ycore_void, "ylexbor_render: null");
    }
    return yetty_ylexbor_paint(r, buf);
}

int yetty_ylexbor_content_height(const struct yetty_ylexbor *r)
{
    return r ? r->content_height : 0;
}

int yetty_ylexbor_dom_dirty(const struct yetty_ylexbor *r)
{
    return r ? r->dom_dirty : 0;
}

/* Re-resolve box tree + layout from the (possibly mutated) DOM.
 * Used by the host after a JS turn that flipped r->dom_dirty, OR
 * directly after a viewport change. */
struct yetty_ycore_void_result yetty_ylexbor_relayout(struct yetty_ylexbor *r)
{
    if (r == NULL) {
        return YETTY_ERR(yetty_ycore_void, "null");
    }
    r->dom_dirty = 0;
    struct yetty_ycore_void_result br = yetty_ylexbor_box_build(r);
    if (YETTY_IS_ERR(br)) {
        return br;
    }
    return yetty_ylexbor_layout(r);
}

void yetty_ylexbor_set_defer_scripts(struct yetty_ylexbor *r, int on)
{
    if (r != NULL) {
        r->defer_scripts = on ? 1 : 0;
    }
}

struct yetty_ycore_void_result yetty_ylexbor_run_deferred_scripts(struct yetty_ylexbor *r)
{
    if (r == NULL) {
        return YETTY_ERR(yetty_ycore_void, "ylexbor_run_deferred_scripts: null");
    }
    /* Run the <script> blocks load_html skipped, then rebuild the box tree +
	 * layout from the (now script-mutated) DOM so the next paint shows the
	 * scripted result. */
    if (getenv("YBROWSER_NO_JS") == NULL) {
        (void)yetty_ylexbor_js_run_inline_scripts(r);
    }
    return yetty_ylexbor_relayout(r);
}

/* Make box_vec_reserve visible to box-build. Static-but-shared via
 * attribute would be cleaner; this single-TU project uses a header
 * shim. */
struct yetty_ycore_void_result _yetty_ylexbor_box_vec_reserve(struct yetty_ylexbor_box_vec *v,
                                                              uint32_t want)
{
    return box_vec_reserve(v, want);
}

/* Test-only — see header. */
int yetty_ylexbor_test_box_count(const struct yetty_ylexbor *r)
{
    if (r == NULL) {
        return 0;
    }
    return (int)r->boxes.size;
}

int yetty_ylexbor_test_box_at(const struct yetty_ylexbor *r, int index, float *x, float *y,
                              float *w, float *h, char *tag_out, int tag_cap)
{
    if (r == NULL || index < 0 || (uint32_t)index >= r->boxes.size) {
        return -1;
    }
    const struct yetty_ylexbor_box *b = &r->boxes.data[index];
    if (x) {
        *x = b->x;
    }
    if (y) {
        *y = b->y;
    }
    if (w) {
        *w = b->w;
    }
    if (h) {
        *h = b->h;
    }
    if (tag_out && tag_cap > 0) {
        tag_out[0] = '\0';
        if (b->element) {
            size_t nlen = 0;
            const unsigned char *nm = lxb_dom_element_local_name(b->element, &nlen);
            if (nm && nlen > 0) {
                int n = nlen < (size_t)(tag_cap - 1) ? (int)nlen : tag_cap - 1;
                for (int i = 0; i < n; i++) {
                    tag_out[i] = (char)nm[i];
                }
                tag_out[n] = '\0';
            }
        }
    }
    return 0;
}

int yetty_ylexbor_test_box_info_at(const struct yetty_ylexbor *r, int index, int *kind_out,
                                   int *font_weight_out, int *italic_out, int *underline_out,
                                   char *text_out, int text_cap)
{
    if (r == NULL || index < 0 || (uint32_t)index >= r->boxes.size) {
        return -1;
    }
    const struct yetty_ylexbor_box *b = &r->boxes.data[index];
    if (kind_out) {
        *kind_out = (int)b->kind;
    }
    if (font_weight_out) {
        *font_weight_out = b->font_weight;
    }
    if (italic_out) {
        *italic_out = b->font_italic ? 1 : 0;
    }
    if (underline_out) {
        *underline_out = b->underline ? 1 : 0;
    }
    if (text_out && text_cap > 0) {
        text_out[0] = '\0';
        if (b->kind == YL_BOX_INLINE_TEXT && b->text && b->text_len > 0) {
            int n = b->text_len < (size_t)(text_cap - 1) ? (int)b->text_len : text_cap - 1;
            memcpy(text_out, b->text, (size_t)n);
            text_out[n] = '\0';
        } else if (b->kind == YL_BOX_BLOCK && b->marker_text && b->marker_text_len > 0) {
            /* Surface list-item markers via the same text channel so
			 * tests can search for them with the inline-text helpers.
			 * Block boxes without a marker still come back as empty. */
            int n = b->marker_text_len < (size_t)(text_cap - 1) ? (int)b->marker_text_len
                                                                : text_cap - 1;
            memcpy(text_out, b->marker_text, (size_t)n);
            text_out[n] = '\0';
        }
    }
    return 0;
}

int yetty_ylexbor_test_box_attr_at(const struct yetty_ylexbor *r, int index, const char *attr,
                                   char *out_buf, int cap)
{
    if (out_buf && cap > 0) {
        out_buf[0] = '\0';
    }
    if (r == NULL || index < 0 || (uint32_t)index >= r->boxes.size || out_buf == NULL || cap <= 0 ||
        attr == NULL) {
        return -1;
    }
    const struct yetty_ylexbor_box *b = &r->boxes.data[index];
    if (b->element == NULL) {
        return -1;
    }
    size_t vlen = 0;
    const lxb_char_t *val =
        lxb_dom_element_get_attribute(b->element, (const lxb_char_t *)attr, strlen(attr), &vlen);
    if (val == NULL) {
        return -1;
    }
    int n = vlen < (size_t)(cap - 1) ? (int)vlen : cap - 1;
    memcpy(out_buf, val, (size_t)n);
    out_buf[n] = '\0';
    return 0;
}

int yetty_ylexbor_test_box_data_test_at(const struct yetty_ylexbor *r, int index, char *out_buf,
                                        int cap)
{
    if (out_buf && cap > 0) {
        out_buf[0] = '\0';
    }
    if (r == NULL || index < 0 || (uint32_t)index >= r->boxes.size || out_buf == NULL || cap <= 0) {
        return -1;
    }
    const struct yetty_ylexbor_box *b = &r->boxes.data[index];
    if (b->element == NULL) {
        return -1;
    }
    size_t vlen = 0;
    const lxb_char_t *val =
        lxb_dom_element_get_attribute(b->element, (const lxb_char_t *)"data-test", 9, &vlen);
    if (val == NULL || vlen == 0) {
        return -1;
    }
    int n = vlen < (size_t)(cap - 1) ? (int)vlen : cap - 1;
    memcpy(out_buf, val, (size_t)n);
    out_buf[n] = '\0';
    return 0;
}

const char *yetty_ylexbor_size_source_name(int source)
{
    static const char *const names[] = {
        [YL_SRC_NONE] = "none",
        [YL_SRC_VIEWPORT] = "viewport",
        [YL_SRC_CSS] = "css",
        [YL_SRC_AVAIL] = "avail",
        [YL_SRC_SHRINK_TO_FIT] = "fit",
        [YL_SRC_CONTENT] = "content",
        [YL_SRC_FLEX_BASIS] = "flex-basis",
        [YL_SRC_FLEX_EVEN] = "flex-even",
        [YL_SRC_FLEX_SHARE] = "flex-share",
        [YL_SRC_FLEX_GROW] = "flex-grow",
        [YL_SRC_FLEX_SHRINK] = "flex-shrink",
        [YL_SRC_FLEX_MIN] = "flex-min",
        [YL_SRC_FLEX_STRETCH] = "flex-stretch",
        [YL_SRC_GRID_TRACKS] = "grid-tracks",
        [YL_SRC_GRID_STRETCH] = "grid-stretch",
        [YL_SRC_TABLE_COLS] = "table-cols",
        [YL_SRC_ABS_INSET] = "abs-inset",
        [YL_SRC_ABS_FIT] = "abs-fit",
        [YL_SRC_IMG_INTRINSIC] = "img",
    };
    if (source < 0 || (size_t)source >= sizeof(names) / sizeof(names[0]) || names[source] == NULL) {
        return "?";
    }
    return names[source];
}

int yetty_ylexbor_test_box_sources_at(const struct yetty_ylexbor *r, int index,
                                      const char **width_source_out, const char **height_source_out)
{
    if (width_source_out) {
        *width_source_out = "?";
    }
    if (height_source_out) {
        *height_source_out = "?";
    }
    if (r == NULL || index < 0 || (uint32_t)index >= r->boxes.size) {
        return -1;
    }
    const struct yetty_ylexbor_box *b = &r->boxes.data[index];
    if (width_source_out) {
        *width_source_out = yetty_ylexbor_size_source_name(b->width_source);
    }
    if (height_source_out) {
        *height_source_out = yetty_ylexbor_size_source_name(b->height_source);
    }
    return 0;
}

int yetty_ylexbor_test_box_path_at(const struct yetty_ylexbor *r, int index, char *out_buf, int cap)
{
    if (out_buf && cap > 0) {
        out_buf[0] = '\0';
    }
    if (r == NULL || index < 0 || (uint32_t)index >= r->boxes.size || out_buf == NULL || cap <= 0) {
        return -1;
    }
    const struct yetty_ylexbor_box *b = &r->boxes.data[index];
    if (b->element == NULL) {
        return -1;
    }
    /* Collect "tag:nth" segments deepest-first, then join top-down. */
    char segs[64][48];
    int nseg = 0;
    lxb_dom_node_t *node = lxb_dom_interface_node(b->element);
    while (node != NULL && node->type == LXB_DOM_NODE_TYPE_ELEMENT && nseg < 64) {
        lxb_dom_element_t *el = lxb_dom_interface_element(node);
        size_t nlen = 0;
        const unsigned char *nm = lxb_dom_element_local_name(el, &nlen);
        int nth = 1;
        for (lxb_dom_node_t *p = node->prev; p != NULL; p = p->prev) {
            if (p->type != LXB_DOM_NODE_TYPE_ELEMENT) {
                continue;
            }
            size_t plen = 0;
            const unsigned char *pnm =
                lxb_dom_element_local_name(lxb_dom_interface_element(p), &plen);
            if (plen == nlen && nm != NULL && pnm != NULL && memcmp(nm, pnm, nlen) == 0) {
                nth++;
            }
        }
        snprintf(segs[nseg], sizeof(segs[0]), "%.*s:%d", (int)nlen,
                 nm != NULL ? (const char *)nm : "x", nth);
        nseg++;
        node = node->parent;
    }
    int pos = 0;
    for (int i = nseg - 1; i >= 0; i--) {
        int written =
            snprintf(out_buf + pos, (size_t)(cap - pos), "%s%s", pos > 0 ? ">" : "", segs[i]);
        if (written < 0 || written >= cap - pos) {
            break;
        }
        pos += written;
    }
    return 0;
}

/* Copy of `name`'s value on `element` when non-empty and containing
 * `contains` (NULL = any content). NULL when absent/filtered. */
static char *element_attr_copy(lxb_dom_element_t *element, const char *name, size_t name_len,
                               const char *contains)
{
    size_t value_len = 0;
    const lxb_char_t *value =
        lxb_dom_element_get_attribute(element, (const lxb_char_t *)name, name_len, &value_len);
    if (value == NULL || value_len == 0) {
        return NULL;
    }
    char *copy = malloc(value_len + 1);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, value, value_len);
    copy[value_len] = '\0';
    if (contains != NULL && strstr(copy, contains) == NULL) {
        free(copy);
        return NULL;
    }
    return copy;
}

/* Depth-first scan of `node`'s subtree for the first element whose `name`
 * attribute passes element_attr_copy's filter. Depth-capped — a match sits
 * a handful of levels inside a card, never hundreds. */
static char *subtree_attr_find(lxb_dom_node_t *node, const char *name, size_t name_len,
                               const char *contains, int depth)
{
    if (depth > 64) {
        return NULL;
    }
    for (lxb_dom_node_t *child = node->first_child; child; child = child->next) {
        if (child->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            char *found =
                element_attr_copy(lxb_dom_interface_element(child), name, name_len, contains);
            if (found) {
                return found;
            }
        }
        char *found = subtree_attr_find(child, name, name_len, contains, depth + 1);
        if (found) {
            return found;
        }
    }
    return NULL;
}

char *yetty_ylexbor_ancestor_attr_at(struct yetty_ylexbor *r, float x, float y, const char *name,
                                     const char *contains)
{
    if (r == NULL || name == NULL) {
        return NULL;
    }
    size_t name_len = strlen(name);
    /* Deepest box containing (x, y) — same scan as link_at. */
    lxb_dom_element_t *target = NULL;
    for (uint32_t i = 0; i < r->boxes.size; i++) {
        struct yetty_ylexbor_box *b = &r->boxes.data[i];
        if (b->element == NULL) {
            continue;
        }
        if (x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h) {
            target = b->element;
        }
    }
    if (target == NULL) {
        return NULL;
    }
    for (lxb_dom_node_t *n = lxb_dom_interface_node(target); n; n = n->parent) {
        if (n->type != LXB_DOM_NODE_TYPE_ELEMENT) {
            continue;
        }
        char *found = element_attr_copy(lxb_dom_interface_element(n), name, name_len, contains);
        if (found) {
            return found;
        }
        /* Filtered search: the payload often lives on a SIBLING subtree of
		 * the hit — e.g. gnews puts the article-URL jslog on a separate
		 * overlay <a> next to the headline link, inside the same card. The
		 * nearest ancestor whose subtree holds a match is that card. Only
		 * done with a filter — the unfiltered walk keeps its plain
		 * "attribute on an ancestor" contract. */
        if (contains != NULL) {
            found = subtree_attr_find(n, name, name_len, contains, 0);
            if (found) {
                return found;
            }
        }
    }
    return NULL;
}

char *yetty_ylexbor_link_at(struct yetty_ylexbor *r, float x, float y)
{
    if (r == NULL) {
        return NULL;
    }
    /* Hit-test the laid-out box vector for the deepest box containing
     * (x, y) — same scan dispatch_click uses. (x, y) are document
     * coordinates (the layout origin), so the caller must subtract the
     * page's on-screen offset first. */
    lxb_dom_element_t *target = NULL;
    const struct yetty_ylexbor_box *target_box = NULL;
    for (uint32_t i = 0; i < r->boxes.size; i++) {
        struct yetty_ylexbor_box *b = &r->boxes.data[i];
        if (b->element == NULL) {
            continue;
        }
        if (b->vis_hidden || b->opacity < 0.02f || yetty_ylexbor_box_clipped_out(r, i)) {
            continue; /* hidden / transparent / clipped boxes are hit-transparent */
        }
        if (x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h) {
            target = b->element;
            target_box = b;
        }
    }
    if (target == NULL) {
        return NULL;
    }

    /* Inline <svg>: map the click through the same scene→page transform
	 * the paint merge used and test the SVG-internal <a> regions — the
	 * innermost (last-registered) hit wins. Falls through to the DOM
	 * ancestor walk (an enclosing HTML <a> around the whole svg) when no
	 * internal anchor contains the point. */
    if (target_box != NULL && target->node.local_name == LXB_TAG_SVG) {
        struct yetty_ylexbor_svg_inline_entry *entry = yetty_ylexbor_svg_inline_find(r, target);
        ydebug("link_at: svg box hit entry=%p links=%zu", (void *)entry,
               entry ? entry->link_count : 0);
        if (entry && entry->scene && entry->link_count > 0) {
            float scale_x, scale_y, offset_x, offset_y;
            yetty_ylexbor_svg_merge_transform(
                entry->min_x, entry->min_y, entry->w, entry->h, entry->par_align_x,
                entry->par_align_y, entry->par_mode, target_box->x, target_box->y, target_box->w,
                target_box->h, &scale_x, &scale_y, &offset_x, &offset_y);
            if (scale_x != 0.0f && scale_y != 0.0f) {
                float scene_x = (x - offset_x) / scale_x;
                float scene_y = (y - offset_y) / scale_y;
                ydebug("link_at: svg scene point %.1f,%.1f", scene_x, scene_y);
                for (size_t li = entry->link_count; li-- > 0;) {
                    const struct yetty_ysvg_link_region *region = &entry->links[li];
                    if (!region->href || region->href[0] == '#' || region->min_x > region->max_x) {
                        continue; /* fragment link or empty region */
                    }
                    if (scene_x >= region->min_x && scene_x <= region->max_x &&
                        scene_y >= region->min_y && scene_y <= region->max_y) {
                        return yetty_ylexbor_resolve_url(r, region->href);
                    }
                }
            }
        }
    }
    /* Walk up to the nearest element carrying an href (an <a>/<area>).
     * Return it resolved against the base URL; NULL for in-page fragments
     * or no link. */
    for (lxb_dom_node_t *n = lxb_dom_interface_node(target); n; n = n->parent) {
        if (n->type != LXB_DOM_NODE_TYPE_ELEMENT) {
            continue;
        }
        lxb_dom_element_t *el = lxb_dom_interface_element(n);
        size_t hl = 0;
        const lxb_char_t *href =
            lxb_dom_element_get_attribute(el, (const lxb_char_t *)"href", 4, &hl);
        if (href == NULL || hl == 0) {
            continue;
        }
        char *h = malloc(hl + 1);
        if (h == NULL) {
            return NULL;
        }
        memcpy(h, href, hl);
        h[hl] = '\0';
        if (h[0] == '#') { /* same-page fragment — not a navigation */
            free(h);
            return NULL;
        }
        char *url = yetty_ylexbor_resolve_url(r, h);
        free(h);
        return url;
    }
    return NULL;
}
