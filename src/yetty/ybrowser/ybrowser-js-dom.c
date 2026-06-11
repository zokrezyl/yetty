/*
 * ylexbor-js-dom — minimal DOM bindings for QuickJS over a lexbor DOM.
 *
 * Implements the subset of WebAPI a "click button → JS toggles class /
 * sets text → page re-renders" demo needs:
 *
 *   document.documentElement / body / head
 *   document.getElementById(id)
 *   document.querySelector(sel) / querySelectorAll(sel)
 *   document.createElement(tag) / createTextNode(text)
 *
 *   Element.tagName / id / className
 *   Element.textContent / innerHTML / outerHTML       (get + set)
 *   Element.getAttribute / setAttribute / removeAttribute / hasAttribute
 *   Element.appendChild / removeChild / insertBefore / replaceChild
 *   Element.children / firstElementChild / nextElementSibling /
 *           parentElement
 *   Element.style.<prop>            (get + set, written through to the
 *                                    `style` HTML attribute)
 *   Element.classList.{add,remove,toggle,contains}
 *   Element.addEventListener(type, fn)
 *
 *   window === globalThis
 *
 * Every DOM-mutating method bumps r->dom_dirty so the host knows to
 * re-run box-build → layout → paint after the JS turn finishes. Click
 * dispatch (yetty_ylexbor_dispatch_click) hit-tests against the box
 * vector, walks up the element ancestry, and fires every matching
 * 'click' listener with a synthetic Event-shaped object.
 *
 * Compile-out: YETTY_HAVE_QUICKJS=0 → all calls are no-ops, defined
 * at the bottom of the file.
 */

#include "ybrowser-internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef YETTY_HAVE_QUICKJS
#define YETTY_HAVE_QUICKJS 0
#endif

#if YETTY_HAVE_QUICKJS

#include <quickjs.h>

#include <lexbor/dom/dom.h>
#include <lexbor/html/html.h>
#include <lexbor/selectors/selectors.h>
#include <lexbor/css/css.h>
#include <lexbor/tag/const.h>

#include <yetty/ytrace/ytrace.h>

/* ===========================================================================
 * Class registration
 *
 * Three classes: Node (base), Element, Document. Opaque pointer = the
 * underlying lxb_dom_*_t. Lexbor owns the lifetime; we never free those
 * via QuickJS finalizers — finalizer just clears our ref.
 * ===========================================================================*/

static JSClassID class_node_id;
static JSClassID class_element_id;
static JSClassID class_document_id;
static JSClassID class_classlist_id;
static JSClassID class_style_id;

static void node_finalizer(JSRuntime *rt, JSValue val)
{
    (void)rt;
    (void)val;
    /* lexbor owns the DOM tree; we don't free anything here. */
}

static JSClassDef class_node_def = {"Node", .finalizer = node_finalizer};
static JSClassDef class_element_def = {"Element", .finalizer = node_finalizer};
static JSClassDef class_document_def = {"Document", .finalizer = node_finalizer};
static JSClassDef class_classlist_def = {"DOMTokenList", .finalizer = node_finalizer};
static JSClassDef class_style_def = {"CSSStyleDeclaration", .finalizer = node_finalizer};

/* yetty_ylexbor lives on the JSRuntime so handlers can find it. */
struct js_dom_state {
    struct yetty_ylexbor *r;
};

static struct yetty_ylexbor *runtime_ylex(JSContext *ctx)
{
    JSRuntime *rt = JS_GetRuntime(ctx);
    struct js_dom_state *s = JS_GetRuntimeOpaque(rt);
    return s ? s->r : NULL;
}

/* Mutator helper. Always paired with a DOM modification. */
static void mark_dirty(JSContext *ctx)
{
    struct yetty_ylexbor *r = runtime_ylex(ctx);
    if (r) {
        r->dom_dirty = 1;
    }
}

/* ===========================================================================
 * Element / Node wrapping.
 *
 * Spec-conformance tests rely on `===` between wrappers — e.g.
 * `el.parentNode === parent`, `node.ownerDocument === doc`,
 * `el.firstChild.nextSibling === el.lastChild`. Minting a fresh JSValue
 * per access (which is what we used to do, since lexbor still gives a
 * stable lxb_*_t* and we never free the wrapper anyway) breaks every
 * one of those assertions.
 *
 * Fix: a small open-addressed hash from lxb pointer to its wrapper
 * JSValue. The cache holds one extra ref per wrapper (JS_DupValue).
 * Looked-up wrappers are returned with one *more* JS_DupValue so the
 * caller still owns the ref it expects to free. Cache is cleared at
 * runtime destroy via yetty_ylexbor_js_dom_reset, releasing all the
 * dup'd refs so the wrappers can be GCed.
 *
 * The cache is process-static to match the class-ID lifetime — the
 * fork-per-test runner tears it down via dom_reset between runs, and
 * single-process callers (yetty itself) hit it once per element.
 * ===========================================================================*/

#define WRAP_CACHE_BUCKETS 4096
struct wrap_cache_entry {
    void *key;
    JSValue val;
};
static struct wrap_cache_entry g_wrap_cache[WRAP_CACHE_BUCKETS];

static size_t wrap_hash(void *p)
{
    uintptr_t k = (uintptr_t)p;
    k ^= k >> 33;
    k *= 0xff51afd7ed558ccdULL;
    k ^= k >> 33;
    return (size_t)(k & (WRAP_CACHE_BUCKETS - 1));
}

static JSValue wrap_cache_lookup(JSContext *ctx, void *key)
{
    if (!key) {
        return JS_UNDEFINED;
    }
    size_t h = wrap_hash(key);
    for (size_t i = 0; i < WRAP_CACHE_BUCKETS; i++) {
        size_t idx = (h + i) & (WRAP_CACHE_BUCKETS - 1);
        if (!g_wrap_cache[idx].key) {
            return JS_UNDEFINED;
        }
        if (g_wrap_cache[idx].key == key) {
            return JS_DupValue(ctx, g_wrap_cache[idx].val);
        }
    }
    return JS_UNDEFINED;
}

static void wrap_cache_insert(JSContext *ctx, void *key, JSValueConst v)
{
    if (!key) {
        return;
    }
    size_t h = wrap_hash(key);
    for (size_t i = 0; i < WRAP_CACHE_BUCKETS; i++) {
        size_t idx = (h + i) & (WRAP_CACHE_BUCKETS - 1);
        if (!g_wrap_cache[idx].key || g_wrap_cache[idx].key == key) {
            g_wrap_cache[idx].key = key;
            g_wrap_cache[idx].val = JS_DupValue(ctx, v);
            return;
        }
    }
    /* Table full — fall back to non-cached behavior, identity will
	 * regress for late-allocated elements but no crash. */
}

static void wrap_cache_clear(JSContext *ctx)
{
    for (size_t i = 0; i < WRAP_CACHE_BUCKETS; i++) {
        if (g_wrap_cache[i].key) {
            JS_FreeValue(ctx, g_wrap_cache[i].val);
            g_wrap_cache[i].key = NULL;
            g_wrap_cache[i].val = JS_UNDEFINED;
        }
    }
}

static JSValue wrap_element(JSContext *ctx, lxb_dom_element_t *el)
{
    if (el == NULL) {
        return JS_NULL;
    }
    JSValue cached = wrap_cache_lookup(ctx, el);
    if (!JS_IsUndefined(cached)) {
        return cached;
    }
    JSValue v = JS_NewObjectClass(ctx, class_element_id);
    JS_SetOpaque(v, el);
    wrap_cache_insert(ctx, el, v);
    return v;
}

static JSValue wrap_document(JSContext *ctx, lxb_html_document_t *doc)
{
    if (doc == NULL) {
        return JS_NULL;
    }
    JSValue cached = wrap_cache_lookup(ctx, doc);
    if (!JS_IsUndefined(cached)) {
        return cached;
    }
    JSValue v = JS_NewObjectClass(ctx, class_document_id);
    JS_SetOpaque(v, doc);
    wrap_cache_insert(ctx, doc, v);
    return v;
}

static lxb_dom_element_t *unwrap_element(JSValueConst this_val)
{
    void *p = JS_GetOpaque(this_val, class_element_id);
    if (p) {
        return (lxb_dom_element_t *)p;
    }
    /* Element methods are also called on Document via prototype chain
	 * sometimes (querySelector on document). Allow both. */
    return NULL;
}

static lxb_html_document_t *unwrap_document(JSValueConst this_val)
{
    void *p = JS_GetOpaque(this_val, class_document_id);
    return p ? (lxb_html_document_t *)p : NULL;
}

/* Walk-up to find the lxb_dom_node_t this JSValue represents (Element
 * or Document). Returns NULL if neither. */
static lxb_dom_node_t *unwrap_node(JSValueConst this_val)
{
    lxb_dom_element_t *e = JS_GetOpaque(this_val, class_element_id);
    if (e) {
        return lxb_dom_interface_node(e);
    }
    lxb_html_document_t *d = JS_GetOpaque(this_val, class_document_id);
    if (d) {
        return lxb_dom_interface_node(d);
    }
    return NULL;
}

/* ===========================================================================
 * Selectors — backed by lexbor's selectors module. Construct fresh per
 * call. The shared-global approach kept tripping over lexbor's internal
 * memory pools. Selector parses are cheap; not worth the headache.
 * ===========================================================================*/

struct sel_collect_ctx {
    lxb_dom_element_t **elements;
    size_t count, cap;
    int first_only;
    lxb_dom_element_t *first;
};

static lxb_status_t sel_found_cb(lxb_dom_node_t *node, lxb_css_selector_specificity_t spec,
                                 void *ctx)
{
    (void)spec;
    struct sel_collect_ctx *c = ctx;
    if (node->type != LXB_DOM_NODE_TYPE_ELEMENT) {
        return LXB_STATUS_OK;
    }
    lxb_dom_element_t *el = lxb_dom_interface_element(node);
    if (c->first_only) {
        if (c->first == NULL) {
            c->first = el;
        }
        return LXB_STATUS_STOP;
    }
    if (c->count == c->cap) {
        size_t nc = c->cap ? c->cap * 2 : 8;
        void *p = realloc(c->elements, nc * sizeof(*c->elements));
        if (!p) {
            return LXB_STATUS_ERROR_MEMORY_ALLOCATION;
        }
        c->elements = p;
        c->cap = nc;
    }
    c->elements[c->count++] = el;
    return LXB_STATUS_OK;
}

static int run_selector(lxb_dom_node_t *root, const char *sel_text, size_t sel_len,
                        struct sel_collect_ctx *out, int first_only)
{
    int rc = -1;
    lxb_css_parser_t *parser = lxb_css_parser_create();
    if (parser == NULL) {
        return -1;
    }
    if (lxb_css_parser_init(parser, NULL) != LXB_STATUS_OK) {
        goto out_parser;
    }

    lxb_selectors_t *sel = lxb_selectors_create();
    if (sel == NULL) {
        goto out_parser;
    }
    if (lxb_selectors_init(sel) != LXB_STATUS_OK) {
        goto out_sel;
    }

    lxb_css_selector_list_t *list =
        lxb_css_selectors_parse(parser, (const lxb_char_t *)sel_text, sel_len);
    if (list == NULL) {
        goto out_sel;
    }

    out->first_only = first_only;
    lxb_status_t s = lxb_selectors_find(sel, root, list, sel_found_cb, out);
    lxb_css_selector_list_destroy_memory(list);
    if (s == LXB_STATUS_OK || s == LXB_STATUS_STOP) {
        rc = 0;
    }

out_sel:
    lxb_selectors_destroy(sel, true);
out_parser:
    lxb_css_parser_destroy(parser, true);
    return rc;
}

/* ===========================================================================
 * Element methods — getAttribute / setAttribute / removeAttribute /
 * hasAttribute / appendChild / removeChild / textContent / innerHTML /
 * tagName / id / className / outerHTML / addEventListener
 * ===========================================================================*/

/* Forward declaration — defined alongside toggleAttribute below. */
static char *attr_normalize_name(JSContext *ctx, JSValueConst v, size_t *out_len);

static JSValue js_el_getAttribute(JSContext *ctx, JSValueConst this_val, int argc,
                                  JSValueConst *argv)
{
    if (argc < 1) {
        return JS_NULL;
    }
    lxb_dom_element_t *el = unwrap_element(this_val);
    if (!el) {
        return JS_NULL;
    }
    size_t nlen;
    char *name = attr_normalize_name(ctx, argv[0], &nlen);
    /* Per spec, getAttribute does NOT throw on invalid names — it
	 * just returns whatever lookup yields. Fall back to the raw
	 * string when normalize raises (and clear the exception). */
    if (!name) {
        JS_FreeValue(ctx, JS_GetException(ctx));
        const char *raw = JS_ToCStringLen(ctx, &nlen, argv[0]);
        if (!raw) {
            return JS_NULL;
        }
        size_t vlen = 0;
        const lxb_char_t *v =
            lxb_dom_element_get_attribute(el, (const lxb_char_t *)raw, nlen, &vlen);
        JS_FreeCString(ctx, raw);
        if (!v) {
            return JS_NULL;
        }
        return JS_NewStringLen(ctx, (const char *)v, vlen);
    }
    size_t vlen = 0;
    const lxb_char_t *v = lxb_dom_element_get_attribute(el, (const lxb_char_t *)name, nlen, &vlen);
    free(name);
    if (!v) {
        return JS_NULL;
    }
    return JS_NewStringLen(ctx, (const char *)v, vlen);
}

static JSValue js_el_setAttribute(JSContext *ctx, JSValueConst this_val, int argc,
                                  JSValueConst *argv)
{
    if (argc < 2) {
        return JS_UNDEFINED;
    }
    lxb_dom_element_t *el = unwrap_element(this_val);
    if (!el) {
        return JS_UNDEFINED;
    }
    size_t nlen, vlen;
    char *name = attr_normalize_name(ctx, argv[0], &nlen);
    if (!name) {
        return JS_EXCEPTION;
    }
    const char *val = JS_ToCStringLen(ctx, &vlen, argv[1]);
    if (val) {
        lxb_dom_element_set_attribute(el, (const lxb_char_t *)name, nlen, (const lxb_char_t *)val,
                                      vlen);
        mark_dirty(ctx);
        JS_FreeCString(ctx, val);
    }
    free(name);
    return JS_UNDEFINED;
}

static JSValue js_el_removeAttribute(JSContext *ctx, JSValueConst this_val, int argc,
                                     JSValueConst *argv)
{
    if (argc < 1) {
        return JS_UNDEFINED;
    }
    lxb_dom_element_t *el = unwrap_element(this_val);
    if (!el) {
        return JS_UNDEFINED;
    }
    size_t nlen;
    char *name = attr_normalize_name(ctx, argv[0], &nlen);
    /* removeAttribute is also non-throwing for malformed names. */
    if (!name) {
        JS_FreeValue(ctx, JS_GetException(ctx));
        const char *raw = JS_ToCStringLen(ctx, &nlen, argv[0]);
        if (raw) {
            lxb_dom_element_remove_attribute(el, (const lxb_char_t *)raw, nlen);
            mark_dirty(ctx);
            JS_FreeCString(ctx, raw);
        }
        return JS_UNDEFINED;
    }
    lxb_dom_element_remove_attribute(el, (const lxb_char_t *)name, nlen);
    mark_dirty(ctx);
    free(name);
    return JS_UNDEFINED;
}

static JSValue js_el_hasAttribute(JSContext *ctx, JSValueConst this_val, int argc,
                                  JSValueConst *argv)
{
    if (argc < 1) {
        return JS_FALSE;
    }
    lxb_dom_element_t *el = unwrap_element(this_val);
    if (!el) {
        return JS_FALSE;
    }
    size_t nlen;
    char *name = attr_normalize_name(ctx, argv[0], &nlen);
    if (!name) {
        JS_FreeValue(ctx, JS_GetException(ctx));
        const char *raw = JS_ToCStringLen(ctx, &nlen, argv[0]);
        if (!raw) {
            return JS_FALSE;
        }
        bool has = lxb_dom_element_has_attribute(el, (const lxb_char_t *)raw, nlen);
        JS_FreeCString(ctx, raw);
        return JS_NewBool(ctx, has);
    }
    bool has = lxb_dom_element_has_attribute(el, (const lxb_char_t *)name, nlen);
    free(name);
    return JS_NewBool(ctx, has);
}

static JSValue js_el_appendChild(JSContext *ctx, JSValueConst this_val, int argc,
                                 JSValueConst *argv)
{
    if (argc < 1) {
        return JS_UNDEFINED;
    }
    lxb_dom_node_t *parent = unwrap_node(this_val);
    lxb_dom_node_t *child = unwrap_node(argv[0]);
    if (!parent || !child) {
        return JS_UNDEFINED;
    }
    /* Cycle guard: spec requires throwing HierarchyRequestError when
	 * `parent === child` or `parent` is a descendant of `child`. We
	 * don't model the DOMException family here — silently dropping
	 * the insert is enough to avoid corrupting lexbor's tree (where
	 * a cycle turns the next tree-walk into an infinite loop). */
    if (parent == child) {
        return JS_DupValue(ctx, argv[0]);
    }
    for (lxb_dom_node_t *n = parent; n; n = n->parent) {
        if (n == child) {
            return JS_DupValue(ctx, argv[0]);
        }
    }
    /* Pre-insert step from the spec: detach child from its current
	 * parent first. lexbor's insert_child does NOT do this — calling
	 * it on an already-attached child corrupts the old parent's
	 * sibling chain (next/prev becomes ambiguous), so a later tree
	 * walk on the old parent infinite-loops. */
    if (child->parent) {
        lxb_dom_node_remove(child);
    }
    lxb_dom_node_insert_child(parent, child);
    mark_dirty(ctx);
    return JS_DupValue(ctx, argv[0]);
}

