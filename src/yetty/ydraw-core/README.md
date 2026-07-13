# ydraw-core — serialized primitive buffer, wire commands, drawable-list registry

The GPU-less core of the ydraw wire format: the drawable-list buffer that
producers append primitives into, the type registry that maps wire type ids to
size/aabb handlers, the streaming iterator that receiving canvases decode
envelopes with, and the control-command / font / text-run record definitions.
Every ydraw producer (ychart, ydiagram, ysvg, ymarkdown, ymusic, yrich/ypdf,
ygui, the `tools/*` CLIs, …) and every receiver (the scrolling canvas in
[ydraw](../ydraw/README.md), ygrid) builds on this module. It links only
`yetty_ycore` (plus the [ywire](../ywire/README.md) statemachine *header* for
the iterator), so client tools and no-GPU builds use it freely.

## Wire type-id tiers

Every record starts with a `u32` type word. Canonical layout (`cmds.h`):

| range | tier | handler |
|-------|------|---------|
| `[0x00000000, 0x0000FFFF]` | control cmds (ZERO, DELETE, UPDATE, GROUP, GROUP_REF) | `cmds.c` |
| `[0x10000000, 0x1FFFFFFF]` | SDF paint primitives (generated, see [ysdf](../ysdf/README.md)) | registry default |
| `[0x40000000, 0x7FFFFFFF]` | drawable-list entries: FONT `0x40000001`, TEXT_DRAWABLE_LIST `0x40000002` | `font-resource.c`, `text-drawable-list.c` |
| `[0x80000000, 0xFFFFFFFF]` | composites (yplot, yimage, yvideo, ymesh, …) | `composite.c` |

Variable-size records share one FAM layout — `u32 type`, `u32 payload_size`,
`u8 payload[]` — so the iterator strides them uniformly. Bits 31:30 of the
type word additionally classify record kind (anonymous content / id+content
declaration / id-only reference); see the comment block in `cmds.h`, including
the collision caveat between `HAS_ID` cmd values and composite type ids.

## Drawable list (`drawable-list.h`)

`struct yetty_ydraw_drawable_list` is a growable byte stream plus scene
bounds. Producers append with:

```c
struct yetty_ydraw_drawable_list_result buf_res =
    yetty_ydraw_drawable_list_config_buffer_create(NULL);
yetty_ydraw_drawable_list_add_prim(buf, bytes, size);      /* raw record   */
yetty_ydraw_drawable_list_add_font(buf, &ttf, "name");     /* FONT (bytes) */
yetty_ydraw_drawable_list_add_font_ref(buf, hex16);        /* FONT by hash */
yetty_ydraw_drawable_list_add_font_named(buf, "Emmentaler");/* installed    */
yetty_ydraw_drawable_list_add_text(buf, x, y, &text, size, color,
                                   layer, font_id, rotation);
size_t n = yetty_ydraw_drawable_list_serialize(buf, &out_bytes);
```

`serialize()` frames the stream with a magic header (`'YPB1'` + scene bounds
+ byte count); `create_from_bytes()` accepts either that framed form or a bare
primitive stream. Entity-scoped editing uses `begin_group[_with_rect]` /
`end_group` (payload size back-patched via a marker), `add_cmd_delete`,
`add_cmd_update`, `add_cmd_group_ref`, plus length-first routed records
(`add_record` / `begin_record` / `end_record`) for figure-tree bodies.
`truncate()` discards a partially written record on failure.

## Registry and iteration

`drawable-list-registry.h` maps type ranges to base-ops handlers
(`size`, `aabb` — the minimum needed to stride a buffer and place a record in
a spatial grid). The fully wired registry for all tiers is built by
`yetty_ydraw_drawable_list_registry_create_default()` in
`../ydraw/drawable-list-registry.c`.

Two decode paths:

- `drawable-iterator.h` — streaming: pulls decoded bytes from a
  `yetty_ywire_wire_statemachine` inside the layer's input coroutine
  (yielding when bytes run dry) and returns one command per `_next()` call:
  `ADD` (a drawable entry), `DELETE` (target id) or `UPDATE` (id + opaque
  payload).
- `yetty_ydraw_drawable_command_parse()` — same record grammar over an
  in-memory buffer, used for GROUP-body inner loops.

## Files

| file | role |
|------|------|
| `drawable-list.c` | buffer create/append/serialize, groups/records, FONT/TEXT packing, YPB1 framing |
| `drawable-list-registry.c` | type-range → handler-ops registry (default + up to 8 ranges) |
| `drawable-iterator.c` | streaming command iterator + in-memory command parse |
| `cmds.c` | CMD_ZERO producer and the cmd-tier stride handler |
| `composite.c` | composite wire helpers: `is_composite`, record aabb/size handler |
| `font-resource.c` | FONT record parse/handler (TTF bytes, content-hash ref, or named ref) |
| `text-drawable-list.c` | TEXT_DRAWABLE_LIST parse/handler (UTF-8 run + PDF Tc/Tw spacing) |

Public headers live in `include/yetty/ydraw-core/`. Two of them are special:
`yaml-factory.h` only declares the factory-callback type used by
[ydraw-yaml](../ydraw-yaml/README.md), and `figure.h` is a legacy composite
base interface kept for [yvterm](../yvterm/README.md) — new composite code
uses `../ydraw-factory/composite-factory.h` instead.

## See also

- [ydraw](../ydraw/README.md) — buffer layout, rolling-row scrolling, canvas.
- [ydraw-factory](../ydraw-factory/README.md) — the GPU-side composite runtime.
- [ydraw-gen](../ydraw-gen/README.md) — generates composite serializers.
- [ywire](../ywire/README.md) — OSC envelopes and the wire statemachine.
- [GPU resource binding](../../../docs/gpu-resource-binding.md).
