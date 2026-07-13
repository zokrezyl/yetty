# yui-core — abstract view interface

`yui-core` is the tiny library at the root of the yui/yterminal dependency
graph. It defines `struct yetty_yui_view` — the contract every concrete view
(terminal, ydvnc viewer, …) implements — so that
[`../yui`](../yui/README.md) (which composes views inside tiles) and
[`../yterminal`](../yterminal/README.md) (which produces a concrete view)
can both depend on it while yui-core depends on neither. Only dependency:
`ycore` result types.

## The contract

A view is a vtable-dispatched base struct, embedded as the first member of
each concrete view (the structural-embedding polymorphism pattern from
[`../../../docs/c-coding-style.md`](../../../docs/c-coding-style.md)):

```c
struct yetty_yui_view_ops {
    struct yetty_ycore_void_result (*destroy)(struct yetty_yui_view *self);
    struct yetty_ycore_void_result (*render)(struct yetty_yui_view *self,
                                             struct yetty_ydraw_target *render_target,
                                             int force_redraw);
    struct yetty_ycore_void_result (*set_bounds)(struct yetty_yui_view *self,
                                                 struct yetty_yui_rect bounds);
    struct yetty_ycore_int_result  (*on_event)(struct yetty_yui_view *self,
                                               const struct yetty_yui_event *event);
};

struct yetty_yui_view {
    const struct yetty_yui_view_ops *ops;
    yetty_ycore_object_id id;
    struct yetty_yui_rect bounds;   /* {x, y, w, h} floats */
};
```

`render(force_redraw)`: `force_redraw=1` means the root event handler
detected a global condition that invalidates pixels under this view —
typically the yui scene-canvas (tabbar / chrome / dialogs) is dirty and about
to repaint, which would leave stale pixels in regions it vacates. The view
must repaint its full contents regardless of internal dirty bits; the
terminal forwards this into `terminal_render_frame`'s two-pass force gate.

`struct yetty_yui_rect` lives here (rather than in yui) so `set_bounds` can
reference it without pulling any yui-side headers into view implementors.

## Public API

```c
#include <yetty/yui-core/view.h>

struct yetty_ycore_void_result yetty_yui_view_destroy(struct yetty_yui_view *view);
struct yetty_ycore_void_result yetty_yui_view_render(struct yetty_yui_view *view,
                                                     struct yetty_ydraw_target *render_target,
                                                     int force_redraw);
struct yetty_ycore_void_result yetty_yui_view_set_bounds(struct yetty_yui_view *view,
                                                         struct yetty_yui_rect bounds);
struct yetty_ycore_int_result  yetty_yui_view_on_event(struct yetty_yui_view *view,
                                                       const struct yetty_yui_event *event);

yetty_ycore_object_id yetty_yui_view_next_id(void);   /* id mint for subclasses */
yetty_ycore_object_id yetty_yui_view_id(const struct yetty_yui_view *view);
struct yetty_yui_rect yetty_yui_view_bounds(const struct yetty_yui_view *view);
```

The wrappers NULL-check the view and the vtable slot, returning an error
Result for missing ops (`set_bounds` alone treats a missing op as success
after stashing the rect on the base). `on_event` returns 1 = consumed,
0 = fall through.

## Files

| file | role |
|------|------|
| `view.c` | vtable-dispatch wrappers, monotonic view-id generation |
| `CMakeLists.txt` | STATIC lib `yetty_yui_core`, no link deps |

Public header: `include/yetty/yui-core/view.h`.

## Implementors and consumers

- Implementors: `../yterminal/terminal.c` (the terminal view),
  `../ydvnc/ydvnc-viewer.c` (the ydvnc viewer view).
- Consumers: `../yui/tile.c` / `workspace.c` (panes hold a stack of views
  and dispatch render/bounds/events through this interface),
  `../yetty/yetty.c` (view-id based routing for CLOSE / screenshot paths).
