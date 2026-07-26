# yview — client emitter for a server-side scrollable figure

`yview` is the yclass class `yview:view`: a client-side handle for **one
positioned figure** under a running yetty's root figure container, driven
over yclass RPC (DCS `YETTY_DCS_YCLASS_RPC`). A tool running inside a pane
creates a view, ships its content once, and thereafter sends only small
mutations (scroll, rect, content size) — scrolling and clipping are
server-side state. It is the standard way for a CLI tool to own a
scrollable surface instead of writing into the scrollback (contrast the
one-shot `YDRAW_BIN` path used by [`ycat`](../ycat/README.md)).

Depends on [`yfigure`](../yfigure/README.md) (producer session + typed
container stubs), [`ydraw-core`](../ydraw-core/README.md) (drawable lists),
and `yplot_core` for `set_plot`. Built when both `YDRAW` and `YFACE`
features are on.

## How it works

`configure()` connects to the host over `yetty_yclass_rpc_connect_fds`
(stdin carries terminal→tool RPC responses; the passed `fd` carries
tool→terminal requests), navigates to the root container with
`yetty_yterminal_figure_root_container`, and remembers the child id, figure kind, background
color, and rect. The content setters build the child's init byte stream
(optionally an opaque background box under the content, sized to the full
content extent) and ship it as the `CREATE_CHILD` init payload via the typed
`yetty_yfigure_*` stubs; later mutations are one-way fire-and-forget calls
(`set_child_scroll`, `set_child_rect`, `set_child_content_size`,
`delete_child`). Scroll offsets are clamped client-side against the known
content extent. The default figure kind (when `configure` gets 0) is the
`"ygrid"` registry token — the only scrollable kind.

Every method slot is `local@`: a view is an in-process emitter, never
proxied over RPC — but the model still records the methods so the FFI /
host-language binding generators emit them.

## Public API (generated `include/yetty/yview/view.h`)

```c
struct yetty_yclass_object_ptr_result vr = yetty_yview_view_create(NULL);
struct yetty_yclass_object *view = vr.value;

yetty_yview_configure(view, STDOUT_FILENO, child_id, /*kind=*/0u,
                      /*bg=*/0xFF0B1014u, min_x, min_y, max_x, max_y);

/* Content — pick one: */
yetty_yview_set_content(view, drawable_list);        /* prebuilt ydraw list */
yetty_yview_set_text(view, utf8_text, font_size);    /* newline-split lines */
yetty_yview_set_plot(view, "sin(x)", x_min, x_max, y_min, y_max);

/* Thereafter: */
yetty_yview_scroll_to(view, x, y);
yetty_yview_scroll_by(view, dx, dy);
yetty_yview_set_rect(view, min_x, min_y, max_x, max_y);
yetty_yview_set_content_size(view, w, h);

yetty_yview_destroy(view);   /* DELETE_CHILD + detach + free */
```

`set_content` derives the content extent from the list's scene bounds;
`set_text` lays lines out itself (server-side default font,
`TEXT_DRAWABLE_LIST` records); `set_plot` renders a yexpr-plot expression
via [`yplot`](../yplot/README.md) into a rect-filling, non-scrolling plot.

## Consumers

- `tools/yless` — the pager: file → drawable list (text, or a score via
  [`ymusic`](../ymusic/README.md)) → view, then key-driven `scroll_by`.
- `tools/yflame/interactive.c` and `tools/ymap/interactive.c` — interactive
  figure frontends.
- Language bindings — `bindings/python/yetty/generated/yview.py`, exported
  from `libyetty_ffi.so` ([`yffi`](../yffi/README.md)).

## Layout of the module

| file | role |
|------|------|
| `view.c` | the only hand-written file: annotated class + method slots (`#include`s `view.gen.c` at the foot) |
| `view.gen.c`, `rpc.gen.c`, `model.yaml` | codegen output — never hand-edited |
| `../../../include/yetty/yview/view.h` | generated public header |

See [`yclass`](../yclass/README.md) for the annotation/codegen model and
[`yterminal`](../yterminal/README.md) for the DCS code registry.
