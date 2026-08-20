"""yetty.yview bindings — GENERATED from model.yaml, do not edit."""
from __future__ import annotations
from ctypes import (c_bool, c_char, c_char_p, c_double, c_float, c_int,
    c_int8, c_int16, c_int32, c_int64, c_long, c_size_t, c_ssize_t,
    c_uint, c_uint8, c_uint16, c_uint32, c_uint64, c_ulong, c_void_p)
from typing import Any, ClassVar
from .. import runtime as _rt
from . import _types as _t

class View(_rt.YClass):
    """yclass yview:view"""
    __yclass_domain__: ClassVar[str] = 'yview'
    __yclass_name__: ClassVar[str] = 'view'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_yview_view_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_yview_view_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(None))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._apply_kwargs(kwargs)
    @classmethod
    def create(cls, **kwargs: Any) -> 'View':
        return cls(**kwargs)
    def configure(self, fd: int, child_id: int, kind: int, bg_color: int, min_x: float, min_y: float, max_x: float, max_y: float) -> None:
        """Call `yetty_yview_configure`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yview_configure", _t.yetty_ycore_void_result, [c_void_p, c_int, c_uint32, c_uint32, c_uint32, c_float, c_float, c_float, c_float])
        res = _rt.result_from_c(_fn(self._handle, fd, child_id, kind, bg_color, min_x, min_y, max_x, max_y))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def set_content(self, content: Any) -> None:
        """Call `yetty_yview_set_content`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yview_set_content", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _rt.result_from_c(_fn(self._handle, _rt.handle(content)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def set_text(self, text: str | bytes | None, font_size: float) -> None:
        """Call `yetty_yview_set_text`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yview_set_text", _t.yetty_ycore_void_result, [c_void_p, c_char_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, _rt.cstr(text), font_size))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def set_plot(self, expr: str | bytes | None, x_min: float, x_max: float, y_min: float, y_max: float) -> None:
        """Call `yetty_yview_set_plot`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yview_set_plot", _t.yetty_ycore_void_result, [c_void_p, c_char_p, c_float, c_float, c_float, c_float])
        res = _rt.result_from_c(_fn(self._handle, _rt.cstr(expr), x_min, x_max, y_min, y_max))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def set_content_size(self, content_w: float, content_h: float) -> None:
        """Call `yetty_yview_set_content_size`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yview_set_content_size", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float])
        res = _rt.result_from_c(_fn(self._handle, content_w, content_h))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def scroll_to(self, scroll_x: float, scroll_y: float) -> None:
        """Call `yetty_yview_scroll_to`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yview_scroll_to", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float])
        res = _rt.result_from_c(_fn(self._handle, scroll_x, scroll_y))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def scroll_by(self, delta_x: float, delta_y: float) -> None:
        """Call `yetty_yview_scroll_by`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yview_scroll_by", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float])
        res = _rt.result_from_c(_fn(self._handle, delta_x, delta_y))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def set_rect(self, min_x: float, min_y: float, max_x: float, max_y: float) -> None:
        """Call `yetty_yview_set_rect`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yview_set_rect", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float, c_float, c_float])
        res = _rt.result_from_c(_fn(self._handle, min_x, min_y, max_x, max_y))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def destroy(self) -> None:
        """Call `yetty_yview_destroy`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yview_destroy", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value

