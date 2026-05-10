/*
 * yzoo — animated control-point zoo frontend.
 *
 * The yetty_yzoo library produces ypaint primitives; this tool drives time,
 * owns a yetty_ypaint_core_buffer, and emits OSC 600001 (bin) per frame so
 * the host pane's ypaint-layer redraws. The bin payload's first prim is
 * CMD_ZERO (added by the renderer), which clears the canvas + resets the
 * cursor in the same envelope as the new prims — same flicker-free pattern
 * as ygui.
 *
 * Modeled on tools/ymaze/main.c. There is no card abstraction in new yetty —
 * the zoo fills the whole pane.
 *
 * Subscribes to terminal-wide input so the scene resizes when the pane
 * resizes and so 'q' / ESC / Ctrl-C work both via raw TTY bytes and via wire
 * key events.
 *
 * Defaults: animate at ~30 fps, never stop (always-animating scene).
 */

#include <yetty/yzoo/yzoo.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yface/yface.h>
#include <yetty/ymgui/wire.h>
#include <yetty/ypaint-core/buffer.h>
#include <yetty/ypaint-core/cmds.h>
#include <yetty/yterm/osc-codes.h>

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/*=============================================================================
 * Output side — emit OSC envelopes via yface_emit.
 *===========================================================================*/

static struct yetty_ycore_void_result
emit_envelope(int osc_code, int compressed,
	      const void *args, size_t args_len,
	      const void *body, size_t body_len)
{
	struct yetty_ycore_buffer env = {0};
	struct yetty_ycore_void_result r = yetty_yface_emit(
		osc_code, compressed, args, args_len, body, body_len, &env);
	if (YETTY_IS_OK(r) && env.size > 0)
		fwrite(env.data, 1, env.size, stdout);
	yetty_ycore_buffer_destroy(&env);
	YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "emit_envelope: yface_emit failed");
	return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result emit_clear(void)
{
	struct yetty_ycore_void_result r =
		emit_envelope(YETTY_OSC_YPAINT_CLEAR, 0, NULL, 0, NULL, 0);
	YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "emit_clear");
	return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result
emit_bin_serialized(struct yetty_ypaint_core_buffer *buf)
{
	const uint8_t *raw = NULL;
	size_t raw_size = yetty_ypaint_core_buffer_serialize(buf, &raw);
	if (raw_size == 0 || !raw)
		return YETTY_ERR(yetty_ycore_void, "emit_bin_serialized: empty buffer");

	struct yetty_yface_bin_meta meta = {
		.magic = YETTY_YFACE_BIN_MAGIC,
		.version = YETTY_YFACE_BIN_VERSION,
		.compressed = YETTY_YFACE_COMP_LZ4F,
		.compression_algo = 0,
		.raw_size = raw_size,
		.reserved = {0, 0},
	};
	struct yetty_ycore_void_result r =
		emit_envelope(YETTY_OSC_YPAINT_BIN, /*compressed=*/1,
			      &meta, sizeof(meta), raw, raw_size);
	YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "emit_bin_serialized");
	return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result term_input_subscribe(uint32_t flags)
{
	struct yetty_ymgui_wire_term_input_sub msg = {
		.magic = YMGUI_WIRE_MAGIC_TERM_INPUT_SUB,
		.version = YMGUI_WIRE_VERSION,
		.flags = flags,
		._pad0 = 0,
	};
	struct yetty_ycore_void_result r =
		emit_envelope(YMGUI_OSC_CS_TERM_INPUT_SUB, /*compressed=*/0,
			      NULL, 0, &msg, sizeof(msg));
	YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "term_input_subscribe");
	return YETTY_OK_VOID();
}

/*=============================================================================
 * TTY raw mode + alternate screen buffer.
 *===========================================================================*/

static struct termios saved_tty;
static int tty_active = 0;
static int alt_screen_active = 0;

static void alt_screen_leave(void)
{
	if (alt_screen_active) {
		static const char seq[] = "\033[?1049l";
		ssize_t w = write(STDOUT_FILENO, seq, sizeof(seq) - 1);
		(void)w;
		alt_screen_active = 0;
	}
}

static void alt_screen_enter(void)
{
	if (!isatty(STDOUT_FILENO))
		return;
	static const char seq[] = "\033[?1049h";
	ssize_t w = write(STDOUT_FILENO, seq, sizeof(seq) - 1);
	(void)w;
	alt_screen_active = 1;
	atexit(alt_screen_leave);
}

