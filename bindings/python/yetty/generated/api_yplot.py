"""yetty.api_yplot bindings — GENERATED from model.yaml, do not edit."""
from __future__ import annotations
from ctypes import (c_bool, c_char, c_char_p, c_double, c_float, c_int,
    c_int8, c_int16, c_int32, c_int64, c_long, c_size_t, c_ssize_t,
    c_uint, c_uint8, c_uint16, c_uint32, c_uint64, c_ulong, c_void_p)
from typing import Any, ClassVar
from .. import runtime as _rt
from . import _types as _t
from . import ydrawlist2 as _ydrawlist2

class Curve(_rt.YClass):
    """yclass api_yplot:curve"""
    __yclass_domain__: ClassVar[str] = 'api_yplot'
    __yclass_name__: ClassVar[str] = 'curve'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_api_yplot_curve_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_api_yplot_curve_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(None))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._owned = True
        self._apply_kwargs(kwargs)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Curve':
        return cls(**kwargs)
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

class Function(Curve):
    """yclass api_yplot:function"""
    __yclass_domain__: ClassVar[str] = 'api_yplot'
    __yclass_name__: ClassVar[str] = 'function'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_api_yplot_function_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, body: Any = None, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_api_yplot_function_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(None))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._owned = True
        if body is not None:
            self.set_body(body)
        self._apply_kwargs(kwargs)
    @classmethod
    def create(cls, body: Any = None, **kwargs: Any) -> 'Function':
        return cls(body, **kwargs)
    def set_body(self, body: str | bytes | None) -> None:
        """Call `yetty_api_yplot_set_body`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_api_yplot_set_body", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
        res = _rt.result_from_c(_fn(self._handle, _rt.cstr(body)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value

class Buffer(Curve):
    """yclass api_yplot:buffer"""
    __yclass_domain__: ClassVar[str] = 'api_yplot'
    __yclass_name__: ClassVar[str] = 'buffer'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_api_yplot_buffer_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, name: Any = None, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_api_yplot_buffer_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(None))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._owned = True
        if name is not None:
            self.set_name(name)
        self._apply_kwargs(kwargs)
    @classmethod
    def create(cls, name: Any = None, **kwargs: Any) -> 'Buffer':
        return cls(name, **kwargs)
    def set_values(self, samples: _t.yetty_ycore_buffer) -> None:
        """Call `yetty_api_yplot_set_values`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_api_yplot_set_values", _t.yetty_ycore_void_result, [c_void_p, _t.yetty_ycore_buffer])
        res = _rt.result_from_c(_fn(self._handle, _rt.as_buffer(samples)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @property
    def size(self) -> int:
        """Property `size` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_api_yplot_buffer_size_get", _t.uint32_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @size.setter
    def size(self, value: int) -> None:
        """Property `size` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_api_yplot_buffer_size_set", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)

class Plot(_ydrawlist2.Drawable):
    """yclass api_yplot:plot"""
    __yclass_domain__: ClassVar[str] = 'api_yplot'
    __yclass_name__: ClassVar[str] = 'plot'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_api_yplot_plot_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, expression: Any = None, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_api_yplot_plot_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(None))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._owned = True
        if expression is not None:
            self.set_expression(expression)
        self._apply_kwargs(kwargs)
    @classmethod
    def create(cls, expression: Any = None, **kwargs: Any) -> 'Plot':
        return cls(expression, **kwargs)
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
    def add_buffer(self, buffer: Any) -> None:
        """Call `yetty_api_yplot_add_buffer`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_api_yplot_add_buffer", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _rt.result_from_c(_fn(self._handle, _rt.handle(buffer)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def set_view(self, x_min: float, x_max: float, y_min: float, y_max: float) -> None:
        """Call `yetty_api_yplot_set_view`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_api_yplot_set_view", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float, c_float, c_float])
        res = _rt.result_from_c(_fn(self._handle, x_min, x_max, y_min, y_max))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def set_nogrid(self, disabled: int) -> None:
        """Call `yetty_api_yplot_set_nogrid`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_api_yplot_set_nogrid", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, disabled))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def set_noaxes(self, disabled: int) -> None:
        """Call `yetty_api_yplot_set_noaxes`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_api_yplot_set_noaxes", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, disabled))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def set_nolabels(self, disabled: int) -> None:
        """Call `yetty_api_yplot_set_nolabels`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_api_yplot_set_nolabels", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, disabled))
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
        """Call `yetty_api_yplot_destroy`; idempotent; raises _rt.YettyError on failure."""
        if self._handle is None:
            return None
        _fn = _rt.cfn("yetty_api_yplot_destroy", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        self._handle = None
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @property
    def x(self) -> float:
        """Property `x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_api_yplot_plot_x_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @x.setter
    def x(self, value: float) -> None:
        """Property `x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_api_yplot_plot_x_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def y(self) -> float:
        """Property `y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_api_yplot_plot_y_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @y.setter
    def y(self, value: float) -> None:
        """Property `y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_api_yplot_plot_y_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def width(self) -> float:
        """Property `width` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_api_yplot_plot_width_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @width.setter
    def width(self, value: float) -> None:
        """Property `width` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_api_yplot_plot_width_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def height(self) -> float:
        """Property `height` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_api_yplot_plot_height_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @height.setter
    def height(self, value: float) -> None:
        """Property `height` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_api_yplot_plot_height_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