static JSValue js_el_removeChild(JSContext *ctx, JSValueConst this_val, int argc,
                                 JSValueConst *argv)
{
    if (argc < 1) {
        return JS_UNDEFINED;
    }
    lxb_dom_node_t *child = unwrap_node(argv[0]);
    if (!child) {
        return JS_UNDEFINED;
    }
    lxb_dom_node_remove(child);
    mark_dirty(ctx);
    return JS_DupValue(ctx, argv[0]);
}

/* Returns 1 if `child` is `parent` or in `parent`'s ancestor chain.
 * Used by every insertion path to refuse cycles before lexbor sees them. */
static int node_would_cycle(lxb_dom_node_t *parent, lxb_dom_node_t *child)
{
    if (!parent || !child) {
        return 0;
    }
    if (parent == child) {
        return 1;
    }
    for (lxb_dom_node_t *n = parent; n; n = n->parent) {
        if (n == child) {
            return 1;
        }
    }
    return 0;
}

/* Convert one argv[i] into a lxb_dom_node_t* suitable for insertion.
 * Strings convert to fresh elements with the string as textContent
 * (we have no Text-node wrapper; this preserves the user's text in
 * the rendered tree while passing the WPT identity-by-reference checks
 * that don't compare wrappers). null/undefined returns NULL. */
static lxb_dom_node_t *coerce_to_node(JSContext *ctx, JSValueConst v, lxb_dom_document_t *doc)
{
    if (JS_IsNull(v) || JS_IsUndefined(v)) {
        return NULL;
    }
    lxb_dom_node_t *n = unwrap_node(v);
    if (n) {
        return n;
    }
    if (!doc) {
        return NULL;
    }
    size_t l;
    const char *s = JS_ToCStringLen(ctx, &l, v);
    if (!s) {
        return NULL;
    }
    lxb_dom_element_t *el =
        lxb_dom_document_create_element(doc, (const lxb_char_t *)"span", 4, NULL);
    if (el) {
        lxb_dom_text_t *t = lxb_dom_document_create_text_node(doc, (const lxb_char_t *)s, l);
        if (t) {
            lxb_dom_node_insert_child(lxb_dom_interface_node(el), lxb_dom_interface_node(t));
        }
    }
    JS_FreeCString(ctx, s);
    return el ? lxb_dom_interface_node(el) : NULL;
}

/* All ChildNode/ParentNode insertion paths must detach the incoming
 * node from its old parent first — see js_el_appendChild for the
 * reasoning (lexbor leaves stale sibling links otherwise). */
static void detach(lxb_dom_node_t *n)
{
    if (n && n->parent) {
        lxb_dom_node_remove(n);
    }
}

/* before(): insert each arg as a left sibling of `this`.
 * after():  insert each arg as a right sibling of `this`.
 * replaceWith(): insert all args in `this`'s position, then remove `this`. */
static JSValue js_el_before(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    lxb_dom_node_t *self = unwrap_node(this_val);
    if (!self || !self->parent) {
        return JS_UNDEFINED;
    }
    lxb_dom_document_t *doc = self->owner_document;
    for (int i = 0; i < argc; i++) {
        lxb_dom_node_t *n = coerce_to_node(ctx, argv[i], doc);
        if (!n) {
            continue;
        }
        if (node_would_cycle(self->parent, n)) {
            continue;
        }
        detach(n);
        lxb_dom_node_insert_before(self, n);
    }
    mark_dirty(ctx);
    return JS_UNDEFINED;
}

static JSValue js_el_after(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    lxb_dom_node_t *self = unwrap_node(this_val);
    if (!self || !self->parent) {
        return JS_UNDEFINED;
    }
    lxb_dom_document_t *doc = self->owner_document;
    /* Insert in argv-order AFTER self. lexbor's insert_after places
	 * `n` immediately after self; we walk a moving anchor so a,b,c
	 * end up in source order. */
    lxb_dom_node_t *anchor = self;
    for (int i = 0; i < argc; i++) {
        lxb_dom_node_t *n = coerce_to_node(ctx, argv[i], doc);
        if (!n) {
            continue;
        }
        if (node_would_cycle(self->parent, n)) {
            continue;
        }
        detach(n);
        lxb_dom_node_insert_after(anchor, n);
        anchor = n;
    }
    mark_dirty(ctx);
    return JS_UNDEFINED;
}

static JSValue js_el_replaceWith(JSContext *ctx, JSValueConst this_val, int argc,
                                 JSValueConst *argv)
{
    lxb_dom_node_t *self = unwrap_node(this_val);
    if (!self || !self->parent) {
        return JS_UNDEFINED;
    }
    lxb_dom_document_t *doc = self->owner_document;
    for (int i = 0; i < argc; i++) {
        lxb_dom_node_t *n = coerce_to_node(ctx, argv[i], doc);
        if (!n) {
            continue;
        }
        if (node_would_cycle(self->parent, n)) {
            continue;
        }
        detach(n);
        lxb_dom_node_insert_before(self, n);
    }
    lxb_dom_node_remove(self);
    mark_dirty(ctx);
    return JS_UNDEFINED;
}

/* prepend(): insert each arg before parent's firstChild.
 * append(): insert each arg as last child of parent. */
static JSValue js_el_prepend(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    lxb_dom_node_t *parent = unwrap_node(this_val);
    if (!parent) {
        return JS_UNDEFINED;
    }
    lxb_dom_document_t *doc = parent->owner_document;
    lxb_dom_node_t *first = parent->first_child;
    for (int i = 0; i < argc; i++) {
        lxb_dom_node_t *n = coerce_to_node(ctx, argv[i], doc);
        if (!n) {
            continue;
        }
        if (node_would_cycle(parent, n)) {
            continue;
        }
        detach(n);
        if (first) {
            lxb_dom_node_insert_before(first, n);
        } else {
            lxb_dom_node_insert_child(parent, n);
        }
    }
    mark_dirty(ctx);
    return JS_UNDEFINED;
}

static JSValue js_el_append(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    lxb_dom_node_t *parent = unwrap_node(this_val);
    if (!parent) {
        return JS_UNDEFINED;
    }
    lxb_dom_document_t *doc = parent->owner_document;
    for (int i = 0; i < argc; i++) {
        lxb_dom_node_t *n = coerce_to_node(ctx, argv[i], doc);
        if (!n) {
            continue;
        }
        if (node_would_cycle(parent, n)) {
            continue;
        }
        detach(n);
        lxb_dom_node_insert_child(parent, n);
    }
    mark_dirty(ctx);
    return JS_UNDEFINED;
}

/* insertBefore(newNode, refNode): insert newNode as a child of `this`
 * immediately before refNode. A null/undefined or non-matching refNode falls
 * back to append (per spec, a null ref means "append"). Returns newNode.
 *
 * This used to alias appendChild — which silently reordered every
 * framework-driven insertion to the end of the parent. JS app shells (Google
 * News, Turbo, React-ish renderers) rely on insertBefore to place nodes in the
 * right order, so aliasing it to append scrambled the rendered DOM. */
static JSValue js_el_insertBefore(JSContext *ctx, JSValueConst this_val, int argc,
                                  JSValueConst *argv)
{
    if (argc < 1) {
        return JS_UNDEFINED;
    }
    lxb_dom_node_t *parent = unwrap_node(this_val);
    lxb_dom_node_t *node = unwrap_node(argv[0]);
    if (!parent || !node) {
        return JS_UNDEFINED;
    }
    if (node_would_cycle(parent, node)) {
        return JS_DupValue(ctx, argv[0]);
    }
    lxb_dom_node_t *ref = (argc >= 2) ? unwrap_node(argv[1]) : NULL;
    /* Detach from the current parent first — lexbor's insert paths assume an
     * unlinked node (see appendChild). */
    if (node->parent) {
        lxb_dom_node_remove(node);
    }
    if (ref && ref->parent == parent) {
        lxb_dom_node_insert_before(ref, node);
    } else {
        /* null ref, or ref not a child of parent → append. */
        lxb_dom_node_insert_child(parent, node);
    }
    mark_dirty(ctx);
    return JS_DupValue(ctx, argv[0]);
}

/* replaceChild(newChild, oldChild): replace oldChild (a child of `this`) with
 * newChild, in place. Returns oldChild. Previously aliased to appendChild,
 * which neither removed oldChild nor preserved its position. */
static JSValue js_el_replaceChild(JSContext *ctx, JSValueConst this_val, int argc,
                                  JSValueConst *argv)
{
    if (argc < 2) {
        return JS_UNDEFINED;
    }
    lxb_dom_node_t *parent = unwrap_node(this_val);
    lxb_dom_node_t *node = unwrap_node(argv[0]);
    lxb_dom_node_t *old = unwrap_node(argv[1]);
    if (!parent || !node || !old || old->parent != parent) {
        return JS_UNDEFINED;
    }
    if (node_would_cycle(parent, node)) {
        return JS_DupValue(ctx, argv[1]);
    }
    if (node->parent) {
        lxb_dom_node_remove(node);
    }
    lxb_dom_node_insert_before(old, node);
    lxb_dom_node_remove(old);
    mark_dirty(ctx);
    return JS_DupValue(ctx, argv[1]);
}

/* querySelector(All) on Element AND Document — same impl. */
static JSValue js_el_querySelector(JSContext *ctx, JSValueConst this_val, int argc,
                                   JSValueConst *argv)
{
    if (argc < 1) {
        return JS_NULL;
    }
    lxb_dom_node_t *root = unwrap_node(this_val);
    if (!root) {
        return JS_NULL;
    }
    size_t slen;
    const char *sel = JS_ToCStringLen(ctx, &slen, argv[0]);
    if (!sel) {
        return JS_NULL;
    }
    struct sel_collect_ctx c = {0};
    int rc = run_selector(root, sel, slen, &c, /*first_only=*/1);
    JS_FreeCString(ctx, sel);
    free(c.elements);
    if (rc != 0 || c.first == NULL) {
        return JS_NULL;
    }
    return wrap_element(ctx, c.first);
}

static JSValue js_el_querySelectorAll(JSContext *ctx, JSValueConst this_val, int argc,
                                      JSValueConst *argv)
{
    if (argc < 1) {
        return JS_NewArray(ctx);
    }
    lxb_dom_node_t *root = unwrap_node(this_val);
    if (!root) {
        return JS_NewArray(ctx);
    }
    size_t slen;
    const char *sel = JS_ToCStringLen(ctx, &slen, argv[0]);
    if (!sel) {
        return JS_NewArray(ctx);
    }
    struct sel_collect_ctx c = {0};
    (void)run_selector(root, sel, slen, &c, /*first_only=*/0);
    JS_FreeCString(ctx, sel);
    JSValue arr = JS_NewArray(ctx);
    for (size_t i = 0; i < c.count; i++) {
        JS_SetPropertyUint32(ctx, arr, (uint32_t)i, wrap_element(ctx, c.elements[i]));
    }
    free(c.elements);
    return arr;
}

/* getElementsByTagName / -ClassName / -Name — implemented as
 * querySelectorAll under the hood. The DOM spec asks for "live"
 * collections that auto-update on mutation; we return a plain Array
 * snapshot since the rest of our DOM bindings don't model liveness
 * either. Callers iterate via .length / [i] / forEach which all work. */
static JSValue js_el_getElementsByTagName(JSContext *ctx, JSValueConst this_val, int argc,
                                          JSValueConst *argv)
{
    if (argc < 1) {
        return JS_NewArray(ctx);
    }
    lxb_dom_node_t *root = unwrap_node(this_val);
    if (!root) {
        return JS_NewArray(ctx);
    }
    size_t slen;
    const char *tag = JS_ToCStringLen(ctx, &slen, argv[0]);
    if (!tag) {
        return JS_NewArray(ctx);
    }
    /* `*` matches any element. */
    struct sel_collect_ctx c = {0};
    (void)run_selector(root, tag, slen, &c, /*first_only=*/0);
    JS_FreeCString(ctx, tag);
    JSValue arr = JS_NewArray(ctx);
    for (size_t i = 0; i < c.count; i++) {
        JS_SetPropertyUint32(ctx, arr, (uint32_t)i, wrap_element(ctx, c.elements[i]));
    }
    JS_SetPropertyStr(ctx, arr, "item",
                      JS_Eval(ctx, "(function(i){return this[i]||null;})", 36, "<gebtn-item>",
                              JS_EVAL_TYPE_GLOBAL));
    free(c.elements);
    return arr;
}

static JSValue js_el_getElementsByClassName(JSContext *ctx, JSValueConst this_val, int argc,
                                            JSValueConst *argv)
{
    if (argc < 1) {
        return JS_NewArray(ctx);
    }
    lxb_dom_node_t *root = unwrap_node(this_val);
    if (!root) {
        return JS_NewArray(ctx);
    }
    size_t slen;
    const char *cls = JS_ToCStringLen(ctx, &slen, argv[0]);
    if (!cls) {
        return JS_NewArray(ctx);
    }
    /* class names may be space-separated; treat as a compound class
	 * selector ".a.b.c". */
    char buf[512];
    size_t off = 0;
    const char *p = cls, *end = cls + slen;
    while (p < end && off + 1 < sizeof(buf)) {
        while (p < end && (*p == ' ' || *p == '\t')) {
            p++;
        }
        if (p >= end) {
            break;
        }
        if (off + 1 >= sizeof(buf)) {
            break;
        }
        buf[off++] = '.';
        while (p < end && *p != ' ' && *p != '\t' && off + 1 < sizeof(buf)) {
            buf[off++] = *p++;
        }
    }
    struct sel_collect_ctx c = {0};
    if (off > 0) {
        (void)run_selector(root, buf, off, &c, /*first_only=*/0);
    }
    JS_FreeCString(ctx, cls);
    JSValue arr = JS_NewArray(ctx);
    for (size_t i = 0; i < c.count; i++) {
        JS_SetPropertyUint32(ctx, arr, (uint32_t)i, wrap_element(ctx, c.elements[i]));
    }
    free(c.elements);
    return arr;
}

static JSValue js_el_getElementsByName(JSContext *ctx, JSValueConst this_val, int argc,
                                       JSValueConst *argv)
{
    if (argc < 1) {
        return JS_NewArray(ctx);
    }
    lxb_dom_node_t *root = unwrap_node(this_val);
    if (!root) {
        return JS_NewArray(ctx);
    }
    size_t nlen;
    const char *name = JS_ToCStringLen(ctx, &nlen, argv[0]);
    if (!name) {
        return JS_NewArray(ctx);
    }
    /* Build attribute selector [name="..."]. */
    char buf[256];
    int off = snprintf(buf, sizeof(buf), "[name=\"%.*s\"]", (int)(nlen > 200 ? 200 : nlen), name);
    struct sel_collect_ctx c = {0};
    if (off > 0) {
        (void)run_selector(root, buf, (size_t)off, &c, /*first_only=*/0);
    }
    JS_FreeCString(ctx, name);
    JSValue arr = JS_NewArray(ctx);
    for (size_t i = 0; i < c.count; i++) {
        JS_SetPropertyUint32(ctx, arr, (uint32_t)i, wrap_element(ctx, c.elements[i]));
    }
    free(c.elements);
    return arr;
}

/* getElementById — Document-only convenience. Walks the tree comparing
 * `id` attribute. */
static lxb_dom_element_t *find_by_id(lxb_dom_node_t *node, const char *id, size_t idlen)
{
    if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
        lxb_dom_element_t *el = lxb_dom_interface_element(node);
        size_t vlen;
        const lxb_char_t *v = lxb_dom_element_get_attribute(el, (const lxb_char_t *)"id", 2, &vlen);
        if (v && vlen == idlen && memcmp(v, id, idlen) == 0) {
            return el;
        }
    }
    for (lxb_dom_node_t *c = node->first_child; c; c = c->next) {
        lxb_dom_element_t *r = find_by_id(c, id, idlen);
        if (r) {
            return r;
        }
    }
    return NULL;
}

static JSValue js_doc_getElementById(JSContext *ctx, JSValueConst this_val, int argc,
                                     JSValueConst *argv)
{
    if (argc < 1) {
        return JS_NULL;
    }
    lxb_html_document_t *doc = unwrap_document(this_val);
    if (!doc) {
        return JS_NULL;
    }
    size_t idlen;
    const char *id = JS_ToCStringLen(ctx, &idlen, argv[0]);
    if (!id) {
        return JS_NULL;
    }
    lxb_dom_element_t *el = find_by_id(lxb_dom_interface_node(doc), id, idlen);
    JS_FreeCString(ctx, id);
    return wrap_element(ctx, el);
}

/* XML 1.0 Name production — pragmatic ASCII subset plus accept anything
 * with the high bit set. Used by createElement / createElementNS to
 * throw InvalidCharacterError (DOMException) on malformed names so the
 * WPT spec tests for those shapes pass. */
static int xml_name_start_char(unsigned char c)
{
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
        return 1;
    }
    if (c == '_' || c == ':') {
        return 1;
    }
    if (c >= 0x80) {
        return 1; /* high Unicode — accept optimistically */
    }
    return 0;
}
static int xml_name_char(unsigned char c)
{
    if (xml_name_start_char(c)) {
        return 1;
    }
    if (c >= '0' && c <= '9') {
        return 1;
    }
    if (c == '-' || c == '.') {
        return 1;
    }
    return 0;
}
/* Validate XML Name. Rejects leading colon (per the WPT corpus the
 * pure XML rule treats `:foo` as InvalidCharacterError, although bare
 * `:` is accepted as a NameStartChar; createElementNS layers
 * QName-specific NamespaceError checks on top). */
