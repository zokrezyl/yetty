# Coroutines

Yetty uses stackful coroutines to write inherently-async code (GPU completion,
network I/O, multi-step protocols) as straight-line C, on top of the
single-threaded libuv event loop.

## Why

The original VNC readback was the canonical case:

```c
wgpuBufferMapAsync(...);
wgpuBufferMapAsync(...);
while (server->flags_map_pending || server->pixels_map_pending)
    wgpuInstanceProcessEvents(server->instance);   /* blocks the entire loop */
read_mapped();
unmap();
```

The whole event loop — PTY reads, input, all other terminals, render — froze
for the GPU round-trip. The natural ways out are all painful in plain C:

- **State-machine across N callbacks** — every protocol step becomes its own
  function sharing a context struct; reading the code requires reconstructing
  the implicit state machine in your head.
- **Threads** — true concurrency, but Dawn's Vulkan backend is not
  thread-safe for arbitrary concurrent calls (we crashed the first version of
  the GPU poll thread this way).
- **Polling everywhere** — we already had this, it doesn't scale.

Coroutines let the same code stay linear:

```c
yplatform_wgpu_buffer_map_await(wgpu, server->dirty_flags_readback, ...);
yplatform_wgpu_buffer_map_await(wgpu, server->tile_readback_buffer, ...);
read_mapped();
unmap();
```

While the coroutine yields, the libuv loop keeps running everything else.
When the wgpu callback fires, the coroutine resumes at its yield point.

## Mental model

- **Single-threaded execution.** All coroutines run on the libuv loop thread.
  Inside a coroutine, the rest of yetty is paused until you yield. There is
  no preemption — every yield is an explicit interleaving point.
- **Cross-thread wakeups go through `post_to_loop`.** GPU callbacks may fire
  on Dawn worker threads or on the loop thread itself; either way the
  callback posts a "resume coroutine N" request via the thread-safe
  `event_loop->ops->post_to_loop`. The loop thread drains and resumes.
- **Coroutines are spawned, not "called".** `yplatform_coro_spawn` creates a
  suspended coroutine; `yplatform_coro_resume` starts/continues it.
- **Fire-and-forget ownership.** When a coroutine yields into the GPU
  pipeline, it owns itself: `resume_coro_on_loop` calls `_destroy` once the
  entry function returns. Synchronous-completion coroutines (those that never
  yield) are destroyed by their spawner.

## Architecture

```
                      ┌─────────────────────────────────────┐
                      │  application code                   │
                      │   spawns coros, calls *_await       │
                      └─────────────────────────────────────┘
                                       │
                                       ▼
        include/yetty/yplatform/ywebgpu.h    (await API)
        include/yetty/yplatform/ycoroutine.h (coro API)
                                       │
                ┌──────────────────────┴────────────────────┐
                ▼ desktop                                   ▼ webasm (TBD)
   src/yetty/yplatform/shared/                    src/yetty/yplatform/webasm/
     ycoroutine.c   (libco via yco/co.h)            ycoroutine.c (emscripten_fiber_t)
     ywebgpu.c      (libuv timer + post_to_loop)    ywebgpu.c    (JS event loop)
                                       │
                                       ▼
              src/yetty/yco/co.c — ydebug-instrumented libco wrappers
                                       │
                                       ▼
              build-tools/cmake/libs/co.cmake → CPM(higan-emu/libco)
```

Key invariants:

- A coroutine only ever runs on the event loop thread.
- The GPU "tick" (a 1ms libuv timer that calls `wgpuInstanceProcessEvents`)
  runs **only when at least one `_await` is in flight**. Outside the bounded
  await window there is no `ProcessEvents` activity at all, so non-coro code
  paths (ydraw, normal rendering) see Dawn behaving exactly as before.
- Callback mode is `WGPUCallbackMode_AllowSpontaneous`: Dawn may also
  deliver callbacks from its own worker threads, in which case `post_to_loop`
  routes them back to the loop. The application never sees the difference.

## What's implemented

### Headers

- `include/yetty/yplatform/ycoroutine.h` — coroutine API: `spawn`, `yield`,
  `resume`, `destroy`, `current`, `is_finished`, `id`, `name`,
  `get_status` / `set_status`.
- `include/yetty/yplatform/ywebgpu.h` — wgpu await API:
  `yplatform_wgpu_create` / `_destroy`, `yplatform_wgpu_buffer_map_await`,
  `yplatform_wgpu_queue_done_await`.
- `include/yetty/yco/co.h` — thin libco wrappers (`yetty_yco_active`,
  `_create`, `_delete`, `_switch`) with `ydebug` instrumentation. Strip or
  inline once the layer is fully trusted.

### Implementations (desktop)

- `src/yetty/yplatform/shared/ycoroutine.c` — desktop coroutine impl on top
  of libco. Single-thread `g_current` tracks the active coroutine on the
  loop thread (NB: this is per-thread, not a cross-process global). Default
  stack 1 MiB.
