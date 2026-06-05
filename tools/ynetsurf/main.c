/*
 * ynetsurf — NetSurf 3.11 browser tool, two operating modes:
 *
 *   ONE-SHOT (--once, or non-TTY stdout/stdin):
 *     navigate URL, wait, redraw, emit one OSC envelope (or raw ydraw
 *     bytes with --raw), exit. Same shape as the rest of yetty's emitter
 *     tools (ycat, yecho, yplot, ymarkdown).
 *
 *   INTERACTIVE (default when stdin + stdout are both TTYs):
 *     after the first render, sit in a select() loop driving:
 *       - libcurl's fdset (page fetches keep progressing)
 *       - the NetSurf scheduler timer
 *       - stdin (keyboard + any inbound OSC envelopes from yterm)
 *     yface owns stdin parsing: it splits OSC envelopes from raw bytes,
 *     calls on_osc(...) for each envelope and on_raw(...) for the bytes
 *     between. After any input, re-render: emit OSC 600000 (ydraw clear)
 *     followed by OSC 600001 (bin) so the receiving ydraw-layer replaces
 *     instead of accumulates primitives. Quit on Ctrl-C, EOF, or SIGTERM.
 *
 *   Notes on input plumbing:
 *     - Keyboard always works: standard PTY raw bytes via on_raw,
 *       translated to NetSurf NS_KEY_* / Unicode codepoints.
 *     - Mouse OSC 700000 (YMGUI card mouse) is wired via on_osc but at
 *       time of writing yterm only emits these when the cursor is over a
 *       registered ymgui card; ynetsurf running as a plain CLI doesn't
 *       receive any. The handler is in place so it lights up the day
 *       yterm forwards non-card mouse events to subscribed foreground
 *       processes.
 */

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <yetty/ynetsurf/ynetsurf.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/yface/yface.h>
#include <yetty/ycore/types.h>
#include <yetty/ymgui/wire.h>
#include <yetty/yterminal/client-input.h>
#include <yetty/yterminal/osc-codes.h>

#include "content/fetch.h"
#include "netsurf/keypress.h"

/* ===========================================================================
 * Output side — clear+bin re-render, single-shot emit.
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

/* Drain the page into a fresh ydraw buffer, then emit clear+bin so the
 * receiving ydraw-layer replaces (not accumulates) the previous frame's
 * primitives. */
static int redraw_and_push(struct yetty_ynetsurf *ns)
{
	struct yetty_ydraw_drawable_list_result br =
		yetty_ydraw_drawable_list_config_buffer_create(NULL);
	if (YETTY_IS_ERR(br)) {
		fprintf(stderr, "buffer_create: %s\n", br.error.msg);
		return 1;
	}
	struct yetty_ydraw_drawable_list *buf = br.value;
	struct yetty_ycore_void_result rd = yetty_ynetsurf_redraw(ns, buf);
	int rc = 1;
	if (YETTY_IS_OK(rd)) {
		const uint8_t *bytes = NULL;
		size_t blen = yetty_ydraw_drawable_list_serialize(buf, &bytes);
		(void)emit_envelope(YETTY_OSC_YDRAW_CLEAR, 0, NULL, 0, NULL, 0);
		(void)emit_bin_osc(bytes, blen);
		fflush(stdout);
		rc = 0;
	}
	yetty_ydraw_drawable_list_destroy(buf);
	return rc;
}

/* ===========================================================================
 * Drive the fetcher (curl) + scheduler for up to max_ms — used by both
 * modes to give the initial fetch + reflow a chance to settle before the
 * first paint.
 * ===========================================================================*/

