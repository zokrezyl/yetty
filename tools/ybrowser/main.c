/*
 * ybrowser — render HTML via the yetty_ybrowser engine (lexbor + libcss +
 * QuickJS), with two operating modes:
 *
 *   ONE-SHOT (--once, or non-TTY stdout):
 *     read HTML, lay out, emit one ydraw OSC envelope to stdout, exit.
 *     Used to pipe HTML into yetty (legacy OSC mode):
 *         echo '<h1>hi</h1>' | ./ybrowser --once
 *
 *   INTERACTIVE (default when stdin + stdout are TTYs):
 *     a Google-Chrome-like UI built on the ygui widget framework — a
 *     tabbar, a toolbar (back / forward / reload + address bar) and a
 *     scrollable page area. Implemented in browser-ui.c; this file only
 *     parses args, gates the two modes, and owns the one-shot path plus
 *     the HTML-acquisition helpers shared with the UI.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <yetty/ybrowser/ybrowser.h>
#include <yetty/ycore/types.h>
#include <yetty/ycore/terminal-detect.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/yface/yface.h>
#include <yetty/yterminal/dcs-codes.h>
#include <yetty/ytrace/ytrace.h>

#include <curl/curl.h>

#include "browser-ui.h"

/* ===========================================================================
 * Input acquisition — file path, stdin, http(s):// URL.
 * ===========================================================================*/

int ybrowser_looks_like_url(const char *s)
{
    return strncmp(s, "http://", 7) == 0 || strncmp(s, "https://", 8) == 0;
}

/* Out-param: caller-owned char* gets filled with the post-redirect URL
 * (e.g. http://wikipedia.org/wiki/Photography → https://en.wikipedia.org/
 * wiki/Photography). NULL passes through. The caller frees with free().
 * Used to set base_url to the effective URL so that image src= references
 * resolve against the HTTPS origin — that pulls them over HTTP/2
 * multiplexing instead of HTTP/1.1 with 30+ TCP handshakes.
 *
 * Goes through the shared fetch layer (kind = DOCUMENT), so the page
 * request carries browser-shape navigation headers, the transfer obeys
 * the same size/speed guards as every other fetch, and the TLS
 * connection it opens stays warm in the loader for the CSS/image
 * fetches that follow. */
static char *slurp_url(struct yetty_ybrowser_loader *loader, const char *url, size_t *out_len,
                       char **out_effective_url)
{
    if (out_effective_url) {
        *out_effective_url = NULL;
    }
    struct yetty_ybrowser_request request = {
        .url = url,
        .kind = YETTY_YBROWSER_REQUEST_DOCUMENT,
    };
    struct yetty_ybrowser_response response = {0};
    struct yetty_ycore_void_result fetch_res = yetty_ybrowser_fetch(loader, &request, &response);
    if (YETTY_IS_ERR(fetch_res)) {
        fprintf(stderr, "ybrowser: fetch %s failed: %s\n", url, fetch_res.error.msg);
        yetty_ycore_error_destroy(fetch_res.error);
        return NULL;
    }
    if (!response.body) {
        fprintf(stderr, "ybrowser: fetch %s failed\n", url);
        yetty_ybrowser_response_dispose(&response);
        return NULL;
    }
    if (response.status >= 400) {
        fprintf(stderr, "ybrowser: %s -> HTTP %ld\n", url, response.status);
        /* Render the body anyway — error pages are HTML too. */
    }
    if (out_effective_url && response.effective_url) {
        *out_effective_url = response.effective_url;
        response.effective_url = NULL; /* ownership to the caller */
    }
    *out_len = response.body_len;
    char *body = response.body;
    response.body = NULL; /* ownership to the caller */
    yetty_ybrowser_response_dispose(&response);
    return body;
}

char *ybrowser_slurp_file(struct yetty_ybrowser_loader *loader, const char *path,
                          size_t *out_len, char **out_effective_url)
{
    if (ybrowser_looks_like_url(path)) {
        return slurp_url(loader, path, out_len, out_effective_url);
    }
    if (out_effective_url) {
        *out_effective_url = NULL;
    }

    FILE *f = strcmp(path, "-") == 0 ? stdin : fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "ybrowser: cannot open %s: %s\n", path, strerror(errno));
        return NULL;
    }
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    if (buf == NULL) {
        if (f != stdin) {
            fclose(f);
        }
        return NULL;
    }
    for (;;) {
        if (len == cap) {
            cap *= 2;
            char *p = realloc(buf, cap);
            if (p == NULL) {
                free(buf);
                if (f != stdin) {
                    fclose(f);
                }
                return NULL;
            }
            buf = p;
        }
        size_t got = fread(buf + len, 1, cap - len, f);
        if (got == 0) {
            break;
        }
        len += got;
    }
    if (f != stdin) {
        fclose(f);
    }
    *out_len = len;
    return buf;
}

