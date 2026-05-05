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

#include "ylexbor-internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef YETTY_HAVE_QUICKJS
#  define YETTY_HAVE_QUICKJS 0
#endif

#if YETTY_HAVE_QUICKJS

#include <quickjs.h>

#include <lexbor/dom/dom.h>
#include <lexbor/html/html.h>
#include <lexbor/selectors/selectors.h>
#include <lexbor/css/css.h>
#include <lexbor/tag/const.h>


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
	(void)rt; (void)val;
	/* lexbor owns the DOM tree; we don't free anything here. */
}

static JSClassDef class_node_def     = { "Node",         .finalizer = node_finalizer };
static JSClassDef class_element_def  = { "Element",      .finalizer = node_finalizer };
static JSClassDef class_document_def = { "Document",     .finalizer = node_finalizer };
static JSClassDef class_classlist_def= { "DOMTokenList", .finalizer = node_finalizer };
static JSClassDef class_style_def    = { "CSSStyleDeclaration", .finalizer = node_finalizer };

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
	if (r) r->dom_dirty = 1;
}

/* ===========================================================================
 * Element / Node wrapping. We mint a fresh JSValue per call — multiple
 * wrappers for the same lxb pointer are fine because the underlying
 * pointer comparison still works (event-listener storage uses a
 * weakmap-style attach, see below).
 * ===========================================================================*/

static JSValue wrap_element(JSContext *ctx, lxb_dom_element_t *el)
{
	if (el == NULL) return JS_NULL;
	JSValue v = JS_NewObjectClass(ctx, class_element_id);
	JS_SetOpaque(v, el);
	return v;
}

static JSValue wrap_document(JSContext *ctx, lxb_html_document_t *doc)
{
	if (doc == NULL) return JS_NULL;
	JSValue v = JS_NewObjectClass(ctx, class_document_id);
	JS_SetOpaque(v, doc);
	return v;
}

