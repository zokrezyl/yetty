"""yetty.yview bindings — GENERATED from model.yaml, do not edit."""
from __future__ import annotations
from ctypes import (c_bool, c_char, c_char_p, c_double, c_float, c_int,
    c_int8, c_int16, c_int32, c_int64, c_long, c_size_t, c_ssize_t,
    c_uint, c_uint8, c_uint16, c_uint32, c_uint64, c_ulong, c_void_p)
from typing import Any, ClassVar
from .. import runtime as _rt
from . import _types as _t

class View(_rt.YClass):
    """yclass yview:view"""
    __yclass_domain__: ClassVar[str] = 'yview'
    __yclass_name__: ClassVar[str] = 'view'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_yview_view_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_yview_view_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['View']:
        obj = cls()
        return obj.init_result
    def configure(self, child_id: int, kind: int, bg_color: int, min_x: float, min_y: float, max_x: float, max_y: float) -> _rt.Result[None]:
        """Call `yetty_yview_configure`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yview_configure", _t.yetty_ycore_void_result, [c_void_p, c_int, c_uint32, c_uint32, c_uint32, c_float, c_float, c_float, c_float])
        res = _fn(None, self._handle, child_id, kind, bg_color, min_x, min_y, max_x, max_y)
        return _rt.result_from_c(res)
    def set_content(self) -> _rt.Result[None]:
        """Call `yetty_yview_set_content`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yview_set_content", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def set_text(self, font_size: float) -> _rt.Result[None]:
        """Call `yetty_yview_set_text`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yview_set_text", _t.yetty_ycore_void_result, [c_void_p, c_char_p, c_float])
        res = _fn(None, self._handle, font_size)
        return _rt.result_from_c(res)
    def set_plot(self, x_min: float, x_max: float, y_min: float, y_max: float) -> _rt.Result[None]:
        """Call `yetty_yview_set_plot`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yview_set_plot", _t.yetty_ycore_void_result, [c_void_p, c_char_p, c_float, c_float, c_float, c_float])
        res = _fn(None, self._handle, x_min, x_max, y_min, y_max)
        return _rt.result_from_c(res)
    def set_content_size(self, content_h: float) -> _rt.Result[None]:
        """Call `yetty_yview_set_content_size`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yview_set_content_size", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float])
        res = _fn(None, self._handle, content_h)
        return _rt.result_from_c(res)
    def scroll_to(self, scroll_y: float) -> _rt.Result[None]:
        """Call `yetty_yview_scroll_to`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yview_scroll_to", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float])
        res = _fn(None, self._handle, scroll_y)
        return _rt.result_from_c(res)
    def scroll_by(self, delta_y: float) -> _rt.Result[None]:
        """Call `yetty_yview_scroll_by`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yview_scroll_by", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float])
        res = _fn(None, self._handle, delta_y)
        return _rt.result_from_c(res)
    def set_rect(self, min_y: float, max_x: float, max_y: float) -> _rt.Result[None]:
        """Call `yetty_yview_set_rect`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yview_set_rect", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float, c_float, c_float])
        res = _fn(None, self._handle, min_y, max_x, max_y)
        return _rt.result_from_c(res)
    def destroy(self) -> _rt.Result[None]:
        """Call `yetty_yview_destroy`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yview_destroy", _t.yetty_ycore_void_result, [c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)

