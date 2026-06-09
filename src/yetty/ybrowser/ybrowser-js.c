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
#include <lexbor/tag/const.h>

#include <yetty/ytrace/ytrace.h>

/* ===========================================================================
 * console.* binding — one C function per level.
 * ===========================================================================*/

/* Page console.* goes through ytrace (off by default; visible with
 * YTRACE_DEFAULT_ON). Real sites log copiously and many partially fail on
 * this minimal JS surface — spewing that straight to stderr would clutter
 * the operator's terminal in standalone, and corrupt the OSC stream when
 * the engine runs under `yetty -e` (stderr → PTY slave). */
static void console_print(JSContext *ctx, const char *level, int argc, JSValueConst *argv)
{
    char buf[1024];
    size_t off = 0;
    for (int i = 0; i < argc && off < sizeof(buf) - 1; i++) {
        const char *s = JS_ToCString(ctx, argv[i]);
        if (!s) {
            continue;
        }
        if (i > 0 && off < sizeof(buf) - 1) {
            buf[off++] = ' ';
        }
        size_t room = sizeof(buf) - 1 - off;
        size_t sl = strlen(s);
        size_t cp = sl < room ? sl : room;
        memcpy(buf + off, s, cp);
        off += cp;
        JS_FreeCString(ctx, s);
    }
    buf[off] = '\0';
    ydebug("[js:%s] %s", level, buf);
    /* Also mirror to stderr when YBROWSER_JS_CONSOLE is set — the ybrowser
     * tool enables this in its standalone (own-window) mode so page JS
     * errors are visible for debugging, while leaving it off under
     * `yetty -e` (where stderr → the PTY would corrupt the OSC stream). */
    static int to_stderr = -1;
    if (to_stderr < 0) {
        to_stderr = getenv("YBROWSER_JS_CONSOLE") != NULL;
    }
    if (to_stderr) {
        fprintf(stderr, "[js:%s] %s\n", level, buf);
        fflush(stderr);
    }
}

#define DEFINE_CONSOLE_FN(name, level)                                                             \
    static JSValue js_console_##name(JSContext *ctx, JSValueConst this_val, int argc,              \
                                     JSValueConst *argv)                                           \
    {                                                                                              \
        (void)this_val;                                                                            \
        console_print(ctx, level, argc, argv);                                                     \
        return JS_UNDEFINED;                                                                       \
    }

DEFINE_CONSOLE_FN(log, "log")
DEFINE_CONSOLE_FN(info, "info")
DEFINE_CONSOLE_FN(debug, "debug")
DEFINE_CONSOLE_FN(warn, "warn")
DEFINE_CONSOLE_FN(error, "error")

static const JSCFunctionListEntry console_funcs[] = {
    JS_CFUNC_DEF("log", 1, js_console_log),     JS_CFUNC_DEF("info", 1, js_console_info),
    JS_CFUNC_DEF("debug", 1, js_console_debug), JS_CFUNC_DEF("warn", 1, js_console_warn),
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
    if (r->js_rt) {
        return YETTY_OK_VOID(); /* idempotent */
    }

    JSRuntime *rt = JS_NewRuntime();
    if (rt == NULL) {
        return YETTY_ERR(yetty_ycore_void, "JS_NewRuntime");
    }
    JSContext *ctx = JS_NewContext(rt);
    if (ctx == NULL) {
        JS_FreeRuntime(rt);
        return YETTY_ERR(yetty_ycore_void, "JS_NewContext");
    }
    install_console(ctx);

    r->js_rt = (struct JSRuntime *)rt;
    r->js_ctx = (struct JSContext *)ctx;

    /* Install DOM bindings (document, Element, classList, style, …). */
    yetty_ylexbor_js_dom_install(r);
    /* Install WebAPI surface (fetch, timers, navigator, location,
	 * localStorage, crypto, plus a big JS stub blob for Worker /
	 * AbortController / MutationObserver / XMLHttpRequest etc). Must
	 * run AFTER dom_install so it can bolt accessors onto document. */
    yetty_ylexbor_js_web_install(r);
    return YETTY_OK_VOID();
}

