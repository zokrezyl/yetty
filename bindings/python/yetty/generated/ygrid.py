"""yetty.ygrid bindings — GENERATED from model.yaml, do not edit."""
from __future__ import annotations
from ctypes import (c_bool, c_char, c_char_p, c_double, c_float, c_int,
    c_int8, c_int16, c_int32, c_int64, c_long, c_size_t, c_ssize_t,
    c_uint, c_uint8, c_uint16, c_uint32, c_uint64, c_ulong, c_void_p)
from typing import Any, ClassVar
from .. import runtime as _rt
from . import _types as _t
from . import yfigure as _yfigure

class Grid(_yfigure.Figure):
    """yclass ygrid:grid"""
    __yclass_domain__: ClassVar[str] = 'ygrid'
    __yclass_name__: ClassVar[str] = 'grid'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygrid_grid_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygrid_grid_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Grid']:
        obj = cls()
        return obj.init_result
    def destroy(self) -> _rt.Result[None]:
        """Call `yetty_ygrid_destroy`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_ygrid_destroy", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def add_record(self, record: _t.yetty_ycore_buffer) -> _rt.Result[None]:
        """Call `yetty_ygrid_add_record`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_ygrid_add_record", _t.yetty_ycore_void_result, [c_void_p, c_void_p, _t.yetty_ycore_buffer])
        res = _fn(None, self._handle, record)
        return _rt.result_from_c(res)
    def clear(self) -> _rt.Result[None]:
        """Call `yetty_ygrid_clear`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_ygrid_clear", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)

