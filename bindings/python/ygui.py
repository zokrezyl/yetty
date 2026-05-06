"""ygui ctypes bindings — auto-generated.

DO NOT EDIT. Regenerate with:
    tools/ffi-codegen/.venv/bin/python tools/ffi-codegen/emit/python/ygui/generate.py

Source: build/ffi/ygui.yaml
Header: include/yetty/ygui/ygui.h

Library loading is deferred — call ygui.load(path) once at startup, or set
the YGUI_LIB env var to a shared library path and the module loads it on
import.
"""

from __future__ import annotations

import ctypes
import os
from ctypes import (
    CFUNCTYPE, POINTER, Structure, Union,
    c_bool, c_byte, c_char, c_char_p, c_double, c_float, c_int, c_int8,
    c_int16, c_int32, c_int64, c_long, c_longdouble, c_longlong, c_short,
    c_size_t, c_ubyte, c_uint, c_uint8, c_uint16, c_uint32, c_uint64,
    c_ulong, c_ulonglong, c_ushort, c_void_p,
)
from enum import IntEnum

# ---------------------------------------------------------------------------
# Library loading
# ---------------------------------------------------------------------------

_lib: ctypes.CDLL | None = None


def load(path: str) -> ctypes.CDLL:
    """Load the ygui shared library and bind every function in this module
    to its argtypes / restype. Idempotent."""
    global _lib
    _lib = ctypes.CDLL(path)
    _bind_functions()
    return _lib


def _check_loaded() -> ctypes.CDLL:
    if _lib is None:
        raise RuntimeError(
            "ygui: library not loaded — call ygui.load(path) first, "
            "or set YGUI_LIB to a shared library path."
        )
    return _lib


# ---------------------------------------------------------------------------
# Enums
# ---------------------------------------------------------------------------

class ygui_widget_type_t(IntEnum):
    YETTY_YGUI_WIDGET_BUTTON = 0
    YETTY_YGUI_WIDGET_LABEL = 1
    YETTY_YGUI_WIDGET_SLIDER = 2
    YETTY_YGUI_WIDGET_CHECKBOX = 3
    YETTY_YGUI_WIDGET_TEXTINPUT = 4
    YETTY_YGUI_WIDGET_PANEL = 5
    YETTY_YGUI_WIDGET_HBOX = 6
    YETTY_YGUI_WIDGET_VBOX = 7
    YETTY_YGUI_WIDGET_DROPDOWN = 8
    YETTY_YGUI_WIDGET_LISTBOX = 9
    YETTY_YGUI_WIDGET_TABLE = 10
    YETTY_YGUI_WIDGET_TABBAR = 11
    YETTY_YGUI_WIDGET_COLORPICKER = 12
    YETTY_YGUI_WIDGET_SCROLLAREA = 13
    YETTY_YGUI_WIDGET_PROGRESS = 14
    YETTY_YGUI_WIDGET_SEPARATOR = 15
    YETTY_YGUI_WIDGET_POPUP = 16
    YETTY_YGUI_WIDGET_COLLAPSING_HEADER = 17
    YETTY_YGUI_WIDGET_TOOLTIP = 18
    YETTY_YGUI_WIDGET_SELECTABLE = 19
    YETTY_YGUI_WIDGET_CHOICEBOX = 20
    YETTY_YGUI_WIDGET_VSCROLLBAR = 21
    YETTY_YGUI_WIDGET_HSCROLLBAR = 22
    YETTY_YGUI_WIDGET_LIST = 23
    YETTY_YGUI_WIDGET_TREE_NODE = 24
    YETTY_YGUI_WIDGET_CUSTOM = 25

YETTY_YGUI_WIDGET_BUTTON = ygui_widget_type_t.YETTY_YGUI_WIDGET_BUTTON
YETTY_YGUI_WIDGET_LABEL = ygui_widget_type_t.YETTY_YGUI_WIDGET_LABEL
YETTY_YGUI_WIDGET_SLIDER = ygui_widget_type_t.YETTY_YGUI_WIDGET_SLIDER
YETTY_YGUI_WIDGET_CHECKBOX = ygui_widget_type_t.YETTY_YGUI_WIDGET_CHECKBOX
YETTY_YGUI_WIDGET_TEXTINPUT = ygui_widget_type_t.YETTY_YGUI_WIDGET_TEXTINPUT
YETTY_YGUI_WIDGET_PANEL = ygui_widget_type_t.YETTY_YGUI_WIDGET_PANEL
YETTY_YGUI_WIDGET_HBOX = ygui_widget_type_t.YETTY_YGUI_WIDGET_HBOX
YETTY_YGUI_WIDGET_VBOX = ygui_widget_type_t.YETTY_YGUI_WIDGET_VBOX
YETTY_YGUI_WIDGET_DROPDOWN = ygui_widget_type_t.YETTY_YGUI_WIDGET_DROPDOWN
YETTY_YGUI_WIDGET_LISTBOX = ygui_widget_type_t.YETTY_YGUI_WIDGET_LISTBOX
YETTY_YGUI_WIDGET_TABLE = ygui_widget_type_t.YETTY_YGUI_WIDGET_TABLE
YETTY_YGUI_WIDGET_TABBAR = ygui_widget_type_t.YETTY_YGUI_WIDGET_TABBAR
YETTY_YGUI_WIDGET_COLORPICKER = ygui_widget_type_t.YETTY_YGUI_WIDGET_COLORPICKER
YETTY_YGUI_WIDGET_SCROLLAREA = ygui_widget_type_t.YETTY_YGUI_WIDGET_SCROLLAREA
YETTY_YGUI_WIDGET_PROGRESS = ygui_widget_type_t.YETTY_YGUI_WIDGET_PROGRESS
YETTY_YGUI_WIDGET_SEPARATOR = ygui_widget_type_t.YETTY_YGUI_WIDGET_SEPARATOR
YETTY_YGUI_WIDGET_POPUP = ygui_widget_type_t.YETTY_YGUI_WIDGET_POPUP
YETTY_YGUI_WIDGET_COLLAPSING_HEADER = ygui_widget_type_t.YETTY_YGUI_WIDGET_COLLAPSING_HEADER
YETTY_YGUI_WIDGET_TOOLTIP = ygui_widget_type_t.YETTY_YGUI_WIDGET_TOOLTIP
YETTY_YGUI_WIDGET_SELECTABLE = ygui_widget_type_t.YETTY_YGUI_WIDGET_SELECTABLE
YETTY_YGUI_WIDGET_CHOICEBOX = ygui_widget_type_t.YETTY_YGUI_WIDGET_CHOICEBOX
YETTY_YGUI_WIDGET_VSCROLLBAR = ygui_widget_type_t.YETTY_YGUI_WIDGET_VSCROLLBAR
YETTY_YGUI_WIDGET_HSCROLLBAR = ygui_widget_type_t.YETTY_YGUI_WIDGET_HSCROLLBAR
YETTY_YGUI_WIDGET_LIST = ygui_widget_type_t.YETTY_YGUI_WIDGET_LIST
YETTY_YGUI_WIDGET_TREE_NODE = ygui_widget_type_t.YETTY_YGUI_WIDGET_TREE_NODE
YETTY_YGUI_WIDGET_CUSTOM = ygui_widget_type_t.YETTY_YGUI_WIDGET_CUSTOM

class ygui_event_type_t(IntEnum):
    YETTY_YGUI_EVENT_NONE = 0
    YETTY_YGUI_EVENT_CLICK = 1
    YETTY_YGUI_EVENT_PRESS = 2
    YETTY_YGUI_EVENT_RELEASE = 3
    YETTY_YGUI_EVENT_CHANGE = 4
    YETTY_YGUI_EVENT_SCROLL = 5
    YETTY_YGUI_EVENT_FOCUS = 6
    YETTY_YGUI_EVENT_BLUR = 7
    YETTY_YGUI_EVENT_KEY = 8
    YETTY_YGUI_EVENT_TEXT = 9

YETTY_YGUI_EVENT_NONE = ygui_event_type_t.YETTY_YGUI_EVENT_NONE
YETTY_YGUI_EVENT_CLICK = ygui_event_type_t.YETTY_YGUI_EVENT_CLICK
YETTY_YGUI_EVENT_PRESS = ygui_event_type_t.YETTY_YGUI_EVENT_PRESS
YETTY_YGUI_EVENT_RELEASE = ygui_event_type_t.YETTY_YGUI_EVENT_RELEASE
YETTY_YGUI_EVENT_CHANGE = ygui_event_type_t.YETTY_YGUI_EVENT_CHANGE
YETTY_YGUI_EVENT_SCROLL = ygui_event_type_t.YETTY_YGUI_EVENT_SCROLL
YETTY_YGUI_EVENT_FOCUS = ygui_event_type_t.YETTY_YGUI_EVENT_FOCUS
YETTY_YGUI_EVENT_BLUR = ygui_event_type_t.YETTY_YGUI_EVENT_BLUR
YETTY_YGUI_EVENT_KEY = ygui_event_type_t.YETTY_YGUI_EVENT_KEY
YETTY_YGUI_EVENT_TEXT = ygui_event_type_t.YETTY_YGUI_EVENT_TEXT

class ygui_flags_t(IntEnum):
    YETTY_YGUI_FLAG_NONE = 0
    YETTY_YGUI_FLAG_HOVER = 1
    YETTY_YGUI_FLAG_PRESSED = 2
    YETTY_YGUI_FLAG_FOCUSED = 4
    YETTY_YGUI_FLAG_DISABLED = 8
    YETTY_YGUI_FLAG_CHECKED = 16
    YETTY_YGUI_FLAG_OPEN = 32
    YETTY_YGUI_FLAG_VISIBLE = 64

YETTY_YGUI_FLAG_NONE = ygui_flags_t.YETTY_YGUI_FLAG_NONE
YETTY_YGUI_FLAG_HOVER = ygui_flags_t.YETTY_YGUI_FLAG_HOVER
YETTY_YGUI_FLAG_PRESSED = ygui_flags_t.YETTY_YGUI_FLAG_PRESSED
YETTY_YGUI_FLAG_FOCUSED = ygui_flags_t.YETTY_YGUI_FLAG_FOCUSED
YETTY_YGUI_FLAG_DISABLED = ygui_flags_t.YETTY_YGUI_FLAG_DISABLED
YETTY_YGUI_FLAG_CHECKED = ygui_flags_t.YETTY_YGUI_FLAG_CHECKED
YETTY_YGUI_FLAG_OPEN = ygui_flags_t.YETTY_YGUI_FLAG_OPEN
YETTY_YGUI_FLAG_VISIBLE = ygui_flags_t.YETTY_YGUI_FLAG_VISIBLE

class ygui_canvas_mode_t(IntEnum):
    YETTY_YGUI_CANVAS_FIXED = 0
    YETTY_YGUI_CANVAS_FIT = 1

