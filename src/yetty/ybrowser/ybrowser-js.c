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

/* ES-module hooks (defined below); installed on the runtime at init. */
static char *ybrowser_module_normalize(JSContext *ctx, const char *base, const char *name,
                                       void *opaque);
static JSModuleDef *ybrowser_module_loader(JSContext *ctx, const char *module_name, void *opaque);
static char *collect_script_text(lxb_dom_node_t *script_node, size_t *out_len);
static void importmap_clear(struct yetty_ylexbor *r);
static void module_src_clear(struct yetty_ylexbor *r);

/* Abort the running script once the current run's wall-clock deadline passes.
 * Fires periodically during bytecode execution; a no-op until a deadline is
 * armed around a script run. */
static int js_interrupt_handler(JSRuntime *rt, void *opaque)
{
    struct yetty_ylexbor *r = (struct yetty_ylexbor *)opaque;
    (void)rt;
    if (r->js_deadline_ms > 0.0 && yetty_ylexbor_prof_now_ms() > r->js_deadline_ms) {
        return 1;
    }
    return 0;
}

/* Milliseconds a single script run may execute before it is interrupted. */
static double js_script_budget_ms(void)
{
    const char *env = getenv("YBROWSER_JS_BUDGET_MS");
    if (env != NULL && env[0] != '\0') {
        int v = atoi(env);
        if (v > 0) {
            return (double)v;
        }
    }
    return 15000.0;
}

/* Milliseconds the parallel module-graph prefetch may run before giving up and
 * letting the synchronous loader fetch the rest on demand. */
static double module_prefetch_budget_ms(void)
{
    const char *env = getenv("YBROWSER_MODULE_PREFETCH_MS");
    if (env != NULL && env[0] != '\0') {
        int v = atoi(env);
        if (v > 0) {
            return (double)v;
        }
    }
    return 2500.0;
}

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

    /* ES modules: <script type="module"> and their imports resolve + fetch
	 * through the loader below (opaque = the engine, for fetch + base URL). */
    JS_SetModuleLoaderFunc(rt, ybrowser_module_normalize, ybrowser_module_loader, r);

    /* Bound how long a single script run may execute (see js_deadline_ms):
	 * heavy SPAs (github, etc.) schedule endless hydration/timer work that a
	 * one-shot render would otherwise never return from. */
    JS_SetInterruptHandler(rt, js_interrupt_handler, r);

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

    importmap_clear(r);
    free(r->importmap);
    r->importmap = NULL;
    r->importmap_cap = 0;

    module_src_clear(r);
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

/* Report the current pending exception (trace + offending source line) and
 * bump the error counter. `url`/`src` label the frame for context. */
static void report_js_exception(struct yetty_ylexbor *r, JSContext *ctx, const char *url,
                                const char *src, size_t slen)
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
    if (line > 0 && src != NULL) {
        if (line > 1) {
            print_src_at(src, slen, line - 1, 0);
        }
        print_src_at(src, slen, line, col);
        print_src_at(src, slen, line + 1, 0);
    }
    r->js_error_count++;
}

/* Eval a UTF-8 source buffer in the global scope. `url` is the file
 * label used in stack traces. */
static void eval_buf(struct yetty_ylexbor *r, JSContext *ctx, const char *src, size_t slen,
                     const char *url)
{
    JSValue v = JS_Eval(ctx, src, slen, url ? url : "<inline>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(v)) {
        report_js_exception(r, ctx, url ? url : "<inline>", src, slen);
    }
    JS_FreeValue(ctx, v);
    yetty_ylexbor_js_drain_jobs(r);
}

/* Set import.meta.url on a compiled module value (tag JS_TAG_MODULE). */
static void ybrowser_module_set_import_meta(JSContext *ctx, JSValueConst module_val,
                                            const char *url)
{
    JSModuleDef *m = JS_VALUE_GET_PTR(module_val);
    JSValue meta = JS_GetImportMeta(ctx, m);
    if (JS_IsException(meta)) {
        return;
    }
    JS_DefinePropertyValueStr(ctx, meta, "url", JS_NewString(ctx, url ? url : ""), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, meta, "main", JS_FALSE, JS_PROP_C_W_E);
    JS_FreeValue(ctx, meta);
}

