"""High-level Python ygui app helpers.

Generated yclass bindings live in ``yetty.generated.ygui``; the generated
reactor-seam facade (connection + channels + sync/async drivers) lives in
``yetty.generated.connection``. This module adds only the ygui-specific app
glue on top: framework lifetime, widget building, and the input-event →
framework dispatch. All connection plumbing — raw mode, demux, pumps, the
sync/async run loops — is the generated facade's job.
"""

from __future__ import annotations

import ctypes as C
import fcntl
import os
import struct
import sys
import termios
from dataclasses import dataclass
from pathlib import Path

from . import runtime as rt
from .generated import _types as T
from .generated import connection as wire
from .generated import ygui as api

REPO_ROOT = Path(__file__).resolve().parents[3]
DEFAULT_LIB = REPO_ROOT / "build-desktop-ffi-release" / "src" / "yetty" / "yffi" / "libyetty_ffi.so"

Result = rt.Result
Error = rt.Error
Ctypes = C

# Re-exported for callers that used these from here (the canonical constants
# live in the generated facade).
CHANNEL_RPC = wire.CHANNEL_RPC
CHANNEL_INPUT = wire.CHANNEL_INPUT
CHANNEL_RAW = wire.CHANNEL_RAW

CLIENT_INPUT_SUB = 610010
SUB_MAGIC = 0x53504954
WIRE_VERSION = 4
SUB_MOUSE_CLICK = 1 << 0
SUB_MOUSE_MOVE = 1 << 1
SUB_MOUSE_WHEEL = 1 << 2

OSC_FIGURE_MOUSE = 700000
OSC_MOUSE = 700010
MOUSE_STRUCT = struct.Struct("<IIIIiiIfffI")
MOUSE_KIND_POS, MOUSE_KIND_BUTTON, MOUSE_KIND_WHEEL = 0, 1, 2

OSC_FIGURE_KEY = 700003
OSC_KEY = 700012
KEY_STRUCT = struct.Struct("<IIIIiiII")
KEY_KIND_DOWN, KEY_KIND_UP, KEY_KIND_CHAR = 0, 1, 2
KEY_MOD_CTRL = 2

_GLFW_KEY_BYTES = {
    257: b"\r", 335: b"\r", 258: b"\t", 259: b"\x7f", 256: b"\x1b",
    260: b"\x1b[2~", 261: b"\x1b[3~",
    262: b"\x1b[C", 263: b"\x1b[D", 264: b"\x1b[B", 265: b"\x1b[A",
    266: b"\x1b[5~", 267: b"\x1b[6~", 268: b"\x1b[H", 269: b"\x1b[F",
}

_WIDGET_CLASSES = {
    cls.__yclass_name__: cls
    for cls in vars(api).values()
    if isinstance(cls, type) and hasattr(cls, "__yclass_name__")
}


def load_default(path: str | os.PathLike[str] | None = None):
    lib_path = Path(path or os.environ.get("YETTY_FFI_LIB") or DEFAULT_LIB)
    return rt.load(str(lib_path))


def error_text(error: Error | None) -> str:
    if error is None:
        return "unknown error"
    parts = []
    current = error
    while current:
        location = f" at {current.file}:{current.line}" if current.file else ""
        function = f" ({current.func})" if current.func else ""
        parts.append(f"{current.message}{location}{function}")
        current = current.cause
    return "\n  caused by: ".join(parts)


def must(result: Result, context: str = "ygui"):
    if result:
        return result.value
    sys.exit(f"{context}: {error_text(result.error)}")


def _cresult(name: str, restype, argtypes, *args, convert=None) -> Result:
    result = rt.cfn(name, restype, list(argtypes))(*args)
    return rt.result_from_c(result, convert)


def _cvoid(name: str, argtypes, *args) -> Result[None]:
    return _cresult(name, T.yetty_ycore_void_result, argtypes, *args)


def _raw(name: str, restype, argtypes, *args):
    return rt.cfn(name, restype, list(argtypes))(*args)


@dataclass
class Widget:
    app: "App"
    kind: str
    handle: int
    obj: object

    def set_size(self, width: float = 0.0, height: float = 0.0) -> Result[None]:
        return _cvoid("yetty_ygui_widget_set_size", (C.c_void_p, C.c_float, C.c_float),
                      self.handle, C.c_float(width), C.c_float(height))

    def label(self, text: str) -> Result[None]:
        setter = api.label_set_text if self.kind == "label" else getattr(api, f"{self.kind}_set_label")
        return setter(self.obj, text)

    def text(self, text: str) -> Result[None]:
        return self.label(text)

    def slider_range(self, minimum: float, maximum: float) -> Result[None]:
        return api.slider_set_range(self.obj, minimum, maximum)

    def slider_value(self, value: float) -> Result[None]:
        return api.slider_set_value(self.obj, value)


