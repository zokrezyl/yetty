# poc/yvterm — text-grid performance probe

A throwaway, standalone probe for the **yvterm-new** design: drive a terminal
from the libvterm **state** layer (NOT `VTermScreen`), keep the grid as one
contiguous collection of lines, and render it through the **real** MSDF/cdb
text path so we can measure throughput at 4K with small cells.

## What it deliberately does

- **No `VTermScreen`.** We hook `VTermState` via `VTermStateCallbacks`
  (`putglyph` / `movecursor` / `scrollrect` / `moverect` / `erase` /
  `setpenattr` / `resize`) and compose cells ourselves. This is the layer
  below the screen, with the corresponding callbacks — the whole point of the
  rewrite.
- **One contiguous cell ring = the line collection.** The backing store is a
  single `2*rows × cols` array of 16-byte packed cells (the exact layout
  `text-layer.wgsl` reads: `glyph_index`, packed fg/bg, attrs). `line[i]` is
  just an offset into it — no separate cached buffer. The GPU storage buffer
  points straight at this array.
- **O(1) whole-screen scroll** via the `root_row` uniform (the libvterm
  double-buffer trick): a full-width vertical scroll only slides `root_row` and
  blanks the newly exposed lines — no per-line content memmove. Scroll regions
  and reverse-index fall back to `vterm_scroll_rect` decomposition. The probe
  counts how often each path is taken.
- **Real render path.** Reuses `ms-msdf-font.c` + the `ycdb` glyph database +
  the production `text-layer.wgsl` / `ms-msdf-font.wgsl` shaders, unchanged.
  cdb/MSDF is the slow path — measuring it is the entire point.

## What it deliberately omits

Scrollback view UI, rich content / sdf-msdf refs (the per-line ref slot the
real design carries), selection, alt-screen. Text throughput only.

## Perf harness

Per-second stats on stdout: FPS, frame time, dirty-rows/frame, bytes uploaded,
scroll fast-path vs memmove counts. `--stress` dirties the whole grid every
frame to measure worst-case upload + MSDF shading.

## Build / run

Built as a CMake target (`poc-yvterm`) wired from the root `CMakeLists.txt`,
reusing the project's GPU/font/render libs and Dawn — the only way to exercise
the real MSDF/cdb path. After a normal desktop configure:

    ./build-desktop-ytrace-release/poc-yvterm            # spawns $SHELL
    ./build-desktop-ytrace-release/poc-yvterm --stress   # worst-case probe

Keys: `Ctrl-Q` quits. Everything else is forwarded to the child PTY.