/* ---- import maps (<script type="importmap">) --------------------------- */

static void importmap_clear(struct yetty_ylexbor *r)
{
    for (int i = 0; i < r->importmap_count; i++) {
        free(r->importmap[i].from);
        free(r->importmap[i].to);
    }
    r->importmap_count = 0;
}

static void importmap_add(struct yetty_ylexbor *r, const char *from, const char *to)
{
    if (r->importmap_count == r->importmap_cap) {
        int cap = r->importmap_cap ? r->importmap_cap * 2 : 8;
        struct yetty_ylexbor_importmap_entry *grown =
            realloc(r->importmap, (size_t)cap * sizeof(*grown));
        if (grown == NULL) {
            return;
        }
        r->importmap = grown;
        r->importmap_cap = cap;
    }
    char *from_copy = strdup(from);
    char *to_copy = strdup(to);
    if (from_copy == NULL || to_copy == NULL) {
        free(from_copy);
        free(to_copy);
        return;
    }
    r->importmap[r->importmap_count].from = from_copy;
    r->importmap[r->importmap_count].to = to_copy;
    r->importmap_count++;
}

char *yetty_ylexbor_js_importmap_resolve(struct yetty_ylexbor *r, const char *specifier)
{
    if (r == NULL || specifier == NULL || r->importmap_count == 0) {
        return NULL;
    }

    /* Exact specifier match wins. */
    for (int i = 0; i < r->importmap_count; i++) {
        if (strcmp(r->importmap[i].from, specifier) == 0) {
            return strdup(r->importmap[i].to);
        }
    }

    /* Trailing-slash prefix match: "lib/" maps "lib/x" -> to + "x". Longest
	 * matching prefix wins, per the import-maps spec. */
    const char *best_to = NULL;
    size_t best_len = 0;
    for (int i = 0; i < r->importmap_count; i++) {
        const char *from = r->importmap[i].from;
        size_t flen = strlen(from);
        if (flen > 0 && from[flen - 1] == '/' && strncmp(specifier, from, flen) == 0 &&
            flen > best_len) {
            best_to = r->importmap[i].to;
            best_len = flen;
        }
    }
    if (best_to != NULL) {
        const char *rest = specifier + best_len;
        size_t to_len = strlen(best_to);
        char *out = malloc(to_len + strlen(rest) + 1);
        if (out != NULL) {
            memcpy(out, best_to, to_len);
            strcpy(out + to_len, rest);
        }
        return out;
    }
    return NULL;
}

static lxb_dom_node_t *find_importmap_script(lxb_dom_node_t *node)
{
    for (lxb_dom_node_t *c = node->first_child; c != NULL; c = c->next) {
        if (c->type == LXB_DOM_NODE_TYPE_ELEMENT && c->local_name == LXB_TAG_SCRIPT) {
            size_t tlen = 0;
            const lxb_char_t *type = lxb_dom_element_get_attribute(
                lxb_dom_interface_element(c), (const lxb_char_t *)"type", 4, &tlen);
            if (type != NULL && tlen == 9 && strncmp((const char *)type, "importmap", 9) == 0) {
                return c;
            }
        }
        lxb_dom_node_t *found = find_importmap_script(c);
        if (found != NULL) {
            return found;
        }
    }
    return NULL;
}

