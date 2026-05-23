/*
 * ymaze — animated maze frontend.
 *
 * The yetty_ymaze library produces ydraw primitives; this tool drives time,
 * owns a yetty_ydraw_draw_list, and emits OSC 600000 (clear) + OSC 600001
 * (bin) per frame so the host pane's ydraw-layer redraws.
 *
 * Modeled on tools/ymesh/main.c. There is no card abstraction in new yetty
 * — the maze fills the whole pane.
 *
 * Subscribe to terminal-wide input (key + resize rising-edge report) so the
 * scene resizes as the pane resizes and so 'r' / 'q' / '+' / '-' work both
 * via raw TTY bytes and via wire key events.
 *
 * Defaults: animate at ~30 fps, regenerate on finish.
 */

#include <yetty/ymaze/ymaze.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yface/yface.h>
#include <yetty/ymgui/wire.h>
#include <yetty/yterm/client-input.h>
#include <yetty/ydraw-core/draw-list.h>
#include <yetty/yterm/osc-codes.h>

#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <yetty/yplatform/compat.h> /* clock_gettime shim on MSVC */
#include <yetty/yplatform/tty.h>

/*=============================================================================
 * Output side — emit OSC envelopes via yface_emit.
 *===========================================================================*/

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

static int emit_clear(void)
{
	return emit_envelope(YETTY_OSC_YDRAW_CLEAR, 0, NULL, 0, NULL, 0);
}

static int emit_bin_serialized(struct yetty_ydraw_draw_list *buf)
{
	const uint8_t *raw = NULL;
	size_t raw_size = yetty_ydraw_draw_list_serialize(buf, &raw);
	if (raw_size == 0 || !raw)
		return 1;

	struct yetty_yface_bin_meta meta = {
		.magic = YETTY_YFACE_BIN_MAGIC,
		.version = YETTY_YFACE_BIN_VERSION,
		.compressed = YETTY_YFACE_COMP_LZ4F,
		.compression_algo = 0,
		.raw_size = raw_size,
		.reserved = {0, 0},
	};
	return emit_envelope(YETTY_OSC_YDRAW_BIN, /*compressed=*/1,
			     &meta, sizeof(meta), raw, raw_size);
}

/*=============================================================================
 * Terminal-wide input subscription — gets us pane pixel size on rising edge
 * (and on actual resize), plus key + mouse events. We only consume keys here,
 * but the resize emission rides on any subscription bit.
 *===========================================================================*/

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

/*=============================================================================
 * Alternate screen buffer (raw mode is handled by the platform tty API).
 *===========================================================================*/

static int alt_screen_active = 0;

static void alt_screen_leave(void)
{
	if (alt_screen_active) {
		static const char seq[] = "\033[?1049l";
		fwrite(seq, 1, sizeof(seq) - 1, stdout);
		fflush(stdout);
		alt_screen_active = 0;
	}
}

static void alt_screen_enter(void)
{
	if (!yetty_yplatform_tty_stdout_is_tty())
		return;
	static const char seq[] = "\033[?1049h";
	fwrite(seq, 1, sizeof(seq) - 1, stdout);
	fflush(stdout);
	alt_screen_active = 1;
	atexit(alt_screen_leave);
}

static volatile sig_atomic_t signal_quit = 0;
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

struct ymaze_app {
	struct yetty_ymaze              *maze;
	struct yetty_ydraw_draw_list *buf;

	float pane_w;
	float pane_h;
	bool  have_pane_size;

	bool dirty;
	bool want_quit;
};

static void apply_pane_size(struct ymaze_app *app, float w, float h)
{
	if (w < 1.0f || h < 1.0f)
		return;
	if (app->have_pane_size && w == app->pane_w && h == app->pane_h)
		return;
	app->pane_w = w;
	app->pane_h = h;
	app->have_pane_size = true;
	(void)yetty_ymaze_set_scene_size(app->maze, w, h);
	app->dirty = true;
}

/* CSI [H = cursor home (1,1); CSI [2J = clear entire screen. Sent before
 * every frame so any stray text-layer output (or pre-existing terminal
 * content under the alt screen) is wiped and the maze redraws over a
 * blank canvas. We send the whole maze every frame — optimisation later. */
