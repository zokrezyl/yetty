"""yetty.yplatform bindings — GENERATED from model.yaml, do not edit."""
from __future__ import annotations
from ctypes import (c_bool, c_char, c_char_p, c_double, c_float, c_int,
    c_int8, c_int16, c_int32, c_int64, c_long, c_size_t, c_ssize_t,
    c_uint, c_uint8, c_uint16, c_uint32, c_uint64, c_ulong, c_void_p)
from typing import Any, ClassVar
from .. import runtime as _rt
from . import _types as _t

class Clipboard(_rt.YClass):
    """yclass yplatform:clipboard"""
    __yclass_domain__: ClassVar[str] = 'yplatform'
    __yclass_name__: ClassVar[str] = 'clipboard'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_yplatform_clipboard_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_yplatform_clipboard_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Clipboard']:
        obj = cls()
        return obj.init_result
    def clipboard_set_text(self, len: int) -> _rt.Result[None]:
        """Call `yetty_yplatform_clipboard_set_text`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yplatform_clipboard_set_text", _t.yetty_ycore_void_result, [c_void_p, c_char_p, c_size_t])
        res = _fn(None, self._handle, len)
        return _rt.result_from_c(res)
    def clipboard_request_paste(self) -> _rt.Result[None]:
        """Call `yetty_yplatform_clipboard_request_paste`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yplatform_clipboard_request_paste", _t.yetty_ycore_void_result, [c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def clipboard_drain(self) -> _rt.Result[None]:
        """Call `yetty_yplatform_clipboard_drain`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yplatform_clipboard_drain", _t.yetty_ycore_void_result, [c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)

class AndroidClipboard(Clipboard):
    """yclass yplatform:android_clipboard"""
    __yclass_domain__: ClassVar[str] = 'yplatform'
    __yclass_name__: ClassVar[str] = 'android_clipboard'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_yplatform_android_clipboard_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_yplatform_android_clipboard_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['AndroidClipboard']:
        obj = cls()
        return obj.init_result

class GlfwClipboard(Clipboard):
    """yclass yplatform:glfw_clipboard"""
    __yclass_domain__: ClassVar[str] = 'yplatform'
    __yclass_name__: ClassVar[str] = 'glfw_clipboard'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_yplatform_glfw_clipboard_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_yplatform_glfw_clipboard_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['GlfwClipboard']:
        obj = cls()
        return obj.init_result

class IosClipboard(Clipboard):
    """yclass yplatform:ios_clipboard"""
    __yclass_domain__: ClassVar[str] = 'yplatform'
    __yclass_name__: ClassVar[str] = 'ios_clipboard'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_yplatform_ios_clipboard_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_yplatform_ios_clipboard_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['IosClipboard']:
        obj = cls()
        return obj.init_result

class WebasmClipboard(Clipboard):
    """yclass yplatform:webasm_clipboard"""
    __yclass_domain__: ClassVar[str] = 'yplatform'
    __yclass_name__: ClassVar[str] = 'webasm_clipboard'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_yplatform_webasm_clipboard_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_yplatform_webasm_clipboard_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['WebasmClipboard']:
        obj = cls()
        return obj.init_result

class Platform(_rt.YClass):
    """yclass yplatform:platform"""
    __yclass_domain__: ClassVar[str] = 'yplatform'
    __yclass_name__: ClassVar[str] = 'platform'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_yplatform_platform_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_yplatform_platform_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Platform']:
        obj = cls()
        return obj.init_result
    def platform_init(self, argc: int, argv: Any) -> _rt.Result[None]:
        """Call `yetty_yplatform_platform_init`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yplatform_platform_init", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_int, c_void_p])
        res = _fn(None, self._handle, argc, argv)
        return _rt.result_from_c(res)
    def platform_run(self, argc: int, argv: Any) -> _rt.Result[None]:
        """Call `yetty_yplatform_platform_run`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yplatform_platform_run", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_int, c_void_p])
        res = _fn(None, self._handle, argc, argv)
        return _rt.result_from_c(res)

class AndroidPlatform(Platform):
    """yclass yplatform:android_platform"""
    __yclass_domain__: ClassVar[str] = 'yplatform'
    __yclass_name__: ClassVar[str] = 'android_platform'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_yplatform_android_platform_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_yplatform_android_platform_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['AndroidPlatform']:
        obj = cls()
        return obj.init_result

class GlfwPlatform(Platform):
    """yclass yplatform:glfw_platform"""
    __yclass_domain__: ClassVar[str] = 'yplatform'
    __yclass_name__: ClassVar[str] = 'glfw_platform'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_yplatform_glfw_platform_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_yplatform_glfw_platform_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['GlfwPlatform']:
        obj = cls()
        return obj.init_result

class IosPlatform(Platform):
    """yclass yplatform:ios_platform"""
    __yclass_domain__: ClassVar[str] = 'yplatform'
    __yclass_name__: ClassVar[str] = 'ios_platform'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_yplatform_ios_platform_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_yplatform_ios_platform_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['IosPlatform']:
        obj = cls()
        return obj.init_result

class WebasmPlatform(Platform):
    """yclass yplatform:webasm_platform"""
    __yclass_domain__: ClassVar[str] = 'yplatform'
    __yclass_name__: ClassVar[str] = 'webasm_platform'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_yplatform_webasm_platform_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_yplatform_webasm_platform_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['WebasmPlatform']:
        obj = cls()
        return obj.init_result

class WindowChrome(_rt.YClass):
    """yclass yplatform:window_chrome"""
    __yclass_domain__: ClassVar[str] = 'yplatform'
    __yclass_name__: ClassVar[str] = 'window_chrome'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_yplatform_window_chrome_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_yplatform_window_chrome_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['WindowChrome']:
        obj = cls()
        return obj.init_result
    def window_chrome_configure(self) -> _rt.Result[None]:
        """Call `yetty_yplatform_window_chrome_configure`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yplatform_window_chrome_configure", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def window_chrome_destroy(self) -> _rt.Result[None]:
        """Call `yetty_yplatform_window_chrome_destroy`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yplatform_window_chrome_destroy", _t.yetty_ycore_void_result, [c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def window_chrome_iconify(self) -> _rt.Result[None]:
        """Call `yetty_yplatform_window_chrome_iconify`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yplatform_window_chrome_iconify", _t.yetty_ycore_void_result, [c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def window_chrome_toggle_maximize(self) -> _rt.Result[None]:
        """Call `yetty_yplatform_window_chrome_toggle_maximize`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yplatform_window_chrome_toggle_maximize", _t.yetty_ycore_void_result, [c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def window_chrome_request_close(self) -> _rt.Result[None]:
        """Call `yetty_yplatform_window_chrome_request_close`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yplatform_window_chrome_request_close", _t.yetty_ycore_void_result, [c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def window_chrome_drag_by(self, dy: int) -> _rt.Result[None]:
        """Call `yetty_yplatform_window_chrome_drag_by`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yplatform_window_chrome_drag_by", _t.yetty_ycore_void_result, [c_void_p, c_int, c_int])
        res = _fn(None, self._handle, dy)
        return _rt.result_from_c(res)
    def window_chrome_resize_by(self, dy: int, edge: int) -> _rt.Result[None]:
        """Call `yetty_yplatform_window_chrome_resize_by`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yplatform_window_chrome_resize_by", _t.yetty_ycore_void_result, [c_void_p, c_int, c_int, c_int])
        res = _fn(None, self._handle, dy, edge)
        return _rt.result_from_c(res)
    def window_chrome_begin_interactive_move(self) -> _rt.Result[None]:
        """Call `yetty_yplatform_window_chrome_begin_interactive_move`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yplatform_window_chrome_begin_interactive_move", _t.yetty_ycore_void_result, [c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def window_chrome_begin_interactive_resize(self) -> _rt.Result[None]:
        """Call `yetty_yplatform_window_chrome_begin_interactive_resize`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yplatform_window_chrome_begin_interactive_resize", _t.yetty_ycore_void_result, [c_void_p, c_int])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def window_chrome_set_cursor(self) -> _rt.Result[None]:
        """Call `yetty_yplatform_window_chrome_set_cursor`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yplatform_window_chrome_set_cursor", _t.yetty_ycore_void_result, [c_void_p, c_int])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def window_chrome_handle_event(self) -> _rt.Result[None]:
        """Call `yetty_yplatform_window_chrome_handle_event`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yplatform_window_chrome_handle_event", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)

