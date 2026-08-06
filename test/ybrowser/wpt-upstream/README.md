# Upstream WPT layout conformance — the ratchet suite

Runs the REAL Web-Platform-Tests layout suites (the `check-layout-th.js`
geometry-assertion tests Chrome/Firefox/Safari gate on) against ybrowser and
enforces a monotonic conformance score in CI.

## How it works

- `run.py <wpt-root> [suites...]` walks the given WPT suite directories
  (default `css/css-flexbox css/css-grid`), renders every check-layout test
  through `ybrowser --once --dump-wpt`, and compares each asserted element's
  `data-offset-x/-y` (against CSSOM-View `offsetLeft/offsetTop` —
  offsetParent-relative) and `data-expected-width/-height` (border-box size)
  at the WPT ±1px tolerance. A test is green iff every assertion matches.
- Output: whole-test and per-assertion (subtest) scores, per-suite breakdown,
  a failure histogram by feature cluster (start with the biggest cluster —
  one mechanism fix typically greens dozens of files), and `tmp/wpt-fails.txt`
  with every failing assertion for root-cause digging.

## The ratchet

`baseline.json` (committed) records the green-test list and the pinned WPT
commit. CI (`.woodpecker/linux.yml`, step `wpt-ratchet`) sparse-clones WPT at
that commit and runs:

    run.py --baseline test/ybrowser/wpt-upstream/baseline.json tmp/wpt

Any previously-green test that fails → exit 1 → red pipeline. Newly-green
tests are listed; re-record and commit the baseline alongside the layout fix
that earned them:

    run.py --write-baseline test/ybrowser/wpt-upstream/baseline.json tmp/wpt

(The `wpt_commit` key is preserved manually — set it when refreshing the WPT
checkout itself, not on ordinary re-records. Keep the pin and the checkout in
sync.)

## Getting a local WPT checkout

    git clone --no-checkout --filter=blob:none --sparse \
        https://github.com/web-platform-tests/wpt tmp/wpt
    git -C tmp/wpt sparse-checkout set css/css-flexbox css/css-grid css/support resources
    git -C tmp/wpt checkout <wpt_commit from baseline.json>

## Scope and roadmap

Only check-layout tests run today (skip counts name the rest). The big
uncovered classes, in payoff order: **reftests** (pixel-exact test/reference
pairs rendered in the SAME engine — the standards-track form of automated
pixel-correct rendering verification; needs an offscreen render+compare
harness), **testharness.js** DOM/JS API tests (names every missing web API
precisely), and crashtests (robustness). Widening the suite list (css/css-
position, css/css-tables, …) works today — add directories to the CI step and
baseline once triaged.
