"""yetty.ydrawlist2 bindings — GENERATED from model.yaml, do not edit."""
from __future__ import annotations
from ctypes import (c_bool, c_char, c_char_p, c_double, c_float, c_int,
    c_int8, c_int16, c_int32, c_int64, c_long, c_size_t, c_ssize_t,
    c_uint, c_uint8, c_uint16, c_uint32, c_uint64, c_ulong, c_void_p)
from typing import Any, ClassVar
from .. import runtime as _rt
from . import _types as _t

class Drawable(_rt.YClass):
    """yclass ydrawlist2:drawable"""
    __yclass_domain__: ClassVar[str] = 'ydrawlist2'
    __yclass_name__: ClassVar[str] = 'drawable'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ydrawlist2_drawable_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_ydrawlist2_drawable_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(None))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._owned = True
        self._apply_kwargs(kwargs)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Drawable':
        return cls(**kwargs)
    def pack(self, list: Any) -> None:
        """Call `yetty_ydrawlist2_pack`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ydrawlist2_pack", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _rt.result_from_c(_fn(self._handle, _rt.handle(list)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value

class Font(Drawable):
    """yclass ydrawlist2:font"""
    __yclass_domain__: ClassVar[str] = 'ydrawlist2'
    __yclass_name__: ClassVar[str] = 'font'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ydrawlist2_font_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_ydrawlist2_font_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(None))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._owned = True
        self._apply_kwargs(kwargs)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Font':
        return cls(**kwargs)
    def set_name(self, name: str | bytes | None) -> None:
        """Call `yetty_ydrawlist2_set_name`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ydrawlist2_set_name", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
        res = _rt.result_from_c(_fn(self._handle, _rt.cstr(name)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @property
    def font_id(self) -> int:
        """Property `font_id` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ydrawlist2_font_font_id_get", _t.yetty_ycore_int_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @font_id.setter
    def font_id(self, value: int) -> None:
        """Property `font_id` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ydrawlist2_font_font_id_set", _t.yetty_ycore_void_result, [c_void_p, c_int32])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)

class Text(Drawable):
    """yclass ydrawlist2:text"""
    __yclass_domain__: ClassVar[str] = 'ydrawlist2'
    __yclass_name__: ClassVar[str] = 'text'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ydrawlist2_text_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, body: Any = None, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_ydrawlist2_text_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(None))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._owned = True
        if body is not None:
            self.set_body(body)
        self._apply_kwargs(kwargs)
    @classmethod
    def create(cls, body: Any = None, **kwargs: Any) -> 'Text':
        return cls(body, **kwargs)
    def set_body(self, body: str | bytes | None) -> None:
        """Call `yetty_ydrawlist2_set_body`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ydrawlist2_set_body", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
        res = _rt.result_from_c(_fn(self._handle, _rt.cstr(body)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def set_color(self, color: str | bytes | None) -> None:
        """Call `yetty_ydrawlist2_set_color`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ydrawlist2_set_color", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
        res = _rt.result_from_c(_fn(self._handle, _rt.cstr(color)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @property
    def x(self) -> float:
        """Property `x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ydrawlist2_text_x_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @x.setter
    def x(self, value: float) -> None:
        """Property `x` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ydrawlist2_text_x_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def y(self) -> float:
        """Property `y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ydrawlist2_text_y_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @y.setter
    def y(self, value: float) -> None:
        """Property `y` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ydrawlist2_text_y_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def font_size(self) -> float:
        """Property `font_size` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ydrawlist2_text_font_size_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @font_size.setter
    def font_size(self, value: float) -> None:
        """Property `font_size` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ydrawlist2_text_font_size_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def color(self) -> int:
        """Property `color` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ydrawlist2_text_color_get", _t.uint32_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @color.setter
    def color(self, value: int) -> None:
        """Property `color` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ydrawlist2_text_color_set", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def layer(self) -> int:
        """Property `layer` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ydrawlist2_text_layer_get", _t.uint32_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @layer.setter
    def layer(self, value: int) -> None:
        """Property `layer` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ydrawlist2_text_layer_set", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def font_id(self) -> int:
        """Property `font_id` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ydrawlist2_text_font_id_get", _t.yetty_ycore_int_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @font_id.setter
    def font_id(self, value: int) -> None:
        """Property `font_id` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ydrawlist2_text_font_id_set", _t.yetty_ycore_void_result, [c_void_p, c_int32])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def rotation(self) -> float:
        """Property `rotation` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ydrawlist2_text_rotation_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @rotation.setter
    def rotation(self, value: float) -> None:
        """Property `rotation` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ydrawlist2_text_rotation_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)

class DrawableList(_rt.YClass):
    """yclass ydrawlist2:drawable_list"""
    __yclass_domain__: ClassVar[str] = 'ydrawlist2'
    __yclass_name__: ClassVar[str] = 'drawable_list'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ydrawlist2_drawable_list_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_ydrawlist2_drawable_list_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(None))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._owned = True
        self._apply_kwargs(kwargs)
    @classmethod
    def create(cls, **kwargs: Any) -> 'DrawableList':
        return cls(**kwargs)
    def add(self, drawable: Any) -> None:
        """Call `yetty_ydrawlist2_add`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ydrawlist2_add", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _rt.result_from_c(_fn(self._handle, _rt.handle(drawable)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def dcs_emit(self) -> None:
        """Call `yetty_ydrawlist2_dcs_emit`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ydrawlist2_dcs_emit", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def destroy(self) -> None:
        """Call `yetty_ydrawlist2_destroy`; idempotent; raises _rt.YettyError on failure."""
        if self._handle is None:
            return None
        _fn = _rt.cfn("yetty_ydrawlist2_destroy", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        self._handle = None
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value

class Shape(Drawable):
    """yclass ydrawlist2:shape"""
    __yclass_domain__: ClassVar[str] = 'ydrawlist2'
    __yclass_name__: ClassVar[str] = 'shape'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ydrawlist2_shape_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_ydrawlist2_shape_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(None))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._owned = True
        self._apply_kwargs(kwargs)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Shape':
        return cls(**kwargs)
    def set_fill(self, color: str | bytes | None) -> None:
        """Call `yetty_ydrawlist2_set_fill`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ydrawlist2_set_fill", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
        res = _rt.result_from_c(_fn(self._handle, _rt.cstr(color)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def set_stroke(self, color: str | bytes | None) -> None:
        """Call `yetty_ydrawlist2_set_stroke`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ydrawlist2_set_stroke", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
        res = _rt.result_from_c(_fn(self._handle, _rt.cstr(color)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @property
    def id(self) -> int:
        """Property `id` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ydrawlist2_shape_id_get", _t.uint32_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @id.setter
    def id(self, value: int) -> None:
        """Property `id` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ydrawlist2_shape_id_set", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def z(self) -> int:
        """Property `z` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ydrawlist2_shape_z_get", _t.uint32_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @z.setter
    def z(self, value: int) -> None:
        """Property `z` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ydrawlist2_shape_z_set", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def fill(self) -> int:
        """Property `fill` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ydrawlist2_shape_fill_get", _t.uint32_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @fill.setter
    def fill(self, value: int) -> None:
        """Property `fill` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ydrawlist2_shape_fill_set", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def stroke(self) -> int:
        """Property `stroke` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ydrawlist2_shape_stroke_get", _t.uint32_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @stroke.setter
    def stroke(self, value: int) -> None:
        """Property `stroke` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ydrawlist2_shape_stroke_set", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def stroke_width(self) -> float:
        """Property `stroke_width` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ydrawlist2_shape_stroke_width_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @stroke_width.setter
    def stroke_width(self, value: float) -> None:
        """Property `stroke_width` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ydrawlist2_shape_stroke_width_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
