#!/usr/bin/env python3
"""Compare ybrowser layout geometry against the Chrome reference JSON.

The geometry test layer: for each fixture under
``test/ybrowser/ut/fixtures/`` that has a committed ``<fixture>.ref.json``
(produced by ``gen-chrome-refs.py`` from real Chrome), this runs the
``ybrowser`` tool's ``--dump-boxes`` mode, keys boxes by their ``data-test``
attribute, and compares each element's geometry to the reference with
PER-PROPERTY tolerance.

Why per-property: ybrowser sizes text from a glyph-advance estimate while
Chrome uses real proportional metrics, so text-flow ``y``/``h`` will never
match to the pixel even with a forced monospace font. Block-level ``x`` and
``width`` are font-independent and are held tight; ``y``/``h`` are held loose.

Exit status is non-zero if any TIGHT-tolerance property is out of range, so
this is suitable as a CI gate. Loose mismatches are reported but not fatal.

Usage:
    test/ybrowser/ut/geometry-oracle/compare-geometry.py [--ybrowser PATH] [fixture.html ...]
"""

import argparse
import json
import subprocess
import sys
from pathlib import Path

VIEWPORT_W = 1000
VIEWPORT_H = 600

# Per-property tolerances in CSS pixels. x/width are font-metric-independent
# for block boxes → tight. y/height ride on line-height/baseline metrics that
# differ from Chrome → loose.
TIGHT_TOL = 2.0
LOOSE_TOL = 16.0
TOLERANCES = {"x": TIGHT_TOL, "w": TIGHT_TOL, "y": LOOSE_TOL, "h": LOOSE_TOL}
TIGHT_PROPS = {"x", "w"}

# This script lives in test/ybrowser/ut/geometry-oracle/; the fixtures are its
# sibling directory. The repo root is found by walking up to the Makefile so
# the build-*/tools/ybrowser binary can be located.
FIXTURES_DIR = Path(__file__).resolve().parent.parent / "fixtures"


def _find_repo_root():
    p = Path(__file__).resolve()
    for ancestor in p.parents:
        if (ancestor / "Makefile").exists() and (ancestor / "src").is_dir():
            return ancestor
    return p.parents[4]


REPO_ROOT = _find_repo_root()


def find_ybrowser(explicit):
    if explicit:
        path = Path(explicit)
        if not path.exists():
            sys.exit(f"error: --ybrowser path does not exist: {path}")
        return path
    candidates = list(REPO_ROOT.glob("build-*/tools/ybrowser/ybrowser"))
    if not candidates:
        sys.exit(
            "error: no ybrowser tool binary found under build-*/tools/ybrowser/.\n"
            "Build it first (e.g. make build-desktop-ytrace-release) or pass "
            "--ybrowser PATH."
        )

    # Prefer a release, non-sanitizer build: an asan build aborts the process
    # on the engine's (pre-existing) leak reports, and debug is slower.
    def rank(path):
        name = str(path)
        return (0 if "asan" in name else 1, 1 if "release" in name else 0)

    return sorted(candidates, key=rank, reverse=True)[0]


def dump_boxes(ybrowser, fixture_path, viewport_w, viewport_h):
    """Run `ybrowser --dump-boxes` and return {data_test_name: {x,y,w,h}}."""
    cmd = [
        str(ybrowser),
        "--dump-boxes",
        "-w",
        str(viewport_w),
        "-H",
        str(viewport_h),
        str(fixture_path),
    ]
    # errors="replace": the tool may emit non-UTF8 trace bytes on stderr; we
    # only parse the clean TSV on stdout, so lossy decoding is harmless.
    proc = subprocess.run(
        cmd, capture_output=True, text=True, errors="replace", timeout=60
    )
    if proc.returncode != 0:
        raise RuntimeError(f"ybrowser exited {proc.returncode}:\n{proc.stderr}")
    boxes = {}
    for line in proc.stdout.splitlines():
        if not line or line.startswith("#"):
            continue
        cols = line.split("\t")
        if len(cols) < 7:
            continue
        # columns: kind, tag, dt, x, y, w, h, (style fields...)
        dt = cols[2]
        if dt == "-" or dt == "":
            continue
        # First box wins — that is the element's own principal box.
        if dt in boxes:
            continue
        boxes[dt] = {
            "x": float(cols[3]),
            "y": float(cols[4]),
            "w": float(cols[5]),
            "h": float(cols[6]),
        }
    return boxes


def compare_fixture(ybrowser, fixture, ref):
    # `_viewport` (if present) is the exact innerWidth/innerHeight Chrome laid
    # out at; replay ybrowser there so vw/vh resolve against the same box.
    viewport = ref.pop("_viewport", {"w": VIEWPORT_W, "h": VIEWPORT_H})
    got = dump_boxes(ybrowser, fixture, int(viewport["w"]), int(viewport["h"]))
    tight_failures = []
    loose_failures = []
    missing = []
    for name, ref_geom in ref.items():
        if name not in got:
            missing.append(name)
            continue
        got_geom = got[name]
        for prop in ("x", "y", "w", "h"):
            delta = abs(got_geom[prop] - ref_geom[prop])
            if delta > TOLERANCES[prop]:
                rec = (name, prop, got_geom[prop], ref_geom[prop], delta)
                if prop in TIGHT_PROPS:
                    tight_failures.append(rec)
                else:
                    loose_failures.append(rec)
    return tight_failures, loose_failures, missing


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ybrowser", help="path to the ybrowser tool binary")
    parser.add_argument("fixtures", nargs="*", help="fixture html files (default: all)")
    args = parser.parse_args(argv)

    ybrowser = find_ybrowser(args.ybrowser)
    if args.fixtures:
        fixtures = [Path(f) for f in args.fixtures]
    else:
        fixtures = sorted(FIXTURES_DIR.glob("*.html"))

    total_tight = 0
    checked = 0
    for fixture in fixtures:
        ref_path = fixture.with_suffix(".ref.json")
        if not ref_path.exists():
            print(f"skip  {fixture.name}: no ref (run gen-chrome-refs.py)")
            continue
        ref = json.loads(ref_path.read_text())
        try:
            tight, loose, missing = compare_fixture(ybrowser, fixture, ref)
        except Exception as exc:  # noqa: BLE001
            print(f"ERROR {fixture.name}: {exc}")
            total_tight += 1
            continue

        checked += 1
        status = "PASS" if not tight else "FAIL"
        print(f"{status}  {fixture.name}: {len(ref)} element(s)")
        for name, prop, got, exp, delta in tight:
            print(f"        TIGHT {name}.{prop}: got {got:.2f} expected {exp:.2f} (Δ{delta:.2f})")
        for name, prop, got, exp, delta in loose:
            print(f"        loose {name}.{prop}: got {got:.2f} expected {exp:.2f} (Δ{delta:.2f})")
        for name in missing:
            print(f"        MISSING box for data-test={name}")
        total_tight += len(tight)

    print(f"\nchecked {checked} fixture(s); {total_tight} tight-tolerance failure(s)")
    return 1 if total_tight else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