class App:
    """In-terminal ygui app driven by the multiplexed wire connection (#380).

    The generated ``yetty.generated.connection`` facade owns the PTY byte
    stream (raw mode, non-blocking writer, fd()/pump() reactor seam) and the
    rpc/input/raw channel mux. This class binds the ygui framework to the rpc
    channel and translates input-channel events into framework feeds.

    Drive it with ``run()`` (the facade's synchronous select loop) or
    ``await run_async()`` (the facade's asyncio integration).
    """

    def __init__(self, *, compressed: bool = True, in_fd: int | None = None,
                 out_fd: int | None = None):
        load_default()
        self.root: Widget | None = None
        self.running = True
        self._closed = False
        # Declared up front so close() can run after a partial init. The C
        # destroys behind link.close() are NULL-safe.
        self.link: wire.Connection | None = None
        self.framework = None

        # Once raw mode is enabled the terminal MUST be restored on every exit
        # path — any bring-up failure tears down what was set up (closing the
        # link restores raw mode + fd flags).
        try:
            self.link = wire.Connection(in_fd, out_fd, compressed=compressed)

            # The ygui framework, bound to the connection's rpc channel (RPC
            # requests ride the channel transport adapter; the get_root handshake
            # reads the fd synchronously here, before any loop owns it).
            self.framework = must(_cresult(
                "yetty_ygui_framework_create", wire._PtrResult, (C.c_void_p,), None),
                "framework_create")
            channel_transport = must(self.link.rpc.transport(), "channel_transport")
            must(_cvoid("yetty_ygui_framework_attach_transport", (C.c_void_p, C.c_void_p),
                        self.framework, channel_transport), "framework_attach_transport")

            # Inbound routing: forwarded mouse → framework; raw keystrokes →
            # input decoder; resize → viewport.
            self.link.input.set_envelope_sink(self._on_input)
            self.link.raw.set_raw_sink(self._on_raw)
            self.link.set_resize_cb(self._on_resize)

            # Ask the host to forward pointer events (DEC ?1500/?1501), sent
            # verbatim through the raw channel; pick up the initial viewport.
            self.link.raw.write(b"\x1b[?1500h\x1b[?1501h")
            self.link.raw.flush()
            self.link.pickup_winsize()
        except BaseException:
            # Includes SystemExit from must(): restore the terminal, then re-raise.
            self.close()
            raise

    # --- connection plumbing (delegated to the generated facade) -------------

    def fd(self) -> int:
        """The readable fd to register with a host loop (the reactor seam)."""
        return self.link.fd()

    def pump_readable(self) -> Result[int | None]:
        return self.link.pump_readable()

    def pump_writable(self) -> Result[int | None]:
        return self.link.pump_writable()

    def is_eof(self) -> bool:
        return self.link.is_eof()

    # --- inbound sinks (fired from C during pump_readable) -------------------

    def _on_input(self, wire_code: int, _args: bytes, payload: bytes) -> None:
        if wire_code in (OSC_FIGURE_MOUSE, OSC_MOUSE):
            parsed = parse_mouse(payload, len(payload))
            if parsed:
                kind, button, pressed, x, y, wheel = parsed
                self.feed_mouse_event(kind, button, pressed, x, y, wheel)

    def _on_raw(self, data: bytes) -> None:
        if not data:
            return
        if any(b in (0x71, 0x03, 0x04) for b in data):  # q / Ctrl-C / Ctrl-D
            self.running = False
        self.feed_input(data, len(data))

    def _on_resize(self, width_px: int, height_px: int, _cols: int, _rows: int) -> None:
        if width_px > 0 and height_px > 0:
            _cvoid("yetty_ygui_framework_set_viewport", (C.c_void_p, C.c_float, C.c_float),
                   self.framework, C.c_float(float(width_px)), C.c_float(float(height_px)))

    # --- widget building -----------------------------------------------------

    def add(self, kind: str, parent: Widget | None = None, *, width: float = 0.0,
            height: float = 0.0, label: str | None = None, text: str | None = None) -> Widget:
        cls = _WIDGET_CLASSES[kind]
        class_handle = must(cls.yclass(), f"{kind}.class")
        result = api.widget_add(parent.obj, class_handle) if parent else api.widget_new(class_handle)
        handle = must(result, f"add {kind}")
        widget = Widget(self, kind, handle, cls(_handle=handle))
        if width or height:
            must(widget.set_size(width, height), f"{kind}.set_size")
        content = text if text is not None else label
        if content is not None:
            must(widget.label(content), f"{kind}.label")
        return widget

    def set_root(self, root: Widget) -> Result[None]:
        self.root = root
        return _cvoid("yetty_ygui_framework_set_root", (C.c_void_p, C.c_void_p),
                      self.framework, root.handle)

    def emit(self) -> Result[None]:
        return _cvoid("yetty_ygui_framework_emit", (C.c_void_p,), self.framework)

    def emit_if_dirty(self) -> Result[None]:
        if _raw("yetty_ygui_framework_is_dirty", C.c_int, [C.c_void_p], self.framework):
            return self.emit()
        return Result(value=None)

    def feed_mouse_event(self, kind: int, button: int, pressed: int, x: float, y: float,
                         wheel: float) -> Result[int | None]:
        if kind == MOUSE_KIND_BUTTON:
            return _cresult("yetty_ygui_framework_feed_mouse_button", T.yetty_ycore_int_result,
                            (C.c_void_p, C.c_float, C.c_float, C.c_int, C.c_int, C.c_int),
                            self.framework, x, y, button, pressed, 0)
        if kind == MOUSE_KIND_POS:
            return _cresult("yetty_ygui_framework_feed_mouse_motion", T.yetty_ycore_int_result,
                            (C.c_void_p, C.c_float, C.c_float), self.framework, x, y)
        if kind == MOUSE_KIND_WHEEL:
            result = _cvoid("yetty_ygui_framework_feed_mouse_scroll",
                            (C.c_void_p, C.c_float, C.c_float, C.c_float, C.c_float),
                            self.framework, x, y, 0.0, wheel)
            return Result(value=0) if result else Result(error=result.error)
        return Result(value=0)

    def feed_input(self, data, n: int) -> Result[None]:
        return _cvoid("yetty_ygui_framework_feed_input", (C.c_void_p, C.c_void_p, C.c_size_t),
                      self.framework, data, n)

    # --- run loops (the generated facade's drivers) ---------------------------

    def _tick(self) -> None:
        self.emit_if_dirty()

    def _stopped(self) -> bool:
        return not self.running

    def run(self) -> int:
        """Synchronous facade: the generated selectors loop plus a ~30 fps tick
        that ships dirty frames. q / Ctrl-C / Ctrl-D quits."""
        try:
            return self.link.run_forever(on_tick=self._tick, should_stop=self._stopped)
        finally:
            self.close()

    async def run_async(self) -> int:
        """Async facade: the generated asyncio integration — the host loop owns
        the fd, the C connection owns the bytes."""
        try:
            return await self.link.run_async(on_tick=self._tick, should_stop=self._stopped)
        finally:
            self.close()

    def close(self) -> None:
        if self._closed:
            return
        self._closed = True
        # Order: framework first (detaches the producer session, destroying the
        # rpc channel transport adapter), then the link (connection + transport,
        # restoring raw mode). Each is skipped if it was never created.
        if self.framework:
            _cvoid("yetty_ygui_framework_destroy", (C.c_void_p,), self.framework)
            self.framework = None
        if self.link:
            self.link.close()
            self.link = None

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()
        return False


