# yfigure — figure base class, container, kind registry, producer session

The figure/container model of the compositor: **figure** is the yclass base
class of every rich-content unit, **container** is the complex figure that
hosts children keyed by id, **registry** mints figures from kind names, and
**producer** is the client-side helper that drives a remote container over
yclass RPC. Every pane's root container renders after the text/ydraw layers
(see [Layered rendering](../../../docs/layered-rendering.md) and
[yterminal](../yterminal/README.md)). This is a yclass-based module: the only
hand-written yclass sources are the annotated `figure.c` and `container.c`;
`include/yetty/yfigure/figure.h`, `container.h`, the `*.gen.c` files and
`model.yaml` are **generated** by `make codegen` and must never be hand-edited
— see [yclass](../yclass/README.md).

## figure — the base class (`class@yfigure:figure`)

Base data slice (private layout; reached via generated property accessors):
`rect` (AABB in target pixel space), `z` (stacking within the parent), 
`hidden`, `dirty`, `absolute_coords` (content laid out in pane-root space,
scissor-clipped only — ygui chrome; default 0 = rect-local content).

Polymorphic slots, all `local@` (in-process dispatch only), with base defaults
that concrete kinds override:

```c
yetty_yfigure_render(obj, target);                    /* no-op default      */
yetty_yfigure_destroy(obj);
yetty_yfigure_process_input(obj, statemachine);       /* rejects by default */
yetty_yfigure_process_bytes(obj, bytes, len);
yetty_yfigure_reset_content(obj);
yetty_yfigure_set_scroll(obj, x, y);                  /* scrollable kinds   */
yetty_yfigure_set_content_size(obj, w, h);
yetty_yfigure_apply_scroll_anchor(obj, rolling_row_offset, cell_height);
yetty_yfigure_dump_state(obj, indent);                /* test snapshots     */
```

Concrete kinds inherit via `parent@yfigure:figure`: `yfigure:container`
itself, [ygrid](../ygrid/README.md)'s grid, [ymgui](../ymgui/README.md),
[yrdawn](../yrdawn/README.md), [yshadertoy](../yshadertoy/README.md) and
[yvterm](../yvterm/README.md).

## container — id-keyed complex (`class@yfigure:container`)

Children are `id → entry` in a uthash that doubles as an insertion-ordered
list: render walks back-to-front (z-order; `raise` = move-to-end), hit-testing
returns the back-most child containing the cursor plus child-local
coordinates (`yetty_yfigure_container_hit_test`). ids are parent-scoped;
`id == 0` is reserved. Producers drive the tree through typed `oneway@`
mutation slots — `yetty_yfigure_create_child` (kind token + rect + init
body), `delete_child`, `set_child_rect` / `_z` / `_hidden` / `_scroll` /
`_content_size`, `apply_child_body`, `clear_all` — dispatched locally or
marshalled over RPC from the same call sites. Host-side helpers set the
borrowed `registry`/`context`, the viewport offset and scroll context, and
`protect_child` shields structural children (the terminal content grid) from
a client's `clear_all`.

## registry — kind name → factory, no central enum

A kind is identified by `yetty_yfigure_kind_token("<name>")` — a header-inline
FNV-1a hash, so producer and host agree purely on the string and adding a
kind touches no shared header. Figure-kind modules register a factory (plus a
per-kind user pointer) at terminal create time; the container mints children
through `yetty_yfigure_registry_mint()` when a CREATE_CHILD arrives.

## Out-of-process attach — the connect runtime

A wire client connects with the yclass runtime, not a yfigure helper:
`yetty_yclass_rpc_connect_fds(read_fd, write_fd)` (or
`yetty_yclass_rpc_connect_channel(connection)` on a multiplexed
`yetty_ywire_connection`) owns the transport/connection/channel stack and
returns the session **root** (the host terminal). The client then navigates to
the root-container **proxy** with `yetty_yterminal_figure_root_container(root)`
and calls the same generated typed stubs it would call in-process; teardown is
one `yetty_yclass_rpc_disconnect(root)`. Users: ygui's framework, ymgui's imgui
backend, yrdawn's client, yview. (The old `yetty_yfigure_producer_*` helper and
its library are gone — the runtime absorbed them.)

## Files

| file | role |
|------|------|
| `figure.c` | annotated base class: properties, slot defaults (hand-written) |
| `container.c` | annotated container class: child storage, mutation slots, hit-test (hand-written) |
| `producer.c` | RPC attach/detach session wrapper (hand-written, plain C) |
| `registry.c` | kind-token → factory map (hand-written, plain C) |
| `figure.gen.c` / `container.gen.c` / `rpc.gen.c` / `model.yaml` | GENERATED — codegen output, do not edit |

Hand-written public headers: `producer.h`, `registry.h`, `wire.h` (the
length-first `{length, id, payload}` record header for child-addressed
bodies). Note on terminology: the unit is a **figure**; "card" survives only
as the OSC wire keyword and in some input-event type names.

## Consumers

Root containers are created by `yterminal/terminal.c`, `yui/yui.c`,
`yrich/app.c`, `yguiapp/app.c` and the compositor-style tools
(`tools/ycompositor*`, `yhello`, `ygreeter`, `ybrowser`, `yzoo`, `ymaze`,
`yjungle`). Figure kinds register from ygrid, ymgui, yrdawn, yshadertoy.