YETTY_YGUI_CANVAS_FIXED = ygui_canvas_mode_t.YETTY_YGUI_CANVAS_FIXED
YETTY_YGUI_CANVAS_FIT = ygui_canvas_mode_t.YETTY_YGUI_CANVAS_FIT

class ygui_scale_mode_t(IntEnum):
    YETTY_YGUI_SCALE_OFF = 0
    YETTY_YGUI_SCALE_ON = 1

YETTY_YGUI_SCALE_OFF = ygui_scale_mode_t.YETTY_YGUI_SCALE_OFF
YETTY_YGUI_SCALE_ON = ygui_scale_mode_t.YETTY_YGUI_SCALE_ON

class ygui_layout_mode_t(IntEnum):
    YETTY_YGUI_LAYOUT_MANUAL = 0
    YETTY_YGUI_LAYOUT_FLEX = 1

YETTY_YGUI_LAYOUT_MANUAL = ygui_layout_mode_t.YETTY_YGUI_LAYOUT_MANUAL
YETTY_YGUI_LAYOUT_FLEX = ygui_layout_mode_t.YETTY_YGUI_LAYOUT_FLEX

class ygui_flex_direction_t(IntEnum):
    YETTY_YGUI_FLEX_ROW = 0
    YETTY_YGUI_FLEX_COLUMN = 1

YETTY_YGUI_FLEX_ROW = ygui_flex_direction_t.YETTY_YGUI_FLEX_ROW
YETTY_YGUI_FLEX_COLUMN = ygui_flex_direction_t.YETTY_YGUI_FLEX_COLUMN

class ygui_justify_t(IntEnum):
    YETTY_YGUI_JUSTIFY_START = 0
    YETTY_YGUI_JUSTIFY_CENTER = 1
    YETTY_YGUI_JUSTIFY_END = 2
    YETTY_YGUI_JUSTIFY_SPACE_BETWEEN = 3
    YETTY_YGUI_JUSTIFY_SPACE_AROUND = 4
    YETTY_YGUI_JUSTIFY_SPACE_EVENLY = 5

YETTY_YGUI_JUSTIFY_START = ygui_justify_t.YETTY_YGUI_JUSTIFY_START
YETTY_YGUI_JUSTIFY_CENTER = ygui_justify_t.YETTY_YGUI_JUSTIFY_CENTER
YETTY_YGUI_JUSTIFY_END = ygui_justify_t.YETTY_YGUI_JUSTIFY_END
YETTY_YGUI_JUSTIFY_SPACE_BETWEEN = ygui_justify_t.YETTY_YGUI_JUSTIFY_SPACE_BETWEEN
YETTY_YGUI_JUSTIFY_SPACE_AROUND = ygui_justify_t.YETTY_YGUI_JUSTIFY_SPACE_AROUND
YETTY_YGUI_JUSTIFY_SPACE_EVENLY = ygui_justify_t.YETTY_YGUI_JUSTIFY_SPACE_EVENLY

class ygui_align_t(IntEnum):
    YETTY_YGUI_ALIGN_AUTO = 0
    YETTY_YGUI_ALIGN_START = 1
    YETTY_YGUI_ALIGN_CENTER = 2
    YETTY_YGUI_ALIGN_END = 3
    YETTY_YGUI_ALIGN_STRETCH = 4
    YETTY_YGUI_ALIGN_BASELINE = 5

YETTY_YGUI_ALIGN_AUTO = ygui_align_t.YETTY_YGUI_ALIGN_AUTO
YETTY_YGUI_ALIGN_START = ygui_align_t.YETTY_YGUI_ALIGN_START
YETTY_YGUI_ALIGN_CENTER = ygui_align_t.YETTY_YGUI_ALIGN_CENTER
YETTY_YGUI_ALIGN_END = ygui_align_t.YETTY_YGUI_ALIGN_END
YETTY_YGUI_ALIGN_STRETCH = ygui_align_t.YETTY_YGUI_ALIGN_STRETCH
YETTY_YGUI_ALIGN_BASELINE = ygui_align_t.YETTY_YGUI_ALIGN_BASELINE

class ygui_flex_wrap_t(IntEnum):
    YETTY_YGUI_FLEX_NOWRAP = 0
    YETTY_YGUI_FLEX_WRAP = 1

YETTY_YGUI_FLEX_NOWRAP = ygui_flex_wrap_t.YETTY_YGUI_FLEX_NOWRAP
YETTY_YGUI_FLEX_WRAP = ygui_flex_wrap_t.YETTY_YGUI_FLEX_WRAP

class ygui_position_t(IntEnum):
    YETTY_YGUI_POSITION_RELATIVE = 0
    YETTY_YGUI_POSITION_ABSOLUTE = 1

YETTY_YGUI_POSITION_RELATIVE = ygui_position_t.YETTY_YGUI_POSITION_RELATIVE
YETTY_YGUI_POSITION_ABSOLUTE = ygui_position_t.YETTY_YGUI_POSITION_ABSOLUTE

YETTY_YGUI_VERSION_MAJOR = 0
YETTY_YGUI_VERSION_MINOR = 2
YETTY_YGUI_VERSION_PATCH = 0

# ---------------------------------------------------------------------------
# Struct / union forward declarations
# (opaque types declared empty; full layouts assigned below)
# ---------------------------------------------------------------------------

class _yetty_anon_210_5(Union):
    pass

class _yetty_anon_215_9(Structure):
    pass

class _yetty_anon_218_9(Structure):
    pass

class _yetty_anon_221_9(Structure):
    pass

class _yetty_anon_41_1(Union):
    pass

class uv_loop_s(Structure):
    pass

class yetty_ycore_error(Structure):
    pass

class yetty_ycore_void_result(Structure):
    pass

class yetty_ygui_engine(Structure):
    pass

class yetty_ygui_layout(Structure):
    pass

class yetty_ygui_theme(Structure):
    pass

class yetty_ygui_widget(Structure):
    pass

class ygui_engine_ptr_result(Structure):
    pass

class ygui_event_t(Structure):
    pass

# ---------------------------------------------------------------------------
# Struct / union field layouts
# ---------------------------------------------------------------------------

_yetty_anon_41_1._fields_ = [
    ('value', POINTER(yetty_ygui_engine)),
    ('error', yetty_ycore_error),
]

ygui_engine_ptr_result._fields_ = [
    ('ok', c_int),
]

yetty_ygui_layout._fields_ = [
    ('mode', c_int),
    ('direction', c_int),
    ('wrap', c_int),
    ('justify_content', c_int),
    ('align_items', c_int),
    ('align_self', c_int),
    ('align_content', c_int),
    ('position', c_int),
    ('flex_grow', c_float),
    ('flex_shrink', c_float),
    ('flex_basis', c_float),
    ('flex_basis_percent', c_float),
    ('gap', c_float),
    ('padding_top', c_float),
    ('padding_right', c_float),
    ('padding_bottom', c_float),
    ('padding_left', c_float),
    ('margin_top', c_float),
    ('margin_right', c_float),
    ('margin_bottom', c_float),
    ('margin_left', c_float),
    ('min_w', c_float),
    ('min_h', c_float),
    ('max_w', c_float),
    ('max_h', c_float),
    ('min_w_percent', c_float),
    ('min_h_percent', c_float),
    ('max_w_percent', c_float),
    ('max_h_percent', c_float),
    ('width_percent', c_float),
    ('height_percent', c_float),
]

_yetty_anon_215_9._fields_ = [
    ('r', c_float),
    ('g', c_float),
    ('b', c_float),
    ('a', c_float),
]

_yetty_anon_218_9._fields_ = [
    ('x', c_float),
    ('y', c_float),
]

_yetty_anon_221_9._fields_ = [
    ('key', c_uint),
    ('mods', c_int),
]

_yetty_anon_210_5._fields_ = [
    ('float_value', c_float),
    ('int_value', c_int),
    ('bool_value', c_int),
    ('string_value', POINTER(c_char)),
    ('color', _yetty_anon_215_9),
    ('scroll', _yetty_anon_218_9),
    ('key', _yetty_anon_221_9),
]

ygui_event_t._fields_ = [
    ('widget_id', POINTER(c_char)),
    ('type', c_int),
    ('data', _yetty_anon_210_5),
]

# ---------------------------------------------------------------------------
# Function bindings
# ---------------------------------------------------------------------------