void yetty_ylexbor_js_importmap_scan(struct yetty_ylexbor *r)
{
    importmap_clear(r);
    if (r == NULL || r->document == NULL) {
        return;
    }

    /* Only the first import map in the document is honoured (per spec). */
    lxb_dom_node_t *node = find_importmap_script(lxb_dom_interface_node(r->document));
    if (node == NULL) {
        return;
    }

    size_t len = 0;
    char *json = collect_script_text(node, &len);
    if (json == NULL) {
        return;
    }
    if (YETTY_IS_ERR(yetty_ylexbor_js_init(r))) {
        free(json);
        return;
    }

    JSContext *ctx = (JSContext *)r->js_ctx;
    JSValue root = JS_ParseJSON(ctx, json, len, "<importmap>");
    free(json);
    if (JS_IsException(root)) {
        JS_FreeValue(ctx, JS_GetException(ctx));
        JS_FreeValue(ctx, root);
        return;
    }

    JSValue imports = JS_GetPropertyStr(ctx, root, "imports");
    if (JS_IsObject(imports)) {
        JSPropertyEnum *tab = NULL;
        uint32_t count = 0;
        if (JS_GetOwnPropertyNames(ctx, &tab, &count, imports,
                                   JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) == 0) {
            for (uint32_t i = 0; i < count; i++) {
                const char *key = JS_AtomToCString(ctx, tab[i].atom);
                JSValue val = JS_GetProperty(ctx, imports, tab[i].atom);
                const char *sval = JS_ToCString(ctx, val);
                if (key != NULL && sval != NULL) {
                    importmap_add(r, key, sval);
                }
                if (key != NULL) {
                    JS_FreeCString(ctx, key);
                }
                if (sval != NULL) {
                    JS_FreeCString(ctx, sval);
                }
                JS_FreeValue(ctx, val);
            }
            JS_FreePropertyEnum(ctx, tab, count);
        }
    }
    JS_FreeValue(ctx, imports);
    JS_FreeValue(ctx, root);
}

/* ---- prefetched module-source cache ------------------------------------ */

static void module_src_store(struct yetty_ylexbor *r, const char *url, const char *body,
                             size_t len)
{
    if (url == NULL || body == NULL) {
        return;
    }
    for (int i = 0; i < r->module_src_count; i++) {
        if (strcmp(r->module_srcs[i].url, url) == 0) {
            return; /* already cached */
        }
    }
    if (r->module_src_count == r->module_src_cap) {
        int cap = r->module_src_cap ? r->module_src_cap * 2 : 16;
        struct yetty_ylexbor_module_source *grown =
            realloc(r->module_srcs, (size_t)cap * sizeof(*grown));
        if (grown == NULL) {
            return;
        }
        r->module_srcs = grown;
        r->module_src_cap = cap;
    }
    char *url_copy = strdup(url);
    char *body_copy = malloc(len + 1);
    if (url_copy == NULL || body_copy == NULL) {
        free(url_copy);
        free(body_copy);
        return;
    }
    memcpy(body_copy, body, len);
    body_copy[len] = '\0';
    r->module_srcs[r->module_src_count].url = url_copy;
    r->module_srcs[r->module_src_count].body = body_copy;
    r->module_srcs[r->module_src_count].len = len;
    r->module_src_count++;
}

static const char *module_src_lookup(struct yetty_ylexbor *r, const char *url, size_t *len)
{
    for (int i = 0; i < r->module_src_count; i++) {
        if (strcmp(r->module_srcs[i].url, url) == 0) {
            *len = r->module_srcs[i].len;
            return r->module_srcs[i].body;
        }
    }
    return NULL;
}

static void module_src_clear(struct yetty_ylexbor *r)
{
    for (int i = 0; i < r->module_src_count; i++) {
        free(r->module_srcs[i].url);
        free(r->module_srcs[i].body);
    }
    free(r->module_srcs);
    r->module_srcs = NULL;
    r->module_src_count = 0;
    r->module_src_cap = 0;
}

/* Resolve an ES-module import specifier against the importing module's URL
 * (`base`); QuickJS dedupes modules by this canonical name. Bare specifiers go
 * through the page import map. Returns a js_malloc'd string owned by QuickJS. */
static char *ybrowser_module_normalize(JSContext *ctx, const char *base, const char *name,
                                       void *opaque)
{
    struct yetty_ylexbor *r = (struct yetty_ylexbor *)opaque;
    char *mapped = yetty_ylexbor_js_importmap_resolve(r, name);
    const char *spec = mapped ? mapped : name;
    char *absolute = NULL;

    if (strncmp(spec, "http://", 7) == 0 || strncmp(spec, "https://", 8) == 0) {
        absolute = strdup(spec);
    } else {
        const char *anchor = (base != NULL && base[0] != '\0') ? base : r->base_url;
        absolute = yetty_ylexbor_resolve_url_against(anchor, spec);
    }
    free(mapped);
    if (absolute == NULL) {
        return NULL;
    }

    size_t len = strlen(absolute);
    char *out = js_malloc(ctx, len + 1);
    if (out != NULL) {
        memcpy(out, absolute, len + 1);
    }
    free(absolute);
    return out;
}

