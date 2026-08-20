# ydraw-yaml — YAML → ydraw drawable-list parser

Parses a YAML scene description into a
`struct yetty_ydraw_drawable_list` ([ydraw-list](../ydraw-list/README.md))
using libyaml. It is a small dispatch shell: a registry maps primitive type
names to factory callbacks (`yetty_ydraw_yaml_factory_fn`, declared in
`../ydraw-list/yaml-factory.h`), and each factory consumes its own mapping
events and appends records to the buffer. Links `yaml`, `yetty_ydraw_list`,
`yetty_ysdf`, `yetty_yplot_core` and `yetty_yfsvm_core`; fontconfig is an
optional desktop-only extra.

## Document shape

The parser walks the top-level mapping for a `body:` sequence; each item is a
mapping whose first key is the primitive type name, dispatched to the
registered factory:

```yaml
body:
  - circle: { ... }          # any ysdf shape name (generated factories)
  - text:
      content: "hello"
      position: [40, 60]
      font-size: 24
      color: "#6BA892"
      font: "DejaVu Serif"   # optional, resolved via fontconfig
  - yplot: { ... }           # yplot's hand-written factory (yplot-yaml.c)
```

## API

```c
/* One-shot: registers all built-in factories, parses, returns the buffer. */
struct yetty_ydraw_drawable_list_result res = yetty_ydraw_yaml_parse(yaml, len);

/* Or compose: create a parser, register factories, parse into your buffer. */
struct yetty_ydraw_yaml_parser_ptr_result p = yetty_ydraw_yaml_parser_create();
yetty_ydraw_yaml_parser_register(p.value, "circle", factory_fn);
yetty_ydraw_yaml_parser_parse(p.value, buffer, yaml, len);
yetty_ydraw_yaml_parser_destroy(p.value);
```

`yetty_ydraw_yaml_parse()` registers, in order: the generated ysdf shape
factories (`yetty_ysdf_register_yaml_factories`, emitted by
`../ysdf/gen-sdf-code.py`), the built-in `text` factory, and yplot's factory
(`yetty_yplot_register_yaml_factory`). An unknown type name is a parse error,
not a skip.

## The built-in `text` factory

Reads `content`, `position` (2-element sequence), `font-size`/`font_size`,
`color` (`#rgb`/`#rrggbb`/`#rrggbbaa`) and an optional `font` name. A named
font is resolved to a TTF path via fontconfig (`fc-match` semantics), the
file's bytes are packed as a FONT record and the text span references it by
`font_id`; resolution failure is a surfaced error rather than a silent
fallback to the canvas default. Builds without fontconfig
(webasm/android/ios/windows) compile the lookup out — `font:` then always
errors.

## Files

| file | role |
|------|------|
| `ydraw-yaml.c` | factory registry, libyaml event walk, built-in `text` factory, one-shot entry |

Public header: `include/yetty/ydraw-yaml/ydraw-yaml.h`. The CMakeLists carries
the fontconfig static-link dance (link groups so `libfontconfig.a` resolves
its freetype references) — see the comments there before touching it.

## Status

The registration/parse plumbing is exercised by the generated ysdf factories
and yplot's hand-written `yplot-yaml.c`. The high-level
`yetty_ydraw_yaml_parse()` entry point currently has **no in-tree caller**
(the header's "ydraw-layer uses this" note is historical); the library builds
as part of the YDRAW feature set and remains the intended path for
YAML-authored scenes.

## See also

- [ydraw-list](../ydraw-list/README.md) — the buffer the factories fill.
- [ysdf](../ysdf/README.md) — generated per-shape YAML factories.
- [ydraw-gen](../ydraw-gen/README.md) — can emit a complex's YAML factory
  from its schema (`yaml_factory:` modes).