# The widget-framework app IS the App today (the north-star App/GuiApp split —
# generic container host vs. + ygui framework — lands with the yapp:app mirror).
GuiApp = App


def _payload_bytes(payload, payload_len: int) -> bytes:
    """Accept both the facade's bytes payloads and legacy ctypes pointers."""
    if isinstance(payload, (bytes, bytearray, memoryview)):
        return bytes(payload)[:payload_len]
    return C.string_at(payload, payload_len)


def parse_key(payload, payload_len):
    if payload_len < KEY_STRUCT.size:
        return None
    (_magic, _version, _figure, kind, key, mods, codepoint, _pad) = KEY_STRUCT.unpack(
        _payload_bytes(payload, KEY_STRUCT.size))
    return kind, key, mods, codepoint


def key_event_to_bytes(kind, key, mods, codepoint) -> bytes:
    if kind == KEY_KIND_CHAR:
        if codepoint and codepoint >= 32 and codepoint != 127:
            return chr(codepoint).encode("utf-8")
        return b""
    if kind == KEY_KIND_DOWN:
        if key in _GLFW_KEY_BYTES:
            return _GLFW_KEY_BYTES[key]
        if (mods & KEY_MOD_CTRL) and 65 <= key <= 90:
            return bytes([key & 0x1F])
    return b""


def parse_mouse(payload, payload_len):
    if payload_len < MOUSE_STRUCT.size:
        return None
    (_magic, _version, _figure, kind, button, pressed, _held,
     x, y, wheel, _pad) = MOUSE_STRUCT.unpack(_payload_bytes(payload, MOUSE_STRUCT.size))
    return kind, button, pressed, x, y, wheel


def terminal_geometry(fd: int):
    try:
        rows, cols, xpix, ypix = struct.unpack(
            "HHHH", fcntl.ioctl(fd, termios.TIOCGWINSZ, b"\0" * 8))
    except OSError:
        rows = cols = xpix = ypix = 0
    cols = cols or 80
    rows = rows or 24
    if not xpix or not ypix:
        xpix, ypix = cols * 9, rows * 18
    return float(xpix), float(ypix)