def _bind_functions() -> None:
    """Wire argtypes/restype on every function. Called by load()."""
    if _lib is None:
        return
    _lib.yetty_ygui_init.argtypes = []
    _lib.yetty_ygui_init.restype = c_int
    _lib.yetty_ygui_shutdown.argtypes = []
    _lib.yetty_ygui_shutdown.restype = None
    _lib.yetty_ygui_engine_create.argtypes = [POINTER(c_char), c_int, c_int, c_int, c_int]
    _lib.yetty_ygui_engine_create.restype = ygui_engine_ptr_result
    _lib.yetty_ygui_engine_create_with_pixel_hint.argtypes = [POINTER(c_char), c_int, c_int, c_float, c_float]
    _lib.yetty_ygui_engine_create_with_pixel_hint.restype = ygui_engine_ptr_result
    _lib.yetty_ygui_engine_destroy.argtypes = [POINTER(yetty_ygui_engine)]
    _lib.yetty_ygui_engine_destroy.restype = yetty_ycore_void_result
    _lib.yetty_ygui_engine_show.argtypes = [POINTER(yetty_ygui_engine)]
    _lib.yetty_ygui_engine_show.restype = yetty_ycore_void_result
    _lib.yetty_ygui_engine_render.argtypes = [POINTER(yetty_ygui_engine)]
    _lib.yetty_ygui_engine_render.restype = yetty_ycore_void_result
    _lib.yetty_ygui_engine_layout.argtypes = [POINTER(yetty_ygui_engine)]
    _lib.yetty_ygui_engine_layout.restype = yetty_ycore_void_result
    _lib.yetty_ygui_engine_attach.argtypes = [POINTER(yetty_ygui_engine), POINTER(uv_loop_s)]
    _lib.yetty_ygui_engine_attach.restype = None
    _lib.yetty_ygui_engine_run.argtypes = [POINTER(yetty_ygui_engine)]
    _lib.yetty_ygui_engine_run.restype = None
    _lib.yetty_ygui_engine_stop.argtypes = [POINTER(yetty_ygui_engine)]
    _lib.yetty_ygui_engine_stop.restype = None
    _lib.yetty_ygui_engine_set_size.argtypes = [POINTER(yetty_ygui_engine), c_float, c_float]
    _lib.yetty_ygui_engine_set_size.restype = None
    _lib.yetty_ygui_engine_get_size.argtypes = [POINTER(yetty_ygui_engine), POINTER(c_float), POINTER(c_float)]
    _lib.yetty_ygui_engine_get_size.restype = None
    _lib.yetty_ygui_engine_set_theme.argtypes = [POINTER(yetty_ygui_engine), POINTER(yetty_ygui_theme)]
    _lib.yetty_ygui_engine_set_theme.restype = None
    _lib.yetty_ygui_engine_on_key.argtypes = [POINTER(yetty_ygui_engine), POINTER(CFUNCTYPE(None, POINTER(yetty_ygui_engine), c_uint, c_int, c_void_p)), c_void_p]
    _lib.yetty_ygui_engine_on_key.restype = None
    _lib.yetty_ygui_engine_set_event_callback.argtypes = [POINTER(yetty_ygui_engine), POINTER(CFUNCTYPE(None, POINTER(ygui_event_t), c_void_p)), c_void_p]
    _lib.yetty_ygui_engine_set_event_callback.restype = None
    _lib.yetty_ygui_engine_is_dirty.argtypes = [POINTER(yetty_ygui_engine)]
    _lib.yetty_ygui_engine_is_dirty.restype = c_int
    _lib.yetty_ygui_engine_mark_dirty.argtypes = [POINTER(yetty_ygui_engine)]
    _lib.yetty_ygui_engine_mark_dirty.restype = None
    _lib.yetty_ygui_engine_set_canvas_mode.argtypes = [POINTER(yetty_ygui_engine), c_int]
    _lib.yetty_ygui_engine_set_canvas_mode.restype = None
    _lib.yetty_ygui_engine_set_scale_mode.argtypes = [POINTER(yetty_ygui_engine), c_int]
    _lib.yetty_ygui_engine_set_scale_mode.restype = None
    _lib.yetty_ygui_engine_on_resize.argtypes = [POINTER(yetty_ygui_engine), POINTER(CFUNCTYPE(None, POINTER(yetty_ygui_engine), c_float, c_float, c_float, c_float, c_void_p)), c_void_p]
    _lib.yetty_ygui_engine_on_resize.restype = None
    _lib.yetty_ygui_engine_get_zoom.argtypes = [POINTER(yetty_ygui_engine)]
    _lib.yetty_ygui_engine_get_zoom.restype = c_float
    _lib.yetty_ygui_engine_get_scroll_x.argtypes = [POINTER(yetty_ygui_engine)]
    _lib.yetty_ygui_engine_get_scroll_x.restype = c_float
    _lib.yetty_ygui_engine_get_scroll_y.argtypes = [POINTER(yetty_ygui_engine)]
    _lib.yetty_ygui_engine_get_scroll_y.restype = c_float
    _lib.yetty_ygui_engine_subscribe_view_changes.argtypes = [POINTER(yetty_ygui_engine), c_int]
    _lib.yetty_ygui_engine_subscribe_view_changes.restype = None
    _lib.yetty_ygui_engine_set_zoom.argtypes = [POINTER(yetty_ygui_engine), c_float]
    _lib.yetty_ygui_engine_set_zoom.restype = None
    _lib.yetty_ygui_engine_scroll_to.argtypes = [POINTER(yetty_ygui_engine), c_float, c_float]
    _lib.yetty_ygui_engine_scroll_to.restype = None
    _lib.yetty_ygui_engine_scroll_by.argtypes = [POINTER(yetty_ygui_engine), c_float, c_float]
    _lib.yetty_ygui_engine_scroll_by.restype = None
    _lib.yetty_ygui_engine_button.argtypes = [POINTER(yetty_ygui_engine), POINTER(c_char), c_float, c_float, c_float, c_float, POINTER(c_char)]
    _lib.yetty_ygui_engine_button.restype = POINTER(yetty_ygui_widget)
    _lib.yetty_ygui_engine_label.argtypes = [POINTER(yetty_ygui_engine), POINTER(c_char), c_float, c_float, POINTER(c_char)]
    _lib.yetty_ygui_engine_label.restype = POINTER(yetty_ygui_widget)
    _lib.yetty_ygui_engine_slider.argtypes = [POINTER(yetty_ygui_engine), POINTER(c_char), c_float, c_float, c_float, c_float, c_float, c_float, c_float]
    _lib.yetty_ygui_engine_slider.restype = POINTER(yetty_ygui_widget)
    _lib.yetty_ygui_engine_checkbox.argtypes = [POINTER(yetty_ygui_engine), POINTER(c_char), c_float, c_float, c_float, c_float, POINTER(c_char), c_int]
    _lib.yetty_ygui_engine_checkbox.restype = POINTER(yetty_ygui_widget)
    _lib.yetty_ygui_engine_textinput.argtypes = [POINTER(yetty_ygui_engine), POINTER(c_char), c_float, c_float, c_float, c_float, POINTER(c_char)]
    _lib.yetty_ygui_engine_textinput.restype = POINTER(yetty_ygui_widget)
    _lib.yetty_ygui_engine_panel.argtypes = [POINTER(yetty_ygui_engine), POINTER(c_char), c_float, c_float, c_float, c_float]
    _lib.yetty_ygui_engine_panel.restype = POINTER(yetty_ygui_widget)
    _lib.yetty_ygui_engine_hbox.argtypes = [POINTER(yetty_ygui_engine), POINTER(c_char), c_float, c_float, c_float, c_float]
    _lib.yetty_ygui_engine_hbox.restype = POINTER(yetty_ygui_widget)
    _lib.yetty_ygui_engine_vbox.argtypes = [POINTER(yetty_ygui_engine), POINTER(c_char), c_float, c_float, c_float, c_float]
    _lib.yetty_ygui_engine_vbox.restype = POINTER(yetty_ygui_widget)
    _lib.yetty_ygui_engine_dropdown.argtypes = [POINTER(yetty_ygui_engine), POINTER(c_char), c_float, c_float, c_float, c_float, POINTER(POINTER(c_char)), c_int]
    _lib.yetty_ygui_engine_dropdown.restype = POINTER(yetty_ygui_widget)
    _lib.yetty_ygui_engine_progress.argtypes = [POINTER(yetty_ygui_engine), POINTER(c_char), c_float, c_float, c_float, c_float, c_float]
    _lib.yetty_ygui_engine_progress.restype = POINTER(yetty_ygui_widget)
    _lib.yetty_ygui_engine_separator.argtypes = [POINTER(yetty_ygui_engine), POINTER(c_char), c_float, c_float, c_float, c_float]
    _lib.yetty_ygui_engine_separator.restype = POINTER(yetty_ygui_widget)
    _lib.yetty_ygui_engine_colorpicker.argtypes = [POINTER(yetty_ygui_engine), POINTER(c_char), c_float, c_float, c_float, c_float]
    _lib.yetty_ygui_engine_colorpicker.restype = POINTER(yetty_ygui_widget)
    _lib.yetty_ygui_engine_popup.argtypes = [POINTER(yetty_ygui_engine), POINTER(c_char), c_float, c_float, c_float, c_float, POINTER(c_char)]
    _lib.yetty_ygui_engine_popup.restype = POINTER(yetty_ygui_widget)
    _lib.yetty_ygui_engine_collapsing_header.argtypes = [POINTER(yetty_ygui_engine), POINTER(c_char), c_float, c_float, c_float, c_float, POINTER(c_char)]
    _lib.yetty_ygui_engine_collapsing_header.restype = POINTER(yetty_ygui_widget)
    _lib.yetty_ygui_engine_tooltip.argtypes = [POINTER(yetty_ygui_engine), POINTER(c_char), c_float, c_float, c_float, c_float, POINTER(c_char)]
    _lib.yetty_ygui_engine_tooltip.restype = POINTER(yetty_ygui_widget)
    _lib.yetty_ygui_engine_selectable.argtypes = [POINTER(yetty_ygui_engine), POINTER(c_char), c_float, c_float, c_float, c_float, POINTER(c_char)]
    _lib.yetty_ygui_engine_selectable.restype = POINTER(yetty_ygui_widget)
    _lib.yetty_ygui_engine_choicebox.argtypes = [POINTER(yetty_ygui_engine), POINTER(c_char), c_float, c_float, c_float, c_float, POINTER(POINTER(c_char)), c_int]
    _lib.yetty_ygui_engine_choicebox.restype = POINTER(yetty_ygui_widget)
    _lib.yetty_ygui_engine_vscrollbar.argtypes = [POINTER(yetty_ygui_engine), POINTER(c_char), c_float, c_float, c_float, c_float]
    _lib.yetty_ygui_engine_vscrollbar.restype = POINTER(yetty_ygui_widget)
    _lib.yetty_ygui_engine_hscrollbar.argtypes = [POINTER(yetty_ygui_engine), POINTER(c_char), c_float, c_float, c_float, c_float]
    _lib.yetty_ygui_engine_hscrollbar.restype = POINTER(yetty_ygui_widget)
    _lib.yetty_ygui_engine_list.argtypes = [POINTER(yetty_ygui_engine), POINTER(c_char), c_float, c_float, c_float, c_float]
    _lib.yetty_ygui_engine_list.restype = POINTER(yetty_ygui_widget)
    _lib.yetty_ygui_engine_tree_node.argtypes = [POINTER(yetty_ygui_engine), POINTER(c_char), POINTER(c_char)]
    _lib.yetty_ygui_engine_tree_node.restype = POINTER(yetty_ygui_widget)
    _lib.yetty_ygui_widget_button_on_click.argtypes = [POINTER(yetty_ygui_widget), POINTER(CFUNCTYPE(None, POINTER(yetty_ygui_widget), c_void_p)), c_void_p]
    _lib.yetty_ygui_widget_button_on_click.restype = None
    _lib.yetty_ygui_widget_slider_on_change.argtypes = [POINTER(yetty_ygui_widget), POINTER(CFUNCTYPE(None, POINTER(yetty_ygui_widget), c_float, c_void_p)), c_void_p]
    _lib.yetty_ygui_widget_slider_on_change.restype = None
    _lib.yetty_ygui_widget_checkbox_on_change.argtypes = [POINTER(yetty_ygui_widget), POINTER(CFUNCTYPE(None, POINTER(yetty_ygui_widget), c_int, c_void_p)), c_void_p]
    _lib.yetty_ygui_widget_checkbox_on_change.restype = None
    _lib.yetty_ygui_widget_textinput_on_change.argtypes = [POINTER(yetty_ygui_widget), POINTER(CFUNCTYPE(None, POINTER(yetty_ygui_widget), POINTER(c_char), c_void_p)), c_void_p]
    _lib.yetty_ygui_widget_textinput_on_change.restype = None
    _lib.yetty_ygui_widget_add_child.argtypes = [POINTER(yetty_ygui_widget), POINTER(yetty_ygui_widget)]
    _lib.yetty_ygui_widget_add_child.restype = None
    _lib.yetty_ygui_widget_remove_child.argtypes = [POINTER(yetty_ygui_widget), POINTER(yetty_ygui_widget)]
    _lib.yetty_ygui_widget_remove_child.restype = None
    _lib.yetty_ygui_widget_remove.argtypes = [POINTER(yetty_ygui_widget)]
    _lib.yetty_ygui_widget_remove.restype = None
    _lib.yetty_ygui_widget_parent.argtypes = [POINTER(yetty_ygui_widget)]
    _lib.yetty_ygui_widget_parent.restype = POINTER(yetty_ygui_widget)
    _lib.yetty_ygui_widget_first_child.argtypes = [POINTER(yetty_ygui_widget)]
    _lib.yetty_ygui_widget_first_child.restype = POINTER(yetty_ygui_widget)
    _lib.yetty_ygui_widget_next_sibling.argtypes = [POINTER(yetty_ygui_widget)]
    _lib.yetty_ygui_widget_next_sibling.restype = POINTER(yetty_ygui_widget)
    _lib.yetty_ygui_widget_id.argtypes = [POINTER(yetty_ygui_widget)]
    _lib.yetty_ygui_widget_id.restype = POINTER(c_char)
    _lib.yetty_ygui_widget_type.argtypes = [POINTER(yetty_ygui_widget)]
    _lib.yetty_ygui_widget_type.restype = c_int
    _lib.yetty_ygui_widget_set_position.argtypes = [POINTER(yetty_ygui_widget), c_float, c_float]
    _lib.yetty_ygui_widget_set_position.restype = None
    _lib.yetty_ygui_widget_get_position.argtypes = [POINTER(yetty_ygui_widget), POINTER(c_float), POINTER(c_float)]
    _lib.yetty_ygui_widget_get_position.restype = None
    _lib.yetty_ygui_widget_set_size.argtypes = [POINTER(yetty_ygui_widget), c_float, c_float]
    _lib.yetty_ygui_widget_set_size.restype = None
    _lib.yetty_ygui_widget_get_size.argtypes = [POINTER(yetty_ygui_widget), POINTER(c_float), POINTER(c_float)]
    _lib.yetty_ygui_widget_get_size.restype = None
    _lib.yetty_ygui_widget_get_layout_box.argtypes = [POINTER(yetty_ygui_widget), POINTER(c_float), POINTER(c_float), POINTER(c_float), POINTER(c_float)]
    _lib.yetty_ygui_widget_get_layout_box.restype = None
    _lib.yetty_ygui_widget_set_visible.argtypes = [POINTER(yetty_ygui_widget), c_int]
    _lib.yetty_ygui_widget_set_visible.restype = None
    _lib.yetty_ygui_widget_is_visible.argtypes = [POINTER(yetty_ygui_widget)]
    _lib.yetty_ygui_widget_is_visible.restype = c_int
    _lib.yetty_ygui_widget_set_enabled.argtypes = [POINTER(yetty_ygui_widget), c_int]
    _lib.yetty_ygui_widget_set_enabled.restype = None
    _lib.yetty_ygui_widget_is_enabled.argtypes = [POINTER(yetty_ygui_widget)]
    _lib.yetty_ygui_widget_is_enabled.restype = c_int
    _lib.yetty_ygui_widget_get_flags.argtypes = [POINTER(yetty_ygui_widget)]
    _lib.yetty_ygui_widget_get_flags.restype = c_uint
    _lib.yetty_ygui_widget_set_bg_color.argtypes = [POINTER(yetty_ygui_widget), c_uint]
    _lib.yetty_ygui_widget_set_bg_color.restype = None
    _lib.yetty_ygui_widget_set_fg_color.argtypes = [POINTER(yetty_ygui_widget), c_uint]
    _lib.yetty_ygui_widget_set_fg_color.restype = None
    _lib.yetty_ygui_widget_set_accent_color.argtypes = [POINTER(yetty_ygui_widget), c_uint]
    _lib.yetty_ygui_widget_set_accent_color.restype = None
    _lib.yetty_ygui_widget_set_layout_mode.argtypes = [POINTER(yetty_ygui_widget), c_int]
    _lib.yetty_ygui_widget_set_layout_mode.restype = None
    _lib.yetty_ygui_widget_set_flex_direction.argtypes = [POINTER(yetty_ygui_widget), c_int]
    _lib.yetty_ygui_widget_set_flex_direction.restype = None
    _lib.yetty_ygui_widget_set_justify_content.argtypes = [POINTER(yetty_ygui_widget), c_int]
    _lib.yetty_ygui_widget_set_justify_content.restype = None
    _lib.yetty_ygui_widget_set_align_items.argtypes = [POINTER(yetty_ygui_widget), c_int]
    _lib.yetty_ygui_widget_set_align_items.restype = None
    _lib.yetty_ygui_widget_set_align_self.argtypes = [POINTER(yetty_ygui_widget), c_int]
    _lib.yetty_ygui_widget_set_align_self.restype = None
    _lib.yetty_ygui_widget_set_flex.argtypes = [POINTER(yetty_ygui_widget), c_float, c_float, c_float]
    _lib.yetty_ygui_widget_set_flex.restype = None
    _lib.yetty_ygui_widget_set_gap.argtypes = [POINTER(yetty_ygui_widget), c_float]
    _lib.yetty_ygui_widget_set_gap.restype = None
    _lib.yetty_ygui_widget_set_padding.argtypes = [POINTER(yetty_ygui_widget), c_float, c_float, c_float, c_float]
    _lib.yetty_ygui_widget_set_padding.restype = None
    _lib.yetty_ygui_widget_set_margin.argtypes = [POINTER(yetty_ygui_widget), c_float, c_float, c_float, c_float]
    _lib.yetty_ygui_widget_set_margin.restype = None
    _lib.yetty_ygui_widget_set_min_size.argtypes = [POINTER(yetty_ygui_widget), c_float, c_float]
    _lib.yetty_ygui_widget_set_min_size.restype = None
    _lib.yetty_ygui_widget_set_max_size.argtypes = [POINTER(yetty_ygui_widget), c_float, c_float]
    _lib.yetty_ygui_widget_set_max_size.restype = None
    _lib.yetty_ygui_widget_set_flex_wrap.argtypes = [POINTER(yetty_ygui_widget), c_int]
    _lib.yetty_ygui_widget_set_flex_wrap.restype = None
    _lib.yetty_ygui_widget_set_align_content.argtypes = [POINTER(yetty_ygui_widget), c_int]
    _lib.yetty_ygui_widget_set_align_content.restype = None
    _lib.yetty_ygui_widget_set_position_mode.argtypes = [POINTER(yetty_ygui_widget), c_int]
    _lib.yetty_ygui_widget_set_position_mode.restype = None
    _lib.yetty_ygui_widget_set_flex_basis_percent.argtypes = [POINTER(yetty_ygui_widget), c_float]
    _lib.yetty_ygui_widget_set_flex_basis_percent.restype = None
    _lib.yetty_ygui_widget_set_size_percent.argtypes = [POINTER(yetty_ygui_widget), c_float, c_float]
    _lib.yetty_ygui_widget_set_size_percent.restype = None
    _lib.yetty_ygui_widget_set_min_size_percent.argtypes = [POINTER(yetty_ygui_widget), c_float, c_float]
    _lib.yetty_ygui_widget_set_min_size_percent.restype = None
    _lib.yetty_ygui_widget_set_max_size_percent.argtypes = [POINTER(yetty_ygui_widget), c_float, c_float]
    _lib.yetty_ygui_widget_set_max_size_percent.restype = None
    _lib.yetty_ygui_widget_apply_css.argtypes = [POINTER(yetty_ygui_widget), POINTER(c_char)]
    _lib.yetty_ygui_widget_apply_css.restype = yetty_ycore_void_result
    _lib.yetty_ygui_widget_button_set_label.argtypes = [POINTER(yetty_ygui_widget), POINTER(c_char)]
    _lib.yetty_ygui_widget_button_set_label.restype = None
    _lib.yetty_ygui_widget_button_get_label.argtypes = [POINTER(yetty_ygui_widget)]
    _lib.yetty_ygui_widget_button_get_label.restype = POINTER(c_char)
    _lib.yetty_ygui_widget_label_set_text.argtypes = [POINTER(yetty_ygui_widget), POINTER(c_char)]
    _lib.yetty_ygui_widget_label_set_text.restype = None
    _lib.yetty_ygui_widget_label_get_text.argtypes = [POINTER(yetty_ygui_widget)]
    _lib.yetty_ygui_widget_label_get_text.restype = POINTER(c_char)
    _lib.yetty_ygui_widget_label_set_font_size.argtypes = [POINTER(yetty_ygui_widget), c_float]
    _lib.yetty_ygui_widget_label_set_font_size.restype = None
    _lib.yetty_ygui_widget_slider_set_value.argtypes = [POINTER(yetty_ygui_widget), c_float]
    _lib.yetty_ygui_widget_slider_set_value.restype = None
    _lib.yetty_ygui_widget_slider_get_value.argtypes = [POINTER(yetty_ygui_widget)]
    _lib.yetty_ygui_widget_slider_get_value.restype = c_float
    _lib.yetty_ygui_widget_slider_set_range.argtypes = [POINTER(yetty_ygui_widget), c_float, c_float]
    _lib.yetty_ygui_widget_slider_set_range.restype = None
    _lib.yetty_ygui_widget_checkbox_set_checked.argtypes = [POINTER(yetty_ygui_widget), c_int]
    _lib.yetty_ygui_widget_checkbox_set_checked.restype = None
    _lib.yetty_ygui_widget_checkbox_get_checked.argtypes = [POINTER(yetty_ygui_widget)]
    _lib.yetty_ygui_widget_checkbox_get_checked.restype = c_int
    _lib.yetty_ygui_widget_checkbox_set_label.argtypes = [POINTER(yetty_ygui_widget), POINTER(c_char)]
    _lib.yetty_ygui_widget_checkbox_set_label.restype = None
    _lib.yetty_ygui_widget_textinput_set_text.argtypes = [POINTER(yetty_ygui_widget), POINTER(c_char)]
    _lib.yetty_ygui_widget_textinput_set_text.restype = None
    _lib.yetty_ygui_widget_textinput_get_text.argtypes = [POINTER(yetty_ygui_widget)]
    _lib.yetty_ygui_widget_textinput_get_text.restype = POINTER(c_char)
    _lib.yetty_ygui_widget_textinput_set_placeholder.argtypes = [POINTER(yetty_ygui_widget), POINTER(c_char)]
    _lib.yetty_ygui_widget_textinput_set_placeholder.restype = None
    _lib.yetty_ygui_widget_panel_set_scroll.argtypes = [POINTER(yetty_ygui_widget), c_float, c_float]
    _lib.yetty_ygui_widget_panel_set_scroll.restype = None
    _lib.yetty_ygui_widget_panel_get_scroll.argtypes = [POINTER(yetty_ygui_widget), POINTER(c_float), POINTER(c_float)]
    _lib.yetty_ygui_widget_panel_get_scroll.restype = None
    _lib.yetty_ygui_widget_panel_set_content_size.argtypes = [POINTER(yetty_ygui_widget), c_float, c_float]
    _lib.yetty_ygui_widget_panel_set_content_size.restype = None
    _lib.yetty_ygui_widget_panel_set_header_height.argtypes = [POINTER(yetty_ygui_widget), c_float]
    _lib.yetty_ygui_widget_panel_set_header_height.restype = None
    _lib.yetty_ygui_widget_progress_set_value.argtypes = [POINTER(yetty_ygui_widget), c_float]
    _lib.yetty_ygui_widget_progress_set_value.restype = None
    _lib.yetty_ygui_widget_progress_get_value.argtypes = [POINTER(yetty_ygui_widget)]
    _lib.yetty_ygui_widget_progress_get_value.restype = c_float
    _lib.yetty_ygui_widget_dropdown_set_options.argtypes = [POINTER(yetty_ygui_widget), POINTER(POINTER(c_char)), c_int]
    _lib.yetty_ygui_widget_dropdown_set_options.restype = None
    _lib.yetty_ygui_widget_dropdown_set_selected.argtypes = [POINTER(yetty_ygui_widget), c_int]
    _lib.yetty_ygui_widget_dropdown_set_selected.restype = None
    _lib.yetty_ygui_widget_dropdown_get_selected.argtypes = [POINTER(yetty_ygui_widget)]
    _lib.yetty_ygui_widget_dropdown_get_selected.restype = c_int
    _lib.yetty_ygui_widget_colorpicker_set_color.argtypes = [POINTER(yetty_ygui_widget), c_float, c_float, c_float, c_float]
    _lib.yetty_ygui_widget_colorpicker_set_color.restype = None
    _lib.yetty_ygui_widget_colorpicker_get_color.argtypes = [POINTER(yetty_ygui_widget), POINTER(c_float), POINTER(c_float), POINTER(c_float), POINTER(c_float)]
    _lib.yetty_ygui_widget_colorpicker_get_color.restype = None
    _lib.yetty_ygui_widget_popup_set_label.argtypes = [POINTER(yetty_ygui_widget), POINTER(c_char)]
    _lib.yetty_ygui_widget_popup_set_label.restype = None
    _lib.yetty_ygui_widget_popup_get_label.argtypes = [POINTER(yetty_ygui_widget)]
    _lib.yetty_ygui_widget_popup_get_label.restype = POINTER(c_char)
    _lib.yetty_ygui_widget_popup_set_modal.argtypes = [POINTER(yetty_ygui_widget), c_int]
    _lib.yetty_ygui_widget_popup_set_modal.restype = None
    _lib.yetty_ygui_widget_popup_is_modal.argtypes = [POINTER(yetty_ygui_widget)]
    _lib.yetty_ygui_widget_popup_is_modal.restype = c_int
    _lib.yetty_ygui_widget_popup_set_open.argtypes = [POINTER(yetty_ygui_widget), c_int]
    _lib.yetty_ygui_widget_popup_set_open.restype = None
    _lib.yetty_ygui_widget_popup_is_open.argtypes = [POINTER(yetty_ygui_widget)]
    _lib.yetty_ygui_widget_popup_is_open.restype = c_int
    _lib.yetty_ygui_widget_popup_set_scene_size.argtypes = [POINTER(yetty_ygui_widget), c_float, c_float]
    _lib.yetty_ygui_widget_popup_set_scene_size.restype = None
    _lib.yetty_ygui_widget_popup_set_header_color.argtypes = [POINTER(yetty_ygui_widget), c_uint]
    _lib.yetty_ygui_widget_popup_set_header_color.restype = None
    _lib.yetty_ygui_widget_collapsing_header_set_label.argtypes = [POINTER(yetty_ygui_widget), POINTER(c_char)]
    _lib.yetty_ygui_widget_collapsing_header_set_label.restype = None
    _lib.yetty_ygui_widget_collapsing_header_get_label.argtypes = [POINTER(yetty_ygui_widget)]
    _lib.yetty_ygui_widget_collapsing_header_get_label.restype = POINTER(c_char)
    _lib.yetty_ygui_widget_collapsing_header_set_open.argtypes = [POINTER(yetty_ygui_widget), c_int]
    _lib.yetty_ygui_widget_collapsing_header_set_open.restype = None
    _lib.yetty_ygui_widget_collapsing_header_is_open.argtypes = [POINTER(yetty_ygui_widget)]
    _lib.yetty_ygui_widget_collapsing_header_is_open.restype = c_int
    _lib.yetty_ygui_widget_tooltip_set_label.argtypes = [POINTER(yetty_ygui_widget), POINTER(c_char)]
    _lib.yetty_ygui_widget_tooltip_set_label.restype = None
    _lib.yetty_ygui_widget_tooltip_get_label.argtypes = [POINTER(yetty_ygui_widget)]
    _lib.yetty_ygui_widget_tooltip_get_label.restype = POINTER(c_char)
    _lib.yetty_ygui_widget_selectable_set_label.argtypes = [POINTER(yetty_ygui_widget), POINTER(c_char)]
    _lib.yetty_ygui_widget_selectable_set_label.restype = None
    _lib.yetty_ygui_widget_selectable_get_label.argtypes = [POINTER(yetty_ygui_widget)]
    _lib.yetty_ygui_widget_selectable_get_label.restype = POINTER(c_char)
    _lib.yetty_ygui_widget_selectable_set_checked.argtypes = [POINTER(yetty_ygui_widget), c_int]
    _lib.yetty_ygui_widget_selectable_set_checked.restype = None
    _lib.yetty_ygui_widget_selectable_is_checked.argtypes = [POINTER(yetty_ygui_widget)]
    _lib.yetty_ygui_widget_selectable_is_checked.restype = c_int
    _lib.yetty_ygui_widget_choicebox_set_options.argtypes = [POINTER(yetty_ygui_widget), POINTER(POINTER(c_char)), c_int]
    _lib.yetty_ygui_widget_choicebox_set_options.restype = None
    _lib.yetty_ygui_widget_choicebox_set_selected.argtypes = [POINTER(yetty_ygui_widget), c_int]
    _lib.yetty_ygui_widget_choicebox_set_selected.restype = None
    _lib.yetty_ygui_widget_choicebox_get_selected.argtypes = [POINTER(yetty_ygui_widget)]
    _lib.yetty_ygui_widget_choicebox_get_selected.restype = c_int
    _lib.yetty_ygui_widget_scrollbar_set_value.argtypes = [POINTER(yetty_ygui_widget), c_float]
    _lib.yetty_ygui_widget_scrollbar_set_value.restype = None
    _lib.yetty_ygui_widget_scrollbar_get_value.argtypes = [POINTER(yetty_ygui_widget)]
    _lib.yetty_ygui_widget_scrollbar_get_value.restype = c_float
    _lib.yetty_ygui_widget_list_set_selected.argtypes = [POINTER(yetty_ygui_widget), POINTER(yetty_ygui_widget)]
    _lib.yetty_ygui_widget_list_set_selected.restype = None
    _lib.yetty_ygui_widget_list_get_selected.argtypes = [POINTER(yetty_ygui_widget)]
    _lib.yetty_ygui_widget_list_get_selected.restype = POINTER(yetty_ygui_widget)
    _lib.yetty_ygui_widget_list_on_select.argtypes = [POINTER(yetty_ygui_widget), POINTER(CFUNCTYPE(None, POINTER(yetty_ygui_widget), c_void_p)), c_void_p]
    _lib.yetty_ygui_widget_list_on_select.restype = None
    _lib.yetty_ygui_widget_tree_node_set_label.argtypes = [POINTER(yetty_ygui_widget), POINTER(c_char)]
    _lib.yetty_ygui_widget_tree_node_set_label.restype = None
    _lib.yetty_ygui_widget_tree_node_get_label.argtypes = [POINTER(yetty_ygui_widget)]
    _lib.yetty_ygui_widget_tree_node_get_label.restype = POINTER(c_char)
    _lib.yetty_ygui_widget_tree_node_set_expanded.argtypes = [POINTER(yetty_ygui_widget), c_int]
    _lib.yetty_ygui_widget_tree_node_set_expanded.restype = None
    _lib.yetty_ygui_widget_tree_node_is_expanded.argtypes = [POINTER(yetty_ygui_widget)]
    _lib.yetty_ygui_widget_tree_node_is_expanded.restype = c_int
    _lib.yetty_ygui_widget_tree_node_children.argtypes = [POINTER(yetty_ygui_widget)]
    _lib.yetty_ygui_widget_tree_node_children.restype = POINTER(yetty_ygui_widget)
    _lib.yetty_ygui_widget_tree_node_on_toggle.argtypes = [POINTER(yetty_ygui_widget), POINTER(CFUNCTYPE(None, POINTER(yetty_ygui_widget), c_int, c_void_p)), c_void_p]
    _lib.yetty_ygui_widget_tree_node_on_toggle.restype = None
    _lib.yetty_ygui_engine_find.argtypes = [POINTER(yetty_ygui_engine), POINTER(c_char)]
    _lib.yetty_ygui_engine_find.restype = POINTER(yetty_ygui_widget)
    _lib.yetty_ygui_engine_widget_at.argtypes = [POINTER(yetty_ygui_engine), c_float, c_float]
    _lib.yetty_ygui_engine_widget_at.restype = POINTER(yetty_ygui_widget)
    _lib.yetty_ygui_theme_create.argtypes = []
    _lib.yetty_ygui_theme_create.restype = POINTER(yetty_ygui_theme)
    _lib.yetty_ygui_theme_create_default.argtypes = []
    _lib.yetty_ygui_theme_create_default.restype = POINTER(yetty_ygui_theme)
    _lib.yetty_ygui_theme_destroy.argtypes = [POINTER(yetty_ygui_theme)]
    _lib.yetty_ygui_theme_destroy.restype = None
    _lib.yetty_ygui_theme_set_padding.argtypes = [POINTER(yetty_ygui_theme), c_float, c_float, c_float]
    _lib.yetty_ygui_theme_set_padding.restype = None
    _lib.yetty_ygui_theme_set_radius.argtypes = [POINTER(yetty_ygui_theme), c_float, c_float, c_float]
    _lib.yetty_ygui_theme_set_radius.restype = None
    _lib.yetty_ygui_theme_set_row_height.argtypes = [POINTER(yetty_ygui_theme), c_float]
    _lib.yetty_ygui_theme_set_row_height.restype = None
    _lib.yetty_ygui_theme_set_font_size.argtypes = [POINTER(yetty_ygui_theme), c_float]
    _lib.yetty_ygui_theme_set_font_size.restype = None
    _lib.yetty_ygui_theme_set_scrollbar_size.argtypes = [POINTER(yetty_ygui_theme), c_float]
    _lib.yetty_ygui_theme_set_scrollbar_size.restype = None
    _lib.yetty_ygui_theme_set_bg_primary.argtypes = [POINTER(yetty_ygui_theme), c_uint]
    _lib.yetty_ygui_theme_set_bg_primary.restype = None
    _lib.yetty_ygui_theme_set_bg_surface.argtypes = [POINTER(yetty_ygui_theme), c_uint]
    _lib.yetty_ygui_theme_set_bg_surface.restype = None
    _lib.yetty_ygui_theme_set_bg_hover.argtypes = [POINTER(yetty_ygui_theme), c_uint]
    _lib.yetty_ygui_theme_set_bg_hover.restype = None
    _lib.yetty_ygui_theme_set_text_primary.argtypes = [POINTER(yetty_ygui_theme), c_uint]
    _lib.yetty_ygui_theme_set_text_primary.restype = None
    _lib.yetty_ygui_theme_set_text_muted.argtypes = [POINTER(yetty_ygui_theme), c_uint]
    _lib.yetty_ygui_theme_set_text_muted.restype = None
    _lib.yetty_ygui_theme_set_accent.argtypes = [POINTER(yetty_ygui_theme), c_uint]
    _lib.yetty_ygui_theme_set_accent.restype = None
    _lib.yetty_ygui_theme_set_border.argtypes = [POINTER(yetty_ygui_theme), c_uint]
    _lib.yetty_ygui_theme_set_border.restype = None
    _lib.yetty_ygui_theme_set_border_muted.argtypes = [POINTER(yetty_ygui_theme), c_uint]
    _lib.yetty_ygui_theme_set_border_muted.restype = None
    _lib.yetty_ygui_theme_set_bg_dropdown.argtypes = [POINTER(yetty_ygui_theme), c_uint]
    _lib.yetty_ygui_theme_set_bg_dropdown.restype = None
    _lib.yetty_ygui_theme_set_overlay_modal.argtypes = [POINTER(yetty_ygui_theme), c_uint]
    _lib.yetty_ygui_theme_set_overlay_modal.restype = None
    _lib.yetty_ygui_theme_set_shadow.argtypes = [POINTER(yetty_ygui_theme), c_uint]
    _lib.yetty_ygui_theme_set_shadow.restype = None
    _lib.yetty_ygui_theme_set_tooltip_bg.argtypes = [POINTER(yetty_ygui_theme), c_uint]
    _lib.yetty_ygui_theme_set_tooltip_bg.restype = None
    _lib.yetty_ygui_theme_set_selection_bg.argtypes = [POINTER(yetty_ygui_theme), c_uint]
    _lib.yetty_ygui_theme_set_selection_bg.restype = None
    _lib.yetty_ygui_theme_set_elevation.argtypes = [POINTER(yetty_ygui_theme), c_float, c_float, c_float, c_float]
    _lib.yetty_ygui_theme_set_elevation.restype = None
    _lib.yetty_ygui_theme_set_gradient.argtypes = [POINTER(yetty_ygui_theme), c_int]
    _lib.yetty_ygui_theme_set_gradient.restype = None
    _lib.yetty_ygui_engine_set_input_fd.argtypes = [POINTER(yetty_ygui_engine), c_int]
    _lib.yetty_ygui_engine_set_input_fd.restype = None
    _lib.yetty_ygui_engine_set_output_fd.argtypes = [POINTER(yetty_ygui_engine), c_int]
    _lib.yetty_ygui_engine_set_output_fd.restype = None
    _lib.yetty_ygui_engine_set_card_size.argtypes = [POINTER(yetty_ygui_engine), c_int, c_int]
    _lib.yetty_ygui_engine_set_card_size.restype = None
    _lib.yetty_ygui_engine_set_display_pixel_size.argtypes = [POINTER(yetty_ygui_engine), c_float, c_float]
    _lib.yetty_ygui_engine_set_display_pixel_size.restype = None
    _lib.yetty_ygui_engine_get_loop.argtypes = [POINTER(yetty_ygui_engine)]
    _lib.yetty_ygui_engine_get_loop.restype = POINTER(uv_loop_s)
    _lib.yetty_ygui_engine_poll.argtypes = [POINTER(yetty_ygui_engine)]
    _lib.yetty_ygui_engine_poll.restype = c_int
    _lib.yetty_ygui_get_error.argtypes = []
    _lib.yetty_ygui_get_error.restype = POINTER(c_char)
    _lib.yetty_ygui_version.argtypes = []
    _lib.yetty_ygui_version.restype = POINTER(c_char)

