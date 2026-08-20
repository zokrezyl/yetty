# yjungle — incremental GROUP/DELETE wire-command test scene

`yjungle` maintains a connected chain of "segments" — each either a single
SDF primitive or a nested group of 2..N sub-segments — and emits scene
mutations as incremental ydraw wire commands (`CMD_GROUP` / `CMD_DELETE`,
see `ydraw-list/cmds.h`). It exists to stress the receiving side's
recursive group parser and strict-id handling (`process_group_body` and
the entity tree in `../ygrid/README.md`, ported from the scene-canvas).
Pure producer; depends only on `ydraw-list` and `ysdf`.

## Wire model

- **First tick**: `CMD_ZERO` + one `GROUP(id)` per initial chain segment —
  after this the receiver holds the full tree.
- **Later ticks**, when an event fires (random cadence within
  `event_interval_ms_min/max`):
  - *extend* — append `GROUP(new_id)` for a new tail segment;
  - *replace* — `DELETE(old_id)` + `GROUP(new_id)` keeping the same
    (start, end) so neighbours stay connected.
- Otherwise the tick leaves the buffer empty and the frontend skips
  shipping an envelope.

Group ids are monotonic and never reused, so the receiver's strict
"GROUP id already exists" rejection never fires for re-emits. A non-leaf
segment opens a `CMD_GROUP`, recurses into its children (which may open
nested groups), then `end_group` patches the parent's payload size —
exactly the shape that exercises the receiver's recursion.

Config knobs (`yjungle.h`) cover the random-walk step length, an
off-canvas margin (so some segments land fully outside the scene),
group nesting depth / probability / fan-out, initial and maximum chain
length, and the extend-vs-replace probability. Leaf primitives draw from
the same 22-shape ysdf table yzoo uses, so the two scenes look related.

## Public API (`include/yetty/yjungle/yjungle.h`)

```c
struct yetty_yjungle_config cfg = yetty_yjungle_config_default();
struct yetty_yjungle_ptr_result jr = yetty_yjungle_create(&cfg, /*seed=*/0);

/* incremental path — only this tick's deltas (buf may end empty): */
yetty_yjungle_tick(jr.value, buf, now_ms);

/* full-redraw path — the whole current chain as a flat primitive list: */
yetty_yjungle_render(jr.value, buf, now_ms);

yetty_yjungle_set_scene_size(jr.value, width, height);
yetty_yjungle_destroy(jr.value);
```

`tick` is for receivers that accumulate deltas on a persistent scene
canvas; `render` is for consumers that repaint a full list every frame
(the ygui `ydraw_embed` path).

## Consumers

- **`tools/yjungle`** — standalone GPU app: yclass class `yjungle:app`
  (subclass of `yapp:app`, annotated source `tools/yjungle/app.c`;
  generated header `include/yetty/yjungle/app.h`) renders the chain into
  a full-window ygrid figure via the flat `render` path.
- **ygui `yjungle` widget** (`../ygui/widgets/yjungle.c`) — `ydraw_embed`
  wrapper over `yetty_yjungle_render`, self-dirtying so the chain keeps
  growing; exercised by `demo/ygui/27_yjungle`.

## Files

| file | role |
|------|------|
| `yjungle.c` | segment-tree state, random walk, GROUP/DELETE serialisation, flat render |

## See also

- `../ymaze/README.md`, `../yzoo/README.md` — the sibling animated test scenes
- `../ydraw/README.md` — drawable lists, wire commands, scrolling canvas
- `../ygrid/README.md` — the receiver whose grouping model this scene stresses
- `../ysdf/README.md` — the SDF shape set leaf segments draw from