static void wait_for_layout(struct yetty_ynetsurf *ns, int max_ms)
{
	struct timespec t0, now;
	clock_gettime(CLOCK_MONOTONIC, &t0);
	for (;;) {
		clock_gettime(CLOCK_MONOTONIC, &now);
		int elapsed = (int)((now.tv_sec - t0.tv_sec) * 1000 +
				    (now.tv_nsec - t0.tv_nsec) / 1000000);
		if (elapsed >= max_ms) return;
		int budget = max_ms - elapsed;
		int next = yetty_ynetsurf_pump(ns);
		if (next < 0 || next > budget) next = budget;

		fd_set rfds, wfds, efds;
		FD_ZERO(&rfds); FD_ZERO(&wfds); FD_ZERO(&efds);
		int max_fd = -1;
		fetch_fdset(&rfds, &wfds, &efds, &max_fd);
		struct timeval tv = {
			.tv_sec = next / 1000, .tv_usec = (next % 1000) * 1000,
		};
		(void)select(max_fd + 1, &rfds, &wfds, &efds, &tv);
	}
}

/* ===========================================================================
 * TTY raw mode — needed for keystroke-by-keystroke input. Saved + restored
 * around the interactive loop; atexit() cleans up on abnormal exit too.
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
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 0;
	if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) < 0) return -1;
	tty_active = 1;
	atexit(tty_restore);
	return 0;
}

/* ===========================================================================
 * Signals — flag the loop to exit; tty_restore runs from atexit.
 * ===========================================================================*/

static volatile sig_atomic_t signal_quit = 0;
static void on_signal(int sig) { (void)sig; signal_quit = 1; }

/* ===========================================================================
 * Per-iteration input state shared with the yface callbacks.
 * ===========================================================================*/

struct ev_state {
	struct yetty_ynetsurf *ns;
	int dirty;       /* set by handlers when something changed */
	int want_quit;   /* set on Ctrl-C / Ctrl-Q etc. */

	/* Partial-ESC-sequence accumulator. Held across reads because a CSI
	 * sequence (ESC [ … final) can straddle a read boundary in raw
	 * mode, especially under burst input from auto-repeat. */
	char esc_buf[64];
	size_t esc_len;
};

/* ===========================================================================
 * Keyboard parser — translate raw stdin bytes (after escape processing)
 * to NetSurf input_key codes / Unicode codepoints.
 *
 * NetSurf accepts UCS-4 codepoints in browser_window_key_press(). For
 * navigation keys we use the NS_KEY_* enum values from netsurf/keypress.h
 * (NetSurf treats those low-numeric values specially). Printable ASCII
 * / UTF-8 are passed straight through as the codepoint.
 * ===========================================================================*/

/* CSI sequence (ESC [ ... <final>) → NS_KEY_*. Returns 0 if unhandled
 * (caller skips, doesn't pollute the page with garbage). */
static uint32_t csi_to_ns_key(const char *seq, size_t len)
{
	if (len < 3 || seq[0] != 0x1b || seq[1] != '[') return 0;
	char final = seq[len - 1];

	if (len == 3) {
		switch (final) {
		case 'A': return NS_KEY_UP;
		case 'B': return NS_KEY_DOWN;
		case 'C': return NS_KEY_RIGHT;
		case 'D': return NS_KEY_LEFT;
		case 'H': return NS_KEY_LINE_START;  /* xterm Home */
		case 'F': return NS_KEY_LINE_END;    /* xterm End */
		}
		return 0;
	}

	if (final == '~') {
		int n = 0;
		for (size_t i = 2; i < len - 1; i++) {
			if (seq[i] < '0' || seq[i] > '9') return 0;
			n = n * 10 + (seq[i] - '0');
		}
		switch (n) {
		case 1: return NS_KEY_LINE_START;    /* Home (linux console) */
		case 3: return NS_KEY_DELETE_RIGHT;  /* Delete */
		case 4: return NS_KEY_LINE_END;      /* End (linux console) */
		case 5: return NS_KEY_PAGE_UP;
		case 6: return NS_KEY_PAGE_DOWN;
		}
	}
	return 0;
}

/* Decode a UTF-8 codepoint starting at s. Returns the number of bytes
 * consumed (0 if the sequence is incomplete and we should buffer for the
 * next read). On invalid lead bytes we return 1 with the raw byte so the
 * stream advances rather than wedging. */
