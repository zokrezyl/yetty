"""yetty.ygui bindings — GENERATED from model.yaml, do not edit."""
from __future__ import annotations
from ctypes import (c_bool, c_char, c_char_p, c_double, c_float, c_int,
    c_int8, c_int16, c_int32, c_int64, c_long, c_size_t, c_ssize_t,
    c_uint, c_uint8, c_uint16, c_uint32, c_uint64, c_ulong, c_void_p)
from typing import Any, ClassVar
from .. import runtime as _rt
from . import _types as _t

class Framework(_rt.YClass):
    """yclass ygui:framework"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'framework'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_framework_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_framework_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Framework']:
        obj = cls()
        return obj.init_result

class Widget(_rt.YClass):
    """yclass ygui:widget"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'widget'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_widget_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_widget_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Widget']:
        obj = cls()
        return obj.init_result
    def constructor(self) -> _rt.Result[None]:
        """Call `yetty_ygui_constructor`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_ygui_constructor", _t.yetty_ycore_void_result, [c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def destructor(self) -> _rt.Result[None]:
        """Call `yetty_ygui_destructor`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_ygui_destructor", _t.yetty_ycore_void_result, [c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def widget_on_press(self, y: float, button: int) -> _rt.Result[int]:
        """Call `yetty_ygui_widget_on_press`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_ygui_widget_on_press", _t.yetty_ycore_int_result, [c_void_p, c_float, c_float, c_int])
        res = _fn(None, self._handle, y, button)
        return _rt.result_from_c(res)
    def widget_on_release(self, y: float, button: int) -> _rt.Result[int]:
        """Call `yetty_ygui_widget_on_release`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_ygui_widget_on_release", _t.yetty_ycore_int_result, [c_void_p, c_float, c_float, c_int])
        res = _fn(None, self._handle, y, button)
        return _rt.result_from_c(res)
    def widget_on_motion(self, y: float) -> _rt.Result[int]:
        """Call `yetty_ygui_widget_on_motion`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_ygui_widget_on_motion", _t.yetty_ycore_int_result, [c_void_p, c_float, c_float])
        res = _fn(None, self._handle, y)
        return _rt.result_from_c(res)
    def widget_on_scroll(self, y: float, dx: float, dy: float) -> _rt.Result[int]:
        """Call `yetty_ygui_widget_on_scroll`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_ygui_widget_on_scroll", _t.yetty_ycore_int_result, [c_void_p, c_float, c_float, c_float, c_float])
        res = _fn(None, self._handle, y, dx, dy)
        return _rt.result_from_c(res)
    def widget_paint(self) -> _rt.Result[None]:
        """Call `yetty_ygui_widget_paint`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_ygui_widget_paint", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def widget_emit_container(self) -> _rt.Result[None]:
        """Call `yetty_ygui_widget_emit_container`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_ygui_widget_emit_container", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def widget_emit_body(self) -> _rt.Result[None]:
        """Call `yetty_ygui_widget_emit_body`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_ygui_widget_emit_body", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)

class PrimitiveWidget(Widget):
    """yclass ygui:primitive_widget"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'primitive_widget'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_primitive_widget_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_primitive_widget_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['PrimitiveWidget']:
        obj = cls()
        return obj.init_result

class Breadcrumbs(PrimitiveWidget):
    """yclass ygui:breadcrumbs"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'breadcrumbs'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_breadcrumbs_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_breadcrumbs_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Breadcrumbs']:
        obj = cls()
        return obj.init_result

class Button(PrimitiveWidget):
    """yclass ygui:button"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'button'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_button_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_button_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Button']:
        obj = cls()
        return obj.init_result

class Checkbox(PrimitiveWidget):
    """yclass ygui:checkbox"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'checkbox'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_checkbox_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_checkbox_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Checkbox']:
        obj = cls()
        return obj.init_result

class Chip(PrimitiveWidget):
    """yclass ygui:chip"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'chip'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_chip_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_chip_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Chip']:
        obj = cls()
        return obj.init_result

class Choicebox(PrimitiveWidget):
    """yclass ygui:choicebox"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'choicebox'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_choicebox_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_choicebox_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Choicebox']:
        obj = cls()
        return obj.init_result

class Vbox(PrimitiveWidget):
    """yclass ygui:vbox"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'vbox'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_vbox_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_vbox_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Vbox']:
        obj = cls()
        return obj.init_result

class CollapsingHeader(Vbox):
    """yclass ygui:collapsing_header"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'collapsing_header'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_collapsing_header_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_collapsing_header_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['CollapsingHeader']:
        obj = cls()
        return obj.init_result

class Colorpicker(PrimitiveWidget):
    """yclass ygui:colorpicker"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'colorpicker'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_colorpicker_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_colorpicker_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Colorpicker']:
        obj = cls()
        return obj.init_result

class Combobox(PrimitiveWidget):
    """yclass ygui:combobox"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'combobox'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_combobox_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_combobox_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Combobox']:
        obj = cls()
        return obj.init_result

class Datepicker(PrimitiveWidget):
    """yclass ygui:datepicker"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'datepicker'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_datepicker_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_datepicker_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Datepicker']:
        obj = cls()
        return obj.init_result

class Dialog(Vbox):
    """yclass ygui:dialog"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'dialog'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_dialog_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_dialog_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Dialog']:
        obj = cls()
        return obj.init_result

class Dropdown(PrimitiveWidget):
    """yclass ygui:dropdown"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'dropdown'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_dropdown_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_dropdown_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Dropdown']:
        obj = cls()
        return obj.init_result

class Filepicker(PrimitiveWidget):
    """yclass ygui:filepicker"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'filepicker'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_filepicker_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_filepicker_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Filepicker']:
        obj = cls()
        return obj.init_result

class Hbox(PrimitiveWidget):
    """yclass ygui:hbox"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'hbox'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_hbox_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_hbox_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Hbox']:
        obj = cls()
        return obj.init_result

class Label(PrimitiveWidget):
    """yclass ygui:label"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'label'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_label_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_label_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Label']:
        obj = cls()
        return obj.init_result

class List(PrimitiveWidget):
    """yclass ygui:list"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'list'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_list_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_list_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['List']:
        obj = cls()
        return obj.init_result

class Menubar(Hbox):
    """yclass ygui:menubar"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'menubar'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_menubar_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_menubar_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Menubar']:
        obj = cls()
        return obj.init_result

class Panel(PrimitiveWidget):
    """yclass ygui:panel"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'panel'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_panel_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_panel_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Panel']:
        obj = cls()
        return obj.init_result

class PopupMenu(PrimitiveWidget):
    """yclass ygui:popup_menu"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'popup_menu'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_popup_menu_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_popup_menu_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['PopupMenu']:
        obj = cls()
        return obj.init_result

class Progress(PrimitiveWidget):
    """yclass ygui:progress"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'progress'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_progress_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_progress_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Progress']:
        obj = cls()
        return obj.init_result

class Radio(PrimitiveWidget):
    """yclass ygui:radio"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'radio'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_radio_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_radio_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Radio']:
        obj = cls()
        return obj.init_result

class Rich(PrimitiveWidget):
    """yclass ygui:rich"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'rich'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_rich_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_rich_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Rich']:
        obj = cls()
        return obj.init_result

class Scrollarea(Vbox):
    """yclass ygui:scrollarea"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'scrollarea'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_scrollarea_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_scrollarea_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Scrollarea']:
        obj = cls()
        return obj.init_result

class Selectable(PrimitiveWidget):
    """yclass ygui:selectable"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'selectable'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_selectable_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_selectable_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Selectable']:
        obj = cls()
        return obj.init_result

class Separator(PrimitiveWidget):
    """yclass ygui:separator"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'separator'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_separator_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_separator_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Separator']:
        obj = cls()
        return obj.init_result

class Slider(PrimitiveWidget):
    """yclass ygui:slider"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'slider'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_slider_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_slider_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Slider']:
        obj = cls()
        return obj.init_result

class Spinner(PrimitiveWidget):
    """yclass ygui:spinner"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'spinner'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_spinner_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_spinner_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Spinner']:
        obj = cls()
        return obj.init_result

class Splitter(PrimitiveWidget):
    """yclass ygui:splitter"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'splitter'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_splitter_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_splitter_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Splitter']:
        obj = cls()
        return obj.init_result

class Statusbar(PrimitiveWidget):
    """yclass ygui:statusbar"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'statusbar'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_statusbar_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_statusbar_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Statusbar']:
        obj = cls()
        return obj.init_result

class Stepper(PrimitiveWidget):
    """yclass ygui:stepper"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'stepper'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_stepper_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_stepper_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Stepper']:
        obj = cls()
        return obj.init_result

class Tabbar(Hbox):
    """yclass ygui:tabbar"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'tabbar'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_tabbar_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_tabbar_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Tabbar']:
        obj = cls()
        return obj.init_result

class Table(PrimitiveWidget):
    """yclass ygui:table"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'table'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_table_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_table_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Table']:
        obj = cls()
        return obj.init_result

class Textarea(PrimitiveWidget):
    """yclass ygui:textarea"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'textarea'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_textarea_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_textarea_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Textarea']:
        obj = cls()
        return obj.init_result

class Textinput(PrimitiveWidget):
    """yclass ygui:textinput"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'textinput'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_textinput_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_textinput_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Textinput']:
        obj = cls()
        return obj.init_result

class Toggle(PrimitiveWidget):
    """yclass ygui:toggle"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'toggle'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_toggle_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_toggle_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Toggle']:
        obj = cls()
        return obj.init_result

class Tooltip(PrimitiveWidget):
    """yclass ygui:tooltip"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'tooltip'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_tooltip_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_tooltip_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Tooltip']:
        obj = cls()
        return obj.init_result

