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

static void node_finalizer(JSRuntime *rt, JSValue val)
{
    (void)rt;
    (void)val;
    /* lexbor owns the DOM tree; we don't free anything here. */
}

#define WRAP_CACHE_BUCKETS 4096
struct wrap_cache_entry {
    void *key;
    JSValue val;
};

#define MAX_LISTENERS 1024
struct listener {
    lxb_dom_element_t *el;
    char type[32];
    JSValue handler;
};

/* --- MutationObserver -----------------------------------------------------
 * A real, spec-shaped MutationObserver. Frameworks depend on it for far more
 * than change tracking: Polymer/lit use an observed text node as their
 * microtask primitive (mutate its characterData → observer callback drains a
 * task queue), and the custom-elements/ShadyDOM polyfills use childList+subtree
 * to react to inserted nodes. A no-op observer silently strands every one of
 * those flows — which is exactly what kept Polymer's data-binding flush from
 * ever running. */
enum {
    MO_CHILD_LIST = 1 << 0,
    MO_ATTRIBUTES = 1 << 1,
    MO_CHARACTER_DATA = 1 << 2,
    MO_SUBTREE = 1 << 3,
    MO_ATTR_OLD_VALUE = 1 << 4,
    MO_CHAR_OLD_VALUE = 1 << 5,
};

#define MO_MAX_OBSERVATIONS 16
struct mutation_observation {
    lxb_dom_node_t *target;
    int flags;
};

struct mutation_observer {
    JSValue self;     /* the MutationObserver instance (see `rooted`) */
    JSValue callback; /* owned */
    JSValue records;  /* owned JS array of pending MutationRecords, or
	                   * JS_UNDEFINED when the queue is empty */
    struct mutation_observation observations[MO_MAX_OBSERVATIONS];
    int observation_count;
    /* An observer with active observations must outlive every JS reference to
	 * it — the spec keeps it reachable from each observed node's registered-
	 * observer list. Frameworks rely on this: Polymer's microtask scheduler
	 * does `new MutationObserver(cb).observe(node,…)` and keeps NO reference.
	 * While `rooted`, we hold one extra ref on `self` to pin it alive; it is
	 * released on disconnect (or teardown), after which the object is
	 * collectable and its finalizer frees this struct. */
    int rooted;
};

#define MAX_MUTATION_OBSERVERS 128

/* Per-runtime state parked on the JSRuntime opaque so every callback can
 * reach it from `ctx`. One instance per engine — the wrap cache, listener
 * pool, and class IDs were process-wide once, which aliased wrappers and
 * listeners across engines (tabs). Allocated by dom_install, freed by
 * js_destroy. NOTE: `r` must stay the first member — ybrowser-js-web.c
 * reads this opaque through a `{ struct yetty_ylexbor *r; }` prefix view. */
struct js_dom_state {
    struct yetty_ylexbor *r;

    /* QuickJS class IDs — runtime-scoped (JS_NewClassID takes rt), so a
	 * fresh runtime allocates a fresh set. */
    JSClassID class_node_id;
    JSClassID class_element_id;
    JSClassID class_document_id;
    JSClassID class_classlist_id;
    JSClassID class_style_id;
    JSClassID class_mutation_observer_id;

    /* lxb pointer → wrapper JSValue; see the wrapping comment below. */
    struct wrap_cache_entry wrap_cache[WRAP_CACHE_BUCKETS];

    /* addEventListener registrations (element may be NULL for
	 * document/window-targeted listeners). */
    struct listener listeners[MAX_LISTENERS];
    int listener_count;

    /* dispatchEvent re-entrancy guard — a handler that synchronously
	 * re-dispatches its own event would recurse forever. */
    int dispatch_depth;

    /* Live MutationObservers. Slots freed on finalize become NULL holes and
	 * are reused; delivery skips NULLs. */
    struct mutation_observer *mutation_observers[MAX_MUTATION_OBSERVERS];
    int mutation_observer_count;
    int mutation_delivery_scheduled; /* one delivery microtask in flight */

    /* Set once the page defines a custom element. Gates the custom-element
	 * reaction dispatch on DOM insertion so pages with no custom elements
	 * pay nothing. */
    int ce_active;
    /* Re-entrancy depth of the reaction dispatch, so a connectedCallback that
	 * inserts more nodes does not recurse without bound on a malformed tree. */
    int ce_react_depth;

    /* CharacterData.prototype — carries `data`/`length`/appendData/… Text,
	 * Comment and ProcessingInstruction wrappers get this proto (chained above
	 * the element proto) so those members exist on character-data nodes but NOT
	 * on elements. `"data" in <element>` must be false: the Closure/resin DOM
	 * sanitizer classifies `data` as a URL-typed property whenever a generic
	 * element reports it, and would then reject object property bindings (e.g.
	 * Polymer `data=[[obj]]`) as unsafe. Owned reference held for the runtime's
	 * lifetime; JS_UNDEFINED until install. */
    JSValue chardata_proto;

    /* DocumentFragment.prototype — chained on Node.prototype (NOT the element
	 * proto), so a fragment wrapper is `instanceof Node` and
	 * `instanceof DocumentFragment` but NOT `instanceof Element`. The single
	 * element-class wrapper otherwise makes every non-Document node an Element,
	 * an impossible hybrid: frameworks branch on `x instanceof Element` (kevlar
	 * does `F instanceof Element && …` while walking stamped template content)
	 * and a fragment wrongly classified as an element shifts node-info indices.
	 * apply_fragment_proto() re-parents fragment wrappers to this proto. Owned
	 * reference held for the runtime's lifetime; JS_UNDEFINED until install. */
    JSValue fragment_proto;
};

static struct js_dom_state *dom_state(JSContext *ctx)
{
    return JS_GetRuntimeOpaque(JS_GetRuntime(ctx));
}

static struct yetty_ylexbor *runtime_ylex(JSContext *ctx)
{
    struct js_dom_state *state = dom_state(ctx);
    return state ? state->r : NULL;
}

/* Public (module-internal) wrapper: recover the owning engine from a context.
 * Used by the console.* capture in ybrowser-js.c, which has no view of the
 * static js_dom_state layout. */
struct yetty_ylexbor *yetty_ylexbor_js_engine_from_ctx(struct JSContext *ctx)
{
    return runtime_ylex((JSContext *)ctx);
}

/* Mutator helper. Always paired with a DOM modification. */
/* Detach `node` from its parent, guarding a lexbor crash: the removing
 * steps for a <style> element pass style->stylesheet straight into
 * lxb_dom_document_stylesheet_remove with NO NULL check — and a style
 * inserted while EMPTY (the CSS-in-JS pattern: append the element first,
 * fill textContent afterwards; theguardian.com does this on every page)
 * never received a stylesheet, so removing or re-appending it segfaulted.
 * Such elements take the steps-free removal; everything else keeps full
 * removing-steps semantics. */
static void node_remove_safe(lxb_dom_node_t *node)
{
    if (node->type == LXB_DOM_NODE_TYPE_ELEMENT && node->local_name == LXB_TAG_STYLE &&
        lxb_html_interface_style(node)->stylesheet == NULL) {
        lxb_dom_node_remove_wo_events(node);
        return;
    }
    lxb_dom_node_remove(node);
}

/* CSS-in-JS ingestion: a <style> element that reaches the document — or
 * gains text while in it — must feed the engine cascade; libcss only saw
 * the sheets present at load time, so JS-injected styles never applied.
 * Re-ingesting the same rules on a re-append is tolerated (identical
 * author-origin rules cascade to the same result). */
static void collect_text(lxb_dom_node_t *node, char **buf, size_t *len, size_t *cap);

static void style_element_ingest(JSContext *ctx, lxb_dom_node_t *node)
{
    if (node->type != LXB_DOM_NODE_TYPE_ELEMENT || node->local_name != LXB_TAG_STYLE) {
        return;
    }
    struct yetty_ylexbor *r = runtime_ylex(ctx);
    if (!r) {
        return;
    }
    char *css_text = NULL;
    size_t css_len = 0, css_cap = 0;
    collect_text(node, &css_text, &css_len, &css_cap);
    if (css_text && css_len > 0) {
        struct yetty_ycore_void_result add_res = yetty_ylexbor_add_css(r, css_text, css_len);
        if (YETTY_IS_ERR(add_res)) {
            yetty_ycore_error_destroy(add_res.error);
        }
    }
    free(css_text);
}

static void mark_dirty(JSContext *ctx)
{
    struct yetty_ylexbor *r = runtime_ylex(ctx);
    if (r) {
        r->dom_dirty = 1;
        /* A repaint is owed too. Kept separate from dom_dirty because a geometry
         * getter's layout flush clears dom_dirty (layout is current) but leaves
         * the framebuffer stale — see needs_paint in ybrowser-internal.h. */
        r->needs_paint = 1;
        /* Any structure / class / attribute change can flip which selectors
         * match, so BOTH selector caches must drop: the supplementary-match
         * cache and the libcss computed-style cache. Bumping the epochs
         * invalidates them lazily on next lookup. */
        r->supp_match_epoch++;
        r->style_epoch++;
        /* Structure/class/attribute can change layout geometry. */
        r->layout_dirty = 1;
    }
}

/* Like mark_dirty, but for a REGULAR inline-style / CSS-property write
 * (element.style.transform = …, setProperty("width", …), cssText). Selectors
 * never match on inline style, and ybrowser resolves inheritance in box-build,
 * so neither the supplementary-match cache nor the per-element computed-style
 * cache is invalidated globally — the one element that changed is caught by its
 * inline-style hash. This is what keeps both caches warm through CSS animations
 * (the youtube case). `affects_layout` marks whether the property can move a box
 * (width/height/margin/… yes; opacity/color/box-shadow no). */
static void mark_dirty_style_write(JSContext *ctx, bool affects_layout)
{
    struct yetty_ylexbor *r = runtime_ylex(ctx);
    if (r) {
        r->dom_dirty = 1;
        r->needs_paint = 1;
        if (affects_layout) {
            r->layout_dirty = 1;
        }
    }
}

/* An inline CUSTOM-property write (element.style.setProperty("--x", …)).
 * Custom properties inherit, so any descendant's var(--x) resolution can
 * change — the computed-style cache (which bakes in resolved var()s) must drop
 * globally, and var() can feed a layout property (width: var(--x)), so a
 * relayout may be owed too. Selector matching is unaffected, so the
 * supplementary-match cache survives. */
static void mark_dirty_custom_prop(JSContext *ctx)
{
    struct yetty_ylexbor *r = runtime_ylex(ctx);
    if (r) {
        r->dom_dirty = 1;
        r->needs_paint = 1;
        r->style_epoch++;
        r->layout_dirty = 1;
    }
}

/* True for CSS properties that CANNOT change layout geometry — neither the
 * layout box (offsetWidth/Height/clientWidth) nor the transformed border box
 * (getBoundingClientRect). A geometry read after only such a write does not need
 * a relayout. Deliberately conservative: anything not listed here is treated as
 * layout-affecting. Notably EXCLUDES transform (feeds getBoundingClientRect) and
 * visibility (collapse removes table-track space). */
static bool css_prop_is_paint_only(const char *name)
{
    static const char *const paint_only[] = {
        "opacity",
        "color",
        "background",
        "background-color",
        "background-image",
        "background-position",
        "background-repeat",
        "background-size",
        "background-attachment",
        "background-clip",
        "background-origin",
        "box-shadow",
        "text-shadow",
        "outline",
        "outline-color",
        "outline-style",
        "outline-width",
        "outline-offset",
        "border-color",
        "border-top-color",
        "border-right-color",
        "border-bottom-color",
        "border-left-color",
        "cursor",
        "filter",
        "-webkit-filter",
        "backdrop-filter",
        "caret-color",
        "accent-color",
        "pointer-events",
        "user-select",
        "-webkit-user-select",
        "text-decoration",
        "text-decoration-color",
        "text-decoration-style",
        "-webkit-tap-highlight-color",
        "transition",
        "transition-property",
        "transition-duration",
        "transition-timing-function",
        "transition-delay",
        "animation-name",
        "animation-duration",
        "animation-timing-function",
        "animation-delay",
        "animation-iteration-count",
        "animation-direction",
        "animation-fill-mode",
        "animation-play-state",
        "will-change",
    };
    for (size_t i = 0; i < sizeof(paint_only) / sizeof(paint_only[0]); i++) {
        if (strcmp(name, paint_only[i]) == 0) {
            return true;
        }
    }
    return false;
}

/* Route an inline-style property write to the right invalidation: a custom
 * property (--foo) can affect descendants' var() resolution; a plain property
 * only affects this element (caught by its inline-hash), and only forces a
 * relayout when it can actually move a box. */
static void mark_dirty_inline_prop(JSContext *ctx, const char *prop_name)
{
    /* A custom property, or a wholesale cssText replacement (which may add or
     * change custom properties), needs the conservative descendant-affecting
     * invalidation; a plain property only affects this element. */
    bool conservative = prop_name && ((prop_name[0] == '-' && prop_name[1] == '-') ||
                                      strcmp(prop_name, "cssText") == 0 ||
                                      strcmp(prop_name, "css-text") == 0);
    if (conservative) {
        mark_dirty_custom_prop(ctx);
        return;
    }
    mark_dirty_style_write(ctx, /*affects_layout=*/!prop_name || !css_prop_is_paint_only(prop_name));
}

/* MutationObserver notification — implemented below, forward-declared here so
 * the mutation methods can report their changes. `old_value` is NULL when not
 * captured (callers only bother when some observer might want it). */
static void dom_mo_notify_attributes(JSContext *ctx, lxb_dom_element_t *el, const char *name,
                                     size_t name_len, const char *old_value);
static void dom_mo_notify_character_data(JSContext *ctx, lxb_dom_node_t *target,
                                         const char *old_value);
static void dom_mo_notify_child_list(JSContext *ctx, lxb_dom_node_t *parent, lxb_dom_node_t *added,
                                     lxb_dom_node_t *removed, lxb_dom_node_t *prev_sibling,
                                     lxb_dom_node_t *next_sibling);
/* True when any live observer could care about a mutation of `type_flag` at
 * `target` — lets a hot mutation path skip capturing an old value nobody wants. */
static int dom_mo_any_interest(JSContext *ctx, lxb_dom_node_t *target, int type_flag);

/* Return a heap copy of `el`'s current `name` attribute value, but only when
 * some observer wants the pre-change value; NULL otherwise (caller frees). */
static char *mo_capture_attr(JSContext *ctx, lxb_dom_element_t *el, const char *name,
                             size_t name_len)
{
    if (!dom_mo_any_interest(ctx, lxb_dom_interface_node(el), MO_ATTRIBUTES)) {
        return NULL;
    }
    size_t old_len = 0;
    const lxb_char_t *old =
        lxb_dom_element_get_attribute(el, (const lxb_char_t *)name, name_len, &old_len);
    return old != NULL ? strndup((const char *)old, old_len) : NULL;
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
 * The cache lives in the per-runtime js_dom_state, matching the class-ID
 * lifetime — a fresh engine (tab, or the fork-per-test runner) starts
 * with an empty cache instead of aliasing another engine's wrappers.
 * ===========================================================================*/

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
    struct js_dom_state *state = dom_state(ctx);
    if (!key || !state) {
        return JS_UNDEFINED;
    }
    size_t h = wrap_hash(key);
    for (size_t i = 0; i < WRAP_CACHE_BUCKETS; i++) {
        size_t idx = (h + i) & (WRAP_CACHE_BUCKETS - 1);
        if (!state->wrap_cache[idx].key) {
            return JS_UNDEFINED;
        }
        if (state->wrap_cache[idx].key == key) {
            return JS_DupValue(ctx, state->wrap_cache[idx].val);
        }
    }
    return JS_UNDEFINED;
}

static void wrap_cache_insert(JSContext *ctx, void *key, JSValueConst v)
{
    struct js_dom_state *state = dom_state(ctx);
    if (!key || !state) {
        return;
    }
    size_t h = wrap_hash(key);
    for (size_t i = 0; i < WRAP_CACHE_BUCKETS; i++) {
        size_t idx = (h + i) & (WRAP_CACHE_BUCKETS - 1);
        if (!state->wrap_cache[idx].key || state->wrap_cache[idx].key == key) {
            state->wrap_cache[idx].key = key;
            state->wrap_cache[idx].val = JS_DupValue(ctx, v);
            return;
        }
    }
    /* Table full — fall back to non-cached behavior, identity will
	 * regress for late-allocated elements but no crash. */
}

static void wrap_cache_clear(JSContext *ctx)
{
    struct js_dom_state *state = dom_state(ctx);
    if (!state) {
        return;
    }
    for (size_t i = 0; i < WRAP_CACHE_BUCKETS; i++) {
        if (state->wrap_cache[i].key) {
            JS_FreeValue(ctx, state->wrap_cache[i].val);
            state->wrap_cache[i].key = NULL;
            state->wrap_cache[i].val = JS_UNDEFINED;
        }
    }
}

/* Text, Comment and ProcessingInstruction wrappers get the CharacterData
 * prototype (chained above the element proto), so `data`/`length`/appendData/…
 * live only on character-data nodes and not on elements. See the
 * chardata_proto field comment for why elements must not report `data`. */
static void apply_chardata_proto(JSContext *ctx, JSValueConst v, const lxb_dom_node_t *n)
{
    if (n == NULL || (n->type != LXB_DOM_NODE_TYPE_TEXT && n->type != LXB_DOM_NODE_TYPE_COMMENT &&
                      n->type != LXB_DOM_NODE_TYPE_PROCESSING_INSTRUCTION)) {
        return;
    }
    struct js_dom_state *state = dom_state(ctx);
    if (state == NULL || JS_IsUndefined(state->chardata_proto)) {
        return;
    }
    JS_SetPrototype(ctx, v, state->chardata_proto);
}

/* A DocumentFragment wrapper gets the DocumentFragment proto (chained on
 * Node.prototype) instead of the element proto, so it reports the correct
 * interface identity: `instanceof Node`/`instanceof DocumentFragment` true,
 * `instanceof Element` false. The wrapper keeps its element-class opaque
 * (the lexbor node pointer + finalizer) — only the prototype changes, exactly
 * like apply_chardata_proto. See the fragment_proto field comment. */