static size_t utf8_decode(const unsigned char *s, size_t n, uint32_t *cp)
{
	if (n == 0) return 0;
	unsigned char c = s[0];
	if (c < 0x80) { *cp = c; return 1; }
	int trail;
	if      ((c & 0xE0) == 0xC0) { *cp = c & 0x1F; trail = 1; }
	else if ((c & 0xF0) == 0xE0) { *cp = c & 0x0F; trail = 2; }
	else if ((c & 0xF8) == 0xF0) { *cp = c & 0x07; trail = 3; }
	else                         { *cp = c;        return 1; }
	if (n < (size_t)(1 + trail)) return 0;
	for (int i = 0; i < trail; i++) {
		unsigned char t = s[1 + i];
		if ((t & 0xC0) != 0x80) { *cp = c; return 1; }
		*cp = (*cp << 6) | (t & 0x3F);
	}
	return 1 + trail;
}

/* yface raw-byte callback — keystrokes + xterm CSI sequences. */
static void on_raw(void *user, const char *bytes, size_t n)
{
	struct ev_state *st = user;

	for (size_t i = 0; i < n;) {
		unsigned char c = (unsigned char)bytes[i];

		/* Inside an in-progress ESC sequence: keep eating until we
		 * see the final byte of a CSI (0x40..0x7e), then dispatch. */
		if (st->esc_len > 0) {
			if (st->esc_len < sizeof(st->esc_buf))
				st->esc_buf[st->esc_len++] = (char)c;
			i++;

			int complete = 0;
			if (st->esc_len >= 3 && st->esc_buf[1] == '[' &&
			    c >= 0x40 && c <= 0x7e) {
				complete = 1;
			} else if (st->esc_len >= 2 && st->esc_buf[1] != '[' &&
				   st->esc_buf[1] != 'O') {
				/* Bare ESC followed by a non-CSI char — treat
				 * as ESC alone and re-feed the second byte. */
				yetty_ynetsurf_key_press(st->ns, NS_KEY_ESCAPE);
				st->dirty = 1;
				unsigned char second = (unsigned char)st->esc_buf[1];
				st->esc_len = 0;
				if (second != 0x1b) {
					yetty_ynetsurf_key_press(st->ns, second);
				} else {
					st->esc_buf[0] = 0x1b;
					st->esc_len = 1;
				}
				continue;
			}

			if (complete) {
				uint32_t k = csi_to_ns_key(st->esc_buf, st->esc_len);
				if (k != 0) {
					yetty_ynetsurf_key_press(st->ns, k);
					st->dirty = 1;
				}
				st->esc_len = 0;
			} else if (st->esc_len >= sizeof(st->esc_buf)) {
				/* Overflow — give up on this sequence. */
				st->esc_len = 0;
			}
			continue;
		}

		/* Control keys we intercept (don't forward to NetSurf). */
		if (c == 0x03 || c == 0x11) {  /* Ctrl-C / Ctrl-Q */
			st->want_quit = 1;
			i++;
			continue;
		}

		if (c == 0x1b) {  /* ESC — open accumulator */
			st->esc_buf[0] = 0x1b;
			st->esc_len = 1;
			i++;
			continue;
		}

		/* Map a few raw control bytes to NetSurf's enum, otherwise
		 * pass through as a Unicode codepoint (UTF-8 decoded). */
		uint32_t key = 0;
		size_t consumed = 1;
		switch (c) {
		case 0x08: case 0x7f: key = NS_KEY_DELETE_LEFT; break;
		case '\t':            key = NS_KEY_TAB;         break;
		case '\r':            key = NS_KEY_CR;          break;
		case '\n':            key = NS_KEY_NL;          break;
		default:
			if (c >= 0x20 && c < 0x80) {
				key = c;
			} else {
				consumed = utf8_decode((const unsigned char *)bytes + i,
						       n - i, &key);
				if (consumed == 0) {
					/* Incomplete UTF-8 at end of read —
					 * we discard rather than keep state
					 * across reads. Acceptable for ASCII
					 * dominant input; UTF-8 buffering
					 * across reads is a future polish. */
					return;
				}
			}
			break;
		}
		if (key != 0) {
			yetty_ynetsurf_key_press(st->ns, key);
			st->dirty = 1;
		}
		i += consumed;
	}
}

