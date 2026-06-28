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
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['App']:
        obj = cls()
        return obj.init_result

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
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Document']:
        obj = cls()
        return obj.init_result
    def constructor(self) -> _rt.Result[None]:
        """Call `yetty_yrich_constructor`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yrich_constructor", _t.yetty_ycore_void_result, [c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def document_destroy(self) -> _rt.Result[None]:
        """Call `yetty_yrich_document_destroy`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yrich_document_destroy", _t.yetty_ycore_void_result, [c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def document_content_width(self) -> _rt.Result[float]:
        """Call `yetty_yrich_document_content_width`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yrich_document_content_width", _t.yetty_ycore_float_result, [c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def document_content_height(self) -> _rt.Result[float]:
        """Call `yetty_yrich_document_content_height`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yrich_document_content_height", _t.yetty_ycore_float_result, [c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def document_render(self) -> _rt.Result[None]:
        """Call `yetty_yrich_document_render`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yrich_document_render", _t.yetty_ycore_void_result, [c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def document_apply_op(self, local_flag: int) -> _rt.Result[None]:
        """Call `yetty_yrich_document_apply_op`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yrich_document_apply_op", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_int])
        res = _fn(None, self._handle, local_flag)
        return _rt.result_from_c(res)
    def document_undo(self) -> _rt.Result[None]:
        """Call `yetty_yrich_document_undo`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yrich_document_undo", _t.yetty_ycore_void_result, [c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def document_redo(self) -> _rt.Result[None]:
        """Call `yetty_yrich_document_redo`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yrich_document_redo", _t.yetty_ycore_void_result, [c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def document_on_mouse_down(self, y: float, button: int, mods: int) -> _rt.Result[None]:
        """Call `yetty_yrich_document_on_mouse_down`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yrich_document_on_mouse_down", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float, c_uint32, c_uint32])
        res = _fn(None, self._handle, y, button, mods)
        return _rt.result_from_c(res)
    def document_on_mouse_up(self, y: float, button: int, mods: int) -> _rt.Result[None]:
        """Call `yetty_yrich_document_on_mouse_up`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yrich_document_on_mouse_up", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float, c_uint32, c_uint32])
        res = _fn(None, self._handle, y, button, mods)
        return _rt.result_from_c(res)
    def document_on_mouse_drag(self, y: float, button: int, mods: int) -> _rt.Result[None]:
        """Call `yetty_yrich_document_on_mouse_drag`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yrich_document_on_mouse_drag", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float, c_uint32, c_uint32])
        res = _fn(None, self._handle, y, button, mods)
        return _rt.result_from_c(res)
    def document_on_mouse_double_click(self, y: float, button: int, mods: int) -> _rt.Result[None]:
        """Call `yetty_yrich_document_on_mouse_double_click`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yrich_document_on_mouse_double_click", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float, c_uint32, c_uint32])
        res = _fn(None, self._handle, y, button, mods)
        return _rt.result_from_c(res)
    def document_on_key_down(self, mods: int) -> _rt.Result[None]:
        """Call `yetty_yrich_document_on_key_down`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yrich_document_on_key_down", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_uint32])
        res = _fn(None, self._handle, mods)
        return _rt.result_from_c(res)
    def document_on_text_input(self) -> _rt.Result[None]:
        """Call `yetty_yrich_document_on_text_input`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yrich_document_on_text_input", _t.yetty_ycore_void_result, [c_void_p, _t.yetty_ycore_buffer])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)

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
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Element']:
        obj = cls()
        return obj.init_result
    def element_destroy(self) -> _rt.Result[None]:
        """Call `yetty_yrich_element_destroy`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yrich_element_destroy", _t.yetty_ycore_void_result, [c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def element_bounds(self) -> _rt.Result[None]:
        """Call `yetty_yrich_element_bounds`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yrich_element_bounds", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def element_hit_test(self, y: float) -> _rt.Result[int]:
        """Call `yetty_yrich_element_hit_test`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yrich_element_hit_test", _t.yetty_ycore_int_result, [c_void_p, c_float, c_float])
        res = _fn(None, self._handle, y)
        return _rt.result_from_c(res)
    def element_render(self, layer: int, selected: int) -> _rt.Result[None]:
        """Call `yetty_yrich_element_render`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yrich_element_render", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_uint32, c_int])
        res = _fn(None, self._handle, layer, selected)
        return _rt.result_from_c(res)
    def element_is_editable(self) -> _rt.Result[int]:
        """Call `yetty_yrich_element_is_editable`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yrich_element_is_editable", _t.yetty_ycore_int_result, [c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def element_begin_edit(self) -> _rt.Result[None]:
        """Call `yetty_yrich_element_begin_edit`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yrich_element_begin_edit", _t.yetty_ycore_void_result, [c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def element_end_edit(self) -> _rt.Result[None]:
        """Call `yetty_yrich_element_end_edit`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yrich_element_end_edit", _t.yetty_ycore_void_result, [c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def element_is_editing(self) -> _rt.Result[int]:
        """Call `yetty_yrich_element_is_editing`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yrich_element_is_editing", _t.yetty_ycore_int_result, [c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def element_insert_text(self) -> _rt.Result[None]:
        """Call `yetty_yrich_element_insert_text`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yrich_element_insert_text", _t.yetty_ycore_void_result, [c_void_p, _t.yetty_ycore_buffer])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def element_delete_sel(self) -> _rt.Result[None]:
        """Call `yetty_yrich_element_delete_sel`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yrich_element_delete_sel", _t.yetty_ycore_void_result, [c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)

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
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Shape']:
        obj = cls()
        return obj.init_result
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
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Slides']:
        obj = cls()
        return obj.init_result
    def slides_set_current(self) -> _rt.Result[None]:
        """Call `yetty_yrich_slides_set_current`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yrich_slides_set_current", _t.yetty_ycore_void_result, [c_void_p, c_int32])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def slides_next(self) -> _rt.Result[None]:
        """Call `yetty_yrich_slides_next`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yrich_slides_next", _t.yetty_ycore_void_result, [c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def slides_prev(self) -> _rt.Result[None]:
        """Call `yetty_yrich_slides_prev`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yrich_slides_prev", _t.yetty_ycore_void_result, [c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)

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
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Cell']:
        obj = cls()
        return obj.init_result

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
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Spreadsheet']:
        obj = cls()
        return obj.init_result
    def spreadsheet_set_grid_size(self, cols: int) -> _rt.Result[None]:
        """Call `yetty_yrich_spreadsheet_set_grid_size`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yrich_spreadsheet_set_grid_size", _t.yetty_ycore_void_result, [c_void_p, c_int32, c_int32])
        res = _fn(None, self._handle, cols)
        return _rt.result_from_c(res)
    def spreadsheet_set_row_height(self, height: float) -> _rt.Result[None]:
        """Call `yetty_yrich_spreadsheet_set_row_height`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yrich_spreadsheet_set_row_height", _t.yetty_ycore_void_result, [c_void_p, c_int32, c_float])
        res = _fn(None, self._handle, height)
        return _rt.result_from_c(res)
    def spreadsheet_set_col_width(self, width: float) -> _rt.Result[None]:
        """Call `yetty_yrich_spreadsheet_set_col_width`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yrich_spreadsheet_set_col_width", _t.yetty_ycore_void_result, [c_void_p, c_int32, c_float])
        res = _fn(None, self._handle, width)
        return _rt.result_from_c(res)
    def spreadsheet_set_cell_value(self, col: int, value: _t.yetty_ycore_buffer) -> _rt.Result[None]:
        """Call `yetty_yrich_spreadsheet_set_cell_value`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yrich_spreadsheet_set_cell_value", _t.yetty_ycore_void_result, [c_void_p, c_int32, c_int32, _t.yetty_ycore_buffer])
        res = _fn(None, self._handle, col, value)
        return _rt.result_from_c(res)

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
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Paragraph']:
        obj = cls()
        return obj.init_result

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
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['InlineImage']:
        obj = cls()
        return obj.init_result

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
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Ydoc']:
        obj = cls()
        return obj.init_result
    def ydoc_toggle_format(self) -> _rt.Result[None]:
        """Call `yetty_yrich_ydoc_toggle_format`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yrich_ydoc_toggle_format", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def ydoc_set_text_color(self) -> _rt.Result[None]:
        """Call `yetty_yrich_ydoc_set_text_color`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yrich_ydoc_set_text_color", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def ydoc_set_alignment(self) -> _rt.Result[None]:
        """Call `yetty_yrich_ydoc_set_alignment`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yrich_ydoc_set_alignment", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def ydoc_set_heading(self) -> _rt.Result[None]:
        """Call `yetty_yrich_ydoc_set_heading`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yrich_ydoc_set_heading", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def ydoc_change_font_size(self) -> _rt.Result[None]:
        """Call `yetty_yrich_ydoc_change_font_size`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yrich_ydoc_change_font_size", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)

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

