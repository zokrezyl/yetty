"""yetty.yfigure bindings — GENERATED from model.yaml, do not edit."""
from __future__ import annotations
from ctypes import (c_bool, c_char, c_char_p, c_double, c_float, c_int,
    c_int8, c_int16, c_int32, c_int64, c_long, c_size_t, c_ssize_t,
    c_uint, c_uint8, c_uint16, c_uint32, c_uint64, c_ulong, c_void_p)
from .. import runtime as _rt
from . import _types as _t

class Container:
    """yclass yfigure:container"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_yfigure_container_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def destroy(self):
        _fn = _rt.cfn("yetty_yfigure_destroy", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def render(self, target):
        _fn = _rt.cfn("yetty_yfigure_render", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, target)
        _rt.check(res)
        return None
    def constructor(self):
        _fn = _rt.cfn("yetty_yfigure_constructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def add_child(self, child, id):
        _fn = _rt.cfn("yetty_yfigure_add_child", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p, c_uint32])
        res = _fn(None, self._handle, child, id)
        _rt.check(res)
        return None
    def remove_child_by_id(self, id):
        _fn = _rt.cfn("yetty_yfigure_remove_child_by_id", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_uint32])
        res = _fn(None, self._handle, id)
        _rt.check(res)
        return None
    def raise_child_by_id(self, id):
        _fn = _rt.cfn("yetty_yfigure_raise_child_by_id", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_uint32])
        res = _fn(None, self._handle, id)
        _rt.check(res)
        return None
    def process_records(self, bytes):
        _fn = _rt.cfn("yetty_yfigure_process_records", _t.yetty_ycore_void_result, [c_void_p, c_void_p, _t.yetty_ycore_buffer])
        res = _fn(None, self._handle, bytes)
        _rt.check(res)
        return None
    def process_input(self, statemachine):
        _fn = _rt.cfn("yetty_yfigure_process_input", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, statemachine)
        _rt.check(res)
        return None
    def process_bytes(self, bytes, bytes_len):
        _fn = _rt.cfn("yetty_yfigure_process_bytes", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p, c_size_t])
        res = _fn(None, self._handle, bytes, bytes_len)
        _rt.check(res)
        return None
    def dump_state(self, indent):
        _fn = _rt.cfn("yetty_yfigure_dump_state", _t.yetty_ycore_char_ptr_result, [c_void_p, c_void_p, c_int])
        res = _fn(None, self._handle, indent)
        _rt.check(res)
        return res.value

class Figure:
    """yclass yfigure:figure"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_yfigure_figure_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def render(self, target):
        _fn = _rt.cfn("yetty_yfigure_render", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, target)
        _rt.check(res)
        return None
    def destroy(self):
        _fn = _rt.cfn("yetty_yfigure_destroy", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def process_input(self, statemachine):
        _fn = _rt.cfn("yetty_yfigure_process_input", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, statemachine)
        _rt.check(res)
        return None
    def process_bytes(self, bytes, bytes_len):
        _fn = _rt.cfn("yetty_yfigure_process_bytes", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p, c_size_t])
        res = _fn(None, self._handle, bytes, bytes_len)
        _rt.check(res)
        return None
    def reset_content(self):
        _fn = _rt.cfn("yetty_yfigure_reset_content", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def dump_state(self, indent):
        _fn = _rt.cfn("yetty_yfigure_dump_state", _t.yetty_ycore_char_ptr_result, [c_void_p, c_void_p, c_int])
        res = _fn(None, self._handle, indent)
        _rt.check(res)
        return res.value
    def set_scroll(self, scroll_x, scroll_y):
        _fn = _rt.cfn("yetty_yfigure_set_scroll", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_float, c_float])
        res = _fn(None, self._handle, scroll_x, scroll_y)
        _rt.check(res)
        return None
    def set_content_size(self, content_w, content_h):
        _fn = _rt.cfn("yetty_yfigure_set_content_size", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_float, c_float])
        res = _fn(None, self._handle, content_w, content_h)
        _rt.check(res)
        return None

