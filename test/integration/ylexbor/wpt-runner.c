/*
 * wpt-runner — drive a Web Platform Tests "script test" through ylexbor
 * and emit the per-assertion results as JSON-Lines.
 *
 * Modes:
 *
 *   --html <path|url>      Run one test page; print results, exit 0
 *                          if every test PASSed, 1 otherwise.
 *
 *   --suite <dir>          Walk <dir> recursively, run every *.html
 *                          file (lexicographic order), aggregate.
 *
 *   --harness <path>       Override the path to testharness.js.
 *
 *   --boot-budget-ms <n>   How long to drain timers / pending jobs
 *                          before reading results. Default 2000.
 *
 *   --json                 Default output format (one JSON object per
 *                          assertion, one summary at the end).
 *
 *   --tap                  Emit TAP version 13 instead of JSON-Lines
 *                          (for ctest / prove / WPT runner glue).
 *
 * Exit code is the count of FAILed tests, capped at 127.
 *
 * Wire-up:
 *
 * Each test page is read; we splice
 *
 *     <script>__INJECTED_TESTHARNESS__</script>
 *
 * just after <head>. The injected blob is the contents of
 * `testharness.js` from this directory. After load_html + boot pump,
 * we read window.__ylexbor_test_results__ back through QuickJS.
 */

#include <yetty/ylexbor/ylexbor.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/ycore/types.h>

#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* Internal entry point so we can pull results out of QuickJS without
 * adding a dedicated "list test results" public API. */
extern void *yetty_ylexbor_internal_get_js_ctx(struct yetty_ylexbor *r);

/* ===========================================================================
 * I/O helpers
 * ===========================================================================*/

static char *slurp(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "wpt-runner: open %s: %s\n", path, strerror(errno));
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) {
        fclose(f);
        return NULL;
    }
    char *buf = malloc((size_t)n + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t r = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[r] = '\0';
    if (out_len) {
        *out_len = r;
    }
    return buf;
}

/* Splice the testharness.js source as an inline <script> right after
 * <head> (or at the start of the document if no <head>). The result is
 * heap-malloc'd; caller frees. */
static char *splice_harness(const char *html, size_t hlen, const char *harness, size_t hslen,
                            size_t *out_len)
{
    const char *p = strcasestr(html, "<head>");
    size_t insert_after = p ? (size_t)(p - html) + 6 : 0;

    const char *prefix = "<script>";
    const char *suffix = "</script>";
    size_t plen = strlen(prefix), slen = strlen(suffix);

    size_t total = hlen + plen + hslen + slen;
    char *out = malloc(total + 1);
    if (!out) {
        return NULL;
    }
    size_t off = 0;
    memcpy(out + off, html, insert_after);
    off += insert_after;
    memcpy(out + off, prefix, plen);
    off += plen;
    memcpy(out + off, harness, hslen);
    off += hslen;
    memcpy(out + off, suffix, slen);
    off += slen;
    memcpy(out + off, html + insert_after, hlen - insert_after);
    off += hlen - insert_after;
    out[off] = '\0';
    if (out_len) {
        *out_len = off;
    }
    return out;
}

/* ===========================================================================
 * Result extraction — the harness publishes
 *   globalThis.__ylexbor_test_results__ = [ {name, status, message}, ... ]
 *
 * We pull it back through a dedicated `JSON.stringify` evaluation, so
 * we don't need the QuickJS headers in this TU.
 * ===========================================================================*/

#include <quickjs.h>

static char *read_results_json(struct yetty_ylexbor *r)
{
    JSContext *ctx = (JSContext *)yetty_ylexbor_internal_get_js_ctx(r);
    if (!ctx) {
        return NULL;
    }
    /* Finalize the harness suite (no-op if already fired), then
	 * stringify the results. We do this in one Eval to avoid
	 * exception leakage between calls. */
    const char *expr = "(function(){"
                       "  if (typeof __ylexbor_finalize_tests__ === 'function') {"
                       "    try { __ylexbor_finalize_tests__(); } catch(_) {}"
                       "  }"
                       "  return JSON.stringify(globalThis.__ylexbor_test_results__ || []);"
                       "})()";
    JSValue v = JS_Eval(ctx, expr, strlen(expr), "<wpt-runner-readback>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(v)) {
        JSValue ex = JS_GetException(ctx);
        JS_FreeValue(ctx, ex);
        JS_FreeValue(ctx, v);
        return NULL;
    }
    const char *s = JS_ToCString(ctx, v);
    char *out = s ? strdup(s) : NULL;
    if (s) {
        JS_FreeCString(ctx, s);
    }
    JS_FreeValue(ctx, v);
    return out;
}

