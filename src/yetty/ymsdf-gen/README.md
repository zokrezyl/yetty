# ymsdf-gen — CPU MSDF glyph-CDB generator (msdfgen)

`ymsdf-gen` renders every requested glyph of a TTF font to a multi-channel
signed-distance-field bitmap (via the vendored msdfgen library + FreeType)
and writes the results into a single `.cdb` file through the
[`ycdb`](../ycdb/README.md) writer. It is the CPU backend behind the
polymorphic [`ymsdf`](../ymsdf/README.md) generator; the GPU counterpart is
[`ymsdf-wgsl`](../ymsdf-wgsl/README.md). This is one of the few C++
translation units in the tree (msdfgen is a C++ library); the public API is
`extern "C"`.

## Output format

Each CDB entry is keyed by the 4-byte codepoint and holds a 28-byte header
followed by RGBA8 pixels (MSDF in RGB, alpha = 255, Y-flipped):

```c
struct yetty_ymsdf_gen_glyph_header {
    uint32_t codepoint;
    uint16_t width, height;      /* bitmap size incl. pixel_range padding */
    float bearing_x, bearing_y;  /* placement relative to the baseline */
    float size_x, size_y;
    float advance;               /* scaled to font_size */
};  /* followed by width * height * 4 bytes RGBA8 */
```

The consumer side of this format is the MSDF font atlas loader (see
[`yfont`](../yfont/README.md)).

## Public API

One function; everything is driven by the config struct:

```c
struct yetty_ymsdf_gen_config config = {
    .ttf_path = "font.ttf",
    .output_dir = "out",       /* writes out/<ttf_stem>.cdb */
    .font_size = 32.0f,        /* px, 0 → 32 */
    .pixel_range = 4.0f,       /* 0 → 4 */
    .thread_count = 0,         /* 0 → hardware_concurrency */
    .all_glyphs = 1,           /* or pick the built-in charsets: */
    .include_nerd_fonts = 0,   /* Powerline / Devicons / Codicons / … */
    .include_cjk = 0,
};
struct yetty_ycore_void_result r = yetty_ymsdf_gen_config_cpu_generate(&config);
```

Generation is multi-threaded: a work queue of codepoints, one
FreeType+msdfgen context per worker. Without `all_glyphs` the default
charset covers Latin (+ extended), Greek, Cyrillic, punctuation, currency,
arrows, math operators, and box-drawing/block/geometric ranges; the two
opt-in flags add Nerd Font symbol ranges and CJK.

## CLI tool

`CMakeLists.txt` also builds **`yetty-ymsdf-gen`**
(`tools/msdf/gen-msdf/main.c`):

```
yetty-ymsdf-gen [--all] [--nerd-fonts] [--cjk] [--size N] [--range N] [-j N] <font.ttf> <output-dir>
```

This is how the pre-generated atlases shipped with the install (e.g.
`msdf-fonts/Emmentaler.cdb`, referenced by name from
[`ymusic`](../ymusic/README.md)) are produced.

## Consumers

- `../ymsdf/cpu-generator.c` — the ops-table wrapper selected by
  `msdf/generator=cpu`.
- the `yetty-ymsdf-gen` CLI above.

Gated by `YETTY_ENABLE_FEATURE_YMSDF_GEN` (off on Windows: msdfgen ships
`/MT` while the rest of the third-party stack is `/MD`).

## Layout of the module

| file | role |
|------|------|
| `ymsdf-gen.cpp` | charset build, threaded msdfgen workers, CDB writer |
| `msdf-gen/CMakeLists.txt` | legacy, unreferenced — points at `generator.cpp`/`main.cpp` that no longer exist; nothing `add_subdirectory()`s it |
| `../../../include/yetty/ymsdf-gen/ymsdf-gen.h` | glyph header struct, config, the one entry point |

Status: the library itself is complete and in production use; the stray
`msdf-gen/` subdirectory is a leftover from the pre-rewrite generator and is
dead weight.