static void tty_restore(void)
{
	if (tty_active) {
		tcsetattr(STDIN_FILENO, TCSANOW, &saved_tty);
		tty_active = 0;
	}
}

static int tty_raw(void)
{
	if (!isatty(STDIN_FILENO))
		return 0;
	if (tcgetattr(STDIN_FILENO, &saved_tty) < 0)
		return -1;
	struct termios raw = saved_tty;
	cfmakeraw(&raw);
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 0;
	if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) < 0)
		return -1;
	tty_active = 1;
	atexit(tty_restore);
	return 0;
}

static volatile sig_atomic_t signal_quit = 0;

YETTY_EXTERNAL_CALLBACK
static void on_signal(int sig) { (void)sig; signal_quit = 1; }

/*=============================================================================
 * Time helper — monotonic seconds since first call.
 *===========================================================================*/

static float monotonic_now(void)
{
	static int initialized = 0;
	static struct timespec start;
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	if (!initialized) {
		start = ts;
		initialized = 1;
	}
	float sec = (float)(ts.tv_sec - start.tv_sec);
	sec += (float)(ts.tv_nsec - start.tv_nsec) / 1e9f;
	return sec;
}

/*=============================================================================
 * App state — collected so yface callbacks can mutate it via void *user.
 *===========================================================================*/

struct yzoo_app {
	struct yetty_yzoo               *zoo;
	struct yetty_ypaint_core_buffer *buf;
	bool want_quit;
};

static struct yetty_ycore_void_result redraw(struct yzoo_app *app)
{
	float t = monotonic_now();
	struct yetty_ycore_void_result r = yetty_yzoo_render(app->zoo, app->buf, t);
	YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "redraw: render failed");
	struct yetty_ycore_void_result er = emit_bin_serialized(app->buf);
	YETTY_RETURN_IF_ERR(yetty_ycore_void, er, "redraw: emit failed");
	fflush(stdout);
	return YETTY_OK_VOID();
}

/*=============================================================================
 * Key handling — codepoint dispatch, shared by raw bytes + wire CHAR events.
 *===========================================================================*/

static void on_key_codepoint(struct yzoo_app *app, uint32_t cp)
{
	switch (cp) {
	case 'q': case 'Q':
	case 0x03:    /* Ctrl-C */
	case 0x1b:    /* ESC */
		app->want_quit = 1;
		break;
	default: break;
	}
}

/*=============================================================================
 * yface callbacks.
 *===========================================================================*/

YETTY_EXTERNAL_CALLBACK
static void on_osc(void *user, int osc_code,
		   const uint8_t *args, size_t args_len,
		   const uint8_t *payload, size_t payload_len)
{
	(void)args; (void)args_len;
	struct yzoo_app *app = user;

	if (osc_code == YMGUI_OSC_SC_KEY || osc_code == YMGUI_OSC_SC_TERM_KEY) {
		if (payload_len < sizeof(struct yetty_ymgui_wire_input_key))
			return;
		const struct yetty_ymgui_wire_input_key *k =
			(const struct yetty_ymgui_wire_input_key *)payload;
		if (k->kind == YETTY_YMGUI_INPUT_KEY_CHAR && k->codepoint)
			on_key_codepoint(app, k->codepoint);
		return;
	}
}

YETTY_EXTERNAL_CALLBACK
static void on_raw(void *user, const char *bytes, size_t n)
{
	struct yzoo_app *app = user;
	for (size_t i = 0; i < n; i++)
		on_key_codepoint(app, (unsigned char)bytes[i]);
}

/*=============================================================================
 * CLI parsing — same flag set as the C++ ydraw-zoo argv parser, plus our
 * frontend extras (-w/-H/--seed/--help).
 *===========================================================================*/

static uint32_t parse_color(const char *s)
{
	if (!s || !*s)
		return 0;
	if ((s[0] == '0') && (s[1] == 'x' || s[1] == 'X'))
		s += 2;
	return (uint32_t)strtoul(s, NULL, 16);
}

static int parse_radius_pair(const char *s, float *out_min, float *out_max)
{
	const char *comma = strchr(s, ',');
	if (!comma)
		return -1;
	char buf[64];
	size_t lo_len = (size_t)(comma - s);
	if (lo_len >= sizeof(buf))
		return -1;
	memcpy(buf, s, lo_len);
	buf[lo_len] = '\0';
	*out_min = (float)atof(buf);
	*out_max = (float)atof(comma + 1);
	if (*out_max < *out_min) {
		float t = *out_max;
		*out_max = *out_min;
		*out_min = t;
	}
	return 0;
}

