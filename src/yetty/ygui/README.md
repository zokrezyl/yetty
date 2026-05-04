# ygui — Retained-Mode GUI for yetty

A pure-C widget library that builds a tree of widgets, lays it out, draws
into a `ypaint-core` buffer, and ships the result to a running yetty
instance over OSC. The library is self-contained: no Qt, no GTK, no
ImGui-style immediate mode. Apps drive it via FFI from any language that
can call C.

## Concepts

- **Engine** — owns the widget tree, the ypaint-core buffer, the spatial
  hit-test grid, and the libuv loop that processes input and renders.
- **Widget** — a node in the tree (`button`, `label`, `panel`, `hbox`,
  `vbox`, …). Carries identity, geometry, styling, callbacks, and child
  links.
- **Card** — yetty's name for the surface our drawing lands on. The engine
  owns one card; its position and cell dimensions are set at construction
  and updated on terminal resize.
- **Layout pass** — flexbox-style geometry resolution that runs **before**
  rendering. It reads each widget's authored `x/y/w/h` plus its
  `struct yetty_ygui_layout` and writes resolved `layout_x/y/w/h` (absolute
  pixels) and live `x/y/w/h` (relative-to-parent). Render functions and
  the spatial grid both consume the resolved values.
- **Render pass** — walks the tree depth-first and emits SDF primitives
  and text spans into the ypaint-core buffer.
- **OSC sink** — every render serializes the buffer (LZ4 + base64) and
  ships it to yetty as a single OSC envelope. Yetty decodes and draws.

The render context (`ctx->offset_x/offset_y`) accumulates each container's
absolute origin so render functions can keep drawing in their own
relative coordinate system.

## Quick start

```c
#include <yetty/ygui/ygui.h>

int main(void) {
    yetty_ygui_init();

    struct ygui_engine_ptr_result r =
        yetty_ygui_engine_create("hello", 0, 0, /*cols*/ 80, /*rows*/ 24);
    if (YETTY_IS_ERR(r)) { return 1; }
    struct yetty_ygui_engine *engine = r.value;

    struct yetty_ygui_widget *btn =
        yetty_ygui_engine_button(engine, "ok", 20, 20, 120, 32, "OK");
    yetty_ygui_widget_button_on_click(btn, /* callback */ NULL, NULL);

    yetty_ygui_engine_show(engine);   /* creates the card on yetty */
    yetty_ygui_engine_run(engine);    /* libuv loop, blocks until stop */

    yetty_ygui_engine_destroy(engine);
    yetty_ygui_shutdown();
    return 0;
}
```

Build a demo with the existing CMake helpers (see `demo/ygui/CMakeLists.txt`).
All public symbols live in `include/yetty/ygui/ygui.h`.

## Widget tree

```
engine
 ├── widget A         (top-level, x/y absolute)
 ├── widget B
 │    ├── child       (relative to B)
 │    └── child
 └── widget C
```

- `yetty_ygui_widget_add_child(parent, child)` — moves `child` from the
  engine's top-level list into `parent->first_child`.
- `yetty_ygui_widget_remove_child` / `yetty_ygui_widget_remove` — detach
  or destroy.
- `engine_find` looks up by id; `engine_widget_at(x, y)` does an O(1) hit
  test against the spatial grid (hit testing reads `layout_*`).

## Geometry model

Each widget keeps three coordinate sets:

| Field           | Meaning                                        | Set by              |
|-----------------|------------------------------------------------|---------------------|
| `authored_*`    | What the user asked for                        | constructors, setters |
| `x, y, w, h`    | Live geometry (relative to parent)             | layout pass        |
| `layout_*`      | Resolved absolute box                          | layout pass        |
| `content_*`     | `layout_*` minus padding (inner box)           | layout pass        |
| `effective_x/y` | Legacy alias for `layout_x/layout_y`           | layout pass        |

Render functions and the spatial grid use `layout_*` (or the alias). The
authored values stay stable across resize / re-layout — the layout pass
recomputes resolved values from authored inputs every render.

## Layout: flexbox subset

Container layout mode is `MANUAL` by default — children sit at their
authored relative `x/y` (matches absolute-positioning UIs).

To opt in to flex, set `layout.mode = FLEX` (or use `hbox` / `vbox`, which
are pre-configured row / column flex containers).