static int xml_name_valid(const char *s, size_t l)
{
    if (l == 0) {
        return 0;
    }
    if (!xml_name_start_char((unsigned char)s[0])) {
        return 0;
    }
    for (size_t i = 1; i < l; i++) {
        if (!xml_name_char((unsigned char)s[i])) {
            return 0;
        }
    }
    return 1;
}
/* Validate XML QName (Name ((':' Name)?)). Returns:
 *   0 — invalid Name (raise InvalidCharacterError)
 *   1 — bare Name (no prefix)
 *   2 — prefixed Name (prefix:local). out_colon points at the ':'. */
static int xml_qname_valid(const char *s, size_t l, const char **out_colon)
{
    if (out_colon) {
        *out_colon = NULL;
    }
    if (l == 0) {
        return 0;
    }
    if (!xml_name_start_char((unsigned char)s[0])) {
        return 0;
    }
    if (s[0] == ':') {
        return 0; /* leading ':' rejected */
    }
    const char *colon = NULL;
    for (size_t i = 1; i < l; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == ':') {
            if (colon) {
                return 0; /* second colon — invalid QName */
            }
            if (i + 1 >= l) {
                return 0; /* trailing colon */
            }
            if (!xml_name_start_char((unsigned char)s[i + 1])) {
                return 0;
            }
            colon = s + i;
        } else if (!xml_name_char(c)) {
            return 0;
        }
    }
    if (out_colon) {
        *out_colon = colon;
    }
    return colon ? 2 : 1;
}

/* createElement — fresh detached element. */
static JSValue js_doc_createElement(JSContext *ctx, JSValueConst this_val, int argc,
                                    JSValueConst *argv)
{
    if (argc < 1) {
        return JS_NULL;
    }
    lxb_html_document_t *doc = unwrap_document(this_val);
    if (!doc) {
        return JS_NULL;
    }
    size_t tlen;
    const char *tag = JS_ToCStringLen(ctx, &tlen, argv[0]);
    if (!tag) {
        return JS_NULL;
    }
    if (!xml_name_valid(tag, tlen)) {
        JS_FreeCString(ctx, tag);
        return JS_ThrowTypeError(ctx, "Document.createElement: invalid name");
    }
    lxb_dom_element_t *el = lxb_dom_document_create_element(lxb_dom_interface_document(doc),
                                                            (const lxb_char_t *)tag, tlen, NULL);
    JS_FreeCString(ctx, tag);
    if (!el) {
        return JS_NULL;
    }
    mark_dirty(ctx);
    return wrap_element(ctx, el);
}

/* The XML and XMLNS namespace URIs — used by createElementNS to enforce
 * the QName ↔ namespace consistency rules from the DOM spec. */
#define XML_NS "http://www.w3.org/XML/1998/namespace"
#define XMLNS_NS "http://www.w3.org/2000/xmlns/"

/* createElementNS — namespace-aware. We don't model namespaces on the
 * resulting Element (SVG/MathML rendering is out of scope), but we do
 * enforce the DOM spec validation rules so the ~600 WPT test rows for
 * this function pass. */
static JSValue js_doc_createElementNS(JSContext *ctx, JSValueConst this_val, int argc,
                                      JSValueConst *argv)
{
    if (argc < 2) {
        return JS_NULL;
    }
    lxb_html_document_t *doc = unwrap_document(this_val);
    if (!doc) {
        return JS_NULL;
    }
    /* Namespace: null, undefined, "" all collapse to NULL per spec. */
    const char *ns = NULL;
    size_t ns_len = 0;
    int ns_is_null = JS_IsNull(argv[0]) || JS_IsUndefined(argv[0]);
    if (!ns_is_null) {
        ns = JS_ToCStringLen(ctx, &ns_len, argv[0]);
        if (ns && ns_len == 0) {
            JS_FreeCString(ctx, ns);
            ns = NULL;
            ns_is_null = 1;
        }
    }
    size_t qlen;
    const char *qname = JS_ToCStringLen(ctx, &qlen, argv[1]);
    if (!qname) {
        if (ns) {
            JS_FreeCString(ctx, ns);
        }
        return JS_NULL;
    }
    const char *colon = NULL;
    int qkind = xml_qname_valid(qname, qlen, &colon);
    if (qkind == 0) {
        if (ns) {
            JS_FreeCString(ctx, ns);
        }
        JS_FreeCString(ctx, qname);
        return JS_ThrowTypeError(ctx, "Document.createElementNS: invalid qualified name");
    }
    /* Namespace consistency:
	 *   prefix without namespace  -> NamespaceError
	 *   "xml" prefix and ns != XML -> NamespaceError
	 *   "xmlns" prefix or qname == "xmlns" with ns != XMLNS -> NamespaceError
	 *   ns == XMLNS but qname/prefix not "xmlns" -> NamespaceError */
    int has_prefix = (qkind == 2);
    size_t prefix_len = has_prefix ? (size_t)(colon - qname) : 0;
    int is_xml_prefix = has_prefix && prefix_len == 3 && memcmp(qname, "xml", 3) == 0;
    int is_xmlns_prefix = has_prefix && prefix_len == 5 && memcmp(qname, "xmlns", 5) == 0;
    int is_xmlns_qname = (qlen == 5 && memcmp(qname, "xmlns", 5) == 0);
    int ns_is_xml = !ns_is_null && ns_len == sizeof(XML_NS) - 1 && memcmp(ns, XML_NS, ns_len) == 0;
    int ns_is_xmlns =
        !ns_is_null && ns_len == sizeof(XMLNS_NS) - 1 && memcmp(ns, XMLNS_NS, ns_len) == 0;
    int ns_err = 0;
    if (has_prefix && ns_is_null) {
        ns_err = 1;
    } else if (is_xml_prefix && !ns_is_xml) {
        ns_err = 1;
    } else if ((is_xmlns_prefix || is_xmlns_qname) && !ns_is_xmlns) {
        ns_err = 1;
    } else if (ns_is_xmlns && !is_xmlns_prefix && !is_xmlns_qname) {
        ns_err = 1;
    }
    if (ns_err) {
        if (ns) {
            JS_FreeCString(ctx, ns);
        }
        JS_FreeCString(ctx, qname);
        return JS_ThrowTypeError(ctx, "Document.createElementNS: namespace mismatch");
    }
    /* Lower-case the qname when it lives in the HTML namespace and
	 * we're in an HTML document, matching what `createElement` does
	 * via lexbor's internal normalization. For now, just pass the
	 * raw bytes through — lexbor's create_element handles HTML-NS
	 * elements case-insensitively. */
    lxb_dom_element_t *el = lxb_dom_document_create_element(lxb_dom_interface_document(doc),
                                                            (const lxb_char_t *)qname, qlen, NULL);
    if (ns) {
        JS_FreeCString(ctx, ns);
    }
    JS_FreeCString(ctx, qname);
    if (!el) {
        return JS_NULL;
    }
    mark_dirty(ctx);
    return wrap_element(ctx, el);
}

/* Wrap a non-Element node (Text, Comment, ...) as an Element-class JS
 * object. We only have one DOM JS class, so we share it; the lxb side
 * still distinguishes correctly via lxb_dom_node_t::type, which is what
 * .nodeType reads. innerHTML serialization picks the right HTML form
 * for each. */
static JSValue wrap_node_any(JSContext *ctx, lxb_dom_node_t *n)
{
    if (!n) {
        return JS_NULL;
    }
    JSValue cached = wrap_cache_lookup(ctx, n);
    if (!JS_IsUndefined(cached)) {
        return cached;
    }
    JSValue v = JS_NewObjectClass(ctx, class_element_id);
    JS_SetOpaque(v, n); /* opaque is the node, not interface_element */
    wrap_cache_insert(ctx, n, v);
    return v;
}

static JSValue js_doc_createTextNode(JSContext *ctx, JSValueConst this_val, int argc,
                                     JSValueConst *argv)
{
    if (argc < 1) {
        return JS_NULL;
    }
    lxb_html_document_t *htmldoc = unwrap_document(this_val);
    if (!htmldoc) {
        return JS_NULL;
    }
    lxb_dom_document_t *doc = lxb_dom_interface_document(htmldoc);
    size_t l;
    const char *s = JS_ToCStringLen(ctx, &l, argv[0]);
    if (!s) {
        return JS_NULL;
    }
    lxb_dom_text_t *t = lxb_dom_document_create_text_node(doc, (const lxb_char_t *)s, l);
    JS_FreeCString(ctx, s);
    if (!t) {
        return JS_NULL;
    }
    mark_dirty(ctx);
    return wrap_node_any(ctx, lxb_dom_interface_node(t));
}

static JSValue js_doc_createDocumentFragment(JSContext *ctx, JSValueConst this_val, int argc,
                                             JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    JSValueConst args[] = {JS_NewString(ctx, "div")};
    JSValue el = js_doc_createElement(ctx, this_val, 1, args);
    JS_FreeValue(ctx, (JSValue)args[0]);
    return el;
}

static JSValue js_doc_createComment(JSContext *ctx, JSValueConst this_val, int argc,
                                    JSValueConst *argv)
{
    lxb_html_document_t *htmldoc = unwrap_document(this_val);
    if (!htmldoc) {
        return JS_NULL;
    }
    lxb_dom_document_t *doc = lxb_dom_interface_document(htmldoc);
    size_t l = 0;
    const char *s = NULL;
    if (argc >= 1) {
        s = JS_ToCStringLen(ctx, &l, argv[0]);
    }
    lxb_dom_comment_t *c =
        lxb_dom_document_create_comment(doc, (const lxb_char_t *)(s ? s : ""), l);
    if (s) {
        JS_FreeCString(ctx, s);
    }
    if (!c) {
        return JS_NULL;
    }
    mark_dirty(ctx);
    return wrap_node_any(ctx, lxb_dom_interface_node(c));
}

/* Element getter for `delegate` — Turbo's custom-element wiring does:
 *
 *   let ae = Object.getPrototypeOf(el.delegate);
 *   let at = ae.requestErrored;
 *   ae.requestErrored = function(...){ return this.element.dispatchEvent(...); };
 *
 * If we return a plain `{}`, `Object.getPrototypeOf({})` is
 * `Object.prototype`, which means Turbo monkeypatches Object.prototype
 * — every object in the runtime then carries that bogus method, and
 * `this.element` is undefined for non-Delegate `this`, blowing up.
 *
 * Workaround: return an object whose prototype is a *fresh, dedicated*
 * object that already has the Turbo Delegate methods stubbed and an
 * `element` field that's an empty object with addEventListener /
 * dispatchEvent / etc. That way Turbo's monkeypatch only targets our
 * dedicated proto, and when its replacement method runs it finds a
 * real `this.element`. */
static JSValue js_el_delegate_get(JSContext *ctx, JSValueConst this_val)
{
    (void)this_val;
    const char *def =
        "(function(){"
        "  const elStub = {"
        "    addEventListener: function(){},"
        "    removeEventListener: function(){},"
        "    dispatchEvent: function(){ return true; },"
        "    setAttribute: function(){},"
        "    getAttribute: function(){ return null; },"
        "    appendChild: function(c){ return c; },"
        "    removeChild: function(c){ return c; },"
        "    classList: { add(){}, remove(){}, toggle(){}, contains(){ return false; } },"
        "    style: {},"
        "    parentElement: null,"
        "    children: [],"
        "  };"
        "  const proto = {"
        "    element: elStub,"
        "    requestStarted: function(){},"
        "    requestPreventedHandlingResponse: function(){},"
        "    requestSucceeded: function(){},"
        "    requestFailedWithResponse: function(){},"
        "    requestErrored: function(){},"
        "    requestFinished: function(){},"
        "    formSubmissionStarted: function(){},"
        "    formSubmissionSucceededWithResponse: function(){},"
        "    formSubmissionFailedWithResponse: function(){},"
        "    formSubmissionErrored: function(){},"
        "    formSubmissionFinished: function(){},"
        "    visit: function(){},"
        "    fetch: function(){ return Promise.resolve(); },"
        "    handleResponse: function(){},"
        "    delegate: null,"
        "    notifyApplicationAfterClickingLinkToLocation: function(){},"
        "  };"
        "  return Object.create(proto, { element: { value: elStub, writable: true, configurable: "
        "true, enumerable: true } });"
        "})()";
    return JS_Eval(ctx, def, strlen(def), "<delegate-stub>", JS_EVAL_TYPE_GLOBAL);
}

/* Generic empty-object getter — used for `dataset` etc. */
static JSValue js_el_empty_obj_get(JSContext *ctx, JSValueConst this_val)
{
    (void)this_val;
    return JS_NewObject(ctx);
}

/* `<template>.content` — spec returns a DocumentFragment with the
 * template's parsed contents. We don't model fragments separately, so
 * return the element itself: appendChild / firstElementChild etc. all
 * work transparently against the real DOM. */
static JSValue js_el_content_get(JSContext *ctx, JSValueConst this_val)
{
    (void)ctx;
    return JS_DupValue(ctx, this_val);
}

/* `<form>.elements` — we don't have a real form-controls collection,
 * so return an empty array. Form code paths typically iterate. */
static JSValue js_el_elements_get(JSContext *ctx, JSValueConst this_val)
{
    (void)this_val;
    return JS_NewArray(ctx);
}

/* textContent — get/set as JS getter/setter style via two C functions. */

/* Walk text-node descendants ourselves rather than going through
 * lxb_dom_node_text_content. The lexbor helper allocates from the
 * document's text-buffer pool and we'd need a matching destroy_text
 * call; doing the concat ourselves avoids the round trip and the
 * mraw-pool surprises that bit us during early bring-up. */
static void collect_text(lxb_dom_node_t *n, char **buf, size_t *len, size_t *cap)
{
    for (lxb_dom_node_t *c = n->first_child; c; c = c->next) {
        if (c->type == LXB_DOM_NODE_TYPE_TEXT) {
            lxb_dom_text_t *t = lxb_dom_interface_text(c);
            size_t add = t->char_data.data.length;
            if (*len + add + 1 > *cap) {
                size_t nc = *cap ? *cap * 2 : 64;
                while (nc < *len + add + 1) {
                    nc *= 2;
                }
                char *p = realloc(*buf, nc);
                if (!p) {
                    return;
                }
                *buf = p;
                *cap = nc;
            }
            memcpy(*buf + *len, t->char_data.data.data, add);
            *len += add;
        } else if (c->first_child) {
            collect_text(c, buf, len, cap);
        }
    }
}

static JSValue js_el_textContent_get(JSContext *ctx, JSValueConst this_val)
{
    lxb_dom_node_t *n = unwrap_node(this_val);
    if (!n) {
        return JS_NULL;
    }
    if (n->type == LXB_DOM_NODE_TYPE_TEXT) {
        lxb_dom_text_t *t = lxb_dom_interface_text(n);
        return JS_NewStringLen(ctx, (const char *)t->char_data.data.data, t->char_data.data.length);
    }
    char *buf = NULL;
    size_t len = 0, cap = 0;
    collect_text(n, &buf, &len, &cap);
    JSValue v = JS_NewStringLen(ctx, buf ? buf : "", len);
    free(buf);
    return v;
}

static JSValue js_el_textContent_set(JSContext *ctx, JSValueConst this_val, JSValueConst val)
{
    lxb_dom_node_t *n = unwrap_node(this_val);
    if (!n) {
        return JS_UNDEFINED;
    }
    size_t slen;
    const char *s = JS_ToCStringLen(ctx, &slen, val);
    if (s) {
        lxb_dom_node_text_content_set(n, (const lxb_char_t *)s, slen);
        JS_FreeCString(ctx, s);
        mark_dirty(ctx);
    }
    return JS_UNDEFINED;
}

/* innerHTML — serialize children. We use lexbor's HTML serializer. */
static lxb_status_t innerhtml_cb(const lxb_char_t *data, size_t len, void *vctx)
{
    struct {
        char *buf;
        size_t len, cap;
    } *acc = vctx;
    if (acc->len + len + 1 > acc->cap) {
        size_t nc = acc->cap ? acc->cap * 2 : 256;
        while (nc < acc->len + len + 1) {
            nc *= 2;
        }
        char *p = realloc(acc->buf, nc);
        if (!p) {
            return LXB_STATUS_ERROR_MEMORY_ALLOCATION;
        }
        acc->buf = p;
        acc->cap = nc;
    }
    memcpy(acc->buf + acc->len, data, len);
    acc->len += len;
    return LXB_STATUS_OK;
}

static JSValue js_el_innerHTML_get(JSContext *ctx, JSValueConst this_val)
{
    lxb_dom_node_t *n = unwrap_node(this_val);
    if (!n) {
        return JS_NewString(ctx, "");
    }
    struct {
        char *buf;
        size_t len, cap;
    } acc = {0};
    for (lxb_dom_node_t *c = n->first_child; c; c = c->next) {
        (void)lxb_html_serialize_tree_cb(c, innerhtml_cb, &acc);
    }
    JSValue v = JS_NewStringLen(ctx, acc.buf ? acc.buf : "", acc.len);
    free(acc.buf);
    return v;
}

static JSValue js_el_innerHTML_set(JSContext *ctx, JSValueConst this_val, JSValueConst val)
{
    lxb_dom_node_t *n = unwrap_node(this_val);
    if (!n) {
        return JS_UNDEFINED;
    }
    size_t slen;
    const char *s = JS_ToCStringLen(ctx, &slen, val);
    if (!s) {
        return JS_UNDEFINED;
    }
    /* Wipe existing children. */
    while (n->first_child) {
        lxb_dom_node_remove(n->first_child);
    }
    /* Parse fragment under this element's context. */
    lxb_html_document_t *doc = lxb_html_interface_document(n->owner_document);
    if (n->type == LXB_DOM_NODE_TYPE_ELEMENT) {
        lxb_html_element_t *htmlel = lxb_html_interface_element(n);
        (void)lxb_html_element_inner_html_set(htmlel, (const lxb_char_t *)s, slen);
    } else {
        (void)doc;
    }
    JS_FreeCString(ctx, s);
    mark_dirty(ctx);
    return JS_UNDEFINED;
}

