# ybrowser test & Chrome-oracle stack — the map

Everything that validates the ybrowser engine lives under this directory.
This README is the orientation layer: what each tier owns, how the
Chrome-delta tooling works, and which loop to reach for. Per-suite detail
lives in each subdirectory's README.

The stack is a deliberate **precision hierarchy** — each tier owns a
different failure class, and Chrome only ever runs at fixture-authoring /
triage time, never in CI (CI compares offline against committed references):

| Tier | Directory | Owns | Tolerance | ctest |
|---|---|---|---|---|
| C contract/golden | `ut/` | one behaviour per test: layout, inline text, paint stream, DOM, storage isolation, loader, committed-Wikipedia layout | exact | `ybrowser_layout`, `ybrowser_inline`, `ybrowser_paint`, `ybrowser_dom`, `ybrowser_storage_isolation`, `ybrowser_loader`, `ybrowser_wikipedia` |
| Per-feature geometry pins | `wpt/` | tiny synthetic WPT-style cases, 30 categories | ±1.5px | `ybrowser_wpt` |
| Fine-grained Chrome rects | `ut/geometry-oracle/` | per-property `getBoundingClientRect` diffs | x/w ≤2px, y/h ≤16px | **none — manual only** (not wired into ctest/make) |
| Whole-page Chrome parity | `anchors/` | realistic page *shapes* (nav bars, card rows, grids) | loose, structure-first (see below) | `ybrowser_anchors` |
| Paint/compositing regression | `render/` | stacking order, iframe compositing, live-site box-count collapse floors | order/floors only | `ybrowser_stacking_order`, `ybrowser_iframe_render`, `ybrowser_render` (network) |
| Real WPT conformance | `wpt-upstream/` | upstream Web Platform Tests via `--dump-wpt` | spec | manual (needs an external WPT checkout) |

Runner wiring for all of it: `ut/CMakeLists.txt`. Fast lane:
`ctest -L wpt` / `make test-wpt`; nightly networked lane: `make test-nightly`
(`ctest -L 'network|wpt|render|e2e'`, Woodpecker cron).

## The engine-side interface: `--dump-boxes` and size provenance

All geometry oracles consume `ybrowser --once --dump-boxes -w <width>`
(`tools/ybrowser/main.c`): one TSV row per laid-out box —
`kind, tag, data-test, x, y, w, h, …` plus `ws=`/`hs=` columns.

`ws=`/`hs=` are **size provenance**: the engine stamps every box with WHICH
layout decision produced its final width/height (`css`, `avail`,
`flex-grow`, `flex-shrink`, `flex-even`, `grid-tracks`, `abs-fit`,
`abs-inset`, `content`, `img`, `none`; enum `yl_size_source` in
`src/yetty/ybrowser/ybrowser-internal.h`). A wrong size therefore NAMES the
deciding branch in `ybrowser-layout.c` — `src=abs-fit` is the absolute
shrink-to-fit path, `src=content` the max-content measurement (usually the
approximate font metrics), `src=none` means no stamped path assigned the
size (itself a lead). Sibling flags: `--dump-geo` (dom-path keyed rects) and
`--dump-wpt` (expected-vs-actual for `check-layout-th` assertions).

## The delta tool (`anchors/compare.py`) — how divergence is judged

`make-fixture.py` records the Chrome reference: one headless-Chrome run
stamps `data-test="aNNNN"` anchors on every layout-significant element and
emits both `fixture.html` and `ref.json` (so they cannot drift).
`compare.py` then judges ybrowser's `--dump-boxes` output the way layout
errors actually propagate:

- **parent-relative positions** (each anchor vs its nearest anchored
  ancestor) — one wrong box doesn't shift the whole page red;
- **structure first** — children of every container are clustered into rows
  on both sides and the (rows × max-per-row) shape compared: the direct
  detector for "3-column grid became a stack";
- **root-cause folding** — width findings fold under an ancestor width
  finding (width flows parent→child), height findings fold under a
  descendant one (height flows child→parent), structure/missing findings
  fold everything beneath;
- **mechanism clusters** — surviving findings grouped by
  (kind, tag, class, `src=` provenance):
  `54x width <a .VDXfz> src=abs-fit ΣΔ27810 e.g. a0200` — one line per
  divergence mechanism, pointing at one code path.

Tolerances (deliberately loose; exactness belongs to `wpt/`): x ±8, y ±16
parent-relative, width ±max(8px, 3%), height ±max(16px, 10%), doc-height
ratio gate [0.8, 1.25]. The height slack exists because the engine's text
metrics are approximate (flat glyph-advance ratio) — tightening tolerances
before fixing font metrics only produces noise.

## Which loop to reach for

- **"Did my change regress anything?"** — `ctest -L wpt` (offline, fast),
  then `anchors/run.py` (committed fixtures, `KNOWN_FAILURES` XFAIL ratchet
  flags entries that go green so they can be retired).
- **"Did the engine change move the real sites, and what broke?"** —
  `anchors/corpus.py` (live pages from `corpus.txt` → `tmp/anchors-live/`,
  summary table with per-page deltas vs the previous run persisted in
  `corpus-status.json`), then `anchors/inspect.py <page-dir> <anchor>`
  (`--children` / `--parents`) to chase one cluster exemplar.
- **"Rank many sites by severity"** — `tmp/crawl/batch.py` (15 news
  homepages, severity = structure·4 + missing·3 + geometry) and
  `tmp/crawl/measure.py <dir>` (page-height ratio `x1.NN` + oversized-image
  health). These are throwaway triage drivers that import
  `anchors/compare.py`.
- **"Where exactly does one element differ from Chrome?"** —
  `wpt/compare-chrome.py` (DOM-path keyed) / `wpt/compare-chrome-text.py`
  (visible-text keyed, survives wrapper-div path shifts; prints
  median/total dmax buckets). Both drive Chrome live over CDP — triage
  only, not CI.

**Fixture validity caveat:** a captured fixture is only meaningful if its
external CSS actually loads at compare time. Fixtures captured without
inlined/reachable stylesheets produce garbage deltas — check the inlined-CSS
byte count before chasing a site's numbers.

**Corpus policy** (detail in `anchors/README.md`): live captures stay under
`tmp/`; committed fixtures must be synthetic, obfuscated, or reduced —
never third-party content as-is. Pack 1 = script-stripped static fixtures
(hard gate today); Pack 2 = JS-executing fixtures with recorded fetches and
a determinism prelude (planned — tracked in the parity-process epic, #516).

## The fix workflow (keeps investigation cost low)

1. Run the comparator first — never start from grepping the engine. The
   cluster lines + `src=` provenance name the code path.
2. Fix one mechanism cluster at a time; re-run the same comparator; the
   per-page numbers must move monotonically.
3. Reduce the divergence to a committed pin (a `wpt/` case or an `anchors/`
   fixture) before closing — that is the regression guard.
