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

/* Defined below (near load_html); used by the destroy path above it. */
static void destroy_iframe_children(struct yetty_ylexbor *r);

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

void yetty_ylexbor_arena_reset(struct yetty_ylexbor *r)
{
    for (size_t i = 0; i < r->text_chunks_count; i++) {
        free(r->text_chunks[i]);
    }
    r->text_chunks_count = 0;
}

/* ===========================================================================
 * Text width. Two modes, selected by the advance-ratio argument:
 *
 *   ratio > 0  — FLAT: every glyph advances font_size * ratio. Used when the
 *                renderer is known to be monospace (the in-yetty host sets
 *                0.602 to match its terminal font; the WPT Ahem override is
 *                a per-box 1.0) and by tests that pin wrap mechanics.
 *
 *   ratio <= 0 — PROPORTIONAL (the default): per-codepoint advances from
 *                Helvetica/Arial metrics. Liberation Sans — the metric
 *                match for the Helvetica/Arial/system-ui stacks nearly
 *                every site requests — is what Chrome on Linux actually
 *                shapes with, so these advances are the parity reference.
 *                Real shaping (kerning, non-Latin scripts) is still ahead;
 *                this removes the dominant flat-metric error.
 * ===========================================================================*/

size_t yetty_ylexbor_utf8_decode(const char *s, size_t len, uint32_t *out_codepoint)
{
    unsigned char first = (unsigned char)s[0];
    if (first < 0x80) {
        *out_codepoint = first;
        return 1;
    }
    if ((first & 0xE0) == 0xC0 && len >= 2) {
        *out_codepoint = ((uint32_t)(first & 0x1F) << 6) | ((uint32_t)s[1] & 0x3F);
        return 2;
    }
    if ((first & 0xF0) == 0xE0 && len >= 3) {
        *out_codepoint = ((uint32_t)(first & 0x0F) << 12) | (((uint32_t)s[1] & 0x3F) << 6) |
                         ((uint32_t)s[2] & 0x3F);
        return 3;
    }
    if ((first & 0xF8) == 0xF0 && len >= 4) {
        *out_codepoint = ((uint32_t)(first & 0x07) << 18) | (((uint32_t)s[1] & 0x3F) << 12) |
                         (((uint32_t)s[2] & 0x3F) << 6) | ((uint32_t)s[3] & 0x3F);
        return 4;
    }
    *out_codepoint = 0xFFFD;
    return 1;
}

float yetty_ylexbor_codepoint_advance_em(uint32_t codepoint)
{
    /* Helvetica AFM advance widths, thousandths of an em, ASCII 32..126.
     * Arial and Liberation Sans share these to the unit. */
    static const uint16_t helvetica_advance[95] = {
        278,  278, 355, 556, 556, 889, 667, 191, 333, 333, 389, 584, 278, 333, 278, 278,
        556,  556, 556, 556, 556, 556, 556, 556, 556, 556, 278, 278, 584, 584, 584, 556,
        1015, 667, 667, 722, 722, 667, 611, 778, 722, 278, 500, 667, 556, 833, 722, 778,
        667,  778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 278, 278, 278, 469, 556,
        333,  556, 556, 500, 556, 556, 278, 556, 556, 222, 222, 500, 222, 833, 556, 556,
        556,  556, 333, 500, 278, 556, 500, 722, 500, 500, 500, 334, 260, 334, 584,
    };
    if (codepoint >= 32 && codepoint <= 126) {
        return (float)helvetica_advance[codepoint - 32] / 1000.0f;
    }
    /* Zero-width codepoints (also skipped by the wrap pass). */
    if (codepoint == 0x00AD || codepoint == 0x200B || codepoint == 0x200C || codepoint == 0x200D ||
        codepoint == 0xFEFF) {
        return 0.0f;
    }
    if (codepoint == 0x00A0) {
        return 0.278f; /* NBSP — same advance as a space */
    }
    /* Common punctuation outside ASCII. */
    if (codepoint == 0x2013) {
        return 0.556f; /* en dash */
    }
    if (codepoint == 0x2014 || codepoint == 0x2015) {
        return 1.0f; /* em dash / horizontal bar */
    }
    if (codepoint >= 0x2018 && codepoint <= 0x201B) {
        return 0.222f; /* curly single quotes */
    }
    if (codepoint >= 0x201C && codepoint <= 0x201F) {
        return 0.333f; /* curly double quotes */
    }
    if (codepoint == 0x2026) {
        return 1.0f; /* ellipsis */
    }
    if (codepoint == 0x2022) {
        return 0.35f; /* bullet */
    }
    /* CJK, Hangul, kana, full-width forms: one em. */
    if ((codepoint >= 0x1100 && codepoint <= 0x115F) ||
        (codepoint >= 0x2E80 && codepoint <= 0x9FFF) ||
        (codepoint >= 0xAC00 && codepoint <= 0xD7A3) ||
        (codepoint >= 0xF900 && codepoint <= 0xFAFF) ||
        (codepoint >= 0xFF00 && codepoint <= 0xFF60)) {
        return 1.0f;
    }
    /* Everything else (accented Latin, Cyrillic, Greek, …): the Helvetica
     * lowercase average — accented letters keep their base advance. */
    return 0.556f;
}

