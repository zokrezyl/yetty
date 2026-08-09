# ychrome — borderless-window chrome: gesture engine + host glue

Desktop yetty apps run in a borderless OS window (`GLFW_DECORATED = FALSE`)
and draw their own title bar, so the move/resize/maximize gestures the OS
would normally provide are re-implemented here. `ychrome` has two layers:
the `ychrome:chrome` yclass gesture engine (`chrome.c`) and a plain-C host
(`host.c`) that composites the engine's self-painted caption and an opaque
backdrop over any app's content. It depends on neither ygui nor yui —
factored out of yui's tab strip so every app (ygreeter, a ygui demo, a
bare tool) gets draggable / resizable / maximizable windows.

## The engine (`ychrome:chrome`)

Pure logic — no GLFW, no widgets, no rendering pipeline of its own. The
app decides the caption height and resize-border thickness (both in
**logical px**) and feeds mouse events in **logical coordinates**; the
engine hit-tests them and drives a `yplatform:window_chrome` yclass
object:

- press + drag inside the caption strip → interactive move
- double-click on the caption → toggle maximize
- press + drag a right/bottom edge band → interactive resize
- `render()` paints the caption strip (brand-palette fill plus SDF-drawn
  minimize/maximize/close glyphs — no font dependency) into a fresh
  drawable list; `hover_button()` + `edge_cursor_at()` power hover
  highlights and cursor shapes.

Forward an event to `handle_event` only after the app's own controls had
their chance; it returns 1 when chrome claimed it. Every slot is
`local@` — an in-process object, never proxied over RPC.

```c
yetty_ychrome_register();
struct yetty_yclass_object_ptr_result obj = yetty_ychrome_chrome_create(NULL);
yetty_ychrome_configure(obj.value, window_chrome_obj,
                        /*caption_height=*/34.0f, /*edge_size=*/8.0f,
                        YETTY_YCHROME_FLAG_ALL);   /* DRAG | RESIZE | MAXIMIZE */
yetty_ychrome_set_size(obj.value, width, height);  /* on every resize */
int consumed = yetty_ychrome_handle_event(obj.value, &event).value;
```

## The host (`host.c` / `include/yetty/ychrome/host.h`)

Reusable glue extracted from the ygui demo runner: owns one chrome engine
plus a backdrop and a caption figure, with two sinks over one engine:

- **LOCAL** (standalone apps) — the backdrop + caption are pinned ygrid
  figures in the app's yfigure container. Compiled only where ygrid /
  WebGPU exist (`YETTY_YCHROME_HAS_LOCAL`).
- **WIRE** (client / in-terminal apps) — the same figures are driven onto
  the hosting yetty's root figure container proxy via the generated typed
  yclass-RPC stubs (`yetty_yfigure_create_child` / `set_child_z` /
  `delete_child`). The opaque backdrop is what hides the host terminal's
  text under the app. GPU-free — builds on headless cross targets.

Surface: `yetty_ychrome_host_create` / `_create_wire`, `_handle_event`
(forwards to the engine and repaints the caption when the hover highlight
changes), `_resized`, `_clear` / `_resync` (wire-mode figure removal and
re-emission), `_chrome` (the underlying engine object), `_destroy`. The
host is the fb→logical adapter: window dimensions and mouse events come
in as **framebuffer px** (what the caller naturally has from the GPU
context and platform mouse plumbing) plus a `content_scale` captured at
create time; the host divides once and speaks pure logical to the
wrapped engine.

## Codegen layout

`chrome.c` is the hand-written annotated source (it `#include`s
`chrome.gen.c` at the foot). `chrome.gen.c`, `rpc.gen.c`, `model.yaml`,
and the public `include/yetty/ychrome/chrome.h` are codegen outputs —
never hand-edited. `host.c` / `host.h` are plain hand-written C with no
annotations. The `YETTY_YCHROME_FLAG_*` enum is defined in `chrome.c` and
reproduced into the generated header via the `expose` annotation.

## Consumers

- **yui** — the shared window chrome over the terminal UI (one host,
  composited over the same container as the tab strip)
- **yguiapp** — drives the engine directly (own caption compositing)
- **yrich** app runner, **ybrowser**'s browser-ui
- tools: ymaze, yjungle, yzoo, yhello, ygreeter, ycompositor

## Files

| file | role |
|------|------|
| `chrome.c` | annotated `ychrome:chrome` engine: gestures, caption paint, hit-testing |
| `chrome.gen.c` / `rpc.gen.c` / `model.yaml` | codegen outputs (never hand-edited) |
| `host.c` | LOCAL/WIRE sink glue: backdrop + caption figures over any app |

## See also

- `../yplatform/README.md` — the `window_chrome` platform backend the gestures drive
- `../../yclass/README.md` — the annotation/codegen framework
- `../yfigure/README.md`, `../ygrid/README.md` — the figure container and grid the host composites into
- `../yui/README.md` — the primary in-tree consumer
