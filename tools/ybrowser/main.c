/*
 * ylexbor-demo — render HTML via the ylexbor lib, emit ypaint OSC.
 *
 * Two operating modes (same shape ynetsurf uses):
 *
 *   ONE-SHOT (--once, or non-TTY stdout):
 *     read HTML, lay out, emit one OSC envelope to stdout, exit.
 *     Used to pipe HTML into yetty:
 *         echo '<h1>hi</h1>' | ./ylexbor-demo --osc
 *
 *   INTERACTIVE (default when stdin + stdout are TTYs):
 *     subscribe to terminal-wide input via YMGUI_OSC_CS_TERM_INPUT_SUB,
 *     re-render on every input event. Arrow keys / PgUp/PgDn scroll;
 *     Ctrl-C exits. The pane pixel size comes back as
 *     YMGUI_OSC_SC_TERM_RESIZE which we feed straight into the
 *     viewport.
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <yetty/ybrowser/ybrowser.h>
#include <yetty/ypaint-core/buffer.h>
#include <yetty/yface/yface.h>
#include <yetty/ycore/types.h>
#include <yetty/ymgui/wire.h>
#include <yetty/yterm/osc-codes.h>
#include <yetty/ytrace/ytrace.h>

#include <curl/curl.h>


/* ===========================================================================
 * Input acquisition — file path, stdin, http(s):// URL.
 * ===========================================================================*/

static int looks_like_url(const char *s)
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

static char *slurp_url(const char *url, size_t *out_len)
{
	CURL *c = curl_easy_init();
	if (c == NULL) return NULL;
	struct fetch_buf b = {0};

	curl_easy_setopt(c, CURLOPT_URL, url);
	curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(c, CURLOPT_MAXREDIRS, 10L);
	curl_easy_setopt(c, CURLOPT_USERAGENT,
		"ylexbor-demo/0.1 (yetty)");
	curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
	curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
	/* Empty string = "advertise every encoding curl was built with"
	 * (gzip + brotli for our prebuilt). curl decompresses for us. */
	curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, fetch_write_cb);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, &b);
	/* No-op stderr suppression — curl prints to stderr only on
	 * CURLOPT_VERBOSE, which we never set. */

	CURLcode rc = curl_easy_perform(c);
	long http_status = 0;
	curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http_status);

	if (rc != CURLE_OK) {
		fprintf(stderr, "ylexbor-demo: fetch %s failed: %s\n",
			url, curl_easy_strerror(rc));
		curl_easy_cleanup(c);
		free(b.data);
		return NULL;
	}
	if (http_status >= 400) {
		fprintf(stderr, "ylexbor-demo: %s -> HTTP %ld\n",
			url, http_status);
		/* Render the body anyway — error pages are HTML too. */
	}
	curl_easy_cleanup(c);

	*out_len = b.size;
	return b.data;
}

