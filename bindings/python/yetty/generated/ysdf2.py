"""yetty.ysdf2 bindings — GENERATED from model.yaml, do not edit."""
from __future__ import annotations
from ctypes import (c_bool, c_char, c_char_p, c_double, c_float, c_int,
    c_int8, c_int16, c_int32, c_int64, c_long, c_size_t, c_ssize_t,
    c_uint, c_uint8, c_uint16, c_uint32, c_uint64, c_ulong, c_void_p)
from typing import Any, ClassVar
from .. import runtime as _rt
from . import _types as _t
from . import ydrawlist2 as _ydrawlist2

class Circle(_ydrawlist2.Shape):
    """yclass ysdf2:circle"""
    __yclass_domain__: ClassVar[str] = 'ysdf2'
    __yclass_name__: ClassVar[str] = 'circle'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ysdf2_circle_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_ysdf2_circle_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(None))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._apply_kwargs(kwargs)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Circle':
        return cls(**kwargs)
    @property
    def center_x(self) -> float:
        """Property `center_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_circle_center_x_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @center_x.setter
    def center_x(self, value: float) -> None:
        """Property `center_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_circle_center_x_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def center_y(self) -> float:
        """Property `center_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_circle_center_y_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @center_y.setter
    def center_y(self, value: float) -> None:
        """Property `center_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_circle_center_y_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def radius(self) -> float:
        """Property `radius` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_circle_radius_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @radius.setter
    def radius(self, value: float) -> None:
        """Property `radius` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_circle_radius_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)

class Box(_ydrawlist2.Shape):
    """yclass ysdf2:box"""
    __yclass_domain__: ClassVar[str] = 'ysdf2'
    __yclass_name__: ClassVar[str] = 'box'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ysdf2_box_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_ysdf2_box_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(None))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._apply_kwargs(kwargs)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Box':
        return cls(**kwargs)
    @property
    def center_x(self) -> float:
        """Property `center_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_box_center_x_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @center_x.setter
    def center_x(self, value: float) -> None:
        """Property `center_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_box_center_x_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def center_y(self) -> float:
        """Property `center_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_box_center_y_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @center_y.setter
    def center_y(self, value: float) -> None:
        """Property `center_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_box_center_y_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def half_width(self) -> float:
        """Property `half_width` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_box_half_width_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @half_width.setter
    def half_width(self, value: float) -> None:
        """Property `half_width` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_box_half_width_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def half_height(self) -> float:
        """Property `half_height` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_box_half_height_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @half_height.setter
    def half_height(self, value: float) -> None:
        """Property `half_height` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_box_half_height_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def corner_radius(self) -> float:
        """Property `corner_radius` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_box_corner_radius_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @corner_radius.setter
    def corner_radius(self, value: float) -> None:
        """Property `corner_radius` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_box_corner_radius_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)

class Segment(_ydrawlist2.Shape):
    """yclass ysdf2:segment"""
    __yclass_domain__: ClassVar[str] = 'ysdf2'
    __yclass_name__: ClassVar[str] = 'segment'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ysdf2_segment_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_ysdf2_segment_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(None))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._apply_kwargs(kwargs)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Segment':
        return cls(**kwargs)
    @property
    def start_x(self) -> float:
        """Property `start_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_segment_start_x_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @start_x.setter
    def start_x(self, value: float) -> None:
        """Property `start_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_segment_start_x_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def start_y(self) -> float:
        """Property `start_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_segment_start_y_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @start_y.setter
    def start_y(self, value: float) -> None:
        """Property `start_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_segment_start_y_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def end_x(self) -> float:
        """Property `end_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_segment_end_x_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @end_x.setter
    def end_x(self, value: float) -> None:
        """Property `end_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_segment_end_x_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def end_y(self) -> float:
        """Property `end_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_segment_end_y_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @end_y.setter
    def end_y(self, value: float) -> None:
        """Property `end_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_segment_end_y_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)

class Triangle(_ydrawlist2.Shape):
    """yclass ysdf2:triangle"""
    __yclass_domain__: ClassVar[str] = 'ysdf2'
    __yclass_name__: ClassVar[str] = 'triangle'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ysdf2_triangle_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_ysdf2_triangle_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(None))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._apply_kwargs(kwargs)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Triangle':
        return cls(**kwargs)
    @property
    def vertex_a_x(self) -> float:
        """Property `vertex_a_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_triangle_vertex_a_x_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @vertex_a_x.setter
    def vertex_a_x(self, value: float) -> None:
        """Property `vertex_a_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_triangle_vertex_a_x_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def vertex_a_y(self) -> float:
        """Property `vertex_a_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_triangle_vertex_a_y_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @vertex_a_y.setter
    def vertex_a_y(self, value: float) -> None:
        """Property `vertex_a_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_triangle_vertex_a_y_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def vertex_b_x(self) -> float:
        """Property `vertex_b_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_triangle_vertex_b_x_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @vertex_b_x.setter
    def vertex_b_x(self, value: float) -> None:
        """Property `vertex_b_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_triangle_vertex_b_x_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def vertex_b_y(self) -> float:
        """Property `vertex_b_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_triangle_vertex_b_y_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @vertex_b_y.setter
    def vertex_b_y(self, value: float) -> None:
        """Property `vertex_b_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_triangle_vertex_b_y_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def vertex_c_x(self) -> float:
        """Property `vertex_c_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_triangle_vertex_c_x_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @vertex_c_x.setter
    def vertex_c_x(self, value: float) -> None:
        """Property `vertex_c_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_triangle_vertex_c_x_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def vertex_c_y(self) -> float:
        """Property `vertex_c_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_triangle_vertex_c_y_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @vertex_c_y.setter
    def vertex_c_y(self, value: float) -> None:
        """Property `vertex_c_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_triangle_vertex_c_y_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)

class Ellipse(_ydrawlist2.Shape):
    """yclass ysdf2:ellipse"""
    __yclass_domain__: ClassVar[str] = 'ysdf2'
    __yclass_name__: ClassVar[str] = 'ellipse'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ysdf2_ellipse_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_ysdf2_ellipse_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(None))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._apply_kwargs(kwargs)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Ellipse':
        return cls(**kwargs)
    @property
    def center_x(self) -> float:
        """Property `center_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_ellipse_center_x_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @center_x.setter
    def center_x(self, value: float) -> None:
        """Property `center_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_ellipse_center_x_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def center_y(self) -> float:
        """Property `center_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_ellipse_center_y_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @center_y.setter
    def center_y(self, value: float) -> None:
        """Property `center_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_ellipse_center_y_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def radius_x(self) -> float:
        """Property `radius_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_ellipse_radius_x_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @radius_x.setter
    def radius_x(self, value: float) -> None:
        """Property `radius_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_ellipse_radius_x_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def radius_y(self) -> float:
        """Property `radius_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_ellipse_radius_y_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @radius_y.setter
    def radius_y(self, value: float) -> None:
        """Property `radius_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_ellipse_radius_y_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)

