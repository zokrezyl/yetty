/* ssh-websocket-pty — non-blocking SSH client PTY over a byte transport.
 *
 * The webasm sibling of ssh-pty.c. The desktop implementation drives
 * libssh2 with a blocking handshake, a raw TCP socket and a reader
 * thread — none of which exist in the single-threaded emscripten
 * build. This one instead runs libssh2 in non-blocking mode with
 * custom LIBSSH2_CALLBACK_SEND / LIBSSH2_CALLBACK_RECV functions
 * wired to a yetty_ytransport_conn_transport, and advances a connect
 * state machine (handshake → password auth → channel open → pty-req →
 * shell) from the transport's asynchronous callbacks.
 *
 * The transport carries the *encrypted* SSH byte stream — with the
 * websocket transport the server side is a dumb websocket↔TCP bridge
 * to sshd:22 (tools/telnet-websocket.sh works: it never sees
 * plaintext), so this is end-to-end SSH terminating in the browser.
 *
 * v1 limitations (logged at runtime where relevant):
 *   - password auth only (no agent, no keys, no keyboard-interactive)
 *   - the host key is logged (SHA256) but NOT verified against a
 *     known-hosts store — MITM-detection is on the operator.
 */

#ifndef YETTY_YSSH_SSH_WEBSOCKET_PTY_H
#define YETTY_YSSH_SSH_WEBSOCKET_PTY_H

#include <yetty/yplatform/pty.h>
#include <yetty/ytransport/conn-transport.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yconfig_config;

/**
 * Create an SSH PTY on top of a byte transport.
 *
 * Takes ownership of `transport` (destroyed on every failure path and
 * on PTY destroy). Returns immediately after kicking off
 * transport->open(); the SSH handshake/auth/channel sequence runs as
 * transport callbacks deliver bytes.
 *
 * Reads from yconfig:
 *   ssh/username   (required)
 *   ssh/password   (required for now — password auth only)
 *   ssh/term-type  (default "xterm-256color")
 *
 * @param config    yetty config (username/password/term-type)
 * @param transport Byte transport carrying the encrypted stream. Owned.
 * @return PTY result
 */
struct yetty_yplatform_pty_ptr_result yetty_yssh_ssh_websocket_pty_create(
    struct yetty_yconfig_config *config, struct yetty_ytransport_conn_transport *transport);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YSSH_SSH_WEBSOCKET_PTY_H */
