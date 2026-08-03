"""yetty.ytermsink bindings — GENERATED from model.yaml, do not edit."""
from __future__ import annotations
from ctypes import (c_bool, c_char, c_char_p, c_double, c_float, c_int,
    c_int8, c_int16, c_int32, c_int64, c_long, c_size_t, c_ssize_t,
    c_uint, c_uint8, c_uint16, c_uint32, c_uint64, c_ulong, c_void_p)
from typing import Any, ClassVar
from .. import runtime as _rt
from . import _types as _t

class Sink(_rt.YClass):
    """yclass ytermsink:sink"""
    __yclass_domain__: ClassVar[str] = 'ytermsink'
    __yclass_name__: ClassVar[str] = 'sink'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ytermsink_sink_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ytermsink_sink_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if res.error is not None:
                raise _rt.YettyError(res.error.message)
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Sink':
        obj = cls()
        for _key, _value in kwargs.items():
            _setter = getattr(obj, "set_" + _key, None)
            if _setter is None:
                raise TypeError(f"Sink.create: unknown property {_key!r}")
            _setter(*_value) if isinstance(_value, (tuple, list)) else _setter(_value)
        return obj
    def pty_write(self, data: str | bytes | None, len: int) -> None:
        """Call `yetty_ytermsink_pty_write`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ytermsink_pty_write", _t.yetty_ycore_void_result, [c_void_p, c_char_p, c_size_t])
        res = _rt.result_from_c(_fn(self._handle, _rt.cstr(data), len))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def request_render(self) -> None:
        """Call `yetty_ytermsink_request_render`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ytermsink_request_render", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def mouse_sub(self, click_enabled: int, move_enabled: int, key_enabled: int) -> None:
        """Call `yetty_ytermsink_mouse_sub`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ytermsink_mouse_sub", _t.yetty_ycore_void_result, [c_void_p, c_int, c_int, c_int])
        res = _rt.result_from_c(_fn(self._handle, click_enabled, move_enabled, key_enabled))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def clipboard_write(self, text: str | bytes | None, len: int, clipboard: int) -> None:
        """Call `yetty_ytermsink_clipboard_write`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ytermsink_clipboard_write", _t.yetty_ycore_void_result, [c_void_p, c_char_p, c_size_t, c_int])
        res = _rt.result_from_c(_fn(self._handle, _rt.cstr(text), len, clipboard))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def sixel_write(self, data: str | bytes | None, len: int) -> None:
        """Call `yetty_ytermsink_sixel_write`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ytermsink_sixel_write", _t.yetty_ycore_void_result, [c_void_p, c_char_p, c_size_t])
        res = _rt.result_from_c(_fn(self._handle, _rt.cstr(data), len))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value