class Arc(_ydrawlist2.Shape):
    """yclass ysdf2:arc"""
    __yclass_domain__: ClassVar[str] = 'ysdf2'
    __yclass_name__: ClassVar[str] = 'arc'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ysdf2_arc_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_ysdf2_arc_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(None))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._apply_kwargs(kwargs)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Arc':
        return cls(**kwargs)
    @property
    def center_x(self) -> float:
        """Property `center_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_arc_center_x_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @center_x.setter
    def center_x(self, value: float) -> None:
        """Property `center_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_arc_center_x_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def center_y(self) -> float:
        """Property `center_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_arc_center_y_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @center_y.setter
    def center_y(self, value: float) -> None:
        """Property `center_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_arc_center_y_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def aperture_x(self) -> float:
        """Property `aperture_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_arc_aperture_x_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @aperture_x.setter
    def aperture_x(self, value: float) -> None:
        """Property `aperture_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_arc_aperture_x_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def aperture_y(self) -> float:
        """Property `aperture_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_arc_aperture_y_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @aperture_y.setter
    def aperture_y(self, value: float) -> None:
        """Property `aperture_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_arc_aperture_y_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def radius(self) -> float:
        """Property `radius` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_arc_radius_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @radius.setter
    def radius(self, value: float) -> None:
        """Property `radius` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_arc_radius_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def thickness(self) -> float:
        """Property `thickness` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_arc_thickness_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @thickness.setter
    def thickness(self, value: float) -> None:
        """Property `thickness` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_arc_thickness_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)

class RoundedBox(_ydrawlist2.Shape):
    """yclass ysdf2:rounded_box"""
    __yclass_domain__: ClassVar[str] = 'ysdf2'
    __yclass_name__: ClassVar[str] = 'rounded_box'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ysdf2_rounded_box_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_ysdf2_rounded_box_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(None))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._apply_kwargs(kwargs)
    @classmethod
    def create(cls, **kwargs: Any) -> 'RoundedBox':
        return cls(**kwargs)
    @property
    def center_x(self) -> float:
        """Property `center_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_rounded_box_center_x_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @center_x.setter
    def center_x(self, value: float) -> None:
        """Property `center_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_rounded_box_center_x_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def center_y(self) -> float:
        """Property `center_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_rounded_box_center_y_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @center_y.setter
    def center_y(self, value: float) -> None:
        """Property `center_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_rounded_box_center_y_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def half_width(self) -> float:
        """Property `half_width` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_rounded_box_half_width_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @half_width.setter
    def half_width(self, value: float) -> None:
        """Property `half_width` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_rounded_box_half_width_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def half_height(self) -> float:
        """Property `half_height` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_rounded_box_half_height_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @half_height.setter
    def half_height(self, value: float) -> None:
        """Property `half_height` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_rounded_box_half_height_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def radius_top_right(self) -> float:
        """Property `radius_top_right` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_rounded_box_radius_top_right_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @radius_top_right.setter
    def radius_top_right(self, value: float) -> None:
        """Property `radius_top_right` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_rounded_box_radius_top_right_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def radius_bottom_right(self) -> float:
        """Property `radius_bottom_right` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_rounded_box_radius_bottom_right_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @radius_bottom_right.setter
    def radius_bottom_right(self, value: float) -> None:
        """Property `radius_bottom_right` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_rounded_box_radius_bottom_right_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def radius_top_left(self) -> float:
        """Property `radius_top_left` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_rounded_box_radius_top_left_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @radius_top_left.setter
    def radius_top_left(self, value: float) -> None:
        """Property `radius_top_left` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_rounded_box_radius_top_left_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def radius_bottom_left(self) -> float:
        """Property `radius_bottom_left` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_rounded_box_radius_bottom_left_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @radius_bottom_left.setter
    def radius_bottom_left(self, value: float) -> None:
        """Property `radius_bottom_left` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_rounded_box_radius_bottom_left_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)

