"""yetty.yshadertoy bindings — GENERATED from model.yaml, do not edit."""
from __future__ import annotations
from ctypes import (c_bool, c_char, c_char_p, c_double, c_float, c_int,
    c_int8, c_int16, c_int32, c_int64, c_long, c_size_t, c_ssize_t,
    c_uint, c_uint8, c_uint16, c_uint32, c_uint64, c_ulong, c_void_p)
from typing import Any, ClassVar
from .. import runtime as _rt
from . import _types as _t
from . import yfigure as _yfigure

class Figure(_yfigure.Figure):
    """yclass yshadertoy:figure"""
    __yclass_domain__: ClassVar[str] = 'yshadertoy'
    __yclass_name__: ClassVar[str] = 'figure'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_yshadertoy_figure_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_yshadertoy_figure_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Figure']:
        obj = cls()
        return obj.init_result

def create(rect: _t.yetty_ycore_rectangle, shader_src: str | bytes | None, shader_len: int, context: Any) -> _rt.Result[Any]:
    """Call `yetty_yshadertoy_create`."""
    _fn = _rt.cfn("yetty_yshadertoy_create", _t.yetty_yclass_object_ptr_result, [_t.yetty_ycore_rectangle, c_char_p, c_size_t, c_void_p])
    res = _fn(rect, _rt.cstr(shader_src), shader_len, _rt.handle(context))
    return _rt.result_from_c(res)

def as_figure(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_yshadertoy_as_figure`."""
    _fn = _rt.cfn("yetty_yshadertoy_as_figure", _t.yetty_yfigure_figure_ptr_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def set_source(obj: Any, shader_src: str | bytes | None, shader_len: int) -> _rt.Result[None]:
    """Call `yetty_yshadertoy_set_source`."""
    _fn = _rt.cfn("yetty_yshadertoy_set_source", _t.yetty_ycore_void_result, [c_void_p, c_char_p, c_size_t])
    res = _fn(_rt.handle(obj), _rt.cstr(shader_src), shader_len)
    return _rt.result_from_c(res)

def register_factory(registry: Any) -> _rt.Result[None]:
    """Call `yetty_yshadertoy_register_factory`."""
    _fn = _rt.cfn("yetty_yshadertoy_register_factory", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(registry))
    return _rt.result_from_c(res)