/* QuickJS module loader: synchronously fetch `module_name` (an absolute URL
 * from the normalizer), compile it as a module, stamp import.meta, and return
 * the module definition. Dependencies load recursively via the same hook. */
static JSModuleDef *ybrowser_module_loader(JSContext *ctx, const char *module_name, void *opaque)
{
    struct yetty_ylexbor *r = (struct yetty_ylexbor *)opaque;

    /* Module resolution compiles the whole import graph synchronously, which
	 * the execution interrupt handler can't reach. Honour the run deadline
	 * here too so a huge graph (github: 75+ chunks) can't blow the budget. */
    if (r->js_deadline_ms > 0.0 && yetty_ylexbor_prof_now_ms() > r->js_deadline_ms) {
        JS_ThrowInternalError(ctx, "module load budget exceeded");
        return NULL;
    }

    /* Prefer the parallel-prefetched source (see prefetch_module_graph) so the
	 * common case is a memory hit rather than a serial network round-trip. */
    size_t src_len = 0;
    const char *src = module_src_lookup(r, module_name, &src_len);
    struct yetty_ybrowser_response response = {0};
    bool used_response = false;

    if (src == NULL) {
        struct yetty_ybrowser_request request = {
            .url = module_name,
            .kind = YETTY_YBROWSER_REQUEST_SCRIPT,
            .referer = r->base_url,
        };
        struct yetty_ycore_void_result fetch_res =
            yetty_ybrowser_fetch(r->loader, &request, &response);
        if (YETTY_IS_ERR(fetch_res)) {
            yetty_ycore_error_destroy(fetch_res.error);
        }
        if (response.body == NULL || response.status < 200 || response.status >= 300) {
            yetty_ybrowser_response_dispose(&response);
            JS_ThrowReferenceError(ctx, "could not load module '%s'", module_name);
            return NULL;
        }
        src = response.body;
        src_len = response.body_len;
        used_response = true;
    }

    JSValue module_val =
        JS_Eval(ctx, src, src_len, module_name, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    if (used_response) {
        yetty_ybrowser_response_dispose(&response);
    }
    if (JS_IsException(module_val)) {
        return NULL;
    }

    ybrowser_module_set_import_meta(ctx, module_val, module_name);
    JSModuleDef *m = JS_VALUE_GET_PTR(module_val);
    JS_FreeValue(ctx, module_val); /* retained by the loader graph */
    return m;
}

/* Eval a top-level `<script type="module">` body: compile as a module (imports
 * resolve+fetch through the loader), stamp import.meta, evaluate. Module eval is
 * async (returns a promise); drain the queue and surface a rejection so a broken
 * module reports like a classic-script error. */
static void eval_module(struct yetty_ylexbor *r, JSContext *ctx, const char *src, size_t slen,
                        const char *url)
{
    const char *name =
        (url != NULL && url[0] != '\0') ? url : (r->base_url ? r->base_url : "<inline>");
    JSValue module_val =
        JS_Eval(ctx, src, slen, name, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(module_val)) {
        report_js_exception(r, ctx, name, src, slen);
        return;
    }

    ybrowser_module_set_import_meta(ctx, module_val, name);

    JSValue result = JS_EvalFunction(ctx, module_val); /* consumes module_val */
    if (JS_IsException(result)) {
        report_js_exception(r, ctx, name, src, slen);
        JS_FreeValue(ctx, result);
        yetty_ylexbor_js_drain_jobs(r);
        return;
    }

    yetty_ylexbor_js_drain_jobs(r);

    if (JS_PromiseState(ctx, result) == JS_PROMISE_REJECTED) {
        JSValue reason = JS_PromiseResult(ctx, result);
        JS_Throw(ctx, reason);
        report_js_exception(r, ctx, name, src, slen);
    }
    JS_FreeValue(ctx, result);
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
    bool is_module;             /* <script type="module"> — evaluate as an ES module */
    lxb_dom_node_t *element;    /* the <script> element (for document.currentScript) */
};

/* True iff the <script> is type="module" (evaluated as an ES module). */
static int is_module_script_type(lxb_dom_element_t *el)
{
    size_t tlen = 0;
    const lxb_char_t *type =
        lxb_dom_element_get_attribute(el, (const lxb_char_t *)"type", 4, &tlen);
    return type != NULL && tlen == 6 && strncmp((const char *)type, "module", 6) == 0;
}

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
                struct script_entry entry = {
                    .url = url, .is_module = is_module_script_type(el), .element = c};
                script_collect_push(collect, entry);
                continue;
            }
            size_t slen = 0;
            char *inline_src = collect_script_text(c, &slen);
            if (inline_src) {
                struct script_entry entry = {.inline_body = inline_src,
                                             .inline_len = slen,
                                             .is_module = is_module_script_type(el),
                                             .element = c};
                script_collect_push(collect, entry);
            }
            continue; /* don't recurse into <script> children */
        }
        if (c->first_child) {
            collect_scripts_recursive(r, c, collect);
        }
    }
}