void yetty_ylexbor_js_destroy(struct yetty_ylexbor *r)
{
    /* Order of teardown:
	 *   1. Drain any pending jobs/microtasks so they fire while
	 *      our handlers and timer queue are still live.
	 *   2. Drop our timer queue (handler JSValues) — these are owned
	 *      by the dying context and would otherwise leak.
	 *   3. Reset the static DOM listener pool so the next runtime
	 *      doesn't iterate stale handlers.
	 *   4. Then free QuickJS state, finally the opaque blob.
	 *
	 * Skipping #1 made the integration runner crash inside
	 * js_closure during the pump of the *next* test — pending
	 * promise-then jobs were carrying references that got freed
	 * out from under them when JS_FreeRuntime ran the GC. */
    if (r->js_rt && r->js_ctx) {
        yetty_ylexbor_js_drain_jobs(r);
    }
    yetty_ylexbor_js_web_shutdown(r);
    yetty_ylexbor_js_dom_reset(r);
    if (r->js_rt) {
        void *opaque = JS_GetRuntimeOpaque((JSRuntime *)r->js_rt);
        free(opaque);
        JS_SetRuntimeOpaque((JSRuntime *)r->js_rt, NULL);
    }
    if (r->js_ctx) {
        JS_FreeContext((JSContext *)r->js_ctx);
    }
    if (r->js_rt) {
        JS_FreeRuntime((JSRuntime *)r->js_rt);
    }
    r->js_ctx = NULL;
    r->js_rt = NULL;
}

/* ===========================================================================
 * Run all inline <script> elements once.
 * ===========================================================================*/

/* Inlined into eval_buf — kept here as a static no-op shim so the
 * symbol exists for any future call site. */
__attribute__((unused)) static void report_exception(JSContext *ctx, const char *url)
{
    (void)ctx;
    (void)url;
}

/* Print the line of `src` containing 1-based byte offset `line_no`,
 * with a caret pointing at column `col_no` (1-based). For diagnostic
 * dumps when JS throws — easier than re-fetching source by URL. */
static void print_src_at(const char *src, size_t slen, int line_no, int col_no)
{
    int line = 1;
    const char *line_start = src;
    const char *p = src;
    const char *end = src + slen;
    while (p < end && line < line_no) {
        if (*p == '\n') {
            line++;
            line_start = p + 1;
        }
        p++;
    }
    const char *line_end = line_start;
    while (line_end < end && *line_end != '\n') {
        line_end++;
    }
    int len = (int)(line_end - line_start);
    if (len > 240) {
        len = 240;
    }
    ydebug("js source: %.*s", len, line_start);
    if (col_no > 0 && col_no <= 240) {
        char caret[244];
        int n = 0;
        for (int i = 1; i < col_no && n < 240; i++) {
            caret[n++] = '-';
        }
        caret[n++] = '^';
        caret[n] = 0;
        ydebug("js        %s", caret);
    }
}

/* Eval a UTF-8 source buffer in the global scope. `url` is the file
 * label used in stack traces. */
