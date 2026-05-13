/*
 * yrich-runner — interactive loop for yrich documents → OSC 666674 canvas.
 *
 * Mirrors the protocol used by ygui_osc.c: emit --clear and --bin in a
 * single tick to replace the canvas contents with a fresh ypaint buffer.
 * Input events come back via OSC 777777/777778/777779/777780 plus regular
 * key bytes; the runner translates them into yrich document calls.
 */

#include "yrich-runner.h"

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yface/yface.h>
#include <yetty/ypaint-core/buffer.h>
#include <yetty/yterm/osc-codes.h>    /* YETTY_OSC_YPAINT_* */
#include <yetty/yrich/yrich-document.h>
#include <yetty/yrich/yrich-element.h>
#include <yetty/yrich/yrich-types.h>

#include <errno.h>
#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/*=============================================================================
 * Termios raw mode
 *===========================================================================*/

static struct termios g_orig_termios;
static bool g_raw_mode = false;

static void raw_mode_disable(void)
{
	if (g_raw_mode) {
		tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig_termios);
		g_raw_mode = false;
	}
}

void yrich_runner_raw_mode_enable(void)
{
	if (g_raw_mode)
		return;
	if (tcgetattr(STDIN_FILENO, &g_orig_termios) < 0)
		return;
	atexit(raw_mode_disable);

	struct termios raw = g_orig_termios;
	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	raw.c_oflag &= ~(OPOST);
	raw.c_cflag |= CS8;
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 0;
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
	g_raw_mode = true;
}

/*=============================================================================
 * OSC subscription / canvas emission
 *===========================================================================*/

static void write_all(const char *s, size_t n)
{
	ssize_t w = write(STDOUT_FILENO, s, n);
	(void)w;
}

#define OSC_CANVAS_CLEAR "600000"

void yrich_runner_subscribe(bool enable)
{
	const char *clicks = enable ? "\033[?1500h" : "\033[?1500l";
	const char *moves  = enable ? "\033[?1501h" : "\033[?1501l";
	const char *view   = enable ? "\033[?1502h" : "\033[?1502l";
	write_all(clicks, 8);
	write_all(moves, 8);
	write_all(view, 8);
}

static void emit_clear(void)
{
	static const char clear[] =
		"\033]" OSC_CANVAS_CLEAR ";;\033\\";
	write_all(clear, sizeof(clear) - 1);
}

static void emit_bin(const struct yetty_ypaint_core_buffer *buf)
{
	const uint8_t *raw = NULL;
	size_t raw_size = yetty_ypaint_core_buffer_serialize(
		(struct yetty_ypaint_core_buffer *)buf, &raw);
	if (raw_size == 0 || !raw) return;

	struct yetty_yface_bin_meta meta = {
		.magic            = YETTY_YFACE_BIN_MAGIC,
		.version          = YETTY_YFACE_BIN_VERSION,
		.compressed       = YETTY_YFACE_COMP_LZ4F,
		.compression_algo = 0,
		.raw_size         = raw_size,
		.reserved         = {0, 0},
	};
	struct yetty_ycore_buffer envelope = {0};
	struct yetty_ycore_void_result r = yetty_yface_emit(
		YETTY_OSC_YPAINT_BIN, /*compressed=*/1,
		&meta, sizeof(meta),
		raw, raw_size, &envelope);
	if (YETTY_IS_OK(r) && envelope.size > 0)
		write_all((const char *)envelope.data, envelope.size);
	yetty_ycore_buffer_destroy(&envelope);
}

void yrich_runner_emit(struct yrich_runner *r)
{
	if (!r || !r->doc || !r->buf)
		return;
	yetty_yrich_document_render(r->doc);
	emit_clear();
	emit_bin(r->buf);
}

/*=============================================================================
 * Lifecycle
 *===========================================================================*/

void yrich_runner_init(struct yrich_runner *r,
		       struct yetty_yrich_document *doc,
		       struct yetty_ypaint_core_buffer *buf)
{
	if (!r)
		return;
	memset(r, 0, sizeof(*r));
	r->doc = doc;
	r->buf = buf;
	r->zoom = 1.0f;
	r->mouse_button = YETTY_YRICH_MOUSE_LEFT;
}

