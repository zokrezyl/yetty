"""yetty.ymap bindings — GENERATED from model.yaml, do not edit."""
from __future__ import annotations
from ctypes import (c_bool, c_char, c_char_p, c_double, c_float, c_int,
    c_int8, c_int16, c_int32, c_int64, c_long, c_size_t, c_ssize_t,
    c_uint, c_uint8, c_uint16, c_uint32, c_uint64, c_ulong, c_void_p)
from typing import Any, ClassVar
from .. import runtime as _rt
from . import _types as _t

class Map(_rt.YClass):
    """yclass ymap:map"""
    __yclass_domain__: ClassVar[str] = 'ymap'
    __yclass_name__: ClassVar[str] = 'map'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ymap_map_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ymap_map_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Map']:
        obj = cls()
        return obj.init_result
    def configure(self, latitude: float, longitude: float, zoom: int, width_px: int, height_px: int) -> _rt.Result[None]:
        """Call `yetty_ymap_configure`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_ymap_configure", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_double, c_double, c_uint32, c_uint32, c_uint32])
        res = _fn(None, self._handle, latitude, longitude, zoom, width_px, height_px)
        return _rt.result_from_c(res)
    def set_provider(self, name: str | bytes | None) -> _rt.Result[None]:
        """Call `yetty_ymap_set_provider`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_ymap_set_provider", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_char_p])
        res = _fn(None, self._handle, _rt.cstr(name))
        return _rt.result_from_c(res)
    def set_custom_provider(self, url_template: str | bytes | None, is_vector: int, file_extension: str | bytes | None, max_zoom: int, attribution: str | bytes | None) -> _rt.Result[None]:
        """Call `yetty_ymap_set_custom_provider`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_ymap_set_custom_provider", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_char_p, c_int, c_char_p, c_uint32, c_char_p])
        res = _fn(None, self._handle, _rt.cstr(url_template), is_vector, _rt.cstr(file_extension), max_zoom, _rt.cstr(attribution))
        return _rt.result_from_c(res)
    def set_center(self, latitude: float, longitude: float) -> _rt.Result[None]:
        """Call `yetty_ymap_set_center`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_ymap_set_center", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_double, c_double])
        res = _fn(None, self._handle, latitude, longitude)
        return _rt.result_from_c(res)
    def set_zoom(self, zoom: int) -> _rt.Result[None]:
        """Call `yetty_ymap_set_zoom`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_ymap_set_zoom", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_uint32])
        res = _fn(None, self._handle, zoom)
        return _rt.result_from_c(res)
    def set_viewport(self, width_px: int, height_px: int) -> _rt.Result[None]:
        """Call `yetty_ymap_set_viewport`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_ymap_set_viewport", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_uint32, c_uint32])
        res = _fn(None, self._handle, width_px, height_px)
        return _rt.result_from_c(res)
    def pan_by_pixels(self, delta_x: float, delta_y: float) -> _rt.Result[None]:
        """Call `yetty_ymap_pan_by_pixels`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_ymap_pan_by_pixels", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_double, c_double])
        res = _fn(None, self._handle, delta_x, delta_y)
        return _rt.result_from_c(res)
    def zoom_by_at(self, step: int, anchor_x: float, anchor_y: float) -> _rt.Result[int]:
        """Call `yetty_ymap_zoom_by_at`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_ymap_zoom_by_at", _t.yetty_ycore_int_result, [c_void_p, c_void_p, c_int32, c_double, c_double])
        res = _fn(None, self._handle, step, anchor_x, anchor_y)
        return _rt.result_from_c(res)
    def get_zoom(self) -> _rt.Result[int]:
        """Call `yetty_ymap_get_zoom`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_ymap_get_zoom", _t.yetty_ycore_int_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def geolocate(self) -> _rt.Result[None]:
        """Call `yetty_ymap_geolocate`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_ymap_geolocate", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def attribution(self) -> _rt.Result[str | None]:
        """Call `yetty_ymap_attribution`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_ymap_attribution", _t.yetty_ycore_const_char_ptr_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res, _rt.decode_cstr)
    def is_vector(self) -> _rt.Result[int]:
        """Call `yetty_ymap_is_vector`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_ymap_is_vector", _t.yetty_ycore_int_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def render(self) -> _rt.Result[Any]:
        """Call `yetty_ymap_render`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_ymap_render", _t.yetty_ydraw_drawable_list_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)
    def destroy(self) -> _rt.Result[None]:
        """Call `yetty_ymap_destroy`; returns Result, never raises for yclass errors."""
        if self._handle is None:
            return self._invalid_result()
        _fn = _rt.cfn("yetty_ymap_destroy", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
        res = _fn(None, self._handle)
        return _rt.result_from_c(res)

def provider_count() -> int:
    """Call `yetty_ymap_provider_count`."""
    _fn = _rt.cfn("yetty_ymap_provider_count", c_uint32, [])
    return _fn()

def provider_info(index: int, out_name: Any, out_attribution: Any, out_max_zoom: Any, out_is_vector: Any) -> _rt.Result[None]:
    """Call `yetty_ymap_provider_info`."""
    _fn = _rt.cfn("yetty_ymap_provider_info", _t.yetty_ycore_void_result, [c_uint32, c_void_p, c_void_p, c_void_p, c_void_p])
    res = _fn(index, _rt.handle(out_name), _rt.handle(out_attribution), _rt.handle(out_max_zoom), _rt.handle(out_is_vector))
    return _rt.result_from_c(res)

def emit_osc(list: Any, fd: int) -> _rt.Result[None]:
    """Call `yetty_ymap_emit_osc`."""
    _fn = _rt.cfn("yetty_ymap_emit_osc", _t.yetty_ycore_void_result, [c_void_p, c_int])
    res = _fn(_rt.handle(list), fd)
    return _rt.result_from_c(res)