/* Collect the hrefs of every <link rel="modulepreload"> in document order. */
static void collect_modulepreload_recursive(struct yetty_ylexbor *r, lxb_dom_node_t *node,
                                            struct script_collect *collect)
{
    for (lxb_dom_node_t *c = node->first_child; c != NULL; c = c->next) {
        if (c->type == LXB_DOM_NODE_TYPE_ELEMENT && c->local_name == LXB_TAG_LINK) {
            lxb_dom_element_t *el = lxb_dom_interface_element(c);
            size_t rlen = 0;
            const lxb_char_t *rel =
                lxb_dom_element_get_attribute(el, (const lxb_char_t *)"rel", 3, &rlen);
            if (rel != NULL && rlen == 13 && strncmp((const char *)rel, "modulepreload", 13) == 0) {
                size_t hlen = 0;
                const lxb_char_t *href =
                    lxb_dom_element_get_attribute(el, (const lxb_char_t *)"href", 4, &hlen);
                if (href != NULL && hlen > 0) {
                    char *raw = malloc(hlen + 1);
                    if (raw != NULL) {
                        memcpy(raw, href, hlen);
                        raw[hlen] = '\0';
                        char *url = yetty_ylexbor_resolve_url(r, raw);
                        free(raw);
                        if (url != NULL) {
                            struct script_entry entry = {.url = url};
                            script_collect_push(collect, entry);
                        }
                    }
                }
            }
        }
        if (c->first_child) {
            collect_modulepreload_recursive(r, c, collect);
        }
    }
}

/* Warm the HTTP cache with the ES-module dependency graph declared via
 * <link rel="modulepreload"> in ONE parallel batch. The synchronous module
 * loader then hits cache instead of a serial network round-trip per import —
 * turning a page like github.com (75 preloaded chunks) from dozens of blocking
 * fetches into one multiplexed batch. */
/* A growable, de-duplicated URL work-list for the module-graph BFS. */
struct url_queue {
    char **urls; /* owned */
    int count, cap;
};

static bool url_queue_has(const struct url_queue *queue, const char *url)
{
    for (int i = 0; i < queue->count; i++) {
        if (strcmp(queue->urls[i], url) == 0) {
            return true;
        }
    }
    return false;
}

static void url_queue_push(struct url_queue *queue, char *url /* transferred */)
{
    if (queue->count == queue->cap) {
        int cap = queue->cap ? queue->cap * 2 : 32;
        char **grown = realloc(queue->urls, (size_t)cap * sizeof(*grown));
        if (grown == NULL) {
            free(url);
            return;
        }
        queue->urls = grown;
        queue->cap = cap;
    }
    queue->urls[queue->count++] = url;
}

static void url_queue_free(struct url_queue *queue)
{
    for (int i = 0; i < queue->count; i++) {
        free(queue->urls[i]);
    }
    free(queue->urls);
    queue->urls = NULL;
    queue->count = queue->cap = 0;
}

/* Scan a module's source for static import specifiers (`import"x"`, `from"x"`,
 * `import("x")`) and enqueue each resolved, not-yet-cached URL. Heuristic but
 * matches minified ES modules: requires a non-identifier char before the
 * keyword so `transform`/`important` don't false-match. */