float yetty_ylexbor_glyph_advance_ratio(const struct yetty_ylexbor *r)
{
    if (r != NULL && r->glyph_advance_ratio > 0.0f) {
        return r->glyph_advance_ratio;
    }
    return 0.0f; /* 0 = proportional Helvetica metrics (the default) */
}

float yetty_ylexbor_naive_text_width(const char *s, size_t len, float font_size,
                                     float advance_ratio)
{
    if (advance_ratio > 0.0f) {
        /* Flat mode: glyph count × fixed advance. */
        int n = 0;
        for (size_t i = 0; i < len;) {
            uint32_t codepoint;
            i += yetty_ylexbor_utf8_decode(s + i, len - i, &codepoint);
            n++;
        }
        float per_glyph = font_size * advance_ratio;
        if (per_glyph < 1.0f) {
            per_glyph = 1.0f;
        }
        return n * per_glyph;
    }
    /* Proportional mode: sum per-codepoint Helvetica advances. */
    float total_em = 0.0f;
    for (size_t i = 0; i < len;) {
        uint32_t codepoint;
        i += yetty_ylexbor_utf8_decode(s + i, len - i, &codepoint);
        total_em += yetty_ylexbor_codepoint_advance_em(codepoint);
    }
    return total_em * font_size;
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

/* ===========================================================================
 * DevTools DOM inspector — pre-order walk producing display-ready labels.
 * ===========================================================================*/

static int dom_is_blank(const lxb_char_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (p[i] != ' ' && p[i] != '\t' && p[i] != '\n' && p[i] != '\r' && p[i] != '\f') {
            return 0;
        }
    }
    return 1;
}

/* A node appears in the inspector if it is an element, or a text node with any
 * non-whitespace content (blank inter-tag whitespace is noise). */
static int dom_node_shown(lxb_dom_node_t *node)
{
    if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
        return 1;
    }
    if (node->type == LXB_DOM_NODE_TYPE_TEXT) {
        lxb_dom_text_t *text = lxb_dom_interface_text(node);
        return !dom_is_blank(text->char_data.data.data, text->char_data.data.length);
    }
    return 0;
}

static int dom_node_has_shown_child(lxb_dom_node_t *node)
{
    for (lxb_dom_node_t *child = node->first_child; child; child = child->next) {
        if (dom_node_shown(child)) {
            return 1;
        }
    }
    return 0;
}

/* Append a plain NUL-terminated string, honoring the buffer cap. */
static void dom_label_puts(char *buf, size_t cap, size_t *off, const char *s)
{
    while (*s && *off < cap - 1) {
        buf[(*off)++] = *s++;
    }
    buf[*off] = '\0';
}

/* Append a raw byte range, collapsing every whitespace run to a single space
 * (and dropping leading whitespace) so a multi-line text node reads on one row. */
static void dom_label_put_collapsed(char *buf, size_t cap, size_t *off, const lxb_char_t *s,
                                    size_t n)
{
    int in_space = 0;
    for (size_t i = 0; i < n && *off < cap - 1; i++) {
        char c = (char)s[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f') {
            if (in_space || *off == 0) {
                continue;
            }
            c = ' ';
            in_space = 1;
        } else {
            in_space = 0;
        }
        buf[(*off)++] = c;
    }
    buf[*off] = '\0';
}