class GlfwWindowChrome(WindowChrome):
    """yclass yplatform:glfw_window_chrome"""
    __yclass_domain__: ClassVar[str] = 'yplatform'
    __yclass_name__: ClassVar[str] = 'glfw_window_chrome'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_yplatform_glfw_window_chrome_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_yplatform_glfw_window_chrome_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['GlfwWindowChrome']:
        obj = cls()
        return obj.init_result

class Window(_rt.YClass):
    """yclass yplatform:window"""
    __yclass_domain__: ClassVar[str] = 'yplatform'
    __yclass_name__: ClassVar[str] = 'window'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_yplatform_window_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_yplatform_window_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
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
    def window_open(self, height: int, title: str | bytes | None) -> _rt.Result[None]:
        """Call `yetty_yplatform_window_open`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yplatform_window_open", _t.yetty_ycore_void_result, [c_void_p, c_int, c_int, c_char_p])
        res = _fn(None, self._handle, height, _rt.cstr(title))
        return _rt.result_from_c(res)
    def window_destroy(self) -> _rt.Result[None]:
        """Call `yetty_yplatform_window_destroy`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yplatform_window_destroy", _t.yetty_ycore_void_result, [c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def window_create_surface(self) -> _rt.Result[Any]:
        """Call `yetty_yplatform_window_create_surface`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yplatform_window_create_surface", _t.yetty_yclass_void_ptr_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def window_get_size(self, height: Any) -> _rt.Result[None]:
        """Call `yetty_yplatform_window_get_size`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yplatform_window_get_size", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, height)
        return _rt.result_from_c(res)
    def window_get_framebuffer_size(self, height: Any) -> _rt.Result[None]:
        """Call `yetty_yplatform_window_get_framebuffer_size`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yplatform_window_get_framebuffer_size", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, height)
        return _rt.result_from_c(res)
    def window_get_content_scale(self, yscale: Any) -> _rt.Result[None]:
        """Call `yetty_yplatform_window_get_content_scale`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yplatform_window_get_content_scale", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, yscale)
        return _rt.result_from_c(res)
    def window_should_close(self) -> _rt.Result[int]:
        """Call `yetty_yplatform_window_should_close`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yplatform_window_should_close", _t.yetty_ycore_int_result, [c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def window_set_title(self) -> _rt.Result[None]:
        """Call `yetty_yplatform_window_set_title`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yplatform_window_set_title", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)

