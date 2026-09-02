"""yetty.yvterm bindings — GENERATED from model.yaml, do not edit."""
from __future__ import annotations
from ctypes import (c_bool, c_char, c_char_p, c_double, c_float, c_int,
    c_int8, c_int16, c_int32, c_int64, c_long, c_size_t, c_ssize_t,
    c_uint, c_uint8, c_uint16, c_uint32, c_uint64, c_ulong, c_void_p)
from typing import Any, ClassVar
from .. import runtime as _rt
from . import _types as _t
from . import yfigure as _yfigure

class Grid(_rt.YClass):
    """yclass yvterm:grid"""
    __yclass_domain__: ClassVar[str] = 'yvterm'
    __yclass_name__: ClassVar[str] = 'grid'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_yvterm_grid_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_yvterm_grid_make", _t.yetty_yclass_object_ptr_result, [])
        res = _rt.result_from_c(_fn())
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._owned = True
        try:
            self._apply_kwargs(kwargs)
        except BaseException:
            self.destroy()
            raise
    @classmethod
    def create(cls, **kwargs: Any) -> 'Grid':
        return cls(**kwargs)
    def destroy(self) -> None:
        """Dispose via `yetty_yvterm_grid_dispose`; idempotent."""
        if self._handle is None:
            return
        handle, self._handle = self._handle, None
        _fn = _rt.cfn("yetty_yvterm_grid_dispose", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    close = destroy

class Vterm(_yfigure.Figure):
    """yclass yvterm:vterm"""
    __yclass_domain__: ClassVar[str] = 'yvterm'
    __yclass_name__: ClassVar[str] = 'vterm'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_yvterm_vterm_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_yvterm_vterm_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(None))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._apply_kwargs(kwargs)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Vterm':
        return cls(**kwargs)

def grid_make(cols: int, rows: int, scrollback_rows: int, hot_rows: int) -> _rt.Result[Any]:
    """Call `yetty_yvterm_grid_make`."""
    _fn = _rt.cfn("yetty_yvterm_grid_make", _t.yetty_yclass_object_ptr_result, [c_uint32, c_uint32, c_uint32, c_uint32])
    res = _fn(cols, rows, scrollback_rows, hot_rows)
    return _rt.result_from_c(res)

