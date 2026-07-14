/*
 * ybrowser-libcss — libcss (NetSurf, MIT) bridge for ybrowser.
 *
 * ybrowser keeps lexbor for HTML parsing + DOM but routes the CSS
 * cascade through libcss. Per document: one css_select_ctx, one
 * select_handler vtable that translates libcss queries ("does this
 * element have class X?", "what's its parent?") into lexbor DOM calls,
 * and a list of owned css_stylesheets parsed from <style> blocks /
 * external sheets.
 *
 * The select handler does NOT cache libcss_node_data per element —
 * libcss recomputes matching on every css_select_style call. That's
 * slower than NetSurf but keeps the bridge stateless; profile before
 * adding the cache.
 */

#include "ybrowser-internal.h"
#include "ybrowser-libcss.h"

#ifndef YETTY_HAVE_QUICKJS
#define YETTY_HAVE_QUICKJS 0
#endif

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <libwapcaplet/libwapcaplet.h>

#include <lexbor/dom/dom.h>
#include <lexbor/html/html.h>
#include <lexbor/tag/const.h>

#include <yetty/ytrace/ytrace.h>

/* css_color is 0xAARRGGBB packed (libcss convention). */
#define CSS_COLOR_A(c) (uint8_t)(((c) >> 24) & 0xff)
#define CSS_COLOR_R(c) (uint8_t)(((c) >> 16) & 0xff)
#define CSS_COLOR_G(c) (uint8_t)(((c) >> 8) & 0xff)
#define CSS_COLOR_B(c) (uint8_t)((c) & 0xff)

/* Fixed-point → float (22:10 fixed in libcss). */
static inline float fixed_to_float(css_fixed v)
{
    return (float)v / 1024.0f;
}
static inline css_fixed float_to_fixed(float v)
{
    return (css_fixed)(v * 1024.0f);
}

/* ===========================================================================
 * Embedded minimal UA stylesheet. NetSurf's resources/default.css is
 * ~700 lines and assumes things our renderer doesn't have (lists with
 * markers, tables, ::marker, etc.). This is a stripped-down version
 * covering display/font-weight/font-size/margin for the HTML5 tags we
 * actually render — it matches what ybrowser-box.c's default_for() was
 * already doing in code, just expressed as CSS so libcss can cascade
 * over it. CSS_ORIGIN_UA gives it the lowest precedence.
 * ===========================================================================*/

int yetty_ybrowser_libcss_apply_wikipedia_quirks(struct yetty_ylexbor *r)
{
    if (r == NULL || r->libcss == NULL || r->wiki_quirks_applied) {
        return 0;
    }
    r->wiki_quirks_applied = 1;
    /* Wikipedia/MediaWiki float helpers — applied ONLY to MediaWiki pages (see
     * yetty_ybrowser_libcss_apply_wikipedia_quirks, gated on `mw-` class markers).
     * These use generic class names (`.thumb`, `.floatright`, `.infobox`) that
     * collide with other sites, so they must never be global. MediaWiki ships them
     * in /w/load.php; offline / CSS-less we approximate the default-right float so
     * articles get a paragraph-with-sidebar layout instead of full-width images. */
    static const char UA_WIKIPEDIA_CSS[] =
        "figure[typeof~=\"mw:File/Thumb\"], figure[typeof~=\"mw:File/Frame\"],"
        " .mw-default-size, .thumb, .thumb.tright"
        " { float: right; margin: 0 0 0.5em 0.8em; }\n"
        ".mw-halign-right, .mw-floatright, .floatright, .infobox, table.infobox"
        " { float: right; margin: 0 0 0.5em 0.8em; }\n"
        ".mw-halign-left, .mw-floatleft, .floatleft, figure.mw-halign-left,"
        " figure.mw-default-size.mw-halign-left, .thumb.tleft"
        " { float: left; margin: 0 0.8em 0.5em 0; }\n"
        ".mw-halign-center, figure.mw-halign-center,"
        " figure.mw-default-size.mw-halign-center"
        " { float: none; margin: 0.5em auto; }\n"
        /* MediaWiki leans on `aria-hidden`/`role=presentation` to hide nav,
    	 * jump-links, and edit-section markers. Globally honouring those as
    	 * display:none is WRONG (they are accessibility hints, not visual ones —
    	 * it hid Google News' article-image figures), so the generic UA sheet no
    	 * longer does it. Re-apply it HERE, scoped to MediaWiki pages, where the
    	 * junk genuinely relies on it. */
        "[aria-hidden=\"true\"], [role=\"presentation\"] { display: none !important; }\n"
        /* Top-level `<nav>` on MediaWiki is boilerplate (site nav, breadcrumbs,
    	 * language switcher, edit tabs) that adds no article content. On a general
    	 * site, by contrast, a `<nav>` IS the primary menu and must render as Chrome
    	 * shows it — so this blanket hide is scoped to MediaWiki pages here rather
    	 * than living in the global UA sheet, where it suppressed every site's main
    	 * navigation. */
        "nav { display: none !important; }\n";
    return yetty_ybrowser_libcss_add_sheet(r, UA_WIKIPEDIA_CSS, sizeof(UA_WIKIPEDIA_CSS) - 1,
                                           CSS_ORIGIN_USER, NULL);
}

/* ===========================================================================
 * Select-handler callbacks. `pw` is `struct yetty_ylexbor *r`; `node` is
 * always `lxb_dom_node_t *` (we cast from element where libcss gave us
 * one to start with). All callbacks return css_error.
 * ===========================================================================*/

static lxb_dom_node_t *as_node(void *node)
{
    return (lxb_dom_node_t *)node;
}
static lxb_dom_element_t *as_element(void *node)
{
    return lxb_dom_interface_element(as_node(node));
}

/* lwc-intern lexbor element's local-name. Caller owns one ref. */
static css_error intern_local_name(lxb_dom_node_t *n, lwc_string **out)
{
    size_t nlen = 0;
    const lxb_char_t *name = lxb_dom_node_name(n, &nlen);
    if (name == NULL) {
        return CSS_NOMEM;
    }
    /* HTML local-names are ASCII-uppercase from lexbor. CSS selectors
     * match case-insensitively for HTML, but libcss compares via
     * lwc_string_caseless_isequal so we can hand it back as-is. */
    char *lower = malloc(nlen);
    if (!lower) {
        return CSS_NOMEM;
    }
    for (size_t i = 0; i < nlen; i++) {
        lower[i] = (char)tolower(name[i]);
    }
    lwc_error e = lwc_intern_string(lower, nlen, out);
    free(lower);
    return e == lwc_error_ok ? CSS_OK : CSS_NOMEM;
}

static css_error cb_node_name(void *pw, void *node, css_qname *qname)
{
    (void)pw;
    qname->ns = NULL;
    return intern_local_name(as_node(node), &qname->name);
}

static css_error cb_node_classes(void *pw, void *node, lwc_string ***classes, uint32_t *n_classes)
{
    *classes = NULL;
    *n_classes = 0;
    size_t alen = 0;
    const lxb_char_t *attr =
        lxb_dom_element_get_attribute(as_element(node), (const lxb_char_t *)"class", 5, &alen);
    if (!attr || alen == 0) {
        return CSS_OK;
    }
    /* Split on ASCII whitespace, intern each token. libcss unrefs the
     * strings but never frees the ARRAY, and style-sharing can request
     * classes for several nodes within one select — so each call gets its
     * own array, and the batch is freed by yetty_ybrowser_libcss_select
     * after css_select_style returns (see pending_class_arrays). */
    lwc_string **arr = NULL;
    uint32_t count = 0, cap = 0;
    size_t i = 0;
    while (i < alen) {
        while (i < alen &&
               (attr[i] == ' ' || attr[i] == '\t' || attr[i] == '\n' || attr[i] == '\r')) {
            i++;
        }
        size_t s = i;
        while (i < alen &&
               !(attr[i] == ' ' || attr[i] == '\t' || attr[i] == '\n' || attr[i] == '\r')) {
            i++;
        }
        if (i == s) {
            break;
        }
        if (count == cap) {
            uint32_t ncap = cap ? cap * 2 : 4;
            lwc_string **na = realloc(arr, ncap * sizeof(*na));
            if (!na) {
                for (uint32_t k = 0; k < count; k++) {
                    lwc_string_unref(arr[k]);
                }
                free(arr);
                return CSS_NOMEM;
            }
            arr = na;
            cap = ncap;
        }
        lwc_error e = lwc_intern_string((const char *)attr + s, i - s, &arr[count]);
        if (e != lwc_error_ok) {
            for (uint32_t k = 0; k < count; k++) {
                lwc_string_unref(arr[k]);
            }
            free(arr);
            return CSS_NOMEM;
        }
        count++;
    }
    if (arr != NULL) {
        /* Record for the post-select batch free. On record failure keep
         * the strings valid for libcss and accept the one-array leak. */
        struct yetty_ylexbor *r = pw;
        struct yetty_ybrowser_libcss *lc = r->libcss;
        if (lc->pending_class_array_count == lc->pending_class_array_cap) {
            uint32_t ncap = lc->pending_class_array_cap ? lc->pending_class_array_cap * 2 : 8;
            lwc_string ***grown = realloc(lc->pending_class_arrays, ncap * sizeof(*grown));
            if (grown) {
                lc->pending_class_arrays = grown;
                lc->pending_class_array_cap = ncap;
            }
        }
        if (lc->pending_class_array_count < lc->pending_class_array_cap) {
            lc->pending_class_arrays[lc->pending_class_array_count++] = arr;
        }
    }
    *classes = arr;
    *n_classes = count;
    return CSS_OK;
}

static css_error cb_node_id(void *pw, void *node, lwc_string **id)
{
    (void)pw;
    *id = NULL;
    size_t alen = 0;
    const lxb_char_t *attr =
        lxb_dom_element_get_attribute(as_element(node), (const lxb_char_t *)"id", 2, &alen);
    if (!attr || alen == 0) {
        return CSS_OK;
    }
    lwc_error e = lwc_intern_string((const char *)attr, alen, id);
    return e == lwc_error_ok ? CSS_OK : CSS_NOMEM;
}

static css_error cb_parent_node(void *pw, void *node, void **parent)
{
    (void)pw;
    lxb_dom_node_t *n = as_node(node);
    lxb_dom_node_t *p = n->parent;
    if (p && p->type == LXB_DOM_NODE_TYPE_ELEMENT) {
        *parent = p;
    } else {
        *parent = NULL;
    }
    return CSS_OK;
}

