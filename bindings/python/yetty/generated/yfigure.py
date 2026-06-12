"""yetty.yfigure bindings — GENERATED from model.yaml, do not edit."""
from __future__ import annotations
from ctypes import (c_bool, c_char, c_char_p, c_double, c_float, c_int,
    c_int8, c_int16, c_int32, c_int64, c_long, c_size_t, c_ssize_t,
    c_uint, c_uint8, c_uint16, c_uint32, c_uint64, c_ulong, c_void_p)
from typing import Any, ClassVar
from .. import runtime as _rt
from . import _types as _t

class Figure(_rt.YClass):
    """yclass yfigure:figure"""
    __yclass_domain__: ClassVar[str] = 'yfigure'
    __yclass_name__: ClassVar[str] = 'figure'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_yfigure_figure_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_yfigure_figure_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Figure']:
        obj = cls()
        return obj.init_result
    def render(self, target: Any) -> _rt.Result[None]:
        """Call `yetty_yfigure_render`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yfigure_render", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, target)
        return _rt.result_from_c(res)
    def destroy(self) -> _rt.Result[None]:
        """Call `yetty_yfigure_destroy`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yfigure_destroy", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def process_input(self, statemachine: Any) -> _rt.Result[None]:
        """Call `yetty_yfigure_process_input`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yfigure_process_input", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
        res = _fn(None, self._handle, statemachine)
        return _rt.result_from_c(res)
    def process_bytes(self, bytes: Any, bytes_len: int) -> _rt.Result[None]:
        """Call `yetty_yfigure_process_bytes`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yfigure_process_bytes", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p, c_size_t])
        res = _fn(None, self._handle, bytes, bytes_len)
        return _rt.result_from_c(res)
    def reset_content(self) -> _rt.Result[None]:
        """Call `yetty_yfigure_reset_content`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yfigure_reset_content", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def dump_state(self, indent: int) -> _rt.Result[str | None]:
        """Call `yetty_yfigure_dump_state`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yfigure_dump_state", _t.yetty_ycore_char_ptr_result, [c_void_p, c_void_p, c_int])
        res = _fn(None, self._handle, indent)
        return _rt.result_from_c(res, _rt.decode_cstr)
    def set_scroll(self, scroll_x: float, scroll_y: float) -> _rt.Result[None]:
        """Call `yetty_yfigure_set_scroll`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yfigure_set_scroll", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_float, c_float])
        res = _fn(None, self._handle, scroll_x, scroll_y)
        return _rt.result_from_c(res)
    def set_content_size(self, content_w: float, content_h: float) -> _rt.Result[None]:
        """Call `yetty_yfigure_set_content_size`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yfigure_set_content_size", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_float, c_float])
        res = _fn(None, self._handle, content_w, content_h)
        return _rt.result_from_c(res)