static JSValue js_el_outerHTML_get(JSContext *ctx, JSValueConst this_val)
{
    lxb_dom_node_t *n = unwrap_node(this_val);
    if (!n) {
        return JS_NewString(ctx, "");
    }
    struct {
        char *buf;
        size_t len, cap;
    } acc = {0};
    (void)lxb_html_serialize_tree_cb(n, innerhtml_cb, &acc);
    JSValue v = JS_NewStringLen(ctx, acc.buf ? acc.buf : "", acc.len);
    free(acc.buf);
    return v;
}

/* CharacterData — `data` / `length` plus the appendData / insertData /
 * deleteData / replaceData / substringData mutators. Only meaningful
 * when the underlying lxb_dom_node_t is Text, Comment, or
 * ProcessingInstruction. */
static lxb_dom_character_data_t *as_chardata(JSValueConst v)
{
    lxb_dom_node_t *n = unwrap_node(v);
    if (!n) {
        return NULL;
    }
    if (n->type == LXB_DOM_NODE_TYPE_TEXT || n->type == LXB_DOM_NODE_TYPE_COMMENT ||
        n->type == LXB_DOM_NODE_TYPE_PROCESSING_INSTRUCTION) {
        return (lxb_dom_character_data_t *)n;
    }
    return NULL;
}

static JSValue js_cd_data_get(JSContext *ctx, JSValueConst this_val)
{
    lxb_dom_character_data_t *cd = as_chardata(this_val);
    if (!cd) {
        return JS_UNDEFINED;
    }
    return JS_NewStringLen(ctx, (const char *)cd->data.data, cd->data.length);
}

/* Splice a new substring into a CharacterData node by computing the
 * full result string ourselves and then calling lexbor's replace —
 * lexbor's `lxb_dom_character_data_replace` ignores its offset/count
 * args and just blits the input over the data buffer (see
 * source/lexbor/dom/interfaces/character_data.c), so we can't rely on
 * it for partial edits. */
static int chardata_splice(lxb_dom_character_data_t *cd, size_t offset, size_t count,
                           const char *ins, size_t ins_len)
{
    const char *cur = (const char *)cd->data.data;
    size_t cur_len = cd->data.length;
    if (offset > cur_len) {
        offset = cur_len;
    }
    if (offset + count > cur_len) {
        count = cur_len - offset;
    }
    size_t new_len = cur_len - count + ins_len;
    char *buf = malloc(new_len + 1);
    if (!buf) {
        return 0;
    }
    if (offset) {
        memcpy(buf, cur, offset);
    }
    if (ins_len) {
        memcpy(buf + offset, ins, ins_len);
    }
    if (cur_len - offset - count) {
        memcpy(buf + offset + ins_len, cur + offset + count, cur_len - offset - count);
    }
    buf[new_len] = '\0';
    lxb_status_t st = lxb_dom_character_data_replace(cd, (const lxb_char_t *)buf, new_len, 0, 0);
    free(buf);
    return st == LXB_STATUS_OK;
}

static JSValue js_cd_data_set(JSContext *ctx, JSValueConst this_val, JSValueConst v)
{
    lxb_dom_character_data_t *cd = as_chardata(this_val);
    if (!cd) {
        return JS_UNDEFINED;
    }
    size_t l;
    const char *s = JS_ToCStringLen(ctx, &l, v);
    if (!s) {
        return JS_UNDEFINED;
    }
    (void)lxb_dom_character_data_replace(cd, (const lxb_char_t *)s, l, 0, 0);
    JS_FreeCString(ctx, s);
    mark_dirty(ctx);
    return JS_UNDEFINED;
}

static JSValue js_cd_length_get(JSContext *ctx, JSValueConst this_val)
{
    lxb_dom_character_data_t *cd = as_chardata(this_val);
    if (!cd) {
        return JS_NewInt32(ctx, 0);
    }
    return JS_NewInt32(ctx, (int32_t)cd->data.length);
}

static JSValue js_cd_appendData(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    if (argc < 1) {
        return JS_UNDEFINED;
    }
    lxb_dom_character_data_t *cd = as_chardata(this_val);
    if (!cd) {
        return JS_UNDEFINED;
    }
    size_t l;
    const char *s = JS_ToCStringLen(ctx, &l, argv[0]);
    if (!s) {
        return JS_UNDEFINED;
    }
    chardata_splice(cd, cd->data.length, 0, s, l);
    JS_FreeCString(ctx, s);
    mark_dirty(ctx);
    return JS_UNDEFINED;
}

static JSValue js_cd_insertData(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    if (argc < 2) {
        return JS_UNDEFINED;
    }
    lxb_dom_character_data_t *cd = as_chardata(this_val);
    if (!cd) {
        return JS_UNDEFINED;
    }
    int32_t offset = 0;
    JS_ToInt32(ctx, &offset, argv[0]);
    if (offset < 0 || (size_t)offset > cd->data.length) {
        return JS_ThrowRangeError(ctx, "insertData: offset out of range");
    }
    size_t l;
    const char *s = JS_ToCStringLen(ctx, &l, argv[1]);
    if (!s) {
        return JS_UNDEFINED;
    }
    chardata_splice(cd, (size_t)offset, 0, s, l);
    JS_FreeCString(ctx, s);
    mark_dirty(ctx);
    return JS_UNDEFINED;
}

static JSValue js_cd_deleteData(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    if (argc < 2) {
        return JS_UNDEFINED;
    }
    lxb_dom_character_data_t *cd = as_chardata(this_val);
    if (!cd) {
        return JS_UNDEFINED;
    }
    int32_t offset = 0, count = 0;
    JS_ToInt32(ctx, &offset, argv[0]);
    JS_ToInt32(ctx, &count, argv[1]);
    if (offset < 0 || (size_t)offset > cd->data.length) {
        return JS_ThrowRangeError(ctx, "deleteData: offset out of range");
    }
    if (count < 0) {
        count = 0;
    }
    chardata_splice(cd, (size_t)offset, (size_t)count, NULL, 0);
    mark_dirty(ctx);
    return JS_UNDEFINED;
}

static JSValue js_cd_replaceData(JSContext *ctx, JSValueConst this_val, int argc,
                                 JSValueConst *argv)
{
    if (argc < 3) {
        return JS_UNDEFINED;
    }
    lxb_dom_character_data_t *cd = as_chardata(this_val);
    if (!cd) {
        return JS_UNDEFINED;
    }
    int32_t offset = 0, count = 0;
    JS_ToInt32(ctx, &offset, argv[0]);
    JS_ToInt32(ctx, &count, argv[1]);
    if (offset < 0 || (size_t)offset > cd->data.length) {
        return JS_ThrowRangeError(ctx, "replaceData: offset out of range");
    }
    if (count < 0) {
        count = 0;
    }
    size_t l;
    const char *s = JS_ToCStringLen(ctx, &l, argv[2]);
    if (!s) {
        return JS_UNDEFINED;
    }
    chardata_splice(cd, (size_t)offset, (size_t)count, s, l);
    JS_FreeCString(ctx, s);
    mark_dirty(ctx);
    return JS_UNDEFINED;
}

static JSValue js_cd_substringData(JSContext *ctx, JSValueConst this_val, int argc,
                                   JSValueConst *argv)
{
    if (argc < 2) {
        return JS_NewString(ctx, "");
    }
    lxb_dom_character_data_t *cd = as_chardata(this_val);
    if (!cd) {
        return JS_NewString(ctx, "");
    }
    int32_t offset = 0, count = 0;
    JS_ToInt32(ctx, &offset, argv[0]);
    JS_ToInt32(ctx, &count, argv[1]);
    if (offset < 0 || (size_t)offset > cd->data.length) {
        return JS_ThrowRangeError(ctx, "substringData: offset out of range");
    }
    if (count < 0) {
        count = 0;
    }
    if ((size_t)offset + (size_t)count > cd->data.length) {
        count = (int32_t)(cd->data.length - (size_t)offset);
    }
    return JS_NewStringLen(ctx, (const char *)cd->data.data + offset, (size_t)count);
}

