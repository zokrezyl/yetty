"""yetty.yscene bindings — GENERATED from model.yaml, do not edit."""
from __future__ import annotations
from ctypes import (c_bool, c_char, c_char_p, c_double, c_float, c_int,
    c_int8, c_int16, c_int32, c_int64, c_long, c_size_t, c_ssize_t,
    c_uint, c_uint8, c_uint16, c_uint32, c_uint64, c_ulong, c_void_p)
from typing import Any, ClassVar
from .. import runtime as _rt
from . import _types as _t
from . import yfigure as _yfigure

class Scene(_yfigure.Figure):
    """yclass yscene:scene"""
    __yclass_domain__: ClassVar[str] = 'yscene'
    __yclass_name__: ClassVar[str] = 'scene'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_yscene_scene_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_yscene_scene_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if res.error is not None:
                raise _rt.YettyError(res.error.message)
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Scene':
        obj = cls()
        for _key, _value in kwargs.items():
            _setter = getattr(obj, "set_" + _key, None)
            if _setter is None:
                raise TypeError(f"Scene.create: unknown property {_key!r}")
            _setter(*_value) if isinstance(_value, (tuple, list)) else _setter(_value)
        return obj
    def constructor(self) -> None:
        """Call `yetty_yscene_constructor`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yscene_constructor", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def set_registry(self, registry: Any) -> None:
        """Call `yetty_yscene_set_registry`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yscene_set_registry", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _rt.result_from_c(_fn(self._handle, _rt.handle(registry)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def node_declare(self, external_id: int, parent_external_id: int) -> None:
        """Call `yetty_yscene_node_declare`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yscene_node_declare", _t.yetty_ycore_void_result, [c_void_p, c_uint64, c_uint64])
        res = _rt.result_from_c(_fn(self._handle, external_id, parent_external_id))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def node_set_transform(self, external_id: int, m00: float, m01: float, m10: float, m11: float, translate_x: float, translate_y: float) -> None:
        """Call `yetty_yscene_node_set_transform`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yscene_node_set_transform", _t.yetty_ycore_void_result, [c_void_p, c_uint64, c_float, c_float, c_float, c_float, c_float, c_float])
        res = _rt.result_from_c(_fn(self._handle, external_id, m00, m01, m10, m11, translate_x, translate_y))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def node_set_clip(self, external_id: int, min_x: float, min_y: float, max_x: float, max_y: float) -> None:
        """Call `yetty_yscene_node_set_clip`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yscene_node_set_clip", _t.yetty_ycore_void_result, [c_void_p, c_uint64, c_float, c_float, c_float, c_float])
        res = _rt.result_from_c(_fn(self._handle, external_id, min_x, min_y, max_x, max_y))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def node_clear_clip(self, external_id: int) -> None:
        """Call `yetty_yscene_node_clear_clip`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yscene_node_clear_clip", _t.yetty_ycore_void_result, [c_void_p, c_uint64])
        res = _rt.result_from_c(_fn(self._handle, external_id))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def node_set_opacity(self, external_id: int, opacity: float) -> None:
        """Call `yetty_yscene_node_set_opacity`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yscene_node_set_opacity", _t.yetty_ycore_void_result, [c_void_p, c_uint64, c_float])
        res = _rt.result_from_c(_fn(self._handle, external_id, opacity))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def node_set_z(self, external_id: int, paint_z: int) -> None:
        """Call `yetty_yscene_node_set_z`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yscene_node_set_z", _t.yetty_ycore_void_result, [c_void_p, c_uint64, c_int32])
        res = _rt.result_from_c(_fn(self._handle, external_id, paint_z))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def node_set_content(self, external_id: int, content: _t.yetty_ycore_buffer) -> None:
        """Call `yetty_yscene_node_set_content`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yscene_node_set_content", _t.yetty_ycore_void_result, [c_void_p, c_uint64, _t.yetty_ycore_buffer])
        res = _rt.result_from_c(_fn(self._handle, external_id, content))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def node_append_batch(self, external_id: int, content: _t.yetty_ycore_buffer) -> None:
        """Call `yetty_yscene_node_append_batch`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yscene_node_append_batch", _t.yetty_ycore_void_result, [c_void_p, c_uint64, _t.yetty_ycore_buffer])
        res = _rt.result_from_c(_fn(self._handle, external_id, content))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def node_replace_batch(self, external_id: int, batch_index: int, content: _t.yetty_ycore_buffer) -> None:
        """Call `yetty_yscene_node_replace_batch`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yscene_node_replace_batch", _t.yetty_ycore_void_result, [c_void_p, c_uint64, c_uint32, _t.yetty_ycore_buffer])
        res = _rt.result_from_c(_fn(self._handle, external_id, batch_index, content))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def node_remove_batch(self, external_id: int, batch_index: int) -> None:
        """Call `yetty_yscene_node_remove_batch`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yscene_node_remove_batch", _t.yetty_ycore_void_result, [c_void_p, c_uint64, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, external_id, batch_index))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def node_delete(self, external_id: int) -> None:
        """Call `yetty_yscene_node_delete`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yscene_node_delete", _t.yetty_ycore_void_result, [c_void_p, c_uint64])
        res = _rt.result_from_c(_fn(self._handle, external_id))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def zero(self) -> None:
        """Call `yetty_yscene_zero`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yscene_zero", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def commit(self) -> Any:
        """Call `yetty_yscene_commit`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yscene_commit", _t.yetty_ycore_uint64_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value

def create(rect: _t.yetty_ycore_rectangle, context: Any) -> _rt.Result[Any]:
    """Call `yetty_yscene_create`."""
    _fn = _rt.cfn("yetty_yscene_create", _t.yetty_yscene_scene_ptr_result, [_t.yetty_ycore_rectangle, c_void_p])
    res = _fn(rect, _rt.handle(context))
    return _rt.result_from_c(res)

def register_factory(registry: Any, args: Any) -> _rt.Result[None]:
    """Call `yetty_yscene_register_factory`."""
    _fn = _rt.cfn("yetty_yscene_register_factory", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(registry), _rt.handle(args))
    return _rt.result_from_c(res)

def register_factory_for_kind(registry: Any, kind: int, args: Any) -> _rt.Result[None]:
    """Call `yetty_yscene_register_factory_for_kind`."""
    _fn = _rt.cfn("yetty_yscene_register_factory_for_kind", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_void_p])
    res = _fn(_rt.handle(registry), kind, _rt.handle(args))
    return _rt.result_from_c(res)

def set_default_font(obj: Any, font: Any) -> _rt.Result[None]:
    """Call `yetty_yscene_set_default_font`."""
    _fn = _rt.cfn("yetty_yscene_set_default_font", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(font))
    return _rt.result_from_c(res)

def set_composite_factory(obj: Any, factory: Any) -> _rt.Result[None]:
    """Call `yetty_yscene_set_composite_factory`."""
    _fn = _rt.cfn("yetty_yscene_set_composite_factory", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(factory))
    return _rt.result_from_c(res)

def as_figure(scene: Any) -> Any:
    """Call `yetty_yscene_as_figure`."""
    _fn = _rt.cfn("yetty_yscene_as_figure", c_void_p, [c_void_p])
    return _fn(_rt.handle(scene))

def derive(obj: Any) -> _rt.Result[None]:
    """Call `yetty_yscene_derive`."""
    _fn = _rt.cfn("yetty_yscene_derive", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def leaf_count(obj: Any) -> _rt.Result[int]:
    """Call `yetty_yscene_leaf_count`."""
    _fn = _rt.cfn("yetty_yscene_leaf_count", _t.yetty_ycore_uint32_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def hit_test(obj: Any, screen_x: float, screen_y: float) -> _rt.Result[Any]:
    """Call `yetty_yscene_hit_test`."""
    _fn = _rt.cfn("yetty_yscene_hit_test", _t.yetty_ycore_uint64_result, [c_void_p, c_float, c_float])
    res = _fn(_rt.handle(obj), screen_x, screen_y)
    return _rt.result_from_c(res)

def render_plan(obj: Any) -> _rt.Result[str | None]:
    """Call `yetty_yscene_render_plan`."""
    _fn = _rt.cfn("yetty_yscene_render_plan", _t.yetty_ycore_char_ptr_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res, _rt.decode_cstr)

def set_view_scale(obj: Any, view_scale: float) -> _rt.Result[None]:
    """Call `yetty_yscene_set_view_scale`."""
    _fn = _rt.cfn("yetty_yscene_set_view_scale", _t.yetty_ycore_void_result, [c_void_p, c_float])
    res = _fn(_rt.handle(obj), view_scale)
    return _rt.result_from_c(res)

