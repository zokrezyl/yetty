# yui — app UI: tabs, tiled panes, workspaces, chrome

`yui` is the terminal application's own user interface: the browser-style
tab strip that owns N workspaces, the binary tile tree of splits/panes
inside each workspace, and the ygui-driven chrome on top (titlebar, menus,
dialogs, statusbar, per-pane debug windows, toasts). Its only consumer is
[`../yetty`](../yetty/README.md)'s orchestrator; panes host abstract views
defined by [`../yui-core`](../yui-core/README.md) (the terminal view comes
from [`../yterminal`](../yterminal/README.md)).

Headers live beside the sources (there is no `include/yetty/yui/`); they are
included as `<yetty/yui/...>` through the `src/` include root the CMake
target exports. `yui.h` is explicitly internal — only `yetty.c` includes it.

## Structure

```
tabbar_model ── owns N workspaces, one active
  workspace ── owns a tile tree, origin below the tab strip
    tile (SPLIT) ── orientation + ratio + two children
    tile (PANE)  ── stack of yetty_yui_view; one focused pane per tree
yui (singleton) ── ygui engine on its own yfigure container, top-z chrome
```

- **tabbar-model.c** — the strip at the top
  (`YETTY_YUI_TABBAR_HEIGHT_DP` = 36 logical px). Owns workspace lifecycle
  (`add_workspace_from_config`, chrome-driven `attach_empty_workspace`,
  `add_workspace_of_kind` for the v-menu kinds shell/ssh/telnet/yvnc —
  each toggles config keys so the pty-factory picks the right backend),
  keyboard shortcuts (Ctrl+T new, Ctrl+W close, Ctrl+Tab cycle,
  Ctrl+digit jump), window-control wrappers (iconify / toggle-maximize /
  close) and the invisible edge-resize cursor bands. Since the OS window
  decoration is disabled on desktop, the strip is also the window-drag area.
- **workspace.c** — tile-tree root, layout (`resize`, `set_origin` below the
  strip), `set_active` focus cascade down to the leaf view (so
  focus-reporting and cursor blink react to tab switches), split/close
  operations including chrome-driven `_with_ids` variants where the caller
  pre-mints object ids, and `load_layout` from config.
- **tile.c** — the split/pane node: bounds propagation, render recursion,
  event routing, focus and hit-test helpers (`find_pane_at`,
  `find_focused_pane`, `find_pane_with_view`), per-pane view stack
  (`pane_push_view` / `pane_pop_view`) and the per-pane `debug_open` flag.
- **yui.c** — the app-level singleton: a ygui engine whose per-frame
  envelope feeds yui's own yfigure root container in-process (no PTY, no
  OSC framing). It owns the engine-pinned titlebar (hamburger, tabs, +,
  drag spacer, _ □ ✕) reconciled against the tabbar model each render, the
  v-menu and right-click context menu (GPU-info dialog, Split V/H
  submenus), the per-kind connect dialogs, the statusbar (left/right labels
  + a 1 Hz allocator-statistics readout), per-split divider widgets,
  per-pane debug-window reconciliation, and the global
  [`ynotify`](../ynotify/README.md) toast handler.
- **config-dialog.c** — the Settings window: a two-pane view over the live
  yconfig tree (branch tree on the left, read-only `key = value` leaves on
  the right). Editing is not wired yet.
- **debug-window.c** — closable/draggable per-pane overlay fed with
  wire-stats snapshots; opened from the pane context menu.

## API sketch (as used by yetty.c)

```c
#include <yetty/yui/tabbar-model.h>
#include <yetty/yui/yui.h>

struct yetty_yui_tabbar_model_ptr_result yetty_yui_tabbar_model_create(
    const struct yetty_yconfig_config *config);
struct yetty_ycore_void_result yetty_yui_tabbar_model_render(
    struct yetty_yui_tabbar_model *bar, struct yetty_ydraw_target *rt, int force_redraw);
struct yetty_ycore_int_result  yetty_yui_tabbar_model_on_event(
    struct yetty_yui_tabbar_model *bar, const struct yetty_yui_event *event);

struct yetty_yui_ptr_result yetty_yui_create(const struct yetty_context *context,
                                             uint32_t surface_w, uint32_t surface_h,
                                             float cell_w, float cell_h);
struct yetty_ycore_int_result  yetty_yui_is_dirty(const struct yetty_yui *yui);
struct yetty_ycore_int_result  yetty_yui_on_event(struct yetty_yui *yui,
                                                  const struct yetty_yui_event *event);
struct yetty_ycore_void_result yetty_yui_render(struct yetty_yui *yui,
                                                struct yetty_ydraw_target *target);
```

The `force_redraw` flag threading through render calls is the "yui chrome is
dirty this frame" signal: yetty reads `yetty_yui_is_dirty` *before* panes
draw and forces every pane to repaint, so pixels the chrome vacates never
show the previous frame.

## Files

| file | role |
|------|------|
| `yui.c` / `yui.h` | app-level singleton: engine, titlebar, menus, dialogs, statusbar, dividers, debug-window sync, toasts |
| `tabbar-model.c` / `tabbar-model.h` | tab strip owning workspaces; kinds, shortcuts, window controls, edge-resize |
| `workspace.c` / `workspace.h` | tile-tree owner: layout, focus cascade, split/close, config layout loading |
| `tile.c` / `tile.h` | split/pane tree node, view stack, hit-testing |
| `config-dialog.c` / `.h` | Settings window over the live yconfig |
| `debug-window.c` / `.h` | per-pane debug overlay |
| `CMakeLists.txt` | STATIC lib `yetty_yui`; links yfigure, ygrid, yshadertoy, yui-core, yterminal, ygui, ychrome (+ ymgui when enabled) |

## Cross-references

- [`../yui-core/README.md`](../yui-core/README.md) — the view contract panes host
- [`../ygui/README.md`](../ygui/README.md) — the widget engine behind the chrome
- [`../ychrome/README.md`](../ychrome/README.md) — borderless-window move/resize engine
- [`../../../docs/layered-rendering.md`](../../../docs/layered-rendering.md) — where yui composites in the frame