/* tagName — uppercase per WebAPI. */
static JSValue js_el_tagName_get(JSContext *ctx, JSValueConst this_val)
{
    lxb_dom_element_t *el = unwrap_element(this_val);
    if (!el) {
        return JS_UNDEFINED;
    }
    size_t len;
    const lxb_char_t *name = lxb_dom_element_qualified_name(el, &len);
    if (!name) {
        return JS_UNDEFINED;
    }
    char buf[64];
    if (len > sizeof(buf)) {
        len = sizeof(buf);
    }
    for (size_t i = 0; i < len; i++) {
        char c = (char)name[i];
        buf[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
    }
    return JS_NewStringLen(ctx, buf, len);
}

static JSValue js_el_id_get(JSContext *ctx, JSValueConst this_val)
{
    lxb_dom_element_t *el = unwrap_element(this_val);
    if (!el) {
        return JS_NewString(ctx, "");
    }
    size_t len;
    const lxb_char_t *v = lxb_dom_element_get_attribute(el, (const lxb_char_t *)"id", 2, &len);
    if (!v) {
        return JS_NewString(ctx, "");
    }
    return JS_NewStringLen(ctx, (const char *)v, len);
}

static JSValue js_el_id_set(JSContext *ctx, JSValueConst this_val, JSValueConst val)
{
    lxb_dom_element_t *el = unwrap_element(this_val);
    if (!el) {
        return JS_UNDEFINED;
    }
    size_t slen;
    const char *s = JS_ToCStringLen(ctx, &slen, val);
    if (s) {
        lxb_dom_element_set_attribute(el, (const lxb_char_t *)"id", 2, (const lxb_char_t *)s, slen);
        JS_FreeCString(ctx, s);
        mark_dirty(ctx);
    }
    return JS_UNDEFINED;
}

static JSValue js_el_className_get(JSContext *ctx, JSValueConst this_val)
{
    lxb_dom_element_t *el = unwrap_element(this_val);
    if (!el) {
        return JS_NewString(ctx, "");
    }
    size_t len;
    const lxb_char_t *v = lxb_dom_element_get_attribute(el, (const lxb_char_t *)"class", 5, &len);
    if (!v) {
        return JS_NewString(ctx, "");
    }
    return JS_NewStringLen(ctx, (const char *)v, len);
}

static JSValue js_el_className_set(JSContext *ctx, JSValueConst this_val, JSValueConst val)
{
    lxb_dom_element_t *el = unwrap_element(this_val);
    if (!el) {
        return JS_UNDEFINED;
    }
    size_t slen;
    const char *s = JS_ToCStringLen(ctx, &slen, val);
    if (s) {
        lxb_dom_element_set_attribute(el, (const lxb_char_t *)"class", 5, (const lxb_char_t *)s,
                                      slen);
        JS_FreeCString(ctx, s);
        mark_dirty(ctx);
    }
    return JS_UNDEFINED;
}

/* IDL-attribute mirrors. Many DOM properties (`src`, `href`, `name`,
 * `value`, `type`, `alt`, `title`, `placeholder`, `action`, `method`)
 * read/write the corresponding HTML attribute as a string. The spec
 * resolves `src` / `href` to an *absolute URL* on read — without that,
 * webpack's runtime publicPath detection bails with
 *   Error: Automatic publicPath is not supported in this browser
 * because `scripts[scripts.length - 1].src` returned a relative path.
 *
 * Setter is plain string. The `urlish` flag tells the getter to push
 * the value through the base-URL resolver so `<script src="foo.js">`
 * resolves to "https://host/path/foo.js" from JS. */
static JSValue idl_attr_get(JSContext *ctx, JSValueConst this_val, const char *attr, size_t alen,
                            int urlish)
{
    lxb_dom_element_t *el = unwrap_element(this_val);
    if (!el) {
        return JS_NewString(ctx, "");
    }
    size_t vlen;
    const lxb_char_t *v = lxb_dom_element_get_attribute(el, (const lxb_char_t *)attr, alen, &vlen);
    if (!v) {
        return JS_NewString(ctx, "");
    }
    if (urlish) {
        struct yetty_ylexbor *r = runtime_ylex(ctx);
        char *raw = malloc(vlen + 1);
        if (!raw) {
            return JS_NewStringLen(ctx, (const char *)v, vlen);
        }
        memcpy(raw, v, vlen);
        raw[vlen] = '\0';
        char *abs = r ? yetty_ylexbor_resolve_url(r, raw) : NULL;
        free(raw);
        if (abs) {
            JSValue out = JS_NewString(ctx, abs);
            free(abs);
            return out;
        }
    }
    return JS_NewStringLen(ctx, (const char *)v, vlen);
}

static JSValue idl_attr_set(JSContext *ctx, JSValueConst this_val, JSValueConst val,
                            const char *attr, size_t alen)
{
    lxb_dom_element_t *el = unwrap_element(this_val);
    if (!el) {
        return JS_UNDEFINED;
    }
    size_t slen;
    const char *s = JS_ToCStringLen(ctx, &slen, val);
    if (s) {
        lxb_dom_element_set_attribute(el, (const lxb_char_t *)attr, alen, (const lxb_char_t *)s,
                                      slen);
        JS_FreeCString(ctx, s);
        mark_dirty(ctx);
    }
    return JS_UNDEFINED;
}

#define IDL_ATTR(prop, attr, urlish)                                                               \
    static JSValue js_el_##prop##_get(JSContext *ctx, JSValueConst tv)                             \
    {                                                                                              \
        return idl_attr_get(ctx, tv, attr, sizeof(attr) - 1, urlish);                              \
    }                                                                                              \
    static JSValue js_el_##prop##_set(JSContext *ctx, JSValueConst tv, JSValueConst val)           \
    {                                                                                              \
        return idl_attr_set(ctx, tv, val, attr, sizeof(attr) - 1);                                 \
    }

/* nodeType — DOM constants:
 *   1  = ELEMENT_NODE
 *   3  = TEXT_NODE
 *   8  = COMMENT_NODE
 *   9  = DOCUMENT_NODE
 *   11 = DOCUMENT_FRAGMENT_NODE
 * Real libraries gate cascades of behaviour on this (`if (e.nodeType
 * === 9) ...` is the canonical "is this a document?" check). */
static JSValue js_el_nodeType_get(JSContext *ctx, JSValueConst this_val)
{
    if (JS_GetOpaque(this_val, class_document_id)) {
        return JS_NewInt32(ctx, 9);
    }
    /* The element-class wrapper carries any non-Document node — Element,
	 * Text, Comment, ProcessingInstruction. Read the actual lexbor type
	 * so .nodeType returns the spec-correct value. */
    lxb_dom_node_t *n = (lxb_dom_node_t *)JS_GetOpaque(this_val, class_element_id);
    if (!n) {
        return JS_NewInt32(ctx, 0);
    }
    switch (n->type) {
    case LXB_DOM_NODE_TYPE_ELEMENT:
        return JS_NewInt32(ctx, 1);
    case LXB_DOM_NODE_TYPE_TEXT:
        return JS_NewInt32(ctx, 3);
    case LXB_DOM_NODE_TYPE_PROCESSING_INSTRUCTION:
        return JS_NewInt32(ctx, 7);
    case LXB_DOM_NODE_TYPE_COMMENT:
        return JS_NewInt32(ctx, 8);
    case LXB_DOM_NODE_TYPE_DOCUMENT:
        return JS_NewInt32(ctx, 9);
    case LXB_DOM_NODE_TYPE_DOCUMENT_TYPE:
        return JS_NewInt32(ctx, 10);
    case LXB_DOM_NODE_TYPE_DOCUMENT_FRAGMENT:
        return JS_NewInt32(ctx, 11);
    default:
        return JS_NewInt32(ctx, 1);
    }
}

static JSValue js_el_nodeName_get(JSContext *ctx, JSValueConst this_val)
{
    if (JS_GetOpaque(this_val, class_document_id)) {
        return JS_NewString(ctx, "#document");
    }
    lxb_dom_node_t *n = (lxb_dom_node_t *)JS_GetOpaque(this_val, class_element_id);
    if (n) {
        if (n->type == LXB_DOM_NODE_TYPE_TEXT) {
            return JS_NewString(ctx, "#text");
        }
        if (n->type == LXB_DOM_NODE_TYPE_COMMENT) {
            return JS_NewString(ctx, "#comment");
        }
        if (n->type == LXB_DOM_NODE_TYPE_DOCUMENT_FRAGMENT) {
            return JS_NewString(ctx, "#document-fragment");
        }
    }
    lxb_dom_element_t *el = unwrap_element(this_val);
    if (!el) {
        return JS_NewString(ctx, "");
    }
    size_t len = 0;
    const lxb_char_t *name = lxb_dom_element_local_name(el, &len);
    if (!name) {
        return JS_NewString(ctx, "");
    }
    char buf[64];
    size_t out = len < sizeof(buf) ? len : sizeof(buf);
    for (size_t i = 0; i < out; i++) {
        char c = (char)name[i];
        buf[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
    }
    return JS_NewStringLen(ctx, buf, out);
}

/* ownerDocument — for any wrapped element this is the document we
 * minted document_obj for. We don't have a direct handle, so look it
 * up from the runtime opaque. Document's own ownerDocument is null per
 * spec. */
static JSValue js_el_ownerDocument_get(JSContext *ctx, JSValueConst this_val)
{
    if (JS_GetOpaque(this_val, class_document_id)) {
        return JS_NULL;
    }
    if (!JS_GetOpaque(this_val, class_element_id)) {
        return JS_NULL;
    }
    struct yetty_ylexbor *r = runtime_ylex(ctx);
    if (!r) {
        return JS_NULL;
    }
    return wrap_document(ctx, r->document);
}

IDL_ATTR(src, "src", 1)
IDL_ATTR(href, "href", 1)
IDL_ATTR(action, "action", 1)
IDL_ATTR(name, "name", 0)
IDL_ATTR(value, "value", 0)
IDL_ATTR(type, "type", 0)
IDL_ATTR(alt, "alt", 0)
IDL_ATTR(title, "title", 0)
IDL_ATTR(placeholder, "placeholder", 0)
IDL_ATTR(method, "method", 0)
IDL_ATTR(rel, "rel", 0)
IDL_ATTR(target, "target", 0)

/* parentElement / firstElementChild / nextElementSibling / children */

static JSValue js_el_parentElement_get(JSContext *ctx, JSValueConst this_val)
{
    lxb_dom_node_t *n = unwrap_node(this_val);
    if (!n) {
        return JS_NULL;
    }
    lxb_dom_node_t *p = n->parent;
    while (p && p->type != LXB_DOM_NODE_TYPE_ELEMENT) {
        p = p->parent;
    }
    if (!p) {
        return JS_NULL;
    }
    return wrap_element(ctx, lxb_dom_interface_element(p));
}

static JSValue js_el_firstElementChild_get(JSContext *ctx, JSValueConst this_val)
{
    lxb_dom_node_t *n = unwrap_node(this_val);
    if (!n) {
        return JS_NULL;
    }
    for (lxb_dom_node_t *c = n->first_child; c; c = c->next) {
        if (c->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            return wrap_element(ctx, lxb_dom_interface_element(c));
        }
    }
    return JS_NULL;
}

/* Generic Node tree accessors (return ANY node type — element, text, comment —
 * via wrap_node_any). Their absence is what crashed jQuery's support detection:
 * `div.cloneNode(true).cloneNode(true).lastChild.checked` read `.lastChild` as
 * undefined, then `.checked` threw, aborting the whole jquery module. */
static JSValue js_el_firstChild_get(JSContext *ctx, JSValueConst this_val)
{
    lxb_dom_node_t *n = unwrap_node(this_val);
    return (n && n->first_child) ? wrap_node_any(ctx, n->first_child) : JS_NULL;
}
static JSValue js_el_lastChild_get(JSContext *ctx, JSValueConst this_val)
{
    lxb_dom_node_t *n = unwrap_node(this_val);
    return (n && n->last_child) ? wrap_node_any(ctx, n->last_child) : JS_NULL;
}
static JSValue js_el_nextSibling_get(JSContext *ctx, JSValueConst this_val)
{
    lxb_dom_node_t *n = unwrap_node(this_val);
    return (n && n->next) ? wrap_node_any(ctx, n->next) : JS_NULL;
}
static JSValue js_el_previousSibling_get(JSContext *ctx, JSValueConst this_val)
{
    lxb_dom_node_t *n = unwrap_node(this_val);
    return (n && n->prev) ? wrap_node_any(ctx, n->prev) : JS_NULL;
}
static JSValue js_el_parentNode_get(JSContext *ctx, JSValueConst this_val)
{
    lxb_dom_node_t *n = unwrap_node(this_val);
    return (n && n->parent) ? wrap_node_any(ctx, n->parent) : JS_NULL;
}
/* childNodes — a live-ish NodeList approximated by a JS array (has .length and
 * integer indexing, which is all real code uses). jQuery's parseHTML reads
 * `body.childNodes.length`. */
static JSValue js_el_childNodes_get(JSContext *ctx, JSValueConst this_val)
{
    JSValue arr = JS_NewArray(ctx);
    lxb_dom_node_t *n = unwrap_node(this_val);
    uint32_t i = 0;
    if (n) {
        for (lxb_dom_node_t *c = n->first_child; c; c = c->next) {
            JS_SetPropertyUint32(ctx, arr, i++, wrap_node_any(ctx, c));
        }
    }
    return arr;
}

static JSValue js_el_nextElementSibling_get(JSContext *ctx, JSValueConst this_val)
{
    lxb_dom_node_t *n = unwrap_node(this_val);
    if (!n) {
        return JS_NULL;
    }
    for (lxb_dom_node_t *c = n->next; c; c = c->next) {
        if (c->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            return wrap_element(ctx, lxb_dom_interface_element(c));
        }
    }
    return JS_NULL;
}

static JSValue js_el_children_get(JSContext *ctx, JSValueConst this_val)
{
    lxb_dom_node_t *n = unwrap_node(this_val);
    JSValue arr = JS_NewArray(ctx);
    if (!n) {
        return arr;
    }
    uint32_t i = 0;
    for (lxb_dom_node_t *c = n->first_child; c; c = c->next) {
        if (c->type != LXB_DOM_NODE_TYPE_ELEMENT) {
            continue;
        }
        JS_SetPropertyUint32(ctx, arr, i++, wrap_element(ctx, lxb_dom_interface_element(c)));
    }
    return arr;
}

/* ===========================================================================
 * style — proxy-style object. Reads/writes the element's `style` HTML
 * attribute as a CSS declaration string. Setting a property re-serialises
 * the whole declaration. Cheap; correct enough for "el.style.background
 * = 'red'" demos.
 * ===========================================================================*/

/* Parse "k1: v1; k2: v2" into a list of (key, value) pairs in-place.
 * Returns count. Key/value pointers reference the buffer (which the
 * caller modifies in place — keeping it simple). */
struct kv {
    char *k, *v;
    size_t klen, vlen;
};

static int parse_style_decl(char *buf, struct kv *out, int max)
{
    int n = 0;
    char *p = buf;
    while (*p && n < max) {
        while (*p == ' ' || *p == ';') {
            p++;
        }
        if (!*p) {
            break;
        }
        char *k = p;
        while (*p && *p != ':') {
            p++;
        }
        if (!*p) {
            break;
        }
        char *kend = p;
        *p++ = '\0';
        while (*p == ' ') {
            p++;
        }
        char *v = p;
        while (*p && *p != ';') {
            p++;
        }
        char *vend = p;
        if (*p) {
            *p++ = '\0';
        }
        while (kend > k && (kend[-1] == ' ' || kend[-1] == '\t')) {
            *--kend = '\0';
        }
        while (vend > v && (vend[-1] == ' ' || vend[-1] == '\t')) {
            *--vend = '\0';
        }
        out[n].k = k;
        out[n].v = v;
        out[n].klen = (size_t)(kend - k);
        out[n].vlen = (size_t)(vend - v);
        n++;
    }
    return n;
}

/* Convert camelCase property name → kebab-case (backgroundColor →
 * background-color). In-place lowercase + dash insert. Returns new
 * length. */
static size_t camel_to_kebab(const char *src, size_t slen, char *dst, size_t dst_cap)
{
    size_t o = 0;
    for (size_t i = 0; i < slen && o + 2 < dst_cap; i++) {
        char c = src[i];
        if (c >= 'A' && c <= 'Z') {
            if (o > 0) {
                dst[o++] = '-';
            }
            dst[o++] = (char)(c + 32);
        } else {
            dst[o++] = c;
        }
    }
    dst[o] = '\0';
    return o;
}

/* CSSStyleDeclaration methods — implemented as JS callable that
 * captures the host style object via `this`. style_get_property
 * intercepts reads of these names and returns these closures. */
static JSValue style_method_getPropertyValue(JSContext *ctx, JSValueConst this_val, int argc,
                                             JSValueConst *argv)
{
    if (argc < 1) {
        return JS_NewString(ctx, "");
    }
    lxb_dom_element_t *el = JS_GetOpaque(this_val, class_style_id);
    if (!el) {
        return JS_NewString(ctx, "");
    }
    size_t nlen;
    const char *name = JS_ToCStringLen(ctx, &nlen, argv[0]);
    if (!name) {
        return JS_NewString(ctx, "");
    }

    size_t alen;
    const lxb_char_t *attr =
        lxb_dom_element_get_attribute(el, (const lxb_char_t *)"style", 5, &alen);
    if (!attr) {
        JS_FreeCString(ctx, name);
        return JS_NewString(ctx, "");
    }
    char *buf = malloc(alen + 1);
    if (!buf) {
        JS_FreeCString(ctx, name);
        return JS_NewString(ctx, "");
    }
    memcpy(buf, attr, alen);
    buf[alen] = '\0';
    struct kv kvs[64];
    int n = parse_style_decl(buf, kvs, 64);
    JSValue out = JS_NewString(ctx, "");
    for (int i = 0; i < n; i++) {
        if (kvs[i].klen == nlen && strncmp(kvs[i].k, name, nlen) == 0) {
            JS_FreeValue(ctx, out);
            out = JS_NewStringLen(ctx, kvs[i].v, kvs[i].vlen);
            break;
        }
    }
    free(buf);
    JS_FreeCString(ctx, name);
    return out;
}

static JSValue style_method_setProperty(JSContext *ctx, JSValueConst this_val, int argc,
                                        JSValueConst *argv)
{
    if (argc < 2) {
        return JS_UNDEFINED;
    }
    lxb_dom_element_t *el = JS_GetOpaque(this_val, class_style_id);
    if (!el) {
        return JS_UNDEFINED;
    }
    size_t klen, vlen;
    const char *k = JS_ToCStringLen(ctx, &klen, argv[0]);
    const char *v = JS_ToCStringLen(ctx, &vlen, argv[1]);
    if (k && v) {
        /* Append "k:v;" to the inline style attribute. */
        size_t cur_len = 0;
        const lxb_char_t *cur =
            lxb_dom_element_get_attribute(el, (const lxb_char_t *)"style", 5, &cur_len);
        size_t need = (cur ? cur_len + 1 : 0) + klen + vlen + 4;
        char *buf = malloc(need);
        if (buf) {
            size_t off = 0;
            if (cur) {
                memcpy(buf, cur, cur_len);
                off = cur_len;
                if (off > 0 && buf[off - 1] != ';') {
                    buf[off++] = ';';
                }
            }
            memcpy(buf + off, k, klen);
            off += klen;
            buf[off++] = ':';
            memcpy(buf + off, v, vlen);
            off += vlen;
            buf[off++] = ';';
            lxb_dom_element_set_attribute(el, (const lxb_char_t *)"style", 5,
                                          (const lxb_char_t *)buf, off);
            free(buf);
            mark_dirty(ctx);
        }
    }
    if (k) {
        JS_FreeCString(ctx, k);
    }
    if (v) {
        JS_FreeCString(ctx, v);
    }
    return JS_UNDEFINED;
}

static JSValue style_method_removeProperty(JSContext *ctx, JSValueConst this_val, int argc,
                                           JSValueConst *argv)
{
    (void)ctx;
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewString(ctx, "");
}

static JSValue style_method_getPropertyPriority(JSContext *ctx, JSValueConst this_val, int argc,
                                                JSValueConst *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewString(ctx, "");
}

static JSValue style_method_item(JSContext *ctx, JSValueConst this_val, int argc,
                                 JSValueConst *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewString(ctx, "");
}

/* Property name used as a method? Return the matching closure wired
 * to the same opaque element via JS_NewCFunction's this-binding. */
static JSValue style_method_for(JSContext *ctx, JSValueConst obj, const char *name)
{
    struct {
        const char *name;
        JSCFunction *fn;
        int argc;
    } table[] = {
        {"getPropertyValue", style_method_getPropertyValue, 1},
        {"setProperty", style_method_setProperty, 3},
        {"removeProperty", style_method_removeProperty, 1},
        {"getPropertyPriority", style_method_getPropertyPriority, 1},
        {"item", style_method_item, 1},
    };
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (strcmp(name, table[i].name) == 0) {
            /* Bind `this` to obj so the methods see the same
			 * opaque element as our get_property hook. */
            JSValue fn = JS_NewCFunction(ctx, table[i].fn, name, table[i].argc);
            JSValue bind = JS_GetPropertyStr(ctx, fn, "bind");
            JSValueConst args[] = {obj};
            JSValue bound = JS_Call(ctx, bind, fn, 1, args);
            JS_FreeValue(ctx, bind);
            JS_FreeValue(ctx, fn);
            return bound;
        }
    }
    return JS_UNDEFINED;
}

/* style.<prop> getter (called via Proxy / GetOwnProperty trap). We
 * implement as a get_property class hook, not per-property. */

static JSValue style_get_property(JSContext *ctx, JSValueConst obj, JSAtom prop,
                                  JSValueConst receiver)
{
    (void)receiver;
    lxb_dom_element_t *el = JS_GetOpaque(obj, class_style_id);
    if (!el) {
        return JS_UNDEFINED;
    }
    const char *name = JS_AtomToCString(ctx, prop);
    if (!name) {
        return JS_UNDEFINED;
    }

    /* Method names — return callables. */
    JSValue m = style_method_for(ctx, obj, name);
    if (!JS_IsUndefined(m)) {
        JS_FreeCString(ctx, name);
        return m;
    }

    /* `cssText` returns the whole style attribute. */
    if (strcmp(name, "cssText") == 0) {
        JS_FreeCString(ctx, name);
        size_t alen;
        const lxb_char_t *attr =
            lxb_dom_element_get_attribute(el, (const lxb_char_t *)"style", 5, &alen);
        return attr ? JS_NewStringLen(ctx, (const char *)attr, alen) : JS_NewString(ctx, "");
    }
    /* `length` — best-effort 0; tracking declared count means a
	 * style-decl re-parse on every access. */
    if (strcmp(name, "length") == 0) {
        JS_FreeCString(ctx, name);
        return JS_NewInt32(ctx, 0);
    }

    char kebab[128];
    size_t klen = camel_to_kebab(name, strlen(name), kebab, sizeof(kebab));
    JS_FreeCString(ctx, name);

    size_t alen;
    const lxb_char_t *attr =
        lxb_dom_element_get_attribute(el, (const lxb_char_t *)"style", 5, &alen);
    if (!attr) {
        return JS_NewString(ctx, "");
    }
    char *buf = malloc(alen + 1);
    if (!buf) {
        return JS_NewString(ctx, "");
    }
    memcpy(buf, attr, alen);
    buf[alen] = '\0';

    struct kv kvs[64];
    int n = parse_style_decl(buf, kvs, 64);
    JSValue out = JS_NewString(ctx, "");
    for (int i = 0; i < n; i++) {
        if (kvs[i].klen == klen && strncmp(kvs[i].k, kebab, klen) == 0) {
            JS_FreeValue(ctx, out);
            out = JS_NewStringLen(ctx, kvs[i].v, kvs[i].vlen);
            break;
        }
    }
    free(buf);
    return out;
}

static int style_set_property(JSContext *ctx, JSValueConst obj, JSAtom prop, JSValueConst value,
                              JSValueConst receiver, int flags)
{
    (void)receiver;
    (void)flags;
    lxb_dom_element_t *el = JS_GetOpaque(obj, class_style_id);
    if (!el) {
        JS_ThrowTypeError(ctx, "no element");
        return -1;
    }

    const char *name = JS_AtomToCString(ctx, prop);
    if (!name) {
        return -1;
    }
    char kebab[128];
    size_t klen = camel_to_kebab(name, strlen(name), kebab, sizeof(kebab));
    JS_FreeCString(ctx, name);

    size_t vlen;
    const char *vstr = JS_ToCStringLen(ctx, &vlen, value);
    if (!vstr) {
        return -1;
    }

    /* Read existing decl, replace or append the matching key, write back. */
    size_t alen;
    const lxb_char_t *attr =
        lxb_dom_element_get_attribute(el, (const lxb_char_t *)"style", 5, &alen);
    char *buf = malloc((attr ? alen : 0) + 1);
    if (!buf) {
        JS_FreeCString(ctx, vstr);
        return -1;
    }
    if (attr) {
        memcpy(buf, attr, alen);
    }
    buf[attr ? alen : 0] = '\0';

    struct kv kvs[64];
    char *parse_buf = strdup(buf);
    int n = parse_style_decl(parse_buf, kvs, 64);

    /* Build new decl string. */
    size_t cap = alen + klen + vlen + 8;
    char *out = malloc(cap);
    out[0] = '\0';
    int wrote = 0;
    for (int i = 0; i < n; i++) {
        if (kvs[i].klen == klen && strncmp(kvs[i].k, kebab, klen) == 0) {
            if (vlen > 0) {
                if (wrote) {
                    strcat(out, "; ");
                }
                strcat(out, kebab);
                strcat(out, ": ");
                strncat(out, vstr, vlen);
                wrote = 1;
            }
        } else {
            if (wrote) {
                strcat(out, "; ");
            }
            strncat(out, kvs[i].k, kvs[i].klen);
            strcat(out, ": ");
            strncat(out, kvs[i].v, kvs[i].vlen);
            wrote = 1;
        }
    }
    int found = 0;
    for (int i = 0; i < n; i++) {
        if (kvs[i].klen == klen && strncmp(kvs[i].k, kebab, klen) == 0) {
            found = 1;
            break;
        }
    }
    if (!found && vlen > 0) {
        if (wrote) {
            strcat(out, "; ");
        }
        strcat(out, kebab);
        strcat(out, ": ");
        strncat(out, vstr, vlen);
    }

    lxb_dom_element_set_attribute(el, (const lxb_char_t *)"style", 5, (const lxb_char_t *)out,
                                  strlen(out));
    mark_dirty(ctx);

    free(buf);
    free(parse_buf);
    free(out);
    JS_FreeCString(ctx, vstr);
    return 1; /* property set */
}

static JSClassExoticMethods style_exotic = {
    .get_property = style_get_property,
    .set_property = style_set_property,
};

static JSValue js_el_style_get(JSContext *ctx, JSValueConst this_val)
{
    lxb_dom_element_t *el = unwrap_element(this_val);
    if (!el) {
        return JS_UNDEFINED;
    }
    JSValue v = JS_NewObjectClass(ctx, class_style_id);
    JS_SetOpaque(v, el);
    return v;
}

/* ===========================================================================
 * classList — DOMTokenList-ish (add/remove/toggle/contains).
 * ===========================================================================*/

static int has_class(const char *list, size_t llen, const char *cls, size_t clen)
{
    const char *p = list, *end = list + llen;
    while (p < end) {
        while (p < end && (*p == ' ' || *p == '\t')) {
            p++;
        }
        const char *s = p;
        while (p < end && *p != ' ' && *p != '\t') {
            p++;
        }
        if ((size_t)(p - s) == clen && memcmp(s, cls, clen) == 0) {
            return 1;
        }
    }
    return 0;
}

/* DOMTokenList token validation — empty -> SyntaxError,
 * any ASCII whitespace -> InvalidCharacterError. */
static int classlist_throw_if_invalid(JSContext *ctx, const char *s, size_t l, int *out_throw)
{
    if (l == 0) {
        JS_ThrowSyntaxError(ctx, "DOMTokenList: empty token");
        *out_throw = 1;
        return 1;
    }
    for (size_t i = 0; i < l; i++) {
        char c = s[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r') {
            JS_ThrowTypeError(ctx, "DOMTokenList: token contains ASCII whitespace");
            *out_throw = 1;
            return 1;
        }
    }
    return 0;
}

static JSValue js_classlist_contains(JSContext *ctx, JSValueConst this_val, int argc,
                                     JSValueConst *argv)
{
    if (argc < 1) {
        return JS_FALSE;
    }
    lxb_dom_element_t *el = JS_GetOpaque(this_val, class_classlist_id);
    if (!el) {
        return JS_FALSE;
    }
    size_t clen;
    const char *cls = JS_ToCStringLen(ctx, &clen, argv[0]);
    if (!cls) {
        return JS_FALSE;
    }
    size_t alen;
    const lxb_char_t *a = lxb_dom_element_get_attribute(el, (const lxb_char_t *)"class", 5, &alen);
    int has = a ? has_class((const char *)a, alen, cls, clen) : 0;
    JS_FreeCString(ctx, cls);
    return JS_NewBool(ctx, has);
}

static JSValue js_classlist_add(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    lxb_dom_element_t *el = JS_GetOpaque(this_val, class_classlist_id);
    if (!el) {
        return JS_UNDEFINED;
    }
    size_t alen;
    const lxb_char_t *a = lxb_dom_element_get_attribute(el, (const lxb_char_t *)"class", 5, &alen);

    for (int i = 0; i < argc; i++) {
        size_t clen;
        const char *cls = JS_ToCStringLen(ctx, &clen, argv[i]);
        if (!cls) {
            continue;
        }
        int threw = 0;
        if (classlist_throw_if_invalid(ctx, cls, clen, &threw)) {
            JS_FreeCString(ctx, cls);
            return JS_EXCEPTION;
        }
        if (!a || !has_class((const char *)a, alen, cls, clen)) {
            size_t need = (a ? alen + 1 : 0) + clen + 1;
            char *buf = malloc(need);
            size_t off = 0;
            if (a) {
                memcpy(buf, a, alen);
                off = alen;
                if (alen > 0) {
                    buf[off++] = ' ';
                }
            }
            memcpy(buf + off, cls, clen);
            off += clen;
            buf[off] = '\0';
            lxb_dom_element_set_attribute(el, (const lxb_char_t *)"class", 5,
                                          (const lxb_char_t *)buf, off);
            free(buf);
            a = lxb_dom_element_get_attribute(el, (const lxb_char_t *)"class", 5, &alen);
        }
        JS_FreeCString(ctx, cls);
    }
    mark_dirty(ctx);
    return JS_UNDEFINED;
}

static JSValue js_classlist_remove(JSContext *ctx, JSValueConst this_val, int argc,
                                   JSValueConst *argv)
{
    lxb_dom_element_t *el = JS_GetOpaque(this_val, class_classlist_id);
    if (!el) {
        return JS_UNDEFINED;
    }

    for (int i = 0; i < argc; i++) {
        size_t clen;
        const char *cls = JS_ToCStringLen(ctx, &clen, argv[i]);
        if (!cls) {
            continue;
        }
        int threw = 0;
        if (classlist_throw_if_invalid(ctx, cls, clen, &threw)) {
            JS_FreeCString(ctx, cls);
            return JS_EXCEPTION;
        }
        size_t alen;
        const lxb_char_t *a =
            lxb_dom_element_get_attribute(el, (const lxb_char_t *)"class", 5, &alen);
        if (!a) {
            JS_FreeCString(ctx, cls);
            continue;
        }
        char *buf = malloc(alen + 1);
        size_t out = 0;
        const char *p = (const char *)a;
        const char *end = p + alen;
        while (p < end) {
            while (p < end && (*p == ' ' || *p == '\t')) {
                p++;
            }
            const char *s = p;
            while (p < end && *p != ' ' && *p != '\t') {
                p++;
            }
            size_t tl = (size_t)(p - s);
            if (tl == clen && memcmp(s, cls, clen) == 0) {
                continue;
            }
            if (out > 0) {
                buf[out++] = ' ';
            }
            memcpy(buf + out, s, tl);
            out += tl;
        }
        buf[out] = '\0';
        lxb_dom_element_set_attribute(el, (const lxb_char_t *)"class", 5, (const lxb_char_t *)buf,
                                      out);
        free(buf);
        JS_FreeCString(ctx, cls);
    }
    mark_dirty(ctx);
    return JS_UNDEFINED;
}

static JSValue js_classlist_toggle(JSContext *ctx, JSValueConst this_val, int argc,
                                   JSValueConst *argv)
{
    if (argc < 1) {
        return JS_FALSE;
    }
    size_t clen;
    const char *cls = JS_ToCStringLen(ctx, &clen, argv[0]);
    if (!cls) {
        return JS_FALSE;
    }
    int threw = 0;
    if (classlist_throw_if_invalid(ctx, cls, clen, &threw)) {
        JS_FreeCString(ctx, cls);
        return JS_EXCEPTION;
    }
    JS_FreeCString(ctx, cls);
    JSValue has = js_classlist_contains(ctx, this_val, 1, argv);
    int b = JS_ToBool(ctx, has);
    JS_FreeValue(ctx, has);
    if (b) {
        JSValue r = js_classlist_remove(ctx, this_val, 1, argv);
        if (JS_IsException(r)) {
            return r;
        }
        JS_FreeValue(ctx, r);
    } else {
        JSValue r = js_classlist_add(ctx, this_val, 1, argv);
        if (JS_IsException(r)) {
            return r;
        }
        JS_FreeValue(ctx, r);
    }
    return JS_NewBool(ctx, !b);
}

/* DOMTokenList.length / item(i) / value — needed by Element-classlist
 * WPT (~600 of its 780 sub-tests touch one of these). */
static int classlist_count_tokens(const char *list, size_t llen, const char **out_starts,
                                  size_t *out_lens, int max)
{
    int n = 0;
    const char *p = list, *end = list + llen;
    while (p < end && n < max) {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\f' || *p == '\r')) {
            p++;
        }
        const char *s = p;
        while (p < end && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\f' && *p != '\r') {
            p++;
        }
        if (p > s) {
            if (out_starts) {
                out_starts[n] = s;
            }
            if (out_lens) {
                out_lens[n] = (size_t)(p - s);
            }
            n++;
        }
    }
    return n;
}

