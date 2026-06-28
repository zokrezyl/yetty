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
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_yvterm_grid_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Grid']:
        obj = cls()
        return obj.init_result

class Vterm(_yfigure.Figure):
    """yclass yvterm:vterm"""
    __yclass_domain__: ClassVar[str] = 'yvterm'
    __yclass_name__: ClassVar[str] = 'vterm'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_yvterm_vterm_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_yvterm_vterm_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if not res:
                _rt.YClass.__init__(self, None, res.error)
                return
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls) -> _rt.Result['Vterm']:
        obj = cls()
        return obj.init_result

def grid_make(cols: int, rows: int) -> _rt.Result[Any]:
    """Call `yetty_yvterm_grid_make`."""
    _fn = _rt.cfn("yetty_yvterm_grid_make", _t.yetty_yclass_object_ptr_result, [c_uint32, c_uint32])
    res = _fn(cols, rows)
    return _rt.result_from_c(res)

def grid_dispose(obj: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_dispose`."""
    _fn = _rt.cfn("yetty_yvterm_grid_dispose", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def grid_set_pty_write(obj: Any, fn: Any, userdata: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_set_pty_write`."""
    _fn = _rt.cfn("yetty_yvterm_grid_set_pty_write", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(fn), _rt.handle(userdata))
    return _rt.result_from_c(res)

def grid_set_clear_hook(obj: Any, fn: Any, userdata: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_set_clear_hook`."""
    _fn = _rt.cfn("yetty_yvterm_grid_set_clear_hook", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(fn), _rt.handle(userdata))
    return _rt.result_from_c(res)

def grid_set_card_sub(obj: Any, fn: Any, userdata: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_set_card_sub`."""
    _fn = _rt.cfn("yetty_yvterm_grid_set_card_sub", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(fn), _rt.handle(userdata))
    return _rt.result_from_c(res)

def grid_feed(obj: Any, bytes: str | bytes | None, len: int) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_feed`."""
    _fn = _rt.cfn("yetty_yvterm_grid_feed", _t.yetty_ycore_void_result, [c_void_p, c_char_p, c_size_t])
    res = _fn(_rt.handle(obj), _rt.cstr(bytes), len)
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

def grid_scroll_origin(obj: Any) -> _rt.Result[int]:
    """Call `yetty_yvterm_grid_scroll_origin`."""
    _fn = _rt.cfn("yetty_yvterm_grid_scroll_origin", _t.yetty_ycore_uint32_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def grid_append_primitive(obj: Any, row: int, words: Any, word_count: int) -> _rt.Result[int]:
    """Call `yetty_yvterm_grid_append_primitive`."""
    _fn = _rt.cfn("yetty_yvterm_grid_append_primitive", _t.yetty_ycore_uint32_result, [c_void_p, c_uint32, c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), row, _rt.handle(words), word_count)
    return _rt.result_from_c(res)

def grid_attach_composite(obj: Any, row: int, composite: Any) -> _rt.Result[int]:
    """Call `yetty_yvterm_grid_attach_composite`."""
    _fn = _rt.cfn("yetty_yvterm_grid_attach_composite", _t.yetty_ycore_uint32_result, [c_void_p, c_uint32, c_void_p])
    res = _fn(_rt.handle(obj), row, _rt.handle(composite))
    return _rt.result_from_c(res)

def grid_relocate_rich_to_bottom(obj: Any, span_rows: int) -> _rt.Result[None]:
    """Call `yetty_yvterm_grid_relocate_rich_to_bottom`."""
    _fn = _rt.cfn("yetty_yvterm_grid_relocate_rich_to_bottom", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), span_rows)
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

def grid_line_composites(obj: Any, row: int, out_count: Any) -> _rt.Result[Any]:
    """Call `yetty_yvterm_grid_line_composites`."""
    _fn = _rt.cfn("yetty_yvterm_grid_line_composites", _t.yetty_ydraw_composite_const_ptr_ptr_result, [c_void_p, c_uint32, c_void_p])
    res = _fn(_rt.handle(obj), row, _rt.handle(out_count))
    return _rt.result_from_c(res)

def grid_slot_composites(obj: Any, slot: int, out_count: Any) -> _rt.Result[Any]:
    """Call `yetty_yvterm_grid_slot_composites`."""
    _fn = _rt.cfn("yetty_yvterm_grid_slot_composites", _t.yetty_ydraw_composite_const_ptr_ptr_result, [c_void_p, c_uint32, c_void_p])
    res = _fn(_rt.handle(obj), slot, _rt.handle(out_count))
    return _rt.result_from_c(res)

def grid_slot_primitive_count(obj: Any, slot: int) -> _rt.Result[int]:
    """Call `yetty_yvterm_grid_slot_primitive_count`."""
    _fn = _rt.cfn("yetty_yvterm_grid_slot_primitive_count", _t.yetty_ycore_uint32_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), slot)
    return _rt.result_from_c(res)

def grid_slot_span(obj: Any, slot: int) -> _rt.Result[int]:
    """Call `yetty_yvterm_grid_slot_span`."""
    _fn = _rt.cfn("yetty_yvterm_grid_slot_span", _t.yetty_ycore_uint32_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), slot)
    return _rt.result_from_c(res)

def grid_slot_primitive_words(obj: Any, slot: int, index: int, out_word_count: Any) -> _rt.Result[Any]:
    """Call `yetty_yvterm_grid_slot_primitive_words`."""
    _fn = _rt.cfn("yetty_yvterm_grid_slot_primitive_words", _t.yetty_ycore_const_uint32_ptr_result, [c_void_p, c_uint32, c_uint32, c_void_p])
    res = _fn(_rt.handle(obj), slot, index, _rt.handle(out_word_count))
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

def vterm_figure_create(cols: int, rows: int, context: Any, pty_write_fn: Any, pty_write_userdata: Any, request_render_fn: Any, request_render_userdata: Any, mouse_sub_fn: Any, mouse_sub_userdata: Any) -> _rt.Result[Any]:
    """Call `yetty_yvterm_vterm_figure_create`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_figure_create", _t.yetty_yclass_object_ptr_result, [c_uint32, c_uint32, c_void_p, c_void_p, c_void_p, c_void_p, c_void_p, c_void_p, c_void_p])
    res = _fn(cols, rows, _rt.handle(context), _rt.handle(pty_write_fn), _rt.handle(pty_write_userdata), _rt.handle(request_render_fn), _rt.handle(request_render_userdata), _rt.handle(mouse_sub_fn), _rt.handle(mouse_sub_userdata))
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

def vterm_cell_size(obj: Any) -> _rt.Result[int]:
    """Call `yetty_yvterm_vterm_cell_size`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_cell_size", _t.pixel_size_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def vterm_is_dirty(obj: Any) -> _rt.Result[int]:
    """Call `yetty_yvterm_vterm_is_dirty`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_is_dirty", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def vterm_set_content_inset(obj: Any, top: float, right: float, bottom: float, left: float) -> _rt.Result[None]:
    """Call `yetty_yvterm_vterm_set_content_inset`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_set_content_inset", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float, c_float, c_float])
    res = _fn(_rt.handle(obj), top, right, bottom, left)
    return _rt.result_from_c(res)

def vterm_get_content_inset(obj: Any, out_top: Any, out_right: Any, out_bottom: Any, out_left: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_vterm_get_content_inset`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_get_content_inset", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(out_top), _rt.handle(out_right), _rt.handle(out_bottom), _rt.handle(out_left))
    return _rt.result_from_c(res)

def vterm_set_clear_hook(obj: Any, fn: Any, userdata: Any) -> _rt.Result[None]:
    """Call `yetty_yvterm_vterm_set_clear_hook`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_set_clear_hook", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
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

def vterm_scroll_origin(obj: Any) -> _rt.Result[int]:
    """Call `yetty_yvterm_vterm_scroll_origin`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_scroll_origin", _t.yetty_ycore_uint32_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def vterm_append_primitive(obj: Any, row: int, words: Any, word_count: int) -> _rt.Result[int]:
    """Call `yetty_yvterm_vterm_append_primitive`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_append_primitive", _t.yetty_ycore_uint32_result, [c_void_p, c_uint32, c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), row, _rt.handle(words), word_count)
    return _rt.result_from_c(res)

def vterm_attach_composite(obj: Any, row: int, composite: Any) -> _rt.Result[int]:
    """Call `yetty_yvterm_vterm_attach_composite`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_attach_composite", _t.yetty_ycore_uint32_result, [c_void_p, c_uint32, c_void_p])
    res = _fn(_rt.handle(obj), row, _rt.handle(composite))
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

def vterm_get_live_anchor(obj: Any) -> _rt.Result[int]:
    """Call `yetty_yvterm_vterm_get_live_anchor`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_get_live_anchor", _t.yetty_ycore_uint32_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def vterm_get_scrollback_floor(obj: Any) -> _rt.Result[int]:
    """Call `yetty_yvterm_vterm_get_scrollback_floor`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_get_scrollback_floor", _t.yetty_ycore_uint32_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def vterm_set_view_top(obj: Any, active: int, view_top_total_idx: int) -> _rt.Result[None]:
    """Call `yetty_yvterm_vterm_set_view_top`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_set_view_top", _t.yetty_ycore_void_result, [c_void_p, c_int, c_uint32])
    res = _fn(_rt.handle(obj), active, view_top_total_idx)
    return _rt.result_from_c(res)

def vterm_set_visual_zoom(obj: Any, scale: float, offset_x: float, offset_y: float) -> _rt.Result[None]:
    """Call `yetty_yvterm_vterm_set_visual_zoom`."""
    _fn = _rt.cfn("yetty_yvterm_vterm_set_visual_zoom", _t.yetty_ycore_void_result, [c_void_p, c_float, c_float, c_float])
    res = _fn(_rt.handle(obj), scale, offset_x, offset_y)
    return _rt.result_from_c(res)

