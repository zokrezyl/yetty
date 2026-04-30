/*
 * ycat - cat with MIME-dispatched ypaint rendering.
 *
 * For each positional input (file path, or "-" for stdin):
 *   1. read the bytes
 *   2. detect the type via libmagic + extension
 *   3. dispatch to a handler that returns a ypaint-core buffer (markdown,
 *      PDF for now — registry is open for more)
 *   4. emit an OSC 666674 sequence to stdout carrying the base64-encoded
 *      ypaint primitive bytes (consumed by the ypaint scrolling layer)
 *
 * If the handler dispatch yields no buffer (TEXT / UNKNOWN) the bytes are
 * streamed through unchanged. --raw forces pass-through regardless.
 *
 * Runs pure C. Args via <yetty/yplatform/getopt.h> (vendored NetBSD getopt,
 * works on Windows too).
 */

#include <yetty/ycat/ycat.h>

#include <yetty/yplatform/getopt.h>
#include <yetty/ycore/result.h>
#include <yetty/ypaint-core/buffer.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <io.h>
#define isatty _isatty
#else
#include <unistd.h>
#include <sys/ioctl.h>
#endif

/*=============================================================================
 * Options
 *===========================================================================*/

struct ycat_opts {
	int x, y;
	int width_cells;
	int height_cells;
	bool absolute;
	bool raw;
	bool force_ts;          /* --ts : force tree-sitter path */
	const char *force_type; /* name from --card/--type; NULL = autodetect */
	int sleep_after_ms;     /* --sleep-after: sleep N ms before exit
	                         * so a parent yetty has time to drain
	                         * the master PTY's tty_buffer before our
	                         * slave fd closes (workaround for libuv
	                         * giving up reads on POLLHUP). */
};

static int terminal_columns(void)
{
#ifdef __unix__
	struct winsize ws = {0};
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
		return ws.ws_col;
#endif
	return 80;
}

/* Detect whether ycat is running inside a yetty terminal. yetty sets
 * TERM_PROGRAM=yetty at the top of main() so every descendant inherits it. */
static bool in_yetty_terminal(void)
{
	const char *tp = getenv("TERM_PROGRAM");
	return tp && strcmp(tp, "yetty") == 0;
}

static void usage(FILE *out, const char *prog)
{
	fprintf(out,
		"Usage: %s [options] [file|url|-]...\n"
		"\n"
		"  Dispatch (TERM_PROGRAM=yetty distinguishes the two columns):\n"
		"\n"
		"    flags       │ in yetty                    │ non-yetty\n"
		"    ────────────┼─────────────────────────────┼─────────────\n"
		"    (none)      │ ypaint handler → OSC,       │ tree-sitter\n"
		"                │  else ts → OSC, else raw    │  → SGR, else raw\n"
		"    --raw       │ raw bytes                   │ raw bytes\n"
		"    --ts        │ ts → OSC                    │ ts → SGR\n"
		"    --ts --raw  │ ts → SGR                    │ ts → SGR\n"
		"\n"
		"  URL inputs (http://, https://) are fetched via libcurl.\n"
		"\n"
		"Options:\n"
		"  -w, --width=N        card width in cells (default: term cols)\n"
		"  -H, --height=N       card height in cells (default: 0)\n"
		"  -x, --x=N            x origin (default: 0)\n"
		"  -y, --y=N            y origin (default: 0)\n"
		"  -a, --absolute       absolute positioning (default: relative)\n"
		"  -r, --raw            plain cat (no rendering)\n"
		"  -t, --ts             force tree-sitter path (even for md/pdf)\n"
		"  -c, --card=TYPE      force handler: markdown, pdf, text, source\n"
		"      --sleep-after=N  after the OSC is written, sleep N ms before\n"
		"                       exiting. Keeps the slave PTY open long enough\n"
		"                       for the parent terminal to drain the master\n"
		"                       (workaround for libuv giving up on POLLHUP).\n"
		"  -h, --help           show this help\n"
		"\n"
		"  Use '-' or no args to read from stdin.\n",
		prog);
}

/*=============================================================================
 * I/O helpers
 *===========================================================================*/

struct byte_buf {
	uint8_t *data;
	size_t len;
	size_t cap;
};

static void byte_buf_free(struct byte_buf *b)
{
	free(b->data);
	b->data = NULL;
	b->len = 0;
	b->cap = 0;
}

static int byte_buf_append(struct byte_buf *b, const uint8_t *src, size_t n)
{
	if (b->len + n > b->cap) {
		size_t nc = b->cap ? b->cap * 2 : 65536;
		while (nc < b->len + n)
			nc *= 2;
		uint8_t *nd = realloc(b->data, nc);
		if (!nd)
			return -1;
		b->data = nd;
		b->cap = nc;
	}
	memcpy(b->data + b->len, src, n);
	b->len += n;
	return 0;
}

static int read_all_file(const char *path, struct byte_buf *out)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return -1;
	uint8_t buf[65536];
	for (;;) {
		size_t n = fread(buf, 1, sizeof(buf), f);
		if (n > 0 && byte_buf_append(out, buf, n) < 0) {
			fclose(f);
			return -1;
		}
		if (n < sizeof(buf)) {
			int err = ferror(f);
			fclose(f);
			return err ? -1 : 0;
		}
	}
}

