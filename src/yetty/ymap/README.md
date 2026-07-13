# ymap — slippy-map renderer: yclass model + tile engine

`ymap` renders OpenStreetMap-style XYZ tile maps into ydraw drawable
lists. One library, two halves: the `ymap:map` yclass class (`map.c`) —
provider registry, view state (center lat/lon, zoom, viewport), pan/zoom
verbs, render dispatch — and the plain-C engine underneath (`engine.c`,
`vector-render.c`, `tile-fetch.c`). GPU-less by design: everything ends
in generic ydraw drawables, so the receiving terminal needs no
ymap-specific code. Gated on libcurl + yimage.

## Two render paths

- **Raster** (`engine.c`) — lat/lon/zoom → Web-Mercator global-pixel
  viewport → covering tile set → per-tile disk-cache hit or libcurl GET →
  stb_image decode → blit into one RGBA8 composite → a drawable list
  holding **one yimage prim**.
- **Vector** (`vector-render.c` + `vector-tile.c`) — shortbread-schema
  Mapbox Vector Tiles decoded by a minimal hand-rolled protobuf-subset
  reader, emitted as native drawables: ear-clipped SDF triangles for
  polygons, per-segment SDF strokes with per-class width/colour for
  streets/rails/waterways, MSDF text runs for labels. Crisp under cell
  zoom and far smaller on the wire. Tile max zoom is 14; the view
  over-zooms (deepest tile scaled up) to 19.

Per-tile failures are best-effort **by design** on both paths: a tile
that fails to download or decode leaves a hole and the render proceeds.
Everything else bails on first error.

Tiles are fetched with an on-disk cache under
`<yetty cache dir>/osm-tiles` (vector: `osm-vector-tiles`); misses
download in parallel via curl-multi, capped at 2 connections per host per
the OSM tile usage policy, with an identifying User-Agent.

## Providers

A built-in registry of keyless, attribution-required servers — `osm`,
`osm-vector`, `opentopomap`, `cyclosm`, `osm-hot`, `osmfr`,
`carto-light` / `carto-dark` / `carto-voyager`, `gibs-bluemarble`,
`s2cloudless` — plus a custom slot for arbitrary printf-style XYZ
templates (`%u` slots in z, x, y order; POSIX positional specifiers
reorder for z/y/x WMTS servers). Every provider carries its required
attribution line; frontends **must** display it
(`yetty_ymap_attribution`).

## Public API (`include/yetty/ymap/map.h`, generated)

```c
yetty_ymap_register();
struct yetty_yclass_object_ptr_result obj = yetty_ymap_map_create(NULL);
yetty_ymap_configure(obj.value, 47.4979, 19.0402, /*zoom=*/13, 640, 400);
yetty_ymap_set_provider(obj.value, "osm-vector");
yetty_ymap_pan_by_pixels(obj.value, dx, dy);
yetty_ymap_zoom_by_at(obj.value, +1, anchor_x, anchor_y); /* wheel-zoom feel */

struct yetty_ydraw_drawable_list_result lr = yetty_ymap_render(obj.value);
yetty_ymap_emit_osc(lr.value, STDOUT_FILENO); /* YDRAW_BIN envelope helper */
```

Slots are `local@`: `render` returns a pointer (in-process only), and the
navigation math is trivial — a remote frontend re-creates the model
locally and ships drawable lists instead of proxying calls. The engine
half (`include/yetty/ymap/engine.h`, hand-written) additionally exposes
`yetty_ymap_render_raster` / `_render_vector`, the forward/inverse slippy
projection helpers, and `yetty_ymap_geolocate_public_ip` (one keyless
ipinfo.io request, city-level at best, best-effort by contract — used
only to pick a default map center).

## Consumers

- **`tools/ymap`** — one-shot mode emits a single `YDRAW_BIN` OSC envelope
  (map lands at the cursor and scrolls like a ycat image) and prints the
  attribution line; interactive mode (`interactive.c`) ships the map as a
  positioned server figure via yview over yclass-RPC and re-renders on
  wheel-zoom / drag-pan, following the `tools/yflame` input model.
- **Bindings** — `model.yaml` drives the generated FFI surface
  (`bindings/python/yetty/generated/ymap.py`), so host languages get the
  same pan/zoom/render verbs.

## Files

| file | role |
|------|------|
| `map.c` | annotated `ymap:map` class: providers, view state, verbs, dispatch |
| `map.gen.c` / `rpc.gen.c` / `model.yaml` | codegen outputs (never hand-edited) |
| `engine.c` | raster path: fetch → RGBA composite → one yimage prim; projection helpers |
| `vector-render.c` | vector path: MVT → clipped, triangulated SDF/MSDF drawables |
| `vector-tile.c` / `vector-tile.h` | bounds-checked minimal MVT (protobuf subset) decoder |
| `tile-fetch.c` / `tile-fetch.h` | disk cache + curl-multi parallel download, plain GET helper |
| `geoip.c` | public-IP geolocation for the default map center |

## See also

- `../yimage/README.md` — the raster composite's target prim
- `../ydraw/README.md` — drawable lists / `YDRAW_BIN` scrolling path
- `../yview/README.md` — the figure transport the interactive client uses
- `../../yclass/README.md` — the annotation/codegen framework behind `map.h`