static css_error cb_sibling_node(void *pw, void *node, void **sibling)
{
    (void)pw;
    lxb_dom_node_t *n = as_node(node)->prev;
    while (n && n->type != LXB_DOM_NODE_TYPE_ELEMENT) {
        n = n->prev;
    }
    *sibling = n;
    return CSS_OK;
}

/* For named_* lookups we compare the libcss qname.name (case-insensitively)
 * against the lexbor local_name (uppercased ASCII). lwc_string holds the
 * lowercased version; lexbor stores lowercase too in node->local_name
 * but exposes uppercased via node_name. lxb_dom_element_local_name
 * returns lowercase. We compare against lowercase. */
static bool tag_matches_qname(lxb_dom_node_t *n, const css_qname *qname)
{
    if (n == NULL || n->type != LXB_DOM_NODE_TYPE_ELEMENT) {
        return false;
    }
    if (qname == NULL || qname->name == NULL) {
        return true; /* universal selector */
    }
    size_t nlen = 0;
    const lxb_char_t *name = lxb_dom_element_local_name(lxb_dom_interface_element(n), &nlen);
    if (!name) {
        return false;
    }
    size_t qlen = lwc_string_length(qname->name);
    if (qlen != nlen) {
        return false;
    }
    const char *qstr = lwc_string_data(qname->name);
    for (size_t i = 0; i < nlen; i++) {
        if (tolower(qstr[i]) != tolower(name[i])) {
            return false;
        }
    }
    return true;
}

static css_error cb_named_ancestor_node(void *pw, void *node, const css_qname *qname,
                                        void **ancestor)
{
    (void)pw;
    *ancestor = NULL;
    for (lxb_dom_node_t *p = as_node(node)->parent; p; p = p->parent) {
        if (tag_matches_qname(p, qname)) {
            *ancestor = p;
            return CSS_OK;
        }
    }
    return CSS_OK;
}

static css_error cb_named_parent_node(void *pw, void *node, const css_qname *qname, void **parent)
{
    (void)pw;
    *parent = NULL;
    lxb_dom_node_t *p = as_node(node)->parent;
    while (p && p->type != LXB_DOM_NODE_TYPE_ELEMENT) {
        p = p->parent;
    }
    if (tag_matches_qname(p, qname)) {
        *parent = p;
    }
    return CSS_OK;
}

static css_error cb_named_sibling_node(void *pw, void *node, const css_qname *qname, void **sibling)
{
    (void)pw;
    *sibling = NULL;
    lxb_dom_node_t *s = as_node(node)->prev;
    while (s && s->type != LXB_DOM_NODE_TYPE_ELEMENT) {
        s = s->prev;
    }
    if (tag_matches_qname(s, qname)) {
        *sibling = s;
    }
    return CSS_OK;
}

static css_error cb_named_generic_sibling_node(void *pw, void *node, const css_qname *qname,
                                               void **sibling)
{
    (void)pw;
    *sibling = NULL;
    for (lxb_dom_node_t *s = as_node(node)->prev; s; s = s->prev) {
        if (tag_matches_qname(s, qname)) {
            *sibling = s;
            return CSS_OK;
        }
    }
    return CSS_OK;
}

static css_error cb_node_has_name(void *pw, void *node, const css_qname *qname, bool *match)
{
    (void)pw;
    *match = tag_matches_qname(as_node(node), qname);
    return CSS_OK;
}

static css_error cb_node_has_class(void *pw, void *node, lwc_string *name, bool *match)
{
    (void)pw;
    *match = false;
    size_t alen = 0;
    const lxb_char_t *attr =
        lxb_dom_element_get_attribute(as_element(node), (const lxb_char_t *)"class", 5, &alen);
    if (!attr || alen == 0) {
        return CSS_OK;
    }
    size_t qlen = lwc_string_length(name);
    const char *qdata = lwc_string_data(name);
    size_t i = 0;
    while (i < alen) {
        while (i < alen &&
               (attr[i] == ' ' || attr[i] == '\t' || attr[i] == '\n' || attr[i] == '\r')) {
            i++;
        }
        size_t s = i;
        while (i < alen &&
               !(attr[i] == ' ' || attr[i] == '\t' || attr[i] == '\n' || attr[i] == '\r')) {
            i++;
        }
        if (i - s == qlen && memcmp(attr + s, qdata, qlen) == 0) {
            *match = true;
            return CSS_OK;
        }
    }
    return CSS_OK;
}

static css_error cb_node_has_id(void *pw, void *node, lwc_string *name, bool *match)
{
    (void)pw;
    *match = false;
    size_t alen = 0;
    const lxb_char_t *attr =
        lxb_dom_element_get_attribute(as_element(node), (const lxb_char_t *)"id", 2, &alen);
    if (!attr || alen == 0) {
        return CSS_OK;
    }
    if (alen == lwc_string_length(name) && memcmp(attr, lwc_string_data(name), alen) == 0) {
        *match = true;
    }
    return CSS_OK;
}

/* qname-attr handlers — we ignore the qname.ns since lexbor's HTML
 * attributes don't carry namespaces. */
static const lxb_char_t *get_attr_value(lxb_dom_element_t *el, const css_qname *qname,
                                        size_t *out_len)
{
    if (!qname || !qname->name) {
        return NULL;
    }
    const char *name = lwc_string_data(qname->name);
    size_t nlen = lwc_string_length(qname->name);
    return lxb_dom_element_get_attribute(el, (const lxb_char_t *)name, nlen, out_len);
}

static css_error cb_node_has_attribute(void *pw, void *node, const css_qname *qname, bool *match)
{
    (void)pw;
    size_t alen = 0;
    *match = get_attr_value(as_element(node), qname, &alen) != NULL;
    return CSS_OK;
}

static css_error cb_node_has_attribute_equal(void *pw, void *node, const css_qname *qname,
                                             lwc_string *value, bool *match)
{
    (void)pw;
    *match = false;
    size_t alen = 0;
    const lxb_char_t *av = get_attr_value(as_element(node), qname, &alen);
    if (!av) {
        return CSS_OK;
    }
    if (alen == lwc_string_length(value) && memcmp(av, lwc_string_data(value), alen) == 0) {
        *match = true;
    }
    return CSS_OK;
}

static css_error cb_node_has_attribute_dashmatch(void *pw, void *node, const css_qname *qname,
                                                 lwc_string *value, bool *match)
{
    (void)pw;
    *match = false;
    size_t alen = 0;
    const lxb_char_t *av = get_attr_value(as_element(node), qname, &alen);
    if (!av) {
        return CSS_OK;
    }
    size_t vlen = lwc_string_length(value);
    const char *vdata = lwc_string_data(value);
    if (alen == vlen && memcmp(av, vdata, vlen) == 0) {
        *match = true;
    } else if (alen > vlen && av[vlen] == '-' && memcmp(av, vdata, vlen) == 0) {
        *match = true;
    }
    return CSS_OK;
}

static css_error cb_node_has_attribute_includes(void *pw, void *node, const css_qname *qname,
                                                lwc_string *value, bool *match)
{
    (void)pw;
    *match = false;
    size_t alen = 0;
    const lxb_char_t *av = get_attr_value(as_element(node), qname, &alen);
    if (!av) {
        return CSS_OK;
    }
    size_t vlen = lwc_string_length(value);
    const char *vdata = lwc_string_data(value);
    size_t i = 0;
    while (i < alen) {
        while (i < alen && (av[i] == ' ' || av[i] == '\t' || av[i] == '\n' || av[i] == '\r')) {
            i++;
        }
        size_t s = i;
        while (i < alen && !(av[i] == ' ' || av[i] == '\t' || av[i] == '\n' || av[i] == '\r')) {
            i++;
        }
        if (i - s == vlen && memcmp(av + s, vdata, vlen) == 0) {
            *match = true;
            return CSS_OK;
        }
    }
    return CSS_OK;
}

static css_error cb_node_has_attribute_prefix(void *pw, void *node, const css_qname *qname,
                                              lwc_string *value, bool *match)
{
    (void)pw;
    *match = false;
    size_t alen = 0;
    const lxb_char_t *av = get_attr_value(as_element(node), qname, &alen);
    if (!av) {
        return CSS_OK;
    }
    size_t vlen = lwc_string_length(value);
    if (alen >= vlen && memcmp(av, lwc_string_data(value), vlen) == 0) {
        *match = true;
    }
    return CSS_OK;
}

static css_error cb_node_has_attribute_suffix(void *pw, void *node, const css_qname *qname,
                                              lwc_string *value, bool *match)
{
    (void)pw;
    *match = false;
    size_t alen = 0;
    const lxb_char_t *av = get_attr_value(as_element(node), qname, &alen);
    if (!av) {
        return CSS_OK;
    }
    size_t vlen = lwc_string_length(value);
    if (alen >= vlen && memcmp(av + alen - vlen, lwc_string_data(value), vlen) == 0) {
        *match = true;
    }
    return CSS_OK;
}

static css_error cb_node_has_attribute_substring(void *pw, void *node, const css_qname *qname,
                                                 lwc_string *value, bool *match)
{
    (void)pw;
    *match = false;
    size_t alen = 0;
    const lxb_char_t *av = get_attr_value(as_element(node), qname, &alen);
    if (!av) {
        return CSS_OK;
    }
    size_t vlen = lwc_string_length(value);
    const char *vdata = lwc_string_data(value);
    if (vlen == 0) {
        return CSS_OK;
    }
    for (size_t i = 0; i + vlen <= alen; i++) {
        if (memcmp(av + i, vdata, vlen) == 0) {
            *match = true;
            return CSS_OK;
        }
    }
    return CSS_OK;
}

static css_error cb_node_is_root(void *pw, void *node, bool *match)
{
    (void)pw;
    lxb_dom_node_t *p = as_node(node)->parent;
    *match = (p == NULL || p->type != LXB_DOM_NODE_TYPE_ELEMENT);
    return CSS_OK;
}

static css_error cb_node_count_siblings(void *pw, void *node, bool same_name, bool after,
                                        int32_t *count)
{
    (void)pw;
    int32_t n = 0;
    lxb_dom_node_t *self = as_node(node);
    lxb_dom_node_t *s = after ? self->next : self->prev;
    while (s) {
        if (s->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            if (!same_name || s->local_name == self->local_name) {
                n++;
            }
        }
        s = after ? s->next : s->prev;
    }
    *count = n;
    return CSS_OK;
}

