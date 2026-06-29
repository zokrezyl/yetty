#!/usr/bin/env python3
"""Simplest runnable ygui demo in Python.

Run it *inside* yetty — it draws a tiny widget tree (a heading, a couple of
rows, and a button) as a live ygui figure and stays up until you press q /
Ctrl-C:

    uv run poc/python-ygui/demo.py

The earlier "generated-bindings-only" version drove the legacy direct-fd
``framework_attach`` (a bare DCS transport on the raw PTY). The host now speaks
the #380 multiplexed wire (``yetty_ywire_connection`` over
``yetty_yclass_transport_pty``), so that path no longer attaches. All of the
delicate byte handling — raw mode, channel demux, framing, the get_root
handshake, the render loop — lives in the C connection and is wrapped by
``yetty.ygui.App``; this file just builds widgets and calls ``run()``.

Add ``--async`` to drive the same app from an asyncio loop instead of the
synchronous select loop.

Set YETTY_FFI_LIB to libyetty_ffi.so if it is not at the default build path.
"""

from __future__ import annotations

import sys
from pathlib import Path

# Let `uv run poc/python-ygui/demo.py` work without the caller having to export
# PYTHONPATH: the bindings package lives two levels up from this POC directory.
REPO_ROOT = Path(__file__).resolve().parents[2]
BINDINGS = REPO_ROOT / "bindings" / "python"
if str(BINDINGS) not in sys.path:
    sys.path.insert(0, str(BINDINGS))

from yetty.ygui import App


def build(app: App) -> None:
    # The vbox stacks its children top-to-bottom by their heights, so every
    # child needs an explicit size — a zero-height widget collapses onto its
    # sibling and they overlap on one row.
    root = app.add("vbox")
    app.set_root(root)
    app.add("label", root, text="ygui from Python", width=420, height=32)
    app.add("label", root, text="a label, a value, and a button", width=420, height=28)
    app.add("label", root, text="press q or Ctrl-C to quit", width=420, height=28)
    app.add("button", root, label="Click me", width=200, height=40)


def main(argv: list[str]) -> int:
    app = App()
    build(app)
    if "--async" in argv:
        import asyncio

        return asyncio.run(app.run_async())
    return app.run()


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