static void apply_fragment_proto(JSContext *ctx, JSValueConst v, const lxb_dom_node_t *n)
{
    if (n == NULL || n->type != LXB_DOM_NODE_TYPE_DOCUMENT_FRAGMENT) {
        return;
    }
    struct js_dom_state *state = dom_state(ctx);
    if (state == NULL || JS_IsUndefined(state->fragment_proto)) {
        return;
    }
    JS_SetPrototype(ctx, v, state->fragment_proto);
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
    JSValue v = JS_NewObjectClass(ctx, dom_state(ctx)->class_element_id);
    JS_SetOpaque(v, el);
    /* Internal callers sometimes route a non-element node through this
	 * element-typed helper (e.g. ce_react_dispatch casts a DocumentFragment,
	 * MutationRecord helpers wrap generic targets). Correct interface identity
	 * must not depend on which entry point touches the node first, so apply the
	 * per-type proto overrides here too — same as wrap_node_any. */
    apply_chardata_proto(ctx, v, (const lxb_dom_node_t *)el);
    apply_fragment_proto(ctx, v, (const lxb_dom_node_t *)el);
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
    JSValue v = JS_NewObjectClass(ctx, dom_state(ctx)->class_document_id);
    JS_SetOpaque(v, doc);
    wrap_cache_insert(ctx, doc, v);
    return v;
}

static lxb_dom_element_t *unwrap_element(JSContext *ctx, JSValueConst this_val)
{
    struct js_dom_state *state = dom_state(ctx);
    if (!state) {
        return NULL;
    }
    void *p = JS_GetOpaque(this_val, state->class_element_id);
    if (p) {
        return (lxb_dom_element_t *)p;
    }
    /* Element methods are also called on Document via prototype chain
	 * sometimes (querySelector on document). Allow both. */
    return NULL;
}

/* Like unwrap_element, but ONLY for element-attribute operations. The element
 * JS class is shared by Text/Comment/Fragment nodes; lexbor's attribute
 * accessors read the element-struct attribute fields (first_attr / attr_id /
 * attr_class) which sit past the end of those smaller node structs, so reading
 * them is out of bounds and intermittently segfaults on adjacent pool memory.
 * Non-elements have no attributes: return NULL so the attribute method no-ops
 * (returns null / false), matching the DOM and the hasAttributes/attributes
 * guards. Do NOT use this for node-level or CharacterData methods that are valid
 * on non-elements (they still use unwrap_element / unwrap_node). */
static lxb_dom_element_t *unwrap_attr_element(JSContext *ctx, JSValueConst this_val)
{
    lxb_dom_element_t *el = unwrap_element(ctx, this_val);
    if (!el || lxb_dom_interface_node(el)->type != LXB_DOM_NODE_TYPE_ELEMENT) {
        return NULL;
    }
    return el;
}

static lxb_html_document_t *unwrap_document(JSContext *ctx, JSValueConst this_val)
{
    struct js_dom_state *state = dom_state(ctx);
    if (!state) {
        return NULL;
    }
    void *p = JS_GetOpaque(this_val, state->class_document_id);
    return p ? (lxb_html_document_t *)p : NULL;
}

/* Walk-up to find the lxb_dom_node_t this JSValue represents (Element
 * or Document). Returns NULL if neither. */
static lxb_dom_node_t *unwrap_node(JSContext *ctx, JSValueConst this_val)
{
    struct js_dom_state *state = dom_state(ctx);
    if (!state) {
        return NULL;
    }
    lxb_dom_element_t *e = JS_GetOpaque(this_val, state->class_element_id);
    if (e) {
        return lxb_dom_interface_node(e);
    }
    lxb_html_document_t *d = JS_GetOpaque(this_val, dom_state(ctx)->class_document_id);
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

/* Does ONE element match the selector list? Real engine matching
 * (combinators, attribute selectors incl. ~=, pseudo-classes) via
 * lxb_selectors_match_node. Returns 1 match / 0 no / -1 parse error. */
YETTY_EXTERNAL_CALLBACK
static lxb_status_t element_match_found_cb(lxb_dom_node_t *node, lxb_css_selector_specificity_t sp,
                                           void *ctx)
{
    (void)node;
    (void)sp;
    *(int *)ctx = 1;
    return LXB_STATUS_STOP;
}

static int element_matches_selector(lxb_dom_node_t *node, const char *sel_text, size_t sel_len)
{
    int matched = 0;
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
    lxb_status_t s = lxb_selectors_match_node(sel, node, list, element_match_found_cb, &matched);
    lxb_css_selector_list_destroy_memory(list);
    if (s == LXB_STATUS_OK || s == LXB_STATUS_STOP) {
        rc = matched;
    }
out_sel:
    lxb_selectors_destroy(sel, true);
out_parser:
    lxb_css_parser_destroy(parser, true);
    return rc;
}

/* Element.matches(selector) — spec behavior, engine-backed. */
static JSValue js_el_matches(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    if (argc < 1) {
        return JS_FALSE;
    }
    lxb_dom_node_t *node = unwrap_node(ctx, this_val);
    if (!node || node->type != LXB_DOM_NODE_TYPE_ELEMENT) {
        return JS_FALSE;
    }
    size_t slen;
    const char *sel = JS_ToCStringLen(ctx, &slen, argv[0]);
    if (!sel) {
        return JS_FALSE;
    }
    int matched = element_matches_selector(node, sel, slen);
    JS_FreeCString(ctx, sel);
    return matched == 1 ? JS_TRUE : JS_FALSE;
}

/* Element.closest(selector) — walk ANCESTORS (inclusive) for the first
 * match. The old binding aliased this to querySelector, which searches
 * DESCENDANTS — that broke Catalyst's `el.closest(tag) === controller`
 * target scoping and with it every `data-target` lookup on github. */
static JSValue js_el_closest(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    if (argc < 1) {
        return JS_NULL;
    }
    lxb_dom_node_t *node = unwrap_node(ctx, this_val);
    if (!node) {
        return JS_NULL;
    }
    size_t slen;
    const char *sel = JS_ToCStringLen(ctx, &slen, argv[0]);
    if (!sel) {
        return JS_NULL;
    }
    JSValue result = JS_NULL;
    for (lxb_dom_node_t *walk = node; walk != NULL; walk = walk->parent) {
        if (walk->type != LXB_DOM_NODE_TYPE_ELEMENT) {
            continue;
        }
        if (element_matches_selector(walk, sel, slen) == 1) {
            result = wrap_element(ctx, lxb_dom_interface_element(walk));
            break;
        }
    }
    JS_FreeCString(ctx, sel);
    return result;
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
    lxb_dom_element_t *el = unwrap_attr_element(ctx, this_val);
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
    lxb_dom_element_t *el = unwrap_attr_element(ctx, this_val);
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
        char *old_value = mo_capture_attr(ctx, el, name, nlen);
        lxb_dom_element_set_attribute(el, (const lxb_char_t *)name, nlen, (const lxb_char_t *)val,
                                      vlen);
        mark_dirty(ctx);
        dom_mo_notify_attributes(ctx, el, name, nlen, old_value);
        free(old_value);
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
    lxb_dom_element_t *el = unwrap_attr_element(ctx, this_val);
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
            char *old_value = mo_capture_attr(ctx, el, raw, nlen);
            lxb_dom_element_remove_attribute(el, (const lxb_char_t *)raw, nlen);
            mark_dirty(ctx);
            dom_mo_notify_attributes(ctx, el, raw, nlen, old_value);
            free(old_value);
            JS_FreeCString(ctx, raw);
        }
        return JS_UNDEFINED;
    }
    char *old_value = mo_capture_attr(ctx, el, name, nlen);
    lxb_dom_element_remove_attribute(el, (const lxb_char_t *)name, nlen);
    mark_dirty(ctx);
    dom_mo_notify_attributes(ctx, el, name, nlen, old_value);
    free(old_value);
    free(name);
    return JS_UNDEFINED;
}

static JSValue js_el_hasAttribute(JSContext *ctx, JSValueConst this_val, int argc,
                                  JSValueConst *argv)
{
    if (argc < 1) {
        return JS_FALSE;
    }
    lxb_dom_element_t *el = unwrap_attr_element(ctx, this_val);
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

/* ===========================================================================
 * Custom-element reactions.
 *
 * Web-component frameworks (Polymer, Lit, Stencil) require connectedCallback
 * to run SYNCHRONOUSLY while a component stamps its template. Kevlar
 * (YouTube's Polymer app) is the sharp case: ytd-page-manager registers a
 * dependency-injection provider (PAGE_TOKEN) in its ready(), and ytd-app's
 * data-binding observer resolves that provider during ytd-app's own ready().
 * The page-manager is stamped as a child of ytd-app, so it must connect —
 * synchronously — before the parent's observer runs, or the resolve throws.
 * A deferred (microtask) connect reorders those steps and breaks the boot.
 *
 * So immediately after a node lands in the live document, hand the inserted
 * subtree to the registry's __connectSubtree, which upgrades + fires
 * connectedCallback on every defined custom element it contains, in tree
 * order. This mirrors native custom-element reactions. Gated on ce_active
 * (set when the page first defines a custom element) so ordinary pages pay
 * nothing; errors are absorbed so a throwing callback cannot corrupt the DOM
 * mutation in progress. */
static int node_is_connected(lxb_dom_node_t *node)
{
    for (; node != NULL; node = node->parent) {
        if (node->type == LXB_DOM_NODE_TYPE_DOCUMENT) {
            return 1;
        }
    }
    return 0;
}

static void ce_react_dispatch(JSContext *ctx, lxb_dom_node_t *node, const char *method)
{
    struct js_dom_state *state = dom_state(ctx);
    if (state == NULL || !state->ce_active || node == NULL) {
        return;
    }
    if (node->type != LXB_DOM_NODE_TYPE_ELEMENT &&
        node->type != LXB_DOM_NODE_TYPE_DOCUMENT_FRAGMENT) {
        return;
    }
    /* Bound the synchronous cascade (connect -> stamp -> insert -> connect)
	 * against a pathologically deep tree. */
    if (state->ce_react_depth > 256) {
        return;
    }
    state->ce_react_depth++;
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue registry = JS_GetPropertyStr(ctx, global, "customElements");
    JSValue fn = JS_GetPropertyStr(ctx, registry, method);
    if (JS_IsFunction(ctx, fn)) {
        JSValue wrapped = wrap_element(ctx, (lxb_dom_element_t *)node);
        JSValue call_argv[1] = {wrapped};
        JSValue result = JS_Call(ctx, fn, registry, 1, call_argv);
        if (JS_IsException(result)) {
            /* The connect walk (__connectSubtree) catches per-element reaction
			 * exceptions in JS and logs them there, so this outer boundary
			 * normally sees none. If one does surface here, __connectSubtree
			 * itself failed (not a per-element callback) — flag it. */
            JSValue ex = JS_GetException(ctx);
            const char *message = JS_ToCString(ctx, ex);
            ydebug("ce-react-exc %s depth=%d: %s", method, state->ce_react_depth,
                   message ? message : "?");
            if (message != NULL) {
                JS_FreeCString(ctx, message);
            }
            JS_FreeValue(ctx, ex);
        }
        JS_FreeValue(ctx, result);
        JS_FreeValue(ctx, wrapped);
    }
    JS_FreeValue(ctx, fn);
    JS_FreeValue(ctx, registry);
    JS_FreeValue(ctx, global);
    state->ce_react_depth--;
}

/* Fire connect reactions for a node just inserted into the document tree. */
static void ce_react_connect(JSContext *ctx, lxb_dom_node_t *inserted)
{
    if (inserted != NULL && node_is_connected(inserted)) {
        ce_react_dispatch(ctx, inserted, "__connectSubtree");
    }
}

/* Upgrade a freshly-created element in place (document.createElement of a
 * defined custom tag). Unlike connect, the element is detached, so only the
 * constructor + observed-attribute reactions run; connectedCallback waits for a
 * later insertion. Spec "create an element" upgrades synchronously so the
 * returned object already exposes its methods.
 *
 * Takes the ALREADY-WRAPPED element value (not the raw node): the constructor
 * stamps a template that churns the wrapper cache, so we must upgrade the exact
 * JS object the caller keeps, not one re-derived from the node afterwards. */
static void ce_react_upgrade_value(JSContext *ctx, JSValueConst wrapped)
{
    struct js_dom_state *state = dom_state(ctx);
    if (state == NULL || !state->ce_active || JS_IsUndefined(wrapped) || JS_IsNull(wrapped)) {
        return;
    }
    if (state->ce_react_depth > 256) {
        return;
    }
    state->ce_react_depth++;
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue registry = JS_GetPropertyStr(ctx, global, "customElements");
    JSValue fn = JS_GetPropertyStr(ctx, registry, "__upgradeOne");
    if (JS_IsFunction(ctx, fn)) {
        JSValue call_argv[1] = {(JSValue)wrapped};
        JSValue result = JS_Call(ctx, fn, registry, 1, call_argv);
        /* Per-element reaction exceptions are caught inside __upgradeOne. */
        JS_FreeValue(ctx, result);
    }
    JS_FreeValue(ctx, fn);
    JS_FreeValue(ctx, registry);
    JS_FreeValue(ctx, global);
    state->ce_react_depth--;
}

/* Fire disconnect reactions for a node just removed from the document tree.
 * The node is already detached, so connectedness is not re-checked; the
 * registry only acts on elements it had marked connected. */
static void ce_react_disconnect(JSContext *ctx, lxb_dom_node_t *removed)
{
    ce_react_dispatch(ctx, removed, "__disconnectSubtree");
}

/* Spec: inserting a DocumentFragment inserts its CHILDREN, in order, at the
 * insertion point and empties the fragment — the fragment node itself is never
 * inserted. Insert each child before `ref` (which must be a child of `parent`),
 * or append when `ref` is NULL. Runs the same per-child bookkeeping (detach,
 * style ingest, MutationObserver notify, custom-element connect) the single-node
 * paths run. Frameworks build a subtree in a fragment and insert it wholesale
 * (Polymer rewraps a <template>'s content into a fresh fragment and appends it);
 * inserting the fragment node instead leaves a single nested fragment child and
 * shifts every index a stamping walk depends on. */
static void insert_fragment_children(JSContext *ctx, lxb_dom_node_t *parent,
                                     lxb_dom_node_t *fragment, lxb_dom_node_t *ref)
{
    lxb_dom_node_t *next = NULL;
    for (lxb_dom_node_t *sub = fragment->first_child; sub != NULL; sub = next) {
        next = sub->next;
        lxb_dom_node_remove(sub);
        if (ref != NULL && ref->parent == parent) {
            lxb_dom_node_insert_before(ref, sub);
        } else {
            lxb_dom_node_insert_child(parent, sub);
        }
        style_element_ingest(ctx, sub);
        dom_mo_notify_child_list(ctx, parent, sub, NULL, sub->prev, sub->next);
        ce_react_connect(ctx, sub);
    }
}

static JSValue js_el_appendChild(JSContext *ctx, JSValueConst this_val, int argc,
                                 JSValueConst *argv)
{
    if (argc < 1) {
        return JS_UNDEFINED;
    }
    lxb_dom_node_t *parent = unwrap_node(ctx, this_val);
    lxb_dom_node_t *child = unwrap_node(ctx, argv[0]);
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
    if (child->type == LXB_DOM_NODE_TYPE_DOCUMENT_FRAGMENT) {
        insert_fragment_children(ctx, parent, child, NULL);
        mark_dirty(ctx);
        return JS_DupValue(ctx, argv[0]);
    }
    /* Pre-insert step from the spec: detach child from its current
	 * parent first. lexbor's insert_child does NOT do this — calling
	 * it on an already-attached child corrupts the old parent's
	 * sibling chain (next/prev becomes ambiguous), so a later tree
	 * walk on the old parent infinite-loops. */
    if (child->parent) {
        lxb_dom_node_t *old_parent = child->parent;
        lxb_dom_node_t *old_prev = child->prev;
        lxb_dom_node_t *old_next = child->next;
        node_remove_safe(child);
        dom_mo_notify_child_list(ctx, old_parent, NULL, child, old_prev, old_next);
    }
    lxb_dom_node_t *last = parent->last_child;
    lxb_dom_node_insert_child(parent, child);
    style_element_ingest(ctx, child);
    mark_dirty(ctx);
    dom_mo_notify_child_list(ctx, parent, child, NULL, last, NULL);
    ce_react_connect(ctx, child);
    return JS_DupValue(ctx, argv[0]);
}

static JSValue js_el_removeChild(JSContext *ctx, JSValueConst this_val, int argc,
                                 JSValueConst *argv)
{
    if (argc < 1) {
        return JS_UNDEFINED;
    }
    lxb_dom_node_t *child = unwrap_node(ctx, argv[0]);
    if (!child) {
        return JS_UNDEFINED;
    }
    lxb_dom_node_t *old_parent = child->parent;
    lxb_dom_node_t *old_prev = child->prev;
    lxb_dom_node_t *old_next = child->next;
    node_remove_safe(child);
    mark_dirty(ctx);
    if (old_parent != NULL) {
        dom_mo_notify_child_list(ctx, old_parent, NULL, child, old_prev, old_next);
        ce_react_disconnect(ctx, child);
    }
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
    lxb_dom_node_t *n = unwrap_node(ctx, v);
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
        node_remove_safe(n);
    }
}

/* before(): insert each arg as a left sibling of `this`.
 * after():  insert each arg as a right sibling of `this`.
 * replaceWith(): insert all args in `this`'s position, then remove `this`. */
static JSValue js_el_before(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    lxb_dom_node_t *self = unwrap_node(ctx, this_val);
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
    lxb_dom_node_t *self = unwrap_node(ctx, this_val);
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
    lxb_dom_node_t *self = unwrap_node(ctx, this_val);
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
    node_remove_safe(self);
    mark_dirty(ctx);
    return JS_UNDEFINED;
}

/* prepend(): insert each arg before parent's firstChild.
 * append(): insert each arg as last child of parent. */
