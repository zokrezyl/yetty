#!/usr/bin/env python3
"""Pygui Demo 01: Hello Button — Python port of demo/ygui/06_hello_button.

Mirrors the C demo's API call sequence using the auto-generated ctypes
bindings at bindings/python/ygui.py. Run from the repo root inside a real
terminal (the engine takes over the current TTY for input/output):

    YGUI_LIB=build-desktop-ytrace-release/src/yetty/ygui/libygui.so \\
        python3 demo/pygui/01_hello_button/main.py

Press 'q' to quit. The demo also exits when the engine's run loop returns
on stdin EOF.
"""

from __future__ import annotations

import ctypes
import os
import pathlib
import sys


# Make the generated bindings importable without packaging.
REPO = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO / "bindings/python"))

import ygui  # noqa: E402


# Default library path inside the build tree, overridable via env var.
LIB_PATH = os.environ.get(
    "YGUI_LIB",
    str(REPO / "build-desktop-ytrace-release/src/yetty/ygui/libygui.so"),
)
ygui.load(LIB_PATH)


# ---------------------------------------------------------------------------
# State
# ---------------------------------------------------------------------------

class State:
    engine = None
    click_button = None
    status = None
    slider = None
    progress = None
    checkbox = None
    clicks = 0


# ctypes function pointers must outlive the C library's reference. Stash
# them on the module so the GC doesn't collect mid-callback.
_CALLBACKS: list = []


def _keep(cb):
    _CALLBACKS.append(cb)
    return cb


# ---------------------------------------------------------------------------
# Callbacks
# ---------------------------------------------------------------------------

WIDGET_CB = ctypes.CFUNCTYPE(None, ctypes.POINTER(ygui.yetty_ygui_old_widget), ctypes.c_void_p)
SLIDER_CB = ctypes.CFUNCTYPE(None, ctypes.POINTER(ygui.yetty_ygui_old_widget), ctypes.c_float, ctypes.c_void_p)
CHECKBOX_CB = ctypes.CFUNCTYPE(None, ctypes.POINTER(ygui.yetty_ygui_old_widget), ctypes.c_int, ctypes.c_void_p)
KEY_CB = ctypes.CFUNCTYPE(None, ctypes.POINTER(ygui.yetty_ygui_old_engine), ctypes.c_uint32, ctypes.c_int, ctypes.c_void_p)


@_keep
@WIDGET_CB
def on_click(_widget, _user):
    State.clicks += 1
    ygui.yetty_ygui_old_widget_button_set_label(
        State.click_button, f"Clicks: {State.clicks}".encode())
    ygui.yetty_ygui_old_widget_label_set_text(
        State.status, f"Clicked! Total: {State.clicks}".encode())


@_keep
@WIDGET_CB
def on_reset(_widget, _user):
    State.clicks = 0
    ygui.yetty_ygui_old_widget_button_set_label(State.click_button, b"Clicks: 0")
    ygui.yetty_ygui_old_widget_slider_set_value(State.slider, 50)
    ygui.yetty_ygui_old_widget_checkbox_set_checked(State.checkbox, 0)
    ygui.yetty_ygui_old_widget_label_set_text(State.status, b"Reset!")


@_keep
@WIDGET_CB
def on_quit(_widget, _user):
    ygui.yetty_ygui_old_engine_stop(State.engine)


@_keep
@SLIDER_CB
def on_slider_change(_widget, value, _user):
    ygui.yetty_ygui_old_widget_label_set_text(
        State.status, f"Volume: {int(value)}%".encode())
    ygui.yetty_ygui_old_widget_progress_set_value(State.progress, value / 100.0)


@_keep
@CHECKBOX_CB
def on_checkbox_change(_widget, checked, _user):
    text = b"Feature enabled" if checked else b"Feature disabled"
    ygui.yetty_ygui_old_widget_label_set_text(State.status, text)


@_keep
@KEY_CB
def on_key(engine, key, _mods, _user):
    if key in (ord('q'), ord('Q')):
        ygui.yetty_ygui_old_engine_stop(engine)


# ---------------------------------------------------------------------------
# Setup
# ---------------------------------------------------------------------------

def main() -> int:
    if ygui.yetty_ygui_old_init() != 0:
        print("ygui_init failed — run inside a real terminal", file=sys.stderr)
        return 1

    eng_r = ygui.yetty_ygui_old_engine_create_with_pixel_hint(b"dashboard", 2, 2, 500.0, 350.0)
    if not eng_r.ok:
        ygui.yetty_ygui_old_shutdown()
        return 1
    State.engine = eng_r.value

    e = State.engine
    ygui.yetty_ygui_old_engine_label(e, b"title", 20, 15, b"YGui Dashboard (pygui)")

    State.click_button = ygui.yetty_ygui_old_engine_button(
        e, b"btn_click", 20, 50, 150, 45, b"Clicks: 0")
    ygui.yetty_ygui_old_widget_button_on_click(State.click_button, on_click, None)

    btn_reset = ygui.yetty_ygui_old_engine_button(
        e, b"btn_reset", 190, 50, 100, 45, b"Reset")
    ygui.yetty_ygui_old_widget_button_on_click(btn_reset, on_reset, None)

    btn_quit = ygui.yetty_ygui_old_engine_button(
        e, b"btn_quit", 310, 50, 100, 45, b"Quit")
    ygui.yetty_ygui_old_widget_button_on_click(btn_quit, on_quit, None)

    ygui.yetty_ygui_old_engine_label(e, b"slider_lbl", 20, 115, b"Volume: 50%")
    State.slider = ygui.yetty_ygui_old_engine_slider(
        e, b"slider", 20, 145, 300, 30, 0, 100, 50)
    ygui.yetty_ygui_old_widget_slider_on_change(State.slider, on_slider_change, None)

    ygui.yetty_ygui_old_engine_label(e, b"prog_lbl", 20, 195, b"Progress:")
    State.progress = ygui.yetty_ygui_old_engine_progress(
        e, b"progress", 20, 220, 300, 25, 0.5)

    State.checkbox = ygui.yetty_ygui_old_engine_checkbox(
        e, b"checkbox", 20, 265, 200, 30, b"Enable feature", 0)
    ygui.yetty_ygui_old_widget_checkbox_on_change(
        State.checkbox, on_checkbox_change, None)

    State.status = ygui.yetty_ygui_old_engine_label(
        e, b"status", 20, 315, b"Ready - Click widgets or 'q' to quit")

    ygui.yetty_ygui_old_engine_on_key(e, on_key, None)
    ygui.yetty_ygui_old_engine_show(e)
    ygui.yetty_ygui_old_engine_run(e)

    ygui.yetty_ygui_old_engine_destroy(e)
    ygui.yetty_ygui_old_shutdown()
    return 0


if __name__ == "__main__":
    sys.exit(main())
