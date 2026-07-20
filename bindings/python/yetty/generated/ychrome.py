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
            if res.error is not None:
                raise _rt.YettyError(res.error.message)
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Chrome':
        obj = cls()
        for _key, _value in kwargs.items():
            _setter = getattr(obj, "set_" + _key, None)
            if _setter is None:
                raise TypeError(f"Chrome.create: unknown property {_key!r}")
            _setter(*_value) if isinstance(_value, (tuple, list)) else _setter(_value)
        return obj
    def configure(self, window_chrome: Any, caption_height: float, edge_size: float, flags: int) -> None:
        """Call `yetty_ychrome_configure`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ychrome_configure", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_float, c_float, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, _rt.handle(window_chrome), caption_height, edge_size, flags))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def set_size(self, width: float, height: float) -> None:
        """Call `yetty_ychrome_set_size`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ychrome_set_size", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float])
        res = _rt.result_from_c(_fn(self._handle, width, height))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def destroy(self) -> None:
        """Call `yetty_ychrome_destroy`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ychrome_destroy", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def edge_cursor_at(self, x: float, y: float) -> int:
        """Call `yetty_ychrome_edge_cursor_at`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ychrome_edge_cursor_at", _t.yetty_ycore_int_result, [c_void_p, c_float, c_float])
        res = _rt.result_from_c(_fn(self._handle, x, y))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def render(self) -> Any:
        """Call `yetty_ychrome_render`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ychrome_render", _t.yetty_ydraw_drawable_list_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def handle_event(self, event: Any) -> int:
        """Call `yetty_ychrome_handle_event`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ychrome_handle_event", _t.yetty_ycore_int_result, [c_void_p, c_void_p])
        res = _rt.result_from_c(_fn(self._handle, _rt.handle(event)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value

def hover_button(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ychrome_hover_button`."""
    _fn = _rt.cfn("yetty_ychrome_hover_button", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def in_gesture(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ychrome_in_gesture`."""
    _fn = _rt.cfn("yetty_ychrome_in_gesture", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

