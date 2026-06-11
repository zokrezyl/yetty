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
#include <lexbor/tag/const.h>

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
        free(v->data[i].bg_image_url);
        v->data[i].bg_image_url = NULL;
    }
    v->size = 0;
}

static void box_vec_destroy(struct yetty_ylexbor_box_vec *v)
{
    for (uint32_t i = 0; i < v->size; i++) {
        free(v->data[i].segs);
        free(v->data[i].bg_image_url);
    }
    free(v->data);
    v->data = NULL;
    v->size = v->cap = 0;
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

struct yetty_ylexbor_ptr_result yetty_ylexbor_create(const struct yetty_ylexbor_config *cfg)
{
    struct yetty_ylexbor *r = calloc(1, sizeof(*r));
    if (r == NULL) {
        return YETTY_ERR(yetty_ylexbor_ptr, "ylexbor alloc");
    }

    r->viewport_w = cfg && cfg->viewport_width > 0 ? cfg->viewport_width : 1024;
    r->viewport_h = cfg && cfg->viewport_height > 0 ? cfg->viewport_height : 768;
    r->default_font_size = cfg && cfg->default_font_size > 0 ? cfg->default_font_size : 16.0f;

    r->document = lxb_html_document_create();
    if (r->document == NULL) {
        free(r);
        return YETTY_ERR(yetty_ylexbor_ptr, "html_document_create");
    }
    if (lxb_style_init(r->document) != LXB_STATUS_OK) {
        lxb_html_document_destroy(r->document);
        free(r);
        return YETTY_ERR(yetty_ylexbor_ptr, "lxb_style_init");
    }

    r->css_parser = lxb_css_parser_create();
    if (r->css_parser == NULL || lxb_css_parser_init(r->css_parser, NULL) != LXB_STATUS_OK) {
        if (r->css_parser) {
            lxb_css_parser_destroy(r->css_parser, true);
        }
        lxb_html_document_destroy(r->document);
        free(r);
        return YETTY_ERR(yetty_ylexbor_ptr, "css_parser_init");
    }

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
    yetty_ylexbor_grid_classes_free(r);
    free(r->text_chunks);
    free(r->base_url);
    for (int i = 0; i < r->img_cache_count; i++) {
        free(r->img_cache[i].url);
        free(r->img_cache[i].pixels);
    }
    free(r->img_cache);
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

static int g_css_loaded = 0, g_css_failed = 0, g_css_inline = 0;

/* One CSS source to apply. EITHER `inline_body` is set (an inline
 * <style> block — body is owned, will be freed after add_css) OR `url`
 * is set (external <link>; after the parallel-fetch step, body[i] /
 * len[i] / status[i] in the parent arrays carry the fetched response).
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
    char **fetch_urls = NULL;
    char **bodies = NULL;
    size_t *lens = NULL;
    long *status = NULL;
    int *slot_to_entry = NULL;
    if (ext_n > 0) {
        fetch_urls = calloc((size_t)ext_n, sizeof(*fetch_urls));
        bodies = calloc((size_t)ext_n, sizeof(*bodies));
        lens = calloc((size_t)ext_n, sizeof(*lens));
        status = calloc((size_t)ext_n, sizeof(*status));
        slot_to_entry = calloc((size_t)ext_n, sizeof(*slot_to_entry));
        if (!fetch_urls || !bodies || !lens || !status || !slot_to_entry) {
            free(fetch_urls);
            free(bodies);
            free(lens);
            free(status);
            free(slot_to_entry);
            ext_n = 0;
        } else {
            int j = 0;
            for (int i = 0; i < cc.count; i++) {
                if (cc.items[i].is_external) {
                    fetch_urls[j] = cc.items[i].url;
                    slot_to_entry[j] = i;
                    j++;
                }
            }
            yetty_ylexbor_http_get_many((const char *const *)fetch_urls, ext_n, r->base_url,
                                        /*concurrency=*/8, bodies, lens, status);
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
            if (slot >= 0 && bodies[slot] && status[slot] >= 200 && status[slot] < 300) {
                struct yetty_ycore_void_result ar =
                    yetty_ylexbor_add_css(r, bodies[slot], lens[slot]);
                if (YETTY_IS_ERR(ar)) {
                    if (YETTY_IS_OK(apply_res)) {
                        apply_res =
                            YETTY_ERR(yetty_ycore_void, "load_external_stylesheets: add_css", ar);
                    } else {
                        yetty_ycore_error_destroy(ar.error);
                    }
                } else {
                    g_css_loaded++;
                }
            } else {
                g_css_failed++;
            }
            if (slot >= 0) {
                free(bodies[slot]);
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
                g_css_inline++;
            }
            free(e->inline_body);
        }
    }
    free(fetch_urls);
    free(bodies);
    free(lens);
    free(status);
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
    box_vec_clear(&r->boxes);
    arena_reset(r);
    yetty_ylexbor_grid_classes_free(r);
    r->grid_content_max_px = 0.0f;
    r->content_height = 0;
    /* Invalidate any in-flight async image jobs from the previous document —
     * their done() will find a mismatched generation and discard. */
    r->fetch_generation++;

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
    if (!r->defer_scripts) {
        (void)yetty_ylexbor_js_run_inline_scripts(r);
    }
    yetty_ylexbor_prof("  run scripts    %.0f ms", yetty_ylexbor_prof_now_ms() - t_phase);
    t_phase = yetty_ylexbor_prof_now_ms();

    ydebug("css sheets ext=%d inline=%d failed=%d customs=%d", g_css_loaded, g_css_inline,
           g_css_failed, r->customs.size);
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

struct yetty_ycore_void_result yetty_ylexbor_add_css(struct yetty_ylexbor *r, const char *css,
                                                     size_t css_len)
{
    if (r == NULL || css == NULL) {
        return YETTY_ERR(yetty_ycore_void, "ylexbor_add_css: null");
    }

    /* Pre-scan for `:root { --x: y; }` etc. before lexbor parses,
	 * so var() lookups see the latest definitions. */
    yetty_ylexbor_css_vars_scan(r, css, css_len);
    /* Also note any grid content-column cap (minmax(0, Nrem)) — applied as
     * a max-width on display:grid containers since we don't lay out grid
     * tracks. */
    yetty_ylexbor_css_scan_grid_content_width(r, css, css_len);
    yetty_ylexbor_css_scan_grid_templates(r, css, css_len);

    /* Also push the same CSS through libcss so its cascade sees it. */
    (void)yetty_ybrowser_libcss_add_sheet(r, css, css_len, CSS_ORIGIN_AUTHOR);

    lxb_css_stylesheet_t *sheet = lxb_css_stylesheet_create(NULL);
    if (sheet == NULL) {
        return YETTY_ERR(yetty_ycore_void, "stylesheet_create");
    }
    lxb_status_t s =
        lxb_css_stylesheet_parse(sheet, r->css_parser, (const lxb_char_t *)css, css_len);
    if (s != LXB_STATUS_OK) {
        lxb_css_stylesheet_destroy(sheet, true);
        return YETTY_ERR(yetty_ycore_void, "stylesheet_parse");
    }
    s = lxb_html_document_stylesheet_attach(r->document, sheet);
    if (s != LXB_STATUS_OK) {
        lxb_css_stylesheet_destroy(sheet, true);
        return YETTY_ERR(yetty_ycore_void, "stylesheet_attach");
    }
    return YETTY_OK_VOID();
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
    (void)yetty_ylexbor_js_run_inline_scripts(r);
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