/* yface OSC-envelope callback — inbound mouse/key/resize events from yterm.
 *
 * We accept BOTH pane-wide (YETTY_OSC_SC_CLIENT_INPUT_*) and
 * figure-tagged (YETTY_OSC_SC_CLIENT_INPUT_FIGURE_*) variants so the
 * same handlers work whether the subscriber is registered as a card
 * or via YETTY_OSC_CS_CLIENT_INPUT_SUB. ynetsurf uses the latter; the
 * figure-tagged OSCs are wired purely so a future "ynetsurf as a card"
 * experiment doesn't need code changes here. */
static void on_osc(void *user, int osc_code,
		   const uint8_t *args, size_t args_len,
		   const uint8_t *payload, size_t payload_len)
{
	(void)args; (void)args_len;
	struct ev_state *st = user;

	if (osc_code == YETTY_OSC_SC_CLIENT_INPUT_FIGURE_MOUSE || osc_code == YETTY_OSC_SC_CLIENT_INPUT_MOUSE) {
		if (payload_len < sizeof(struct yetty_client_input_mouse))
			return;
		const struct yetty_client_input_mouse *m =
			(const struct yetty_client_input_mouse *)payload;
		switch (m->kind) {
		case YETTY_YMGUI_INPUT_MOUSE_BUTTON: {
			int btn = (m->button == 1) ? 2 : 1;  /* GLFW 1=right -> NS 2 */
			if (m->pressed)
				yetty_ynetsurf_mouse_click(st->ns,
					(int)m->x, (int)m->y, btn);
			else
				yetty_ynetsurf_mouse_release(st->ns,
					(int)m->x, (int)m->y, btn);
			st->dirty = 1;
			break;
		}
		case YETTY_YMGUI_INPUT_MOUSE_POS:
			yetty_ynetsurf_mouse_move(st->ns,
				(int)m->x, (int)m->y);
			st->dirty = 1;
			break;
		case YETTY_YMGUI_INPUT_MOUSE_WHEEL:
			/* Positive wheel_dy = wheel up = scroll content up
			 * = decrease scroll_y. Multiply for terminal feel. */
			yetty_ynetsurf_scroll(st->ns, 0,
				(int)(-m->wheel_dy * 60.0f));
			st->dirty = 1;
			break;
		}
		return;
	}

	if (osc_code == YETTY_OSC_SC_CLIENT_INPUT_FIGURE_RESIZE || osc_code == YETTY_OSC_SC_CLIENT_INPUT_RESIZE) {
		/* yterm replies with the resolved pane pixel size after our
		 * subscribe (or per-card after CARD_PLACE) — the natural
		 * moment to (re)size the NetSurf viewport to match. Same
		 * handler also fires when the user resizes the host terminal. */
		if (payload_len < sizeof(struct yetty_client_input_resize))
			return;
		const struct yetty_client_input_resize *r =
			(const struct yetty_client_input_resize *)payload;
		int w = (int)r->width;
		int h = (int)r->height;
		if (w > 0 && h > 0) {
			yetty_ynetsurf_set_size(st->ns, w, h);
			st->dirty = 1;
		}
		return;
	}

	if (osc_code == YETTY_OSC_SC_CLIENT_INPUT_FIGURE_KEY || osc_code == YETTY_OSC_SC_CLIENT_INPUT_KEY) {
		if (payload_len < sizeof(struct yetty_client_input_key))
			return;
		const struct yetty_client_input_key *k =
			(const struct yetty_client_input_key *)payload;
		if (k->kind == YETTY_YMGUI_INPUT_KEY_CHAR && k->codepoint) {
			yetty_ynetsurf_key_press(st->ns, k->codepoint);
			st->dirty = 1;
		}
		return;
	}
}

