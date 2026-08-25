"""yetty.ynet bindings — GENERATED from model.yaml, do not edit."""
from __future__ import annotations
from ctypes import (c_bool, c_char, c_char_p, c_double, c_float, c_int,
    c_int8, c_int16, c_int32, c_int64, c_long, c_size_t, c_ssize_t,
    c_uint, c_uint8, c_uint16, c_uint32, c_uint64, c_ulong, c_void_p)
from typing import Any, ClassVar
from .. import runtime as _rt
from . import _types as _t

class Capture(_rt.YClass):
    """yclass ynet:capture"""
    __yclass_domain__: ClassVar[str] = 'ynet'
    __yclass_name__: ClassVar[str] = 'capture'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ynet_capture_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_ynet_capture_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(None))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._apply_kwargs(kwargs)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Capture':
        return cls(**kwargs)
    def load_file(self, path: str | bytes | None) -> None:
        """Call `yetty_ynet_load_file`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ynet_load_file", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
        res = _rt.result_from_c(_fn(self._handle, _rt.cstr(path)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def packet_count(self) -> int:
        """Call `yetty_ynet_packet_count`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ynet_packet_count", _t.yetty_ycore_uint32_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def packet_time(self, index: int) -> float:
        """Call `yetty_ynet_packet_time`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ynet_packet_time", _t.yetty_ycore_float_result, [c_void_p, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, index))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def packet_length(self, index: int) -> int:
        """Call `yetty_ynet_packet_length`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ynet_packet_length", _t.yetty_ycore_uint32_result, [c_void_p, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, index))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def packet_protocol(self, index: int) -> str | None:
        """Call `yetty_ynet_packet_protocol`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ynet_packet_protocol", _t.yetty_ycore_const_char_ptr_result, [c_void_p, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, index), _rt.decode_cstr)
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def packet_source(self, index: int) -> str | None:
        """Call `yetty_ynet_packet_source`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ynet_packet_source", _t.yetty_ycore_const_char_ptr_result, [c_void_p, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, index), _rt.decode_cstr)
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def packet_destination(self, index: int) -> str | None:
        """Call `yetty_ynet_packet_destination`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ynet_packet_destination", _t.yetty_ycore_const_char_ptr_result, [c_void_p, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, index), _rt.decode_cstr)
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def packet_info(self, index: int) -> str | None:
        """Call `yetty_ynet_packet_info`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ynet_packet_info", _t.yetty_ycore_const_char_ptr_result, [c_void_p, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, index), _rt.decode_cstr)
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def packet_bytes(self, index: int) -> Any:
        """Call `yetty_ynet_packet_bytes`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ynet_packet_bytes", _t.yetty_ycore_const_uint8_ptr_result, [c_void_p, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, index))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def packet_caplen(self, index: int) -> int:
        """Call `yetty_ynet_packet_caplen`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ynet_packet_caplen", _t.yetty_ycore_uint32_result, [c_void_p, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, index))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def flow_count(self) -> int:
        """Call `yetty_ynet_flow_count`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ynet_flow_count", _t.yetty_ycore_uint32_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def flow_summary(self, index: int) -> str | None:
        """Call `yetty_ynet_flow_summary`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ynet_flow_summary", _t.yetty_ycore_const_char_ptr_result, [c_void_p, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, index), _rt.decode_cstr)
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def render(self, width: int, height: int) -> Any:
        """Call `yetty_ynet_render`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ynet_render", _t.yetty_ydraw_drawable_list_result, [c_void_p, c_uint32, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, width, height))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def destroy(self) -> None:
        """Call `yetty_ynet_destroy`; idempotent; raises _rt.YettyError on failure."""
        if self._handle is None:
            return None
        _fn = _rt.cfn("yetty_ynet_destroy", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        self._handle = None
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value

def emit_osc(list: Any, fd: int) -> _rt.Result[None]:
    """Call `yetty_ynet_emit_osc`."""
    _fn = _rt.cfn("yetty_ynet_emit_osc", _t.yetty_ycore_void_result, [c_void_p, c_int])
    res = _fn(_rt.handle(list), fd)
    return _rt.result_from_c(res)
