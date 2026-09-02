"""yetty.ygui2 bindings — GENERATED from model.yaml, do not edit."""
from __future__ import annotations
from ctypes import (c_bool, c_char, c_char_p, c_double, c_float, c_int,
    c_int8, c_int16, c_int32, c_int64, c_long, c_size_t, c_ssize_t,
    c_uint, c_uint8, c_uint16, c_uint32, c_uint64, c_ulong, c_void_p)
from typing import Any, ClassVar
from .. import runtime as _rt
from . import _types as _t

class Framework(_rt.YClass):
    """yclass ygui2:framework"""
    __yclass_domain__: ClassVar[str] = 'ygui2'
    __yclass_name__: ClassVar[str] = 'framework'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui2_framework_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_ygui2_framework_make", _t.yetty_yclass_object_ptr_result, [])
        res = _rt.result_from_c(_fn())
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._owned = True
        try:
            self._apply_kwargs(kwargs)
        except BaseException:
            self.destroy()
            raise
    @classmethod
    def create(cls, **kwargs: Any) -> 'Framework':
        return cls(**kwargs)
    def destroy(self) -> None:
        """Dispose via `yetty_ygui2_framework_dispose`; idempotent."""
        if self._handle is None:
            return
        handle, self._handle = self._handle, None
        _fn = _rt.cfn("yetty_ygui2_framework_dispose", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    close = destroy

class Widget(_rt.YClass):
    """yclass ygui2:widget"""
    __yclass_domain__: ClassVar[str] = 'ygui2'
    __yclass_name__: ClassVar[str] = 'widget'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui2_widget_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        raise _rt.YettyError("ygui2:widget is factory-owned — create it via the module's own factory (framework_root_create / widget_add / framework_overlay_add), or wrap an existing handle with _handle=...")
    @classmethod
    def create(cls, **kwargs: Any) -> 'Widget':
        return cls(**kwargs)
    def destroy(self) -> None:
        raise _rt.YettyError("ygui2:widget is framework-owned — it dies with its framework; detach it with widget_remove, never a raw destroy")
    def constructor(self) -> None:
        """Call `yetty_ygui2_constructor`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ygui2_constructor", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def destructor(self) -> None:
        """Call `yetty_ygui2_destructor`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ygui2_destructor", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def widget_paint(self, list: Any) -> None:
        """Call `yetty_ygui2_widget_paint`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ygui2_widget_paint", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _rt.result_from_c(_fn(self._handle, _rt.handle(list)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def widget_paint_retained(self, list: Any) -> None:
        """Call `yetty_ygui2_widget_paint_retained`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ygui2_widget_paint_retained", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _rt.result_from_c(_fn(self._handle, _rt.handle(list)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def widget_emit_geometry(self, list: Any) -> None:
        """Call `yetty_ygui2_widget_emit_geometry`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ygui2_widget_emit_geometry", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _rt.result_from_c(_fn(self._handle, _rt.handle(list)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def widget_on_press(self, local_x: float, local_y: float, button: int, mods: int) -> int:
        """Call `yetty_ygui2_widget_on_press`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ygui2_widget_on_press", _t.yetty_ycore_int_result, [c_void_p, c_float, c_float, c_int, c_int])
        res = _rt.result_from_c(_fn(self._handle, local_x, local_y, button, mods))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def widget_on_release(self, local_x: float, local_y: float, button: int, mods: int) -> int:
        """Call `yetty_ygui2_widget_on_release`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ygui2_widget_on_release", _t.yetty_ycore_int_result, [c_void_p, c_float, c_float, c_int, c_int])
        res = _rt.result_from_c(_fn(self._handle, local_x, local_y, button, mods))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def widget_on_motion(self, local_x: float, local_y: float, buttons_held: int) -> int:
        """Call `yetty_ygui2_widget_on_motion`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ygui2_widget_on_motion", _t.yetty_ycore_int_result, [c_void_p, c_float, c_float, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, local_x, local_y, buttons_held))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def widget_on_scroll(self, local_x: float, local_y: float, wheel_dy: float) -> int:
        """Call `yetty_ygui2_widget_on_scroll`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ygui2_widget_on_scroll", _t.yetty_ycore_int_result, [c_void_p, c_float, c_float, c_float])
        res = _rt.result_from_c(_fn(self._handle, local_x, local_y, wheel_dy))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def widget_on_key(self, key: int, mods: int) -> int:
        """Call `yetty_ygui2_widget_on_key`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ygui2_widget_on_key", _t.yetty_ycore_int_result, [c_void_p, c_uint32, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, key, mods))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def widget_cleanup(self) -> None:
        """Call `yetty_ygui2_widget_cleanup`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ygui2_widget_cleanup", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value

class Button(Widget):
    """yclass ygui2:button"""
    __yclass_domain__: ClassVar[str] = 'ygui2'
    __yclass_name__: ClassVar[str] = 'button'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui2_button_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        raise _rt.YettyError("ygui2:button is factory-owned — create it via the module's own factory (framework_root_create / widget_add / framework_overlay_add), or wrap an existing handle with _handle=...")
    @classmethod
    def create(cls, **kwargs: Any) -> 'Button':
        return cls(**kwargs)
    def destroy(self) -> None:
        raise _rt.YettyError("ygui2:button is framework-owned — it dies with its framework; detach it with widget_remove, never a raw destroy")

class Checkbox(Widget):
    """yclass ygui2:checkbox"""
    __yclass_domain__: ClassVar[str] = 'ygui2'
    __yclass_name__: ClassVar[str] = 'checkbox'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui2_checkbox_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        raise _rt.YettyError("ygui2:checkbox is factory-owned — create it via the module's own factory (framework_root_create / widget_add / framework_overlay_add), or wrap an existing handle with _handle=...")
    @classmethod
    def create(cls, **kwargs: Any) -> 'Checkbox':
        return cls(**kwargs)
    def destroy(self) -> None:
        raise _rt.YettyError("ygui2:checkbox is framework-owned — it dies with its framework; detach it with widget_remove, never a raw destroy")

class Chip(Widget):
    """yclass ygui2:chip"""
    __yclass_domain__: ClassVar[str] = 'ygui2'
    __yclass_name__: ClassVar[str] = 'chip'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui2_chip_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        raise _rt.YettyError("ygui2:chip is factory-owned — create it via the module's own factory (framework_root_create / widget_add / framework_overlay_add), or wrap an existing handle with _handle=...")
    @classmethod
    def create(cls, **kwargs: Any) -> 'Chip':
        return cls(**kwargs)
    def destroy(self) -> None:
        raise _rt.YettyError("ygui2:chip is framework-owned — it dies with its framework; detach it with widget_remove, never a raw destroy")

class ComplexHost(Widget):
    """yclass ygui2:complex_host"""
    __yclass_domain__: ClassVar[str] = 'ygui2'
    __yclass_name__: ClassVar[str] = 'complex_host'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui2_complex_host_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        raise _rt.YettyError("ygui2:complex_host is factory-owned — create it via the module's own factory (framework_root_create / widget_add / framework_overlay_add), or wrap an existing handle with _handle=...")
    @classmethod
    def create(cls, **kwargs: Any) -> 'ComplexHost':
        return cls(**kwargs)
    def destroy(self) -> None:
        raise _rt.YettyError("ygui2:complex_host is framework-owned — it dies with its framework; detach it with widget_remove, never a raw destroy")

class Dialog(Widget):
    """yclass ygui2:dialog"""
    __yclass_domain__: ClassVar[str] = 'ygui2'
    __yclass_name__: ClassVar[str] = 'dialog'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui2_dialog_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        raise _rt.YettyError("ygui2:dialog is factory-owned — create it via the module's own factory (framework_root_create / widget_add / framework_overlay_add), or wrap an existing handle with _handle=...")
    @classmethod
    def create(cls, **kwargs: Any) -> 'Dialog':
        return cls(**kwargs)
    def destroy(self) -> None:
        raise _rt.YettyError("ygui2:dialog is framework-owned — it dies with its framework; detach it with widget_remove, never a raw destroy")

class Dropdown(Widget):
    """yclass ygui2:dropdown"""
    __yclass_domain__: ClassVar[str] = 'ygui2'
    __yclass_name__: ClassVar[str] = 'dropdown'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui2_dropdown_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        raise _rt.YettyError("ygui2:dropdown is factory-owned — create it via the module's own factory (framework_root_create / widget_add / framework_overlay_add), or wrap an existing handle with _handle=...")
    @classmethod
    def create(cls, **kwargs: Any) -> 'Dropdown':
        return cls(**kwargs)
    def destroy(self) -> None:
        raise _rt.YettyError("ygui2:dropdown is framework-owned — it dies with its framework; detach it with widget_remove, never a raw destroy")

class Label(Widget):
    """yclass ygui2:label"""
    __yclass_domain__: ClassVar[str] = 'ygui2'
    __yclass_name__: ClassVar[str] = 'label'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui2_label_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        raise _rt.YettyError("ygui2:label is factory-owned — create it via the module's own factory (framework_root_create / widget_add / framework_overlay_add), or wrap an existing handle with _handle=...")
    @classmethod
    def create(cls, **kwargs: Any) -> 'Label':
        return cls(**kwargs)
    def destroy(self) -> None:
        raise _rt.YettyError("ygui2:label is framework-owned — it dies with its framework; detach it with widget_remove, never a raw destroy")

class Panel(Widget):
    """yclass ygui2:panel"""
    __yclass_domain__: ClassVar[str] = 'ygui2'
    __yclass_name__: ClassVar[str] = 'panel'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui2_panel_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        raise _rt.YettyError("ygui2:panel is factory-owned — create it via the module's own factory (framework_root_create / widget_add / framework_overlay_add), or wrap an existing handle with _handle=...")
    @classmethod
    def create(cls, **kwargs: Any) -> 'Panel':
        return cls(**kwargs)
    def destroy(self) -> None:
        raise _rt.YettyError("ygui2:panel is framework-owned — it dies with its framework; detach it with widget_remove, never a raw destroy")

class Plot(Widget):
    """yclass ygui2:plot"""
    __yclass_domain__: ClassVar[str] = 'ygui2'
    __yclass_name__: ClassVar[str] = 'plot'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui2_plot_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        raise _rt.YettyError("ygui2:plot is factory-owned — create it via the module's own factory (framework_root_create / widget_add / framework_overlay_add), or wrap an existing handle with _handle=...")
    @classmethod
    def create(cls, **kwargs: Any) -> 'Plot':
        return cls(**kwargs)
    def destroy(self) -> None:
        raise _rt.YettyError("ygui2:plot is framework-owned — it dies with its framework; detach it with widget_remove, never a raw destroy")

class PopupMenu(Widget):
    """yclass ygui2:popup_menu"""
    __yclass_domain__: ClassVar[str] = 'ygui2'
    __yclass_name__: ClassVar[str] = 'popup_menu'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui2_popup_menu_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        raise _rt.YettyError("ygui2:popup_menu is factory-owned — create it via the module's own factory (framework_root_create / widget_add / framework_overlay_add), or wrap an existing handle with _handle=...")
    @classmethod
    def create(cls, **kwargs: Any) -> 'PopupMenu':
        return cls(**kwargs)
    def destroy(self) -> None:
        raise _rt.YettyError("ygui2:popup_menu is framework-owned — it dies with its framework; detach it with widget_remove, never a raw destroy")

class Progress(Widget):
    """yclass ygui2:progress"""
    __yclass_domain__: ClassVar[str] = 'ygui2'
    __yclass_name__: ClassVar[str] = 'progress'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui2_progress_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        raise _rt.YettyError("ygui2:progress is factory-owned — create it via the module's own factory (framework_root_create / widget_add / framework_overlay_add), or wrap an existing handle with _handle=...")
    @classmethod
    def create(cls, **kwargs: Any) -> 'Progress':
        return cls(**kwargs)
    def destroy(self) -> None:
        raise _rt.YettyError("ygui2:progress is framework-owned — it dies with its framework; detach it with widget_remove, never a raw destroy")

class Radio(Widget):
    """yclass ygui2:radio"""
    __yclass_domain__: ClassVar[str] = 'ygui2'
    __yclass_name__: ClassVar[str] = 'radio'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui2_radio_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        raise _rt.YettyError("ygui2:radio is factory-owned — create it via the module's own factory (framework_root_create / widget_add / framework_overlay_add), or wrap an existing handle with _handle=...")
    @classmethod
    def create(cls, **kwargs: Any) -> 'Radio':
        return cls(**kwargs)
    def destroy(self) -> None:
        raise _rt.YettyError("ygui2:radio is framework-owned — it dies with its framework; detach it with widget_remove, never a raw destroy")

class Scrollarea(Widget):
    """yclass ygui2:scrollarea"""
    __yclass_domain__: ClassVar[str] = 'ygui2'
    __yclass_name__: ClassVar[str] = 'scrollarea'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui2_scrollarea_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        raise _rt.YettyError("ygui2:scrollarea is factory-owned — create it via the module's own factory (framework_root_create / widget_add / framework_overlay_add), or wrap an existing handle with _handle=...")
    @classmethod
    def create(cls, **kwargs: Any) -> 'Scrollarea':
        return cls(**kwargs)
    def destroy(self) -> None:
        raise _rt.YettyError("ygui2:scrollarea is framework-owned — it dies with its framework; detach it with widget_remove, never a raw destroy")

class Separator(Widget):
    """yclass ygui2:separator"""
    __yclass_domain__: ClassVar[str] = 'ygui2'
    __yclass_name__: ClassVar[str] = 'separator'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui2_separator_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        raise _rt.YettyError("ygui2:separator is factory-owned — create it via the module's own factory (framework_root_create / widget_add / framework_overlay_add), or wrap an existing handle with _handle=...")
    @classmethod
    def create(cls, **kwargs: Any) -> 'Separator':
        return cls(**kwargs)
    def destroy(self) -> None:
        raise _rt.YettyError("ygui2:separator is framework-owned — it dies with its framework; detach it with widget_remove, never a raw destroy")

class Slider(Widget):
    """yclass ygui2:slider"""
    __yclass_domain__: ClassVar[str] = 'ygui2'
    __yclass_name__: ClassVar[str] = 'slider'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui2_slider_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        raise _rt.YettyError("ygui2:slider is factory-owned — create it via the module's own factory (framework_root_create / widget_add / framework_overlay_add), or wrap an existing handle with _handle=...")
    @classmethod
    def create(cls, **kwargs: Any) -> 'Slider':
        return cls(**kwargs)
    def destroy(self) -> None:
        raise _rt.YettyError("ygui2:slider is framework-owned — it dies with its framework; detach it with widget_remove, never a raw destroy")

class Spinner(Widget):
    """yclass ygui2:spinner"""
    __yclass_domain__: ClassVar[str] = 'ygui2'
    __yclass_name__: ClassVar[str] = 'spinner'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui2_spinner_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        raise _rt.YettyError("ygui2:spinner is factory-owned — create it via the module's own factory (framework_root_create / widget_add / framework_overlay_add), or wrap an existing handle with _handle=...")
    @classmethod
    def create(cls, **kwargs: Any) -> 'Spinner':
        return cls(**kwargs)
    def destroy(self) -> None:
        raise _rt.YettyError("ygui2:spinner is framework-owned — it dies with its framework; detach it with widget_remove, never a raw destroy")

class Statusbar(Widget):
    """yclass ygui2:statusbar"""
    __yclass_domain__: ClassVar[str] = 'ygui2'
    __yclass_name__: ClassVar[str] = 'statusbar'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui2_statusbar_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        raise _rt.YettyError("ygui2:statusbar is factory-owned — create it via the module's own factory (framework_root_create / widget_add / framework_overlay_add), or wrap an existing handle with _handle=...")
    @classmethod
    def create(cls, **kwargs: Any) -> 'Statusbar':
        return cls(**kwargs)
    def destroy(self) -> None:
        raise _rt.YettyError("ygui2:statusbar is framework-owned — it dies with its framework; detach it with widget_remove, never a raw destroy")

class Stepper(Widget):
    """yclass ygui2:stepper"""
    __yclass_domain__: ClassVar[str] = 'ygui2'
    __yclass_name__: ClassVar[str] = 'stepper'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui2_stepper_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        raise _rt.YettyError("ygui2:stepper is factory-owned — create it via the module's own factory (framework_root_create / widget_add / framework_overlay_add), or wrap an existing handle with _handle=...")
    @classmethod
    def create(cls, **kwargs: Any) -> 'Stepper':
        return cls(**kwargs)
    def destroy(self) -> None:
        raise _rt.YettyError("ygui2:stepper is framework-owned — it dies with its framework; detach it with widget_remove, never a raw destroy")

class Table(Widget):
    """yclass ygui2:table"""
    __yclass_domain__: ClassVar[str] = 'ygui2'
    __yclass_name__: ClassVar[str] = 'table'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui2_table_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        raise _rt.YettyError("ygui2:table is factory-owned — create it via the module's own factory (framework_root_create / widget_add / framework_overlay_add), or wrap an existing handle with _handle=...")
    @classmethod
    def create(cls, **kwargs: Any) -> 'Table':
        return cls(**kwargs)
    def destroy(self) -> None:
        raise _rt.YettyError("ygui2:table is framework-owned — it dies with its framework; detach it with widget_remove, never a raw destroy")

class Textinput(Widget):
    """yclass ygui2:textinput"""
    __yclass_domain__: ClassVar[str] = 'ygui2'
    __yclass_name__: ClassVar[str] = 'textinput'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui2_textinput_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        raise _rt.YettyError("ygui2:textinput is factory-owned — create it via the module's own factory (framework_root_create / widget_add / framework_overlay_add), or wrap an existing handle with _handle=...")
    @classmethod
    def create(cls, **kwargs: Any) -> 'Textinput':
        return cls(**kwargs)
    def destroy(self) -> None:
        raise _rt.YettyError("ygui2:textinput is framework-owned — it dies with its framework; detach it with widget_remove, never a raw destroy")

class Toggle(Widget):
    """yclass ygui2:toggle"""
    __yclass_domain__: ClassVar[str] = 'ygui2'
    __yclass_name__: ClassVar[str] = 'toggle'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui2_toggle_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        raise _rt.YettyError("ygui2:toggle is factory-owned — create it via the module's own factory (framework_root_create / widget_add / framework_overlay_add), or wrap an existing handle with _handle=...")
    @classmethod
    def create(cls, **kwargs: Any) -> 'Toggle':
        return cls(**kwargs)
    def destroy(self) -> None:
        raise _rt.YettyError("ygui2:toggle is framework-owned — it dies with its framework; detach it with widget_remove, never a raw destroy")

class Tooltip(Widget):
    """yclass ygui2:tooltip"""
    __yclass_domain__: ClassVar[str] = 'ygui2'
    __yclass_name__: ClassVar[str] = 'tooltip'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui2_tooltip_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        raise _rt.YettyError("ygui2:tooltip is factory-owned — create it via the module's own factory (framework_root_create / widget_add / framework_overlay_add), or wrap an existing handle with _handle=...")
    @classmethod
    def create(cls, **kwargs: Any) -> 'Tooltip':
        return cls(**kwargs)
    def destroy(self) -> None:
        raise _rt.YettyError("ygui2:tooltip is framework-owned — it dies with its framework; detach it with widget_remove, never a raw destroy")

class YdrawEmbed(Widget):
    """yclass ygui2:ydraw_embed"""
    __yclass_domain__: ClassVar[str] = 'ygui2'
    __yclass_name__: ClassVar[str] = 'ydraw_embed'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygui2_ydraw_embed_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        raise _rt.YettyError("ygui2:ydraw_embed is factory-owned — create it via the module's own factory (framework_root_create / widget_add / framework_overlay_add), or wrap an existing handle with _handle=...")
    @classmethod
    def create(cls, **kwargs: Any) -> 'YdrawEmbed':
        return cls(**kwargs)
    def destroy(self) -> None:
        raise _rt.YettyError("ygui2:ydraw_embed is framework-owned — it dies with its framework; detach it with widget_remove, never a raw destroy")

def framework_make() -> _rt.Result[Any]:
    """Call `yetty_ygui2_framework_make`."""
    _fn = _rt.cfn("yetty_ygui2_framework_make", _t.yetty_yclass_object_ptr_result, [])
    res = _fn()
    return _rt.result_from_c(res)

def framework_dispose(obj: Any) -> _rt.Result[None]:
    """Call `yetty_ygui2_framework_dispose`."""
    _fn = _rt.cfn("yetty_ygui2_framework_dispose", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    if hasattr(obj, '_handle'):
        obj._handle = None  # consumed: no dangling wrapper
    return _rt.result_from_c(res)

def framework_root_create(obj: Any, cls: Any) -> _rt.Result[Any]:
    """Call `yetty_ygui2_framework_root_create`."""
    _fn = _rt.cfn("yetty_ygui2_framework_root_create", _t.yetty_yclass_object_ptr_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(cls))
    return _rt.result_from_c(res)

def widget_add(parent: Any, cls: Any) -> _rt.Result[Any]:
    """Call `yetty_ygui2_widget_add`."""
    _fn = _rt.cfn("yetty_ygui2_widget_add", _t.yetty_yclass_object_ptr_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(parent), _rt.handle(cls))
    return _rt.result_from_c(res)

def framework_set_sink(obj: Any, sink: Any, userdata: Any) -> _rt.Result[None]:
    """Call `yetty_ygui2_framework_set_sink`."""
    _fn = _rt.cfn("yetty_ygui2_framework_set_sink", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(sink), _rt.handle(userdata))
    return _rt.result_from_c(res)

def framework_set_viewport(obj: Any, width: float, height: float) -> _rt.Result[None]:
    """Call `yetty_ygui2_framework_set_viewport`."""
    _fn = _rt.cfn("yetty_ygui2_framework_set_viewport", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float])
    res = _fn(_rt.handle(obj), width, height)
    return _rt.result_from_c(res)

def framework_set_fullscreen(obj: Any, fullscreen: int) -> _rt.Result[None]:
    """Call `yetty_ygui2_framework_set_fullscreen`."""
    _fn = _rt.cfn("yetty_ygui2_framework_set_fullscreen", _t.yetty_ycore_void_result, [c_void_p, c_int])
    res = _fn(_rt.handle(obj), fullscreen)
    return _rt.result_from_c(res)

def framework_content_scale(obj: Any, out_scale: Any) -> _rt.Result[None]:
    """Call `yetty_ygui2_framework_content_scale`."""
    _fn = _rt.cfn("yetty_ygui2_framework_content_scale", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(out_scale))
    return _rt.result_from_c(res)

def framework_set_key_cb(obj: Any, callback: Any, userdata: Any) -> _rt.Result[None]:
    """Call `yetty_ygui2_framework_set_key_cb`."""
    _fn = _rt.cfn("yetty_ygui2_framework_set_key_cb", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(callback), _rt.handle(userdata))
    return _rt.result_from_c(res)

def row_add(parent: Any) -> _rt.Result[Any]:
    """Call `yetty_ygui2_row_add`."""
    _fn = _rt.cfn("yetty_ygui2_row_add", _t.yetty_yclass_object_ptr_result, [c_void_p])
    res = _fn(_rt.handle(parent))
    return _rt.result_from_c(res)

def column_add(parent: Any) -> _rt.Result[Any]:
    """Call `yetty_ygui2_column_add`."""
    _fn = _rt.cfn("yetty_ygui2_column_add", _t.yetty_yclass_object_ptr_result, [c_void_p])
    res = _fn(_rt.handle(parent))
    return _rt.result_from_c(res)

def framework_attach(obj: Any, read_fd: int, write_fd: int) -> _rt.Result[None]:
    """Call `yetty_ygui2_framework_attach`."""
    _fn = _rt.cfn("yetty_ygui2_framework_attach", _t.yetty_ycore_void_result, [c_void_p, c_int, c_int])
    res = _fn(_rt.handle(obj), read_fd, write_fd)
    return _rt.result_from_c(res)

def framework_send_hold(obj: Any) -> _rt.Result[None]:
    """Call `yetty_ygui2_framework_send_hold`."""
    _fn = _rt.cfn("yetty_ygui2_framework_send_hold", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def framework_hold_ack_seen(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ygui2_framework_hold_ack_seen`."""
    _fn = _rt.cfn("yetty_ygui2_framework_hold_ack_seen", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def framework_detach(obj: Any) -> _rt.Result[None]:
    """Call `yetty_ygui2_framework_detach`."""
    _fn = _rt.cfn("yetty_ygui2_framework_detach", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def framework_clear(obj: Any) -> _rt.Result[None]:
    """Call `yetty_ygui2_framework_clear`."""
    _fn = _rt.cfn("yetty_ygui2_framework_clear", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def framework_theme_copy(obj: Any, out_theme: Any) -> _rt.Result[None]:
    """Call `yetty_ygui2_framework_theme_copy`."""
    _fn = _rt.cfn("yetty_ygui2_framework_theme_copy", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(out_theme))
    return _rt.result_from_c(res)

def framework_set_theme(obj: Any, theme: Any) -> _rt.Result[None]:
    """Call `yetty_ygui2_framework_set_theme`."""
    _fn = _rt.cfn("yetty_ygui2_framework_set_theme", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(theme))
    return _rt.result_from_c(res)

def framework_overlay_add(obj: Any, cls: Any) -> _rt.Result[Any]:
    """Call `yetty_ygui2_framework_overlay_add`."""
    _fn = _rt.cfn("yetty_ygui2_framework_overlay_add", _t.yetty_yclass_object_ptr_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(cls))
    return _rt.result_from_c(res)

def framework_widget_is_focused(obj: Any, widget_obj: Any) -> _rt.Result[int]:
    """Call `yetty_ygui2_framework_widget_is_focused`."""
    _fn = _rt.cfn("yetty_ygui2_framework_widget_is_focused", _t.yetty_ycore_int_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(widget_obj))
    return _rt.result_from_c(res)

def framework_focus_set(obj: Any, widget_obj: Any) -> _rt.Result[None]:
    """Call `yetty_ygui2_framework_focus_set`."""
    _fn = _rt.cfn("yetty_ygui2_framework_focus_set", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(widget_obj))
    return _rt.result_from_c(res)

def framework_forget_subtree(obj: Any, widget_obj: Any) -> _rt.Result[None]:
    """Call `yetty_ygui2_framework_forget_subtree`."""
    _fn = _rt.cfn("yetty_ygui2_framework_forget_subtree", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(widget_obj))
    return _rt.result_from_c(res)

def framework_feed_input(obj: Any, bytes: Any, byte_count: int) -> _rt.Result[None]:
    """Call `yetty_ygui2_framework_feed_input`."""
    _fn = _rt.cfn("yetty_ygui2_framework_feed_input", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_size_t])
    res = _fn(_rt.handle(obj), _rt.handle(bytes), byte_count)
    return _rt.result_from_c(res)

def framework_feed_input_flush(obj: Any) -> _rt.Result[None]:
    """Call `yetty_ygui2_framework_feed_input_flush`."""
    _fn = _rt.cfn("yetty_ygui2_framework_feed_input_flush", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def framework_is_dirty(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ygui2_framework_is_dirty`."""
    _fn = _rt.cfn("yetty_ygui2_framework_is_dirty", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def framework_feed_mouse_button(obj: Any, x: float, y: float, button: int, pressed: int, mods: int) -> _rt.Result[None]:
    """Call `yetty_ygui2_framework_feed_mouse_button`."""
    _fn = _rt.cfn("yetty_ygui2_framework_feed_mouse_button", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float, c_int, c_int, c_int])
    res = _fn(_rt.handle(obj), x, y, button, pressed, mods)
    return _rt.result_from_c(res)

def framework_feed_mouse_motion(obj: Any, x: float, y: float, buttons_held: int) -> _rt.Result[None]:
    """Call `yetty_ygui2_framework_feed_mouse_motion`."""
    _fn = _rt.cfn("yetty_ygui2_framework_feed_mouse_motion", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float, c_uint32])
    res = _fn(_rt.handle(obj), x, y, buttons_held)
    return _rt.result_from_c(res)

def framework_feed_mouse_scroll(obj: Any, x: float, y: float, wheel_dy: float) -> _rt.Result[None]:
    """Call `yetty_ygui2_framework_feed_mouse_scroll`."""
    _fn = _rt.cfn("yetty_ygui2_framework_feed_mouse_scroll", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float, c_float])
    res = _fn(_rt.handle(obj), x, y, wheel_dy)
    return _rt.result_from_c(res)

def widget_scroll_limit(obj: Any, out_limit: Any) -> _rt.Result[None]:
    """Call `yetty_ygui2_widget_scroll_limit`."""
    _fn = _rt.cfn("yetty_ygui2_widget_scroll_limit", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(out_limit))
    return _rt.result_from_c(res)

def framework_stream_update(widget_obj: Any, child_node_id: int, payload: Any, payload_size: int) -> _rt.Result[None]:
    """Call `yetty_ygui2_framework_stream_update`."""
    _fn = _rt.cfn("yetty_ygui2_framework_stream_update", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_void_p, c_size_t])
    res = _fn(_rt.handle(widget_obj), child_node_id, _rt.handle(payload), payload_size)
    return _rt.result_from_c(res)

def framework_append_addressed_update(widget_obj: Any, list: Any, child_node_id: int, payload: Any, payload_size: int) -> _rt.Result[None]:
    """Call `yetty_ygui2_framework_append_addressed_update`."""
    _fn = _rt.cfn("yetty_ygui2_framework_append_addressed_update", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_uint32, c_void_p, c_size_t])
    res = _fn(_rt.handle(widget_obj), _rt.handle(list), child_node_id, _rt.handle(payload), payload_size)
    return _rt.result_from_c(res)

def framework_emit(obj: Any) -> _rt.Result[None]:
    """Call `yetty_ygui2_framework_emit`."""
    _fn = _rt.cfn("yetty_ygui2_framework_emit", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def widget_mark_skin_dirty(obj: Any) -> _rt.Result[None]:
    """Call `yetty_ygui2_widget_mark_skin_dirty`."""
    _fn = _rt.cfn("yetty_ygui2_widget_mark_skin_dirty", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def widget_mark_structure_dirty(obj: Any) -> _rt.Result[None]:
    """Call `yetty_ygui2_widget_mark_structure_dirty`."""
    _fn = _rt.cfn("yetty_ygui2_widget_mark_structure_dirty", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def widget_layout_set(obj: Any, spec: Any) -> _rt.Result[None]:
    """Call `yetty_ygui2_widget_layout_set`."""
    _fn = _rt.cfn("yetty_ygui2_widget_layout_set", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(spec))
    return _rt.result_from_c(res)

def widget_rect(obj: Any, out_x: Any, out_y: Any, out_w: Any, out_h: Any) -> _rt.Result[None]:
    """Call `yetty_ygui2_widget_rect`."""
    _fn = _rt.cfn("yetty_ygui2_widget_rect", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(out_x), _rt.handle(out_y), _rt.handle(out_w), _rt.handle(out_h))
    return _rt.result_from_c(res)

def widget_init_base(obj: Any, framework: Any, parent: Any, node_id: int) -> _rt.Result[None]:
    """Call `yetty_ygui2_widget_init_base`."""
    _fn = _rt.cfn("yetty_ygui2_widget_init_base", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), _rt.handle(framework), _rt.handle(parent), node_id)
    return _rt.result_from_c(res)

def widget_link_child(parent: Any, child: Any) -> _rt.Result[None]:
    """Call `yetty_ygui2_widget_link_child`."""
    _fn = _rt.cfn("yetty_ygui2_widget_link_child", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(parent), _rt.handle(child))
    return _rt.result_from_c(res)

def widget_first_child(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_ygui2_widget_first_child`."""
    _fn = _rt.cfn("yetty_ygui2_widget_first_child", _t.yetty_yclass_object_ptr_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def widget_next_sibling(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_ygui2_widget_next_sibling`."""
    _fn = _rt.cfn("yetty_ygui2_widget_next_sibling", _t.yetty_yclass_object_ptr_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def widget_parent_obj(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_ygui2_widget_parent_obj`."""
    _fn = _rt.cfn("yetty_ygui2_widget_parent_obj", _t.yetty_yclass_object_ptr_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def widget_framework_obj(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_ygui2_widget_framework_obj`."""
    _fn = _rt.cfn("yetty_ygui2_widget_framework_obj", _t.yetty_yclass_object_ptr_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def widget_node_id(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ygui2_widget_node_id`."""
    _fn = _rt.cfn("yetty_ygui2_widget_node_id", _t.yetty_ycore_uint32_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def widget_skin_node_id(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ygui2_widget_skin_node_id`."""
    _fn = _rt.cfn("yetty_ygui2_widget_skin_node_id", _t.yetty_ycore_uint32_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def widget_set_skin_node_id(obj: Any, skin_node_id: int) -> _rt.Result[None]:
    """Call `yetty_ygui2_widget_set_skin_node_id`."""
    _fn = _rt.cfn("yetty_ygui2_widget_set_skin_node_id", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), skin_node_id)
    return _rt.result_from_c(res)

def widget_set_node_id(obj: Any, node_id: int) -> _rt.Result[None]:
    """Call `yetty_ygui2_widget_set_node_id`."""
    _fn = _rt.cfn("yetty_ygui2_widget_set_node_id", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), node_id)
    return _rt.result_from_c(res)

def widget_set_transparent(obj: Any) -> _rt.Result[None]:
    """Call `yetty_ygui2_widget_set_transparent`."""
    _fn = _rt.cfn("yetty_ygui2_widget_set_transparent", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def widget_is_transparent(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ygui2_widget_is_transparent`."""
    _fn = _rt.cfn("yetty_ygui2_widget_is_transparent", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def widget_is_visible(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ygui2_widget_is_visible`."""
    _fn = _rt.cfn("yetty_ygui2_widget_is_visible", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def widget_set_focusable(obj: Any, focusable: int) -> _rt.Result[None]:
    """Call `yetty_ygui2_widget_set_focusable`."""
    _fn = _rt.cfn("yetty_ygui2_widget_set_focusable", _t.yetty_ycore_void_result, [c_void_p, c_int])
    res = _fn(_rt.handle(obj), focusable)
    return _rt.result_from_c(res)

def widget_is_focusable(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ygui2_widget_is_focusable`."""
    _fn = _rt.cfn("yetty_ygui2_widget_is_focusable", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def widget_set_dismiss_on_outside(obj: Any, dismiss: int) -> _rt.Result[None]:
    """Call `yetty_ygui2_widget_set_dismiss_on_outside`."""
    _fn = _rt.cfn("yetty_ygui2_widget_set_dismiss_on_outside", _t.yetty_ycore_void_result, [c_void_p, c_int])
    res = _fn(_rt.handle(obj), dismiss)
    return _rt.result_from_c(res)

def widget_dismiss_on_outside(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ygui2_widget_dismiss_on_outside`."""
    _fn = _rt.cfn("yetty_ygui2_widget_dismiss_on_outside", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def widget_has_focus(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ygui2_widget_has_focus`."""
    _fn = _rt.cfn("yetty_ygui2_widget_has_focus", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def widget_theme_copy(obj: Any, out_theme: Any) -> _rt.Result[None]:
    """Call `yetty_ygui2_widget_theme_copy`."""
    _fn = _rt.cfn("yetty_ygui2_widget_theme_copy", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(out_theme))
    return _rt.result_from_c(res)

def widget_set_rect(obj: Any, x: float, y: float, w: float, h: float) -> _rt.Result[None]:
    """Call `yetty_ygui2_widget_set_rect`."""
    _fn = _rt.cfn("yetty_ygui2_widget_set_rect", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float, c_float, c_float])
    res = _fn(_rt.handle(obj), x, y, w, h)
    return _rt.result_from_c(res)

def widget_layout_copy(obj: Any, out_spec: Any) -> _rt.Result[None]:
    """Call `yetty_ygui2_widget_layout_copy`."""
    _fn = _rt.cfn("yetty_ygui2_widget_layout_copy", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(out_spec))
    return _rt.result_from_c(res)

def widget_geometry_dirty(obj: Any, out_geometry: Any) -> _rt.Result[None]:
    """Call `yetty_ygui2_widget_geometry_dirty`."""
    _fn = _rt.cfn("yetty_ygui2_widget_geometry_dirty", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(out_geometry))
    return _rt.result_from_c(res)

def widget_dirty_flags(obj: Any, out_skin: Any, out_structure: Any, out_position: Any) -> _rt.Result[None]:
    """Call `yetty_ygui2_widget_dirty_flags`."""
    _fn = _rt.cfn("yetty_ygui2_widget_dirty_flags", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(out_skin), _rt.handle(out_structure), _rt.handle(out_position))
    return _rt.result_from_c(res)

def widget_clear_dirty(obj: Any) -> _rt.Result[None]:
    """Call `yetty_ygui2_widget_clear_dirty`."""
    _fn = _rt.cfn("yetty_ygui2_widget_clear_dirty", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def widget_set_position(obj: Any, x: float, y: float) -> _rt.Result[None]:
    """Call `yetty_ygui2_widget_set_position`."""
    _fn = _rt.cfn("yetty_ygui2_widget_set_position", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float])
    res = _fn(_rt.handle(obj), x, y)
    return _rt.result_from_c(res)

def widget_set_size(obj: Any, w: float, h: float) -> _rt.Result[None]:
    """Call `yetty_ygui2_widget_set_size`."""
    _fn = _rt.cfn("yetty_ygui2_widget_set_size", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float])
    res = _fn(_rt.handle(obj), w, h)
    return _rt.result_from_c(res)

def widget_absolute_rect(obj: Any, out_absolute: Any, out_x: Any, out_y: Any, out_w: Any, out_h: Any) -> _rt.Result[None]:
    """Call `yetty_ygui2_widget_absolute_rect`."""
    _fn = _rt.cfn("yetty_ygui2_widget_absolute_rect", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p, c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(out_absolute), _rt.handle(out_x), _rt.handle(out_y), _rt.handle(out_w), _rt.handle(out_h))
    return _rt.result_from_c(res)

def widget_set_visible(obj: Any, visible: int) -> _rt.Result[None]:
    """Call `yetty_ygui2_widget_set_visible`."""
    _fn = _rt.cfn("yetty_ygui2_widget_set_visible", _t.yetty_ycore_void_result, [c_void_p, c_int])
    res = _fn(_rt.handle(obj), visible)
    return _rt.result_from_c(res)

def widget_emitted_offset(obj: Any, out_x: Any, out_y: Any, out_ever: Any) -> _rt.Result[None]:
    """Call `yetty_ygui2_widget_emitted_offset`."""
    _fn = _rt.cfn("yetty_ygui2_widget_emitted_offset", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(out_x), _rt.handle(out_y), _rt.handle(out_ever))
    return _rt.result_from_c(res)

def widget_set_emitted_offset(obj: Any, x: float, y: float) -> _rt.Result[None]:
    """Call `yetty_ygui2_widget_set_emitted_offset`."""
    _fn = _rt.cfn("yetty_ygui2_widget_set_emitted_offset", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float])
    res = _fn(_rt.handle(obj), x, y)
    return _rt.result_from_c(res)

def widget_set_clip_enabled(obj: Any) -> _rt.Result[None]:
    """Call `yetty_ygui2_widget_set_clip_enabled`."""
    _fn = _rt.cfn("yetty_ygui2_widget_set_clip_enabled", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def widget_clip_state(obj: Any, out_enabled: Any, out_w: Any, out_h: Any) -> _rt.Result[None]:
    """Call `yetty_ygui2_widget_clip_state`."""
    _fn = _rt.cfn("yetty_ygui2_widget_clip_state", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(out_enabled), _rt.handle(out_w), _rt.handle(out_h))
    return _rt.result_from_c(res)

def widget_set_emitted_clip(obj: Any, w: float, h: float) -> _rt.Result[None]:
    """Call `yetty_ygui2_widget_set_emitted_clip`."""
    _fn = _rt.cfn("yetty_ygui2_widget_set_emitted_clip", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float])
    res = _fn(_rt.handle(obj), w, h)
    return _rt.result_from_c(res)

def widget_reset_emitted(obj: Any) -> _rt.Result[None]:
    """Call `yetty_ygui2_widget_reset_emitted`."""
    _fn = _rt.cfn("yetty_ygui2_widget_reset_emitted", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def widget_set_scroll(obj: Any, scroll_x: float, scroll_y: float) -> _rt.Result[None]:
    """Call `yetty_ygui2_widget_set_scroll`."""
    _fn = _rt.cfn("yetty_ygui2_widget_set_scroll", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float])
    res = _fn(_rt.handle(obj), scroll_x, scroll_y)
    return _rt.result_from_c(res)

def widget_scroll(obj: Any, out_x: Any, out_y: Any) -> _rt.Result[None]:
    """Call `yetty_ygui2_widget_scroll`."""
    _fn = _rt.cfn("yetty_ygui2_widget_scroll", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(out_x), _rt.handle(out_y))
    return _rt.result_from_c(res)

def widget_remove(obj: Any) -> _rt.Result[None]:
    """Call `yetty_ygui2_widget_remove`."""
    _fn = _rt.cfn("yetty_ygui2_widget_remove", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def button_set_label(obj: Any, text: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ygui2_button_set_label`."""
    _fn = _rt.cfn("yetty_ygui2_button_set_label", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(text))
    return _rt.result_from_c(res)

def button_on_click_set(obj: Any, callback: Any, userdata: Any) -> _rt.Result[None]:
    """Call `yetty_ygui2_button_on_click_set`."""
    _fn = _rt.cfn("yetty_ygui2_button_on_click_set", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(callback), _rt.handle(userdata))
    return _rt.result_from_c(res)

def checkbox_set_label(obj: Any, text: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ygui2_checkbox_set_label`."""
    _fn = _rt.cfn("yetty_ygui2_checkbox_set_label", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(text))
    return _rt.result_from_c(res)

def checkbox_set_checked(obj: Any, checked: int) -> _rt.Result[None]:
    """Call `yetty_ygui2_checkbox_set_checked`."""
    _fn = _rt.cfn("yetty_ygui2_checkbox_set_checked", _t.yetty_ycore_void_result, [c_void_p, c_int])
    res = _fn(_rt.handle(obj), checked)
    return _rt.result_from_c(res)

def checkbox_checked(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ygui2_checkbox_checked`."""
    _fn = _rt.cfn("yetty_ygui2_checkbox_checked", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def checkbox_on_toggle_set(obj: Any, callback: Any, userdata: Any) -> _rt.Result[None]:
    """Call `yetty_ygui2_checkbox_on_toggle_set`."""
    _fn = _rt.cfn("yetty_ygui2_checkbox_on_toggle_set", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(callback), _rt.handle(userdata))
    return _rt.result_from_c(res)

def chip_set_label(obj: Any, text: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ygui2_chip_set_label`."""
    _fn = _rt.cfn("yetty_ygui2_chip_set_label", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(text))
    return _rt.result_from_c(res)

def chip_set_selectable(obj: Any, selectable: int) -> _rt.Result[None]:
    """Call `yetty_ygui2_chip_set_selectable`."""
    _fn = _rt.cfn("yetty_ygui2_chip_set_selectable", _t.yetty_ycore_void_result, [c_void_p, c_int])
    res = _fn(_rt.handle(obj), selectable)
    return _rt.result_from_c(res)

def chip_set_selected(obj: Any, selected: int) -> _rt.Result[None]:
    """Call `yetty_ygui2_chip_set_selected`."""
    _fn = _rt.cfn("yetty_ygui2_chip_set_selected", _t.yetty_ycore_void_result, [c_void_p, c_int])
    res = _fn(_rt.handle(obj), selected)
    return _rt.result_from_c(res)

def chip_selected(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ygui2_chip_selected`."""
    _fn = _rt.cfn("yetty_ygui2_chip_selected", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def chip_on_toggle_set(obj: Any, callback: Any, userdata: Any) -> _rt.Result[None]:
    """Call `yetty_ygui2_chip_on_toggle_set`."""
    _fn = _rt.cfn("yetty_ygui2_chip_on_toggle_set", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(callback), _rt.handle(userdata))
    return _rt.result_from_c(res)

def complex_host_set_record(obj: Any, words: Any, word_count: int, child_node_id: int) -> _rt.Result[None]:
    """Call `yetty_ygui2_complex_host_set_record`."""
    _fn = _rt.cfn("yetty_ygui2_complex_host_set_record", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_uint32, c_uint32])
    res = _fn(_rt.handle(obj), _rt.handle(words), word_count, child_node_id)
    return _rt.result_from_c(res)

def complex_host_stream(obj: Any, payload: Any, payload_size: int) -> _rt.Result[None]:
    """Call `yetty_ygui2_complex_host_stream`."""
    _fn = _rt.cfn("yetty_ygui2_complex_host_stream", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_size_t])
    res = _fn(_rt.handle(obj), _rt.handle(payload), payload_size)
    return _rt.result_from_c(res)

def dialog_set_title(obj: Any, text: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ygui2_dialog_set_title`."""
    _fn = _rt.cfn("yetty_ygui2_dialog_set_title", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(text))
    return _rt.result_from_c(res)

def dialog_on_close_set(obj: Any, callback: Any, userdata: Any) -> _rt.Result[None]:
    """Call `yetty_ygui2_dialog_on_close_set`."""
    _fn = _rt.cfn("yetty_ygui2_dialog_on_close_set", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(callback), _rt.handle(userdata))
    return _rt.result_from_c(res)

def dropdown_item_add(obj: Any, text: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ygui2_dropdown_item_add`."""
    _fn = _rt.cfn("yetty_ygui2_dropdown_item_add", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(text))
    return _rt.result_from_c(res)

def dropdown_set_selected(obj: Any, selected_index: int) -> _rt.Result[None]:
    """Call `yetty_ygui2_dropdown_set_selected`."""
    _fn = _rt.cfn("yetty_ygui2_dropdown_set_selected", _t.yetty_ycore_void_result, [c_void_p, c_int])
    res = _fn(_rt.handle(obj), selected_index)
    return _rt.result_from_c(res)

def dropdown_selected(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ygui2_dropdown_selected`."""
    _fn = _rt.cfn("yetty_ygui2_dropdown_selected", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def dropdown_on_change_set(obj: Any, callback: Any, userdata: Any) -> _rt.Result[None]:
    """Call `yetty_ygui2_dropdown_on_change_set`."""
    _fn = _rt.cfn("yetty_ygui2_dropdown_on_change_set", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(callback), _rt.handle(userdata))
    return _rt.result_from_c(res)

def label_set_text(obj: Any, text: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ygui2_label_set_text`."""
    _fn = _rt.cfn("yetty_ygui2_label_set_text", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(text))
    return _rt.result_from_c(res)

def label_set_color(obj: Any, packed_rgba: int) -> _rt.Result[None]:
    """Call `yetty_ygui2_label_set_color`."""
    _fn = _rt.cfn("yetty_ygui2_label_set_color", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), packed_rgba)
    return _rt.result_from_c(res)

def label_set_font_size(obj: Any, font_size: float) -> _rt.Result[None]:
    """Call `yetty_ygui2_label_set_font_size`."""
    _fn = _rt.cfn("yetty_ygui2_label_set_font_size", _t.yetty_ycore_void_result, [c_void_p, c_float])
    res = _fn(_rt.handle(obj), font_size)
    return _rt.result_from_c(res)

def panel_set_bg(obj: Any, packed_rgba: int) -> _rt.Result[None]:
    """Call `yetty_ygui2_panel_set_bg`."""
    _fn = _rt.cfn("yetty_ygui2_panel_set_bg", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), packed_rgba)
    return _rt.result_from_c(res)

def panel_set_border(obj: Any, packed_rgba: int, width_px: float) -> _rt.Result[None]:
    """Call `yetty_ygui2_panel_set_border`."""
    _fn = _rt.cfn("yetty_ygui2_panel_set_border", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_float])
    res = _fn(_rt.handle(obj), packed_rgba, width_px)
    return _rt.result_from_c(res)

def plot_set_title(obj: Any, title: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ygui2_plot_set_title`."""
    _fn = _rt.cfn("yetty_ygui2_plot_set_title", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(title))
    return _rt.result_from_c(res)

def plot_set_expression(obj: Any, source: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ygui2_plot_set_expression`."""
    _fn = _rt.cfn("yetty_ygui2_plot_set_expression", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(source))
    return _rt.result_from_c(res)

def plot_set_y_range(obj: Any, min: float, max: float) -> _rt.Result[None]:
    """Call `yetty_ygui2_plot_set_y_range`."""
    _fn = _rt.cfn("yetty_ygui2_plot_set_y_range", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float])
    res = _fn(_rt.handle(obj), min, max)
    return _rt.result_from_c(res)

def plot_set_x_range(obj: Any, min: float, max: float) -> _rt.Result[None]:
    """Call `yetty_ygui2_plot_set_x_range`."""
    _fn = _rt.cfn("yetty_ygui2_plot_set_x_range", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float])
    res = _fn(_rt.handle(obj), min, max)
    return _rt.result_from_c(res)

def plot_add_stream_buffer(obj: Any, name: str | bytes | None, capacity: int, color: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ygui2_plot_add_stream_buffer`."""
    _fn = _rt.cfn("yetty_ygui2_plot_add_stream_buffer", _t.yetty_ycore_void_result, [c_void_p, c_char_p, c_uint32, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(name), capacity, _rt.cstr(color))
    return _rt.result_from_c(res)

def plot_stream_samples(obj: Any, samples: Any, count: int) -> _rt.Result[None]:
    """Call `yetty_ygui2_plot_stream_samples`."""
    _fn = _rt.cfn("yetty_ygui2_plot_stream_samples", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), _rt.handle(samples), count)
    return _rt.result_from_c(res)

def plot_append_samples(obj: Any, samples: Any, count: int) -> _rt.Result[None]:
    """Call `yetty_ygui2_plot_append_samples`."""
    _fn = _rt.cfn("yetty_ygui2_plot_append_samples", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), _rt.handle(samples), count)
    return _rt.result_from_c(res)

def popup_menu_item_add(obj: Any, text: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ygui2_popup_menu_item_add`."""
    _fn = _rt.cfn("yetty_ygui2_popup_menu_item_add", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(text))
    return _rt.result_from_c(res)

def popup_menu_items_clear(obj: Any) -> _rt.Result[None]:
    """Call `yetty_ygui2_popup_menu_items_clear`."""
    _fn = _rt.cfn("yetty_ygui2_popup_menu_items_clear", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def popup_menu_content_height(obj: Any) -> _rt.Result[float]:
    """Call `yetty_ygui2_popup_menu_content_height`."""
    _fn = _rt.cfn("yetty_ygui2_popup_menu_content_height", _t.yetty_ycore_float_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def popup_menu_on_select_set(obj: Any, callback: Any, userdata: Any) -> _rt.Result[None]:
    """Call `yetty_ygui2_popup_menu_on_select_set`."""
    _fn = _rt.cfn("yetty_ygui2_popup_menu_on_select_set", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(callback), _rt.handle(userdata))
    return _rt.result_from_c(res)

def progress_set_value(obj: Any, value: float) -> _rt.Result[None]:
    """Call `yetty_ygui2_progress_set_value`."""
    _fn = _rt.cfn("yetty_ygui2_progress_set_value", _t.yetty_ycore_void_result, [c_void_p, c_float])
    res = _fn(_rt.handle(obj), value)
    return _rt.result_from_c(res)

def progress_set_accent(obj: Any, packed_rgba: int) -> _rt.Result[None]:
    """Call `yetty_ygui2_progress_set_accent`."""
    _fn = _rt.cfn("yetty_ygui2_progress_set_accent", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), packed_rgba)
    return _rt.result_from_c(res)

def radio_set_label(obj: Any, text: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ygui2_radio_set_label`."""
    _fn = _rt.cfn("yetty_ygui2_radio_set_label", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(text))
    return _rt.result_from_c(res)

def radio_set_selected(obj: Any, selected: int) -> _rt.Result[None]:
    """Call `yetty_ygui2_radio_set_selected`."""
    _fn = _rt.cfn("yetty_ygui2_radio_set_selected", _t.yetty_ycore_void_result, [c_void_p, c_int])
    res = _fn(_rt.handle(obj), selected)
    return _rt.result_from_c(res)

def radio_selected(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ygui2_radio_selected`."""
    _fn = _rt.cfn("yetty_ygui2_radio_selected", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def radio_on_select_set(obj: Any, callback: Any, userdata: Any) -> _rt.Result[None]:
    """Call `yetty_ygui2_radio_on_select_set`."""
    _fn = _rt.cfn("yetty_ygui2_radio_on_select_set", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(callback), _rt.handle(userdata))
    return _rt.result_from_c(res)

def scrollarea_configure(obj: Any, wheel_step: float, max_scroll: float) -> _rt.Result[None]:
    """Call `yetty_ygui2_scrollarea_configure`."""
    _fn = _rt.cfn("yetty_ygui2_scrollarea_configure", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float])
    res = _fn(_rt.handle(obj), wheel_step, max_scroll)
    return _rt.result_from_c(res)

def separator_set_color(obj: Any, packed_rgba: int) -> _rt.Result[None]:
    """Call `yetty_ygui2_separator_set_color`."""
    _fn = _rt.cfn("yetty_ygui2_separator_set_color", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), packed_rgba)
    return _rt.result_from_c(res)

def slider_set_range(obj: Any, minimum: float, maximum: float) -> _rt.Result[None]:
    """Call `yetty_ygui2_slider_set_range`."""
    _fn = _rt.cfn("yetty_ygui2_slider_set_range", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float])
    res = _fn(_rt.handle(obj), minimum, maximum)
    return _rt.result_from_c(res)

def slider_set_value(obj: Any, value: float) -> _rt.Result[None]:
    """Call `yetty_ygui2_slider_set_value`."""
    _fn = _rt.cfn("yetty_ygui2_slider_set_value", _t.yetty_ycore_void_result, [c_void_p, c_float])
    res = _fn(_rt.handle(obj), value)
    return _rt.result_from_c(res)

def slider_value(obj: Any) -> _rt.Result[float]:
    """Call `yetty_ygui2_slider_value`."""
    _fn = _rt.cfn("yetty_ygui2_slider_value", _t.yetty_ycore_float_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def slider_on_change_set(obj: Any, callback: Any, userdata: Any) -> _rt.Result[None]:
    """Call `yetty_ygui2_slider_on_change_set`."""
    _fn = _rt.cfn("yetty_ygui2_slider_on_change_set", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(callback), _rt.handle(userdata))
    return _rt.result_from_c(res)

def spinner_configure(obj: Any, minimum: float, maximum: float, step: float) -> _rt.Result[None]:
    """Call `yetty_ygui2_spinner_configure`."""
    _fn = _rt.cfn("yetty_ygui2_spinner_configure", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float, c_float])
    res = _fn(_rt.handle(obj), minimum, maximum, step)
    return _rt.result_from_c(res)

def spinner_set_value(obj: Any, value: float) -> _rt.Result[None]:
    """Call `yetty_ygui2_spinner_set_value`."""
    _fn = _rt.cfn("yetty_ygui2_spinner_set_value", _t.yetty_ycore_void_result, [c_void_p, c_float])
    res = _fn(_rt.handle(obj), value)
    return _rt.result_from_c(res)

def spinner_value(obj: Any) -> _rt.Result[float]:
    """Call `yetty_ygui2_spinner_value`."""
    _fn = _rt.cfn("yetty_ygui2_spinner_value", _t.yetty_ycore_float_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def spinner_on_change_set(obj: Any, callback: Any, userdata: Any) -> _rt.Result[None]:
    """Call `yetty_ygui2_spinner_on_change_set`."""
    _fn = _rt.cfn("yetty_ygui2_spinner_on_change_set", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(callback), _rt.handle(userdata))
    return _rt.result_from_c(res)

def statusbar_set_left(obj: Any, text: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ygui2_statusbar_set_left`."""
    _fn = _rt.cfn("yetty_ygui2_statusbar_set_left", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(text))
    return _rt.result_from_c(res)

def statusbar_set_right(obj: Any, text: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ygui2_statusbar_set_right`."""
    _fn = _rt.cfn("yetty_ygui2_statusbar_set_right", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(text))
    return _rt.result_from_c(res)

def stepper_set_count(obj: Any, step_count: int) -> _rt.Result[None]:
    """Call `yetty_ygui2_stepper_set_count`."""
    _fn = _rt.cfn("yetty_ygui2_stepper_set_count", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), step_count)
    return _rt.result_from_c(res)

def stepper_set_current(obj: Any, current: int) -> _rt.Result[None]:
    """Call `yetty_ygui2_stepper_set_current`."""
    _fn = _rt.cfn("yetty_ygui2_stepper_set_current", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), current)
    return _rt.result_from_c(res)

def stepper_current(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ygui2_stepper_current`."""
    _fn = _rt.cfn("yetty_ygui2_stepper_current", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def table_set_columns(obj: Any, headers: Any, widths: Any, count: int) -> _rt.Result[None]:
    """Call `yetty_ygui2_table_set_columns`."""
    _fn = _rt.cfn("yetty_ygui2_table_set_columns", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), _rt.handle(headers), _rt.handle(widths), count)
    return _rt.result_from_c(res)

def table_clear_rows(obj: Any) -> _rt.Result[None]:
    """Call `yetty_ygui2_table_clear_rows`."""
    _fn = _rt.cfn("yetty_ygui2_table_clear_rows", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def table_add_row(obj: Any, cells: Any, count: int) -> _rt.Result[None]:
    """Call `yetty_ygui2_table_add_row`."""
    _fn = _rt.cfn("yetty_ygui2_table_add_row", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), _rt.handle(cells), count)
    return _rt.result_from_c(res)

def textinput_set_text(obj: Any, text: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ygui2_textinput_set_text`."""
    _fn = _rt.cfn("yetty_ygui2_textinput_set_text", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(text))
    return _rt.result_from_c(res)

def textinput_set_placeholder(obj: Any, text: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ygui2_textinput_set_placeholder`."""
    _fn = _rt.cfn("yetty_ygui2_textinput_set_placeholder", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(text))
    return _rt.result_from_c(res)

def textinput_text_copy(obj: Any, out_text: Any, out_capacity: int) -> _rt.Result[int]:
    """Call `yetty_ygui2_textinput_text_copy`."""
    _fn = _rt.cfn("yetty_ygui2_textinput_text_copy", _t.yetty_ycore_size_result, [c_void_p, c_void_p, c_size_t])
    res = _fn(_rt.handle(obj), _rt.handle(out_text), out_capacity)
    return _rt.result_from_c(res)

def textinput_on_submit_set(obj: Any, callback: Any, userdata: Any) -> _rt.Result[None]:
    """Call `yetty_ygui2_textinput_on_submit_set`."""
    _fn = _rt.cfn("yetty_ygui2_textinput_on_submit_set", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(callback), _rt.handle(userdata))
    return _rt.result_from_c(res)

def textinput_on_change_set(obj: Any, callback: Any, userdata: Any) -> _rt.Result[None]:
    """Call `yetty_ygui2_textinput_on_change_set`."""
    _fn = _rt.cfn("yetty_ygui2_textinput_on_change_set", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(callback), _rt.handle(userdata))
    return _rt.result_from_c(res)

def toggle_set_label(obj: Any, text: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ygui2_toggle_set_label`."""
    _fn = _rt.cfn("yetty_ygui2_toggle_set_label", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(text))
    return _rt.result_from_c(res)

def toggle_set_checked(obj: Any, checked: int) -> _rt.Result[None]:
    """Call `yetty_ygui2_toggle_set_checked`."""
    _fn = _rt.cfn("yetty_ygui2_toggle_set_checked", _t.yetty_ycore_void_result, [c_void_p, c_int])
    res = _fn(_rt.handle(obj), checked)
    return _rt.result_from_c(res)

def toggle_checked(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ygui2_toggle_checked`."""
    _fn = _rt.cfn("yetty_ygui2_toggle_checked", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def toggle_on_toggle_set(obj: Any, callback: Any, userdata: Any) -> _rt.Result[None]:
    """Call `yetty_ygui2_toggle_on_toggle_set`."""
    _fn = _rt.cfn("yetty_ygui2_toggle_on_toggle_set", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(callback), _rt.handle(userdata))
    return _rt.result_from_c(res)

def tooltip_set_text(obj: Any, text: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ygui2_tooltip_set_text`."""
    _fn = _rt.cfn("yetty_ygui2_tooltip_set_text", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(text))
    return _rt.result_from_c(res)

def ydraw_embed_set_buffer(obj: Any, buffer: Any) -> _rt.Result[None]:
    """Call `yetty_ygui2_ydraw_embed_set_buffer`."""
    _fn = _rt.cfn("yetty_ygui2_ydraw_embed_set_buffer", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(buffer))
    return _rt.result_from_c(res)