/* ===========================================================================
 * JSON-Lines parsing — we just need to emit the entries verbatim. The
 * input is `[{...},{...},...]`; walk it splitting at top-level commas.
 *
 * Counts pass/fail in *out_pass / *out_fail. Returns 0 on success.
 * ===========================================================================*/

struct counts {
    int pass, fail, timeout, notrun, total;
};

static int dispatch_results(const char *json, int tap, struct counts *cnt, int tap_offset,
                            const char *test_path)
{
    if (!json || json[0] != '[') {
        return -1;
    }
    const char *p = json + 1, *end = json + strlen(json);
    if (p < end && *p == ']') {
        return 0;
    }

    while (p < end) {
        while (p < end && (*p == ' ' || *p == ',' || *p == '\n')) {
            p++;
        }
        if (p >= end || *p != '{') {
            break;
        }
        const char *obj_start = p;
        int depth = 1;
        p++;
        char quote = 0;
        while (p < end && depth > 0) {
            char c = *p;
            if (quote) {
                if (c == '\\' && p + 1 < end) {
                    p++;
                } else if (c == quote) {
                    quote = 0;
                }
            } else if (c == '"' || c == '\'') {
                quote = c;
            } else if (c == '{') {
                depth++;
            } else if (c == '}') {
                depth--;
            }
            p++;
        }
        size_t olen = (size_t)(p - obj_start);

        /* Pull the status field — we control the producer so a
		 * literal substring search is good enough. */
        char status[16] = {0};
        const char *sp = strstr(obj_start, "\"status\":\"");
        if (sp && sp < p) {
            sp += 10;
            size_t i = 0;
            while (sp < p && *sp != '"' && i + 1 < sizeof(status)) {
                status[i++] = *sp++;
            }
            status[i] = '\0';
        }
        if (strcmp(status, "PASS") == 0) {
            cnt->pass++;
        } else if (strcmp(status, "FAIL") == 0) {
            cnt->fail++;
        } else if (strcmp(status, "TIMEOUT") == 0) {
            cnt->timeout++;
        } else {
            cnt->notrun++;
        }
        cnt->total++;

        if (tap) {
            /* TAP test name → unwrap the JSON "name". */
            char name[256] = "?";
            const char *np = strstr(obj_start, "\"name\":\"");
            if (np && np < p) {
                np += 8;
                size_t i = 0;
                while (np < p && *np != '"' && i + 1 < sizeof(name)) {
                    if (*np == '\\' && np + 1 < p) {
                        name[i++] = np[1];
                        np += 2;
                    } else {
                        name[i++] = *np++;
                    }
                }
                name[i] = '\0';
            }
            int ok = strcmp(status, "PASS") == 0;
            fprintf(stdout, "%s %d - %s%s%s\n", ok ? "ok" : "not ok", cnt->total + tap_offset, name,
                    test_path ? " # source=" : "", test_path ? test_path : "");
        } else {
            /* JSON-Lines: dump the entry verbatim, prefixed
			 * by the source path so a multi-test run is
			 * grep-able. */
            fprintf(stdout, "{\"src\":\"%s\",\"r\":", test_path ? test_path : "<inline>");
            fwrite(obj_start, 1, olen, stdout);
            fputs("}\n", stdout);
        }
    }
    return 0;
}

/* ===========================================================================
 * One test
 * ===========================================================================*/

