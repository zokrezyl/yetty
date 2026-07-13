# ygui — native widget toolkit (yclass-based, producer-side)

ygui is a pure-C widget toolkit built on the [yclass](../yclass/README.md)
object framework. An app builds a tree of widget objects; each
`yetty_ygui_framework_emit` cycle translates that tree into figure-tree
mutations plus one shared [`ygrid`](../ygrid/README.md) drawable list on the
receiving compositor's [`yfigure`](../yfigure/README.md) container. The same
call sites work in-process (standalone window) or over yclass-RPC into a
running yetty pane — the typed yfigure stubs dispatch locally or over the
wire depending on the session.

## Key concepts

- **framework** (`framework.c`) — per-instance app state: widget id
  allocator with free-list, pending-delete queue, the receiver-side ygrid id,
  and the two-pass emit. Pass 1 walks the tree dispatching
  `emit_container` (figure create/rect/z/hidden via
  `yetty_yfigure_create_child`, `set_child_rect`, …); pass 2 dispatches
  `emit_body` (chrome widgets append prims to the shared ygrid drawable
  list; figure widgets upload their body via
  `yetty_yfigure_apply_child_body`). Input arrives back through
  `feed_input` (terminal bytes) and `feed_mouse_button/motion/scroll`.
- **widget** (`widget.c`) — the base class: rect in absolute target pixels
  plus a flex layout struct. All other widgets inherit from it.
- **primitive-widget** (`primitive-widget.c`) — base for *chrome* widgets
  (button, label, panel, …): overrides `emit_body` so subclasses only
  override `paint` and write SDF/glyph records into the shared drawable list.
- **figure widgets** (`widgets/yimage.c`, `widgets/yplot.c`,
  `widgets/yvideo.c`, `widgets/ydiagram.c`, `widgets/ybrowser.c`, …) —
  extend the base widget directly and mint their own compositor figure.
- **mixins** (`mixins/clickable.c`, `mixins/draggable.c`) — press/drag
  behaviour composed into widget classes via yclass mixin dispatch.
- **events** (`event.c`) — synchronous subscribe/emit
  (`yetty_ygui_widget_subscribe`); subscriptions live on the target object.
- **layout** (`layout.c`) — a CSS-flexbox-shaped engine: row/column
  direction, justify, align + per-child align-self, flex-grow/shrink with
  the min/max freeze loop, per-side margins, gap, wrap, absolute
  positioning, and an intrinsic content-measure pass. Out of scope: percent
  sizing, baseline alignment, align-content, RTL.
- **theme** (`theme.c`) — brand-default palette, overridable from `style.*`
  config keys via `yetty_ygui_framework_apply_config_to_theme`.

## Codegen — what is generated

ygui is a yclass module: the *only* hand-written files are the annotated
`.c` sources plus `internal.h`, `framework-defs.h`, `theme.h`, `event.h`,
and `widgets/paint-helpers.h`. Everything else is produced by `make codegen`
and must never be hand-edited:

- every public class header under `include/yetty/ygui/` (`widget.h`,
  `primitive-widget.h`, `framework.h`, all of `widgets/*.h` and
  `mixins/clickable.h`/`draggable.h`),
- the per-class `*.gen.c` files (each annotated `.c` includes its own
  `<stem>.gen.c` at the foot), `rpc.gen.c`, and `model.yaml`.

See [yclass](../yclass/README.md) for the annotation grammar
(`class@ygui:<name>`, `parent@`, `override@`, `mixin@`).

## Using it

```c
/* Inside a yguiapp `build` override — root is the styled body widget. */
struct yetty_yclass_object_ptr_result button_res =
    yetty_ygui_widget_add(root, yetty_ygui_button_class_get().value);
yetty_ygui_button_set_label(button_res.value, "Hello");

struct yetty_ygui_layout layout = *yetty_ygui_widget_layout_get(button_res.value).value;
layout.width = 200;
layout.height = 40;
yetty_ygui_widget_layout_set(button_res.value, &layout);
```

App bring-up (window/GPU or in-terminal attach) lives in
[`yguiapp`](../yguiapp/README.md) — demos only override the `build` slot.
Standalone hosts attach the framework to a container directly; in-terminal
producers attach over an fd (`yetty_ygui_framework_attach`) or a
ywire-channel transport (`yetty_ygui_framework_attach_transport`).

## Widgets (53 classes)

Chrome: breadcrumbs, button, checkbox, chip, choicebox, collapsing_header,
colorpicker, combobox, datepicker, dialog, dropdown, filepicker, hbox,
label, list, menubar, panel, popup_menu, progress, radio, rich, scrollarea,
selectable, separator, slider, spinner, splitter, statusbar, stepper,
tabbar, table, textarea, textinput, toggle, tooltip, tree_node, vbox,
window.

Figure: ybrowser, ydiagram, ydraw_embed, yimage, yjungle, ymarkdown, ymaze,
ynode, ynodes, ypdf, yplot, yrich_view, yshadertoy, yvideo, yzoo.

## File map

| file | role |
|------|------|
| `framework.c` | framework class: ids, deletes, two-pass emission, input routing |
| `widget.c` | base widget class: rect, flex layout struct, tree ops |
| `primitive-widget.c` | chrome-widget base: `emit_body` → `paint` |
| `layout.c` | flex layout pass |
| `event.c` | subscribe / emit |
| `theme.c` | theme defaults + config overlay |
| `dispatch.c` | thin wrappers over yclass slot lookup / super dispatch |
| `internal.h` | private shared types (`YETTY_YGUI_DOMAIN`, object layout) |
| `mixins/`, `widgets/` | one annotated `.c` (+ generated `.gen.c`) per class |

## Consumers

- [`yguiapp`](../yguiapp/README.md) — the generic app host (standalone and
  in-terminal); `demo/ygui/NN_*` — one demo per widget/pattern.
- `yui` (config dialog, debug window), [`yrich`](../yrich/README.md).
- Tools: `tools/ygreeter`, `tools/ytop`, `tools/yaudio`,
  `tools/ycompositor-ygui`, `tools/ybrowser` (browser UI), the `ccc` and
  `yai` HUD tools.