static void eval_buf(struct yetty_ylexbor *r, JSContext *ctx, const char *src, size_t slen,
                     const char *url)
{
    JSValue v = JS_Eval(ctx, src, slen, url ? url : "<inline>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(v)) {
        /* Pull the line+col out of the stack frame so we can show
		 * the offending source line. ydebug fires only when the
		 * trace point is enabled (default off in non-ytrace
		 * builds) so this no-ops in production. */
        {
            JSValue ex0 = JS_GetException(ctx);
            const char *m = JS_ToCString(ctx, ex0);
            ydebug("js exception %s: %s", url ? url : "<inline>", m ? m : "?");
            if (m) {
                JS_FreeCString(ctx, m);
            }
            JSValue stack = JS_GetPropertyStr(ctx, ex0, "stack");
            const char *st = JS_ToCString(ctx, stack);
            if (st) {
                ydebug("js stack:\n%s", st);
            }
            /* Parse the *first* (deepest) frame. Lines look like
			 *   "    at <anonymous> (<inline>:2:64)\n" */
            int line = 0, col = 0;
            if (st) {
                const char *p = strstr(st, "<inline>:");
                if (!p) {
                    p = strchr(st, ':');
                }
                if (p) {
                    p = strchr(p, ':');
                    if (p) {
                        line = atoi(p + 1);
                        const char *q = strchr(p + 1, ':');
                        if (q) {
                            col = atoi(q + 1);
                        }
                    }
                }
                JS_FreeCString(ctx, st);
            }
            JS_FreeValue(ctx, stack);
            JS_FreeValue(ctx, ex0);
            if (line > 0) {
                /* Show line ±1 around the failure for context. */
                if (line > 1) {
                    print_src_at(src, slen, line - 1, 0);
                }
                print_src_at(src, slen, line, col);
                print_src_at(src, slen, line + 1, 0);
            }
        }
        r->js_error_count++;
    }
    JS_FreeValue(ctx, v);
    yetty_ylexbor_js_drain_jobs(r);
}

/* Concatenate text-node children of a <script> element into a freshly
 * malloc'd buffer. NUL-terminates. *out_len is the byte length without
 * the NUL. Returns NULL on OOM or empty body. */
static char *collect_script_text(lxb_dom_node_t *script_node, size_t *out_len)
{
    char *src = NULL;
    size_t slen = 0, scap = 0;
    for (lxb_dom_node_t *t = script_node->first_child; t; t = t->next) {
        if (t->type != LXB_DOM_NODE_TYPE_TEXT) {
            continue;
        }
        lxb_dom_text_t *tn = lxb_dom_interface_text(t);
        size_t n = tn->char_data.data.length;
        const lxb_char_t *p = tn->char_data.data.data;
        if (slen + n + 1 > scap) {
            size_t nc = scap ? scap * 2 : 256;
            while (nc < slen + n + 1) {
                nc *= 2;
            }
            char *np = realloc(src, nc);
            if (!np) {
                free(src);
                return NULL;
            }
            src = np;
            scap = nc;
        }
        memcpy(src + slen, p, n);
        slen += n;
    }
    if (slen == 0) {
        free(src);
        return NULL;
    }
    src[slen] = '\0';
    if (out_len) {
        *out_len = slen;
    }
    return src;
}

/* True iff the type= attribute (if any) names a JS MIME type. Empty /
 * missing / "module" all count as JS. */
static int is_js_script_type(lxb_dom_element_t *el)
{
    size_t tlen = 0;
    const lxb_char_t *type =
        lxb_dom_element_get_attribute(el, (const lxb_char_t *)"type", 4, &tlen);
    if (!type || tlen == 0) {
        return 1;
    }
    if (tlen >= 15 && strncmp((const char *)type, "text/javascript", 15) == 0) {
        return 1;
    }
    if (tlen == 22 && strncmp((const char *)type, "application/javascript", 22) == 0) {
        return 1;
    }
    if (tlen == 6 && strncmp((const char *)type, "module", 6) == 0) {
        return 1;
    }
    return 0;
}

/* True iff `url` points at a well-known analytics / advertising / tracking
 * script. These never produce visible content, yet a synchronous inline
 * fetch+eval of one (e.g. googletagmanager's 425 KB gtag.js) blocks first
 * paint for hundreds of milliseconds. Real browsers load them async, off the
 * critical path; a viewer can skip them outright with no rendering effect. */
static int is_tracking_script_url(const char *url)
{
    static const char *const needles[] = {
        "googletagmanager.com",
        "google-analytics.com",
        "/gtag/js",
        "/gtm.js",
        "/analytics.js",
        "/ga.js",
        "doubleclick.net",
        "googlesyndication.com",
        "googleadservices.com",
        "google-analytics",
        "connect.facebook.net",
        "/fbevents.js",
        "scorecardresearch.com",
        "static.hotjar.com",
        "cdn.segment.com",
        "cdn.mxpnl.com",
        "amplitude.com/libs",
    };
    if (!url) {
        return 0;
    }
    for (size_t i = 0; i < sizeof(needles) / sizeof(needles[0]); i++) {
        if (strstr(url, needles[i]) != NULL) {
            return 1;
        }
    }
    return 0;
}