static int run_one(const char *html_path, const char *harness, size_t harness_len,
                   int boot_budget_ms, int tap, struct counts *cnt, int tap_offset)
{
    size_t hlen = 0;
    char *html = slurp(html_path, &hlen);
    if (!html) {
        fprintf(stderr, "wpt-runner: skip %s\n", html_path);
        return -1;
    }

    size_t splen = 0;
    char *spliced = splice_harness(html, hlen, harness, harness_len, &splen);
    free(html);
    if (!spliced) {
        return -1;
    }

    struct yetty_ylexbor_config cfg = {
        .viewport_width = 1024,
        .viewport_height = 768,
        .default_font_size = 16.0f,
    };
    struct yetty_ylexbor_ptr_result rr = yetty_ylexbor_create(&cfg);
    if (YETTY_IS_ERR(rr)) {
        fprintf(stderr, "wpt-runner: create: %s\n", rr.error.msg);
        free(spliced);
        return -1;
    }
    struct yetty_ylexbor *r = rr.value;

    /* Treat <file://path/test.html> so external <script src>= loaders
	 * don't fight a missing base URL. */
    char base[1024];
    snprintf(base, sizeof(base), "file://%s", html_path);
    (void)yetty_ylexbor_set_base_url(r, base);

    struct yetty_ycore_void_result lr = yetty_ylexbor_load_html(r, spliced, splen);
    if (YETTY_IS_ERR(lr)) {
        fprintf(stderr, "wpt-runner: load_html %s: %s\n", html_path, lr.error.msg);
        yetty_ylexbor_destroy(r);
        free(spliced);
        return -1;
    }

    /* Pump timers / promise jobs until the budget is exhausted, so
	 * promise_test() etc. land their final state. */
    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    while (boot_budget_ms > 0) {
        int wait_ms = yetty_ylexbor_pump_timers(r);
        if (wait_ms < 0) {
            break;
        }
        if (wait_ms > 25) {
            wait_ms = 25;
        }
        struct timespec ts = {
            .tv_sec = wait_ms / 1000,
            .tv_nsec = (wait_ms % 1000) * 1000000L,
        };
        nanosleep(&ts, NULL);
        struct timespec t1;
        clock_gettime(CLOCK_MONOTONIC, &t1);
        long elapsed = (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000;
        if (elapsed >= boot_budget_ms) {
            break;
        }
    }

    char *json = read_results_json(r);
    if (json) {
        (void)dispatch_results(json, tap, cnt, tap_offset, html_path);
        free(json);
    } else {
        fprintf(stderr, "wpt-runner: no results from %s\n", html_path);
        cnt->fail++;
        cnt->total++;
        if (tap) {
            fprintf(stdout, "not ok %d - %s # NO RESULTS\n", cnt->total + tap_offset, html_path);
        }
    }

    yetty_ylexbor_destroy(r);
    free(spliced);
    return 0;
}

/* ===========================================================================
 * Suite walker — recursive, lexicographic.
 *
 * Each test runs in its OWN process. ylexbor + QuickJS hold a fair
 * amount of static state (class IDs, listener pool, libcurl + lexbor
 * internal pools), and clearing all of it between runs in-process is
 * fragile — we hit SIGSEGVs in `js_closure` after the third or fourth
 * test as JSValues from prior runtimes survived a leaky path. Forking
 * is what every real browser test runner does (Chromium content_shell,
 * WebKit DumpRenderTree) for the same reason.
 *
 * The child re-execs `argv[0] --html <file>` (with --tap or --json)
 * and pipes its stdout straight through our parent process's stdout
 * (TAP numbering is patched up by the parent). On crash, the child's
 * non-zero exit becomes a synthetic "not ok" line.
 * ===========================================================================*/

static int has_html_suffix(const char *name)
{
    size_t n = strlen(name);
    return n >= 5 && strcmp(name + n - 5, ".html") == 0;
}

static int strcmp_qsort(const void *a, const void *b)
{
    return strcmp(*(const char **)a, *(const char **)b);
}

/* Fork+exec the same binary in single-test mode and pipe the child's
 * stdout into our `tap` / `json` aggregator. Returns 0 on child exit 0,
 * non-zero if the child failed or crashed. */
static int run_one_in_subprocess(const char *self_path, const char *harness_path,
                                 const char *test_path, int boot_budget_ms, int tap,
                                 struct counts *cnt)
{
    int pipe_fd[2];
    if (pipe(pipe_fd) < 0) {
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        return -1;
    }
    if (pid == 0) {
        /* Child — redirect stdout into the pipe. */
        close(pipe_fd[0]);
        dup2(pipe_fd[1], STDOUT_FILENO);
        close(pipe_fd[1]);
        char budget_buf[16];
        snprintf(budget_buf, sizeof(budget_buf), "%d", boot_budget_ms);
        char *args[] = {
            (char *)self_path,
            tap ? "--tap" : "--json",
            "--harness",
            (char *)harness_path,
            "--boot-budget-ms",
            budget_buf,
            "--html",
            (char *)test_path,
            NULL,
        };
        execv(self_path, args);
        fprintf(stderr, "wpt-runner: execv: %s\n", strerror(errno));
        _exit(127);
    }

    close(pipe_fd[1]);
    FILE *fp = fdopen(pipe_fd[0], "r");
    if (!fp) {
        close(pipe_fd[0]);
        return -1;
    }

    int local_pass = 0, local_fail = 0;
    char *line = NULL;
    size_t lcap = 0;
    while (1) {
        ssize_t got = getline(&line, &lcap, fp);
        if (got <= 0) {
            break;
        }
        /* Child emits its own `TAP version 13` and `1..N` summary
		 * for the single-test run. Skip those — the parent owns
		 * the suite header / footer. */
        if (tap) {
            if (strncmp(line, "TAP version", 11) == 0) {
                continue;
            }
            if (strncmp(line, "1..", 3) == 0) {
                continue;
            }
            /* Re-number: the child says "ok 1 - X" or
			 * "not ok 1 - X", we want "<tag> <cnt+1> - X". */
            const char *tag = NULL;
            const char *after = NULL;
            int is_pass = 0;
            if (strncmp(line, "ok ", 3) == 0) {
                tag = "ok";
                after = line + 3;
                is_pass = 1;
            } else if (strncmp(line, "not ok ", 7) == 0) {
                tag = "not ok";
                after = line + 7;
                is_pass = 0;
            }
            if (tag) {
                /* Skip the child-side number. */
                while (*after >= '0' && *after <= '9') {
                    after++;
                }
                while (*after == ' ') {
                    after++;
                }
                /* `after` now points at "- name # ...\n". */
                size_t alen = strlen(after);
                while (alen > 0 && (after[alen - 1] == '\n' || after[alen - 1] == '\r')) {
                    alen--;
                }
                if (is_pass) {
                    cnt->pass++;
                    local_pass++;
                } else {
                    cnt->fail++;
                    local_fail++;
                }
                cnt->total++;
                fprintf(stdout, "%s %d %.*s\n", tag, cnt->total, (int)alen, after);
                continue;
            }
        } else {
            /* JSON mode: pass through entries that are NOT
			 * the summary, count the summary. */
            if (strstr(line, "\"summary\":true")) {
                continue;
            }
            fputs(line, stdout);
            if (strstr(line, "\"status\":\"PASS\"")) {
                cnt->pass++;
                local_pass++;
                cnt->total++;
            } else if (strstr(line, "\"status\":\"FAIL\"")) {
                cnt->fail++;
                local_fail++;
                cnt->total++;
            }
        }
    }
    free(line);
    fclose(fp);

    int status;
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) > 127) {
        /* Child crashed — synthesize a single FAIL so the suite
		 * reports the failure rather than silently truncating. */
        cnt->fail++;
        cnt->total++;
        if (tap) {
            fprintf(stdout, "not ok %d - %s # CRASH (signal %d)\n", cnt->total, test_path,
                    WIFSIGNALED(status) ? WTERMSIG(status) : -1);
        } else {
            fprintf(stdout,
                    "{\"src\":\"%s\",\"r\":{\"status\":\"FAIL\","
                    "\"message\":\"crash\"}}\n",
                    test_path);
        }
        return -1;
    }
    if (local_pass + local_fail == 0) {
        /* Child exited cleanly but emitted no test entries — the
		 * page either threw before any test() call or our shim
		 * never observed any. Synthesize a fail so silent
		 * coverage gaps show up in the suite tally. */
        cnt->fail++;
        cnt->total++;
        if (tap) {
            fprintf(stdout, "not ok %d - %s # NO TESTS RAN\n", cnt->total, test_path);
        } else {
            fprintf(stdout,
                    "{\"src\":\"%s\",\"r\":{\"status\":\"FAIL\","
                    "\"message\":\"no tests ran\"}}\n",
                    test_path);
        }
        return -1;
    }
    return 0;
}

