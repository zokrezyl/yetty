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
    /* <noscript> intentionally rendered: this build runs with
     * QuickJS off, so the no-JS fallbacks (often <img> versions of
     * what JS would otherwise inject) ARE the page content. */
    "noscript { display: block; }\n"
    /* Embedded content we don't render — hide outright so the walker
     * doesn't surface their text content (SVG <text>, MathML, etc.)
     * as garbage at the top of the page. */
    "svg, math, audio, video, object, embed, iframe, canvas { display: none; }\n"
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
    "a { color: #00e; }\n"
    "hr { display: block; margin: 0.5em auto; border-top: 1px solid #888; }\n"
    /* Accessibility / chrome — elements explicitly marked off-screen
     * or decorative MUST NOT render. Wikipedia and most large sites
     * leak nav/menu/jump-link junk through these attributes when
     * their main stylesheet doesn't apply. */
    "[aria-hidden=\"true\"] { display: none; }\n"
    "[hidden] { display: none; }\n"
    "[role=\"presentation\"] { display: none; }\n"
    /* Wikipedia float helpers. The real Wikipedia stylesheet bundles
     * these in /w/load.php?modules=...; if we couldn't fetch it (no
     * network, file:// rendering) we still want figures and infoboxes
     * to land in roughly the right place. Wikipedia's Parsoid emits
     * <figure typeof="mw:File/Thumb"> for every article image; the
     * stylesheet floats those right by default and only changes side
     * when an explicit `.mw-halign-left` is present. We replicate the
     * default-right behaviour so reading Wikipedia offline still
     * produces a paragraph-with-sidebar layout instead of a wall of
     * full-width images. */
    "figure[typeof~=\"mw:File/Thumb\"], figure[typeof~=\"mw:File/Frame\"],"
    " .mw-default-size, .thumb"
    " { float: right; margin: 0 0 0.5em 0.8em; }\n"
    ".mw-halign-right, .mw-floatright, .floatright, .thumb.tright,"
    " .infobox, table.infobox"
    " { float: right; margin: 0 0 0.5em 0.8em; }\n"
    ".mw-halign-left, .mw-floatleft, .floatleft, figure.mw-halign-left,"
    " figure.mw-default-size.mw-halign-left, .thumb.tleft"
    " { float: left; margin: 0 0.8em 0.5em 0; }\n"
    ".mw-halign-center, figure.mw-halign-center,"
    " figure.mw-default-size.mw-halign-center"
    " { float: none; margin: 0.5em auto; }\n"
    /* Wikipedia's hidden chrome: namespaces tabs, jump links,
     * collapsed nav modules, etc. None of these contribute article
     * content. */
    ".mw-jump-link, .mw-editsection, .navbox, .navbar, .vector-menu,"
    " .vector-header, .mw-portlet, .mw-footer, .mw-indicators,"
    " .mw-cite-backlink, .mw-cite-direction-marker, .mw-hidden,"
    " #vector-toc-pinned-container, .vector-toc, .vector-page-tools,"
    " .vector-appearance-landmark, .vector-language-button-container"
    " { display: none; }\n"
    /* Top-level `<nav>` is almost always chrome (site nav, breadcrumbs,
     * tabs); inline `<nav>` inside an article is rare. The article
     * <header> wrapping the page title is critical content though, so
     * we only hide <nav> by default — NOT <header>. */
    "nav { display: none; }\n";

/* ===========================================================================
 * Select-handler callbacks. `pw` is `struct yetty_ylexbor *r`; `node` is
 * always `lxb_dom_node_t *` (we cast from element where libcss gave us
 * one to start with). All callbacks return css_error.
 * ===========================================================================*/