def yetty_ygui_init():
    return _check_loaded().yetty_ygui_init()

def yetty_ygui_shutdown():
    return _check_loaded().yetty_ygui_shutdown()

def yetty_ygui_engine_create(card_name, x, y, cols, rows):
    return _check_loaded().yetty_ygui_engine_create(card_name, x, y, cols, rows)

def yetty_ygui_engine_create_with_pixel_hint(card_name, x, y, width_hint, height_hint):
    return _check_loaded().yetty_ygui_engine_create_with_pixel_hint(card_name, x, y, width_hint, height_hint)

def yetty_ygui_engine_destroy(engine):
    return _check_loaded().yetty_ygui_engine_destroy(engine)

def yetty_ygui_engine_show(engine):
    return _check_loaded().yetty_ygui_engine_show(engine)

def yetty_ygui_engine_render(engine):
    return _check_loaded().yetty_ygui_engine_render(engine)

def yetty_ygui_engine_layout(engine):
    return _check_loaded().yetty_ygui_engine_layout(engine)

def yetty_ygui_engine_attach(engine, loop):
    return _check_loaded().yetty_ygui_engine_attach(engine, loop)

def yetty_ygui_engine_run(engine):
    return _check_loaded().yetty_ygui_engine_run(engine)

def yetty_ygui_engine_stop(engine):
    return _check_loaded().yetty_ygui_engine_stop(engine)

