# yclient — libuv event loop for client apps

`yclient` is a small shared support library for programs that run *inside* a
yetty terminal and consume the OSC/yface byte stream on stdin — ymgui
frontends (`../ymgui/frontend/imgui_impl_yetty.cpp`), demo clients
(`demo/ymgui/01_demo_window`). It exposes one piece: a libuv-backed event
loop with GLFW-style typed input callbacks. Dependencies: `yface` (stream
decoder), `ycore`, libuv. Not built on emscripten (no libuv there).

Historical note: the library was called **yclient-lib** — the header banner
and several comments still carry that name, and the CMake `install(FILES …)`
rule still points at the old `include/yetty/yclient-lib/` path; the header
actually lives at `include/yetty/yclient/event-loop.h`.

## I/O model

```
stdin ── uv_poll(READABLE) ── read(2)
            └─ yetty_yface_feed_bytes()
                 ├─ on_osc(code, args, payload) → typed dispatch
                 │    mouse pos/button/wheel · resize · focus · key
                 └─ on_raw(bytes, n)           → app's raw cb (keyboard/CSI)
            └─ frame_pending = 1
uv_check (once per iteration) ── frame_pending? → frame_cb(user)
```

- Every typed event carries a `figure_id` — figures are placed sub-regions
  of the terminal grid; coordinates are figure-local pixels and the wire
  structs live in `<yetty/ymgui/wire.h>`. Buttons follow ImGui ordering.
- Frame coalescing: any number of input events in one loop iteration produce
  a single `frame_cb` tick, so the app does NewFrame/Render once per batch.
  `yetty_yclient_event_loop_request_frame()` forces a tick from any thread.
- Write-side queueing is deliberately *not* here — consumers register their
  own out fd via `add_fd(WRITABLE)` and remove it once drained.
- App extensions: fixed-size slots for polled fds (16), timers (16), and
  thread-safe posted tasks.

## Public API sketch

```c
#include <yetty/yclient/event-loop.h>

struct yetty_yclient_lib_event_loop_config cfg = {.in_fd = -1 /* stdin */, .user = app};
struct yetty_yclient_event_loop_ptr_result r = yetty_yclient_event_loop_create(&cfg);

yetty_yclient_event_loop_set_mouse_pos_cb(r.value, on_mouse_pos);
yetty_yclient_event_loop_set_key_cb(r.value, on_key);
yetty_yclient_event_loop_set_raw_cb(r.value, on_raw);       /* non-envelope bytes */
yetty_yclient_event_loop_set_frame_cb(r.value, on_frame);   /* coalesced tick */

int id = yetty_yclient_event_loop_add_timer(r.value, 16, 16, on_tick, app);
yetty_yclient_event_loop_run(r.value);        /* or _poll(loop, wait) per iteration */
yetty_yclient_event_loop_stop(r.value);       /* thread-safe, uv_async */
(void)yetty_yclient_event_loop_destroy(r.value);
```

`run` blocks until `stop`; `poll(loop, wait)` mirrors libuv's
`UV_RUN_ONCE` / `UV_RUN_NOWAIT` for apps that own their own outer loop.

## Files

| file | role |
|------|------|
| `event-loop.c` | the loop: stdin poll, yface dispatch, frame coalescing, fd/timer/task extension slots |
| `CMakeLists.txt` | STATIC lib `yetty_yclient`; links `yetty_yface`, `yetty_ycore`, `uv_a`; skipped on emscripten |

## Consumers

- `../ymgui/frontend/imgui_impl_yetty.cpp` — the Dear ImGui backend bridges
  the typed callbacks into ImGui IO.
- `demo/ymgui/01_demo_window/main.cpp` — minimal end-to-end client.

Note that [`../yguiapp`](../yguiapp/README.md)'s in-terminal host uses its
own libuv + `ywire_connection` path rather than this loop.

## Cross-references

- [`../yface/README.md`](../yface/README.md) — the stream decoder this loop owns
- [`../ymgui/README.md`](../ymgui/README.md) — wire structs and the figure protocol
- [`../ywire/README.md`](../ywire/README.md) — OSC envelope / escape-code background
