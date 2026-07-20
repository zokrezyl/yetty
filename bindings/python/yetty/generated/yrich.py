"""yetty.yrich bindings — GENERATED from model.yaml, do not edit."""
from __future__ import annotations
from ctypes import (c_bool, c_char, c_char_p, c_double, c_float, c_int,
    c_int8, c_int16, c_int32, c_int64, c_long, c_size_t, c_ssize_t,
    c_uint, c_uint8, c_uint16, c_uint32, c_uint64, c_ulong, c_void_p)
from typing import Any, ClassVar
from .. import runtime as _rt
from . import _types as _t
from . import yapp as _yapp

class App(_yapp.App):
    """yclass yrich:app"""
    __yclass_domain__: ClassVar[str] = 'yrich'
    __yclass_name__: ClassVar[str] = 'app'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_yrich_app_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_yrich_app_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if res.error is not None:
                raise _rt.YettyError(res.error.message)
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls, **kwargs: Any) -> 'App':
        obj = cls()
        for _key, _value in kwargs.items():
            _setter = getattr(obj, "set_" + _key, None)
            if _setter is None:
                raise TypeError(f"App.create: unknown property {_key!r}")
            _setter(*_value) if isinstance(_value, (tuple, list)) else _setter(_value)
        return obj

class Document(_rt.YClass):
    """yclass yrich:document"""
    __yclass_domain__: ClassVar[str] = 'yrich'
    __yclass_name__: ClassVar[str] = 'document'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_yrich_document_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_yrich_document_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if res.error is not None:
                raise _rt.YettyError(res.error.message)
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Document':
        obj = cls()
        for _key, _value in kwargs.items():
            _setter = getattr(obj, "set_" + _key, None)
            if _setter is None:
                raise TypeError(f"Document.create: unknown property {_key!r}")
            _setter(*_value) if isinstance(_value, (tuple, list)) else _setter(_value)
        return obj
    def constructor(self) -> None:
        """Call `yetty_yrich_constructor`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_constructor", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def document_destroy(self) -> None:
        """Call `yetty_yrich_document_destroy`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_document_destroy", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def document_content_width(self) -> float:
        """Call `yetty_yrich_document_content_width`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_document_content_width", _t.yetty_ycore_float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def document_content_height(self) -> float:
        """Call `yetty_yrich_document_content_height`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_document_content_height", _t.yetty_ycore_float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def document_render(self) -> None:
        """Call `yetty_yrich_document_render`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_document_render", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def document_apply_op(self, op: Any, local_flag: int) -> None:
        """Call `yetty_yrich_document_apply_op`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_document_apply_op", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_int])
        res = _rt.result_from_c(_fn(self._handle, _rt.handle(op), local_flag))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def document_undo(self) -> None:
        """Call `yetty_yrich_document_undo`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_document_undo", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def document_redo(self) -> None:
        """Call `yetty_yrich_document_redo`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_document_redo", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def document_on_mouse_down(self, x: float, y: float, button: int, mods: int) -> None:
        """Call `yetty_yrich_document_on_mouse_down`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_document_on_mouse_down", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float, c_uint32, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, x, y, button, mods))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def document_on_mouse_up(self, x: float, y: float, button: int, mods: int) -> None:
        """Call `yetty_yrich_document_on_mouse_up`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_document_on_mouse_up", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float, c_uint32, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, x, y, button, mods))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def document_on_mouse_drag(self, x: float, y: float, button: int, mods: int) -> None:
        """Call `yetty_yrich_document_on_mouse_drag`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_document_on_mouse_drag", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float, c_uint32, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, x, y, button, mods))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def document_on_mouse_double_click(self, x: float, y: float, button: int, mods: int) -> None:
        """Call `yetty_yrich_document_on_mouse_double_click`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_document_on_mouse_double_click", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float, c_uint32, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, x, y, button, mods))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def document_on_key_down(self, key: int, mods: int) -> None:
        """Call `yetty_yrich_document_on_key_down`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_document_on_key_down", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, key, mods))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def document_on_text_input(self, text: _t.yetty_ycore_buffer) -> None:
        """Call `yetty_yrich_document_on_text_input`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_document_on_text_input", _t.yetty_ycore_void_result, [c_void_p, _t.yetty_ycore_buffer])
        res = _rt.result_from_c(_fn(self._handle, text))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value

class Element(_rt.YClass):
    """yclass yrich:element"""
    __yclass_domain__: ClassVar[str] = 'yrich'
    __yclass_name__: ClassVar[str] = 'element'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_yrich_element_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_yrich_element_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if res.error is not None:
                raise _rt.YettyError(res.error.message)
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Element':
        obj = cls()
        for _key, _value in kwargs.items():
            _setter = getattr(obj, "set_" + _key, None)
            if _setter is None:
                raise TypeError(f"Element.create: unknown property {_key!r}")
            _setter(*_value) if isinstance(_value, (tuple, list)) else _setter(_value)
        return obj
    def element_destroy(self) -> None:
        """Call `yetty_yrich_element_destroy`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_element_destroy", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def element_bounds(self, out_bounds: Any) -> None:
        """Call `yetty_yrich_element_bounds`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_element_bounds", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _rt.result_from_c(_fn(self._handle, _rt.handle(out_bounds)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def element_hit_test(self, x: float, y: float) -> int:
        """Call `yetty_yrich_element_hit_test`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_element_hit_test", _t.yetty_ycore_int_result, [c_void_p, c_float, c_float])
        res = _rt.result_from_c(_fn(self._handle, x, y))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def element_render(self, drawable_list: Any, layer: int, selected: int) -> None:
        """Call `yetty_yrich_element_render`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_element_render", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_uint32, c_int])
        res = _rt.result_from_c(_fn(self._handle, _rt.handle(drawable_list), layer, selected))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def element_is_editable(self) -> int:
        """Call `yetty_yrich_element_is_editable`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_element_is_editable", _t.yetty_ycore_int_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def element_begin_edit(self) -> None:
        """Call `yetty_yrich_element_begin_edit`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_element_begin_edit", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def element_end_edit(self) -> None:
        """Call `yetty_yrich_element_end_edit`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_element_end_edit", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def element_is_editing(self) -> int:
        """Call `yetty_yrich_element_is_editing`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_element_is_editing", _t.yetty_ycore_int_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def element_insert_text(self, text: _t.yetty_ycore_buffer) -> None:
        """Call `yetty_yrich_element_insert_text`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_element_insert_text", _t.yetty_ycore_void_result, [c_void_p, _t.yetty_ycore_buffer])
        res = _rt.result_from_c(_fn(self._handle, text))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def element_delete_sel(self) -> None:
        """Call `yetty_yrich_element_delete_sel`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_element_delete_sel", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value

