# ybrowser layered rendering / layout test harness

Automated, layered red/green feedback for the `ybrowser` web engine, so
rendering work is validated against contracts and against a real browser
instead of by eyeballing screenshots.

## The four layers

1. **Narrow C contract tests** — `test/ybrowser/ut/ybrowser-layout-test.c`,
   `ybrowser-inline-test.c`. One behaviour pinned per test (box-sizing,
   percent basis, viewport units, line-height, inline wrap, …).

2. **Paint-output tests** — `test/ybrowser/ut/ybrowser-paint-test.c`.
   Inspect the serialized `yetty_ydraw_drawable_list` the engine emits, with
   no Chrome and no GPU: image primitives whose bounds must match the
   laid-out box, and the text-decoration rects (underline / line-through /
   overline). Catches "right box, wrong draw stream" bugs that geometry
   comparison alone reports green.

3. **Chrome geometry oracle** — this directory. Run the same fixture in
   Chrome and in `ybrowser`, compare selected box geometry as JSON (not
   pixels) with per-property tolerance.

4. **Sparse visual smoke** — deferred (see the tracking issue). Pixel
   snapshots stay few and coarse until layers 2–3 are stable.

## Layer 3 usage

> **Status:** layer 3 is currently **manual-only** — it is not wired into
> ctest or the Makefile. Run the two scripts below directly. (Wiring it in
> is part of the Chrome-parity process epic, #516.)

Fixtures live in `test/ybrowser/ut/fixtures/`. Each fixture:

- forces `* { font-family: monospace; font-size: 16px }` so `ybrowser`'s
  glyph-advance estimate (~0.602em) lines up with Chrome's real monospace
  metrics, making text-flow geometry comparable;
- marks key elements with `data-test="NAME"`;
- carries an inline script that, after load, walks `[data-test]`, reads
  `getBoundingClientRect()`, records the layout viewport
  (`window.innerWidth/Height`), and writes it all as JSON into
  `<pre id="geom">`.

### Refresh the references (intentional layout changes only)

```sh
python3 test/ybrowser/ut/geometry-oracle/gen-chrome-refs.py [fixture.html ...]
```

This runs each fixture through headless Chrome (`--dump-dom`, which executes
the page script) and writes `<fixture>.ref.json` next to it. Review the diff
before committing — a ref change is a deliberate "we now match Chrome
differently" decision.

### Compare ybrowser against the references

```sh
python3 test/ybrowser/ut/geometry-oracle/compare-geometry.py [--ybrowser PATH] [fixture.html ...]
```

This runs the `ybrowser` tool's `--dump-boxes` mode (keyed by `data-test`),
replays at the exact viewport Chrome used, and compares per property:

- **tight** (≤2px) on `x` / `width` — font-metric-independent for block
  boxes; a real layout regression here fails the build (non-zero exit);
- **loose** (≤16px) on `y` / `height` — these ride on line-height/baseline
  metrics that differ from Chrome; reported but not fatal.

Both scripts are stdlib-only Python 3 and need a `google-chrome` /
`chromium` on `PATH` (generator) and a built `ybrowser` tool (comparator).
