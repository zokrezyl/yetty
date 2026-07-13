# ypty — concrete PTY backends

`ypty` holds the *implementations* of yetty's PTY abstraction — one file per
backend, spanning real OS PTYs, in-process VMs, in-memory pairs, and network
transports. The abstract interface (`struct yetty_platform_pty` + ops
vtable), the readiness-source types, and the per-platform factory that picks
a backend from config all live in [yplatform](../yplatform/README.md)
(`include/yetty/yplatform/pty.h`, `src/yetty/yplatform/pty-factory/`); this
directory only provides the `*_create` functions the factory dispatches to.

Every backend implements the same seven-op vtable (`destroy`, `read`,
`write`, `resize` with pixel size, `stop`, `pipe_source`, optional
`child_alive`) and is created through a
`struct yetty_yplatform_pty_ptr_result`. See the "PTY architecture" section
of [../yplatform/README.md](../yplatform/README.md) for the buffer/readiness
design the backends follow.

## The backends

| file | backend | platform / mode |
|------|---------|-----------------|
| `forkpty.c` | `yetty_yplatform_fork_pty_create` — kernel PTY via `forkpty()`, shell from yconfig's shell argv, non-blocking master fd, SIGHUP→SIGTERM→SIGKILL stop escalation, `child_alive` via `waitpid(WNOHANG)` | Linux/macOS/Android default; compiled on iOS/tvOS but returns an error (sandbox forbids fork/exec) |
| `conpty.c` | Windows ConPTY (`HPCON` + pipe pair, CRT fds for libuv) — **also contains the whole Windows PTY factory**, including its `--qemu`/`--telnet` handling | Windows default |
| `memory-pty.c` | `yetty_yplatform_memory_pty_pair_create` — in-process endpoint pair over two ring buffers (default 16 MiB/direction, refcounted owner). No fd: `pipe_source()` returns NULL; a wake callback notifies the reader, a resize callback + `get_winsize` mirror TIOCSWINSZ/SIGWINCH/TIOCGWINSZ across the pair. Single-thread only | all platforms; in-process bridges (used by the yvterm terminal unit test today) |
| `temu-pty.c` | `yetty_yplatform_tinyemu_pty_create` — RISC-V Linux VM run by TinyEMU on a pthread inside the yetty process; the guest's hvc0 virtio-console is bridged through two pipes; resize propagates live via `virtio_console_resize_event` | desktop `--temu` (in-file comments still call it `--virtual`), and platforms without fork/exec |
| `windows-temu-pty.c` | Windows port of the TinyEMU backend | Windows `--temu` |
| `iframepty.c` | `yetty_yplatform_iframe_pty_create` — the WebASM console: TinyEMU runs in a separate hidden iframe (its own single-threaded wasm instance) and exchanges bytes with yetty via `postMessage`; output lands in a same-thread pipe the event loop drains each rAF tick. Singleton — one VM per page | webasm, first console |
| `websocket-pty.c` / `websocket-pty.h` | `yetty_ypty_websocket_pty_create` — raw shell bytes over a message-framed `conn_transport` (WebSocket). The server end *is* a PTY (`tools/websocket-pty-server`), so there is no protocol state machine; client → server messages carry a type byte (`0x00` input, `0x01` resize with four big-endian uint16s), server → client messages are raw output. Producer-side overflow ring keeps pipe writes non-blocking | webasm `--websocket` |

Related backends that live in their own modules but implement the same
interface: telnet ([ytelnet](../ytelnet/README.md)), SSH — both the desktop
threaded variant and the browser libssh2-over-WebSocket variant
([yssh](../yssh/README.md)).

## Build wiring

There is no `CMakeLists.txt` here; each platform's target file under
`build-tools/yetty/platform/<os>/cmake.cmake` lists exactly the backends
that exist on that OS (e.g. linux: `forkpty.c`, `memory-pty.c`, plus
`temu-pty.c` when TinyEMU is enabled; webasm: `iframepty.c`,
`memory-pty.c`, `websocket-pty.c`). Most create functions are declared in
`include/yetty/yplatform/pty.h`; `websocket-pty.h` is the one in-module
header, included as `<yetty/ypty/websocket-pty.h>` (`src/` is on the
include path).

## Who selects what

The factory (`yetty_yplatform_pty_factory_create`) is the only caller of
these create functions:

- **Unix** (`../yplatform/pty-factory/default.c`): `--telnet` → telnet-pty;
  `--temu` → TinyEMU console for tab 1, telnet to TinyEMU's slirp hostfwd
  for later tabs; `--ssh` → ssh-pty; `--qemu` → telnet to the spawned QEMU
  ([yqemu](../yqemu/README.md)); default → `forkpty.c`.
- **Windows** (`conpty.c`): same shape with ConPTY as the default.
- **WebASM** (`../yplatform/pty-factory/webasm.c`): `--temu`/default →
  `iframepty.c` then telnet-over-iframe-transport; `--websocket` →
  `websocket-pty.c`; `--telnet`/`--ssh` → telnet/SSH over a WebSocket
  bridge.

The consumer of the resulting PTY is the terminal
([yterminal](../yterminal/README.md)): shell output is pulled through
`ops->read` when the pipe source signals readiness, keyboard bytes go out
through `ops->write`.