static char *slurp_file(const char *path, size_t *out_len)
{
	if (looks_like_url(path))
		return slurp_url(path, out_len);

	FILE *f = strcmp(path, "-") == 0 ? stdin : fopen(path, "rb");
	if (f == NULL) {
		fprintf(stderr, "ylexbor-demo: cannot open %s: %s\n",
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
 * Output — yface emit helpers (clone of ynetsurf's pattern).
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
	return emit_envelope(YETTY_OSC_YPAINT_BIN, /*compressed=*/1,
			     &meta, sizeof(meta), bytes, blen);
}

static int redraw_and_push(struct yetty_ylexbor *yl)
{
	struct yetty_ypaint_core_buffer_result br =
		yetty_ypaint_core_buffer_config_buffer_create(NULL);
	if (YETTY_IS_ERR(br)) return 1;
	struct yetty_ypaint_core_buffer *buf = br.value;
	struct yetty_ycore_void_result rd = yetty_ylexbor_render(yl, buf);
	int rc = 1;
	if (YETTY_IS_OK(rd)) {
		const uint8_t *bytes = NULL;
		size_t blen = yetty_ypaint_core_buffer_serialize(buf, &bytes);
		(void)emit_envelope(YETTY_OSC_YPAINT_CLEAR, 0, NULL, 0, NULL, 0);
		(void)emit_bin_osc(bytes, blen);
		fflush(stdout);
		rc = 0;
	}
	yetty_ypaint_core_buffer_destroy(buf);
	return rc;
}

/* ===========================================================================
 * Interactive scaffolding — same shape as tools/ynetsurf/main.c.
 * ===========================================================================*/

static struct termios saved_tty;
static int tty_active = 0;

static void tty_restore(void)
{
	if (tty_active) {
		tcsetattr(STDIN_FILENO, TCSANOW, &saved_tty);
		tty_active = 0;
	}
}
static int tty_raw(void)
{
	if (!isatty(STDIN_FILENO)) return 0;
	if (tcgetattr(STDIN_FILENO, &saved_tty) < 0) return -1;
	struct termios raw = saved_tty;
	cfmakeraw(&raw);
	raw.c_cc[VMIN] = 0; raw.c_cc[VTIME] = 0;
	if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) < 0) return -1;
	tty_active = 1;
	atexit(tty_restore);
	return 0;
}

static volatile sig_atomic_t signal_quit = 0;
static void on_signal(int s) { (void)s; signal_quit = 1; }

struct ev_state {
	struct yetty_ylexbor *yl;
	int dirty;
	int want_quit;
};

static void term_input_subscribe(uint32_t flags)
{
	struct yetty_ymgui_wire_term_input_sub msg = {
		.magic = YMGUI_WIRE_MAGIC_TERM_INPUT_SUB,
		.version = YMGUI_WIRE_VERSION,
		.flags = flags,
		._pad0 = 0,
	};
	(void)emit_envelope(YMGUI_OSC_CS_TERM_INPUT_SUB, 0,
			    NULL, 0, &msg, sizeof(msg));
}

/* Inbound: resize → set viewport, mouse/key → mark dirty (re-render).
 * No real navigation — this is just a previewer. */
static void on_osc(void *user, int osc_code,
		   const uint8_t *args, size_t args_len,
		   const uint8_t *payload, size_t payload_len)
{
	(void)args; (void)args_len;
	struct ev_state *st = user;

	if (osc_code == YMGUI_OSC_SC_TERM_RESIZE ||
	    osc_code == YMGUI_OSC_SC_RESIZE) {
		if (payload_len < sizeof(struct yetty_ymgui_wire_input_resize))
			return;
		const struct yetty_ymgui_wire_input_resize *r =
			(const struct yetty_ymgui_wire_input_resize *)payload;
		if (r->width > 0 && r->height > 0) {
			yetty_ylexbor_set_viewport(st->yl,
				(int)r->width, (int)r->height);
			st->dirty = 1;
		}
		return;
	}
	if (osc_code == YMGUI_OSC_SC_TERM_MOUSE ||
	    osc_code == YMGUI_OSC_SC_MOUSE) {
		if (payload_len < sizeof(struct yetty_ymgui_wire_input_mouse))
			return;
		const struct yetty_ymgui_wire_input_mouse *m =
			(const struct yetty_ymgui_wire_input_mouse *)payload;
		/* Click → fire JS handlers attached at (x,y). The handlers
		 * may mutate the DOM; ylexbor's dom_dirty flag tells us so
		 * the main loop knows to relayout before the next paint. */
		if (m->kind == YETTY_YMGUI_INPUT_MOUSE_BUTTON && m->pressed) {
			yetty_ylexbor_dispatch_click(st->yl, m->x, m->y);
			st->dirty = 1;
		}
		return;
	}
	if (osc_code == YMGUI_OSC_SC_TERM_KEY) {
		st->dirty = 1;
	}
}

static void on_raw(void *user, const char *bytes, size_t n)
{
	struct ev_state *st = user;
	for (size_t i = 0; i < n; i++) {
		unsigned char c = (unsigned char)bytes[i];
		if (c == 0x03 || c == 0x11) { st->want_quit = 1; return; }
	}
	st->dirty = 1;
}

static int interactive_loop(struct yetty_ylexbor *yl)
{
	struct ev_state st = { .yl = yl };

	struct yetty_yface_ptr_result yr = yetty_yface_create();
	if (YETTY_IS_ERR(yr)) return 1;
	struct yetty_yface *yface = yr.value;
	yetty_yface_set_handlers(yface, on_osc, on_raw, &st);

	signal(SIGINT,  on_signal);
	signal(SIGTERM, on_signal);
	signal(SIGHUP,  on_signal);

	if (tty_raw() < 0) {
		yetty_yface_destroy(yface);
		return 1;
	}

	term_input_subscribe(YETTY_YMGUI_TERM_SUB_MOUSE_CLICK |
			     YETTY_YMGUI_TERM_SUB_MOUSE_MOVE  |
			     YETTY_YMGUI_TERM_SUB_MOUSE_WHEEL |
			     YETTY_YMGUI_TERM_SUB_KEY);
	fflush(stdout);

	(void)redraw_and_push(yl);

	char buf[4096];
	while (!signal_quit && !st.want_quit) {
		fd_set rfds; FD_ZERO(&rfds); FD_SET(STDIN_FILENO, &rfds);
		/* Cap the select timeout at whatever JS timer fires next.
		 * pump_timers returns -1 when there are none — fall back to
		 * 100ms so we still relayout if the JS loop posts work via
		 * setTimeout(0,...) right after our last drain. */
		int wait_ms = yetty_ylexbor_pump_timers(yl);
		if (wait_ms < 0 || wait_ms > 100) wait_ms = 100;
		struct timeval tv = {
			.tv_sec  = wait_ms / 1000,
			.tv_usec = (wait_ms % 1000) * 1000,
		};
		int rc = select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv);
		if (rc < 0) { if (errno == EINTR) continue; break; }

		st.dirty = 0;
		if (FD_ISSET(STDIN_FILENO, &rfds)) {
			ssize_t got = read(STDIN_FILENO, buf, sizeof(buf));
			if (got > 0) {
				(void)yetty_yface_feed_bytes(yface, buf, (size_t)got);
			} else if (got == 0 && !isatty(STDIN_FILENO)) {
				break;
			}
		}
		/* Tick timers again after handling input — JS timers, click
		 * handlers and DOMContentLoaded may all have queued work. */
		(void)yetty_ylexbor_pump_timers(yl);
		if (st.dirty || yetty_ylexbor_dom_dirty(yl)) {
			if (yetty_ylexbor_dom_dirty(yl)) {
				(void)yetty_ylexbor_relayout(yl);
			}
			(void)redraw_and_push(yl);
		}
	}

	term_input_subscribe(0);
	fflush(stdout);
	yetty_yface_destroy(yface);
	return 0;
}

