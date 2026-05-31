# yvnc — VNC server/client design

## 1. Is (current design)

### Threading

Single-threaded. The VNC server runs entirely on the main libuv event loop.

### Data path per frame

On every `YETTY_EVENT_RENDER`:

1. `yetty_event_handler` renders the workspace into the render target.
2. `render_target->ops->present()` is called.
3. For the VNC render target, `present()` calls
   `yetty_vnc_server_send_frame_gpu(server, texture, w, h)`.
4. `send_frame_gpu` runs an async state machine:
   - **CAPTURE_IDLE**: builds one command buffer with
     clear-flags + compute-diff + copy-flags-to-readback +
     copy-texture-to-prev + copy-texture-to-tile-readback. Submits.
     Registers `wgpuBufferMapAsync` for both readback buffers.
     Calls `wgpuInstanceProcessEvents` (useless — GPU just submitted).
     Transitions to `CAPTURE_WAITING_DATA`. Returns.
   - **CAPTURE_WAITING_DATA**: calls `wgpuInstanceProcessEvents`. If
     map callbacks have fired, consumes the pixels, JPEG-encodes
     dirty tiles, sends over TCP. Sets state back to IDLE and
     **recursively** calls `send_frame_gpu` with the same texture,
     starting the next pipeline.
5. Map callbacks call `uv_async_send(gpu_async)` when they fire, but
   they only fire from inside `wgpuInstanceProcessEvents` — and the
   only calls to `wgpuInstanceProcessEvents` live inside
   `send_frame_gpu` itself.

### Why there is a one-keystroke display lag

Dawn native has no background thread (verified in
`src/dawn/native/` — `std::thread` only appears in the GL context
for ownership tracking; no polling thread). `AllowSpontaneous`
callbacks fire from inside `ProcessEvents` / `DeviceTick` /
`WaitAny`, not spontaneously.

In the current code, the only calls to `wgpuInstanceProcessEvents`
are in `send_frame_gpu`, which is called only from `present()`,
which runs only on `YETTY_EVENT_RENDER`, which fires only on
`request_render`, which is triggered by user input.

Sequence:

- User types char N → render → submit pipeline N → ProcessEvents
  (GPU not done yet, nothing happens) → return.
- Main loop blocks in `poll()`. GPU finishes. Nothing ticks Dawn.
  The map callbacks sit pending.
- User types char N+1 → render → `send_frame_gpu` enters
  `CAPTURE_WAITING_DATA` → ProcessEvents → **now** pipeline N's
  callbacks fire → pipeline N's pixels (char N content) are
  consumed and shipped. Recursive call submits pipeline N+1.

The client receives char N's tiles at the moment char N+1 is typed.
Every keystroke ships the previous keystroke's screen.

---

## 2. Should (proposed design)

Move VNC capture and send to its own thread. Double-buffer the
capture texture so the render thread never stalls waiting for
VNC.

### Threads

- **Render thread** — the existing main libuv event loop.
  Owns the pool of render textures, renders into them, hands
  freshly-rendered textures to the VNC thread.
- **VNC thread** — a dedicated thread created by the VNC server.
  Owns its own blocking loop. Receives a texture handle per frame,
  performs GPU capture + readback + JPEG + TCP send, then returns
  the texture to the pool.

### Texture pool

Two textures (`tex_A`, `tex_B`) allocated by the VNC render target.
Invariant: at any moment, one is owned by the render thread (the
"current target"), one by the VNC thread (in flight), or both are
free. On each render, the render thread picks the free one, renders
into it, hands it to VNC, takes the other one as the next target.

If the VNC thread is still busy when the render thread tries to
render again, the render thread blocks on a condition variable
until VNC releases a texture. In practice, yetty is event-driven;
the human typing interval dwarfs the GPU+encode latency, so this
will almost never block.

### Handoff channel

A small thread-safe slot:

- `uv_mutex_t` + `uv_cond_t`
- state: `{ WGPUTexture tex, uint32_t w, uint32_t h }` or `NULL`
  meaning "nothing pending".