class Shape(Element):
    """yclass yrich:shape"""
    __yclass_domain__: ClassVar[str] = 'yrich'
    __yclass_name__: ClassVar[str] = 'shape'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_yrich_shape_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_yrich_shape_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if res.error is not None:
                raise _rt.YettyError(res.error.message)
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Shape':
        obj = cls()
        for _key, _value in kwargs.items():
            _setter = getattr(obj, "set_" + _key, None)
            if _setter is None:
                raise TypeError(f"Shape.create: unknown property {_key!r}")
            _setter(*_value) if isinstance(_value, (tuple, list)) else _setter(_value)
        return obj
    @property
    def fill_color(self) -> int:
        """Property `fill_color` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_shape_fill_color_get", _t.uint32_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @fill_color.setter
    def fill_color(self, value: int) -> None:
        """Property `fill_color` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_shape_fill_color_set", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def stroke_color(self) -> int:
        """Property `stroke_color` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_shape_stroke_color_get", _t.uint32_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @stroke_color.setter
    def stroke_color(self, value: int) -> None:
        """Property `stroke_color` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_shape_stroke_color_set", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def stroke_width(self) -> float:
        """Property `stroke_width` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_shape_stroke_width_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @stroke_width.setter
    def stroke_width(self, value: float) -> None:
        """Property `stroke_width` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_shape_stroke_width_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def rotation(self) -> float:
        """Property `rotation` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_shape_rotation_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @rotation.setter
    def rotation(self, value: float) -> None:
        """Property `rotation` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_shape_rotation_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def corner_radius(self) -> float:
        """Property `corner_radius` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_shape_corner_radius_get", _t.float_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @corner_radius.setter
    def corner_radius(self, value: float) -> None:
        """Property `corner_radius` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_shape_corner_radius_set", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def text_align(self) -> int:
        """Property `text_align` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_shape_text_align_get", _t.uint32_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @text_align.setter
    def text_align(self, value: int) -> None:
        """Property `text_align` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_shape_text_align_set", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def text_valign(self) -> int:
        """Property `text_valign` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_shape_text_valign_get", _t.uint32_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @text_valign.setter
    def text_valign(self, value: int) -> None:
        """Property `text_valign` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_shape_text_valign_set", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)

class Slides(Document):
    """yclass yrich:slides"""
    __yclass_domain__: ClassVar[str] = 'yrich'
    __yclass_name__: ClassVar[str] = 'slides'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_yrich_slides_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_yrich_slides_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if res.error is not None:
                raise _rt.YettyError(res.error.message)
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Slides':
        obj = cls()
        for _key, _value in kwargs.items():
            _setter = getattr(obj, "set_" + _key, None)
            if _setter is None:
                raise TypeError(f"Slides.create: unknown property {_key!r}")
            _setter(*_value) if isinstance(_value, (tuple, list)) else _setter(_value)
        return obj
    def slides_set_current(self, index: int) -> None:
        """Call `yetty_yrich_slides_set_current`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_slides_set_current", _t.yetty_ycore_void_result, [c_void_p, c_int32])
        res = _rt.result_from_c(_fn(self._handle, index))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def slides_next(self) -> None:
        """Call `yetty_yrich_slides_next`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_slides_next", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def slides_prev(self) -> None:
        """Call `yetty_yrich_slides_prev`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_slides_prev", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value

class Cell(Element):
    """yclass yrich:cell"""
    __yclass_domain__: ClassVar[str] = 'yrich'
    __yclass_name__: ClassVar[str] = 'cell'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_yrich_cell_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_yrich_cell_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if res.error is not None:
                raise _rt.YettyError(res.error.message)
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Cell':
        obj = cls()
        for _key, _value in kwargs.items():
            _setter = getattr(obj, "set_" + _key, None)
            if _setter is None:
                raise TypeError(f"Cell.create: unknown property {_key!r}")
            _setter(*_value) if isinstance(_value, (tuple, list)) else _setter(_value)
        return obj

class Spreadsheet(Document):
    """yclass yrich:spreadsheet"""
    __yclass_domain__: ClassVar[str] = 'yrich'
    __yclass_name__: ClassVar[str] = 'spreadsheet'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_yrich_spreadsheet_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_yrich_spreadsheet_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if res.error is not None:
                raise _rt.YettyError(res.error.message)
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Spreadsheet':
        obj = cls()
        for _key, _value in kwargs.items():
            _setter = getattr(obj, "set_" + _key, None)
            if _setter is None:
                raise TypeError(f"Spreadsheet.create: unknown property {_key!r}")
            _setter(*_value) if isinstance(_value, (tuple, list)) else _setter(_value)
        return obj
    def spreadsheet_set_grid_size(self, rows: int, cols: int) -> None:
        """Call `yetty_yrich_spreadsheet_set_grid_size`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_spreadsheet_set_grid_size", _t.yetty_ycore_void_result, [c_void_p, c_int32, c_int32])
        res = _rt.result_from_c(_fn(self._handle, rows, cols))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def spreadsheet_set_row_height(self, row: int, height: float) -> None:
        """Call `yetty_yrich_spreadsheet_set_row_height`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_spreadsheet_set_row_height", _t.yetty_ycore_void_result, [c_void_p, c_int32, c_float])
        res = _rt.result_from_c(_fn(self._handle, row, height))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def spreadsheet_set_col_width(self, col: int, width: float) -> None:
        """Call `yetty_yrich_spreadsheet_set_col_width`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_spreadsheet_set_col_width", _t.yetty_ycore_void_result, [c_void_p, c_int32, c_float])
        res = _rt.result_from_c(_fn(self._handle, col, width))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def spreadsheet_set_cell_value(self, row: int, col: int, value: _t.yetty_ycore_buffer) -> None:
        """Call `yetty_yrich_spreadsheet_set_cell_value`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_spreadsheet_set_cell_value", _t.yetty_ycore_void_result, [c_void_p, c_int32, c_int32, _t.yetty_ycore_buffer])
        res = _rt.result_from_c(_fn(self._handle, row, col, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value

class Paragraph(Element):
    """yclass yrich:paragraph"""
    __yclass_domain__: ClassVar[str] = 'yrich'
    __yclass_name__: ClassVar[str] = 'paragraph'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_yrich_paragraph_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_yrich_paragraph_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if res.error is not None:
                raise _rt.YettyError(res.error.message)
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Paragraph':
        obj = cls()
        for _key, _value in kwargs.items():
            _setter = getattr(obj, "set_" + _key, None)
            if _setter is None:
                raise TypeError(f"Paragraph.create: unknown property {_key!r}")
            _setter(*_value) if isinstance(_value, (tuple, list)) else _setter(_value)
        return obj

class InlineImage(Element):
    """yclass yrich:inline_image"""
    __yclass_domain__: ClassVar[str] = 'yrich'
    __yclass_name__: ClassVar[str] = 'inline_image'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_yrich_inline_image_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_yrich_inline_image_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if res.error is not None:
                raise _rt.YettyError(res.error.message)
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls, **kwargs: Any) -> 'InlineImage':
        obj = cls()
        for _key, _value in kwargs.items():
            _setter = getattr(obj, "set_" + _key, None)
            if _setter is None:
                raise TypeError(f"InlineImage.create: unknown property {_key!r}")
            _setter(*_value) if isinstance(_value, (tuple, list)) else _setter(_value)
        return obj

class Ydoc(Document):
    """yclass yrich:ydoc"""
    __yclass_domain__: ClassVar[str] = 'yrich'
    __yclass_name__: ClassVar[str] = 'ydoc'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_yrich_ydoc_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_yrich_ydoc_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if res.error is not None:
                raise _rt.YettyError(res.error.message)
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Ydoc':
        obj = cls()
        for _key, _value in kwargs.items():
            _setter = getattr(obj, "set_" + _key, None)
            if _setter is None:
                raise TypeError(f"Ydoc.create: unknown property {_key!r}")
            _setter(*_value) if isinstance(_value, (tuple, list)) else _setter(_value)
        return obj
    def ydoc_toggle_format(self, format_flag: int) -> None:
        """Call `yetty_yrich_ydoc_toggle_format`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_ydoc_toggle_format", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, format_flag))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def ydoc_set_text_color(self, color: int) -> None:
        """Call `yetty_yrich_ydoc_set_text_color`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_ydoc_set_text_color", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, color))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def ydoc_set_alignment(self, halign: int) -> None:
        """Call `yetty_yrich_ydoc_set_alignment`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_ydoc_set_alignment", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, halign))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def ydoc_set_line_spacing(self, spacing: float) -> None:
        """Call `yetty_yrich_ydoc_set_line_spacing`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_ydoc_set_line_spacing", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, spacing))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def ydoc_adjust_indent(self, direction: int) -> None:
        """Call `yetty_yrich_ydoc_adjust_indent`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_ydoc_adjust_indent", _t.yetty_ycore_void_result, [c_void_p, c_int32])
        res = _rt.result_from_c(_fn(self._handle, direction))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def ydoc_set_highlight(self, bg_color: int) -> None:
        """Call `yetty_yrich_ydoc_set_highlight`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_ydoc_set_highlight", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, bg_color))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def ydoc_clear_format(self) -> None:
        """Call `yetty_yrich_ydoc_clear_format`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_ydoc_clear_format", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def ydoc_set_heading(self, level: int) -> None:
        """Call `yetty_yrich_ydoc_set_heading`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_ydoc_set_heading", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, level))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def ydoc_change_font_size(self, delta: float) -> None:
        """Call `yetty_yrich_ydoc_change_font_size`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_ydoc_change_font_size", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, delta))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def ydoc_set_font_size(self, size: float) -> None:
        """Call `yetty_yrich_ydoc_set_font_size`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yrich_ydoc_set_font_size", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, size))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value

def super_void(obj: Any, self_class: Any, method_id: Any) -> _rt.Result[None]:
    """Call `yetty_yrich_super_void`."""
    _fn = _rt.cfn("yetty_yrich_super_void", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(self_class), _rt.handle(method_id))
    return _rt.result_from_c(res)

def document_mark_dirty(obj: Any) -> _rt.Result[None]:
    """Call `yetty_yrich_document_mark_dirty`."""
    _fn = _rt.cfn("yetty_yrich_document_mark_dirty", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def document_is_dirty(obj: Any) -> _rt.Result[int]:
    """Call `yetty_yrich_document_is_dirty`."""
    _fn = _rt.cfn("yetty_yrich_document_is_dirty", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def document_clear_dirty(obj: Any) -> _rt.Result[None]:
    """Call `yetty_yrich_document_clear_dirty`."""
    _fn = _rt.cfn("yetty_yrich_document_clear_dirty", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def document_set_buffer(obj: Any, drawable_list: Any) -> _rt.Result[None]:
    """Call `yetty_yrich_document_set_buffer`."""
    _fn = _rt.cfn("yetty_yrich_document_set_buffer", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(drawable_list))
    return _rt.result_from_c(res)

def document_buffer(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_yrich_document_buffer`."""
    _fn = _rt.cfn("yetty_yrich_document_buffer", _t.yetty_yrich_drawable_list_ptr_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def document_set_bg_color(obj: Any, color: int) -> _rt.Result[None]:
    """Call `yetty_yrich_document_set_bg_color`."""
    _fn = _rt.cfn("yetty_yrich_document_set_bg_color", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), color)
    return _rt.result_from_c(res)

