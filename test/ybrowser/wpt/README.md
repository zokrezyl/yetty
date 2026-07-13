# ybrowser WPT-style layout tests

Web-platform-feature tests for the ybrowser layout engine, ordered basic →
advanced by the numeric category prefix.

Web Platform Tests proper are pixel reftests; ybrowser has no headless pixel
pipeline, but it can emit its post-layout box tree (`--dump-boxes`), so these
tests assert on box **geometry** instead of pixels. Two shapes (both standard
WPT forms):

- **Self-assert** — an element carries
  `data-test="<name>" data-expect="<x>,<y>,<w>,<h>"`; each field is an absolute
  px value or `*` to skip. The named box must match within ±1.5px.
- **Reftest** — `<link rel="match" href="<name>-ref.html">`; the test and its
  reference must produce identical geometry for every shared `data-test` name.

## Running

```sh
test/ybrowser/wpt/run.py            # all categories
test/ybrowser/wpt/run.py 01 06      # only paths containing 01 / 06
ctest --test-dir <build> -R ybrowser_wpt
```

Env: `YBROWSER=<path>` (default `build-desktop-ytrace-release/...`),
`WPT_WIDTH=<px>` (default 800). A graphical `DISPLAY` is required because the
`--once` path still spins up GPU asset extraction.

## Categories

| dir | feature | status |
|-----|---------|--------|
| 01-box-model | width/height, margin, padding, box-sizing, min/max-width | green |
| 02-display | block stacking, inline-block, visibility, display:none | inline-block open |
| 03-floats | float width, doesn't swallow row | green |
| 04-position | relative offset, absolute inset | green |
| 05-flexbox | grow/ratio, basis, justify (start/center/end/between), gap, column | shorthand-basis open |
| 06-grid | px/fr/repeat tracks, gap, 3-col, sidebar+content | green |
| 07-text | line-height single + br two-line | — |
| 08-overflow | overflow:hidden keeps height | green |
| 09-tables | table-layout:fixed even split | open |
| 10-transform | translate offset | green |
| 11-sizing | auto height sum, px height, % height | %-height open |
| 12-margins | margin:auto centering, adjacent collapse | green |
| 13-borders | border widens (content-box) / included (border-box) | green |
| 14-flex-advanced | flex-wrap, item min-width floor | open |
| 15-grid-advanced | auto-placement to next row | green |
| 16-position-advanced | fixed viewport, absolute right/bottom insets | green |
| 17-zindex-overflow | z-index no geometry, overflow:scroll keeps size | — |
| 18-custom-elements | custom/unknown elements lay out as inline/blockified | — |
| 19-media-queries | @media width gating (incl. range syntax) | — |
| 20-events | DOM event dispatch affecting layout | — |
| 21-dom-implementation | document/DOM API contract | — |
| 22-url | URL resolution | — |
| 23-grid-span | grid-column/row span placement | — |
| 24-flex-image | replaced items (images) in flex containers | — |
| 25-position-flow | positioned boxes vs normal flow | — |
| 27-flex-shrink | shrink distribution, min-content floors | — |
| 28-form-controls | input/button/select intrinsic boxes | — |
| 29-selectors | selector matching (:is/:where/:not desugar &c.) | — |
| 30-iframe | iframe box + child-document geometry | — |

(There is no category 26 — 25 jumps to 27.)


## Known failures (open engine bugs the suite pins)

- `02-display/inline-block-sized` — `display:inline-block` produces no box
  (treated as plain inline text); width/height ignored.
- `05-flexbox/basis` — `flex:0 0 150px` shorthand: libcss in this tree does not
  expand the `flex` shorthand's basis component.
- `09-tables/fixed-two-cells` — `table-layout:fixed` collapses cells to
  min-content instead of splitting the declared width.
- `11-sizing/height-percent` — percentage height against a fixed-height parent
  is not resolved.
- `14-flex-advanced/wrap` — `flex-wrap` is not implemented (items overflow on
  one line).
- `14-flex-advanced/min-width-floor` — a shrinking flex item does not respect
  its `min-width`.

## Fixed via this suite

- `display:grid` from an inline `style` (was ignored — only stylesheet-class
  grids were detected).
- flex `gap` / `column-gap` (was ignored).
- absolute child `bottom`/`right` insets against a parent with an explicit
  height (containing-block height was taken as the in-flow content height = 0).
