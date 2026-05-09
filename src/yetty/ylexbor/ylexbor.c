/*
 * ylexbor — top-level lifecycle. Wires lexbor's HTML+CSS parsing to the
 * box-build → layout → paint pipeline implemented in the sibling files.
 */

#include "ylexbor-internal.h"

#include <stdlib.h>
#include <string.h>

#include <stdio.h>
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
    v->size = 0;
}

static void box_vec_destroy(struct yetty_ylexbor_box_vec *v)
{
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
    if (r->text_arena_size + len > r->text_arena_cap) {
        size_t new_cap = r->text_arena_cap ? r->text_arena_cap * 2 : 4096;
        while (new_cap < r->text_arena_size + len) {
            new_cap *= 2;
        }
        char *p = realloc(r->text_arena, new_cap);
        if (p == NULL) {
            return NULL;
        }
        r->text_arena = p;
        r->text_arena_cap = new_cap;
    }
    char *out = r->text_arena + r->text_arena_size;
    memcpy(out, bytes, len);
    r->text_arena_size += len;
    return out;
}

static void arena_reset(struct yetty_ylexbor *r)
{
    r->text_arena_size = 0;
}

/* ===========================================================================
 * Naive text width — placeholder, will become FreeType-driven later.
 * Good enough for the same MVP layout shape ynetsurf uses.
 * ===========================================================================*/

float yetty_ylexbor_naive_text_width(const char *s, size_t len, float font_size)
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
    float per_glyph = font_size * 0.55f;
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

    return YETTY_OK(yetty_ylexbor_ptr, r);
}

struct yetty_ycore_void_result yetty_ylexbor_destroy(struct yetty_ylexbor *r)
{
    if (r == NULL) {
        return YETTY_OK_VOID();
    }
    yetty_ylexbor_js_destroy(r);
    if (r->css_parser) {
        lxb_css_parser_destroy(r->css_parser, true);
    }
    if (r->document) {
        lxb_html_document_destroy(r->document);
    }
    box_vec_destroy(&r->boxes);
    free(r->text_arena);
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

struct yetty_ycore_void_result yetty_ylexbor_set_base_url(struct yetty_ylexbor *r, const char *url)
{
    if (r == NULL) {
        return YETTY_ERR(yetty_ycore_void, "ylexbor_set_base_url: null r");
    }
    free(r->base_url);
    r->base_url = url ? strdup(url) : NULL;
    return YETTY_OK_VOID();
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

/* Walk the DOM, find every <link rel="stylesheet" href="..."> and
 * <style>...</style>, fetch external sheets via http_get, parse, and
 * attach to the document. Internal <style> blocks are normally already
 * processed by lexbor's HTML parser, but we re-attach them defensively
 * so the cascade fires for css class/id selectors used by the boxes
 * we'll later read computed style from. */
static void load_external_stylesheets(struct yetty_ylexbor *r, lxb_dom_node_t *node)
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
                                size_t blen = 0;
                                long status = 0;
                                char *body = yetty_ylexbor_http_get(url, &blen, &status);
                                if (body && status >= 200 && status < 300) {
                                    struct yetty_ycore_void_result ar =
                                        yetty_ylexbor_add_css(r, body, blen);
                                    if (YETTY_IS_ERR(ar)) {
                                        g_css_failed++;
                                    } else {
                                        g_css_loaded++;
                                    }
                                } else {
                                    g_css_failed++;
                                }
                                free(body);
                                free(url);
                            }
                        }
                    }
                }
            } else if (c->local_name == LXB_TAG_STYLE) {
                /* Concatenate text-node children for the
				 * <style> block. lexbor normally does this
				 * automatically; the explicit attach is a
				 * belt-and-braces measure for cases where
				 * the parser's auto-attach didn't run. */
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
                        struct yetty_ycore_void_result ar = yetty_ylexbor_add_css(r, css, off);
                        if (YETTY_IS_ERR(ar)) {
                            g_css_failed++;
                        } else {
                            g_css_inline++;
                        }
                        free(css);
                    }
                }
            }
        }
        if (c->first_child) {
            load_external_stylesheets(r, c);
        }
    }
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
    r->content_height = 0;

    lxb_status_t s = lxb_html_document_parse(r->document, (const lxb_char_t *)html, html_len);
    if (s != LXB_STATUS_OK) {
        return YETTY_ERR(yetty_ycore_void, "html_document_parse failed");
    }

    /* Pull every external CSS referenced via <link rel=stylesheet>
	 * into the cascade. Done before scripts run so getComputedStyle
	 * reads make sense; done before box-build so colored backgrounds
	 * land on the boxes we paint. Skipped silently when libcurl is
	 * unavailable or a fetch errors. */
    load_external_stylesheets(r, lxb_dom_interface_node(r->document));

    /* Run inline + external <script> blocks. */
    (void)yetty_ylexbor_js_run_inline_scripts(r);

    ydebug("css sheets ext=%d inline=%d failed=%d customs=%d", g_css_loaded, g_css_inline,
           g_css_failed, r->customs.size);
    for (int i = 0; i < r->customs.size; i++) {
        ydebug("css   %s = %s", r->customs.data[i].name, r->customs.data[i].value);
    }

    struct yetty_ycore_void_result br = yetty_ylexbor_box_build(r);
    if (YETTY_IS_ERR(br)) {
        return br;
    }

    struct yetty_ycore_void_result lr = yetty_ylexbor_layout(r);
    if (YETTY_IS_ERR(lr)) {
        return lr;
    }

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
                                                    struct yetty_ypaint_core_buffer *buf)
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

/* Make box_vec_reserve visible to box-build. Static-but-shared via
 * attribute would be cleaner; this single-TU project uses a header
 * shim. */
struct yetty_ycore_void_result _yetty_ylexbor_box_vec_reserve(struct yetty_ylexbor_box_vec *v,
                                                              uint32_t want)
{
    return box_vec_reserve(v, want);
}
