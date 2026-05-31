# Platform PTY Architecture

How PTY I/O is modeled uniformly across platforms. All backends implement one C
interface (`struct yetty_platform_pty`, `include/yetty/yplatform/pty.h`); the
event loop only needs a readiness signal, and the reader pulls bytes on demand.

See [Platform Abstraction](platform.md) for where this sits, and
[Platform Pipe](platform-pipe.md) for the sibling cross-thread input pipe.

## Design principle

Every platform models PTY I/O after the Unix fd pattern:

1. **The data buffer lives outside the PTY object** — in the kernel on Unix, in
   the in-iframe VM on WebAssembly. No double-buffering.
2. **The readiness source is notification-only** — like an fd, it just signals
   "data available"; it never holds data.
3. **`read()` pulls** from the external buffer when called.

```c
struct yetty_platform_pty_ops {
    struct yetty_ycore_void_result (*destroy)(struct yetty_platform_pty *self);
    struct yetty_ycore_size_result (*read)(struct yetty_platform_pty *self, char *buf, size_t max);
    struct yetty_ycore_size_result (*write)(struct yetty_platform_pty *self, const char *d, size_t n);
    struct yetty_ycore_void_result (*resize)(struct yetty_platform_pty *self,
                                             uint32_t cols, uint32_t rows,
                                             uint32_t pixel_w, uint32_t pixel_h);
    struct yetty_ycore_void_result (*stop)(struct yetty_platform_pty *self);
    struct yetty_platform_pty_pipe_source *(*pipe_source)(struct yetty_platform_pty *self);
};
```

`pipe_source()` returns the readiness handle the event loop polls
(`pty-pipe-source.h` / `pty-poll-source.h`); it has no `read`/`write` and holds
no buffer.

## Unix / desktop (Linux, macOS)

```
Kernel PTY buffer (owned by the OS)
        ↑↓  master fd
  fork-pty backend  (yetty_yplatform_fork_pty_create)
    read()  → ::read(master_fd, ...)
    write() → ::write(master_fd, ...)
    pipe_source() → fd-backed source (just holds the fd number)
        │
  yevent loop (libuv)
    polls the fd via uv_poll; on UV_READABLE, dispatches a readable event
```

**Shell output → screen:** shell writes stdout → kernel PTY buffer → master fd
readable → libuv fires → the reader calls `pty->ops->read(buf, len)` → bytes are
fed into the text layer's libvterm (`vterm_input_write`).

**Keyboard → shell:** key → libvterm produces the escape sequence → the layer's
PTY-write callback calls `pty->ops->write(data, len)` → kernel buffer → shell
reads stdin.

On Windows the same backend wraps ConPTY instead of `forkpty()`.

## WebAssembly

WebAssembly has no file descriptors. The console is backed by an **in-iframe
TinyEMU RISC-V VM** (`yetty_yplatform_iframe_pty_create`), and the byte buffer
lives in JavaScript in the parent window. C calls into JS to read.

```
TinyEMU VM (in iframe)  ── produces output ──► postMessage to parent
        │
Parent window JS:  appends to a JS buffer; calls back into wasm to signal "data available"
        │
  iframe PTY backend
    read()  → reads from the JS buffer via the wasm/JS bridge
    write() → postMessage into the iframe
    pipe_source() → callback-backed source (no fd, no buffer)
        │
  yevent loop (emscripten async)
    the source's callback fires when JS signals data; dispatches a readable event
```

The data flow mirrors Unix exactly — the only differences are *where the buffer
lives* (kernel vs JS) and *how the source signals* (fd readiness vs a JS-driven
callback). On WebAssembly the first console is the in-iframe VM; additional
sessions fall back to telnet over an iframe transport.

## Other backends

Behind the same interface:

- `yetty_yplatform_tinyemu_pty_create` — a native console backed by the embedded
  TinyEMU VM (see `yqemu` / `src/tinyemu`).
- `yetty_yplatform_memory_pty_pair_create` — an in-process PTY pair (two ring
  buffers, no fd); `pipe_source()` returns NULL and a wake callback notifies the
  reader instead.
- `yssh` / `ytelnet` — SSH and Telnet sessions, exposed as `yetty_platform_pty`.

A factory (`yetty_yplatform_pty_factory_create`) picks the backend from config.

## Key design points

| | Unix | WebAssembly |
|---|---|---|
| Buffer location | kernel (via fd) | parent-window JS (via wasm/JS bridge) |
| Readiness source | holds the fd; libuv polls it | holds a callback; JS calls it when data arrives |
| `read()` | synchronous read from the kernel | synchronous read from the JS buffer |
| Buffer in the PTY object | none | none |

## Pointers

- Interface + backends: `include/yetty/yplatform/pty.h`
- Readiness sources: `include/yetty/yplatform/pty-pipe-source.h`,
  `pty-poll-source.h`
- Factory: `src/yetty/yplatform/pty-factory/`
