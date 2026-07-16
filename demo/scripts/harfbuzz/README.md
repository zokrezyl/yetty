# HarfBuzz shaping demos

Sample text for everything that needs OpenType shaping to render correctly:
cursive joining (Arabic), glyph reordering + conjuncts (Indic), stacked
above/below marks (Thai and the rest of the Brahmic family), and programming
ligatures (Fira Code). Each script echoes canonical samples with a note on the
shaping behaviour it exercises.

| Script            | Family    | What it exercises                                          |
|-------------------|-----------|------------------------------------------------------------|
| `arabic.sh`       | Arabic    | Cursive joining (isolated/initial/medial/final), RTL, harakat |
| `devanagari.sh`   | Indic     | Pre-base matra reordering, virama conjuncts, i/ī vowel signs |
| `bengali.sh`      | Indic     | Reordering, conjuncts, the reph                            |
| `tamil.sh`        | Indic     | Two-part vowel signs that wrap the consonant               |
| `thai.sh`         | Brahmic   | Stacked above/below vowels + tone marks, no word spaces    |
| `ligatures.sh`    | Latin ops | Programming ligatures (`=>`, `!=`, `===`, `->`, `\|>`, …)   |

Run one, or `all.sh` for the complex-script set:

```sh
./build-desktop-ytrace-release/yetty -e demo/scripts/harfbuzz/arabic.sh
./build-desktop-ytrace-release/yetty -e demo/scripts/harfbuzz/ligatures.sh
```

## How it renders

Shaping is gated behind `YETTY_ENABLE_LIB_HARFBUZZ` (off by default) — build it
on to exercise the path:

```sh
cmake -B build-desktop-ytrace-release -DYETTY_ENABLE_LIB_HARFBUZZ=ON
make build-desktop-ytrace-release
```

Text echoed by these scripts arrives on the **terminal grid**, which cannot
shape (one codepoint → one glyph, each hard-clipped to its cell). So the shaped
runs are handed off to the **ydraw free-position path**: `vterm_pack_line`
suppresses the covered grid cells and the SDF layer (`sdf-layer.c`,
`shape_row_cells` for complex scripts, `shape_row_ligatures` for ligatures)
re-draws them as HarfBuzz-shaped glyphs placed by advance + GPOS offset, on top
of the grid. Complex-script runs use the matching bundled Noto face; ligatures
use the bundled Fira Code face. Ordinary text stays on the crisp MSDF grid.

With the gate on, all of it renders **live** — type or `echo` the samples and
the joining/reordering/marks/ligatures appear as drawn.

The headless tests pin the same behaviour: `yfont_shaping` (Arabic joining,
Devanagari reordering, glyph-id atlas), `yfont_shaping_render` (full
shape→atlas→rasterize→composite; `YFONT_SHAPING_DUMP=tmp/shaped.ppm` dumps a
canvas), and `yfont_ligature` (the ligature table).
