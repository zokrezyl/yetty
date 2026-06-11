"""yetty.yflame bindings — GENERATED from model.yaml, do not edit."""
from __future__ import annotations
from ctypes import (c_bool, c_char, c_char_p, c_double, c_float, c_int,
    c_int8, c_int16, c_int32, c_int64, c_long, c_size_t, c_ssize_t,
    c_uint, c_uint8, c_uint16, c_uint32, c_uint64, c_ulong, c_void_p)
from .. import runtime as _rt
from . import _types as _t

class Flame:
    """yclass yflame:flame"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_yflame_flame_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def configure(self, width, frame_height, min_width, flags):
        _fn = _rt.cfn("yetty_yflame_configure", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_float, c_float, c_float, c_uint32])
        res = _fn(None, self._handle, width, frame_height, min_width, flags)
        _rt.check(res)
        return None
    def parse(self, input, len):
        _fn = _rt.cfn("yetty_yflame_parse", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_char_p, c_size_t])
        res = _fn(None, self._handle, _rt.cstr(input), len)
        _rt.check(res)
        return None
    def render(self):
        _fn = _rt.cfn("yetty_yflame_render", _t.yetty_ydraw_drawable_list_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return res.value
    def hit_test(self, x, y):
        _fn = _rt.cfn("yetty_yflame_hit_test", _t.yetty_ycore_int_result, [c_void_p, c_void_p, c_float, c_float])
        res = _fn(None, self._handle, x, y)
        _rt.check(res)
        return res.value
    def focus(self, node_id):
        _fn = _rt.cfn("yetty_yflame_focus", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_int32])
        res = _fn(None, self._handle, node_id)
        _rt.check(res)
        return None
    def focus_parent(self):
        _fn = _rt.cfn("yetty_yflame_focus_parent", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def reset(self):
        _fn = _rt.cfn("yetty_yflame_reset", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def set_highlight(self, node_id):
        _fn = _rt.cfn("yetty_yflame_set_highlight", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_int32])
        res = _fn(None, self._handle, node_id)
        _rt.check(res)
        return None
    def destroy(self):
        _fn = _rt.cfn("yetty_yflame_destroy", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None

