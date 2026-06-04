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
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/yface/yface.h>
#include <yetty/yterm/osc-codes.h>
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

struct fetch_buf {
	char  *data;
	size_t size, cap;
};

static size_t fetch_write_cb(char *p, size_t sz, size_t n, void *ud)
{
	struct fetch_buf *b = ud;
	size_t add = sz * n;
	if (b->size + add + 1 > b->cap) {
		size_t nc = b->cap ? b->cap * 2 : 16384;
		while (nc < b->size + add + 1) nc *= 2;
		char *np = realloc(b->data, nc);
		if (np == NULL) return 0;  /* signals curl to abort */
		b->data = np; b->cap = nc;
	}
	memcpy(b->data + b->size, p, add);
	b->size += add;
	return add;
}

/* Out-param: caller-owned char* gets filled with the post-redirect URL
 * (e.g. http://wikipedia.org/wiki/Photography → https://en.wikipedia.org/
 * wiki/Photography). NULL passes through. The caller frees with free().
 * Used to set base_url to the effective URL so that image src= references
 * resolve against the HTTPS origin — that pulls them over HTTP/2
 * multiplexing instead of HTTP/1.1 with 30+ TCP handshakes. */
static char *slurp_url(const char *url, size_t *out_len, char **out_effective_url)
{
	CURL *c = curl_easy_init();
	if (c == NULL) return NULL;
	struct fetch_buf b = {0};
	if (out_effective_url) *out_effective_url = NULL;

	/* Attach the process-wide CURLSH so the TLS connection we open
	 * here gets cached and reused by load_external_stylesheets and
	 * the image-fetch path. Without this the page fetch's
	 * connection to e.g. en.wikipedia.org is discarded immediately
	 * after the easy_cleanup below, forcing CSS to re-handshake. */
	void *share = yetty_ylexbor_curl_share();
	if (share) {
		curl_easy_setopt(c, CURLOPT_SHARE, share);
	}

	curl_easy_setopt(c, CURLOPT_URL, url);
	curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(c, CURLOPT_MAXREDIRS, 10L);
	/* Send a standard Chrome UA. Bot-looking UAs ("ybrowser/0.1")
	 * cause sites like Wikipedia and news.google.com to serve a
	 * stripped-down legacy/no-JS page that hides images behind sprite
	 * tricks or replaces <img src> with text labels. The image-fetch
	 * path in ybrowser-js-web.c already sends Chrome 120; align the
	 * page-fetch UA so the *whole* document looks like a real browser
	 * to the origin. YETTY_USER_AGENT env var overrides for ops who
	 * need bot-identifying UAs (ToS compliance, scraping budgets). */
	const char *ua = getenv("YETTY_USER_AGENT");
	if (!ua || !*ua) {
		ua = "Mozilla/5.0 (X11; Linux x86_64) "
		     "AppleWebKit/537.36 (KHTML, like Gecko) "
		     "Chrome/120.0.0.0 Safari/537.36";
	}
	curl_easy_setopt(c, CURLOPT_USERAGENT, ua);
	curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
	curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
	/* Empty string = "advertise every encoding curl was built with"
	 * (gzip + brotli for our prebuilt). curl decompresses for us. */
	curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, fetch_write_cb);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, &b);
	/* Send a browser-shape Accept header. Without an Accept header,
	 * Wikipedia's caching tier sometimes returns the lightweight bot
	 * variant of pages. */
	struct curl_slist *headers = NULL;
	headers = curl_slist_append(headers,
		"Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8");
	headers = curl_slist_append(headers, "Accept-Language: en-US,en;q=0.9");
	if (headers) {
		curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
	}

	CURLcode rc = curl_easy_perform(c);
	long http_status = 0;
	curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http_status);

	if (rc != CURLE_OK) {
		fprintf(stderr, "ybrowser: fetch %s failed: %s\n",
			url, curl_easy_strerror(rc));
		curl_easy_cleanup(c);
		if (headers) curl_slist_free_all(headers);
		free(b.data);
		return NULL;
	}
	if (http_status >= 400) {
		fprintf(stderr, "ybrowser: %s -> HTTP %ld\n",
			url, http_status);
		/* Render the body anyway — error pages are HTML too. */
	}
	/* Capture the effective URL after redirect chain so the caller
	 * can pin base_url to the actual HTTPS origin (not the http://
	 * the user typed). Without this, protocol-relative image URLs
	 * like //upload.wikimedia.org/... resolve to http://, which
	 * forces HTTP/1.1 and 30+ fresh TCP handshakes per page. */
	if (out_effective_url) {
		char *eff = NULL;
		curl_easy_getinfo(c, CURLINFO_EFFECTIVE_URL, &eff);
		if (eff && *eff) {
			*out_effective_url = strdup(eff);
		}
	}
	curl_easy_cleanup(c);
	if (headers) curl_slist_free_all(headers);

	*out_len = b.size;
	return b.data;
}