static JSValue js_el_prepend(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    lxb_dom_node_t *parent = unwrap_node(ctx, this_val);
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
    lxb_dom_node_t *parent = unwrap_node(ctx, this_val);
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
    lxb_dom_node_t *parent = unwrap_node(ctx, this_val);
    lxb_dom_node_t *node = unwrap_node(ctx, argv[0]);
    if (!parent || !node) {
        return JS_UNDEFINED;
    }
    if (node_would_cycle(parent, node)) {
        return JS_DupValue(ctx, argv[0]);
    }
    lxb_dom_node_t *ref = (argc >= 2) ? unwrap_node(ctx, argv[1]) : NULL;
    if (node->type == LXB_DOM_NODE_TYPE_DOCUMENT_FRAGMENT) {
        insert_fragment_children(ctx, parent, node, ref);
        mark_dirty(ctx);
        return JS_DupValue(ctx, argv[0]);
    }
    /* Detach from the current parent first — lexbor's insert paths assume an
     * unlinked node (see appendChild). */
    if (node->parent) {
        lxb_dom_node_t *old_parent = node->parent;
        lxb_dom_node_t *old_prev = node->prev;
        lxb_dom_node_t *old_next = node->next;
        node_remove_safe(node);
        dom_mo_notify_child_list(ctx, old_parent, NULL, node, old_prev, old_next);
    }
    if (ref && ref->parent == parent) {
        lxb_dom_node_insert_before(ref, node);
    } else {
        /* null ref, or ref not a child of parent → append. */
        lxb_dom_node_insert_child(parent, node);
    }
    mark_dirty(ctx);
    dom_mo_notify_child_list(ctx, parent, node, NULL, node->prev, node->next);
    ce_react_connect(ctx, node);
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
    lxb_dom_node_t *parent = unwrap_node(ctx, this_val);
    lxb_dom_node_t *node = unwrap_node(ctx, argv[0]);
    lxb_dom_node_t *old = unwrap_node(ctx, argv[1]);
    if (!parent || !node || !old || old->parent != parent) {
        return JS_UNDEFINED;
    }
    if (node_would_cycle(parent, node)) {
        return JS_DupValue(ctx, argv[1]);
    }
    if (node->type == LXB_DOM_NODE_TYPE_DOCUMENT_FRAGMENT) {
        lxb_dom_node_t *old_prev = old->prev;
        lxb_dom_node_t *old_next = old->next;
        insert_fragment_children(ctx, parent, node, old);
        node_remove_safe(old);
        mark_dirty(ctx);
        dom_mo_notify_child_list(ctx, parent, NULL, old, old_prev, old_next);
        ce_react_disconnect(ctx, old);
        return JS_DupValue(ctx, argv[1]);
    }
    if (node->parent) {
        lxb_dom_node_t *node_old_parent = node->parent;
        lxb_dom_node_t *node_old_prev = node->prev;
        lxb_dom_node_t *node_old_next = node->next;
        node_remove_safe(node);
        dom_mo_notify_child_list(ctx, node_old_parent, NULL, node, node_old_prev, node_old_next);
    }
    lxb_dom_node_t *old_prev = old->prev;
    lxb_dom_node_t *old_next = old->next;
    lxb_dom_node_insert_before(old, node);
    node_remove_safe(old);
    mark_dirty(ctx);
    /* One record carrying both the added and removed node, as the spec's
	 * "replace all"/replace step produces. */
    dom_mo_notify_child_list(ctx, parent, node, old, old_prev, old_next);
    ce_react_disconnect(ctx, old);
    ce_react_connect(ctx, node);
    return JS_DupValue(ctx, argv[1]);
}

/* querySelector(All) on Element AND Document — same impl. */
static JSValue js_el_querySelector(JSContext *ctx, JSValueConst this_val, int argc,
                                   JSValueConst *argv)
{
    if (argc < 1) {
        return JS_NULL;
    }
    lxb_dom_node_t *root = unwrap_node(ctx, this_val);
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
    lxb_dom_node_t *root = unwrap_node(ctx, this_val);
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
    lxb_dom_node_t *root = unwrap_node(ctx, this_val);
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
    lxb_dom_node_t *root = unwrap_node(ctx, this_val);
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
    lxb_dom_node_t *root = unwrap_node(ctx, this_val);
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
    lxb_html_document_t *doc = unwrap_document(ctx, this_val);
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
    lxb_html_document_t *doc = unwrap_document(ctx, this_val);
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
    /* A tag containing '-' is a custom-element candidate; if it is defined,
     * upgrade it now (spec "create an element" is synchronous) so callers that
     * immediately invoke a method on the result get the real prototype. */
    bool custom_candidate = memchr(tag, '-', tlen) != NULL;
    JS_FreeCString(ctx, tag);
    if (!el) {
        return JS_NULL;
    }
    mark_dirty(ctx);
    /* Wrap FIRST, then upgrade THIS wrapper. Upgrading runs the element's
     * constructor, which stamps its template — creating many child elements that
     * churn wrap_element's wrapper cache. If we let the upgrade path re-wrap the
     * node internally, that first wrapper can be evicted before we return, so the
     * caller would get a fresh, un-upgraded wrapper. Holding the wrapper across
     * the upgrade keeps the upgraded object alive and returns exactly it. */
    JSValue wrapped = wrap_element(ctx, el);
    if (custom_candidate) {
        ce_react_upgrade_value(ctx, wrapped);
    }
    return wrapped;
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
    lxb_html_document_t *doc = unwrap_document(ctx, this_val);
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
    JSValue v = JS_NewObjectClass(ctx, dom_state(ctx)->class_element_id);
    JS_SetOpaque(v, n); /* opaque is the node, not interface_element */
    apply_chardata_proto(ctx, v, n);
    apply_fragment_proto(ctx, v, n);
    wrap_cache_insert(ctx, n, v);
    return v;
}

static JSValue js_doc_createTextNode(JSContext *ctx, JSValueConst this_val, int argc,
                                     JSValueConst *argv)
{
    if (argc < 1) {
        return JS_NULL;
    }
    lxb_html_document_t *htmldoc = unwrap_document(ctx, this_val);
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

/* document.createDocumentFragment() — a real DocumentFragment node
 * (LXB_DOM_NODE_TYPE_DOCUMENT_FRAGMENT), NOT a `<div>`. A fragment has
 * distinct insertion behaviour: appending it moves its children into the host
 * and empties the fragment, rather than inserting a wrapper element. Frameworks
 * rewrap parsed template content through this API before stamping (Polymer's
 * nested-template prep does `createDocumentFragment().appendChild(tpl.content)`
 * then `importNode()`s the result), so returning an element here corrupts the
 * stamped tree shape and shifts the node indices the framework walks.
 *
 * The wrapper/insertion machinery already handles real fragment nodes:
 * wrap_node_any carries any non-Document node, .nodeType reports 11, .nodeName
 * reports "#document-fragment", and appendChild/insertBefore move fragment
 * children in order. So this only needs to mint the correct lexbor node.
 *
 * No mark_dirty: a freshly created fragment is detached and has no visual
 * effect until its children are inserted into the live tree, and that insertion
 * marks dirty itself. Polymer creates many of these during stamping, so
 * triggering a relayout per creation would be pure waste. */
static JSValue js_doc_createDocumentFragment(JSContext *ctx, JSValueConst this_val, int argc,
                                             JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    lxb_html_document_t *htmldoc = unwrap_document(ctx, this_val);
    if (!htmldoc) {
        return JS_NULL;
    }
    lxb_dom_document_t *doc = lxb_dom_interface_document(htmldoc);
    lxb_dom_document_fragment_t *fragment = lxb_dom_document_fragment_interface_create(doc);
    if (!fragment) {
        return JS_NULL;
    }
    return wrap_node_any(ctx, lxb_dom_interface_node(fragment));
}

/* A `<template>`'s parsed contents live in a *separate* content fragment
 * (`lxb_html_template_element_t.content`), not in the element's normal child
 * list. lexbor's `lxb_dom_node_clone` copies only the normal child tree, so a
 * deep clone of a subtree that contains templates comes back with every
 * template's `.content` empty. That silently breaks any framework that clones a
 * template and then stamps it — Polymer/lit walk the clone by precomputed node
 * indices, and an empty nested-template content shifts those indices so the node
 * finder returns `undefined` (→ "cannot set property '__dataHost' of undefined",
 * binding a `data=` onto `<undefined>`, etc.).
 *
 * Per the HTML spec's cloning steps for template elements, deep-cloning a
 * template must also deep-clone its template contents. Walk `source` and `clone`
 * in lockstep and, for each template pair, deep-clone the source content
 * children into the clone's content fragment (recursing so nested templates get
 * their contents too). */
static void clone_fixup_template_content(lxb_dom_node_t *source, lxb_dom_node_t *clone)
{
    if (source == NULL || clone == NULL) {
        return;
    }
    if (source->type == LXB_DOM_NODE_TYPE_ELEMENT && source->local_name == LXB_TAG_TEMPLATE &&
        clone->type == LXB_DOM_NODE_TYPE_ELEMENT && clone->local_name == LXB_TAG_TEMPLATE) {
        lxb_html_template_element_t *source_template = lxb_html_interface_template(source);
        lxb_html_template_element_t *clone_template = lxb_html_interface_template(clone);
        if (source_template->content != NULL) {
            /* Ensure the clone has a content fragment to receive the children. */
            if (clone_template->content == NULL) {
                clone_template->content =
                    lxb_dom_document_fragment_interface_create(clone->owner_document);
            }
            if (clone_template->content != NULL) {
                lxb_dom_node_t *source_fragment = lxb_dom_interface_node(source_template->content);
                lxb_dom_node_t *clone_fragment = lxb_dom_interface_node(clone_template->content);
                /* Defensive: a freshly cloned template's content should be empty,
                 * but clear anything present so we never double-fill. */
                while (clone_fragment->first_child != NULL) {
                    node_remove_safe(clone_fragment->first_child);
                }
                for (lxb_dom_node_t *child = source_fragment->first_child; child != NULL;
                     child = child->next) {
                    lxb_dom_node_t *child_clone = lxb_dom_node_clone(child, true);
                    if (child_clone != NULL) {
                        lxb_dom_node_insert_child(clone_fragment, child_clone);
                        clone_fixup_template_content(child, child_clone);
                    }
                }
            }
        }
    }
    /* Recurse over the normal child tree in lockstep — the deep clone keeps it
     * structurally identical to the source, so children pair up positionally. */
    lxb_dom_node_t *source_child = source->first_child;
    lxb_dom_node_t *clone_child = clone->first_child;
    while (source_child != NULL && clone_child != NULL) {
        clone_fixup_template_content(source_child, clone_child);
        source_child = source_child->next;
        clone_child = clone_child->next;
    }
}

/* document.importNode(node, deep) — imports a node from another document.
 * The engine is single-document, so this reduces to a deep/shallow clone of the
 * source node returned as a wrapped node. Polymer's template stamping
 * (`_stampTemplate`) relies on it to clone `<template>.content`. */
static JSValue js_doc_importNode(JSContext *ctx, JSValueConst this_val, int argc,
                                 JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) {
        return JS_NULL;
    }
    lxb_dom_node_t *source = unwrap_node(ctx, argv[0]);
    if (!source) {
        return JS_NULL;
    }
    int deep = argc > 1 ? JS_ToBool(ctx, argv[1]) : 0;
    lxb_dom_node_t *clone = lxb_dom_node_clone(source, deep ? true : false);
    if (!clone) {
        return JS_NULL;
    }
    if (deep) {
        clone_fixup_template_content(source, clone);
    }
    mark_dirty(ctx);
    return wrap_node_any(ctx, clone);
}

