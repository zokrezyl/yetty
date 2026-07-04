# ybrowser anchor suite — whole-page Chrome-parity geometry tests

Whole-page fixtures with **anchor points** stamped on every layout-significant
element, compared against a committed, Chrome-generated geometry reference.
This is the layer between the per-feature WPT pins (`../wpt`, tiny synthetic
cases, tight ±1.5px) and the live-site smoke test (`../render`, box-count
floors only): realistic page *shapes* — nav bars, card rows, sidebar grids —
checked with loose tolerances that catch the layout-class divergences
("3-column grid became a stack", "flex item took half the row") without
tripping on ±px font drift.

No pixels are ever compared, and Chrome only runs at **fixture-authoring
time** — CI compares against the committed `ref.json`, offline and headless.

## How it works

1. `make-fixture.py <page.html>` loads the page in headless Chrome with an
   injected epilogue. After settle (load + virtual-time delay, so
   timer-driven JS has run) the epilogue stamps `data-test="aNNNN"` on every
   layout-significant element in document order and records each anchor's
   `{tag, rect, parent, display}` — `parent` is the nearest anchored
   ancestor. Chrome's DOM dump becomes `fixture.html`; the recording becomes
   `ref.json`. One Chrome run produces both, so they cannot drift apart.
   `data-test` is used as the anchor attribute because
   `ybrowser --dump-boxes` already emits it — no engine change needed.
2. `run.py` renders each `fixtures/<name>/fixture.html` with
   `ybrowser --once --dump-boxes` at the reference's viewport width and hands
   the box dump to the comparator.
3. `compare.py` judges geometry the way layout errors actually propagate:
   - **positions are parent-relative** (each anchor vs its nearest anchored
     ancestor), so one wrong box doesn't shift the whole page red;
   - **structure first**: children of every container are clustered into
     rows on both sides and the (rows × max-per-row) shape is compared —
     the direct detector for collapsed grids/columns;
   - **root-cause folding** in the report: a width finding is folded when an
     ancestor already has one (width flows parent→child); a height finding
     is folded when a descendant has one (height flows child→parent);
     structure/missing findings fold everything beneath them.

Tolerances (in `compare.py`): x ±8px, y ±16px (parent-relative),
width ±max(8px, 3%), height ±max(16px, 10%), content-height ratio
[0.8, 1.25]. Deliberately loose — exactness belongs to `../wpt`.

## Running

```sh
test/ybrowser/anchors/run.py              # all fixtures
test/ybrowser/anchors/run.py flex         # filter by name substring
ctest --test-dir <build> -R ybrowser_anchors
```

Env: `YBROWSER=<path>` overrides the binary (default release build path).

## Authoring a fixture

```sh
mkdir test/ybrowser/anchors/fixtures/<name>
$EDITOR test/ybrowser/anchors/fixtures/<name>/fixture.html
test/ybrowser/anchors/make-fixture.py \
    test/ybrowser/anchors/fixtures/<name>/fixture.html -w 1200
```

Re-running `make-fixture.py` is idempotent (old anchors are stripped and
re-stamped) — use it to regenerate the reference after editing the page.
Keep a fixture's subresources inside its directory; the fixture must render
offline. Google Chrome must be installed (`CHROME=<path>` to override).

## Corpus policy — the two packs

- **Pack 1 — static fixtures** (this directory today): pages that are
  scriptless by nature or whose settled post-JS DOM was captured. These are
  hard-gate material: deterministic, offline, fast.
- **Pack 2 — JS fixtures** (planned): fixtures whose scripts must *run* in
  ybrowser before layout is read. Needs recorded fetch responses served
  locally, a determinism prelude (seeded random/frozen clock) injected into
  both engines, and a DOM-structure diff before the geometry diff. Run as a
  ratchet until green enough to gate.

**Never commit captured third-party page content as-is.** Committed fixtures
must be synthetic (like the starter set), obfuscated captures (text replaced
word-for-word via a site-wide map, fonts substituted with committed free
ones, images replaced by same-size placeholders — reference generated AFTER
obfuscation), or reduced repros in which no original content survives.

## Known failures / ratchet

`KNOWN_FAILURES` in `run.py` lists fixtures with triaged engine gaps: they
report as XFAIL without failing the lane, and the runner flags them when
they go green so the entry can be retired. The starter fixtures currently
pin real gaps: stylesheet `flex: N` grow/shrink-to-fit mis-splits
(flex-cards), stylesheet `grid-column: span 2` ignored and missing grid-item
stretch (grid-sidebar).