char *ybrowser_slurp_file(const char *path, size_t *out_len, char **out_effective_url)
{
	if (ybrowser_looks_like_url(path))
		return slurp_url(path, out_len, out_effective_url);
	if (out_effective_url) *out_effective_url = NULL;

	FILE *f = strcmp(path, "-") == 0 ? stdin : fopen(path, "rb");
	if (f == NULL) {
		fprintf(stderr, "ybrowser: cannot open %s: %s\n",
			path, strerror(errno));
		return NULL;
	}
	size_t cap = 4096, len = 0;
	char *buf = malloc(cap);
	if (buf == NULL) { if (f != stdin) fclose(f); return NULL; }
	for (;;) {
		if (len == cap) {
			cap *= 2;
			char *p = realloc(buf, cap);
			if (p == NULL) { free(buf); if (f != stdin) fclose(f); return NULL; }
			buf = p;
		}
		size_t got = fread(buf + len, 1, cap - len, f);
		if (got == 0) break;
		len += got;
	}
	if (f != stdin) fclose(f);
	*out_len = len;
	return buf;
}

/* ===========================================================================
 * One-shot output — yface emit helpers.
 * ===========================================================================*/

static int emit_envelope(int osc_code, int compressed,
			 const void *args, size_t args_len,
			 const void *body, size_t body_len)
{
	struct yetty_ycore_buffer env = {0};
	struct yetty_ycore_void_result r = yetty_yface_emit(
		osc_code, compressed, args, args_len, body, body_len, &env);
	int rc = 0;
	if (YETTY_IS_OK(r) && env.size > 0)
		fwrite(env.data, 1, env.size, stdout);
	else if (YETTY_IS_ERR(r))
		rc = 1;
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
	return emit_envelope(YETTY_OSC_YDRAW_BIN, /*compressed=*/1,
			     &meta, sizeof(meta), bytes, blen);
}

/* ===========================================================================
 * main
 * ===========================================================================*/