class AndroidWindow(Window):
    """yclass yplatform:android_window"""
    __yclass_domain__: ClassVar[str] = 'yplatform'
    __yclass_name__: ClassVar[str] = 'android_window'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_yplatform_android_window_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_yplatform_android_window_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['AndroidWindow']:
        obj = cls()
        return obj.init_result

class GlfwWindow(Window):
    """yclass yplatform:glfw_window"""
    __yclass_domain__: ClassVar[str] = 'yplatform'
    __yclass_name__: ClassVar[str] = 'glfw_window'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_yplatform_glfw_window_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_yplatform_glfw_window_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['GlfwWindow']:
        obj = cls()
        return obj.init_result

class IosWindow(Window):
    """yclass yplatform:ios_window"""
    __yclass_domain__: ClassVar[str] = 'yplatform'
    __yclass_name__: ClassVar[str] = 'ios_window'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_yplatform_ios_window_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_yplatform_ios_window_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['IosWindow']:
        obj = cls()
        return obj.init_result

class WebasmWindow(Window):
    """yclass yplatform:webasm_window"""
    __yclass_domain__: ClassVar[str] = 'yplatform'
    __yclass_name__: ClassVar[str] = 'webasm_window'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_yplatform_webasm_window_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_yplatform_webasm_window_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['WebasmWindow']:
        obj = cls()
        return obj.init_result

def android_clipboard_configure(obj: Any, response_pipe: Any) -> _rt.Result[None]:
    """Call `yetty_yplatform_android_clipboard_configure`."""
    _fn = _rt.cfn("yetty_yplatform_android_clipboard_configure", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(response_pipe))
    return _rt.result_from_c(res)