class Rhombus(_ydrawlist2.Shape):
    """yclass ysdf2:rhombus"""
    __yclass_domain__: ClassVar[str] = 'ysdf2'
    __yclass_name__: ClassVar[str] = 'rhombus'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ysdf2_rhombus_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_ysdf2_rhombus_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(None))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._apply_kwargs(kwargs)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Rhombus':
        return cls(**kwargs)
    @property
    def center_x(self) -> float:
        """Property `center_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_rhombus_center_x_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @center_x.setter
    def center_x(self, value: float) -> None:
        """Property `center_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_rhombus_center_x_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def center_y(self) -> float:
        """Property `center_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_rhombus_center_y_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @center_y.setter
    def center_y(self, value: float) -> None:
        """Property `center_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_rhombus_center_y_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def half_width(self) -> float:
        """Property `half_width` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_rhombus_half_width_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @half_width.setter
    def half_width(self, value: float) -> None:
        """Property `half_width` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_rhombus_half_width_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def half_height(self) -> float:
        """Property `half_height` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_rhombus_half_height_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @half_height.setter
    def half_height(self, value: float) -> None:
        """Property `half_height` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_rhombus_half_height_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)

class Pentagon(_ydrawlist2.Shape):
    """yclass ysdf2:pentagon"""
    __yclass_domain__: ClassVar[str] = 'ysdf2'
    __yclass_name__: ClassVar[str] = 'pentagon'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ysdf2_pentagon_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_ysdf2_pentagon_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(None))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._apply_kwargs(kwargs)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Pentagon':
        return cls(**kwargs)
    @property
    def center_x(self) -> float:
        """Property `center_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_pentagon_center_x_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @center_x.setter
    def center_x(self, value: float) -> None:
        """Property `center_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_pentagon_center_x_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def center_y(self) -> float:
        """Property `center_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_pentagon_center_y_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @center_y.setter
    def center_y(self, value: float) -> None:
        """Property `center_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_pentagon_center_y_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def radius(self) -> float:
        """Property `radius` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_pentagon_radius_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @radius.setter
    def radius(self, value: float) -> None:
        """Property `radius` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_pentagon_radius_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)

class Hexagon(_ydrawlist2.Shape):
    """yclass ysdf2:hexagon"""
    __yclass_domain__: ClassVar[str] = 'ysdf2'
    __yclass_name__: ClassVar[str] = 'hexagon'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ysdf2_hexagon_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_ysdf2_hexagon_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(None))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._apply_kwargs(kwargs)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Hexagon':
        return cls(**kwargs)
    @property
    def center_x(self) -> float:
        """Property `center_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_hexagon_center_x_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @center_x.setter
    def center_x(self, value: float) -> None:
        """Property `center_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_hexagon_center_x_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def center_y(self) -> float:
        """Property `center_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_hexagon_center_y_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @center_y.setter
    def center_y(self, value: float) -> None:
        """Property `center_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_hexagon_center_y_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def radius(self) -> float:
        """Property `radius` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_hexagon_radius_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @radius.setter
    def radius(self, value: float) -> None:
        """Property `radius` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_hexagon_radius_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)

