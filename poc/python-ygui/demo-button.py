#!/usr/bin/env python3
"""Runnable Python ygui demo over the #380 multiplexed wire connection.

Run inside yetty:

    ./build-desktop-ytrace-release/yetty \
        -e 'python3 poc/python-ygui/demo-button.py'

  add --async to drive the same app from an asyncio loop instead of the
  synchronous select loop.

The app owns the single PTY stream through ``yetty_yclass_transport_pty``,
multiplexes the rpc / input / raw channels with ``yetty_ywire_connection``,
binds the ygui framework to the rpc channel, and drives the loop through the
connection's ``fd()`` / ``pump()`` reactor seam — no Python-side demux, raw
mode, or framing. Press q / Ctrl-C to quit.

Set YETTY_FFI_LIB to libyetty_ffi.so if it is not at the default build path.
"""

from __future__ import annotations

import sys

from yetty.ygui import App


def build(app: App) -> None:
    root = app.add("vbox")
    app.set_root(root)
    app.add("label", root, text="ygui from Python — over ywire_connection")
    app.add("button", root, label="Click me")


def main(argv: list[str]) -> int:
    app = App()
    build(app)
    if "--async" in argv:
        import asyncio

        return asyncio.run(app.run_async())
    return app.run()


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
