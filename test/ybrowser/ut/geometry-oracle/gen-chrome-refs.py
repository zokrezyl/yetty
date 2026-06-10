#!/usr/bin/env python3
"""Generate Chrome geometry reference JSON for ybrowser layout fixtures.

This is the oracle side of the ybrowser geometry test layer. Each fixture in
``test/ut/ybrowser/fixtures/*.html`` marks key elements with
``data-test="NAME"`` and carries an inline script that, after load, walks
``[data-test]``, reads ``getBoundingClientRect()``, and writes the geometry as
JSON into ``<pre id="geom">``.

This tool runs each fixture through headless Chrome with ``--dump-dom`` (which
executes the page script and serializes the post-layout DOM), extracts the
``#geom`` JSON, and writes ``<fixture>.ref.json`` next to the fixture. Refs are
regenerated only on intentional layout changes — review the diff before
committing.

Usage:
    test/ut/ybrowser/geometry-oracle/gen-chrome-refs.py [fixture.html ...]

With no arguments it refreshes every fixture under the fixtures directory. The
viewport is fixed at 1000x600 to match the comparator and the C layout tests.
"""

import json
import re
import subprocess
import sys
from pathlib import Path

VIEWPORT_W = 1000
VIEWPORT_H = 600

# This script lives in test/ut/ybrowser/geometry-oracle/; the fixtures are its
# sibling directory.
FIXTURES_DIR = Path(__file__).resolve().parent.parent / "fixtures"

CHROME_CANDIDATES = [
    "google-chrome",
    "google-chrome-stable",
    "chromium",
    "chromium-browser",
]

GEOM_RE = re.compile(r'<pre id="geom"[^>]*>(.*?)</pre>', re.DOTALL)


def find_chrome():
    from shutil import which

    for name in CHROME_CANDIDATES:
        path = which(name)
        if path:
            return path
    sys.exit(
        "error: no Chrome/Chromium binary found (looked for: "
        + ", ".join(CHROME_CANDIDATES)
        + ")"
    )


def dump_geometry(chrome, fixture_path):
    """Run the fixture through headless Chrome and return the parsed geometry."""
    url = fixture_path.resolve().as_uri()
    cmd = [
        chrome,
        "--headless",
        "--disable-gpu",
        "--no-sandbox",
        f"--window-size={VIEWPORT_W},{VIEWPORT_H}",
        "--dump-dom",
        url,
    ]
    proc = subprocess.run(
        cmd, capture_output=True, text=True, errors="replace", timeout=60
    )
    if proc.returncode != 0:
        raise RuntimeError(
            f"chrome exited {proc.returncode} for {fixture_path.name}:\n{proc.stderr}"
        )
    # There may be several `<pre id="geom">` literals in the dump (e.g. one
    # inside an HTML comment that documents the convention). Take the first
    # match whose body actually parses as JSON.
    for match in GEOM_RE.finditer(proc.stdout):
        body = match.group(1).strip()
        # textContent serialization may HTML-escape & < > — undo the few that matter.
        body = body.replace("&amp;", "&").replace("&lt;", "<").replace("&gt;", ">")
        try:
            return json.loads(body)
        except json.JSONDecodeError:
            continue
    raise RuntimeError(
        f"no parseable <pre id=\"geom\"> JSON in dumped DOM for {fixture_path.name} "
        "(did the fixture's geometry script run?)"
    )


def main(argv):
    chrome = find_chrome()
    if argv:
        fixtures = [Path(a) for a in argv]
    else:
        fixtures = sorted(FIXTURES_DIR.glob("*.html"))
    if not fixtures:
        sys.exit(f"error: no fixtures found in {FIXTURES_DIR}")

    failures = 0
    for fixture in fixtures:
        try:
            geom = dump_geometry(chrome, fixture)
        except Exception as exc:  # noqa: BLE001 — report-and-continue is the point
            print(f"FAIL  {fixture.name}: {exc}")
            failures += 1
            continue
        ref_path = fixture.with_suffix(".ref.json")
        ref_path.write_text(json.dumps(geom, indent=2, sort_keys=True) + "\n")
        print(f"ok    {fixture.name} -> {ref_path.name} ({len(geom)} elements)")

    if failures:
        sys.exit(f"\n{failures} fixture(s) failed to generate")
    print(f"\nrefreshed {len(fixtures)} ref(s) at {VIEWPORT_W}x{VIEWPORT_H}")


if __name__ == "__main__":
    main(sys.argv[1:])
