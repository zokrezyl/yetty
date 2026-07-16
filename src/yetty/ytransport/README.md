# ytransport — polymorphic byte-stream transport for PTYs

`ytransport` pulls the network/IPC details out of the application-level PTY
protocols so the same protocol code runs over real TCP, a browser WebSocket,
the in-browser lwIP netstack, or a postMessage channel into the TinyEMU
iframe. Consumers are [`ytelnet`](../ytelnet/README.md)'s telnet-pty,
[`yssh`](../yssh/README.md)'s ssh-websocket-pty, [`ypty`](../ypty/README.md)'s
websocket-pty, and the webasm PTY factory
(`../yplatform/pty-factory/webasm.c`); the only dependencies are `ycore`
result types and the [`yevent`](../yevent/README.md) callback structs.

## The contract

A transport is one client-side connection lifecycle: `open()` once, the
platform fires the caller's `yetty_yevent_tcp_client_callbacks`
asynchronously (`on_connect` → `on_data`\* → `on_disconnect`), then
`send()` / `close()` on the connection handle `on_connect` delivered.
The vtable lives in `include/yetty/ytransport/conn-transport.h`:

```c
struct yetty_ytransport_conn_transport_ops {
    int (*open)(struct yetty_ytransport_conn_transport *self,
                const struct yetty_yevent_tcp_client_callbacks *cb);
    struct yetty_ycore_size_result (*send)(struct yetty_ytransport_conn_transport *self,
                                           struct yetty_yevent_conn *conn,
                                           const void *data, size_t len);
    struct yetty_ycore_void_result (*close)(struct yetty_ytransport_conn_transport *self,
                                            struct yetty_yevent_conn *conn);
    void (*destroy)(struct yetty_ytransport_conn_transport *self);
};
```

The endpoint binding (host+port, ws URL, iframe port) is fixed at
`*_create()` time, so protocol layers never see addressing. PTY layers take
ownership of the transport they are handed and `destroy()` it on teardown.

## Backends

| backend | create | platform | wire |
|---|---|---|---|
| tcp | `yetty_ytransport_tcp_transport_create(host, port, event_loop)` | desktop / iOS / Android | event loop's TCP client API (libuv) |
| websocket | `yetty_ytransport_websocket_transport_create(url)` | webasm | one `send()` = one binary WebSocket message (framing preserved) |
| lwip | `yetty_ytransport_lwip_transport_create(host, port)` | webasm | lwIP `tcp_pcb` over the [`ywasmnet`](../ywasmnet/README.md) netstack (DNS-resolves, waits for DHCP, honours `tcp_sndbuf` backpressure with a pending ring) |
| iframe | `yetty_ytransport_iframe_transport_create(port)` | webasm | postMessage to the TinyEMU iframe, which injects a synthetic inbound TCP connection into slirp — many terminals share one in-VM telnetd |

Message-framing matters: `websocket-pty` (raw shell bytes + resize control
messages) may only sit on the websocket backend because it relies on
message boundaries; byte-stream protocols (telnet, ssh) run on any backend.

The iframe backend keys connections on a process-local `clientSid` and
exposes three `EMSCRIPTEN_KEEPALIVE` entry points
(`yetty_ytransport_iframe_transport_on_opened` / `_on_rx` / `_on_closed`)
that the page-side postMessage listener calls back into
(exported in `build-tools/yetty/platform/webasm/cmake.cmake`).

## File map

| file | role |
|---|---|
| `../../../include/yetty/ytransport/conn-transport.h` | the ops vtable + lifecycle contract |
| `tcp-transport.c` | TCP backend — **duplicate; the compiled copy is `../ycore/tcp-transport.c`** (see below) |
| `websocket-transport.c` | emscripten WebSocket backend |
| `lwip-transport.c` | lwIP-over-relay backend (DNS, connect-when-DHCP-bound, send backpressure) |
| `iframe-transport.c` | postMessage-into-slirp backend + clientSid demux table |
| `CMakeLists.txt` | **not wired into the build** (see below) |

## Build wiring — read this before touching CMake

This directory's `CMakeLists.txt` is never `add_subdirectory()`ed and even
names its library `yetty_ycore`; it is dead. The sources are actually built
elsewhere:

- **desktop**: the TCP backend is compiled from
  `src/yetty/ycore/tcp-transport.c` into `yetty_ycore`
  (`src/yetty/ycore/CMakeLists.txt`). The `tcp-transport.c` here is a
  byte-identical copy that no target compiles.
- **webasm**: `iframe-transport.c`, `websocket-transport.c` and
  `lwip-transport.c` are listed directly in the yetty executable's source
  list in `build-tools/yetty/platform/webasm/cmake.cmake`.

## Consumers

- `../ytelnet/telnet-pty.c` — telnet over any backend (desktop tcp, webasm iframe/websocket/lwip).
- `../yssh/ssh-websocket-pty.c` — encrypted SSH stream over websocket or lwip.
- `../ypty/websocket-pty.c` — raw PTY bytes, websocket backend only.
- `../yplatform/pty-factory/webasm.c` — picks the backend per session mode
  (`--telnet`, `--ssh`, `--websocket`, `--net-relay`, default TinyEMU).

See [`../yplatform/README.md`](../yplatform/README.md) for the PTY / pipe
plumbing these transports feed into.