def ydoc_insert_image(obj: Any, paragraph_index: int, width: float, height: float) -> _rt.Result[Any]:
    """Call `yetty_yrich_ydoc_insert_image`."""
    _fn = _rt.cfn("yetty_yrich_ydoc_insert_image", _t.yetty_yclass_object_ptr_result, [c_void_p, c_int32, c_float, c_float])
    res = _fn(_rt.handle(obj), paragraph_index, width, height)
    return _rt.result_from_c(res)

def ydoc_selection_text(obj: Any) -> _rt.Result[str | None]:
    """Call `yetty_yrich_ydoc_selection_text`."""
    _fn = _rt.cfn("yetty_yrich_ydoc_selection_text", _t.yetty_ycore_char_ptr_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res, _rt.decode_cstr)

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

def paragraph_run_count(obj: Any) -> _rt.Result[int]:
    """Call `yetty_yrich_paragraph_run_count`."""
    _fn = _rt.cfn("yetty_yrich_paragraph_run_count", _t.yetty_ycore_size_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def paragraph_run_get(obj: Any, index: int, out_start: Any, out_end: Any, out_format: Any, out_color: Any) -> _rt.Result[None]:
    """Call `yetty_yrich_paragraph_run_get`."""
    _fn = _rt.cfn("yetty_yrich_paragraph_run_get", _t.yetty_ycore_void_result, [c_void_p, c_size_t, c_void_p, c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), index, _rt.handle(out_start), _rt.handle(out_end), _rt.handle(out_format), _rt.handle(out_color))
    return _rt.result_from_c(res)

def paragraph_add_run(obj: Any, start: int, end: int, format: int, color: int) -> _rt.Result[None]:
    """Call `yetty_yrich_paragraph_add_run`."""
    _fn = _rt.cfn("yetty_yrich_paragraph_add_run", _t.yetty_ycore_void_result, [c_void_p, c_int32, c_int32, c_uint32, c_uint32])
    res = _fn(_rt.handle(obj), start, end, format, color)
    return _rt.result_from_c(res)