def yetty_ygui_engine_set_size(engine, width, height):
    return _check_loaded().yetty_ygui_engine_set_size(engine, width, height)

def yetty_ygui_engine_get_size(engine, width, height):
    return _check_loaded().yetty_ygui_engine_get_size(engine, width, height)

def yetty_ygui_engine_set_theme(engine, theme):
    return _check_loaded().yetty_ygui_engine_set_theme(engine, theme)

def yetty_ygui_engine_on_key(engine, callback, userdata):
    return _check_loaded().yetty_ygui_engine_on_key(engine, callback, userdata)

def yetty_ygui_engine_set_event_callback(engine, callback, userdata):
    return _check_loaded().yetty_ygui_engine_set_event_callback(engine, callback, userdata)

def yetty_ygui_engine_is_dirty(engine):
    return _check_loaded().yetty_ygui_engine_is_dirty(engine)

def yetty_ygui_engine_mark_dirty(engine):
    return _check_loaded().yetty_ygui_engine_mark_dirty(engine)

def yetty_ygui_engine_set_canvas_mode(engine, mode):
    return _check_loaded().yetty_ygui_engine_set_canvas_mode(engine, mode)

def yetty_ygui_engine_set_scale_mode(engine, mode):
    return _check_loaded().yetty_ygui_engine_set_scale_mode(engine, mode)

