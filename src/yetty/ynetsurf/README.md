# ynetsurf — NetSurf 3.11 frontend that emits ydraw primitives

`ynetsurf` plugs a full NetSurf core build (libnetsurf + libcss + libdom +
libhubbub + libsvgtiny + the rest of the NetSurf helper libraries) under a
yetty-style frontend: it registers the mandatory NetSurf `gui_*` tables
(misc, window, fetch, bitmap, layout) plus a `plotter_table` whose
callbacks drain the page's plot operations into a
`yetty_ydraw_drawable_list`. It is the third web engine next to the
in-house pair — a complete, mature layout engine at the cost of an
external, GPL-licensed dependency, which is why it lives in its own
optional library.

**Status:** functional MVP. Text, rectangles, discs, lines, arcs,
polygons, and flattened paths render; the bitmap plotter is a placeholder
filled box (a real implementation needs a yimage complex upload). One
instance per process: NetSurf's table registration is process-wide, so
the module keeps a singleton (`yetty_ynetsurf_singleton`) — a documented
exception forced by the NetSurf API.

## Plotter mapping

| NetSurf plot op | ydraw output |
|------|------|
| `clip` | CPU-tracked rect; AABB-culls every later primitive |
| `rectangle` | ysdf box (rounded when the style asks) |
| `disc` | ysdf circle |
| `line` | ysdf segment (stroke width carried) |
| `arc` | ysdf arc |
| `polygon` | fan-triangulated ysdf triangles (convex fills) |
| `path` | cubic beziers flattened to segments; closed runs fan-triangulated |
| `bitmap` | placeholder filled box (MVP) |
| `text` | `TEXT_DRAWABLE_LIST` entry (`font_id = -1` — canvas default font) |

Plotter callbacks have NetSurf-dictated signatures, so drawable-list
append failures are translated to `NSERROR_NOMEM` at that boundary.

## Public API and host loop (`include/yetty/ynetsurf/ynetsurf.h`)

```c
struct yetty_ynetsurf_config cfg = { .width = 1024, .height = 768 };
struct yetty_ynetsurf_ptr_result nr = yetty_ynetsurf_create(&cfg);
yetty_ynetsurf_navigate(nr.value, "https://example.org/");
for (;;) {
    int next_ms = yetty_ynetsurf_pump(nr.value); /* run due scheduled callbacks */
    /* select() over libcurl's fdset + stdin; feed input events ... */
    yetty_ynetsurf_redraw(nr.value, buf);        /* drain page → primitives */
}
yetty_ynetsurf_destroy(nr.value);
```

Input plumbing: `yetty_ynetsurf_mouse_move/click/release`, `key_press`,
`scroll`, `set_scroll`, `set_size`; `get_extents` / `get_title` feed the
host's own chrome. The host owns the lifetime, viewport, and event-loop
pump.

## Build gating

The library depends on the `netsurf_core` IMPORTED target minted by
`build-tools/yetty/libs/netsurf.cmake` from a prebuilt 3rdparty tarball.
Where the tarball is a placeholder (windows-MSVC, webasm, iOS, tvOS) the
target does not exist and this module skips itself — the rest of yetty
configures unaffected. NetSurf's built-in `resource:` stylesheets and its
per-language Messages file are baked in via
`YETTY_NETSURF_RESOURCES_DIR` / `YETTY_NETSURF_MESSAGES_FILE`.

## Consumers

- **`tools/ynetsurf`** — the CLI browser. One-shot mode (`--once`, or
  non-TTY stdio) navigates, redraws, and emits a single `YDRAW_BIN` DCS
  envelope, like the other emitter tools (ycat, yplot). Interactive mode
  sits in a `select()` loop over libcurl's fdset, the NetSurf scheduler
  deadline, and stdin (yface splits inbound OSC envelopes from raw
  keystrokes), re-emitting a ydraw clear + bin pair after input so the
  receiving layer replaces rather than accumulates.

## Files

| file | role |
|------|------|
| `ynetsurf.c` | gui tables, schedule queue, lifecycle, navigation, input entry points |
| `ynetsurf-plotters.c` | `plotter_table` → ydraw primitive translation |
| `ynetsurf-internal.h` | shared state (`struct yetty_ynetsurf`, `gui_window`, schedule entries) |

## See also

- `../ylexbor/README.md`, `../ybrowser/README.md` — the in-house web engines and their testing stack
- `../ydraw/README.md` — the drawable lists the plotters fill
- `../yface/README.md` — stdin OSC/raw splitting used by the interactive tool
