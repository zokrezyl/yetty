# ytelnet — telnet protocol as a PTY backend

`ytelnet` implements a telnet client (RFC 854 protocol, RFC 856 binary,
RFC 858 suppress-go-ahead, RFC 1073 NAWS window size, plus a terminal-type
reply per RFC 1091) and surfaces it as a `yetty_platform_pty`. The protocol
state machine is transport-agnostic: the byte stream underneath is a
[`ytransport`](../ytransport/README.md) `conn_transport` supplied at
construction, so the same code serves desktop TCP, the TinyEMU iframe
channel, a browser WebSocket bridge, and the in-browser lwIP netstack.

## How it works

- `yetty_ytelnet_telnet_pty_create(transport)` takes ownership of the
  transport and kicks off `transport->open()`; connect completes
  asynchronously when the transport fires `on_connect`.
- Inbound bytes run through a per-byte IAC state machine
  (`DATA / IAC / WILL / WONT / DO / DONT / SB / SB_IAC`). Option handling:
  agrees to server ECHO / SGA / BINARY, announces NAWS and TTYPE when
  asked, refuses everything else. Once the server negotiates `DO NAWS`,
  every `resize()` ships an updated NAWS subnegotiation.
- Decoded output bytes are written to a `yetty_yplatform_input_pipe` whose
  read fd is registered with the event loop via the PTY `pipe_source` —
  the same path fork-pty / conpty use (see
  [`../yplatform/README.md`](../yplatform/README.md)). If the pipe backs
  up, a producer-side overflow ring buffers the excess and drains it
  opportunistically instead of dropping bytes.

## Public API sketch

```c
/* Protocol over an arbitrary transport (transport is owned). */
struct yetty_yplatform_pty_ptr_result yetty_ytelnet_telnet_pty_create(
    struct yetty_ytransport_conn_transport *transport);

/* TCP convenience — builds the tcp transport internally. */
struct yetty_yplatform_pty_ptr_result yetty_ytelnet_telnet_pty_create_tcp(
    const char *host, uint16_t port, struct yetty_yevent_event_loop *event_loop);

/* PTY-factory shape: event loop arrives later via create_pty(). */
struct yetty_yplatform_pty_factory_ptr_result yetty_ytelnet_telnet_pty_factory_create(
    const char *host, uint16_t port);
```

## File map

| file | role |
|---|---|
| `telnet-pty.c` | state machine, option negotiation, transport callbacks, overflow ring, PTY ops |
| `telnet-pty.h` | the public API (see header-shadowing note below) |
| `telnet-protocol.h` | RFC 854/1073 command and option constants |
| `CMakeLists.txt` | `yetty_telnet` static lib, gated on `YETTY_ENABLE_FEATURE_TELNET` |

## Consumers

- `../yplatform/pty-factory/default.c` — `--telnet host:port`, the second
  and subsequent `--temu` panes (telnet to the slirp hostfwd of the in-VM
  telnetd), and the `--qemu` console/telnet chardev ports.
- `../ypty/conpty.c` — the same `--telnet` / `--qemu` routes on Windows.
- `../yplatform/pty-factory/webasm.c` — telnet over the iframe transport
  (default TinyEMU mode), a WebSocket bridge, or the lwip netstack.

## Known discrepancies

- `include/yetty/ytelnet/telnet-pty.h` is a **stale copy** of the public
  header: it still declares the pre-refactor, unprefixed
  `telnet_pty_create(host, port, event_loop)`. It is harmless only because
  every build puts `-I<root>/src` before `-I<root>/include`, so
  `<yetty/ytelnet/telnet-pty.h>` resolves to the current header in this
  directory. The `include/` copy should be updated or removed.
- `yetty_ytelnet_telnet_pty_factory_create` currently has no in-tree
  caller (the factories in yplatform construct PTYs directly).
