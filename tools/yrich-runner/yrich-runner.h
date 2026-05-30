#ifndef YETTY_TOOLS_YRICH_RUNNER_H
#define YETTY_TOOLS_YRICH_RUNNER_H

/*
 * yrich-runner — interactive driver for yrich documents.
 *
 * Renders a yetty_yrich_document to a ydraw buffer and emits it on stdout
 * as OSC 666674 (the canvas sink consumed by yetty's ydraw-layer). There is
 * no card abstraction — the document IS the canvas.
 *
 * Output protocol:
 *   \033]666674;--clear\033\\          — empty the canvas
 *   \033]666674;--bin;<base64>\033\\   — push a fresh ydraw buffer
 *
 * Input protocol:
 *   OSC 777777 — mouse click       (buttons;press;x;y)
 *   OSC 777778 — mouse move        (buttons;x;y)
 *   OSC 777779 — view change       (zoom;sx;sy)
 *   OSC 777780 — pixel size report (w;h)
 *   plus regular keys / CSI arrows / Ctrl-letter shortcuts
 *
 * Tools provide a yetty_yrich_document; the runner owns everything else.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yrich_document;
struct yetty_ydraw_draw_list;

struct yrich_runner {
	struct yetty_yrich_document *doc;       /* not owned */
	struct yetty_ydraw_draw_list *buf;   /* not owned */

	/* View state, populated from OSC 777779 / 777780. */
	float display_w;
	float display_h;
	bool have_pixel_size;
	float zoom;
	float scroll_x;
	float scroll_y;

	/* Mouse drag tracking. */
	bool mouse_down;
	uint32_t mouse_button;   /* enum yetty_yrich_mouse_button */
	uint64_t last_click_ms;
	float last_click_x;
	float last_click_y;

	/* Escape-sequence parser. */
	int esc_state;            /* 0=none, 1=esc, 2=osc, 3=csi */
	char *esc_buf;
	size_t esc_buf_len;
	size_t esc_buf_cap;

	bool running;
	bool dump_once;           /* render once and exit */
};

/* Initialise an existing struct (zero-fills, attaches refs). NULL-safe. */
void yrich_runner_init(struct yrich_runner *r,
		       struct yetty_yrich_document *doc,
		       struct yetty_ydraw_draw_list *buf);

/* Free transient state owned by the runner (escape buffer). */
void yrich_runner_fini(struct yrich_runner *r);

/* Render once: clear canvas, repaint document, emit to stdout. Sets the
 * document non-dirty. */
void yrich_runner_emit(struct yrich_runner *r);

/* Subscribe / unsubscribe to OSC click, move, and view-change reports. */
void yrich_runner_subscribe(bool enable);

/* Enter raw mode (termios) for stdin. atexit-restored. */
void yrich_runner_raw_mode_enable(void);

/* Run the interactive loop until the user exits. Caller's responsibility to
 * have called raw_mode_enable() and subscribe() beforehand. Returns
 * YETTY_OK_VOID on clean exit. */
struct yetty_ycore_void_result
yrich_runner_loop(struct yrich_runner *r);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_TOOLS_YRICH_RUNNER_H */