static const char *g_self_path;
static const char *g_harness_path;

static int walk_suite(const char *dir, const char *harness, size_t harness_len, int boot_budget_ms,
                      int tap, struct counts *cnt)
{
    (void)harness;
    (void)harness_len;
    DIR *d = opendir(dir);
    if (!d) {
        fprintf(stderr, "wpt-runner: opendir %s: %s\n", dir, strerror(errno));
        return -1;
    }
    char **names = NULL;
    size_t n = 0, cap = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') {
            continue;
        }
        if (n == cap) {
            cap = cap ? cap * 2 : 16;
            names = realloc(names, cap * sizeof(*names));
            if (!names) {
                closedir(d);
                return -1;
            }
        }
        names[n++] = strdup(e->d_name);
    }
    closedir(d);
    qsort(names, n, sizeof(*names), strcmp_qsort);
    for (size_t i = 0; i < n; i++) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
        struct stat st;
        if (stat(path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                walk_suite(path, harness, harness_len, boot_budget_ms, tap, cnt);
            } else if (has_html_suffix(names[i])) {
                run_one_in_subprocess(g_self_path, g_harness_path, path, boot_budget_ms, tap, cnt);
            }
        }
        free(names[i]);
    }
    free(names);
    return 0;
}

/* ===========================================================================
 * main
 * ===========================================================================*/