class Star(_ydrawlist2.Shape):
    """yclass ysdf2:star"""
    __yclass_domain__: ClassVar[str] = 'ysdf2'
    __yclass_name__: ClassVar[str] = 'star'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ysdf2_star_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_ysdf2_star_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(None))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._apply_kwargs(kwargs)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Star':
        return cls(**kwargs)
    @property
    def center_x(self) -> float:
        """Property `center_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_star_center_x_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @center_x.setter
    def center_x(self, value: float) -> None:
        """Property `center_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_star_center_x_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def center_y(self) -> float:
        """Property `center_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_star_center_y_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @center_y.setter
    def center_y(self, value: float) -> None:
        """Property `center_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_star_center_y_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def radius(self) -> float:
        """Property `radius` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_star_radius_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @radius.setter
    def radius(self, value: float) -> None:
        """Property `radius` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_star_radius_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def num_points(self) -> float:
        """Property `num_points` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_star_num_points_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @num_points.setter
    def num_points(self, value: float) -> None:
        """Property `num_points` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_star_num_points_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def inner_ratio(self) -> float:
        """Property `inner_ratio` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_star_inner_ratio_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @inner_ratio.setter
    def inner_ratio(self, value: float) -> None:
        """Property `inner_ratio` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_star_inner_ratio_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)

class Pie(_ydrawlist2.Shape):
    """yclass ysdf2:pie"""
    __yclass_domain__: ClassVar[str] = 'ysdf2'
    __yclass_name__: ClassVar[str] = 'pie'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ysdf2_pie_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_ysdf2_pie_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(None))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._apply_kwargs(kwargs)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Pie':
        return cls(**kwargs)
    @property
    def center_x(self) -> float:
        """Property `center_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_pie_center_x_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @center_x.setter
    def center_x(self, value: float) -> None:
        """Property `center_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_pie_center_x_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def center_y(self) -> float:
        """Property `center_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_pie_center_y_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @center_y.setter
    def center_y(self, value: float) -> None:
        """Property `center_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_pie_center_y_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def aperture_x(self) -> float:
        """Property `aperture_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_pie_aperture_x_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @aperture_x.setter
    def aperture_x(self, value: float) -> None:
        """Property `aperture_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_pie_aperture_x_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def aperture_y(self) -> float:
        """Property `aperture_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_pie_aperture_y_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @aperture_y.setter
    def aperture_y(self, value: float) -> None:
        """Property `aperture_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_pie_aperture_y_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def radius(self) -> float:
        """Property `radius` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_pie_radius_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @radius.setter
    def radius(self, value: float) -> None:
        """Property `radius` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_pie_radius_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)

class Ring(_ydrawlist2.Shape):
    """yclass ysdf2:ring"""
    __yclass_domain__: ClassVar[str] = 'ysdf2'
    __yclass_name__: ClassVar[str] = 'ring'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ysdf2_ring_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_ysdf2_ring_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(None))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._apply_kwargs(kwargs)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Ring':
        return cls(**kwargs)
    @property
    def center_x(self) -> float:
        """Property `center_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_ring_center_x_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @center_x.setter
    def center_x(self, value: float) -> None:
        """Property `center_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_ring_center_x_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def center_y(self) -> float:
        """Property `center_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_ring_center_y_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @center_y.setter
    def center_y(self, value: float) -> None:
        """Property `center_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_ring_center_y_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def normal_x(self) -> float:
        """Property `normal_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_ring_normal_x_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @normal_x.setter
    def normal_x(self, value: float) -> None:
        """Property `normal_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_ring_normal_x_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def normal_y(self) -> float:
        """Property `normal_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_ring_normal_y_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @normal_y.setter
    def normal_y(self, value: float) -> None:
        """Property `normal_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_ring_normal_y_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def radius(self) -> float:
        """Property `radius` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_ring_radius_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @radius.setter
    def radius(self, value: float) -> None:
        """Property `radius` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_ring_radius_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def thickness(self) -> float:
        """Property `thickness` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_ring_thickness_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @thickness.setter
    def thickness(self, value: float) -> None:
        """Property `thickness` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_ring_thickness_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)

class Heart(_ydrawlist2.Shape):
    """yclass ysdf2:heart"""
    __yclass_domain__: ClassVar[str] = 'ysdf2'
    __yclass_name__: ClassVar[str] = 'heart'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ysdf2_heart_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_ysdf2_heart_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(None))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._apply_kwargs(kwargs)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Heart':
        return cls(**kwargs)
    @property
    def center_x(self) -> float:
        """Property `center_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_heart_center_x_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @center_x.setter
    def center_x(self, value: float) -> None:
        """Property `center_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_heart_center_x_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def center_y(self) -> float:
        """Property `center_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_heart_center_y_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @center_y.setter
    def center_y(self, value: float) -> None:
        """Property `center_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_heart_center_y_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def scale(self) -> float:
        """Property `scale` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_heart_scale_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @scale.setter
    def scale(self, value: float) -> None:
        """Property `scale` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_heart_scale_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)