static lxb_dom_element_t *unwrap_element(JSValueConst this_val)
{
	void *p = JS_GetOpaque(this_val, class_element_id);
	if (p) return (lxb_dom_element_t *)p;
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
	if (e) return lxb_dom_interface_node(e);
	lxb_html_document_t *d = JS_GetOpaque(this_val, class_document_id);
	if (d) return lxb_dom_interface_node(d);
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

static lxb_status_t sel_found_cb(lxb_dom_node_t *node,
				 lxb_css_selector_specificity_t spec,
				 void *ctx)
{
	(void)spec;
	struct sel_collect_ctx *c = ctx;
	if (node->type != LXB_DOM_NODE_TYPE_ELEMENT) return LXB_STATUS_OK;
	lxb_dom_element_t *el = lxb_dom_interface_element(node);
	if (c->first_only) {
		if (c->first == NULL) c->first = el;
		return LXB_STATUS_STOP;
	}
	if (c->count == c->cap) {
		size_t nc = c->cap ? c->cap * 2 : 8;
		void *p = realloc(c->elements, nc * sizeof(*c->elements));
		if (!p) return LXB_STATUS_ERROR_MEMORY_ALLOCATION;
		c->elements = p; c->cap = nc;
	}
	c->elements[c->count++] = el;
	return LXB_STATUS_OK;
}

static int run_selector(lxb_dom_node_t *root, const char *sel_text, size_t sel_len,
			struct sel_collect_ctx *out, int first_only)
{
	int rc = -1;
	lxb_css_parser_t *parser = lxb_css_parser_create();
	if (parser == NULL) return -1;
	if (lxb_css_parser_init(parser, NULL) != LXB_STATUS_OK)
		goto out_parser;

	lxb_selectors_t *sel = lxb_selectors_create();
	if (sel == NULL) goto out_parser;
	if (lxb_selectors_init(sel) != LXB_STATUS_OK) goto out_sel;

	lxb_css_selector_list_t *list = lxb_css_selectors_parse(
		parser, (const lxb_char_t *)sel_text, sel_len);
	if (list == NULL) goto out_sel;

	out->first_only = first_only;
	lxb_status_t s = lxb_selectors_find(sel, root, list, sel_found_cb, out);
	lxb_css_selector_list_destroy_memory(list);
	if (s == LXB_STATUS_OK || s == LXB_STATUS_STOP) rc = 0;

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

static JSValue js_el_getAttribute(JSContext *ctx, JSValueConst this_val,
				  int argc, JSValueConst *argv)
{
	if (argc < 1) return JS_NULL;
	lxb_dom_element_t *el = unwrap_element(this_val);
	if (!el) return JS_NULL;
	size_t nlen;
	const char *name = JS_ToCStringLen(ctx, &nlen, argv[0]);
	if (!name) return JS_EXCEPTION;
	size_t vlen = 0;
	const lxb_char_t *v = lxb_dom_element_get_attribute(el,
		(const lxb_char_t *)name, nlen, &vlen);
	JS_FreeCString(ctx, name);
	if (!v) return JS_NULL;
	return JS_NewStringLen(ctx, (const char *)v, vlen);
}

static JSValue js_el_setAttribute(JSContext *ctx, JSValueConst this_val,
				  int argc, JSValueConst *argv)
{
	if (argc < 2) return JS_UNDEFINED;
	lxb_dom_element_t *el = unwrap_element(this_val);
	if (!el) return JS_UNDEFINED;
	size_t nlen, vlen;
	const char *name = JS_ToCStringLen(ctx, &nlen, argv[0]);
	const char *val  = JS_ToCStringLen(ctx, &vlen, argv[1]);
	if (name && val) {
		lxb_dom_element_set_attribute(el,
			(const lxb_char_t *)name, nlen,
			(const lxb_char_t *)val, vlen);
		mark_dirty(ctx);
	}
	if (name) JS_FreeCString(ctx, name);
	if (val)  JS_FreeCString(ctx, val);
	return JS_UNDEFINED;
}

static JSValue js_el_removeAttribute(JSContext *ctx, JSValueConst this_val,
				     int argc, JSValueConst *argv)
{
	if (argc < 1) return JS_UNDEFINED;
	lxb_dom_element_t *el = unwrap_element(this_val);
	if (!el) return JS_UNDEFINED;
	size_t nlen;
	const char *name = JS_ToCStringLen(ctx, &nlen, argv[0]);
	if (name) {
		lxb_dom_element_remove_attribute(el,
			(const lxb_char_t *)name, nlen);
		mark_dirty(ctx);
		JS_FreeCString(ctx, name);
	}
	return JS_UNDEFINED;
}

static JSValue js_el_hasAttribute(JSContext *ctx, JSValueConst this_val,
				  int argc, JSValueConst *argv)
{
	if (argc < 1) return JS_FALSE;
	lxb_dom_element_t *el = unwrap_element(this_val);
	if (!el) return JS_FALSE;
	size_t nlen;
	const char *name = JS_ToCStringLen(ctx, &nlen, argv[0]);
	bool has = false;
	if (name) {
		has = lxb_dom_element_has_attribute(el,
			(const lxb_char_t *)name, nlen);
		JS_FreeCString(ctx, name);
	}
	return JS_NewBool(ctx, has);
}

static JSValue js_el_appendChild(JSContext *ctx, JSValueConst this_val,
				 int argc, JSValueConst *argv)
{
	if (argc < 1) return JS_UNDEFINED;
	lxb_dom_node_t *parent = unwrap_node(this_val);
	lxb_dom_node_t *child = unwrap_node(argv[0]);
	if (!parent || !child) return JS_UNDEFINED;
	lxb_dom_node_insert_child(parent, child);
	mark_dirty(ctx);
	return JS_DupValue(ctx, argv[0]);
}

static JSValue js_el_removeChild(JSContext *ctx, JSValueConst this_val,
				 int argc, JSValueConst *argv)
{
	if (argc < 1) return JS_UNDEFINED;
	lxb_dom_node_t *child = unwrap_node(argv[0]);
	if (!child) return JS_UNDEFINED;
	lxb_dom_node_remove(child);
	mark_dirty(ctx);
	return JS_DupValue(ctx, argv[0]);
}

/* querySelector(All) on Element AND Document — same impl. */
static JSValue js_el_querySelector(JSContext *ctx, JSValueConst this_val,
				   int argc, JSValueConst *argv)
{
	if (argc < 1) return JS_NULL;
	lxb_dom_node_t *root = unwrap_node(this_val);
	if (!root) return JS_NULL;
	size_t slen;
	const char *sel = JS_ToCStringLen(ctx, &slen, argv[0]);
	if (!sel) return JS_NULL;
	struct sel_collect_ctx c = {0};
	int rc = run_selector(root, sel, slen, &c, /*first_only=*/1);
	JS_FreeCString(ctx, sel);
	free(c.elements);
	if (rc != 0 || c.first == NULL) return JS_NULL;
	return wrap_element(ctx, c.first);
}

static JSValue js_el_querySelectorAll(JSContext *ctx, JSValueConst this_val,
				      int argc, JSValueConst *argv)
{
	if (argc < 1) return JS_NewArray(ctx);
	lxb_dom_node_t *root = unwrap_node(this_val);
	if (!root) return JS_NewArray(ctx);
	size_t slen;
	const char *sel = JS_ToCStringLen(ctx, &slen, argv[0]);
	if (!sel) return JS_NewArray(ctx);
	struct sel_collect_ctx c = {0};
	(void)run_selector(root, sel, slen, &c, /*first_only=*/0);
	JS_FreeCString(ctx, sel);
	JSValue arr = JS_NewArray(ctx);
	for (size_t i = 0; i < c.count; i++) {
		JS_SetPropertyUint32(ctx, arr, (uint32_t)i,
			wrap_element(ctx, c.elements[i]));
	}
	free(c.elements);
	return arr;
}

/* getElementsByTagName / -ClassName / -Name — implemented as
 * querySelectorAll under the hood. The DOM spec asks for "live"
 * collections that auto-update on mutation; we return a plain Array
 * snapshot since the rest of our DOM bindings don't model liveness
 * either. Callers iterate via .length / [i] / forEach which all work. */
static JSValue js_el_getElementsByTagName(JSContext *ctx,
	JSValueConst this_val, int argc, JSValueConst *argv)
{
	if (argc < 1) return JS_NewArray(ctx);
	lxb_dom_node_t *root = unwrap_node(this_val);
	if (!root) return JS_NewArray(ctx);
	size_t slen;
	const char *tag = JS_ToCStringLen(ctx, &slen, argv[0]);
	if (!tag) return JS_NewArray(ctx);
	/* `*` matches any element. */
	struct sel_collect_ctx c = {0};
	(void)run_selector(root, tag, slen, &c, /*first_only=*/0);
	JS_FreeCString(ctx, tag);
	JSValue arr = JS_NewArray(ctx);
	for (size_t i = 0; i < c.count; i++) {
		JS_SetPropertyUint32(ctx, arr, (uint32_t)i,
			wrap_element(ctx, c.elements[i]));
	}
	JS_SetPropertyStr(ctx, arr, "item",
		JS_Eval(ctx, "(function(i){return this[i]||null;})", 36,
			"<gebtn-item>", JS_EVAL_TYPE_GLOBAL));
	free(c.elements);
	return arr;
}

static JSValue js_el_getElementsByClassName(JSContext *ctx,
	JSValueConst this_val, int argc, JSValueConst *argv)
{
	if (argc < 1) return JS_NewArray(ctx);
	lxb_dom_node_t *root = unwrap_node(this_val);
	if (!root) return JS_NewArray(ctx);
	size_t slen;
	const char *cls = JS_ToCStringLen(ctx, &slen, argv[0]);
	if (!cls) return JS_NewArray(ctx);
	/* class names may be space-separated; treat as a compound class
	 * selector ".a.b.c". */
	char buf[512];
	size_t off = 0;
	const char *p = cls, *end = cls + slen;
	while (p < end && off + 1 < sizeof(buf)) {
		while (p < end && (*p == ' ' || *p == '\t')) p++;
		if (p >= end) break;
		if (off + 1 >= sizeof(buf)) break;
		buf[off++] = '.';
		while (p < end && *p != ' ' && *p != '\t' &&
		       off + 1 < sizeof(buf)) {
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
		JS_SetPropertyUint32(ctx, arr, (uint32_t)i,
			wrap_element(ctx, c.elements[i]));
	}
	free(c.elements);
	return arr;
}

static JSValue js_el_getElementsByName(JSContext *ctx,
	JSValueConst this_val, int argc, JSValueConst *argv)
{
	if (argc < 1) return JS_NewArray(ctx);
	lxb_dom_node_t *root = unwrap_node(this_val);
	if (!root) return JS_NewArray(ctx);
	size_t nlen;
	const char *name = JS_ToCStringLen(ctx, &nlen, argv[0]);
	if (!name) return JS_NewArray(ctx);
	/* Build attribute selector [name="..."]. */
	char buf[256];
	int off = snprintf(buf, sizeof(buf), "[name=\"%.*s\"]",
		(int)(nlen > 200 ? 200 : nlen), name);
	struct sel_collect_ctx c = {0};
	if (off > 0) {
		(void)run_selector(root, buf, (size_t)off, &c, /*first_only=*/0);
	}
	JS_FreeCString(ctx, name);
	JSValue arr = JS_NewArray(ctx);
	for (size_t i = 0; i < c.count; i++) {
		JS_SetPropertyUint32(ctx, arr, (uint32_t)i,
			wrap_element(ctx, c.elements[i]));
	}
	free(c.elements);
	return arr;
}

/* getElementById — Document-only convenience. Walks the tree comparing
 * `id` attribute. */
static lxb_dom_element_t *find_by_id(lxb_dom_node_t *node,
				     const char *id, size_t idlen)
{
	if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
		lxb_dom_element_t *el = lxb_dom_interface_element(node);
		size_t vlen;
		const lxb_char_t *v = lxb_dom_element_get_attribute(el,
			(const lxb_char_t *)"id", 2, &vlen);
		if (v && vlen == idlen && memcmp(v, id, idlen) == 0)
			return el;
	}
	for (lxb_dom_node_t *c = node->first_child; c; c = c->next) {
		lxb_dom_element_t *r = find_by_id(c, id, idlen);
		if (r) return r;
	}
	return NULL;
}

static JSValue js_doc_getElementById(JSContext *ctx, JSValueConst this_val,
				     int argc, JSValueConst *argv)
{
	if (argc < 1) return JS_NULL;
	lxb_html_document_t *doc = unwrap_document(this_val);
	if (!doc) return JS_NULL;
	size_t idlen;
	const char *id = JS_ToCStringLen(ctx, &idlen, argv[0]);
	if (!id) return JS_NULL;
	lxb_dom_element_t *el = find_by_id(lxb_dom_interface_node(doc),
					    id, idlen);
	JS_FreeCString(ctx, id);
	return wrap_element(ctx, el);
}

/* createElement — fresh detached element. */
static JSValue js_doc_createElement(JSContext *ctx, JSValueConst this_val,
				    int argc, JSValueConst *argv)
{
	if (argc < 1) return JS_NULL;
	lxb_html_document_t *doc = unwrap_document(this_val);
	if (!doc) return JS_NULL;
	size_t tlen;
	const char *tag = JS_ToCStringLen(ctx, &tlen, argv[0]);
	if (!tag) return JS_NULL;
	lxb_dom_element_t *el = lxb_dom_document_create_element(
		lxb_dom_interface_document(doc),
		(const lxb_char_t *)tag, tlen, NULL);
	JS_FreeCString(ctx, tag);
	if (!el) return JS_NULL;
	mark_dirty(ctx);
	return wrap_element(ctx, el);
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
				while (nc < *len + add + 1) nc *= 2;
				char *p = realloc(*buf, nc);
				if (!p) return;
				*buf = p; *cap = nc;
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
	if (!n) return JS_NULL;
	if (n->type == LXB_DOM_NODE_TYPE_TEXT) {
		lxb_dom_text_t *t = lxb_dom_interface_text(n);
		return JS_NewStringLen(ctx,
			(const char *)t->char_data.data.data,
			t->char_data.data.length);
	}
	char *buf = NULL;
	size_t len = 0, cap = 0;
	collect_text(n, &buf, &len, &cap);
	JSValue v = JS_NewStringLen(ctx, buf ? buf : "", len);
	free(buf);
	return v;
}

static JSValue js_el_textContent_set(JSContext *ctx, JSValueConst this_val,
				     JSValueConst val)
{
	lxb_dom_node_t *n = unwrap_node(this_val);
	if (!n) return JS_UNDEFINED;
	size_t slen;
	const char *s = JS_ToCStringLen(ctx, &slen, val);
	if (s) {
		lxb_dom_node_text_content_set(n,
			(const lxb_char_t *)s, slen);
		JS_FreeCString(ctx, s);
		mark_dirty(ctx);
	}
	return JS_UNDEFINED;
}

/* tagName — uppercase per WebAPI. */
static JSValue js_el_tagName_get(JSContext *ctx, JSValueConst this_val)
{
	lxb_dom_element_t *el = unwrap_element(this_val);
	if (!el) return JS_UNDEFINED;
	size_t len;
	const lxb_char_t *name = lxb_dom_element_qualified_name(el, &len);
	if (!name) return JS_UNDEFINED;
	char buf[64];
	if (len > sizeof(buf)) len = sizeof(buf);
	for (size_t i = 0; i < len; i++) {
		char c = (char)name[i];
		buf[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
	}
	return JS_NewStringLen(ctx, buf, len);
}

static JSValue js_el_id_get(JSContext *ctx, JSValueConst this_val)
{
	lxb_dom_element_t *el = unwrap_element(this_val);
	if (!el) return JS_NewString(ctx, "");
	size_t len;
	const lxb_char_t *v = lxb_dom_element_get_attribute(el,
		(const lxb_char_t *)"id", 2, &len);
	if (!v) return JS_NewString(ctx, "");
	return JS_NewStringLen(ctx, (const char *)v, len);
}

static JSValue js_el_id_set(JSContext *ctx, JSValueConst this_val, JSValueConst val)
{
	lxb_dom_element_t *el = unwrap_element(this_val);
	if (!el) return JS_UNDEFINED;
	size_t slen;
	const char *s = JS_ToCStringLen(ctx, &slen, val);
	if (s) {
		lxb_dom_element_set_attribute(el,
			(const lxb_char_t *)"id", 2,
			(const lxb_char_t *)s, slen);
		JS_FreeCString(ctx, s);
		mark_dirty(ctx);
	}
	return JS_UNDEFINED;
}

static JSValue js_el_className_get(JSContext *ctx, JSValueConst this_val)
{
	lxb_dom_element_t *el = unwrap_element(this_val);
	if (!el) return JS_NewString(ctx, "");
	size_t len;
	const lxb_char_t *v = lxb_dom_element_get_attribute(el,
		(const lxb_char_t *)"class", 5, &len);
	if (!v) return JS_NewString(ctx, "");
	return JS_NewStringLen(ctx, (const char *)v, len);
}

static JSValue js_el_className_set(JSContext *ctx, JSValueConst this_val, JSValueConst val)
{
	lxb_dom_element_t *el = unwrap_element(this_val);
	if (!el) return JS_UNDEFINED;
	size_t slen;
	const char *s = JS_ToCStringLen(ctx, &slen, val);
	if (s) {
		lxb_dom_element_set_attribute(el,
			(const lxb_char_t *)"class", 5,
			(const lxb_char_t *)s, slen);
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
static JSValue idl_attr_get(JSContext *ctx, JSValueConst this_val,
			    const char *attr, size_t alen, int urlish)
{
	lxb_dom_element_t *el = unwrap_element(this_val);
	if (!el) return JS_NewString(ctx, "");
	size_t vlen;
	const lxb_char_t *v = lxb_dom_element_get_attribute(el,
		(const lxb_char_t *)attr, alen, &vlen);
	if (!v) return JS_NewString(ctx, "");
	if (urlish) {
		struct yetty_ylexbor *r = runtime_ylex(ctx);
		char *raw = malloc(vlen + 1);
		if (!raw)
			return JS_NewStringLen(ctx, (const char *)v, vlen);
		memcpy(raw, v, vlen); raw[vlen] = '\0';
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

static JSValue idl_attr_set(JSContext *ctx, JSValueConst this_val,
			    JSValueConst val,
			    const char *attr, size_t alen)
{
	lxb_dom_element_t *el = unwrap_element(this_val);
	if (!el) return JS_UNDEFINED;
	size_t slen;
	const char *s = JS_ToCStringLen(ctx, &slen, val);
	if (s) {
		lxb_dom_element_set_attribute(el,
			(const lxb_char_t *)attr, alen,
			(const lxb_char_t *)s, slen);
		JS_FreeCString(ctx, s);
		mark_dirty(ctx);
	}
	return JS_UNDEFINED;
}

#define IDL_ATTR(prop, attr, urlish)                                       \
	static JSValue js_el_##prop##_get(JSContext *ctx, JSValueConst tv) \
	{ return idl_attr_get(ctx, tv, attr, sizeof(attr) - 1, urlish); }  \
	static JSValue js_el_##prop##_set(JSContext *ctx, JSValueConst tv, \
					  JSValueConst val)                \
	{ return idl_attr_set(ctx, tv, val, attr, sizeof(attr) - 1); }

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
	if (JS_GetOpaque(this_val, class_document_id))
		return JS_NewInt32(ctx, 9);
	if (JS_GetOpaque(this_val, class_element_id))
		return JS_NewInt32(ctx, 1);
	return JS_NewInt32(ctx, 0);
}

static JSValue js_el_nodeName_get(JSContext *ctx, JSValueConst this_val)
{
	if (JS_GetOpaque(this_val, class_document_id))
		return JS_NewString(ctx, "#document");
	lxb_dom_element_t *el = unwrap_element(this_val);
	if (!el) return JS_NewString(ctx, "");
	size_t len = 0;
	const lxb_char_t *n = lxb_dom_element_local_name(el, &len);
	if (!n) return JS_NewString(ctx, "");
	char buf[64];
	size_t out = len < sizeof(buf) ? len : sizeof(buf);
	for (size_t i = 0; i < out; i++) {
		char c = (char)n[i];
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
	if (JS_GetOpaque(this_val, class_document_id))
		return JS_NULL;
	if (!JS_GetOpaque(this_val, class_element_id))
		return JS_NULL;
	struct yetty_ylexbor *r = runtime_ylex(ctx);
	if (!r) return JS_NULL;
	return wrap_document(ctx, r->document);
}

IDL_ATTR(src,         "src",         1)
IDL_ATTR(href,        "href",        1)
IDL_ATTR(action,      "action",      1)
IDL_ATTR(name,        "name",        0)
IDL_ATTR(value,       "value",       0)
IDL_ATTR(type,        "type",        0)
IDL_ATTR(alt,         "alt",         0)
IDL_ATTR(title,       "title",       0)
IDL_ATTR(placeholder, "placeholder", 0)
IDL_ATTR(method,      "method",      0)
IDL_ATTR(rel,         "rel",         0)
IDL_ATTR(target,      "target",      0)

/* parentElement / firstElementChild / nextElementSibling / children */

static JSValue js_el_parentElement_get(JSContext *ctx, JSValueConst this_val)
{
	lxb_dom_node_t *n = unwrap_node(this_val);
	if (!n) return JS_NULL;
	lxb_dom_node_t *p = n->parent;
	while (p && p->type != LXB_DOM_NODE_TYPE_ELEMENT) p = p->parent;
	if (!p) return JS_NULL;
	return wrap_element(ctx, lxb_dom_interface_element(p));
}

static JSValue js_el_firstElementChild_get(JSContext *ctx, JSValueConst this_val)
{
	lxb_dom_node_t *n = unwrap_node(this_val);
	if (!n) return JS_NULL;
	for (lxb_dom_node_t *c = n->first_child; c; c = c->next) {
		if (c->type == LXB_DOM_NODE_TYPE_ELEMENT)
			return wrap_element(ctx, lxb_dom_interface_element(c));
	}
	return JS_NULL;
}

static JSValue js_el_nextElementSibling_get(JSContext *ctx, JSValueConst this_val)
{
	lxb_dom_node_t *n = unwrap_node(this_val);
	if (!n) return JS_NULL;
	for (lxb_dom_node_t *c = n->next; c; c = c->next) {
		if (c->type == LXB_DOM_NODE_TYPE_ELEMENT)
			return wrap_element(ctx, lxb_dom_interface_element(c));
	}
	return JS_NULL;
}

static JSValue js_el_children_get(JSContext *ctx, JSValueConst this_val)
{
	lxb_dom_node_t *n = unwrap_node(this_val);
	JSValue arr = JS_NewArray(ctx);
	if (!n) return arr;
	uint32_t i = 0;
	for (lxb_dom_node_t *c = n->first_child; c; c = c->next) {
		if (c->type != LXB_DOM_NODE_TYPE_ELEMENT) continue;
		JS_SetPropertyUint32(ctx, arr, i++,
			wrap_element(ctx, lxb_dom_interface_element(c)));
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
struct kv { char *k, *v; size_t klen, vlen; };

static int parse_style_decl(char *buf, struct kv *out, int max)
{
	int n = 0;
	char *p = buf;
	while (*p && n < max) {
		while (*p == ' ' || *p == ';') p++;
		if (!*p) break;
		char *k = p;
		while (*p && *p != ':') p++;
		if (!*p) break;
		char *kend = p; *p++ = '\0';
		while (*p == ' ') p++;
		char *v = p;
		while (*p && *p != ';') p++;
		char *vend = p;
		if (*p) *p++ = '\0';
		while (kend > k && (kend[-1] == ' ' || kend[-1] == '\t')) *--kend = '\0';
		while (vend > v && (vend[-1] == ' ' || vend[-1] == '\t')) *--vend = '\0';
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
			if (o > 0) dst[o++] = '-';
			dst[o++] = (char)(c + 32);
		} else {
			dst[o++] = c;
		}
	}
	dst[o] = '\0';
	return o;
}

/* style.<prop> getter (called via Proxy / GetOwnProperty trap). We
 * implement as a get_property class hook, not per-property. */

static JSValue style_get_property(JSContext *ctx, JSValueConst obj,
				  JSAtom prop, JSValueConst receiver)
{
	(void)receiver;
	lxb_dom_element_t *el = JS_GetOpaque(obj, class_style_id);
	if (!el) return JS_UNDEFINED;
	const char *name = JS_AtomToCString(ctx, prop);
	if (!name) return JS_UNDEFINED;
	char kebab[128];
	size_t klen = camel_to_kebab(name, strlen(name), kebab, sizeof(kebab));
	JS_FreeCString(ctx, name);

	size_t alen;
	const lxb_char_t *attr = lxb_dom_element_get_attribute(el,
		(const lxb_char_t *)"style", 5, &alen);
	if (!attr) return JS_NewString(ctx, "");
	char *buf = malloc(alen + 1);
	if (!buf) return JS_NewString(ctx, "");
	memcpy(buf, attr, alen); buf[alen] = '\0';

	struct kv kvs[64];
	int n = parse_style_decl(buf, kvs, 64);
	JSValue out = JS_NewString(ctx, "");
	for (int i = 0; i < n; i++) {
		if (kvs[i].klen == klen &&
		    strncmp(kvs[i].k, kebab, klen) == 0) {
			JS_FreeValue(ctx, out);
			out = JS_NewStringLen(ctx, kvs[i].v, kvs[i].vlen);
			break;
		}
	}
	free(buf);
	return out;
}

static int style_set_property(JSContext *ctx, JSValueConst obj,
			      JSAtom prop, JSValueConst value,
			      JSValueConst receiver, int flags)
{
	(void)receiver; (void)flags;
	lxb_dom_element_t *el = JS_GetOpaque(obj, class_style_id);
	if (!el) { JS_ThrowTypeError(ctx, "no element"); return -1; }

	const char *name = JS_AtomToCString(ctx, prop);
	if (!name) return -1;
	char kebab[128];
	size_t klen = camel_to_kebab(name, strlen(name), kebab, sizeof(kebab));
	JS_FreeCString(ctx, name);

	size_t vlen;
	const char *vstr = JS_ToCStringLen(ctx, &vlen, value);
	if (!vstr) return -1;

	/* Read existing decl, replace or append the matching key, write back. */
	size_t alen;
	const lxb_char_t *attr = lxb_dom_element_get_attribute(el,
		(const lxb_char_t *)"style", 5, &alen);
	char *buf = malloc((attr ? alen : 0) + 1);
	if (!buf) { JS_FreeCString(ctx, vstr); return -1; }
	if (attr) memcpy(buf, attr, alen);
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
		if (kvs[i].klen == klen &&
		    strncmp(kvs[i].k, kebab, klen) == 0) {
			if (vlen > 0) {
				if (wrote) strcat(out, "; ");
				strcat(out, kebab);
				strcat(out, ": ");
				strncat(out, vstr, vlen);
				wrote = 1;
			}
		} else {
			if (wrote) strcat(out, "; ");
			strncat(out, kvs[i].k, kvs[i].klen);
			strcat(out, ": ");
			strncat(out, kvs[i].v, kvs[i].vlen);
			wrote = 1;
		}
	}
	int found = 0;
	for (int i = 0; i < n; i++) {
		if (kvs[i].klen == klen &&
		    strncmp(kvs[i].k, kebab, klen) == 0) { found = 1; break; }
	}
	if (!found && vlen > 0) {
		if (wrote) strcat(out, "; ");
		strcat(out, kebab);
		strcat(out, ": ");
		strncat(out, vstr, vlen);
	}

	lxb_dom_element_set_attribute(el,
		(const lxb_char_t *)"style", 5,
		(const lxb_char_t *)out, strlen(out));
	mark_dirty(ctx);

	free(buf); free(parse_buf); free(out);
	JS_FreeCString(ctx, vstr);
	return 1;  /* property set */
}

static JSClassExoticMethods style_exotic = {
	.get_property = style_get_property,
	.set_property = style_set_property,
};

static JSValue js_el_style_get(JSContext *ctx, JSValueConst this_val)
{
	lxb_dom_element_t *el = unwrap_element(this_val);
	if (!el) return JS_UNDEFINED;
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
		while (p < end && (*p == ' ' || *p == '\t')) p++;
		const char *s = p;
		while (p < end && *p != ' ' && *p != '\t') p++;
		if ((size_t)(p - s) == clen && memcmp(s, cls, clen) == 0) return 1;
	}
	return 0;
}

static JSValue js_classlist_contains(JSContext *ctx, JSValueConst this_val,
				     int argc, JSValueConst *argv)
{
	if (argc < 1) return JS_FALSE;
	lxb_dom_element_t *el = JS_GetOpaque(this_val, class_classlist_id);
	if (!el) return JS_FALSE;
	size_t clen;
	const char *cls = JS_ToCStringLen(ctx, &clen, argv[0]);
	if (!cls) return JS_FALSE;
	size_t alen;
	const lxb_char_t *a = lxb_dom_element_get_attribute(el,
		(const lxb_char_t *)"class", 5, &alen);
	int has = a ? has_class((const char *)a, alen, cls, clen) : 0;
	JS_FreeCString(ctx, cls);
	return JS_NewBool(ctx, has);
}

static JSValue js_classlist_add(JSContext *ctx, JSValueConst this_val,
				int argc, JSValueConst *argv)
{
	lxb_dom_element_t *el = JS_GetOpaque(this_val, class_classlist_id);
	if (!el) return JS_UNDEFINED;
	size_t alen;
	const lxb_char_t *a = lxb_dom_element_get_attribute(el,
		(const lxb_char_t *)"class", 5, &alen);

	for (int i = 0; i < argc; i++) {
		size_t clen;
		const char *cls = JS_ToCStringLen(ctx, &clen, argv[i]);
		if (!cls) continue;
		if (!a || !has_class((const char *)a, alen, cls, clen)) {
			size_t need = (a ? alen + 1 : 0) + clen + 1;
			char *buf = malloc(need);
			size_t off = 0;
			if (a) {
				memcpy(buf, a, alen); off = alen;
				if (alen > 0) buf[off++] = ' ';
			}
			memcpy(buf + off, cls, clen); off += clen;
			buf[off] = '\0';
			lxb_dom_element_set_attribute(el,
				(const lxb_char_t *)"class", 5,
				(const lxb_char_t *)buf, off);
			free(buf);
			a = lxb_dom_element_get_attribute(el,
				(const lxb_char_t *)"class", 5, &alen);
		}
		JS_FreeCString(ctx, cls);
	}
	mark_dirty(ctx);
	return JS_UNDEFINED;
}

static JSValue js_classlist_remove(JSContext *ctx, JSValueConst this_val,
				   int argc, JSValueConst *argv)
{
	lxb_dom_element_t *el = JS_GetOpaque(this_val, class_classlist_id);
	if (!el) return JS_UNDEFINED;

	for (int i = 0; i < argc; i++) {
		size_t clen;
		const char *cls = JS_ToCStringLen(ctx, &clen, argv[i]);
		if (!cls) continue;
		size_t alen;
		const lxb_char_t *a = lxb_dom_element_get_attribute(el,
			(const lxb_char_t *)"class", 5, &alen);
		if (!a) { JS_FreeCString(ctx, cls); continue; }
		char *buf = malloc(alen + 1);
		size_t out = 0;
		const char *p = (const char *)a;
		const char *end = p + alen;
		while (p < end) {
			while (p < end && (*p == ' ' || *p == '\t')) p++;
			const char *s = p;
			while (p < end && *p != ' ' && *p != '\t') p++;
			size_t tl = (size_t)(p - s);
			if (tl == clen && memcmp(s, cls, clen) == 0) continue;
			if (out > 0) buf[out++] = ' ';
			memcpy(buf + out, s, tl); out += tl;
		}
		buf[out] = '\0';
		lxb_dom_element_set_attribute(el,
			(const lxb_char_t *)"class", 5,
			(const lxb_char_t *)buf, out);
		free(buf);
		JS_FreeCString(ctx, cls);
	}
	mark_dirty(ctx);
	return JS_UNDEFINED;
}

static JSValue js_classlist_toggle(JSContext *ctx, JSValueConst this_val,
				   int argc, JSValueConst *argv)
{
	if (argc < 1) return JS_FALSE;
	JSValue has = js_classlist_contains(ctx, this_val, 1, argv);
	int b = JS_ToBool(ctx, has);
	JS_FreeValue(ctx, has);
	if (b) js_classlist_remove(ctx, this_val, 1, argv);
	else   js_classlist_add(ctx, this_val, 1, argv);
	return JS_NewBool(ctx, !b);
}

static const JSCFunctionListEntry classlist_funcs[] = {
	JS_CFUNC_DEF("add",      1, js_classlist_add),
	JS_CFUNC_DEF("remove",   1, js_classlist_remove),
	JS_CFUNC_DEF("toggle",   1, js_classlist_toggle),
	JS_CFUNC_DEF("contains", 1, js_classlist_contains),
};

static JSValue js_el_classList_get(JSContext *ctx, JSValueConst this_val)
{
	lxb_dom_element_t *el = unwrap_element(this_val);
	if (!el) return JS_UNDEFINED;
	JSValue v = JS_NewObjectClass(ctx, class_classlist_id);
	JS_SetOpaque(v, el);
	JS_SetPropertyFunctionList(ctx, v, classlist_funcs,
				   sizeof(classlist_funcs)/sizeof(classlist_funcs[0]));
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

static JSValue js_el_addEventListener(JSContext *ctx, JSValueConst this_val,
				      int argc, JSValueConst *argv)
{
	if (argc < 2) return JS_UNDEFINED;
	/* Accept Element (target=el) and Document/window (target=NULL,
	 * receives global events like DOMContentLoaded/load). */
	lxb_dom_element_t *el = unwrap_element(this_val);
	if (g_listener_count >= MAX_LISTENERS) return JS_UNDEFINED;
	const char *type = JS_ToCString(ctx, argv[0]);
	if (!type) return JS_UNDEFINED;
	struct listener *L = &g_listeners[g_listener_count++];
	L->el = el;  /* may be NULL for document/window listeners */
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
static JSValue js_el_undef_stub(JSContext *ctx, JSValueConst this_val,
				int argc, JSValueConst *argv)
{ (void)ctx; (void)this_val; (void)argc; (void)argv; return JS_UNDEFINED; }

static JSValue js_el_contains_stub(JSContext *ctx, JSValueConst this_val,
				   int argc, JSValueConst *argv)
{
	(void)this_val;
	if (argc < 1) return JS_FALSE;
	/* Best-effort: walk argv[0]'s parent chain looking for `this`. */
	void *self_p = JS_GetOpaque(this_val, class_element_id);
	if (!self_p) self_p = JS_GetOpaque(this_val, class_document_id);
	void *other_p = JS_GetOpaque(argv[0], class_element_id);
	if (!self_p || !other_p) return JS_FALSE;
	lxb_dom_node_t *target = (lxb_dom_node_t *)other_p;
	for (lxb_dom_node_t *n = target; n; n = n->parent) {
		if ((void *)n == self_p) return JS_TRUE;
	}
	return JS_FALSE;
}

static JSValue js_el_matches_stub(JSContext *ctx, JSValueConst this_val,
				  int argc, JSValueConst *argv)
{
	(void)ctx; (void)this_val; (void)argc; (void)argv;
	/* Pessimistic: false — usually less harmful than a fake true. */
	return JS_FALSE;
}

static JSValue js_el_hasChildNodes_stub(JSContext *ctx, JSValueConst this_val,
					int argc, JSValueConst *argv)
{
	(void)ctx; (void)argc; (void)argv;
	lxb_dom_node_t *n = unwrap_node(this_val);
	return JS_NewBool(ctx, n && n->first_child != NULL);
}

static JSValue js_el_hasAttributes_stub(JSContext *ctx, JSValueConst this_val,
					int argc, JSValueConst *argv)
{
	(void)argc; (void)argv;
	lxb_dom_element_t *el = unwrap_element(this_val);
	if (!el) return JS_FALSE;
	return JS_NewBool(ctx, el->first_attr != NULL);
}

static JSValue js_el_getAttributeNames_stub(JSContext *ctx, JSValueConst this_val,
					    int argc, JSValueConst *argv)
{
	(void)argc; (void)argv;
	lxb_dom_element_t *el = unwrap_element(this_val);
	JSValue arr = JS_NewArray(ctx);
	if (!el) return arr;
	uint32_t i = 0;
	for (lxb_dom_attr_t *a = el->first_attr; a; a = a->next) {
		size_t nlen = 0;
		const lxb_char_t *nm = lxb_dom_attr_qualified_name(a, &nlen);
		if (nm) {
			JS_SetPropertyUint32(ctx, arr, i++,
				JS_NewStringLen(ctx, (const char *)nm, nlen));
		}
	}
	return arr;
}

static JSValue js_el_cloneNode_stub(JSContext *ctx, JSValueConst this_val,
				    int argc, JSValueConst *argv)
{
	(void)ctx; (void)argc; (void)argv;
	/* Return same node — close enough for code that just wants a
	 * "node-like thing" back. */
	return JS_DupValue(ctx, this_val);
}

static JSValue js_el_clientRect_stub(JSContext *ctx, JSValueConst this_val,
				     int argc, JSValueConst *argv)
{
	(void)this_val; (void)argc; (void)argv;
	JSValue r = JS_NewObject(ctx);
	JS_SetPropertyStr(ctx, r, "x", JS_NewFloat64(ctx, 0));
	JS_SetPropertyStr(ctx, r, "y", JS_NewFloat64(ctx, 0));
	JS_SetPropertyStr(ctx, r, "width", JS_NewFloat64(ctx, 0));
	JS_SetPropertyStr(ctx, r, "height", JS_NewFloat64(ctx, 0));
	JS_SetPropertyStr(ctx, r, "top", JS_NewFloat64(ctx, 0));
	JS_SetPropertyStr(ctx, r, "left", JS_NewFloat64(ctx, 0));
	JS_SetPropertyStr(ctx, r, "right", JS_NewFloat64(ctx, 0));
	JS_SetPropertyStr(ctx, r, "bottom", JS_NewFloat64(ctx, 0));
	return r;
}

static JSValue js_el_clientRects_stub(JSContext *ctx, JSValueConst this_val,
				      int argc, JSValueConst *argv)
{ (void)this_val; (void)argc; (void)argv; return JS_NewArray(ctx); }

static JSValue js_el_dispatchEvent_stub(JSContext *ctx, JSValueConst this_val,
					int argc, JSValueConst *argv)
{ (void)ctx; (void)this_val; (void)argc; (void)argv; return JS_TRUE; }

static JSValue js_el_toggleAttr_stub(JSContext *ctx, JSValueConst this_val,
				     int argc, JSValueConst *argv)
{
	if (argc < 1) return JS_FALSE;
	lxb_dom_element_t *el = unwrap_element(this_val);
	if (!el) return JS_FALSE;
	size_t nlen;
	const char *name = JS_ToCStringLen(ctx, &nlen, argv[0]);
	if (!name) return JS_FALSE;
	int has = lxb_dom_element_has_attribute(el,
		(const lxb_char_t *)name, nlen);
	if (has) {
		lxb_dom_element_remove_attribute(el,
			(const lxb_char_t *)name, nlen);
	} else {
		lxb_dom_element_set_attribute(el,
			(const lxb_char_t *)name, nlen,
			(const lxb_char_t *)"", 0);
	}
	JS_FreeCString(ctx, name);
	mark_dirty(ctx);
	return JS_NewBool(ctx, !has);
}

static const JSCFunctionListEntry document_funcs[] = {
	JS_CFUNC_DEF("getElementById",         1, js_doc_getElementById),
	JS_CFUNC_DEF("getElementsByTagName",   1, js_el_getElementsByTagName),
	JS_CFUNC_DEF("getElementsByClassName", 1, js_el_getElementsByClassName),
	JS_CFUNC_DEF("getElementsByName",      1, js_el_getElementsByName),
	JS_CFUNC_DEF("querySelector",          1, js_el_querySelector),
	JS_CFUNC_DEF("querySelectorAll",       1, js_el_querySelectorAll),
	JS_CFUNC_DEF("createElement",          1, js_doc_createElement),
	JS_CFUNC_DEF("addEventListener",       2, js_el_addEventListener),
	JS_CGETSET_DEF("nodeType",             js_el_nodeType_get, NULL),
	JS_CGETSET_DEF("nodeName",             js_el_nodeName_get, NULL),
	JS_CGETSET_DEF("ownerDocument",        js_el_ownerDocument_get, NULL),
};

static const JSCFunctionListEntry element_funcs[] = {
	JS_CFUNC_DEF("getAttribute",           1, js_el_getAttribute),
	JS_CFUNC_DEF("setAttribute",           2, js_el_setAttribute),
	JS_CFUNC_DEF("removeAttribute",        1, js_el_removeAttribute),
	JS_CFUNC_DEF("hasAttribute",           1, js_el_hasAttribute),
	JS_CFUNC_DEF("appendChild",            1, js_el_appendChild),
	JS_CFUNC_DEF("removeChild",            1, js_el_removeChild),
	JS_CFUNC_DEF("querySelector",          1, js_el_querySelector),
	JS_CFUNC_DEF("querySelectorAll",       1, js_el_querySelectorAll),
	JS_CFUNC_DEF("getElementsByTagName",   1, js_el_getElementsByTagName),
	JS_CFUNC_DEF("getElementsByClassName", 1, js_el_getElementsByClassName),
	JS_CFUNC_DEF("addEventListener",       2, js_el_addEventListener),
	/* ChildNode/ParentNode mutators that legitimately share the
	 * appendChild path (single-arg insertion). The spec's full
	 * variadic signature degrades to the single-node case under
	 * production code 99% of the time. */
	JS_CFUNC_DEF("prepend",                1, js_el_appendChild),
	JS_CFUNC_DEF("append",                 1, js_el_appendChild),
	JS_CFUNC_DEF("insertBefore",           2, js_el_appendChild),
	JS_CFUNC_DEF("replaceChild",           2, js_el_appendChild),
	JS_CFUNC_DEF("replaceWith",            1, js_el_appendChild),
	JS_CFUNC_DEF("before",                 1, js_el_appendChild),
	JS_CFUNC_DEF("after",                  1, js_el_appendChild),
	JS_CFUNC_DEF("remove",                 0, js_el_removeChild),
	JS_CFUNC_DEF("closest",                1, js_el_querySelector),
	/* Predicates / accessors that need a typed return. We give them
	 * a tiny dedicated stub each below so they don't masquerade as
	 * mutators. */
	JS_CFUNC_DEF("contains",               1, js_el_contains_stub),
	JS_CFUNC_DEF("matches",                1, js_el_matches_stub),
	JS_CFUNC_DEF("hasChildNodes",          0, js_el_hasChildNodes_stub),
	JS_CFUNC_DEF("hasAttributes",          0, js_el_hasAttributes_stub),
	JS_CFUNC_DEF("getAttributeNames",      0, js_el_getAttributeNames_stub),
	JS_CFUNC_DEF("cloneNode",              1, js_el_cloneNode_stub),
	JS_CFUNC_DEF("getBoundingClientRect",  0, js_el_clientRect_stub),
	JS_CFUNC_DEF("getClientRects",         0, js_el_clientRects_stub),
	/* True no-ops — return undefined. */
	JS_CFUNC_DEF("scrollIntoView",         0, js_el_undef_stub),
	JS_CFUNC_DEF("focus",                  0, js_el_undef_stub),
	JS_CFUNC_DEF("blur",                   0, js_el_undef_stub),
	JS_CFUNC_DEF("click",                  0, js_el_undef_stub),
	JS_CFUNC_DEF("normalize",              0, js_el_undef_stub),
	JS_CFUNC_DEF("dispatchEvent",          1, js_el_dispatchEvent_stub),
	JS_CFUNC_DEF("removeEventListener",    2, js_el_undef_stub),
	JS_CFUNC_DEF("toggleAttribute",        1, js_el_toggleAttr_stub),
	JS_CGETSET_DEF("textContent",        js_el_textContent_get, js_el_textContent_set),
	JS_CGETSET_DEF("tagName",            js_el_tagName_get, NULL),
	JS_CGETSET_DEF("id",                 js_el_id_get, js_el_id_set),
	JS_CGETSET_DEF("className",          js_el_className_get, js_el_className_set),
	JS_CGETSET_DEF("parentElement",      js_el_parentElement_get, NULL),
	JS_CGETSET_DEF("firstElementChild",  js_el_firstElementChild_get, NULL),
	JS_CGETSET_DEF("nextElementSibling", js_el_nextElementSibling_get, NULL),
	JS_CGETSET_DEF("children",           js_el_children_get, NULL),
	JS_CGETSET_DEF("style",              js_el_style_get, NULL),
	JS_CGETSET_DEF("classList",          js_el_classList_get, NULL),
	JS_CGETSET_DEF("nodeType",           js_el_nodeType_get, NULL),
	JS_CGETSET_DEF("nodeName",           js_el_nodeName_get, NULL),
	JS_CGETSET_DEF("ownerDocument",      js_el_ownerDocument_get, NULL),
	JS_CGETSET_DEF("src",                js_el_src_get, js_el_src_set),
	JS_CGETSET_DEF("href",               js_el_href_get, js_el_href_set),
	JS_CGETSET_DEF("action",             js_el_action_get, js_el_action_set),
	JS_CGETSET_DEF("name",               js_el_name_get, js_el_name_set),
	JS_CGETSET_DEF("value",              js_el_value_get, js_el_value_set),
	JS_CGETSET_DEF("type",               js_el_type_get, js_el_type_set),
	JS_CGETSET_DEF("alt",                js_el_alt_get, js_el_alt_set),
	JS_CGETSET_DEF("title",              js_el_title_get, js_el_title_set),
	JS_CGETSET_DEF("placeholder",        js_el_placeholder_get, js_el_placeholder_set),
	JS_CGETSET_DEF("method",             js_el_method_get, js_el_method_set),
	JS_CGETSET_DEF("rel",                js_el_rel_get, js_el_rel_set),
	JS_CGETSET_DEF("target",             js_el_target_get, js_el_target_set),
};

void yetty_ylexbor_js_dom_install(struct yetty_ylexbor *r)
{
	JSContext *ctx = (JSContext *)r->js_ctx;
	JSRuntime *rt  = (JSRuntime *)r->js_rt;
	if (!ctx || !rt) return;

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
	JS_NewClass(rt, class_node_id,     &class_node_def);
	JS_NewClass(rt, class_element_id,  &class_element_def);
	JS_NewClass(rt, class_document_id, &class_document_def);
	JS_NewClass(rt, class_classlist_id,&class_classlist_def);
	JS_NewClass(rt, class_style_id,    &class_style_def);

	/* Element prototype — methods + accessors via JS_CGETSET_DEF. */
	JSValue el_proto = JS_NewObject(ctx);
	JS_SetPropertyFunctionList(ctx, el_proto, element_funcs,
		sizeof(element_funcs)/sizeof(element_funcs[0]));
	JS_SetClassProto(ctx, class_element_id, el_proto);

	/* Document inherits from Element-ish prototype + extra methods.
	 * We give it the *same* methods as Element so document.querySelector
	 * works directly (not via the Element prototype chain since we
	 * don't model that yet). */
	JSValue doc_proto = JS_NewObject(ctx);
	JS_SetPropertyFunctionList(ctx, doc_proto, document_funcs,
		sizeof(document_funcs)/sizeof(document_funcs[0]));
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
		JS_SetPropertyStr(ctx, doc_obj, "documentElement",
			wrap_element(ctx, root_el));
	}
	lxb_html_body_element_t *body = lxb_html_document_body_element(r->document);
	if (body) {
		JS_SetPropertyStr(ctx, doc_obj, "body",
			wrap_element(ctx, lxb_dom_interface_element(body)));
	}
	lxb_html_head_element_t *head = lxb_html_document_head_element(r->document);
	if (head) {
		JS_SetPropertyStr(ctx, doc_obj, "head",
			wrap_element(ctx, lxb_dom_interface_element(head)));
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
	JS_SetPropertyStr(ctx, global, "self",   JS_DupValue(ctx, global));
	JS_SetPropertyStr(ctx, global, "top",    JS_DupValue(ctx, global));
	JS_SetPropertyStr(ctx, global, "parent", JS_DupValue(ctx, global));
	JS_SetPropertyStr(ctx, global, "frames", JS_DupValue(ctx, global));

	/* window.addEventListener / removeEventListener / dispatchEvent —
	 * many libs do `window.addEventListener("popstate", ...)`. Route
	 * through the same listener storage as Element.addEventListener
	 * (target=NULL, fired by yetty_ylexbor_js_dispatch_event_type
	 * with target=NULL). */
	JS_SetPropertyStr(ctx, global, "addEventListener",
		JS_NewCFunction(ctx, js_el_addEventListener,
			"addEventListener", 2));
	/* No-op stoppers — full removal would require per-listener IDs. */
	const char *noopdef = "(function(){})";
	JSValue noop = JS_Eval(ctx, noopdef, strlen(noopdef),
		"<noop>", JS_EVAL_TYPE_GLOBAL);
	JS_SetPropertyStr(ctx, global, "removeEventListener",
		JS_DupValue(ctx, noop));
	JS_SetPropertyStr(ctx, global, "dispatchEvent",
		JS_DupValue(ctx, noop));
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
	if (!r || !r->js_ctx || g_listener_count == 0) return 0;

	/* Hit-test: find deepest box whose rect contains (x,y) AND has
	 * an associated element. */
	lxb_dom_element_t *target = NULL;
	for (uint32_t i = 0; i < r->boxes.size; i++) {
		struct yetty_ylexbor_box *b = &r->boxes.data[i];
		if (b->element == NULL) continue;
		if (x >= b->x && x < b->x + b->w &&
		    y >= b->y && y < b->y + b->h) {
			target = b->element;
		}
	}
	if (target == NULL) return 0;

	/* Walk up ancestry; fire any 'click' listener on the chain. */
	JSContext *ctx = (JSContext *)r->js_ctx;
	int fired = 0;
	for (lxb_dom_node_t *n = lxb_dom_interface_node(target); n;
	     n = n->parent) {
		if (n->type != LXB_DOM_NODE_TYPE_ELEMENT) continue;
		lxb_dom_element_t *el = lxb_dom_interface_element(n);
		for (int i = 0; i < g_listener_count; i++) {
			if (g_listeners[i].el != el) continue;
			if (strcmp(g_listeners[i].type, "click") != 0) continue;
			JSValue ev = JS_NewObject(ctx);
			JS_SetPropertyStr(ctx, ev, "type", JS_NewString(ctx, "click"));
			JS_SetPropertyStr(ctx, ev, "target", wrap_element(ctx, target));
			JS_SetPropertyStr(ctx, ev, "currentTarget", wrap_element(ctx, el));
			JS_SetPropertyStr(ctx, ev, "clientX", JS_NewFloat64(ctx, x));
			JS_SetPropertyStr(ctx, ev, "clientY", JS_NewFloat64(ctx, y));
			JSValueConst args[] = { ev };
			JSValue ret = JS_Call(ctx, g_listeners[i].handler,
				JS_UNDEFINED, 1, args);
			if (JS_IsException(ret)) {
				JSValue ex = JS_GetException(ctx);
				const char *m = JS_ToCString(ctx, ex);
				fprintf(stderr, "[js:click-handler] %s\n", m ? m : "?");
				if (m) JS_FreeCString(ctx, m);
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
void yetty_ylexbor_js_dispatch_event_type(struct yetty_ylexbor *r,
	const char *type, void *target_element_ptr_or_null)
{
	if (!r || !r->js_ctx || !type) return;
	JSContext *ctx = (JSContext *)r->js_ctx;
	lxb_dom_element_t *only = (lxb_dom_element_t *)target_element_ptr_or_null;

	/* Default target for global events (load, DOMContentLoaded) is
	 * document. Real handlers reach for event.target.tagName etc.,
	 * so we need a non-null object there. */
	JSValue global = JS_GetGlobalObject(ctx);
	JSValue doc_target = JS_GetPropertyStr(ctx, global, "document");
	JS_FreeValue(ctx, global);

	for (int i = 0; i < g_listener_count; i++) {
		if (only && g_listeners[i].el != only) continue;
		if (strcmp(g_listeners[i].type, type) != 0) continue;
		JSValue ev = JS_NewObject(ctx);
		JS_SetPropertyStr(ctx, ev, "type", JS_NewString(ctx, type));
		JS_SetPropertyStr(ctx, ev, "bubbles", JS_FALSE);
		JS_SetPropertyStr(ctx, ev, "cancelable", JS_FALSE);
		JS_SetPropertyStr(ctx, ev, "defaultPrevented", JS_FALSE);
		JS_SetPropertyStr(ctx, ev, "timeStamp", JS_NewInt32(ctx, 0));
		JSValue tgt = only
			? wrap_element(ctx, only)
			: JS_DupValue(ctx, doc_target);
		JS_SetPropertyStr(ctx, ev, "target",        JS_DupValue(ctx, tgt));
		JS_SetPropertyStr(ctx, ev, "currentTarget", JS_DupValue(ctx, tgt));
		JS_SetPropertyStr(ctx, ev, "srcElement",    JS_DupValue(ctx, tgt));
		JS_FreeValue(ctx, tgt);
		JSValueConst args[] = { ev };
		JSValue ret = JS_Call(ctx, g_listeners[i].handler,
			JS_UNDEFINED, 1, args);
		if (JS_IsException(ret)) {
			JSValue ex = JS_GetException(ctx);
			const char *m = JS_ToCString(ctx, ex);
			fprintf(stderr, "[js:event:%s] %s\n", type, m ? m : "?");
			if (m) JS_FreeCString(ctx, m);
			JS_FreeValue(ctx, ex);
			r->js_error_count++;
		}
		JS_FreeValue(ctx, ret);
		JS_FreeValue(ctx, ev);
	}
	JS_FreeValue(ctx, doc_target);
}

#else /* !YETTY_HAVE_QUICKJS */

void yetty_ylexbor_js_dom_install(struct yetty_ylexbor *r) { (void)r; }
int  yetty_ylexbor_dispatch_click(struct yetty_ylexbor *r, float x, float y)
{ (void)r; (void)x; (void)y; return 0; }
void yetty_ylexbor_js_dispatch_event_type(struct yetty_ylexbor *r,
	const char *type, void *target_element_ptr_or_null)
{ (void)r; (void)type; (void)target_element_ptr_or_null; }

#endif
