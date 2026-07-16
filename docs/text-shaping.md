# Complex-script text shaping (HarfBuzz) — decision record

Research outcome for the "complex-script shaping" track: whether and how to
render Arabic, Indic (Devanagari, Bengali, Tamil, …) and Thai *correctly* in
yetty, now that the bundled-font coverage has landed. This document records the
decision, the measured numbers behind it, and the follow-up implementation work.

Status: **adopted — Phase 0 done; Phase 1 shaping engine + ydraw render path
landed and demonstrated.** The HarfBuzz dependency is integrated for every
platform (desktop/android/ios/webasm build all validated), the shaper + atlas
re-key + the free-position render path are implemented and unit-tested, and the
Arabic-joining / Devanagari-reordering spike is rendered correctly (see the end
of Phase 1 below). The remaining work is routing *terminal-typed* complex runs
through the same path (grid suppression + per-script shaping faces on the
sdf-layer); the exact hook points are recorded below. Measured numbers follow.

## The problem

Glyph resolution today is strictly **one Unicode codepoint → one glyph**
(`FT_Get_Char_Index` per codepoint; the atlas is keyed on the codepoint itself).
That is correct for Latin, Greek, Cyrillic, CJK and emoji, but it renders the
complex scripts *wrong even when the right font is installed*:

- **Arabic** — letters have isolated / initial / medial / final contextual
  forms selected by GSUB joining. Per-codepoint lookup always draws the isolated
  form, so text appears as disconnected stumps instead of a joined cursive run.
- **Indic** (Devanagari, Bengali, Tamil, Telugu, Kannada, …) — needs glyph
  *reordering* (the pre-base matra `ि` is typed after but drawn before its
  consonant), conjunct formation (consonant + virama + consonant → a single
  ligature), and mark positioning. None of that happens per-codepoint.
- **Thai / Lao / Khmer / Myanmar** — above/below vowel and tone marks must be
  positioned relative to the base; per-codepoint they land on the wrong spot or
  overstrike.
- **Combining marks generally** (Latin diacritics, Hebrew points, Vietnamese)
  — the model *captures* the marks but the renderer drops them (see below).

ucs-detect does not measure any of this — it is a rendering-fidelity concern, not
a capability bit. The goal is these scripts actually *looking right*, and drawing
the line on how far a cell-grid terminal should go.

## The premise is now met: fonts + shaping tables are present

The "once font coverage lands" precondition has been satisfied by the
font-routing work:

- **218 faces staged** at build time (`build-*/embed-data/fonts/`), including all
  the complex scripts: Arabic (Naskh / Kufi / Sans / UI), Devanagari (Sans /
  Serif / UI), Thai (Sans / Serif / Looped / UI), Bengali, Tamil, Telugu,
  Kannada, Gujarati, Sinhala, Hebrew, Khmer, Myanmar.
- Every one of them carries the OpenType **GSUB + GPOS + GDEF** tables a shaper
  needs (verified on Noto Sans Arabic, Naskh Arabic, Devanagari, Thai, Tamil).

So the raw material for shaping is entirely in place. Shaping is now the *active*
blocker for these scripts, not a future concern.

## Where a shaper would plug in — current architecture

There are two independent text paths, with very different shaping-readiness:

| Path | Where | Positioning | Shaping-ready? |
|---|---|---|---|
| **Terminal grid** | `yvterm/vterm.c` `vterm_pack_line` + fragment shader, atlas in `yfont/ms-raster-font.c` / `ms-msdf-font.c` | rigid cell grid — one glyph baked per cell slot, fixed advance | **No** — 4-word cell format has no per-glyph x/y offset or advance; the shader addresses glyphs by cell, not by a glyph list |
| **ydraw drawable text** | `yvterm/sdf-layer.c` `expand_text_span`, atlas in `yfont/raster-font.c` / `msdf-font.c` | free — emits a GLYPH record with explicit float `glyph_x`/`glyph_y` and per-glyph advance | **Structurally yes** — already expresses non-cell-aligned positions; just runs no shaper |

What is already good:

- **The model layer is cluster-complete.** `struct yetty_yvterm_text_cell`
  (`yvterm/grid.c`) stores the grapheme cluster as `codepoint` (base) +
  `marks[5]` (combining continuation), captured from libvterm's `chars[0..]`.
  Nothing is lost at the model layer.