/* ===========================================================================
 * main
 * ===========================================================================*/

static void usage(const char *argv0)
{
	fprintf(stderr,
		"usage: %s [--once|--interactive] [--osc|--raw]\n"
		"            [-w W] [-H H] [--font-size PX] [<file>|-]\n"
		"\n"
		"  Reads HTML from <file> (or '-' / no arg = stdin), renders\n"
		"  via lexbor + ylexbor's block-flow layout, emits a ypaint\n"
		"  OSC envelope on stdout (or raw YPB1 bytes with --raw).\n"
		"\n"
		"  default mode: interactive when stdin+stdout are TTYs,\n"
		"                one-shot otherwise.\n", argv0);
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
		ydebug("ylexbor-demo: detected terminal viewport %dx%d",
		       width, height);
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

	/* Slurp HTML. If interactive AND no input, complain — interactive
	 * mode reads keystrokes from stdin, so HTML must be a file. */
	const char *src = path ? path : "-";
	if (interactive && (!path || !strcmp(path, "-"))) {
		/* Interactive mode reads keystrokes from stdin, so the
		 * HTML source must be a file path or http(s):// URL. */
		fprintf(stderr,
		    "ylexbor-demo: interactive mode needs a file path or URL\n"
		    "              (stdin is reserved for keystrokes). Try --once.\n");
		return 2;
	}
	if (looks_like_url(src)) {
		curl_global_init(CURL_GLOBAL_DEFAULT);
	}
	size_t html_len = 0;
	char *html = slurp_file(src, &html_len);
	if (html == NULL) return 1;

	struct yetty_ylexbor_config cfg = {
		.viewport_width = width,
		.viewport_height = height,
		.default_font_size = font_size,
	};
	struct yetty_ylexbor_ptr_result r = yetty_ylexbor_create(&cfg);
	if (YETTY_IS_ERR(r)) {
		fprintf(stderr, "ylexbor_create: %s\n", r.error.msg);
		free(html); return 1;
	}
	struct yetty_ylexbor *yl = r.value;

	/* Tell ylexbor where the document came from so JS fetch() and
	 * external <script src=...> can resolve relative URLs. */
	if (looks_like_url(src)) {
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
	 * we paint. Skip in interactive mode — the loop pumps anyway. */
	if (!interactive) {
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
	}

	int rc;
	if (interactive) {
		rc = interactive_loop(yl);
	} else {
		struct yetty_ypaint_core_buffer_result br =
			yetty_ypaint_core_buffer_config_buffer_create(NULL);
		if (YETTY_IS_ERR(br)) {
			yetty_ylexbor_destroy(yl); free(html); return 1;
		}
		struct yetty_ypaint_core_buffer *buf = br.value;
		struct yetty_ycore_void_result rr = yetty_ylexbor_render(yl, buf);
		if (YETTY_IS_ERR(rr)) {
			fprintf(stderr, "render: %s\n", rr.error.msg);
			yetty_ypaint_core_buffer_destroy(buf);
			yetty_ylexbor_destroy(yl); free(html); return 1;
		}
		const uint8_t *bytes = NULL;
		size_t blen = yetty_ypaint_core_buffer_serialize(buf, &bytes);
		if (osc) (void)emit_bin_osc(bytes, blen);
		else     fwrite(bytes, 1, blen, stdout);
		fflush(stdout);
		yetty_ypaint_core_buffer_destroy(buf);
		rc = 0;
	}

	yetty_ylexbor_destroy(yl);
	free(html);
	return rc;
}