/* ===========================================================================
 * Terminal-wide input subscription (YETTY_OSC_CS_CLIENT_INPUT_SUB).
 *
 * ynetsurf is not card-shaped — it draws into the whole pane, not into a
 * registered card rect. The TERM_INPUT_SUB channel gives us pane-wide
 * mouse + keyboard events without entering the card hit table:
 *   subscribe → yterm emits SC_TERM_RESIZE with the pane pixel size,
 *               then SC_TERM_MOUSE / SC_TERM_KEY for every event.
 * ===========================================================================*/

static void term_input_subscribe(uint32_t flags)
{
	struct yetty_client_input_sub msg = {
		.magic = YETTY_CLIENT_INPUT_SUB_MAGIC,
		.version = YMGUI_WIRE_VERSION,
		.flags = flags,
		._pad0 = 0,
	};
	(void)emit_envelope(YETTY_OSC_CS_CLIENT_INPUT_SUB, /*compressed=*/0,
			    NULL, 0, &msg, sizeof(msg));
}

/* ===========================================================================
 * Interactive event loop. Returns 0 on normal exit (Ctrl-C / EOF).
 * ===========================================================================*/

static int interactive_loop(struct yetty_ynetsurf *ns)
{
	struct ev_state st = { .ns = ns };

	struct yetty_yface_ptr_result yr = yetty_yface_create();
	if (YETTY_IS_ERR(yr)) {
		fprintf(stderr, "yface_create: %s\n", yr.error.msg);
		return 1;
	}
	struct yetty_yface *yface = yr.value;
	yetty_yface_set_handlers(yface, on_osc, on_raw, &st);

	signal(SIGINT,  on_signal);
	signal(SIGTERM, on_signal);
	signal(SIGHUP,  on_signal);

	if (tty_raw() < 0) {
		fprintf(stderr, "ynetsurf: cannot put stdin into raw mode\n");
		yetty_yface_destroy(yface);
		return 1;
	}

	/* Subscribe to terminal-wide mouse + keyboard via the new
	 * YETTY_OSC_CS_CLIENT_INPUT_SUB channel — pane-pixel events,
	 * figure_id=0, fires regardless of card hit-test. yterm replies with
	 * a YETTY_OSC_SC_CLIENT_INPUT_RESIZE giving the pane pixel size; our
	 * on_osc handler picks that up and (re)sizes the NetSurf viewport. */
	term_input_subscribe(YETTY_CLIENT_INPUT_SUB_MOUSE_CLICK |
			     YETTY_CLIENT_INPUT_SUB_MOUSE_MOVE  |
			     YETTY_CLIENT_INPUT_SUB_MOUSE_WHEEL |
			     YETTY_CLIENT_INPUT_SUB_KEY);
	fflush(stdout);

	(void)redraw_and_push(ns);

	char buf[8192];
	while (!signal_quit && !st.want_quit) {
		int sched_ms = yetty_ynetsurf_pump(ns);
		if (sched_ms < 0) sched_ms = 100;  /* idle poll cap */

		fd_set rfds, wfds, efds;
		FD_ZERO(&rfds); FD_ZERO(&wfds); FD_ZERO(&efds);
		FD_SET(STDIN_FILENO, &rfds);
		int max_fd = STDIN_FILENO;
		int curl_max = -1;
		fetch_fdset(&rfds, &wfds, &efds, &curl_max);
		if (curl_max > max_fd) max_fd = curl_max;

		struct timeval tv = {
			.tv_sec = sched_ms / 1000,
			.tv_usec = (sched_ms % 1000) * 1000,
		};
		int rdy = select(max_fd + 1, &rfds, &wfds, &efds, &tv);
		if (rdy < 0) {
			if (errno == EINTR) continue;
			break;
		}

		st.dirty = 0;

		if (FD_ISSET(STDIN_FILENO, &rfds)) {
			ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
			if (n > 0) {
				(void)yetty_yface_feed_bytes(yface, buf, (size_t)n);
			} else if (n == 0 && !isatty(STDIN_FILENO)) {
				break;  /* EOF on a pipe */
			}
		}

		if (st.dirty)
			(void)redraw_and_push(ns);
	}

	/* Unsubscribe so the user's next foreground program doesn't inherit
	 * the term-wide forwarding state. */
	term_input_subscribe(0);
	fflush(stdout);

	yetty_yface_destroy(yface);
	return 0;
}

