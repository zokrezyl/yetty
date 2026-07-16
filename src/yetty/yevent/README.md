# yevent — event-loop abstraction: typed events, listeners, timers, TCP

`yevent` defines the event vocabulary and the event-loop interface the whole
application runs on. It is almost entirely headers: the POD event union, the
listener contract, and the loop ops vtable live in `include/yetty/yevent/`;
the concrete implementations live in
[yplatform](../yplatform/README.md)`/libuv-event-loop/` (libuv on desktop and
mobile, an emscripten main-loop variant on webasm). The only compiled source
here is `dispatch.c`, two small wiring helpers.

## The event

`struct yetty_yui_event` (`event.h`) is a tagged POD union — deliberately
copyable by value so it can cross threads through the platform input pipe.
`enum yetty_yevent_event_type` covers input (key/char/mouse/scroll, a
loop-synthesized `MOUSE_DOUBLE_CLICK`), lifecycle (`RESIZE`, `RENDER`,
`WINDOW_REFRESH`, `SHUTDOWN`), tile-tree mutations (`WORKSPACE_CREATE`,
`PANE_CREATE`, `PANE_SPLIT`, `SPLIT_RESIZE` — carrying ids pre-minted with
`yetty_ycore_next_object_id` so chrome and workspace agree without a
round-trip), clipboard, zoom (`ZOOM_VISUAL*`, `ZOOM_CELL_SIZE`), screenshot,
and the CSD window-control requests (`WINDOW_DRAG_BY`,
`WINDOW_BEGIN_INTERACTIVE_MOVE/RESIZE`, …). Naming note: the enum values are
`YETTY_YCORE_*` and the struct is `yetty_yui_event` — historical prefixes
that predate the module split.

## The loop interface

`event-loop.h` declares `struct yetty_yevent_event_loop` (ops-vtable base)
with:

- **listeners** — embed `struct yetty_yevent_event_listener` as the first
  member of your struct, set `.handler`, register per event type with a
  priority. `dispatch` walks listeners until one returns handled (int
  Result); `broadcast` calls all of them.
- **PTY pipes** — `register_pty_pipe` with alloc/read callbacks (libuv
  `uv_read_start` underneath).
- **timers** — create/config/start/stop/destroy plus per-timer listeners
  (deregistration is safe from inside the handler).
- **TCP** — server and client creation with callback structs
  (`on_connect` / `on_alloc` / `on_data` / `on_disconnect`), plus
  `tcp_send` / `tcp_close` on the opaque `yetty_yevent_conn`.
- **thread hand-off** — `post_to_loop(fn, arg)` (any thread → loop thread,
  FIFO), `request_render`, and `post_fatal_error`, which turns a
  callback-boundary `struct yetty_ycore_error` into a user-visible
  [ynotify](../ynotify/README.md) notification without stopping the loop
  (ownership of the chain moves).

```c
struct yetty_ycore_event_loop_result loop_res = yetty_ycore_event_loop_create(input_pipe);
struct yetty_yevent_event_loop *loop = loop_res.value;

struct my_thing { struct yetty_yevent_event_listener listener; /* ... */ };
thing->listener.handler = my_handler;   /* yetty_ycore_int_result(listener, event) */
loop->ops->register_listener(loop, YETTY_YCORE_KEY_DOWN, &thing->listener, 0);
loop->ops->start(loop);
```

## dispatch.c

- `yetty_yevent_register_default_listeners(el, listener)` — subscribes the
  top-level yetty handler to the canonical ~20 event types (input, framing,
  zoom, paste, tile-tree mutations, close).
- `yetty_yevent_post_async(pipe, event)` — posts an event into the
  cross-thread input pipe so translated input (Ctrl+Scroll → `ZOOM_VISUAL`,
  yctl injections, key remapping) reuses normal dispatch.

## File map

| file | role |
|------|------|
| `event.h` | event type enum + per-event payload structs + the POD union |
| `event-loop.h` | loop/listener/TCP-callback interfaces, `yetty_ycore_event_loop_create` |
| `dispatch.h` / `dispatch.c` | default-listener registration, async post helper |

## Consumers

[yframework](../yframework/README.md) creates and owns the loop;
[yctl](../yctl/README.md) builds its RPC server on the TCP surface;
[yterminal](../yterminal/README.md) registers PTY pipes;
[yui](../yui/README.md), [yvnc](../yvnc/README.md), ydvnc, ypty, ywasmnet and
others use timers, TCP, and listeners. Implementations:
`src/yetty/yplatform/libuv-event-loop/{default,webasm}.c`.

See also [contexts.md](../../../docs/contexts.md) for where the loop sits in
the bootstrap chain.
