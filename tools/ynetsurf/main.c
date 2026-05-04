/*
 * ynetsurf — minimal browser tool.
 *
 * Loads a URL through the NetSurf core, drains the rendered page into
 * a yetty_ypaint_core_buffer, then base64-encodes the buffer (the wire
 * format that yetty's ypaint reader accepts) and dumps it to stdout —
 * mirroring what tools/html2ydraw does with the legacy HTML pipeline.
 *
 * Usage:
 *   ynetsurf <url>           # binary on stdout
 *   ynetsurf --osc <url>     # OSC-wrapped for paste into a yetty session
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

#include <yetty/ynetsurf/ynetsurf.h>
#include <yetty/ypaint-core/buffer.h>
#include <yetty/yface/yface.h>
#include <yetty/ycore/types.h>
#include <yetty/yterm/osc-codes.h>

#include "content/fetch.h"

/* Drive the fetcher (curl) + ynetsurf scheduler for up to max_ms. Mirrors
 * the monkey frontend's main loop: select() on the fetcher's fds with
 * the schedule's next-deadline as timeout, then pump. */
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
			.tv_sec  = next / 1000,
			.tv_usec = (next % 1000) * 1000,
		};
		(void)select(max_fd + 1, &rfds, &wfds, &efds, &tv);
	}
}

/* Wrap the serialized ypaint buffer in a YETTY_OSC_YPAINT_BIN envelope
 * (the same wire format ycat / yecho / ygui / yplot emit) and write it
 * to stdout. yterm/ypaint-layer.c on the receiving side dispatches by
 * OSC code; the args slot carries the bin_meta (LZ4F flag + raw_size),
 * the payload is the base64'd LZ4F'd serialized buffer. */
static int emit_osc(const uint8_t *bytes, size_t blen)
{
	struct yetty_yface_bin_meta meta = {
		.magic = YETTY_YFACE_BIN_MAGIC,
		.version = YETTY_YFACE_BIN_VERSION,
		.compressed = YETTY_YFACE_COMP_LZ4F,
		.compression_algo = 0,
		.raw_size = blen,
		.reserved = {0, 0},
	};
	struct yetty_ycore_buffer env = {0};
	struct yetty_ycore_void_result r = yetty_yface_emit(
		YETTY_OSC_YPAINT_BIN, /*compressed=*/1,
		&meta, sizeof(meta), bytes, blen, &env);
	if (YETTY_IS_ERR(r)) {
		fprintf(stderr, "yface_emit failed: %s\n", r.error.msg);
		yetty_ycore_buffer_destroy(&env);
		return 1;
	}
	if (env.size > 0)
		fwrite(env.data, 1, env.size, stdout);
	yetty_ycore_buffer_destroy(&env);
	return 0;
}

int main(int argc, char **argv)
{
	/* OSC envelope is the default when stdout is a terminal — that's
	 * what users expect when they run `ynetsurf <url>` inside yetty.
	 * --raw forces the bare ypaint buffer for piping to files / other
	 * tools; --osc still works as an explicit opt-in. */
	int osc = isatty(STDOUT_FILENO) ? 1 : 0;
	int width = 1024, height = 768;
	const char *url = NULL;
	int wait_ms = 4000;

	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		if (!strcmp(a, "--osc")) osc = 1;
		else if (!strcmp(a, "--raw")) osc = 0;
		else if (!strcmp(a, "-w") && i + 1 < argc) width  = atoi(argv[++i]);
		else if (!strcmp(a, "-H") && i + 1 < argc) height = atoi(argv[++i]);
		else if (!strcmp(a, "--wait") && i + 1 < argc)
			wait_ms = atoi(argv[++i]);
		else if (a[0] != '-') url = a;
	}

	if (url == NULL) {
		fprintf(stderr,
			"usage: %s [--osc|--raw] [-w W] [-H H] [--wait MS] <url>\n"
			"       default: --osc when stdout is a TTY, --raw otherwise\n",
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

	struct yetty_ypaint_core_buffer_result br =
		yetty_ypaint_core_buffer_config_buffer_create(NULL);
	if (YETTY_IS_ERR(br)) {
		fprintf(stderr, "buffer_create failed: %s\n", br.error.msg);
		yetty_ynetsurf_destroy(ns);
		return 1;
	}
	struct yetty_ypaint_core_buffer *buf = br.value;

	struct yetty_ycore_void_result rd = yetty_ynetsurf_redraw(ns, buf);
	if (YETTY_IS_ERR(rd)) {
		fprintf(stderr, "redraw failed: %s\n", rd.error.msg);
		yetty_ypaint_core_buffer_destroy(buf);
		yetty_ynetsurf_destroy(ns);
		return 1;
	}

	const uint8_t *bytes = NULL;
	size_t blen = yetty_ypaint_core_buffer_serialize(buf, &bytes);

	if (osc) {
		emit_osc(bytes, blen);
	} else {
		fwrite(bytes, 1, blen, stdout);
	}
	fflush(stdout);

	yetty_ypaint_core_buffer_destroy(buf);
	yetty_ynetsurf_destroy(ns);
	return 0;
}