static void emit_term_clear_home(void)
{
	static const char seq[] = "\033[H\033[2J";
	fwrite(seq, 1, sizeof(seq) - 1, stdout);
}

static void redraw(struct ymaze_app *app)
{
	float t = monotonic_now();
	bool regenerated = false;
	struct yetty_ycore_void_result r =
		yetty_ymaze_render(app->maze, app->buf, t, &regenerated);
	if (YETTY_IS_ERR(r)) {
		fprintf(stderr, "ymaze: render failed: %s\n", r.error.msg);
		yetty_ycore_error_destroy(r.error);
		return;
	}
	emit_term_clear_home();
	(void)emit_clear();
	(void)emit_bin_serialized(app->buf);
	fflush(stdout);
}

/*=============================================================================
 * Key handling — codepoint dispatch, shared by raw bytes + wire CHAR events.
 *===========================================================================*/

static void on_key_codepoint(struct ymaze_app *app, uint32_t cp)
{
	switch (cp) {
	case 'q': case 'Q':
	case 0x03:    /* Ctrl-C */
	case 0x1b:    /* ESC */
		app->want_quit = 1;
		break;
	case 'r': case 'R':
		(void)yetty_ymaze_regenerate(app->maze);
		app->dirty = true;
		break;
	case '+': case '=':
	case '-': case '_': {
		const struct yetty_ymaze_config *cur =
			yetty_ymaze_config_get(app->maze);
		if (!cur)
			break;
		struct yetty_ymaze_config nc = *cur;
		float factor = (cp == '+' || cp == '=') ? 1.25f : 0.8f;
		nc.actor_speed *= factor;
		/* Same clamps the library applies on create — re-create with
		 * the same maze layout. */
		if (nc.actor_speed < 0.5f)  nc.actor_speed = 0.5f;
		if (nc.actor_speed > 50.0f) nc.actor_speed = 50.0f;
		struct yetty_ymaze_ptr_result nr = yetty_ymaze_create(&nc, 0);
		if (YETTY_IS_OK(nr)) {
			yetty_ymaze_destroy(app->maze);
			app->maze = nr.value;
			if (app->have_pane_size)
				(void)yetty_ymaze_set_scene_size(app->maze,
								 app->pane_w,
								 app->pane_h);
			app->dirty = true;
		} else {
			yetty_ycore_error_destroy(nr.error);
		}
		break;
	}
	default: break;
	}
}

/*=============================================================================
 * yface callbacks.
 *===========================================================================*/

static void on_osc(void *user, int osc_code,
		   const uint8_t *args, size_t args_len,
		   const uint8_t *payload, size_t payload_len)
{
	(void)args; (void)args_len;
	struct ymaze_app *app = user;

	if (osc_code == YETTY_OSC_SC_CLIENT_INPUT_FIGURE_RESIZE
	    || osc_code == YETTY_OSC_SC_CLIENT_INPUT_RESIZE) {
		if (payload_len < sizeof(struct yetty_client_input_resize))
			return;
		const struct yetty_client_input_resize *r =
			(const struct yetty_client_input_resize *)payload;
		apply_pane_size(app, r->width, r->height);
		return;
	}

	if (osc_code == YETTY_OSC_SC_CLIENT_INPUT_FIGURE_KEY || osc_code == YETTY_OSC_SC_CLIENT_INPUT_KEY) {
		if (payload_len < sizeof(struct yetty_client_input_key))
			return;
		const struct yetty_client_input_key *k =
			(const struct yetty_client_input_key *)payload;
		if (k->kind == YETTY_YMGUI_INPUT_KEY_CHAR && k->codepoint)
			on_key_codepoint(app, k->codepoint);
		return;
	}
}

static void on_raw(void *user, const char *bytes, size_t n)
{
	struct ymaze_app *app = user;
	for (size_t i = 0; i < n; i++)
		on_key_codepoint(app, (unsigned char)bytes[i]);
}

/*=============================================================================
 * CLI parsing — same flags as the C++ MazeConfig parser, plus our extras.
 *===========================================================================*/

