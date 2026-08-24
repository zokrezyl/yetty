# ydrawlist2 — yclass client interface over the ydraw drawable list (v2)

The object spelling of the [ydraw-list](../ydraw-list/README.md) producer
model, built so the FFI bindings (and, after migration, the C tools) can
create ydraw content through generated, typed surfaces in every language.
Semantics are exactly the drawable list's: one list, immediate appends in
call order, no retained scene, no hidden state.

## Classes

| class | role |
|---|---|
| `drawable` | abstract base; virtual `pack(list)` appends the record |
| `shape` | mid-base with the SDF paint prefix (`id`, `z`, `fill`, `stroke`, `stroke_width`); the 28 geometry classes derive it in the generated [ysdf2](../ysdf2/README.md) module |
| `font` | FONT resource record; `font_id` is a USER-chosen record field (no auto-numbering, no pairing logic anywhere) |
| `text` | TEXT run record; UTF-8 body, references a font by id (-1 = default face) |
| `drawable_list` | wraps `yetty_ydraw_drawable_list`; `add()` = virtual-pack dispatch, `dcs_emit()` = the standard `YETTY_DCS_YDRAW_BIN` envelope on stdout |

Every slot is `local@` — in-process emitters, never RPC-proxied; the model
still records the methods so the binding generators emit them.

## The `2` suffix

Transitional. These modules run alongside the plain-C producer surface
(`add_cmd_add_*` builders, per-tool DCS emitters) until the tools migrate
to the class interface; then the old surface retires and the suffix drops
(epic #712). The byte-identity unit test (`test/ut/ydrawlist2/`) pins that
both surfaces produce identical record bytes throughout the migration.

## v2 limits (deliberate)

- `font`: installed-face references only (no embedded TTF bytes yet).
- `text`: one record's body is capped inline (2048 bytes) — split long
  content into several runs; lifts when byte-payload marshalling lands.
- `to_bytes()` (the yscene `node_set_content` route) is planned, not yet
  exposed.

## Demos / target surface

`demo/ffi/ydraw/` — per-language sketches (python is the source of
truth) that this module plus `ysdf2` make runnable.
