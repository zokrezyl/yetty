"""ywin — small floating windows over the yetty scroll buffer, via the yview FFI.

cc_loop draws inline figures by piping through `ycat` (an OSC envelope on the
scrolling ydraw layer). This module is the other half: a *positioned compositor
figure* — a small window that floats at fixed pixel coordinates on top of the
scroll buffer and does NOT scroll away. It is driven by the `yview` yclass
class exposed through `libyetty_ffi.so` and its Python bindings under
`bindings/python/`.

The whole path is: create a `yview.View`, `configure(fd, …)` it with the PTY
file descriptor and a pixel rect, then `set_text` / `set_plot` to ship content.
The emitter writes a compositor DCS envelope straight to the fd; the yetty
server creates a child figure at that rect and owns its scroll/clip/position
state thereafter. (Inside tmux the envelope is auto-wrapped in tmux passthrough;
directly under yetty it is the bare DCS — both decode.)

The library is loaded lazily and *optionally*: if it can't be found or loaded,
`Window.create()` returns None and the caller falls back to its normal output.
Point `YETTY_FFI_LIB` at the .so to override discovery.
"""

from __future__ import annotations

import fcntl
import os
import struct
import sys
import termios

# ---------------------------------------------------------------------------
# Library discovery + lazy import of the generated bindings
# ---------------------------------------------------------------------------

# Default build location of the FFI shared library, relative to the repo root
# (the dedicated PIC "double build" tree). YETTY_FFI_LIB overrides this.
_DEFAULT_LIB_RELPATH = "build-desktop-ffi-release/src/yetty/yffi/libyetty_ffi.so"


def _repo_root() -> str:
    """Repo root = three levels up from tools/ai/cc/ywin.py."""
    return os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))


def _discover_lib() -> str | None:
    """Resolve the FFI .so path: YETTY_FFI_LIB, else the default build tree."""
    env = os.environ.get("YETTY_FFI_LIB")
    if env and os.path.isfile(env):
        return env
    candidate = os.path.join(_repo_root(), _DEFAULT_LIB_RELPATH)
    return candidate if os.path.isfile(candidate) else None


_View = None  # cached bindings class once loaded


def _load_view_class():
    """Import yetty.generated.yview.View against the FFI .so. Returns the class,
    or None if the library or bindings are unavailable. Cached after first call."""
    global _View
    if _View is not None:
        return _View
    lib = _discover_lib()
    if not lib:
        return None
    bindings_dir = os.path.join(_repo_root(), "bindings", "python")
    if bindings_dir not in sys.path:
        sys.path.insert(0, bindings_dir)
    try:
        # The documented bindings entry point (yetty/__init__.py): load the
        # shared library once, then use the generated per-module classes.
        import yetty
        yetty.load(lib)
        from yetty.generated.yview import View
    except Exception:
        return None
    _View = View
    return _View


# ---------------------------------------------------------------------------
# Terminal pixel geometry
# ---------------------------------------------------------------------------


def terminal_pixels(fd: int) -> tuple[int, int]:
    """(width_px, height_px) of the terminal. Uses TIOCGWINSZ pixel fields when
    the terminal reports them; otherwise estimates from the cell grid with a
    9x19 px cell, so a rect can always be computed."""
    try:
        packed = fcntl.ioctl(fd, termios.TIOCGWINSZ, b"\0" * 8)
        rows, cols, xpix, ypix = struct.unpack("HHHH", packed)
    except OSError:
        rows = cols = xpix = ypix = 0
    if xpix and ypix:
        return xpix, ypix
    cols = cols or 80
    rows = rows or 24
    return cols * 9, rows * 19


# ---------------------------------------------------------------------------
# Window
# ---------------------------------------------------------------------------

def pack_abgr(rrggbb: int, opacity: float = 1.0) -> int:
    """Pack 0xRRGGBB + opacity into the 0xAABBGGRR word the yview/SDF background
    box expects (same packing the shipping yless/yflame tools use)."""
    r = (rrggbb >> 16) & 0xFF
    g = (rrggbb >> 8) & 0xFF
    b = rrggbb & 0xFF
    a = int(opacity * 255.0 + 0.5) & 0xFF
    return (a << 24) | (b << 16) | (g << 8) | r


# Brand "lifted" popup body (#141A1F), opaque — in the ABGR word yview wants.
BG_LIFTED = pack_abgr(0x141A1F, 1.0)


