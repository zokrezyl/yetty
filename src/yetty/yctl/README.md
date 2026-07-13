# yctl — msgpack-RPC control server for terminal automation

`yctl` is the TCP server inside a running yetty that lets an external client
inject input events (keystrokes, mouse, resize) and query runtime state —
the mechanism behind driving a live yetty from scripts and tests. The
external client is `tools/yctl-client/yctl.py`; the server is created by
[yframework](../yframework/README.md) and only starts listening when
`rpc/port` is configured (the `-r <port>` flag). Built on the
[yevent](../yevent/README.md) TCP surface and msgpack-c; gated by
`YETTY_ENABLE_FEATURE_YCTL`.

Not to be confused with **yrpc** — the in-app yclass RPC / remote-object
layer that proxies objects over a wire (see
[yclass](../yclass/README.md)). yctl is the *external control* protocol.

## Wire protocol

msgpack-RPC arrays over TCP (default bind `127.0.0.1`, port from config):

```
Request:      [0, msgid, channel, method, params]
Response:     [1, msgid, error, result]
Notification: [2, channel, method, params]
```

Channels: `YETTY_YCTL_CHANNEL_EVENT_LOOP` (0) and
`YETTY_YCTL_CHANNEL_STREAM` (1). `rpc-message.c` parses frames
streaming-safely: `yetty_yctl_message_parse` reports bytes consumed so the
per-connection accumulator can extract several coalesced messages from one
read and carry a truncated tail over to the next — malformed bytes close
the connection.

## Server and handlers

```c
struct yetty_rpc_server_ptr_result srv_res = yetty_yctl_server_create(event_loop);
yetty_yctl_server_start(srv_res.value, "127.0.0.1", 9999);
yetty_yctl_server_register_handler(srv_res.value, channel, "my_method", handler_fn, userdata);
yetty_yctl_server_set_memtag_registry(srv_res.value, registry); /* enables `memtags` */
```

One handler per (channel, method); a handler receives the parsed message and
returns `struct yetty_rpc_handler_result` (msgpack data, bool, or error).
Built-in EventLoop-channel handlers unpack the params map, build a
`struct yetty_yui_event`, and dispatch it on the loop — the same path
kernel-level input takes:

| method | effect |
|--------|--------|
| `key_down` / `key_up` / `char` | keyboard events |
| `mouse_down` / `mouse_up` / `mouse_move` / `mouse_scroll` | pointer events |
| `resize` | window resize |
| `screenshot` | posts `YETTY_YCORE_SCREENSHOT` (frame → PPM on disk) |
| `shutdown` | clean shutdown |
| `memtags` | dumps the [ycore](../ycore/README.md) memtag registry as a text table |

## The client — `tools/yctl-client/yctl.py`

A `uv run` script (inline deps: msgpack, click, pyyaml). Subcommands cover
`run '<shell line>'`, `type`, single keys (`enter`, `tab`, `ctrl-c`, …),
mouse events, `resize`, `screenshot`, `memtags`, `shutdown`, and
`plays <script.yaml>` for replaying multi-step scenarios with human-like
typing. It is a fire-and-forget event channel — terminal *output* is
observed via trace logs or screenshots, not returned over RPC.

## File map

| file | role |
|------|------|
| `rpc-message.c` | streaming frame parser + response writers (`response_ok/error/bool`) |
| `rpc-server.c` | yevent TCP server, per-connection accumulator, handler table, built-in handlers |

The CMakeLists also compiles `yplatform/ipc-socket/{default,windows}.c` into
`yetty_yctl`. Status: the server itself is TCP-only today — the ipc-socket
sources and the reserved `rpc/socket-path` config key
(`YETTY_YCONFIG_KEY_RPC_SOCKET_PATH`) have no caller yet.

## Cross-references

- [yframework](../yframework/README.md) — creates/starts the server from `rpc/host` + `rpc/port`
- [yevent](../yevent/README.md) — the TCP + dispatch substrate
- [yconfig](../yconfig/README.md) — the `-r` flag → `rpc/port` mapping
