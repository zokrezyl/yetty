/*
 * demo/yrdawn/common.h — small helpers shared by every yrdawn demo.
 *
 * Each demo runs as the inferior of a yetty terminal launched with
 * `./yetty -e <demo>`. Stdin / stdout are the PTY slave; yetty's
 * wire-statemachine parses the `\e]630000;` envelopes the demo
 * writes (figure-tree records targeted at yrdawn canvases) and
 * routes them to the matching figures. Server→client OSCs come
 * back on the PTY input — the client demuxes by figure_id and
 * dispatches per-canvas callbacks.
 *
 * `demo_raw_stdin()` puts the tty into raw mode so OSC ESCs don't
 * get echoed back as caret-notation and corrupt the wire.
 */
#ifndef YETTY_DEMO_YRDAWN_COMMON_H
#define YETTY_DEMO_YRDAWN_COMMON_H

#include <stdio.h>
#include <stdint.h>

struct yetty_yrdawn_client;
struct yetty_yrdawn_canvas;

void demo_raw_stdin(void);
void demo_sleep_ms(int ms);

/* Side-channel trace file under /tmp keyed by the demo name. NULL if
 * the file can't be opened. */
FILE *demo_trace_open(const char *demo_name);

/* 'q' from the host yetty flips this flag. Install on a canvas via
 * `demo_install_quit_on_q` and poll inside any wait loop. */
extern int demo_quit_flag;
void demo_install_quit_on_q(struct yetty_yrdawn_canvas *canvas);

/* One-shot bring-up for the common single-canvas demo flow:
 *   - client_create(STDIN, STDOUT, session_id=getpid())
 *   - canvas_create(figure_id, rect=(16, 16, w, h))
 *   - install_quit_on_q on the canvas
 *   - pump until the canvas's HELLO_ACK lands (or timeout)
 *
 * Pick `w` and `h` to match the pixel size the demo will present so
 * the figure's rect doesn't stretch its frame texture. Match the
 * demo's `W`/`H` constants; the helper places it with a small inset
 * so it doesn't bleed into pane chrome.
 *
 * Returns the canvas on success; out_client receives the owning
 * client pointer (for codegen wgpu* calls). On failure logs to
 * `trace` (when non-NULL) and returns NULL with *out_client = NULL.
 *
 * `figure_id` is the canvas's wire address; pick any non-zero u32 the
 * demo isn't already using. Demos with one canvas can just pass 1. */
struct yetty_yrdawn_canvas *demo_bringup_single_canvas(uint32_t figure_id, float w, float h,
                                                       FILE *trace,
                                                       struct yetty_yrdawn_client **out_client);

#endif /* YETTY_DEMO_YRDAWN_COMMON_H */
