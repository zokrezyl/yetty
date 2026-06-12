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
from typing import Callable

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
    def __init__(self, out_fd: int | None = None):
        load_default()
        self.out_fd = out_fd if out_fd is not None else sys.stdout.fileno()
        self.pty = _raw("yetty_yffi_fd_pty_create", C.c_void_p, [C.c_int], self.out_fd)
        self.framework = must(
            _cresult("yetty_ygui_framework_create", _PtrResult, (C.c_void_p,), self.pty),
            "framework_create")
        self.root: Widget | None = None
        self._closed = False

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

    def resize_to_terminal(self) -> Result[None]:
        width, height = terminal_geometry(self.out_fd)
        return _cvoid("yetty_ygui_framework_set_viewport", (C.c_void_p, C.c_float, C.c_float),
                      self.framework, C.c_float(width), C.c_float(height))

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

    def clear(self) -> Result[None]:
        return _cvoid("yetty_ygui_framework_clear_remote_fd", (C.c_void_p, C.c_int),
                      self.framework, self.out_fd)

    def close(self) -> None:
        if self._closed:
            return
        self._closed = True
        self.clear()
        _cvoid("yetty_ygui_framework_destroy", (C.c_void_p,), self.framework)
        _raw("yetty_yffi_fd_pty_destroy", None, [C.c_void_p], self.pty)

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()
        return False


class Demux:
    def __init__(self, on_osc: Callable, on_raw: Callable):
        load_default()
        self.yface = must(_cresult("yetty_yface_create", _PtrResult, ()), "yface_create")
        self._osc_cb = MSG_CB(on_osc)
        self._raw_cb = RAW_CB(on_raw)
        _raw("yetty_yface_set_handlers", None, [C.c_void_p, MSG_CB, RAW_CB, C.c_void_p],
             self.yface, self._osc_cb, self._raw_cb, None)

    def feed(self, data: bytes) -> Result[None]:
        buf = C.create_string_buffer(data, len(data))
        return _cvoid("yetty_yface_feed_bytes", (C.c_void_p, C.c_void_p, C.c_size_t),
                      self.yface, buf, len(data))

    def close(self) -> Result[None]:
        return _cvoid("yetty_yface_destroy", (C.c_void_p,), self.yface)


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


def _emit_sub(fd: int, flags: int) -> Result[None]:
    sub = struct.pack("<IIII", SUB_MAGIC, WIRE_VERSION, flags, 0)
    buf = C.create_string_buffer(sub, len(sub))
    return _cvoid("yetty_yface_emit_to_fd",
                  (C.c_int, C.c_int, C.c_int, C.c_void_p, C.c_size_t, C.c_void_p, C.c_size_t),
                  fd, CLIENT_INPUT_SUB, 0, None, 0, buf, len(sub))


def subscribe_mouse(fd: int) -> Result[None]:
    result = _emit_sub(fd, SUB_MOUSE_CLICK | SUB_MOUSE_MOVE | SUB_MOUSE_WHEEL)
    os.write(fd, b"\x1b[?1500h\x1b[?1501h")
    return result


def unsubscribe_mouse(fd: int) -> Result[None]:
    os.write(fd, b"\x1b[?1500l\x1b[?1501l")
    return _emit_sub(fd, 0)


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
