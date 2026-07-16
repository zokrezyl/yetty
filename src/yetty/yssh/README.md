# yssh — SSH PTY backends (libssh2)

`yssh` turns an SSH session into a `yetty_platform_pty`, so a remote shell
plugs into the terminal exactly like a local fork-pty. There are two
implementations sharing the libssh2 dependency but nothing else: a
threaded desktop client (`ssh-pty.c`) and a single-threaded, non-blocking
webasm client (`ssh-websocket-pty.c`) that rides a
[`ytransport`](../ytransport/README.md) byte transport.

## Desktop: `ssh-pty.c`

Built as `yetty_ssh` (this directory's `CMakeLists.txt`, gated on
`YETTY_ENABLE_FEATURE_SSH`), consumed by
`../yplatform/pty-factory/default.c` when `--ssh` is set.

Connection parameters resolve from `~/.ssh/config` (then
`/etc/ssh/ssh_config`) first; yconfig values override:

```
ssh/host   ssh/port   ssh/username   ssh/password
ssh/private-key-path   ssh/private-key-passphrase   ssh/term-type
```

`yetty_yssh_ssh_pty_create(config)` connects the TCP socket, handshakes,
authenticates (public key from file first if one is configured or found in
`IdentityFile`, then password), opens a channel, requests a PTY
(80x24 initial; the terminal drives later sizes via `resize()`), and
starts a shell — all before returning. Threading model:

- libssh2 runs in non-blocking mode; a mutex serializes every session
  call (libssh2 is not thread-safe per session).
- A dedicated reader thread `poll()`s the socket and writes decrypted
  channel bytes into an internal pipe exposed via the PTY `pipe_source` —
  the same read-fd path fork-pty uses.
- Writes and resizes happen on the caller's thread under the same mutex.

`openssh-config-parser.c` mirrors openssh semantics: all matching `Host`
blocks apply (fnmatch globs, space-separated pattern lists, a `!`-negated
pattern forces a no-match), first occurrence of a keyword wins except `IdentityFile`
which accumulates (up to 8), and `Include` is followed with glob expansion
and a recursion depth limit.

## Webasm: `ssh-websocket-pty.c`

Compiled straight into the webasm yetty target
(`build-tools/yetty/platform/webasm/cmake.cmake`, libssh2 with an mbedTLS
backend), consumed by `../yplatform/pty-factory/webasm.c` when `--ssh` is
set. No threads, no sockets: libssh2's `LIBSSH2_CALLBACK_SEND` /
`LIBSSH2_CALLBACK_RECV` are wired to a `yetty_ytransport_conn_transport`,
and a connect state machine (`WAIT_TRANSPORT → HANDSHAKE → AUTH →
CHANNEL_OPEN → PTY_REQUEST → SHELL_REQUEST → READY`) advances from the
transport's asynchronous callbacks.

The transport carries the *encrypted* stream — with the websocket backend
the server side is a dumb ws↔TCP bridge to `sshd:22` that never sees
plaintext; with the lwip backend ([`ywasmnet`](../ywasmnet/README.md) relay) the
browser dials the real `sshd` itself. v1 limits, logged at runtime:
password auth only, and the host key is fingerprint-logged (SHA256) but
not verified against a known-hosts store.

## Public API sketch

```c
/* Desktop (include/yetty/yssh/ssh-pty.h). */
struct yetty_yplatform_pty_ptr_result yetty_yssh_ssh_pty_create(
    struct yetty_yconfig_config *config);

/* Webasm (src/yetty/yssh/ssh-websocket-pty.h). Takes ownership of transport. */
struct yetty_yplatform_pty_ptr_result yetty_yssh_ssh_websocket_pty_create(
    struct yetty_yconfig_config *config,
    struct yetty_ytransport_conn_transport *transport);
```

## File map

| file | role |
|---|---|
| `ssh-pty.c` | desktop client: blocking bring-up, reader thread, mutex-serialized session |
| `openssh-config-parser.{c,h}` | `~/.ssh/config` / `/etc/ssh/ssh_config` resolution |
| `ssh-websocket-pty.{c,h}` | webasm client: non-blocking libssh2 over a conn-transport |
| `CMakeLists.txt` | `yetty_ssh` static lib (desktop sources only; the webasm file is compiled by the webasm platform cmake) |

## Cross-references

- [`../ytransport/README.md`](../ytransport/README.md) — the byte-transport contract under the webasm client.
- [`../ytelnet/README.md`](../ytelnet/README.md) — the sibling protocol PTY with the same transport-polymorphic shape.
- [`../yplatform/README.md`](../yplatform/README.md) — PTY ops, pipe source, and factory plumbing.