static css_error cb_node_is_empty(void *pw, void *node, bool *match)
{
    (void)pw;
    *match = (as_node(node)->first_child == NULL);
    return CSS_OK;
}

/* :link / :visited / :hover / :focus / :active — we don't track UI
 * state. Treat <a href> as :link, everything else as not-link/hover/etc.
 * :visited always false (we have no history). */
static css_error cb_node_is_link(void *pw, void *node, bool *match)
{
    (void)pw;
    *match = false;
    lxb_dom_element_t *el = as_element(node);
    if (el && (el->node.local_name == LXB_TAG_A || el->node.local_name == LXB_TAG_AREA)) {
        size_t hlen = 0;
        const lxb_char_t *href =
            lxb_dom_element_get_attribute(el, (const lxb_char_t *)"href", 4, &hlen);
        if (href && hlen > 0) {
            *match = true;
        }
    }
    return CSS_OK;
}

static css_error cb_false(void *pw, void *node, bool *match)
{
    (void)pw;
    (void)node;
    *match = false;
    return CSS_OK;
}

static css_error cb_node_is_lang(void *pw, void *node, lwc_string *lang, bool *match)
{
    (void)pw;
    *match = false;
    /* Walk ancestor chain looking for `lang=` attribute. */
    for (lxb_dom_node_t *p = as_node(node); p && p->type == LXB_DOM_NODE_TYPE_ELEMENT;
         p = p->parent) {
        size_t alen = 0;
        const lxb_char_t *av = lxb_dom_element_get_attribute(lxb_dom_interface_element(p),
                                                             (const lxb_char_t *)"lang", 4, &alen);
        if (av && alen > 0) {
            size_t vlen = lwc_string_length(lang);
            const char *vdata = lwc_string_data(lang);
            if (alen == vlen && memcmp(av, vdata, vlen) == 0) {
                *match = true;
            } else if (alen > vlen && av[vlen] == '-' && memcmp(av, vdata, vlen) == 0) {
                *match = true;
            }
            return CSS_OK;
        }
    }
    return CSS_OK;
}

static css_error cb_node_presentational_hint(void *pw, void *node, uint32_t *nhints,
                                             css_hint **hints)
{
    (void)pw;
    (void)node;
    *nhints = 0;
    *hints = NULL;
    return CSS_OK;
}

static css_error cb_ua_default_for_property(void *pw, uint32_t property, css_hint *hint)
{
    (void)pw;
    /* libcss queries this for a small set of properties where the
     * cascade demands a concrete UA default the engine can't pick on
     * its own (COLOR / FONT_FAMILY / QUOTES / VOICE_FAMILY). For
     * everything else, returning CSS_INVALID tells libcss to use its
     * built-in initial value.
     *
     * Returning CSS_INVALID for CSS_PROP_COLOR / FONT_FAMILY etc.
     * makes css__initial_* propagate the error up through
     * css_select_style, and the cleanup path then tries to free the
     * static empty_bloom sentinel — visible as `free(): invalid
     * pointer`. Mirror NetSurf's reference handler. */
    switch (property) {
    case CSS_PROP_COLOR:
        hint->data.color = 0xff000000; /* opaque black */
        hint->status = CSS_COLOR_COLOR;
        return CSS_OK;
    case CSS_PROP_FONT_FAMILY:
        hint->data.strings = NULL;
        hint->status = CSS_FONT_FAMILY_SANS_SERIF;
        return CSS_OK;
    case CSS_PROP_QUOTES:
        hint->data.strings = NULL;
        hint->status = CSS_QUOTES_NONE;
        return CSS_OK;
    case CSS_PROP_VOICE_FAMILY:
        hint->data.strings = NULL;
        hint->status = 0;
        return CSS_OK;
    default:
        return CSS_INVALID;
    }
}

/* Find the slot for `node` (or the empty slot where it would go). Open
 * addressing, linear probe; cap is always a power of two. */
static struct yetty_ybrowser_libcss_node_slot *node_data_slot_find(struct yetty_ybrowser_libcss *lc,
                                                                   void *node)
{
    size_t mask = lc->node_data_slot_cap - 1;
    size_t index = ((uintptr_t)node >> 4) & mask;
    for (;;) {
        struct yetty_ybrowser_libcss_node_slot *slot = &lc->node_data_slots[index];
        if (slot->node == node || slot->node == NULL) {
            return slot;
        }
        index = (index + 1) & mask;
    }
}

static int node_data_store_grow(struct yetty_ybrowser_libcss *lc)
{
    size_t new_cap = lc->node_data_slot_cap ? lc->node_data_slot_cap * 2 : 256;
    struct yetty_ybrowser_libcss_node_slot *old_slots = lc->node_data_slots;
    size_t old_cap = lc->node_data_slot_cap;
    struct yetty_ybrowser_libcss_node_slot *new_slots = calloc(new_cap, sizeof(*new_slots));
    if (!new_slots) {
        return -1;
    }
    lc->node_data_slots = new_slots;
    lc->node_data_slot_cap = new_cap;
    for (size_t i = 0; i < old_cap; i++) {
        if (old_slots[i].node) {
            *node_data_slot_find(lc, old_slots[i].node) = old_slots[i];
        }
    }
    free(old_slots);
    return 0;
}

static css_error cb_set_libcss_node_data(void *pw, void *node, void *data)
{
    /* Keep-alive store, one live entry per element. libcss keeps USING
     * the handed-over node_data after this returns (the parent-bloom path
     * reads node_data->bloom for the rest of the select), so destroying
     * here is a use-after-free — and the old "(void)data" discard leaked
     * node_data + a saturated parent bloom + one computed-style ref per
     * pseudo element, for every element of every select. Replacing an
     * element's previous entry destroys it via the official handler; only
     * the current select's node_data pointers are ever live-referenced,
     * and those are never the replaced ones. */
    struct yetty_ylexbor *r = pw;
    struct yetty_ybrowser_libcss *lc = r ? r->libcss : NULL;
    if (!lc || !data) {
        return CSS_OK;
    }
    if (lc->node_data_slot_cap == 0 ||
        lc->node_data_slot_count * 10 >= lc->node_data_slot_cap * 7) {
        if (node_data_store_grow(lc) != 0) {
            /* Can't record it — keep libcss's data alive (leak) rather
             * than free something still in use. */
            return CSS_OK;
        }
    }
    struct yetty_ybrowser_libcss_node_slot *slot = node_data_slot_find(lc, node);
    if (slot->node == NULL) {
        slot->node = node;
        lc->node_data_slot_count++;
    } else if (slot->data && slot->data != data) {
        (void)css_libcss_node_data_handler(&lc->handler, CSS_NODE_DELETED, pw, node, NULL,
                                           slot->data);
    }
    slot->data = data;
    return CSS_OK;
}

static css_error cb_get_libcss_node_data(void *pw, void *node, void **data)
{
    /* Deliberately NOT serving the store back to libcss: returning cached
     * node_data enables the style-sharing fast path, which without
     * DOM-mutation invalidation would apply stale sibling styles on
     * JS-mutating pages. The store exists to own the memory, not to
     * cache. */
    (void)pw;
    (void)node;
    *data = NULL;
    return CSS_OK;
}

/* libcss requires a URL resolution callback at stylesheet_create time
 * (rejects with CSS_BADPARM otherwise). We don't resolve @import /
 * background-image URLs through libcss — fetching is the host's job
 * via libcurl — so the resolver just hands back the relative URL as-is
 * with a fresh ref. Good enough for parsing rules; the abs URL only
 * matters if someone actually consumes it. */
static css_error url_resolve(void *pw, const char *base, lwc_string *rel, lwc_string **abs)
{
    (void)pw;
    (void)base;
    *abs = lwc_string_ref(rel);
    return CSS_OK;
}

/* ===========================================================================
 * Public lifecycle.
 * ===========================================================================*/

