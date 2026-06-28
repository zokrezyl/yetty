"""yetty.ycircuit bindings — GENERATED from model.yaml, do not edit."""
from __future__ import annotations
from ctypes import (c_bool, c_char, c_char_p, c_double, c_float, c_int,
    c_int8, c_int16, c_int32, c_int64, c_long, c_size_t, c_ssize_t,
    c_uint, c_uint8, c_uint16, c_uint32, c_uint64, c_ulong, c_void_p)
from typing import Any, ClassVar
from .. import runtime as _rt
from . import _types as _t

class Circuit(_rt.YClass):
    """yclass ycircuit:circuit"""
    __yclass_domain__: ClassVar[str] = 'ycircuit'
    __yclass_name__: ClassVar[str] = 'circuit'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ycircuit_circuit_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ycircuit_circuit_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Circuit']:
        obj = cls()
        return obj.init_result
    def configure(self, flags: int) -> _rt.Result[None]:
        """Call `yetty_ycircuit_configure`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_ycircuit_configure", _t.yetty_ycore_void_result, [c_void_p, c_float, c_uint32])
        res = _fn(None, self._handle, flags)
        return _rt.result_from_c(res)
    def parse(self, len: int) -> _rt.Result[None]:
        """Call `yetty_ycircuit_parse`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_ycircuit_parse", _t.yetty_ycore_void_result, [c_void_p, c_char_p, c_size_t])
        res = _fn(None, self._handle, len)
        return _rt.result_from_c(res)
    def clear(self) -> _rt.Result[None]:
        """Call `yetty_ycircuit_clear`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_ycircuit_clear", _t.yetty_ycore_void_result, [c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def add_component(self, x: float, y: float, rotation_deg: int, name: str | bytes | None, value: str | bytes | None) -> _rt.Result[int]:
        """Call `yetty_ycircuit_add_component`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_ycircuit_add_component", _t.yetty_ycore_int_result, [c_void_p, c_char_p, c_float, c_float, c_int32, c_char_p, c_char_p])
        res = _fn(None, self._handle, x, y, rotation_deg, _rt.cstr(name), _rt.cstr(value))
        return _rt.result_from_c(res)
    def add_ic(self, y: float, rotation_deg: int, name: str | bytes | None, value: str | bytes | None, pins_left: str | bytes | None, pins_right: str | bytes | None, pins_top: str | bytes | None, pins_bottom: str | bytes | None) -> _rt.Result[int]:
        """Call `yetty_ycircuit_add_ic`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_ycircuit_add_ic", _t.yetty_ycore_int_result, [c_void_p, c_float, c_float, c_int32, c_char_p, c_char_p, c_char_p, c_char_p, c_char_p, c_char_p])
        res = _fn(None, self._handle, y, rotation_deg, _rt.cstr(name), _rt.cstr(value), _rt.cstr(pins_left), _rt.cstr(pins_right), _rt.cstr(pins_top), _rt.cstr(pins_bottom))
        return _rt.result_from_c(res)
    def add_wire(self, y0: float, x1: float, y1: float) -> _rt.Result[int]:
        """Call `yetty_ycircuit_add_wire`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_ycircuit_add_wire", _t.yetty_ycore_int_result, [c_void_p, c_float, c_float, c_float, c_float])
        res = _fn(None, self._handle, y0, x1, y1)
        return _rt.result_from_c(res)
    def add_junction(self, y: float) -> _rt.Result[int]:
        """Call `yetty_ycircuit_add_junction`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_ycircuit_add_junction", _t.yetty_ycore_int_result, [c_void_p, c_float, c_float])
        res = _fn(None, self._handle, y)
        return _rt.result_from_c(res)
    def add_label(self, y: float, text: str | bytes | None) -> _rt.Result[int]:
        """Call `yetty_ycircuit_add_label`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_ycircuit_add_label", _t.yetty_ycore_int_result, [c_void_p, c_float, c_float, c_char_p])
        res = _fn(None, self._handle, y, _rt.cstr(text))
        return _rt.result_from_c(res)
    def render(self) -> _rt.Result[Any]:
        """Call `yetty_ycircuit_render`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_ycircuit_render", _t.yetty_ydraw_drawable_list_result, [c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def hit_test(self, y: float) -> _rt.Result[int]:
        """Call `yetty_ycircuit_hit_test`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_ycircuit_hit_test", _t.yetty_ycore_int_result, [c_void_p, c_float, c_float])
        res = _fn(None, self._handle, y)
        return _rt.result_from_c(res)
    def set_highlight(self) -> _rt.Result[None]:
        """Call `yetty_ycircuit_set_highlight`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_ycircuit_set_highlight", _t.yetty_ycore_void_result, [c_void_p, c_int32])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def destroy(self) -> _rt.Result[None]:
        """Call `yetty_ycircuit_destroy`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_ycircuit_destroy", _t.yetty_ycore_void_result, [c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)

def emit_osc(list: Any, fd: int) -> _rt.Result[None]:
    """Call `yetty_ycircuit_emit_osc`."""
    _fn = _rt.cfn("yetty_ycircuit_emit_osc", _t.yetty_ycore_void_result, [c_void_p, c_int])
    res = _fn(_rt.handle(list), fd)
    return _rt.result_from_c(res)