def yetty_ygui_engine_on_resize(engine, callback, userdata):
    return _check_loaded().yetty_ygui_engine_on_resize(engine, callback, userdata)

def yetty_ygui_engine_get_zoom(engine):
    return _check_loaded().yetty_ygui_engine_get_zoom(engine)

def yetty_ygui_engine_get_scroll_x(engine):
    return _check_loaded().yetty_ygui_engine_get_scroll_x(engine)

def yetty_ygui_engine_get_scroll_y(engine):
    return _check_loaded().yetty_ygui_engine_get_scroll_y(engine)

def yetty_ygui_engine_subscribe_view_changes(engine, enable):
    return _check_loaded().yetty_ygui_engine_subscribe_view_changes(engine, enable)

def yetty_ygui_engine_set_zoom(engine, level):
    return _check_loaded().yetty_ygui_engine_set_zoom(engine, level)

def yetty_ygui_engine_scroll_to(engine, x, y):
    return _check_loaded().yetty_ygui_engine_scroll_to(engine, x, y)

def yetty_ygui_engine_scroll_by(engine, dx, dy):
    return _check_loaded().yetty_ygui_engine_scroll_by(engine, dx, dy)

def yetty_ygui_engine_button(engine, id, x, y, w, h, label):
    return _check_loaded().yetty_ygui_engine_button(engine, id, x, y, w, h, label)

def yetty_ygui_engine_label(engine, id, x, y, text):
    return _check_loaded().yetty_ygui_engine_label(engine, id, x, y, text)

def yetty_ygui_engine_slider(engine, id, x, y, w, h, min_val, max_val, value):
    return _check_loaded().yetty_ygui_engine_slider(engine, id, x, y, w, h, min_val, max_val, value)

def yetty_ygui_engine_checkbox(engine, id, x, y, w, h, label, checked):
    return _check_loaded().yetty_ygui_engine_checkbox(engine, id, x, y, w, h, label, checked)

def yetty_ygui_engine_textinput(engine, id, x, y, w, h, placeholder):
    return _check_loaded().yetty_ygui_engine_textinput(engine, id, x, y, w, h, placeholder)

def yetty_ygui_engine_panel(engine, id, x, y, w, h):
    return _check_loaded().yetty_ygui_engine_panel(engine, id, x, y, w, h)

def yetty_ygui_engine_hbox(engine, id, x, y, w, h):
    return _check_loaded().yetty_ygui_engine_hbox(engine, id, x, y, w, h)

def yetty_ygui_engine_vbox(engine, id, x, y, w, h):
    return _check_loaded().yetty_ygui_engine_vbox(engine, id, x, y, w, h)

def yetty_ygui_engine_dropdown(engine, id, x, y, w, h, options, option_count):
    return _check_loaded().yetty_ygui_engine_dropdown(engine, id, x, y, w, h, options, option_count)

def yetty_ygui_engine_progress(engine, id, x, y, w, h, value):
    return _check_loaded().yetty_ygui_engine_progress(engine, id, x, y, w, h, value)

def yetty_ygui_engine_separator(engine, id, x, y, w, h):
    return _check_loaded().yetty_ygui_engine_separator(engine, id, x, y, w, h)

def yetty_ygui_engine_colorpicker(engine, id, x, y, w, h):
    return _check_loaded().yetty_ygui_engine_colorpicker(engine, id, x, y, w, h)