static void scan_module_imports(struct yetty_ylexbor *r, const char *base_url, const char *src,
                                size_t len, struct url_queue *next)
{
    for (size_t i = 0; i < len; i++) {
        size_t kwlen = 0;
        if (i + 4 <= len && memcmp(src + i, "from", 4) == 0) {
            kwlen = 4;
        } else if (i + 6 <= len && memcmp(src + i, "import", 6) == 0) {
            kwlen = 6;
        } else {
            continue;
        }
        if (i > 0) {
            char prev = src[i - 1];
            if ((prev >= 'a' && prev <= 'z') || (prev >= 'A' && prev <= 'Z') ||
                (prev >= '0' && prev <= '9') || prev == '_' || prev == '$' || prev == '.') {
                continue;
            }
        }
        size_t j = i + kwlen;
        while (j < len && (src[j] == ' ' || src[j] == '\t' || src[j] == '\n' || src[j] == '\r')) {
            j++;
        }
        if (j < len && src[j] == '(') {
            j++;
            while (j < len && (src[j] == ' ' || src[j] == '\t')) {
                j++;
            }
        }
        if (j >= len || (src[j] != '"' && src[j] != '\'')) {
            continue;
        }
        char quote = src[j++];
        size_t start = j;
        while (j < len && src[j] != quote) {
            j++;
        }
        if (j >= len) {
            break;
        }
        size_t spec_len = j - start;
        i = j; /* advance past the string literal */
        if (spec_len == 0 || spec_len > 1024) {
            continue;
        }

        char *spec = strndup(src + start, spec_len);
        if (spec == NULL) {
            continue;
        }
        char *absolute = NULL;
        if (strncmp(spec, "http://", 7) == 0 || strncmp(spec, "https://", 8) == 0) {
            absolute = strdup(spec);
        } else if (spec[0] == '.' || spec[0] == '/') {
            absolute = yetty_ylexbor_resolve_url_against(base_url, spec);
        }
        /* Bare specifiers (import maps) are rare here and resolved at load. */
        free(spec);
        if (absolute == NULL) {
            continue;
        }
        size_t cached_len = 0;
        if (module_src_lookup(r, absolute, &cached_len) != NULL || url_queue_has(next, absolute)) {
            free(absolute);
            continue;
        }
        url_queue_push(next, absolute);
    }
}

/* Warm the module cache with the WHOLE import graph in parallel BFS waves,
 * seeded by <link rel="modulepreload"> and <script type="module" src>. Without
 * this the synchronous module loader fetches each of a code-split SPA's chunks
 * one at a time (nytimes: 378 chunks, ~3.5s serial). Bounded by wave/total caps
 * so a pathological graph can't run away. */