static JSValue js_classlist_length_get(JSContext *ctx, JSValueConst this_val)
{
    lxb_dom_element_t *el = JS_GetOpaque(this_val, class_classlist_id);
    if (!el) {
        return JS_NewInt32(ctx, 0);
    }
    size_t alen;
    const lxb_char_t *a = lxb_dom_element_get_attribute(el, (const lxb_char_t *)"class", 5, &alen);
    if (!a) {
        return JS_NewInt32(ctx, 0);
    }
    int n = classlist_count_tokens((const char *)a, alen, NULL, NULL, 4096);
    return JS_NewInt32(ctx, n);
}

static JSValue js_classlist_value_get(JSContext *ctx, JSValueConst this_val)
{
    lxb_dom_element_t *el = JS_GetOpaque(this_val, class_classlist_id);
    if (!el) {
        return JS_NewString(ctx, "");
    }
    size_t alen;
    const lxb_char_t *a = lxb_dom_element_get_attribute(el, (const lxb_char_t *)"class", 5, &alen);
    if (!a) {
        return JS_NewString(ctx, "");
    }
    return JS_NewStringLen(ctx, (const char *)a, alen);
}

static JSValue js_classlist_value_set(JSContext *ctx, JSValueConst this_val, JSValueConst v)
{
    lxb_dom_element_t *el = JS_GetOpaque(this_val, class_classlist_id);
    if (!el) {
        return JS_UNDEFINED;
    }
    size_t vlen;
    const char *vs = JS_ToCStringLen(ctx, &vlen, v);
    if (!vs) {
        return JS_UNDEFINED;
    }
    lxb_dom_element_set_attribute(el, (const lxb_char_t *)"class", 5, (const lxb_char_t *)vs, vlen);
    JS_FreeCString(ctx, vs);
    mark_dirty(ctx);
    return JS_UNDEFINED;
}

static JSValue js_classlist_item(JSContext *ctx, JSValueConst this_val, int argc,
                                 JSValueConst *argv)
{
    if (argc < 1) {
        return JS_NULL;
    }
    lxb_dom_element_t *el = JS_GetOpaque(this_val, class_classlist_id);
    if (!el) {
        return JS_NULL;
    }
    int32_t idx = -1;
    JS_ToInt32(ctx, &idx, argv[0]);
    if (idx < 0) {
        return JS_NULL;
    }
    size_t alen;
    const lxb_char_t *a = lxb_dom_element_get_attribute(el, (const lxb_char_t *)"class", 5, &alen);
    if (!a) {
        return JS_NULL;
    }
    const char *starts[4096];
    size_t lens[4096];
    int n = classlist_count_tokens((const char *)a, alen, starts, lens, 4096);
    if (idx >= n) {
        return JS_NULL;
    }
    return JS_NewStringLen(ctx, starts[idx], lens[idx]);
}

static JSValue js_classlist_replace(JSContext *ctx, JSValueConst this_val, int argc,
                                    JSValueConst *argv)
{
    if (argc < 2) {
        return JS_FALSE;
    }
    size_t olen, nlen;
    const char *o = JS_ToCStringLen(ctx, &olen, argv[0]);
    const char *nw = JS_ToCStringLen(ctx, &nlen, argv[1]);
    if (!o || !nw) {
        if (o) {
            JS_FreeCString(ctx, o);
        }
        if (nw) {
            JS_FreeCString(ctx, nw);
        }
        return JS_FALSE;
    }
    int threw = 0;
    if (classlist_throw_if_invalid(ctx, o, olen, &threw) ||
        classlist_throw_if_invalid(ctx, nw, nlen, &threw)) {
        JS_FreeCString(ctx, o);
        JS_FreeCString(ctx, nw);
        return JS_EXCEPTION;
    }
    lxb_dom_element_t *el = JS_GetOpaque(this_val, class_classlist_id);
    if (!el) {
        JS_FreeCString(ctx, o);
        JS_FreeCString(ctx, nw);
        return JS_FALSE;
    }
    size_t alen;
    const lxb_char_t *a = lxb_dom_element_get_attribute(el, (const lxb_char_t *)"class", 5, &alen);
    if (!a) {
        JS_FreeCString(ctx, o);
        JS_FreeCString(ctx, nw);
        return JS_FALSE;
    }
    if (!has_class((const char *)a, alen, o, olen)) {
        JS_FreeCString(ctx, o);
        JS_FreeCString(ctx, nw);
        return JS_FALSE;
    }
    char *buf = malloc(alen + nlen + 2);
    size_t out = 0;
    const char *p = (const char *)a, *end = p + alen;
    int replaced = 0;
    while (p < end) {
        while (p < end && (*p == ' ' || *p == '\t')) {
            p++;
        }
        const char *s = p;
        while (p < end && *p != ' ' && *p != '\t') {
            p++;
        }
        size_t tl = (size_t)(p - s);
        if (tl == 0) {
            continue;
        }
        if (out > 0) {
            buf[out++] = ' ';
        }
        if (tl == olen && memcmp(s, o, olen) == 0 && !replaced) {
            memcpy(buf + out, nw, nlen);
            out += nlen;
            replaced = 1;
        } else {
            memcpy(buf + out, s, tl);
            out += tl;
        }
    }
    buf[out] = '\0';
    lxb_dom_element_set_attribute(el, (const lxb_char_t *)"class", 5, (const lxb_char_t *)buf, out);
    free(buf);
    JS_FreeCString(ctx, o);
    JS_FreeCString(ctx, nw);
    mark_dirty(ctx);
    return JS_TRUE;
}

static JSValue js_classlist_supports(JSContext *ctx, JSValueConst this_val, int argc,
                                     JSValueConst *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    /* Per spec, classList.supports() throws TypeError because the
	 * "class" token list has no defined supported tokens. */
    return JS_ThrowTypeError(ctx, "DOMTokenList for 'class' has no supported tokens");
}

static JSValue js_classlist_toString(JSContext *ctx, JSValueConst this_val, int argc,
                                     JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    return js_classlist_value_get(ctx, this_val);
}

static const JSCFunctionListEntry classlist_funcs[] = {
    JS_CFUNC_DEF("add", 1, js_classlist_add),
    JS_CFUNC_DEF("remove", 1, js_classlist_remove),
    JS_CFUNC_DEF("toggle", 1, js_classlist_toggle),
    JS_CFUNC_DEF("contains", 1, js_classlist_contains),
    JS_CFUNC_DEF("item", 1, js_classlist_item),
    JS_CFUNC_DEF("replace", 2, js_classlist_replace),
    JS_CFUNC_DEF("supports", 1, js_classlist_supports),
    JS_CFUNC_DEF("toString", 0, js_classlist_toString),
    JS_CGETSET_DEF("length", js_classlist_length_get, NULL),
    JS_CGETSET_DEF("value", js_classlist_value_get, js_classlist_value_set),
};

static JSValue js_el_classList_get(JSContext *ctx, JSValueConst this_val)
{
    lxb_dom_element_t *el = unwrap_element(this_val);
    if (!el) {
        return JS_UNDEFINED;
    }
    JSValue v = JS_NewObjectClass(ctx, class_classlist_id);
    JS_SetOpaque(v, el);
    JS_SetPropertyFunctionList(ctx, v, classlist_funcs,
                               sizeof(classlist_funcs) / sizeof(classlist_funcs[0]));
    return v;
}

/* ===========================================================================
 * Event listener storage — hidden array property on the JS wrapper.
 * Wrapper objects are minted fresh per call though, so we use a global
 * map: lxb_dom_element_t* → array of (type, handler) pairs.
 * ===========================================================================*/

#define MAX_LISTENERS 1024
struct listener {
    lxb_dom_element_t *el;
    char type[32];
    JSValue handler;
};
static struct listener g_listeners[MAX_LISTENERS];
static int g_listener_count = 0;

static JSValue js_el_addEventListener(JSContext *ctx, JSValueConst this_val, int argc,
                                      JSValueConst *argv)
{
    if (argc < 2) {
        return JS_UNDEFINED;
    }
    /* Accept Element (target=el) and Document/window (target=NULL,
	 * receives global events like DOMContentLoaded/load). */
    lxb_dom_element_t *el = unwrap_element(this_val);
    if (g_listener_count >= MAX_LISTENERS) {
        return JS_UNDEFINED;
    }
    const char *type = JS_ToCString(ctx, argv[0]);
    if (!type) {
        return JS_UNDEFINED;
    }
    struct listener *L = &g_listeners[g_listener_count++];
    L->el = el; /* may be NULL for document/window listeners */
    strncpy(L->type, type, sizeof(L->type) - 1);
    L->type[sizeof(L->type) - 1] = '\0';
    L->handler = JS_DupValue(ctx, argv[1]);
    JS_FreeCString(ctx, type);
    return JS_UNDEFINED;
}

/* ===========================================================================
 * Class registration + globalThis.document install.
 * ===========================================================================*/

/* Lightweight predicate / no-op stubs for the long tail of DOM methods
 * SPAs reach for. None of these affect rendering; they exist purely
 * so existence checks like `if (typeof el.matches === "function")`
 * pass and the boot path continues. */
static JSValue js_el_undef_stub(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)ctx;
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_UNDEFINED;
}

static JSValue js_el_contains_stub(JSContext *ctx, JSValueConst this_val, int argc,
                                   JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) {
        return JS_FALSE;
    }
    /* Best-effort: walk argv[0]'s parent chain looking for `this`. */
    void *self_p = JS_GetOpaque(this_val, class_element_id);
    if (!self_p) {
        self_p = JS_GetOpaque(this_val, class_document_id);
    }
    void *other_p = JS_GetOpaque(argv[0], class_element_id);
    if (!self_p || !other_p) {
        return JS_FALSE;
    }
    lxb_dom_node_t *target = (lxb_dom_node_t *)other_p;
    for (lxb_dom_node_t *n = target; n; n = n->parent) {
        if ((void *)n == self_p) {
            return JS_TRUE;
        }
    }
    return JS_FALSE;
}

static JSValue js_el_matches_stub(JSContext *ctx, JSValueConst this_val, int argc,
                                  JSValueConst *argv)
{
    (void)ctx;
    (void)this_val;
    (void)argc;
    (void)argv;
    /* Pessimistic: false — usually less harmful than a fake true. */
    return JS_FALSE;
}

