# Complex-script text shaping (HarfBuzz) — decision record

Research outcome for the "complex-script shaping" track: whether and how to
render Arabic, Indic (Devanagari, Bengali, Tamil, …) and Thai *correctly* in
yetty, now that the bundled-font coverage has landed. This document records the
decision, the measured numbers behind it, and the follow-up implementation work.

Status: **decision recorded — adopt HarfBuzz, scoped and phased.** Measured
numbers below; the runtime perf figure is designed-and-bounded and is confirmed
by the Phase 1 spike. Implementation is split into the follow-up issues listed at
the end.

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
| HarfBuzz, minimal static (`HB_TINY`/`HB_LEAN`, built-in `hb-ucd` Unicode, no glib/ICU/Graphite) | ~0.4–0.7 MB (est.; confirm in spike) | the intended build |
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
- **WebASM is the one real risk.** webasm currently disables its other C++
  prebuilts over an emcc ABI mismatch. HarfBuzz is pure-compute (no threads, no
  POSIX file I/O), so it should pattern after FreeType (which *does* build for
  wasm) rather than the rejected C++ prebuilts — but it must be built in the same
  emcc pipeline (non-mt variant, no `-pthread`) and a spike must confirm it links
  into `yetty.wasm` before committing to it there.

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

- **Phase 0 — interim combining-mark rendering (no HarfBuzz).** Render the
  already-captured `marks[]` via FreeType metrics (stack over the base within the
  cell, spill like a wide glyph where needed). Biggest bang-for-buck: it fixes
  the fully-broken diacritic case (Latin/Greek/Cyrillic/Hebrew/Vietnamese) with
  no new dependency and no atlas re-key. Does **not** address Arabic joining or
  Indic reordering.
- **Phase 1 — HarfBuzz for complex scripts via the ydraw free-position path.**
  Add the HarfBuzz dependency (minimal static build). Route runs of
  Arabic/Indic/Thai codepoints through a shaper feeding `expand_text_span`
  (per-glyph x/y + advance already exist there), with a per-line shape cache.
  Requires the atlas to gain a `(glyph_id, face)` key (or a parallel shaped-glyph
  atlas). Delivers correct Arabic and Devanagari — the spike that demonstrates
  this in a live session is the acceptance for this phase.
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

1. **Interim combining-mark rendering (no HarfBuzz)** — render captured
   `marks[]` via FreeType metrics into the cell; the diacritic case that is
   fully dropped today. (Phase 0.)
2. **HarfBuzz dependency integration** — `build-tools/3rdparty/harfbuzz` +
   `libs/harfbuzz.cmake` + CI matrix + `YETTY_ENABLE_LIB_HARFBUZZ` option;
   minimal static build (no ICU/glib/Graphite), standalone FT coupling. (Phase 1
   prerequisite.)
3. **Complex-script shaping via the ydraw free-position path** — Arabic/Indic/
   Thai run detection → HarfBuzz → `expand_text_span`, with the atlas
   `(glyph_id, face)` re-key and a per-line shape cache. The live-session Arabic +
   Devanagari demonstration lands here. (Phase 1.)
4. **WebASM HarfBuzz build spike** — validate the emcc-built tarball links into
   `yetty.wasm`; decide the webasm gate. (Phase 1.)
5. **(Stretch) Programming-ligature shaping on the terminal grid** — kitty-style
   shaped-glyph→cell mapping; requires the terminal cell GPU format to carry
   ligature spans. (Phase 2, demand-gated.)