class TreeNode(Vbox):
    """yclass ygui:tree_node"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'tree_node'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_tree_node_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_tree_node_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['TreeNode']:
        obj = cls()
        return obj.init_result

class Window(Vbox):
    """yclass ygui:window"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'window'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_window_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_window_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Window']:
        obj = cls()
        return obj.init_result

class YdrawEmbed(PrimitiveWidget):
    """yclass ygui:ydraw_embed"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'ydraw_embed'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_ydraw_embed_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_ydraw_embed_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['YdrawEmbed']:
        obj = cls()
        return obj.init_result

class Ybrowser(YdrawEmbed):
    """yclass ygui:ybrowser"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'ybrowser'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_ybrowser_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_ybrowser_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Ybrowser']:
        obj = cls()
        return obj.init_result

class Ydiagram(YdrawEmbed):
    """yclass ygui:ydiagram"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'ydiagram'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_ydiagram_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_ydiagram_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Ydiagram']:
        obj = cls()
        return obj.init_result

class Yimage(Widget):
    """yclass ygui:yimage"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'yimage'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_yimage_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_yimage_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Yimage']:
        obj = cls()
        return obj.init_result

class Yjungle(YdrawEmbed):
    """yclass ygui:yjungle"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'yjungle'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_yjungle_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_yjungle_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Yjungle']:
        obj = cls()
        return obj.init_result

class Ymarkdown(YdrawEmbed):
    """yclass ygui:ymarkdown"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'ymarkdown'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_ymarkdown_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_ymarkdown_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Ymarkdown']:
        obj = cls()
        return obj.init_result

class Ymaze(YdrawEmbed):
    """yclass ygui:ymaze"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'ymaze'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_ymaze_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_ymaze_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Ymaze']:
        obj = cls()
        return obj.init_result

class Ynode(Vbox):
    """yclass ygui:ynode"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'ynode'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_ynode_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_ynode_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Ynode']:
        obj = cls()
        return obj.init_result

class Ynodes(PrimitiveWidget):
    """yclass ygui:ynodes"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'ynodes'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_ynodes_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_ynodes_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Ynodes']:
        obj = cls()
        return obj.init_result

class Ypdf(YdrawEmbed):
    """yclass ygui:ypdf"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'ypdf'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_ypdf_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_ypdf_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Ypdf']:
        obj = cls()
        return obj.init_result

class Yplot(Widget):
    """yclass ygui:yplot"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'yplot'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_yplot_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_yplot_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Yplot']:
        obj = cls()
        return obj.init_result

class YrichView(YdrawEmbed):
    """yclass ygui:yrich_view"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'yrich_view'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_yrich_view_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_yrich_view_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['YrichView']:
        obj = cls()
        return obj.init_result

class Yshadertoy(Widget):
    """yclass ygui:yshadertoy"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'yshadertoy'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_yshadertoy_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_yshadertoy_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Yshadertoy']:
        obj = cls()
        return obj.init_result

class Yvideo(Widget):
    """yclass ygui:yvideo"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'yvideo'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_yvideo_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_yvideo_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Yvideo']:
        obj = cls()
        return obj.init_result

class Yzoo(YdrawEmbed):
    """yclass ygui:yzoo"""
    __yclass_domain__: ClassVar[str] = 'ygui'
    __yclass_name__: ClassVar[str] = 'yzoo'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui_yzoo_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_yzoo_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Yzoo']:
        obj = cls()
        return obj.init_result

def framework_destroy(obj: Any) -> _rt.Result[None]:
    """Call `yetty_ygui_framework_destroy`."""
    _fn = _rt.cfn("yetty_ygui_framework_destroy", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def framework_set_container_obj(obj: Any, container: Any) -> _rt.Result[None]:
    """Call `yetty_ygui_framework_set_container_obj`."""
    _fn = _rt.cfn("yetty_ygui_framework_set_container_obj", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(container))
    return _rt.result_from_c(res)

def framework_set_session(obj: Any, session: Any) -> _rt.Result[None]:
    """Call `yetty_ygui_framework_set_session`."""
    _fn = _rt.cfn("yetty_ygui_framework_set_session", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(session))
    return _rt.result_from_c(res)

def framework_attach(obj: Any, read_fd: int, write_fd: int, compressed: int) -> _rt.Result[None]:
    """Call `yetty_ygui_framework_attach`."""
    _fn = _rt.cfn("yetty_ygui_framework_attach", _t.yetty_ycore_void_result, [c_void_p, c_int, c_int, c_int])
    res = _fn(_rt.handle(obj), read_fd, write_fd, compressed)
    return _rt.result_from_c(res)

def framework_set_viewport(obj: Any, width_px: float, height_px: float) -> _rt.Result[None]:
    """Call `yetty_ygui_framework_set_viewport`."""
    _fn = _rt.cfn("yetty_ygui_framework_set_viewport", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float])
    res = _fn(_rt.handle(obj), width_px, height_px)
    return _rt.result_from_c(res)

def framework_set_theme(obj: Any, theme: Any) -> _rt.Result[None]:
    """Call `yetty_ygui_framework_set_theme`."""
    _fn = _rt.cfn("yetty_ygui_framework_set_theme", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(theme))
    return _rt.result_from_c(res)

def framework_apply_config_to_theme(obj: Any, config: Any) -> _rt.Result[None]:
    """Call `yetty_ygui_framework_apply_config_to_theme`."""
    _fn = _rt.cfn("yetty_ygui_framework_apply_config_to_theme", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(config))
    return _rt.result_from_c(res)

def framework_feed_input(obj: Any, bytes: str | bytes | None, n: int) -> _rt.Result[None]:
    """Call `yetty_ygui_framework_feed_input`."""
    _fn = _rt.cfn("yetty_ygui_framework_feed_input", _t.yetty_ycore_void_result, [c_void_p, c_char_p, c_size_t])
    res = _fn(_rt.handle(obj), _rt.cstr(bytes), n)
    return _rt.result_from_c(res)

def framework_feed_mouse_button(obj: Any, x: float, y: float, button: int, pressed: int, mods: int) -> _rt.Result[int]:
    """Call `yetty_ygui_framework_feed_mouse_button`."""
    _fn = _rt.cfn("yetty_ygui_framework_feed_mouse_button", _t.yetty_ycore_int_result, [c_void_p, c_float, c_float, c_int, c_int, c_int])
    res = _fn(_rt.handle(obj), x, y, button, pressed, mods)
    return _rt.result_from_c(res)

def framework_feed_mouse_motion(obj: Any, x: float, y: float) -> _rt.Result[int]:
    """Call `yetty_ygui_framework_feed_mouse_motion`."""
    _fn = _rt.cfn("yetty_ygui_framework_feed_mouse_motion", _t.yetty_ycore_int_result, [c_void_p, c_float, c_float])
    res = _fn(_rt.handle(obj), x, y)
    return _rt.result_from_c(res)

def framework_feed_mouse_scroll(obj: Any, x: float, y: float, dx: float, dy: float) -> _rt.Result[None]:
    """Call `yetty_ygui_framework_feed_mouse_scroll`."""
    _fn = _rt.cfn("yetty_ygui_framework_feed_mouse_scroll", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float, c_float, c_float])
    res = _fn(_rt.handle(obj), x, y, dx, dy)
    return _rt.result_from_c(res)

def framework_set_root(obj: Any, root: Any) -> _rt.Result[None]:
    """Call `yetty_ygui_framework_set_root`."""
    _fn = _rt.cfn("yetty_ygui_framework_set_root", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(root))
    return _rt.result_from_c(res)

