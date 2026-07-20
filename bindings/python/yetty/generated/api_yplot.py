"""yetty.api_yplot bindings — GENERATED from model.yaml, do not edit."""
from __future__ import annotations
from ctypes import (c_bool, c_char, c_char_p, c_double, c_float, c_int,
    c_int8, c_int16, c_int32, c_int64, c_long, c_size_t, c_ssize_t,
    c_uint, c_uint8, c_uint16, c_uint32, c_uint64, c_ulong, c_void_p)
from typing import Any, ClassVar
from .. import runtime as _rt
from . import _types as _t

class Plot(_rt.YClass):
    """yclass api_yplot:plot"""
    __yclass_domain__: ClassVar[str] = 'api_yplot'
    __yclass_name__: ClassVar[str] = 'plot'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_api_yplot_plot_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_api_yplot_plot_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if res.error is not None:
                raise _rt.YettyError(res.error.message)
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls, expression: Any = None, **kwargs: Any) -> 'Plot':
        obj = cls()
        if expression is not None:
            obj.set_expression(expression)
        for _key, _value in kwargs.items():
            _setter = getattr(obj, "set_" + _key, None)
            if _setter is None:
                raise TypeError(f"Plot.create: unknown property {_key!r}")
            _setter(*_value) if isinstance(_value, (tuple, list)) else _setter(_value)
        return obj
    def set_expression(self, source: str | bytes | None) -> None:
        """Call `yetty_api_yplot_set_expression`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_api_yplot_set_expression", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
        res = _rt.result_from_c(_fn(self._handle, _rt.cstr(source)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def add_function(self, function: Any) -> None:
        """Call `yetty_api_yplot_add_function`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_api_yplot_add_function", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _rt.result_from_c(_fn(self._handle, _rt.handle(function)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def set_title(self, title: str | bytes | None) -> None:
        """Call `yetty_api_yplot_set_title`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_api_yplot_set_title", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
        res = _rt.result_from_c(_fn(self._handle, _rt.cstr(title)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def set_x_label(self, label: str | bytes | None) -> None:
        """Call `yetty_api_yplot_set_x_label`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_api_yplot_set_x_label", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
        res = _rt.result_from_c(_fn(self._handle, _rt.cstr(label)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def set_y_label(self, label: str | bytes | None) -> None:
        """Call `yetty_api_yplot_set_y_label`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_api_yplot_set_y_label", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
        res = _rt.result_from_c(_fn(self._handle, _rt.cstr(label)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def set_size(self, width: float, height: float) -> None:
        """Call `yetty_api_yplot_set_size`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_api_yplot_set_size", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float])
        res = _rt.result_from_c(_fn(self._handle, width, height))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def set_x_range(self, min: float, max: float) -> None:
        """Call `yetty_api_yplot_set_x_range`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_api_yplot_set_x_range", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float])
        res = _rt.result_from_c(_fn(self._handle, min, max))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def set_y_range(self, min: float, max: float) -> None:
        """Call `yetty_api_yplot_set_y_range`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_api_yplot_set_y_range", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float])
        res = _rt.result_from_c(_fn(self._handle, min, max))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def show(self) -> None:
        """Call `yetty_api_yplot_show`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_api_yplot_show", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def destroy(self) -> None:
        """Call `yetty_api_yplot_destroy`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_api_yplot_destroy", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value

class Function(_rt.YClass):
    """yclass api_yplot:function"""
    __yclass_domain__: ClassVar[str] = 'api_yplot'
    __yclass_name__: ClassVar[str] = 'function'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_api_yplot_function_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_api_yplot_function_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if res.error is not None:
                raise _rt.YettyError(res.error.message)
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls, body: Any = None, **kwargs: Any) -> 'Function':
        obj = cls()
        if body is not None:
            obj.set_body(body)
        for _key, _value in kwargs.items():
            _setter = getattr(obj, "set_" + _key, None)
            if _setter is None:
                raise TypeError(f"Function.create: unknown property {_key!r}")
            _setter(*_value) if isinstance(_value, (tuple, list)) else _setter(_value)
        return obj
    def set_body(self, body: str | bytes | None) -> None:
        """Call `yetty_api_yplot_set_body`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_api_yplot_set_body", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
        res = _rt.result_from_c(_fn(self._handle, _rt.cstr(body)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def set_name(self, name: str | bytes | None) -> None:
        """Call `yetty_api_yplot_set_name`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_api_yplot_set_name", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
        res = _rt.result_from_c(_fn(self._handle, _rt.cstr(name)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def set_color(self, color: str | bytes | None) -> None:
        """Call `yetty_api_yplot_set_color`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_api_yplot_set_color", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
        res = _rt.result_from_c(_fn(self._handle, _rt.cstr(color)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value