/* Fill a display label for a shown node. Elements become `<tag id=".." class="..">`;
 * text nodes become a quoted, whitespace-collapsed snippet. */
static void dom_format_label(lxb_dom_node_t *node, char *buf, size_t cap)
{
    size_t off = 0;
    buf[0] = '\0';
    if (node->type == LXB_DOM_NODE_TYPE_TEXT) {
        lxb_dom_text_t *text = lxb_dom_interface_text(node);
        dom_label_puts(buf, cap, &off, "\"");
        dom_label_put_collapsed(buf, cap, &off, text->char_data.data.data,
                                text->char_data.data.length);
        dom_label_puts(buf, cap, &off, "\"");
        return;
    }
    lxb_dom_element_t *element = lxb_dom_interface_element(node);
    size_t name_len = 0;
    const lxb_char_t *name = lxb_dom_element_local_name(element, &name_len);
    dom_label_puts(buf, cap, &off, "<");
    if (name) {
        dom_label_put_collapsed(buf, cap, &off, name, name_len);
    }
    size_t id_len = 0;
    const lxb_char_t *id =
        lxb_dom_element_get_attribute(element, (const lxb_char_t *)"id", 2, &id_len);
    if (id && id_len > 0) {
        dom_label_puts(buf, cap, &off, " id=\"");
        dom_label_put_collapsed(buf, cap, &off, id, id_len);
        dom_label_puts(buf, cap, &off, "\"");
    }
    size_t class_len = 0;
    const lxb_char_t *class_value =
        lxb_dom_element_get_attribute(element, (const lxb_char_t *)"class", 5, &class_len);
    if (class_value && class_len > 0) {
        dom_label_puts(buf, cap, &off, " class=\"");
        dom_label_put_collapsed(buf, cap, &off, class_value, class_len);
        dom_label_puts(buf, cap, &off, "\"");
    }
    dom_label_puts(buf, cap, &off, ">");
}

struct dom_walk_state {
    yetty_ylexbor_dom_visit_fn visit;
    void *user;
    int stop;
};

static void dom_walk_rec(lxb_dom_node_t *node, int depth, struct dom_walk_state *state)
{
    if (state->stop || !dom_node_shown(node)) {
        return;
    }
    char label[192];
    dom_format_label(node, label, sizeof(label));
    if (state->visit(state->user, depth, dom_node_has_shown_child(node), label)) {
        state->stop = 1;
        return;
    }
    for (lxb_dom_node_t *child = node->first_child; child && !state->stop; child = child->next) {
        dom_walk_rec(child, depth + 1, state);
    }
}

