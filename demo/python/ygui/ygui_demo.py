#!/usr/bin/env python3
"""
ygui_demo — a native ygui application written in Python, running inside yetty.

Builds a real ygui widget tree (label / button / checkbox / toggle / slider)
and ships it to the running yetty over the PTY as compositor envelopes — the
exact path the C tools yless / yflame / ygreeter use. No new C in the app.

Input demux (the "client-side multiplexer"): we subscribe to pane-wide mouse
events; yetty forwards them as OSC envelopes on our stdin. yetty_yface splits
the stream — OSC envelopes drive the widgets (feed_mouse_*), raw keystrokes go
to the framework (feed_input).

Run INSIDE a yetty pane:

    uv run demo/python/ygui/ygui_demo.py

Quit with 'q' or Ctrl-C. Needs libyetty_ffi.so
(`make build-desktop-ffi-release`), or set YETTY_FFI_LIB.
"""

from __future__ import annotations

import os
import sys
import termios

import ygui_ffi as g

OUT_FD = sys.stdout.fileno()


def build_app():
    framework = g.framework_create(g.make_output_pty(OUT_FD))

    root = g.add("vbox", None)
    g.set_root(framework, root)

    title = g.add("label", root, height=30)
    g.label_text(title, "🐍  ygui from Python — live widgets over the PTY")

    button = g.add("button", root, height=34)
    g.set_label("button", button, "Click me")

    checkbox = g.add("checkbox", root, height=26)
    g.set_label("checkbox", checkbox, "Enable feature")

    toggle = g.add("toggle", root, height=26)
    g.set_label("toggle", toggle, "Notifications")

    slider = g.add("slider", root, height=26)
    g.call("yetty_ygui_slider_set_range", g.VOID, (g.C.c_void_p, g.C.c_float, g.C.c_float),
           slider, g.C.c_float(0.0), g.C.c_float(1.0))
    g.call("yetty_ygui_slider_set_value", g.VOID, (g.C.c_void_p, g.C.c_float),
           slider, g.C.c_float(0.4))

    hint = g.add("label", root, height=24)
    g.label_text(hint, "click / drag the widgets · press q to quit")

    width, height = g.terminal_geometry(OUT_FD)
    g.set_viewport(framework, width, height)
    g.emit(framework)
    return framework


def run(framework):
    state = {"quit": False}

    def on_osc(user, code, args, args_len, payload, payload_len):
        if code not in (g.OSC_MOUSE, g.OSC_FIGURE_MOUSE):
            return
        parsed = g.parse_mouse(payload, payload_len)
        if not parsed:
            return
        kind, button, pressed, x, y, wheel = parsed
        if kind == g.MOUSE_KIND_BUTTON:
            g.feed_mouse_button(framework, x, y, button, pressed)
        elif kind == g.MOUSE_KIND_POS:
            g.feed_mouse_motion(framework, x, y)
        elif kind == g.MOUSE_KIND_WHEEL:
            g.feed_mouse_scroll(framework, x, y, 0.0, wheel)
        g.emit_if_dirty(framework)

    def on_raw(user, data, n):
        chunk = g.C.string_at(data, n)
        if b"q" in chunk or b"\x03" in chunk:  # q or Ctrl-C
            state["quit"] = True
            return
        g.feed_input(framework, data, n)
        g.emit_if_dirty(framework)

    osc_cb = g.MSG_CB(on_osc)
    raw_cb = g.RAW_CB(on_raw)
    yface = g.yface_create()
    g.yface_set_handlers(yface, osc_cb, raw_cb)

    if not sys.stdin.isatty():
        sys.stderr.write("ygui_demo: stdin not a tty — rendered once, exiting.\n")
        return

    in_fd = sys.stdin.fileno()
    saved = termios.tcgetattr(in_fd)
    g.subscribe_mouse(OUT_FD)
    try:
        raw = termios.tcgetattr(in_fd)
        raw[3] &= ~(termios.ICANON | termios.ECHO | termios.ISIG)
        termios.tcsetattr(in_fd, termios.TCSANOW, raw)
        while not state["quit"]:
            data = os.read(in_fd, 4096)
            if not data:
                break
            g.yface_feed(yface, data)
    finally:
        termios.tcsetattr(in_fd, termios.TCSANOW, saved)
        g.unsubscribe_mouse(OUT_FD)
        g.clear_remote_fd(framework, OUT_FD)


def main() -> int:
    framework = build_app()
    try:
        run(framework)
    finally:
        g.framework_destroy(framework)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
