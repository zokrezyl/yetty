"""ygui_ffi — shared plumbing for the Python ygui demos.

Loads libyetty_ffi.so and wraps the bits both demos need: the ygui engine
(framework + widget tree), the fd-backed output pty, the yetty_yface stream
demux, and the mouse subscription / parsing. Nothing here is demo-specific.

The engine entry points (yetty_ygui_framework_*, yetty_ygui_add, …) and
yetty_yface_* are plain C, not yclass methods, so they are not in the generated
per-class bindings; we bind them directly against the same shared library via
yetty.runtime.cfn, reusing the generated Result structs from _types.
"""

from __future__ import annotations

import ctypes as C
import fcntl
import os
import struct
import sys
import termios

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
DEFAULT_LIB = os.path.join(
    REPO_ROOT, "build-desktop-ffi-release", "src", "yetty", "yffi", "libyetty_ffi.so"
)


def _resolve_lib() -> str:
    lib = os.environ.get("YETTY_FFI_LIB") or DEFAULT_LIB
    if not os.path.isfile(lib):
        sys.exit(
            f"ygui demo: libyetty_ffi.so not found at {lib}.\n"
            f"Build it with `make build-desktop-ffi-release`, or set YETTY_FFI_LIB."
        )
    return lib


sys.path.insert(0, os.path.join(REPO_ROOT, "bindings", "python"))
from yetty import runtime as rt  # noqa: E402
from yetty.generated import _types as T  # noqa: E402

rt.load(_resolve_lib())

# Every `*_ptr_result` shares one layout {int ok; union{void* value; err}}.
PRES = T.yetty_ygui_object_ptr_result
VOID = T.yetty_ycore_void_result
SIZE = T.yetty_ycore_size_result
INT = T.yetty_ycore_int_result

YettyError = rt.YettyError


def call(name, restype, argtypes, *args):
    """Invoke a C symbol, raising on an error Result."""
    res = rt.cfn(name, restype, list(argtypes))(*args)
    if restype in (PRES, VOID, SIZE, INT):
        rt.check(res)
    return res


def ptr(name, *args, argtypes=()):
    """Call a Result-returning symbol and return its .value pointer."""
    return call(name, PRES, argtypes, *args).value


# --------------------------------------------------------------------------
# Output pty (fd-backed, from the FFI lib) + widget-tree helpers.
# --------------------------------------------------------------------------


def make_output_pty(fd: int):
    # Returns a raw `struct yetty_platform_pty *` (NOT a Result) — bind as ptr.
    return rt.cfn("yetty_yffi_fd_pty_create", C.c_void_p, [C.c_int])(fd)


def destroy_output_pty(pty):
    rt.cfn("yetty_yffi_fd_pty_destroy", None, [C.c_void_p])(pty)


def widget_class(name: str):
    return ptr(f"yetty_ygui_{name}_class_get")


def add(name: str, parent, width: float = 0.0, height: float = 0.0):
    obj = ptr("yetty_ygui_add", widget_class(name), parent, argtypes=(C.c_void_p, C.c_void_p))
    if width or height:
        call("yetty_ygui_widget_set_size", VOID, (C.c_void_p, C.c_float, C.c_float),
             obj, C.c_float(width), C.c_float(height))
    return obj


def set_label(name: str, obj, text: str):
    call(f"yetty_ygui_{name}_set_label", VOID, (C.c_void_p, C.c_char_p), obj, text.encode())


def label_text(obj, text: str):
    call("yetty_ygui_label_set_text", VOID, (C.c_void_p, C.c_char_p), obj, text.encode())


def panel_set_bg(obj, red: int, green: int, blue: int, alpha: int = 255):
    rgba = T.yetty_ycore_rgba(red, green, blue, alpha)
    call("yetty_ygui_panel_set_bg", VOID, (C.c_void_p, T.yetty_ycore_rgba), obj, rgba)


