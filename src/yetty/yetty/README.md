# yetty — top-level terminal application

`yetty` is the terminal program itself: the `yapp:app` subclass the platform
bootstrap instantiates, plus the orchestrator that owns the tab strip, the
tiled workspaces and the render cycle. It sits on top of
[`yframework`](../yframework/README.md) (GPU/event/RPC services) and composes
[`yui`](../yui/README.md) (tabbar model, workspaces, chrome) around
[`yterminal`](../yterminal/README.md) views.

## Startup chain

```
ymain (../yplatform/ymain/<plat>.c)
  └─ yetty_yapp_create_app()            app.c — registers + builds yetty:app
       └─ run() override                app.c
            ├─ yetty_yplatform_pty_factory_create()
            ├─ yetty_yframework_create(platform)
            ├─ yetty_create(framework, pty_factory)   yetty.c
            ├─ initial RESIZE event (live framebuffer size → first frame)
            └─ yetty_run()  →  event_loop->start()    blocks until shutdown
```

On desktop, `run()` tears everything down after `yetty_run` returns
(yetty → yframework → pty factory). On webasm `event_loop->start()` is
non-blocking (the browser drives frames after `main()` returns), so the
teardown block is intentionally skipped — the runtime must live for program
lifetime.

`yetty_create` builds the `yetty_yui_tabbar_model` (loading the first
workspace from config), the app-level `yui` singleton (non-fatal on failure),
and wires the bridges between them: tabbar v-button → yui popup menu, yui
Connect/Split callbacks → tabbar `add_workspace_of_kind` / workspace split.
Under `--temu`/`--qemu` (and by default on webasm) a second tab is opened so
the user gets both the VM console and a telnet session out of the box.

## Event handling (yetty.c)

`yetty_event_handler` is the single listener on the event loop; it owns:

- **RENDER** — back-pressure skip when the target is still flushing an async
  readback; `yetty_yframework_frame_tick()` (shared animation clock); probe
  `yetty_yui_is_dirty()` *before* panes draw and force a full repaint when
  yui chrome will overpaint this frame; then tabbar render → yui render →
  `render_target->ops->present`.
- **WINDOW_REFRESH** — mark damage-aware targets fully dirty, re-dispatch as
  RENDER.
- **RESIZE** — reconfigure the surface via yframework, resize tabbar/yui,
  honouring the statusbar inset.
- **Visual zoom** — Ctrl+scroll emits ZOOM_VISUAL (shader-level, anchored at
  the cursor, scale clamped to [1, 100]); drag pans; Esc/Enter exits;
  keyboard is captured while zoomed. Ctrl+Shift+scroll becomes
  ZOOM_CELL_SIZE (font-size zoom) unless a wheel-subscribed figure under the
  cursor consumes it first.
- **SCREENSHOT** (Ctrl+F2) — capture via `yrender-utils/screenshot.h`,
  default path under the XDG data dir.
- **Chrome-driven model events** — WORKSPACE_CREATE, PANE_CREATE, PANE_SPLIT,
  SPLIT_RESIZE, CLOSE (closing the last pane of the last workspace escalates
  to SHUTDOWN), SHUTDOWN (window close and SIGINT/SIGTERM funnel here).
- **Input routing** — yui first (`yetty_yui_on_event`, menus/dialogs/widgets),
  then the tabbar model, which forwards to the active workspace's tile tree.

## Public API sketch

```c
#include <yetty/yetty/yetty.h>

struct yetty_yetty_yetty_result yetty_create(struct yetty_yframework *runtime,
                                             struct yetty_yplatform_pty_factory *pty_factory);
struct yetty_ycore_void_result  yetty_run(struct yetty_yetty_yetty *yetty);
struct yetty_ycore_void_result  yetty_destroy(struct yetty_yetty_yetty *yetty);
```

`yetty.h` also defines two structs used far beyond this module:
`struct yetty_yframework_gpu_context` (adapter/device/queue/format/allocator/
msdf on top of the platform GPU slice) and `struct yetty_context`
(runtime + pty_factory + event_loop — the context propagated down the
terminal hierarchy; see [`../../../docs/contexts.md`](../../../docs/contexts.md)).
That is why `yframework.h` includes `yetty/yetty.h` rather than the reverse.

## Files

| file | role |
|------|------|
| `app.c` | annotated yclass `yetty:app` (parent `yapp:app`): the `run()` override + `yetty_yapp_create_app` definition |
| `app.gen.c`, `rpc.gen.c`, `model.yaml` | codegen outputs — never hand-edit |
| `yetty.c` | the orchestrator: `yetty_create/run/destroy`, the event handler, zoom/screenshot/pane plumbing |
| `CMakeLists.txt` | INTERFACE target (`yetty_yetty`) for the header + webgpu dep; `yetty.c` compiles into the executable via `YETTY_SOURCES` to avoid archive-order pain |

## Cross-references

- [`../yapp/README.md`](../yapp/README.md) — the app base class and injection point
- [`../yframework/README.md`](../yframework/README.md) — everything `runtime` provides
- [`../yui/README.md`](../yui/README.md) — tabbar model, workspaces, tiles, chrome
- [`../yui-core/README.md`](../yui-core/README.md) — the view abstraction panes host
- [`../yrender/README.md`](../yrender/README.md) — render targets and present
- [`../../../docs/layered-rendering.md`](../../../docs/layered-rendering.md) — how layers and figures complex
