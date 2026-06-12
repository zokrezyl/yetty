"""yetty.yplatform bindings — GENERATED from model.yaml, do not edit."""
from __future__ import annotations
from ctypes import (c_bool, c_char, c_char_p, c_double, c_float, c_int,
    c_int8, c_int16, c_int32, c_int64, c_long, c_size_t, c_ssize_t,
    c_uint, c_uint8, c_uint16, c_uint32, c_uint64, c_ulong, c_void_p)
from typing import Any, ClassVar
from .. import runtime as _rt
from . import _types as _t

class WindowManager(_rt.YClass):
    """yclass yplatform:window_manager"""
    __yclass_domain__: ClassVar[str] = 'yplatform'
    __yclass_name__: ClassVar[str] = 'window_manager'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_yplatform_window_manager_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_yplatform_window_manager_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['WindowManager']:
        obj = cls()
        return obj.init_result
    def window_manager_configure(self, os_window: Any, output_pipe: Any, input_pipe: Any) -> _rt.Result[None]:
        """Call `yetty_yplatform_window_manager_configure`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yplatform_window_manager_configure", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, os_window, output_pipe, input_pipe)
        return _rt.result_from_c(res)
    def window_manager_destroy(self) -> _rt.Result[None]:
        """Call `yetty_yplatform_window_manager_destroy`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yplatform_window_manager_destroy", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def window_manager_iconify(self) -> _rt.Result[None]:
        """Call `yetty_yplatform_window_manager_iconify`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yplatform_window_manager_iconify", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def window_manager_toggle_maximize(self) -> _rt.Result[None]:
        """Call `yetty_yplatform_window_manager_toggle_maximize`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yplatform_window_manager_toggle_maximize", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def window_manager_request_close(self) -> _rt.Result[None]:
        """Call `yetty_yplatform_window_manager_request_close`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yplatform_window_manager_request_close", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def window_manager_drag_by(self, dx: int, dy: int) -> _rt.Result[None]:
        """Call `yetty_yplatform_window_manager_drag_by`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yplatform_window_manager_drag_by", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_int, c_int])
        res = _fn(None, self._handle, dx, dy)
        return _rt.result_from_c(res)
    def window_manager_resize_by(self, dx: int, dy: int, edge: int) -> _rt.Result[None]:
        """Call `yetty_yplatform_window_manager_resize_by`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yplatform_window_manager_resize_by", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_int, c_int, c_int])
        res = _fn(None, self._handle, dx, dy, edge)
        return _rt.result_from_c(res)
    def window_manager_begin_interactive_move(self) -> _rt.Result[None]:
        """Call `yetty_yplatform_window_manager_begin_interactive_move`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yplatform_window_manager_begin_interactive_move", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def window_manager_begin_interactive_resize(self, edge: int) -> _rt.Result[None]:
        """Call `yetty_yplatform_window_manager_begin_interactive_resize`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yplatform_window_manager_begin_interactive_resize", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_int])
        res = _fn(None, self._handle, edge)
        return _rt.result_from_c(res)
    def window_manager_set_cursor(self, shape: int) -> _rt.Result[None]:
        """Call `yetty_yplatform_window_manager_set_cursor`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yplatform_window_manager_set_cursor", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_int])
        res = _fn(None, self._handle, shape)
        return _rt.result_from_c(res)
    def window_manager_handle_event(self, event: Any) -> _rt.Result[None]:
        """Call `yetty_yplatform_window_manager_handle_event`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yplatform_window_manager_handle_event", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, event)
        return _rt.result_from_c(res)

