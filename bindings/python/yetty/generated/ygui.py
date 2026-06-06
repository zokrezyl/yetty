"""yetty.ygui bindings — GENERATED from model.yaml, do not edit."""
from __future__ import annotations
from ctypes import (c_bool, c_char, c_char_p, c_double, c_float, c_int,
    c_int8, c_int16, c_int32, c_int64, c_long, c_size_t, c_ssize_t,
    c_uint, c_uint8, c_uint16, c_uint32, c_uint64, c_ulong, c_void_p)
from .. import runtime as _rt
from . import _types as _t

class PrimitiveWidget:
    """yclass ygui:primitive_widget"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_primitive_widget_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def widget_emit_body(self, ctx):
        _fn = _rt.cfn("yetty_ygui_widget_emit_body", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, ctx)
        _rt.check(res)
        return None

class Widget:
    """yclass ygui:widget"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_widget_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def constructor(self):
        _fn = _rt.cfn("yetty_ygui_constructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def destructor(self):
        _fn = _rt.cfn("yetty_ygui_destructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def widget_on_press(self, x, y, button):
        _fn = _rt.cfn("yetty_ygui_widget_on_press", _t.yetty_ycore_int_result, [c_void_p, c_void_p, c_float, c_float, c_int])
        res = _fn(None, self._handle, x, y, button)
        _rt.check(res)
        return res.value
    def widget_on_release(self, x, y, button):
        _fn = _rt.cfn("yetty_ygui_widget_on_release", _t.yetty_ycore_int_result, [c_void_p, c_void_p, c_float, c_float, c_int])
        res = _fn(None, self._handle, x, y, button)
        _rt.check(res)
        return res.value
    def widget_on_motion(self, x, y):
        _fn = _rt.cfn("yetty_ygui_widget_on_motion", _t.yetty_ycore_int_result, [c_void_p, c_void_p, c_float, c_float])
        res = _fn(None, self._handle, x, y)
        _rt.check(res)
        return res.value
    def widget_on_scroll(self, x, y, dx, dy):
        _fn = _rt.cfn("yetty_ygui_widget_on_scroll", _t.yetty_ycore_int_result, [c_void_p, c_void_p, c_float, c_float, c_float, c_float])
        res = _fn(None, self._handle, x, y, dx, dy)
        _rt.check(res)
        return res.value
    def widget_paint(self, emit_ctx):
        _fn = _rt.cfn("yetty_ygui_widget_paint", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, emit_ctx)
        _rt.check(res)
        return None
    def widget_emit_container(self, emit_ctx):
        _fn = _rt.cfn("yetty_ygui_widget_emit_container", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, emit_ctx)
        _rt.check(res)
        return None
    def widget_emit_body(self, ctx):
        _fn = _rt.cfn("yetty_ygui_widget_emit_body", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, ctx)
        _rt.check(res)
        return None

class Breadcrumbs:
    """yclass ygui:breadcrumbs"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_breadcrumbs_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def constructor(self):
        _fn = _rt.cfn("yetty_ygui_constructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def destructor(self):
        _fn = _rt.cfn("yetty_ygui_destructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def widget_paint(self, emit_ctx):
        _fn = _rt.cfn("yetty_ygui_widget_paint", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, emit_ctx)
        _rt.check(res)
        return None

class Button:
    """yclass ygui:button"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_button_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def constructor(self):
        _fn = _rt.cfn("yetty_ygui_constructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def destructor(self):
        _fn = _rt.cfn("yetty_ygui_destructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def widget_paint(self, emit_ctx):
        _fn = _rt.cfn("yetty_ygui_widget_paint", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, emit_ctx)
        _rt.check(res)
        return None

class Checkbox:
    """yclass ygui:checkbox"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_checkbox_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def constructor(self):
        _fn = _rt.cfn("yetty_ygui_constructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def destructor(self):
        _fn = _rt.cfn("yetty_ygui_destructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def widget_paint(self, emit_ctx):
        _fn = _rt.cfn("yetty_ygui_widget_paint", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, emit_ctx)
        _rt.check(res)
        return None

class Chip:
    """yclass ygui:chip"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_chip_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def constructor(self):
        _fn = _rt.cfn("yetty_ygui_constructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def destructor(self):
        _fn = _rt.cfn("yetty_ygui_destructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def widget_paint(self, emit_ctx):
        _fn = _rt.cfn("yetty_ygui_widget_paint", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, emit_ctx)
        _rt.check(res)
        return None

class Choicebox:
    """yclass ygui:choicebox"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_choicebox_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def constructor(self):
        _fn = _rt.cfn("yetty_ygui_constructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def destructor(self):
        _fn = _rt.cfn("yetty_ygui_destructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def widget_on_press(self, x, y, button):
        _fn = _rt.cfn("yetty_ygui_widget_on_press", _t.yetty_ycore_int_result, [c_void_p, c_void_p, c_float, c_float, c_int])
        res = _fn(None, self._handle, x, y, button)
        _rt.check(res)
        return res.value
    def widget_paint(self, emit_ctx):
        _fn = _rt.cfn("yetty_ygui_widget_paint", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, emit_ctx)
        _rt.check(res)
        return None

class CollapsingHeader:
    """yclass ygui:collapsing_header"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_collapsing_header_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def constructor(self):
        _fn = _rt.cfn("yetty_ygui_constructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def destructor(self):
        _fn = _rt.cfn("yetty_ygui_destructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def widget_on_press(self, x, y, button):
        _fn = _rt.cfn("yetty_ygui_widget_on_press", _t.yetty_ycore_int_result, [c_void_p, c_void_p, c_float, c_float, c_int])
        res = _fn(None, self._handle, x, y, button)
        _rt.check(res)
        return res.value
    def widget_paint(self, emit_ctx):
        _fn = _rt.cfn("yetty_ygui_widget_paint", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, emit_ctx)
        _rt.check(res)
        return None

class Colorpicker:
    """yclass ygui:colorpicker"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_colorpicker_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def constructor(self):
        _fn = _rt.cfn("yetty_ygui_constructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def widget_paint(self, emit_ctx):
        _fn = _rt.cfn("yetty_ygui_widget_paint", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, emit_ctx)
        _rt.check(res)
        return None

class Combobox:
    """yclass ygui:combobox"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_combobox_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def constructor(self):
        _fn = _rt.cfn("yetty_ygui_constructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def destructor(self):
        _fn = _rt.cfn("yetty_ygui_destructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def widget_paint(self, emit_ctx):
        _fn = _rt.cfn("yetty_ygui_widget_paint", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, emit_ctx)
        _rt.check(res)
        return None

class Datepicker:
    """yclass ygui:datepicker"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_datepicker_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def constructor(self):
        _fn = _rt.cfn("yetty_ygui_constructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def widget_paint(self, emit_ctx):
        _fn = _rt.cfn("yetty_ygui_widget_paint", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, emit_ctx)
        _rt.check(res)
        return None
    def widget_on_press(self, x, y, button):
        _fn = _rt.cfn("yetty_ygui_widget_on_press", _t.yetty_ycore_int_result, [c_void_p, c_void_p, c_float, c_float, c_int])
        res = _fn(None, self._handle, x, y, button)
        _rt.check(res)
        return res.value

class Dialog:
    """yclass ygui:dialog"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_dialog_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def constructor(self):
        _fn = _rt.cfn("yetty_ygui_constructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def destructor(self):
        _fn = _rt.cfn("yetty_ygui_destructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def widget_paint(self, emit_ctx):
        _fn = _rt.cfn("yetty_ygui_widget_paint", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, emit_ctx)
        _rt.check(res)
        return None

class Dropdown:
    """yclass ygui:dropdown"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_dropdown_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def constructor(self):
        _fn = _rt.cfn("yetty_ygui_constructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def destructor(self):
        _fn = _rt.cfn("yetty_ygui_destructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def widget_paint(self, emit_ctx):
        _fn = _rt.cfn("yetty_ygui_widget_paint", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, emit_ctx)
        _rt.check(res)
        return None

class Filepicker:
    """yclass ygui:filepicker"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_filepicker_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def constructor(self):
        _fn = _rt.cfn("yetty_ygui_constructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def destructor(self):
        _fn = _rt.cfn("yetty_ygui_destructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def widget_paint(self, emit_ctx):
        _fn = _rt.cfn("yetty_ygui_widget_paint", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, emit_ctx)
        _rt.check(res)
        return None
    def widget_on_motion(self, x, y):
        _fn = _rt.cfn("yetty_ygui_widget_on_motion", _t.yetty_ycore_int_result, [c_void_p, c_void_p, c_float, c_float])
        res = _fn(None, self._handle, x, y)
        _rt.check(res)
        return res.value
    def widget_on_scroll(self, x, y, dx, dy):
        _fn = _rt.cfn("yetty_ygui_widget_on_scroll", _t.yetty_ycore_int_result, [c_void_p, c_void_p, c_float, c_float, c_float, c_float])
        res = _fn(None, self._handle, x, y, dx, dy)
        _rt.check(res)
        return res.value
    def widget_on_press(self, x, y, button):
        _fn = _rt.cfn("yetty_ygui_widget_on_press", _t.yetty_ycore_int_result, [c_void_p, c_void_p, c_float, c_float, c_int])
        res = _fn(None, self._handle, x, y, button)
        _rt.check(res)
        return res.value

class Hbox:
    """yclass ygui:hbox"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_hbox_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def constructor(self):
        _fn = _rt.cfn("yetty_ygui_constructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None

class Label:
    """yclass ygui:label"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_label_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def constructor(self):
        _fn = _rt.cfn("yetty_ygui_constructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def destructor(self):
        _fn = _rt.cfn("yetty_ygui_destructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def widget_paint(self, emit_ctx):
        _fn = _rt.cfn("yetty_ygui_widget_paint", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, emit_ctx)
        _rt.check(res)
        return None

class List:
    """yclass ygui:list"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_list_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def constructor(self):
        _fn = _rt.cfn("yetty_ygui_constructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def destructor(self):
        _fn = _rt.cfn("yetty_ygui_destructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def widget_on_press(self, x, y, button):
        _fn = _rt.cfn("yetty_ygui_widget_on_press", _t.yetty_ycore_int_result, [c_void_p, c_void_p, c_float, c_float, c_int])
        res = _fn(None, self._handle, x, y, button)
        _rt.check(res)
        return res.value
    def widget_paint(self, emit_ctx):
        _fn = _rt.cfn("yetty_ygui_widget_paint", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, emit_ctx)
        _rt.check(res)
        return None

class Menubar:
    """yclass ygui:menubar"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_menubar_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def constructor(self):
        _fn = _rt.cfn("yetty_ygui_constructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None

class Panel:
    """yclass ygui:panel"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_panel_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def constructor(self):
        _fn = _rt.cfn("yetty_ygui_constructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def widget_paint(self, emit_ctx):
        _fn = _rt.cfn("yetty_ygui_widget_paint", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, emit_ctx)
        _rt.check(res)
        return None

class PopupMenu:
    """yclass ygui:popup_menu"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_popup_menu_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def constructor(self):
        _fn = _rt.cfn("yetty_ygui_constructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def destructor(self):
        _fn = _rt.cfn("yetty_ygui_destructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def widget_paint(self, emit_ctx):
        _fn = _rt.cfn("yetty_ygui_widget_paint", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, emit_ctx)
        _rt.check(res)
        return None
    def widget_on_press(self, x, y, button):
        _fn = _rt.cfn("yetty_ygui_widget_on_press", _t.yetty_ycore_int_result, [c_void_p, c_void_p, c_float, c_float, c_int])
        res = _fn(None, self._handle, x, y, button)
        _rt.check(res)
        return res.value
    def widget_on_motion(self, x, y):
        _fn = _rt.cfn("yetty_ygui_widget_on_motion", _t.yetty_ycore_int_result, [c_void_p, c_void_p, c_float, c_float])
        res = _fn(None, self._handle, x, y)
        _rt.check(res)
        return res.value

class Progress:
    """yclass ygui:progress"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_progress_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def constructor(self):
        _fn = _rt.cfn("yetty_ygui_constructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def widget_paint(self, emit_ctx):
        _fn = _rt.cfn("yetty_ygui_widget_paint", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, emit_ctx)
        _rt.check(res)
        return None

class Radio:
    """yclass ygui:radio"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_radio_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def constructor(self):
        _fn = _rt.cfn("yetty_ygui_constructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def destructor(self):
        _fn = _rt.cfn("yetty_ygui_destructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def widget_paint(self, emit_ctx):
        _fn = _rt.cfn("yetty_ygui_widget_paint", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, emit_ctx)
        _rt.check(res)
        return None

class Rich:
    """yclass ygui:rich"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_rich_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def constructor(self):
        _fn = _rt.cfn("yetty_ygui_constructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def destructor(self):
        _fn = _rt.cfn("yetty_ygui_destructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def widget_paint(self, emit_ctx):
        _fn = _rt.cfn("yetty_ygui_widget_paint", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, emit_ctx)
        _rt.check(res)
        return None

class Scrollarea:
    """yclass ygui:scrollarea"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_scrollarea_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def widget_on_scroll(self, x, y, dx, dy):
        _fn = _rt.cfn("yetty_ygui_widget_on_scroll", _t.yetty_ycore_int_result, [c_void_p, c_void_p, c_float, c_float, c_float, c_float])
        res = _fn(None, self._handle, x, y, dx, dy)
        _rt.check(res)
        return res.value
    def constructor(self):
        _fn = _rt.cfn("yetty_ygui_constructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def widget_paint(self, emit_ctx):
        _fn = _rt.cfn("yetty_ygui_widget_paint", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, emit_ctx)
        _rt.check(res)
        return None

class Selectable:
    """yclass ygui:selectable"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_selectable_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def constructor(self):
        _fn = _rt.cfn("yetty_ygui_constructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def destructor(self):
        _fn = _rt.cfn("yetty_ygui_destructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def widget_paint(self, emit_ctx):
        _fn = _rt.cfn("yetty_ygui_widget_paint", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, emit_ctx)
        _rt.check(res)
        return None

class Separator:
    """yclass ygui:separator"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_separator_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def widget_paint(self, emit_ctx):
        _fn = _rt.cfn("yetty_ygui_widget_paint", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, emit_ctx)
        _rt.check(res)
        return None

class Slider:
    """yclass ygui:slider"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_slider_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def constructor(self):
        _fn = _rt.cfn("yetty_ygui_constructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def widget_paint(self, emit_ctx):
        _fn = _rt.cfn("yetty_ygui_widget_paint", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, emit_ctx)
        _rt.check(res)
        return None
    def widget_on_press(self, x, y, button):
        _fn = _rt.cfn("yetty_ygui_widget_on_press", _t.yetty_ycore_int_result, [c_void_p, c_void_p, c_float, c_float, c_int])
        res = _fn(None, self._handle, x, y, button)
        _rt.check(res)
        return res.value
    def widget_on_motion(self, x, y):
        _fn = _rt.cfn("yetty_ygui_widget_on_motion", _t.yetty_ycore_int_result, [c_void_p, c_void_p, c_float, c_float])
        res = _fn(None, self._handle, x, y)
        _rt.check(res)
        return res.value

class Spinner:
    """yclass ygui:spinner"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_spinner_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def constructor(self):
        _fn = _rt.cfn("yetty_ygui_constructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def widget_on_press(self, x, y, button):
        _fn = _rt.cfn("yetty_ygui_widget_on_press", _t.yetty_ycore_int_result, [c_void_p, c_void_p, c_float, c_float, c_int])
        res = _fn(None, self._handle, x, y, button)
        _rt.check(res)
        return res.value
    def widget_paint(self, emit_ctx):
        _fn = _rt.cfn("yetty_ygui_widget_paint", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, emit_ctx)
        _rt.check(res)
        return None

class Splitter:
    """yclass ygui:splitter"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_splitter_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def widget_paint(self, emit_ctx):
        _fn = _rt.cfn("yetty_ygui_widget_paint", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, emit_ctx)
        _rt.check(res)
        return None
    def widget_on_press(self, x, y, button):
        _fn = _rt.cfn("yetty_ygui_widget_on_press", _t.yetty_ycore_int_result, [c_void_p, c_void_p, c_float, c_float, c_int])
        res = _fn(None, self._handle, x, y, button)
        _rt.check(res)
        return res.value
    def widget_on_motion(self, x, y):
        _fn = _rt.cfn("yetty_ygui_widget_on_motion", _t.yetty_ycore_int_result, [c_void_p, c_void_p, c_float, c_float])
        res = _fn(None, self._handle, x, y)
        _rt.check(res)
        return res.value

class Statusbar:
    """yclass ygui:statusbar"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_statusbar_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def constructor(self):
        _fn = _rt.cfn("yetty_ygui_constructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def destructor(self):
        _fn = _rt.cfn("yetty_ygui_destructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def widget_paint(self, emit_ctx):
        _fn = _rt.cfn("yetty_ygui_widget_paint", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, emit_ctx)
        _rt.check(res)
        return None

class Stepper:
    """yclass ygui:stepper"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_stepper_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def constructor(self):
        _fn = _rt.cfn("yetty_ygui_constructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def destructor(self):
        _fn = _rt.cfn("yetty_ygui_destructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def widget_paint(self, emit_ctx):
        _fn = _rt.cfn("yetty_ygui_widget_paint", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, emit_ctx)
        _rt.check(res)
        return None

class Tabbar:
    """yclass ygui:tabbar"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_tabbar_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def widget_on_press(self, x, y, button):
        _fn = _rt.cfn("yetty_ygui_widget_on_press", _t.yetty_ycore_int_result, [c_void_p, c_void_p, c_float, c_float, c_int])
        res = _fn(None, self._handle, x, y, button)
        _rt.check(res)
        return res.value
    def widget_on_release(self, x, y, button):
        _fn = _rt.cfn("yetty_ygui_widget_on_release", _t.yetty_ycore_int_result, [c_void_p, c_void_p, c_float, c_float, c_int])
        res = _fn(None, self._handle, x, y, button)
        _rt.check(res)
        return res.value
    def constructor(self):
        _fn = _rt.cfn("yetty_ygui_constructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def widget_paint(self, emit_ctx):
        _fn = _rt.cfn("yetty_ygui_widget_paint", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, emit_ctx)
        _rt.check(res)
        return None

class Table:
    """yclass ygui:table"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_table_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def constructor(self):
        _fn = _rt.cfn("yetty_ygui_constructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def destructor(self):
        _fn = _rt.cfn("yetty_ygui_destructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def widget_paint(self, emit_ctx):
        _fn = _rt.cfn("yetty_ygui_widget_paint", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, emit_ctx)
        _rt.check(res)
        return None

class Textarea:
    """yclass ygui:textarea"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_textarea_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def constructor(self):
        _fn = _rt.cfn("yetty_ygui_constructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def destructor(self):
        _fn = _rt.cfn("yetty_ygui_destructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def widget_paint(self, emit_ctx):
        _fn = _rt.cfn("yetty_ygui_widget_paint", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, emit_ctx)
        _rt.check(res)
        return None

class Textinput:
    """yclass ygui:textinput"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_textinput_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def constructor(self):
        _fn = _rt.cfn("yetty_ygui_constructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def destructor(self):
        _fn = _rt.cfn("yetty_ygui_destructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def widget_paint(self, emit_ctx):
        _fn = _rt.cfn("yetty_ygui_widget_paint", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, emit_ctx)
        _rt.check(res)
        return None

class Toggle:
    """yclass ygui:toggle"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_toggle_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def constructor(self):
        _fn = _rt.cfn("yetty_ygui_constructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def destructor(self):
        _fn = _rt.cfn("yetty_ygui_destructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def widget_paint(self, emit_ctx):
        _fn = _rt.cfn("yetty_ygui_widget_paint", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, emit_ctx)
        _rt.check(res)
        return None

class Tooltip:
    """yclass ygui:tooltip"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_tooltip_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def constructor(self):
        _fn = _rt.cfn("yetty_ygui_constructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def destructor(self):
        _fn = _rt.cfn("yetty_ygui_destructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def widget_paint(self, emit_ctx):
        _fn = _rt.cfn("yetty_ygui_widget_paint", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, emit_ctx)
        _rt.check(res)
        return None

class TreeNode:
    """yclass ygui:tree_node"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_tree_node_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def constructor(self):
        _fn = _rt.cfn("yetty_ygui_constructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def destructor(self):
        _fn = _rt.cfn("yetty_ygui_destructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def widget_on_press(self, x, y, button):
        _fn = _rt.cfn("yetty_ygui_widget_on_press", _t.yetty_ycore_int_result, [c_void_p, c_void_p, c_float, c_float, c_int])
        res = _fn(None, self._handle, x, y, button)
        _rt.check(res)
        return res.value
    def widget_paint(self, emit_ctx):
        _fn = _rt.cfn("yetty_ygui_widget_paint", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, emit_ctx)
        _rt.check(res)
        return None

class Vbox:
    """yclass ygui:vbox"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_vbox_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def constructor(self):
        _fn = _rt.cfn("yetty_ygui_constructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None

class Window:
    """yclass ygui:window"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_window_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def constructor(self):
        _fn = _rt.cfn("yetty_ygui_constructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def destructor(self):
        _fn = _rt.cfn("yetty_ygui_destructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def widget_paint(self, emit_ctx):
        _fn = _rt.cfn("yetty_ygui_widget_paint", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, emit_ctx)
        _rt.check(res)
        return None
    def widget_on_press(self, x, y, button):
        _fn = _rt.cfn("yetty_ygui_widget_on_press", _t.yetty_ycore_int_result, [c_void_p, c_void_p, c_float, c_float, c_int])
        res = _fn(None, self._handle, x, y, button)
        _rt.check(res)
        return res.value
    def widget_on_motion(self, x, y):
        _fn = _rt.cfn("yetty_ygui_widget_on_motion", _t.yetty_ycore_int_result, [c_void_p, c_void_p, c_float, c_float])
        res = _fn(None, self._handle, x, y)
        _rt.check(res)
        return res.value
    def widget_on_release(self, x, y, button):
        _fn = _rt.cfn("yetty_ygui_widget_on_release", _t.yetty_ycore_int_result, [c_void_p, c_void_p, c_float, c_float, c_int])
        res = _fn(None, self._handle, x, y, button)
        _rt.check(res)
        return res.value

class Ybrowser:
    """yclass ygui:ybrowser"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_ybrowser_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def constructor(self):
        _fn = _rt.cfn("yetty_ygui_constructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def destructor(self):
        _fn = _rt.cfn("yetty_ygui_destructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def widget_emit_body(self, ctx):
        _fn = _rt.cfn("yetty_ygui_widget_emit_body", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, ctx)
        _rt.check(res)
        return None

class Ydiagram:
    """yclass ygui:ydiagram"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_ydiagram_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def constructor(self):
        _fn = _rt.cfn("yetty_ygui_constructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def destructor(self):
        _fn = _rt.cfn("yetty_ygui_destructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None

class YdrawEmbed:
    """yclass ygui:ydraw_embed"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_ydraw_embed_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def constructor(self):
        _fn = _rt.cfn("yetty_ygui_constructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def destructor(self):
        _fn = _rt.cfn("yetty_ygui_destructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def widget_paint(self, emit_ctx):
        _fn = _rt.cfn("yetty_ygui_widget_paint", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, emit_ctx)
        _rt.check(res)
        return None

class Yimage:
    """yclass ygui:yimage"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_yimage_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def constructor(self):
        _fn = _rt.cfn("yetty_ygui_constructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def destructor(self):
        _fn = _rt.cfn("yetty_ygui_destructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def widget_emit_container(self, emit_ctx):
        _fn = _rt.cfn("yetty_ygui_widget_emit_container", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, emit_ctx)
        _rt.check(res)
        return None
    def widget_emit_body(self, ctx):
        _fn = _rt.cfn("yetty_ygui_widget_emit_body", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, ctx)
        _rt.check(res)
        return None

class Yjungle:
    """yclass ygui:yjungle"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_yjungle_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def constructor(self):
        _fn = _rt.cfn("yetty_ygui_constructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def destructor(self):
        _fn = _rt.cfn("yetty_ygui_destructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def widget_emit_body(self, ctx):
        _fn = _rt.cfn("yetty_ygui_widget_emit_body", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, ctx)
        _rt.check(res)
        return None

class Ymarkdown:
    """yclass ygui:ymarkdown"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_ymarkdown_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def constructor(self):
        _fn = _rt.cfn("yetty_ygui_constructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def destructor(self):
        _fn = _rt.cfn("yetty_ygui_destructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def widget_emit_body(self, ctx):
        _fn = _rt.cfn("yetty_ygui_widget_emit_body", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, ctx)
        _rt.check(res)
        return None

class Ymaze:
    """yclass ygui:ymaze"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_ymaze_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def constructor(self):
        _fn = _rt.cfn("yetty_ygui_constructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def destructor(self):
        _fn = _rt.cfn("yetty_ygui_destructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def widget_emit_body(self, ctx):
        _fn = _rt.cfn("yetty_ygui_widget_emit_body", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, ctx)
        _rt.check(res)
        return None

class Ynode:
    """yclass ygui:ynode"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_ynode_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def constructor(self):
        _fn = _rt.cfn("yetty_ygui_constructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def destructor(self):
        _fn = _rt.cfn("yetty_ygui_destructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def widget_paint(self, emit_ctx):
        _fn = _rt.cfn("yetty_ygui_widget_paint", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, emit_ctx)
        _rt.check(res)
        return None
    def widget_on_press(self, x, y, button):
        _fn = _rt.cfn("yetty_ygui_widget_on_press", _t.yetty_ycore_int_result, [c_void_p, c_void_p, c_float, c_float, c_int])
        res = _fn(None, self._handle, x, y, button)
        _rt.check(res)
        return res.value
    def widget_on_motion(self, x, y):
        _fn = _rt.cfn("yetty_ygui_widget_on_motion", _t.yetty_ycore_int_result, [c_void_p, c_void_p, c_float, c_float])
        res = _fn(None, self._handle, x, y)
        _rt.check(res)
        return res.value
    def widget_on_release(self, x, y, button):
        _fn = _rt.cfn("yetty_ygui_widget_on_release", _t.yetty_ycore_int_result, [c_void_p, c_void_p, c_float, c_float, c_int])
        res = _fn(None, self._handle, x, y, button)
        _rt.check(res)
        return res.value

class Ynodes:
    """yclass ygui:ynodes"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_ynodes_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def constructor(self):
        _fn = _rt.cfn("yetty_ygui_constructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def destructor(self):
        _fn = _rt.cfn("yetty_ygui_destructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def widget_paint(self, emit_ctx):
        _fn = _rt.cfn("yetty_ygui_widget_paint", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, emit_ctx)
        _rt.check(res)
        return None
    def widget_on_press(self, x, y, button):
        _fn = _rt.cfn("yetty_ygui_widget_on_press", _t.yetty_ycore_int_result, [c_void_p, c_void_p, c_float, c_float, c_int])
        res = _fn(None, self._handle, x, y, button)
        _rt.check(res)
        return res.value
    def widget_on_motion(self, x, y):
        _fn = _rt.cfn("yetty_ygui_widget_on_motion", _t.yetty_ycore_int_result, [c_void_p, c_void_p, c_float, c_float])
        res = _fn(None, self._handle, x, y)
        _rt.check(res)
        return res.value
    def widget_on_release(self, x, y, button):
        _fn = _rt.cfn("yetty_ygui_widget_on_release", _t.yetty_ycore_int_result, [c_void_p, c_void_p, c_float, c_float, c_int])
        res = _fn(None, self._handle, x, y, button)
        _rt.check(res)
        return res.value
    def widget_on_scroll(self, x, y, dx, dy):
        _fn = _rt.cfn("yetty_ygui_widget_on_scroll", _t.yetty_ycore_int_result, [c_void_p, c_void_p, c_float, c_float, c_float, c_float])
        res = _fn(None, self._handle, x, y, dx, dy)
        _rt.check(res)
        return res.value

class Ypdf:
    """yclass ygui:ypdf"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_ypdf_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle

class Yplot:
    """yclass ygui:yplot"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_yplot_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def constructor(self):
        _fn = _rt.cfn("yetty_ygui_constructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def destructor(self):
        _fn = _rt.cfn("yetty_ygui_destructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def widget_emit_container(self, emit_ctx):
        _fn = _rt.cfn("yetty_ygui_widget_emit_container", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, emit_ctx)
        _rt.check(res)
        return None
    def widget_emit_body(self, ctx):
        _fn = _rt.cfn("yetty_ygui_widget_emit_body", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, ctx)
        _rt.check(res)
        return None

class YrichView:
    """yclass ygui:yrich_view"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_yrich_view_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def constructor(self):
        _fn = _rt.cfn("yetty_ygui_constructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def destructor(self):
        _fn = _rt.cfn("yetty_ygui_destructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def widget_emit_body(self, ctx):
        _fn = _rt.cfn("yetty_ygui_widget_emit_body", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, ctx)
        _rt.check(res)
        return None
    def widget_on_press(self, x, y, button):
        _fn = _rt.cfn("yetty_ygui_widget_on_press", _t.yetty_ycore_int_result, [c_void_p, c_void_p, c_float, c_float, c_int])
        res = _fn(None, self._handle, x, y, button)
        _rt.check(res)
        return res.value
    def widget_on_release(self, x, y, button):
        _fn = _rt.cfn("yetty_ygui_widget_on_release", _t.yetty_ycore_int_result, [c_void_p, c_void_p, c_float, c_float, c_int])
        res = _fn(None, self._handle, x, y, button)
        _rt.check(res)
        return res.value
    def widget_on_motion(self, x, y):
        _fn = _rt.cfn("yetty_ygui_widget_on_motion", _t.yetty_ycore_int_result, [c_void_p, c_void_p, c_float, c_float])
        res = _fn(None, self._handle, x, y)
        _rt.check(res)
        return res.value

class Yshadertoy:
    """yclass ygui:yshadertoy"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_yshadertoy_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def constructor(self):
        _fn = _rt.cfn("yetty_ygui_constructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def destructor(self):
        _fn = _rt.cfn("yetty_ygui_destructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def widget_emit_container(self, emit_ctx):
        _fn = _rt.cfn("yetty_ygui_widget_emit_container", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, emit_ctx)
        _rt.check(res)
        return None
    def widget_emit_body(self, ctx):
        _fn = _rt.cfn("yetty_ygui_widget_emit_body", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, ctx)
        _rt.check(res)
        return None

class Yvideo:
    """yclass ygui:yvideo"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_yvideo_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def constructor(self):
        _fn = _rt.cfn("yetty_ygui_constructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def destructor(self):
        _fn = _rt.cfn("yetty_ygui_destructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def widget_emit_container(self, emit_ctx):
        _fn = _rt.cfn("yetty_ygui_widget_emit_container", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, emit_ctx)
        _rt.check(res)
        return None
    def widget_emit_body(self, ctx):
        _fn = _rt.cfn("yetty_ygui_widget_emit_body", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, ctx)
        _rt.check(res)
        return None

class Yzoo:
    """yclass ygui:yzoo"""
    def __init__(self, _handle=None):
        if _handle is None:
            _fn = _rt.cfn("yetty_ygui_yzoo_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _fn(None)
            _rt.check(res)
            _handle = res.value
        self._handle = _handle
    def constructor(self):
        _fn = _rt.cfn("yetty_ygui_constructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def destructor(self):
        _fn = _rt.cfn("yetty_ygui_destructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        _rt.check(res)
        return None
    def widget_emit_body(self, ctx):
        _fn = _rt.cfn("yetty_ygui_widget_emit_body", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, ctx)
        _rt.check(res)
        return None

