/*
 * ylexbor-js — QuickJS integration for ylexbor.
 *
 * MVP scope (this file):
 *   - One JSRuntime + JSContext per ylexbor instance, created lazily.
 *   - Walk the parsed DOM, find <script> elements with no `src`
 *     attribute, JS_Eval their inline text.
 *   - Provide a minimal `console.{log,error,warn,info,debug}` that
 *     prints to stderr (one line per call, args space-separated).
 *   - Surface uncaught exceptions to stderr; bump r->js_error_count
 *     so the host can decide whether to keep going.
 *
 * Explicitly NOT in this MVP:
 *   - DOM bindings (document.*, Element.*, addEventListener) — that's
 *     the next iteration and is the bigger of the two halves.
 *   - External <script src=...>: needs a fetcher + ordering rules.
 *   - <script type=module>: needs an import resolver.
 *   - Timers (setTimeout/setInterval) / microtasks — needs an event
 *     loop hook.
 *   - fetch() / XMLHttpRequest.
 *   - Re-running scripts on DOM mutation.
 *
 * Compile-out: when YETTY_HAVE_QUICKJS is 0 this whole TU compiles to
 * a set of no-op stubs so callers don't have to ifdef.
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
#include <lexbor/tag/const.h>


/* ===========================================================================
 * console.* binding — one C function per level.
 * ===========================================================================*/

static void console_print(JSContext *ctx, FILE *out, const char *level,
			  int argc, JSValueConst *argv)
{
	fprintf(out, "[js:%s] ", level);
	for (int i = 0; i < argc; i++) {
		const char *s = JS_ToCString(ctx, argv[i]);
		if (s) {
			if (i > 0) fputc(' ', out);
			fputs(s, out);
			JS_FreeCString(ctx, s);
		}
	}
	fputc('\n', out);
	fflush(out);
}

#define DEFINE_CONSOLE_FN(name, level, fp)                                \
	static JSValue js_console_##name(JSContext *ctx, JSValueConst this_val, \
					 int argc, JSValueConst *argv)    \
	{                                                                  \
		(void)this_val;                                            \
		console_print(ctx, fp, level, argc, argv);                 \
		return JS_UNDEFINED;                                       \
	}

DEFINE_CONSOLE_FN(log,   "log",   stderr)
DEFINE_CONSOLE_FN(info,  "info",  stderr)
DEFINE_CONSOLE_FN(debug, "debug", stderr)
DEFINE_CONSOLE_FN(warn,  "warn",  stderr)
DEFINE_CONSOLE_FN(error, "error", stderr)

static const JSCFunctionListEntry console_funcs[] = {
	JS_CFUNC_DEF("log",   1, js_console_log),
	JS_CFUNC_DEF("info",  1, js_console_info),
	JS_CFUNC_DEF("debug", 1, js_console_debug),
	JS_CFUNC_DEF("warn",  1, js_console_warn),
	JS_CFUNC_DEF("error", 1, js_console_error),
};

static void install_console(JSContext *ctx)
{
	JSValue global = JS_GetGlobalObject(ctx);
	JSValue console = JS_NewObject(ctx);
	JS_SetPropertyFunctionList(ctx, console, console_funcs,
				   sizeof(console_funcs) / sizeof(console_funcs[0]));
	JS_SetPropertyStr(ctx, global, "console", console);
	JS_FreeValue(ctx, global);
}

/* ===========================================================================
 * Runtime lifecycle
 * ===========================================================================*/

struct yetty_ycore_void_result yetty_ylexbor_js_init(struct yetty_ylexbor *r)
{
	if (r->js_rt) return YETTY_OK_VOID();  /* idempotent */

	JSRuntime *rt = JS_NewRuntime();
	if (rt == NULL)
		return YETTY_ERR(yetty_ycore_void, "JS_NewRuntime");
	JSContext *ctx = JS_NewContext(rt);
	if (ctx == NULL) {
		JS_FreeRuntime(rt);
		return YETTY_ERR(yetty_ycore_void, "JS_NewContext");
	}
	install_console(ctx);

	r->js_rt = (struct JSRuntime *)rt;
	r->js_ctx = (struct JSContext *)ctx;
	return YETTY_OK_VOID();
}

void yetty_ylexbor_js_destroy(struct yetty_ylexbor *r)
{
	if (r->js_ctx) JS_FreeContext((JSContext *)r->js_ctx);
	if (r->js_rt)  JS_FreeRuntime((JSRuntime *)r->js_rt);
	r->js_ctx = NULL;
	r->js_rt = NULL;
}

/* ===========================================================================
 * Run all inline <script> elements once.
 * ===========================================================================*/