- `src/yetty/yplatform/shared/ywebgpu.c` — desktop wgpu await impl.
  - `struct yplatform_wgpu` owns instance, loop reference, tick timer,
    pending-await counter.
  - 1 ms libuv timer driving `ProcessEvents` on the loop thread, started
    only while `pending_awaits > 0`.
  - Two awaits: `buffer_map_await` and `queue_done_await`.
  - Per-await context is stack-allocated on the awaiting coroutine and
    carries both the coro pointer and the wgpu pointer back to the
    callback — no globals.
- `src/yetty/yco/co.c` — libco wrapper module. Each function logs entry
  via `ydebug` for tracing.
- `src/yetty/yco/CMakeLists.txt` — builds `yetty_yco`, links `co`.

### Event loop additions

- `include/yetty/yevent/event-loop.h` — added `post_to_loop` op:
  `void (*post_to_loop)(struct yetty_ycore_event_loop *self,
                        void (*fn)(void *), void *arg)`.
  Thread-safe; may be called from any thread; `fn(arg)` runs on the loop
  thread.
- `src/yetty/yplatform/shared/libuv-event-loop.c` — implementation:
  mutex-protected single-linked queue of `(fn, arg)` nodes,
  drained by an `uv_async_t` callback after each `uv_async_send`.

### Build

- `build-tools/cmake/libs/co.cmake` — CPM fetch of `higan-emu/libco`.
  Skipped on emscripten.
- `build-tools/cmake/variables.cmake` — `YETTY_ENABLE_LIB_LIBCO ON`.
- `build-tools/cmake/targets/shared.cmake` — includes `co.cmake`.
- `build-tools/cmake/targets/webasm.cmake` — forces `LIBCO OFF`.
- `build-tools/cmake/targets/linux.cmake` — adds `ycoroutine.c` and
  `ywebgpu.c` to platform sources, links `yetty_yco`.
- `src/yetty/CMakeLists.txt` — `add_subdirectory(yco)` (non-emscripten only).

### Wiring

- `struct yetty_yetty_yetty` owns a `struct yplatform_wgpu *wgpu` member.
- Created in `yetty_create` **before** `init_webgpu` (so the VNC server
  picks it up), destroyed in `yetty_destroy` **before** the event loop.
- `yetty_vnc_server_create` takes the `wgpu` pointer; the server stashes it
  for the readback coroutine to pass into the awaits.

### Migrated code

- **VNC readback** (`src/yetty/yvnc/vnc-server.c`): the public
  `yetty_vnc_server_send_frame_gpu` is now a thin spawner that builds an
  args struct, spawns `vnc_send_frame_gpu_coro_entry`, resumes once. The
  coro body does encode + submit synchronously (so the texture stays valid),
  then yields at each `_await`. The previous busy-wait
  `while (...) wgpuInstanceProcessEvents(server->instance);` is gone.

## Lifetime / cancellation

The current implementation has **no cancellation model**. If an owner
(e.g. a VNC session) is destroyed while one of its coroutines is suspended
in `_await`, the resume callback will touch freed memory.

Acceptable for the POC because the VNC readback is fast and the only
in-flight coroutine; not acceptable once coroutines are common. The plan
is **scope-based** structured concurrency: each long-lived owner gets a
"coroutine scope"; spawned coros belong to it; `_destroy` walks the scope
and resumes every suspended coro with a cancellation status that propagates
as the normal `Result` error path.

## Debugging

- Every libco wrapper call goes through `yetty_yco_*` in `src/yetty/yco/co.c`
  which emits `ydebug` for `active`/`create`/`delete`/`switch`. Trace shows
  the exact stack-switch transitions.
- `yplatform_coro_*` functions emit `ydebug` for `spawn`, `resume`, `yield`,
  `destroy`, including coro id and name (e.g. `coro 1 (vnc-send-frame)`).
- `ywebgpu` await wrappers log `coro=N buffer=... offset=... size=...` at
  registration and `status=N msg="..."` (the wgpu `WGPUStringView` message)
  on callback. **Do not** silence the `msg` argument — it's the failure
  context.

Run with `YTRACE_DEFAULT_ON=yes` to see the full trace.

A post-POC convenience would be a `yplatform_coro_dump_all()` that walks a
global registry and prints id / name / wait-reason / spawn-site for every
live coroutine; not implemented yet.

## Roadmap

The coro primitive and the wgpu awaits are in place. Each new use case
needs a small `_await` wrapper for the underlying async source. Listed by
expected payoff.

### Tier 1 — high payoff, painful otherwise

| Migration | Why | New await needed |
|---|---|---|
| **SSH handshake & channel ops** (yssh) | Multi-step state machine: version exchange, KEX, key derivation, auth, channel open. ~80 lines of linear C reads like the RFC. | `tcp_read_await`, `tcp_write_await` |
| **VNC RFB handshake** (yvnc) | Protocol version → security types → optional auth → server init. Currently fragmented across `on_data` callbacks. | same TCP awaits as SSH |
| **PTY reader main loop** (per terminal) | Parser state for OSC/CSI/DCS sequences moves out of long-lived structs and into coro locals. OSC handlers that need to wait (image decode + GPU upload, clipboard, kitty graphics) become child coros. | `pty_read_await` |
| **Frame render pipeline as a coroutine** (per render target) | `for(;;) { await render_request; encode(); submit(); await queue_done; release(); }` — unlocks proper N-frames-in-flight pipelining. | already have `queue_done_await`; add `render_request_await` |