class Container(Figure):
    """yclass yfigure:container"""
    __yclass_domain__: ClassVar[str] = 'yfigure'
    __yclass_name__: ClassVar[str] = 'container'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_yfigure_container_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_yfigure_container_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Container']:
        obj = cls()
        return obj.init_result
    def constructor(self) -> _rt.Result[None]:
        """Call `yetty_yfigure_constructor`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yfigure_constructor", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def add_child(self, child: Any, id: int) -> _rt.Result[None]:
        """Call `yetty_yfigure_add_child`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yfigure_add_child", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p, c_uint32])
        res = _fn(None, self._handle, child, id)
        return _rt.result_from_c(res)
    def remove_child_by_id(self, id: int) -> _rt.Result[None]:
        """Call `yetty_yfigure_remove_child_by_id`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yfigure_remove_child_by_id", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_uint32])
        res = _fn(None, self._handle, id)
        return _rt.result_from_c(res)
    def raise_child_by_id(self, id: int) -> _rt.Result[None]:
        """Call `yetty_yfigure_raise_child_by_id`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yfigure_raise_child_by_id", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_uint32])
        res = _fn(None, self._handle, id)
        return _rt.result_from_c(res)
    def process_records(self, bytes: _t.yetty_ycore_buffer) -> _rt.Result[None]:
        """Call `yetty_yfigure_process_records`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_yfigure_process_records", _t.yetty_ycore_void_result, [c_void_p, c_void_p, _t.yetty_ycore_buffer])
        res = _fn(None, self._handle, bytes)
        return _rt.result_from_c(res)

def dump(self: Any, indent: int) -> _rt.Result[str | None]:
    """Call `yetty_yfigure_dump`."""
    _fn = _rt.cfn("yetty_yfigure_dump", _t.yetty_ycore_char_ptr_result, [c_void_p, c_int])
    res = _fn(_rt.handle(self), indent)
    return _rt.result_from_c(res, _rt.decode_cstr)

def container_clear_all(obj: Any) -> _rt.Result[None]:
    """Call `yetty_yfigure_container_clear_all`."""
    _fn = _rt.cfn("yetty_yfigure_container_clear_all", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def container_set_registry(obj: Any, registry: Any) -> None:
    """Call `yetty_yfigure_container_set_registry`."""
    _fn = _rt.cfn("yetty_yfigure_container_set_registry", None, [c_void_p, c_void_p])
    _fn(_rt.handle(obj), _rt.handle(registry))
    return None

def container_set_context(obj: Any, context: Any) -> None:
    """Call `yetty_yfigure_container_set_context`."""
    _fn = _rt.cfn("yetty_yfigure_container_set_context", None, [c_void_p, c_void_p])
    _fn(_rt.handle(obj), _rt.handle(context))
    return None

def container_set_rect(obj: Any, rect: _t.yetty_ycore_rectangle) -> _rt.Result[None]:
    """Call `yetty_yfigure_container_set_rect`."""
    _fn = _rt.cfn("yetty_yfigure_container_set_rect", _t.yetty_ycore_void_result, [c_void_p, _t.yetty_ycore_rectangle])
    res = _fn(_rt.handle(obj), rect)
    return _rt.result_from_c(res)

def container_set_viewport_offset(obj: Any, offset_x: float, offset_y: float) -> None:
    """Call `yetty_yfigure_container_set_viewport_offset`."""
    _fn = _rt.cfn("yetty_yfigure_container_set_viewport_offset", None, [c_void_p, c_float, c_float])
    _fn(_rt.handle(obj), offset_x, offset_y)
    return None

def container_consume_envelope(obj: Any, sm: Any) -> _rt.Result[None]:
    """Call `yetty_yfigure_container_consume_envelope`."""
    _fn = _rt.cfn("yetty_yfigure_container_consume_envelope", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(sm))
    return _rt.result_from_c(res)

def container_process_input(userdata: Any, sm: Any) -> _rt.Result[None]:
    """Call `yetty_yfigure_container_process_input`."""
    _fn = _rt.cfn("yetty_yfigure_container_process_input", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(userdata), _rt.handle(sm))
    return _rt.result_from_c(res)

def container_process_records(obj: Any, bytes: Any, bytes_len: int) -> _rt.Result[None]:
    """Call `yetty_yfigure_container_process_records`."""
    _fn = _rt.cfn("yetty_yfigure_container_process_records", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_size_t])
    res = _fn(_rt.handle(obj), _rt.handle(bytes), bytes_len)
    return _rt.result_from_c(res)

def container_as_figure(obj: Any) -> Any:
    """Call `yetty_yfigure_container_as_figure`."""
    _fn = _rt.cfn("yetty_yfigure_container_as_figure", c_void_p, [c_void_p])
    return _fn(_rt.handle(obj))

def container_add_child(obj: Any, child: Any, id: int) -> _rt.Result[None]:
    """Call `yetty_yfigure_container_add_child`."""
    _fn = _rt.cfn("yetty_yfigure_container_add_child", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), _rt.handle(child), id)
    return _rt.result_from_c(res)

def container_find_child_by_id(obj: Any, id: int) -> Any:
    """Call `yetty_yfigure_container_find_child_by_id`."""
    _fn = _rt.cfn("yetty_yfigure_container_find_child_by_id", c_void_p, [c_void_p, c_uint32])
    return _fn(_rt.handle(obj), id)

def container_remove_child_by_id(obj: Any, id: int) -> _rt.Result[None]:
    """Call `yetty_yfigure_container_remove_child_by_id`."""
    _fn = _rt.cfn("yetty_yfigure_container_remove_child_by_id", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), id)
    return _rt.result_from_c(res)

def container_protect_child(obj: Any, id: int) -> _rt.Result[None]:
    """Call `yetty_yfigure_container_protect_child`."""
    _fn = _rt.cfn("yetty_yfigure_container_protect_child", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), id)
    return _rt.result_from_c(res)

def container_raise_child_by_id(obj: Any, id: int) -> _rt.Result[None]:
    """Call `yetty_yfigure_container_raise_child_by_id`."""
    _fn = _rt.cfn("yetty_yfigure_container_raise_child_by_id", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), id)
    return _rt.result_from_c(res)

def container_hit_test(obj: Any, x: float, y: float) -> _t.yetty_yfigure_hit:
    """Call `yetty_yfigure_container_hit_test`."""
    _fn = _rt.cfn("yetty_yfigure_container_hit_test", _t.yetty_yfigure_hit, [c_void_p, c_float, c_float])
    return _fn(_rt.handle(obj), x, y)