def document_bg_color(obj: Any) -> _rt.Result[int]:
    """Call `yetty_yrich_document_bg_color`."""
    _fn = _rt.cfn("yetty_yrich_document_bg_color", _t.yetty_ycore_uint32_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def document_set_dirty_cb(obj: Any, callback: Any, userdata: Any) -> _rt.Result[None]:
    """Call `yetty_yrich_document_set_dirty_cb`."""
    _fn = _rt.cfn("yetty_yrich_document_set_dirty_cb", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(callback), _rt.handle(userdata))
    return _rt.result_from_c(res)

def document_set_sync_cb(obj: Any, callback: Any, userdata: Any) -> _rt.Result[None]:
    """Call `yetty_yrich_document_set_sync_cb`."""
    _fn = _rt.cfn("yetty_yrich_document_set_sync_cb", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(callback), _rt.handle(userdata))
    return _rt.result_from_c(res)

def document_next_id(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_yrich_document_next_id`."""
    _fn = _rt.cfn("yetty_yrich_document_next_id", _t.yetty_yrich_element_id_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def document_create_op(obj: Any, op_type: int) -> _rt.Result[Any]:
    """Call `yetty_yrich_document_create_op`."""
    _fn = _rt.cfn("yetty_yrich_document_create_op", _t.yetty_yrich_operation_ptr_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), op_type)
    return _rt.result_from_c(res)

def document_add_element(obj: Any, element_obj: Any) -> _rt.Result[None]:
    """Call `yetty_yrich_document_add_element`."""
    _fn = _rt.cfn("yetty_yrich_document_add_element", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(element_obj))
    return _rt.result_from_c(res)

def document_remove_element(obj: Any, id: Any) -> _rt.Result[None]:
    """Call `yetty_yrich_document_remove_element`."""
    _fn = _rt.cfn("yetty_yrich_document_remove_element", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(id))
    return _rt.result_from_c(res)

def document_find(obj: Any, id: Any) -> _rt.Result[Any]:
    """Call `yetty_yrich_document_find`."""
    _fn = _rt.cfn("yetty_yrich_document_find", _t.yetty_yclass_object_ptr_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(id))
    return _rt.result_from_c(res)

def document_element_at(obj: Any, x: float, y: float) -> _rt.Result[Any]:
    """Call `yetty_yrich_document_element_at`."""
    _fn = _rt.cfn("yetty_yrich_document_element_at", _t.yetty_yclass_object_ptr_result, [c_void_p, c_float, c_float])
    res = _fn(_rt.handle(obj), x, y)
    return _rt.result_from_c(res)

def document_selection(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_yrich_document_selection`."""
    _fn = _rt.cfn("yetty_yrich_document_selection", _t.yetty_yrich_selection_ptr_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def document_is_selected(obj: Any, id: Any) -> _rt.Result[int]:
    """Call `yetty_yrich_document_is_selected`."""
    _fn = _rt.cfn("yetty_yrich_document_is_selected", _t.yetty_ycore_int_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(id))
    return _rt.result_from_c(res)

def document_clear_selection(obj: Any) -> _rt.Result[None]:
    """Call `yetty_yrich_document_clear_selection`."""
    _fn = _rt.cfn("yetty_yrich_document_clear_selection", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def document_clear_history(obj: Any) -> _rt.Result[None]:
    """Call `yetty_yrich_document_clear_history`."""
    _fn = _rt.cfn("yetty_yrich_document_clear_history", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def document_execute(obj: Any, cmd: Any) -> _rt.Result[None]:
    """Call `yetty_yrich_document_execute`."""
    _fn = _rt.cfn("yetty_yrich_document_execute", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(cmd))
    return _rt.result_from_c(res)

def element_id_value(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_yrich_element_id_value`."""
    _fn = _rt.cfn("yetty_yrich_element_id_value", _t.yetty_yrich_element_id_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def element_set_id(obj: Any, id: Any) -> _rt.Result[None]:
    """Call `yetty_yrich_element_set_id`."""
    _fn = _rt.cfn("yetty_yrich_element_set_id", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(id))
    return _rt.result_from_c(res)

def shape_set_text(obj: Any, text: str | bytes | None, len: int) -> _rt.Result[None]:
    """Call `yetty_yrich_shape_set_text`."""
    _fn = _rt.cfn("yetty_yrich_shape_set_text", _t.yetty_ycore_void_result, [c_void_p, c_char_p, c_size_t])
    res = _fn(_rt.handle(obj), _rt.cstr(text), len)
    return _rt.result_from_c(res)

def shape_set_font_size(obj: Any, font_size: float) -> _rt.Result[None]:
    """Call `yetty_yrich_shape_set_font_size`."""
    _fn = _rt.cfn("yetty_yrich_shape_set_font_size", _t.yetty_ycore_void_result, [c_void_p, c_float])
    res = _fn(_rt.handle(obj), font_size)
    return _rt.result_from_c(res)

def shape_set_text_color(obj: Any, color: int) -> _rt.Result[None]:
    """Call `yetty_yrich_shape_set_text_color`."""
    _fn = _rt.cfn("yetty_yrich_shape_set_text_color", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), color)
    return _rt.result_from_c(res)

def shape_set_image_source(obj: Any, source: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_yrich_shape_set_image_source`."""
    _fn = _rt.cfn("yetty_yrich_shape_set_image_source", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(source))
    return _rt.result_from_c(res)

def slides_set_slide_size(obj: Any, width: float, height: float) -> _rt.Result[None]:
    """Call `yetty_yrich_slides_set_slide_size`."""
    _fn = _rt.cfn("yetty_yrich_slides_set_slide_size", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float])
    res = _fn(_rt.handle(obj), width, height)
    return _rt.result_from_c(res)

def slides_add_slide(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_yrich_slides_add_slide`."""
    _fn = _rt.cfn("yetty_yrich_slides_add_slide", _t.yetty_yrich_slide_ptr_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def slides_slide_at(obj: Any, index: int) -> _rt.Result[Any]:
    """Call `yetty_yrich_slides_slide_at`."""
    _fn = _rt.cfn("yetty_yrich_slides_slide_at", _t.yetty_yrich_slide_ptr_result, [c_void_p, c_int32])
    res = _fn(_rt.handle(obj), index)
    return _rt.result_from_c(res)

def slides_current(obj: Any) -> _rt.Result[int]:
    """Call `yetty_yrich_slides_current`."""
    _fn = _rt.cfn("yetty_yrich_slides_current", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def slides_add_rectangle(obj: Any, x: float, y: float, width: float, height: float) -> _rt.Result[Any]:
    """Call `yetty_yrich_slides_add_rectangle`."""
    _fn = _rt.cfn("yetty_yrich_slides_add_rectangle", _t.yetty_yclass_object_ptr_result, [c_void_p, c_float, c_float, c_float, c_float])
    res = _fn(_rt.handle(obj), x, y, width, height)
    return _rt.result_from_c(res)

def slides_add_ellipse(obj: Any, x: float, y: float, width: float, height: float) -> _rt.Result[Any]:
    """Call `yetty_yrich_slides_add_ellipse`."""
    _fn = _rt.cfn("yetty_yrich_slides_add_ellipse", _t.yetty_yclass_object_ptr_result, [c_void_p, c_float, c_float, c_float, c_float])
    res = _fn(_rt.handle(obj), x, y, width, height)
    return _rt.result_from_c(res)

def slides_add_textbox(obj: Any, x: float, y: float, width: float, height: float, text: str | bytes | None, text_len: int) -> _rt.Result[Any]:
    """Call `yetty_yrich_slides_add_textbox`."""
    _fn = _rt.cfn("yetty_yrich_slides_add_textbox", _t.yetty_yclass_object_ptr_result, [c_void_p, c_float, c_float, c_float, c_float, c_char_p, c_size_t])
    res = _fn(_rt.handle(obj), x, y, width, height, _rt.cstr(text), text_len)
    return _rt.result_from_c(res)

def slides_add_line(obj: Any, x1: float, y1: float, x2: float, y2: float) -> _rt.Result[Any]:
    """Call `yetty_yrich_slides_add_line`."""
    _fn = _rt.cfn("yetty_yrich_slides_add_line", _t.yetty_yclass_object_ptr_result, [c_void_p, c_float, c_float, c_float, c_float])
    res = _fn(_rt.handle(obj), x1, y1, x2, y2)
    return _rt.result_from_c(res)

def slides_add_image(obj: Any, x: float, y: float, width: float, height: float) -> _rt.Result[Any]:
    """Call `yetty_yrich_slides_add_image`."""
    _fn = _rt.cfn("yetty_yrich_slides_add_image", _t.yetty_yclass_object_ptr_result, [c_void_p, c_float, c_float, c_float, c_float])
    res = _fn(_rt.handle(obj), x, y, width, height)
    return _rt.result_from_c(res)

def cell_set_text(obj: Any, text: str | bytes | None, len: int) -> _rt.Result[None]:
    """Call `yetty_yrich_cell_set_text`."""
    _fn = _rt.cfn("yetty_yrich_cell_set_text", _t.yetty_ycore_void_result, [c_void_p, c_char_p, c_size_t])
    res = _fn(_rt.handle(obj), _rt.cstr(text), len)
    return _rt.result_from_c(res)

def cell_text(obj: Any) -> _rt.Result[str | None]:
    """Call `yetty_yrich_cell_text`."""
    _fn = _rt.cfn("yetty_yrich_cell_text", _t.yetty_ycore_const_char_ptr_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res, _rt.decode_cstr)

def spreadsheet_row_height(obj: Any, row: int) -> _rt.Result[float]:
    """Call `yetty_yrich_spreadsheet_row_height`."""
    _fn = _rt.cfn("yetty_yrich_spreadsheet_row_height", _t.yetty_ycore_float_result, [c_void_p, c_int32])
    res = _fn(_rt.handle(obj), row)
    return _rt.result_from_c(res)

def spreadsheet_col_width(obj: Any, col: int) -> _rt.Result[float]:
    """Call `yetty_yrich_spreadsheet_col_width`."""
    _fn = _rt.cfn("yetty_yrich_spreadsheet_col_width", _t.yetty_ycore_float_result, [c_void_p, c_int32])
    res = _fn(_rt.handle(obj), col)
    return _rt.result_from_c(res)

def spreadsheet_row_y(obj: Any, row: int) -> _rt.Result[float]:
    """Call `yetty_yrich_spreadsheet_row_y`."""
    _fn = _rt.cfn("yetty_yrich_spreadsheet_row_y", _t.yetty_ycore_float_result, [c_void_p, c_int32])
    res = _fn(_rt.handle(obj), row)
    return _rt.result_from_c(res)

def spreadsheet_col_x(obj: Any, col: int) -> _rt.Result[float]:
    """Call `yetty_yrich_spreadsheet_col_x`."""
    _fn = _rt.cfn("yetty_yrich_spreadsheet_col_x", _t.yetty_ycore_float_result, [c_void_p, c_int32])
    res = _fn(_rt.handle(obj), col)
    return _rt.result_from_c(res)

def spreadsheet_cell_at(obj: Any, row: int, col: int) -> _rt.Result[Any]:
    """Call `yetty_yrich_spreadsheet_cell_at`."""
    _fn = _rt.cfn("yetty_yrich_spreadsheet_cell_at", _t.yetty_yclass_object_ptr_result, [c_void_p, c_int32, c_int32])
    res = _fn(_rt.handle(obj), row, col)
    return _rt.result_from_c(res)

def spreadsheet_ensure_cell(obj: Any, row: int, col: int) -> _rt.Result[Any]:
    """Call `yetty_yrich_spreadsheet_ensure_cell`."""
    _fn = _rt.cfn("yetty_yrich_spreadsheet_ensure_cell", _t.yetty_yclass_object_ptr_result, [c_void_p, c_int32, c_int32])
    res = _fn(_rt.handle(obj), row, col)
    return _rt.result_from_c(res)

def spreadsheet_cell_value(obj: Any, row: int, col: int) -> _rt.Result[str | None]:
    """Call `yetty_yrich_spreadsheet_cell_value`."""
    _fn = _rt.cfn("yetty_yrich_spreadsheet_cell_value", _t.yetty_ycore_const_char_ptr_result, [c_void_p, c_int32, c_int32])
    res = _fn(_rt.handle(obj), row, col)
    return _rt.result_from_c(res, _rt.decode_cstr)

def spreadsheet_cell_addr_at(obj: Any, x: float, y: float) -> _rt.Result[Any]:
    """Call `yetty_yrich_spreadsheet_cell_addr_at`."""
    _fn = _rt.cfn("yetty_yrich_spreadsheet_cell_addr_at", _t.yetty_yrich_cell_addr_result, [c_void_p, c_float, c_float])
    res = _fn(_rt.handle(obj), x, y)
    return _rt.result_from_c(res)

def paragraph_set_text(obj: Any, text: str | bytes | None, len: int) -> _rt.Result[None]:
    """Call `yetty_yrich_paragraph_set_text`."""
    _fn = _rt.cfn("yetty_yrich_paragraph_set_text", _t.yetty_ycore_void_result, [c_void_p, c_char_p, c_size_t])
    res = _fn(_rt.handle(obj), _rt.cstr(text), len)
    return _rt.result_from_c(res)

def paragraph_set_font_size(obj: Any, font_size: float) -> _rt.Result[None]:
    """Call `yetty_yrich_paragraph_set_font_size`."""
    _fn = _rt.cfn("yetty_yrich_paragraph_set_font_size", _t.yetty_ycore_void_result, [c_void_p, c_float])
    res = _fn(_rt.handle(obj), font_size)
    return _rt.result_from_c(res)

def paragraph_set_color(obj: Any, color: int) -> _rt.Result[None]:
    """Call `yetty_yrich_paragraph_set_color`."""
    _fn = _rt.cfn("yetty_yrich_paragraph_set_color", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), color)
    return _rt.result_from_c(res)

def paragraph_set_format(obj: Any, format: int) -> _rt.Result[None]:
    """Call `yetty_yrich_paragraph_set_format`."""
    _fn = _rt.cfn("yetty_yrich_paragraph_set_format", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), format)
    return _rt.result_from_c(res)

def ydoc_set_page_width(obj: Any, width: float) -> _rt.Result[None]:
    """Call `yetty_yrich_ydoc_set_page_width`."""
    _fn = _rt.cfn("yetty_yrich_ydoc_set_page_width", _t.yetty_ycore_void_result, [c_void_p, c_float])
    res = _fn(_rt.handle(obj), width)
    return _rt.result_from_c(res)

def ydoc_set_margin(obj: Any, margin: float) -> _rt.Result[None]:
    """Call `yetty_yrich_ydoc_set_margin`."""
    _fn = _rt.cfn("yetty_yrich_ydoc_set_margin", _t.yetty_ycore_void_result, [c_void_p, c_float])
    res = _fn(_rt.handle(obj), margin)
    return _rt.result_from_c(res)

def ydoc_add_paragraph(obj: Any, text: str | bytes | None, text_len: int) -> _rt.Result[Any]:
    """Call `yetty_yrich_ydoc_add_paragraph`."""
    _fn = _rt.cfn("yetty_yrich_ydoc_add_paragraph", _t.yetty_yclass_object_ptr_result, [c_void_p, c_char_p, c_size_t])
    res = _fn(_rt.handle(obj), _rt.cstr(text), text_len)
    return _rt.result_from_c(res)

def ydoc_paragraph_at(obj: Any, index: int) -> _rt.Result[Any]:
    """Call `yetty_yrich_ydoc_paragraph_at`."""
    _fn = _rt.cfn("yetty_yrich_ydoc_paragraph_at", _t.yetty_yclass_object_ptr_result, [c_void_p, c_int32])
    res = _fn(_rt.handle(obj), index)
    return _rt.result_from_c(res)

def inline_image_set_source(obj: Any, source: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_yrich_inline_image_set_source`."""
    _fn = _rt.cfn("yetty_yrich_inline_image_set_source", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(source))
    return _rt.result_from_c(res)

def inline_image_source(obj: Any) -> _rt.Result[str | None]:
    """Call `yetty_yrich_inline_image_source`."""
    _fn = _rt.cfn("yetty_yrich_inline_image_source", _t.yetty_ycore_const_char_ptr_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res, _rt.decode_cstr)

def inline_image_set_bounds(obj: Any, x: float, y: float, width: float, height: float) -> _rt.Result[None]:
    """Call `yetty_yrich_inline_image_set_bounds`."""
    _fn = _rt.cfn("yetty_yrich_inline_image_set_bounds", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float, c_float, c_float])
    res = _fn(_rt.handle(obj), x, y, width, height)
    return _rt.result_from_c(res)

def inline_image_bounds(obj: Any, out_x: Any, out_y: Any, out_width: Any, out_height: Any) -> _rt.Result[None]:
    """Call `yetty_yrich_inline_image_bounds`."""
    _fn = _rt.cfn("yetty_yrich_inline_image_bounds", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(out_x), _rt.handle(out_y), _rt.handle(out_width), _rt.handle(out_height))
    return _rt.result_from_c(res)

def ydoc_image_count(obj: Any) -> _rt.Result[int]:
    """Call `yetty_yrich_ydoc_image_count`."""
    _fn = _rt.cfn("yetty_yrich_ydoc_image_count", _t.yetty_ycore_size_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def ydoc_image_at(obj: Any, index: int) -> _rt.Result[Any]:
    """Call `yetty_yrich_ydoc_image_at`."""
    _fn = _rt.cfn("yetty_yrich_ydoc_image_at", _t.yetty_yclass_object_ptr_result, [c_void_p, c_int32])
    res = _fn(_rt.handle(obj), index)
    return _rt.result_from_c(res)

def ydoc_insert_image(obj: Any, paragraph_index: int, width: float, height: float) -> _rt.Result[Any]:
    """Call `yetty_yrich_ydoc_insert_image`."""
    _fn = _rt.cfn("yetty_yrich_ydoc_insert_image", _t.yetty_yclass_object_ptr_result, [c_void_p, c_int32, c_float, c_float])
    res = _fn(_rt.handle(obj), paragraph_index, width, height)
    return _rt.result_from_c(res)

def ydoc_set_styled_font_mask(obj: Any, mask: int) -> _rt.Result[None]:
    """Call `yetty_yrich_ydoc_set_styled_font_mask`."""
    _fn = _rt.cfn("yetty_yrich_ydoc_set_styled_font_mask", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), mask)
    return _rt.result_from_c(res)

def ydoc_toggle_nonprinting(obj: Any) -> _rt.Result[None]:
    """Call `yetty_yrich_ydoc_toggle_nonprinting`."""
    _fn = _rt.cfn("yetty_yrich_ydoc_toggle_nonprinting", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def ydoc_insert_horizontal_rule(obj: Any) -> _rt.Result[None]:
    """Call `yetty_yrich_ydoc_insert_horizontal_rule`."""
    _fn = _rt.cfn("yetty_yrich_ydoc_insert_horizontal_rule", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def ydoc_insert_page_break(obj: Any) -> _rt.Result[None]:
    """Call `yetty_yrich_ydoc_insert_page_break`."""
    _fn = _rt.cfn("yetty_yrich_ydoc_insert_page_break", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def ydoc_insert_table(obj: Any, rows: int, cols: int) -> _rt.Result[None]:
    """Call `yetty_yrich_ydoc_insert_table`."""
    _fn = _rt.cfn("yetty_yrich_ydoc_insert_table", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_uint32])
    res = _fn(_rt.handle(obj), rows, cols)
    return _rt.result_from_c(res)

def ydoc_insert_toc(obj: Any) -> _rt.Result[None]:
    """Call `yetty_yrich_ydoc_insert_toc`."""
    _fn = _rt.cfn("yetty_yrich_ydoc_insert_toc", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def ydoc_table_edit(obj: Any, op: int) -> _rt.Result[None]:
    """Call `yetty_yrich_ydoc_table_edit`."""
    _fn = _rt.cfn("yetty_yrich_ydoc_table_edit", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), op)
    return _rt.result_from_c(res)

def ydoc_all_text(ydoc: Any) -> bytes | None:
    """Call `ydoc_all_text`."""
    _fn = _rt.cfn("ydoc_all_text", c_char_p, [c_void_p])
    return _fn(_rt.handle(ydoc))

def ydoc_selection_text(obj: Any) -> _rt.Result[str | None]:
    """Call `yetty_yrich_ydoc_selection_text`."""
    _fn = _rt.cfn("yetty_yrich_ydoc_selection_text", _t.yetty_ycore_char_ptr_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res, _rt.decode_cstr)

def ydoc_word_count(obj: Any, out_words: Any, out_chars: Any, out_chars_no_spaces: Any, out_paragraphs: Any) -> _rt.Result[None]:
    """Call `yetty_yrich_ydoc_word_count`."""
    _fn = _rt.cfn("yetty_yrich_ydoc_word_count", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(out_words), _rt.handle(out_chars), _rt.handle(out_chars_no_spaces), _rt.handle(out_paragraphs))
    return _rt.result_from_c(res)

def ydoc_find_next(obj: Any, query: str | bytes | None) -> _rt.Result[int]:
    """Call `yetty_yrich_ydoc_find_next`."""
    _fn = _rt.cfn("yetty_yrich_ydoc_find_next", _t.yetty_ycore_int_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(query))
    return _rt.result_from_c(res)

def ydoc_find_prev(obj: Any, query: str | bytes | None) -> _rt.Result[int]:
    """Call `yetty_yrich_ydoc_find_prev`."""
    _fn = _rt.cfn("yetty_yrich_ydoc_find_prev", _t.yetty_ycore_int_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(query))
    return _rt.result_from_c(res)

def ydoc_replace_all(obj: Any, query: str | bytes | None, replacement: str | bytes | None) -> _rt.Result[int]:
    """Call `yetty_yrich_ydoc_replace_all`."""
    _fn = _rt.cfn("yetty_yrich_ydoc_replace_all", _t.yetty_ycore_int_result, [c_void_p, c_char_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(query), _rt.cstr(replacement))
    return _rt.result_from_c(res)

def ydoc_set_space_before(obj: Any, px: float) -> _rt.Result[None]:
    """Call `yetty_yrich_ydoc_set_space_before`."""
    _fn = _rt.cfn("yetty_yrich_ydoc_set_space_before", _t.yetty_ycore_void_result, [c_void_p, c_float])
    res = _fn(_rt.handle(obj), px)
    return _rt.result_from_c(res)

def ydoc_set_space_after(obj: Any, px: float) -> _rt.Result[None]:
    """Call `yetty_yrich_ydoc_set_space_after`."""
    _fn = _rt.cfn("yetty_yrich_ydoc_set_space_after", _t.yetty_ycore_void_result, [c_void_p, c_float])
    res = _fn(_rt.handle(obj), px)
    return _rt.result_from_c(res)

def ydoc_change_list_level(obj: Any, direction: int) -> _rt.Result[None]:
    """Call `yetty_yrich_ydoc_change_list_level`."""
    _fn = _rt.cfn("yetty_yrich_ydoc_change_list_level", _t.yetty_ycore_void_result, [c_void_p, c_int32])
    res = _fn(_rt.handle(obj), direction)
    return _rt.result_from_c(res)

def ydoc_copy_format(obj: Any) -> _rt.Result[None]:
    """Call `yetty_yrich_ydoc_copy_format`."""
    _fn = _rt.cfn("yetty_yrich_ydoc_copy_format", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def ydoc_paint_format(obj: Any) -> _rt.Result[None]:
    """Call `yetty_yrich_ydoc_paint_format`."""
    _fn = _rt.cfn("yetty_yrich_ydoc_paint_format", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def ydoc_set_link(obj: Any, url: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_yrich_ydoc_set_link`."""
    _fn = _rt.cfn("yetty_yrich_ydoc_set_link", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(url))
    return _rt.result_from_c(res)

def ydoc_remove_link(obj: Any) -> _rt.Result[None]:
    """Call `yetty_yrich_ydoc_remove_link`."""
    _fn = _rt.cfn("yetty_yrich_ydoc_remove_link", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def ydoc_link_at_caret(obj: Any) -> _rt.Result[str | None]:
    """Call `yetty_yrich_ydoc_link_at_caret`."""
    _fn = _rt.cfn("yetty_yrich_ydoc_link_at_caret", _t.yetty_ycore_const_char_ptr_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res, _rt.decode_cstr)

def ydoc_link_url(obj: Any, link_id: int) -> _rt.Result[str | None]:
    """Call `yetty_yrich_ydoc_link_url`."""
    _fn = _rt.cfn("yetty_yrich_ydoc_link_url", _t.yetty_ycore_const_char_ptr_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), link_id)
    return _rt.result_from_c(res, _rt.decode_cstr)

def ydoc_apply_run_link(obj: Any, paragraph_obj: Any, start: int, end: int, url: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_yrich_ydoc_apply_run_link`."""
    _fn = _rt.cfn("yetty_yrich_ydoc_apply_run_link", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_int32, c_int32, c_char_p])
    res = _fn(_rt.handle(obj), _rt.handle(paragraph_obj), start, end, _rt.cstr(url))
    return _rt.result_from_c(res)

def ydoc_set_bookmark(obj: Any, name: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_yrich_ydoc_set_bookmark`."""
    _fn = _rt.cfn("yetty_yrich_ydoc_set_bookmark", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(name))
    return _rt.result_from_c(res)

def ydoc_set_list(obj: Any, kind: int) -> _rt.Result[None]:
    """Call `yetty_yrich_ydoc_set_list`."""
    _fn = _rt.cfn("yetty_yrich_ydoc_set_list", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), kind)
    return _rt.result_from_c(res)

def ydoc_toggle_checked(obj: Any) -> _rt.Result[None]:
    """Call `yetty_yrich_ydoc_toggle_checked`."""
    _fn = _rt.cfn("yetty_yrich_ydoc_toggle_checked", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def ydoc_place_caret(obj: Any, paragraph_index: int, position: int) -> _rt.Result[None]:
    """Call `yetty_yrich_ydoc_place_caret`."""
    _fn = _rt.cfn("yetty_yrich_ydoc_place_caret", _t.yetty_ycore_void_result, [c_void_p, c_int32, c_int32])
    res = _fn(_rt.handle(obj), paragraph_index, position)
    return _rt.result_from_c(res)

def ydoc_goto_bookmark(obj: Any, name: str | bytes | None) -> _rt.Result[int]:
    """Call `yetty_yrich_ydoc_goto_bookmark`."""
    _fn = _rt.cfn("yetty_yrich_ydoc_goto_bookmark", _t.yetty_ycore_int_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(name))
    return _rt.result_from_c(res)

def ydoc_select_range(obj: Any, anchor_paragraph: int, anchor_offset: int, focus_paragraph: int, focus_offset: int) -> _rt.Result[None]:
    """Call `yetty_yrich_ydoc_select_range`."""
    _fn = _rt.cfn("yetty_yrich_ydoc_select_range", _t.yetty_ycore_void_result, [c_void_p, c_int32, c_int32, c_int32, c_int32])
    res = _fn(_rt.handle(obj), anchor_paragraph, anchor_offset, focus_paragraph, focus_offset)
    return _rt.result_from_c(res)

def ydoc_clear(obj: Any) -> _rt.Result[None]:
    """Call `yetty_yrich_ydoc_clear`."""
    _fn = _rt.cfn("yetty_yrich_ydoc_clear", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def ydoc_set_source_path(obj: Any, path: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_yrich_ydoc_set_source_path`."""
    _fn = _rt.cfn("yetty_yrich_ydoc_set_source_path", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(path))
    return _rt.result_from_c(res)

def ydoc_source_path(obj: Any) -> _rt.Result[str | None]:
    """Call `yetty_yrich_ydoc_source_path`."""
    _fn = _rt.cfn("yetty_yrich_ydoc_source_path", _t.yetty_ycore_const_char_ptr_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res, _rt.decode_cstr)

def ydoc_page_width(obj: Any) -> _rt.Result[float]:
    """Call `yetty_yrich_ydoc_page_width`."""
    _fn = _rt.cfn("yetty_yrich_ydoc_page_width", _t.yetty_ycore_float_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def ydoc_margin(obj: Any) -> _rt.Result[float]:
    """Call `yetty_yrich_ydoc_margin`."""
    _fn = _rt.cfn("yetty_yrich_ydoc_margin", _t.yetty_ycore_float_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def ydoc_paragraph_count(obj: Any) -> _rt.Result[int]:
    """Call `yetty_yrich_ydoc_paragraph_count`."""
    _fn = _rt.cfn("yetty_yrich_ydoc_paragraph_count", _t.yetty_ycore_size_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def paragraph_text(obj: Any) -> _rt.Result[str | None]:
    """Call `yetty_yrich_paragraph_text`."""
    _fn = _rt.cfn("yetty_yrich_paragraph_text", _t.yetty_ycore_const_char_ptr_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res, _rt.decode_cstr)

def paragraph_text_len(obj: Any) -> _rt.Result[int]:
    """Call `yetty_yrich_paragraph_text_len`."""
    _fn = _rt.cfn("yetty_yrich_paragraph_text_len", _t.yetty_ycore_size_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def paragraph_font_size(obj: Any) -> _rt.Result[float]:
    """Call `yetty_yrich_paragraph_font_size`."""
    _fn = _rt.cfn("yetty_yrich_paragraph_font_size", _t.yetty_ycore_float_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def paragraph_line_spacing(obj: Any) -> _rt.Result[float]:
    """Call `yetty_yrich_paragraph_line_spacing`."""
    _fn = _rt.cfn("yetty_yrich_paragraph_line_spacing", _t.yetty_ycore_float_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def paragraph_set_line_spacing(obj: Any, spacing: float) -> _rt.Result[None]:
    """Call `yetty_yrich_paragraph_set_line_spacing`."""
    _fn = _rt.cfn("yetty_yrich_paragraph_set_line_spacing", _t.yetty_ycore_void_result, [c_void_p, c_float])
    res = _fn(_rt.handle(obj), spacing)
    return _rt.result_from_c(res)

def paragraph_indent(obj: Any) -> _rt.Result[float]:
    """Call `yetty_yrich_paragraph_indent`."""
    _fn = _rt.cfn("yetty_yrich_paragraph_indent", _t.yetty_ycore_float_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def paragraph_heading_level(obj: Any) -> _rt.Result[int]:
    """Call `yetty_yrich_paragraph_heading_level`."""
    _fn = _rt.cfn("yetty_yrich_paragraph_heading_level", _t.yetty_ycore_uint32_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def paragraph_set_heading_level(obj: Any, level: int) -> _rt.Result[None]:
    """Call `yetty_yrich_paragraph_set_heading_level`."""
    _fn = _rt.cfn("yetty_yrich_paragraph_set_heading_level", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), level)
    return _rt.result_from_c(res)

def paragraph_set_indent(obj: Any, indent: float) -> _rt.Result[None]:
    """Call `yetty_yrich_paragraph_set_indent`."""
    _fn = _rt.cfn("yetty_yrich_paragraph_set_indent", _t.yetty_ycore_void_result, [c_void_p, c_float])
    res = _fn(_rt.handle(obj), indent)
    return _rt.result_from_c(res)

def paragraph_color(obj: Any) -> _rt.Result[int]:
    """Call `yetty_yrich_paragraph_color`."""
    _fn = _rt.cfn("yetty_yrich_paragraph_color", _t.yetty_ycore_uint32_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def paragraph_format(obj: Any) -> _rt.Result[int]:
    """Call `yetty_yrich_paragraph_format`."""
    _fn = _rt.cfn("yetty_yrich_paragraph_format", _t.yetty_ycore_uint32_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def paragraph_alignment(obj: Any) -> _rt.Result[int]:
    """Call `yetty_yrich_paragraph_alignment`."""
    _fn = _rt.cfn("yetty_yrich_paragraph_alignment", _t.yetty_ycore_uint32_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def paragraph_set_alignment(obj: Any, halign: int) -> _rt.Result[None]:
    """Call `yetty_yrich_paragraph_set_alignment`."""
    _fn = _rt.cfn("yetty_yrich_paragraph_set_alignment", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), halign)
    return _rt.result_from_c(res)

def paragraph_list_kind(obj: Any) -> _rt.Result[int]:
    """Call `yetty_yrich_paragraph_list_kind`."""
    _fn = _rt.cfn("yetty_yrich_paragraph_list_kind", _t.yetty_ycore_uint32_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def paragraph_set_list_kind(obj: Any, list_kind: int) -> _rt.Result[None]:
    """Call `yetty_yrich_paragraph_set_list_kind`."""
    _fn = _rt.cfn("yetty_yrich_paragraph_set_list_kind", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), list_kind)
    return _rt.result_from_c(res)

def paragraph_list_checked(obj: Any) -> _rt.Result[int]:
    """Call `yetty_yrich_paragraph_list_checked`."""
    _fn = _rt.cfn("yetty_yrich_paragraph_list_checked", _t.yetty_ycore_uint32_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def paragraph_set_list_checked(obj: Any, checked: int) -> _rt.Result[None]:
    """Call `yetty_yrich_paragraph_set_list_checked`."""
    _fn = _rt.cfn("yetty_yrich_paragraph_set_list_checked", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), checked)
    return _rt.result_from_c(res)

def paragraph_block_kind(obj: Any) -> _rt.Result[int]:
    """Call `yetty_yrich_paragraph_block_kind`."""
    _fn = _rt.cfn("yetty_yrich_paragraph_block_kind", _t.yetty_ycore_uint32_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def paragraph_set_block_kind(obj: Any, block_kind: int) -> _rt.Result[None]:
    """Call `yetty_yrich_paragraph_set_block_kind`."""
    _fn = _rt.cfn("yetty_yrich_paragraph_set_block_kind", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), block_kind)
    return _rt.result_from_c(res)

def paragraph_table_size(obj: Any, out_rows: Any, out_cols: Any) -> _rt.Result[None]:
    """Call `yetty_yrich_paragraph_table_size`."""
    _fn = _rt.cfn("yetty_yrich_paragraph_table_size", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(out_rows), _rt.handle(out_cols))
    return _rt.result_from_c(res)

def paragraph_set_table(obj: Any, rows: int, cols: int) -> _rt.Result[None]:
    """Call `yetty_yrich_paragraph_set_table`."""
    _fn = _rt.cfn("yetty_yrich_paragraph_set_table", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_uint32])
    res = _fn(_rt.handle(obj), rows, cols)
    return _rt.result_from_c(res)

def paragraph_table_cell(obj: Any, row: int, col: int) -> _rt.Result[str | None]:
    """Call `yetty_yrich_paragraph_table_cell`."""
    _fn = _rt.cfn("yetty_yrich_paragraph_table_cell", _t.yetty_ycore_const_char_ptr_result, [c_void_p, c_uint32, c_uint32])
    res = _fn(_rt.handle(obj), row, col)
    return _rt.result_from_c(res, _rt.decode_cstr)

def paragraph_set_table_cell(obj: Any, row: int, col: int, text: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_yrich_paragraph_set_table_cell`."""
    _fn = _rt.cfn("yetty_yrich_paragraph_set_table_cell", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_uint32, c_char_p])
    res = _fn(_rt.handle(obj), row, col, _rt.cstr(text))
    return _rt.result_from_c(res)

def paragraph_space_before(obj: Any) -> _rt.Result[float]:
    """Call `yetty_yrich_paragraph_space_before`."""
    _fn = _rt.cfn("yetty_yrich_paragraph_space_before", _t.yetty_ycore_float_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def paragraph_set_space_before(obj: Any, px: float) -> _rt.Result[None]:
    """Call `yetty_yrich_paragraph_set_space_before`."""
    _fn = _rt.cfn("yetty_yrich_paragraph_set_space_before", _t.yetty_ycore_void_result, [c_void_p, c_float])
    res = _fn(_rt.handle(obj), px)
    return _rt.result_from_c(res)

def paragraph_space_after(obj: Any) -> _rt.Result[float]:
    """Call `yetty_yrich_paragraph_space_after`."""
    _fn = _rt.cfn("yetty_yrich_paragraph_space_after", _t.yetty_ycore_float_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def paragraph_set_space_after(obj: Any, px: float) -> _rt.Result[None]:
    """Call `yetty_yrich_paragraph_set_space_after`."""
    _fn = _rt.cfn("yetty_yrich_paragraph_set_space_after", _t.yetty_ycore_void_result, [c_void_p, c_float])
    res = _fn(_rt.handle(obj), px)
    return _rt.result_from_c(res)

def paragraph_list_level(obj: Any) -> _rt.Result[int]:
    """Call `yetty_yrich_paragraph_list_level`."""
    _fn = _rt.cfn("yetty_yrich_paragraph_list_level", _t.yetty_ycore_uint32_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def paragraph_set_list_level(obj: Any, level: int) -> _rt.Result[None]:
    """Call `yetty_yrich_paragraph_set_list_level`."""
    _fn = _rt.cfn("yetty_yrich_paragraph_set_list_level", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), level)
    return _rt.result_from_c(res)

def paragraph_run_count(obj: Any) -> _rt.Result[int]:
    """Call `yetty_yrich_paragraph_run_count`."""
    _fn = _rt.cfn("yetty_yrich_paragraph_run_count", _t.yetty_ycore_size_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def paragraph_run_get(obj: Any, index: int, out_start: Any, out_end: Any, out_format: Any, out_color: Any, out_bg_color: Any, out_font_size: Any) -> _rt.Result[None]:
    """Call `yetty_yrich_paragraph_run_get`."""
    _fn = _rt.cfn("yetty_yrich_paragraph_run_get", _t.yetty_ycore_void_result, [c_void_p, c_size_t, c_void_p, c_void_p, c_void_p, c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), index, _rt.handle(out_start), _rt.handle(out_end), _rt.handle(out_format), _rt.handle(out_color), _rt.handle(out_bg_color), _rt.handle(out_font_size))
    return _rt.result_from_c(res)

def paragraph_run_link_id(obj: Any, index: int) -> _rt.Result[int]:
    """Call `yetty_yrich_paragraph_run_link_id`."""
    _fn = _rt.cfn("yetty_yrich_paragraph_run_link_id", _t.yetty_ycore_uint32_result, [c_void_p, c_size_t])
    res = _fn(_rt.handle(obj), index)
    return _rt.result_from_c(res)

def paragraph_set_bookmark(obj: Any, name: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_yrich_paragraph_set_bookmark`."""
    _fn = _rt.cfn("yetty_yrich_paragraph_set_bookmark", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(name))
    return _rt.result_from_c(res)

def paragraph_bookmark(obj: Any) -> _rt.Result[str | None]:
    """Call `yetty_yrich_paragraph_bookmark`."""
    _fn = _rt.cfn("yetty_yrich_paragraph_bookmark", _t.yetty_ycore_const_char_ptr_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res, _rt.decode_cstr)

def paragraph_add_run(obj: Any, start: int, end: int, format: int, color: int, bg_color: int, font_size: float) -> _rt.Result[None]:
    """Call `yetty_yrich_paragraph_add_run`."""
    _fn = _rt.cfn("yetty_yrich_paragraph_add_run", _t.yetty_ycore_void_result, [c_void_p, c_int32, c_int32, c_uint32, c_uint32, c_uint32, c_float])
    res = _fn(_rt.handle(obj), start, end, format, color, bg_color, font_size)
    return _rt.result_from_c(res)