### Tier 2 — meaningful cleanup

| Migration | Why | New await needed |
|---|---|---|
| **VNC client write loop** (per client) | `for(;;) { update = await dirty(); await write(update); }` — backpressure becomes "await takes longer", replacing manual queue management. | `tcp_write_await`, `vnc_dirty_await` |
| **GPU init at startup** | `init_webgpu` currently does `while (!adapter_ready) emscripten_sleep(0);` — straight-line coro version is cleaner. | `wgpu_request_adapter_await`, `wgpu_request_device_await` |
| **PTY child process lifecycle** | fork → spawn → register exit → drain → reap. Multi-step, currently fragmented. | `pty_exit_await` |

### Tier 3 — opportunistic

- **Network reconnect / retry** (ssh, vnc client) — `try → on_fail await sleep(backoff) → retry`. Needs `sleep_await`.
- **Async file I/O sequences** at startup (config + fonts + shaders).
- **UI animations / transitions** — `for (t = 0..1) { advance(t); await next_frame(); }`.
- **yvideo decode → upload loop** — natural fit; audio sync is a sibling coro sharing a clock.

### Don't bother

- Single-callback events (key press → PTY write).
- Plain timer-only handlers with no state across ticks.
- The `request_render` async wakeup (already a one-shot signal).
- The binder's per-frame writes (sync, no yield).

### Recommended next step

`pty_read_await` + PTY reader as a coroutine. It unlocks the inline-parser
pattern that everything else (kitty graphics, OSC 52, yimage, yvideo when
wired through PTY) benefits from. SSH is a close second since it's an
active development target.

## Webasm

Implemented, but with a different mechanism than desktop:

- `src/yetty/yplatform/webasm/ywebgpu.c` — wgpu `_await` wrappers register a
  callback and busy-wait on a `volatile done` flag using `emscripten_sleep`.
  Asyncify (already enabled via `-sASYNCIFY` in `webasm.cmake`) suspends
  the entire C call stack at the sleep; the browser pumps the wgpu
  Promise from the JS event loop and the callback flips the flag, at
  which point asyncify resumes the C stack. No `ProcessEvents` tick, no
  cross-thread routing, no `post_to_loop`.
- `src/yetty/yplatform/webasm/ycoroutine.c` — degenerate stub. `spawn`
  allocates a control struct, `resume` runs the entry function inline,
  `yield` is a warn-only no-op. The asyncify-suspend inside `_await` is
  what gives the JS event loop air to breathe; no fiber stack needed.

The application API (`yplatform_coro_*`, `yplatform_wgpu_*_await`) is
identical to desktop. Trade-off: on webasm, `yplatform_coro_resume`
blocks (asyncify-suspended) until the entry returns rather than
returning at the first yield. In the GPU readback use case this is
observably equivalent — the browser's JS event loop continues running
during the suspension so input / `requestAnimationFrame` / PTY callbacks
all still fire.

If a future caller needs *real* concurrent coroutines on webasm
(multiple in-flight reads with overlapping yields), the stub would need
to be replaced with `emscripten_fiber_t` (`-sFIBERS`) or JSPI. For now
the GPU readback path is the only consumer and the stub is sufficient.

## File index

```
build-tools/cmake/libs/co.cmake               libco fetch (CPM)
build-tools/cmake/variables.cmake             + YETTY_ENABLE_LIB_LIBCO
build-tools/cmake/targets/shared.cmake        includes co.cmake
build-tools/cmake/targets/webasm.cmake        forces LIBCO OFF
build-tools/cmake/targets/linux.cmake         adds new sources, links yetty_yco

include/yetty/yco/co.h                        libco wrapper API
include/yetty/yevent/event-loop.h              + post_to_loop op
include/yetty/yplatform/ycoroutine.h          coroutine API
include/yetty/yplatform/ywebgpu.h             wgpu await API
include/yetty/yvnc/vnc-server.h               + wgpu param to create

src/yetty/CMakeLists.txt                      add_subdirectory(yco)
src/yetty/yco/co.c                            libco wrappers + ydebug
src/yetty/yco/CMakeLists.txt                  yetty_yco library
src/yetty/yetty.c                             owns wgpu, creates/destroys
src/yetty/yplatform/shared/ycoroutine.c       desktop coroutine impl
src/yetty/yplatform/shared/ywebgpu.c          desktop wgpu awaits
src/yetty/yplatform/shared/libuv-event-loop.c + post_to_loop queue
src/yetty/yplatform/webasm/ycoroutine.c       webasm coroutine stub (inline-resume)
src/yetty/yplatform/webasm/ywebgpu.c          webasm wgpu awaits (asyncify+sleep)
src/yetty/yvnc/vnc-server.c                   readback migrated to coroutine
```
