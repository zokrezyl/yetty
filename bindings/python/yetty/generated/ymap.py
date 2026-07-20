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
            if res.error is not None:
                raise _rt.YettyError(res.error.message)
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Map':
        obj = cls()
        for _key, _value in kwargs.items():
            _setter = getattr(obj, "set_" + _key, None)
            if _setter is None:
                raise TypeError(f"Map.create: unknown property {_key!r}")
            _setter(*_value) if isinstance(_value, (tuple, list)) else _setter(_value)
        return obj
    def configure(self, latitude: float, longitude: float, zoom: int, width_px: int, height_px: int) -> None:
        """Call `yetty_ymap_configure`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ymap_configure", _t.yetty_ycore_void_result, [c_void_p, c_double, c_double, c_uint32, c_uint32, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, latitude, longitude, zoom, width_px, height_px))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def set_provider(self, name: str | bytes | None) -> None:
        """Call `yetty_ymap_set_provider`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ymap_set_provider", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
        res = _rt.result_from_c(_fn(self._handle, _rt.cstr(name)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def set_custom_provider(self, url_template: str | bytes | None, is_vector: int, file_extension: str | bytes | None, max_zoom: int, attribution: str | bytes | None) -> None:
        """Call `yetty_ymap_set_custom_provider`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ymap_set_custom_provider", _t.yetty_ycore_void_result, [c_void_p, c_char_p, c_int, c_char_p, c_uint32, c_char_p])
        res = _rt.result_from_c(_fn(self._handle, _rt.cstr(url_template), is_vector, _rt.cstr(file_extension), max_zoom, _rt.cstr(attribution)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def set_center(self, latitude: float, longitude: float) -> None:
        """Call `yetty_ymap_set_center`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ymap_set_center", _t.yetty_ycore_void_result, [c_void_p, c_double, c_double])
        res = _rt.result_from_c(_fn(self._handle, latitude, longitude))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def set_zoom(self, zoom: int) -> None:
        """Call `yetty_ymap_set_zoom`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ymap_set_zoom", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, zoom))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def set_viewport(self, width_px: int, height_px: int) -> None:
        """Call `yetty_ymap_set_viewport`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ymap_set_viewport", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, width_px, height_px))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def pan_by_pixels(self, delta_x: float, delta_y: float) -> None:
        """Call `yetty_ymap_pan_by_pixels`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ymap_pan_by_pixels", _t.yetty_ycore_void_result, [c_void_p, c_double, c_double])
        res = _rt.result_from_c(_fn(self._handle, delta_x, delta_y))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def zoom_by_at(self, step: int, anchor_x: float, anchor_y: float) -> int:
        """Call `yetty_ymap_zoom_by_at`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ymap_zoom_by_at", _t.yetty_ycore_int_result, [c_void_p, c_int32, c_double, c_double])
        res = _rt.result_from_c(_fn(self._handle, step, anchor_x, anchor_y))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def get_zoom(self) -> int:
        """Call `yetty_ymap_get_zoom`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ymap_get_zoom", _t.yetty_ycore_int_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def geolocate(self) -> None:
        """Call `yetty_ymap_geolocate`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ymap_geolocate", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def attribution(self) -> str | None:
        """Call `yetty_ymap_attribution`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ymap_attribution", _t.yetty_ycore_const_char_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle), _rt.decode_cstr)
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def is_vector(self) -> int:
        """Call `yetty_ymap_is_vector`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ymap_is_vector", _t.yetty_ycore_int_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def overlay_geojson(self, geojson_text: str | bytes | None) -> None:
        """Call `yetty_ymap_overlay_geojson`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ymap_overlay_geojson", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
        res = _rt.result_from_c(_fn(self._handle, _rt.cstr(geojson_text)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def render(self) -> Any:
        """Call `yetty_ymap_render`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ymap_render", _t.yetty_ydraw_drawable_list_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def destroy(self) -> None:
        """Call `yetty_ymap_destroy`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ymap_destroy", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value

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