def framework_alloc_id(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ygui_framework_alloc_id`."""
    _fn = _rt.cfn("yetty_ygui_framework_alloc_id", _t.uint32_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def framework_free_id(obj: Any, id: int) -> _rt.Result[None]:
    """Call `yetty_ygui_framework_free_id`."""
    _fn = _rt.cfn("yetty_ygui_framework_free_id", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), id)
    return _rt.result_from_c(res)

def framework_clear(obj: Any) -> _rt.Result[None]:
    """Call `yetty_ygui_framework_clear`."""
    _fn = _rt.cfn("yetty_ygui_framework_clear", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def framework_emit(obj: Any) -> _rt.Result[None]:
    """Call `yetty_ygui_framework_emit`."""
    _fn = _rt.cfn("yetty_ygui_framework_emit", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def clickable_on_click_set(obj: Any, cb: Any, userdata: Any) -> _rt.Result[None]:
    """Call `yetty_ygui_clickable_on_click_set`."""
    _fn = _rt.cfn("yetty_ygui_clickable_on_click_set", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(cb), _rt.handle(userdata))
    return _rt.result_from_c(res)

def clickable_is_pressed(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ygui_clickable_is_pressed`."""
    _fn = _rt.cfn("yetty_ygui_clickable_is_pressed", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def clickable_press_pos(obj: Any, x: Any, y: Any) -> _rt.Result[None]:
    """Call `yetty_ygui_clickable_press_pos`."""
    _fn = _rt.cfn("yetty_ygui_clickable_press_pos", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(x), _rt.handle(y))
    return _rt.result_from_c(res)

def draggable_on_drag_set(obj: Any, cb: Any, userdata: Any) -> _rt.Result[None]:
    """Call `yetty_ygui_draggable_on_drag_set`."""
    _fn = _rt.cfn("yetty_ygui_draggable_on_drag_set", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(cb), _rt.handle(userdata))
    return _rt.result_from_c(res)

def draggable_is_dragging(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ygui_draggable_is_dragging`."""
    _fn = _rt.cfn("yetty_ygui_draggable_is_dragging", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def layout_compute(root: Any, root_rect: _t.yetty_ycore_rectangle) -> _rt.Result[None]:
    """Call `yetty_ygui_layout_compute`."""
    _fn = _rt.cfn("yetty_ygui_layout_compute", _t.yetty_ycore_void_result, [c_void_p, _t.yetty_ycore_rectangle])
    res = _fn(_rt.handle(root), root_rect)
    return _rt.result_from_c(res)

def layout_default() -> _t.yetty_ygui_layout:
    """Call `yetty_ygui_layout_default`."""
    _fn = _rt.cfn("yetty_ygui_layout_default", _t.yetty_ygui_layout, [])
    return _fn()

def widget_set_rect(obj: Any, rect: _t.yetty_ycore_rectangle) -> _rt.Result[None]:
    """Call `yetty_ygui_widget_set_rect`."""
    _fn = _rt.cfn("yetty_ygui_widget_set_rect", _t.yetty_ycore_void_result, [c_void_p, _t.yetty_ycore_rectangle])
    res = _fn(_rt.handle(obj), rect)
    return _rt.result_from_c(res)

def widget_rect(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_ygui_widget_rect`."""
    _fn = _rt.cfn("yetty_ygui_widget_rect", _t.yetty_ycore_rectangle_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def widget_layout_set(obj: Any, layout: Any) -> _rt.Result[None]:
    """Call `yetty_ygui_widget_layout_set`."""
    _fn = _rt.cfn("yetty_ygui_widget_layout_set", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(layout))
    return _rt.result_from_c(res)

def widget_layout_get(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_ygui_widget_layout_get`."""
    _fn = _rt.cfn("yetty_ygui_widget_layout_get", _t.yetty_ygui_layout_const_ptr_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def widget_set_visible(obj: Any, visible: int) -> _rt.Result[None]:
    """Call `yetty_ygui_widget_set_visible`."""
    _fn = _rt.cfn("yetty_ygui_widget_set_visible", _t.yetty_ycore_void_result, [c_void_p, c_int])
    res = _fn(_rt.handle(obj), visible)
    return _rt.result_from_c(res)

def widget_is_visible(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ygui_widget_is_visible`."""
    _fn = _rt.cfn("yetty_ygui_widget_is_visible", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def widget_set_size(obj: Any, w: float, h: float) -> _rt.Result[None]:
    """Call `yetty_ygui_widget_set_size`."""
    _fn = _rt.cfn("yetty_ygui_widget_set_size", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float])
    res = _fn(_rt.handle(obj), w, h)
    return _rt.result_from_c(res)

def widget_set_position(obj: Any, x: float, y: float) -> _rt.Result[None]:
    """Call `yetty_ygui_widget_set_position`."""
    _fn = _rt.cfn("yetty_ygui_widget_set_position", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float])
    res = _fn(_rt.handle(obj), x, y)
    return _rt.result_from_c(res)

def widget_make_figure(obj: Any, kind: int, z: int) -> _rt.Result[None]:
    """Call `yetty_ygui_widget_make_figure`."""
    _fn = _rt.cfn("yetty_ygui_widget_make_figure", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_int32])
    res = _fn(_rt.handle(obj), kind, z)
    return _rt.result_from_c(res)

def widget_set_figure_z(obj: Any, z: int) -> _rt.Result[None]:
    """Call `yetty_ygui_widget_set_figure_z`."""
    _fn = _rt.cfn("yetty_ygui_widget_set_figure_z", _t.yetty_ycore_void_result, [c_void_p, c_int32])
    res = _fn(_rt.handle(obj), z)
    return _rt.result_from_c(res)

def widget_set_floating(obj: Any, floating: int) -> _rt.Result[None]:
    """Call `yetty_ygui_widget_set_floating`."""
    _fn = _rt.cfn("yetty_ygui_widget_set_floating", _t.yetty_ycore_void_result, [c_void_p, c_int])
    res = _fn(_rt.handle(obj), floating)
    return _rt.result_from_c(res)

def widget_is_floating(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ygui_widget_is_floating`."""
    _fn = _rt.cfn("yetty_ygui_widget_is_floating", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def widget_figure_kind(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ygui_widget_figure_kind`."""
    _fn = _rt.cfn("yetty_ygui_widget_figure_kind", _t.yetty_ycore_uint32_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def widget_figure_z(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ygui_widget_figure_z`."""
    _fn = _rt.cfn("yetty_ygui_widget_figure_z", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def widget_set_bg_color(obj: Any, color: int) -> _rt.Result[None]:
    """Call `yetty_ygui_widget_set_bg_color`."""
    _fn = _rt.cfn("yetty_ygui_widget_set_bg_color", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), color)
    return _rt.result_from_c(res)

def widget_bg(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ygui_widget_bg`."""
    _fn = _rt.cfn("yetty_ygui_widget_bg", _t.yetty_ycore_uint32_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def widget_apply_css(obj: Any, css: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ygui_widget_apply_css`."""
    _fn = _rt.cfn("yetty_ygui_widget_apply_css", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(css))
    return _rt.result_from_c(res)

def super_void(obj: Any, self_class: Any, method_id: Any) -> _rt.Result[None]:
    """Call `yetty_ygui_super_void`."""
    _fn = _rt.cfn("yetty_ygui_super_void", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(self_class), _rt.handle(method_id))
    return _rt.result_from_c(res)

def super_int(obj: Any, self_class: Any, method_id: Any) -> _rt.Result[int]:
    """Call `yetty_ygui_super_int`."""
    _fn = _rt.cfn("yetty_ygui_super_int", _t.yetty_ycore_int_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(self_class), _rt.handle(method_id))
    return _rt.result_from_c(res)

def class_expect(class_result: _t.yetty_yclass_ptr_result, name: str | bytes | None) -> Any:
    """Call `yetty_ygui_class_expect`."""
    _fn = _rt.cfn("yetty_ygui_class_expect", c_void_p, [_t.yetty_yclass_ptr_result, c_char_p])
    return _fn(class_result, _rt.cstr(name))

def widget_parent(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_ygui_widget_parent`."""
    _fn = _rt.cfn("yetty_ygui_widget_parent", _t.yetty_yclass_object_ptr_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def widget_first_child(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_ygui_widget_first_child`."""
    _fn = _rt.cfn("yetty_ygui_widget_first_child", _t.yetty_yclass_object_ptr_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def widget_next_sibling(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_ygui_widget_next_sibling`."""
    _fn = _rt.cfn("yetty_ygui_widget_next_sibling", _t.yetty_yclass_object_ptr_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def widget_framework(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_ygui_widget_framework`."""
    _fn = _rt.cfn("yetty_ygui_widget_framework", _t.yetty_yclass_object_ptr_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def widget_id(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ygui_widget_id`."""
    _fn = _rt.cfn("yetty_ygui_widget_id", _t.yetty_ycore_uint32_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def widget_set_dirty(obj: Any) -> _rt.Result[None]:
    """Call `yetty_ygui_widget_set_dirty`."""
    _fn = _rt.cfn("yetty_ygui_widget_set_dirty", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def widget_is_dirty(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ygui_widget_is_dirty`."""
    _fn = _rt.cfn("yetty_ygui_widget_is_dirty", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def widget_is_hovered(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ygui_widget_is_hovered`."""
    _fn = _rt.cfn("yetty_ygui_widget_is_hovered", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def widget_new(cls: Any) -> _rt.Result[Any]:
    """Call `yetty_ygui_widget_new`."""
    _fn = _rt.cfn("yetty_ygui_widget_new", _t.yetty_yclass_object_ptr_result, [c_void_p])
    res = _fn(_rt.handle(cls))
    return _rt.result_from_c(res)

def widget_add(parent: Any, cls: Any) -> _rt.Result[Any]:
    """Call `yetty_ygui_widget_add`."""
    _fn = _rt.cfn("yetty_ygui_widget_add", _t.yetty_yclass_object_ptr_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(parent), _rt.handle(cls))
    return _rt.result_from_c(res)

def widget_destroy(obj: Any) -> _rt.Result[None]:
    """Call `yetty_ygui_widget_destroy`."""
    _fn = _rt.cfn("yetty_ygui_widget_destroy", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def widget_raise(obj: Any) -> _rt.Result[None]:
    """Call `yetty_ygui_widget_raise`."""
    _fn = _rt.cfn("yetty_ygui_widget_raise", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def breadcrumbs_add(obj: Any, text: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ygui_breadcrumbs_add`."""
    _fn = _rt.cfn("yetty_ygui_breadcrumbs_add", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(text))
    return _rt.result_from_c(res)

def breadcrumbs_clear(obj: Any) -> _rt.Result[None]:
    """Call `yetty_ygui_breadcrumbs_clear`."""
    _fn = _rt.cfn("yetty_ygui_breadcrumbs_clear", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def button_set_label(obj: Any, label: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ygui_button_set_label`."""
    _fn = _rt.cfn("yetty_ygui_button_set_label", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(label))
    return _rt.result_from_c(res)

def button_set_chrome_icon(obj: Any, kind: int) -> _rt.Result[None]:
    """Call `yetty_ygui_button_set_chrome_icon`."""
    _fn = _rt.cfn("yetty_ygui_button_set_chrome_icon", _t.yetty_ycore_void_result, [c_void_p, c_int])
    res = _fn(_rt.handle(obj), kind)
    return _rt.result_from_c(res)

def button_get_label(obj: Any) -> _rt.Result[str | None]:
    """Call `yetty_ygui_button_get_label`."""
    _fn = _rt.cfn("yetty_ygui_button_get_label", _t.yetty_ycore_const_char_ptr_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res, _rt.decode_cstr)

def checkbox_set_label(obj: Any, label: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ygui_checkbox_set_label`."""
    _fn = _rt.cfn("yetty_ygui_checkbox_set_label", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(label))
    return _rt.result_from_c(res)

def checkbox_set_checked(obj: Any, checked: int) -> _rt.Result[None]:
    """Call `yetty_ygui_checkbox_set_checked`."""
    _fn = _rt.cfn("yetty_ygui_checkbox_set_checked", _t.yetty_ycore_void_result, [c_void_p, c_int])
    res = _fn(_rt.handle(obj), checked)
    return _rt.result_from_c(res)

def checkbox_get_checked(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ygui_checkbox_get_checked`."""
    _fn = _rt.cfn("yetty_ygui_checkbox_get_checked", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def chip_set_label(obj: Any, label: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ygui_chip_set_label`."""
    _fn = _rt.cfn("yetty_ygui_chip_set_label", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(label))
    return _rt.result_from_c(res)

def chip_set_closable(obj: Any, c: int) -> _rt.Result[None]:
    """Call `yetty_ygui_chip_set_closable`."""
    _fn = _rt.cfn("yetty_ygui_chip_set_closable", _t.yetty_ycore_void_result, [c_void_p, c_int])
    res = _fn(_rt.handle(obj), c)
    return _rt.result_from_c(res)

def choicebox_add(obj: Any, label: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ygui_choicebox_add`."""
    _fn = _rt.cfn("yetty_ygui_choicebox_add", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(label))
    return _rt.result_from_c(res)

def choicebox_is_selected(obj: Any, idx: int) -> _rt.Result[int]:
    """Call `yetty_ygui_choicebox_is_selected`."""
    _fn = _rt.cfn("yetty_ygui_choicebox_is_selected", _t.yetty_ycore_int_result, [c_void_p, c_int])
    res = _fn(_rt.handle(obj), idx)
    return _rt.result_from_c(res)

def collapsing_header_set_title(obj: Any, title: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ygui_collapsing_header_set_title`."""
    _fn = _rt.cfn("yetty_ygui_collapsing_header_set_title", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(title))
    return _rt.result_from_c(res)

def collapsing_header_set_open(obj: Any, open: int) -> _rt.Result[None]:
    """Call `yetty_ygui_collapsing_header_set_open`."""
    _fn = _rt.cfn("yetty_ygui_collapsing_header_set_open", _t.yetty_ycore_void_result, [c_void_p, c_int])
    res = _fn(_rt.handle(obj), open)
    return _rt.result_from_c(res)

def collapsing_header_is_open(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ygui_collapsing_header_is_open`."""
    _fn = _rt.cfn("yetty_ygui_collapsing_header_is_open", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def colorpicker_set_color(obj: Any, c: int) -> _rt.Result[None]:
    """Call `yetty_ygui_colorpicker_set_color`."""
    _fn = _rt.cfn("yetty_ygui_colorpicker_set_color", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), c)
    return _rt.result_from_c(res)

def colorpicker_get_color(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ygui_colorpicker_get_color`."""
    _fn = _rt.cfn("yetty_ygui_colorpicker_get_color", _t.yetty_ycore_uint32_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def combobox_set_text(obj: Any, t: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ygui_combobox_set_text`."""
    _fn = _rt.cfn("yetty_ygui_combobox_set_text", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(t))
    return _rt.result_from_c(res)

def combobox_add_suggestion(obj: Any, t: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ygui_combobox_add_suggestion`."""
    _fn = _rt.cfn("yetty_ygui_combobox_add_suggestion", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(t))
    return _rt.result_from_c(res)

def combobox_set_menu(obj: Any, menu: Any) -> _rt.Result[None]:
    """Call `yetty_ygui_combobox_set_menu`."""
    _fn = _rt.cfn("yetty_ygui_combobox_set_menu", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(menu))
    return _rt.result_from_c(res)

def combobox_get_text(obj: Any) -> _rt.Result[str | None]:
    """Call `yetty_ygui_combobox_get_text`."""
    _fn = _rt.cfn("yetty_ygui_combobox_get_text", _t.yetty_ycore_const_char_ptr_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res, _rt.decode_cstr)

def datepicker_set_date(obj: Any, year: int, month_0_based: int, day: int) -> _rt.Result[None]:
    """Call `yetty_ygui_datepicker_set_date`."""
    _fn = _rt.cfn("yetty_ygui_datepicker_set_date", _t.yetty_ycore_void_result, [c_void_p, c_int, c_int, c_int])
    res = _fn(_rt.handle(obj), year, month_0_based, day)
    return _rt.result_from_c(res)

def datepicker_get_date(obj: Any, year: Any, month_0_based: Any, day: Any) -> _rt.Result[None]:
    """Call `yetty_ygui_datepicker_get_date`."""
    _fn = _rt.cfn("yetty_ygui_datepicker_get_date", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(year), _rt.handle(month_0_based), _rt.handle(day))
    return _rt.result_from_c(res)

def dialog_set_title(obj: Any, title: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ygui_dialog_set_title`."""
    _fn = _rt.cfn("yetty_ygui_dialog_set_title", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(title))
    return _rt.result_from_c(res)

def dialog_set_closable(obj: Any, closable: int) -> _rt.Result[None]:
    """Call `yetty_ygui_dialog_set_closable`."""
    _fn = _rt.cfn("yetty_ygui_dialog_set_closable", _t.yetty_ycore_void_result, [c_void_p, c_int])
    res = _fn(_rt.handle(obj), closable)
    return _rt.result_from_c(res)

def dialog_open_at(obj: Any, x: float, y: float, width: float, height: float) -> _rt.Result[None]:
    """Call `yetty_ygui_dialog_open_at`."""
    _fn = _rt.cfn("yetty_ygui_dialog_open_at", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float, c_float, c_float])
    res = _fn(_rt.handle(obj), x, y, width, height)
    return _rt.result_from_c(res)

def dialog_close(obj: Any) -> _rt.Result[None]:
    """Call `yetty_ygui_dialog_close`."""
    _fn = _rt.cfn("yetty_ygui_dialog_close", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def dialog_is_open(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ygui_dialog_is_open`."""
    _fn = _rt.cfn("yetty_ygui_dialog_is_open", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def dropdown_add_option(obj: Any, label: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ygui_dropdown_add_option`."""
    _fn = _rt.cfn("yetty_ygui_dropdown_add_option", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(label))
    return _rt.result_from_c(res)

def dropdown_set_selected(obj: Any, index: int) -> _rt.Result[None]:
    """Call `yetty_ygui_dropdown_set_selected`."""
    _fn = _rt.cfn("yetty_ygui_dropdown_set_selected", _t.yetty_ycore_void_result, [c_void_p, c_int])
    res = _fn(_rt.handle(obj), index)
    return _rt.result_from_c(res)

def dropdown_get_selected(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ygui_dropdown_get_selected`."""
    _fn = _rt.cfn("yetty_ygui_dropdown_get_selected", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def dropdown_set_menu(obj: Any, menu: Any) -> _rt.Result[None]:
    """Call `yetty_ygui_dropdown_set_menu`."""
    _fn = _rt.cfn("yetty_ygui_dropdown_set_menu", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(menu))
    return _rt.result_from_c(res)

def filepicker_set_dir(obj: Any, path: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ygui_filepicker_set_dir`."""
    _fn = _rt.cfn("yetty_ygui_filepicker_set_dir", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(path))
    return _rt.result_from_c(res)

def filepicker_get_dir(obj: Any) -> _rt.Result[str | None]:
    """Call `yetty_ygui_filepicker_get_dir`."""
    _fn = _rt.cfn("yetty_ygui_filepicker_get_dir", _t.yetty_ycore_const_char_ptr_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res, _rt.decode_cstr)

def label_set_text(obj: Any, text: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ygui_label_set_text`."""
    _fn = _rt.cfn("yetty_ygui_label_set_text", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(text))
    return _rt.result_from_c(res)

def label_get_text(obj: Any) -> _rt.Result[str | None]:
    """Call `yetty_ygui_label_get_text`."""
    _fn = _rt.cfn("yetty_ygui_label_get_text", _t.yetty_ycore_const_char_ptr_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res, _rt.decode_cstr)

def label_set_font_size(obj: Any, size_px: float) -> _rt.Result[None]:
    """Call `yetty_ygui_label_set_font_size`."""
    _fn = _rt.cfn("yetty_ygui_label_set_font_size", _t.yetty_ycore_void_result, [c_void_p, c_float])
    res = _fn(_rt.handle(obj), size_px)
    return _rt.result_from_c(res)

def label_set_color(obj: Any, color: _t.yetty_ycore_rgba) -> _rt.Result[None]:
    """Call `yetty_ygui_label_set_color`."""
    _fn = _rt.cfn("yetty_ygui_label_set_color", _t.yetty_ycore_void_result, [c_void_p, _t.yetty_ycore_rgba])
    res = _fn(_rt.handle(obj), color)
    return _rt.result_from_c(res)

def list_add(obj: Any, label: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ygui_list_add`."""
    _fn = _rt.cfn("yetty_ygui_list_add", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(label))
    return _rt.result_from_c(res)

def list_set_selected(obj: Any, i: int) -> _rt.Result[None]:
    """Call `yetty_ygui_list_set_selected`."""
    _fn = _rt.cfn("yetty_ygui_list_set_selected", _t.yetty_ycore_void_result, [c_void_p, c_int])
    res = _fn(_rt.handle(obj), i)
    return _rt.result_from_c(res)

def list_get_selected(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ygui_list_get_selected`."""
    _fn = _rt.cfn("yetty_ygui_list_get_selected", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def menubar_add(bar: Any, label: str | bytes | None, menu: Any) -> _rt.Result[None]:
    """Call `yetty_ygui_menubar_add`."""
    _fn = _rt.cfn("yetty_ygui_menubar_add", _t.yetty_ycore_void_result, [c_void_p, c_char_p, c_void_p])
    res = _fn(_rt.handle(bar), _rt.cstr(label), _rt.handle(menu))
    return _rt.result_from_c(res)

def panel_set_bg(obj: Any, color: _t.yetty_ycore_rgba) -> _rt.Result[None]:
    """Call `yetty_ygui_panel_set_bg`."""
    _fn = _rt.cfn("yetty_ygui_panel_set_bg", _t.yetty_ycore_void_result, [c_void_p, _t.yetty_ycore_rgba])
    res = _fn(_rt.handle(obj), color)
    return _rt.result_from_c(res)

def panel_set_border(obj: Any, color: _t.yetty_ycore_rgba, width_px: float) -> _rt.Result[None]:
    """Call `yetty_ygui_panel_set_border`."""
    _fn = _rt.cfn("yetty_ygui_panel_set_border", _t.yetty_ycore_void_result, [c_void_p, _t.yetty_ycore_rgba, c_float])
    res = _fn(_rt.handle(obj), color, width_px)
    return _rt.result_from_c(res)

def popup_menu_add_item(obj: Any, label: str | bytes | None, cb: Any, userdata: Any) -> _rt.Result[None]:
    """Call `yetty_ygui_popup_menu_add_item`."""
    _fn = _rt.cfn("yetty_ygui_popup_menu_add_item", _t.yetty_ycore_void_result, [c_void_p, c_char_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.cstr(label), _rt.handle(cb), _rt.handle(userdata))
    return _rt.result_from_c(res)

def popup_menu_add_separator(obj: Any) -> _rt.Result[None]:
    """Call `yetty_ygui_popup_menu_add_separator`."""
    _fn = _rt.cfn("yetty_ygui_popup_menu_add_separator", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def popup_menu_open_at(obj: Any, x: float, y: float) -> _rt.Result[None]:
    """Call `yetty_ygui_popup_menu_open_at`."""
    _fn = _rt.cfn("yetty_ygui_popup_menu_open_at", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float])
    res = _fn(_rt.handle(obj), x, y)
    return _rt.result_from_c(res)

def popup_menu_close(obj: Any) -> _rt.Result[None]:
    """Call `yetty_ygui_popup_menu_close`."""
    _fn = _rt.cfn("yetty_ygui_popup_menu_close", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def popup_menu_is_open(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ygui_popup_menu_is_open`."""
    _fn = _rt.cfn("yetty_ygui_popup_menu_is_open", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def popup_menu_toggle_at(obj: Any, x: float, y: float) -> _rt.Result[None]:
    """Call `yetty_ygui_popup_menu_toggle_at`."""
    _fn = _rt.cfn("yetty_ygui_popup_menu_toggle_at", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float])
    res = _fn(_rt.handle(obj), x, y)
    return _rt.result_from_c(res)

def popup_menu_clear(obj: Any) -> _rt.Result[None]:
    """Call `yetty_ygui_popup_menu_clear`."""
    _fn = _rt.cfn("yetty_ygui_popup_menu_clear", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def popup_menu_set_title(obj: Any, title: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ygui_popup_menu_set_title`."""
    _fn = _rt.cfn("yetty_ygui_popup_menu_set_title", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(title))
    return _rt.result_from_c(res)

def popup_menu_set_back(obj: Any, label: str | bytes | None, cb: Any, userdata: Any) -> _rt.Result[None]:
    """Call `yetty_ygui_popup_menu_set_back`."""
    _fn = _rt.cfn("yetty_ygui_popup_menu_set_back", _t.yetty_ycore_void_result, [c_void_p, c_char_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.cstr(label), _rt.handle(cb), _rt.handle(userdata))
    return _rt.result_from_c(res)

def popup_menu_add_drill_item(obj: Any, label: str | bytes | None, cb: Any, userdata: Any) -> _rt.Result[None]:
    """Call `yetty_ygui_popup_menu_add_drill_item`."""
    _fn = _rt.cfn("yetty_ygui_popup_menu_add_drill_item", _t.yetty_ycore_void_result, [c_void_p, c_char_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.cstr(label), _rt.handle(cb), _rt.handle(userdata))
    return _rt.result_from_c(res)

def popup_menu_set_modal(obj: Any, modal: int) -> _rt.Result[None]:
    """Call `yetty_ygui_popup_menu_set_modal`."""
    _fn = _rt.cfn("yetty_ygui_popup_menu_set_modal", _t.yetty_ycore_void_result, [c_void_p, c_int])
    res = _fn(_rt.handle(obj), modal)
    return _rt.result_from_c(res)

def progress_set_value(obj: Any, value: float) -> _rt.Result[None]:
    """Call `yetty_ygui_progress_set_value`."""
    _fn = _rt.cfn("yetty_ygui_progress_set_value", _t.yetty_ycore_void_result, [c_void_p, c_float])
    res = _fn(_rt.handle(obj), value)
    return _rt.result_from_c(res)

def progress_get_value(obj: Any) -> _rt.Result[float]:
    """Call `yetty_ygui_progress_get_value`."""
    _fn = _rt.cfn("yetty_ygui_progress_get_value", _t.yetty_ycore_float_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def progress_set_accent(obj: Any, color: int) -> _rt.Result[None]:
    """Call `yetty_ygui_progress_set_accent`."""
    _fn = _rt.cfn("yetty_ygui_progress_set_accent", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), color)
    return _rt.result_from_c(res)

def radio_set_label(obj: Any, label: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ygui_radio_set_label`."""
    _fn = _rt.cfn("yetty_ygui_radio_set_label", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(label))
    return _rt.result_from_c(res)

def radio_set_selected(obj: Any, s: int) -> _rt.Result[None]:
    """Call `yetty_ygui_radio_set_selected`."""
    _fn = _rt.cfn("yetty_ygui_radio_set_selected", _t.yetty_ycore_void_result, [c_void_p, c_int])
    res = _fn(_rt.handle(obj), s)
    return _rt.result_from_c(res)

def radio_is_selected(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ygui_radio_is_selected`."""
    _fn = _rt.cfn("yetty_ygui_radio_is_selected", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def rich_clear(obj: Any) -> _rt.Result[None]:
    """Call `yetty_ygui_rich_clear`."""
    _fn = _rt.cfn("yetty_ygui_rich_clear", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def rich_add_line(obj: Any) -> _rt.Result[None]:
    """Call `yetty_ygui_rich_add_line`."""
    _fn = _rt.cfn("yetty_ygui_rich_add_line", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def rich_add_span(obj: Any, text: str | bytes | None, font_size: float, color_rgba: int) -> _rt.Result[None]:
    """Call `yetty_ygui_rich_add_span`."""
    _fn = _rt.cfn("yetty_ygui_rich_add_span", _t.yetty_ycore_void_result, [c_void_p, c_char_p, c_float, c_uint32])
    res = _fn(_rt.handle(obj), _rt.cstr(text), font_size, color_rgba)
    return _rt.result_from_c(res)

def selectable_set_text(obj: Any, t: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ygui_selectable_set_text`."""
    _fn = _rt.cfn("yetty_ygui_selectable_set_text", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(t))
    return _rt.result_from_c(res)

def selectable_set_selected(obj: Any, s: int) -> _rt.Result[None]:
    """Call `yetty_ygui_selectable_set_selected`."""
    _fn = _rt.cfn("yetty_ygui_selectable_set_selected", _t.yetty_ycore_void_result, [c_void_p, c_int])
    res = _fn(_rt.handle(obj), s)
    return _rt.result_from_c(res)

def selectable_is_selected(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ygui_selectable_is_selected`."""
    _fn = _rt.cfn("yetty_ygui_selectable_is_selected", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def slider_set_range(obj: Any, min: float, max: float) -> _rt.Result[None]:
    """Call `yetty_ygui_slider_set_range`."""
    _fn = _rt.cfn("yetty_ygui_slider_set_range", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float])
    res = _fn(_rt.handle(obj), min, max)
    return _rt.result_from_c(res)

def slider_set_value(obj: Any, value: float) -> _rt.Result[None]:
    """Call `yetty_ygui_slider_set_value`."""
    _fn = _rt.cfn("yetty_ygui_slider_set_value", _t.yetty_ycore_void_result, [c_void_p, c_float])
    res = _fn(_rt.handle(obj), value)
    return _rt.result_from_c(res)

def slider_get_value(obj: Any) -> _rt.Result[float]:
    """Call `yetty_ygui_slider_get_value`."""
    _fn = _rt.cfn("yetty_ygui_slider_get_value", _t.yetty_ycore_float_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def spinner_set_value(obj: Any, v: float) -> _rt.Result[None]:
    """Call `yetty_ygui_spinner_set_value`."""
    _fn = _rt.cfn("yetty_ygui_spinner_set_value", _t.yetty_ycore_void_result, [c_void_p, c_float])
    res = _fn(_rt.handle(obj), v)
    return _rt.result_from_c(res)

def spinner_set_range(obj: Any, mn: float, mx: float, step: float) -> _rt.Result[None]:
    """Call `yetty_ygui_spinner_set_range`."""
    _fn = _rt.cfn("yetty_ygui_spinner_set_range", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float, c_float])
    res = _fn(_rt.handle(obj), mn, mx, step)
    return _rt.result_from_c(res)

def spinner_get_value(obj: Any) -> _rt.Result[float]:
    """Call `yetty_ygui_spinner_get_value`."""
    _fn = _rt.cfn("yetty_ygui_spinner_get_value", _t.yetty_ycore_float_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def splitter_set_axis(obj: Any, row: int) -> _rt.Result[None]:
    """Call `yetty_ygui_splitter_set_axis`."""
    _fn = _rt.cfn("yetty_ygui_splitter_set_axis", _t.yetty_ycore_void_result, [c_void_p, c_int])
    res = _fn(_rt.handle(obj), row)
    return _rt.result_from_c(res)

def splitter_get_axis(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ygui_splitter_get_axis`."""
    _fn = _rt.cfn("yetty_ygui_splitter_get_axis", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def splitter_set_min(obj: Any, min_size: float) -> _rt.Result[None]:
    """Call `yetty_ygui_splitter_set_min`."""
    _fn = _rt.cfn("yetty_ygui_splitter_set_min", _t.yetty_ycore_void_result, [c_void_p, c_float])
    res = _fn(_rt.handle(obj), min_size)
    return _rt.result_from_c(res)

def splitter_on_change(obj: Any, cb: Any, userdata: Any) -> _rt.Result[None]:
    """Call `yetty_ygui_splitter_on_change`."""
    _fn = _rt.cfn("yetty_ygui_splitter_on_change", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(cb), _rt.handle(userdata))
    return _rt.result_from_c(res)

def statusbar_set_left(obj: Any, text: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ygui_statusbar_set_left`."""
    _fn = _rt.cfn("yetty_ygui_statusbar_set_left", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(text))
    return _rt.result_from_c(res)

def statusbar_set_right(obj: Any, text: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ygui_statusbar_set_right`."""
    _fn = _rt.cfn("yetty_ygui_statusbar_set_right", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(text))
    return _rt.result_from_c(res)

def stepper_add_step(obj: Any, label: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ygui_stepper_add_step`."""
    _fn = _rt.cfn("yetty_ygui_stepper_add_step", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(label))
    return _rt.result_from_c(res)

def stepper_set_current(obj: Any, i: int) -> _rt.Result[None]:
    """Call `yetty_ygui_stepper_set_current`."""
    _fn = _rt.cfn("yetty_ygui_stepper_set_current", _t.yetty_ycore_void_result, [c_void_p, c_int])
    res = _fn(_rt.handle(obj), i)
    return _rt.result_from_c(res)

def tabbar_add_tab(tabbar: Any, label: str | bytes | None) -> _rt.Result[Any]:
    """Call `yetty_ygui_tabbar_add_tab`."""
    _fn = _rt.cfn("yetty_ygui_tabbar_add_tab", _t.yetty_yclass_object_ptr_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(tabbar), _rt.cstr(label))
    return _rt.result_from_c(res)

def tabbar_remove_tab(tabbar: Any, index: int) -> _rt.Result[None]:
    """Call `yetty_ygui_tabbar_remove_tab`."""
    _fn = _rt.cfn("yetty_ygui_tabbar_remove_tab", _t.yetty_ycore_void_result, [c_void_p, c_int])
    res = _fn(_rt.handle(tabbar), index)
    return _rt.result_from_c(res)

def tabbar_set_label(tabbar: Any, index: int, label: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ygui_tabbar_set_label`."""
    _fn = _rt.cfn("yetty_ygui_tabbar_set_label", _t.yetty_ycore_void_result, [c_void_p, c_int, c_char_p])
    res = _fn(_rt.handle(tabbar), index, _rt.cstr(label))
    return _rt.result_from_c(res)

def tabbar_count(tabbar: Any) -> _rt.Result[int]:
    """Call `yetty_ygui_tabbar_count`."""
    _fn = _rt.cfn("yetty_ygui_tabbar_count", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(tabbar))
    return _rt.result_from_c(res)

def tabbar_active(tabbar: Any) -> _rt.Result[int]:
    """Call `yetty_ygui_tabbar_active`."""
    _fn = _rt.cfn("yetty_ygui_tabbar_active", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(tabbar))
    return _rt.result_from_c(res)

def tabbar_set_active(tabbar: Any, index: int) -> _rt.Result[None]:
    """Call `yetty_ygui_tabbar_set_active`."""
    _fn = _rt.cfn("yetty_ygui_tabbar_set_active", _t.yetty_ycore_void_result, [c_void_p, c_int])
    res = _fn(_rt.handle(tabbar), index)
    return _rt.result_from_c(res)

def tabbar_set_on_close(tabbar: Any, cb: Any, userdata: Any) -> _rt.Result[None]:
    """Call `yetty_ygui_tabbar_set_on_close`."""
    _fn = _rt.cfn("yetty_ygui_tabbar_set_on_close", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(tabbar), _rt.handle(cb), _rt.handle(userdata))
    return _rt.result_from_c(res)

def tabbar_set_on_new_tab(tabbar: Any, cb: Any, userdata: Any) -> _rt.Result[None]:
    """Call `yetty_ygui_tabbar_set_on_new_tab`."""
    _fn = _rt.cfn("yetty_ygui_tabbar_set_on_new_tab", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(tabbar), _rt.handle(cb), _rt.handle(userdata))
    return _rt.result_from_c(res)

def table_set_columns(obj: Any, n_cols: int, headers: Any) -> _rt.Result[None]:
    """Call `yetty_ygui_table_set_columns`."""
    _fn = _rt.cfn("yetty_ygui_table_set_columns", _t.yetty_ycore_void_result, [c_void_p, c_int, c_void_p])
    res = _fn(_rt.handle(obj), n_cols, _rt.handle(headers))
    return _rt.result_from_c(res)

def table_add_row(obj: Any, cells: Any, n_cells: int) -> _rt.Result[None]:
    """Call `yetty_ygui_table_add_row`."""
    _fn = _rt.cfn("yetty_ygui_table_add_row", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_int])
    res = _fn(_rt.handle(obj), _rt.handle(cells), n_cells)
    return _rt.result_from_c(res)

def table_clear_rows(obj: Any) -> _rt.Result[None]:
    """Call `yetty_ygui_table_clear_rows`."""
    _fn = _rt.cfn("yetty_ygui_table_clear_rows", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def textarea_set_text(obj: Any, text: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ygui_textarea_set_text`."""
    _fn = _rt.cfn("yetty_ygui_textarea_set_text", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(text))
    return _rt.result_from_c(res)

def textarea_get_text(obj: Any) -> _rt.Result[str | None]:
    """Call `yetty_ygui_textarea_get_text`."""
    _fn = _rt.cfn("yetty_ygui_textarea_get_text", _t.yetty_ycore_const_char_ptr_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res, _rt.decode_cstr)

def textinput_set_text(obj: Any, text: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ygui_textinput_set_text`."""
    _fn = _rt.cfn("yetty_ygui_textinput_set_text", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(text))
    return _rt.result_from_c(res)

def textinput_get_text(obj: Any) -> _rt.Result[str | None]:
    """Call `yetty_ygui_textinput_get_text`."""
    _fn = _rt.cfn("yetty_ygui_textinput_get_text", _t.yetty_ycore_const_char_ptr_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res, _rt.decode_cstr)

def textinput_set_placeholder(obj: Any, placeholder: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ygui_textinput_set_placeholder`."""
    _fn = _rt.cfn("yetty_ygui_textinput_set_placeholder", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(placeholder))
    return _rt.result_from_c(res)

def textinput_set_focus(obj: Any, focused: int) -> _rt.Result[None]:
    """Call `yetty_ygui_textinput_set_focus`."""
    _fn = _rt.cfn("yetty_ygui_textinput_set_focus", _t.yetty_ycore_void_result, [c_void_p, c_int])
    res = _fn(_rt.handle(obj), focused)
    return _rt.result_from_c(res)

def textinput_handle_key(obj: Any, key: int) -> _rt.Result[int]:
    """Call `yetty_ygui_textinput_handle_key`."""
    _fn = _rt.cfn("yetty_ygui_textinput_handle_key", _t.yetty_ycore_int_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), key)
    return _rt.result_from_c(res)

def toggle_set_label(obj: Any, label: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ygui_toggle_set_label`."""
    _fn = _rt.cfn("yetty_ygui_toggle_set_label", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(label))
    return _rt.result_from_c(res)

def toggle_set_on(obj: Any, arg1: int) -> _rt.Result[None]:
    """Call `yetty_ygui_toggle_set_on`."""
    _fn = _rt.cfn("yetty_ygui_toggle_set_on", _t.yetty_ycore_void_result, [c_void_p, c_int])
    res = _fn(_rt.handle(obj), arg0)
    return _rt.result_from_c(res)

def toggle_get_on(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ygui_toggle_get_on`."""
    _fn = _rt.cfn("yetty_ygui_toggle_get_on", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def tooltip_set_text(obj: Any, text: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ygui_tooltip_set_text`."""
    _fn = _rt.cfn("yetty_ygui_tooltip_set_text", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(text))
    return _rt.result_from_c(res)

def tooltip_get_text(obj: Any) -> _rt.Result[str | None]:
    """Call `yetty_ygui_tooltip_get_text`."""
    _fn = _rt.cfn("yetty_ygui_tooltip_get_text", _t.yetty_ycore_const_char_ptr_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res, _rt.decode_cstr)

def tree_node_set_label(obj: Any, label: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ygui_tree_node_set_label`."""
    _fn = _rt.cfn("yetty_ygui_tree_node_set_label", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(label))
    return _rt.result_from_c(res)

def tree_node_set_open(obj: Any, o: int) -> _rt.Result[None]:
    """Call `yetty_ygui_tree_node_set_open`."""
    _fn = _rt.cfn("yetty_ygui_tree_node_set_open", _t.yetty_ycore_void_result, [c_void_p, c_int])
    res = _fn(_rt.handle(obj), o)
    return _rt.result_from_c(res)

def tree_node_is_open(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ygui_tree_node_is_open`."""
    _fn = _rt.cfn("yetty_ygui_tree_node_is_open", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def tree_node_on_toggle(obj: Any, cb: Any, userdata: Any) -> _rt.Result[None]:
    """Call `yetty_ygui_tree_node_on_toggle`."""
    _fn = _rt.cfn("yetty_ygui_tree_node_on_toggle", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(cb), _rt.handle(userdata))
    return _rt.result_from_c(res)

def window_body(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_ygui_window_body`."""
    _fn = _rt.cfn("yetty_ygui_window_body", _t.yetty_yclass_object_ptr_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def window_set_title(obj: Any, title: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ygui_window_set_title`."""
    _fn = _rt.cfn("yetty_ygui_window_set_title", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(title))
    return _rt.result_from_c(res)

def window_set_menu(obj: Any, menu: Any) -> _rt.Result[None]:
    """Call `yetty_ygui_window_set_menu`."""
    _fn = _rt.cfn("yetty_ygui_window_set_menu", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(menu))
    return _rt.result_from_c(res)

def window_set_closable(obj: Any, closable: int) -> _rt.Result[None]:
    """Call `yetty_ygui_window_set_closable`."""
    _fn = _rt.cfn("yetty_ygui_window_set_closable", _t.yetty_ycore_void_result, [c_void_p, c_int])
    res = _fn(_rt.handle(obj), closable)
    return _rt.result_from_c(res)

def window_set_chromeless(obj: Any, chromeless: int) -> _rt.Result[None]:
    """Call `yetty_ygui_window_set_chromeless`."""
    _fn = _rt.cfn("yetty_ygui_window_set_chromeless", _t.yetty_ycore_void_result, [c_void_p, c_int])
    res = _fn(_rt.handle(obj), chromeless)
    return _rt.result_from_c(res)

def ybrowser_set_html(obj: Any, html: str | bytes | None, len: int) -> _rt.Result[None]:
    """Call `yetty_ygui_ybrowser_set_html`."""
    _fn = _rt.cfn("yetty_ygui_ybrowser_set_html", _t.yetty_ycore_void_result, [c_void_p, c_char_p, c_size_t])
    res = _fn(_rt.handle(obj), _rt.cstr(html), len)
    return _rt.result_from_c(res)

def ydiagram_set_source(obj: Any, source: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ygui_ydiagram_set_source`."""
    _fn = _rt.cfn("yetty_ygui_ydiagram_set_source", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(source))
    return _rt.result_from_c(res)

def ydiagram_get_source(obj: Any) -> _rt.Result[str | None]:
    """Call `yetty_ygui_ydiagram_get_source`."""
    _fn = _rt.cfn("yetty_ygui_ydiagram_get_source", _t.yetty_ycore_const_char_ptr_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res, _rt.decode_cstr)

def ydraw_embed_set_buffer(obj: Any, buf: Any) -> _rt.Result[None]:
    """Call `yetty_ygui_ydraw_embed_set_buffer`."""
    _fn = _rt.cfn("yetty_ygui_ydraw_embed_set_buffer", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(buf))
    return _rt.result_from_c(res)

def yimage_set_bytes(obj: Any, bytes: Any, len: int) -> _rt.Result[None]:
    """Call `yetty_ygui_yimage_set_bytes`."""
    _fn = _rt.cfn("yetty_ygui_yimage_set_bytes", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_size_t])
    res = _fn(_rt.handle(obj), _rt.handle(bytes), len)
    return _rt.result_from_c(res)

def yimage_bytes(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_ygui_yimage_bytes`."""
    _fn = _rt.cfn("yetty_ygui_yimage_bytes", _t.yetty_ycore_const_uint8_ptr_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def yimage_bytes_len(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ygui_yimage_bytes_len`."""
    _fn = _rt.cfn("yetty_ygui_yimage_bytes_len", _t.yetty_ycore_size_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def ymarkdown_set_source(obj: Any, src: str | bytes | None, len: int) -> _rt.Result[None]:
    """Call `yetty_ygui_ymarkdown_set_source`."""
    _fn = _rt.cfn("yetty_ygui_ymarkdown_set_source", _t.yetty_ycore_void_result, [c_void_p, c_char_p, c_size_t])
    res = _fn(_rt.handle(obj), _rt.cstr(src), len)
    return _rt.result_from_c(res)

def ymarkdown_set_file(obj: Any, path: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ygui_ymarkdown_set_file`."""
    _fn = _rt.cfn("yetty_ygui_ymarkdown_set_file", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(path))
    return _rt.result_from_c(res)

def ynode_reflow(node: Any) -> _rt.Result[None]:
    """Call `yetty_ygui_ynode_reflow`."""
    _fn = _rt.cfn("yetty_ygui_ynode_reflow", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(node))
    return _rt.result_from_c(res)

def ynode_pin_pos(node: Any, output: int, index: int, x: Any, y: Any) -> _rt.Result[int]:
    """Call `yetty_ygui_ynode_pin_pos`."""
    _fn = _rt.cfn("yetty_ygui_ynode_pin_pos", _t.yetty_ycore_int_result, [c_void_p, c_int, c_int, c_void_p, c_void_p])
    res = _fn(_rt.handle(node), output, index, _rt.handle(x), _rt.handle(y))
    return _rt.result_from_c(res)

def ynode_pin_at(node: Any, x: float, y: float, output: Any, index: Any) -> _rt.Result[int]:
    """Call `yetty_ygui_ynode_pin_at`."""
    _fn = _rt.cfn("yetty_ygui_ynode_pin_at", _t.yetty_ycore_int_result, [c_void_p, c_float, c_float, c_void_p, c_void_p])
    res = _fn(_rt.handle(node), x, y, _rt.handle(output), _rt.handle(index))
    return _rt.result_from_c(res)

def ynode_set_title(node: Any, title: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ygui_ynode_set_title`."""
    _fn = _rt.cfn("yetty_ygui_ynode_set_title", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(node), _rt.cstr(title))
    return _rt.result_from_c(res)

def ynode_set_graph_pos(node: Any, gx: float, gy: float) -> _rt.Result[None]:
    """Call `yetty_ygui_ynode_set_graph_pos`."""
    _fn = _rt.cfn("yetty_ygui_ynode_set_graph_pos", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float])
    res = _fn(_rt.handle(node), gx, gy)
    return _rt.result_from_c(res)

def ynode_graph_pos(node: Any, gx: Any, gy: Any) -> _rt.Result[None]:
    """Call `yetty_ygui_ynode_graph_pos`."""
    _fn = _rt.cfn("yetty_ygui_ynode_graph_pos", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(node), _rt.handle(gx), _rt.handle(gy))
    return _rt.result_from_c(res)

def ynode_set_graph_size(node: Any, gw: float, gh: float) -> _rt.Result[None]:
    """Call `yetty_ygui_ynode_set_graph_size`."""
    _fn = _rt.cfn("yetty_ygui_ynode_set_graph_size", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float])
    res = _fn(_rt.handle(node), gw, gh)
    return _rt.result_from_c(res)

def ynode_graph_size(node: Any, gw: Any, gh: Any) -> _rt.Result[None]:
    """Call `yetty_ygui_ynode_graph_size`."""
    _fn = _rt.cfn("yetty_ygui_ynode_graph_size", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(node), _rt.handle(gw), _rt.handle(gh))
    return _rt.result_from_c(res)

def ynode_add_input(node: Any, name: str | bytes | None) -> _rt.Result[int]:
    """Call `yetty_ygui_ynode_add_input`."""
    _fn = _rt.cfn("yetty_ygui_ynode_add_input", _t.uint32_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(node), _rt.cstr(name))
    return _rt.result_from_c(res)

def ynode_add_output(node: Any, name: str | bytes | None) -> _rt.Result[int]:
    """Call `yetty_ygui_ynode_add_output`."""
    _fn = _rt.cfn("yetty_ygui_ynode_add_output", _t.uint32_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(node), _rt.cstr(name))
    return _rt.result_from_c(res)

def ynode_input_count(node: Any) -> _rt.Result[int]:
    """Call `yetty_ygui_ynode_input_count`."""
    _fn = _rt.cfn("yetty_ygui_ynode_input_count", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(node))
    return _rt.result_from_c(res)

def ynode_output_count(node: Any) -> _rt.Result[int]:
    """Call `yetty_ygui_ynode_output_count`."""
    _fn = _rt.cfn("yetty_ygui_ynode_output_count", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(node))
    return _rt.result_from_c(res)

def ynodes_view(editor: Any, pan_x: Any, pan_y: Any, zoom: Any) -> _rt.Result[None]:
    """Call `yetty_ygui_ynodes_view`."""
    _fn = _rt.cfn("yetty_ygui_ynodes_view", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(editor), _rt.handle(pan_x), _rt.handle(pan_y), _rt.handle(zoom))
    return _rt.result_from_c(res)

def ynodes_zoom(editor: Any) -> _rt.Result[float]:
    """Call `yetty_ygui_ynodes_zoom`."""
    _fn = _rt.cfn("yetty_ygui_ynodes_zoom", _t.yetty_ycore_float_result, [c_void_p])
    res = _fn(_rt.handle(editor))
    return _rt.result_from_c(res)

def ynodes_reflow(editor: Any) -> _rt.Result[None]:
    """Call `yetty_ygui_ynodes_reflow`."""
    _fn = _rt.cfn("yetty_ygui_ynodes_reflow", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(editor))
    return _rt.result_from_c(res)

def ynodes_set_view(editor: Any, pan_x: float, pan_y: float, zoom: float) -> _rt.Result[None]:
    """Call `yetty_ygui_ynodes_set_view`."""
    _fn = _rt.cfn("yetty_ygui_ynodes_set_view", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float, c_float])
    res = _fn(_rt.handle(editor), pan_x, pan_y, zoom)
    return _rt.result_from_c(res)

def ynodes_link(editor: Any, arg1: Any, out_idx: int, to: Any, in_idx: int) -> _rt.Result[None]:
    """Call `yetty_ygui_ynodes_link`."""
    _fn = _rt.cfn("yetty_ygui_ynodes_link", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_int, c_void_p, c_int])
    res = _fn(_rt.handle(editor), _rt.handle(arg0), out_idx, _rt.handle(to), in_idx)
    return _rt.result_from_c(res)

def ynodes_drop_links_for(editor: Any, node: Any) -> _rt.Result[None]:
    """Call `yetty_ygui_ynodes_drop_links_for`."""
    _fn = _rt.cfn("yetty_ygui_ynodes_drop_links_for", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(editor), _rt.handle(node))
    return _rt.result_from_c(res)

def ynodes_on_link_set(editor: Any, cb: Any, userdata: Any) -> _rt.Result[None]:
    """Call `yetty_ygui_ynodes_on_link_set`."""
    _fn = _rt.cfn("yetty_ygui_ynodes_on_link_set", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(editor), _rt.handle(cb), _rt.handle(userdata))
    return _rt.result_from_c(res)

def ynodes_begin_link(editor: Any, arg1: Any, pin: int, output: int, x: float, y: float) -> _rt.Result[None]:
    """Call `yetty_ygui_ynodes_begin_link`."""
    _fn = _rt.cfn("yetty_ygui_ynodes_begin_link", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_int, c_int, c_float, c_float])
    res = _fn(_rt.handle(editor), _rt.handle(arg0), pin, output, x, y)
    return _rt.result_from_c(res)

def ynodes_update_link(editor: Any, x: float, y: float) -> _rt.Result[None]:
    """Call `yetty_ygui_ynodes_update_link`."""
    _fn = _rt.cfn("yetty_ygui_ynodes_update_link", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float])
    res = _fn(_rt.handle(editor), x, y)
    return _rt.result_from_c(res)

def ynodes_end_link(editor: Any, x: float, y: float) -> _rt.Result[None]:
    """Call `yetty_ygui_ynodes_end_link`."""
    _fn = _rt.cfn("yetty_ygui_ynodes_end_link", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float])
    res = _fn(_rt.handle(editor), x, y)
    return _rt.result_from_c(res)

def ynodes_add_node(editor: Any, gx: float, gy: float) -> _rt.Result[Any]:
    """Call `yetty_ygui_ynodes_add_node`."""
    _fn = _rt.cfn("yetty_ygui_ynodes_add_node", _t.yetty_yclass_object_ptr_result, [c_void_p, c_float, c_float])
    res = _fn(_rt.handle(editor), gx, gy)
    return _rt.result_from_c(res)

def ynodes_register_widget(editor: Any, label: str | bytes | None, cls: Any) -> _rt.Result[None]:
    """Call `yetty_ygui_ynodes_register_widget`."""
    _fn = _rt.cfn("yetty_ygui_ynodes_register_widget", _t.yetty_ycore_void_result, [c_void_p, c_char_p, c_void_p])
    res = _fn(_rt.handle(editor), _rt.cstr(label), _rt.handle(cls))
    return _rt.result_from_c(res)

def ynodes_open_canvas_menu(editor: Any, x: float, y: float) -> _rt.Result[None]:
    """Call `yetty_ygui_ynodes_open_canvas_menu`."""
    _fn = _rt.cfn("yetty_ygui_ynodes_open_canvas_menu", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float])
    res = _fn(_rt.handle(editor), x, y)
    return _rt.result_from_c(res)

def ynodes_open_node_menu(editor: Any, node: Any, x: float, y: float) -> _rt.Result[None]:
    """Call `yetty_ygui_ynodes_open_node_menu`."""
    _fn = _rt.cfn("yetty_ygui_ynodes_open_node_menu", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_float, c_float])
    res = _fn(_rt.handle(editor), _rt.handle(node), x, y)
    return _rt.result_from_c(res)

def ynodes_close_menu(editor: Any) -> _rt.Result[None]:
    """Call `yetty_ygui_ynodes_close_menu`."""
    _fn = _rt.cfn("yetty_ygui_ynodes_close_menu", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(editor))
    return _rt.result_from_c(res)

def ynodes_menu_is_open(editor: Any) -> _rt.Result[int]:
    """Call `yetty_ygui_ynodes_menu_is_open`."""
    _fn = _rt.cfn("yetty_ygui_ynodes_menu_is_open", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(editor))
    return _rt.result_from_c(res)

def ypdf_set_file(obj: Any, path: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ygui_ypdf_set_file`."""
    _fn = _rt.cfn("yetty_ygui_ypdf_set_file", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(path))
    return _rt.result_from_c(res)

def yplot_set_source(obj: Any, source: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ygui_yplot_set_source`."""
    _fn = _rt.cfn("yetty_ygui_yplot_set_source", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(source))
    return _rt.result_from_c(res)

def yplot_set_config(obj: Any, cfg: Any) -> _rt.Result[None]:
    """Call `yetty_ygui_yplot_set_config`."""
    _fn = _rt.cfn("yetty_ygui_yplot_set_config", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(cfg))
    return _rt.result_from_c(res)

def yplot_set_buffers(obj: Any, source: str | bytes | None, source_len: int, buffers: Any, buffer_count: int, config: Any) -> _rt.Result[None]:
    """Call `yetty_ygui_yplot_set_buffers`."""
    _fn = _rt.cfn("yetty_ygui_yplot_set_buffers", _t.yetty_ycore_void_result, [c_void_p, c_char_p, c_size_t, c_void_p, c_size_t, c_void_p])
    res = _fn(_rt.handle(obj), _rt.cstr(source), source_len, _rt.handle(buffers), buffer_count, _rt.handle(config))
    return _rt.result_from_c(res)

def yrich_view_set_document(obj: Any, doc: Any, own: int) -> _rt.Result[None]:
    """Call `yetty_ygui_yrich_view_set_document`."""
    _fn = _rt.cfn("yetty_ygui_yrich_view_set_document", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_int])
    res = _fn(_rt.handle(obj), _rt.handle(doc), own)
    return _rt.result_from_c(res)

def yrich_view_invalidate(obj: Any) -> _rt.Result[None]:
    """Call `yetty_ygui_yrich_view_invalidate`."""
    _fn = _rt.cfn("yetty_ygui_yrich_view_invalidate", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def yrich_view_document(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_ygui_yrich_view_document`."""
    _fn = _rt.cfn("yetty_ygui_yrich_view_document", _t.yetty_yclass_object_ptr_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def yrich_view_content_size(obj: Any, w: Any, h: Any) -> _rt.Result[None]:
    """Call `yetty_ygui_yrich_view_content_size`."""
    _fn = _rt.cfn("yetty_ygui_yrich_view_content_size", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(w), _rt.handle(h))
    return _rt.result_from_c(res)

def yrich_view_fit_content(obj: Any) -> _rt.Result[None]:
    """Call `yetty_ygui_yrich_view_fit_content`."""
    _fn = _rt.cfn("yetty_ygui_yrich_view_fit_content", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def yrich_view_feed_key(obj: Any, key: int, mods: int) -> _rt.Result[None]:
    """Call `yetty_ygui_yrich_view_feed_key`."""
    _fn = _rt.cfn("yetty_ygui_yrich_view_feed_key", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_uint32])
    res = _fn(_rt.handle(obj), key, mods)
    return _rt.result_from_c(res)

def yrich_view_feed_text(obj: Any, text: str | bytes | None, len: int) -> _rt.Result[None]:
    """Call `yetty_ygui_yrich_view_feed_text`."""
    _fn = _rt.cfn("yetty_ygui_yrich_view_feed_text", _t.yetty_ycore_void_result, [c_void_p, c_char_p, c_size_t])
    res = _fn(_rt.handle(obj), _rt.cstr(text), len)
    return _rt.result_from_c(res)

def yshadertoy_set_source(obj: Any, src: str | bytes | None, len: int) -> _rt.Result[None]:
    """Call `yetty_ygui_yshadertoy_set_source`."""
    _fn = _rt.cfn("yetty_ygui_yshadertoy_set_source", _t.yetty_ycore_void_result, [c_void_p, c_char_p, c_size_t])
    res = _fn(_rt.handle(obj), _rt.cstr(src), len)
    return _rt.result_from_c(res)

def yvideo_set_bytes(obj: Any, bytes: Any, len: int) -> _rt.Result[None]:
    """Call `yetty_ygui_yvideo_set_bytes`."""
    _fn = _rt.cfn("yetty_ygui_yvideo_set_bytes", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_size_t])
    res = _fn(_rt.handle(obj), _rt.handle(bytes), len)
    return _rt.result_from_c(res)