Per-widget `struct yetty_ygui_layout` fields:

```c
ygui_layout_mode_t    mode;            /* MANUAL | FLEX */
ygui_flex_direction_t direction;       /* ROW | COLUMN */
ygui_justify_t        justify_content; /* main-axis distribution */
ygui_align_t          align_items;     /* cross-axis alignment, all children */
ygui_align_t          align_self;      /* per-child override (AUTO = inherit) */
float flex_grow, flex_shrink, flex_basis;
float gap;
float padding_top, padding_right, padding_bottom, padding_left;
float margin_top,  margin_right,  margin_bottom,  margin_left;
float min_w, min_h, max_w, max_h;
```

Setters mirror those fields:

```c
yetty_ygui_widget_set_layout_mode(w, YETTY_YGUI_LAYOUT_FLEX);
yetty_ygui_widget_set_flex_direction(w, YETTY_YGUI_FLEX_ROW);
yetty_ygui_widget_set_justify_content(w, YETTY_YGUI_JUSTIFY_SPACE_BETWEEN);
yetty_ygui_widget_set_align_items(w, YETTY_YGUI_ALIGN_STRETCH);
yetty_ygui_widget_set_align_self(child, YETTY_YGUI_ALIGN_CENTER);
yetty_ygui_widget_set_flex(child, /*grow*/ 1, /*shrink*/ 0, /*basis*/ 0);
yetty_ygui_widget_set_gap(w, 12.0f);
yetty_ygui_widget_set_padding(w, top, right, bottom, left);
yetty_ygui_widget_set_margin(w, top, right, bottom, left);
yetty_ygui_widget_set_min_size(w, min_w, min_h);
yetty_ygui_widget_set_max_size(w, max_w, max_h);
```

Single-line flexbox semantics:

- **Main axis** is X for `ROW`, Y for `COLUMN`.
- Each child's main-axis size starts at `flex_basis`, falling back to the
  authored size on the main axis when `flex_basis <= 0`.
- Free space along the main axis is distributed via `flex_grow` (when
  positive) or `flex_shrink * basis` (when negative).
- `justify_content` adds leading offset and inter-child spacing for the
  unused remainder.
- Cross-axis size = authored size, or the container's content cross-size
  when the resolved align is `STRETCH`.
- Resolved sizes are clamped to `[min_*, max_*]`.

**Not implemented yet (deferred per issue #41):** wrapping, baseline
alignment, percent sizing, absolute-positioned children inside flex
containers, CSS parser.

### Manual mode

In `MANUAL`, children render at `parent.layout + (child.authored_x,
child.authored_y)` with `child.authored_w/h` size. No padding / gap
handling — the parent's `padding_*` is ignored. Use this for
absolute-positioned widgets (the default).

## Rendering pipeline

`engine_render` runs once per frame:

1. `handle_resize` if `needs_resize` is set (snapshots prev size, updates
   `engine->width/height`, fires `resize_callback`).
2. Clear the ypaint-core buffer.
3. **Layout pass** — `yetty_ygui_layout_compute_engine` walks the tree,
   resolving authored geometry into `layout_*` and `x/y/w/h`.
4. **Render pass** — each widget's `render` is called via `render_all`
   (default implementation handles offset bookkeeping and skips invisible
   widgets).
5. **Spatial grid** — top-level widgets are inserted into the grid at
   their `layout_*` boxes. Inner traversal recurses through children.
6. Serialize the buffer; ship over OSC.

`yetty_ygui_engine_layout(engine)` runs only step 3 — useful in tests and
headless tools that need post-flex geometry without a full render.

## Resize handling

Engine-level:
- The card always tracks the host terminal's cell count (the
  `prepare_cb` SIGWINCH path calls `ioctl(TIOCGWINSZ)` and re-emits
  `osc_card_place` with the new dims).
- Yetty replies with `OSC 777780` carrying the new card pixel size; that
  sets `needs_resize = 1; dirty = 1`.
- The next render runs `handle_resize`, updates `engine->width / height`,
  and fires the user's `resize_callback`.

`SCALE_ON` is currently a no-op (issue #41 retired the destructive
in-place scaling that drifted on repeated resizes; layout-driven scaling
is a follow-up).