void yetty_ylexbor_dom_walk(struct yetty_ylexbor *r, yetty_ylexbor_dom_visit_fn visit, void *user)
{
    if (!r || !r->document || !visit) {
        return;
    }
    struct dom_walk_state state = {visit, user, 0};
    lxb_dom_node_t *root = lxb_dom_interface_node(r->document);
    for (lxb_dom_node_t *child = root->first_child; child && !state.stop; child = child->next) {
        dom_walk_rec(child, 0, &state);
    }
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
    destroy_iframe_children(r);
    box_vec_destroy(&r->boxes);
    yetty_ylexbor_arena_reset(r);
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
    yetty_ylexbor_console_clear(r); /* frees each retained line's text */
    free(r->console_ring);
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

/* Nested browsing contexts have a hard depth cap so a document that iframes
 * back into a chain (or itself) can't spin up engines without bound. */
enum { YL_IFRAME_MAX_DEPTH = 3 };

/* Tear down every child engine spun up for the previous document's iframes.
 * Called before rebuilding them (each relayout) and at engine destroy. */
static void destroy_iframe_children(struct yetty_ylexbor *r)
{
    for (int i = 0; i < r->iframe_child_count; i++) {
        if (r->iframe_children[i].child != NULL) {
            (void)yetty_ylexbor_destroy(r->iframe_children[i].child);
        }
        free(r->iframe_children[i].src_key);
    }
    free(r->iframe_children);
    r->iframe_children = NULL;
    r->iframe_child_count = 0;
    r->iframe_child_cap = 0;
}

/* Copy a (non-NUL-terminated) lexbor attribute value into a fresh C string. */
static char *dup_attr(const lxb_char_t *value, size_t len)
{
    char *out = malloc(len + 1);
    if (out != NULL) {
        memcpy(out, value, len);
        out[len] = '\0';
    }
    return out;
}

/* Post-layout pass: for every laid-out <iframe> box, fetch its src (or read its
 * srcdoc), render that document in a child engine sized to the iframe's content
 * box, and hang the child off the box so paint can composite it. Runs after
 * layout so the iframe box dimensions (hence the child viewport) are known. */
/* Compute an iframe box's inner content size (border+padding removed), never
 * below 1px so the child viewport is valid. */
static void iframe_content_size(const struct yetty_ylexbor_box *b, int *out_w, int *out_h)
{
    int content_w =
        (int)(b->w - b->border_left - b->border_right - b->padding_left - b->padding_right);
    if (content_w < 1) {
        content_w = (int)b->w;
    }
    int content_h =
        (int)(b->h - b->border_top - b->border_bottom - b->padding_top - b->padding_bottom);
    if (content_h < 1) {
        content_h = (int)b->h;
    }
    *out_w = content_w;
    *out_h = content_h;
}

static struct yetty_ylexbor_iframe_child *iframe_child_find(struct yetty_ylexbor *r,
                                                            lxb_dom_element_t *element)
{
    for (int i = 0; i < r->iframe_child_count; i++) {
        if (r->iframe_children[i].element == element) {
            return &r->iframe_children[i];
        }
    }
    return NULL;
}

int yetty_ylexbor_is_youtube_host(const char *url)
{
    if (url == NULL) {
        return 0;
    }
    const char *host = strstr(url, "://");
    host = host ? host + 3 : url;
    size_t host_len = 0;
    while (host[host_len] != '\0' && host[host_len] != '/' && host[host_len] != ':' &&
           host[host_len] != '?') {
        host_len++;
    }
    static const char suffix[] = "youtube.com";
    size_t suffix_len = sizeof(suffix) - 1;
    if (host_len < suffix_len) {
        return 0;
    }
    if (strncasecmp(host + host_len - suffix_len, suffix, suffix_len) != 0) {
        return 0;
    }
    /* Exact host or a subdomain — not `youtube.com.evil.test`. */
    return host_len == suffix_len || host[host_len - suffix_len - 1] == '.';
}

static struct yetty_ycore_void_result resolve_iframes(struct yetty_ylexbor *r)
{
    /* Every box starts with no linked child; the reuse loop re-links the ones
     * that resolve. (box-build does not initialise iframe_doc.) */
    for (uint32_t i = 0; i < r->boxes.size; i++) {
        r->boxes.data[i].iframe_doc = NULL;
    }

    if (r->iframe_depth >= YL_IFRAME_MAX_DEPTH || getenv("YBROWSER_NO_IFRAMES") != NULL) {
        destroy_iframe_children(r);
        return YETTY_OK_VOID();
    }

    /* Mark-and-sweep: entries not touched this pass are iframes that left the
     * DOM and get torn down at the end. Retained children are REUSED — no
     * re-fetch, no re-parse, no re-running their scripts. */
    for (int i = 0; i < r->iframe_child_count; i++) {
        r->iframe_children[i].used = 0;
    }

    for (uint32_t i = 0; i < r->boxes.size; i++) {
        struct yetty_ylexbor_box *b = &r->boxes.data[i];
        if (b->element == NULL || b->element->node.local_name != LXB_TAG_IFRAME) {
            continue;
        }
        if (b->w <= 0.0f || b->h <= 0.0f) {
            continue;
        }

        /* Cheaply derive the source key WITHOUT fetching: inline `srcdoc`
         * content wins, otherwise the resolved absolute `src` URL. Reuse hinges
         * on this key being unchanged since the child was built. */
        int is_srcdoc = 0;
        char *src_key = NULL;
        size_t attr_len = 0;
        const lxb_char_t *srcdoc =
            lxb_dom_element_get_attribute(b->element, (const lxb_char_t *)"srcdoc", 6, &attr_len);
        if (srcdoc != NULL && attr_len > 0) {
            is_srcdoc = 1;
            src_key = dup_attr(srcdoc, attr_len);
        } else {
            const lxb_char_t *src =
                lxb_dom_element_get_attribute(b->element, (const lxb_char_t *)"src", 3, &attr_len);
            if (src == NULL || attr_len == 0) {
                continue;
            }
            char *href = dup_attr(src, attr_len);
            if (href == NULL) {
                continue;
            }
            char *absolute = yetty_ylexbor_resolve_url(r, href);
            free(href);
            if (absolute == NULL) {
                continue;
            }
            /* `about:`/`javascript:` iframes have no fetchable document. */
            if (strncmp(absolute, "about:", 6) == 0 || strncmp(absolute, "javascript:", 11) == 0) {
                free(absolute);
                continue;
            }
            src_key = absolute;
        }
        if (src_key == NULL) {
            continue;
        }

        int content_w = 0, content_h = 0;
        iframe_content_size(b, &content_w, &content_h);

        /* Reuse a retained child when the same iframe element still points at
         * the same source. Only re-lay-out (never re-parse) when it resized. */
        struct yetty_ylexbor_iframe_child *entry = iframe_child_find(r, b->element);
        if (entry != NULL && entry->child != NULL && entry->src_key != NULL &&
            strcmp(entry->src_key, src_key) == 0) {
            entry->used = 1;
            free(src_key);
            if (content_w != entry->content_w || content_h != entry->content_h) {
                (void)yetty_ylexbor_set_viewport(entry->child, content_w, content_h);
                (void)yetty_ylexbor_relayout(entry->child);
                entry->content_w = content_w;
                entry->content_h = content_h;
            }
            b->iframe_doc = entry->child;
            continue;
        }

        /* Source changed on an existing element — drop the stale child, then
         * fall through and build a fresh one into the same entry. */
        if (entry != NULL && entry->child != NULL) {
            (void)yetty_ylexbor_destroy(entry->child);
            entry->child = NULL;
            free(entry->src_key);
            entry->src_key = NULL;
        }

        /* Build a new child: srcdoc content is already in src_key; a `src`
         * iframe fetches its document now. */
        char *doc_html = NULL;
        size_t doc_len = 0;
        char *child_base = NULL;
        if (is_srcdoc) {
            doc_html = src_key; /* borrowed for the load; src_key still owns it */
            doc_len = attr_len;
            child_base = r->base_url ? strdup(r->base_url) : NULL;
        } else {
            struct yetty_ybrowser_request request = {
                .url = src_key, .kind = YETTY_YBROWSER_REQUEST_DOCUMENT, .referer = r->base_url};
            struct yetty_ybrowser_response response = {0};
            struct yetty_ycore_void_result fetch_res =
                yetty_ybrowser_fetch(r->loader, &request, &response);
            if (YETTY_IS_ERR(fetch_res)) {
                yetty_ycore_error_destroy(fetch_res.error);
            } else if (response.status >= 200 && response.status < 300 && response.body != NULL) {
                doc_html = dup_attr((const lxb_char_t *)response.body, response.body_len);
                doc_len = response.body_len;
                child_base = strdup(response.effective_url ? response.effective_url : src_key);
            }
            yetty_ybrowser_response_dispose(&response);
        }
        if (doc_html == NULL) {
            free(child_base);
            free(src_key);
            continue;
        }

        struct yetty_ylexbor_config child_cfg = {.viewport_width = content_w,
                                                 .viewport_height = content_h,
                                                 .default_font_size = r->default_font_size,
                                                 .loader = r->loader};
        struct yetty_ylexbor_ptr_result create_res = yetty_ylexbor_create(&child_cfg);
        if (YETTY_IS_ERR(create_res)) {
            yetty_ycore_error_destroy(create_res.error);
            if (!is_srcdoc) {
                free(doc_html);
            }
            free(child_base);
            free(src_key);
            continue;
        }
        struct yetty_ylexbor *child = create_res.value;
        child->iframe_depth = r->iframe_depth + 1;
        child->glyph_advance_ratio = r->glyph_advance_ratio;
        if (child_base != NULL) {
            (void)yetty_ylexbor_set_base_url(child, child_base);
        }
        (void)yetty_ylexbor_load_html(child, doc_html, doc_len);
        if (!is_srcdoc) {
            free(doc_html);
        }
        free(child_base);

        /* Stash the child in a retained entry (reuse the stale slot if this was
         * a source change, else append). src_key ownership transfers in. */
        if (entry == NULL) {
            if (r->iframe_child_count == r->iframe_child_cap) {
                int cap = r->iframe_child_cap ? r->iframe_child_cap * 2 : 4;
                struct yetty_ylexbor_iframe_child *grown =
                    realloc(r->iframe_children, (size_t)cap * sizeof(*grown));
                if (grown == NULL) {
                    (void)yetty_ylexbor_destroy(child);
                    free(src_key);
                    continue;
                }
                r->iframe_children = grown;
                r->iframe_child_cap = cap;
            }
            entry = &r->iframe_children[r->iframe_child_count++];
            memset(entry, 0, sizeof(*entry));
        }
        entry->element = b->element;
        entry->src_key = src_key;
        entry->child = child;
        entry->content_w = content_w;
        entry->content_h = content_h;
        entry->used = 1;
        /* box vector never moves during a child render (child touches only its
         * own state), so `b` is still valid here. */
        b->iframe_doc = child;
        ydebug("iframe resolved: depth=%d content=%dx%d child_boxes=%u", child->iframe_depth,
               content_w, content_h, child->boxes.size);
    }

    /* Sweep untouched entries — their iframe left the DOM. Compact in place. */
    int write = 0;
    for (int read = 0; read < r->iframe_child_count; read++) {
        struct yetty_ylexbor_iframe_child *e = &r->iframe_children[read];
        if (e->used) {
            if (write != read) {
                r->iframe_children[write] = *e;
            }
            write++;
        } else {
            if (e->child != NULL) {
                (void)yetty_ylexbor_destroy(e->child);
            }
            free(e->src_key);
        }
    }
    r->iframe_child_count = write;
    return YETTY_OK_VOID();
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

    /* New document → new cascade context. The libcss bridge is per-document
	 * by design (one select_ctx; its sheets and the per-element node_data
	 * store are keyed to the DOM being torn down). Carrying it across a
	 * navigation left page-1 stylesheets cascading into page 2, and
	 * node_data entries keyed by element pointers that the re-parse below
	 * frees and recycles. */
    yetty_ybrowser_libcss_destroy(r);
    if (yetty_ybrowser_libcss_init(r) != 0) {
        ydebug("ybrowser: libcss re-init failed, using lexbor cascade fallback");
    }

    box_vec_clear(&r->boxes);
    yetty_ylexbor_arena_reset(r);
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
    /* Iframe children are keyed by parent element pointers that the re-parse
	 * frees — drop them all here so resolve_iframes rebuilds fresh (and can't
	 * match a recycled pointer). Across a plain relayout (same DOM) they are
	 * instead reused, which is the whole point of the retained map. */
    destroy_iframe_children(r);
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

    /* Render each <iframe>'s document into a child engine (nested browsing
     * context) now that the iframe boxes are sized. */
    (void)resolve_iframes(r);
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
    struct yetty_ycore_void_result lr = yetty_ylexbor_layout(r);
    if (YETTY_IS_ERR(lr)) {
        return lr;
    }
    (void)resolve_iframes(r);
    return YETTY_OK_VOID();
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

struct dump_dom_accumulator {
    char *buf;
    size_t len, cap;
};

static lxb_status_t dump_dom_cb(const lxb_char_t *data, size_t len, void *vctx)
{
    struct dump_dom_accumulator *acc = vctx;
    if (acc->len + len + 1 > acc->cap) {
        size_t new_cap = acc->cap ? acc->cap * 2 : 4096;
        while (new_cap < acc->len + len + 1) {
            new_cap *= 2;
        }
        char *grown = realloc(acc->buf, new_cap);
        if (!grown) {
            return LXB_STATUS_ERROR_MEMORY_ALLOCATION;
        }
        acc->buf = grown;
        acc->cap = new_cap;
    }
    memcpy(acc->buf + acc->len, data, len);
    acc->len += len;
    return LXB_STATUS_OK;
}

struct yetty_ycore_char_ptr_result yetty_ylexbor_dump_dom(const struct yetty_ylexbor *r)
{
    if (r == NULL || r->document == NULL) {
        return YETTY_ERR(yetty_ycore_char_ptr, "dump_dom: no document");
    }
    struct dump_dom_accumulator acc = {0};
    lxb_status_t status =
        lxb_html_serialize_tree_cb(lxb_dom_interface_node(r->document), dump_dom_cb, &acc);
    if (status != LXB_STATUS_OK) {
        free(acc.buf);
        return YETTY_ERR(yetty_ycore_char_ptr, "dump_dom: serialize failed");
    }
    if (acc.buf == NULL) {
        acc.buf = calloc(1, 1);
        if (acc.buf == NULL) {
            return YETTY_ERR(yetty_ycore_char_ptr, "dump_dom: out of memory");
        }
        return YETTY_OK(yetty_ycore_char_ptr, acc.buf);
    }
    acc.buf[acc.len] = '\0';
    return YETTY_OK(yetty_ycore_char_ptr, acc.buf);
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

int yetty_ylexbor_test_box_paint_at(const struct yetty_ylexbor *r, int index, float *opacity_out,
                                    int *vis_hidden_out)
{
    if (r == NULL || index < 0 || (uint32_t)index >= r->boxes.size) {
        return -1;
    }
    const struct yetty_ylexbor_box *box = &r->boxes.data[index];
    if (opacity_out) {
        *opacity_out = box->opacity;
    }
    if (vis_hidden_out) {
        *vis_hidden_out = box->vis_hidden ? 1 : 0;
    }
    return 0;
}

int yetty_ylexbor_test_box_fg_at(const struct yetty_ylexbor *r, int index, uint8_t *red_out,
                                 uint8_t *green_out, uint8_t *blue_out, uint8_t *alpha_out)
{
    if (r == NULL || index < 0 || (uint32_t)index >= r->boxes.size) {
        return -1;
    }
    const struct yetty_ylexbor_box *box = &r->boxes.data[index];
    if (red_out) {
        *red_out = box->fg.r;
    }
    if (green_out) {
        *green_out = box->fg.g;
    }
    if (blue_out) {
        *blue_out = box->fg.b;
    }
    if (alpha_out) {
        *alpha_out = box->fg.a;
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
    /* Topmost box containing (x, y) in paint/stacking order — same ranking as
	 * link_at, so URL/attribute extraction on a click resolves against the
	 * element actually painted on top (an overlay), not one earlier in the box
	 * vector but behind it. */
    lxb_dom_element_t *target = NULL;
    uint32_t best_index = 0;
    bool have_hit = false;
    for (uint32_t i = 0; i < r->boxes.size; i++) {
        struct yetty_ylexbor_box *b = &r->boxes.data[i];
        if (b->element == NULL) {
            continue;
        }
        if (b->vis_hidden || b->opacity < 0.02f || yetty_ylexbor_box_clipped_out(r, i)) {
            continue; /* hidden / transparent / clipped boxes are hit-transparent */
        }
        if (x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h) {
            if (!have_hit || yetty_ylexbor_paint_order_cmp(r, best_index, i) < 0) {
                best_index = i;
                target = b->element;
                have_hit = true;
            }
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
    uint32_t best_index = 0;
    bool have_hit = false;
    for (uint32_t i = 0; i < r->boxes.size; i++) {
        struct yetty_ylexbor_box *b = &r->boxes.data[i];
        if (b->element == NULL) {
            continue;
        }
        if (b->vis_hidden || b->opacity < 0.02f || yetty_ylexbor_box_clipped_out(r, i)) {
            continue; /* hidden / transparent / clipped boxes are hit-transparent */
        }
        if (x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h) {
            /* Topmost in paint order wins — an overlay (dialog/dropdown) that
			 * paints over content earlier in the box vector must also capture
			 * the click, else the click falls through to what's behind it. */
            if (!have_hit || yetty_ylexbor_paint_order_cmp(r, best_index, i) < 0) {
                best_index = i;
                target = b->element;
                target_box = b;
                have_hit = true;
            }
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