void yrich_runner_fini(struct yrich_runner *r)
{
	if (!r)
		return;
	free(r->esc_buf);
	r->esc_buf = NULL;
	r->esc_buf_len = r->esc_buf_cap = 0;
}

/*=============================================================================
 * Time helper (monotonic ms, for double-click detection)
 *===========================================================================*/

static uint64_t now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

/*=============================================================================
 * Coordinate transform — display pixels → document scene
 *===========================================================================*/

static float scene_x(const struct yrich_runner *r, float dx)
{
	if (!r->have_pixel_size || r->display_w <= 0)
		return dx;
	float content_w = yetty_yrich_document_content_width(r->doc);
	float visible_w = content_w / (r->zoom > 0 ? r->zoom : 1.0f);
	return r->scroll_x + (dx / r->display_w) * visible_w;
}

static float scene_y(const struct yrich_runner *r, float dy)
{
	if (!r->have_pixel_size || r->display_h <= 0)
		return dy;
	float content_h = yetty_yrich_document_content_height(r->doc);
	float visible_h = content_h / (r->zoom > 0 ? r->zoom : 1.0f);
	return r->scroll_y + (dy / r->display_h) * visible_h;
}

/*=============================================================================
 * Tiny helpers — parsing semi-colon-separated OSC payloads
 *===========================================================================*/

static int split_semi(char *s, char *parts[], int max_parts)
{
	int n = 0;
	char *p = s;
	parts[n++] = p;
	while (*p && n < max_parts) {
		if (*p == ';') {
			*p = '\0';
			parts[n++] = p + 1;
		}
		p++;
	}
	return n;
}

/*=============================================================================
 * OSC handlers
 *===========================================================================*/

static void handle_osc_click(struct yrich_runner *r, char *payload)
{
	/* card; buttons; press; x; y — we ignore the leading "card" name as
	 * there's only one canvas per process. */
	char *parts[8];
	int n = split_semi(payload, parts, 8);
	if (n < 5)
		return;

	int buttons = atoi(parts[1]);
	int press = atoi(parts[2]);
	float x = strtof(parts[3], NULL);
	float y = strtof(parts[4], NULL);

	uint32_t button = YETTY_YRICH_MOUSE_LEFT;
	if (buttons & 2) button = YETTY_YRICH_MOUSE_RIGHT;
	if (buttons & 4) button = YETTY_YRICH_MOUSE_MIDDLE;

	float sx = scene_x(r, x);
	float sy = scene_y(r, y);
	struct yetty_yrich_input_mods mods = {0};

	if (press) {
		uint64_t t = now_ms();
		bool dbl = r->last_click_ms > 0 &&
			   (t - r->last_click_ms) < 300 &&
			   fabsf(r->last_click_x - sx) < 5.0f &&
			   fabsf(r->last_click_y - sy) < 5.0f;
		if (dbl) {
			yetty_yrich_document_on_mouse_double_click(
				r->doc, sx, sy, button, mods);
			r->last_click_ms = 0;
		} else {
			yetty_yrich_document_on_mouse_down(r->doc, sx, sy,
							   button, mods);
			r->last_click_ms = t;
			r->last_click_x = sx;
			r->last_click_y = sy;
		}
		r->mouse_down = true;
		r->mouse_button = button;
	} else {
		yetty_yrich_document_on_mouse_up(r->doc, sx, sy, button, mods);
		r->mouse_down = false;
	}
}

static void handle_osc_move(struct yrich_runner *r, char *payload)
{
	/* card; buttons; x; y */
	char *parts[8];
	int n = split_semi(payload, parts, 8);
	if (n < 4)
		return;

	float x = strtof(parts[2], NULL);
	float y = strtof(parts[3], NULL);
	float sx = scene_x(r, x);
	float sy = scene_y(r, y);
	struct yetty_yrich_input_mods mods = {0};

	if (r->mouse_down)
		yetty_yrich_document_on_mouse_drag(r->doc, sx, sy,
						   r->mouse_button, mods);
}