int yetty_ybrowser_libcss_init(struct yetty_ylexbor *r)
{
    if (r->libcss != NULL) {
        return 0;
    }
    struct yetty_ybrowser_libcss *lc = calloc(1, sizeof(*lc));
    if (!lc) {
        return -1;
    }
    lc->r = r;
    /* Vtable. Sizes/ABI must match css_select_handler exactly. */
    static const css_select_handler select_handler = {
        .handler_version = CSS_SELECT_HANDLER_VERSION_1,
        .node_name = cb_node_name,
        .node_classes = cb_node_classes,
        .node_id = cb_node_id,
        .named_ancestor_node = cb_named_ancestor_node,
        .named_parent_node = cb_named_parent_node,
        .named_sibling_node = cb_named_sibling_node,
        .named_generic_sibling_node = cb_named_generic_sibling_node,
        .parent_node = cb_parent_node,
        .sibling_node = cb_sibling_node,
        .node_has_name = cb_node_has_name,
        .node_has_class = cb_node_has_class,
        .node_has_id = cb_node_has_id,
        .node_has_attribute = cb_node_has_attribute,
        .node_has_attribute_equal = cb_node_has_attribute_equal,
        .node_has_attribute_dashmatch = cb_node_has_attribute_dashmatch,
        .node_has_attribute_includes = cb_node_has_attribute_includes,
        .node_has_attribute_prefix = cb_node_has_attribute_prefix,
        .node_has_attribute_suffix = cb_node_has_attribute_suffix,
        .node_has_attribute_substring = cb_node_has_attribute_substring,
        .node_is_root = cb_node_is_root,
        .node_count_siblings = cb_node_count_siblings,
        .node_is_empty = cb_node_is_empty,
        .node_is_link = cb_node_is_link,
        .node_is_visited = cb_false,
        .node_is_hover = cb_false,
        .node_is_active = cb_false,
        .node_is_focus = cb_false,
        .node_is_enabled = cb_false,
        .node_is_disabled = cb_false,
        .node_is_checked = cb_false,
        .node_is_target = cb_false,
        .node_is_lang = cb_node_is_lang,
        .node_presentational_hint = cb_node_presentational_hint,
        .ua_default_for_property = cb_ua_default_for_property,
        .set_libcss_node_data = cb_set_libcss_node_data,
        .get_libcss_node_data = cb_get_libcss_node_data,
    };
    lc->handler = select_handler;

    if (css_select_ctx_create(&lc->select_ctx) != CSS_OK) {
        free(lc);
        return -1;
    }

    /* Viewport / unit context. Values are in CSS pixels (1 CSS px =
     * 1/96 of an inch). We don't have a real DPI for the renderer; the
     * standard "96" works for em/rem/% but means cm/mm/in/pt are off
     * by the device DPI — acceptable for what we render. */
    /* unit_ctx.measure and unit_ctx.pw are const-qualified in libcss
     * 0.10+, so we initialise the whole struct at once via designated
     * initialiser rather than per-field assignment. calloc has already
     * zeroed everything; the assignment below overwrites the fields
     * we set explicitly. */
    css_unit_ctx uctx = {
        .viewport_width = float_to_fixed((float)r->viewport_w),
        .viewport_height = float_to_fixed((float)r->viewport_h),
        .font_size_default = float_to_fixed(r->default_font_size),
        .font_size_minimum = 0,
        .device_dpi = float_to_fixed(96.0f),
        .root_style = NULL,
        .pw = NULL,
        .measure = NULL,
    };
    memcpy(&lc->unit_ctx, &uctx, sizeof(uctx));

    lc->media.type = CSS_MEDIA_SCREEN;
    lc->media.width = lc->unit_ctx.viewport_width;
    lc->media.height = lc->unit_ctx.viewport_height;
    lc->media.color = float_to_fixed(8.0f); /* 8 bpp per channel */

    r->libcss = lc;

    /* Bake in the UA defaults. */
    /* Canonical browser defaults (matches NetSurf resources/default.css
     * for the common HTML5 tags). Critical that body's `margin: 8px` is
     * present so empty pages produce a visible body box; without it the
     * cascade reports margin:0 and the layout pass collapses the body to
     * zero height. */
    static const char UA_DEFAULT_CSS[] =
        "html, body { display: block; }\n"
        "body { margin: 8px; line-height: 1.33; }\n"
        "div, section, article, aside, header, footer, nav, main, figcaption,"
        " hgroup, address, blockquote, form, dd, dl, dt, fieldset { display: block; }\n"
        /* `figure` needs an explicit display:block — libcss's compiled-in
         * defaults report computed display as CSS_DISPLAY_TABLE (6) for
         * <figure> without this rule. !important to override whatever the
         * library's internal UA sheet declared. Without this our layout
         * dispatches to layout_table on every <figure>, which scans for
         * <tr> descendants, finds none, and returns h=0 — so the inner
         * <img> stays at the memset-default (0,0), giving the
         * "all images stacked at the upper-left" symptom. */
        "figure { display: block !important; }\n"
        "ul, ol { display: block; margin: 1em 0; padding-left: 40px; }\n"
        "li { display: list-item; margin: 0; }\n"
        "head, title, meta, link, script, style, template { display: none; }\n"
#if YETTY_HAVE_QUICKJS
        /* Scripts run in this build — <noscript> fallbacks stay hidden
         * exactly as in any JS-capable browser. Without this, Google's
         * "activate JavaScript" interstitial paints over the real,
         * script-built page (maps.google.com). !important so an author
         * rule can't resurrect it. */
        "noscript { display: none !important; }\n"
#else
        /* QuickJS is compiled out: the no-JS fallbacks (often <img>
         * versions of what JS would otherwise inject) ARE the page
         * content. */
        "noscript { display: block; }\n"
#endif
        /* Embedded content we don't render — hide outright so the walker
         * doesn't surface their text content (MathML etc.) as garbage at
         * the top of the page. <svg>, <iframe> and <video> are NOT hidden:
         * the box builder gives each a replaced box (subtree never walked)
         * so the layout reserves the right space — the iframe box then
         * renders its src document as a nested browsing context; the video
         * box keeps hero/media frames from collapsing (playback is a
         * separate concern). */
        "math, audio, object, embed, canvas { display: none; }\n"
        "video { display: inline; }\n"
        "span, a, strong, b, em, i, cite, code, small, sub, sup, mark, ins, del,"
        " s, u, kbd, samp, var, time, q, abbr, dfn { display: inline; }\n"
        "br { display: inline; }\n"
        "p { display: block; margin: 1em 0; }\n"
        "pre { display: block; margin: 1em 0; white-space: pre; }\n"
        /* Tables — minimal CSS 2.1 defaults. ybrowser's layout pass
         * recognises CSS_DISPLAY_TABLE and walks the descendant <tr>
         * elements to render cells side-by-side. */
        "table { display: table; border-collapse: separate; border-spacing: 2px; }\n"
        "thead, tbody, tfoot { display: table-row-group; }\n"
        "tr { display: table-row; }\n"
        "td, th { display: table-cell; padding: 2px; vertical-align: top; }\n"
        "th { font-weight: bold; text-align: center; }\n"
        "caption { display: table-caption; text-align: center; }\n"
        "h1 { display: block; font-size: 2em;    font-weight: bold; margin: 0.67em 0; }\n"
        "h2 { display: block; font-size: 1.5em;  font-weight: bold; margin: 0.83em 0; }\n"
        "h3 { display: block; font-size: 1.17em; font-weight: bold; margin: 1em 0; }\n"
        "h4 { display: block; font-size: 1em;    font-weight: bold; margin: 1.33em 0; }\n"
        "h5 { display: block; font-size: 0.83em; font-weight: bold; margin: 1.67em 0; }\n"
        "h6 { display: block; font-size: 0.67em; font-weight: bold; margin: 2.33em 0; }\n"
        "strong, b { font-weight: bold; }\n"
        "em, i, cite, dfn { font-style: italic; }\n"
        "a { color: #00e; text-decoration: underline; }\n"
        "hr { display: block; margin: 0.5em auto; border-top: 1px solid #888; }\n"
        /* Only the HTML `hidden` attribute hides an element visually. NOTE:
         * `aria-hidden="true"` and `role="presentation"` are ACCESSIBILITY hints
         * — they remove an element from the a11y tree but DO NOT affect visual
         * rendering (Chrome paints them normally). Mapping them to display:none
         * wrongly drops visible content: e.g. Google News marks each story's
         * thumbnail figure `aria-hidden="true" role="presentation"` (the headline
         * conveys it to screen readers) yet still shows the image. Site-specific
         * hidden-nav junk is handled by explicit selectors below, not by abusing
         * these attributes. */
        "[hidden] { display: none !important; }\n"
        /* NOTE: Wikipedia's float helpers used to live here and were injected into
         * EVERY page — floating any site's generic `.thumb`/`.floatright`/`.infobox`
         * out of flow (a news card's thumbnail collapsed to 0x0). They now live in
         * UA_WIKIPEDIA_CSS, applied only to MediaWiki pages (see
         * yetty_ybrowser_libcss_apply_wikipedia_quirks). */
        /* Wikipedia's hidden navigation: jump links, edit-section markers,
         * collapsed nav modules, sidebar menus, footer, indicators,
         * language-switcher etc. None of these contribute article content
         * to a CSS-less / terminal renderer. `!important` because the
         * loaded author CSS often overrides display via clip-path /
         * positional tricks that look hidden in a real browser but render
         * as visible text for us. The classic symptom without this rule is
         * "Jump to content" leaking into the upper-left corner of every
         * Wikipedia page. */
        ".mw-jump-link, .mw-editsection, .navbox, .navbar, .vector-menu,"
        " .vector-header, .mw-portlet, .mw-footer, .mw-indicators,"
        " .mw-cite-backlink, .mw-cite-direction-marker, .mw-hidden,"
        " #vector-toc-pinned-container, .vector-toc, .vector-page-tools,"
        " .vector-appearance-landmark, .vector-language-button-container,"
        " .mw-page-container-inner > .vector-column-start,"
        " .mw-page-container-inner > .vector-column-end,"
        " .vector-sticky-pinned-container, .vector-sitenotice-container,"
        " .mw-footer-container, #footer, .vector-pinnable-header,"
        " .skip-link, .visualClear"
        " { display: none !important; }\n";
    if (yetty_ybrowser_libcss_add_sheet(r, UA_DEFAULT_CSS, sizeof(UA_DEFAULT_CSS) - 1,
                                        CSS_ORIGIN_UA, NULL) != 0) {
        ydebug("libcss: UA stylesheet append failed");
    }
    /* Hostile-author nav hider, installed as CSS_ORIGIN_USER with
     * !important. User !important is the highest cascade origin per
     * CSS spec (beats author !important and UA !important). We need
     * this because libcss treats UA-origin rules as LOWER priority
     * than author rules even with !important, so my UA stylesheet's
     * `.mw-jump-link { display: none !important }` lost to Wikipedia's
     * `.mw-jump-link { display: block }` and the "Jump to content" /
     * sidebar / nav-pinning leaked through. CSS_ORIGIN_USER places
     * these one tier higher in the cascade. */
    /* NOTE: don't hide .vector-page-titlebar — it wraps the article
     * <h1>. The header element with that class IS the page title
     * region we want to render. */
    static const char NAV_HIDE_CSS[] =
        "[hidden],"
        " .mw-jump-link, .mw-editsection, .navbox, .navbar, .vector-menu,"
        " .vector-header, .vector-page-toolbar,"
        " .vector-sticky-header, .vector-sticky-pinned-container,"
        " .vector-sitenotice-container, .vector-pinnable-header,"
        " .mw-portlet, .mw-footer, .mw-footer-container, #footer,"
        " .mw-indicators, .mw-cite-backlink, .mw-cite-direction-marker,"
        " .mw-hidden, .mw-page-container-inner > .vector-column-start,"
        " .mw-page-container-inner > .vector-column-end,"
        " #vector-toc-pinned-container, .vector-toc, .vector-page-tools,"
        " .vector-appearance-landmark, .vector-language-button-container,"
        " .skip-link, .visualClear"
        " { display: none !important; }\n"
        /* And re-pin our figure fix at user-origin too in case libcss's
         * compiled-in defaults for <figure> beat the UA origin. */
        "figure { display: block !important; }\n";
    if (yetty_ybrowser_libcss_add_sheet(r, NAV_HIDE_CSS, sizeof(NAV_HIDE_CSS) - 1, CSS_ORIGIN_USER,
                                        NULL) != 0) {
        ydebug("libcss: nav-hide stylesheet append failed");
    }
    return 0;
}