def yetty_ygui_engine_popup(engine, id, x, y, w, h, label):
    return _check_loaded().yetty_ygui_engine_popup(engine, id, x, y, w, h, label)

def yetty_ygui_engine_collapsing_header(engine, id, x, y, w, h, label):
    return _check_loaded().yetty_ygui_engine_collapsing_header(engine, id, x, y, w, h, label)

def yetty_ygui_engine_tooltip(engine, id, x, y, w, h, label):
    return _check_loaded().yetty_ygui_engine_tooltip(engine, id, x, y, w, h, label)

def yetty_ygui_engine_selectable(engine, id, x, y, w, h, label):
    return _check_loaded().yetty_ygui_engine_selectable(engine, id, x, y, w, h, label)

def yetty_ygui_engine_choicebox(engine, id, x, y, w, h, options, option_count):
    return _check_loaded().yetty_ygui_engine_choicebox(engine, id, x, y, w, h, options, option_count)

def yetty_ygui_engine_vscrollbar(engine, id, x, y, w, h):
    return _check_loaded().yetty_ygui_engine_vscrollbar(engine, id, x, y, w, h)

def yetty_ygui_engine_hscrollbar(engine, id, x, y, w, h):
    return _check_loaded().yetty_ygui_engine_hscrollbar(engine, id, x, y, w, h)

def yetty_ygui_engine_list(engine, id, x, y, w, h):
    return _check_loaded().yetty_ygui_engine_list(engine, id, x, y, w, h)

def yetty_ygui_engine_tree_node(engine, id, label):
    return _check_loaded().yetty_ygui_engine_tree_node(engine, id, label)

def yetty_ygui_widget_button_on_click(button, callback, userdata):
    return _check_loaded().yetty_ygui_widget_button_on_click(button, callback, userdata)

def yetty_ygui_widget_slider_on_change(slider, callback, userdata):
    return _check_loaded().yetty_ygui_widget_slider_on_change(slider, callback, userdata)

def yetty_ygui_widget_checkbox_on_change(checkbox, callback, userdata):
    return _check_loaded().yetty_ygui_widget_checkbox_on_change(checkbox, callback, userdata)

def yetty_ygui_widget_textinput_on_change(input, callback, userdata):
    return _check_loaded().yetty_ygui_widget_textinput_on_change(input, callback, userdata)

def yetty_ygui_widget_add_child(parent, child):
    return _check_loaded().yetty_ygui_widget_add_child(parent, child)

def yetty_ygui_widget_remove_child(parent, child):
    return _check_loaded().yetty_ygui_widget_remove_child(parent, child)

def yetty_ygui_widget_remove(widget):
    return _check_loaded().yetty_ygui_widget_remove(widget)

def yetty_ygui_widget_parent(widget):
    return _check_loaded().yetty_ygui_widget_parent(widget)

def yetty_ygui_widget_first_child(widget):
    return _check_loaded().yetty_ygui_widget_first_child(widget)

def yetty_ygui_widget_next_sibling(widget):
    return _check_loaded().yetty_ygui_widget_next_sibling(widget)

def yetty_ygui_widget_id(widget):
    return _check_loaded().yetty_ygui_widget_id(widget)

def yetty_ygui_widget_type(widget):
    return _check_loaded().yetty_ygui_widget_type(widget)

def yetty_ygui_widget_set_position(widget, x, y):
    return _check_loaded().yetty_ygui_widget_set_position(widget, x, y)

def yetty_ygui_widget_get_position(widget, x, y):
    return _check_loaded().yetty_ygui_widget_get_position(widget, x, y)

def yetty_ygui_widget_set_size(widget, w, h):
    return _check_loaded().yetty_ygui_widget_set_size(widget, w, h)

def yetty_ygui_widget_get_size(widget, w, h):
    return _check_loaded().yetty_ygui_widget_get_size(widget, w, h)

def yetty_ygui_widget_get_layout_box(widget, x, y, w, h):
    return _check_loaded().yetty_ygui_widget_get_layout_box(widget, x, y, w, h)

def yetty_ygui_widget_set_visible(widget, visible):
    return _check_loaded().yetty_ygui_widget_set_visible(widget, visible)

def yetty_ygui_widget_is_visible(widget):
    return _check_loaded().yetty_ygui_widget_is_visible(widget)

def yetty_ygui_widget_set_enabled(widget, enabled):
    return _check_loaded().yetty_ygui_widget_set_enabled(widget, enabled)

def yetty_ygui_widget_is_enabled(widget):
    return _check_loaded().yetty_ygui_widget_is_enabled(widget)

def yetty_ygui_widget_get_flags(widget):
    return _check_loaded().yetty_ygui_widget_get_flags(widget)

def yetty_ygui_widget_set_bg_color(widget, color):
    return _check_loaded().yetty_ygui_widget_set_bg_color(widget, color)

def yetty_ygui_widget_set_fg_color(widget, color):
    return _check_loaded().yetty_ygui_widget_set_fg_color(widget, color)

def yetty_ygui_widget_set_accent_color(widget, color):
    return _check_loaded().yetty_ygui_widget_set_accent_color(widget, color)

def yetty_ygui_widget_set_layout_mode(widget, mode):
    return _check_loaded().yetty_ygui_widget_set_layout_mode(widget, mode)

def yetty_ygui_widget_set_flex_direction(widget, direction):
    return _check_loaded().yetty_ygui_widget_set_flex_direction(widget, direction)

def yetty_ygui_widget_set_justify_content(widget, justify):
    return _check_loaded().yetty_ygui_widget_set_justify_content(widget, justify)

def yetty_ygui_widget_set_align_items(widget, align):
    return _check_loaded().yetty_ygui_widget_set_align_items(widget, align)

def yetty_ygui_widget_set_align_self(widget, align):
    return _check_loaded().yetty_ygui_widget_set_align_self(widget, align)

def yetty_ygui_widget_set_flex(widget, grow, shrink, basis):
    return _check_loaded().yetty_ygui_widget_set_flex(widget, grow, shrink, basis)

def yetty_ygui_widget_set_gap(widget, gap):
    return _check_loaded().yetty_ygui_widget_set_gap(widget, gap)

def yetty_ygui_widget_set_padding(widget, top, right, bottom, left):
    return _check_loaded().yetty_ygui_widget_set_padding(widget, top, right, bottom, left)

def yetty_ygui_widget_set_margin(widget, top, right, bottom, left):
    return _check_loaded().yetty_ygui_widget_set_margin(widget, top, right, bottom, left)

def yetty_ygui_widget_set_min_size(widget, min_w, min_h):
    return _check_loaded().yetty_ygui_widget_set_min_size(widget, min_w, min_h)

def yetty_ygui_widget_set_max_size(widget, max_w, max_h):
    return _check_loaded().yetty_ygui_widget_set_max_size(widget, max_w, max_h)

def yetty_ygui_widget_set_flex_wrap(widget, wrap):
    return _check_loaded().yetty_ygui_widget_set_flex_wrap(widget, wrap)

def yetty_ygui_widget_set_align_content(widget, align):
    return _check_loaded().yetty_ygui_widget_set_align_content(widget, align)

def yetty_ygui_widget_set_position_mode(widget, position):
    return _check_loaded().yetty_ygui_widget_set_position_mode(widget, position)

def yetty_ygui_widget_set_flex_basis_percent(widget, pct):
    return _check_loaded().yetty_ygui_widget_set_flex_basis_percent(widget, pct)

def yetty_ygui_widget_set_size_percent(widget, w_pct, h_pct):
    return _check_loaded().yetty_ygui_widget_set_size_percent(widget, w_pct, h_pct)

def yetty_ygui_widget_set_min_size_percent(widget, min_w_pct, min_h_pct):
    return _check_loaded().yetty_ygui_widget_set_min_size_percent(widget, min_w_pct, min_h_pct)

def yetty_ygui_widget_set_max_size_percent(widget, max_w_pct, max_h_pct):
    return _check_loaded().yetty_ygui_widget_set_max_size_percent(widget, max_w_pct, max_h_pct)

def yetty_ygui_widget_apply_css(widget, css):
    return _check_loaded().yetty_ygui_widget_apply_css(widget, css)

def yetty_ygui_widget_button_set_label(widget, label):
    return _check_loaded().yetty_ygui_widget_button_set_label(widget, label)

def yetty_ygui_widget_button_get_label(widget):
    return _check_loaded().yetty_ygui_widget_button_get_label(widget)

def yetty_ygui_widget_label_set_text(widget, text):
    return _check_loaded().yetty_ygui_widget_label_set_text(widget, text)

def yetty_ygui_widget_label_get_text(widget):
    return _check_loaded().yetty_ygui_widget_label_get_text(widget)

def yetty_ygui_widget_label_set_font_size(widget, size):
    return _check_loaded().yetty_ygui_widget_label_set_font_size(widget, size)

def yetty_ygui_widget_slider_set_value(widget, value):
    return _check_loaded().yetty_ygui_widget_slider_set_value(widget, value)

def yetty_ygui_widget_slider_get_value(widget):
    return _check_loaded().yetty_ygui_widget_slider_get_value(widget)

def yetty_ygui_widget_slider_set_range(widget, min_val, max_val):
    return _check_loaded().yetty_ygui_widget_slider_set_range(widget, min_val, max_val)

def yetty_ygui_widget_checkbox_set_checked(widget, checked):
    return _check_loaded().yetty_ygui_widget_checkbox_set_checked(widget, checked)

def yetty_ygui_widget_checkbox_get_checked(widget):
    return _check_loaded().yetty_ygui_widget_checkbox_get_checked(widget)

def yetty_ygui_widget_checkbox_set_label(widget, label):
    return _check_loaded().yetty_ygui_widget_checkbox_set_label(widget, label)

def yetty_ygui_widget_textinput_set_text(widget, text):
    return _check_loaded().yetty_ygui_widget_textinput_set_text(widget, text)

def yetty_ygui_widget_textinput_get_text(widget):
    return _check_loaded().yetty_ygui_widget_textinput_get_text(widget)

def yetty_ygui_widget_textinput_set_placeholder(widget, text):
    return _check_loaded().yetty_ygui_widget_textinput_set_placeholder(widget, text)

def yetty_ygui_widget_panel_set_scroll(widget, x, y):
    return _check_loaded().yetty_ygui_widget_panel_set_scroll(widget, x, y)

def yetty_ygui_widget_panel_get_scroll(widget, x, y):
    return _check_loaded().yetty_ygui_widget_panel_get_scroll(widget, x, y)

def yetty_ygui_widget_panel_set_content_size(widget, w, h):
    return _check_loaded().yetty_ygui_widget_panel_set_content_size(widget, w, h)

def yetty_ygui_widget_panel_set_header_height(widget, h):
    return _check_loaded().yetty_ygui_widget_panel_set_header_height(widget, h)

