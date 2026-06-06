#!/usr/bin/env python3
"""yview demo — draw scrollable text into a positioned figure, from Python.

Run this INSIDE a yetty terminal: stdout is the PTY, so the DCS envelopes the
bindings emit reach yetty and render a bounded, server-side-scrolling surface
near the top-left. It scrolls itself for a few seconds, then clears.

    # build the FFI shared library once:
    make build-desktop-ffi-release

    # then, inside a yetty terminal:
    python demo/python/yview/hello.py

YETTY_FFI_LIB overrides the shared-library path; otherwise the default build
location is used. Set YVIEW_DEMO_DRYRUN=1 to run outside yetty (envelopes go to
stdout as raw bytes — useful only for smoke-testing the binding).
"""

import os
import pathlib
import sys
import time

REPO = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO / "bindings" / "python"))

import yetty  # noqa: E402
from yetty.generated.yview import View  # noqa: E402

DEFAULT_LIB = REPO / "build-desktop-ffi-release" / "src" / "yetty" / "yffi" / "libyetty_ffi.so"

# Brand near-black with full opacity (0xAARRGGBB).
BG_OPAQUE = 0xFF0B1014


def main() -> int:
    lib = os.environ.get("YETTY_FFI_LIB") or str(DEFAULT_LIB)
    if not os.path.exists(lib):
        sys.stderr.write(f"yview demo: shared lib not found: {lib}\n"
                         "build it with: make build-desktop-ffi-release\n")
        return 1
    yetty.load(lib)

    # Viewport in target pixels: a box near the top-left. Tune to your pane.
    min_x, min_y, max_x, max_y = 40.0, 40.0, 680.0, 520.0

    view = View()                                   # yetty_yview_view_create
    view.configure(1, os.getpid(), 0, BG_OPAQUE,    # fd=1 (stdout → yetty)
                   min_x, min_y, max_x, max_y)
    # Render a yplot expression as the figure (0 ranges → yplot defaults).
    view.set_plot("f=sin(x); g=cos(x)", -3.14159, 3.14159, -1.5, 1.5)

    time.sleep(3.0)                                 # leave it on screen
    view.destroy()                                  # clears the surface
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