void yetty_ybrowser_libcss_destroy(struct yetty_ylexbor *r)
{
    if (r == NULL || r->libcss == NULL) {
        return;
    }
    struct yetty_ybrowser_libcss *lc = r->libcss;
    /* Release every element's stored node_data (blooms + computed-style
     * refs) through the official handler before the ctx goes away. */
    for (size_t i = 0; i < lc->node_data_slot_cap; i++) {
        if (lc->node_data_slots[i].node && lc->node_data_slots[i].data) {
            (void)css_libcss_node_data_handler(&lc->handler, CSS_NODE_DELETED, r,
                                               lc->node_data_slots[i].node, NULL,
                                               lc->node_data_slots[i].data);
        }
    }
    free(lc->node_data_slots);
    if (lc->select_ctx) {
        css_select_ctx_destroy(lc->select_ctx);
    }
    for (size_t i = 0; i < lc->sheet_count; i++) {
        css_stylesheet_destroy(lc->sheets[i]);
    }
    free(lc->sheets);
    for (uint32_t i = 0; i < lc->pending_class_array_count; i++) {
        free(lc->pending_class_arrays[i]);
    }
    free(lc->pending_class_arrays);
    free(lc);
    r->libcss = NULL;
}

static void libcss_load_imports(struct yetty_ylexbor *r, css_stylesheet *parent,
                                const char *parent_url, css_origin origin);

int yetty_ybrowser_libcss_add_sheet(struct yetty_ylexbor *r, const char *css, size_t len,
                                    css_origin origin, const char *sheet_url)
{
    if (r == NULL || r->libcss == NULL || css == NULL || len == 0) {
        return -1;
    }
    struct yetty_ybrowser_libcss *lc = r->libcss;

    css_stylesheet_params params = {0};
    params.params_version = CSS_STYLESHEET_PARAMS_VERSION_1;
    params.level = CSS_LEVEL_DEFAULT;
    params.charset = "UTF-8";
    params.url = sheet_url ? sheet_url : "ybrowser:inline";
    params.allow_quirks = false;
    params.inline_style = false;
    params.resolve = url_resolve;

    css_stylesheet *sheet = NULL;
    css_error err = css_stylesheet_create(&params, &sheet);
    if (err != CSS_OK) {
        ydebug("libcss stylesheet_create -> %d", (int)err);
        return -1;
    }
    /* Pre-resolve var(--name) references in the source. libcss 0.9.x
     * has no CSS-custom-properties support — declarations whose value
     * contains var() get dropped as invalid. We textually substitute
     * each var(...) with the value scanned out of prior :root /
     * html / * rules so libcss receives a vanilla CSS 2.1/3 stylesheet
     * with concrete colors and lengths. */
    char *resolved = yetty_ylexbor_css_vars_resolve(r, css, len);
    const char *eff = resolved ? resolved : css;
    size_t eff_len = resolved ? strlen(resolved) : len;

    /* libcss 0.9.x has no Media Queries Level 4 range syntax — rewrite
     * `@media (width>=960px)` etc. to classic min-/max-width so those blocks
     * (often a site's entire desktop layout) are not dropped. */
    char *mq_rewritten = yetty_ybrowser_css_rewrite_media_ranges(eff, eff_len);
    if (mq_rewritten) {
        eff = mq_rewritten;
        eff_len = strlen(mq_rewritten);
    }

    /* libcss 0.9.x rejects Selectors Level 4 `:not()` compound/list args and
     * `:is()` / `:where()` entirely, dropping the whole rule — de-sugar them
     * into Level 3 selector lists. */
    char *not_rewritten = yetty_ybrowser_css_desugar_selectors(eff, eff_len);
    if (not_rewritten) {
        eff = not_rewritten;
        eff_len = strlen(not_rewritten);
    }

    /* libcss 0.9.x can't parse calc() and drops the declaration — collapsing
     * `width:calc(33.33% - 16px)` grid/flex columns into single-column stacks.
     * Approximate by keeping just the percentage. */
    char *calc_rewritten = yetty_ybrowser_css_simplify_calc(eff, eff_len);
    if (calc_rewritten) {
        eff = calc_rewritten;
        eff_len = strlen(calc_rewritten);
    }

    err = css_stylesheet_append_data(sheet, (const uint8_t *)eff, eff_len);
    /* CSS_NEEDDATA is normal — parser is asking for more bytes. We
     * close the stream below with data_done. */
    if (err != CSS_OK && err != CSS_NEEDDATA) {
        ydebug("libcss append_data -> %d", (int)err);
    }
    err = css_stylesheet_data_done(sheet);
    free(calc_rewritten);
    free(not_rewritten);
    free(mq_rewritten);
    free(resolved);
    if (err != CSS_OK && err != CSS_IMPORTS_PENDING) {
        ydebug("libcss data_done -> %d (len=%zu)", (int)err, len);
        css_stylesheet_destroy(sheet);
        return -1;
    }
    /* @import: fetch + parse every imported sheet now, appending each to
	 * the select ctx BEFORE this sheet — imported rules precede the
	 * importing sheet's rules in cascade source order. */
    if (err == CSS_IMPORTS_PENDING) {
        libcss_load_imports(r, sheet, sheet_url, origin);
    }

    /* Keep alive — select_ctx_append_sheet does not take ownership. */
    if (lc->sheet_count == lc->sheet_cap) {
        size_t ncap = lc->sheet_cap ? lc->sheet_cap * 2 : 8;
        css_stylesheet **na = realloc(lc->sheets, ncap * sizeof(*na));
        if (!na) {
            css_stylesheet_destroy(sheet);
            return -1;
        }
        lc->sheets = na;
        lc->sheet_cap = ncap;
    }
    lc->sheets[lc->sheet_count++] = sheet;

    if (css_select_ctx_append_sheet(lc->select_ctx, sheet, origin, NULL) != CSS_OK) {
        /* keep the sheet in the list so destroy frees it */
        return -1;
    }
    return 0;
}

/* Register a parse of `css` (may be empty) as the child for one pending
 * import — a pending import that never gets a registered child would be
 * handed out by css_stylesheet_next_pending_import forever. Returns the
 * child so failures can register a stub. */
static css_stylesheet *libcss_import_child_build(struct yetty_ylexbor *r, const char *css,
                                                 size_t len, css_origin origin,
                                                 const char *child_url)
{
    struct yetty_ybrowser_libcss *lc = r->libcss;
    if (css && len > 0) {
        /* Full path: parses, resolves ITS imports recursively, appends to
		 * the select ctx, and stores in lc->sheets — the child is the
		 * last stored sheet on success. */
        if (yetty_ybrowser_libcss_add_sheet(r, css, len, origin, child_url) == 0) {
            return lc->sheets[lc->sheet_count - 1];
        }
    }
    /* Stub: an empty, never-selected sheet that only satisfies the
	 * pending-import bookkeeping. */
    css_stylesheet_params params = {0};
    params.params_version = CSS_STYLESHEET_PARAMS_VERSION_1;
    params.level = CSS_LEVEL_DEFAULT;
    params.charset = "UTF-8";
    params.url = child_url ? child_url : "ybrowser:import-failed";
    params.resolve = url_resolve;
    css_stylesheet *stub = NULL;
    if (css_stylesheet_create(&params, &stub) != CSS_OK) {
        return NULL;
    }
    (void)css_stylesheet_append_data(stub, (const uint8_t *)" ", 1);
    (void)css_stylesheet_data_done(stub);
    if (lc->sheet_count == lc->sheet_cap) {
        size_t ncap = lc->sheet_cap ? lc->sheet_cap * 2 : 8;
        css_stylesheet **na = realloc(lc->sheets, ncap * sizeof(*na));
        if (!na) {
            css_stylesheet_destroy(stub);
            return NULL;
        }
        lc->sheets = na;
        lc->sheet_cap = ncap;
    }
    lc->sheets[lc->sheet_count++] = stub;
    return stub;
}

static void libcss_load_imports(struct yetty_ylexbor *r, css_stylesheet *parent,
                                const char *parent_url, css_origin origin)
{
    struct yetty_ybrowser_libcss *lc = r->libcss;
    lwc_string *pending = NULL;
    while (css_stylesheet_next_pending_import(parent, &pending) == CSS_OK && pending) {
        /* lwc strings are length-counted, not NUL-guaranteed — copy. */
        size_t raw_len = lwc_string_length(pending);
        char raw_url[1024];
        if (raw_len >= sizeof(raw_url)) {
            raw_len = sizeof(raw_url) - 1;
        }
        memcpy(raw_url, lwc_string_data(pending), raw_len);
        raw_url[raw_len] = '\0';

        /* @import URLs resolve against the importing SHEET, falling back
		 * to the document base for inline <style> imports. */
        char *absolute =
            yetty_ylexbor_resolve_url_against(parent_url ? parent_url : r->base_url, raw_url);

        int cyclic = 0;
        for (int i = 0; absolute && i < lc->import_depth; i++) {
            if (lc->import_chain[i] && strcmp(lc->import_chain[i], absolute) == 0) {
                cyclic = 1;
                break;
            }
        }
        int too_deep =
            lc->import_depth >= (int)(sizeof(lc->import_chain) / sizeof(lc->import_chain[0]));

        css_stylesheet *child = NULL;
        if (absolute && !cyclic && !too_deep) {
            lc->import_chain[lc->import_depth++] = absolute;
            struct yetty_ybrowser_request request = {
                .url = absolute,
                .kind = YETTY_YBROWSER_REQUEST_STYLE,
                .referer = r->base_url,
            };
            struct yetty_ybrowser_response response = {0};
            struct yetty_ycore_void_result fetch_res =
                yetty_ybrowser_fetch(r->loader, &request, &response);
            if (YETTY_IS_ERR(fetch_res)) {
                yetty_ycore_error_destroy(fetch_res.error);
            }
            if (response.body && response.status >= 200 && response.status < 300) {
                child = libcss_import_child_build(r, response.body, response.body_len, origin,
                                                  absolute);
                r->css_sheets_loaded++;
            } else {
                ydebug("@import fetch failed status=%ld %s", response.status, absolute);
                r->css_sheets_failed++;
            }
            yetty_ybrowser_response_dispose(&response);
            lc->import_depth--;
            lc->import_chain[lc->import_depth] = NULL;
        } else if (cyclic || too_deep) {
            ydebug("@import %s: %s", cyclic ? "cycle" : "chain too deep",
                   absolute ? absolute : raw_url);
        }
        if (!child) {
            child = libcss_import_child_build(r, NULL, 0, origin, absolute);
        }
        free(absolute);
        if (!child) {
            break; /* cannot even build a stub — abandon the remainder */
        }
        if (css_stylesheet_register_import(parent, child) != CSS_OK) {
            break;
        }
    }
}