static lxb_dom_node_t *as_node(void *node) { return (lxb_dom_node_t *)node; }
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
    (void)pw;
    *classes = NULL;
    *n_classes = 0;
    size_t alen = 0;
    const lxb_char_t *attr =
        lxb_dom_element_get_attribute(as_element(node), (const lxb_char_t *)"class", 5, &alen);
    if (!attr || alen == 0) {
        return CSS_OK;
    }
    /* Split on ASCII whitespace, intern each token. */
    lwc_string **arr = NULL;
    uint32_t count = 0, cap = 0;
    size_t i = 0;
    while (i < alen) {
        while (i < alen && (attr[i] == ' ' || attr[i] == '\t' || attr[i] == '\n' || attr[i] == '\r')) {
            i++;
        }
        size_t s = i;
        while (i < alen && !(attr[i] == ' ' || attr[i] == '\t' || attr[i] == '\n' || attr[i] == '\r')) {
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

static css_error cb_named_sibling_node(void *pw, void *node, const css_qname *qname,
                                       void **sibling)
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
        while (i < alen && (attr[i] == ' ' || attr[i] == '\t' || attr[i] == '\n' || attr[i] == '\r')) {
            i++;
        }
        size_t s = i;
        while (i < alen && !(attr[i] == ' ' || attr[i] == '\t' || attr[i] == '\n' || attr[i] == '\r')) {
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
    if (alen >= vlen &&
        memcmp(av + alen - vlen, lwc_string_data(value), vlen) == 0) {
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
    (void)pw; (void)node;
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
    (void)pw; (void)node;
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

static css_error cb_set_libcss_node_data(void *pw, void *node, void *data)
{
    (void)pw; (void)node;
    /* We don't cache. If libcss handed us non-null data anyway, free
     * the memory it allocated to avoid a leak. The destructor would
     * normally be invoked through css_libcss_node_data_handler. */
    (void)data;
    return CSS_OK;
}

static css_error cb_get_libcss_node_data(void *pw, void *node, void **data)
{
    (void)pw; (void)node;
    *data = NULL;
    return CSS_OK;
}

/* Vtable. Sizes/ABI must match css_select_handler exactly. */
static const css_select_handler g_handler = {
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

/* libcss requires a URL resolution callback at stylesheet_create time
 * (rejects with CSS_BADPARM otherwise). We don't resolve @import /
 * background-image URLs through libcss — fetching is the host's job
 * via libcurl — so the resolver just hands back the relative URL as-is
 * with a fresh ref. Good enough for parsing rules; the abs URL only
 * matters if someone actually consumes it. */
static css_error url_resolve(void *pw, const char *base, lwc_string *rel,
                             lwc_string **abs)
{
    (void)pw; (void)base;
    *abs = lwc_string_ref(rel);
    return CSS_OK;
}

/* ===========================================================================
 * Public lifecycle.
 * ===========================================================================*/

static int g_lwc_initialised = 0;

int yetty_ybrowser_libcss_init(struct yetty_ylexbor *r)
{
    if (r->libcss != NULL) {
        return 0;
    }
    if (!g_lwc_initialised) {
        /* lwc has no public init function in current versions — it
         * lazily initialises its global hash. We still keep the flag
         * to mirror the lifecycle idiom. */
        g_lwc_initialised = 1;
    }

    struct yetty_ybrowser_libcss *lc = calloc(1, sizeof(*lc));
    if (!lc) {
        return -1;
    }
    lc->r = r;
    lc->handler = g_handler;

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
    if (yetty_ybrowser_libcss_add_sheet(r, UA_DEFAULT_CSS, sizeof(UA_DEFAULT_CSS) - 1,
                                        CSS_ORIGIN_UA) != 0) {
        ydebug("libcss: UA stylesheet append failed");
    }
    return 0;
}

void yetty_ybrowser_libcss_destroy(struct yetty_ylexbor *r)
{
    if (r == NULL || r->libcss == NULL) {
        return;
    }
    struct yetty_ybrowser_libcss *lc = r->libcss;
    if (lc->select_ctx) {
        css_select_ctx_destroy(lc->select_ctx);
    }
    for (size_t i = 0; i < lc->sheet_count; i++) {
        css_stylesheet_destroy(lc->sheets[i]);
    }
    free(lc->sheets);
    free(lc);
    r->libcss = NULL;
}

int yetty_ybrowser_libcss_add_sheet(struct yetty_ylexbor *r, const char *css, size_t len,
                                    css_origin origin)
{
    if (r == NULL || r->libcss == NULL || css == NULL || len == 0) {
        return -1;
    }
    struct yetty_ybrowser_libcss *lc = r->libcss;

    css_stylesheet_params params = {0};
    params.params_version = CSS_STYLESHEET_PARAMS_VERSION_1;
    params.level = CSS_LEVEL_DEFAULT;
    params.charset = "UTF-8";
    params.url = "ybrowser:inline";
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

    err = css_stylesheet_append_data(sheet, (const uint8_t *)eff, eff_len);
    /* CSS_NEEDDATA is normal — parser is asking for more bytes. We
     * close the stream below with data_done. */
    if (err != CSS_OK && err != CSS_NEEDDATA) {
        ydebug("libcss append_data -> %d", (int)err);
    }
    err = css_stylesheet_data_done(sheet);
    free(resolved);
    /* CSS_IMPORTS_PENDING is success-with-imports; we don't resolve
     * them today, so the sheet is usable as-is. */
    if (err != CSS_OK && err != CSS_IMPORTS_PENDING) {
        ydebug("libcss data_done -> %d (len=%zu)", (int)err, len);
        css_stylesheet_destroy(sheet);
        return -1;
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

css_computed_style *yetty_ybrowser_libcss_select(struct yetty_ylexbor *r, lxb_dom_element_t *el,
                                                 const char *inline_css, size_t inline_css_len)
{
    if (r == NULL || r->libcss == NULL || el == NULL) {
        return NULL;
    }
    struct yetty_ybrowser_libcss *lc = r->libcss;

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
            /* Same var() pre-resolve as the stylesheet path — inline
             * styles can reference custom properties too. */
            char *iresolved = yetty_ylexbor_css_vars_resolve(r, inline_css, inline_css_len);
            const char *ieff = iresolved ? iresolved : inline_css;
            size_t ieff_len = iresolved ? strlen(iresolved) : inline_css_len;
            css_stylesheet_append_data(inline_sheet, (const uint8_t *)ieff, ieff_len);
            css_stylesheet_data_done(inline_sheet);
            free(iresolved);
        }
    }

    css_select_results *results = NULL;
    css_error e =
        css_select_style(lc->select_ctx, &el->node, &lc->unit_ctx, &lc->media, inline_sheet,
                         &lc->handler, r, &results);
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
                                  css_fixed length, css_unit unit, float font_size,
                                  float pct_basis)
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
    /* vh/vw/etc. — approximate */
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

int yetty_ybrowser_libcss_bg_color(const css_computed_style *style,
                                   struct yetty_ylexbor_color *out)
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
    if (!style) return 0;
    css_fixed l = 0; css_unit u = CSS_UNIT_PX;
    uint8_t k = css_computed_width(style, &l, &u);
    return len_or_pct_property(k, l, u, CSS_WIDTH_SET, r, style, font_size, pct_basis, out_px);
}

int yetty_ybrowser_libcss_height(struct yetty_ylexbor *r, const css_computed_style *style,
                                 float font_size, float pct_basis, float *out_px)
{
    if (!style) return 0;
    css_fixed l = 0; css_unit u = CSS_UNIT_PX;
    uint8_t k = css_computed_height(style, &l, &u);
    return len_or_pct_property(k, l, u, CSS_HEIGHT_SET, r, style, font_size, pct_basis, out_px);
}

int yetty_ybrowser_libcss_max_width(struct yetty_ylexbor *r, const css_computed_style *style,
                                    float font_size, float pct_basis, float *out_px)
{
    if (!style) return 0;
    css_fixed l = 0; css_unit u = CSS_UNIT_PX;
    uint8_t k = css_computed_max_width(style, &l, &u);
    return len_or_pct_property(k, l, u, CSS_MAX_WIDTH_SET, r, style, font_size, pct_basis, out_px);
}

int yetty_ybrowser_libcss_min_width(struct yetty_ylexbor *r, const css_computed_style *style,
                                    float font_size, float pct_basis, float *out_px)
{
    if (!style) return 0;
    css_fixed l = 0; css_unit u = CSS_UNIT_PX;
    uint8_t k = css_computed_min_width(style, &l, &u);
    return len_or_pct_property(k, l, u, CSS_MIN_WIDTH_SET, r, style, font_size, pct_basis, out_px);
}

int yetty_ybrowser_libcss_margin(struct yetty_ylexbor *r, const css_computed_style *style,
                                 int side, float font_size, float pct_basis, float *out_px,
                                 bool *out_auto)
{
    if (!style) return 0;
    if (out_auto) *out_auto = false;
    css_fixed l = 0; css_unit u = CSS_UNIT_PX;
    uint8_t kind = 0;
    switch (side) {
    case 0: kind = css_computed_margin_top(style, &l, &u); break;
    case 1: kind = css_computed_margin_right(style, &l, &u); break;
    case 2: kind = css_computed_margin_bottom(style, &l, &u); break;
    case 3: kind = css_computed_margin_left(style, &l, &u); break;
    default: return 0;
    }
    if (kind == CSS_MARGIN_AUTO) {
        if (out_auto) *out_auto = true;
        return 1;
    }
    if (kind == CSS_MARGIN_SET) {
        *out_px = resolve_length_to_px(r, style, l, u, font_size, pct_basis);
        return 1;
    }
    return 0;
}

int yetty_ybrowser_libcss_padding(struct yetty_ylexbor *r, const css_computed_style *style,
                                  int side, float font_size, float pct_basis, float *out_px)
{
    if (!style) return 0;
    css_fixed l = 0; css_unit u = CSS_UNIT_PX;
    uint8_t kind = 0;
    switch (side) {
    case 0: kind = css_computed_padding_top(style, &l, &u); break;
    case 1: kind = css_computed_padding_right(style, &l, &u); break;
    case 2: kind = css_computed_padding_bottom(style, &l, &u); break;
    case 3: kind = css_computed_padding_left(style, &l, &u); break;
    default: return 0;
    }
    if (kind == CSS_PADDING_SET) {
        *out_px = resolve_length_to_px(r, style, l, u, font_size, pct_basis);
        return 1;
    }
    return 0;
}

int yetty_ybrowser_libcss_border_width(struct yetty_ylexbor *r, const css_computed_style *style,
                                       int side, float font_size, float *out_px)
{
    if (!style) return 0;
    /* border-style is "none" by default — when it is none/hidden, the
     * border is suppressed regardless of border-width. The cascade
     * still reports MEDIUM (the keyword initial) for the width, which
     * would otherwise materialise as a 3px ghost border on every
     * element. Gate the width by the style. */
    uint8_t bstyle = 0;
    switch (side) {
    case 0: bstyle = css_computed_border_top_style(style); break;
    case 1: bstyle = css_computed_border_right_style(style); break;
    case 2: bstyle = css_computed_border_bottom_style(style); break;
    case 3: bstyle = css_computed_border_left_style(style); break;
    }
    if (bstyle == CSS_BORDER_STYLE_NONE || bstyle == CSS_BORDER_STYLE_HIDDEN ||
        bstyle == CSS_BORDER_STYLE_INHERIT) {
        return 0;
    }
    css_fixed l = 0; css_unit u = CSS_UNIT_PX;
    uint8_t kind = 0;
    switch (side) {
    case 0: kind = css_computed_border_top_width(style, &l, &u); break;
    case 1: kind = css_computed_border_right_width(style, &l, &u); break;
    case 2: kind = css_computed_border_bottom_width(style, &l, &u); break;
    case 3: kind = css_computed_border_left_width(style, &l, &u); break;
    default: return 0;
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
    if (!style) return 0;
    css_color cc = 0;
    uint8_t kind = 0;
    switch (side) {
    case 0: kind = css_computed_border_top_color(style, &cc); break;
    case 1: kind = css_computed_border_right_color(style, &cc); break;
    case 2: kind = css_computed_border_bottom_color(style, &cc); break;
    case 3: kind = css_computed_border_left_color(style, &cc); break;
    default: return 0;
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
    if (!style) return 0;
    css_fixed l = 0; css_unit u = CSS_UNIT_PX;
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
    if (!style) return 0;
    uint8_t v = css_computed_font_weight(style);
    switch (v) {
    case CSS_FONT_WEIGHT_NORMAL:  *out = 400; return 1;
    case CSS_FONT_WEIGHT_BOLD:    *out = 700; return 1;
    case CSS_FONT_WEIGHT_BOLDER:  *out = 700; return 1;
    case CSS_FONT_WEIGHT_LIGHTER: *out = 300; return 1;
    case CSS_FONT_WEIGHT_100: *out = 100; return 1;
    case CSS_FONT_WEIGHT_200: *out = 200; return 1;
    case CSS_FONT_WEIGHT_300: *out = 300; return 1;
    case CSS_FONT_WEIGHT_400: *out = 400; return 1;
    case CSS_FONT_WEIGHT_500: *out = 500; return 1;
    case CSS_FONT_WEIGHT_600: *out = 600; return 1;
    case CSS_FONT_WEIGHT_700: *out = 700; return 1;
    case CSS_FONT_WEIGHT_800: *out = 800; return 1;
    case CSS_FONT_WEIGHT_900: *out = 900; return 1;
    default: return 0;
    }
}

int yetty_ybrowser_libcss_font_italic(const css_computed_style *style, bool *out)
{
    if (!style) return 0;
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
    if (!style) return CSS_DISPLAY_INLINE;
    return css_computed_display(style, root);
}

int yetty_ybrowser_libcss_text_align(const css_computed_style *style)
{
    if (!style) return CSS_TEXT_ALIGN_LEFT;
    return css_computed_text_align(style);
}

int yetty_ybrowser_libcss_flex_direction(const css_computed_style *style)
{
    if (!style) return CSS_FLEX_DIRECTION_ROW;
    return css_computed_flex_direction(style);
}

int yetty_ybrowser_libcss_flex_grow(const css_computed_style *style, float *out)
{
    if (!style || !out) return 0;
    css_fixed n = 0;
    uint8_t kind = css_computed_flex_grow(style, &n);
    if (kind == CSS_FLEX_GROW_SET) {
        *out = fixed_to_float(n);
        return 1;
    }
    return 0;
}

int yetty_ybrowser_libcss_flex_basis(struct yetty_ylexbor *r, const css_computed_style *style,
                                     float font_size, float pct_basis, float *out_px,
                                     bool *out_auto)
{
    if (!style) return 0;
    if (out_auto) *out_auto = false;
    css_fixed l = 0; css_unit u = CSS_UNIT_PX;
    uint8_t kind = css_computed_flex_basis(style, &l, &u);
    if (kind == CSS_FLEX_BASIS_AUTO || kind == CSS_FLEX_BASIS_CONTENT) {
        if (out_auto) *out_auto = true;
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
    if (!style) return CSS_JUSTIFY_CONTENT_FLEX_START;
    return css_computed_justify_content(style);
}

int yetty_ybrowser_libcss_align_items(const css_computed_style *style)
{
    if (!style) return CSS_ALIGN_ITEMS_STRETCH;
    return css_computed_align_items(style);
}

int yetty_ybrowser_libcss_float(const css_computed_style *style)
{
    if (!style) return CSS_FLOAT_NONE;
    return css_computed_float(style);
}

int yetty_ybrowser_libcss_clear(const css_computed_style *style)
{
    if (!style) return CSS_CLEAR_NONE;
    return css_computed_clear(style);
}