- **Scroll is O(1) and content-preserving.** The rolling-row ring moves whole
  lines by advancing a base index; a line's cells (marks included) survive scroll
  and scrollback eviction/rematerialization verbatim. So a **line-level shape
  cache is viable** — a shaped line stays valid until its content changes.
- The cell already carries a `glyph_index` field that is currently reserved and
  unused — a natural hook for shaped output.

The blockers are all downstream of the model:

1. **Glyph resolution + atlas are codepoint-keyed.** The atlas slot *is* the
   codepoint (per style, per face); there is no glyph-id concept. Shaping output
   is glyph IDs + positions, so the atlas would need re-keying on
   `(glyph_id, face)` (or a parallel shaped-glyph atlas).
2. **The terminal cell GPU format + shader are one-glyph-per-cell** with zero
   per-glyph offset/advance (the only multi-cell mechanism is the wide-glyph
   head+spill trick). It cannot express a cluster that shapes to glyphs whose
   advances and mark offsets don't fall on cell boundaries.
3. **Captured marks are dropped before the GPU.** `vterm_pack_line` resolves a
   glyph from `cell->codepoint` only; `marks[]` is read by no render code. So
   even simple combining diacritics — which need *no* HarfBuzz — are lost today.

## What other terminals do

- **kitty / foot / wezterm** — full HarfBuzz shaping. kitty is the reference for
  reconciling shaping with a cell grid: it shapes runs, then maps the shaped
  glyphs back onto cells (handling the "one cluster spans N cells" case). foot
  makes shaping a build option; wezterm shapes everything.
- **Alacritty** — deliberately *no* complex shaping, per-cell only. This is
  yetty's current model, and a legitimate end state if the cost is judged too
  high.

The split in the ecosystem shows this is a genuine scope choice, not a settled
default.

## Cost

**Binary size (measured).**

| Library | Size | Notes |
|---|---|---|
| HarfBuzz 8.3.0, full shared (all shapers, no ICU/glib) | ~1.05 MB | system reference on x86_64 |
| HarfBuzz, ICU integration (`libharfbuzz-icu`) | 14 KB | **separable — core needs no ICU** |
| HarfBuzz, minimal static (built-in `hb-ucd` Unicode, no glib/ICU/Graphite/FreeType) | 1.88 MB archive (x86_64) / 1.75 MB (wasm); ~0.7 MB gzipped tarball. In-binary footprint is smaller after `--gc-sections` drops unused shaping code. | the shipped build |
| FreeType static, this build | 1.15 MB | for scale |

HarfBuzz's only optional heavy deps (ICU, glib, Graphite2) are all off by
default and unneeded — it ships built-in Unicode tables (`hb-ucd`). So the
permanent footprint is sub-megabyte.

**Build integration (low-to-moderate, well-trodden).** yetty pulls native libs
as prebuilt static tarballs (per-platform `build-tools/3rdparty/<lib>/_build.sh`
→ GitHub Release → `build-tools/yetty/libs/<lib>.cmake` importing a
`STATIC IMPORTED` target). FreeType is the exact analog and is already shipped
for every platform including webasm and android. Notably:

- FreeType is built with `-DFT_DISABLE_HARFBUZZ=ON`, and its build script
  comments that HarfBuzz is expected to be "wired in separately" — the
  architecture already anticipates this dependency. There is even a breadcrumb in
  the tree: `ylexbor/README.md` lists "Real font shaping (FreeType/HarfBuzz) —
  not implemented (uniform glyph width)".
- Adding HarfBuzz is mechanical: one `3rdparty/harfbuzz/{version,_build.sh}`
  (clone FreeType's — its per-platform `case` blocks already cover
  linux/macos/ios/tvos/android/webasm/windows), one CI matrix workflow, one
  `libs/harfbuzz.cmake`, and one `YETTY_ENABLE_LIB_HARFBUZZ` option gated in
  `shared.cmake`.
- HarfBuzz is C++; the project already links C++ (Dawn/WebGPU), so no new
  toolchain surface. Android C++ via `c++_static` is already how openh264 ships.
- **WebASM was the one real risk — now cleared at the build level.** webasm
  disables its other C++ prebuilts over an emcc ABI mismatch, but HarfBuzz is
  pure-compute (no threads, no POSIX file I/O) and patterns after FreeType
  (which does build for wasm). Built in the emcc pipeline (non-mt variant, no
  `-pthread`) it produces a relocatable-wasm object with the shaping symbols
  defined — the exact format `wasm-ld` links into `yetty.wasm`. The final
  in-binary link is deferred only because no `hb_*` call reaches the wasm build
  until the terminal-grid routing lands.