static int read_all_stdin(struct byte_buf *out)
{
	uint8_t buf[65536];
	for (;;) {
		size_t n = fread(buf, 1, sizeof(buf), stdin);
		if (n > 0 && byte_buf_append(out, buf, n) < 0)
			return -1;
		if (n < sizeof(buf))
			return ferror(stdin) ? -1 : 0;
	}
}

static int write_all_stdout(const uint8_t *data, size_t len)
{
	size_t written = 0;
	while (written < len) {
		size_t n = fwrite(data + written, 1, len - written, stdout);
		if (n == 0)
			return -1;
		written += n;
	}
	return 0;
}

/*=============================================================================
 * Per-file processing
 *===========================================================================*/

static int process_one(const char *arg, const struct ycat_opts *opts)
{
	const bool is_stdin = (strcmp(arg, "-") == 0);
	const bool is_url = yetty_ycat_is_url(arg);
	const char *path_hint = (is_stdin || is_url) ? NULL : arg;

	struct byte_buf buf = {0};
	char *url_mime = NULL;
	int rc;
	if (is_stdin) {
		rc = read_all_stdin(&buf);
	} else if (is_url) {
		rc = yetty_ycat_fetch_url(arg, &buf.data, &buf.len, &url_mime);
		buf.cap = buf.len;
	} else {
		rc = read_all_file(arg, &buf);
	}
	if (rc < 0) {
		fprintf(stderr, "ycat: %s: failed to read\n", arg);
		byte_buf_free(&buf);
		free(url_mime);
		return -1;
	}

	/*---------------------------------------------------------------
	 * Dispatch matrix (see `ycat --help`):
	 *
	 *   flags          │ in yetty                     │ non-yetty
	 *   ───────────────┼──────────────────────────────┼──────────────
	 *   (none)         │ ypaint handler → OSC;         │ ts → SGR;
	 *                  │  else ts → OSC;               │  else raw.
	 *                  │  else raw.                    │
	 *   --raw          │ raw bytes                    │ raw bytes
	 *   --ts           │ ts → OSC                     │ ts → SGR
	 *   --ts --raw     │ ts → SGR                     │ ts → SGR
	 *-------------------------------------------------------------*/

	const bool in_yetty = in_yetty_terminal();

	/* --raw (without --ts): just passthrough. */
	if (opts->raw && !opts->force_ts) {
		rc = write_all_stdout(buf.data, buf.len);
		byte_buf_free(&buf);
		free(url_mime);
		return rc;
	}

	struct yetty_ycat_config cfg = {
		.cell_width = 8,
		.cell_height = 16,
		.width_cells = (uint32_t)(opts->width_cells > 0
					   ? opts->width_cells : 80),
		.height_cells = (uint32_t)(opts->height_cells > 0
					   ? opts->height_cells : 0),
	};

	/* Resolve the tree-sitter grammar once — used by several branches. */
	const char *grammar = yetty_ycat_grammar_lookup(url_mime, path_hint);

	/* --ts: force tree-sitter. Output is SGR when --raw is also set OR when
	 * not inside a yetty terminal; OSC otherwise. */
	if (opts->force_ts) {
		if (!grammar) {
			fprintf(stderr,
				"ycat: %s: --ts requested but no grammar for this file\n",
				arg);
			byte_buf_free(&buf);
			free(url_mime);
			return -1;
		}

		const bool sgr = opts->raw || !in_yetty;
		if (sgr) {
			int er = yetty_ycat_ts_emit_sgr(buf.data, buf.len,
							grammar, stdout);
			byte_buf_free(&buf);
			free(url_mime);
			return er;
		}

		struct yetty_ypaint_core_buffer_result r =
			yetty_ycat_ts_render(buf.data, buf.len, grammar, &cfg);
		if (YETTY_IS_ERR(r)) {
			fprintf(stderr, "ycat: %s: ts render failed: %s\n",
				arg, r.error.msg);
			byte_buf_free(&buf);
			free(url_mime);
			return -1;
		}
		struct yetty_ycore_size_result em_r = yetty_ycat_osc_bin_emit(r.value, stdout);
		yetty_ypaint_core_buffer_destroy(r.value);
		byte_buf_free(&buf);
		free(url_mime);
		if (YETTY_IS_ERR(em_r)) {
			yetty_ycore_error_destroy(em_r.error);
			return -1;
		}
		return em_r.value > 0 ? 0 : -1;
	}

	/* Default path (no --ts, no --raw-alone). */

	enum yetty_ycat_type type;
	if (opts->force_type) {
		type = yetty_ycat_type_from_name(opts->force_type);
	} else if (url_mime && *url_mime) {
		type = yetty_ycat_type_from_mime(url_mime);
		if (type == YETTY_YCAT_TYPE_UNKNOWN)
			type = yetty_ycat_detect(buf.data, buf.len, arg);
	} else {
		type = yetty_ycat_detect(buf.data, buf.len, path_hint);
	}

	/* Inside a yetty terminal: try the dedicated ypaint handler first,
	 * then ts → OSC, then raw. */
	if (in_yetty) {
		yetty_ycat_handler_fn fn = yetty_ycat_get_handler(type);
		if (fn) {
			struct yetty_ypaint_core_buffer_result r =
				fn(buf.data, buf.len, path_hint, &cfg);
			if (YETTY_IS_OK(r)) {
				struct yetty_ycore_size_result em_r =
					yetty_ycat_osc_bin_emit(r.value, stdout);
				yetty_ypaint_core_buffer_destroy(r.value);
				byte_buf_free(&buf);
				free(url_mime);
				if (YETTY_IS_ERR(em_r)) {
					yetty_ycore_error_destroy(em_r.error);
					return -1;
				}
				return em_r.value > 0 ? 0 : -1;
			}
			fprintf(stderr,
				"ycat: %s: ypaint handler failed (%s), trying tree-sitter\n",
				arg, r.error.msg);
		}
		if (grammar) {
			struct yetty_ypaint_core_buffer_result r =
				yetty_ycat_ts_render(buf.data, buf.len,
						     grammar, &cfg);
			if (YETTY_IS_OK(r)) {
				struct yetty_ycore_size_result em_r =
					yetty_ycat_osc_bin_emit(r.value, stdout);
				yetty_ypaint_core_buffer_destroy(r.value);
				byte_buf_free(&buf);
				free(url_mime);
				if (YETTY_IS_ERR(em_r)) {
					yetty_ycore_error_destroy(em_r.error);
					return -1;
				}
				return em_r.value > 0 ? 0 : -1;
			}
		}
	} else {
		/* Non-yetty terminal: only tree-sitter has anything useful to
		 * show, since OSC payloads are invisible here. */
		if (grammar) {
			int er = yetty_ycat_ts_emit_sgr(buf.data, buf.len,
							grammar, stdout);
			if (er == 0) {
				byte_buf_free(&buf);
				free(url_mime);
				return 0;
			}
		}
	}

	/* Final fallback: raw bytes. */
	rc = write_all_stdout(buf.data, buf.len);
	byte_buf_free(&buf);
	free(url_mime);
	return rc;
}

