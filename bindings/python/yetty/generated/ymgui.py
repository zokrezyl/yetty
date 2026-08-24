"""yetty.ymgui bindings — GENERATED from model.yaml, do not edit."""
from __future__ import annotations
from ctypes import (c_bool, c_char, c_char_p, c_double, c_float, c_int,
    c_int8, c_int16, c_int32, c_int64, c_long, c_size_t, c_ssize_t,
    c_uint, c_uint8, c_uint16, c_uint32, c_uint64, c_ulong, c_void_p)
from typing import Any, ClassVar
from .. import runtime as _rt
from . import _types as _t
from . import yfigure as _yfigure

class Figure(_yfigure.Figure):
    """yclass ymgui:figure"""
    __yclass_domain__: ClassVar[str] = 'ymgui'
    __yclass_name__: ClassVar[str] = 'figure'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ymgui_figure_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_ymgui_figure_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(None))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._apply_kwargs(kwargs)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Figure':
        return cls(**kwargs)

def figure_create_local(rect: _t.yetty_ycore_rectangle, pipeline: Any, context: Any) -> _rt.Result[Any]:
    """Call `yetty_ymgui_figure_create_local`."""
    _fn = _rt.cfn("yetty_ymgui_figure_create_local", _t.yetty_ymgui_figure_ptr_result, [_t.yetty_ycore_rectangle, c_void_p, c_void_p])
    res = _fn(rect, _rt.handle(pipeline), _rt.handle(context))
    return _rt.result_from_c(res)

def figure_from_base(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_ymgui_figure_from_base`."""
    _fn = _rt.cfn("yetty_ymgui_figure_from_base", _t.yetty_ymgui_figure_ptr_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def figure_set_frame(obj: Any, frame_bytes: Any, frame_size: int) -> _rt.Result[None]:
    """Call `yetty_ymgui_figure_set_frame`."""
    _fn = _rt.cfn("yetty_ymgui_figure_set_frame", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_size_t])
    res = _fn(_rt.handle(obj), _rt.handle(frame_bytes), frame_size)
    return _rt.result_from_c(res)

def figure_set_atlas(obj: Any, atlas_bytes: Any, atlas_size: int, atlas_w: int, atlas_h: int) -> _rt.Result[None]:
    """Call `yetty_ymgui_figure_set_atlas`."""
    _fn = _rt.cfn("yetty_ymgui_figure_set_atlas", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_size_t, c_uint32, c_uint32])
    res = _fn(_rt.handle(obj), _rt.handle(atlas_bytes), atlas_size, atlas_w, atlas_h)
    return _rt.result_from_c(res)

def register_factory(registry: Any, args: Any) -> _rt.Result[None]:
    """Call `yetty_ymgui_register_factory`."""
    _fn = _rt.cfn("yetty_ymgui_register_factory", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(registry), _rt.handle(args))
    return _rt.result_from_c(res)

def factory_args_release(args: Any) -> _rt.Result[None]:
    """Call `yetty_ymgui_factory_args_release`."""
    _fn = _rt.cfn("yetty_ymgui_factory_args_release", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(args))
    return _rt.result_from_c(res)