css_computed_style *yetty_ybrowser_libcss_select(struct yetty_ylexbor *r, lxb_dom_element_t *el,
                                                 const char *inline_css, size_t inline_css_len)
{
    if (r == NULL || r->libcss == NULL || el == NULL) {
        return NULL;
    }
    struct yetty_ybrowser_libcss *lc = r->libcss;

    /* Refresh the media context from the LIVE viewport before every
	 * selection. lc->media was seeded once at init; a later set_viewport()
	 * (window resize, or the host laying the embed out at its real width
	 * after the engine was created at the 1024 default) never touched it,
	 * so @media(min-width/max-width) evaluated against the stale init width
	 * — a responsive page's mobile/desktop rules resolved for the wrong
	 * breakpoint regardless of the actual render width. Keep it current. */
    lc->media.width = float_to_fixed((float)r->viewport_w);
    lc->media.height = float_to_fixed((float)r->viewport_h);
    lc->unit_ctx.viewport_width = lc->media.width;
    lc->unit_ctx.viewport_height = lc->media.height;

    /* Build inline-style sheet on demand. We could cache one per
     * element, but inline styles change rarely and select-time is
     * dominated by selector matching anyway. */
    css_stylesheet *inline_sheet = NULL;
    if (inline_css && inline_css_len > 0) {
        css_stylesheet_params p = {0};
        p.params_version = CSS_STYLESHEET_PARAMS_VERSION_1;
        p.level = CSS_LEVEL_DEFAULT;
        p.charset = "UTF-8";
        p.url = "ybrowser:inline-style";
        p.inline_style = true;
        p.resolve = url_resolve;
        if (css_stylesheet_create(&p, &inline_sheet) == CSS_OK) {
            /* var() pre-resolve, ELEMENT-scoped: style="--x: v" tokens
             * on this element or an ancestor beat the global table. */
            char *iresolved =
                yetty_ylexbor_css_vars_resolve_for_element(r, el, inline_css, inline_css_len);
            const char *ieff = iresolved ? iresolved : inline_css;
            size_t ieff_len = iresolved ? strlen(iresolved) : inline_css_len;
            css_stylesheet_append_data(inline_sheet, (const uint8_t *)ieff, ieff_len);
            css_stylesheet_data_done(inline_sheet);
            free(iresolved);
        }
    }

    css_select_results *results = NULL;
    css_error e = css_select_style(lc->select_ctx, &el->node, &lc->unit_ctx, &lc->media,
                                   inline_sheet, &lc->handler, r, &results);
    /* Free the class arrays cb_node_classes handed out during this select
     * — libcss has unref'd the strings by now but never frees the arrays. */
    for (uint32_t i = 0; i < lc->pending_class_array_count; i++) {
        free(lc->pending_class_arrays[i]);
    }
    lc->pending_class_array_count = 0;
    if (inline_sheet) {
        css_stylesheet_destroy(inline_sheet);
    }
    if (e != CSS_OK || results == NULL) {
        return NULL;
    }
    css_computed_style *style = results->styles[CSS_PSEUDO_ELEMENT_NONE];
    /* Steal the unstyled-pseudo style out of the results so destroy
     * doesn't free it from under us. */
    results->styles[CSS_PSEUDO_ELEMENT_NONE] = NULL;
    css_select_results_destroy(results);
    return style;
}

void yetty_ybrowser_libcss_release(css_computed_style *style)
{
    if (style) {
        css_computed_style_destroy(style);
    }
}

/* ===========================================================================
 * Property bridges. Conventions:
 *   - Length getters that return 0 leave *out_px untouched.
 *   - Color getters honor css_color (0xAARRGGBB).
 *   - "Auto" / "inherit" results are treated as "no value" → return 0.
 * ===========================================================================*/

static float resolve_length_to_px(struct yetty_ylexbor *r, const css_computed_style *style,
                                  css_fixed length, css_unit unit, float font_size, float pct_basis)
{
    (void)style;
    if (unit == CSS_UNIT_PX) {
        return fixed_to_float(length);
    }
    if (unit == CSS_UNIT_EM) {
        return fixed_to_float(length) * font_size;
    }
    if (unit == CSS_UNIT_REM) {
        return fixed_to_float(length) * (r ? r->default_font_size : 16.0f);
    }
    if (unit == CSS_UNIT_PCT) {
        return fixed_to_float(length) * pct_basis / 100.0f;
    }
    if (unit == CSS_UNIT_PT) {
        return fixed_to_float(length) * (96.0f / 72.0f);
    }
    if (unit == CSS_UNIT_PC) {
        return fixed_to_float(length) * 16.0f;
    }
    if (unit == CSS_UNIT_IN) {
        return fixed_to_float(length) * 96.0f;
    }
    if (unit == CSS_UNIT_CM) {
        return fixed_to_float(length) * (96.0f / 2.54f);
    }
    if (unit == CSS_UNIT_MM) {
        return fixed_to_float(length) * (96.0f / 25.4f);
    }
    if (unit == CSS_UNIT_EX) {
        return fixed_to_float(length) * font_size * 0.5f;
    }
    if (unit == CSS_UNIT_CH) {
        /* Advance of the '0' glyph — 0.556em in the Helvetica/Arial metrics
         * the proportional text measure uses (Chrome's default sans is the
         * metric-compatible Liberation Sans). */
        return fixed_to_float(length) * font_size * 0.556f;
    }
    if (unit == CSS_UNIT_Q) {
        /* 1Q = 1/40 cm. */
        return fixed_to_float(length) * (96.0f / 2.54f) / 40.0f;
    }
    if (unit == CSS_UNIT_LH) {
        /* Relative to the line box; we don't resolve line-height here, so
         * approximate with the default normal line box (1.25 * font). */
        return fixed_to_float(length) * font_size * 1.25f;
    }
    /* Viewport-percentage units, resolved against the live viewport.
     * Horizontal writing mode is assumed, so the inline axis (vi) maps to
     * the viewport width and the block axis (vb) to the height. */
    float viewport_w = r ? (float)r->viewport_w : 0.0f;
    float viewport_h = r ? (float)r->viewport_h : 0.0f;
    if (unit == CSS_UNIT_VW || unit == CSS_UNIT_VI) {
        return fixed_to_float(length) * viewport_w / 100.0f;
    }
    if (unit == CSS_UNIT_VH || unit == CSS_UNIT_VB) {
        return fixed_to_float(length) * viewport_h / 100.0f;
    }
    if (unit == CSS_UNIT_VMIN) {
        float side = viewport_w < viewport_h ? viewport_w : viewport_h;
        return fixed_to_float(length) * side / 100.0f;
    }
    if (unit == CSS_UNIT_VMAX) {
        float side = viewport_w > viewport_h ? viewport_w : viewport_h;
        return fixed_to_float(length) * side / 100.0f;
    }
    /* Unknown / angular / time units — return the raw magnitude. */
    return fixed_to_float(length);
}

static void unpack_color(css_color cc, struct yetty_ylexbor_color *out)
{
    out->a = CSS_COLOR_A(cc);
    out->r = CSS_COLOR_R(cc);
    out->g = CSS_COLOR_G(cc);
    out->b = CSS_COLOR_B(cc);
}

int yetty_ybrowser_libcss_color(const css_computed_style *style, struct yetty_ylexbor_color *out)
{
    if (style == NULL || out == NULL) {
        return 0;
    }
    css_color cc = 0;
    if (css_computed_color(style, &cc) == CSS_COLOR_COLOR) {
        unpack_color(cc, out);
        return 1;
    }
    return 0;
}

int yetty_ybrowser_libcss_bg_color(const css_computed_style *style, struct yetty_ylexbor_color *out)
{
    if (style == NULL || out == NULL) {
        return 0;
    }
    css_color cc = 0;
    if (css_computed_background_color(style, &cc) != CSS_BACKGROUND_COLOR_COLOR) {
        return 0;
    }
    unpack_color(cc, out);
    /* Caller wants to know whether a non-transparent paint should be
     * emitted — alpha=0 carries no pixels. */
    return out->a != 0;
}

/* Variant for width-class properties: when the CSS value is a percent,
 * encode it as a negative ratio (`-percent/100`) instead of resolving
 * against pct_basis. Layout resolves the negative case against the
 * parent's actual content width. Pixel / em / rem / etc. round-trip
 * as positive pixels. */
static int len_or_pct_property(uint8_t kind, css_fixed length, css_unit unit, int set_value,
                               struct yetty_ylexbor *r, const css_computed_style *style,
                               float font_size, float pct_basis, float *out_px)
{
    if (kind != set_value) {
        return 0;
    }
    if (unit == CSS_UNIT_PCT) {
        *out_px = -fixed_to_float(length) / 100.0f;
        return 1;
    }
    *out_px = resolve_length_to_px(r, style, length, unit, font_size, pct_basis);
    return 1;
}

/* NB: each of these bridge functions used to be written as
 *
 *   return len_property(css_computed_width(style, &l, &u), l, u, ...);
 *
 * which is undefined behaviour — C's argument-evaluation order is
 * unsequenced, so the compiler is free to load `l`/`u` BEFORE
 * css_computed_width writes them. We hit this in practice (gcc 12
 * with -O2 inlining the bridge): width values came back as zero for
 * .float boxes, but materialised correctly the moment any extra
 * function call sat between the write and the read.
 *
 * The fix is a real sequence point: bind the call's return value to
 * a local before passing l and u as arguments. Do NOT collapse these
 * back into one expression. */
int yetty_ybrowser_libcss_width(struct yetty_ylexbor *r, const css_computed_style *style,
                                float font_size, float pct_basis, float *out_px)
{
    if (!style) {
        return 0;
    }
    css_fixed l = 0;
    css_unit u = CSS_UNIT_PX;
    uint8_t k = css_computed_width(style, &l, &u);
    return len_or_pct_property(k, l, u, CSS_WIDTH_SET, r, style, font_size, pct_basis, out_px);
}

int yetty_ybrowser_libcss_height(struct yetty_ylexbor *r, const css_computed_style *style,
                                 float font_size, float pct_basis, float *out_px)
{
    if (!style) {
        return 0;
    }
    css_fixed l = 0;
    css_unit u = CSS_UNIT_PX;
    uint8_t k = css_computed_height(style, &l, &u);
    return len_or_pct_property(k, l, u, CSS_HEIGHT_SET, r, style, font_size, pct_basis, out_px);
}

int yetty_ybrowser_libcss_max_width(struct yetty_ylexbor *r, const css_computed_style *style,
                                    float font_size, float pct_basis, float *out_px)
{
    if (!style) {
        return 0;
    }
    css_fixed l = 0;
    css_unit u = CSS_UNIT_PX;
    uint8_t k = css_computed_max_width(style, &l, &u);
    return len_or_pct_property(k, l, u, CSS_MAX_WIDTH_SET, r, style, font_size, pct_basis, out_px);
}

