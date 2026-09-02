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
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_yfigure_figure_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(None))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._apply_kwargs(kwargs)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Figure':
        return cls(**kwargs)
    def render(self, target: Any) -> None:
        """Call `yetty_yfigure_render`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yfigure_render", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _rt.result_from_c(_fn(self._handle, _rt.handle(target)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def destroy(self) -> None:
        """Call `yetty_yfigure_destroy`; idempotent; raises _rt.YettyError on failure."""
        if self._handle is None:
            return None
        _fn = _rt.cfn("yetty_yfigure_destroy", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        self._handle = None
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def process_input(self, statemachine: Any) -> None:
        """Call `yetty_yfigure_process_input`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yfigure_process_input", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _rt.result_from_c(_fn(self._handle, _rt.handle(statemachine)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def hit_opaque(self, local_x: float, local_y: float) -> int:
        """Call `yetty_yfigure_hit_opaque`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yfigure_hit_opaque", _t.yetty_ycore_int_result, [c_void_p, c_float, c_float])
        res = _rt.result_from_c(_fn(self._handle, local_x, local_y))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def process_bytes(self, bytes: Any, bytes_len: int) -> None:
        """Call `yetty_yfigure_process_bytes`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yfigure_process_bytes", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_size_t])
        res = _rt.result_from_c(_fn(self._handle, _rt.handle(bytes), bytes_len))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def reset_content(self) -> None:
        """Call `yetty_yfigure_reset_content`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yfigure_reset_content", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def dump_state(self, indent: int) -> str | None:
        """Call `yetty_yfigure_dump_state`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yfigure_dump_state", _t.yetty_ycore_char_ptr_result, [c_void_p, c_int])
        res = _rt.result_from_c(_fn(self._handle, indent), _rt.take_owned_cstr)
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def set_scroll(self, scroll_x: float, scroll_y: float) -> None:
        """Call `yetty_yfigure_set_scroll`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yfigure_set_scroll", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float])
        res = _rt.result_from_c(_fn(self._handle, scroll_x, scroll_y))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def set_content_size(self, content_w: float, content_h: float) -> None:
        """Call `yetty_yfigure_set_content_size`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yfigure_set_content_size", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float])
        res = _rt.result_from_c(_fn(self._handle, content_w, content_h))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def apply_scroll_anchor(self, rolling_row_offset: int, cell_height: float) -> None:
        """Call `yetty_yfigure_apply_scroll_anchor`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yfigure_apply_scroll_anchor", _t.yetty_ycore_void_result, [c_void_p, c_int32, c_float])
        res = _rt.result_from_c(_fn(self._handle, rolling_row_offset, cell_height))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @property
    def rect(self) -> Any:
        """Property `rect` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yfigure_figure_rect_get", _t.rectangle_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @rect.setter
    def rect(self, value: _t.yetty_ycore_rectangle) -> None:
        """Property `rect` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yfigure_figure_rect_set", _t.yetty_ycore_void_result, [c_void_p, _t.yetty_ycore_rectangle])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def z(self) -> int:
        """Property `z` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yfigure_figure_z_get", _t.yetty_ycore_int_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @z.setter
    def z(self, value: int) -> None:
        """Property `z` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yfigure_figure_z_set", _t.yetty_ycore_void_result, [c_void_p, c_int])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def hidden(self) -> int:
        """Property `hidden` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yfigure_figure_hidden_get", _t.yetty_ycore_int_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @hidden.setter
    def hidden(self, value: int) -> None:
        """Property `hidden` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yfigure_figure_hidden_set", _t.yetty_ycore_void_result, [c_void_p, c_int])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def dirty(self) -> int:
        """Property `dirty` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yfigure_figure_dirty_get", _t.yetty_ycore_int_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @dirty.setter
    def dirty(self, value: int) -> None:
        """Property `dirty` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yfigure_figure_dirty_set", _t.yetty_ycore_void_result, [c_void_p, c_int])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    @property
    def absolute_coords(self) -> int:
        """Property `absolute_coords` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yfigure_figure_absolute_coords_get", _t.yetty_ycore_int_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    @absolute_coords.setter
    def absolute_coords(self, value: int) -> None:
        """Property `absolute_coords` (raises YettyError on failure)."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yfigure_figure_absolute_coords_set", _t.yetty_ycore_void_result, [c_void_p, c_int])
        res = _rt.result_from_c(_fn(self._handle, value))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)

class Container(Figure):
    """yclass yfigure:container"""
    __yclass_domain__: ClassVar[str] = 'yfigure'
    __yclass_name__: ClassVar[str] = 'container'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_yfigure_container_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_yfigure_container_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(None))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._apply_kwargs(kwargs)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Container':
        return cls(**kwargs)
    def constructor(self) -> None:
        """Call `yetty_yfigure_constructor`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yfigure_constructor", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def add_child(self, child: Any, id: int) -> None:
        """Call `yetty_yfigure_add_child`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yfigure_add_child", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, _rt.handle(child), id))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def remove_child_by_id(self, id: int) -> None:
        """Call `yetty_yfigure_remove_child_by_id`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yfigure_remove_child_by_id", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, id))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def raise_child_by_id(self, id: int) -> None:
        """Call `yetty_yfigure_raise_child_by_id`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yfigure_raise_child_by_id", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, id))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def create_child(self, kind_token: int, id: int, rect: _t.yetty_ycore_rectangle, init: _t.yetty_ycore_buffer) -> None:
        """Call `yetty_yfigure_create_child`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yfigure_create_child", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_uint32, _t.yetty_ycore_rectangle, _t.yetty_ycore_buffer])
        res = _rt.result_from_c(_fn(self._handle, kind_token, id, rect, _rt.as_buffer(init)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def delete_child(self, id: int) -> None:
        """Call `yetty_yfigure_delete_child`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yfigure_delete_child", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, id))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def set_child_rect(self, id: int, rect: _t.yetty_ycore_rectangle) -> None:
        """Call `yetty_yfigure_set_child_rect`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yfigure_set_child_rect", _t.yetty_ycore_void_result, [c_void_p, c_uint32, _t.yetty_ycore_rectangle])
        res = _rt.result_from_c(_fn(self._handle, id, rect))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def set_rect(self, rect: _t.yetty_ycore_rectangle) -> None:
        """Call `yetty_yfigure_set_rect`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yfigure_set_rect", _t.yetty_ycore_void_result, [c_void_p, _t.yetty_ycore_rectangle])
        res = _rt.result_from_c(_fn(self._handle, rect))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def child_object(self, child_id: int) -> Any:
        """Call `yetty_yfigure_child_object`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yfigure_child_object", _t.yetty_yclass_object_ptr_result, [c_void_p, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, child_id))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def seat_overlay(self, id: int, rect: _t.yetty_ycore_rectangle) -> None:
        """Call `yetty_yfigure_seat_overlay`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yfigure_seat_overlay", _t.yetty_ycore_void_result, [c_void_p, c_uint32, _t.yetty_ycore_rectangle])
        res = _rt.result_from_c(_fn(self._handle, id, rect))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def set_child_z(self, id: int, z: int) -> None:
        """Call `yetty_yfigure_set_child_z`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yfigure_set_child_z", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_int32])
        res = _rt.result_from_c(_fn(self._handle, id, z))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def set_child_input_passthrough(self, id: int, passthrough: int) -> None:
        """Call `yetty_yfigure_set_child_input_passthrough`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yfigure_set_child_input_passthrough", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, id, passthrough))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def set_child_hidden(self, id: int, hidden: int) -> None:
        """Call `yetty_yfigure_set_child_hidden`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yfigure_set_child_hidden", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, id, hidden))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def set_child_scroll(self, id: int, scroll_x: float, scroll_y: float) -> None:
        """Call `yetty_yfigure_set_child_scroll`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yfigure_set_child_scroll", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_float, c_float])
        res = _rt.result_from_c(_fn(self._handle, id, scroll_x, scroll_y))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def set_child_content_size(self, id: int, content_w: float, content_h: float) -> None:
        """Call `yetty_yfigure_set_child_content_size`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yfigure_set_child_content_size", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_float, c_float])
        res = _rt.result_from_c(_fn(self._handle, id, content_w, content_h))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def apply_child_body(self, id: int, body: _t.yetty_ycore_buffer) -> None:
        """Call `yetty_yfigure_apply_child_body`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yfigure_apply_child_body", _t.yetty_ycore_void_result, [c_void_p, c_uint32, _t.yetty_ycore_buffer])
        res = _rt.result_from_c(_fn(self._handle, id, _rt.as_buffer(body)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def clear_all(self) -> None:
        """Call `yetty_yfigure_clear_all`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yfigure_clear_all", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value

def dump(self: Any, indent: int) -> _rt.Result[str | None]:
    """Call `yetty_yfigure_dump`."""
    _fn = _rt.cfn("yetty_yfigure_dump", _t.yetty_ycore_char_ptr_result, [c_void_p, c_int])
    res = _fn(_rt.handle(self), indent)
    return _rt.result_from_c(res, _rt.take_owned_cstr)

def container_clear_all(obj: Any) -> _rt.Result[None]:
    """Call `yetty_yfigure_container_clear_all`."""
    _fn = _rt.cfn("yetty_yfigure_container_clear_all", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def container_set_registry(obj: Any, registry: Any) -> _rt.Result[None]:
    """Call `yetty_yfigure_container_set_registry`."""
    _fn = _rt.cfn("yetty_yfigure_container_set_registry", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(registry))
    return _rt.result_from_c(res)

def container_set_context(obj: Any, context: Any) -> _rt.Result[None]:
    """Call `yetty_yfigure_container_set_context`."""
    _fn = _rt.cfn("yetty_yfigure_container_set_context", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(context))
    return _rt.result_from_c(res)

def container_set_rect(obj: Any, rect: _t.yetty_ycore_rectangle) -> _rt.Result[None]:
    """Call `yetty_yfigure_container_set_rect`."""
    _fn = _rt.cfn("yetty_yfigure_container_set_rect", _t.yetty_ycore_void_result, [c_void_p, _t.yetty_ycore_rectangle])
    res = _fn(_rt.handle(obj), rect)
    return _rt.result_from_c(res)

def container_set_viewport_offset(obj: Any, offset_x: float, offset_y: float) -> _rt.Result[None]:
    """Call `yetty_yfigure_container_set_viewport_offset`."""
    _fn = _rt.cfn("yetty_yfigure_container_set_viewport_offset", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float])
    res = _fn(_rt.handle(obj), offset_x, offset_y)
    return _rt.result_from_c(res)

def container_set_scroll_context(obj: Any, content_root_row: int, cell_height: float) -> _rt.Result[None]:
    """Call `yetty_yfigure_container_set_scroll_context`."""
    _fn = _rt.cfn("yetty_yfigure_container_set_scroll_context", _t.yetty_ycore_void_result, [c_void_p, c_uint64, c_float])
    res = _fn(_rt.handle(obj), content_root_row, cell_height)
    return _rt.result_from_c(res)

def container_as_figure(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_yfigure_container_as_figure`."""
    _fn = _rt.cfn("yetty_yfigure_container_as_figure", _t.yetty_yfigure_figure_ptr_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def container_add_child(obj: Any, child: Any, id: int) -> _rt.Result[None]:
    """Call `yetty_yfigure_container_add_child`."""
    _fn = _rt.cfn("yetty_yfigure_container_add_child", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), _rt.handle(child), id)
    return _rt.result_from_c(res)

def container_find_child_by_id(obj: Any, id: int) -> _rt.Result[Any]:
    """Call `yetty_yfigure_container_find_child_by_id`."""
    _fn = _rt.cfn("yetty_yfigure_container_find_child_by_id", _t.yetty_yfigure_figure_ptr_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), id)
    return _rt.result_from_c(res)

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

def container_hit_test(obj: Any, x: float, y: float) -> _rt.Result[Any]:
    """Call `yetty_yfigure_container_hit_test`."""
    _fn = _rt.cfn("yetty_yfigure_container_hit_test", _t.yetty_yfigure_hit_result, [c_void_p, c_float, c_float])
    res = _fn(_rt.handle(obj), x, y)
    return _rt.result_from_c(res)