static void usage(FILE *out, const char *prog)
{
	fprintf(out,
		"Usage: %s [options]\n"
		"\n"
		"Animated control-point zoo — emits ypaint OSC envelopes to stdout.\n"
		"\n"
		"Zoo options:\n"
		"  -n, --points N         target control points (2..100, default 20)\n"
		"  -k, --connections K    target connections per CP (1..10, default 3)\n"
		"  -g, --growth F         outward drift rate (0.05..3.0, default 0.30)\n"
		"      --max-dist F       max connection distance (20..600, default 280)\n"
		"      --marker-size F    CP marker size (0.5..20, default 3.0)\n"
		"      --stroke-min F     min stroke width (0.1..10, default 0.5)\n"
		"      --stroke-max F     max stroke width (0.5..20, default 2.5)\n"
		"      --curve-ratio F    fraction of curved connections (0..1, default 0.80)\n"
		"      --bezier-segs N   segments per bezier curve (1..32, default 12)\n"
		"      --spawn-radius MIN,MAX  spawn radius range (default 20,140)\n"
		"      --bg-color HEX     advisory bg (default 0xFF1A1A2E)\n"
		"\n"
		"Frontend options:\n"
		"  -w, --width PX         scene width fallback when host pane unknown (default 600)\n"
		"  -H, --height PX        scene height fallback when host pane unknown (default 400)\n"
		"      --seed N           RNG seed (default = monotonic clock)\n"
		"  -h, --help             show this help\n"
		"\n"
		"Interactive controls:\n"
		"  q / Q / ESC / Ctrl-C   quit\n",
		prog);
}