/*=============================================================================
 * main
 *===========================================================================*/

enum {
	OPT_SLEEP_AFTER = 1000,
};

int main(int argc, char **argv)
{
	struct ycat_opts opts = {
		.x = 0,
		.y = 0,
		.width_cells = 0,
		.height_cells = 0,
		.absolute = false,
		.raw = false,
		.force_type = NULL,
		.sleep_after_ms = 0,
	};

	static const struct option long_opts[] = {
		{ "width",       required_argument, NULL, 'w' },
		{ "height",      required_argument, NULL, 'H' },
		{ "x",           required_argument, NULL, 'x' },
		{ "y",           required_argument, NULL, 'y' },
		{ "absolute",    no_argument,       NULL, 'a' },
		{ "raw",         no_argument,       NULL, 'r' },
		{ "ts",          no_argument,       NULL, 't' },
		{ "card",        required_argument, NULL, 'c' },
		{ "type",        required_argument, NULL, 'c' },
		{ "sleep-after", required_argument, NULL, OPT_SLEEP_AFTER },
		{ "help",        no_argument,       NULL, 'h' },
		{ NULL,          0,                 NULL, 0   },
	};

	int c;
	while ((c = getopt_long(argc, argv, "w:H:x:y:artc:h", long_opts, NULL))
	       != -1) {
		switch (c) {
		case 'w': opts.width_cells = atoi(optarg); break;
		case 'H': opts.height_cells = atoi(optarg); break;
		case 'x': opts.x = atoi(optarg); break;
		case 'y': opts.y = atoi(optarg); break;
		case 'a': opts.absolute = true; break;
		case 'r': opts.raw = true; break;
		case 't': opts.force_ts = true; break;
		case 'c': opts.force_type = optarg; break;
		case OPT_SLEEP_AFTER: opts.sleep_after_ms = atoi(optarg); break;
		case 'h': usage(stdout, argv[0]); return 0;
		default:  usage(stderr, argv[0]); return 2;
		}
	}

	if (opts.width_cells == 0)
		opts.width_cells = terminal_columns();

	int rc = 0;
	if (optind >= argc) {
		if (process_one("-", &opts) < 0)
			rc = 1;
	} else {
		for (int i = optind; i < argc; i++) {
			if (process_one(argv[i], &opts) < 0)
				rc = 1;
		}
	}

	fflush(stdout);

	/* Hold the slave PTY open a bit longer so a parent yetty has time
	 * to drain the master before EOF arrives there. */
	if (opts.sleep_after_ms > 0) {
#ifdef _WIN32
		Sleep((DWORD)opts.sleep_after_ms);
#else
		struct timespec ts = {
			.tv_sec  = opts.sleep_after_ms / 1000,
			.tv_nsec = (long)(opts.sleep_after_ms % 1000) * 1000000L,
		};
		nanosleep(&ts, NULL);
#endif
	}

	return rc;
}