static void report_exception(JSContext *ctx, const char *url)
{
	JSValue ex = JS_GetException(ctx);
	int is_err = JS_IsError(ex);
	const char *msg = JS_ToCString(ctx, ex);
	fprintf(stderr, "[js:exception] %s: %s\n",
		url ? url : "<inline>", msg ? msg : "?");
	if (msg) JS_FreeCString(ctx, msg);

	if (is_err) {
		JSValue stack = JS_GetPropertyStr(ctx, ex, "stack");
		if (!JS_IsUndefined(stack)) {
			const char *st = JS_ToCString(ctx, stack);
			if (st) {
				fprintf(stderr, "%s\n", st);
				JS_FreeCString(ctx, st);
			}
		}
		JS_FreeValue(ctx, stack);
	}
	JS_FreeValue(ctx, ex);
}

/* Iterate <script> elements in document order. lexbor doesn't expose a
 * direct iterator, so we walk the tree. Skip elements with `src` (no
 * external fetcher yet) and those with non-JS `type`. */
static void run_scripts_recursive(struct yetty_ylexbor *r,
				  JSContext *ctx,
				  lxb_dom_node_t *node)
{
	for (lxb_dom_node_t *c = node->first_child; c != NULL; c = c->next) {
		if (c->type == LXB_DOM_NODE_TYPE_ELEMENT &&
		    c->local_name == LXB_TAG_SCRIPT) {

			lxb_dom_element_t *el = lxb_dom_interface_element(c);
			size_t attr_len = 0;
			(void)attr_len;
			if (lxb_dom_element_has_attribute(el,
				(const lxb_char_t *)"src", 3)) {
				continue;  /* external script — TODO */
			}
			/* Type filter: accept missing, "", or text/javascript* */
			size_t tlen = 0;
			const lxb_char_t *type =
				lxb_dom_element_get_attribute(el,
					(const lxb_char_t *)"type", 4, &tlen);
			if (type && tlen > 0) {
				if (!(tlen >= 15 &&
				      strncmp((const char *)type,
					      "text/javascript", 15) == 0) &&
				    !(tlen == 16 &&
				      strncmp((const char *)type,
					      "application/javascript", 16) == 0)) {
					continue;
				}
			}

			/* Concatenate the script's text-node children. */
			char  *src  = NULL;
			size_t slen = 0, scap = 0;
			for (lxb_dom_node_t *t = c->first_child; t; t = t->next) {
				if (t->type != LXB_DOM_NODE_TYPE_TEXT) continue;
				lxb_dom_text_t *tn = lxb_dom_interface_text(t);
				size_t n = tn->char_data.data.length;
				const lxb_char_t *p = tn->char_data.data.data;
				if (slen + n + 1 > scap) {
					size_t nc = scap ? scap * 2 : 256;
					while (nc < slen + n + 1) nc *= 2;
					char *np = realloc(src, nc);
					if (!np) { free(src); src = NULL; break; }
					src = np; scap = nc;
				}
				if (!src) break;
				memcpy(src + slen, p, n);
				slen += n;
			}
			if (src && slen > 0) {
				src[slen] = '\0';
				JSValue v = JS_Eval(ctx, src, slen,
					"<inline>",
					JS_EVAL_TYPE_GLOBAL);
				if (JS_IsException(v)) {
					report_exception(ctx, "<inline>");
					r->js_error_count++;
				}
				JS_FreeValue(ctx, v);
			}
			free(src);
			continue;  /* don't recurse into <script> children */
		}
		if (c->first_child) {
			run_scripts_recursive(r, ctx, c);
		}
	}
}

struct yetty_ycore_void_result yetty_ylexbor_js_run_inline_scripts(
	struct yetty_ylexbor *r)
{
	if (r == NULL || r->document == NULL) return YETTY_OK_VOID();

	struct yetty_ycore_void_result ir = yetty_ylexbor_js_init(r);
	if (YETTY_IS_ERR(ir)) return ir;

	run_scripts_recursive(r, (JSContext *)r->js_ctx,
		lxb_dom_interface_node(r->document));
	return YETTY_OK_VOID();
}

#else /* !YETTY_HAVE_QUICKJS — compile-out */

struct yetty_ycore_void_result yetty_ylexbor_js_init(struct yetty_ylexbor *r)
{
	(void)r; return YETTY_OK_VOID();
}
void yetty_ylexbor_js_destroy(struct yetty_ylexbor *r) { (void)r; }
struct yetty_ycore_void_result yetty_ylexbor_js_run_inline_scripts(
	struct yetty_ylexbor *r)
{
	(void)r; return YETTY_OK_VOID();
}

#endif /* YETTY_HAVE_QUICKJS */