int main(int argc, char **argv)
{
	struct yetty_yzoo_config cfg = yetty_yzoo_config_default();
	uint32_t seed = 0;

	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
			usage(stdout, argv[0]);
			return 0;
		}
		if ((!strcmp(a, "-n") || !strcmp(a, "--points")) && i + 1 < argc) {
			cfg.target_points = (uint32_t)strtoul(argv[++i], NULL, 10);
		} else if ((!strcmp(a, "-k") || !strcmp(a, "--connections"))
			   && i + 1 < argc) {
			cfg.target_conns_per_cp = (uint32_t)strtoul(argv[++i], NULL, 10);
		} else if ((!strcmp(a, "-g") || !strcmp(a, "--growth"))
			   && i + 1 < argc) {
			cfg.growth_rate = (float)atof(argv[++i]);
		} else if (!strcmp(a, "--max-dist") && i + 1 < argc) {
			cfg.max_connection_dist = (float)atof(argv[++i]);
		} else if (!strcmp(a, "--marker-size") && i + 1 < argc) {
			cfg.cp_marker_size = (float)atof(argv[++i]);
		} else if (!strcmp(a, "--stroke-min") && i + 1 < argc) {
			cfg.stroke_min = (float)atof(argv[++i]);
		} else if (!strcmp(a, "--stroke-max") && i + 1 < argc) {
			cfg.stroke_max = (float)atof(argv[++i]);
		} else if (!strcmp(a, "--curve-ratio") && i + 1 < argc) {
			cfg.curve_ratio = (float)atof(argv[++i]);
		} else if (!strcmp(a, "--bezier-segs") && i + 1 < argc) {
			cfg.bezier_segments = (uint32_t)strtoul(argv[++i], NULL, 10);
		} else if (!strcmp(a, "--spawn-radius") && i + 1 < argc) {
			if (parse_radius_pair(argv[++i], &cfg.spawn_radius_min,
					      &cfg.spawn_radius_max) < 0) {
				fprintf(stderr, "yzoo: --spawn-radius expects MIN,MAX\n");
				return 2;
			}
		} else if (!strcmp(a, "--bg-color") && i + 1 < argc) {
			cfg.bg_color = parse_color(argv[++i]);
		} else if ((!strcmp(a, "-w") || !strcmp(a, "--width"))
			   && i + 1 < argc) {
			cfg.scene_width = (float)atof(argv[++i]);
		} else if ((!strcmp(a, "-H") || !strcmp(a, "--height"))
			   && i + 1 < argc) {
			cfg.scene_height = (float)atof(argv[++i]);
		} else if (!strcmp(a, "--seed") && i + 1 < argc) {
			seed = (uint32_t)strtoul(argv[++i], NULL, 10);
		} else {
			fprintf(stderr, "yzoo: unknown option %s\n", a);
			usage(stderr, argv[0]);
			return 2;
		}
	}

	struct yetty_yzoo_ptr_result zr = yetty_yzoo_create(&cfg, seed);
	if (YETTY_IS_ERR(zr)) {
		fprintf(stderr, "yzoo: %s\n", zr.error.msg);
		yetty_ycore_error_destroy(zr.error);
		return 1;
	}

	struct yetty_ypaint_core_buffer_config bcfg = {
		.scene_min_x = 0.0f,
		.scene_min_y = 0.0f,
		.scene_max_x = cfg.scene_width,
		.scene_max_y = cfg.scene_height,
	};
	struct yetty_ypaint_core_buffer_result br =
		yetty_ypaint_core_buffer_config_buffer_create(&bcfg);
	if (YETTY_IS_ERR(br)) {
		fprintf(stderr, "yzoo: %s\n", br.error.msg);
		yetty_ycore_error_destroy(br.error);
		yetty_yzoo_destroy(zr.value);
		return 1;
	}

	struct yzoo_app app = {
		.zoo       = zr.value,
		.buf       = br.value,
		.want_quit = false,
	};

	struct yetty_yface_ptr_result yr = yetty_yface_create();
	if (YETTY_IS_ERR(yr)) {
		fprintf(stderr, "yzoo: yface_create: %s\n", yr.error.msg);
		yetty_ycore_error_destroy(yr.error);
		yetty_ypaint_core_buffer_destroy(app.buf);
		yetty_yzoo_destroy(app.zoo);
		return 1;
	}
	struct yetty_yface *yface = yr.value;
	yetty_yface_set_handlers(yface, on_osc, on_raw, &app);

	signal(SIGINT,  on_signal);
	signal(SIGTERM, on_signal);
	signal(SIGHUP,  on_signal);

	if (tty_raw() < 0) {
		fprintf(stderr, "yzoo: cannot put stdin into raw mode\n");
		yetty_yface_destroy(yface);
		yetty_ypaint_core_buffer_destroy(app.buf);
		yetty_yzoo_destroy(app.zoo);
		return 1;
	}

	alt_screen_enter();

	{
		struct yetty_ycore_void_result sr =
			term_input_subscribe(YETTY_YMGUI_TERM_SUB_KEY);
		if (YETTY_IS_ERR(sr)) {
			fprintf(stderr, "yzoo: subscribe: %s\n", sr.error.msg);
			yetty_ycore_error_destroy(sr.error);
		}
	}
	fflush(stdout);

	{
		struct yetty_ycore_void_result dr = redraw(&app);
		if (YETTY_IS_ERR(dr)) {
			fprintf(stderr, "yzoo: initial redraw: %s\n", dr.error.msg);
			yetty_ycore_error_destroy(dr.error);
		}
	}

	char ibuf[4096];
	while (!signal_quit && !app.want_quit) {
		fd_set rfds;
		FD_ZERO(&rfds);
		FD_SET(STDIN_FILENO, &rfds);
		struct timeval tv = {.tv_sec = 0, .tv_usec = 33000};
		int rdy = select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv);
		if (rdy < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (rdy > 0 && FD_ISSET(STDIN_FILENO, &rfds)) {
			ssize_t n = read(STDIN_FILENO, ibuf, sizeof(ibuf));
			if (n > 0) {
				struct yetty_ycore_void_result fr =
					yetty_yface_feed_bytes(yface, ibuf, (size_t)n);
				if (YETTY_IS_ERR(fr))
					yetty_ycore_error_destroy(fr.error);
			} else if (n == 0 && !isatty(STDIN_FILENO)) {
				break;
			}
		}
		struct yetty_ycore_void_result dr = redraw(&app);
		if (YETTY_IS_ERR(dr)) {
			fprintf(stderr, "yzoo: redraw: %s\n", dr.error.msg);
			yetty_ycore_error_destroy(dr.error);
			app.want_quit = true;
		}
	}

	{
		struct yetty_ycore_void_result sr = term_input_subscribe(0);
		if (YETTY_IS_ERR(sr))
			yetty_ycore_error_destroy(sr.error);
		struct yetty_ycore_void_result cr = emit_clear();
		if (YETTY_IS_ERR(cr))
			yetty_ycore_error_destroy(cr.error);
	}
	fflush(stdout);
	alt_screen_leave();

	yetty_yface_destroy(yface);
	yetty_ypaint_core_buffer_destroy(app.buf);
	yetty_yzoo_destroy(app.zoo);
	return 0;
}
