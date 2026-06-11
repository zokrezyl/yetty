#!/usr/bin/env python3
"""
ygui_term — a terminal multiplexer with a live ygui overlay, in Python.

This is the multiplexer the architecture has been building toward: it forks a
real shell under a PTY and sits in the middle. The user's keystrokes go to the
shell; the shell's output is passed through to the screen; and the mouse —
forwarded by yetty as OSC envelopes — drives a ygui toolbar floating over the
terminal.

    user keys ─┐                                  ┌─▶ shell stdin (PTY master)
               │  yetty_yface demux on our stdin: │
   stdin ──────┤    raw bytes ────────────────────┘
               │    OSC mouse envelopes ─▶ ygui framework ─▶ widgets (our stdout)
               └
   shell stdout (PTY master) ───────────────────────▶ our stdout (passes through)

So: ANY text goes to the terminal; OSC/DCS input drives the GUI. The ygui
toolbar is a positioned compositor figure — it floats over the shell output and
does not scroll away.

Run INSIDE a yetty pane:

    uv run demo/python/ygui/ygui_term.py            # runs $SHELL
    uv run demo/python/ygui/ygui_term.py htop       # runs a specific command

Type normally — it goes to the shell. Click the toolbar buttons — the click
counter updates. Press Ctrl-] to quit the multiplexer (the shell keeps Ctrl-C
etc. as usual). Needs libyetty_ffi.so (`make build-desktop-ffi-release`).
"""

from __future__ import annotations

import fcntl
import os
import select
import signal
import sys
import termios
import tty

import ygui_ffi as g

OUT_FD = sys.stdout.fileno()
IN_FD = sys.stdin.fileno()

QUIT_KEY = 0x1D  # Ctrl-] — detach / quit the multiplexer.


def copy_winsize(dst_fd: int) -> None:
    """Mirror our terminal's window size onto the child PTY so the shell lays
    out correctly."""
    try:
        packed = fcntl.ioctl(OUT_FD, termios.TIOCGWINSZ, b"\0" * 8)
        fcntl.ioctl(dst_fd, termios.TIOCSWINSZ, packed)
    except OSError:
        pass


def build_overlay():
    framework = g.framework_create(g.make_output_pty(OUT_FD))
    root = g.add("vbox", None)
    g.set_root(framework, root)

    bar = g.add("hbox", root, height=30)
    status = g.add("label", bar, width=420, height=26)
    g.label_text(status, "ygui overlay · type into the shell · click me · Ctrl-] quits")
    button_a = g.add("button", bar, width=64, height=26)
    g.set_label("button", button_a, "A")
    button_b = g.add("button", bar, width=64, height=26)
    g.set_label("button", button_b, "B")

    width, height = g.terminal_geometry(OUT_FD)
    g.set_viewport(framework, width, height)
    g.emit(framework)
    return framework, status


def main() -> int:
    command = sys.argv[1:] or [os.environ.get("SHELL", "/bin/bash")]

    pid, master_fd = os.forkpty()
    if pid == 0:
        # Child: become the shell. Inherits our env (TERM, TERM_PROGRAM=yetty).
        try:
            os.execvp(command[0], command)
        except OSError as exc:
            sys.stderr.write(f"ygui_term: cannot exec {command[0]}: {exc}\n")
        os._exit(127)

    copy_winsize(master_fd)
    framework, status = build_overlay()
    state = {"quit": False, "clicks": 0, "resized": False}

    def write_to_shell(chunk: bytes):
        if bytes([QUIT_KEY]) in chunk:
            state["quit"] = True
            return
        if chunk:
            os.write(master_fd, chunk)

    def on_osc(user, code, args, args_len, payload, payload_len):
        if code in (g.OSC_MOUSE, g.OSC_FIGURE_MOUSE):
            parsed = g.parse_mouse(payload, payload_len)
            if not parsed:
                return
            kind, button, pressed, x, y, wheel = parsed
            if kind == g.MOUSE_KIND_BUTTON:
                consumed = g.feed_mouse_button(framework, x, y, button, pressed)
                if consumed and pressed:
                    state["clicks"] += 1
                    g.label_text(
                        status, f"ygui overlay · clicks: {state['clicks']} · Ctrl-] quits")
            elif kind == g.MOUSE_KIND_POS:
                g.feed_mouse_motion(framework, x, y)
            elif kind == g.MOUSE_KIND_WHEEL:
                g.feed_mouse_scroll(framework, x, y, 0.0, wheel)
            g.emit_if_dirty(framework)
        elif code in (g.OSC_KEY, g.OSC_FIGURE_KEY):
            # An overlay figure has keyboard focus (yetty's click-focus model),
            # so keys come here instead of as shell input — decode and forward,
            # keeping the shell typeable after a click.
            parsed = g.parse_key(payload, payload_len)
            if parsed:
                write_to_shell(g.key_event_to_bytes(*parsed))

    def on_raw(user, data, n):
        # Everything that is not a forwarded OSC envelope is real keyboard
        # input (delivered while no overlay figure has focus) — to the shell.
        write_to_shell(g.C.string_at(data, n))

    osc_cb = g.MSG_CB(on_osc)
    raw_cb = g.RAW_CB(on_raw)
    yface = g.yface_create()
    g.yface_set_handlers(yface, osc_cb, raw_cb)

    def on_sigwinch(signum, frame):
        state["resized"] = True

    signal.signal(signal.SIGWINCH, on_sigwinch)

    saved = termios.tcgetattr(IN_FD) if sys.stdin.isatty() else None
    if saved is not None:
        tty.setraw(IN_FD)
    g.subscribe_mouse(OUT_FD)
    try:
        while not state["quit"]:
            if state["resized"]:
                state["resized"] = False
                copy_winsize(master_fd)
                width, height = g.terminal_geometry(OUT_FD)
                g.set_viewport(framework, width, height)
                g.emit(framework)
            try:
                readable, _, _ = select.select([IN_FD, master_fd], [], [])
            except InterruptedError:
                continue  # SIGWINCH — loop to handle it
            if IN_FD in readable:
                data = os.read(IN_FD, 4096)
                if data:
                    g.yface_feed(yface, data)  # splits OSC (mouse) vs raw (keys)
            if master_fd in readable:
                try:
                    out = os.read(master_fd, 65536)
                except OSError:
                    out = b""
                if not out:
                    break  # shell exited / EOF
                os.write(OUT_FD, out)  # pass shell output through to the screen
    finally:
        if saved is not None:
            termios.tcsetattr(IN_FD, termios.TCSANOW, saved)
        g.unsubscribe_mouse(OUT_FD)
        g.clear_remote_fd(framework, OUT_FD)
        g.framework_destroy(framework)
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
