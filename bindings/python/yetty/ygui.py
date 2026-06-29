"""High-level Python ygui app helpers.

Generated yclass bindings live in ``yetty.generated.ygui``. This module adds
only the process/app glue that is not described by yclass model.yaml today:
framework lifetime, fd-backed PTY output, yface demux, and terminal mouse
subscription.
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
from .generated import ygui as api

REPO_ROOT = Path(__file__).resolve().parents[3]
DEFAULT_LIB = REPO_ROOT / "build-desktop-ffi-release" / "src" / "yetty" / "yffi" / "libyetty_ffi.so"


class _PtrResultUnion(C.Union):
    _fields_ = [("value", C.c_void_p), ("error", T.yetty_ycore_error)]


class _PtrResult(C.Structure):
    _anonymous_ = ("_anon",)
    _fields_ = [("ok", C.c_int), ("_anon", _PtrResultUnion)]


Result = rt.Result
Error = rt.Error
Ctypes = C

MSG_CB = C.CFUNCTYPE(None, C.c_void_p, C.c_int, C.c_void_p, C.c_size_t, C.c_void_p, C.c_size_t)
RAW_CB = C.CFUNCTYPE(None, C.c_void_p, C.c_void_p, C.c_size_t)
RESIZE_CB = C.CFUNCTYPE(None, C.c_void_p, C.c_int, C.c_int, C.c_int, C.c_int)

# The #380 multiplexed-wire reactor seam (yetty_ywire_connection over
# yetty_yclass_transport_pty). Well-known channel ids match channel.h.
CHANNEL_RPC = 1
CHANNEL_INPUT = 2
CHANNEL_RAW = 3


class _Reactor(C.Structure):
    """struct yetty_yclass_transport_reactor — {userdata, ops}, passed by value
    from transport_pty_reactor() into connection_create()."""

    _fields_ = [("userdata", C.c_void_p), ("ops", C.c_void_p)]

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

    Owns the single PTY byte stream via ``yetty_yclass_transport_pty`` (raw-mode,
    non-blocking writer, the fd()/pump() reactor seam), multiplexes rpc/input/raw
    channels over it with ``yetty_ywire_connection``, and binds the ygui
    framework to the rpc channel. All the dangerous byte handling — demux, raw
    mode, framing — lives in the C connection; Python just registers the fd with
    its loop and calls pump(). No Python-side frame parsing.

    Drive it with ``run()`` (synchronous select loop) or ``await run_async()``
    (registers the connection fd with the running asyncio loop).
    """

    def __init__(self, *, compressed: bool = True, in_fd: int | None = None,
                 out_fd: int | None = None):
        load_default()
        self.in_fd = in_fd if in_fd is not None else sys.stdin.fileno()
        self.out_fd = out_fd if out_fd is not None else sys.stdout.fileno()
        self.root: Widget | None = None
        self.running = True
        self._closed = False

        # Sole owner of the PTY stream: raw-mode + non-blocking writer + reactor.
        self.transport = must(_cresult(
            "yetty_yclass_transport_pty_create", _PtrResult, (C.c_int, C.c_int),
            self.in_fd, self.out_fd), "transport_pty_create")
        _cvoid("yetty_yclass_transport_pty_enable_raw_mode", (C.c_void_p,), self.transport)
        reactor = _raw("yetty_yclass_transport_pty_reactor", _Reactor, [C.c_void_p], self.transport)

        # The multiplexed connection over the single stream.
        self.conn = must(_cresult(
            "yetty_ywire_connection_create", _PtrResult, (_Reactor, C.c_int),
            reactor, 1 if compressed else 0), "connection_create")

        # The ygui framework, bound to the connection's rpc channel (RPC requests
        # ride the channel transport adapter; the get_root handshake reads stdin
        # synchronously here, before any loop owns the fd).
        self.framework = must(_cresult(
            "yetty_ygui_framework_create", _PtrResult, (C.c_void_p,), None), "framework_create")
        chtr = must(_cresult("yetty_ywire_channel_transport", _PtrResult, (C.c_void_p,),
                             self._channel(CHANNEL_RPC)), "channel_transport")
        must(_cvoid("yetty_ygui_framework_attach_transport", (C.c_void_p, C.c_void_p),
                    self.framework, chtr), "framework_attach_transport")

        # Inbound routing: forwarded mouse → framework; raw keystrokes → input
        # decoder; resize → viewport. Keep the CFUNCTYPE trampolines alive on self.
        self._env_cb = MSG_CB(self._on_input)
        self._raw_cb = RAW_CB(self._on_raw)
        self._resize_cb = RESIZE_CB(self._on_resize)
        _cvoid("yetty_ywire_channel_set_envelope_sink", (C.c_void_p, MSG_CB, C.c_void_p),
               self._channel(CHANNEL_INPUT), self._env_cb, None)
        _cvoid("yetty_ywire_channel_set_raw_sink", (C.c_void_p, RAW_CB, C.c_void_p),
               self._channel(CHANNEL_RAW), self._raw_cb, None)
        _cvoid("yetty_ywire_connection_set_resize_cb", (C.c_void_p, RESIZE_CB, C.c_void_p),
               self.conn, self._resize_cb, None)

        # Ask the host to forward pointer events (DEC ?1500/?1501), sent verbatim
        # through the raw channel; pick up the initial viewport.
        self._enable_mouse_forwarding()
        _cvoid("yetty_ywire_connection_pickup_winsize", (C.c_void_p,), self.conn)

    # --- connection plumbing -------------------------------------------------

    def _channel(self, channel_id: int):
        return _raw("yetty_ywire_connection_channel", C.c_void_p, [C.c_void_p, C.c_uint],
                    self.conn, channel_id)

    def _enable_mouse_forwarding(self) -> None:
        raw = self._channel(CHANNEL_RAW)
        seq = b"\x1b[?1500h\x1b[?1501h"
        buf = C.create_string_buffer(seq, len(seq))
        _cresult("yetty_ywire_channel_write", T.yetty_ycore_size_result,
                 (C.c_void_p, C.c_void_p, C.c_size_t), raw, buf, len(seq))
        _cvoid("yetty_ywire_channel_flush", (C.c_void_p,), raw)

    def fd(self) -> int:
        """The readable fd to register with a host loop (the reactor seam)."""
        return _raw("yetty_ywire_connection_fd", C.c_int, [C.c_void_p], self.conn)

    def pump_readable(self) -> Result[int | None]:
        return _cresult("yetty_ywire_connection_pump_readable", T.yetty_ycore_size_result,
                        (C.c_void_p,), self.conn)

    def pump_writable(self) -> Result[int | None]:
        return _cresult("yetty_ywire_connection_pump_writable", T.yetty_ycore_size_result,
                        (C.c_void_p,), self.conn)

    def is_eof(self) -> bool:
        return bool(_raw("yetty_ywire_connection_is_eof", C.c_int, [C.c_void_p], self.conn))

    # --- inbound sinks (fired from C during pump_readable) -------------------

    def _on_input(self, _user, wire_code, _args, _args_len, payload, payload_len) -> None:
        if wire_code in (OSC_FIGURE_MOUSE, OSC_MOUSE):
            parsed = parse_mouse(payload, payload_len)
            if parsed:
                kind, button, pressed, x, y, wheel = parsed
                self.feed_mouse_event(kind, button, pressed, x, y, wheel)

    def _on_raw(self, _user, data, n) -> None:
        if not data or n <= 0:
            return
        raw = C.string_at(data, n)
        if any(b in (0x71, 0x03, 0x04) for b in raw):  # q / Ctrl-C / Ctrl-D
            self.running = False
        self.feed_input(raw, n)

    def _on_resize(self, _user, width_px, height_px, _cols, _rows) -> None:
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

    # --- run loops -----------------------------------------------------------

    def run(self) -> int:
        """Synchronous facade: a select() loop over the connection fd plus a
        ~30 fps tick that ships dirty frames. q / Ctrl-C / Ctrl-D quits."""
        import selectors

        sel = selectors.DefaultSelector()
        sel.register(self.fd(), selectors.EVENT_READ)
        try:
            while self.running:
                for _key, _mask in sel.select(timeout=0.033):
                    self.pump_readable()
                self.emit_if_dirty()
                self.pump_writable()
                if self.is_eof():
                    self.running = False
        except KeyboardInterrupt:
            pass
        finally:
            sel.close()
            self.close()
        return 0

    async def run_async(self) -> int:
        """Async facade: register the connection fd with the running asyncio
        loop and pump on readiness; a periodic task ships dirty frames. This is
        the loop-agnostic integration — the host loop owns the fd, the C
        connection owns the bytes."""
        import asyncio

        loop = asyncio.get_running_loop()
        fd = self.fd()
        done: asyncio.Future = loop.create_future()

        def finish() -> None:
            if not done.done():
                done.set_result(0)

        def on_readable() -> None:
            self.pump_readable()
            self.emit_if_dirty()
            self.pump_writable()
            if self.is_eof() or not self.running:
                finish()

        async def ticker() -> None:
            try:
                while self.running and not done.done():
                    self.emit_if_dirty()
                    self.pump_writable()
                    await asyncio.sleep(0.033)
            except asyncio.CancelledError:
                pass
            finish()

        loop.add_reader(fd, on_readable)
        task = loop.create_task(ticker())
        try:
            await done
        finally:
            loop.remove_reader(fd)
            task.cancel()
            self.close()
        return 0

    def close(self) -> None:
        if self._closed:
            return
        self._closed = True
        # Order: framework first (detaches the producer session, destroying the
        # rpc channel transport adapter), then the connection (state machine +
        # channels), then the transport (restores raw mode). NULL-safe in C.
        _cvoid("yetty_ygui_framework_destroy", (C.c_void_p,), self.framework)
        _cvoid("yetty_ywire_connection_destroy", (C.c_void_p,), self.conn)
        _cvoid("yetty_yclass_transport_pty_destroy", (C.c_void_p,), self.transport)

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()
        return False


# The widget-framework app IS the App today (the north-star App/GuiApp split —
# generic container host vs. + ygui framework — lands with the yapp:app mirror).
GuiApp = App


def parse_key(payload, payload_len):
    if payload_len < KEY_STRUCT.size:
        return None
    (_magic, _version, _figure, kind, key, mods, codepoint, _pad) = KEY_STRUCT.unpack(
        C.string_at(payload, KEY_STRUCT.size))
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
     x, y, wheel, _pad) = MOUSE_STRUCT.unpack(C.string_at(payload, MOUSE_STRUCT.size))
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
