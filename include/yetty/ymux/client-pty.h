#ifndef YETTY_YMUX_CLIENT_PTY_H
#define YETTY_YMUX_CLIENT_PTY_H

#include <yetty/yplatform/pty.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yevent_event_loop;

/*
 * ymux client PTY — a PTY backend whose "shell" is a remote ymux server pane.
 *
 * The whole existing terminal stack (yterminal + ymux_pane + yvterm:grid +
 * the vterm renderer) drives it unchanged: it reads decoded PTY bytes off a
 * self-pipe and writes keystrokes to a sink. Under the covers:
 *
 *   - a TCP connection to the ymux server carries msgpack-RPC,
 *   - on connect it sends `ymux-attach {pane, cols, rows}`,
 *   - a poll timer sends `ymux-poll {pane, since}`; the returned output bytes
 *     are written into the self-pipe (the terminal reads them like a real PTY),
 *   - `write()` (keyboard/query) ships `ymux-input {pane, data}`,
 *   - `resize()` ships `ymux-resize {pane, cols, rows}`.
 *
 * This is the client half of model-shipping: the client re-runs its own
 * emulator over the server's byte stream and renders natively.
 */
struct yetty_yplatform_pty_ptr_result yetty_ymux_client_pty_create(
    const char *host, int port, int pane, struct yetty_yevent_event_loop *event_loop);

/* Factory wrapper so the app can hand it to yetty_create in place of the
 * default forkpty factory. Every create_pty yields a client PTY bound to the
 * same server pane. `host` is copied. */
struct yetty_yplatform_pty_factory_ptr_result yetty_ymux_client_pty_factory_create(const char *host,
                                                                                   int port,
                                                                                   int pane);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YMUX_CLIENT_PTY_H */