def yetty_ygui_widget_progress_set_value(widget, value):
    return _check_loaded().yetty_ygui_widget_progress_set_value(widget, value)

def yetty_ygui_widget_progress_get_value(widget):
    return _check_loaded().yetty_ygui_widget_progress_get_value(widget)

def yetty_ygui_widget_dropdown_set_options(widget, options, count):
    return _check_loaded().yetty_ygui_widget_dropdown_set_options(widget, options, count)

def yetty_ygui_widget_dropdown_set_selected(widget, index):
    return _check_loaded().yetty_ygui_widget_dropdown_set_selected(widget, index)

def yetty_ygui_widget_dropdown_get_selected(widget):
    return _check_loaded().yetty_ygui_widget_dropdown_get_selected(widget)

def yetty_ygui_widget_colorpicker_set_color(widget, r, g, b, a):
    return _check_loaded().yetty_ygui_widget_colorpicker_set_color(widget, r, g, b, a)

def yetty_ygui_widget_colorpicker_get_color(widget, r, g, b, a):
    return _check_loaded().yetty_ygui_widget_colorpicker_get_color(widget, r, g, b, a)

def yetty_ygui_widget_popup_set_label(widget, label):
    return _check_loaded().yetty_ygui_widget_popup_set_label(widget, label)

def yetty_ygui_widget_popup_get_label(widget):
    return _check_loaded().yetty_ygui_widget_popup_get_label(widget)

def yetty_ygui_widget_popup_set_modal(widget, modal):
    return _check_loaded().yetty_ygui_widget_popup_set_modal(widget, modal)

def yetty_ygui_widget_popup_is_modal(widget):
    return _check_loaded().yetty_ygui_widget_popup_is_modal(widget)

def yetty_ygui_widget_popup_set_open(widget, open):
    return _check_loaded().yetty_ygui_widget_popup_set_open(widget, open)

def yetty_ygui_widget_popup_is_open(widget):
    return _check_loaded().yetty_ygui_widget_popup_is_open(widget)

def yetty_ygui_widget_popup_set_scene_size(widget, w, h):
    return _check_loaded().yetty_ygui_widget_popup_set_scene_size(widget, w, h)

def yetty_ygui_widget_popup_set_header_color(widget, color):
    return _check_loaded().yetty_ygui_widget_popup_set_header_color(widget, color)

def yetty_ygui_widget_collapsing_header_set_label(widget, label):
    return _check_loaded().yetty_ygui_widget_collapsing_header_set_label(widget, label)

def yetty_ygui_widget_collapsing_header_get_label(widget):
    return _check_loaded().yetty_ygui_widget_collapsing_header_get_label(widget)

def yetty_ygui_widget_collapsing_header_set_open(widget, open):
    return _check_loaded().yetty_ygui_widget_collapsing_header_set_open(widget, open)

def yetty_ygui_widget_collapsing_header_is_open(widget):
    return _check_loaded().yetty_ygui_widget_collapsing_header_is_open(widget)

def yetty_ygui_widget_tooltip_set_label(widget, label):
    return _check_loaded().yetty_ygui_widget_tooltip_set_label(widget, label)

def yetty_ygui_widget_tooltip_get_label(widget):
    return _check_loaded().yetty_ygui_widget_tooltip_get_label(widget)

def yetty_ygui_widget_selectable_set_label(widget, label):
    return _check_loaded().yetty_ygui_widget_selectable_set_label(widget, label)

def yetty_ygui_widget_selectable_get_label(widget):
    return _check_loaded().yetty_ygui_widget_selectable_get_label(widget)

def yetty_ygui_widget_selectable_set_checked(widget, checked):
    return _check_loaded().yetty_ygui_widget_selectable_set_checked(widget, checked)

def yetty_ygui_widget_selectable_is_checked(widget):
    return _check_loaded().yetty_ygui_widget_selectable_is_checked(widget)

def yetty_ygui_widget_choicebox_set_options(widget, options, count):
    return _check_loaded().yetty_ygui_widget_choicebox_set_options(widget, options, count)

def yetty_ygui_widget_choicebox_set_selected(widget, index):
    return _check_loaded().yetty_ygui_widget_choicebox_set_selected(widget, index)

def yetty_ygui_widget_choicebox_get_selected(widget):
    return _check_loaded().yetty_ygui_widget_choicebox_get_selected(widget)

def yetty_ygui_widget_scrollbar_set_value(widget, value):
    return _check_loaded().yetty_ygui_widget_scrollbar_set_value(widget, value)

def yetty_ygui_widget_scrollbar_get_value(widget):
    return _check_loaded().yetty_ygui_widget_scrollbar_get_value(widget)

def yetty_ygui_widget_list_set_selected(list, child):
    return _check_loaded().yetty_ygui_widget_list_set_selected(list, child)

def yetty_ygui_widget_list_get_selected(list):
    return _check_loaded().yetty_ygui_widget_list_get_selected(list)

def yetty_ygui_widget_list_on_select(list, cb, userdata):
    return _check_loaded().yetty_ygui_widget_list_on_select(list, cb, userdata)

def yetty_ygui_widget_tree_node_set_label(node, label):
    return _check_loaded().yetty_ygui_widget_tree_node_set_label(node, label)

def yetty_ygui_widget_tree_node_get_label(node):
    return _check_loaded().yetty_ygui_widget_tree_node_get_label(node)

def yetty_ygui_widget_tree_node_set_expanded(node, expanded):
    return _check_loaded().yetty_ygui_widget_tree_node_set_expanded(node, expanded)

def yetty_ygui_widget_tree_node_is_expanded(node):
    return _check_loaded().yetty_ygui_widget_tree_node_is_expanded(node)

def yetty_ygui_widget_tree_node_children(node):
    return _check_loaded().yetty_ygui_widget_tree_node_children(node)

def yetty_ygui_widget_tree_node_on_toggle(node, cb, userdata):
    return _check_loaded().yetty_ygui_widget_tree_node_on_toggle(node, cb, userdata)

def yetty_ygui_engine_find(engine, id):
    return _check_loaded().yetty_ygui_engine_find(engine, id)

def yetty_ygui_engine_widget_at(engine, x, y):
    return _check_loaded().yetty_ygui_engine_widget_at(engine, x, y)

def yetty_ygui_theme_create():
    return _check_loaded().yetty_ygui_theme_create()

def yetty_ygui_theme_create_default():
    return _check_loaded().yetty_ygui_theme_create_default()

def yetty_ygui_theme_destroy(theme):
    return _check_loaded().yetty_ygui_theme_destroy(theme)

def yetty_ygui_theme_set_padding(theme, sm, med, lg):
    return _check_loaded().yetty_ygui_theme_set_padding(theme, sm, med, lg)

def yetty_ygui_theme_set_radius(theme, sm, med, lg):
    return _check_loaded().yetty_ygui_theme_set_radius(theme, sm, med, lg)

def yetty_ygui_theme_set_row_height(theme, height):
    return _check_loaded().yetty_ygui_theme_set_row_height(theme, height)

def yetty_ygui_theme_set_font_size(theme, size):
    return _check_loaded().yetty_ygui_theme_set_font_size(theme, size)

def yetty_ygui_theme_set_scrollbar_size(theme, size):
    return _check_loaded().yetty_ygui_theme_set_scrollbar_size(theme, size)

def yetty_ygui_theme_set_bg_primary(theme, color):
    return _check_loaded().yetty_ygui_theme_set_bg_primary(theme, color)

def yetty_ygui_theme_set_bg_surface(theme, color):
    return _check_loaded().yetty_ygui_theme_set_bg_surface(theme, color)

def yetty_ygui_theme_set_bg_hover(theme, color):
    return _check_loaded().yetty_ygui_theme_set_bg_hover(theme, color)

def yetty_ygui_theme_set_text_primary(theme, color):
    return _check_loaded().yetty_ygui_theme_set_text_primary(theme, color)

def yetty_ygui_theme_set_text_muted(theme, color):
    return _check_loaded().yetty_ygui_theme_set_text_muted(theme, color)

def yetty_ygui_theme_set_accent(theme, color):
    return _check_loaded().yetty_ygui_theme_set_accent(theme, color)

def yetty_ygui_theme_set_border(theme, color):
    return _check_loaded().yetty_ygui_theme_set_border(theme, color)

def yetty_ygui_theme_set_border_muted(theme, color):
    return _check_loaded().yetty_ygui_theme_set_border_muted(theme, color)

def yetty_ygui_theme_set_bg_dropdown(theme, color):
    return _check_loaded().yetty_ygui_theme_set_bg_dropdown(theme, color)

def yetty_ygui_theme_set_overlay_modal(theme, color):
    return _check_loaded().yetty_ygui_theme_set_overlay_modal(theme, color)

def yetty_ygui_theme_set_shadow(theme, color):
    return _check_loaded().yetty_ygui_theme_set_shadow(theme, color)

def yetty_ygui_theme_set_tooltip_bg(theme, color):
    return _check_loaded().yetty_ygui_theme_set_tooltip_bg(theme, color)

def yetty_ygui_theme_set_selection_bg(theme, color):
    return _check_loaded().yetty_ygui_theme_set_selection_bg(theme, color)

def yetty_ygui_theme_set_elevation(theme, low, medium, high, alpha):
    return _check_loaded().yetty_ygui_theme_set_elevation(theme, low, medium, high, alpha)

def yetty_ygui_theme_set_gradient(theme, enable):
    return _check_loaded().yetty_ygui_theme_set_gradient(theme, enable)

def yetty_ygui_engine_set_input_fd(engine, fd):
    return _check_loaded().yetty_ygui_engine_set_input_fd(engine, fd)

def yetty_ygui_engine_set_output_fd(engine, fd):
    return _check_loaded().yetty_ygui_engine_set_output_fd(engine, fd)

def yetty_ygui_engine_set_card_size(engine, card_w, card_h):
    return _check_loaded().yetty_ygui_engine_set_card_size(engine, card_w, card_h)

def yetty_ygui_engine_set_display_pixel_size(engine, width, height):
    return _check_loaded().yetty_ygui_engine_set_display_pixel_size(engine, width, height)

def yetty_ygui_engine_get_loop(engine):
    return _check_loaded().yetty_ygui_engine_get_loop(engine)

def yetty_ygui_engine_poll(engine):
    return _check_loaded().yetty_ygui_engine_poll(engine)

def yetty_ygui_get_error():
    return _check_loaded().yetty_ygui_get_error()

def yetty_ygui_version():
    return _check_loaded().yetty_ygui_version()


# Auto-load if YGUI_LIB is set in the environment. Useful for ad-hoc REPL
# work where the user doesn't want to call load() manually.
if (_p := os.environ.get("YGUI_LIB")):
    load(_p)
