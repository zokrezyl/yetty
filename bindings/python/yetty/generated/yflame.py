"""yetty.yflame bindings — GENERATED from model.yaml, do not edit."""
from __future__ import annotations
from ctypes import (c_bool, c_char, c_char_p, c_double, c_float, c_int,
    c_int8, c_int16, c_int32, c_int64, c_long, c_size_t, c_ssize_t,
    c_uint, c_uint8, c_uint16, c_uint32, c_uint64, c_ulong, c_void_p)
from typing import Any, ClassVar
from .. import runtime as _rt
from . import _types as _t

class Flame(_rt.YClass):
    """yclass yflame:flame"""
    __yclass_domain__: ClassVar[str] = 'yflame'
    __yclass_name__: ClassVar[str] = 'flame'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_yflame_flame_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_yflame_flame_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Flame']:
        obj = cls()
        return obj.init_result
    def configure(self, frame_height: float, min_width: float, flags: int) -> _rt.Result[None]:
        """Call `yetty_yflame_configure`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yflame_configure", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float, c_float, c_uint32])
        res = _fn(None, self._handle, frame_height, min_width, flags)
        return _rt.result_from_c(res)
    def parse(self, len: int) -> _rt.Result[None]:
        """Call `yetty_yflame_parse`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yflame_parse", _t.yetty_ycore_void_result, [c_void_p, c_char_p, c_size_t])
        res = _fn(None, self._handle, len)
        return _rt.result_from_c(res)
    def render(self) -> _rt.Result[Any]:
        """Call `yetty_yflame_render`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yflame_render", _t.yetty_ydraw_drawable_list_result, [c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def hit_test(self, y: float) -> _rt.Result[int]:
        """Call `yetty_yflame_hit_test`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yflame_hit_test", _t.yetty_ycore_int_result, [c_void_p, c_float, c_float])
        res = _fn(None, self._handle, y)
        return _rt.result_from_c(res)
    def focus(self) -> _rt.Result[None]:
        """Call `yetty_yflame_focus`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yflame_focus", _t.yetty_ycore_void_result, [c_void_p, c_int32])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def focus_parent(self) -> _rt.Result[None]:
        """Call `yetty_yflame_focus_parent`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yflame_focus_parent", _t.yetty_ycore_void_result, [c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def reset(self) -> _rt.Result[None]:
        """Call `yetty_yflame_reset`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yflame_reset", _t.yetty_ycore_void_result, [c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def set_highlight(self) -> _rt.Result[None]:
        """Call `yetty_yflame_set_highlight`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yflame_set_highlight", _t.yetty_ycore_void_result, [c_void_p, c_int32])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def destroy(self) -> _rt.Result[None]:
        """Call `yetty_yflame_destroy`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yflame_destroy", _t.yetty_ycore_void_result, [c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)

def emit_osc(list: Any, fd: int) -> _rt.Result[None]:
    """Call `yetty_yflame_emit_osc`."""
    _fn = _rt.cfn("yetty_yflame_emit_osc", _t.yetty_ycore_void_result, [c_void_p, c_int])
    res = _fn(_rt.handle(list), fd)
    return _rt.result_from_c(res)

