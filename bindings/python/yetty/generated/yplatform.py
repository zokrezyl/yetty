"""yetty.yplatform bindings — GENERATED from model.yaml, do not edit."""
from __future__ import annotations
from ctypes import (c_bool, c_char, c_char_p, c_double, c_float, c_int,
    c_int8, c_int16, c_int32, c_int64, c_long, c_size_t, c_ssize_t,
    c_uint, c_uint8, c_uint16, c_uint32, c_uint64, c_ulong, c_void_p)
from .. import runtime as _rt
from . import _types as _t

class WindowManager:
    """yclass yplatform:window_manager"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_yplatform_window_manager_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def window_manager_configure(self, os_window, output_pipe, input_pipe):
        _fn = _rt.cfn("yetty_yplatform_window_manager_configure", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, os_window, output_pipe, input_pipe)
        _rt.check(res)
        return None
    def window_manager_destroy(self):
        _fn = _rt.cfn("yetty_yplatform_window_manager_destroy", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def window_manager_iconify(self):
        _fn = _rt.cfn("yetty_yplatform_window_manager_iconify", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def window_manager_toggle_maximize(self):
        _fn = _rt.cfn("yetty_yplatform_window_manager_toggle_maximize", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def window_manager_request_close(self):
        _fn = _rt.cfn("yetty_yplatform_window_manager_request_close", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def window_manager_drag_by(self, dx, dy):
        _fn = _rt.cfn("yetty_yplatform_window_manager_drag_by", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_int, c_int])
        res = _fn(None, self._handle, dx, dy)
        _rt.check(res)
        return None
    def window_manager_resize_by(self, dx, dy, edge):
        _fn = _rt.cfn("yetty_yplatform_window_manager_resize_by", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_int, c_int, c_int])
        res = _fn(None, self._handle, dx, dy, edge)
        _rt.check(res)
        return None
    def window_manager_begin_interactive_move(self):
        _fn = _rt.cfn("yetty_yplatform_window_manager_begin_interactive_move", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def window_manager_begin_interactive_resize(self, edge):
        _fn = _rt.cfn("yetty_yplatform_window_manager_begin_interactive_resize", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_int])
        res = _fn(None, self._handle, edge)
        _rt.check(res)
        return None
    def window_manager_set_cursor(self, shape):
        _fn = _rt.cfn("yetty_yplatform_window_manager_set_cursor", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_int])
        res = _fn(None, self._handle, shape)
        _rt.check(res)
        return None
    def window_manager_handle_event(self, event):
        _fn = _rt.cfn("yetty_yplatform_window_manager_handle_event", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, event)
        _rt.check(res)
        return None

