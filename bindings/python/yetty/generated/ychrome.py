"""yetty.ychrome bindings — GENERATED from model.yaml, do not edit."""
from __future__ import annotations
from ctypes import (c_bool, c_char, c_char_p, c_double, c_float, c_int,
    c_int8, c_int16, c_int32, c_int64, c_long, c_size_t, c_ssize_t,
    c_uint, c_uint8, c_uint16, c_uint32, c_uint64, c_ulong, c_void_p)
from typing import Any, ClassVar
from .. import runtime as _rt
from . import _types as _t

class Chrome(_rt.YClass):
    """yclass ychrome:chrome"""
    __yclass_domain__: ClassVar[str] = 'ychrome'
    __yclass_name__: ClassVar[str] = 'chrome'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ychrome_chrome_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ychrome_chrome_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Chrome']:
        obj = cls()
        return obj.init_result
    def configure(self, caption_height: float, edge_size: float, flags: int) -> _rt.Result[None]:
        """Call `yetty_ychrome_configure`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_ychrome_configure", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_float, c_float, c_uint32])
        res = _fn(None, self._handle, caption_height, edge_size, flags)
        return _rt.result_from_c(res)
    def set_size(self, height: float) -> _rt.Result[None]:
        """Call `yetty_ychrome_set_size`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_ychrome_set_size", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float])
        res = _fn(None, self._handle, height)
        return _rt.result_from_c(res)
    def destroy(self) -> _rt.Result[None]:
        """Call `yetty_ychrome_destroy`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_ychrome_destroy", _t.yetty_ycore_void_result, [c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def edge_cursor_at(self, y: float) -> _rt.Result[int]:
        """Call `yetty_ychrome_edge_cursor_at`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_ychrome_edge_cursor_at", _t.yetty_ycore_int_result, [c_void_p, c_float, c_float])
        res = _fn(None, self._handle, y)
        return _rt.result_from_c(res)
    def render(self) -> _rt.Result[Any]:
        """Call `yetty_ychrome_render`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_ychrome_render", _t.yetty_ydraw_drawable_list_result, [c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def handle_event(self) -> _rt.Result[int]:
        """Call `yetty_ychrome_handle_event`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_ychrome_handle_event", _t.yetty_ycore_int_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)

def hover_button(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ychrome_hover_button`."""
    _fn = _rt.cfn("yetty_ychrome_hover_button", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

