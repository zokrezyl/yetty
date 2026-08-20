"""yetty.ycircuit bindings — GENERATED from model.yaml, do not edit."""
from __future__ import annotations
from ctypes import (c_bool, c_char, c_char_p, c_double, c_float, c_int,
    c_int8, c_int16, c_int32, c_int64, c_long, c_size_t, c_ssize_t,
    c_uint, c_uint8, c_uint16, c_uint32, c_uint64, c_ulong, c_void_p)
from typing import Any, ClassVar
from .. import runtime as _rt
from . import _types as _t

class Circuit(_rt.YClass):
    """yclass ycircuit:circuit"""
    __yclass_domain__: ClassVar[str] = 'ycircuit'
    __yclass_name__: ClassVar[str] = 'circuit'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ycircuit_circuit_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_ycircuit_circuit_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(None))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._apply_kwargs(kwargs)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Circuit':
        return cls(**kwargs)
    def configure(self, grid_px: float, flags: int) -> None:
        """Call `yetty_ycircuit_configure`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ycircuit_configure", _t.yetty_ycore_void_result, [c_void_p, c_float, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, grid_px, flags))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def parse(self, input: str | bytes | None, len: int) -> None:
        """Call `yetty_ycircuit_parse`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ycircuit_parse", _t.yetty_ycore_void_result, [c_void_p, c_char_p, c_size_t])
        res = _rt.result_from_c(_fn(self._handle, _rt.cstr(input), len))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def clear(self) -> None:
        """Call `yetty_ycircuit_clear`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ycircuit_clear", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def add_component(self, kind: str | bytes | None, x: float, y: float, rotation_deg: int, name: str | bytes | None, value: str | bytes | None) -> int:
        """Call `yetty_ycircuit_add_component`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ycircuit_add_component", _t.yetty_ycore_int_result, [c_void_p, c_char_p, c_float, c_float, c_int32, c_char_p, c_char_p])
        res = _rt.result_from_c(_fn(self._handle, _rt.cstr(kind), x, y, rotation_deg, _rt.cstr(name), _rt.cstr(value)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def add_ic(self, x: float, y: float, rotation_deg: int, name: str | bytes | None, value: str | bytes | None, pins_left: str | bytes | None, pins_right: str | bytes | None, pins_top: str | bytes | None, pins_bottom: str | bytes | None) -> int:
        """Call `yetty_ycircuit_add_ic`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ycircuit_add_ic", _t.yetty_ycore_int_result, [c_void_p, c_float, c_float, c_int32, c_char_p, c_char_p, c_char_p, c_char_p, c_char_p, c_char_p])
        res = _rt.result_from_c(_fn(self._handle, x, y, rotation_deg, _rt.cstr(name), _rt.cstr(value), _rt.cstr(pins_left), _rt.cstr(pins_right), _rt.cstr(pins_top), _rt.cstr(pins_bottom)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def add_wire(self, x0: float, y0: float, x1: float, y1: float) -> int:
        """Call `yetty_ycircuit_add_wire`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ycircuit_add_wire", _t.yetty_ycore_int_result, [c_void_p, c_float, c_float, c_float, c_float])
        res = _rt.result_from_c(_fn(self._handle, x0, y0, x1, y1))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def add_junction(self, x: float, y: float) -> int:
        """Call `yetty_ycircuit_add_junction`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ycircuit_add_junction", _t.yetty_ycore_int_result, [c_void_p, c_float, c_float])
        res = _rt.result_from_c(_fn(self._handle, x, y))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def add_label(self, x: float, y: float, text: str | bytes | None) -> int:
        """Call `yetty_ycircuit_add_label`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ycircuit_add_label", _t.yetty_ycore_int_result, [c_void_p, c_float, c_float, c_char_p])
        res = _rt.result_from_c(_fn(self._handle, x, y, _rt.cstr(text)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def render(self) -> Any:
        """Call `yetty_ycircuit_render`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ycircuit_render", _t.yetty_ydraw_drawable_list_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def hit_test(self, x: float, y: float) -> int:
        """Call `yetty_ycircuit_hit_test`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ycircuit_hit_test", _t.yetty_ycore_int_result, [c_void_p, c_float, c_float])
        res = _rt.result_from_c(_fn(self._handle, x, y))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def set_highlight(self, element_id: int) -> None:
        """Call `yetty_ycircuit_set_highlight`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ycircuit_set_highlight", _t.yetty_ycore_void_result, [c_void_p, c_int32])
        res = _rt.result_from_c(_fn(self._handle, element_id))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def destroy(self) -> None:
        """Call `yetty_ycircuit_destroy`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ycircuit_destroy", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value

def emit_osc(list: Any, fd: int) -> _rt.Result[None]:
    """Call `yetty_ycircuit_emit_osc`."""
    _fn = _rt.cfn("yetty_ycircuit_emit_osc", _t.yetty_ycore_void_result, [c_void_p, c_int])
    res = _fn(_rt.handle(list), fd)
    return _rt.result_from_c(res)