class Cross(_ydrawlist2.Shape):
    """yclass ysdf2:cross"""
    __yclass_domain__: ClassVar[str] = 'ysdf2'
    __yclass_name__: ClassVar[str] = 'cross'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ysdf2_cross_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_ysdf2_cross_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(None))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._apply_kwargs(kwargs)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Cross':
        return cls(**kwargs)
    @property
    def center_x(self) -> float:
        """Property `center_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_cross_center_x_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @center_x.setter
    def center_x(self, value: float) -> None:
        """Property `center_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_cross_center_x_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def center_y(self) -> float:
        """Property `center_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_cross_center_y_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @center_y.setter
    def center_y(self, value: float) -> None:
        """Property `center_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_cross_center_y_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def half_width(self) -> float:
        """Property `half_width` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_cross_half_width_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @half_width.setter
    def half_width(self, value: float) -> None:
        """Property `half_width` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_cross_half_width_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def half_height(self) -> float:
        """Property `half_height` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_cross_half_height_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @half_height.setter
    def half_height(self, value: float) -> None:
        """Property `half_height` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_cross_half_height_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def corner_radius(self) -> float:
        """Property `corner_radius` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_cross_corner_radius_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @corner_radius.setter
    def corner_radius(self, value: float) -> None:
        """Property `corner_radius` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_cross_corner_radius_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)

class RoundedX(_ydrawlist2.Shape):
    """yclass ysdf2:rounded_x"""
    __yclass_domain__: ClassVar[str] = 'ysdf2'
    __yclass_name__: ClassVar[str] = 'rounded_x'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ysdf2_rounded_x_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_ysdf2_rounded_x_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(None))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._apply_kwargs(kwargs)
    @classmethod
    def create(cls, **kwargs: Any) -> 'RoundedX':
        return cls(**kwargs)
    @property
    def center_x(self) -> float:
        """Property `center_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_rounded_x_center_x_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @center_x.setter
    def center_x(self, value: float) -> None:
        """Property `center_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_rounded_x_center_x_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def center_y(self) -> float:
        """Property `center_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_rounded_x_center_y_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @center_y.setter
    def center_y(self, value: float) -> None:
        """Property `center_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_rounded_x_center_y_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def width(self) -> float:
        """Property `width` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_rounded_x_width_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @width.setter
    def width(self, value: float) -> None:
        """Property `width` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_rounded_x_width_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def radius(self) -> float:
        """Property `radius` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_rounded_x_radius_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @radius.setter
    def radius(self, value: float) -> None:
        """Property `radius` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_rounded_x_radius_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)

class Capsule(_ydrawlist2.Shape):
    """yclass ysdf2:capsule"""
    __yclass_domain__: ClassVar[str] = 'ysdf2'
    __yclass_name__: ClassVar[str] = 'capsule'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ysdf2_capsule_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_ysdf2_capsule_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(None))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._apply_kwargs(kwargs)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Capsule':
        return cls(**kwargs)
    @property
    def start_x(self) -> float:
        """Property `start_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_capsule_start_x_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @start_x.setter
    def start_x(self, value: float) -> None:
        """Property `start_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_capsule_start_x_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def start_y(self) -> float:
        """Property `start_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_capsule_start_y_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @start_y.setter
    def start_y(self, value: float) -> None:
        """Property `start_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_capsule_start_y_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def end_x(self) -> float:
        """Property `end_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_capsule_end_x_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @end_x.setter
    def end_x(self, value: float) -> None:
        """Property `end_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_capsule_end_x_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def end_y(self) -> float:
        """Property `end_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_capsule_end_y_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @end_y.setter
    def end_y(self, value: float) -> None:
        """Property `end_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_capsule_end_y_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def radius(self) -> float:
        """Property `radius` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_capsule_radius_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @radius.setter
    def radius(self, value: float) -> None:
        """Property `radius` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_capsule_radius_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)

class Moon(_ydrawlist2.Shape):
    """yclass ysdf2:moon"""
    __yclass_domain__: ClassVar[str] = 'ysdf2'
    __yclass_name__: ClassVar[str] = 'moon'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ysdf2_moon_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_ysdf2_moon_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(None))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._apply_kwargs(kwargs)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Moon':
        return cls(**kwargs)
    @property
    def center_x(self) -> float:
        """Property `center_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_moon_center_x_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @center_x.setter
    def center_x(self, value: float) -> None:
        """Property `center_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_moon_center_x_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def center_y(self) -> float:
        """Property `center_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_moon_center_y_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @center_y.setter
    def center_y(self, value: float) -> None:
        """Property `center_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_moon_center_y_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def offset(self) -> float:
        """Property `offset` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_moon_offset_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @offset.setter
    def offset(self, value: float) -> None:
        """Property `offset` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_moon_offset_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def radius_outer(self) -> float:
        """Property `radius_outer` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_moon_radius_outer_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @radius_outer.setter
    def radius_outer(self, value: float) -> None:
        """Property `radius_outer` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_moon_radius_outer_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def radius_inner(self) -> float:
        """Property `radius_inner` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_moon_radius_inner_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @radius_inner.setter
    def radius_inner(self, value: float) -> None:
        """Property `radius_inner` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_moon_radius_inner_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)

class Egg(_ydrawlist2.Shape):
    """yclass ysdf2:egg"""
    __yclass_domain__: ClassVar[str] = 'ysdf2'
    __yclass_name__: ClassVar[str] = 'egg'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ysdf2_egg_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_ysdf2_egg_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(None))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._apply_kwargs(kwargs)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Egg':
        return cls(**kwargs)
    @property
    def center_x(self) -> float:
        """Property `center_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_egg_center_x_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @center_x.setter
    def center_x(self, value: float) -> None:
        """Property `center_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_egg_center_x_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def center_y(self) -> float:
        """Property `center_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_egg_center_y_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @center_y.setter
    def center_y(self, value: float) -> None:
        """Property `center_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_egg_center_y_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def radius_outer(self) -> float:
        """Property `radius_outer` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_egg_radius_outer_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @radius_outer.setter
    def radius_outer(self, value: float) -> None:
        """Property `radius_outer` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_egg_radius_outer_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def radius_inner(self) -> float:
        """Property `radius_inner` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_egg_radius_inner_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @radius_inner.setter
    def radius_inner(self, value: float) -> None:
        """Property `radius_inner` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_egg_radius_inner_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)