/* ===========================================================================
 * main
 * ===========================================================================*/

int main(int argc, char **argv)
{
	/* Default mode: interactive when both stdin and stdout are terminals
	 * (the natural "ynetsurf <url>" case from a yetty pane). When piped
	 * to a file or another process, fall back to one-shot. --once forces
	 * one-shot even from a TTY (for scripting); --interactive forces it
	 * even when stdin/stdout aren't TTYs (rare). */
	int interactive = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
	int osc = isatty(STDOUT_FILENO) ? 1 : 0;
	int width = 1024, height = 768;
	const char *url = NULL;
	int wait_ms = 4000;

	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		if      (!strcmp(a, "--osc"))         osc = 1;
		else if (!strcmp(a, "--raw"))         osc = 0;
		else if (!strcmp(a, "--once"))        interactive = 0;
		else if (!strcmp(a, "--interactive")) interactive = 1;
		else if (!strcmp(a, "-w")     && i + 1 < argc) width  = atoi(argv[++i]);
		else if (!strcmp(a, "-H")     && i + 1 < argc) height = atoi(argv[++i]);
		else if (!strcmp(a, "--wait") && i + 1 < argc) wait_ms = atoi(argv[++i]);
		else if (a[0] != '-') url = a;
	}

	if (url == NULL) {
		fprintf(stderr,
			"usage: %s [--once|--interactive] [--osc|--raw]\n"
			"            [-w W] [-H H] [--wait MS] <url>\n"
			"\n"
			"  default: interactive when stdin+stdout are TTYs,\n"
			"           one-shot otherwise. --osc/--raw apply only\n"
			"           in one-shot mode (interactive always uses OSC).\n",
			argv[0]);
		return 2;
	}

	struct yetty_ynetsurf_config cfg = {
		.width = width, .height = height, .resource_path = NULL,
	};
	struct yetty_ynetsurf_ptr_result r = yetty_ynetsurf_create(&cfg);
	if (YETTY_IS_ERR(r)) {
		fprintf(stderr, "ynetsurf_create failed: %s\n", r.error.msg);
		return 1;
	}
	struct yetty_ynetsurf *ns = r.value;

	yetty_ynetsurf_set_size(ns, width, height);

	struct yetty_ycore_void_result nv = yetty_ynetsurf_navigate(ns, url);
	if (YETTY_IS_ERR(nv)) {
		fprintf(stderr, "navigate failed: %s\n", nv.error.msg);
		yetty_ynetsurf_destroy(ns);
		return 1;
	}

	wait_for_layout(ns, wait_ms);

	int rc;
	if (interactive) {
		rc = interactive_loop(ns);
	} else {
		struct yetty_ydraw_drawable_list_result br =
			yetty_ydraw_drawable_list_config_buffer_create(NULL);
		if (YETTY_IS_ERR(br)) {
			fprintf(stderr, "buffer_create failed: %s\n", br.error.msg);
			yetty_ynetsurf_destroy(ns);
			return 1;
		}
		struct yetty_ydraw_drawable_list *buf = br.value;
		struct yetty_ycore_void_result rd = yetty_ynetsurf_redraw(ns, buf);
		if (YETTY_IS_ERR(rd)) {
			fprintf(stderr, "redraw failed: %s\n", rd.error.msg);
			yetty_ydraw_drawable_list_destroy(buf);
			yetty_ynetsurf_destroy(ns);
			return 1;
		}
		const uint8_t *bytes = NULL;
		size_t blen = yetty_ydraw_drawable_list_serialize(buf, &bytes);
		if (osc)
			(void)emit_bin_osc(bytes, blen);
		else
			fwrite(bytes, 1, blen, stdout);
		fflush(stdout);
		yetty_ydraw_drawable_list_destroy(buf);
		rc = 0;
	}

	yetty_ynetsurf_destroy(ns);
	return rc;
}