static void handle_osc_view(struct yrich_runner *r, char *payload)
{
	/* card; zoom; sx; sy */
	char *parts[8];
	int n = split_semi(payload, parts, 8);
	if (n < 4)
		return;
	r->zoom = strtof(parts[1], NULL);
	r->scroll_x = strtof(parts[2], NULL);
	r->scroll_y = strtof(parts[3], NULL);
}

static void handle_osc_pixel_size(struct yrich_runner *r, char *payload)
{
	/* card; w; h */
	char *parts[8];
	int n = split_semi(payload, parts, 8);
	if (n < 3)
		return;
	r->display_w = strtof(parts[1], NULL);
	r->display_h = strtof(parts[2], NULL);
	r->have_pixel_size = true;
}

static void handle_osc(struct yrich_runner *r, char *seq, size_t len)
{
	/* seq starts with ESC ] and ends with ESC \ or BEL. Strip the framing
	 * and split off the numeric code. */
	if (len < 4)
		return;
	char *p = seq + 2;            /* skip ESC ] */
	size_t n = len - 2;
	if (n >= 2 && p[n - 2] == '\033' && p[n - 1] == '\\') {
		p[n - 2] = '\0';
	} else if (n >= 1 && p[n - 1] == '\007') {
		p[n - 1] = '\0';
	} else {
		p[n] = '\0';
	}

	char *semi = strchr(p, ';');
	if (!semi)
		return;
	*semi = '\0';
	char *payload = semi + 1;
	int code = atoi(p);

	switch (code) {
	case 777777: handle_osc_click(r, payload); break;
	case 777778: handle_osc_move(r, payload); break;
	case 777779: handle_osc_view(r, payload); break;
	case 777780: handle_osc_pixel_size(r, payload); break;
	default:     break;
	}
}

/*=============================================================================
 * CSI handlers (arrow keys, function keys)
 *===========================================================================*/

static void handle_csi(struct yrich_runner *r, const char *seq, size_t len)
{
	if (len < 3)
		return;
	char cmd = seq[len - 1];

	struct yetty_yrich_input_mods mods = {0};
	/* Modifier-aware sequences look like "ESC [ 1 ; 5 C" — index [-2]. */
	if (len >= 5) {
		char m = seq[len - 2];
		if (m == '5') mods.ctrl = true;
		else if (m == '2') mods.shift = true;
	}

	uint32_t key = YETTY_YRICH_KEY_UNKNOWN;
	switch (cmd) {
	case 'A': key = YETTY_YRICH_KEY_UP; break;
	case 'B': key = YETTY_YRICH_KEY_DOWN; break;
	case 'C': key = YETTY_YRICH_KEY_RIGHT; break;
	case 'D': key = YETTY_YRICH_KEY_LEFT; break;
	case 'H': key = YETTY_YRICH_KEY_HOME; break;
	case 'F': key = YETTY_YRICH_KEY_END; break;
	case '~':
		if (memchr(seq, '3', len)) key = YETTY_YRICH_KEY_DELETE;
		break;
	default: break;
	}
	if (key != YETTY_YRICH_KEY_UNKNOWN)
		yetty_yrich_document_on_key_down(r->doc, key, mods);
}

/*=============================================================================
 * Plain-key dispatch
 *===========================================================================*/

static void handle_key_byte(struct yrich_runner *r, char c)
{
	if (c == 'q' || c == 'Q') {
		r->running = false;
		return;
	}

	struct yetty_yrich_input_mods mods = {0};

	/* Ctrl-A..Z arrives as 1..26. */
	if (c >= 1 && c <= 26) {
		mods.ctrl = true;
		uint32_t key = YETTY_YRICH_KEY_A + (uint32_t)(c - 1);
		yetty_yrich_document_on_key_down(r->doc, key, mods);
		return;
	}

	if (c == '\r' || c == '\n') {
		yetty_yrich_document_on_key_down(r->doc,
						 YETTY_YRICH_KEY_ENTER, mods);
		return;
	}
	if (c == '\t') {
		yetty_yrich_document_on_key_down(r->doc,
						 YETTY_YRICH_KEY_TAB, mods);
		return;
	}
	if (c == 127 || c == '\b') {
		yetty_yrich_document_on_key_down(r->doc,
						 YETTY_YRICH_KEY_BACKSPACE,
						 mods);
		return;
	}
	if ((unsigned char)c >= 32 && (unsigned char)c < 127) {
		char s[2] = { c, 0 };
		yetty_yrich_document_on_text_input(r->doc, s, 1);
	}
}