/* ===========================================================================
 * One-shot output — yface emit helpers.
 * ===========================================================================*/

static int emit_envelope(int osc_code, int compressed, const void *args, size_t args_len,
                         const void *body, size_t body_len)
{
    struct yetty_ycore_buffer env = {0};
    struct yetty_ycore_void_result r =
        yetty_yface_emit(osc_code, compressed, args, args_len, body, body_len, &env);
    int rc = 0;
    if (YETTY_IS_OK(r) && env.size > 0) {
        fwrite(env.data, 1, env.size, stdout);
    } else if (YETTY_IS_ERR(r)) {
        rc = 1;
    }
    yetty_ycore_buffer_destroy(&env);
    return rc;
}

static int emit_bin_osc(const uint8_t *bytes, size_t blen)
{
    struct yetty_yface_bin_meta meta = {
        .magic = YETTY_YFACE_BIN_MAGIC,
        .version = YETTY_YFACE_BIN_VERSION,
        .compressed = YETTY_YFACE_COMP_LZ4F,
        .compression_algo = 0,
        .raw_size = blen,
        .reserved = {0, 0},
    };
    return emit_envelope(YETTY_DCS_YDRAW_BIN, /*compressed=*/1, &meta, sizeof(meta), bytes, blen);
}

/* ===========================================================================
 * main
 * ===========================================================================*/

static void usage(const char *argv0)
{
    fprintf(stderr,
            "usage: %s [--once|--interactive] [--osc|--raw] [--no-ui]\n"
            "            [--record FILE] [-w W] [-H H] [--font-size PX]\n"
            "            [<file>|<url>|-]\n"
            "\n"
            "  Interactive (default on a TTY): a Chrome-like browser — tabbar,\n"
            "  address bar, back/forward/reload, scrollable page. <file>/<url>\n"
            "  is the optional first page.\n"
            "\n"
            "  --no-ui (standalone): hide the tab strip + address bar; the\n"
            "  window is just the rendered page.\n"
            "  --record FILE (standalone): record the run to an mp4 (the core\n"
            "  yframework/yvnc recorder; captures the GPU output directly).\n"
            "\n"
            "  --once (legacy OSC mode; default when stdout is not a TTY):\n"
            "  read HTML from <file> (or '-' / no arg = stdin) or a url, render\n"
            "  it, and emit one ydraw OSC envelope on stdout (raw YPB1 bytes\n"
            "  with --raw), then exit.\n",
            argv0);
}

/* Probe the terminal for its pixel viewport via TIOCGWINSZ. ws_xpixel /
 * ws_ypixel is the field xterm/yetty/most modern terminals fill with
 * their drawable area in pixels. If the terminal reports cell counts
 * but not pixels (e.g. tmux, screen), we approximate from the cell
 * grid using a ~9x18 default cell — close enough that pages don't
 * get squished into 80px viewports.
 *
 * Returns 1 and writes (*w_out, *h_out) on success, 0 if no TTY. */