static void run_scripts_recursive(struct yetty_ylexbor *r, JSContext *ctx, lxb_dom_node_t *node)
{
    for (lxb_dom_node_t *c = node->first_child; c != NULL; c = c->next) {
        if (c->type == LXB_DOM_NODE_TYPE_ELEMENT && c->local_name == LXB_TAG_SCRIPT) {

            lxb_dom_element_t *el = lxb_dom_interface_element(c);
            if (!is_js_script_type(el)) {
                continue;
            }

            /* External script: fetch + eval. */
            size_t srclen = 0;
            const lxb_char_t *src_attr =
                lxb_dom_element_get_attribute(el, (const lxb_char_t *)"src", 3, &srclen);
            if (src_attr && srclen > 0) {
                char *href = malloc(srclen + 1);
                if (!href) {
                    continue;
                }
                memcpy(href, src_attr, srclen);
                href[srclen] = '\0';
                char *url = yetty_ylexbor_resolve_url(r, href);
                free(href);
                if (!url) {
                    continue;
                }
                if (is_tracking_script_url(url)) {
                    yetty_ylexbor_prof("    skip tracking script %.80s", url);
                    free(url);
                    continue;
                }
                size_t blen = 0;
                long status = 0;
                char *body = yetty_ylexbor_http_get(url, &blen, &status);
                if (body && status >= 200 && status < 300) {
                    eval_buf(r, ctx, body, blen, url);
                } else {
                    ydebug("js script-load %s status=%ld", url, status);
                }
                free(body);
                free(url);
                continue;
            }

            /* Inline script. */
            size_t slen = 0;
            char *src = collect_script_text(c, &slen);
            if (src) {
                eval_buf(r, ctx, src, slen, "<inline>");
                free(src);
            }
            continue; /* don't recurse into <script> children */
        }
        if (c->first_child) {
            run_scripts_recursive(r, ctx, c);
        }
    }
}

struct yetty_ycore_void_result yetty_ylexbor_js_run_inline_scripts(struct yetty_ylexbor *r)
{
    return yetty_ylexbor_js_run_all_scripts(r);
}

struct yetty_ycore_void_result yetty_ylexbor_js_run_all_scripts(struct yetty_ylexbor *r)
{
    if (r == NULL || r->document == NULL) {
        return YETTY_OK_VOID();
    }

    struct yetty_ycore_void_result ir = yetty_ylexbor_js_init(r);
    if (YETTY_IS_ERR(ir)) {
        return ir;
    }

    run_scripts_recursive(r, (JSContext *)r->js_ctx, lxb_dom_interface_node(r->document));

    /* Fire DOMContentLoaded + load on document — many SPAs gate boot
	 * on these. We synthesise both right after script execution
	 * because the parse is already complete by the time we get
	 * here. */
    yetty_ylexbor_js_dispatch_event_type(r, "DOMContentLoaded", NULL);
    yetty_ylexbor_js_dispatch_event_type(r, "load", NULL);
    yetty_ylexbor_js_drain_jobs(r);

    return YETTY_OK_VOID();
}

#else /* !YETTY_HAVE_QUICKJS — compile-out */

struct yetty_ycore_void_result yetty_ylexbor_js_init(struct yetty_ylexbor *r)
{
    (void)r;
    return YETTY_OK_VOID();
}
void yetty_ylexbor_js_destroy(struct yetty_ylexbor *r)
{
    (void)r;
}
struct yetty_ycore_void_result yetty_ylexbor_js_run_inline_scripts(struct yetty_ylexbor *r)
{
    (void)r;
    return YETTY_OK_VOID();
}
struct yetty_ycore_void_result yetty_ylexbor_js_run_all_scripts(struct yetty_ylexbor *r)
{
    (void)r;
    return YETTY_OK_VOID();
}

#endif /* YETTY_HAVE_QUICKJS */
