# HarfBuzz complex-script shaping demos

Sample text for the scripts that need OpenType shaping to render correctly:
cursive joining (Arabic), glyph reordering + conjuncts (Indic), and stacked
above/below marks (Thai and the rest of the Brahmic family). Each script echoes
canonical words with a romanization and a note on the shaping behaviour it
exercises.

One script per script-family:

| Script            | Family   | What it exercises                                          |
|-------------------|----------|------------------------------------------------------------|
| `arabic.sh`       | Arabic   | Cursive joining (isolated/initial/medial/final), RTL, harakat |
| `devanagari.sh`   | Indic    | Pre-base matra reordering, virama conjuncts, i/ī vowel signs |
| `bengali.sh`      | Indic    | Reordering, conjuncts, the reph                            |
| `tamil.sh`        | Indic    | Two-part vowel signs that wrap the consonant               |
| `thai.sh`         | Brahmic  | Stacked above/below vowels + tone marks, no word spaces    |

Run one, or `all.sh` for the set:

```sh
./build-desktop-ytrace-release/yetty -e demo/scripts/harfbuzz/arabic.sh
./build-desktop-ytrace-release/yetty -e demo/scripts/harfbuzz/all.sh
```

## What renders today vs. what needs the grid routing

Shaping is implemented in the **ydraw free-position path** (`sdf-layer.c`
`expand_text_span`): a run of complex-script codepoints is handed to HarfBuzz,
and the shaped glyphs are placed by advance + GPOS offset. It is gated behind
`YETTY_ENABLE_LIB_HARFBUZZ` (off by default) — build with it on to exercise the
path:

```sh
cmake -B build-desktop-ytrace-release -DYETTY_ENABLE_LIB_HARFBUZZ=ON
make build-desktop-ytrace-release
```

These scripts `echo` their samples, so the text arrives on the **terminal
grid**, which is a separate render path. Today the grid:

- **positions combining marks** (Arabic harakat, Thai/Indic vowel signs stack on
  their base) — this part is live;
- does **not** yet apply joining, reordering, or conjunct formation — that needs
  the grid run to be routed through the shaping path (tracked separately). Until
  then, echoed Arabic shows disconnected isolated letters and echoed Devanagari
  keeps the typed (unreordered) glyph order.

To see the **fully shaped** output right now, render it through the shaping
pipeline directly. The `yfont_shaping_render` test shapes and rasterizes real
runs and dumps a canvas:

```sh
YFONT_SHAPING_DUMP=tmp/shaped.ppm \
  ./build-desktop-ytrace-release/test/ut/yfont/yfont_shaping_render-test
# then view tmp/shaped.ppm (Arabic العربية joined, Devanagari हिन्दी reordered)
```

The `yfont_shaping` unit test asserts the same behaviour headlessly (Arabic
contextual joining, Devanagari reordering, the glyph-id atlas).
