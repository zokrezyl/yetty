"""yetty.yview bindings — GENERATED from model.yaml, do not edit."""
from __future__ import annotations
from ctypes import (c_bool, c_char, c_char_p, c_double, c_float, c_int,
    c_int8, c_int16, c_int32, c_int64, c_long, c_size_t, c_ssize_t,
    c_uint, c_uint8, c_uint16, c_uint32, c_uint64, c_ulong, c_void_p)
from .. import runtime as _rt
from . import _types as _t

class View:
    """yclass yview:view"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_yview_view_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def configure(self, fd, child_id, kind, bg_color, min_x, min_y, max_x, max_y):
        _fn = _rt.cfn("yetty_yview_configure", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_int, c_uint32, c_uint32, c_uint32, c_float, c_float, c_float, c_float])
        res = _fn(None, self._handle, fd, child_id, kind, bg_color, min_x, min_y, max_x, max_y)
        _rt.check(res)
        return None
    def set_content(self, content):
        _fn = _rt.cfn("yetty_yview_set_content", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, content)
        _rt.check(res)
        return None
    def set_text(self, text, font_size):
        _fn = _rt.cfn("yetty_yview_set_text", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_char_p, c_float])
        res = _fn(None, self._handle, _rt.cstr(text), font_size)
        _rt.check(res)
        return None
    def set_plot(self, expr, x_min, x_max, y_min, y_max):
        _fn = _rt.cfn("yetty_yview_set_plot", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_char_p, c_float, c_float, c_float, c_float])
        res = _fn(None, self._handle, _rt.cstr(expr), x_min, x_max, y_min, y_max)
        _rt.check(res)
        return None
    def set_content_size(self, content_w, content_h):
        _fn = _rt.cfn("yetty_yview_set_content_size", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_float, c_float])
        res = _fn(None, self._handle, content_w, content_h)
        _rt.check(res)
        return None
    def scroll_to(self, scroll_x, scroll_y):
        _fn = _rt.cfn("yetty_yview_scroll_to", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_float, c_float])
        res = _fn(None, self._handle, scroll_x, scroll_y)
        _rt.check(res)
        return None
    def scroll_by(self, delta_x, delta_y):
        _fn = _rt.cfn("yetty_yview_scroll_by", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_float, c_float])
        res = _fn(None, self._handle, delta_x, delta_y)
        _rt.check(res)
        return None
    def set_rect(self, min_x, min_y, max_x, max_y):
        _fn = _rt.cfn("yetty_yview_set_rect", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_float, c_float, c_float, c_float])
        res = _fn(None, self._handle, min_x, min_y, max_x, max_y)
        _rt.check(res)
        return None
    def destroy(self):
        _fn = _rt.cfn("yetty_yview_destroy", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None

