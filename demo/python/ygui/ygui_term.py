#!/usr/bin/env python3
"""Terminal multiplexer with a live ygui overlay, in Python.

Run inside a yetty pane:

    uv run demo/python/ygui/ygui_term.py
    uv run demo/python/ygui/ygui_term.py htop

Type normally. Click the overlay. Press Ctrl-] to quit the multiplexer.
"""

from __future__ import annotations

import fcntl
import os
import select
import signal
import sys
from pathlib import Path
import termios
import tty

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "bindings" / "python"))
from yetty import ygui

OUT_FD = sys.stdout.fileno()
IN_FD = sys.stdin.fileno()
QUIT_KEY = 0x1D  # Ctrl-]


def copy_winsize(dst_fd: int) -> None:
    try:
        packed = fcntl.ioctl(OUT_FD, termios.TIOCGWINSZ, b"\0" * 8)
        fcntl.ioctl(dst_fd, termios.TIOCSWINSZ, packed)
    except OSError:
        pass


def build_overlay(app: ygui.App):
    root = app.add("vbox")
    ygui.must(app.set_root(root), "set_root")

    bar = app.add("hbox", root, height=30)
    status = app.add("label", bar, width=420, height=26,
                     text="ygui overlay - type into shell - click me - Ctrl-] quits")
    app.add("button", bar, width=64, height=26, label="A")
    app.add("button", bar, width=64, height=26, label="B")

    ygui.must(app.resize_to_terminal(), "viewport")
    ygui.must(app.emit(), "emit")
    return status


def main() -> int:
    command = sys.argv[1:] or [os.environ.get("SHELL", "/bin/bash")]

    pid, master_fd = os.forkpty()
    if pid == 0:
        try:
            os.execvp(command[0], command)
        except OSError as exc:
            sys.stderr.write(f"ygui_term: cannot exec {command[0]}: {exc}\n")
        os._exit(127)

    copy_winsize(master_fd)
    app = ygui.App(OUT_FD)
    status = build_overlay(app)
    state = {"quit": False, "clicks": 0, "resized": False}

    def write_to_shell(chunk: bytes):
        if bytes([QUIT_KEY]) in chunk:
            state["quit"] = True
            return
        if chunk:
            os.write(master_fd, chunk)

    def on_osc(_user, code, _args, _args_len, payload, payload_len):
        if code in (ygui.OSC_MOUSE, ygui.OSC_FIGURE_MOUSE):
            event = ygui.parse_mouse(payload, payload_len)
            if not event:
                return
            result = app.feed_mouse_event(*event)
            if result and result.value and event[0] == ygui.MOUSE_KIND_BUTTON and event[2]:
                state["clicks"] += 1
                status.text(f"ygui overlay - clicks: {state['clicks']} - Ctrl-] quits")
            ygui.must(app.emit_if_dirty(), "emit_if_dirty")
        elif code in (ygui.OSC_KEY, ygui.OSC_FIGURE_KEY):
            parsed = ygui.parse_key(payload, payload_len)
            if parsed:
                write_to_shell(ygui.key_event_to_bytes(*parsed))

    def on_raw(_user, data, n):
        write_to_shell(ygui.C.string_at(data, n))

    demux = ygui.Demux(on_osc, on_raw)

    def on_sigwinch(_signum, _frame):
        state["resized"] = True

    signal.signal(signal.SIGWINCH, on_sigwinch)

    saved = termios.tcgetattr(IN_FD) if sys.stdin.isatty() else None
    if saved is not None:
        tty.setraw(IN_FD)
    ygui.must(ygui.subscribe_mouse(OUT_FD), "subscribe_mouse")
    try:
        while not state["quit"]:
            if state["resized"]:
                state["resized"] = False
                copy_winsize(master_fd)
                ygui.must(app.resize_to_terminal(), "viewport")
                ygui.must(app.emit(), "emit")
            try:
                readable, _, _ = select.select([IN_FD, master_fd], [], [])
            except InterruptedError:
                continue
            if IN_FD in readable:
                data = os.read(IN_FD, 4096)
                if data:
                    ygui.must(demux.feed(data), "yface_feed")
            if master_fd in readable:
                try:
                    out = os.read(master_fd, 65536)
                except OSError:
                    out = b""
                if not out:
                    break
                os.write(OUT_FD, out)
    finally:
        if saved is not None:
            termios.tcsetattr(IN_FD, termios.TCSANOW, saved)
        ygui.unsubscribe_mouse(OUT_FD)
        app.close()
        try:
            os.close(master_fd)
        except OSError:
            pass
        try:
            os.waitpid(pid, 0)
        except OSError:
            pass
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