class Octogon(_ydrawlist2.Shape):
    """yclass ysdf2:octogon"""
    __yclass_domain__: ClassVar[str] = 'ysdf2'
    __yclass_name__: ClassVar[str] = 'octogon'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ysdf2_octogon_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_ysdf2_octogon_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(None))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._apply_kwargs(kwargs)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Octogon':
        return cls(**kwargs)
    @property
    def center_x(self) -> float:
        """Property `center_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_octogon_center_x_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @center_x.setter
    def center_x(self, value: float) -> None:
        """Property `center_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_octogon_center_x_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def center_y(self) -> float:
        """Property `center_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_octogon_center_y_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @center_y.setter
    def center_y(self, value: float) -> None:
        """Property `center_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_octogon_center_y_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def radius(self) -> float:
        """Property `radius` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_octogon_radius_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @radius.setter
    def radius(self, value: float) -> None:
        """Property `radius` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_octogon_radius_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)

class Hexagram(_ydrawlist2.Shape):
    """yclass ysdf2:hexagram"""
    __yclass_domain__: ClassVar[str] = 'ysdf2'
    __yclass_name__: ClassVar[str] = 'hexagram'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ysdf2_hexagram_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_ysdf2_hexagram_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(None))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._apply_kwargs(kwargs)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Hexagram':
        return cls(**kwargs)
    @property
    def center_x(self) -> float:
        """Property `center_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_hexagram_center_x_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @center_x.setter
    def center_x(self, value: float) -> None:
        """Property `center_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_hexagram_center_x_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def center_y(self) -> float:
        """Property `center_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_hexagram_center_y_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @center_y.setter
    def center_y(self, value: float) -> None:
        """Property `center_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_hexagram_center_y_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def radius(self) -> float:
        """Property `radius` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_hexagram_radius_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @radius.setter
    def radius(self, value: float) -> None:
        """Property `radius` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_hexagram_radius_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)

class Pentagram(_ydrawlist2.Shape):
    """yclass ysdf2:pentagram"""
    __yclass_domain__: ClassVar[str] = 'ysdf2'
    __yclass_name__: ClassVar[str] = 'pentagram'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ysdf2_pentagram_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_ysdf2_pentagram_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(None))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._apply_kwargs(kwargs)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Pentagram':
        return cls(**kwargs)
    @property
    def center_x(self) -> float:
        """Property `center_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_pentagram_center_x_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @center_x.setter
    def center_x(self, value: float) -> None:
        """Property `center_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_pentagram_center_x_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def center_y(self) -> float:
        """Property `center_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_pentagram_center_y_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @center_y.setter
    def center_y(self, value: float) -> None:
        """Property `center_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_pentagram_center_y_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def radius(self) -> float:
        """Property `radius` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_pentagram_radius_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @radius.setter
    def radius(self, value: float) -> None:
        """Property `radius` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_pentagram_radius_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)