static void usage(const char *argv0)
{
    fprintf(stderr,
            "usage: %s [--html <file>|--suite <dir>]\n"
            "         [--harness <path>] [--boot-budget-ms <n>]\n"
            "         [--json|--tap]\n",
            argv0);
}

int main(int argc, char **argv)
{
    /* When stdout is redirected to a file (suite mode > results.tap),
	 * libc switches it to block buffering — TAP lines stack up in
	 * memory and don't reach disk until the buffer fills. The
	 * suite-progress monitor sees an empty output file even after
	 * dozens of tests run. Force line buffering so each `ok N` lands
	 * immediately. */
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);
    g_self_path = argv[0];
    const char *html_path = NULL;
    const char *suite_path = NULL;
    const char *harness_path =
#ifdef YLEXBOR_TEST_HARNESS_JS
        YLEXBOR_TEST_HARNESS_JS;
#else
        "./testharness.js";
#endif
    int boot_budget_ms = 2000;
    int tap = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--html") && i + 1 < argc) {
            html_path = argv[++i];
        } else if (!strcmp(a, "--suite") && i + 1 < argc) {
            suite_path = argv[++i];
        } else if (!strcmp(a, "--harness") && i + 1 < argc) {
            harness_path = argv[++i];
        } else if (!strcmp(a, "--boot-budget-ms") && i + 1 < argc) {
            boot_budget_ms = atoi(argv[++i]);
        } else if (!strcmp(a, "--tap")) {
            tap = 1;
        } else if (!strcmp(a, "--json")) {
            tap = 0;
        } else if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 2;
        }
    }
    if (!html_path && !suite_path) {
        usage(argv[0]);
        return 2;
    }

    g_harness_path = harness_path;

    size_t harness_len = 0;
    char *harness = slurp(harness_path, &harness_len);
    if (!harness) {
        fprintf(stderr, "wpt-runner: cannot read harness %s\n", harness_path);
        return 2;
    }

    struct counts cnt = {0};
    if (tap) {
        fputs("TAP version 13\n", stdout);
    }
    if (html_path) {
        (void)run_one(html_path, harness, harness_len, boot_budget_ms, tap, &cnt, 0);
    }
    if (suite_path) {
        (void)walk_suite(suite_path, harness, harness_len, boot_budget_ms, tap, &cnt);
    }
    if (tap) {
        fprintf(stdout, "1..%d\n", cnt.total);
    } else {
        fprintf(stdout,
                "{\"summary\":true,\"total\":%d,\"pass\":%d,\"fail\":%d,"
                "\"timeout\":%d,\"notrun\":%d}\n",
                cnt.total, cnt.pass, cnt.fail, cnt.timeout, cnt.notrun);
    }
    free(harness);
    return cnt.fail > 127 ? 127 : cnt.fail;
}