static void prefetch_module_graph(struct yetty_ylexbor *r, lxb_dom_node_t *node)
{
    enum { MAX_MODULES = 1200, MAX_WAVES = 20, MAX_WAVE = 256 };

    struct script_collect seeds = {0};
    collect_modulepreload_recursive(r, node, &seeds);
    /* Also seed top-level <script type="module" src> entry points. */
    {
        struct script_collect scripts = {0};
        collect_scripts_recursive(r, node, &scripts);
        for (int i = 0; i < scripts.count; i++) {
            if (scripts.items[i].is_module && scripts.items[i].url != NULL) {
                struct script_entry entry = {.url = strdup(scripts.items[i].url)};
                if (entry.url != NULL) {
                    script_collect_push(&seeds, entry);
                }
            }
            free(scripts.items[i].url);
            free(scripts.items[i].inline_body);
        }
        free(scripts.items);
    }
    if (seeds.count == 0) {
        free(seeds.items);
        return;
    }

    struct url_queue current = {0};
    for (int i = 0; i < seeds.count; i++) {
        if (seeds.items[i].url != NULL && !url_queue_has(&current, seeds.items[i].url)) {
            url_queue_push(&current, seeds.items[i].url); /* transfers */
        } else {
            free(seeds.items[i].url);
        }
        free(seeds.items[i].inline_body);
    }
    free(seeds.items);

    /* Wall-clock cap: this prefetch runs BEFORE the JS execution budget arms,
	 * so it must bound itself. It pays off when static-import scanning catches
	 * the graph (nytimes: 378 chunks -> 0 serial); when a site hides chunks
	 * behind webpack dynamic import() by id (github), scanning misses them and
	 * the prefetch is pure overhead — stop early rather than fetch a huge graph
	 * speculatively. The synchronous loader then fetches the rest on demand. */
    double prefetch_deadline = yetty_ylexbor_prof_now_ms() + module_prefetch_budget_ms();

    int fetched_total = 0;
    for (int wave = 0; wave < MAX_WAVES && current.count > 0 && fetched_total < MAX_MODULES &&
                       yetty_ylexbor_prof_now_ms() < prefetch_deadline;
         wave++) {
        int n = current.count;
        if (n > MAX_WAVE) {
            n = MAX_WAVE;
        }
        struct yetty_ybrowser_request *requests = calloc((size_t)n, sizeof(*requests));
        struct yetty_ybrowser_response *responses = calloc((size_t)n, sizeof(*responses));
        struct url_queue next = {0};
        if (requests != NULL && responses != NULL) {
            for (int i = 0; i < n; i++) {
                requests[i].url = current.urls[i];
                requests[i].kind = YETTY_YBROWSER_REQUEST_SCRIPT;
                requests[i].referer = r->base_url;
            }
            struct yetty_ycore_void_result res = yetty_ybrowser_fetch_many(
                r->loader, requests, n, responses, /*host_connection_cap=*/8);
            if (YETTY_IS_ERR(res)) {
                yetty_ycore_error_destroy(res.error);
            }
            for (int i = 0; i < n; i++) {
                if (responses[i].body != NULL && responses[i].status >= 200 &&
                    responses[i].status < 300) {
                    module_src_store(r, requests[i].url, responses[i].body, responses[i].body_len);
                    fetched_total++;
                    if (fetched_total < MAX_MODULES) {
                        scan_module_imports(r, requests[i].url, responses[i].body,
                                            responses[i].body_len, &next);
                    }
                }
                yetty_ybrowser_response_dispose(&responses[i]);
            }
        }
        free(requests);
        free(responses);
        /* Drop the URLs consumed this wave; carry the leftover + newly found. */
        for (int i = 0; i < n; i++) {
            free(current.urls[i]);
        }
        for (int i = n; i < current.count; i++) {
            url_queue_push(&next, current.urls[i]); /* transfers leftover */
        }
        free(current.urls);
        current = next;
    }
    url_queue_free(&current);
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

    /* Build the import map before any module executes so bare specifiers in
	 * `import` statements resolve. Cheap no-op when the page has none. */
    yetty_ylexbor_js_importmap_scan(r);

    /* Warm the cache with the module dependency graph in parallel so the
	 * synchronous module loader below doesn't serialize network fetches. */
    prefetch_module_graph(r, node);

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

    /* Bound total execution: an SPA's scripts never idle, so cap the run and
	 * render whatever DOM state exists at the deadline. */
    r->js_deadline_ms = yetty_ylexbor_prof_now_ms() + js_script_budget_ms();

    for (int i = 0; i < collect.count; i++) {
        struct script_entry *entry = &collect.items[i];
        if (entry->url) {
            struct yetty_ybrowser_response *response =
                entry_to_slot ? &fetch_responses[entry_to_slot[i]] : NULL;
            if (response && response->body && response->status >= 200 && response->status < 300) {
                if (entry->is_module) {
                    eval_module(r, ctx, response->body, response->body_len, entry->url);
                } else {
                    /* Classic scripts see document.currentScript; modules do not. */
                    yetty_ylexbor_js_set_current_script(r, entry->element);
                    eval_buf(r, ctx, response->body, response->body_len, entry->url);
                    yetty_ylexbor_js_set_current_script(r, NULL);
                }
            } else {
                ydebug("js script-load %s status=%ld", entry->url,
                       response ? response->status : 0L);
            }
            if (response) {
                yetty_ybrowser_response_dispose(response);
            }
            free(entry->url);
        } else {
            if (entry->is_module) {
                eval_module(r, ctx, entry->inline_body, entry->inline_len,
                            r->base_url ? r->base_url : "<inline>");
            } else {
                yetty_ylexbor_js_set_current_script(r, entry->element);
                eval_buf(r, ctx, entry->inline_body, entry->inline_len, "<inline>");
                yetty_ylexbor_js_set_current_script(r, NULL);
            }
            free(entry->inline_body);
        }
    }

    r->js_deadline_ms = 0.0; /* disarm outside the script run */
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