class LinearGradientBox(_ydrawlist2.Shape):
    """yclass ysdf2:linear_gradient_box"""
    __yclass_domain__: ClassVar[str] = 'ysdf2'
    __yclass_name__: ClassVar[str] = 'linear_gradient_box'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ysdf2_linear_gradient_box_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_ysdf2_linear_gradient_box_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(None))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._apply_kwargs(kwargs)
    @classmethod
    def create(cls, **kwargs: Any) -> 'LinearGradientBox':
        return cls(**kwargs)
    @property
    def center_x(self) -> float:
        """Property `center_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_linear_gradient_box_center_x_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @center_x.setter
    def center_x(self, value: float) -> None:
        """Property `center_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_linear_gradient_box_center_x_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def center_y(self) -> float:
        """Property `center_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_linear_gradient_box_center_y_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @center_y.setter
    def center_y(self, value: float) -> None:
        """Property `center_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_linear_gradient_box_center_y_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def half_width(self) -> float:
        """Property `half_width` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_linear_gradient_box_half_width_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @half_width.setter
    def half_width(self, value: float) -> None:
        """Property `half_width` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_linear_gradient_box_half_width_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def half_height(self) -> float:
        """Property `half_height` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_linear_gradient_box_half_height_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @half_height.setter
    def half_height(self, value: float) -> None:
        """Property `half_height` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_linear_gradient_box_half_height_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def corner_radius(self) -> float:
        """Property `corner_radius` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_linear_gradient_box_corner_radius_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @corner_radius.setter
    def corner_radius(self, value: float) -> None:
        """Property `corner_radius` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_linear_gradient_box_corner_radius_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def grad_x0(self) -> float:
        """Property `grad_x0` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_linear_gradient_box_grad_x0_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @grad_x0.setter
    def grad_x0(self, value: float) -> None:
        """Property `grad_x0` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_linear_gradient_box_grad_x0_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def grad_y0(self) -> float:
        """Property `grad_y0` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_linear_gradient_box_grad_y0_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @grad_y0.setter
    def grad_y0(self, value: float) -> None:
        """Property `grad_y0` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_linear_gradient_box_grad_y0_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def grad_x1(self) -> float:
        """Property `grad_x1` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_linear_gradient_box_grad_x1_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @grad_x1.setter
    def grad_x1(self, value: float) -> None:
        """Property `grad_x1` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_linear_gradient_box_grad_x1_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def grad_y1(self) -> float:
        """Property `grad_y1` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_linear_gradient_box_grad_y1_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @grad_y1.setter
    def grad_y1(self, value: float) -> None:
        """Property `grad_y1` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_linear_gradient_box_grad_y1_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def color0(self) -> int:
        """Property `color0` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_linear_gradient_box_color0_get", _t.uint32_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @color0.setter
    def color0(self, value: int) -> None:
        """Property `color0` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_linear_gradient_box_color0_set", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def color1(self) -> int:
        """Property `color1` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_linear_gradient_box_color1_get", _t.uint32_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @color1.setter
    def color1(self, value: int) -> None:
        """Property `color1` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_linear_gradient_box_color1_set", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)

class RadialGradientBox(_ydrawlist2.Shape):
    """yclass ysdf2:radial_gradient_box"""
    __yclass_domain__: ClassVar[str] = 'ysdf2'
    __yclass_name__: ClassVar[str] = 'radial_gradient_box'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ysdf2_radial_gradient_box_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_ysdf2_radial_gradient_box_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(None))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._apply_kwargs(kwargs)
    @classmethod
    def create(cls, **kwargs: Any) -> 'RadialGradientBox':
        return cls(**kwargs)
    @property
    def center_x(self) -> float:
        """Property `center_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_radial_gradient_box_center_x_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @center_x.setter
    def center_x(self, value: float) -> None:
        """Property `center_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_radial_gradient_box_center_x_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def center_y(self) -> float:
        """Property `center_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_radial_gradient_box_center_y_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @center_y.setter
    def center_y(self, value: float) -> None:
        """Property `center_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_radial_gradient_box_center_y_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def half_width(self) -> float:
        """Property `half_width` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_radial_gradient_box_half_width_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @half_width.setter
    def half_width(self, value: float) -> None:
        """Property `half_width` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_radial_gradient_box_half_width_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def half_height(self) -> float:
        """Property `half_height` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_radial_gradient_box_half_height_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @half_height.setter
    def half_height(self, value: float) -> None:
        """Property `half_height` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_radial_gradient_box_half_height_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def corner_radius(self) -> float:
        """Property `corner_radius` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_radial_gradient_box_corner_radius_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @corner_radius.setter
    def corner_radius(self, value: float) -> None:
        """Property `corner_radius` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_radial_gradient_box_corner_radius_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def grad_cx(self) -> float:
        """Property `grad_cx` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_radial_gradient_box_grad_cx_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @grad_cx.setter
    def grad_cx(self, value: float) -> None:
        """Property `grad_cx` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_radial_gradient_box_grad_cx_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def grad_cy(self) -> float:
        """Property `grad_cy` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_radial_gradient_box_grad_cy_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @grad_cy.setter
    def grad_cy(self, value: float) -> None:
        """Property `grad_cy` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_radial_gradient_box_grad_cy_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def grad_radius(self) -> float:
        """Property `grad_radius` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_radial_gradient_box_grad_radius_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @grad_radius.setter
    def grad_radius(self, value: float) -> None:
        """Property `grad_radius` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_radial_gradient_box_grad_radius_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def color_inner(self) -> int:
        """Property `color_inner` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_radial_gradient_box_color_inner_get", _t.uint32_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @color_inner.setter
    def color_inner(self, value: int) -> None:
        """Property `color_inner` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_radial_gradient_box_color_inner_set", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def color_outer(self) -> int:
        """Property `color_outer` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_radial_gradient_box_color_outer_get", _t.uint32_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @color_outer.setter
    def color_outer(self, value: int) -> None:
        """Property `color_outer` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_radial_gradient_box_color_outer_set", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)

class Sphere3d(_ydrawlist2.Shape):
    """yclass ysdf2:sphere_3d"""
    __yclass_domain__: ClassVar[str] = 'ysdf2'
    __yclass_name__: ClassVar[str] = 'sphere_3d'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ysdf2_sphere_3d_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_ysdf2_sphere_3d_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(None))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._apply_kwargs(kwargs)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Sphere3d':
        return cls(**kwargs)
    @property
    def position_x(self) -> float:
        """Property `position_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_sphere_3d_position_x_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @position_x.setter
    def position_x(self, value: float) -> None:
        """Property `position_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_sphere_3d_position_x_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def position_y(self) -> float:
        """Property `position_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_sphere_3d_position_y_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @position_y.setter
    def position_y(self, value: float) -> None:
        """Property `position_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_sphere_3d_position_y_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def position_z(self) -> float:
        """Property `position_z` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_sphere_3d_position_z_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @position_z.setter
    def position_z(self, value: float) -> None:
        """Property `position_z` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_sphere_3d_position_z_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def radius(self) -> float:
        """Property `radius` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_sphere_3d_radius_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @radius.setter
    def radius(self, value: float) -> None:
        """Property `radius` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_sphere_3d_radius_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)

