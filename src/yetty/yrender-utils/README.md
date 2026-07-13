# yrender-utils — GPU readback helpers: screenshot capture and tile-diff engine

Two self-contained WebGPU readback helpers that are reusable across render
targets and output sinks: a one-shot screenshot writer and a tile-granularity
frame-diff engine. Consumed by the top-level app (`yetty.c`, screenshots), the
VNC server ([yvnc](../yvnc/README.md)) and the X11-tile render target in
[yrender](../yrender/README.md). Depends only on the WebGPU C API plus the
yplatform coroutine / wgpu-await machinery (see [yco](../yco/README.md) and
[yplatform](../yplatform/README.md)).

## screenshot — texture → PPM file

`yetty_yrender_utils_screenshot_capture()` encodes a `CopyTextureToBuffer`
into a 256-byte-row-aligned readback buffer, submits it, and spawns a
coroutine that awaits the buffer map via
`yetty_yplatform_wgpu_buffer_map_await()`. Once mapped, rows are swizzled
(BGRA→RGB or RGBA→RGB) and written as a binary PPM (P6) — deliberately
codec-free so the module has no image-library dependency. Supported formats:
`BGRA8Unorm[Srgb]`, `RGBA8Unorm[Srgb]`; anything else is an error.

The call returns as soon as GPU work is submitted; the file write happens on
the loop thread when the map completes. Triggered today by the
`YETTY_YCORE_SCREENSHOT` event (fired by the yctl RPC server's screenshot op
and yetty's own handler, which defaults the path to
`<data-dir>/screenshots/yetty-<timestamp>.ppm`).

```c
struct yetty_ycore_void_result yetty_yrender_utils_screenshot_capture(
    WGPUDevice device, WGPUQueue queue, struct yetty_yplatform_wgpu *wgpu,
    WGPUTexture texture, const char *path);
```

## tile-diff — compute-shader frame diff + readback, sink-agnostic

The engine compares each submitted texture against the previously submitted
one with an embedded WGSL compute shader (one workgroup per `tile_size`×
`tile_size` tile, 64 px today; any differing pixel marks the tile dirty),
copies both the full frame and the per-tile dirty bitmap back to the CPU
inside a coroutine, and invokes a caller-supplied sink exactly once per
submit:

```c
struct yetty_yrender_utils_tile_diff_engine_ptr_result eng_res =
    yetty_yrender_utils_tile_diff_engine_create(device, queue, wgpu, 64);

yetty_yrender_utils_tile_diff_engine_submit(eng_res.value, texture,
                                            width, height, sink_fn, sink_ctx);

/* sink_fn(ctx, frame): frame->pixels (BGRA8, rows padded to
 * frame->aligned_bytes_per_row), frame->dirty_bitmap (1 byte/tile,
 * row-major), frame->dirty_count. Valid only during the callback. */
```

The engine owns the pipeline (built lazily on first submit), the
previous-frame texture, the dirty-flag storage buffer and the readback
buffers; per-size resources are reallocated when dimensions change. It knows
nothing about VNC, X11 or terminals — the sink decides what dirty tiles mean.

Concurrency contract: only one submit may be in flight. A second submit while
busy is dropped (concurrent readbacks would race on the shared buffers), so
callers gate on `_is_busy()` and record skipped renders with
`_mark_redraw_pending()`; the `_set_on_idle()` callback then fires a catch-up
render once the coroutine finishes — without it, dropped submits manifest as
one-keystroke display lag. `_force_full()` marks the next frame all-dirty
(e.g. a new VNC client), `_set_always_full()` disables diffing for debugging.

## Files

| file | role |
|------|------|
| `screenshot.c` | one-shot texture readback → binary PPM (P6) writer |
| `tile-diff.c` | compute-shader diff pipeline, readback coroutine, sink dispatch, busy/idle bookkeeping |

Public headers: `include/yetty/yrender-utils/screenshot.h`, `tile-diff.h`.

## Consumers

- `src/yetty/yetty/yetty.c` — `YETTY_YCORE_SCREENSHOT` event handler.
- `src/yetty/yvnc/vnc-server.c` — tile-diff engine feeding the VNC wire
  encoder (the engine was extracted from there for reuse).
- `src/yetty/yrender/render-target-x11-tile.c` — tile-diff engine feeding an
  `XShmPutImage` blitter.

## See also

- [yrender](../yrender/README.md) — render targets that drive the engine.
- [yvnc](../yvnc/README.md) — the original tile-diff consumer.
- [Layered rendering](../../../docs/layered-rendering.md) — where frames come
  from before they reach these helpers.