static int probe_terminal_size(int *w_out, int *h_out)
{
    int fds[] = {STDOUT_FILENO, STDERR_FILENO, STDIN_FILENO};
    for (size_t i = 0; i < sizeof(fds) / sizeof(fds[0]); i++) {
        if (!isatty(fds[i])) {
            continue;
        }
        struct winsize ws = {0};
        if (ioctl(fds[i], TIOCGWINSZ, &ws) != 0) {
            continue;
        }
        if (ws.ws_xpixel > 0 && ws.ws_ypixel > 0) {
            *w_out = ws.ws_xpixel;
            *h_out = ws.ws_ypixel;
            return 1;
        }
        if (ws.ws_col > 0 && ws.ws_row > 0) {
            /* Cells only — approximate pixel viewport. Most
			 * monospaced fonts end up around 9×18 px at the
			 * default size. Better than the hardcoded 1024×768
			 * for narrow terminals. */
            *w_out = (int)ws.ws_col * 9;
            *h_out = (int)ws.ws_row * 18;
            return 1;
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    int interactive = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
    int osc = isatty(STDOUT_FILENO) ? 1 : 0;
    /* Detect the viewport from the terminal. -w / -H still override. */
    int width = 1024, height = 768;
    int term_w = 0, term_h = 0;
    if (probe_terminal_size(&term_w, &term_h)) {
        width = term_w;
        height = term_h;
    }
    float font_size = 16.0f;
    const char *path = NULL;
    /* Debug: after layout, print one TSV row per box (kind, tag, x, y, w, h)
     * to stdout and exit — no OSC. Feeds a wireframe renderer so layout
     * changes (e.g. floats moving figures to the right rail) can be seen
     * without a GPU window. */
    int dump_boxes = 0;
    /* --dump-wpt: emit, for every box whose element carries a check-layout-th.js
     * assertion (data-expected-width/-height/data-offset-x/-y), one TSV line of
     * actual vs expected geometry, for the upstream-WPT conformance runner. */
    int dump_wpt = 0;
    /* --dump-geo: emit `dom-path  x  y  w  h` per element box, for matching
     * against Chrome's getBoundingClientRect (geometry diff oracle). */
    int dump_geo = 0;
    /* Standalone-only: hide the browser chrome (tab strip + address bar) so
     * the window is just the page, and record the GPU output to an mp4 via
     * the core (yframework/yvnc) recorder. */
    int no_ui = 0;
    const char *record_path = NULL;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--osc")) {
            osc = 1;
        } else if (!strcmp(a, "--dump-boxes")) {
            dump_boxes = 1;
            interactive = 0;
        } else if (!strcmp(a, "--dump-geo")) {
            dump_geo = 1;
            interactive = 0;
        } else if (!strcmp(a, "--dump-wpt")) {
            dump_wpt = 1;
            interactive = 0;
        } else if (!strcmp(a, "--raw")) {
            osc = 0;
        } else if (!strcmp(a, "--once")) {
            interactive = 0;
        } else if (!strcmp(a, "--interactive")) {
            interactive = 1;
        } else if (!strcmp(a, "--no-ui")) {
            no_ui = 1;
        } else if (!strcmp(a, "--record") && i + 1 < argc) {
            record_path = argv[++i];
        } else if (!strcmp(a, "-w") && i + 1 < argc) {
            width = atoi(argv[++i]);
        } else if (!strcmp(a, "-H") && i + 1 < argc) {
            height = atoi(argv[++i]);
        } else if (!strcmp(a, "--font-size") && i + 1 < argc) {
            font_size = (float)atof(argv[++i]);
        } else if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            usage(argv[0]);
            return 0;
        } else if (a[0] != '-' || !strcmp(a, "-")) {
            path = a;
        }
    }

    /* Interactive mode: hand off to the ygui browser UI. The positional
	 * arg, if any, is the first page to open (URL or file); stdin carries
	 * keystrokes, not HTML, so it is never read as a document here.
	 *
	 * Outside a host yetty we open our OWN GPU window (standalone) — that
	 * is the mode where the mouse wheel scrolls the page (a host yetty eats
	 * the wheel for its own scrollback). Inside a yetty we run as a client. */
    if (interactive) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
#ifdef YETTY_YBROWSER_HAS_STANDALONE
        int in_yetty = yetty_running_under_yetty();
        if (!in_yetty) {
            /* Hand the platform bootstrap a clean argv (just the program name) so it does
			 * not try to parse ybrowser's URL/flags as yetty options. */
            char record_arg[1100];
            char *clean_argv[3];
            int clean_argc = 0;
            clean_argv[clean_argc++] = argv[0];
            if (record_path && record_path[0]) {
                snprintf(record_arg, sizeof(record_arg), "--record=%s", record_path);
                clean_argv[clean_argc++] = record_arg;
            }
            clean_argv[clean_argc] = NULL;
            return ybrowser_ui_run_standalone(path, font_size, no_ui, clean_argc, clean_argv);
        }