class Box3d(_ydrawlist2.Shape):
    """yclass ysdf2:box_3d"""
    __yclass_domain__: ClassVar[str] = 'ysdf2'
    __yclass_name__: ClassVar[str] = 'box_3d'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ysdf2_box_3d_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_ysdf2_box_3d_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(None))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._apply_kwargs(kwargs)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Box3d':
        return cls(**kwargs)
    @property
    def position_x(self) -> float:
        """Property `position_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_box_3d_position_x_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @position_x.setter
    def position_x(self, value: float) -> None:
        """Property `position_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_box_3d_position_x_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def position_y(self) -> float:
        """Property `position_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_box_3d_position_y_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @position_y.setter
    def position_y(self, value: float) -> None:
        """Property `position_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_box_3d_position_y_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def position_z(self) -> float:
        """Property `position_z` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_box_3d_position_z_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @position_z.setter
    def position_z(self, value: float) -> None:
        """Property `position_z` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_box_3d_position_z_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def half_size_x(self) -> float:
        """Property `half_size_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_box_3d_half_size_x_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @half_size_x.setter
    def half_size_x(self, value: float) -> None:
        """Property `half_size_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_box_3d_half_size_x_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def half_size_y(self) -> float:
        """Property `half_size_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_box_3d_half_size_y_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @half_size_y.setter
    def half_size_y(self, value: float) -> None:
        """Property `half_size_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_box_3d_half_size_y_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def half_size_z(self) -> float:
        """Property `half_size_z` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_box_3d_half_size_z_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @half_size_z.setter
    def half_size_z(self, value: float) -> None:
        """Property `half_size_z` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_box_3d_half_size_z_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)

class Torus3d(_ydrawlist2.Shape):
    """yclass ysdf2:torus_3d"""
    __yclass_domain__: ClassVar[str] = 'ysdf2'
    __yclass_name__: ClassVar[str] = 'torus_3d'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ysdf2_torus_3d_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_ysdf2_torus_3d_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(None))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._apply_kwargs(kwargs)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Torus3d':
        return cls(**kwargs)
    @property
    def position_x(self) -> float:
        """Property `position_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_torus_3d_position_x_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @position_x.setter
    def position_x(self, value: float) -> None:
        """Property `position_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_torus_3d_position_x_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def position_y(self) -> float:
        """Property `position_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_torus_3d_position_y_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @position_y.setter
    def position_y(self, value: float) -> None:
        """Property `position_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_torus_3d_position_y_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def position_z(self) -> float:
        """Property `position_z` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_torus_3d_position_z_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @position_z.setter
    def position_z(self, value: float) -> None:
        """Property `position_z` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_torus_3d_position_z_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def major_radius(self) -> float:
        """Property `major_radius` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_torus_3d_major_radius_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @major_radius.setter
    def major_radius(self, value: float) -> None:
        """Property `major_radius` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_torus_3d_major_radius_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def minor_radius(self) -> float:
        """Property `minor_radius` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_torus_3d_minor_radius_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @minor_radius.setter
    def minor_radius(self, value: float) -> None:
        """Property `minor_radius` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_torus_3d_minor_radius_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)

class Cylinder3d(_ydrawlist2.Shape):
    """yclass ysdf2:cylinder_3d"""
    __yclass_domain__: ClassVar[str] = 'ysdf2'
    __yclass_name__: ClassVar[str] = 'cylinder_3d'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ysdf2_cylinder_3d_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_ysdf2_cylinder_3d_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(None))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._apply_kwargs(kwargs)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Cylinder3d':
        return cls(**kwargs)
    @property
    def position_x(self) -> float:
        """Property `position_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_cylinder_3d_position_x_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @position_x.setter
    def position_x(self, value: float) -> None:
        """Property `position_x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_cylinder_3d_position_x_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def position_y(self) -> float:
        """Property `position_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_cylinder_3d_position_y_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @position_y.setter
    def position_y(self, value: float) -> None:
        """Property `position_y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_cylinder_3d_position_y_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def position_z(self) -> float:
        """Property `position_z` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_cylinder_3d_position_z_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @position_z.setter
    def position_z(self, value: float) -> None:
        """Property `position_z` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_cylinder_3d_position_z_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def radius(self) -> float:
        """Property `radius` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_cylinder_3d_radius_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @radius.setter
    def radius(self, value: float) -> None:
        """Property `radius` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_cylinder_3d_radius_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def half_height(self) -> float:
        """Property `half_height` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_cylinder_3d_half_height_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @half_height.setter
    def half_height(self, value: float) -> None:
        """Property `half_height` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ysdf2_cylinder_3d_half_height_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)