static void usage(const char *argv0)
{
	fprintf(stderr,
		"usage: %s [--once|--interactive] [--osc|--raw]\n"
		"            [-w W] [-H H] [--font-size PX] [<file>|<url>|-]\n"
		"\n"
		"  Interactive (default on a TTY): a Chrome-like browser — tabbar,\n"
		"  address bar, back/forward/reload, scrollable page. <file>/<url>\n"
		"  is the optional first page.\n"
		"\n"
		"  --once (legacy OSC mode; default when stdout is not a TTY):\n"
		"  read HTML from <file> (or '-' / no arg = stdin) or a url, render\n"
		"  it, and emit one ydraw OSC envelope on stdout (raw YPB1 bytes\n"
		"  with --raw), then exit.\n", argv0);
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
	int fds[] = { STDOUT_FILENO, STDERR_FILENO, STDIN_FILENO };
	for (size_t i = 0; i < sizeof(fds)/sizeof(fds[0]); i++) {
		if (!isatty(fds[i])) continue;
		struct winsize ws = {0};
		if (ioctl(fds[i], TIOCGWINSZ, &ws) != 0) continue;
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

	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		if      (!strcmp(a, "--osc"))         osc = 1;
		else if (!strcmp(a, "--raw"))         osc = 0;
		else if (!strcmp(a, "--once"))        interactive = 0;
		else if (!strcmp(a, "--interactive")) interactive = 1;
		else if (!strcmp(a, "-w")     && i + 1 < argc) width  = atoi(argv[++i]);
		else if (!strcmp(a, "-H")     && i + 1 < argc) height = atoi(argv[++i]);
		else if (!strcmp(a, "--font-size") && i + 1 < argc)
			font_size = (float)atof(argv[++i]);
		else if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
			usage(argv[0]); return 0;
		}
		else if (a[0] != '-' || !strcmp(a, "-")) path = a;
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
		const char *term_program = getenv("TERM_PROGRAM");
		int in_yetty = term_program && strcmp(term_program, "yetty") == 0;
		if (!in_yetty) {
			/* Hand yinit a clean argv (just the program name) so it does
			 * not try to parse ybrowser's URL/flags as yetty options. */
			char *clean_argv[2] = {argv[0], NULL};
			return ybrowser_ui_run_standalone(path, font_size, 1, clean_argv);
		}
#endif
		return ybrowser_ui_run(path, width, height, font_size);
	}

	/* ---- One-shot: render the page and emit one OSC envelope. ---- */
	const char *src = path ? path : "-";
	if (ybrowser_looks_like_url(src)) {
		curl_global_init(CURL_GLOBAL_DEFAULT);
	}
	size_t html_len = 0;
	char *effective_url = NULL;
	char *html = ybrowser_slurp_file(src, &html_len, &effective_url);
	if (html == NULL) return 1;

	struct yetty_ylexbor_config cfg = {
		.viewport_width = width,
		.viewport_height = height,
		.default_font_size = font_size,
	};
	struct yetty_ylexbor_ptr_result r = yetty_ylexbor_create(&cfg);
	if (YETTY_IS_ERR(r)) {
		fprintf(stderr, "ylexbor_create: %s\n", r.error.msg);
		free(html); free(effective_url); return 1;
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

	struct yetty_ycore_void_result lr =
		yetty_ylexbor_load_html(yl, html, html_len);
	if (YETTY_IS_ERR(lr)) {
		fprintf(stderr, "load_html: %s\n", lr.error.msg);
		yetty_ylexbor_destroy(yl); free(html); return 1;
	}

	/* SPA boot: most modern pages render their initial body via
	 * setTimeout(fn, 0) / queueMicrotask / requestAnimationFrame
	 * chains that haven't fired by the time load_html returns. Pump
	 * the timer queue for a bounded budget (default 2s wall-clock)
	 * so the actual content has a chance to land in the DOM before
	 * we paint. */
	long budget_ms = 2000;
	const char *e = getenv("YLEXBOR_BOOT_BUDGET_MS");
	if (e) budget_ms = atol(e);
	struct timespec t0;
	clock_gettime(CLOCK_MONOTONIC, &t0);
	while (budget_ms > 0) {
		int wait_ms = yetty_ylexbor_pump_timers(yl);
		if (wait_ms < 0) break;  /* no more timers */
		if (wait_ms > 50) wait_ms = 50;
		struct timespec ts = {
			.tv_sec  = wait_ms / 1000,
			.tv_nsec = (wait_ms % 1000) * 1000000L,
		};
		nanosleep(&ts, NULL);
		struct timespec t1;
		clock_gettime(CLOCK_MONOTONIC, &t1);
		long elapsed = (t1.tv_sec - t0.tv_sec) * 1000 +
			       (t1.tv_nsec - t0.tv_nsec) / 1000000;
		if (elapsed >= budget_ms) break;
	}
	if (yetty_ylexbor_dom_dirty(yl)) {
		(void)yetty_ylexbor_relayout(yl);
	}

	struct yetty_ydraw_drawable_list_result br =
		yetty_ydraw_drawable_list_config_buffer_create(NULL);
	if (YETTY_IS_ERR(br)) {
		yetty_ylexbor_destroy(yl); free(html); return 1;
	}
	struct yetty_ydraw_drawable_list *buf = br.value;
	struct yetty_ycore_void_result rr = yetty_ylexbor_render(yl, buf);
	if (YETTY_IS_ERR(rr)) {
		fprintf(stderr, "render: %s\n", rr.error.msg);
		yetty_ydraw_drawable_list_destroy(buf);
		yetty_ylexbor_destroy(yl); free(html); return 1;
	}
	const uint8_t *bytes = NULL;
	size_t blen = yetty_ydraw_drawable_list_serialize(buf, &bytes);
	if (osc) (void)emit_bin_osc(bytes, blen);
	else     fwrite(bytes, 1, blen, stdout);
	fflush(stdout);
	yetty_ydraw_drawable_list_destroy(buf);

	yetty_ylexbor_destroy(yl);
	free(html);
	return 0;
}
