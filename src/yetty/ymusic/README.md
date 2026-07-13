# ymusic — LilyPond-subset score engraver → ydraw drawable list

`ymusic` is the yclass class `ymusic:music`: it parses a subset of LilyPond
into an editable score model, lays the model out, and renders it to a
[`ydraw-core`](../ydraw-core/README.md) drawable list. Like
[`yflame`](../yflame/README.md) it is a pure frontend — it produces the
picture; [`yfigure`](../yfigure/README.md) displays it, and
[`yview`](../yview/README.md) ships it to a server figure so it scrolls.
GPU-less; depends on `ycore`, `yclass`, `yface`, `ydraw_core`, `ysdf`.

## Engraving model

It engraves the way LilyPond does: **symbolic** shapes (clefs, noteheads,
rests, accidentals, flags, dots, time-signature digits) are glyphs from the
Emmentaler music font rendered through the MSDF text path; **geometric**
shapes (staff lines, stems, ledger lines, barlines) are drawn directly as
[`ysdf`](../ysdf/README.md) segments. No intermediate format — model
straight to GPU primitives.

The Emmentaler font (SIL OFL, vendored at `assets/fonts/Emmentaler-20.otf`)
is referenced **by name** via
`yetty_ydraw_drawable_list_add_font_named(buf, "Emmentaler")`; the receiver
resolves it from the install's pre-generated MSDF atlas
(`msdf-fonts/Emmentaler.cdb`, built with
[`ymsdf-gen`](../ymsdf-gen/README.md)), so a score never carries font bytes.
The SMuFL-PUA codepoint table is a named-constant block — re-pointing it at
Bravura's codepoints is the whole font swap.

## Parser (a pragmatic LilyPond subset)

Understood: `\clef`, `\time N/D`, `\key <pitch> \major|\minor`,
`\relative [<pitch>]`, notes (`a..g` with `is`/`es` accidentals, `'`/`,`
octave marks, optional duration and dots), rests (`r`), chords (`<...>`),
and bar checks (`|`). Unknown `\commands`, strings and braces are skipped,
so `\version` / `\header` / `\score` / `{ }` / `\new Staff` wrapping is
tolerated rather than parsed.

The score model (score → staff → measure → element → note) is a mutable
tree with a stable id on every element, so an editor can hit-test, select
and mutate notes in place; `hit_test` / `set_highlight` already exercise
that id surface. Layout wraps whole measures into systems at the configured
width. Durations render up to 32nds with flags (no beaming).

## Public API (generated `include/yetty/ymusic/music.h`)

```c
yetty_ymusic_register();
struct yetty_yclass_object_ptr_result mr = yetty_ymusic_music_create(NULL);
struct yetty_yclass_object *music = mr.value;

yetty_ymusic_configure(music, width_px, staff_space_px, YETTY_YMUSIC_FLAG_NONE);
yetty_ymusic_parse(music, lilypond_text, len);
struct yetty_ydraw_drawable_list_result lr = yetty_ymusic_render(music);
/* lr.value is caller-owned; hit_test/set_highlight need a prior render() */
struct yetty_ycore_int_result hit = yetty_ymusic_hit_test(music, x, y);
yetty_ymusic_set_highlight(music, hit.value);   /* -1 clears */
yetty_ymusic_destroy(music);

/* One-shot: serialize a rendered list as a YDRAW_BIN DCS envelope
 * (LZ4F bin meta via yface) straight to an fd — the CLI path. */
yetty_ymusic_emit_osc(lr.value, STDOUT_FILENO);
```

All method slots are `local@` — an in-process frontend, never proxied over
RPC; the model still records the methods so the FFI bindings emit them.

## Consumers

- `../ycat/handler-music.c` — `.ly` files drawn inline in the scrollback
  ([`ycat`](../ycat/README.md)); staff spacing tracks the cell height.
- `tools/yless` — scores in a scrollable [`yview`](../yview/README.md)
  figure.
- `tools/yhello`, `tools/ygreeter` — demo tabs.
- `bindings/python/yetty/generated/ymusic.py` via
  [`yffi`](../yffi/README.md).

Gated by `YETTY_ENABLE_FEATURE_YMUSIC` (plus `YDRAW` and `YFACE`).

## Layout of the module

| file | role |
|------|------|
| `music.c` | the only hand-written file: model, parser, layout, emit passes, method slots (`#include`s `music.gen.c` at the foot) |
| `music.gen.c`, `rpc.gen.c`, `model.yaml` | codegen output — never hand-edited |
| `../../../include/yetty/ymusic/music.h` | generated public header |

Status: single staff per score for now — the model tree is shaped to grow
to multiple staves; ties are recorded in the model but not yet drawn.
