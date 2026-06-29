/*
 * yetty/yguiapp/run.h — dual-mode launcher + terminal host for yguiapp:app.
 *
 * Hand-written sibling of the generated app.h. The functions here are plain C
 * (not yclass methods): they host a yguiapp:app subclass in one of two modes,
 * deciding which by yetty_running_under_yetty():
 *
 *   - STANDALONE: the shared glfw_platform brings up window/GPU/event loop and
 *     drives the app's run() override (the full bring-up in app.c).
 *   - TERMINAL: when running inside a host yetty (TERM_PROGRAM=yetty), the app
 *     ships OSC envelopes over stdout to the host; stdin feeds keystrokes. No
 *     own window. Backed by a libuv loop over a single transport-pty +
 *     ywire_connection (the #380 single-reader path).
 *
 * A demo/app subclasses yguiapp:app, overrides build(self, body), and its
 * main() forwards to yetty_yguiapp_run_main(argc, argv, <cls>_class_get().value).
 */
#ifndef YETTY_YGUIAPP_RUN_H
#define YETTY_YGUIAPP_RUN_H

#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Build the shared two-level root for a ygui app: an outer stretch vbox that
 * owns the viewport, plus an inner brand body panel (column direction, padding,
 * gap, justify-center) that the app builds its widgets into. Sets the framework
 * root to the outer vbox and returns both objects. `chrome_inset_top` adds extra
 * top padding to the body so content clears a chrome caption strip (0 when no
 * chrome). Used by both the standalone run() (app.c) and the terminal host.
 */
struct yetty_ycore_void_result yetty_yguiapp_build_root_body(struct yetty_yclass_object *engine,
                                                             float chrome_inset_top,
                                                             struct yetty_yclass_object **root_out,
                                                             struct yetty_yclass_object **body_out);

/*
 * Run an already-created yguiapp:app subclass instance in TERMINAL (in-host-yetty)
 * mode: a libuv loop over a single transport-pty + ywire_connection that ships
 * the framework's figure output on the rpc channel, routes forwarded mouse on the
 * input channel and raw keystrokes on the raw channel, and forwards resize via
 * the connection's winsize pickup. Terminal raw mode is restored on every exit
 * and error path. Requires libuv (YETTY_YGUI_HAS_UV); without it this returns an
 * error.
 */
struct yetty_ycore_void_result yetty_yguiapp_run_terminal(struct yetty_yclass_object *app);

/*
 * Dual-mode launcher. Instantiates the given app class and runs it standalone or
 * in-terminal depending on yetty_running_under_yetty(). Returns a process exit
 * code (0 on clean shutdown, non-zero on error). The typical app main() is a
 * one-liner forwarding here with its own <cls>_class_get().value.
 */
int yetty_yguiapp_run_main(int argc, char **argv, const struct yetty_yclass *app_class);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YGUIAPP_RUN_H */