def set_absolute(obj, pos_x: float, pos_y: float, width: float, height: float):
    """Pin a widget at an absolute pixel rect within its parent (overrides flow
    layout) — used to float a panel in a corner."""
    layout_get = rt.cfn(
        "yetty_ygui_widget_layout_get", C.POINTER(T.yetty_ygui_layout), [C.c_void_p])
    layout = T.yetty_ygui_layout.from_buffer_copy(layout_get(obj).contents)
    layout.absolute = 1
    layout.pos_x, layout.pos_y = float(pos_x), float(pos_y)
    layout.width, layout.height = float(width), float(height)
    call("yetty_ygui_widget_layout_set", VOID, (C.c_void_p, C.POINTER(T.yetty_ygui_layout)),
         obj, C.byref(layout))


# --------------------------------------------------------------------------
# Framework wrappers.
# --------------------------------------------------------------------------


def framework_create(pty):
    return ptr("yetty_ygui_framework_create", pty, argtypes=(C.c_void_p,))


def set_root(framework, root):
    call("yetty_ygui_framework_set_root", VOID, (C.c_void_p, C.c_void_p), framework, root)


def set_viewport(framework, width: float, height: float):
    call("yetty_ygui_framework_set_viewport", VOID, (C.c_void_p, C.c_float, C.c_float),
         framework, C.c_float(width), C.c_float(height))


def emit(framework):
    call("yetty_ygui_framework_emit", VOID, (C.c_void_p,), framework)


def emit_if_dirty(framework):
    if rt.cfn("yetty_ygui_framework_is_dirty", C.c_int, [C.c_void_p])(framework):
        emit(framework)


def feed_mouse_button(framework, x, y, button, pressed, mods=0):
    return rt.cfn("yetty_ygui_framework_feed_mouse_button", INT,
                  [C.c_void_p, C.c_float, C.c_float, C.c_int, C.c_int, C.c_int])(
        framework, x, y, button, pressed, mods).value


def feed_mouse_motion(framework, x, y):
    rt.cfn("yetty_ygui_framework_feed_mouse_motion", INT,
           [C.c_void_p, C.c_float, C.c_float])(framework, x, y)


def feed_mouse_scroll(framework, x, y, dx, dy):
    rt.cfn("yetty_ygui_framework_feed_mouse_scroll", VOID,
           [C.c_void_p, C.c_float, C.c_float, C.c_float, C.c_float])(framework, x, y, dx, dy)


def feed_input(framework, data, n):
    rt.cfn("yetty_ygui_framework_feed_input", VOID,
           [C.c_void_p, C.c_void_p, C.c_size_t])(framework, data, n)


def clear_remote_fd(framework, fd):
    try:
        call("yetty_ygui_framework_clear_remote_fd", VOID, (C.c_void_p, C.c_int), framework, fd)
    except YettyError:
        pass


def framework_destroy(framework):
    call("yetty_ygui_framework_destroy", VOID, (C.c_void_p,), framework)


# --------------------------------------------------------------------------
# yetty_yface stream demux + mouse subscription.
# --------------------------------------------------------------------------

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
# yetty_client_input_mouse: magic ver fig kind btn pressed held x y wheel pad
MOUSE_STRUCT = struct.Struct("<IIIIiiIfffI")
MOUSE_KIND_POS, MOUSE_KIND_BUTTON, MOUSE_KIND_WHEEL = 0, 1, 2

# When a compositor figure has keyboard focus (yetty's click-focus model),
# keystrokes are NOT fed to the pane's shell — they arrive as these key OSC
# envelopes instead. A multiplexer must decode them back to terminal bytes.
OSC_FIGURE_KEY = 700003
OSC_KEY = 700012
# yetty_client_input_key: magic ver fig kind key(i32) mods(i32) codepoint pad
KEY_STRUCT = struct.Struct("<IIIIiiII")
KEY_KIND_DOWN, KEY_KIND_UP, KEY_KIND_CHAR = 0, 1, 2
KEY_MOD_CTRL = 2

