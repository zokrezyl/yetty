"""yetty.ychrome bindings — GENERATED from model.yaml, do not edit."""
from __future__ import annotations
from ctypes import (c_bool, c_char, c_char_p, c_double, c_float, c_int,
    c_int8, c_int16, c_int32, c_int64, c_long, c_size_t, c_ssize_t,
    c_uint, c_uint8, c_uint16, c_uint32, c_uint64, c_ulong, c_void_p)
from .. import runtime as _rt
from . import _types as _t

class Chrome:
    """yclass ychrome:chrome"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ychrome_chrome_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def configure(self, window_manager, caption_height, edge_size, flags):
        _fn = _rt.cfn("yetty_ychrome_configure", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p, c_float, c_float, c_uint32])
        res = _fn(None, self._handle, window_manager, caption_height, edge_size, flags)
        _rt.check(res)
        return None
    def set_size(self, width, height):
        _fn = _rt.cfn("yetty_ychrome_set_size", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_float, c_float])
        res = _fn(None, self._handle, width, height)
        _rt.check(res)
        return None
    def destroy(self):
        _fn = _rt.cfn("yetty_ychrome_destroy", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def edge_cursor_at(self, x, y):
        _fn = _rt.cfn("yetty_ychrome_edge_cursor_at", _t.yetty_ycore_int_result, [c_void_p, c_void_p, c_float, c_float])
        res = _fn(None, self._handle, x, y)
        _rt.check(res)
        return res.value
    def render(self):
        _fn = _rt.cfn("yetty_ychrome_render", _t.yetty_ydraw_drawable_list_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return res.value
    def handle_event(self, event):
        _fn = _rt.cfn("yetty_ychrome_handle_event", _t.yetty_ycore_int_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, event)
        _rt.check(res)
        return res.value

