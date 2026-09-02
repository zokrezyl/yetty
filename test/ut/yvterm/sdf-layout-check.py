#!/usr/bin/env python3
"""grid-sdf-layer C <-> WGSL prim-buffer layout contract check.

The CPU staging (grid-sdf-layer.c) prepends an N-word header to every prim
record; the shader (grid-sdf-layer.wgsl) reads the header and the record via
hardcoded word offsets. Nothing ties the two at compile time, so a header
change that misses one WGSL offset silently corrupts every read after it —
glyphs kept rendering while all SDF shapes vanished when the header grew from
1 word to 3 and the generated evaluate_sdf_2d record base stayed at +1.

This check pins:
  1. the header word count on the C side (staging writes + size accounting);
  2. every accessor offset in the WGSL against the documented layout;
  3. the record-base argument of the generated-evaluator call sites
     (evaluate_sdf_2d / eval_gradient) == the header word count.

Runs as a plain ctest: exits nonzero with a precise message on any mismatch.
"""
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
C_FILE = REPO / "src/yetty/yvterm/grid-sdf-layer.c"
WGSL_FILE = REPO / "src/yetty/yvterm/grid-sdf-layer.wgsl"

HEADER_WORDS = 7  # [rolling_row][offset_x][offset_y][clip_x][clip_y][clip_w][clip_h]

# accessor -> expected word offset (SDF records)
SDF_ACCESSORS = {
    "ydraw_read_rolling_row": 0,
    "ydraw_read_drawable_type": HEADER_WORDS + 0,
    "ydraw_read_fill_color": HEADER_WORDS + 2,
    "ydraw_read_stroke_color": HEADER_WORDS + 3,
    "ydraw_read_stroke_width": HEADER_WORDS + 4,
    "ydraw_read_geom_f32": HEADER_WORDS + 5,
}
GLYPH_ACCESSORS = {
    "glyph_read_x": HEADER_WORDS + 2,
    "glyph_read_y": HEADER_WORDS + 3,
    "glyph_read_font_size": HEADER_WORDS + 4,
    "glyph_read_packed": HEADER_WORDS + 5,
    "glyph_read_color": HEADER_WORDS + 6,
}

failures = []


def fail(message):
    failures.append(message)


def main():
    c_source = C_FILE.read_text()
    wgsl = WGSL_FILE.read_text()

    # 1) C: the per-prim staging size accounting names the header width.
    match = re.search(
        r"total_record_words \+=\s*(\d+)u\s*/\* rolling_row \+ offset_x \+ offset_y \+ clip rect \*/",
        c_source)
    if not match:
        fail("C: header size accounting (total_record_words += Nu /* ... */) not found")
    elif int(match.group(1)) != HEADER_WORDS:
        fail(f"C: staging header is {match.group(1)} words, contract says {HEADER_WORDS}")

    # 1b) C: the staging loop writes exactly the three header words before the
    # record copy (rolling_row + two offset memcpys).
    stage = re.search(
        r"prim_staging\[cursor\+\+\] = \(uint32_t\)meta->rolling_row;.*?"
        r"memcpy\(&layer->prim_staging\[cursor\+\+\], &meta->offset_x.*?"
        r"memcpy\(&layer->prim_staging\[cursor\+\+\], &meta->offset_y.*?"
        r"memcpy\(&layer->prim_staging\[cursor\+\+\], &meta->clip_x.*?"
        r"memcpy\(&layer->prim_staging\[cursor\+\+\], &meta->clip_y.*?"
        r"memcpy\(&layer->prim_staging\[cursor\+\+\], &meta->clip_w.*?"
        r"memcpy\(&layer->prim_staging\[cursor\+\+\], &meta->clip_h",
        c_source, re.S)
    if not stage:
        fail("C: staging loop does not write the 7-word [rolling_row][offsets][clip] header")

    # 2) WGSL: accessor offsets match the documented layout.
    for name, expected in {**SDF_ACCESSORS, **GLYPH_ACCESSORS}.items():
        pattern = rf"fn {name}\([^)]*\)[^{{]*\{{\s*return[^;]*drawable_offset \+ (\d+)u"
        found = re.search(pattern, wgsl)
        if not found:
            fail(f"WGSL: accessor {name} not found / not in expected form")
        elif int(found.group(1)) != expected:
            fail(f"WGSL: {name} reads word +{found.group(1)}, layout says +{expected}")

    # 3) WGSL: generated-evaluator record base == header width.
    for call in ("evaluate_sdf_2d", "yetty_ysdf_eval_gradient_color_2d"):
        for base in re.findall(rf"{call}\(drawable_offset \+ (\d+)u", wgsl):
            if int(base) != HEADER_WORDS:
                fail(f"WGSL: {call} record base is +{base}, header is {HEADER_WORDS} words")
        if not re.search(rf"{call}\(drawable_offset \+ \d+u", wgsl):
            fail(f"WGSL: no {call}(drawable_offset + Nu, ...) call site found")

    if failures:
        for message in failures:
            print(f"sdf-layout-check: FAIL: {message}")
        return 1
    print("sdf-layout-check: OK — C staging header and WGSL offsets agree "
          f"({HEADER_WORDS}-word header)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