# GLFW keycode -> terminal byte sequence, for the non-printable keys that don't
# come through as CHAR codepoints.
_GLFW_KEY_BYTES = {
    257: b"\r", 335: b"\r", 258: b"\t", 259: b"\x7f", 256: b"\x1b",
    260: b"\x1b[2~", 261: b"\x1b[3~",
    262: b"\x1b[C", 263: b"\x1b[D", 264: b"\x1b[B", 265: b"\x1b[A",
    266: b"\x1b[5~", 267: b"\x1b[6~", 268: b"\x1b[H", 269: b"\x1b[F",
}


def parse_key(payload, payload_len):
    """Return (kind, key, mods, codepoint) from a key OSC payload, or None."""
    if payload_len < KEY_STRUCT.size:
        return None
    (_magic, _ver, _fig, kind, key, mods, codepoint, _pad) = KEY_STRUCT.unpack(
        C.string_at(payload, KEY_STRUCT.size))
    return kind, key, mods, codepoint


def key_event_to_bytes(kind, key, mods, codepoint) -> bytes:
    """Translate one focused-figure key event into the bytes a terminal would
    send. CHAR carries printable text; DOWN carries special keys + Ctrl combos;
    UP is ignored (so a keypress yields exactly one byte sequence)."""
    if kind == KEY_KIND_CHAR:
        if codepoint and codepoint >= 32 and codepoint != 127:
            return chr(codepoint).encode("utf-8")
        return b""
    if kind == KEY_KIND_DOWN:
        if key in _GLFW_KEY_BYTES:
            return _GLFW_KEY_BYTES[key]
        if (mods & KEY_MOD_CTRL) and 65 <= key <= 90:  # Ctrl-A .. Ctrl-Z
            return bytes([key & 0x1F])
        return b""  # plain printable — delivered via CHAR instead
    return b""


def yface_create():
    return ptr("yetty_yface_create")


def yface_set_handlers(yface, on_osc_cb, on_raw_cb):
    rt.cfn("yetty_yface_set_handlers", None,
           [C.c_void_p, MSG_CB, RAW_CB, C.c_void_p])(yface, on_osc_cb, on_raw_cb, None)


def yface_feed(yface, data: bytes):
    buf = C.create_string_buffer(data, len(data))
    call("yetty_yface_feed_bytes", VOID, (C.c_void_p, C.c_void_p, C.c_size_t),
         yface, buf, len(data))


def _emit_sub(fd: int, flags: int):
    sub = struct.pack("<IIII", SUB_MAGIC, WIRE_VERSION, flags, 0)
    buf = C.create_string_buffer(sub, len(sub))
    call("yetty_yface_emit_to_fd", VOID,
         (C.c_int, C.c_int, C.c_int, C.c_void_p, C.c_size_t, C.c_void_p, C.c_size_t),
         fd, CLIENT_INPUT_SUB, 0, None, 0, buf, len(sub))


def subscribe_mouse(fd: int):
    _emit_sub(fd, SUB_MOUSE_CLICK | SUB_MOUSE_MOVE | SUB_MOUSE_WHEEL)
    os.write(fd, b"\x1b[?1500h\x1b[?1501h")


def unsubscribe_mouse(fd: int):
    os.write(fd, b"\x1b[?1500l\x1b[?1501l")
    try:
        _emit_sub(fd, 0)
    except YettyError:
        pass


def parse_mouse(payload, payload_len):
    """Return (kind, button, pressed, x, y, wheel) from a mouse OSC payload, or
    None if it is not a complete mouse event."""
    if payload_len < MOUSE_STRUCT.size:
        return None
    (_magic, _ver, _fig, kind, button, pressed, _held,
     x, y, wheel, _pad) = MOUSE_STRUCT.unpack(C.string_at(payload, MOUSE_STRUCT.size))
    return kind, button, pressed, x, y, wheel


def terminal_geometry(fd: int):
    """(width_px, height_px) from TIOCGWINSZ, falling back to a 9x18 cell."""
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
