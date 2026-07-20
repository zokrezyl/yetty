"""yetty.yflame bindings — GENERATED from model.yaml, do not edit."""
from __future__ import annotations
from ctypes import (c_bool, c_char, c_char_p, c_double, c_float, c_int,
    c_int8, c_int16, c_int32, c_int64, c_long, c_size_t, c_ssize_t,
    c_uint, c_uint8, c_uint16, c_uint32, c_uint64, c_ulong, c_void_p)
from typing import Any, ClassVar
from .. import runtime as _rt
from . import _types as _t

class Flame(_rt.YClass):
    """yclass yflame:flame"""
    __yclass_domain__: ClassVar[str] = 'yflame'
    __yclass_name__: ClassVar[str] = 'flame'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_yflame_flame_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_yflame_flame_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if res.error is not None:
                raise _rt.YettyError(res.error.message)
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Flame':
        obj = cls()
        for _key, _value in kwargs.items():
            _setter = getattr(obj, "set_" + _key, None)
            if _setter is None:
                raise TypeError(f"Flame.create: unknown property {_key!r}")
            _setter(*_value) if isinstance(_value, (tuple, list)) else _setter(_value)
        return obj
    def configure(self, width: float, frame_height: float, min_width: float, flags: int) -> None:
        """Call `yetty_yflame_configure`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yflame_configure", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float, c_float, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, width, frame_height, min_width, flags))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def parse(self, input: str | bytes | None, len: int) -> None:
        """Call `yetty_yflame_parse`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yflame_parse", _t.yetty_ycore_void_result, [c_void_p, c_char_p, c_size_t])
        res = _rt.result_from_c(_fn(self._handle, _rt.cstr(input), len))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def render(self) -> Any:
        """Call `yetty_yflame_render`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yflame_render", _t.yetty_ydraw_drawable_list_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def hit_test(self, x: float, y: float) -> int:
        """Call `yetty_yflame_hit_test`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yflame_hit_test", _t.yetty_ycore_int_result, [c_void_p, c_float, c_float])
        res = _rt.result_from_c(_fn(self._handle, x, y))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def focus(self, node_id: int) -> None:
        """Call `yetty_yflame_focus`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yflame_focus", _t.yetty_ycore_void_result, [c_void_p, c_int32])
        res = _rt.result_from_c(_fn(self._handle, node_id))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def focus_parent(self) -> None:
        """Call `yetty_yflame_focus_parent`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yflame_focus_parent", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def reset(self) -> None:
        """Call `yetty_yflame_reset`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yflame_reset", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def set_highlight(self, node_id: int) -> None:
        """Call `yetty_yflame_set_highlight`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yflame_set_highlight", _t.yetty_ycore_void_result, [c_void_p, c_int32])
        res = _rt.result_from_c(_fn(self._handle, node_id))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def highlight_name(self, name: str | bytes | None, len: int) -> None:
        """Call `yetty_yflame_highlight_name`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yflame_highlight_name", _t.yetty_ycore_void_result, [c_void_p, c_char_p, c_size_t])
        res = _rt.result_from_c(_fn(self._handle, _rt.cstr(name), len))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def focus_name(self, name: str | bytes | None, len: int) -> None:
        """Call `yetty_yflame_focus_name`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yflame_focus_name", _t.yetty_ycore_void_result, [c_void_p, c_char_p, c_size_t])
        res = _rt.result_from_c(_fn(self._handle, _rt.cstr(name), len))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def set_baseline(self, folded: str | bytes | None, len: int) -> None:
        """Call `yetty_yflame_set_baseline`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yflame_set_baseline", _t.yetty_ycore_void_result, [c_void_p, c_char_p, c_size_t])
        res = _rt.result_from_c(_fn(self._handle, _rt.cstr(folded), len))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def node_name(self, id: int) -> str | None:
        """Call `yetty_yflame_node_name`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yflame_node_name", _t.yetty_ycore_const_char_ptr_result, [c_void_p, c_int32])
        res = _rt.result_from_c(_fn(self._handle, id), _rt.decode_cstr)
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def node_value(self, id: int) -> Any:
        """Call `yetty_yflame_node_value`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yflame_node_value", _t.yetty_ycore_uint64_result, [c_void_p, c_int32])
        res = _rt.result_from_c(_fn(self._handle, id))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def root_value(self) -> Any:
        """Call `yetty_yflame_root_value`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yflame_root_value", _t.yetty_ycore_uint64_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def destroy(self) -> None:
        """Call `yetty_yflame_destroy`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yflame_destroy", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value

def emit_osc(list: Any, fd: int) -> _rt.Result[None]:
    """Call `yetty_yflame_emit_osc`."""
    _fn = _rt.cfn("yetty_yflame_emit_osc", _t.yetty_ycore_void_result, [c_void_p, c_int])
    res = _fn(_rt.handle(list), fd)
    return _rt.result_from_c(res)

