"""yetty.ydummy bindings — GENERATED from model.yaml, do not edit."""
from __future__ import annotations
from ctypes import (c_bool, c_char, c_char_p, c_double, c_float, c_int,
    c_int8, c_int16, c_int32, c_int64, c_long, c_size_t, c_ssize_t,
    c_uint, c_uint8, c_uint16, c_uint32, c_uint64, c_ulong, c_void_p)
from typing import Any, ClassVar
from .. import runtime as _rt
from . import _types as _t

class Canvas(_rt.YClass):
    """yclass ydummy:canvas"""
    __yclass_domain__: ClassVar[str] = 'ydummy'
    __yclass_name__: ClassVar[str] = 'canvas'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ydummy_canvas_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_ydummy_canvas_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(None))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._apply_kwargs(kwargs)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Canvas':
        return cls(**kwargs)
    def constructor(self) -> None:
        """Call `yetty_ydummy_constructor`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ydummy_constructor", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def set_shader(self, wgsl: _t.yetty_ycore_buffer) -> None:
        """Call `yetty_ydummy_set_shader`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ydummy_set_shader", _t.yetty_ycore_void_result, [c_void_p, _t.yetty_ycore_buffer])
        res = _rt.result_from_c(_fn(self._handle, _rt.as_buffer(wgsl)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def set_rect(self, min_x: float, min_y: float, max_x: float, max_y: float) -> None:
        """Call `yetty_ydummy_set_rect`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ydummy_set_rect", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float, c_float, c_float])
        res = _rt.result_from_c(_fn(self._handle, min_x, min_y, max_x, max_y))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def set_time(self, seconds: float) -> None:
        """Call `yetty_ydummy_set_time`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ydummy_set_time", _t.yetty_ycore_void_result, [c_void_p, c_float])
        res = _rt.result_from_c(_fn(self._handle, seconds))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def destroy(self) -> None:
        """Call `yetty_ydummy_destroy`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ydummy_destroy", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value

def canvas_shader_text(obj: Any) -> _rt.Result[str | None]:
    """Call `yetty_ydummy_canvas_shader_text`."""
    _fn = _rt.cfn("yetty_ydummy_canvas_shader_text", _t.yetty_ycore_const_char_ptr_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res, _rt.decode_cstr)

def canvas_shader_length(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ydummy_canvas_shader_length`."""
    _fn = _rt.cfn("yetty_ydummy_canvas_shader_length", _t.yetty_ycore_size_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def canvas_shader_generation(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ydummy_canvas_shader_generation`."""
    _fn = _rt.cfn("yetty_ydummy_canvas_shader_generation", _t.yetty_ycore_uint32_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def canvas_rect(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_ydummy_canvas_rect`."""
    _fn = _rt.cfn("yetty_ydummy_canvas_rect", _t.yetty_ycore_rectangle_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def canvas_time(obj: Any) -> _rt.Result[float]:
    """Call `yetty_ydummy_canvas_time`."""
    _fn = _rt.cfn("yetty_ydummy_canvas_time", _t.yetty_ycore_float_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

