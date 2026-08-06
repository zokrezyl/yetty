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

/* ===========================================================================
 * DevTools console ring — plain storage, compiled regardless of QuickJS so the
 * accessors resolve even in builds without a JS engine (they just stay empty).
 * ===========================================================================*/

void yetty_ylexbor_console_push(struct yetty_ylexbor *r, int level, const char *text)
{
    if (!r || !text) {
        return;
    }
    if (!r->console_ring) {
        r->console_ring = calloc(YETTY_YLEXBOR_CONSOLE_CAP, sizeof(*r->console_ring));
        if (!r->console_ring) {
            return;
        }
    }
    struct yetty_ylexbor_console_entry *slot = &r->console_ring[r->console_head];
    free(slot->text); /* overwrite the oldest line once the ring wraps */
    slot->text = strdup(text);
    if (!slot->text) {
        return;
    }
    slot->level = level;
    r->console_head = (r->console_head + 1) % YETTY_YLEXBOR_CONSOLE_CAP;
    if (r->console_count < YETTY_YLEXBOR_CONSOLE_CAP) {
        r->console_count++;
    }
    r->console_total++;
}

uint64_t yetty_ylexbor_console_total(const struct yetty_ylexbor *r)
{
    return r ? r->console_total : 0;
}

int yetty_ylexbor_console_count(const struct yetty_ylexbor *r)
{
    return r ? r->console_count : 0;
}

struct yetty_ylexbor_console_view yetty_ylexbor_console_at(const struct yetty_ylexbor *r, int index)
{
    struct yetty_ylexbor_console_view view = {YETTY_YLEXBOR_CONSOLE_LOG, NULL};
    if (!r || !r->console_ring || index < 0 || index >= r->console_count) {
        return view;
    }
    /* index 0 = oldest retained. The oldest slot sits `console_count` steps
     * behind the write head; +CAP keeps the modulo non-negative (count <= CAP). */
    int slot = (r->console_head - r->console_count + index + YETTY_YLEXBOR_CONSOLE_CAP) %
               YETTY_YLEXBOR_CONSOLE_CAP;
    view.level = r->console_ring[slot].level;
    view.text = r->console_ring[slot].text;
    return view;
}

void yetty_ylexbor_console_clear(struct yetty_ylexbor *r)
{
    if (!r || !r->console_ring) {
        return;
    }
    for (int i = 0; i < YETTY_YLEXBOR_CONSOLE_CAP; i++) {
        free(r->console_ring[i].text);
        r->console_ring[i].text = NULL;
    }
    r->console_head = 0;
    r->console_count = 0;
}

#if YETTY_HAVE_QUICKJS

#include <quickjs.h>
#include <quickjs-jit.h>
#include <lexbor/dom/dom.h>
#include <lexbor/html/html.h>
#include <lexbor/tag/const.h>
#include <unistd.h>

#include <yetty/ytrace/ytrace.h>

/* ===========================================================================
 * console.* binding — one C function per level.
 * ===========================================================================*/

/* Page console.* goes through ytrace (off by default; visible with
 * YTRACE_DEFAULT_ON). Real sites log copiously and many partially fail on
 * this minimal JS surface — spewing that straight to stderr would clutter
 * the operator's terminal in standalone, and corrupt the OSC stream when
 * the engine runs under `yetty -e` (stderr → PTY slave). */
static void console_append(char *buf, size_t cap, size_t *off, const char *s)
{
    if (!s || *off >= cap - 1) {
        return;
    }
    size_t room = cap - 1 - *off;
    size_t sl = strlen(s);
    size_t cp = sl < room ? sl : room;
    memcpy(buf + *off, s, cp);
    *off += cp;
}

