#!/usr/bin/env python3
"""Standalone ygui app written in Python.

Run inside a yetty pane:

    uv run demo/python/ygui/ygui_demo.py

Press q or Ctrl-C to quit.
"""

from __future__ import annotations

import os
import sys
from pathlib import Path
import termios
import tty

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "bindings" / "python"))
from yetty import ygui

OUT_FD = sys.stdout.fileno()
IN_FD = sys.stdin.fileno()


def build_app(app: ygui.App):
    root = app.add("vbox")
    ygui.must(app.set_root(root), "set_root")

    app.add("label", root, height=30, text="ygui from Python - live widgets over the PTY")
    button = app.add("button", root, height=34, label="Click me")
    checkbox = app.add("checkbox", root, height=26, label="Enable feature")
    toggle = app.add("toggle", root, height=26, label="Notifications")
    slider = app.add("slider", root, height=26)
    ygui.must(slider.slider_range(0.0, 1.0), "slider.range")
    ygui.must(slider.slider_value(0.4), "slider.value")
    status = app.add("label", root, height=24, text="click / drag widgets - press q to quit")

    ygui.must(app.resize_to_terminal(), "viewport")
    ygui.must(app.emit(), "emit")
    return {"button": button, "checkbox": checkbox, "toggle": toggle, "slider": slider, "status": status}


def run(app: ygui.App, widgets: dict):
    state = {"quit": False, "clicks": 0}

    def on_osc(_user, code, _args, _args_len, payload, payload_len):
        if code not in (ygui.OSC_MOUSE, ygui.OSC_FIGURE_MOUSE):
            return
        event = ygui.parse_mouse(payload, payload_len)
        if not event:
            return
        result = app.feed_mouse_event(*event)
        if result and result.value and event[0] == ygui.MOUSE_KIND_BUTTON and event[2]:
            state["clicks"] += 1
            widgets["status"].text(f"clicks: {state['clicks']} - press q to quit")
        ygui.must(app.emit_if_dirty(), "emit_if_dirty")

    def on_raw(_user, data, n):
        chunk = ygui.C.string_at(data, n)
        if b"q" in chunk or b"\x03" in chunk:
            state["quit"] = True
            return
        ygui.must(app.feed_input(data, n), "feed_input")
        ygui.must(app.emit_if_dirty(), "emit_if_dirty")

    demux = ygui.Demux(on_osc, on_raw)

    if not sys.stdin.isatty():
        sys.stderr.write("ygui_demo: stdin not a tty - rendered once, exiting.\n")
        return

    saved = termios.tcgetattr(IN_FD)
    ygui.must(ygui.subscribe_mouse(OUT_FD), "subscribe_mouse")
    try:
        tty.setraw(IN_FD)
        while not state["quit"]:
            data = os.read(IN_FD, 4096)
            if not data:
                break
            ygui.must(demux.feed(data), "yface_feed")
    finally:
        termios.tcsetattr(IN_FD, termios.TCSANOW, saved)
        ygui.unsubscribe_mouse(OUT_FD)
        app.clear()


def main() -> int:
    with ygui.App(OUT_FD) as app:
        run(app, build_app(app))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