static uint32_t parse_color(const char *s)
{
	if (!s || !*s)
		return 0;
	if ((s[0] == '0') && (s[1] == 'x' || s[1] == 'X'))
		s += 2;
	return (uint32_t)strtoul(s, NULL, 16);
}

static void usage(FILE *out, const char *prog)
{
	fprintf(out,
		"Usage: %s [options]\n"
		"\n"
		"Animated maze demo — emits ydraw OSC envelopes to stdout.\n"
		"\n"
		"Maze options:\n"
		"  --cols N              maze columns (3..80, default 15)\n"
		"  --rows N              maze rows    (3..50, default 10)\n"
		"  --speed F             actor speed cells/sec (0.5..50, default 4.0)\n"
		"  --wall-width F        wall stroke width (0.3..5.0, default 1.5)\n"
		"  --wall-color HEX      AARRGGBB (default 0xFF808080)\n"
		"  --actor-color HEX     AARRGGBB (default 0xFF00CCFF)\n"
		"  --start-color HEX     AARRGGBB (default 0xFF00FF00)\n"
		"  --end-color HEX       AARRGGBB (default 0xFF0000FF)\n"
		"  --bg-color HEX        AARRGGBB (default 0xFF1A1A2E)\n"
		"\n"
		"Frontend options:\n"
		"  -w, --width PX        scene width  fallback when host pane unknown (default 600)\n"
		"  -H, --height PX       scene height fallback when host pane unknown (default 400)\n"
		"  --no-auto-regen       stop after first solve instead of regenerating\n"
		"  --seed N              RNG seed (default = monotonic clock)\n"
		"  -h, --help            show this help\n"
		"\n"
		"Interactive controls:\n"
		"  q / Q / ESC / Ctrl-C  quit\n"
		"  r / R                 regenerate maze now\n"
		"  + / =                 speed up actor\n"
		"  - / _                 slow down actor\n",
		prog);
}

int main(int argc, char **argv)
{
	struct yetty_ymaze_config cfg = yetty_ymaze_config_default();
	uint32_t seed = 0;

	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
			usage(stdout, argv[0]);
			return 0;
		}
		if (!strcmp(a, "--cols") && i + 1 < argc) {
			cfg.cols = (uint32_t)strtoul(argv[++i], NULL, 10);
		} else if (!strcmp(a, "--rows") && i + 1 < argc) {
			cfg.rows = (uint32_t)strtoul(argv[++i], NULL, 10);
		} else if (!strcmp(a, "--speed") && i + 1 < argc) {
			cfg.actor_speed = (float)atof(argv[++i]);
		} else if (!strcmp(a, "--wall-width") && i + 1 < argc) {
			cfg.wall_width = (float)atof(argv[++i]);
		} else if (!strcmp(a, "--wall-color") && i + 1 < argc) {
			cfg.wall_color = parse_color(argv[++i]);
		} else if (!strcmp(a, "--actor-color") && i + 1 < argc) {
			cfg.actor_color = parse_color(argv[++i]);
		} else if (!strcmp(a, "--start-color") && i + 1 < argc) {
			cfg.start_color = parse_color(argv[++i]);
		} else if (!strcmp(a, "--end-color") && i + 1 < argc) {
			cfg.end_color = parse_color(argv[++i]);
		} else if (!strcmp(a, "--bg-color") && i + 1 < argc) {
			cfg.bg_color = parse_color(argv[++i]);
		} else if ((!strcmp(a, "-w") || !strcmp(a, "--width"))
			   && i + 1 < argc) {
			cfg.scene_width = (float)atof(argv[++i]);
		} else if ((!strcmp(a, "-H") || !strcmp(a, "--height"))
			   && i + 1 < argc) {
			cfg.scene_height = (float)atof(argv[++i]);
		} else if (!strcmp(a, "--no-auto-regen")) {
			cfg.auto_regen = false;
		} else if (!strcmp(a, "--seed") && i + 1 < argc) {
			seed = (uint32_t)strtoul(argv[++i], NULL, 10);
		} else {
			fprintf(stderr, "ymaze: unknown option %s\n", a);
			usage(stderr, argv[0]);
			return 2;
		}
	}

	struct yetty_ymaze_ptr_result mr = yetty_ymaze_create(&cfg, seed);
	if (YETTY_IS_ERR(mr)) {
		fprintf(stderr, "ymaze: %s\n", mr.error.msg);
		yetty_ycore_error_destroy(mr.error);
		return 1;
	}

	struct yetty_ydraw_draw_list_config bcfg = {
		.scene_min_x = 0.0f,
		.scene_min_y = 0.0f,
		.scene_max_x = cfg.scene_width,
		.scene_max_y = cfg.scene_height,
	};
	struct yetty_ydraw_draw_list_result br =
		yetty_ydraw_draw_list_config_buffer_create(&bcfg);
	if (YETTY_IS_ERR(br)) {
		fprintf(stderr, "ymaze: %s\n", br.error.msg);
		yetty_ycore_error_destroy(br.error);
		yetty_ymaze_destroy(mr.value);
		return 1;
	}

	struct ymaze_app app = {
		.maze   = mr.value,
		.buf    = br.value,
		.pane_w = cfg.scene_width,
		.pane_h = cfg.scene_height,
		.have_pane_size = false,
		.dirty  = true,
		.want_quit = false,
	};

	struct yetty_yface_ptr_result yr = yetty_yface_create();
	if (YETTY_IS_ERR(yr)) {
		fprintf(stderr, "ymaze: yface_create: %s\n", yr.error.msg);
		yetty_ycore_error_destroy(yr.error);
		yetty_ydraw_draw_list_destroy(app.buf);
		yetty_ymaze_destroy(app.maze);
		return 1;
	}
	struct yetty_yface *yface = yr.value;
	yetty_yface_set_handlers(yface, on_osc, on_raw, &app);

	signal(SIGINT,  on_signal);
	signal(SIGTERM, on_signal);