Render thread after submit: lock, write slot, signal, unlock.
VNC thread waits on the cond, takes the slot, clears it.
Release-back uses a second slot (or an atomic free-mask) so the
render thread knows when a texture is free again.

### Render thread, per frame

1. Wait for a free texture from the pool. Call it `cap_tex`.
2. Build one `WGPUCommandEncoder`:
   - record the workspace render pass into `cap_tex`;
   - if mirror mode, record a blit `cap_tex` → surface_view in
     the same encoder.
3. `wgpuCommandEncoderFinish` + `wgpuQueueSubmit`.
4. If mirror mode: `wgpuSurfacePresent(surface)`.
5. Push `cap_tex` onto the handoff channel to VNC thread.
6. Pick the other pool texture as the next target.
7. Return to the event loop.

### VNC thread, per frame

1. Block on handoff channel. Wake with `cap_tex`.
2. Build its own `WGPUCommandEncoder`:
   - clear `dirty_flags_buffer`;
   - compute-diff pass (inputs `cap_tex`, `prev_texture`; output
     `dirty_flags_buffer`);
   - copy `dirty_flags_buffer` → `dirty_flags_readback`;
   - copy `cap_tex` → `prev_texture` (for next frame's diff);
   - copy `cap_tex` → `tile_readback_buffer`.
3. `wgpuCommandEncoderFinish` + `wgpuQueueSubmit`.
4. `wgpuBufferMapAsync` on both readback buffers.
5. `while (flags_pending || pixels_pending) wgpuInstanceProcessEvents(instance);`
   — this thread, blocking itself, is fine.
6. Read mapped ranges; unmap; JPEG-encode dirty tiles; send over
   `tcp_send` to each client. (TCP send from the VNC thread uses a
   thread-safe path — see "TCP from VNC thread" below.)
7. Release `cap_tex` back to the pool and signal the render thread.

### Ordering guarantee

Render thread step 3 (render submit) **happens-before** step 5
(channel push) **happens-before** VNC thread's step 1 (channel pop)
**happens-before** VNC thread's step 3 (capture submit). Dawn
queue executes submits in submission order → the GPU always runs
render commands before capture commands → the capture reads the
completed frame. No race.

### Dawn thread safety

Dawn native serializes device/queue access with an internal lock.
Concurrent `wgpuQueueSubmit` and `wgpuInstanceProcessEvents` from
two threads are safe. No toggle required.

### TCP from VNC thread

The main-loop libuv TCP handles are not safe to call from another
thread. Options, in order of preference:

- **Send over the VNC thread's own sockets.** The VNC server owns
  its listener, so move the listener and all client sockets onto
  the VNC thread's loop. The VNC thread runs a small libuv loop
  that handles TCP and also, between frames, blocks on the handoff
  channel.
- Or: buffer tiles in the VNC thread, then post them to the render
  thread's loop via `uv_async_send` with a FIFO — and let the
  render thread do the actual `tcp_send`. Adds a copy; keeps the
  render thread doing TCP.

Preferred: **move the VNC server's sockets to the VNC thread**.
The VNC thread then owns everything VNC-related:

- listener socket
- per-client recv buffers
- input dispatch → bridged via `uv_async_send` + FIFO to the main
  thread so keyboard/mouse events enter the normal event-dispatch
  path on the main thread.

That gives a clean split:
- VNC thread: all GPU capture, TCP I/O, JPEG.
- Main thread: app logic, rendering, input dispatch.

### Lifecycle

- `yetty_vnc_server_create` allocates the pool, mutex, cond,
  channel, creates the VNC thread (but does not start it until
  `_start`).
- `_start` launches the thread's loop.
- `_stop` posts a sentinel (NULL texture with a shutdown flag),
  joins the thread, closes sockets.
- Resize: render thread posts a "drain" sentinel, waits for VNC
  thread to finish pending work, then reallocates the pool at the
  new size.

### Removed

- `enum capture_state` and all `CAPTURE_*` states.
- The recursive `send_frame_gpu`.
- `process_capture_state`, `on_gpu_async`, the `uv_async_t
  *gpu_async`.
- The `flags_map_pending` / `pixels_map_pending` volatiles.
- The stale-texture re-capture loop.