class Window:
    """A single floating compositor window. Reused across updates: `set_text`
    re-ships content into the same child figure, `move`/`resize` reposition it.

    Construct via `Window.create(...)`, which returns None when the FFI library
    isn't available — callers treat that as "windows unsupported" and fall back.
    """

    def __init__(self, view, fd: int, child_id: int, rect: tuple[float, float, float, float]):
        self._view = view
        self._fd = fd
        self.child_id = child_id
        self.min_x, self.min_y, self.max_x, self.max_y = rect

    @classmethod
    def create(
        cls,
        fd: int | None = None,
        child_id: int | None = None,
        rect: tuple[float, float, float, float] | None = None,
        bg_color: int = BG_LIFTED,
        kind: int = 0,
    ) -> "Window | None":
        """Create and configure a window. `fd` defaults to stdout (the PTY);
        `rect` defaults to a panel in the top-right quarter of the terminal.
        Returns None if the FFI library can't be loaded."""
        view_class = _load_view_class()
        if view_class is None:
            return None
        if fd is None:
            fd = sys.stdout.fileno()
        if child_id is None:
            # Distinct from the running pid so it won't collide with a tool that
            # keys its figure on getpid() (e.g. yless/yflame).
            child_id = (os.getpid() ^ 0x5C100) & 0x7FFFFFFF
        if rect is None:
            rect = cls.default_rect(fd)
        try:
            view = view_class()
            view.configure(fd, child_id, kind, bg_color, *rect)
        except Exception:
            return None
        return cls(view, fd, child_id, rect)

    @staticmethod
    def default_rect(fd: int) -> tuple[float, float, float, float]:
        """A panel anchored at the pane's top-left, ~55% wide / ~45% tall.

        Anchored top-left on purpose: the compositor rect is in target pixels,
        which may differ from the tty's reported pixel size (HiDPI / scaling),
        so a right/bottom anchor can land off-screen. Top-left is always
        visible; the server clips content to the rect regardless."""
        width_px, height_px = terminal_pixels(fd)
        margin = 12.0
        win_w = max(320.0, width_px * 0.55)
        win_h = max(140.0, height_px * 0.45)
        return (margin, margin, margin + win_w, margin + win_h)

    def set_text(self, text: str, font_size: float = 16.0) -> bool:
        """Ship UTF-8 text into the window. stdout is flushed first so this
        os-level write to the fd serializes after any pending buffered output
        (single-writer discipline). Returns False on emit failure."""
        sys.stdout.flush()
        try:
            self._view.set_text(text, font_size)
        except Exception:
            return False
        return True

    def set_plot(
        self,
        expr: str,
        x_min: float = 0.0,
        x_max: float = 0.0,
        y_min: float = 0.0,
        y_max: float = 0.0,
    ) -> bool:
        """Ship a yplot expression as the window content. Ranges left at 0 pick
        yplot defaults. Returns False on emit failure."""
        sys.stdout.flush()
        try:
            self._view.set_plot(expr, x_min, x_max, y_min, y_max)
        except Exception:
            return False
        return True

    def move_resize(self, rect: tuple[float, float, float, float]) -> bool:
        """Reposition/resize the window to a new pixel rect."""
        sys.stdout.flush()
        try:
            self._view.set_rect(*rect)
        except Exception:
            return False
        self.min_x, self.min_y, self.max_x, self.max_y = rect
        return True

    def destroy(self) -> None:
        try:
            self._view.destroy()
        except Exception:
            pass


__all__ = ["Window", "terminal_pixels", "pack_abgr", "BG_LIFTED"]


def _demo() -> int:
    """Pop a window in the current yetty pane so you can confirm yview works
    independently of cc_loop. Run from inside a yetty pane:

        uv run tools/ai/cc/ywin.py            # text window
        uv run tools/ai/cc/ywin.py --plot     # a plot window
    """
    if _discover_lib() is None:
        sys.stderr.write(
            "ywin: libyetty_ffi.so not found. Build it with "
            "`make build-desktop-ffi-release`, or set YETTY_FFI_LIB.\n"
        )
        return 1
    win = Window.create()
    if win is None:
        sys.stderr.write("ywin: failed to create window (FFI load error).\n")
        return 1
    wpx, hpx = terminal_pixels(sys.stdout.fileno())
    if "--plot" in sys.argv[1:]:
        ok = win.set_plot("sin(x); cos(x)")
        what = "plot"
    else:
        ok = win.set_text(
            "yview window — live\n"
            f"terminal ≈ {wpx:.0f}x{hpx:.0f} px\n"
            f"rect = ({win.min_x:.0f},{win.min_y:.0f}).."
            f"({win.max_x:.0f},{win.max_y:.0f})\n"
            "this floats over the scroll buffer\n"
            "and does NOT scroll away.",
            18.0,
        )
        what = "text"
    sys.stderr.write(
        f"ywin: emitted {what} window child_id={win.child_id} "
        f"rect=({win.min_x:.0f},{win.min_y:.0f},{win.max_x:.0f},{win.max_y:.0f}). "
        f"{'OK' if ok else 'EMIT FAILED'}. "
        "If you see nothing, you are likely not inside a yetty pane, or tmux "
        "passthrough is off.\n"
    )
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(_demo())