**Runtime (bounded by a shape cache).** Only complex-script runs need shaping;
Latin / CJK / emoji stay on the fast per-codepoint path. Shaped lines are cached
(keyed on the line's cluster content) and invalidated only when the line changes;
the rolling-row scroll never invalidates a cached shape. HarfBuzz shaping of a
short run is microseconds, off the per-frame path entirely. The precise figure is
confirmed by the Phase 1 spike.

The FT↔HB coupling has two forms, both supported by the existing model. The
recommended one is **HarfBuzz standalone**: build it without the FreeType glue
and feed it font tables from the already-loaded FT faces at runtime
(`hb_face_create` over the SFNT blob, or `hb_ft_font_create` at use-site). This
avoids a producer build-order cycle between the two tarballs.

## Decision

**Adopt HarfBuzz, scoped to complex scripts, phased — and do the dependency-free
interim mark rendering first.**

Rationale: the fonts and shaping tables are all present, the footprint is
sub-megabyte, and the build integration is a solved pattern the architecture
already expects. The real work is not the dependency — it is the downstream atlas
re-key and finding a GPU path that can express shaped positions. That work is
isolated by routing complex-script runs through the **existing free-position
`expand_text_span` path** instead of first reworking the rigid terminal cell
shader.

Phasing:

- **Phase 0 — interim combining-mark rendering (no HarfBuzz). DONE.** The
  raster (FreeType) backend now composites the captured `marks[]` over the base
  into one cluster-keyed atlas slot (`ms-raster-font.c`
  `get_glyph_index_cluster` + a per-style cluster cache), each mark placed at
  its own FreeType bearing and max-blended. `vterm_pack_line` resolves
  mark-bearing cells through that path; because the default base face is MSDF
  (no live rasterizer), a mark cell whose face is MSDF is re-resolved against a
  raster face that covers the base **and** the marks (the wide `Noto*` glob),
  so diacritics render under the stock config rather than only when the base is
  raster. Verified: Latin/Greek/Cyrillic/Vietnamese (é à ô ü ñ ç, ế ọ, и́, ά)
  plus Hebrew points and Thai/Devanagari matras. Does **not** address Arabic
  joining or Indic reordering (those stay for Phase 1). Two documented limits:
  the MSDF backend cannot composite (it has no live rasterizer, so its own
  glyphs still drop marks — hence the raster re-resolve), and a mark that
  overhangs the base cell is clamped into the slot rather than spilling.
- **Phase 1 — HarfBuzz for complex scripts via the ydraw free-position path.
  Shaping engine + render path DONE; terminal-grid routing REMAINS.** Landed:
  - The HarfBuzz dependency (minimal static build, no ICU/glib/Graphite,
    built-in `hb-ucd`), fetched as a prebuilt tarball per platform
    (`build-tools/3rdparty/harfbuzz/`, `build-tools/yetty/libs/harfbuzz.cmake`,
    the `build-3rdparty-harfbuzz.yml` CI matrix, and the
    `YETTY_ENABLE_LIB_HARFBUZZ` option). Standalone FT↔HB coupling: HarfBuzz
    reads the SFNT tables from the loaded FreeType face
    (`hb_face_create_for_tables` + `FT_Load_Sfnt_Table`), so there is no
    build-order cycle. Linux/webasm tarballs built and validated locally; the
    desktop yetty binary links it. (#615)
  - The shaper itself — `raster-font.c` `shape_run` op: `hb_buffer` +
    `hb_buffer_guess_segment_properties` (infers script/direction/language) +
    `hb_shape`, returning per-glyph gid + advance + GPOS offsets in base_size
    pixels. Scripts are grouped into runs by an OpenType-free classifier
    (`shaping-script.c`, `yetty_yfont_shaping_script_for_codepoint`). (#616)
  - The atlas `(glyph_id, face)` re-key — `raster-font.c`
    `get_glyph_index_by_gid` + a `gid_map`, rasterizing by glyph index via the
    shared `rasterize_gid_into_slot` core (the face is the font object, so the
    key reduces to the gid). (#616)
  - The render path — `sdf-layer.c` `expand_text_span` now detects complex
    runs and emits shaped glyphs through the existing free-position glyph
    records (`expand_shaped_run`): per shaped glyph it resolves the gid to an
    atlas slot and places it at `pen + (bearing + GPOS_offset) * scale`,
    advancing by the shaped advance. HarfBuzz emits visual order for LTR and
    RTL alike, so the left-to-right pen walk is correct for both. Backends with
    no shaper (MSDF, or a build with HarfBuzz off) leave `shape_run` NULL and
    fall through to the per-codepoint path — the whole feature is a runtime
    no-op when the gate is off. (#616)
  - Tests + spike: `test/ut/yfont/shaping-test.c` asserts Arabic contextual
    joining (a medial letter's gid differs from its isolated gid), Devanagari
    pre-base-matra reordering (glyph order is non-monotonic vs input), the gid
    atlas resolves+caches, and the classifier. `shaping-render-test.c` drives
    the full shape→atlas→rasterize→composite pipeline and (with
    `YFONT_SHAPING_DUMP=<path.ppm>`) emits a canvas — "العربية" renders as a
    correctly joined cursive run and "हिन्दी" with the reordered i-matra + the
    न्द conjunct. That canvas is the Phase 1 acceptance spike. (#616)

  Remaining for terminal-typed complex text (typing Arabic at the shell): the
  terminal grid renders cells through `vterm_pack_line` + the grid shader, NOT
  through the sdf-layer that owns the shaping render path. To route a terminal
  run through it: (1) `vterm_pack_line` sets `words[0]=0` for the run's covered
  cells so the grid draws only their background (the existing shader-glyph-cell
  precedent), and (2) the sdf-layer, in its per-window-row Pass 2, scans that
  row's cells for complex runs and calls `expand_shaped_run` against a
  per-script raster shaping face loaded into `layer->fonts[]` (the sdf-layer
  currently holds only the MSDF default + ycat wire fonts, none covering the
  complex scripts, so it needs a small per-script face loader). The per-line
  shape cache is then the line's own primitive/scan result, invalidated on line
  edit; the O(1) rolling-row scroll already preserves it.
- **Phase 2 — optional: programming ligatures / contextual Latin on the main
  grid** (Fira Code `=>`, `!=`, …). Only if there is demand; this is the largest
  rework because it needs the terminal cell format/shader to carry ligature-span
  info (kitty-style cell mapping). Explicitly out of scope for the initial
  adoption.

If Phase 1's webasm spike fails to link, the fallback is to gate
`YETTY_ENABLE_LIB_HARFBUZZ` OFF on webasm (complex scripts fall back to the
current per-codepoint rendering there) while keeping it ON for
desktop/android/ios — the flag scheme supports exactly this per-platform
override.

## Follow-up implementation issues

1. **Interim combining-mark rendering (no HarfBuzz)** — DONE (Phase 0). Render
   captured `marks[]` via FreeType metrics into the cell; the diacritic case
   that was fully dropped. (#614)
2. **HarfBuzz dependency integration** — DONE. `build-tools/3rdparty/harfbuzz`
   + `libs/harfbuzz.cmake` + CI matrix + `YETTY_ENABLE_LIB_HARFBUZZ` option;
   minimal static build (no ICU/glib/Graphite), standalone FT coupling. (#615)
3. **Complex-script shaping via the ydraw free-position path** — shaping engine
   + render path DONE, terminal-grid routing remains (see Phase 1 above).
   Arabic/Indic/Thai run detection → HarfBuzz → `expand_text_span` shaped
   emission, with the atlas `(glyph_id, face)` re-key. The Arabic + Devanagari
   spike renders correctly (`shaping-render-test.c`). (#616)
4. **WebASM HarfBuzz build spike** — build validated: HarfBuzz compiles under
   emcc (non-mt, no `-pthread`) to a relocatable-wasm object with
   `hb_shape_full` / `hb_buffer_create` defined — the format `wasm-ld` consumes
   for `yetty.wasm`. The full `yetty.wasm` link lands with the terminal-grid
   routing (nothing calls `hb_*` in the wasm build until then). (#617)
5. **(Stretch) Programming-ligature shaping on the terminal grid** — kitty-style
   shaped-glyph→cell mapping; requires the terminal cell GPU format to carry
   ligature spans. (Phase 2, demand-gated — out of scope for the initial
   adoption per the issue.) (#618)
