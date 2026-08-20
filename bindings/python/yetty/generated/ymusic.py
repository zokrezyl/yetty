"""yetty.ymusic bindings — GENERATED from model.yaml, do not edit."""
from __future__ import annotations
from ctypes import (c_bool, c_char, c_char_p, c_double, c_float, c_int,
    c_int8, c_int16, c_int32, c_int64, c_long, c_size_t, c_ssize_t,
    c_uint, c_uint8, c_uint16, c_uint32, c_uint64, c_ulong, c_void_p)
from typing import Any, ClassVar
from .. import runtime as _rt
from . import _types as _t

class Music(_rt.YClass):
    """yclass ymusic:music"""
    __yclass_domain__: ClassVar[str] = 'ymusic'
    __yclass_name__: ClassVar[str] = 'music'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ymusic_music_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_ymusic_music_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(None))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._apply_kwargs(kwargs)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Music':
        return cls(**kwargs)
    def configure(self, width: float, staff_space: float, flags: int) -> None:
        """Call `yetty_ymusic_configure`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ymusic_configure", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, width, staff_space, flags))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def parse(self, input: str | bytes | None, len: int) -> None:
        """Call `yetty_ymusic_parse`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ymusic_parse", _t.yetty_ycore_void_result, [c_void_p, c_char_p, c_size_t])
        res = _rt.result_from_c(_fn(self._handle, _rt.cstr(input), len))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def render(self) -> Any:
        """Call `yetty_ymusic_render`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ymusic_render", _t.yetty_ydraw_drawable_list_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def hit_test(self, x: float, y: float) -> int:
        """Call `yetty_ymusic_hit_test`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ymusic_hit_test", _t.yetty_ycore_int_result, [c_void_p, c_float, c_float])
        res = _rt.result_from_c(_fn(self._handle, x, y))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def set_highlight(self, element_id: int) -> None:
        """Call `yetty_ymusic_set_highlight`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ymusic_set_highlight", _t.yetty_ycore_void_result, [c_void_p, c_int32])
        res = _rt.result_from_c(_fn(self._handle, element_id))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def destroy(self) -> None:
        """Call `yetty_ymusic_destroy`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ymusic_destroy", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value

def emit_osc(list: Any, fd: int) -> _rt.Result[None]:
    """Call `yetty_ymusic_emit_osc`."""
    _fn = _rt.cfn("yetty_ymusic_emit_osc", _t.yetty_ycore_void_result, [c_void_p, c_int])
    res = _fn(_rt.handle(list), fd)
    return _rt.result_from_c(res)