#ifdef SIGHUP
	signal(SIGHUP,  on_signal);
#endif

	yetty_yplatform_tty_binary_io();
	if (yetty_yplatform_tty_set_raw() < 0) {
		fprintf(stderr, "ymaze: cannot put stdin into raw mode\n");
		yetty_yface_destroy(yface);
		yetty_ydraw_draw_list_destroy(app.buf);
		yetty_ymaze_destroy(app.maze);
		return 1;
	}
	atexit(yetty_yplatform_tty_restore);

	/* Switch to the alt screen so the maze runs over a fresh page and the
	 * user's prior terminal content is restored on exit. atexit handles
	 * the restore even on signal-driven shutdown. */
	alt_screen_enter();

	/* Subscribe to keys so we get rising-edge TERM_RESIZE with pane size,
	 * plus subsequent resizes and CHAR-coded keystrokes. */
	term_input_subscribe(YETTY_CLIENT_INPUT_SUB_KEY);
	fflush(stdout);

	/* Initial frame — uses CLI-supplied scene size; the rising-edge resize
	 * report (if any arrives) overrides it. */
	redraw(&app);
	app.dirty = false;

	char buf[4096];
	while (!signal_quit && !app.want_quit) {
		/* ~30 fps animation tick — drives time forward even with no
		 * input. */
		int rdy = yetty_yplatform_tty_stdin_wait(33);
		if (rdy < 0) {
			break;
		}
		if (rdy > 0) {
			int n = yetty_yplatform_tty_stdin_read(buf, sizeof(buf));
			if (n > 0) {
				(void)yetty_yface_feed_bytes(yface, buf, (size_t)n);
			} else if (n == 0 && !yetty_yplatform_tty_stdin_is_tty()) {
				break;
			}
		}
		/* Always redraw — the maze is animated, so each tick advances
		 * the actor regardless of input. */
		redraw(&app);
		app.dirty = false;
	}

	term_input_subscribe(0);
	(void)emit_clear();
	fflush(stdout);
	alt_screen_leave();

	yetty_yface_destroy(yface);
	yetty_ydraw_draw_list_destroy(app.buf);
	yetty_ymaze_destroy(app.maze);
	return 0;
}