def glfw_clipboard_configure(obj: Any, output_pipe: Any, input_pipe: Any) -> _rt.Result[None]:
    """Call `yetty_yplatform_glfw_clipboard_configure`."""
    _fn = _rt.cfn("yetty_yplatform_glfw_clipboard_configure", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(output_pipe), _rt.handle(input_pipe))
    return _rt.result_from_c(res)

def ios_clipboard_configure(obj: Any, response_pipe: Any) -> _rt.Result[None]:
    """Call `yetty_yplatform_ios_clipboard_configure`."""
    _fn = _rt.cfn("yetty_yplatform_ios_clipboard_configure", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(response_pipe))
    return _rt.result_from_c(res)

def webasm_clipboard_configure(obj: Any, response_pipe: Any) -> _rt.Result[None]:
    """Call `yetty_yplatform_webasm_clipboard_configure`."""
    _fn = _rt.cfn("yetty_yplatform_webasm_clipboard_configure", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(response_pipe))
    return _rt.result_from_c(res)

def platform_set_gpu_context(obj: Any, gpu: Any) -> _rt.Result[None]:
    """Call `yetty_yplatform_platform_set_gpu_context`."""
    _fn = _rt.cfn("yetty_yplatform_platform_set_gpu_context", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(gpu))
    return _rt.result_from_c(res)

def platform_set_services(obj: Any, config: Any, input_pipe: Any, clipboard: Any, window_chrome: Any) -> _rt.Result[None]:
    """Call `yetty_yplatform_platform_set_services`."""
    _fn = _rt.cfn("yetty_yplatform_platform_set_services", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(config), _rt.handle(input_pipe), _rt.handle(clipboard), _rt.handle(window_chrome))
    return _rt.result_from_c(res)

def platform_gpu_context(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_yplatform_platform_gpu_context`."""
    _fn = _rt.cfn("yetty_yplatform_platform_gpu_context", _t.yetty_yplatform_gpu_context_const_ptr_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def platform_config(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_yplatform_platform_config`."""
    _fn = _rt.cfn("yetty_yplatform_platform_config", _t.yetty_yconfig_config_ptr_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def platform_input_pipe(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_yplatform_platform_input_pipe`."""
    _fn = _rt.cfn("yetty_yplatform_platform_input_pipe", _t.yetty_ycore_xthread_event_pipe_ptr_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def platform_clipboard(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_yplatform_platform_clipboard`."""
    _fn = _rt.cfn("yetty_yplatform_platform_clipboard", _t.yetty_yclass_object_ptr_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def platform_window_chrome(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_yplatform_platform_window_chrome`."""
    _fn = _rt.cfn("yetty_yplatform_platform_window_chrome", _t.yetty_yclass_object_ptr_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def glfw_window_chrome_attach(obj: Any, os_window: Any, input_pipe: Any) -> _rt.Result[None]:
    """Call `yetty_yplatform_glfw_window_chrome_attach`."""
    _fn = _rt.cfn("yetty_yplatform_glfw_window_chrome_attach", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(os_window), _rt.handle(input_pipe))
    return _rt.result_from_c(res)

def android_window_set_metrics(obj: Any, framebuffer_width: int, framebuffer_height: int, content_scale: float) -> _rt.Result[None]:
    """Call `yetty_yplatform_android_window_set_metrics`."""
    _fn = _rt.cfn("yetty_yplatform_android_window_set_metrics", _t.yetty_ycore_void_result, [c_void_p, c_int, c_int, c_float])
    res = _fn(_rt.handle(obj), framebuffer_width, framebuffer_height, content_scale)
    return _rt.result_from_c(res)

def ios_window_set_metrics(obj: Any, framebuffer_width: int, framebuffer_height: int, content_scale: float) -> _rt.Result[None]:
    """Call `yetty_yplatform_ios_window_set_metrics`."""
    _fn = _rt.cfn("yetty_yplatform_ios_window_set_metrics", _t.yetty_ycore_void_result, [c_void_p, c_int, c_int, c_float])
    res = _fn(_rt.handle(obj), framebuffer_width, framebuffer_height, content_scale)
    return _rt.result_from_c(res)