static JSValue js_doc_createComment(JSContext *ctx, JSValueConst this_val, int argc,
                                    JSValueConst *argv)
{
    lxb_html_document_t *htmldoc = unwrap_document(ctx, this_val);
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

/* Forward declarations — the IDL attribute reflection helpers are defined
 * further down, but `content` reflection (just below) needs them. */
static JSValue idl_attr_get(JSContext *ctx, JSValueConst this_val, const char *attr, size_t alen,
                            int urlish);
static JSValue idl_attr_set(JSContext *ctx, JSValueConst this_val, JSValueConst val,
                            const char *attr, size_t alen);

/* `<template>.content` — spec returns a DocumentFragment holding the template's
 * parsed contents. lexbor parses that content into a *separate* content
 * fragment (`lxb_html_template_element_t.content`), NOT as regular children of
 * the <template> element — so returning the element itself hands back an empty
 * subtree. Polymer's `_stampTemplate` clones `.content` via
 * document.importNode; an empty clone yields no stamped nodes. Return the real
 * content fragment when present. */
static JSValue js_el_content_get(JSContext *ctx, JSValueConst this_val)
{
    lxb_dom_node_t *node = unwrap_node(ctx, this_val);
    if (node && node->type == LXB_DOM_NODE_TYPE_ELEMENT && node->local_name == LXB_TAG_TEMPLATE) {
        lxb_html_template_element_t *tmpl = lxb_html_interface_template(node);
        if (tmpl->content) {
            return wrap_node_any(ctx, lxb_dom_interface_node(tmpl->content));
        }
        return JS_DupValue(ctx, this_val);
    }
    /* Non-template: `content` reflects the `content` content attribute — this is
     * the `<meta name=... content=...>` IDL attribute (a writable string), which
     * is a different property from `<template>.content` (a live fragment). */
    return idl_attr_get(ctx, this_val, "content", sizeof("content") - 1, 0);
}

/* `content` setter. `<template>.content` is a read-only live fragment, so a
 * strict-mode assignment to it must be a harmless no-op rather than a throw
 * (returning without touching anything is what a real getter-only accessor
 * needs to avoid a "no setter for property" TypeError). Every other element
 * reflects the writable `content` attribute (e.g. YouTube sets
 * `metaViewport.content = "width=device-width, ..."` during app-shell setup). */
static JSValue js_el_content_set(JSContext *ctx, JSValueConst this_val, JSValueConst val)
{
    lxb_dom_node_t *node = unwrap_node(ctx, this_val);
    if (node && node->type == LXB_DOM_NODE_TYPE_ELEMENT && node->local_name == LXB_TAG_TEMPLATE) {
        return JS_UNDEFINED;
    }
    return idl_attr_set(ctx, this_val, val, "content", sizeof("content") - 1);
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
    lxb_dom_node_t *n = unwrap_node(ctx, this_val);
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
    lxb_dom_node_t *n = unwrap_node(ctx, this_val);
    if (!n) {
        return JS_UNDEFINED;
    }
    size_t slen;
    const char *s = JS_ToCStringLen(ctx, &slen, val);
    if (s) {
        /* Detach the old children instead of letting lexbor's
		 * text_content_set destroy_deep them: JS wrappers (and laid-out
		 * boxes) hold raw pointers into the subtree, so freeing it here
		 * turns any retained reference into a use-after-free. Detached
		 * nodes stay allocated until the document dies — same contract
		 * as the innerHTML setter below. */
        int is_char_data =
            (n->type == LXB_DOM_NODE_TYPE_TEXT || n->type == LXB_DOM_NODE_TYPE_COMMENT ||
             n->type == LXB_DOM_NODE_TYPE_CDATA_SECTION);
        char *old_value = NULL;
        if (is_char_data && dom_mo_any_interest(ctx, n, MO_CHARACTER_DATA)) {
            lxb_dom_character_data_t *cd = lxb_dom_interface_character_data(n);
            old_value = strndup((const char *)cd->data.data, cd->data.length);
        }
        while (n->first_child) {
            node_remove_safe(n->first_child);
        }
        lxb_dom_node_text_content_set(n, (const lxb_char_t *)s, slen);
        if (n->parent) {
            style_element_ingest(ctx, n);
        }
        JS_FreeCString(ctx, s);
        mark_dirty(ctx);
        /* A text/comment node's textContent IS its character data — Polymer's
		 * microtask scheduler mutates a text node here, so this path must fire
		 * a characterData record. An element's textContent is a childList
		 * change (children replaced by one text node). */
        if (is_char_data) {
            dom_mo_notify_character_data(ctx, n, old_value);
        } else {
            dom_mo_notify_child_list(ctx, n, n->first_child, NULL, NULL, NULL);
        }
        free(old_value);
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
    lxb_dom_node_t *n = unwrap_node(ctx, this_val);
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
    lxb_dom_node_t *n = unwrap_node(ctx, this_val);
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
        node_remove_safe(n->first_child);
    }
    /* Parse fragment under this element's context. */
    lxb_html_document_t *doc = lxb_html_interface_document(n->owner_document);
    if (n->type == LXB_DOM_NODE_TYPE_ELEMENT) {
        lxb_html_element_t *htmlel = lxb_html_interface_element(n);
        (void)lxb_html_element_inner_html_set(htmlel, (const lxb_char_t *)s, slen);
        /* Per spec, setting `.innerHTML` on a <template> parses into its
         * content DocumentFragment, not as element children. lexbor's
         * inner_html_set leaves the parsed nodes as regular children, so
         * relocate them into the content fragment — matching how the HTML
         * parser fills a parser-created template and keeping `.content`
         * authoritative (Polymer builds element templates this way). */
        if (n->local_name == LXB_TAG_TEMPLATE) {
            lxb_html_template_element_t *tmpl = lxb_html_interface_template(n);
            if (tmpl->content) {
                lxb_dom_node_t *fragment = lxb_dom_interface_node(tmpl->content);
                while (fragment->first_child) {
                    node_remove_safe(fragment->first_child);
                }
                lxb_dom_node_t *child;
                while ((child = n->first_child) != NULL) {
                    lxb_dom_node_remove(child);
                    lxb_dom_node_insert_child(fragment, child);
                }
            }
        }
    } else {
        (void)doc;
    }
    JS_FreeCString(ctx, s);
    mark_dirty(ctx);
    return JS_UNDEFINED;
}

static JSValue js_el_outerHTML_get(JSContext *ctx, JSValueConst this_val)
{
    lxb_dom_node_t *n = unwrap_node(ctx, this_val);
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
static lxb_dom_character_data_t *as_chardata(JSContext *ctx, JSValueConst v)
{
    lxb_dom_node_t *n = unwrap_node(ctx, v);
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
    lxb_dom_character_data_t *cd = as_chardata(ctx, this_val);
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
    lxb_dom_character_data_t *cd = as_chardata(ctx, this_val);
    if (!cd) {
        return JS_UNDEFINED;
    }
    size_t l;
    const char *s = JS_ToCStringLen(ctx, &l, v);
    if (!s) {
        return JS_UNDEFINED;
    }
    lxb_dom_node_t *node = lxb_dom_interface_node(cd);
    char *old_value = NULL;
    if (dom_mo_any_interest(ctx, node, MO_CHARACTER_DATA)) {
        old_value = strndup((const char *)cd->data.data, cd->data.length);
    }
    (void)lxb_dom_character_data_replace(cd, (const lxb_char_t *)s, l, 0, 0);
    JS_FreeCString(ctx, s);
    mark_dirty(ctx);
    dom_mo_notify_character_data(ctx, node, old_value);
    free(old_value);
    return JS_UNDEFINED;
}

static JSValue js_cd_length_get(JSContext *ctx, JSValueConst this_val)
{
    lxb_dom_character_data_t *cd = as_chardata(ctx, this_val);
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
    lxb_dom_character_data_t *cd = as_chardata(ctx, this_val);
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
    lxb_dom_character_data_t *cd = as_chardata(ctx, this_val);
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
    lxb_dom_character_data_t *cd = as_chardata(ctx, this_val);
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
    lxb_dom_character_data_t *cd = as_chardata(ctx, this_val);
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
    lxb_dom_character_data_t *cd = as_chardata(ctx, this_val);
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
    lxb_dom_element_t *el = unwrap_element(ctx, this_val);
    if (!el) {
        return JS_UNDEFINED;
    }
    /* tagName exists only on Element. Text/Comment/DocumentFragment share
	 * the same wrapper class, and qualified_name on a non-element reads
	 * through an element-only union member — a crash, not just junk. */
    if (lxb_dom_interface_node(el)->type != LXB_DOM_NODE_TYPE_ELEMENT) {
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
    lxb_dom_element_t *el = unwrap_attr_element(ctx, this_val);
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
    lxb_dom_element_t *el = unwrap_attr_element(ctx, this_val);
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
    lxb_dom_element_t *el = unwrap_attr_element(ctx, this_val);
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
    lxb_dom_element_t *el = unwrap_attr_element(ctx, this_val);
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
    lxb_dom_element_t *el = unwrap_attr_element(ctx, this_val);
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
    lxb_dom_element_t *el = unwrap_attr_element(ctx, this_val);
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

/* HTMLAnchorElement / HTMLAreaElement URL-decomposition IDL attributes:
 * protocol / host / hostname / port / pathname / search / hash / origin. Each
 * parses the element's `href` resolved to an absolute URL. YouTube's kevlar
 * router normalizes routes with `a.href = url; a.pathname` — with pathname
 * missing it returned undefined, `pathname.startsWith("/")` threw, the router
 * aborted, and the page-manager never stamped ytd-browse (blank homepage). */
enum anchor_url_part {
    ANCHOR_PROTOCOL,
    ANCHOR_HOST,
    ANCHOR_HOSTNAME,
    ANCHOR_PORT,
    ANCHOR_PATHNAME,
    ANCHOR_SEARCH,
    ANCHOR_HASH,
    ANCHOR_ORIGIN,
};

static JSValue anchor_url_component(JSContext *ctx, JSValueConst this_val, enum anchor_url_part which)
{
    lxb_dom_element_t *el = unwrap_attr_element(ctx, this_val);
    if (!el) {
        return JS_NewString(ctx, "");
    }
    size_t vlen = 0;
    const lxb_char_t *v = lxb_dom_element_get_attribute(el, (const lxb_char_t *)"href", 4, &vlen);
    if (!v || vlen == 0) {
        return JS_NewString(ctx, "");
    }
    struct yetty_ylexbor *r = runtime_ylex(ctx);
    char *raw = malloc(vlen + 1);
    if (!raw) {
        return JS_NewString(ctx, "");
    }
    memcpy(raw, v, vlen);
    raw[vlen] = '\0';
    char *abs = r ? yetty_ylexbor_resolve_url(r, raw) : NULL;
    const char *url = abs ? abs : raw;

    /* Split scheme://authority/rest. */
    size_t scheme_len = 0;
    const char *authority = NULL;
    const char *rest = url;
    const char *sep = strstr(url, "://");
    if (sep) {
        scheme_len = (size_t)(sep - url);
        authority = sep + 3;
        rest = authority + strcspn(authority, "/?#");
    }
    /* Host / port from the authority (userinfo stripped). */
    const char *host = NULL;
    size_t host_len = 0, port_len = 0;
    const char *port = NULL;
    if (authority) {
        const char *auth_end = rest;
        const char *at = memchr(authority, '@', (size_t)(auth_end - authority));
        const char *hstart = at ? at + 1 : authority;
        const char *colon = memchr(hstart, ':', (size_t)(auth_end - hstart));
        if (colon) {
            host = hstart;
            host_len = (size_t)(colon - hstart);
            port = colon + 1;
            port_len = (size_t)(auth_end - (colon + 1));
        } else {
            host = hstart;
            host_len = (size_t)(auth_end - hstart);
        }
    }
    /* Path / query / fragment from rest. */
    const char *frag = strchr(rest, '#');
    const char *query = strchr(rest, '?');
    if (frag && query && query > frag) {
        query = NULL; /* '?' after '#' is part of the fragment */
    }
    const char *path = rest;
    const char *path_end = query ? query : (frag ? frag : rest + strlen(rest));

    char out[2048];
    out[0] = '\0';
    switch (which) {
    case ANCHOR_PROTOCOL:
        if (scheme_len) {
            snprintf(out, sizeof(out), "%.*s:", (int)scheme_len, url);
        }
        break;
    case ANCHOR_HOSTNAME:
        if (host) {
            snprintf(out, sizeof(out), "%.*s", (int)host_len, host);
        }
        break;
    case ANCHOR_PORT:
        if (port) {
            snprintf(out, sizeof(out), "%.*s", (int)port_len, port);
        }
        break;
    case ANCHOR_HOST:
        if (host && port) {
            snprintf(out, sizeof(out), "%.*s:%.*s", (int)host_len, host, (int)port_len, port);
        } else if (host) {
            snprintf(out, sizeof(out), "%.*s", (int)host_len, host);
        }
        break;
    case ANCHOR_PATHNAME:
        if (path_end > path) {
            snprintf(out, sizeof(out), "%.*s", (int)(path_end - path), path);
        } else if (authority) {
            /* Empty path on a hierarchical URL normalizes to "/". */
            snprintf(out, sizeof(out), "/");
        }
        break;
    case ANCHOR_SEARCH:
        if (query) {
            const char *q_end = frag ? frag : query + strlen(query);
            if (q_end > query + 1) {
                snprintf(out, sizeof(out), "%.*s", (int)(q_end - query), query);
            }
        }
        break;
    case ANCHOR_HASH:
        if (frag && frag[1]) {
            snprintf(out, sizeof(out), "%s", frag);
        }
        break;
    case ANCHOR_ORIGIN:
        if (scheme_len && host) {
            if (port) {
                snprintf(out, sizeof(out), "%.*s://%.*s:%.*s", (int)scheme_len, url, (int)host_len,
                         host, (int)port_len, port);
            } else {
                snprintf(out, sizeof(out), "%.*s://%.*s", (int)scheme_len, url, (int)host_len, host);
            }
        }
        break;
    }
    free(raw);
    free(abs);
    return JS_NewString(ctx, out);
}

static JSValue js_el_protocol_get(JSContext *ctx, JSValueConst tv)
{
    return anchor_url_component(ctx, tv, ANCHOR_PROTOCOL);
}
static JSValue js_el_host_get(JSContext *ctx, JSValueConst tv)
{
    return anchor_url_component(ctx, tv, ANCHOR_HOST);
}
static JSValue js_el_hostname_get(JSContext *ctx, JSValueConst tv)
{
    return anchor_url_component(ctx, tv, ANCHOR_HOSTNAME);
}
static JSValue js_el_port_get(JSContext *ctx, JSValueConst tv)
{
    return anchor_url_component(ctx, tv, ANCHOR_PORT);
}
static JSValue js_el_pathname_get(JSContext *ctx, JSValueConst tv)
{
    return anchor_url_component(ctx, tv, ANCHOR_PATHNAME);
}
static JSValue js_el_search_get(JSContext *ctx, JSValueConst tv)
{
    return anchor_url_component(ctx, tv, ANCHOR_SEARCH);
}
static JSValue js_el_hash_get(JSContext *ctx, JSValueConst tv)
{
    return anchor_url_component(ctx, tv, ANCHOR_HASH);
}
static JSValue js_el_urlorigin_get(JSContext *ctx, JSValueConst tv)
{
    return anchor_url_component(ctx, tv, ANCHOR_ORIGIN);
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
    if (JS_GetOpaque(this_val, dom_state(ctx)->class_document_id)) {
        return JS_NewInt32(ctx, 9);
    }
    /* The element-class wrapper carries any non-Document node — Element,
	 * Text, Comment, ProcessingInstruction. Read the actual lexbor type
	 * so .nodeType returns the spec-correct value. */
    lxb_dom_node_t *n = (lxb_dom_node_t *)JS_GetOpaque(this_val, dom_state(ctx)->class_element_id);
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

/* isConnected — true when the node's ancestor chain reaches the document.
 * Shadow-root fragments hang off their host as regular children in this
 * tree, so the plain parent walk gives the spec behaviour (a node inside a
 * shadow tree is connected iff its host is). Frameworks gate their entire
 * lifecycle plumbing on this bit — e.g. a patched appendChild that only
 * fires connect callbacks when `inserted.isConnected` — so a missing
 * property (undefined = falsy) silently disables all of it. */
static JSValue js_el_isConnected_get(JSContext *ctx, JSValueConst this_val)
{
    if (JS_GetOpaque(this_val, dom_state(ctx)->class_document_id)) {
        return JS_TRUE;
    }
    lxb_dom_node_t *node =
        (lxb_dom_node_t *)JS_GetOpaque(this_val, dom_state(ctx)->class_element_id);
    for (; node; node = node->parent) {
        if (node->type == LXB_DOM_NODE_TYPE_DOCUMENT) {
            return JS_TRUE;
        }
    }
    return JS_FALSE;
}

static JSValue js_el_nodeName_get(JSContext *ctx, JSValueConst this_val)
{
    if (JS_GetOpaque(this_val, dom_state(ctx)->class_document_id)) {
        return JS_NewString(ctx, "#document");
    }
    lxb_dom_node_t *n = (lxb_dom_node_t *)JS_GetOpaque(this_val, dom_state(ctx)->class_element_id);
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
    lxb_dom_element_t *el = unwrap_element(ctx, this_val);
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

/* localName — the local part of an element's qualified name, in the
 * case it was parsed with (lowercase for HTML). Per DOM spec this is
 * null for non-element nodes (text, comment, document, fragment).
 * Distinct from nodeName, which uppercases for HTML elements. Several
 * libraries (e.g. Closure's DOM sanitizer) key per-element policy off
 * localName; a missing getter returns undefined and breaks them. */
static JSValue js_el_localName_get(JSContext *ctx, JSValueConst this_val)
{
    if (JS_GetOpaque(this_val, dom_state(ctx)->class_document_id)) {
        return JS_NULL;
    }
    lxb_dom_node_t *node =
        (lxb_dom_node_t *)JS_GetOpaque(this_val, dom_state(ctx)->class_element_id);
    if (node && node->type != LXB_DOM_NODE_TYPE_ELEMENT) {
        return JS_NULL;
    }
    lxb_dom_element_t *el = unwrap_element(ctx, this_val);
    if (!el) {
        return JS_NULL;
    }
    size_t len = 0;
    const lxb_char_t *name = lxb_dom_element_local_name(el, &len);
    if (!name) {
        return JS_NULL;
    }
    return JS_NewStringLen(ctx, (const char *)name, len);
}

/* ownerDocument — for any wrapped element this is the document we
 * minted document_obj for. We don't have a direct handle, so look it
 * up from the runtime opaque. Document's own ownerDocument is null per
 * spec. */
static JSValue js_el_ownerDocument_get(JSContext *ctx, JSValueConst this_val)
{
    if (JS_GetOpaque(this_val, dom_state(ctx)->class_document_id)) {
        return JS_NULL;
    }
    if (!JS_GetOpaque(this_val, dom_state(ctx)->class_element_id)) {
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

/* `hidden` — the BOOLEAN global HTML attribute, reflected as a property.
 * IDL_ATTR only handles STRING attributes; hidden is boolean: `el.hidden = true`
 * must ADD `hidden=""`, `el.hidden = false` must REMOVE it, getter is
 * attribute-presence. This reflection was MISSING, so frameworks that toggle
 * visibility via the property (Polymer binds `hidden="[[x]]"` → sets the
 * property, not the attribute) never actually hid the node — the box builder
 * hides by the `hidden` ATTRIBUTE. Concretely: YouTube's consent lightbox keeps
 * its `loading-overlay` / `error-overlay` divs hidden by binding their `hidden`
 * property; without reflection those overlays painted over the real Accept/Reject
 * choice, so the consent looked stuck on "Saving your choice / An error occurred". */
static JSValue js_el_hidden_get(JSContext *ctx, JSValueConst this_val)
{
    lxb_dom_element_t *el = unwrap_attr_element(ctx, this_val);
    if (!el) {
        return JS_FALSE;
    }
    size_t vlen = 0;
    const lxb_char_t *v = lxb_dom_element_get_attribute(el, (const lxb_char_t *)"hidden", 6, &vlen);
    return JS_NewBool(ctx, v != NULL);
}

static JSValue js_el_hidden_set(JSContext *ctx, JSValueConst this_val, JSValueConst val)
{
    lxb_dom_element_t *el = unwrap_attr_element(ctx, this_val);
    if (!el) {
        return JS_UNDEFINED;
    }
    int on = JS_ToBool(ctx, val);
    size_t vlen = 0;
    bool already =
        lxb_dom_element_get_attribute(el, (const lxb_char_t *)"hidden", 6, &vlen) != NULL;
    /* Reflect only a real change. Frameworks re-assign `el.hidden = false` on
     * already-visible nodes every render tick; without this guard each no-op
     * assignment marked the tree dirty and queued a MutationObserver record. */
    if ((on > 0) == already) {
        return JS_UNDEFINED;
    }
    char *old_value = mo_capture_attr(ctx, el, "hidden", 6);
    if (on > 0) {
        lxb_dom_element_set_attribute(el, (const lxb_char_t *)"hidden", 6, (const lxb_char_t *)"",
                                      0);
    } else {
        lxb_dom_element_remove_attribute(el, (const lxb_char_t *)"hidden", 6);
    }
    mark_dirty(ctx);
    dom_mo_notify_attributes(ctx, el, "hidden", 6, old_value);
    free(old_value);
    return JS_UNDEFINED;
}

/* parentElement / firstElementChild / nextElementSibling / children */

static JSValue js_el_parentElement_get(JSContext *ctx, JSValueConst this_val)
{
    lxb_dom_node_t *n = unwrap_node(ctx, this_val);
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
    lxb_dom_node_t *n = unwrap_node(ctx, this_val);
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
    lxb_dom_node_t *n = unwrap_node(ctx, this_val);
    return (n && n->first_child) ? wrap_node_any(ctx, n->first_child) : JS_NULL;
}
static JSValue js_el_lastChild_get(JSContext *ctx, JSValueConst this_val)
{
    lxb_dom_node_t *n = unwrap_node(ctx, this_val);
    return (n && n->last_child) ? wrap_node_any(ctx, n->last_child) : JS_NULL;
}
static JSValue js_el_nextSibling_get(JSContext *ctx, JSValueConst this_val)
{
    lxb_dom_node_t *n = unwrap_node(ctx, this_val);
    return (n && n->next) ? wrap_node_any(ctx, n->next) : JS_NULL;
}
static JSValue js_el_previousSibling_get(JSContext *ctx, JSValueConst this_val)
{
    lxb_dom_node_t *n = unwrap_node(ctx, this_val);
    return (n && n->prev) ? wrap_node_any(ctx, n->prev) : JS_NULL;
}
static JSValue js_el_parentNode_get(JSContext *ctx, JSValueConst this_val)
{
    lxb_dom_node_t *n = unwrap_node(ctx, this_val);
    return (n && n->parent) ? wrap_node_any(ctx, n->parent) : JS_NULL;
}
/* childNodes — a live-ish NodeList approximated by a JS array (has .length and
 * integer indexing, which is all real code uses). jQuery's parseHTML reads
 * `body.childNodes.length`. */
static JSValue js_el_childNodes_get(JSContext *ctx, JSValueConst this_val)
{
    JSValue arr = JS_NewArray(ctx);
    lxb_dom_node_t *n = unwrap_node(ctx, this_val);
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
    lxb_dom_node_t *n = unwrap_node(ctx, this_val);
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
    lxb_dom_node_t *n = unwrap_node(ctx, this_val);
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
    lxb_dom_element_t *el = JS_GetOpaque(this_val, dom_state(ctx)->class_style_id);
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
    lxb_dom_element_t *el = JS_GetOpaque(this_val, dom_state(ctx)->class_style_id);
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
            mark_dirty_inline_prop(ctx, k);
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
    lxb_dom_element_t *el = JS_GetOpaque(obj, dom_state(ctx)->class_style_id);
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
    lxb_dom_element_t *el = JS_GetOpaque(obj, dom_state(ctx)->class_style_id);
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
    if (!parse_buf) {
        free(buf);
        JS_FreeCString(ctx, vstr);
        return -1;
    }
    int n = parse_style_decl(parse_buf, kvs, 64);

    /* Build new decl string. The rebuild loop re-serializes every existing
     * declaration with normalized "; " and ": " separators (up to 4 bytes
     * per pair more than the compact ";"/":" the source attribute may use),
     * plus the appended key/value. `alen + klen + vlen + 8` did not account
     * for that per-pair expansion and overflowed `out` for inputs with
     * several compact declarations. Reserve 4 bytes per existing pair plus
     * one appended pair, and the terminating NUL. */
    size_t cap = alen + klen + vlen + 4 * ((size_t)n + 1) + 1;
    char *out = malloc(cap);
    if (!out) {
        free(buf);
        free(parse_buf);
        JS_FreeCString(ctx, vstr);
        return -1;
    }
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
    mark_dirty_inline_prop(ctx, kebab);

    free(buf);
    free(parse_buf);
    free(out);
    JS_FreeCString(ctx, vstr);
    return 1; /* property set */
}

static JSValue js_el_style_get(JSContext *ctx, JSValueConst this_val)
{
    lxb_dom_element_t *el = unwrap_attr_element(ctx, this_val);
    if (!el) {
        return JS_UNDEFINED;
    }
    JSValue v = JS_NewObjectClass(ctx, dom_state(ctx)->class_style_id);
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
    lxb_dom_element_t *el = JS_GetOpaque(this_val, dom_state(ctx)->class_classlist_id);
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
    lxb_dom_element_t *el = JS_GetOpaque(this_val, dom_state(ctx)->class_classlist_id);
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
    lxb_dom_element_t *el = JS_GetOpaque(this_val, dom_state(ctx)->class_classlist_id);
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
    lxb_dom_element_t *el = JS_GetOpaque(this_val, dom_state(ctx)->class_classlist_id);
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
    lxb_dom_element_t *el = JS_GetOpaque(this_val, dom_state(ctx)->class_classlist_id);
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
    lxb_dom_element_t *el = JS_GetOpaque(this_val, dom_state(ctx)->class_classlist_id);
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
    lxb_dom_element_t *el = JS_GetOpaque(this_val, dom_state(ctx)->class_classlist_id);
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
    lxb_dom_element_t *el = JS_GetOpaque(this_val, dom_state(ctx)->class_classlist_id);
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

/* Define `object[Symbol.iterator] = fn`. QuickJS's function-list installer keys
 * on plain string atoms, so a well-known symbol has to be attached by hand. */
static void js_dom_define_iterator(JSContext *ctx, JSValueConst object, JSValue fn)
{
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue symbol_ctor = JS_GetPropertyStr(ctx, global, "Symbol");
    JSValue iterator_sym = JS_GetPropertyStr(ctx, symbol_ctor, "iterator");
    JSAtom iterator_atom = JS_ValueToAtom(ctx, iterator_sym);
    JS_DefinePropertyValue(ctx, object, iterator_atom, fn, JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE);
    JS_FreeAtom(ctx, iterator_atom);
    JS_FreeValue(ctx, iterator_sym);
    JS_FreeValue(ctx, symbol_ctor);
    JS_FreeValue(ctx, global);
}

/* classList[Symbol.iterator]() — DOMTokenList is iterable. Build an array of
 * the current class tokens and hand back its own array iterator so that
 * `[...el.classList]` / `for (const c of el.classList)` work. */
static JSValue js_classlist_symbol_iterator(JSContext *ctx, JSValueConst this_val, int argc,
                                            JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    JSValue arr = JS_NewArray(ctx);
    lxb_dom_element_t *el = JS_GetOpaque(this_val, dom_state(ctx)->class_classlist_id);
    uint32_t count = 0;
    if (el) {
        size_t clen = 0;
        const lxb_char_t *cls =
            lxb_dom_element_get_attribute(el, (const lxb_char_t *)"class", 5, &clen);
        if (cls) {
            size_t start = 0;
            for (size_t pos = 0; pos <= clen; pos++) {
                int is_ws = (pos == clen) || cls[pos] == ' ' || cls[pos] == '\t' ||
                            cls[pos] == '\n' || cls[pos] == '\r' || cls[pos] == '\f';
                if (is_ws) {
                    if (pos > start) {
                        JS_SetPropertyUint32(
                            ctx, arr, count++,
                            JS_NewStringLen(ctx, (const char *)(cls + start), pos - start));
                    }
                    start = pos + 1;
                }
            }
        }
    }
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue symbol_ctor = JS_GetPropertyStr(ctx, global, "Symbol");
    JSValue iterator_sym = JS_GetPropertyStr(ctx, symbol_ctor, "iterator");
    JSAtom iterator_atom = JS_ValueToAtom(ctx, iterator_sym);
    JSValue array_iter_fn = JS_GetProperty(ctx, arr, iterator_atom);
    JSValue result = JS_Call(ctx, array_iter_fn, arr, 0, NULL);
    JS_FreeValue(ctx, array_iter_fn);
    JS_FreeAtom(ctx, iterator_atom);
    JS_FreeValue(ctx, iterator_sym);
    JS_FreeValue(ctx, symbol_ctor);
    JS_FreeValue(ctx, global);
    JS_FreeValue(ctx, arr);
    return result;
}

static JSValue js_el_classList_get(JSContext *ctx, JSValueConst this_val)
{
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
    lxb_dom_element_t *el = unwrap_attr_element(ctx, this_val);
    if (!el) {
        return JS_UNDEFINED;
    }
    JSValue v = JS_NewObjectClass(ctx, dom_state(ctx)->class_classlist_id);
    JS_SetOpaque(v, el);
    JS_SetPropertyFunctionList(ctx, v, classlist_funcs,
                               sizeof(classlist_funcs) / sizeof(classlist_funcs[0]));
    js_dom_define_iterator(
        ctx, v, JS_NewCFunction(ctx, js_classlist_symbol_iterator, "[Symbol.iterator]", 0));
    return v;
}

/* ===========================================================================
 * Event listener storage — hidden array property on the JS wrapper.
 * Wrapper objects are minted fresh per call though, so we use a
 * per-runtime map (js_dom_state.listeners): lxb_dom_element_t* → array
 * of (type, handler) pairs.
 * ===========================================================================*/

static JSValue js_el_addEventListener(JSContext *ctx, JSValueConst this_val, int argc,
                                      JSValueConst *argv)
{
    struct js_dom_state *state = dom_state(ctx);
    if (!state || argc < 2) {
        return JS_UNDEFINED;
    }
    /* Accept Element (target=el) and Document/window (target=NULL,
	 * receives global events like DOMContentLoaded/load). */
    lxb_dom_element_t *el = unwrap_element(ctx, this_val);
    if (state->listener_count >= MAX_LISTENERS) {
        return JS_UNDEFINED;
    }
    const char *type = JS_ToCString(ctx, argv[0]);
    if (!type) {
        return JS_UNDEFINED;
    }
    struct listener *entry = &state->listeners[state->listener_count++];
    entry->el = el; /* may be NULL for document/window listeners */
    strncpy(entry->type, type, sizeof(entry->type) - 1);
    entry->type[sizeof(entry->type) - 1] = '\0';
    entry->handler = JS_DupValue(ctx, argv[1]);
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
    void *self_p = JS_GetOpaque(this_val, dom_state(ctx)->class_element_id);
    if (!self_p) {
        self_p = JS_GetOpaque(this_val, dom_state(ctx)->class_document_id);
    }
    void *other_p = JS_GetOpaque(argv[0], dom_state(ctx)->class_element_id);
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
    lxb_dom_node_t *n = unwrap_node(ctx, this_val);
    return JS_NewBool(ctx, n && n->first_child != NULL);
}

static JSValue js_el_hasAttributes_stub(JSContext *ctx, JSValueConst this_val, int argc,
                                        JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    lxb_dom_element_t *el = unwrap_element(ctx, this_val);
    /* Only real elements carry attributes. The wrapper for a DocumentFragment
     * (e.g. a <template>.content node) shares the element JS class, so
     * unwrap_element returns a non-NULL pointer for it — reading `first_attr`
     * off a non-element struct yields garbage that reads as "has attributes".
     * Polymer's template parser calls `content.hasAttributes()` and asserts the
     * result is only truthy for a node it built nodeInfo for; a spurious true
     * makes it throw and abandon that element's template. Guard on node type. */
    if (!el || ((lxb_dom_node_t *)el)->type != LXB_DOM_NODE_TYPE_ELEMENT) {
        return JS_FALSE;
    }
    return JS_NewBool(ctx, el->first_attr != NULL);
}

static JSValue js_el_getAttributeNames_stub(JSContext *ctx, JSValueConst this_val, int argc,
                                            JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    lxb_dom_element_t *el = unwrap_element(ctx, this_val);
    JSValue arr = JS_NewArray(ctx);
    if (!el || ((lxb_dom_node_t *)el)->type != LXB_DOM_NODE_TYPE_ELEMENT) {
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

/* element.attributes — a NamedNodeMap. Returned as a real JS Array so it is
 * spread-able / for-of iterable (`[...el.attributes]`) and index/length
 * addressable out of the box, with `item()` and `getNamedItem()` added on top
 * for the NamedNodeMap surface. Polymer/kevlar element constructors spread
 * `[...this.attributes]`; an undefined value there throws
 * "cannot read property 'Symbol.iterator' of undefined". */
static JSValue js_el_attr_item(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    if (argc < 1) {
        return JS_NULL;
    }
    int32_t index = 0;
    if (JS_ToInt32(ctx, &index, argv[0]) < 0) {
        return JS_NULL;
    }
    JSValue at = JS_GetPropertyUint32(ctx, this_val, (uint32_t)index);
    if (JS_IsUndefined(at)) {
        return JS_NULL;
    }
    return at;
}

static JSValue js_el_attr_getNamedItem(JSContext *ctx, JSValueConst this_val, int argc,
                                       JSValueConst *argv)
{
    if (argc < 1) {
        return JS_NULL;
    }
    const char *want = JS_ToCString(ctx, argv[0]);
    if (!want) {
        return JS_NULL;
    }
    JSValue result = JS_NULL;
    uint32_t length = 0;
    JSValue length_val = JS_GetPropertyStr(ctx, this_val, "length");
    JS_ToUint32(ctx, &length, length_val);
    JS_FreeValue(ctx, length_val);
    for (uint32_t i = 0; i < length; i++) {
        JSValue entry = JS_GetPropertyUint32(ctx, this_val, i);
        JSValue name_val = JS_GetPropertyStr(ctx, entry, "name");
        const char *name = JS_ToCString(ctx, name_val);
        if (name && strcmp(name, want) == 0) {
            JS_FreeCString(ctx, name);
            JS_FreeValue(ctx, name_val);
            result = entry;
            break;
        }
        if (name) {
            JS_FreeCString(ctx, name);
        }
        JS_FreeValue(ctx, name_val);
        JS_FreeValue(ctx, entry);
    }
    JS_FreeCString(ctx, want);
    return result;
}

static JSValue js_el_attributes_get(JSContext *ctx, JSValueConst this_val)
{
    JSValue arr = JS_NewArray(ctx);
    lxb_dom_element_t *el = unwrap_element(ctx, this_val);
    uint32_t i = 0;
    /* The element-class wrapper also carries Text/Comment nodes; reading
     * ->first_attr off a non-element node walks garbage. Only Elements
     * have an attribute list. */
    if (el && ((lxb_dom_node_t *)el)->type == LXB_DOM_NODE_TYPE_ELEMENT) {
        for (lxb_dom_attr_t *a = el->first_attr; a; a = a->next) {
            size_t nlen = 0;
            const lxb_char_t *nm = lxb_dom_attr_qualified_name(a, &nlen);
            size_t llen = 0;
            const lxb_char_t *ln = lxb_dom_attr_local_name(a, &llen);
            size_t vlen = 0;
            const lxb_char_t *va = lxb_dom_attr_value(a, &vlen);
            JSValue entry = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, entry, "name",
                              JS_NewStringLen(ctx, nm ? (const char *)nm : "", nm ? nlen : 0));
            JS_SetPropertyStr(ctx, entry, "localName",
                              JS_NewStringLen(ctx, ln ? (const char *)ln : "", ln ? llen : 0));
            JS_SetPropertyStr(ctx, entry, "value",
                              JS_NewStringLen(ctx, va ? (const char *)va : "", va ? vlen : 0));
            JS_SetPropertyStr(ctx, entry, "namespaceURI", JS_NULL);
            JS_SetPropertyStr(ctx, entry, "prefix", JS_NULL);
            JS_SetPropertyStr(ctx, entry, "specified", JS_TRUE);
            JS_SetPropertyUint32(ctx, arr, i++, entry);
        }
    }
    JS_SetPropertyStr(ctx, arr, "item", JS_NewCFunction(ctx, js_el_attr_item, "item", 1));
    JS_SetPropertyStr(ctx, arr, "getNamedItem",
                      JS_NewCFunction(ctx, js_el_attr_getNamedItem, "getNamedItem", 1));
    return arr;
}

static JSValue js_el_cloneNode_stub(JSContext *ctx, JSValueConst this_val, int argc,
                                    JSValueConst *argv)
{
    lxb_dom_node_t *self = unwrap_node(ctx, this_val);
    if (!self) {
        return JS_DupValue(ctx, this_val);
    }
    int deep = argc > 0 ? JS_ToBool(ctx, argv[0]) : 0;
    lxb_dom_node_t *cl = lxb_dom_node_clone(self, deep ? true : false);
    if (!cl) {
        return JS_NULL;
    }
    if (deep) {
        /* lexbor does not clone a <template>'s separate content fragment; do it
         * ourselves so stamping frameworks see the cloned contents. */
        clone_fixup_template_content(self, cl);
    }
    /* Wrap the CLONE (any node type) — a document fragment or text clone must
     * not fall through to returning the original node. */
    return wrap_node_any(ctx, cl);
}

/* CSSOM forced-layout point. offsetWidth/Height, clientWidth/Height and
 * getBoundingClientRect() are defined against *current* layout, so a browser
 * synchronously flushes pending style + layout before returning them. ybrowser
 * otherwise returns the last completed layout, so JS that inserts a node and
 * immediately measures it — e.g. a framework's dataChanged() reading clientWidth
 * to size a grid — reads 0 and lays out wrong. Flush only when the DOM was
 * mutated since the last layout (dom_dirty), and guard against reentrancy: a
 * geometry read reached from inside the flush's own box-build/layout returns the
 * in-progress layout rather than recursing. Observer callbacks (ResizeObserver /
 * MutationObserver) are NOT delivered here — that stays asynchronous. */
static void geometry_flush_pending_layout(struct yetty_ylexbor *r)
{
    /* Only force a full relayout when a LAYOUT-affecting mutation is pending.
	 * A paint-only change (opacity/color/…) leaves box geometry current, so the
	 * read is served from the last layout — this is what spares youtube's
	 * paint-only animation frames the O(page) rebuild. */
    if (r == NULL || !r->layout_dirty || r->layout_in_progress) {
        return;
    }
    r->forced_flush_count++;
    struct timespec flush_t0;
    clock_gettime(CLOCK_MONOTONIC, &flush_t0);
    /* Box-build + layout only — NOT the full relayout: a geometry getter must
     * flush style/layout without initiating iframe resolution/fetch/DOM work
     * while JS is synchronously reading geometry. The callee sets/clears
     * layout_in_progress and restores dom_dirty on failure. */
    struct yetty_ycore_void_result res = yetty_ylexbor_relayout_boxes_and_layout(r);
    if (YETTY_IS_ERR(res)) {
        /* A getter must not throw a synchronous JS exception for a transient
         * layout failure — trace it and fall back to the last completed
         * layout (dom_dirty was restored, so the next read retries). */
        ydebug("geometry-flush relayout failed: %s", res.error.msg ? res.error.msg : "?");
        yetty_ycore_error_destroy(res.error);
    }
    struct timespec flush_t1;
    clock_gettime(CLOCK_MONOTONIC, &flush_t1);
    r->forced_flush_ns += (uint64_t)(flush_t1.tv_sec - flush_t0.tv_sec) * 1000000000ULL +
                          (uint64_t)(flush_t1.tv_nsec - flush_t0.tv_nsec);
}

/* getBoundingClientRect — return the element's real laid-out rectangle (union
 * of its boxes) instead of a zero stub. As a CSSOM forced-layout point it first
 * flushes any pending layout (geometry_flush_pending_layout), so even a call
 * from an initial inline script that just mutated the DOM sees current geometry
 * rather than a stale/empty layout. An element with no box (disconnected /
 * display:none / never laid out) still reports zeros. */
static JSValue js_el_getBoundingClientRect(JSContext *ctx, JSValueConst this_val, int argc,
                                           JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    struct yetty_ylexbor *r = runtime_ylex(ctx);
    geometry_flush_pending_layout(r);
    lxb_dom_node_t *node = unwrap_node(ctx, this_val);
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

static void computed_set_px(JSContext *ctx, JSValue obj, const char *name, float value)
{
    char buf[48];
    snprintf(buf, sizeof(buf), "%gpx", (double)value);
    JS_SetPropertyStr(ctx, obj, name, JS_NewString(ctx, buf));
}

/* window.getComputedStyle(el) — a resolved-value view backed by the element's
 * laid-out box. The inline-style object is installed as the RESULT'S PROTOTYPE
 * so the returned declaration still answers getPropertyValue / setProperty /
 * item / cssText and any inline property that layout does not override — a plain
 * fresh object would be missing those methods and breaks callers that use them.
 * On top of that we set the box-derived resolved properties that overlay /
 * positioning code (iron-fit-behavior, overlay managers) reads: position, the
 * four inset sides, the four margins/paddings, max-width/height, box-sizing and
 * z-index. An element with no laid-out box (display:none, disconnected) returns
 * the pure inline view — a browser likewise returns empty resolved values for an
 * unrendered element, so no box-derived overrides are added. */
JSValue yetty_ylexbor_js_getComputedStyle(JSContext *ctx, JSValueConst this_val, int argc,
                                          JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) {
        return JS_NULL;
    }
    JSValue inline_style = JS_GetPropertyStr(ctx, argv[0], "style");
    JSValue out = JS_NewObject(ctx);
    if (JS_IsObject(inline_style)) {
        JS_SetPrototype(ctx, out, inline_style);
    }

    struct yetty_ylexbor *r = runtime_ylex(ctx);
    lxb_dom_node_t *node = unwrap_node(ctx, argv[0]);
    const struct yetty_ylexbor_box *box = NULL;
    if (r != NULL && node != NULL && node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
        geometry_flush_pending_layout(r);
        lxb_dom_element_t *el = lxb_dom_interface_element(node);
        for (uint32_t i = 0; i < r->boxes.size; i++) {
            if (r->boxes.data[i].element == el) {
                box = &r->boxes.data[i];
                break;
            }
        }
    }

    if (box != NULL) {
        const char *pos = "static";
        switch (box->position) {
        case YL_POS_RELATIVE:
            pos = "relative";
            break;
        case YL_POS_ABSOLUTE:
            pos = "absolute";
            break;
        case YL_POS_FIXED:
            pos = "fixed";
            break;
        default:
            pos = "static";
            break;
        }
        JS_SetPropertyStr(ctx, out, "position", JS_NewString(ctx, pos));

        /* Inset sides resolve to a px value when specified, else `auto`
         * (0px is a real offset, distinct from auto — hence the mask). */
        if (box->pos_set_mask & 0x1) {
            computed_set_px(ctx, out, "top", box->pos_top);
        } else {
            JS_SetPropertyStr(ctx, out, "top", JS_NewString(ctx, "auto"));
        }
        if (box->pos_set_mask & 0x2) {
            computed_set_px(ctx, out, "right", box->pos_right);
        } else {
            JS_SetPropertyStr(ctx, out, "right", JS_NewString(ctx, "auto"));
        }
        if (box->pos_set_mask & 0x4) {
            computed_set_px(ctx, out, "bottom", box->pos_bottom);
        } else {
            JS_SetPropertyStr(ctx, out, "bottom", JS_NewString(ctx, "auto"));
        }
        if (box->pos_set_mask & 0x8) {
            computed_set_px(ctx, out, "left", box->pos_left);
        } else {
            JS_SetPropertyStr(ctx, out, "left", JS_NewString(ctx, "auto"));
        }

        computed_set_px(ctx, out, "marginTop", box->margin_top);
        computed_set_px(ctx, out, "marginRight", box->margin_right);
        computed_set_px(ctx, out, "marginBottom", box->margin_bottom);
        computed_set_px(ctx, out, "marginLeft", box->margin_left);

        computed_set_px(ctx, out, "paddingTop", box->padding_top);
        computed_set_px(ctx, out, "paddingRight", box->padding_right);
        computed_set_px(ctx, out, "paddingBottom", box->padding_bottom);
        computed_set_px(ctx, out, "paddingLeft", box->padding_left);

        if (box->css_max_width > 0.0f) {
            computed_set_px(ctx, out, "maxWidth", box->css_max_width);
        } else {
            JS_SetPropertyStr(ctx, out, "maxWidth", JS_NewString(ctx, "none"));
        }
        JS_SetPropertyStr(ctx, out, "maxHeight", JS_NewString(ctx, "none"));

        JS_SetPropertyStr(ctx, out, "boxSizing",
                          JS_NewString(ctx, box->border_box ? "border-box" : "content-box"));

        if (box->z_index_set) {
            JS_SetPropertyStr(ctx, out, "zIndex", JS_NewInt32(ctx, box->z_index));
        } else {
            JS_SetPropertyStr(ctx, out, "zIndex", JS_NewString(ctx, "auto"));
        }
    }

    JS_FreeValue(ctx, inline_style);
    return out;
}

/* clientWidth/Height + offsetWidth/Height. All four derive from the same
 * laid-out box union getBoundingClientRect() uses (border box), so the box-kind
 * knowledge lives here once rather than being duplicated per getter:
 *   offset* = border-box dimensions (integer CSS px);
 *   client* = padding-box = border-box minus the two side borders.
 * A disconnected / unlaid-out / display:none element has no matching box and
 * reports 0 (spec). Reading a metric first flushes any pending layout (see
 * geometry_flush_pending_layout — a CSSOM forced-layout point) but never itself
 * dirties layout; post-layout change notification remains ResizeObserver's job. */
struct el_box_metrics {
    int client_w, client_h, offset_w, offset_h;
    bool found;
};

static struct el_box_metrics element_box_metrics(struct yetty_ylexbor *r, lxb_dom_element_t *el)
{
    struct el_box_metrics m = {0, 0, 0, 0, false};
    if (r == NULL || el == NULL) {
        return m;
    }
    float min_x = 0.0f, min_y = 0.0f, max_x = 0.0f, max_y = 0.0f;
    float border_left = 0.0f, border_right = 0.0f, border_top = 0.0f, border_bottom = 0.0f;
    for (uint32_t i = 0; i < r->boxes.size; i++) {
        struct yetty_ylexbor_box *b = &r->boxes.data[i];
        if (b->element != el) {
            continue;
        }
        if (!m.found) {
            min_x = b->x;
            min_y = b->y;
            max_x = b->x + b->w;
            max_y = b->y + b->h;
            /* Principal box carries the border widths for the padding-box math. */
            border_left = b->border_left;
            border_right = b->border_right;
            border_top = b->border_top;
            border_bottom = b->border_bottom;
            m.found = true;
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
    if (!m.found) {
        return m;
    }
    float border_box_w = max_x - min_x;
    float border_box_h = max_y - min_y;
    float client_w = border_box_w - border_left - border_right;
    float client_h = border_box_h - border_top - border_bottom;
    if (client_w < 0.0f) {
        client_w = 0.0f;
    }
    if (client_h < 0.0f) {
        client_h = 0.0f;
    }
    m.offset_w = (int)(border_box_w + 0.5f);
    m.offset_h = (int)(border_box_h + 0.5f);
    m.client_w = (int)(client_w + 0.5f);
    m.client_h = (int)(client_h + 0.5f);
    return m;
}

/* The document's root element (<html>) — the first element child of the
 * document. CSSOM gives it special client-size semantics (the viewport). */
static lxb_dom_element_t *document_root_element(struct yetty_ylexbor *r)
{
    if (r == NULL || r->document == NULL) {
        return NULL;
    }
    lxb_dom_node_t *doc_node = lxb_dom_interface_node(r->document);
    for (lxb_dom_node_t *c = doc_node->first_child; c; c = c->next) {
        if (c->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            return lxb_dom_interface_element(c);
        }
    }
    return NULL;
}

static struct el_box_metrics el_metrics_of(JSContext *ctx, JSValueConst this_val)
{
    struct el_box_metrics m = {0, 0, 0, 0, false};
    struct yetty_ylexbor *r = runtime_ylex(ctx);
    geometry_flush_pending_layout(r);
    lxb_dom_node_t *node = unwrap_node(ctx, this_val);
    if (r == NULL || node == NULL || node->type != LXB_DOM_NODE_TYPE_ELEMENT) {
        return m;
    }
    lxb_dom_element_t *el = lxb_dom_interface_element(node);
    m = element_box_metrics(r, el);
    /* CSSOM standards-mode root-element special case: documentElement's
     * clientWidth/clientHeight report the layout viewport (not the root box's
     * content size — the root box is viewport-wide but content-tall). offset*
     * and getBoundingClientRect keep the real box geometry. body and every other
     * element use ordinary box metrics. */
    if (el != NULL && el == document_root_element(r)) {
        m.client_w = r->viewport_w;
        m.client_h = r->viewport_h;
        m.found = true;
    }
    return m;
}

static JSValue js_el_clientWidth_get(JSContext *ctx, JSValueConst this_val)
{
    return JS_NewInt32(ctx, el_metrics_of(ctx, this_val).client_w);
}

static JSValue js_el_clientHeight_get(JSContext *ctx, JSValueConst this_val)
{
    return JS_NewInt32(ctx, el_metrics_of(ctx, this_val).client_h);
}

static JSValue js_el_offsetWidth_get(JSContext *ctx, JSValueConst this_val)
{
    return JS_NewInt32(ctx, el_metrics_of(ctx, this_val).offset_w);
}

static JSValue js_el_offsetHeight_get(JSContext *ctx, JSValueConst this_val)
{
    return JS_NewInt32(ctx, el_metrics_of(ctx, this_val).offset_h);
}

/* Fire every registered listener for (el, type) at one propagation level.
 * `current_target` is the wrapper for the level being visited; the event's
 * target property was already set to the dispatch origin by the caller.
 * Returns 1 when a handler called stopImmediatePropagation(). */
static int dispatch_fire_level(JSContext *ctx, struct js_dom_state *state, lxb_dom_element_t *el,
                               const char *type, JSValueConst event, JSValueConst current_target,
                               int snapshot)
{
    JS_SetPropertyStr(ctx, (JSValue)event, "currentTarget", JS_DupValue(ctx, current_target));
    for (int i = 0; i < snapshot; i++) {
        if (state->listeners[i].el != el || strcmp(state->listeners[i].type, type) != 0) {
            continue;
        }
        JSValueConst call_args[] = {event};
        JSValue ret = JS_Call(ctx, state->listeners[i].handler, current_target, 1, call_args);
        if (JS_IsException(ret)) {
            JSValue exc = JS_GetException(ctx);
            JS_FreeValue(ctx, exc);
        } else {
            JS_FreeValue(ctx, ret);
        }
        JSValue imm = JS_GetPropertyStr(ctx, (JSValue)event, "__immediateStopped");
        int stopped = JS_ToBool(ctx, imm);
        JS_FreeValue(ctx, imm);
        if (stopped) {
            return 1;
        }
    }
    return 0;
}

/* True when a handler called stopPropagation() (or stopImmediate…) on the
 * event — the flags are set by the Event constructor stubs. Events built as
 * plain object literals carry no flag and simply keep propagating. */
static int dispatch_propagation_stopped(JSContext *ctx, JSValueConst event)
{
    JSValue flag = JS_GetPropertyStr(ctx, (JSValue)event, "__propagationStopped");
    int stopped = JS_ToBool(ctx, flag);
    JS_FreeValue(ctx, flag);
    return stopped;
}

/* Real EventTarget.dispatchEvent: invoke every registered listener whose type
 * matches the event, starting at the dispatch target and — for events created
 * with bubbles:true — walking up the ancestor chain, firing each ancestor's
 * listeners with currentTarget updated per level, then the document/window
 * bucket (el == NULL) at the top. Shadow-root fragments in the parent chain
 * are crossed only for composed events (non-composed events stay inside their
 * shadow tree, per spec). Web-component UIs depend on bubbling wholesale: a
 * child fires a CustomEvent and the app root listens for it — without the
 * upward walk every such notification silently matches zero listeners.
 * Returns !event.defaultPrevented. */
static JSValue js_el_dispatchEvent(JSContext *ctx, JSValueConst this_val, int argc,
                                   JSValueConst *argv)
{
    if (argc < 1) {
        return JS_TRUE;
    }
    struct js_dom_state *state = dom_state(ctx);
    if (!state) {
        return JS_TRUE;
    }
    /* Re-entrancy guard: a handler that synchronously re-dispatches the event
	 * it is handling would recurse forever and freeze the page. Cap nesting. */
    if (state->dispatch_depth > 32) {
        return JS_TRUE;
    }
    JSValue type_v = JS_GetPropertyStr(ctx, argv[0], "type");
    const char *type = JS_ToCString(ctx, type_v);
    JS_FreeValue(ctx, type_v);
    if (type == NULL) {
        return JS_TRUE;
    }
    state->dispatch_depth++;
    lxb_dom_element_t *el = unwrap_element(ctx, this_val); /* NULL for document/window */
    JS_SetPropertyStr(ctx, (JSValue)argv[0], "target", JS_DupValue(ctx, this_val));
    /* Snapshot the count: a handler may addEventListener during dispatch and
	 * those new listeners must not fire for this same event. */
    int snapshot = state->listener_count;
    int immediate_stopped = dispatch_fire_level(ctx, state, el, type, argv[0], this_val, snapshot);

    JSValue bubbles_v = JS_GetPropertyStr(ctx, (JSValue)argv[0], "bubbles");
    int bubbles = JS_ToBool(ctx, bubbles_v);
    JS_FreeValue(ctx, bubbles_v);
    JSValue composed_v = JS_GetPropertyStr(ctx, (JSValue)argv[0], "composed");
    int composed = JS_ToBool(ctx, composed_v);
    JS_FreeValue(ctx, composed_v);

    if (el != NULL && bubbles && !immediate_stopped &&
        !dispatch_propagation_stopped(ctx, argv[0])) {
        for (lxb_dom_node_t *node = lxb_dom_interface_node(el)->parent; node; node = node->parent) {
            if (node->type == LXB_DOM_NODE_TYPE_DOCUMENT_FRAGMENT && !composed) {
                break; /* non-composed events stay inside their shadow tree */
            }
            if (node->type != LXB_DOM_NODE_TYPE_ELEMENT) {
                continue;
            }
            lxb_dom_element_t *ancestor = lxb_dom_interface_element(node);
            JSValue ancestor_wrap = wrap_element(ctx, ancestor);
            immediate_stopped =
                dispatch_fire_level(ctx, state, ancestor, type, argv[0], ancestor_wrap, snapshot);
            JS_FreeValue(ctx, ancestor_wrap);
            if (immediate_stopped || dispatch_propagation_stopped(ctx, argv[0])) {
                break;
            }
        }
        /* Top of the tree: the document/window listener bucket. */
        if (!immediate_stopped && !dispatch_propagation_stopped(ctx, argv[0])) {
            JSValue global = JS_GetGlobalObject(ctx);
            JSValue doc_obj = JS_GetPropertyStr(ctx, global, "document");
            JS_FreeValue(ctx, global);
            dispatch_fire_level(ctx, state, NULL, type, argv[0],
                                JS_IsObject(doc_obj) ? doc_obj : this_val, snapshot);
            JS_FreeValue(ctx, doc_obj);
        }
    }
    JS_FreeCString(ctx, type);
    state->dispatch_depth--;
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
    lxb_dom_element_t *el = unwrap_attr_element(ctx, this_val);
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

/* Constructor stub for the DOM interface globals (EventTarget/Node/Element/…).
 * `new Element()` returns a plain object whose prototype is the constructor's
 * .prototype so `class X extends HTMLElement {…}` (custom elements) and the
 * Shady-DOM polyfill's `X.prototype` walks both work. Instances of real DOM
 * nodes come from wrap_element(), not from calling these. */
static JSValue js_dom_iface_ctor(JSContext *ctx, JSValueConst new_target, int argc,
                                 JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    JSValue proto = JS_GetPropertyStr(ctx, new_target, "prototype");
    JSValue self = JS_IsObject(proto) ? JS_NewObjectProto(ctx, proto) : JS_NewObject(ctx);
    JS_FreeValue(ctx, proto);
    return self;
}

/* Install a DOM interface constructor `name` on the global with `.prototype`
 * set to `proto` (a borrowed ref — this dups what it keeps). */
static void install_dom_iface(JSContext *ctx, JSValueConst global, const char *name,
                              JSValueConst proto)
{
    JSValue ctor = JS_NewCFunction2(ctx, js_dom_iface_ctor, name, 0, JS_CFUNC_constructor, 0);
    JS_SetPropertyStr(ctx, ctor, "prototype", JS_DupValue(ctx, proto));
    JS_DefinePropertyValueStr(ctx, (JSValue)proto, "constructor", JS_DupValue(ctx, ctor),
                              JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE);
    JS_SetPropertyStr(ctx, (JSValue)global, name, ctor);
}

/* ===========================================================================
 * MutationObserver — a real implementation (see the struct comment above).
 *
 * DOM mutations report themselves through dom_mo_notify_*; for every live
 * observer that covers the target, one MutationRecord is queued and a single
 * delivery microtask is scheduled. Delivery coalesces per observer (all its
 * records in one callback call), matching the spec's "queue a mutation
 * observer microtask" step. Records deliver on the QuickJS job queue, i.e. as
 * a microtask after the current turn — the timing frameworks expect.
 * ===========================================================================*/

static void mo_register(struct js_dom_state *state, struct mutation_observer *observer)
{
    for (int i = 0; i < state->mutation_observer_count; i++) {
        if (state->mutation_observers[i] == NULL) {
            state->mutation_observers[i] = observer;
            return;
        }
    }
    if (state->mutation_observer_count < MAX_MUTATION_OBSERVERS) {
        state->mutation_observers[state->mutation_observer_count++] = observer;
    }
}

static void mo_unregister(struct js_dom_state *state, struct mutation_observer *observer)
{
    for (int i = 0; i < state->mutation_observer_count; i++) {
        if (state->mutation_observers[i] == observer) {
            state->mutation_observers[i] = NULL;
            return;
        }
    }
}

/* Does this observation cover `target` for the given mutation? An observation
 * covers its exact target always, and any descendant when `subtree` is set. */
static int mo_observation_covers(const struct mutation_observation *observation,
                                 lxb_dom_node_t *target)
{
    if (observation->target == target) {
        return 1;
    }
    if (!(observation->flags & MO_SUBTREE)) {
        return 0;
    }
    for (lxb_dom_node_t *node = target->parent; node != NULL; node = node->parent) {
        if (node == observation->target) {
            return 1;
        }
    }
    return 0;
}

static int dom_mo_any_interest(JSContext *ctx, lxb_dom_node_t *target, int type_flag)
{
    struct js_dom_state *state = dom_state(ctx);
    if (state == NULL) {
        return 0;
    }
    for (int i = 0; i < state->mutation_observer_count; i++) {
        struct mutation_observer *observer = state->mutation_observers[i];
        if (observer == NULL) {
            continue;
        }
        for (int j = 0; j < observer->observation_count; j++) {
            struct mutation_observation *observation = &observer->observations[j];
            if ((observation->flags & type_flag) && mo_observation_covers(observation, target)) {
                return 1;
            }
        }
    }
    return 0;
}

static JSValue mo_deliver_job(JSContext *ctx, int argc, JSValueConst *argv);

static void mo_schedule_delivery(JSContext *ctx)
{
    struct js_dom_state *state = dom_state(ctx);
    if (state == NULL || state->mutation_delivery_scheduled) {
        return;
    }
    state->mutation_delivery_scheduled = 1;
    /* Deliver on the microtask queue. The job body re-reads the registry, so
	 * mutations that happen before it runs are folded into the same delivery. */
    JS_EnqueueJob(ctx, mo_deliver_job, 0, NULL);
}

/* Push a record onto an observer's pending queue (creating it on demand); the
 * record ref is transferred to the array. */
static void mo_push_record(JSContext *ctx, struct mutation_observer *observer, JSValue record)
{
    if (JS_IsUndefined(observer->records)) {
        observer->records = JS_NewArray(ctx);
    }
    JSValue length_val = JS_GetPropertyStr(ctx, observer->records, "length");
    uint32_t length = 0;
    JS_ToUint32(ctx, &length, length_val);
    JS_FreeValue(ctx, length_val);
    JS_SetPropertyUint32(ctx, observer->records, length, record);
}

static JSValue mo_new_record(JSContext *ctx, const char *type, lxb_dom_node_t *target)
{
    JSValue record = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, record, "type", JS_NewString(ctx, type));
    JS_SetPropertyStr(ctx, record, "target", wrap_element(ctx, (lxb_dom_element_t *)target));
    JS_SetPropertyStr(ctx, record, "addedNodes", JS_NewArray(ctx));
    JS_SetPropertyStr(ctx, record, "removedNodes", JS_NewArray(ctx));
    JS_SetPropertyStr(ctx, record, "previousSibling", JS_NULL);
    JS_SetPropertyStr(ctx, record, "nextSibling", JS_NULL);
    JS_SetPropertyStr(ctx, record, "attributeName", JS_NULL);
    JS_SetPropertyStr(ctx, record, "attributeNamespace", JS_NULL);
    JS_SetPropertyStr(ctx, record, "oldValue", JS_NULL);
    return record;
}

/* Single-element NodeList-ish array holding one wrapped node. */
static JSValue mo_single_node_array(JSContext *ctx, lxb_dom_node_t *node)
{
    JSValue array = JS_NewArray(ctx);
    JS_SetPropertyUint32(ctx, array, 0, wrap_element(ctx, (lxb_dom_element_t *)node));
    return array;
}

static void dom_mo_notify_attributes(JSContext *ctx, lxb_dom_element_t *el, const char *name,
                                     size_t name_len, const char *old_value)
{
    struct js_dom_state *state = dom_state(ctx);
    if (state == NULL || state->mutation_observer_count == 0) {
        return;
    }
    lxb_dom_node_t *target = lxb_dom_interface_node(el);
    int queued = 0;
    for (int i = 0; i < state->mutation_observer_count; i++) {
        struct mutation_observer *observer = state->mutation_observers[i];
        if (observer == NULL) {
            continue;
        }
        int interested = 0, want_old_value = 0;
        for (int j = 0; j < observer->observation_count; j++) {
            struct mutation_observation *observation = &observer->observations[j];
            if (!(observation->flags & MO_ATTRIBUTES) ||
                !mo_observation_covers(observation, target)) {
                continue;
            }
            interested = 1;
            if (observation->flags & MO_ATTR_OLD_VALUE) {
                want_old_value = 1;
            }
        }
        if (!interested) {
            continue;
        }
        JSValue record = mo_new_record(ctx, "attributes", target);
        JS_SetPropertyStr(ctx, record, "attributeName", JS_NewStringLen(ctx, name, name_len));
        if (want_old_value && old_value != NULL) {
            JS_SetPropertyStr(ctx, record, "oldValue", JS_NewString(ctx, old_value));
        }
        mo_push_record(ctx, observer, record);
        queued = 1;
    }
    if (queued) {
        mo_schedule_delivery(ctx);
    }
}

static void dom_mo_notify_character_data(JSContext *ctx, lxb_dom_node_t *target,
                                         const char *old_value)
{
    struct js_dom_state *state = dom_state(ctx);
    if (state == NULL || state->mutation_observer_count == 0) {
        return;
    }
    int queued = 0;
    for (int i = 0; i < state->mutation_observer_count; i++) {
        struct mutation_observer *observer = state->mutation_observers[i];
        if (observer == NULL) {
            continue;
        }
        int interested = 0, want_old_value = 0;
        for (int j = 0; j < observer->observation_count; j++) {
            struct mutation_observation *observation = &observer->observations[j];
            if (!(observation->flags & MO_CHARACTER_DATA) ||
                !mo_observation_covers(observation, target)) {
                continue;
            }
            interested = 1;
            if (observation->flags & MO_CHAR_OLD_VALUE) {
                want_old_value = 1;
            }
        }
        if (!interested) {
            continue;
        }
        JSValue record = mo_new_record(ctx, "characterData", target);
        if (want_old_value && old_value != NULL) {
            JS_SetPropertyStr(ctx, record, "oldValue", JS_NewString(ctx, old_value));
        }
        mo_push_record(ctx, observer, record);
        queued = 1;
    }
    if (queued) {
        mo_schedule_delivery(ctx);
    }
}

static void dom_mo_notify_child_list(JSContext *ctx, lxb_dom_node_t *parent, lxb_dom_node_t *added,
                                     lxb_dom_node_t *removed, lxb_dom_node_t *prev_sibling,
                                     lxb_dom_node_t *next_sibling)
{
    struct js_dom_state *state = dom_state(ctx);
    if (state == NULL || state->mutation_observer_count == 0) {
        return;
    }
    int queued = 0;
    for (int i = 0; i < state->mutation_observer_count; i++) {
        struct mutation_observer *observer = state->mutation_observers[i];
        if (observer == NULL) {
            continue;
        }
        int interested = 0;
        for (int j = 0; j < observer->observation_count; j++) {
            struct mutation_observation *observation = &observer->observations[j];
            if ((observation->flags & MO_CHILD_LIST) &&
                mo_observation_covers(observation, parent)) {
                interested = 1;
                break;
            }
        }
        if (!interested) {
            continue;
        }
        JSValue record = mo_new_record(ctx, "childList", parent);
        if (added != NULL) {
            JS_SetPropertyStr(ctx, record, "addedNodes", mo_single_node_array(ctx, added));
        }
        if (removed != NULL) {
            JS_SetPropertyStr(ctx, record, "removedNodes", mo_single_node_array(ctx, removed));
        }
        if (prev_sibling != NULL) {
            JS_SetPropertyStr(ctx, record, "previousSibling",
                              wrap_element(ctx, (lxb_dom_element_t *)prev_sibling));
        }
        if (next_sibling != NULL) {
            JS_SetPropertyStr(ctx, record, "nextSibling",
                              wrap_element(ctx, (lxb_dom_element_t *)next_sibling));
        }
        mo_push_record(ctx, observer, record);
        queued = 1;
    }
    if (queued) {
        mo_schedule_delivery(ctx);
    }
}

/* The delivery microtask: hand each observer its queued records in one call.
 * Clearing `mutation_delivery_scheduled` first means mutations made by a
 * callback schedule a fresh delivery, which drains in the same job loop. */
static void mo_deliver_job_body(JSContext *ctx)
{
    struct js_dom_state *state = dom_state(ctx);
    if (state == NULL) {
        return;
    }
    state->mutation_delivery_scheduled = 0;
    for (int i = 0; i < state->mutation_observer_count; i++) {
        struct mutation_observer *observer = state->mutation_observers[i];
        if (observer == NULL || JS_IsUndefined(observer->records)) {
            continue;
        }
        JSValue records = observer->records;
        observer->records = JS_UNDEFINED;
        JSValueConst args[2] = {records, observer->self};
        JSValue ret = JS_Call(ctx, observer->callback, observer->self, 2, args);
        if (JS_IsException(ret)) {
            JSValue exc = JS_GetException(ctx);
            const char *msg = JS_ToCString(ctx, exc);
            ydebug("MutationObserver callback: %s", msg ? msg : "?");
            if (msg) {
                JS_FreeCString(ctx, msg);
            }
            JS_FreeValue(ctx, exc);
        } else {
            JS_FreeValue(ctx, ret);
        }
        JS_FreeValue(ctx, records);
    }
}

static JSValue mo_deliver_job(JSContext *ctx, int argc, JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    mo_deliver_job_body(ctx);
    return JS_UNDEFINED;
}

static struct mutation_observer *mo_from_this(JSContext *ctx, JSValueConst this_val)
{
    return JS_GetOpaque(this_val, dom_state(ctx)->class_mutation_observer_id);
}

static int mo_opt_bool(JSContext *ctx, JSValueConst options, const char *name)
{
    JSValue value = JS_GetPropertyStr(ctx, options, name);
    int result = JS_ToBool(ctx, value);
    JS_FreeValue(ctx, value);
    return result;
}

static JSValue js_mutation_observer_observe(JSContext *ctx, JSValueConst this_val, int argc,
                                            JSValueConst *argv)
{
    struct mutation_observer *observer = mo_from_this(ctx, this_val);
    if (observer == NULL) {
        return JS_ThrowTypeError(ctx, "observe called on non-MutationObserver");
    }
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "observe requires a target node");
    }
    lxb_dom_node_t *target = unwrap_node(ctx, argv[0]);
    if (target == NULL) {
        return JS_ThrowTypeError(ctx, "observe target is not a node");
    }
    int flags = 0;
    if (argc >= 2 && JS_IsObject(argv[1])) {
        JSValueConst options = argv[1];
        if (mo_opt_bool(ctx, options, "childList")) {
            flags |= MO_CHILD_LIST;
        }
        if (mo_opt_bool(ctx, options, "attributes")) {
            flags |= MO_ATTRIBUTES;
        }
        if (mo_opt_bool(ctx, options, "characterData")) {
            flags |= MO_CHARACTER_DATA;
        }
        if (mo_opt_bool(ctx, options, "subtree")) {
            flags |= MO_SUBTREE;
        }
        if (mo_opt_bool(ctx, options, "attributeOldValue")) {
            flags |= MO_ATTR_OLD_VALUE | MO_ATTRIBUTES;
        }
        if (mo_opt_bool(ctx, options, "characterDataOldValue")) {
            flags |= MO_CHAR_OLD_VALUE | MO_CHARACTER_DATA;
        }
    }
    if (!(flags & (MO_CHILD_LIST | MO_ATTRIBUTES | MO_CHARACTER_DATA))) {
        return JS_ThrowTypeError(
            ctx, "observe requires childList, attributes, or characterData to be true");
    }
    /* Pin the observer alive now that it has an active observation. */
    if (!observer->rooted) {
        JS_DupValue(ctx, this_val);
        observer->rooted = 1;
    }
    /* Re-observing the same target replaces its options (spec). */
    for (int i = 0; i < observer->observation_count; i++) {
        if (observer->observations[i].target == target) {
            observer->observations[i].flags = flags;
            return JS_UNDEFINED;
        }
    }
    if (observer->observation_count < MO_MAX_OBSERVATIONS) {
        observer->observations[observer->observation_count].target = target;
        observer->observations[observer->observation_count].flags = flags;
        observer->observation_count++;
    }
    return JS_UNDEFINED;
}

static JSValue js_mutation_observer_disconnect(JSContext *ctx, JSValueConst this_val, int argc,
                                               JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    struct mutation_observer *observer = mo_from_this(ctx, this_val);
    if (observer == NULL) {
        return JS_UNDEFINED;
    }
    observer->observation_count = 0;
    JS_FreeValue(ctx, observer->records);
    observer->records = JS_UNDEFINED;
    /* No active observations → release the keep-alive pin. `this_val` still
	 * holds a ref for the duration of this call, so the object is not
	 * finalized out from under us here. */
    if (observer->rooted) {
        observer->rooted = 0;
        JS_FreeValue(ctx, this_val);
    }
    return JS_UNDEFINED;
}

static JSValue js_mutation_observer_take_records(JSContext *ctx, JSValueConst this_val, int argc,
                                                 JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    struct mutation_observer *observer = mo_from_this(ctx, this_val);
    if (observer == NULL || JS_IsUndefined(observer->records)) {
        return JS_NewArray(ctx);
    }
    JSValue records = observer->records;
    observer->records = JS_UNDEFINED;
    return records;
}

static void mutation_observer_finalizer(JSRuntime *rt, JSValue val)
{
    struct js_dom_state *state = JS_GetRuntimeOpaque(rt);
    if (state == NULL) {
        return;
    }
    struct mutation_observer *observer = JS_GetOpaque(val, state->class_mutation_observer_id);
    if (observer == NULL) {
        return;
    }
    mo_unregister(state, observer);
    JS_FreeValueRT(rt, observer->callback);
    JS_FreeValueRT(rt, observer->records);
    free(observer);
}

static JSValue js_mutation_observer_ctor(JSContext *ctx, JSValueConst new_target, int argc,
                                         JSValueConst *argv)
{
    if (argc < 1 || !JS_IsFunction(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "MutationObserver requires a callback function");
    }
    struct js_dom_state *state = dom_state(ctx);
    JSValue proto = JS_GetPropertyStr(ctx, new_target, "prototype");
    if (JS_IsException(proto)) {
        return proto;
    }
    JSValue obj = JS_NewObjectProtoClass(ctx, proto, state->class_mutation_observer_id);
    JS_FreeValue(ctx, proto);
    if (JS_IsException(obj)) {
        return obj;
    }
    struct mutation_observer *observer = calloc(1, sizeof(*observer));
    if (observer == NULL) {
        JS_FreeValue(ctx, obj);
        return JS_ThrowOutOfMemory(ctx);
    }
    observer->self = obj; /* non-owned: the finalizer unregisters, so a
	                       * registered observer's self is always live */
    observer->callback = JS_DupValue(ctx, argv[0]);
    observer->records = JS_UNDEFINED;
    JS_SetOpaque(obj, observer);
    mo_register(state, observer);
    return obj;
}

/* Install globalThis.MutationObserver backed by the C implementation. */
/* Called by the custom-element registry the first time a page defines a
 * custom element. Arms the reaction dispatch in the DOM insertion paths so
 * pages that never use custom elements pay no per-insertion cost. */
static JSValue js_ce_activate(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    struct js_dom_state *state = dom_state(ctx);
    if (state != NULL) {
        state->ce_active = 1;
    }
    return JS_UNDEFINED;
}

static void install_mutation_observer(JSContext *ctx, JSValueConst global)
{
    struct js_dom_state *state = dom_state(ctx);
    JS_SetPropertyStr(ctx, (JSValue)global, "__ceActivate",
                      JS_NewCFunction(ctx, js_ce_activate, "__ceActivate", 0));
    JSValue proto = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, proto, "observe",
                      JS_NewCFunction(ctx, js_mutation_observer_observe, "observe", 2));
    JS_SetPropertyStr(ctx, proto, "disconnect",
                      JS_NewCFunction(ctx, js_mutation_observer_disconnect, "disconnect", 0));
    JS_SetPropertyStr(ctx, proto, "takeRecords",
                      JS_NewCFunction(ctx, js_mutation_observer_take_records, "takeRecords", 0));
    JS_SetClassProto(ctx, state->class_mutation_observer_id, JS_DupValue(ctx, proto));
    JSValue ctor = JS_NewCFunction2(ctx, js_mutation_observer_ctor, "MutationObserver", 1,
                                    JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, ctor, proto);
    JS_SetPropertyStr(ctx, (JSValue)global, "MutationObserver", ctor);
    /* WebKitMutationObserver is the legacy alias some libraries feature-detect. */
    JS_SetPropertyStr(ctx, (JSValue)global, "WebKitMutationObserver",
                      JS_GetPropertyStr(ctx, global, "MutationObserver"));
    JS_FreeValue(ctx, proto);
}

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
        /* A zeroed JSValue is JS_TAG_INT 0, not undefined; set it explicitly so
    	 * the apply_chardata_proto/apply_fragment_proto guards hold before this
    	 * install wires them. */
        state->chardata_proto = JS_UNDEFINED;
        state->fragment_proto = JS_UNDEFINED;
        JS_SetRuntimeOpaque(rt, state);
    }
    state->r = r;

    /* Class IDs — once per runtime; a fresh state starts them at 0.
	 * The defs are copied by JS_NewClass; only the exotic-methods
	 * pointer is retained, so that one must have program lifetime
	 * (static const — QuickJS never writes through it). */
    static const JSClassExoticMethods style_exotic_methods = {
        .get_property = style_get_property,
        .set_property = style_set_property,
    };
    if (state->class_node_id == 0) {
        JS_NewClassID(rt, &state->class_node_id);
        JS_NewClassID(rt, &state->class_element_id);
        JS_NewClassID(rt, &state->class_document_id);
        JS_NewClassID(rt, &state->class_classlist_id);
        JS_NewClassID(rt, &state->class_style_id);
        JS_NewClassID(rt, &state->class_mutation_observer_id);
    }
    const JSClassDef class_node_def = {"Node", .finalizer = node_finalizer};
    const JSClassDef class_element_def = {"Element", .finalizer = node_finalizer};
    const JSClassDef class_document_def = {"Document", .finalizer = node_finalizer};
    const JSClassDef class_classlist_def = {"DOMTokenList", .finalizer = node_finalizer};
    const JSClassDef class_style_def = {"CSSStyleDeclaration", .finalizer = node_finalizer,
                                        .exotic = (JSClassExoticMethods *)&style_exotic_methods};
    const JSClassDef class_mutation_observer_def = {"MutationObserver",
                                                    .finalizer = mutation_observer_finalizer};
    JS_NewClass(rt, state->class_node_id, &class_node_def);
    JS_NewClass(rt, state->class_element_id, &class_element_def);
    JS_NewClass(rt, state->class_document_id, &class_document_def);
    JS_NewClass(rt, state->class_classlist_id, &class_classlist_def);
    JS_NewClass(rt, state->class_style_id, &class_style_def);
    JS_NewClass(rt, state->class_mutation_observer_id, &class_mutation_observer_def);

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
        JS_CFUNC_DEF("closest", 1, js_el_closest),
        /* Predicates / accessors that need a typed return. We give them
    	 * a tiny dedicated stub each below so they don't masquerade as
    	 * mutators. */
        JS_CFUNC_DEF("contains", 1, js_el_contains_stub),
        JS_CFUNC_DEF("matches", 1, js_el_matches),
        JS_CFUNC_DEF("hasChildNodes", 0, js_el_hasChildNodes_stub),
        JS_CFUNC_DEF("hasAttributes", 0, js_el_hasAttributes_stub),
        JS_CFUNC_DEF("getAttributeNames", 0, js_el_getAttributeNames_stub),
        JS_CGETSET_DEF("attributes", js_el_attributes_get, NULL),
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
        /* CharacterData members (`data`/`length`/appendData/…) are NOT here:
    	 * they live on chardata_proto, which only Text/Comment/PI wrappers
    	 * inherit, so `"data" in <element>` stays false (see chardata_proto). */
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
        JS_CGETSET_DEF("localName", js_el_localName_get, NULL),
        JS_CGETSET_DEF("isConnected", js_el_isConnected_get, NULL),
        JS_CGETSET_DEF("ownerDocument", js_el_ownerDocument_get, NULL),
        /* Laid-out geometry the responsive components read (YouTube's
    	 * ytd-rich-grid-renderer bails out of reflow when hostElement.clientWidth
    	 * is undefined). Integer CSS px from the last completed layout. */
        JS_CGETSET_DEF("clientWidth", js_el_clientWidth_get, NULL),
        JS_CGETSET_DEF("clientHeight", js_el_clientHeight_get, NULL),
        JS_CGETSET_DEF("offsetWidth", js_el_offsetWidth_get, NULL),
        JS_CGETSET_DEF("offsetHeight", js_el_offsetHeight_get, NULL),
        /* `delegate` — Turbo's custom-element wiring does
    	 *   Object.getPrototypeOf(el.delegate)
    	 * during boot. Returning an empty object lets that walk through.
    	 * Same for the few other "library-private slot" reads modern
    	 * frameworks make on elements. */
        JS_CGETSET_DEF("delegate", js_el_delegate_get, NULL),
        JS_CGETSET_DEF("dataset", js_el_empty_obj_get, NULL),
        JS_CGETSET_DEF("content", js_el_content_get, js_el_content_set),
        JS_CGETSET_DEF("elements", js_el_elements_get, NULL),
        JS_CGETSET_DEF("src", js_el_src_get, js_el_src_set),
        JS_CGETSET_DEF("href", js_el_href_get, js_el_href_set),
        /* HTMLAnchorElement/HTMLAreaElement URL-decomposition attributes —
		 * parsed from the resolved href. Read-only here (setters are rarely
		 * used and would need URL re-composition). */
        JS_CGETSET_DEF("protocol", js_el_protocol_get, NULL),
        JS_CGETSET_DEF("host", js_el_host_get, NULL),
        JS_CGETSET_DEF("hostname", js_el_hostname_get, NULL),
        JS_CGETSET_DEF("port", js_el_port_get, NULL),
        JS_CGETSET_DEF("pathname", js_el_pathname_get, NULL),
        JS_CGETSET_DEF("search", js_el_search_get, NULL),
        JS_CGETSET_DEF("hash", js_el_hash_get, NULL),
        JS_CGETSET_DEF("origin", js_el_urlorigin_get, NULL),
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
        JS_CGETSET_DEF("hidden", js_el_hidden_get, js_el_hidden_set),
    };
    /* DOM interface prototype CHAIN: element_proto -> node_proto ->
     * event_target_proto. Every node instance points at element_proto, so it
     * still reaches all methods by inheritance (all methods start on
     * element_proto). YouTube's Shady-DOM polyfill captures native methods
     * per-interface via Object.getOwnPropertyDescriptor(Node.prototype, …) and
     * re-patches them, expecting instances to pick up the patch through
     * inheritance — impossible when every method is an OWN prop of one flat
     * proto. The redistribute step (js_dom_redistribute_protos below, run after
     * the globals are wired) moves the Node/EventTarget methods UP to the level
     * the polyfill inspects; reachability is unchanged either way. */
    JSValue event_target_proto = JS_NewObject(ctx);
    JSValue node_proto = JS_NewObjectProto(ctx, event_target_proto);
    JSValue element_proto = JS_NewObjectProto(ctx, node_proto);
    JS_SetPropertyFunctionList(ctx, element_proto, element_funcs,
                               sizeof(element_funcs) / sizeof(element_funcs[0]));
    /* SetClassProto consumes a ref; keep element_proto for the wiring below. */
    JS_SetClassProto(ctx, state->class_element_id, JS_DupValue(ctx, element_proto));

    /* CharacterData.prototype, chained above the element proto so Text/Comment/PI
     * wrappers keep every Node/Element method they reach today AND gain the
     * character-data members — while plain elements (which inherit element_proto
     * directly) never report `data`. apply_chardata_proto() re-parents the
     * relevant wrappers to this proto. */
    static const JSCFunctionListEntry chardata_funcs[] = {
        JS_CGETSET_DEF("data", js_cd_data_get, js_cd_data_set),
        JS_CGETSET_DEF("length", js_cd_length_get, NULL),
        JS_CFUNC_DEF("appendData", 1, js_cd_appendData),
        JS_CFUNC_DEF("insertData", 2, js_cd_insertData),
        JS_CFUNC_DEF("deleteData", 2, js_cd_deleteData),
        JS_CFUNC_DEF("replaceData", 3, js_cd_replaceData),
        JS_CFUNC_DEF("substringData", 2, js_cd_substringData),
    };
    JSValue chardata_proto = JS_NewObjectProto(ctx, element_proto);
    JS_SetPropertyFunctionList(ctx, chardata_proto, chardata_funcs,
                               sizeof(chardata_funcs) / sizeof(chardata_funcs[0]));
    JS_FreeValue(ctx, state->chardata_proto); /* release a prior install's proto */
    state->chardata_proto = JS_DupValue(ctx, chardata_proto);

    /* DocumentFragment.prototype — chained on node_proto (Node.prototype), NOT
	 * element_proto, so a fragment is not `instanceof Element`. The redistribute
	 * step below moves the core Node/insertion methods (appendChild, insertBefore,
	 * cloneNode, append/prepend, childNodes, firstChild, nodeType, …) up to
	 * node_proto, which a fragment reaches by inheritance. The ParentNode *query*
	 * methods stay on element_proto, so copy the ones a fragment legitimately has
	 * (querySelector/querySelectorAll/children/firstElementChild) directly onto
	 * the fragment proto — mirroring how doc_proto gets its own method copy. */
    static const JSCFunctionListEntry fragment_funcs[] = {
        JS_CFUNC_DEF("querySelector", 1, js_el_querySelector),
        JS_CFUNC_DEF("querySelectorAll", 1, js_el_querySelectorAll),
        JS_CGETSET_DEF("children", js_el_children_get, NULL),
        JS_CGETSET_DEF("firstElementChild", js_el_firstElementChild_get, NULL),
    };
    JSValue fragment_proto = JS_NewObjectProto(ctx, node_proto);
    JS_SetPropertyFunctionList(ctx, fragment_proto, fragment_funcs,
                               sizeof(fragment_funcs) / sizeof(fragment_funcs[0]));
    JS_FreeValue(ctx, state->fragment_proto); /* release a prior install's proto */
    state->fragment_proto = JS_DupValue(ctx, fragment_proto);

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
        JS_CFUNC_DEF("importNode", 2, js_doc_importNode),
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
        JS_CGETSET_DEF("localName", js_el_localName_get, NULL),
        JS_CGETSET_DEF("isConnected", js_el_isConnected_get, NULL),
        JS_CGETSET_DEF("ownerDocument", js_el_ownerDocument_get, NULL),
        JS_CGETSET_DEF("implementation", js_doc_implementation_get, NULL),
    };
    /* Document inherits from Element-ish prototype + extra methods.
	 * We give it the *same* methods as Element so document.querySelector
	 * works directly (not via the Element prototype chain since we
	 * don't model that yet). */
    JSValue doc_proto = JS_NewObjectProto(ctx, node_proto);
    JS_SetPropertyFunctionList(ctx, doc_proto, document_funcs,
                               sizeof(document_funcs) / sizeof(document_funcs[0]));
    JS_SetClassProto(ctx, state->class_document_id, JS_DupValue(ctx, doc_proto));

    /* globalThis.document */
    JSValue global = JS_GetGlobalObject(ctx);

    /* Wire the real DOM interface constructors so their .prototype IS the chain
	 * object instances inherit from. The web-api stubs (js_web_install, run
	 * next) must not clobber these — they guard with `|| function(){}`. */
    install_dom_iface(ctx, global, "EventTarget", event_target_proto);
    install_dom_iface(ctx, global, "Node", node_proto);
    install_dom_iface(ctx, global, "Element", element_proto);
    install_dom_iface(ctx, global, "Document", doc_proto);
    install_dom_iface(ctx, global, "DocumentFragment", fragment_proto);
    install_mutation_observer(ctx, global);

    /* Move Node/EventTarget methods from element_proto UP to the interface
	 * level the Shady-DOM polyfill inspects (it captures own-props per
	 * prototype). Instances still reach everything through the chain; any name
	 * absent from element_proto is skipped. */
    static const char redistribute_js[] =
        "(function(){"
        "  function move(from,to,names){"
        "    for(var i=0;i<names.length;i++){var n=names[i];"
        "      var d=Object.getOwnPropertyDescriptor(from,n);"
        "      if(d){Object.defineProperty(to,n,d); if(d.configurable) delete from[n];}}}"
        "  var ep=Element.prototype, np=Node.prototype, tp=EventTarget.prototype;"
        "  move(ep,tp,['addEventListener','removeEventListener','dispatchEvent']);"
        "  move(ep,np,['appendChild','removeChild','insertBefore','replaceChild',"
        "    'prepend','append','before','after','remove','replaceWith','cloneNode',"
        "    'contains','hasChildNodes','normalize','textContent','firstChild','lastChild',"
        "    'nextSibling','previousSibling','parentNode','parentElement','childNodes',"
        "    'nodeType','nodeName','ownerDocument']);"
        "})();";
    JSValue redistribute_res = JS_Eval(ctx, redistribute_js, sizeof(redistribute_js) - 1,
                                       "<dom-protos>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(redistribute_res)) {
        JS_FreeValue(ctx, JS_GetException(ctx));
    }
    JS_FreeValue(ctx, redistribute_res);

    JS_FreeValue(ctx, event_target_proto);
    JS_FreeValue(ctx, node_proto);
    JS_FreeValue(ctx, element_proto);
    JS_FreeValue(ctx, chardata_proto);
    JS_FreeValue(ctx, fragment_proto);
    JS_FreeValue(ctx, doc_proto);

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

/* Synthetic pointer-event helpers. The host builds a plain event object for
 * mouse dispatch: apps routinely replace window.MouseEvent with their own
 * wrapper (YouTube's produces an event whose .type is undefined in our engine),
 * so we cannot construct the page's MouseEvent. These give our plain object the
 * DOM Event methods real handlers call — most importantly composedPath(), which
 * Polymer's tap-gesture emitter invokes; on the old bare object that call threw,
 * silently killing every button tap. */
static JSValue synth_event_composed_path(JSContext *ctx, JSValueConst this_val, int argc,
                                         JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    JSValue path = JS_GetPropertyStr(ctx, this_val, "__composedPath");
    if (JS_IsUndefined(path) || JS_IsNull(path)) {
        JS_FreeValue(ctx, path);
        return JS_NewArray(ctx);
    }
    return path;
}

static JSValue synth_event_prevent_default(JSContext *ctx, JSValueConst this_val, int argc,
                                           JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    JS_SetPropertyStr(ctx, (JSValue)this_val, "defaultPrevented", JS_TRUE);
    return JS_UNDEFINED;
}

static JSValue synth_event_stop_propagation(JSContext *ctx, JSValueConst this_val, int argc,
                                            JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    JS_SetPropertyStr(ctx, (JSValue)this_val, "__propagationStopped", JS_TRUE);
    return JS_UNDEFINED;
}

static JSValue synth_event_stop_immediate_propagation(JSContext *ctx, JSValueConst this_val,
                                                      int argc, JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    JS_SetPropertyStr(ctx, (JSValue)this_val, "__propagationStopped", JS_TRUE);
    JS_SetPropertyStr(ctx, (JSValue)this_val, "__immediateStopped", JS_TRUE);
    return JS_UNDEFINED;
}

/* Fire one synthetic event over the target's ancestor chain (bubble phase) then
 * the document/window bucket, honouring stopPropagation / stopImmediatePropagation
 * flags the handlers set. Returns 1 if any listener ran. */
static int synth_dispatch_one(struct yetty_ylexbor *r, struct js_dom_state *state,
                              lxb_dom_element_t *target, const char *type, JSValueConst event)
{
    JSContext *ctx = (JSContext *)r->js_ctx;
    int snapshot = state->listener_count;
    int fired = 0;
    int stop = 0;
    for (lxb_dom_node_t *n = lxb_dom_interface_node(target); n && !stop; n = n->parent) {
        if (n->type != LXB_DOM_NODE_TYPE_ELEMENT) {
            continue;
        }
        lxb_dom_element_t *el = lxb_dom_interface_element(n);
        JSValue current = wrap_element(ctx, el);
        JS_SetPropertyStr(ctx, (JSValue)event, "currentTarget", JS_DupValue(ctx, current));
        for (int i = 0; i < snapshot; i++) {
            if (state->listeners[i].el != el || strcmp(state->listeners[i].type, type) != 0) {
                continue;
            }
            JSValueConst args[] = {event};
            JSValue ret = JS_Call(ctx, state->listeners[i].handler, current, 1, args);
            if (JS_IsException(ret)) {
                JSValue ex = JS_GetException(ctx);
                JS_FreeValue(ctx, ex);
                r->js_error_count++;
            } else {
                JS_FreeValue(ctx, ret);
            }
            fired = 1;
            JSValue immediate = JS_GetPropertyStr(ctx, event, "__immediateStopped");
            int immediate_stop = JS_ToBool(ctx, immediate);
            JS_FreeValue(ctx, immediate);
            if (immediate_stop) {
                break;
            }
        }
        JS_FreeValue(ctx, current);
        JSValue propagation = JS_GetPropertyStr(ctx, event, "__propagationStopped");
        stop = JS_ToBool(ctx, propagation);
        JS_FreeValue(ctx, propagation);
    }
    if (!stop) {
        JSValue global = JS_GetGlobalObject(ctx);
        JSValue document = JS_GetPropertyStr(ctx, global, "document");
        JSValue current =
            JS_IsObject(document) ? JS_DupValue(ctx, document) : JS_DupValue(ctx, global);
        JS_SetPropertyStr(ctx, (JSValue)event, "currentTarget", JS_DupValue(ctx, current));
        for (int i = 0; i < snapshot; i++) {
            if (state->listeners[i].el != NULL || strcmp(state->listeners[i].type, type) != 0) {
                continue;
            }
            JSValueConst args[] = {event};
            JSValue ret = JS_Call(ctx, state->listeners[i].handler, current, 1, args);
            if (JS_IsException(ret)) {
                JSValue ex = JS_GetException(ctx);
                JS_FreeValue(ctx, ex);
                r->js_error_count++;
            } else {
                JS_FreeValue(ctx, ret);
            }
            fired = 1;
            JSValue immediate = JS_GetPropertyStr(ctx, event, "__immediateStopped");
            int immediate_stop = JS_ToBool(ctx, immediate);
            JS_FreeValue(ctx, immediate);
            if (immediate_stop) {
                break;
            }
        }
        JS_FreeValue(ctx, current);
        JS_FreeValue(ctx, document);
        JS_FreeValue(ctx, global);
    }
    return fired;
}

int yetty_ylexbor_dispatch_click(struct yetty_ylexbor *r, float x, float y)
{
    if (!r || !r->js_ctx || !r->js_rt) {
        return 0;
    }
    struct js_dom_state *state = JS_GetRuntimeOpaque((JSRuntime *)r->js_rt);
    if (!state || state->listener_count == 0) {
        return 0;
    }
    if (state->dispatch_depth > 32) {
        return 0;
    }

    /* Hit-test: find deepest box whose rect contains (x,y) AND has
	 * an associated element. */
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
            /* Topmost in paint/stacking order wins, so a click lands on an
			 * overlay painted above content earlier in the box vector. */
            if (!have_hit || yetty_ylexbor_paint_order_cmp(r, best_index, i) < 0) {
                best_index = i;
                target = b->element;
                have_hit = true;
            }
        }
    }
    if (target == NULL) {
        return 0;
    }

    JSContext *ctx = (JSContext *)r->js_ctx;
    state->dispatch_depth++;

    /* composedPath = [target, ...element ancestors, document, window]. Real
     * handlers (Polymer's tap-gesture emitter) call event.composedPath(); built
     * once and shared across the down/up/click events. */
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue document = JS_GetPropertyStr(ctx, global, "document");
    JSValue path = JS_NewArray(ctx);
    uint32_t path_len = 0;
    for (lxb_dom_node_t *n = lxb_dom_interface_node(target); n; n = n->parent) {
        if (n->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            JS_SetPropertyUint32(ctx, path, path_len++,
                                 wrap_element(ctx, lxb_dom_interface_element(n)));
        }
    }
    if (JS_IsObject(document)) {
        JS_SetPropertyUint32(ctx, path, path_len++, JS_DupValue(ctx, document));
    }
    JS_SetPropertyUint32(ctx, path, path_len++, JS_DupValue(ctx, global));

    /* A real click is a pointer/mouse SEQUENCE, not a lone 'click'. Polymer's
     * gesture recognizer arms its tap on mousedown and emits it on click, so
     * firing only 'click' (as the old code did) never produced a tap and button
     * activations did nothing. */
    static const char *const sequence[] = {"mousedown", "mouseup", "click"};
    int fired = 0;
    for (size_t phase = 0; phase < sizeof(sequence) / sizeof(sequence[0]); phase++) {
        const char *type = sequence[phase];
        int is_down = (phase == 0);

        JSValue event = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, event, "type", JS_NewString(ctx, type));
        JS_SetPropertyStr(ctx, event, "target", wrap_element(ctx, target));
        JS_SetPropertyStr(ctx, event, "clientX", JS_NewFloat64(ctx, x));
        JS_SetPropertyStr(ctx, event, "clientY", JS_NewFloat64(ctx, y));
        JS_SetPropertyStr(ctx, event, "pageX", JS_NewFloat64(ctx, x));
        JS_SetPropertyStr(ctx, event, "pageY", JS_NewFloat64(ctx, y));
        JS_SetPropertyStr(ctx, event, "screenX", JS_NewFloat64(ctx, x));
        JS_SetPropertyStr(ctx, event, "screenY", JS_NewFloat64(ctx, y));
        JS_SetPropertyStr(ctx, event, "button", JS_NewInt32(ctx, 0));
        JS_SetPropertyStr(ctx, event, "buttons", JS_NewInt32(ctx, is_down ? 1 : 0));
        JS_SetPropertyStr(ctx, event, "detail", JS_NewInt32(ctx, 1));
        JS_SetPropertyStr(ctx, event, "bubbles", JS_TRUE);
        JS_SetPropertyStr(ctx, event, "cancelable", JS_TRUE);
        JS_SetPropertyStr(ctx, event, "composed", JS_TRUE);
        JS_SetPropertyStr(ctx, event, "isTrusted", JS_FALSE);
        JS_SetPropertyStr(ctx, event, "defaultPrevented", JS_FALSE);
        JS_SetPropertyStr(ctx, event, "__composedPath", JS_DupValue(ctx, path));
        JS_SetPropertyStr(ctx, event, "composedPath",
                          JS_NewCFunction(ctx, synth_event_composed_path, "composedPath", 0));
        JS_SetPropertyStr(ctx, event, "preventDefault",
                          JS_NewCFunction(ctx, synth_event_prevent_default, "preventDefault", 0));
        JS_SetPropertyStr(ctx, event, "stopPropagation",
                          JS_NewCFunction(ctx, synth_event_stop_propagation, "stopPropagation", 0));
        JS_SetPropertyStr(ctx, event, "stopImmediatePropagation",
                          JS_NewCFunction(ctx, synth_event_stop_immediate_propagation,
                                          "stopImmediatePropagation", 0));

        if (synth_dispatch_one(r, state, target, type, event)) {
            fired = 1;
        }
        JS_FreeValue(ctx, event);
    }

    JS_FreeValue(ctx, path);
    JS_FreeValue(ctx, document);
    JS_FreeValue(ctx, global);
    state->dispatch_depth--;
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
    }
    /* Listener handler refs are owned by the dying context — the pool
	 * dies with the js_dom_state that js_destroy frees right after
	 * this, so only the count needs resetting for the re-install path. */
    if (r && r->js_rt) {
        struct js_dom_state *state = JS_GetRuntimeOpaque((JSRuntime *)r->js_rt);
        if (state) {
            /* Release the CharacterData proto ref while the context is live, so
    		 * the next dom_install starts from JS_UNDEFINED. */
            if (r->js_ctx && !JS_IsUndefined(state->chardata_proto)) {
                JS_FreeValue((JSContext *)r->js_ctx, state->chardata_proto);
                state->chardata_proto = JS_UNDEFINED;
            }
            if (r->js_ctx && !JS_IsUndefined(state->fragment_proto)) {
                JS_FreeValue((JSContext *)r->js_ctx, state->fragment_proto);
                state->fragment_proto = JS_UNDEFINED;
            }
            state->listener_count = 0;
            memset(state->listeners, 0, sizeof(state->listeners));
            /* Free MutationObserver structs while the context is still live:
			 * the runtime opaque is cleared before finalizers run at
			 * JS_FreeRuntime, so mutation_observer_finalizer would find a NULL
			 * state and leak them. */
            if (r->js_ctx) {
                JSContext *ctx = (JSContext *)r->js_ctx;
                for (int i = 0; i < state->mutation_observer_count; i++) {
                    struct mutation_observer *observer = state->mutation_observers[i];
                    if (observer == NULL) {
                        continue;
                    }
                    JSValue self = observer->self;
                    int rooted = observer->rooted;
                    /* NULL the opaque first so the finalizer no-ops when the
					 * pin release below drops the last ref — no double free. */
                    JS_SetOpaque(self, NULL);
                    JS_FreeValue(ctx, observer->callback);
                    JS_FreeValue(ctx, observer->records);
                    free(observer);
                    state->mutation_observers[i] = NULL;
                    if (rooted) {
                        JS_FreeValue(ctx, self);
                    }
                }
            }
            state->mutation_observer_count = 0;
            state->mutation_delivery_scheduled = 0;
        }
    }
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
    struct js_dom_state *state = dom_state(ctx);
    if (!state) {
        return;
    }
    lxb_dom_element_t *only = (lxb_dom_element_t *)target_element_ptr_or_null;

    /* Default target for global events (load, DOMContentLoaded) is
	 * document. Real handlers reach for event.target.tagName etc.,
	 * so we need a non-null object there. */
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue doc_target = JS_GetPropertyStr(ctx, global, "document");
    JS_FreeValue(ctx, global);

    for (int i = 0; i < state->listener_count; i++) {
        if (only && state->listeners[i].el != only) {
            continue;
        }
        if (strcmp(state->listeners[i].type, type) != 0) {
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
        JSValue ret = JS_Call(ctx, state->listeners[i].handler, JS_UNDEFINED, 1, args);
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