/*=============================================================================
 * Escape-sequence state machine
 *===========================================================================*/

static int esc_buf_push(struct yrich_runner *r, char c)
{
	if (r->esc_buf_len + 1 >= r->esc_buf_cap) {
		size_t nc = r->esc_buf_cap ? r->esc_buf_cap * 2 : 64;
		char *nb = realloc(r->esc_buf, nc);
		if (!nb)
			return -1;
		r->esc_buf = nb;
		r->esc_buf_cap = nc;
	}
	r->esc_buf[r->esc_buf_len++] = c;
	r->esc_buf[r->esc_buf_len] = '\0';
	return 0;
}

static void esc_buf_reset(struct yrich_runner *r)
{
	r->esc_buf_len = 0;
	if (r->esc_buf)
		r->esc_buf[0] = '\0';
	r->esc_state = 0;
}

static void process_byte(struct yrich_runner *r, char c)
{
	switch (r->esc_state) {
	case 0:
		if (c == '\033') {
			esc_buf_reset(r);
			esc_buf_push(r, c);
			r->esc_state = 1;
		} else {
			handle_key_byte(r, c);
		}
		break;
	case 1:
		esc_buf_push(r, c);
		if (c == ']')      r->esc_state = 2;
		else if (c == '[') r->esc_state = 3;
		else               esc_buf_reset(r);
		break;
	case 2:  /* OSC */
		esc_buf_push(r, c);
		if (c == '\007') {
			handle_osc(r, r->esc_buf, r->esc_buf_len);
			esc_buf_reset(r);
		} else if (c == '\\' && r->esc_buf_len >= 2 &&
			   r->esc_buf[r->esc_buf_len - 2] == '\033') {
			handle_osc(r, r->esc_buf, r->esc_buf_len);
			esc_buf_reset(r);
		}
		break;
	case 3:  /* CSI */
		esc_buf_push(r, c);
		if ((unsigned char)c >= 0x40 && (unsigned char)c <= 0x7E) {
			/* CSI body starts after "ESC [". */
			handle_csi(r, r->esc_buf + 2, r->esc_buf_len - 2);
			esc_buf_reset(r);
		}
		break;
	}
}

/*=============================================================================
 * Main loop
 *===========================================================================*/

struct yetty_ycore_void_result yrich_runner_loop(struct yrich_runner *r)
{
	if (!r || !r->doc || !r->buf)
		return YETTY_ERR(yetty_ycore_void,
				 "yrich-runner: missing doc/buf");

	r->running = true;

	/* Initial render. */
	yrich_runner_emit(r);

	if (r->dump_once)
		return YETTY_OK_VOID();

	while (r->running) {
		struct pollfd pfd = { .fd = STDIN_FILENO, .events = POLLIN };
		int pr = poll(&pfd, 1, 16);  /* ~60fps idle tick */
		if (pr < 0) {
			if (errno == EINTR)
				continue;
			return YETTY_ERR(yetty_ycore_void,
					 "yrich-runner: poll failed");
		}

		if (pr > 0 && (pfd.revents & POLLIN)) {
			char buf[4096];
			ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
			if (n == 0)  /* EOF */
				break;
			if (n < 0) {
				if (errno == EINTR || errno == EAGAIN)
					continue;
				return YETTY_ERR(yetty_ycore_void,
						 "yrich-runner: read failed");
			}
			for (ssize_t i = 0; i < n; i++)
				process_byte(r, buf[i]);
		}

		if (yetty_yrich_document_is_dirty(r->doc))
			yrich_runner_emit(r);
	}

	return YETTY_OK_VOID();
}
