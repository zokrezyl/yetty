# ydraw-gen — schema-driven code generator for complex figures

`generate.py` reads a complex figure's YAML schema
(`src/yetty/<module>/<module>.yaml`) and emits all the boilerplate a
complex needs: the public C header, the concrete-factory + instance
implementation, the pure-CPU wire serializer, and the WGSL accessor
functions. The figure author writes only the schema and the main `.wgsl`
shader. Distinct from the yclass codegen
([yclass](../yclass/README.md)) — this generator covers only the
ydraw complex tier.

## Invocation and outputs

```sh
python3 src/yetty/ydraw-gen/generate.py src/yetty/yplot/yplot.yaml
```

Run manually per schema (no Makefile/CMake wiring). For `name: <name>` it
writes:

| output | contents |
|--------|----------|
| `include/yetty/<name>/<name>-gen.h` | type id, uniforms struct, wire-size/serialize API, factory decl — GPU-less, safe for client emitters |
| `src/yetty/<name>/<name>-gen.c` | concrete factory + instance impl on the [ydraw-factory](../ydraw-factory/README.md) / [yrender](../yrender/README.md) binder framework |
| `src/yetty/<name>/<name>-gen-wire.c` | wire packing only (no Dawn; lives in the module's `_core` lib for riscv64/wasm) |
| `src/yetty/<name>/<name>-gen.wgsl` | uniform/buffer accessor functions with computed offsets |
| `src/yetty/<name>/<name>-gen-yaml.c` | optional YAML-driven constructor for [ydraw-yaml](../ydraw-yaml/README.md) (per `yaml_factory:` mode) |

All outputs carry an `Auto-generated from <name>.yaml - DO NOT EDIT` banner —
regenerate, never hand-edit. Modules generated today: **yplot, yimage,
yvideo**. (ymesh and yshadertoy implement the same concrete-factory interface
by hand; their `-gen.c` names are historical.)

## Schema surface

Top-level keys consumed by the generator:

- `name`, `type_id` (complex tier, `0x80000000+`), `description`.
- `uniforms:` — fixed-size scalars (`f32`/`u32`/`i32`, optional `count`,
  `default`); serialized first on the wire, uploaded as the GPU uniform block.
- `buffers:` — variable-size payload regions. `array: true` marks a
  count-prefixed list of element buffers and forces the merged storage layout
  (one RS buffer spanning the whole post-uniform payload). `rs_name` renames
  the binding (the first buffer keeps the literal `buffer`).
- `textures:` — declared with `format:` (`r8`/`rgba8`), backed by a
  `pixels_buffer` + `width_uniform`/`height_uniform` reference; the pixel
  buffer is diverted out of the storage binding into an atlas texture. Wire
  dimensions are bounds-checked (`TEXTURE_MAX_DIM`, bytes-per-pixel) so a
  hostile record cannot drive an out-of-bounds GPU upload.
- `libraries:` — WGSL libraries (e.g. `yfsvm`) concatenated before the main
  shader.
- Escape hatches so stateful figures need no fork of the generated code:
  `update: extern` (ops->update points at a hand-written
  `yetty_<name>_instance_update`), `lifecycle_extern` (create/destroy
  callouts, e.g. yplot's animation timer), `external_uniforms` (server-side
  slots not on the wire, e.g. `time`), `hooks`, and
  `yaml_factory: generated | manual | none` with `yaml_mapping` /
  `yaml_flags` key translation.

Wire layout: `[type][payload_size][uniform words][buffer data …]` — the
receiver binds bytes to the pre-compiled pipeline without interpreting them;
only the serializer and the shader know the semantics.

## Status

- Working and current for yplot / yimage / yvideo; invoked manually when a
  schema changes, outputs are committed.
- The `include/yetty/{yimage,yplot,yvideo}/` tree **inside this directory**
  contains stale header snapshots from earlier generator runs — the live
  headers are the ones under the repo-root `include/yetty/<module>/`.
- This module is not listed in the architecture module map.

## See also

- [ydraw](../ydraw/README.md) — the complex/code-generation design the
  generator implements.
- [ydraw-factory](../ydraw-factory/README.md) — the runtime the generated
  factories plug into.
- [ysdf](../ysdf/README.md) — the sibling generator for the fixed-size SDF
  primitive tier (`gen-sdf-code.py`).
