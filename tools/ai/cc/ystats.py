"""ystats — a floating ygui dialog showing cc_loop's token / cost stats.

The conversation keeps streaming to the terminal as plain text; this puts the
per-turn and session token accounting into a small ygui panel that floats in
the top-right corner over the scroll buffer and updates each turn.

It drives the real ygui toolkit through libyetty_ffi.so via the shared
`ygui_ffi` helpers (under demo/python/ygui). If the FFI library isn't built,
`StatsPanel.create()` returns None and cc_loop falls back to a printed line.
"""

from __future__ import annotations

import os
import sys

_REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
sys.path.insert(0, os.path.join(_REPO_ROOT, "demo", "python", "ygui"))


def _load():
    """Import the shared ygui FFI helpers, or None if unavailable."""
    try:
        import ygui_ffi
        return ygui_ffi
    except (ImportError, SystemExit, OSError):
        return None


PANEL_WIDTH = 360.0
PANEL_HEIGHT = 96.0
MARGIN = 16.0


class StatsPanel:
    """A floating ygui panel with a title + per-turn + session line."""

    def __init__(self, ffi, framework, lines: dict):
        self._ffi = ffi
        self._framework = framework
        self._lines = lines

    @classmethod
    def create(cls, out_fd: int) -> "StatsPanel | None":
        ffi = _load()
        if ffi is None:
            return None
        try:
            framework = ffi.framework_create(ffi.make_output_pty(out_fd))
            root = ffi.add("vbox", None)
            ffi.set_root(framework, root)

            panel = ffi.add("panel", root, width=PANEL_WIDTH, height=PANEL_HEIGHT)
            ffi.panel_set_bg(panel, 20, 26, 31, 245)  # BRAND_BG_LIFTED, near-opaque

            title = ffi.add("label", panel, height=22)
            ffi.label_text(title, "◆ tokens")
            turn = ffi.add("label", panel, height=20)
            ffi.label_text(turn, "waiting for first turn…")
            session = ffi.add("label", panel, height=20)
            ffi.label_text(session, "")

            width, height = ffi.terminal_geometry(out_fd)
            ffi.set_absolute(panel, width - PANEL_WIDTH - MARGIN, MARGIN,
                             PANEL_WIDTH, PANEL_HEIGHT)
            ffi.set_viewport(framework, width, height)
            _emit_serialized(ffi, framework)
            return cls(ffi, framework, {"turn": turn, "session": session})
        except Exception:
            return None

    def update(self, turn_line: str, session_line: str) -> None:
        try:
            self._ffi.label_text(self._lines["turn"], turn_line)
            self._ffi.label_text(self._lines["session"], session_line)
            _emit_serialized(self._ffi, self._framework)
        except Exception:
            pass

    def destroy(self, out_fd: int) -> None:
        try:
            sys.stdout.flush()
            self._ffi.clear_remote_fd(self._framework, out_fd)
            self._ffi.framework_destroy(self._framework)
        except Exception:
            pass


def _emit_serialized(ffi, framework) -> None:
    """Flush Python's buffered stdout BEFORE the framework's C-level write to the
    same fd, so the compositor DCS envelope can't be cut in half by pending text
    (which corrupts it and yetty silently drops the figure)."""
    sys.stdout.flush()
    ffi.emit(framework)