User-facing API:

```c
void yetty_ygui_engine_set_size(struct yetty_ygui_engine *e, float w, float h);
void yetty_ygui_engine_get_size(const struct yetty_ygui_engine *e,
                                 float *w, float *h);
void yetty_ygui_engine_on_resize(struct yetty_ygui_engine *e,
                                 ygui_resize_callback_t cb, void *userdata);
```

The resize callback receives the new and previous canvas dims:

```c
void on_resize(struct yetty_ygui_engine *e,
               float new_w, float new_h,
               float prev_w, float prev_h,
               void *userdata);
```

The typical pattern in a demo is to resize the top-level flex container
to the canvas inside the callback (see `demo/ygui/19_flex_dashboard`).

## Error handling

Every fallible function returns a Result type (see `docs/result.md`):

```c
struct yetty_ycore_void_result r = yetty_ygui_engine_render(engine);
if (YETTY_IS_ERR(r)) {
    /* r.error carries a heap-linked cause chain — destroy when done. */
    yetty_ycore_error_destroy(r.error);
}
```

Error wrapping must preserve the `cause` chain — pass the whole result
struct, never `.error.msg` (see `~/.claude/yetty-new/rules/07-error-handling.md`).

## File layout

| File              | Role                                                     |
|-------------------|----------------------------------------------------------|
| `ygui_engine.c`   | Engine lifecycle, libuv loop, SIGWINCH, OSC parsing      |
| `ygui_layout.c`   | Flexbox layout pass                                      |
| `ygui_widgets.c`  | Widget allocation, hierarchy, all built-in widget types  |
| `ygui_render.c`   | Render-context drawing helpers (boxes, text, circles)    |
| `ygui_grid.c`     | Spatial grid for O(1) hit testing                        |
| `ygui_theme.c`    | Default theme + setters                                  |
| `ygui_osc.c`      | OSC encoding for card create / update / place            |
| `ygui_internal.h` | Engine + widget struct definitions, internal entry points|

Public API: `include/yetty/ygui/ygui.h`. Don't include `ygui_internal.h`
from outside the library.

## Testing

A small unit test exercises the layout pass headlessly. Enable with
`-DYETTY_ENABLE_FEATURE_TESTS=ON` and run via ctest:

```sh
cmake -B build-desktop-ytrace-release -DYETTY_ENABLE_FEATURE_TESTS=ON
cmake --build build-desktop-ytrace-release --target ygui-flex-layout-test
ctest -R ygui_flex_layout --test-dir build-desktop-ytrace-release
```

The test (`test/ut/ygui/ygui-flex-layout-test.c`) covers row-grow,
column-stretch, justify-space-between, padding, and manual-mode no-drift.

## Demos

`demo/ygui/` contains runnable demos. The flex-specific ones:

- `17_flex_row` — interactive horizontal flex; keys cycle
  `justify_content` / `align_items`, toggle grow on the middle child.
- `18_flex_column` — vertical version with `align_self` overrides.
- `19_flex_dashboard` — nested layout (toolbar + sidebar + content +
  status bar); `h`/`l` shrink and grow the sidebar's `flex_basis`.

Build with `make build-desktop-ytrace-release`; binaries land at
`build-desktop-ytrace-release/demo/ygui/<name>/<binary>`.

## Adding a new widget

1. Pick a `ygui_widget_type_t` value (`include/yetty/ygui/ygui.h`) — add a
   new enum entry if needed.
2. Add a constructor `yetty_ygui_engine_<type>(engine, id, x, y, w, h, …)`
   that calls `yetty_ygui_engine_widget_alloc` + `yetty_ygui_widget_init_base`,
   sets `render` (and optionally `render_all`, `on_press`, …), and calls
   `add_to_engine`.
3. Implement the `render` callback. Read `self->x/y/w/h` (already in the
   right coord system after layout) and call `yetty_ygui_render_ctx_*`
   helpers.
4. Add type-specific data inside the existing union in
   `struct yetty_ygui_widget` if you need state.
5. Add public setters/getters mirroring existing widget patterns.

If your widget is a layout container, set `widget->layout.mode = FLEX`
(or whatever applies) in the constructor and let the layout pass do the
positioning — don't mutate child geometry from `render_all`.