def grid_dispose(obj: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_dispose`."""
    _fn = _rt.cfn("yetty_yvterm_grid_dispose", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    if hasattr(obj, '_handle'):
        obj._handle = None  # consumed: no dangling wrapper
    return _rt.result_from_c(res)

def grid_set_sink(obj: Any, sink: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_set_sink`."""
    _fn = _rt.cfn("yetty_yvterm_grid_set_sink", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(sink))
    return _rt.result_from_c(res)

def grid_set_clear_hook(obj: Any, fn: Any, userdata: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_set_clear_hook`."""
    _fn = _rt.cfn("yetty_yvterm_grid_set_clear_hook", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(fn), _rt.handle(userdata))
    return _rt.result_from_c(res)

def grid_set_reset_hook(obj: Any, fn: Any, userdata: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_set_reset_hook`."""
    _fn = _rt.cfn("yetty_yvterm_grid_set_reset_hook", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(fn), _rt.handle(userdata))
    return _rt.result_from_c(res)

def grid_set_materialize(obj: Any, fn: Any, userdata: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_set_materialize`."""
    _fn = _rt.cfn("yetty_yvterm_grid_set_materialize", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(fn), _rt.handle(userdata))
    return _rt.result_from_c(res)

def grid_feed(obj: Any, bytes: str | bytes | None, len: int) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_feed`."""
    _fn = _rt.cfn("yetty_yvterm_grid_feed", _t.yetty_ycore_void_result, [c_void_p, c_char_p, c_size_t])
    res = _fn(_rt.handle(obj), _rt.cstr(bytes), len)
    return _rt.result_from_c(res)

def grid_set_pixel_size(obj: Any, width_px: int, height_px: int) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_set_pixel_size`."""
    _fn = _rt.cfn("yetty_yvterm_grid_set_pixel_size", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_uint32])
    res = _fn(_rt.handle(obj), width_px, height_px)
    return _rt.result_from_c(res)

def grid_set_rich_density(obj: Any, density_scale: float) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_set_rich_density`."""
    _fn = _rt.cfn("yetty_yvterm_grid_set_rich_density", _t.yetty_ycore_void_result, [c_void_p, c_float])
    res = _fn(_rt.handle(obj), density_scale)
    return _rt.result_from_c(res)

def grid_rich_density(obj: Any, out_density: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_rich_density`."""
    _fn = _rt.cfn("yetty_yvterm_grid_rich_density", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(out_density))
    return _rt.result_from_c(res)

def grid_resize(obj: Any, cols: int, rows: int) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_resize`."""
    _fn = _rt.cfn("yetty_yvterm_grid_resize", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_uint32])
    res = _fn(_rt.handle(obj), cols, rows)
    return _rt.result_from_c(res)

def grid_is_dirty(obj: Any) -> _rt.Result[int]:
    """Call `yetty_yvterm_grid_is_dirty`."""
    _fn = _rt.cfn("yetty_yvterm_grid_is_dirty", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def grid_cursor(obj: Any, out_row: Any, out_col: Any, out_visible: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_cursor`."""
    _fn = _rt.cfn("yetty_yvterm_grid_cursor", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(out_row), _rt.handle(out_col), _rt.handle(out_visible))
    return _rt.result_from_c(res)

def grid_scroll_origin(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_yvterm_grid_scroll_origin`."""
    _fn = _rt.cfn("yetty_yvterm_grid_scroll_origin", _t.yetty_ycore_uint64_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def grid_rich_push_paint_z(obj: Any, z: int) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_rich_push_paint_z`."""
    _fn = _rt.cfn("yetty_yvterm_grid_rich_push_paint_z", _t.yetty_ycore_void_result, [c_void_p, c_int32])
    res = _fn(_rt.handle(obj), z)
    return _rt.result_from_c(res)

def grid_rich_pop_paint_z(obj: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_rich_pop_paint_z`."""
    _fn = _rt.cfn("yetty_yvterm_grid_rich_pop_paint_z", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def grid_rich_reset_paint_z(obj: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_rich_reset_paint_z`."""
    _fn = _rt.cfn("yetty_yvterm_grid_rich_reset_paint_z", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def grid_append_primitive(obj: Any, row: int, words: Any, word_count: int) -> _rt.Result[int]:
    """Call `yetty_yvterm_grid_append_primitive`."""
    _fn = _rt.cfn("yetty_yvterm_grid_append_primitive", _t.yetty_ycore_uint32_result, [c_void_p, c_uint32, c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), row, _rt.handle(words), word_count)
    return _rt.result_from_c(res)

def grid_append_primitive_extent(obj: Any, row: int, words: Any, word_count: int, content_top_px: float, content_bottom_px: float) -> _rt.Result[int]:
    """Call `yetty_yvterm_grid_append_primitive_extent`."""
    _fn = _rt.cfn("yetty_yvterm_grid_append_primitive_extent", _t.yetty_ycore_uint32_result, [c_void_p, c_uint32, c_void_p, c_uint32, c_float, c_float])
    res = _fn(_rt.handle(obj), row, _rt.handle(words), word_count, content_top_px, content_bottom_px)
    return _rt.result_from_c(res)

def grid_attach_complex(obj: Any, row: int, complex: Any) -> _rt.Result[int]:
    """Call `yetty_yvterm_grid_attach_complex`."""
    _fn = _rt.cfn("yetty_yvterm_grid_attach_complex", _t.yetty_ycore_uint32_result, [c_void_p, c_uint32, c_void_p])
    res = _fn(_rt.handle(obj), row, _rt.handle(complex))
    return _rt.result_from_c(res)

def grid_rich_span_declare(obj: Any, span_rows: int) -> _rt.Result[int]:
    """Call `yetty_yvterm_grid_rich_span_declare`."""
    _fn = _rt.cfn("yetty_yvterm_grid_rich_span_declare", _t.yetty_ycore_uint32_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), span_rows)
    return _rt.result_from_c(res)

def grid_rich_batch_abort(obj: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_rich_batch_abort`."""
    _fn = _rt.cfn("yetty_yvterm_grid_rich_batch_abort", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def grid_rich_reserve_advance(obj: Any, advance_rows: int) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_rich_reserve_advance`."""
    _fn = _rt.cfn("yetty_yvterm_grid_rich_reserve_advance", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), advance_rows)
    return _rt.result_from_c(res)

def grid_relocate_rich_to_bottom(obj: Any, span_rows: int) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_relocate_rich_to_bottom`."""
    _fn = _rt.cfn("yetty_yvterm_grid_relocate_rich_to_bottom", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), span_rows)
    return _rt.result_from_c(res)

def grid_rich_group_open(obj: Any, row: int, group_key: int) -> _rt.Result[int]:
    """Call `yetty_yvterm_grid_rich_group_open`."""
    _fn = _rt.cfn("yetty_yvterm_grid_rich_group_open", _t.yetty_ycore_uint32_result, [c_void_p, c_uint32, c_uint64])
    res = _fn(_rt.handle(obj), row, group_key)
    return _rt.result_from_c(res)

def grid_rich_group_close(obj: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_rich_group_close`."""
    _fn = _rt.cfn("yetty_yvterm_grid_rich_group_close", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def grid_rich_group_delete(obj: Any, group_key: int) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_rich_group_delete`."""
    _fn = _rt.cfn("yetty_yvterm_grid_rich_group_delete", _t.yetty_ycore_void_result, [c_void_p, c_uint64])
    res = _fn(_rt.handle(obj), group_key)
    return _rt.result_from_c(res)

def grid_rich_update_bind(obj: Any, update_key: int) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_rich_update_bind`."""
    _fn = _rt.cfn("yetty_yvterm_grid_rich_update_bind", _t.yetty_ycore_void_result, [c_void_p, c_uint64])
    res = _fn(_rt.handle(obj), update_key)
    return _rt.result_from_c(res)

def grid_rich_group_offset_set(obj: Any, group_key: int, offset_x: float, offset_y: float) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_rich_group_offset_set`."""
    _fn = _rt.cfn("yetty_yvterm_grid_rich_group_offset_set", _t.yetty_ycore_void_result, [c_void_p, c_uint64, c_float, c_float])
    res = _fn(_rt.handle(obj), group_key, offset_x, offset_y)
    return _rt.result_from_c(res)

def grid_rich_group_clip_set(obj: Any, group_key: int, clip_x: float, clip_y: float, clip_w: float, clip_h: float) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_rich_group_clip_set`."""
    _fn = _rt.cfn("yetty_yvterm_grid_rich_group_clip_set", _t.yetty_ycore_void_result, [c_void_p, c_uint64, c_float, c_float, c_float, c_float])
    res = _fn(_rt.handle(obj), group_key, clip_x, clip_y, clip_w, clip_h)
    return _rt.result_from_c(res)

def grid_rich_update_target(obj: Any, update_key: int, out_complex: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_rich_update_target`."""
    _fn = _rt.cfn("yetty_yvterm_grid_rich_update_target", _t.yetty_ycore_void_result, [c_void_p, c_uint64, c_void_p])
    res = _fn(_rt.handle(obj), update_key, _rt.handle(out_complex))
    return _rt.result_from_c(res)

def grid_rich_update_journal(obj: Any, update_key: int, target_field: int, payload_words: Any, payload_word_count: int) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_rich_update_journal`."""
    _fn = _rt.cfn("yetty_yvterm_grid_rich_update_journal", _t.yetty_ycore_void_result, [c_void_p, c_uint64, c_uint32, c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), update_key, target_field, _rt.handle(payload_words), payload_word_count)
    return _rt.result_from_c(res)

def grid_rich_update_journal_poison(obj: Any, update_key: int) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_rich_update_journal_poison`."""
    _fn = _rt.cfn("yetty_yvterm_grid_rich_update_journal_poison", _t.yetty_ycore_void_result, [c_void_p, c_uint64])
    res = _fn(_rt.handle(obj), update_key)
    return _rt.result_from_c(res)

def grid_rich_update_extent_refresh(obj: Any, update_key: int, content_top_px: float, content_bottom_px: float) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_rich_update_extent_refresh`."""
    _fn = _rt.cfn("yetty_yvterm_grid_rich_update_extent_refresh", _t.yetty_ycore_void_result, [c_void_p, c_uint64, c_float, c_float])
    res = _fn(_rt.handle(obj), update_key, content_top_px, content_bottom_px)
    return _rt.result_from_c(res)

def grid_rich_update_extent(obj: Any, update_key: int, out_top_px: Any, out_bottom_px: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_rich_update_extent`."""
    _fn = _rt.cfn("yetty_yvterm_grid_rich_update_extent", _t.yetty_ycore_void_result, [c_void_p, c_uint64, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), update_key, _rt.handle(out_top_px), _rt.handle(out_bottom_px))
    return _rt.result_from_c(res)

def grid_rich_update_paint_z(obj: Any, update_key: int, out_paint_z: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_rich_update_paint_z`."""
    _fn = _rt.cfn("yetty_yvterm_grid_rich_update_paint_z", _t.yetty_ycore_void_result, [c_void_p, c_uint64, c_void_p])
    res = _fn(_rt.handle(obj), update_key, _rt.handle(out_paint_z))
    return _rt.result_from_c(res)

def grid_rich_group_reserve(obj: Any, group_key: int, extra_records: int, extra_words: int) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_rich_group_reserve`."""
    _fn = _rt.cfn("yetty_yvterm_grid_rich_group_reserve", _t.yetty_ycore_void_result, [c_void_p, c_uint64, c_uint32, c_uint32])
    res = _fn(_rt.handle(obj), group_key, extra_records, extra_words)
    return _rt.result_from_c(res)

def grid_rich_group_token(obj: Any, group_key: int) -> _rt.Result[Any]:
    """Call `yetty_yvterm_grid_rich_group_token`."""
    _fn = _rt.cfn("yetty_yvterm_grid_rich_group_token", _t.yetty_ycore_uint64_result, [c_void_p, c_uint64])
    res = _fn(_rt.handle(obj), group_key)
    return _rt.result_from_c(res)

def grid_rich_group_query(obj: Any, group_key: int, out_span_rows: Any) -> _rt.Result[int]:
    """Call `yetty_yvterm_grid_rich_group_query`."""
    _fn = _rt.cfn("yetty_yvterm_grid_rich_group_query", _t.yetty_ycore_uint32_result, [c_void_p, c_uint64, c_void_p])
    res = _fn(_rt.handle(obj), group_key, _rt.handle(out_span_rows))
    return _rt.result_from_c(res)

def grid_set_update_journal_budget(obj: Any, budget_bytes: int) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_set_update_journal_budget`."""
    _fn = _rt.cfn("yetty_yvterm_grid_set_update_journal_budget", _t.yetty_ycore_void_result, [c_void_p, c_uint64])
    res = _fn(_rt.handle(obj), budget_bytes)
    return _rt.result_from_c(res)

def grid_clear_rich_line(obj: Any, row: int) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_clear_rich_line`."""
    _fn = _rt.cfn("yetty_yvterm_grid_clear_rich_line", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), row)
    return _rt.result_from_c(res)

def grid_clear_rich_all(obj: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_clear_rich_all`."""
    _fn = _rt.cfn("yetty_yvterm_grid_clear_rich_all", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def grid_register_wire(obj: Any, sm: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_register_wire`."""
    _fn = _rt.cfn("yetty_yvterm_grid_register_wire", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(sm))
    return _rt.result_from_c(res)

def grid_on_char(obj: Any, codepoint: int, mods: int) -> _rt.Result[int]:
    """Call `yetty_yvterm_grid_on_char`."""
    _fn = _rt.cfn("yetty_yvterm_grid_on_char", _t.yetty_ycore_int_result, [c_void_p, c_uint32, c_int])
    res = _fn(_rt.handle(obj), codepoint, mods)
    return _rt.result_from_c(res)

def grid_on_key(obj: Any, key: int, mods: int) -> _rt.Result[int]:
    """Call `yetty_yvterm_grid_on_key`."""
    _fn = _rt.cfn("yetty_yvterm_grid_on_key", _t.yetty_ycore_int_result, [c_void_p, c_int, c_int])
    res = _fn(_rt.handle(obj), key, mods)
    return _rt.result_from_c(res)

def grid_set_selection(obj: Any, active: int, anchor_row: int, anchor_col: int, head_row: int, head_col: int) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_set_selection`."""
    _fn = _rt.cfn("yetty_yvterm_grid_set_selection", _t.yetty_ycore_void_result, [c_void_p, c_int, c_uint32, c_uint32, c_uint32, c_uint32])
    res = _fn(_rt.handle(obj), active, anchor_row, anchor_col, head_row, head_col)
    return _rt.result_from_c(res)

def grid_get_selection_text(obj: Any, out: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_get_selection_text`."""
    _fn = _rt.cfn("yetty_yvterm_grid_get_selection_text", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(out))
    return _rt.result_from_c(res)

def grid_word_bounds(obj: Any, row: int, col: int, out_start_col: Any, out_end_col: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_word_bounds`."""
    _fn = _rt.cfn("yetty_yvterm_grid_word_bounds", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_uint32, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), row, col, _rt.handle(out_start_col), _rt.handle(out_end_col))
    return _rt.result_from_c(res)

def grid_dims(obj: Any, out_cols: Any, out_rows: Any, out_base: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_dims`."""
    _fn = _rt.cfn("yetty_yvterm_grid_dims", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(out_cols), _rt.handle(out_rows), _rt.handle(out_base))
    return _rt.result_from_c(res)

def grid_line_cells(obj: Any, row: int) -> _rt.Result[Any]:
    """Call `yetty_yvterm_grid_line_cells`."""
    _fn = _rt.cfn("yetty_yvterm_grid_line_cells", _t.yetty_yvterm_text_cell_const_ptr_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), row)
    return _rt.result_from_c(res)

def grid_line_dirty(obj: Any, row: int) -> _rt.Result[int]:
    """Call `yetty_yvterm_grid_line_dirty`."""
    _fn = _rt.cfn("yetty_yvterm_grid_line_dirty", _t.yetty_ycore_int_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), row)
    return _rt.result_from_c(res)

def grid_paint_plan_leaf_clip(obj: Any, leaf_index: int, out_valid: Any, out_x: Any, out_y: Any, out_w: Any, out_h: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_paint_plan_leaf_clip`."""
    _fn = _rt.cfn("yetty_yvterm_grid_paint_plan_leaf_clip", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_void_p, c_void_p, c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), leaf_index, _rt.handle(out_valid), _rt.handle(out_x), _rt.handle(out_y), _rt.handle(out_w), _rt.handle(out_h))
    return _rt.result_from_c(res)

def grid_paint_plan_leaf_count(obj: Any) -> _rt.Result[int]:
    """Call `yetty_yvterm_grid_paint_plan_leaf_count`."""
    _fn = _rt.cfn("yetty_yvterm_grid_paint_plan_leaf_count", _t.yetty_ycore_uint32_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def grid_paint_plan_leaf(obj: Any, leaf_index: int, out_block_slot: Any, out_record_index: Any, out_kind: Any, out_paint_z: Any, out_paint_sequence: Any, out_record_ordinal: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_paint_plan_leaf`."""
    _fn = _rt.cfn("yetty_yvterm_grid_paint_plan_leaf", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_void_p, c_void_p, c_void_p, c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), leaf_index, _rt.handle(out_block_slot), _rt.handle(out_record_index), _rt.handle(out_kind), _rt.handle(out_paint_z), _rt.handle(out_paint_sequence), _rt.handle(out_record_ordinal))
    return _rt.result_from_c(res)

def grid_paint_plan_leaf_offset(obj: Any, leaf_index: int, out_offset_x: Any, out_offset_y: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_paint_plan_leaf_offset`."""
    _fn = _rt.cfn("yetty_yvterm_grid_paint_plan_leaf_offset", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), leaf_index, _rt.handle(out_offset_x), _rt.handle(out_offset_y))
    return _rt.result_from_c(res)

def grid_paint_plan_leaf_anchor(obj: Any, leaf_index: int, out_bottom_owner_row: Any, out_span_rows: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_paint_plan_leaf_anchor`."""
    _fn = _rt.cfn("yetty_yvterm_grid_paint_plan_leaf_anchor", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), leaf_index, _rt.handle(out_bottom_owner_row), _rt.handle(out_span_rows))
    return _rt.result_from_c(res)

def grid_paint_plan_build_count(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_yvterm_grid_paint_plan_build_count`."""
    _fn = _rt.cfn("yetty_yvterm_grid_paint_plan_build_count", _t.yetty_ycore_uint64_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def grid_slot_rich_block_count(obj: Any, slot: int) -> _rt.Result[int]:
    """Call `yetty_yvterm_grid_slot_rich_block_count`."""
    _fn = _rt.cfn("yetty_yvterm_grid_slot_rich_block_count", _t.yetty_ycore_uint32_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), slot)
    return _rt.result_from_c(res)

def grid_slot_rich_block(obj: Any, slot: int, block_index: int, out_span_rows: Any, out_record_count: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_slot_rich_block`."""
    _fn = _rt.cfn("yetty_yvterm_grid_slot_rich_block", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_uint32, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), slot, block_index, _rt.handle(out_span_rows), _rt.handle(out_record_count))
    return _rt.result_from_c(res)

def grid_slot_rich_block_stats(obj: Any, slot: int, block_index: int, out_record_count: Any, out_group_count: Any, out_arena_words: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_slot_rich_block_stats`."""
    _fn = _rt.cfn("yetty_yvterm_grid_slot_rich_block_stats", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_uint32, c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), slot, block_index, _rt.handle(out_record_count), _rt.handle(out_group_count), _rt.handle(out_arena_words))
    return _rt.result_from_c(res)

def grid_slot_rich_reusable_scans(obj: Any, slot: int, block_index: int, out_scans: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_slot_rich_reusable_scans`."""
    _fn = _rt.cfn("yetty_yvterm_grid_slot_rich_reusable_scans", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_uint32, c_void_p])
    res = _fn(_rt.handle(obj), slot, block_index, _rt.handle(out_scans))
    return _rt.result_from_c(res)

def grid_rich_change_stamp(obj: Any, out_stamp: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_rich_change_stamp`."""
    _fn = _rt.cfn("yetty_yvterm_grid_rich_change_stamp", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(out_stamp))
    return _rt.result_from_c(res)

def grid_rich_binding_occupancy(obj: Any, out_live: Any, out_capacity: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_rich_binding_occupancy`."""
    _fn = _rt.cfn("yetty_yvterm_grid_rich_binding_occupancy", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(out_live), _rt.handle(out_capacity))
    return _rt.result_from_c(res)

def grid_slot_rich_block_sealed(obj: Any, slot: int, block_index: int) -> _rt.Result[int]:
    """Call `yetty_yvterm_grid_slot_rich_block_sealed`."""
    _fn = _rt.cfn("yetty_yvterm_grid_slot_rich_block_sealed", _t.yetty_ycore_uint32_result, [c_void_p, c_uint32, c_uint32])
    res = _fn(_rt.handle(obj), slot, block_index)
    return _rt.result_from_c(res)

def grid_slot_rich_block_record(obj: Any, slot: int, block_index: int, record_index: int, out_word_count: Any, out_complex: Any) -> _rt.Result[Any]:
    """Call `yetty_yvterm_grid_slot_rich_block_record`."""
    _fn = _rt.cfn("yetty_yvterm_grid_slot_rich_block_record", _t.yetty_ycore_const_uint32_ptr_result, [c_void_p, c_uint32, c_uint32, c_uint32, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), slot, block_index, record_index, _rt.handle(out_word_count), _rt.handle(out_complex))
    return _rt.result_from_c(res)

def grid_slot_rich_block_record_paint_key(obj: Any, slot: int, block_index: int, record_index: int, out_paint_z: Any, out_paint_sequence: Any, out_record_ordinal: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_slot_rich_block_record_paint_key`."""
    _fn = _rt.cfn("yetty_yvterm_grid_slot_rich_block_record_paint_key", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_uint32, c_uint32, c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), slot, block_index, record_index, _rt.handle(out_paint_z), _rt.handle(out_paint_sequence), _rt.handle(out_record_ordinal))
    return _rt.result_from_c(res)

def grid_live_anchor(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_yvterm_grid_live_anchor`."""
    _fn = _rt.cfn("yetty_yvterm_grid_live_anchor", _t.yetty_ycore_uint64_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def grid_history_floor_value(grid: Any) -> int:
    """Call `grid_history_floor_value`."""
    _fn = _rt.cfn("grid_history_floor_value", c_uint64, [c_void_p])
    return _fn(_rt.handle(grid))

def grid_history_floor(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_yvterm_grid_history_floor`."""
    _fn = _rt.cfn("yetty_yvterm_grid_history_floor", _t.yetty_ycore_uint64_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def grid_set_view(obj: Any, active: int, view_top_line: int) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_set_view`."""
    _fn = _rt.cfn("yetty_yvterm_grid_set_view", _t.yetty_ycore_void_result, [c_void_p, c_int, c_uint64])
    res = _fn(_rt.handle(obj), active, view_top_line)
    return _rt.result_from_c(res)

def grid_view(obj: Any, out_active: Any, out_view_top: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_view`."""
    _fn = _rt.cfn("yetty_yvterm_grid_view", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(out_active), _rt.handle(out_view_top))
    return _rt.result_from_c(res)

def grid_seed_timeline(obj: Any, base: int) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_seed_timeline`."""
    _fn = _rt.cfn("yetty_yvterm_grid_seed_timeline", _t.yetty_ycore_void_result, [c_void_p, c_uint64])
    res = _fn(_rt.handle(obj), base)
    return _rt.result_from_c(res)

def grid_inject_ring_alloc_failure(obj: Any, nth_allocation: int) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_inject_ring_alloc_failure`."""
    _fn = _rt.cfn("yetty_yvterm_grid_inject_ring_alloc_failure", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), nth_allocation)
    return _rt.result_from_c(res)

def grid_view_window(obj: Any, row_count: int, out_row_count: Any) -> _rt.Result[Any]:
    """Call `yetty_yvterm_grid_view_window`."""
    _fn = _rt.cfn("yetty_yvterm_grid_view_window", _t.yetty_ycore_const_uint32_ptr_result, [c_void_p, c_uint32, c_void_p])
    res = _fn(_rt.handle(obj), row_count, _rt.handle(out_row_count))
    return _rt.result_from_c(res)

def grid_set_tier_budgets(obj: Any, warm_bytes: int, file_max_bytes: int) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_set_tier_budgets`."""
    _fn = _rt.cfn("yetty_yvterm_grid_set_tier_budgets", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_uint32])
    res = _fn(_rt.handle(obj), warm_bytes, file_max_bytes)
    return _rt.result_from_c(res)

def grid_set_palette_color(obj: Any, index: int, rgb: int) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_set_palette_color`."""
    _fn = _rt.cfn("yetty_yvterm_grid_set_palette_color", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_uint32])
    res = _fn(_rt.handle(obj), index, rgb)
    return _rt.result_from_c(res)

def grid_set_default_colors(obj: Any, fg_rgb: int, bg_rgb: int) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_set_default_colors`."""
    _fn = _rt.cfn("yetty_yvterm_grid_set_default_colors", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_uint32])
    res = _fn(_rt.handle(obj), fg_rgb, bg_rgb)
    return _rt.result_from_c(res)

def grid_register_memtags(obj: Any, registry: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_register_memtags`."""
    _fn = _rt.cfn("yetty_yvterm_grid_register_memtags", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(registry))
    return _rt.result_from_c(res)

def grid_selection(obj: Any, out_active: Any, out_anchor_row: Any, out_anchor_col: Any, out_head_row: Any, out_head_col: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_selection`."""
    _fn = _rt.cfn("yetty_yvterm_grid_selection", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p, c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(out_active), _rt.handle(out_anchor_row), _rt.handle(out_anchor_col), _rt.handle(out_head_row), _rt.handle(out_head_col))
    return _rt.result_from_c(res)

def grid_clear_dirty(obj: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_clear_dirty`."""
    _fn = _rt.cfn("yetty_yvterm_grid_clear_dirty", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def grid_slot_count(obj: Any) -> _rt.Result[int]:
    """Call `yetty_yvterm_grid_slot_count`."""
    _fn = _rt.cfn("yetty_yvterm_grid_slot_count", _t.yetty_ycore_uint32_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def grid_slot_cells(obj: Any, slot: int) -> _rt.Result[Any]:
    """Call `yetty_yvterm_grid_slot_cells`."""
    _fn = _rt.cfn("yetty_yvterm_grid_slot_cells", _t.yetty_yvterm_text_cell_const_ptr_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), slot)
    return _rt.result_from_c(res)

def grid_slot_dirty(obj: Any, slot: int) -> _rt.Result[int]:
    """Call `yetty_yvterm_grid_slot_dirty`."""
    _fn = _rt.cfn("yetty_yvterm_grid_slot_dirty", _t.yetty_ycore_int_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), slot)
    return _rt.result_from_c(res)

def grid_slot_rich_coverage(obj: Any, slot: int) -> _rt.Result[int]:
    """Call `yetty_yvterm_grid_slot_rich_coverage`."""
    _fn = _rt.cfn("yetty_yvterm_grid_slot_rich_coverage", _t.yetty_ycore_uint32_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), slot)
    return _rt.result_from_c(res)

def vterm_figure_create(cols: int, rows: int, context: Any, sink: Any) -> _rt.Result[Any]:
    """Call `yetty_yvterm_vterm_figure_create`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_figure_create", _t.yetty_yclass_object_ptr_result, [c_uint32, c_uint32, c_void_p, c_void_p])
    res = _fn(cols, rows, _rt.handle(context), _rt.handle(sink))
    return _rt.result_from_c(res)

def vterm_as_figure(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_yvterm_vterm_as_figure`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_as_figure", _t.yetty_yfigure_figure_ptr_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def vterm_feed(obj: Any, bytes: str | bytes | None, len: int) -> _rt.Result[None]:
    """Call `yetty_yvterm_vterm_feed`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_feed", _t.yetty_ycore_void_result, [c_void_p, c_char_p, c_size_t])
    res = _fn(_rt.handle(obj), _rt.cstr(bytes), len)
    return _rt.result_from_c(res)

def vterm_resize(obj: Any, grid_size: _t.yetty_ycore_grid_size, cell_size: _t.yetty_ycore_pixel_size) -> _rt.Result[None]:
    """Call `yetty_yvterm_vterm_resize`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_resize", _t.yetty_ycore_void_result, [c_void_p, _t.yetty_ycore_grid_size, _t.yetty_ycore_pixel_size])
    res = _fn(_rt.handle(obj), grid_size, cell_size)
    return _rt.result_from_c(res)

def vterm_set_content_scale(obj: Any, content_scale: float) -> _rt.Result[None]:
    """Call `yetty_yvterm_vterm_set_content_scale`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_set_content_scale", _t.yetty_ycore_void_result, [c_void_p, c_float])
    res = _fn(_rt.handle(obj), content_scale)
    return _rt.result_from_c(res)

def vterm_cell_size(obj: Any) -> _rt.Result[int]:
    """Call `yetty_yvterm_vterm_cell_size`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_cell_size", _t.pixel_size_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def vterm_rich_density(obj: Any, out_density: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_vterm_rich_density`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_rich_density", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(out_density))
    return _rt.result_from_c(res)

def vterm_is_dirty(obj: Any) -> _rt.Result[int]:
    """Call `yetty_yvterm_vterm_is_dirty`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_is_dirty", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def vterm_set_content_rect(obj: Any, x: float, y: float, width: float, height: float) -> _rt.Result[None]:
    """Call `yetty_yvterm_vterm_set_content_rect`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_set_content_rect", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float, c_float, c_float])
    res = _fn(_rt.handle(obj), x, y, width, height)
    return _rt.result_from_c(res)

def vterm_get_content_rect(obj: Any, out_x: Any, out_y: Any, out_width: Any, out_height: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_vterm_get_content_rect`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_get_content_rect", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(out_x), _rt.handle(out_y), _rt.handle(out_width), _rt.handle(out_height))
    return _rt.result_from_c(res)

def vterm_set_clear_hook(obj: Any, fn: Any, userdata: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_vterm_set_clear_hook`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_set_clear_hook", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(fn), _rt.handle(userdata))
    return _rt.result_from_c(res)

def vterm_set_reset_hook(obj: Any, fn: Any, userdata: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_vterm_set_reset_hook`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_set_reset_hook", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(fn), _rt.handle(userdata))
    return _rt.result_from_c(res)

def vterm_set_materialize(obj: Any, fn: Any, userdata: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_vterm_set_materialize`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_set_materialize", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(fn), _rt.handle(userdata))
    return _rt.result_from_c(res)

def vterm_cursor(obj: Any, out_row: Any, out_col: Any, out_visible: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_vterm_cursor`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_cursor", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(out_row), _rt.handle(out_col), _rt.handle(out_visible))
    return _rt.result_from_c(res)

def vterm_word_bounds(obj: Any, row: int, col: int, out_start_col: Any, out_end_col: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_vterm_word_bounds`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_word_bounds", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_uint32, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), row, col, _rt.handle(out_start_col), _rt.handle(out_end_col))
    return _rt.result_from_c(res)

def vterm_scroll_origin(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_yvterm_vterm_scroll_origin`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_scroll_origin", _t.yetty_ycore_uint64_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def vterm_append_primitive(obj: Any, row: int, words: Any, word_count: int) -> _rt.Result[int]:
    """Call `yetty_yvterm_vterm_append_primitive`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_append_primitive", _t.yetty_ycore_uint32_result, [c_void_p, c_uint32, c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), row, _rt.handle(words), word_count)
    return _rt.result_from_c(res)

def vterm_append_primitive_extent(obj: Any, row: int, words: Any, word_count: int, content_top_px: float, content_bottom_px: float) -> _rt.Result[int]:
    """Call `yetty_yvterm_vterm_append_primitive_extent`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_append_primitive_extent", _t.yetty_ycore_uint32_result, [c_void_p, c_uint32, c_void_p, c_uint32, c_float, c_float])
    res = _fn(_rt.handle(obj), row, _rt.handle(words), word_count, content_top_px, content_bottom_px)
    return _rt.result_from_c(res)

def vterm_rich_group_open(obj: Any, row: int, group_key: int) -> _rt.Result[int]:
    """Call `yetty_yvterm_vterm_rich_group_open`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_rich_group_open", _t.yetty_ycore_uint32_result, [c_void_p, c_uint32, c_uint64])
    res = _fn(_rt.handle(obj), row, group_key)
    return _rt.result_from_c(res)

def vterm_rich_group_close(obj: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_vterm_rich_group_close`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_rich_group_close", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def vterm_rich_group_delete(obj: Any, group_key: int) -> _rt.Result[None]:
    """Call `yetty_yvterm_vterm_rich_group_delete`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_rich_group_delete", _t.yetty_ycore_void_result, [c_void_p, c_uint64])
    res = _fn(_rt.handle(obj), group_key)
    return _rt.result_from_c(res)

def vterm_rich_group_query(obj: Any, group_key: int, out_span_rows: Any) -> _rt.Result[int]:
    """Call `yetty_yvterm_vterm_rich_group_query`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_rich_group_query", _t.yetty_ycore_uint32_result, [c_void_p, c_uint64, c_void_p])
    res = _fn(_rt.handle(obj), group_key, _rt.handle(out_span_rows))
    return _rt.result_from_c(res)

def vterm_rich_group_token(obj: Any, group_key: int) -> _rt.Result[Any]:
    """Call `yetty_yvterm_vterm_rich_group_token`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_rich_group_token", _t.yetty_ycore_uint64_result, [c_void_p, c_uint64])
    res = _fn(_rt.handle(obj), group_key)
    return _rt.result_from_c(res)

def vterm_rich_push_paint_z(obj: Any, z: int) -> _rt.Result[None]:
    """Call `yetty_yvterm_vterm_rich_push_paint_z`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_rich_push_paint_z", _t.yetty_ycore_void_result, [c_void_p, c_int32])
    res = _fn(_rt.handle(obj), z)
    return _rt.result_from_c(res)

def vterm_rich_pop_paint_z(obj: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_vterm_rich_pop_paint_z`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_rich_pop_paint_z", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def vterm_rich_reset_paint_z(obj: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_vterm_rich_reset_paint_z`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_rich_reset_paint_z", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def vterm_paint_plan_leaf_count(obj: Any) -> _rt.Result[int]:
    """Call `yetty_yvterm_vterm_paint_plan_leaf_count`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_paint_plan_leaf_count", _t.yetty_ycore_uint32_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def vterm_paint_plan_leaf(obj: Any, leaf_index: int, out_block_slot: Any, out_record_index: Any, out_kind: Any, out_paint_z: Any, out_paint_sequence: Any, out_record_ordinal: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_vterm_paint_plan_leaf`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_paint_plan_leaf", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_void_p, c_void_p, c_void_p, c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), leaf_index, _rt.handle(out_block_slot), _rt.handle(out_record_index), _rt.handle(out_kind), _rt.handle(out_paint_z), _rt.handle(out_paint_sequence), _rt.handle(out_record_ordinal))
    return _rt.result_from_c(res)

def vterm_paint_plan_build_count(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_yvterm_vterm_paint_plan_build_count`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_paint_plan_build_count", _t.yetty_ycore_uint64_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def vterm_rich_update_bind(obj: Any, update_key: int) -> _rt.Result[None]:
    """Call `yetty_yvterm_vterm_rich_update_bind`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_rich_update_bind", _t.yetty_ycore_void_result, [c_void_p, c_uint64])
    res = _fn(_rt.handle(obj), update_key)
    return _rt.result_from_c(res)

def vterm_rich_group_offset_set(obj: Any, group_key: int, offset_x: float, offset_y: float) -> _rt.Result[None]:
    """Call `yetty_yvterm_vterm_rich_group_offset_set`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_rich_group_offset_set", _t.yetty_ycore_void_result, [c_void_p, c_uint64, c_float, c_float])
    res = _fn(_rt.handle(obj), group_key, offset_x, offset_y)
    return _rt.result_from_c(res)

def vterm_rich_update_target(obj: Any, update_key: int, out_complex: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_vterm_rich_update_target`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_rich_update_target", _t.yetty_ycore_void_result, [c_void_p, c_uint64, c_void_p])
    res = _fn(_rt.handle(obj), update_key, _rt.handle(out_complex))
    return _rt.result_from_c(res)

def vterm_rich_update_journal(obj: Any, update_key: int, target_field: int, payload_words: Any, payload_word_count: int) -> _rt.Result[None]:
    """Call `yetty_yvterm_vterm_rich_update_journal`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_rich_update_journal", _t.yetty_ycore_void_result, [c_void_p, c_uint64, c_uint32, c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), update_key, target_field, _rt.handle(payload_words), payload_word_count)
    return _rt.result_from_c(res)

def vterm_rich_update_journal_poison(obj: Any, update_key: int) -> _rt.Result[None]:
    """Call `yetty_yvterm_vterm_rich_update_journal_poison`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_rich_update_journal_poison", _t.yetty_ycore_void_result, [c_void_p, c_uint64])
    res = _fn(_rt.handle(obj), update_key)
    return _rt.result_from_c(res)

def vterm_grid_object(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_yvterm_vterm_grid_object`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_grid_object", _t.yetty_yclass_object_ptr_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def vterm_rich_update_extent_refresh(obj: Any, update_key: int, content_top_px: float, content_bottom_px: float) -> _rt.Result[None]:
    """Call `yetty_yvterm_vterm_rich_update_extent_refresh`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_rich_update_extent_refresh", _t.yetty_ycore_void_result, [c_void_p, c_uint64, c_float, c_float])
    res = _fn(_rt.handle(obj), update_key, content_top_px, content_bottom_px)
    return _rt.result_from_c(res)

def vterm_rich_update_paint_z(obj: Any, update_key: int, out_paint_z: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_vterm_rich_update_paint_z`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_rich_update_paint_z", _t.yetty_ycore_void_result, [c_void_p, c_uint64, c_void_p])
    res = _fn(_rt.handle(obj), update_key, _rt.handle(out_paint_z))
    return _rt.result_from_c(res)

def vterm_rich_group_reserve(obj: Any, group_key: int, extra_records: int, extra_words: int) -> _rt.Result[None]:
    """Call `yetty_yvterm_vterm_rich_group_reserve`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_rich_group_reserve", _t.yetty_ycore_void_result, [c_void_p, c_uint64, c_uint32, c_uint32])
    res = _fn(_rt.handle(obj), group_key, extra_records, extra_words)
    return _rt.result_from_c(res)

def vterm_attach_complex(obj: Any, row: int, complex: Any) -> _rt.Result[int]:
    """Call `yetty_yvterm_vterm_attach_complex`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_attach_complex", _t.yetty_ycore_uint32_result, [c_void_p, c_uint32, c_void_p])
    res = _fn(_rt.handle(obj), row, _rt.handle(complex))
    return _rt.result_from_c(res)

def vterm_rich_group_clip_set(obj: Any, group_key: int, clip_x: float, clip_y: float, clip_w: float, clip_h: float) -> _rt.Result[None]:
    """Call `yetty_yvterm_vterm_rich_group_clip_set`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_rich_group_clip_set", _t.yetty_ycore_void_result, [c_void_p, c_uint64, c_float, c_float, c_float, c_float])
    res = _fn(_rt.handle(obj), group_key, clip_x, clip_y, clip_w, clip_h)
    return _rt.result_from_c(res)

def vterm_rich_span_declare(obj: Any, span_rows: int) -> _rt.Result[int]:
    """Call `yetty_yvterm_vterm_rich_span_declare`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_rich_span_declare", _t.yetty_ycore_uint32_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), span_rows)
    return _rt.result_from_c(res)

def vterm_rich_reserve_advance(obj: Any, advance_rows: int) -> _rt.Result[None]:
    """Call `yetty_yvterm_vterm_rich_reserve_advance`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_rich_reserve_advance", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), advance_rows)
    return _rt.result_from_c(res)

def vterm_rich_batch_abort(obj: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_vterm_rich_batch_abort`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_rich_batch_abort", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def vterm_relocate_rich_to_bottom(obj: Any, span_rows: int) -> _rt.Result[None]:
    """Call `yetty_yvterm_vterm_relocate_rich_to_bottom`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_relocate_rich_to_bottom", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), span_rows)
    return _rt.result_from_c(res)

def vterm_clear_rich_line(obj: Any, row: int) -> _rt.Result[None]:
    """Call `yetty_yvterm_vterm_clear_rich_line`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_clear_rich_line", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), row)
    return _rt.result_from_c(res)

def vterm_clear_rich_all(obj: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_vterm_clear_rich_all`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_clear_rich_all", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def vterm_register_wire(obj: Any, sm: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_vterm_register_wire`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_register_wire", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(sm))
    return _rt.result_from_c(res)

def vterm_on_char(obj: Any, codepoint: int, mods: int) -> _rt.Result[int]:
    """Call `yetty_yvterm_vterm_on_char`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_on_char", _t.yetty_ycore_int_result, [c_void_p, c_uint32, c_int])
    res = _fn(_rt.handle(obj), codepoint, mods)
    return _rt.result_from_c(res)

def vterm_on_key(obj: Any, key: int, mods: int) -> _rt.Result[int]:
    """Call `yetty_yvterm_vterm_on_key`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_on_key", _t.yetty_ycore_int_result, [c_void_p, c_int, c_int])
    res = _fn(_rt.handle(obj), key, mods)
    return _rt.result_from_c(res)

def vterm_set_selection(obj: Any, active: int, anchor_row: int, anchor_col: int, head_row: int, head_col: int) -> _rt.Result[None]:
    """Call `yetty_yvterm_vterm_set_selection`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_set_selection", _t.yetty_ycore_void_result, [c_void_p, c_int, c_uint32, c_uint32, c_uint32, c_uint32])
    res = _fn(_rt.handle(obj), active, anchor_row, anchor_col, head_row, head_col)
    return _rt.result_from_c(res)

def vterm_get_selection_text(obj: Any, out: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_vterm_get_selection_text`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_get_selection_text", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(out))
    return _rt.result_from_c(res)

def vterm_get_live_anchor(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_yvterm_vterm_get_live_anchor`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_get_live_anchor", _t.yetty_ycore_uint64_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def vterm_get_scrollback_floor(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_yvterm_vterm_get_scrollback_floor`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_get_scrollback_floor", _t.yetty_ycore_uint64_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def vterm_set_view_top(obj: Any, active: int, view_top_total_idx: int) -> _rt.Result[None]:
    """Call `yetty_yvterm_vterm_set_view_top`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_set_view_top", _t.yetty_ycore_void_result, [c_void_p, c_int, c_uint64])
    res = _fn(_rt.handle(obj), active, view_top_total_idx)
    return _rt.result_from_c(res)

def vterm_get_view(obj: Any, out_active: Any, out_view_top: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_vterm_get_view`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_get_view", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(out_active), _rt.handle(out_view_top))
    return _rt.result_from_c(res)

def vterm_set_visual_zoom(obj: Any, scale: float, offset_x: float, offset_y: float) -> _rt.Result[None]:
    """Call `yetty_yvterm_vterm_set_visual_zoom`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_set_visual_zoom", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float, c_float])
    res = _fn(_rt.handle(obj), scale, offset_x, offset_y)
    return _rt.result_from_c(res)

def vterm_set_post_effect(obj: Any, index: int, p0: float, p1: float, p2: float, p3: float, p4: float, p5: float) -> _rt.Result[None]:
    """Call `yetty_yvterm_vterm_set_post_effect`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_set_post_effect", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_float, c_float, c_float, c_float, c_float, c_float])
    res = _fn(_rt.handle(obj), index, p0, p1, p2, p3, p4, p5)
    return _rt.result_from_c(res)

def vterm_set_coord_effect(obj: Any, index: int, p0: float, p1: float, p2: float, p3: float, p4: float, p5: float) -> _rt.Result[None]:
    """Call `yetty_yvterm_vterm_set_coord_effect`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_set_coord_effect", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_float, c_float, c_float, c_float, c_float, c_float])
    res = _fn(_rt.handle(obj), index, p0, p1, p2, p3, p4, p5)
    return _rt.result_from_c(res)

def vterm_set_mouse(obj: Any, x: float, y: float) -> _rt.Result[None]:
    """Call `yetty_yvterm_vterm_set_mouse`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_set_mouse", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float])
    res = _fn(_rt.handle(obj), x, y)
    return _rt.result_from_c(res)