int yetty_ybrowser_libcss_min_width(struct yetty_ylexbor *r, const css_computed_style *style,
                                    float font_size, float pct_basis, float *out_px)
{
    if (!style) {
        return 0;
    }
    css_fixed l = 0;
    css_unit u = CSS_UNIT_PX;
    uint8_t k = css_computed_min_width(style, &l, &u);
    return len_or_pct_property(k, l, u, CSS_MIN_WIDTH_SET, r, style, font_size, pct_basis, out_px);
}

int yetty_ybrowser_libcss_max_height(struct yetty_ylexbor *r, const css_computed_style *style,
                                     float font_size, float pct_basis, float *out_px)
{
    if (!style) {
        return 0;
    }
    css_fixed l = 0;
    css_unit u = CSS_UNIT_PX;
    uint8_t k = css_computed_max_height(style, &l, &u);
    return len_or_pct_property(k, l, u, CSS_MAX_HEIGHT_SET, r, style, font_size, pct_basis, out_px);
}

/* Margin / padding percentages resolve against the containing block's
 * content WIDTH, which the box pass doesn't know yet. So when the value
 * is a percent we return the ratio (e.g. 0.10 for 10%) in *out_px and set
 * *out_pct = true; the layout pass multiplies by the parent content width
 * and clears the flag. Non-percent values round-trip as resolved px with
 * *out_pct = false. `pct_basis` is therefore unused for percents now and
 * only feeds em/viewport resolution for the non-percent path. */
int yetty_ybrowser_libcss_margin(struct yetty_ylexbor *r, const css_computed_style *style, int side,
                                 float font_size, float pct_basis, float *out_px, bool *out_auto,
                                 bool *out_pct)
{
    if (!style) {
        return 0;
    }
    if (out_auto) {
        *out_auto = false;
    }
    if (out_pct) {
        *out_pct = false;
    }
    css_fixed l = 0;
    css_unit u = CSS_UNIT_PX;
    uint8_t kind = 0;
    switch (side) {
    case 0:
        kind = css_computed_margin_top(style, &l, &u);
        break;
    case 1:
        kind = css_computed_margin_right(style, &l, &u);
        break;
    case 2:
        kind = css_computed_margin_bottom(style, &l, &u);
        break;
    case 3:
        kind = css_computed_margin_left(style, &l, &u);
        break;
    default:
        return 0;
    }
    if (kind == CSS_MARGIN_AUTO) {
        if (out_auto) {
            *out_auto = true;
        }
        return 1;
    }
    if (kind == CSS_MARGIN_SET) {
        if (u == CSS_UNIT_PCT) {
            *out_px = fixed_to_float(l) / 100.0f;
            if (out_pct) {
                *out_pct = true;
            }
        } else {
            *out_px = resolve_length_to_px(r, style, l, u, font_size, pct_basis);
        }
        return 1;
    }
    return 0;
}

int yetty_ybrowser_libcss_padding(struct yetty_ylexbor *r, const css_computed_style *style,
                                  int side, float font_size, float pct_basis, float *out_px,
                                  bool *out_pct)
{
    if (!style) {
        return 0;
    }
    if (out_pct) {
        *out_pct = false;
    }
    css_fixed l = 0;
    css_unit u = CSS_UNIT_PX;
    uint8_t kind = 0;
    switch (side) {
    case 0:
        kind = css_computed_padding_top(style, &l, &u);
        break;
    case 1:
        kind = css_computed_padding_right(style, &l, &u);
        break;
    case 2:
        kind = css_computed_padding_bottom(style, &l, &u);
        break;
    case 3:
        kind = css_computed_padding_left(style, &l, &u);
        break;
    default:
        return 0;
    }
    if (kind == CSS_PADDING_SET) {
        if (u == CSS_UNIT_PCT) {
            *out_px = fixed_to_float(l) / 100.0f;
            if (out_pct) {
                *out_pct = true;
            }
        } else {
            *out_px = resolve_length_to_px(r, style, l, u, font_size, pct_basis);
        }
        return 1;
    }
    return 0;
}

/* box-sizing: returns 1 for border-box, 0 for content-box (the CSS
 * initial) / inherit. */
int yetty_ybrowser_libcss_box_sizing(const css_computed_style *style)
{
    if (!style) {
        return 0;
    }
    return css_computed_box_sizing(style) == CSS_BOX_SIZING_BORDER_BOX ? 1 : 0;
}

/* line-height: returns 1 when the cascade has a concrete value, 0 for
 * `normal` / inherit (caller keeps its default line box).
 *
 * A unitless NUMBER (e.g. line-height: 1.5) inherits as the *factor*, to
 * be multiplied by each element's own font-size — so it is returned in
 * *out_factor with *out_px = 0. A length / percentage resolves to a used
 * px value that inherits as-is — returned in *out_px with *out_factor = 0.
 * Keeping the two cases distinct is what lets an inherited `body { line-
 * height: 1.33 }` scale correctly on a larger-font descendant like <h1>. */
int yetty_ybrowser_libcss_line_height(struct yetty_ylexbor *r, const css_computed_style *style,
                                      float font_size, float *out_px, float *out_factor)
{
    *out_px = 0.0f;
    *out_factor = 0.0f;
    if (!style) {
        return 0;
    }
    css_fixed l = 0;
    css_unit u = CSS_UNIT_PX;
    uint8_t kind = css_computed_line_height(style, &l, &u);
    if (kind == CSS_LINE_HEIGHT_NUMBER) {
        *out_factor = fixed_to_float(l);
        return 1;
    }
    if (kind == CSS_LINE_HEIGHT_DIMENSION) {
        *out_px = resolve_length_to_px(r, style, l, u, font_size, font_size);
        return 1;
    }
    return 0;
}

int yetty_ybrowser_libcss_border_width(struct yetty_ylexbor *r, const css_computed_style *style,
                                       int side, float font_size, float *out_px)
{
    if (!style) {
        return 0;
    }
    /* border-style is "none" by default — when it is none/hidden, the
     * border is suppressed regardless of border-width. The cascade
     * still reports MEDIUM (the keyword initial) for the width, which
     * would otherwise materialise as a 3px ghost border on every
     * element. Gate the width by the style. */
    uint8_t bstyle = 0;
    switch (side) {
    case 0:
        bstyle = css_computed_border_top_style(style);
        break;
    case 1:
        bstyle = css_computed_border_right_style(style);
        break;
    case 2:
        bstyle = css_computed_border_bottom_style(style);
        break;
    case 3:
        bstyle = css_computed_border_left_style(style);
        break;
    }
    if (bstyle == CSS_BORDER_STYLE_NONE || bstyle == CSS_BORDER_STYLE_HIDDEN ||
        bstyle == CSS_BORDER_STYLE_INHERIT) {
        return 0;
    }
    css_fixed l = 0;
    css_unit u = CSS_UNIT_PX;
    uint8_t kind = 0;
    switch (side) {
    case 0:
        kind = css_computed_border_top_width(style, &l, &u);
        break;
    case 1:
        kind = css_computed_border_right_width(style, &l, &u);
        break;
    case 2:
        kind = css_computed_border_bottom_width(style, &l, &u);
        break;
    case 3:
        kind = css_computed_border_left_width(style, &l, &u);
        break;
    default:
        return 0;
    }
    if (kind == CSS_BORDER_WIDTH_WIDTH) {
        *out_px = resolve_length_to_px(r, style, l, u, font_size, 0);
        return 1;
    }
    if (kind == CSS_BORDER_WIDTH_THIN) {
        *out_px = 1.0f;
        return 1;
    }
    if (kind == CSS_BORDER_WIDTH_MEDIUM) {
        *out_px = 3.0f;
        return 1;
    }
    if (kind == CSS_BORDER_WIDTH_THICK) {
        *out_px = 5.0f;
        return 1;
    }
    return 0;
}

int yetty_ybrowser_libcss_border_color(const css_computed_style *style, int side,
                                       const struct yetty_ylexbor_color *current_color,
                                       struct yetty_ylexbor_color *out)
{
    if (!style) {
        return 0;
    }
    css_color cc = 0;
    uint8_t kind = 0;
    switch (side) {
    case 0:
        kind = css_computed_border_top_color(style, &cc);
        break;
    case 1:
        kind = css_computed_border_right_color(style, &cc);
        break;
    case 2:
        kind = css_computed_border_bottom_color(style, &cc);
        break;
    case 3:
        kind = css_computed_border_left_color(style, &cc);
        break;
    default:
        return 0;
    }
    if (kind == CSS_BORDER_COLOR_COLOR) {
        unpack_color(cc, out);
        return 1;
    }
    if (kind == CSS_BORDER_COLOR_CURRENT_COLOR && current_color) {
        *out = *current_color;
        return 1;
    }
    return 0;
}

int yetty_ybrowser_libcss_font_size(struct yetty_ylexbor *r, const css_computed_style *style,
                                    float parent_font_size, float *out_px)
{
    if (!style) {
        return 0;
    }
    css_fixed l = 0;
    css_unit u = CSS_UNIT_PX;
    css_computed_font_size(style, &l, &u);
    /* libcss usually returns CSS_UNIT_PX for computed font-size — the
     * cascade folds em/rem/etc. up. Still resolve defensively. */
    *out_px = resolve_length_to_px(r, style, l, u, parent_font_size, parent_font_size);
    if (*out_px <= 0) {
        return 0;
    }
    return 1;
}

int yetty_ybrowser_libcss_font_weight(const css_computed_style *style, int *out)
{
    if (!style) {
        return 0;
    }
    uint8_t v = css_computed_font_weight(style);
    switch (v) {
    case CSS_FONT_WEIGHT_NORMAL:
        *out = 400;
        return 1;
    case CSS_FONT_WEIGHT_BOLD:
        *out = 700;
        return 1;
    case CSS_FONT_WEIGHT_BOLDER:
        *out = 700;
        return 1;
    case CSS_FONT_WEIGHT_LIGHTER:
        *out = 300;
        return 1;
    case CSS_FONT_WEIGHT_100:
        *out = 100;
        return 1;
    case CSS_FONT_WEIGHT_200:
        *out = 200;
        return 1;
    case CSS_FONT_WEIGHT_300:
        *out = 300;
        return 1;
    case CSS_FONT_WEIGHT_400:
        *out = 400;
        return 1;
    case CSS_FONT_WEIGHT_500:
        *out = 500;
        return 1;
    case CSS_FONT_WEIGHT_600:
        *out = 600;
        return 1;
    case CSS_FONT_WEIGHT_700:
        *out = 700;
        return 1;
    case CSS_FONT_WEIGHT_800:
        *out = 800;
        return 1;
    case CSS_FONT_WEIGHT_900:
        *out = 900;
        return 1;
    default:
        return 0;
    }
}