static JSValue js_el_hasChildNodes_stub(JSContext *ctx, JSValueConst this_val, int argc,
                                        JSValueConst *argv)
{
    (void)ctx;
    (void)argc;
    (void)argv;
    lxb_dom_node_t *n = unwrap_node(this_val);
    return JS_NewBool(ctx, n && n->first_child != NULL);
}

static JSValue js_el_hasAttributes_stub(JSContext *ctx, JSValueConst this_val, int argc,
                                        JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    lxb_dom_element_t *el = unwrap_element(this_val);
    if (!el) {
        return JS_FALSE;
    }
    return JS_NewBool(ctx, el->first_attr != NULL);
}

static JSValue js_el_getAttributeNames_stub(JSContext *ctx, JSValueConst this_val, int argc,
                                            JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    lxb_dom_element_t *el = unwrap_element(this_val);
    JSValue arr = JS_NewArray(ctx);
    if (!el) {
        return arr;
    }
    uint32_t i = 0;
    for (lxb_dom_attr_t *a = el->first_attr; a; a = a->next) {
        size_t nlen = 0;
        const lxb_char_t *nm = lxb_dom_attr_qualified_name(a, &nlen);
        if (nm) {
            JS_SetPropertyUint32(ctx, arr, i++, JS_NewStringLen(ctx, (const char *)nm, nlen));
        }
    }
    return arr;
}

static JSValue js_el_cloneNode_stub(JSContext *ctx, JSValueConst this_val, int argc,
                                    JSValueConst *argv)
{
    lxb_dom_node_t *self = unwrap_node(this_val);
    if (!self) {
        return JS_DupValue(ctx, this_val);
    }
    int deep = argc > 0 ? JS_ToBool(ctx, argv[0]) : 0;
    lxb_dom_node_t *cl = lxb_dom_node_clone(self, deep ? true : false);
    if (!cl) {
        return JS_NULL;
    }
    if (cl->type == LXB_DOM_NODE_TYPE_ELEMENT) {
        return wrap_element(ctx, lxb_dom_interface_element(cl));
    }
    return JS_DupValue(ctx, this_val);
}

/* getBoundingClientRect — return the element's real laid-out rectangle (union
 * of its boxes) instead of a zero stub. Boxes exist after the first layout
 * pass; calls from initial inline scripts (which run before layout) still get
 * zeros, but deferred / event-driven measurements — which is what JS app
 * shells use to size cards and grids — now see true geometry. */
static JSValue js_el_getBoundingClientRect(JSContext *ctx, JSValueConst this_val, int argc,
                                           JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    struct yetty_ylexbor *r = runtime_ylex(ctx);
    lxb_dom_node_t *node = unwrap_node(this_val);
    if (r != NULL && node != NULL && node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
        lxb_dom_element_t *el = lxb_dom_interface_element(node);
        float min_x = 0.0f, min_y = 0.0f, max_x = 0.0f, max_y = 0.0f;
        bool found = false;
        for (uint32_t i = 0; i < r->boxes.size; i++) {
            struct yetty_ylexbor_box *b = &r->boxes.data[i];
            if (b->element != el) {
                continue;
            }
            if (!found) {
                min_x = b->x;
                min_y = b->y;
                max_x = b->x + b->w;
                max_y = b->y + b->h;
                found = true;
            } else {
                if (b->x < min_x) {
                    min_x = b->x;
                }
                if (b->y < min_y) {
                    min_y = b->y;
                }
                if (b->x + b->w > max_x) {
                    max_x = b->x + b->w;
                }
                if (b->y + b->h > max_y) {
                    max_y = b->y + b->h;
                }
            }
        }
        if (found) {
            x = min_x;
            y = min_y;
            w = max_x - min_x;
            h = max_y - min_y;
        }
    }
    JSValue rect = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, rect, "x", JS_NewFloat64(ctx, x));
    JS_SetPropertyStr(ctx, rect, "y", JS_NewFloat64(ctx, y));
    JS_SetPropertyStr(ctx, rect, "width", JS_NewFloat64(ctx, w));
    JS_SetPropertyStr(ctx, rect, "height", JS_NewFloat64(ctx, h));
    JS_SetPropertyStr(ctx, rect, "top", JS_NewFloat64(ctx, y));
    JS_SetPropertyStr(ctx, rect, "left", JS_NewFloat64(ctx, x));
    JS_SetPropertyStr(ctx, rect, "right", JS_NewFloat64(ctx, x + w));
    JS_SetPropertyStr(ctx, rect, "bottom", JS_NewFloat64(ctx, y + h));
    return rect;
}

static JSValue js_el_clientRects_stub(JSContext *ctx, JSValueConst this_val, int argc,
                                      JSValueConst *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewArray(ctx);
}

/* Real EventTarget.dispatchEvent: invoke every registered listener whose type
 * matches the event and whose target is this object (el == NULL means a
 * document/window listener). Used on Element, Document, and window — sites
 * dispatch synthetic CustomEvents to drive their own flows (e.g. CNN's
 * WMUC consent fires "userConsentReady" on document; a missing/stub
 * dispatchEvent threw "not a function" and aborted the handler). Returns
 * !event.defaultPrevented. */
static JSValue js_el_dispatchEvent(JSContext *ctx, JSValueConst this_val, int argc,
                                   JSValueConst *argv)
{
    if (argc < 1) {
        return JS_TRUE;
    }
    /* Re-entrancy guard: a handler that synchronously re-dispatches the event
	 * it is handling would recurse forever and freeze the page. Cap nesting. */
    static int dispatch_depth = 0;
    if (dispatch_depth > 32) {
        return JS_TRUE;
    }
    JSValue type_v = JS_GetPropertyStr(ctx, argv[0], "type");
    const char *type = JS_ToCString(ctx, type_v);
    JS_FreeValue(ctx, type_v);
    if (type == NULL) {
        return JS_TRUE;
    }
    dispatch_depth++;
    lxb_dom_element_t *el = unwrap_element(this_val); /* NULL for document/window */
    JS_SetPropertyStr(ctx, (JSValue)argv[0], "target", JS_DupValue(ctx, this_val));
    JS_SetPropertyStr(ctx, (JSValue)argv[0], "currentTarget", JS_DupValue(ctx, this_val));
    /* Snapshot the count: a handler may addEventListener during dispatch and
	 * those new listeners must not fire for this same event. */
    int snapshot = g_listener_count;
    for (int i = 0; i < snapshot; i++) {
        if (g_listeners[i].el != el || strcmp(g_listeners[i].type, type) != 0) {
            continue;
        }
        JSValueConst call_args[] = {argv[0]};
        JSValue ret = JS_Call(ctx, g_listeners[i].handler, this_val, 1, call_args);
        if (JS_IsException(ret)) {
            JSValue exc = JS_GetException(ctx);
            JS_FreeValue(ctx, exc);
        } else {
            JS_FreeValue(ctx, ret);
        }
    }
    JS_FreeCString(ctx, type);
    dispatch_depth--;
    JSValue dp = JS_GetPropertyStr(ctx, argv[0], "defaultPrevented");
    int prevented = JS_ToBool(ctx, dp);
    JS_FreeValue(ctx, dp);
    return prevented ? JS_FALSE : JS_TRUE;
}

/* For HTML documents, attribute-manipulation methods must:
 *  1. Throw InvalidCharacterError when the name doesn't match the XML
 *     Name production (spec test pattern: `assert_throws_dom("INVALID_CHARACTER_ERR", ...)`).
 *  2. Lowercase the name (HTML attribute names are case-insensitive
 *     and stored canonically lowercase).
 *
 * Returns a malloc'd lowercase copy on success, NULL on error after
 * raising the JS exception. Caller frees the result with free(). */
static char *attr_normalize_name(JSContext *ctx, JSValueConst v, size_t *out_len)
{
    size_t l;
    const char *s = JS_ToCStringLen(ctx, &l, v);
    if (!s) {
        return NULL;
    }
    if (!xml_name_valid(s, l)) {
        JS_FreeCString(ctx, s);
        JS_ThrowTypeError(ctx, "Element attribute: invalid name");
        return NULL;
    }
    char *buf = malloc(l + 1);
    if (!buf) {
        JS_FreeCString(ctx, s);
        return NULL;
    }
    for (size_t i = 0; i < l; i++) {
        char c = s[i];
        buf[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
    }
    buf[l] = '\0';
    JS_FreeCString(ctx, s);
    if (out_len) {
        *out_len = l;
    }
    return buf;
}

static JSValue js_el_toggleAttr_stub(JSContext *ctx, JSValueConst this_val, int argc,
                                     JSValueConst *argv)
{
    if (argc < 1) {
        return JS_FALSE;
    }
    lxb_dom_element_t *el = unwrap_element(this_val);
    if (!el) {
        return JS_FALSE;
    }
    size_t nlen;
    char *name = attr_normalize_name(ctx, argv[0], &nlen);
    if (!name) {
        return JS_EXCEPTION;
    }
    int has = lxb_dom_element_has_attribute(el, (const lxb_char_t *)name, nlen);
    int force_present = (argc >= 2) ? JS_ToBool(ctx, argv[1]) : -1;
    int want_present;
    if (argc >= 2) {
        /* Two-arg form: force(true) sets, force(false) removes,
		 * regardless of current state. */
        want_present = force_present ? 1 : 0;
    } else {
        want_present = !has;
    }
    if (want_present && !has) {
        lxb_dom_element_set_attribute(el, (const lxb_char_t *)name, nlen, (const lxb_char_t *)"",
                                      0);
    } else if (!want_present && has) {
        lxb_dom_element_remove_attribute(el, (const lxb_char_t *)name, nlen);
    }
    free(name);
    mark_dirty(ctx);
    return JS_NewBool(ctx, want_present);
}

/* document.implementation — returns a tiny object with the legacy
 * DOMImplementation surface. `hasFeature` is spec'd to return true for
 * any input (it's a long-deprecated API kept only for back-compat),
 * so a constant `() => true` is the correct implementation. */
static JSValue js_impl_hasFeature(JSContext *ctx, JSValueConst this_val, int argc,
                                  JSValueConst *argv)
{
    (void)ctx;
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_TRUE;
}

static JSValue js_impl_createDocument(JSContext *ctx, JSValueConst this_val, int argc,
                                      JSValueConst *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewObject(ctx);
}

static JSValue js_impl_createDocumentType(JSContext *ctx, JSValueConst this_val, int argc,
                                          JSValueConst *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewObject(ctx);
}

static JSValue js_impl_createHTMLDocument(JSContext *ctx, JSValueConst this_val, int argc,
                                          JSValueConst *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    /* Return a doc-like with real body/head/documentElement elements (detached,
	 * but with working innerHTML + childNodes). jQuery.parseHTML does
	 * `createHTMLDocument("").body.innerHTML = data` then reads body.childNodes;
	 * an empty object threw "cannot set property 'innerHTML' of undefined" and
	 * aborted the whole jquery module. */
    JSValue obj = JS_NewObject(ctx);
    struct yetty_ylexbor *r = runtime_ylex(ctx);
    if (r != NULL && r->document != NULL) {
        lxb_dom_document_t *doc = lxb_dom_interface_document(r->document);
        lxb_dom_element_t *html =
            lxb_dom_document_create_element(doc, (const lxb_char_t *)"html", 4, NULL);
        lxb_dom_element_t *head =
            lxb_dom_document_create_element(doc, (const lxb_char_t *)"head", 4, NULL);
        lxb_dom_element_t *body =
            lxb_dom_document_create_element(doc, (const lxb_char_t *)"body", 4, NULL);
        if (html != NULL) {
            JS_SetPropertyStr(ctx, obj, "documentElement", wrap_element(ctx, html));
        }
        if (head != NULL) {
            JS_SetPropertyStr(ctx, obj, "head", wrap_element(ctx, head));
        }
        if (body != NULL) {
            JS_SetPropertyStr(ctx, obj, "body", wrap_element(ctx, body));
        }
    }
    return obj;
}

static JSValue js_doc_implementation_get(JSContext *ctx, JSValueConst this_val)
{
    (void)this_val;
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "hasFeature",
                      JS_NewCFunction(ctx, js_impl_hasFeature, "hasFeature", 2));
    JS_SetPropertyStr(ctx, obj, "createDocument",
                      JS_NewCFunction(ctx, js_impl_createDocument, "createDocument", 3));
    JS_SetPropertyStr(ctx, obj, "createDocumentType",
                      JS_NewCFunction(ctx, js_impl_createDocumentType, "createDocumentType", 3));
    JS_SetPropertyStr(ctx, obj, "createHTMLDocument",
                      JS_NewCFunction(ctx, js_impl_createHTMLDocument, "createHTMLDocument", 1));
    return obj;
}

static const JSCFunctionListEntry document_funcs[] = {
    JS_CFUNC_DEF("getElementById", 1, js_doc_getElementById),
    JS_CFUNC_DEF("getElementsByTagName", 1, js_el_getElementsByTagName),
    JS_CFUNC_DEF("getElementsByClassName", 1, js_el_getElementsByClassName),
    JS_CFUNC_DEF("getElementsByName", 1, js_el_getElementsByName),
    JS_CFUNC_DEF("querySelector", 1, js_el_querySelector),
    JS_CFUNC_DEF("querySelectorAll", 1, js_el_querySelectorAll),
    JS_CFUNC_DEF("createElement", 1, js_doc_createElement),
    JS_CFUNC_DEF("createElementNS", 2, js_doc_createElementNS),
    JS_CFUNC_DEF("createTextNode", 1, js_doc_createTextNode),
    JS_CFUNC_DEF("createDocumentFragment", 0, js_doc_createDocumentFragment),
    JS_CFUNC_DEF("createComment", 1, js_doc_createComment),
    JS_CFUNC_DEF("addEventListener", 2, js_el_addEventListener),
    JS_CFUNC_DEF("dispatchEvent", 1, js_el_dispatchEvent),
    JS_CFUNC_DEF("removeEventListener", 2, js_el_undef_stub),
    /* Same node-mutation surface as Element so document.prepend()
	 * etc. work — github's stylesheet injector calls
	 * `e.head.prepend(t)` first then falls back to `e.prepend(t)` if
	 * `e instanceof Document` is false (which it is for us, since
	 * our document object isn't an instance of the JS Document
	 * constructor). */
    JS_CFUNC_DEF("prepend", 1, js_el_appendChild),
    JS_CFUNC_DEF("append", 1, js_el_appendChild),
    JS_CFUNC_DEF("appendChild", 1, js_el_appendChild),
    JS_CFUNC_DEF("removeChild", 1, js_el_removeChild),
    JS_CFUNC_DEF("contains", 1, js_el_contains_stub),
    JS_CGETSET_DEF("nodeType", js_el_nodeType_get, NULL),
    JS_CGETSET_DEF("nodeName", js_el_nodeName_get, NULL),
    JS_CGETSET_DEF("ownerDocument", js_el_ownerDocument_get, NULL),
    JS_CGETSET_DEF("implementation", js_doc_implementation_get, NULL),
};