#endif
        return ybrowser_ui_run(path, width, height, font_size);
    }

    /* ---- One-shot: render the page and emit one OSC envelope. ---- */
    const char *src = path ? path : "-";
    if (ybrowser_looks_like_url(src)) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }
    /* One loader for the whole run — the page fetch below warms the
	 * connection the engine then reuses for CSS/images. */
    struct yetty_ybrowser_loader *loader = NULL;
    {
        struct yetty_ybrowser_loader_ptr_result loader_res = yetty_ybrowser_loader_create();
        if (YETTY_IS_OK(loader_res)) {
            loader = loader_res.value;
        } else {
            yetty_ycore_error_destroy(loader_res.error);
        }
    }
    size_t html_len = 0;
    char *effective_url = NULL;
    char *html = ybrowser_slurp_file(loader, src, &html_len, &effective_url);
    if (html == NULL) {
        (void)yetty_ybrowser_loader_destroy(loader);
        return 1;
    }

    struct yetty_ylexbor_config cfg = {
        .viewport_width = width,
        .viewport_height = height,
        .default_font_size = font_size,
        .loader = loader,
    };
    struct yetty_ylexbor_ptr_result r = yetty_ylexbor_create(&cfg);
    if (YETTY_IS_ERR(r)) {
        fprintf(stderr, "ylexbor_create: %s\n", r.error.msg);
        free(html);
        free(effective_url);
        return 1;
    }
    struct yetty_ylexbor *yl = r.value;

    /* Tell the engine where the document came from so JS fetch() and
	 * external <script src=...> can resolve relative URLs. Prefer the
	 * post-redirect effective URL — gets us onto HTTPS (HTTP/2
	 * multiplexed image fetches) when the user typed an http://. */
    if (effective_url) {
        (void)yetty_ylexbor_set_base_url(yl, effective_url);
        free(effective_url);
    } else if (ybrowser_looks_like_url(src)) {
        (void)yetty_ylexbor_set_base_url(yl, src);
    }

    struct yetty_ycore_void_result lr = yetty_ylexbor_load_html(yl, html, html_len);
    if (YETTY_IS_ERR(lr)) {
        fprintf(stderr, "load_html: %s\n", lr.error.msg);
        yetty_ylexbor_destroy(yl);
        free(html);
        return 1;
    }

    /* SPA boot: most modern pages render their initial body via
	 * setTimeout(fn, 0) / queueMicrotask / requestAnimationFrame
	 * chains that haven't fired by the time load_html returns. Pump
	 * the timer queue for a bounded budget (default 2s wall-clock)
	 * so the actual content has a chance to land in the DOM before
	 * we paint. */
    long budget_ms = 2000;
    const char *e = getenv("YLEXBOR_BOOT_BUDGET_MS");
    if (e) {
        budget_ms = atol(e);
    }
    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    while (budget_ms > 0) {
        int wait_ms = yetty_ylexbor_pump_timers(yl);
        if (wait_ms < 0) {
            break; /* no more timers */
        }
        if (wait_ms > 50) {
            wait_ms = 50;
        }
        struct timespec ts = {
            .tv_sec = wait_ms / 1000,
            .tv_nsec = (wait_ms % 1000) * 1000000L,
        };
        nanosleep(&ts, NULL);
        struct timespec t1;
        clock_gettime(CLOCK_MONOTONIC, &t1);
        long elapsed = (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000;
        if (elapsed >= budget_ms) {
            break;
        }
    }
    if (yetty_ylexbor_dom_dirty(yl)) {
        (void)yetty_ylexbor_relayout(yl);
    }

    if (dump_geo) {
        /* `dom-path  x  y  w  h` for every element box, for the Chrome
         * geometry-diff oracle (matches Chrome's getBoundingClientRect by
         * identical nth-of-type DOM path). Anonymous/text boxes have no
         * element and are skipped. The LAST box for a given path wins in the
         * consumer (deepest layout pass), matching Chrome's single rect. */
        int count = yetty_ylexbor_test_box_count(yl);
        for (int bi = 0; bi < count; bi++) {
            char path[512] = {0};
            if (yetty_ylexbor_test_box_path_at(yl, bi, path, (int)sizeof(path)) != 0 || !path[0]) {
                continue;
            }
            float x = 0, y = 0, w = 0, h = 0;
            char tag[32] = {0};
            if (yetty_ylexbor_test_box_at(yl, bi, &x, &y, &w, &h, tag, sizeof(tag)) != 0) {
                continue;
            }
            printf("%s\t%.1f\t%.1f\t%.1f\t%.1f\n", path, x, y, w, h);
        }
        fflush(stdout);
        yetty_ylexbor_destroy(yl);
        free(html);
        (void)yetty_ybrowser_loader_destroy(loader);
        return 0;
    }

    if (dump_wpt) {
        /* One line per box whose element carries a check-layout-th.js
         * assertion: `tag ox oy ew eh ax ay aw ah`. The o/e columns are the
         * data-offset-x, data-offset-y, data-expected-width, data-expected-height
         * attribute values ("-" if absent); the a columns are ybrowser's actual
         * document-coord geometry. The runner compares present expectations
         * against the actuals. */
        int count = yetty_ylexbor_test_box_count(yl);
        /* check-layout-th.js's data-offset-x/-y are ABSOLUTE document
         * coordinates (the harness sums offsetLeft/offsetTop up the offsetParent
         * chain). ybrowser already applies the UA `body{margin:8px}`, so its
         * absolute box coords compare directly — no rebasing needed. */
        printf("#tag\tox\toy\tew\teh\tax\tay\taw\tah\n");
        for (int bi = 0; bi < count; bi++) {
            float x = 0, y = 0, w = 0, h = 0;
            char tag[32] = {0};
            if (yetty_ylexbor_test_box_at(yl, bi, &x, &y, &w, &h, tag, sizeof(tag)) != 0) {
                continue;
            }
            char ox[32] = {0}, oy[32] = {0}, ew[32] = {0}, eh[32] = {0};
            (void)yetty_ylexbor_test_box_attr_at(yl, bi, "data-offset-x", ox, sizeof(ox));
            (void)yetty_ylexbor_test_box_attr_at(yl, bi, "data-offset-y", oy, sizeof(oy));
            (void)yetty_ylexbor_test_box_attr_at(yl, bi, "data-expected-width", ew, sizeof(ew));
            (void)yetty_ylexbor_test_box_attr_at(yl, bi, "data-expected-height", eh, sizeof(eh));
            if (!ox[0] && !oy[0] && !ew[0] && !eh[0]) {
                continue; /* no assertion on this element */
            }
            printf("%s\t%s\t%s\t%s\t%s\t%.1f\t%.1f\t%.1f\t%.1f\n", tag[0] ? tag : "-",
                   ox[0] ? ox : "-", oy[0] ? oy : "-", ew[0] ? ew : "-", eh[0] ? eh : "-", x, y, w,
                   h);
        }
        fflush(stdout);
        yetty_ylexbor_destroy(yl);
        free(html);
        (void)yetty_ybrowser_loader_destroy(loader);
        return 0;
    }

    if (dump_boxes) {
        int count = yetty_ylexbor_test_box_count(yl);
        /* `dt` keys a box by its `data-test` attribute so the Chrome
         * geometry oracle can match boxes to its *.ref.json entries by name
         * instead of by fragile DOM index. "-" when the box has none. */
        printf("#kind\ttag\tdt\tx\ty\tw\th\n");
        for (int bi = 0; bi < count; bi++) {
            float x = 0, y = 0, w = 0, h = 0;
            char tag[32] = {0};
            if (yetty_ylexbor_test_box_at(yl, bi, &x, &y, &w, &h, tag, sizeof(tag)) != 0) {
                continue;
            }
            int kind = 0, weight = 0, italic = 0, underline = 0;
            char snippet[40] = {0};
            (void)yetty_ylexbor_test_box_info_at(yl, bi, &kind, &weight, &italic, &underline,
                                                 snippet, (int)sizeof(snippet));
            char data_test[64] = {0};
            (void)yetty_ylexbor_test_box_data_test_at(yl, bi, data_test, (int)sizeof(data_test));
            printf("%d\t%s\t%s\t%.1f\t%.1f\t%.1f\t%.1f\tw=%d\ti=%d\tu=%d\t%s\n", kind,
                   tag[0] ? tag : "-", data_test[0] ? data_test : "-", x, y, w, h, weight, italic,
                   underline, snippet);
        }
        fflush(stdout);
        yetty_ylexbor_destroy(yl);
        free(html);
        (void)yetty_ybrowser_loader_destroy(loader);
        return 0;
    }

    struct yetty_ydraw_drawable_list_result br =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    if (YETTY_IS_ERR(br)) {
        yetty_ylexbor_destroy(yl);
        free(html);
        (void)yetty_ybrowser_loader_destroy(loader);
        return 1;
    }
    struct yetty_ydraw_drawable_list *buf = br.value;
    struct yetty_ycore_void_result rr = yetty_ylexbor_render(yl, buf);
    if (YETTY_IS_ERR(rr)) {
        fprintf(stderr, "render: %s\n", rr.error.msg);
        yetty_ydraw_drawable_list_destroy(buf);
        yetty_ylexbor_destroy(yl);
        free(html);
        (void)yetty_ybrowser_loader_destroy(loader);
        return 1;
    }
    const uint8_t *bytes = NULL;
    size_t blen = yetty_ydraw_drawable_list_serialize(buf, &bytes);
    if (osc) {
        (void)emit_bin_osc(bytes, blen);
    } else {
        fwrite(bytes, 1, blen, stdout);
    }
    fflush(stdout);
    yetty_ydraw_drawable_list_destroy(buf);

    yetty_ylexbor_destroy(yl);
    free(html);
    (void)yetty_ybrowser_loader_destroy(loader);
    return 0;
}
