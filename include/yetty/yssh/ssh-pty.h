/* SSH PTY - libssh2 as PTY backend */

#ifndef YETTY_YSSH_PTY_H
#define YETTY_YSSH_PTY_H

#include <yetty/platform/pty.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yconfig_config;

/**
 * Create an SSH PTY.
 *
 * Reads connection parameters from yconfig:
 *   ssh/host                    (string, default "127.0.0.1")
 *   ssh/port                    (int,    default 22)
 *   ssh/username                (string, default "")
 *   ssh/password                (string, default "")
 *   ssh/private-key-path        (string, default "")
 *   ssh/private-key-passphrase  (string, default "")
 *   ssh/term-type               (string, default "xterm-256color")
 *
 * Initial PTY size is 80x24; the terminal drives subsequent sizes via resize().
 *
 * Connects the TCP socket, performs the SSH handshake, authenticates,
 * opens a channel, requests a PTY, and starts a shell — all synchronously.
 * On success a background reader thread streams channel output into an
 * internal pipe exposed through the PTY's pipe_source.
 *
 * @param config yetty config
 * @return PTY result
 */
struct yetty_yplatform_pty_ptr_result yetty_yssh_ssh_pty_create(struct yetty_yconfig_config *config);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YSSH_PTY_H */