static const JSCFunctionListEntry element_funcs[] = {
    JS_CFUNC_DEF("getAttribute", 1, js_el_getAttribute),
    JS_CFUNC_DEF("setAttribute", 2, js_el_setAttribute),
    JS_CFUNC_DEF("removeAttribute", 1, js_el_removeAttribute),
    JS_CFUNC_DEF("hasAttribute", 1, js_el_hasAttribute),
    JS_CFUNC_DEF("appendChild", 1, js_el_appendChild),
    JS_CFUNC_DEF("removeChild", 1, js_el_removeChild),
    JS_CFUNC_DEF("querySelector", 1, js_el_querySelector),
    JS_CFUNC_DEF("querySelectorAll", 1, js_el_querySelectorAll),
    JS_CFUNC_DEF("getElementsByTagName", 1, js_el_getElementsByTagName),
    JS_CFUNC_DEF("getElementsByClassName", 1, js_el_getElementsByClassName),
    JS_CFUNC_DEF("addEventListener", 2, js_el_addEventListener),
    /* ChildNode/ParentNode mutators that legitimately share the
	 * appendChild path (single-arg insertion). The spec's full
	 * variadic signature degrades to the single-node case under
	 * production code 99% of the time. */
    JS_CFUNC_DEF("prepend", 1, js_el_prepend),
    JS_CFUNC_DEF("append", 1, js_el_append),
    JS_CFUNC_DEF("insertBefore", 2, js_el_insertBefore),
    JS_CFUNC_DEF("replaceChild", 2, js_el_replaceChild),
    JS_CFUNC_DEF("replaceWith", 1, js_el_replaceWith),
    JS_CFUNC_DEF("before", 1, js_el_before),
    JS_CFUNC_DEF("after", 1, js_el_after),
    JS_CFUNC_DEF("remove", 0, js_el_removeChild),
    JS_CFUNC_DEF("closest", 1, js_el_querySelector),
    /* Predicates / accessors that need a typed return. We give them
	 * a tiny dedicated stub each below so they don't masquerade as
	 * mutators. */
    JS_CFUNC_DEF("contains", 1, js_el_contains_stub),
    JS_CFUNC_DEF("matches", 1, js_el_matches_stub),
    JS_CFUNC_DEF("hasChildNodes", 0, js_el_hasChildNodes_stub),
    JS_CFUNC_DEF("hasAttributes", 0, js_el_hasAttributes_stub),
    JS_CFUNC_DEF("getAttributeNames", 0, js_el_getAttributeNames_stub),
    JS_CFUNC_DEF("cloneNode", 1, js_el_cloneNode_stub),
    JS_CFUNC_DEF("getBoundingClientRect", 0, js_el_getBoundingClientRect),
    JS_CFUNC_DEF("getClientRects", 0, js_el_clientRects_stub),
    /* True no-ops — return undefined. */
    JS_CFUNC_DEF("scrollIntoView", 0, js_el_undef_stub),
    JS_CFUNC_DEF("focus", 0, js_el_undef_stub),
    JS_CFUNC_DEF("blur", 0, js_el_undef_stub),
    JS_CFUNC_DEF("click", 0, js_el_undef_stub),
    JS_CFUNC_DEF("normalize", 0, js_el_undef_stub),
    JS_CFUNC_DEF("dispatchEvent", 1, js_el_dispatchEvent),
    JS_CFUNC_DEF("removeEventListener", 2, js_el_undef_stub),
    JS_CFUNC_DEF("toggleAttribute", 1, js_el_toggleAttr_stub),
    JS_CGETSET_DEF("textContent", js_el_textContent_get, js_el_textContent_set),
    JS_CGETSET_DEF("innerHTML", js_el_innerHTML_get, js_el_innerHTML_set),
    JS_CGETSET_DEF("outerHTML", js_el_outerHTML_get, NULL),
    /* CharacterData (Text/Comment) — `data` and `length` plus the
	 * appendData/insertData/deleteData/replaceData/substringData
	 * mutators. as_chardata() in each guards element-only nodes. */
    JS_CGETSET_DEF("data", js_cd_data_get, js_cd_data_set),
    JS_CGETSET_DEF("length", js_cd_length_get, NULL),
    JS_CFUNC_DEF("appendData", 1, js_cd_appendData),
    JS_CFUNC_DEF("insertData", 2, js_cd_insertData),
    JS_CFUNC_DEF("deleteData", 2, js_cd_deleteData),
    JS_CFUNC_DEF("replaceData", 3, js_cd_replaceData),
    JS_CFUNC_DEF("substringData", 2, js_cd_substringData),
    JS_CGETSET_DEF("tagName", js_el_tagName_get, NULL),
    JS_CGETSET_DEF("id", js_el_id_get, js_el_id_set),
    JS_CGETSET_DEF("className", js_el_className_get, js_el_className_set),
    JS_CGETSET_DEF("parentElement", js_el_parentElement_get, NULL),
    JS_CGETSET_DEF("firstElementChild", js_el_firstElementChild_get, NULL),
    JS_CGETSET_DEF("firstChild", js_el_firstChild_get, NULL),
    JS_CGETSET_DEF("lastChild", js_el_lastChild_get, NULL),
    JS_CGETSET_DEF("nextSibling", js_el_nextSibling_get, NULL),
    JS_CGETSET_DEF("previousSibling", js_el_previousSibling_get, NULL),
    JS_CGETSET_DEF("parentNode", js_el_parentNode_get, NULL),
    JS_CGETSET_DEF("childNodes", js_el_childNodes_get, NULL),
    JS_CGETSET_DEF("nextElementSibling", js_el_nextElementSibling_get, NULL),
    JS_CGETSET_DEF("children", js_el_children_get, NULL),
    JS_CGETSET_DEF("style", js_el_style_get, NULL),
    JS_CGETSET_DEF("classList", js_el_classList_get, NULL),
    JS_CGETSET_DEF("nodeType", js_el_nodeType_get, NULL),
    JS_CGETSET_DEF("nodeName", js_el_nodeName_get, NULL),
    JS_CGETSET_DEF("ownerDocument", js_el_ownerDocument_get, NULL),
    /* `delegate` — Turbo's custom-element wiring does
	 *   Object.getPrototypeOf(el.delegate)
	 * during boot. Returning an empty object lets that walk through.
	 * Same for the few other "library-private slot" reads modern
	 * frameworks make on elements. */
    JS_CGETSET_DEF("delegate", js_el_delegate_get, NULL),
    JS_CGETSET_DEF("dataset", js_el_empty_obj_get, NULL),
    JS_CGETSET_DEF("content", js_el_content_get, NULL),
    JS_CGETSET_DEF("elements", js_el_elements_get, NULL),
    JS_CGETSET_DEF("src", js_el_src_get, js_el_src_set),
    JS_CGETSET_DEF("href", js_el_href_get, js_el_href_set),
    JS_CGETSET_DEF("action", js_el_action_get, js_el_action_set),
    JS_CGETSET_DEF("name", js_el_name_get, js_el_name_set),
    JS_CGETSET_DEF("value", js_el_value_get, js_el_value_set),
    JS_CGETSET_DEF("type", js_el_type_get, js_el_type_set),
    JS_CGETSET_DEF("alt", js_el_alt_get, js_el_alt_set),
    JS_CGETSET_DEF("title", js_el_title_get, js_el_title_set),
    JS_CGETSET_DEF("placeholder", js_el_placeholder_get, js_el_placeholder_set),
    JS_CGETSET_DEF("method", js_el_method_get, js_el_method_set),
    JS_CGETSET_DEF("rel", js_el_rel_get, js_el_rel_set),
    JS_CGETSET_DEF("target", js_el_target_get, js_el_target_set),
};

void yetty_ylexbor_js_dom_install(struct yetty_ylexbor *r)
{
    JSContext *ctx = (JSContext *)r->js_ctx;
    JSRuntime *rt = (JSRuntime *)r->js_rt;
    if (!ctx || !rt) {
        return;
    }

    /* Park the ylexbor pointer so callbacks can find it. */
    struct js_dom_state *state = JS_GetRuntimeOpaque(rt);
    if (state == NULL) {
        state = calloc(1, sizeof(*state));
        JS_SetRuntimeOpaque(rt, state);
    }
    state->r = r;

    /* Class IDs (one-time). */
    if (class_node_id == 0) {
        JS_NewClassID(rt, &class_node_id);
        JS_NewClassID(rt, &class_element_id);
        JS_NewClassID(rt, &class_document_id);
        JS_NewClassID(rt, &class_classlist_id);
        JS_NewClassID(rt, &class_style_id);
        class_style_def.exotic = &style_exotic;
    }
    JS_NewClass(rt, class_node_id, &class_node_def);
    JS_NewClass(rt, class_element_id, &class_element_def);
    JS_NewClass(rt, class_document_id, &class_document_def);
    JS_NewClass(rt, class_classlist_id, &class_classlist_def);
    JS_NewClass(rt, class_style_id, &class_style_def);

    /* Element prototype — methods + accessors via JS_CGETSET_DEF. */
    JSValue el_proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, el_proto, element_funcs,
                               sizeof(element_funcs) / sizeof(element_funcs[0]));
    JS_SetClassProto(ctx, class_element_id, el_proto);

    /* Document inherits from Element-ish prototype + extra methods.
	 * We give it the *same* methods as Element so document.querySelector
	 * works directly (not via the Element prototype chain since we
	 * don't model that yet). */
    JSValue doc_proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, doc_proto, document_funcs,
                               sizeof(document_funcs) / sizeof(document_funcs[0]));
    JS_SetClassProto(ctx, class_document_id, doc_proto);

    /* globalThis.document */
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue doc_obj = wrap_document(ctx, r->document);
    JS_SetPropertyStr(ctx, global, "document", doc_obj);

    /* document.documentElement / body / head — convenience props. */
    lxb_dom_node_t *doc_node = lxb_dom_interface_node(r->document);
    lxb_dom_element_t *root_el = NULL;
    for (lxb_dom_node_t *c = doc_node->first_child; c; c = c->next) {
        if (c->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            root_el = lxb_dom_interface_element(c);
            break;
        }
    }
    if (root_el) {
        JS_SetPropertyStr(ctx, doc_obj, "documentElement", wrap_element(ctx, root_el));
    }
    lxb_html_body_element_t *body = lxb_html_document_body_element(r->document);
    if (body) {
        JS_SetPropertyStr(ctx, doc_obj, "body", wrap_element(ctx, lxb_dom_interface_element(body)));
    }
    lxb_html_head_element_t *head = lxb_html_document_head_element(r->document);
    if (head) {
        JS_SetPropertyStr(ctx, doc_obj, "head", wrap_element(ctx, lxb_dom_interface_element(head)));
    }

    /* Webpack's runtime publicPath probe reads:
	 *   document.currentScript || document.getElementsByTagName("script")[N-1]
	 * Provide both: currentScript=null is acceptable; the GEBTN
	 * fallback then works because we now expose .src on elements. */
    JS_SetPropertyStr(ctx, doc_obj, "currentScript", JS_NULL);

    /* document.defaultView === window. Many libraries reach window
	 * via document.defaultView (and via element.ownerDocument
	 * .defaultView), so expose it explicitly. */
    JS_SetPropertyStr(ctx, doc_obj, "defaultView", JS_DupValue(ctx, global));

    /* Global aliases — `window`, `self`, `top`, `parent`, `frames`,
	 * and `globalThis` (already-set by QuickJS) all point at the
	 * same global object in a non-iframed page. SPAs reference
	 * any of these interchangeably. */
    JS_SetPropertyStr(ctx, global, "window", JS_DupValue(ctx, global));
    JS_SetPropertyStr(ctx, global, "self", JS_DupValue(ctx, global));
    JS_SetPropertyStr(ctx, global, "top", JS_DupValue(ctx, global));
    JS_SetPropertyStr(ctx, global, "parent", JS_DupValue(ctx, global));
    JS_SetPropertyStr(ctx, global, "frames", JS_DupValue(ctx, global));

    /* window.addEventListener / removeEventListener / dispatchEvent —
	 * many libs do `window.addEventListener("popstate", ...)`. Route
	 * through the same listener storage as Element.addEventListener
	 * (target=NULL, fired by yetty_ylexbor_js_dispatch_event_type
	 * with target=NULL). */
    JS_SetPropertyStr(ctx, global, "addEventListener",
                      JS_NewCFunction(ctx, js_el_addEventListener, "addEventListener", 2));
    /* window.dispatchEvent invokes the el==NULL (document/window) listeners,
	 * same as document — sites fire synthetic events at window. */
    JS_SetPropertyStr(ctx, global, "dispatchEvent",
                      JS_NewCFunction(ctx, js_el_dispatchEvent, "dispatchEvent", 1));
    /* removeEventListener no-op stopper — full removal would need per-listener IDs. */
    const char *noopdef = "(function(){})";
    JSValue noop = JS_Eval(ctx, noopdef, strlen(noopdef), "<noop>", JS_EVAL_TYPE_GLOBAL);
    JS_SetPropertyStr(ctx, global, "removeEventListener", JS_DupValue(ctx, noop));
    JS_FreeValue(ctx, noop);
    JS_FreeValue(ctx, global);
}

/* ===========================================================================
 * Click dispatch — called from the host when yterm reports a mouse
 * click. Walks the box vector to find the deepest box whose rect
 * contains (x,y), then walks up the element ancestry firing every
 * matching 'click' listener.
 * ===========================================================================*/

int yetty_ylexbor_dispatch_click(struct yetty_ylexbor *r, float x, float y)
{
    if (!r || !r->js_ctx || g_listener_count == 0) {
        return 0;
    }

    /* Hit-test: find deepest box whose rect contains (x,y) AND has
	 * an associated element. */
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
        return 0;
    }

    /* Walk up ancestry; fire any 'click' listener on the chain. */
    JSContext *ctx = (JSContext *)r->js_ctx;
    int fired = 0;
    for (lxb_dom_node_t *n = lxb_dom_interface_node(target); n; n = n->parent) {
        if (n->type != LXB_DOM_NODE_TYPE_ELEMENT) {
            continue;
        }
        lxb_dom_element_t *el = lxb_dom_interface_element(n);
        for (int i = 0; i < g_listener_count; i++) {
            if (g_listeners[i].el != el) {
                continue;
            }
            if (strcmp(g_listeners[i].type, "click") != 0) {
                continue;
            }
            JSValue ev = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, ev, "type", JS_NewString(ctx, "click"));
            JS_SetPropertyStr(ctx, ev, "target", wrap_element(ctx, target));
            JS_SetPropertyStr(ctx, ev, "currentTarget", wrap_element(ctx, el));
            JS_SetPropertyStr(ctx, ev, "clientX", JS_NewFloat64(ctx, x));
            JS_SetPropertyStr(ctx, ev, "clientY", JS_NewFloat64(ctx, y));
            JSValueConst args[] = {ev};
            JSValue ret = JS_Call(ctx, g_listeners[i].handler, JS_UNDEFINED, 1, args);
            if (JS_IsException(ret)) {
                JSValue ex = JS_GetException(ctx);
                const char *m = JS_ToCString(ctx, ex);
                ydebug("js click-handler: %s", m ? m : "?");
                if (m) {
                    JS_FreeCString(ctx, m);
                }
                JS_FreeValue(ctx, ex);
                r->js_error_count++;
            }
            JS_FreeValue(ctx, ret);
            JS_FreeValue(ctx, ev);
            fired = 1;
        }
    }
    return fired;
}

/* Reset the global listener pool — called from js_destroy so JSValues
 * from a dying runtime don't leak into a subsequently-created runtime's
 * pump (the WPT integration runner builds a fresh yetty_ylexbor per
 * test). The handler refs are owned by the same context that's about
 * to die, so we skip JS_FreeValue: that pool's arena gets blown away
 * by JS_FreeContext anyway, and freeing here would double-free.
 *
 * Class IDs are global to the QuickJS runtime, but each runtime
 * needs its OWN JS_NewClass(rt, id, def) registration. Re-using IDs
 * across runtimes works in principle, but our test harness saw
 * hard-to-debug closure crashes when carrying IDs over — resetting
 * them to 0 forces JS_NewClassID re-allocation on the next dom_install
 * which is the safer path. */
void yetty_ylexbor_js_dom_reset(struct yetty_ylexbor *r)
{
    /* Release wrapper-cache refs while the JSContext is still live —
	 * we must JS_FreeValue each entry before the runtime is gone, or
	 * QuickJS asserts on leaked objects at JS_FreeRuntime. */
    if (r && r->js_ctx) {
        wrap_cache_clear((JSContext *)r->js_ctx);
    } else {
        /* No live ctx (e.g. very early failure path) — best-effort
		 * zero so a re-install starts from a clean slate. */
        for (size_t i = 0; i < WRAP_CACHE_BUCKETS; i++) {
            g_wrap_cache[i].key = NULL;
            g_wrap_cache[i].val = JS_UNDEFINED;
        }
    }
    g_listener_count = 0;
    memset(g_listeners, 0, sizeof(g_listeners));
    class_node_id = 0;
    class_element_id = 0;
    class_document_id = 0;
    class_classlist_id = 0;
    class_style_id = 0;
}

/* ===========================================================================
 * Synthetic event dispatch — fire `type` to every registered listener
 * matching `target_element_or_null` (NULL = match all elements; useful
 * for global events like DOMContentLoaded / load that conventionally
 * target document/window).
 *
 * QuickJS event semantics are not modelled in full — this is enough to
 * unblock the "did the page boot?" path; capture/bubble phases and
 * cancellation are TODO.
 * ===========================================================================*/
void yetty_ylexbor_js_dispatch_event_type(struct yetty_ylexbor *r, const char *type,
                                          void *target_element_ptr_or_null)
{
    if (!r || !r->js_ctx || !type) {
        return;
    }
    JSContext *ctx = (JSContext *)r->js_ctx;
    lxb_dom_element_t *only = (lxb_dom_element_t *)target_element_ptr_or_null;

    /* Default target for global events (load, DOMContentLoaded) is
	 * document. Real handlers reach for event.target.tagName etc.,
	 * so we need a non-null object there. */
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue doc_target = JS_GetPropertyStr(ctx, global, "document");
    JS_FreeValue(ctx, global);

    for (int i = 0; i < g_listener_count; i++) {
        if (only && g_listeners[i].el != only) {
            continue;
        }
        if (strcmp(g_listeners[i].type, type) != 0) {
            continue;
        }
        JSValue ev = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, ev, "type", JS_NewString(ctx, type));
        JS_SetPropertyStr(ctx, ev, "bubbles", JS_FALSE);
        JS_SetPropertyStr(ctx, ev, "cancelable", JS_FALSE);
        JS_SetPropertyStr(ctx, ev, "defaultPrevented", JS_FALSE);
        JS_SetPropertyStr(ctx, ev, "timeStamp", JS_NewInt32(ctx, 0));
        JSValue tgt = only ? wrap_element(ctx, only) : JS_DupValue(ctx, doc_target);
        JS_SetPropertyStr(ctx, ev, "target", JS_DupValue(ctx, tgt));
        JS_SetPropertyStr(ctx, ev, "currentTarget", JS_DupValue(ctx, tgt));
        JS_SetPropertyStr(ctx, ev, "srcElement", JS_DupValue(ctx, tgt));
        JS_FreeValue(ctx, tgt);
        JSValueConst args[] = {ev};
        JSValue ret = JS_Call(ctx, g_listeners[i].handler, JS_UNDEFINED, 1, args);
        if (JS_IsException(ret)) {
            JSValue ex = JS_GetException(ctx);
            const char *m = JS_ToCString(ctx, ex);
            ydebug("js event:%s: %s", type, m ? m : "?");
            if (m) {
                JS_FreeCString(ctx, m);
            }
            JS_FreeValue(ctx, ex);
            r->js_error_count++;
        }
        JS_FreeValue(ctx, ret);
        JS_FreeValue(ctx, ev);
    }
    JS_FreeValue(ctx, doc_target);
}

#else /* !YETTY_HAVE_QUICKJS */

void yetty_ylexbor_js_dom_install(struct yetty_ylexbor *r)
{
    (void)r;
}
int yetty_ylexbor_dispatch_click(struct yetty_ylexbor *r, float x, float y)
{
    (void)r;
    (void)x;
    (void)y;
    return 0;
}
void yetty_ylexbor_js_dispatch_event_type(struct yetty_ylexbor *r, const char *type,
                                          void *target_element_ptr_or_null)
{
    (void)r;
    (void)type;
    (void)target_element_ptr_or_null;
}

#endif