static void console_print(JSContext *ctx, const char *level, int argc, JSValueConst *argv)
{
    char buf[4096];
    size_t off = 0;
    for (int i = 0; i < argc && off < sizeof(buf) - 1; i++) {
        const char *s = JS_ToCString(ctx, argv[i]);
        if (!s) {
            continue;
        }
        if (i > 0) {
            console_append(buf, sizeof(buf), &off, " ");
        }
        console_append(buf, sizeof(buf), &off, s);
        JS_FreeCString(ctx, s);
        /* For Error-like args, append the stack so a caught exception logged
		 * by a framework (e.g. MediaWiki ResourceLoader) reveals where it
		 * actually threw, not just its message. */
        if (JS_IsObject(argv[i])) {
            JSValue stk = JS_GetPropertyStr(ctx, argv[i], "stack");
            if (JS_IsString(stk)) {
                const char *ss = JS_ToCString(ctx, stk);
                if (ss) {
                    console_append(buf, sizeof(buf), &off, " | STACK ");
                    console_append(buf, sizeof(buf), &off, ss);
                    JS_FreeCString(ctx, ss);
                }
            }
            JS_FreeValue(ctx, stk);
        }
    }
    buf[off] = '\0';
    /* Record into the DevTools console ring so the UI can show page output.
     * The engine is the runtime opaque, set once the DOM bindings install —
     * always true by the time page scripts run console.*. */
    {
        struct yetty_ylexbor *engine = yetty_ylexbor_js_engine_from_ctx(ctx);
        if (engine) {
            int lvl = YETTY_YLEXBOR_CONSOLE_LOG;
            if (strcmp(level, "error") == 0) {
                lvl = YETTY_YLEXBOR_CONSOLE_ERROR;
            } else if (strcmp(level, "warn") == 0) {
                lvl = YETTY_YLEXBOR_CONSOLE_WARN;
            } else if (strcmp(level, "info") == 0) {
                lvl = YETTY_YLEXBOR_CONSOLE_INFO;
            } else if (strcmp(level, "debug") == 0) {
                lvl = YETTY_YLEXBOR_CONSOLE_DEBUG;
            }
            yetty_ylexbor_console_push(engine, lvl, buf);
        }
    }
    ydebug("[js:%s] %s", level, buf);
    /* Also mirror to stderr when YBROWSER_JS_CONSOLE is set — the ybrowser
     * tool enables this in its standalone (own-window) mode so page JS
     * errors are visible for debugging, while leaving it off under
     * `yetty -e` (where stderr → the PTY would corrupt the OSC stream).
     * getenv-per-call on purpose: console output is not hot, and caching
     * the answer would need a static variable. */
    if (getenv("YBROWSER_JS_CONSOLE") != NULL) {
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

static void install_console(JSContext *ctx)
{
    static const JSCFunctionListEntry console_funcs[] = {
        JS_CFUNC_DEF("log", 1, js_console_log),     JS_CFUNC_DEF("info", 1, js_console_info),
        JS_CFUNC_DEF("debug", 1, js_console_debug), JS_CFUNC_DEF("warn", 1, js_console_warn),
        JS_CFUNC_DEF("error", 1, js_console_error),
    };
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

    /* Baseline JIT (see src/quickjs/quickjs-jit.h). ON by default
	 * (baseline: compile hot functions). YBROWSER_JS_JIT=eager compiles
	 * every compilable function on first call; =off|0 disables.
	 * Compiled in only on the Linux x86-64 target; a no-op elsewhere. */
    {
        const char *jit_env = getenv("YBROWSER_JS_JIT");
        int jit_mode = JS_JIT_MODE_BASELINE; /* default on */
        if (jit_env != NULL && jit_env[0] != '\0') {
            if (strcmp(jit_env, "eager") == 0) {
                jit_mode = JS_JIT_MODE_EAGER;
            } else if (strcmp(jit_env, "off") == 0 || strcmp(jit_env, "0") == 0) {
                jit_mode = JS_JIT_MODE_OFF;
            }
        }
        (void)JS_JITSetMode(rt, jit_mode);
    }

    /* Stage 0 JS profiler (see src/quickjs/quickjs-jit.h). Gated on
	 * env; one profile per thread, so iframe child runtimes fail the
	 * start silently and only the top document is sampled. */
    {
        const char *profile_env = getenv("YBROWSER_JS_PROFILE");
        if (profile_env == NULL) {
            profile_env = getenv("YBROWSER_PROFILE");
        }
        if (profile_env != NULL && profile_env[0] != '\0' && strcmp(profile_env, "0") != 0) {
            int sample_hz = 1000;
            const char *hz_env = getenv("YBROWSER_JS_PROFILE_HZ");
            if (hz_env != NULL && atoi(hz_env) > 0) {
                sample_hz = atoi(hz_env);
            }
            (void)JS_ProfileStart(rt, sample_hz);
        }
    }

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
    /* Stage 0 JS profile dump. The profile is thread-wide: iframe
	 * child runtimes contribute samples too, so EVERY runtime dumps
	 * its own function rows at teardown (rows are drained on dump, so
	 * nothing double-counts). The owning runtime additionally stops
	 * the sampler; child runtimes dump to "<base>.<runtime>" side
	 * files that the analyzer merges. Dump while the context can
	 * still stringify atoms. */
    if (r->js_rt && r->js_ctx && JS_ProfileThreadActive()) {
        const char *dump_path = getenv("YBROWSER_JS_PROFILE_OUT");
        char default_path[64];
        char child_path[160];
        bool owner = JS_ProfileStop((JSRuntime *)r->js_rt) == 0;
        if (dump_path == NULL || dump_path[0] == '\0') {
            snprintf(default_path, sizeof(default_path), "tmp/js-profile-%d.tsv", (int)getpid());
            dump_path = default_path;
        }
        if (!owner) {
            snprintf(child_path, sizeof(child_path), "%s.%p", dump_path, (void *)r->js_rt);
            dump_path = child_path;
        }
        (void)JS_ProfileDump((JSContext *)r->js_ctx, dump_path);
    }
    yetty_ylexbor_js_web_shutdown(r);
    yetty_ylexbor_js_dom_reset(r);
    /* Queued dynamic scripts hold element pointers into the dying document —
	 * drop them (urls are owned; elements are the document's). */
    for (int i = 0; i < r->pending_script_count; i++) {
        free(r->pending_scripts[i].url);
    }
    free(r->pending_scripts);
    r->pending_scripts = NULL;
    r->pending_script_count = 0;
    r->pending_script_cap = 0;
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

/* One <script> to execute, in document order. EITHER `url` is set (an
 * external script — after the parallel-fetch step the matching response
 * carries its body) OR `inline_body` is set. Execution order is the
 * collected order in both cases — only the FETCHING is parallel. */
struct script_entry {
    char *url;         /* owned; external script when non-NULL */
    char *inline_body; /* owned when url == NULL */
    size_t inline_len;
};

struct script_collect {
    struct script_entry *items;
    int count, cap;
};

static void script_collect_push(struct script_collect *collect, struct script_entry entry)
{
    if (collect->count == collect->cap) {
        int new_cap = collect->cap ? collect->cap * 2 : 8;
        struct script_entry *items = realloc(collect->items, (size_t)new_cap * sizeof(*items));
        if (!items) {
            free(entry.url);
            free(entry.inline_body);
            return;
        }
        collect->items = items;
        collect->cap = new_cap;
    }
    collect->items[collect->count++] = entry;
}

/* DOM walk — append every runnable <script> (external or inline) to the
 * collect list in document order. */
static void collect_scripts_recursive(struct yetty_ylexbor *r, lxb_dom_node_t *node,
                                      struct script_collect *collect)
{
    for (lxb_dom_node_t *c = node->first_child; c != NULL; c = c->next) {
        if (c->type == LXB_DOM_NODE_TYPE_ELEMENT && c->local_name == LXB_TAG_SCRIPT) {
            lxb_dom_element_t *el = lxb_dom_interface_element(c);
            if (!is_js_script_type(el)) {
                continue;
            }
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
                struct script_entry entry = {.url = url};
                script_collect_push(collect, entry);
                continue;
            }
            size_t slen = 0;
            char *inline_src = collect_script_text(c, &slen);
            if (inline_src) {
                struct script_entry entry = {.inline_body = inline_src, .inline_len = slen};
                script_collect_push(collect, entry);
            }
            continue; /* don't recurse into <script> children */
        }
        if (c->first_child) {
            collect_scripts_recursive(r, c, collect);
        }
    }
}

/* Two-phase script run — same shape as the stylesheet loader:
 *   1) DOM walk → collect external + inline scripts in document order.
 *   2) Parallel-fetch every external URL (one HTTP/2-multiplexed batch
 *      instead of one blocking round-trip per script).
 *   3) Evaluate in collected order, so execution semantics match the
 *      sequential fetch exactly. */
static void run_collected_scripts(struct yetty_ylexbor *r, JSContext *ctx, lxb_dom_node_t *node)
{
    struct script_collect collect = {0};
    collect_scripts_recursive(r, node, &collect);
    if (collect.count == 0) {
        return;
    }

    int external_count = 0;
    for (int i = 0; i < collect.count; i++) {
        if (collect.items[i].url) {
            external_count++;
        }
    }
    struct yetty_ybrowser_request *fetch_requests = NULL;
    struct yetty_ybrowser_response *fetch_responses = NULL;
    int *entry_to_slot = NULL; /* collect index → response slot, -1 for inline */
    if (external_count > 0) {
        fetch_requests = calloc((size_t)external_count, sizeof(*fetch_requests));
        fetch_responses = calloc((size_t)external_count, sizeof(*fetch_responses));
        entry_to_slot = calloc((size_t)collect.count, sizeof(*entry_to_slot));
        if (fetch_requests && fetch_responses && entry_to_slot) {
            int slot = 0;
            for (int i = 0; i < collect.count; i++) {
                if (collect.items[i].url) {
                    fetch_requests[slot].url = collect.items[i].url;
                    fetch_requests[slot].kind = YETTY_YBROWSER_REQUEST_SCRIPT;
                    fetch_requests[slot].referer = r->base_url;
                    entry_to_slot[i] = slot;
                    slot++;
                } else {
                    entry_to_slot[i] = -1;
                }
            }
            struct yetty_ycore_void_result many_res =
                yetty_ybrowser_fetch_many(r->loader, fetch_requests, external_count,
                                          fetch_responses, /*host_connection_cap=*/8);
            if (YETTY_IS_ERR(many_res)) {
                yetty_ycore_error_destroy(many_res.error);
            }
        } else {
            free(fetch_requests);
            free(fetch_responses);
            free(entry_to_slot);
            fetch_requests = NULL;
            fetch_responses = NULL;
            entry_to_slot = NULL;
        }
    }

    for (int i = 0; i < collect.count; i++) {
        struct script_entry *entry = &collect.items[i];
        if (entry->url) {
            struct yetty_ybrowser_response *response =
                entry_to_slot ? &fetch_responses[entry_to_slot[i]] : NULL;
            if (response && response->body && response->status >= 200 && response->status < 300) {
                eval_buf(r, ctx, response->body, response->body_len, entry->url);
            } else {
                ydebug("js script-load %s status=%ld", entry->url,
                       response ? response->status : 0L);
            }
            if (response) {
                yetty_ybrowser_response_dispose(response);
            }
            free(entry->url);
        } else {
            eval_buf(r, ctx, entry->inline_body, entry->inline_len, "<inline>");
            free(entry->inline_body);
        }
    }
    free(fetch_requests);
    free(fetch_responses);
    free(entry_to_slot);
    free(collect.items);
}

/* ===========================================================================
 * Dynamically-inserted <script> execution.
 *
 * The DOM insertion paths (appendChild & friends, ybrowser-js-dom.c) call
 * queue_script for every <script> element that lands connected in the
 * document. Per spec a dynamic INLINE script runs synchronously at
 * insertion; a dynamic EXTERNAL script loads without blocking the inserting
 * script. Delivery mirrors the fetch()/image split: with a worker pool the
 * fetch runs as an async pool job (js_script_job, ybrowser-js-web.c); the
 * pool-less hosts (one-shot render, in-yetty client) queue on
 * r->pending_scripts and the pump executes a small batch per tick. This is
 * the script-loader pattern every tag manager, SPA chunk loader and
 * challenge page uses; without it a createElement('script') site renders an
 * empty shell.
 * ===========================================================================*/
void yetty_ylexbor_js_eval_script_body(struct yetty_ylexbor *r, const char *body, size_t body_len,
                                       const char *url)
{
    if (r == NULL || r->js_ctx == NULL || body == NULL) {
        return;
    }
    yetty_ylexbor_js_update_stack_top(r);
    eval_buf(r, (JSContext *)r->js_ctx, body, body_len, url);
}
void yetty_ylexbor_js_queue_script(struct yetty_ylexbor *r, lxb_dom_element_t *element)
{
    if (r == NULL || element == NULL || r->js_ctx == NULL) {
        return;
    }
    if (!is_js_script_type(element)) {
        return;
    }
    size_t srclen = 0;
    const lxb_char_t *src_attr =
        lxb_dom_element_get_attribute(element, (const lxb_char_t *)"src", 3, &srclen);
    if (src_attr && srclen > 0) {
        char *href = malloc(srclen + 1);
        if (!href) {
            return;
        }
        memcpy(href, src_attr, srclen);
        href[srclen] = '\0';
        char *url = yetty_ylexbor_resolve_url(r, href);
        free(href);
        if (!url) {
            return;
        }
        if (is_tracking_script_url(url)) {
            ydebug("dynamic script skip tracking %.120s", url);
            free(url);
            return;
        }
        /* Pool available (standalone browser): async worker-pool job, same
		 * lifecycle as fetch() — nothing blocks, delivery is generation-
		 * guarded. Ownership of url moves to the job on success. */
        ydebug("dynamic script queued %.140s", url);
        if (yetty_ylexbor_js_submit_script_job(r, element, url)) {
            return;
        }
        /* No pool (one-shot render, in-yetty client): queue for the pump's
		 * per-tick batch. The same element re-inserted (moved) must not run
		 * twice. */
        for (int i = 0; i < r->pending_script_count; i++) {
            if (r->pending_scripts[i].element == element) {
                free(url);
                return;
            }
        }
        if (r->pending_script_count == r->pending_script_cap) {
            int new_cap = r->pending_script_cap ? r->pending_script_cap * 2 : 4;
            struct yetty_ylexbor_pending_script *grown =
                realloc(r->pending_scripts, (size_t)new_cap * sizeof(*grown));
            if (!grown) {
                free(url);
                return;
            }
            r->pending_scripts = grown;
            r->pending_script_cap = new_cap;
        }
        r->pending_scripts[r->pending_script_count].element = element;
        r->pending_scripts[r->pending_script_count].url = url;
        r->pending_script_count++;
        return;
    }
    size_t slen = 0;
    char *inline_src = collect_script_text(lxb_dom_interface_node(element), &slen);
    if (inline_src) {
        ydebug("dynamic inline script %zu bytes", slen);
        eval_buf(r, (JSContext *)r->js_ctx, inline_src, slen, "<dynamic>");
        free(inline_src);
    }
}

int yetty_ylexbor_js_run_pending_scripts(struct yetty_ylexbor *r)
{
    if (r == NULL || r->js_ctx == NULL || r->pending_script_count == 0) {
        return 0;
    }
    if (r->loader == NULL) {
        /* String-loaded document with no network — drop the queue. */
        for (int i = 0; i < r->pending_script_count; i++) {
            free(r->pending_scripts[i].url);
        }
        r->pending_script_count = 0;
        return 0;
    }
    yetty_ylexbor_js_update_stack_top(r);
    /* Pool-less degraded mode, same shape as the client image path: ONE
	 * small multiplexed batch per pump tick. Scripts an executed batch
	 * enqueues run next tick — the pump reports pending work so the host
	 * keeps ticking. */
    enum { SCRIPT_BATCH_MAX = 4 };
    int batch_count = r->pending_script_count;
    if (batch_count > SCRIPT_BATCH_MAX) {
        batch_count = SCRIPT_BATCH_MAX;
    }
    struct yetty_ylexbor_pending_script batch[SCRIPT_BATCH_MAX];
    memcpy(batch, r->pending_scripts, (size_t)batch_count * sizeof(*batch));
    r->pending_script_count -= batch_count;
    memmove(r->pending_scripts, r->pending_scripts + batch_count,
            (size_t)r->pending_script_count * sizeof(*batch));

    struct yetty_ybrowser_request requests[SCRIPT_BATCH_MAX] = {0};
    struct yetty_ybrowser_response responses[SCRIPT_BATCH_MAX] = {0};
    for (int i = 0; i < batch_count; i++) {
        requests[i].url = batch[i].url;
        requests[i].kind = YETTY_YBROWSER_REQUEST_SCRIPT;
        requests[i].referer = r->base_url;
    }
    struct yetty_ycore_void_result many_res = yetty_ybrowser_fetch_many(
        r->loader, requests, batch_count, responses, /*host_connection_cap=*/8);
    if (YETTY_IS_ERR(many_res)) {
        yetty_ycore_error_destroy(many_res.error);
    }
    int executed = 0;
    for (int i = 0; i < batch_count; i++) {
        struct yetty_ybrowser_response *response = &responses[i];
        if (response->body && response->status >= 200 && response->status < 300) {
            eval_buf(r, (JSContext *)r->js_ctx, response->body, response->body_len, batch[i].url);
            executed++;
            yetty_ylexbor_js_fire_element_event(r, batch[i].element, "load");
        } else {
            ydebug("dynamic script %.140s status=%ld", batch[i].url, response->status);
            yetty_ylexbor_js_fire_element_event(r, batch[i].element, "error");
        }
        yetty_ybrowser_response_dispose(response);
        free(batch[i].url);
    }
    return executed;
}

/* Update document.readyState (plain data property on the document object). */
static void js_doc_set_ready_state(struct yetty_ylexbor *r, const char *ready_state)
{
    JSContext *ctx = (JSContext *)r->js_ctx;
    if (ctx == NULL) {
        return;
    }
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue doc = JS_GetPropertyStr(ctx, global, "document");
    if (JS_IsObject(doc)) {
        JS_SetPropertyStr(ctx, doc, "readyState", JS_NewString(ctx, ready_state));
    }
    JS_FreeValue(ctx, doc);
    JS_FreeValue(ctx, global);
}

void yetty_ylexbor_js_update_stack_top(struct yetty_ylexbor *r)
{
    if (r != NULL && r->js_rt != NULL) {
        JS_UpdateStackTop((JSRuntime *)r->js_rt);
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
    yetty_ylexbor_js_update_stack_top(r);

    /* Investigation prelude (YBROWSER_PRELUDE=<file>): evaluate a JS file in the
     * page context after the environment is initialised but before any page
     * script runs. Lets an investigator install probes (e.g. an event-timeline
     * logger) that observe the very first boot navigation. Env-gated; no effect
     * unless the variable points at a readable file. */
    {
        const char *prelude_path = getenv("YBROWSER_PRELUDE");
        if (prelude_path != NULL) {
            FILE *prelude_file = fopen(prelude_path, "rb");
            if (prelude_file != NULL) {
                fseek(prelude_file, 0, SEEK_END);
                long prelude_len = ftell(prelude_file);
                fseek(prelude_file, 0, SEEK_SET);
                if (prelude_len > 0) {
                    char *prelude_src = calloc(1, (size_t)prelude_len + 1);
                    if (prelude_src != NULL &&
                        fread(prelude_src, 1, (size_t)prelude_len, prelude_file) > 0) {
                        JSContext *prelude_ctx = (JSContext *)r->js_ctx;
                        JSValue prelude_val = JS_Eval(prelude_ctx, prelude_src, strlen(prelude_src),
                                                      "<prelude>", JS_EVAL_TYPE_GLOBAL);
                        if (JS_IsException(prelude_val)) {
                            JSValue exc = JS_GetException(prelude_ctx);
                            const char *exc_s = JS_ToCString(prelude_ctx, exc);
                            fprintf(stderr, "prelude: ERR %s\n", exc_s ? exc_s : "(unknown)");
                            JS_FreeCString(prelude_ctx, exc_s);
                            JS_FreeValue(prelude_ctx, exc);
                        }
                        JS_FreeValue(prelude_ctx, prelude_val);
                    }
                    free(prelude_src);
                }
                fclose(prelude_file);
            }
        }
    }

    run_collected_scripts(r, (JSContext *)r->js_ctx, lxb_dom_interface_node(r->document));

    /* Document readiness sequence, matching browser order:
	 *   readyState=interactive → readystatechange → DOMContentLoaded →
	 *   readyState=complete → readystatechange → load.
	 * readyState is "loading" for the whole script pass above, so a
	 * framework that defers a boot walk to readystatechange (rather than
	 * running it against the half-built page) gets the same timing it
	 * gets in a browser. */
    js_doc_set_ready_state(r, "interactive");
    yetty_ylexbor_js_dispatch_event_type(r, "readystatechange", NULL);
    yetty_ylexbor_js_dispatch_event_type(r, "DOMContentLoaded", NULL);
    js_doc_set_ready_state(r, "complete");
    yetty_ylexbor_js_dispatch_event_type(r, "readystatechange", NULL);
    yetty_ylexbor_js_dispatch_event_type(r, "load", NULL);
    yetty_ylexbor_js_drain_jobs(r);

    return YETTY_OK_VOID();
}

/* ===========================================================================
 * DevTools REPL — evaluate a typed expression in the page's JS context.
 * ===========================================================================*/

/* Render a successful eval result the way a browser console does: undefined as
 * "undefined", strings quoted, objects/arrays via JSON.stringify (falling back
 * to toString), everything else via its default string form. Returns an owned
 * string, or NULL on OOM. */
static char *eval_stringify(JSContext *ctx, JSValueConst value)
{
    if (JS_IsUndefined(value)) {
        return strdup("undefined");
    }
    if (JS_IsString(value)) {
        const char *raw = JS_ToCString(ctx, value);
        if (!raw) {
            return NULL;
        }
        size_t len = strlen(raw);
        char *out = malloc(len + 3);
        if (out) {
            out[0] = '"';
            memcpy(out + 1, raw, len);
            out[len + 1] = '"';
            out[len + 2] = '\0';
        }
        JS_FreeCString(ctx, raw);
        return out;
    }
    if (JS_IsObject(value) && !JS_IsFunction(ctx, value)) {
        JSValue global = JS_GetGlobalObject(ctx);
        JSValue json = JS_GetPropertyStr(ctx, global, "JSON");
        JSValue stringify = JS_GetPropertyStr(ctx, json, "stringify");
        char *out = NULL;
        if (JS_IsFunction(ctx, stringify)) {
            JSValueConst argv[1] = {value};
            JSValue res = JS_Call(ctx, stringify, json, 1, argv);
            if (!JS_IsException(res) && JS_IsString(res)) {
                const char *raw = JS_ToCString(ctx, res);
                if (raw) {
                    out = strdup(raw);
                    JS_FreeCString(ctx, raw);
                }
            }
            JS_FreeValue(ctx, res);
        }
        JS_FreeValue(ctx, stringify);
        JS_FreeValue(ctx, json);
        JS_FreeValue(ctx, global);
        if (out) {
            return out; /* e.g. {"a":1} or [1,2,3] */
        }
        /* JSON.stringify returns undefined for cyclic/function-only objects —
         * fall through to the plain string form. */
    }
    const char *raw = JS_ToCString(ctx, value);
    if (!raw) {
        return NULL;
    }
    char *out = strdup(raw);
    JS_FreeCString(ctx, raw);
    return out;
}

/* Pull the pending exception and format it as "Uncaught <message>", appending
 * the first stack frame when present. Returns an owned string, or NULL on OOM. */
static char *eval_format_exception(JSContext *ctx)
{
    JSValue exception = JS_GetException(ctx);
    const char *message = JS_ToCString(ctx, exception);
    JSValue stack = JS_GetPropertyStr(ctx, exception, "stack");
    const char *stack_text = JS_IsString(stack) ? JS_ToCString(ctx, stack) : NULL;

    size_t need = strlen("Uncaught ") + (message ? strlen(message) : 1) +
                  (stack_text ? strlen(stack_text) + 1 : 0) + 1;
    char *out = malloc(need);
    if (out) {
        int off = snprintf(out, need, "Uncaught %s", message ? message : "?");
        if (stack_text && stack_text[0] && off >= 0 && (size_t)off < need) {
            snprintf(out + off, need - (size_t)off, "\n%s", stack_text);
        }
    }
    if (message) {
        JS_FreeCString(ctx, message);
    }
    if (stack_text) {
        JS_FreeCString(ctx, stack_text);
    }
    JS_FreeValue(ctx, stack);
    JS_FreeValue(ctx, exception);
    return out;
}

struct yetty_ycore_char_ptr_result yetty_ylexbor_eval_js(struct yetty_ylexbor *r, const char *src)
{
    if (!r || !src) {
        return YETTY_ERR(yetty_ycore_char_ptr, "eval_js: NULL argument");
    }
    struct yetty_ycore_void_result init_res = yetty_ylexbor_js_init(r);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, init_res, "eval_js: JS init");
    yetty_ylexbor_js_update_stack_top(r);

    JSContext *ctx = (JSContext *)r->js_ctx;
    yetty_ylexbor_console_push(r, YETTY_YLEXBOR_CONSOLE_INPUT, src);

    JSValue value = JS_Eval(ctx, src, strlen(src), "<console>", JS_EVAL_TYPE_GLOBAL);
    int level = YETTY_YLEXBOR_CONSOLE_RESULT;
    char *display = NULL;
    if (JS_IsException(value)) {
        level = YETTY_YLEXBOR_CONSOLE_ERROR;
        display = eval_format_exception(ctx);
        r->js_error_count++;
    } else {
        display = eval_stringify(ctx, value);
    }
    JS_FreeValue(ctx, value);
    /* Timers / promise jobs the expression scheduled run now, so their console
     * output lands before we hand control back to the UI. */
    yetty_ylexbor_js_drain_jobs(r);

    if (!display) {
        return YETTY_ERR(yetty_ycore_char_ptr, "eval_js: out of memory formatting result");
    }
    yetty_ylexbor_console_push(r, level, display);
    return YETTY_OK(yetty_ycore_char_ptr, display);
}

#else /* !YETTY_HAVE_QUICKJS — compile-out */

void yetty_ylexbor_js_update_stack_top(struct yetty_ylexbor *r)
{
    (void)r;
}
void yetty_ylexbor_js_queue_script(struct yetty_ylexbor *r, lxb_dom_element_t *element)
{
    (void)r;
    (void)element;
}
int yetty_ylexbor_js_run_pending_scripts(struct yetty_ylexbor *r)
{
    (void)r;
    return 0;
}
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
struct yetty_ycore_char_ptr_result yetty_ylexbor_eval_js(struct yetty_ylexbor *r, const char *src)
{
    if (!r || !src) {
        return YETTY_ERR(yetty_ycore_char_ptr, "eval_js: NULL argument");
    }
    yetty_ylexbor_console_push(r, YETTY_YLEXBOR_CONSOLE_INPUT, src);
    const char *unavailable = "JavaScript is not available in this build";
    yetty_ylexbor_console_push(r, YETTY_YLEXBOR_CONSOLE_ERROR, unavailable);
    char *copy = strdup(unavailable);
    if (!copy) {
        return YETTY_ERR(yetty_ycore_char_ptr, "eval_js: out of memory");
    }
    return YETTY_OK(yetty_ycore_char_ptr, copy);
}

#endif /* YETTY_HAVE_QUICKJS */