int yetty_ybrowser_libcss_font_italic(const css_computed_style *style, bool *out)
{
    if (!style) {
        return 0;
    }
    uint8_t v = css_computed_font_style(style);
    if (v == CSS_FONT_STYLE_ITALIC || v == CSS_FONT_STYLE_OBLIQUE) {
        *out = true;
        return 1;
    }
    if (v == CSS_FONT_STYLE_NORMAL) {
        *out = false;
        return 1;
    }
    return 0;
}

int yetty_ybrowser_libcss_display(const css_computed_style *style, bool root)
{
    if (!style) {
        return CSS_DISPLAY_INLINE;
    }
    return css_computed_display(style, root);
}

int yetty_ybrowser_libcss_text_align(const css_computed_style *style)
{
    if (!style) {
        return CSS_TEXT_ALIGN_LEFT;
    }
    return css_computed_text_align(style);
}

int yetty_ybrowser_libcss_flex_direction(const css_computed_style *style)
{
    if (!style) {
        return CSS_FLEX_DIRECTION_ROW;
    }
    return css_computed_flex_direction(style);
}

int yetty_ybrowser_libcss_font_advance_class(const css_computed_style *style)
{
    if (!style) {
        return 0;
    }
    lwc_string **names = NULL;
    uint8_t generic = css_computed_font_family(style, &names);
    if (names != NULL) {
        for (int i = 0; names[i] != NULL; i++) {
            const char *d = lwc_string_data(names[i]);
            size_t l = lwc_string_length(names[i]);
            if (l == 4 && strncasecmp(d, "ahem", 4) == 0) {
                return 1;
            }
            /* Named monospace families: "Courier New", "Consolas", "Menlo",
             * "Monaco", and anything carrying a "mono" token ("SF Mono",
             * "Roboto Mono", "monospace" spelled as a name, …). */
            for (size_t k = 0; k + 4 <= l; k++) {
                if (strncasecmp(d + k, "mono", 4) == 0) {
                    return 2;
                }
            }
            if ((l >= 7 && strncasecmp(d, "courier", 7) == 0) ||
                (l == 8 && strncasecmp(d, "consolas", 8) == 0) ||
                (l == 5 && strncasecmp(d, "menlo", 5) == 0) ||
                (l == 6 && strncasecmp(d, "monaco", 6) == 0)) {
                return 2;
            }
        }
    }
    if (generic == CSS_FONT_FAMILY_MONOSPACE) {
        return 2;
    }
    return 0;
}

int yetty_ybrowser_libcss_flex_wrap(const css_computed_style *style)
{
    if (!style) {
        return CSS_FLEX_WRAP_NOWRAP;
    }
    return css_computed_flex_wrap(style);
}

int yetty_ybrowser_libcss_align_content(const css_computed_style *style)
{
    if (!style) {
        return 0;
    }
    return css_computed_align_content(style);
}

int yetty_ybrowser_libcss_flex_grow(const css_computed_style *style, float *out)
{
    if (!style || !out) {
        return 0;
    }
    css_fixed n = 0;
    uint8_t kind = css_computed_flex_grow(style, &n);
    if (kind == CSS_FLEX_GROW_SET) {
        *out = fixed_to_float(n);
        return 1;
    }
    return 0;
}

int yetty_ybrowser_libcss_flex_shrink(const css_computed_style *style, float *out)
{
    if (!style || !out) {
        return 0;
    }
    css_fixed n = 0;
    uint8_t kind = css_computed_flex_shrink(style, &n);
    if (kind == CSS_FLEX_SHRINK_SET) {
        *out = fixed_to_float(n);
        return 1;
    }
    return 0;
}

int yetty_ybrowser_libcss_flex_basis(struct yetty_ylexbor *r, const css_computed_style *style,
                                     float font_size, float pct_basis, float *out_px,
                                     bool *out_auto)
{
    if (!style) {
        return 0;
    }
    if (out_auto) {
        *out_auto = false;
    }
    css_fixed l = 0;
    css_unit u = CSS_UNIT_PX;
    uint8_t kind = css_computed_flex_basis(style, &l, &u);
    if (kind == CSS_FLEX_BASIS_AUTO || kind == CSS_FLEX_BASIS_CONTENT) {
        if (out_auto) {
            *out_auto = true;
        }
        return 1;
    }
    if (kind == CSS_FLEX_BASIS_SET) {
        if (u == CSS_UNIT_PCT) {
            /* Encode as negative ratio — flex layout resolves against
			 * the container's main-axis budget. Same convention as
			 * width / height. */
            *out_px = -fixed_to_float(l) / 100.0f;
        } else {
            *out_px = resolve_length_to_px(r, style, l, u, font_size, pct_basis);
        }
        return 1;
    }
    return 0;
}

int yetty_ybrowser_libcss_justify_content(const css_computed_style *style)
{
    if (!style) {
        return CSS_JUSTIFY_CONTENT_FLEX_START;
    }
    return css_computed_justify_content(style);
}

int yetty_ybrowser_libcss_align_items(const css_computed_style *style)
{
    if (!style) {
        return CSS_ALIGN_ITEMS_STRETCH;
    }
    return css_computed_align_items(style);
}

int yetty_ybrowser_libcss_float(const css_computed_style *style)
{
    if (!style) {
        return CSS_FLOAT_NONE;
    }
    return css_computed_float(style);
}

int yetty_ybrowser_libcss_clear(const css_computed_style *style)
{
    if (!style) {
        return CSS_CLEAR_NONE;
    }
    return css_computed_clear(style);
}

int yetty_ybrowser_libcss_position(const css_computed_style *style)
{
    if (!style) {
        return CSS_POSITION_STATIC;
    }
    return css_computed_position(style);
}

int yetty_ybrowser_libcss_visibility(const css_computed_style *style)
{
    if (!style) {
        return CSS_VISIBILITY_VISIBLE;
    }
    return (int)css_computed_visibility(style);
}

/* z-index for a positioned box. Returns 1 and writes the integer value when
 * an explicit z-index is set; returns 0 for `auto` (the default). Only
 * meaningful on positioned elements — the paint pass gates the read on that. */
int yetty_ybrowser_libcss_z_index(const css_computed_style *style, int32_t *out_z)
{
    if (!style) {
        return 0;
    }
    css_fixed z = 0;
    uint8_t kind = css_computed_z_index(style, &z);
    if (kind != CSS_Z_INDEX_SET) {
        return 0; /* auto */
    }
    if (out_z) {
        /* z-index is a whole number stored in fixed-point; the float round-trip
		 * is exact for the small integers pages use. */
        *out_z = (int32_t)fixed_to_float(z);
    }
    return 1;
}

float yetty_ybrowser_libcss_opacity(const css_computed_style *style)
{
    if (!style) {
        return 1.0f;
    }
    css_fixed opacity = 0;
    if (css_computed_opacity(style, &opacity) != CSS_OPACITY_SET) {
        return 1.0f;
    }
    float value = fixed_to_float(opacity);
    if (value < 0.0f) {
        return 0.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

bool yetty_ybrowser_libcss_clips_overflow(const css_computed_style *style)
{
    if (!style) {
        return false;
    }
    /* Any non-`visible` overflow on either axis establishes a clip: hidden,
	 * clip, scroll and auto all confine descendants to the padding box. We do
	 * not scroll, so scroll/auto are treated as a static clip too. */
    uint8_t overflow_x = css_computed_overflow_x(style);
    uint8_t overflow_y = css_computed_overflow_y(style);
    return (overflow_x != CSS_OVERFLOW_VISIBLE && overflow_x != CSS_OVERFLOW_INHERIT) ||
           (overflow_y != CSS_OVERFLOW_VISIBLE && overflow_y != CSS_OVERFLOW_INHERIT);
}

int yetty_ybrowser_libcss_inset(struct yetty_ylexbor *r, const css_computed_style *style, int side,
                                float font_size, float pct_basis, float *out_value)
{
    if (!style || !out_value) {
        return 0;
    }
    css_fixed length = 0;
    css_unit unit = CSS_UNIT_PX;
    uint8_t kind = 0;
    int set_value = 0;
    /* Bind the accessor's return before reading length/unit — the same
     * unsequenced-evaluation hazard the width/height bridges document. */
    switch (side) {
    case 0:
        kind = css_computed_top(style, &length, &unit);
        set_value = CSS_TOP_SET;
        break;
    case 1:
        kind = css_computed_right(style, &length, &unit);
        set_value = CSS_RIGHT_SET;
        break;
    case 2:
        kind = css_computed_bottom(style, &length, &unit);
        set_value = CSS_BOTTOM_SET;
        break;
    case 3:
        kind = css_computed_left(style, &length, &unit);
        set_value = CSS_LEFT_SET;
        break;
    default:
        return 0;
    }
    if (kind != set_value) {
        return 0; /* auto / inherit-default → not specified */
    }
    if (unit == CSS_UNIT_PCT) {
        *out_value = fixed_to_float(length) / 100.0f;
        return 2;
    }
    *out_value = resolve_length_to_px(r, style, length, unit, font_size, pct_basis);
    return 1;
}

int yetty_ybrowser_libcss_white_space(const css_computed_style *style)
{
    if (!style) {
        return CSS_WHITE_SPACE_NORMAL;
    }
    return css_computed_white_space(style);
}

int yetty_ybrowser_libcss_text_decoration(const css_computed_style *style)
{
    if (!style) {
        return 0;
    }
    return css_computed_text_decoration(style);
}

int yetty_ybrowser_libcss_order(const css_computed_style *style, int32_t *out)
{
    if (!style) {
        return 0;
    }
    int32_t value = 0;
    if (css_computed_order(style, &value) != CSS_ORDER_SET) {
        return 0;
    }
    *out = value;
    return 1;
}

int yetty_ybrowser_libcss_align_self(const css_computed_style *style)
{
    if (!style) {
        return CSS_ALIGN_SELF_AUTO;
    }
    return (int)css_computed_align_self(style);
}

int yetty_ybrowser_libcss_list_style_type(const css_computed_style *style)
{
    if (!style) {
        return CSS_LIST_STYLE_TYPE_DISC;
    }
    return css_computed_list_style_type(style);
}
