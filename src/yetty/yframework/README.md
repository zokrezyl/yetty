# yframework — generic GPU / event / RPC services layer

`yframework` lifts the whole WebGPU + event-loop + services bring-up out of
the terminal so the same code serves any app that wants a window, a GPU and
an event loop. Given a populated `yplatform:platform` object it produces one
owned `struct yetty_yframework` that the app borrows. Consumers today: the
terminal app ([`../yetty`](../yetty/README.md)), the generic ygui host
([`../yguiapp`](../yguiapp/README.md)) and the standalone `yapp:app` tools.

## What create() builds

`yetty_yframework_create(platform)` borrows config, input pipe, clipboard and
window-chrome objects from the platform, then creates (owned, torn down in
reverse order by `yetty_yframework_destroy`):

1. **Event loop** (`yevent`) on the platform input pipe.
2. **Coroutine-aware wgpu await machinery** (`yplatform/ywebgpu.h`) — lets
   VNC and the texture render target yield instead of blocking on async
   WebGPU work (see [`../yco/README.md`](../yco/README.md)).
3. **Adapter → device → queue** — Dawn auto-picks the OS-best backend;
   `YETTY_WEBGPU_BACKEND=<vulkan|metal|d3d12|gl|…>` forces one (GL backends
   automatically request the Compatibility feature level, without which Dawn
   filters them out of discovery). Software/WARP adapters are detected and
   deprioritised. `YETTY_DAWN_DEBUG` enables extra device diagnostics.
4. **Surface configuration** — format picked from surface capabilities;
   present mode from the `rendering/present-mode` config key
   (mailbox / fifo-relaxed / immediate / fifo), remembered so RESIZE
   reconfiguration never silently flips back to Fifo.
5. **GPU allocator** and the polymorphic **MSDF generator** (cpu | gpu,
   selected by the `msdf/generator` config key).
6. Optional **VNC server** (`vnc/server` config; also a record-only mode)
   and the matching render target flavor — surface, texture (VNC), or
   x11-tile where compiled in.
7. **Render target** (see [`../yrender/README.md`](../yrender/README.md)).
8. **Memtag registry** — per-owner allocation accounting, dumped by the
   yctl `memtags` command; best-effort (may be NULL).
9. Optional **yctl RPC server** when `rpc/port` is set (host from
   `rpc/host`, default `127.0.0.1`) — the external control channel driven by
   `tools/yctl-client/yctl.py` (distinct from the in-app yclass RPC layer).

Lifetime rule: build platform → yframework → app; tear down app →
yframework → platform. The app must be gone before `yetty_yframework_destroy`
runs because pending readbacks dereference the wgpu await machinery and the
event loop.

## Frame clock and figure factories

`yetty_yframework_frame_tick()` stamps `frame_time_sec` / `frame_delta_sec`
once at the top of every render frame so every layer, figure and effect
shader in that frame samples the identical time value — effects stay
phase-coherent instead of each ticking from its own creation origin.

`yetty_yframework_register_figure_factories(framework, registry, context)`
registers every figure kind the framework ships (yshadertoy, ymgui when
enabled, yrdawn on non-webasm builds) onto a host-owned
`yetty_yfigure_registry`. The per-kind args bundles live on the opaque
`framework->factory_state`, so one framework can populate any number of
registries; hosts install their yrdawn callbacks first via
`yetty_yframework_factory_args_yrdawn()`.

## Public API sketch

```c
#include <yetty/yframework/yframework.h>

struct yetty_yframework_ptr_result yetty_yframework_create(struct yetty_yclass_object *platform);
struct yetty_ycore_void_result     yetty_yframework_destroy(struct yetty_yframework *rt);
void                               yetty_yframework_frame_tick(struct yetty_yframework *rt);
struct yetty_ycore_void_result     yetty_yframework_reconfigure_surface(
                                       struct yetty_yframework *rt, uint32_t w, uint32_t h);
struct yetty_ycore_void_result     yetty_yframework_log_gpu_info(WGPUAdapter a, WGPUSurface s);
struct yetty_ycore_void_result     yetty_yframework_register_figure_factories(
                                       struct yetty_yframework *framework,
                                       struct yetty_yfigure_registry *registry,
                                       const struct yetty_context *context);
```

The struct itself is public: apps read `rt->gpu.device`, `rt->event_loop`,
`rt->render_target`, `rt->config`, `rt->clipboard`, … directly. Note that
`struct yetty_yframework_gpu_context` is declared in `yetty/yetty.h` (which
this header includes) — a header-dependency quirk to be aware of.

## Files

| file | role |
|------|------|
| `yframework.c` | the whole bring-up: adapter selection, surface config, allocator/MSDF/VNC/RPC/render-target creation, teardown, figure-factory state |
| `CMakeLists.txt` | INTERFACE target (`yetty_yframework`); `yframework.c` compiles into the executable via `YETTY_SOURCES` |

## Cross-references

- [`../yplatform/README.md`](../yplatform/README.md) — the platform object that feeds create()
- [`../yevent/README.md`](../yevent/README.md) — the event loop
- [`../yvnc/README.md`](../yvnc/README.md) — the VNC server
- [`../yctl/README.md`](../yctl/README.md) — the RPC server / external control
- [`../yfigure/README.md`](../yfigure/README.md) — figure registry and kinds
- [`../../../docs/webgpu-architecture.md`](../../../docs/webgpu-architecture.md), [`../../../docs/contexts.md`](../../../docs/contexts.md)
