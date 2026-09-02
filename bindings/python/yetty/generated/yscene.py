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
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_yscene_scene_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(None))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._apply_kwargs(kwargs)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Scene':
        return cls(**kwargs)
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
        res = _rt.result_from_c(_fn(self._handle, external_id, _rt.as_buffer(content)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def node_append_batch(self, external_id: int, content: _t.yetty_ycore_buffer) -> None:
        """Call `yetty_yscene_node_append_batch`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yscene_node_append_batch", _t.yetty_ycore_void_result, [c_void_p, c_uint64, _t.yetty_ycore_buffer])
        res = _rt.result_from_c(_fn(self._handle, external_id, _rt.as_buffer(content)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def node_replace_batch(self, external_id: int, batch_index: int, content: _t.yetty_ycore_buffer) -> None:
        """Call `yetty_yscene_node_replace_batch`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yscene_node_replace_batch", _t.yetty_ycore_void_result, [c_void_p, c_uint64, c_uint32, _t.yetty_ycore_buffer])
        res = _rt.result_from_c(_fn(self._handle, external_id, batch_index, _rt.as_buffer(content)))
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
    def layout_barrier_begin(self) -> None:
        """Call `yetty_yscene_layout_barrier_begin`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yscene_layout_barrier_begin", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def layout_barrier_end(self) -> None:
        """Call `yetty_yscene_layout_barrier_end`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yscene_layout_barrier_end", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def terminal_grid_generation(self) -> int:
        """Call `yetty_yscene_terminal_grid_generation`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yscene_terminal_grid_generation", _t.yetty_ycore_uint32_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def terminal_grid_create(self, rows: int, cols: int, cell_width: float, cell_height: float) -> None:
        """Call `yetty_yscene_terminal_grid_create`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yscene_terminal_grid_create", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_uint32, c_float, c_float])
        res = _rt.result_from_c(_fn(self._handle, rows, cols, cell_width, cell_height))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def terminal_grid_write(self, bytes: _t.yetty_ycore_buffer) -> None:
        """Call `yetty_yscene_terminal_grid_write`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yscene_terminal_grid_write", _t.yetty_ycore_void_result, [c_void_p, _t.yetty_ycore_buffer])
        res = _rt.result_from_c(_fn(self._handle, _rt.as_buffer(bytes)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def terminal_write_content(self, vt: _t.yetty_ycore_buffer, rich: _t.yetty_ycore_buffer) -> None:
        """Call `yetty_yscene_terminal_write_content`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yscene_terminal_write_content", _t.yetty_ycore_void_result, [c_void_p, _t.yetty_ycore_buffer, _t.yetty_ycore_buffer])
        res = _rt.result_from_c(_fn(self._handle, _rt.as_buffer(vt), _rt.as_buffer(rich)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def terminal_grid_resize(self, rows: int, cols: int) -> None:
        """Call `yetty_yscene_terminal_grid_resize`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yscene_terminal_grid_resize", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, rows, cols))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def terminal_reply_pending(self) -> int:
        """Call `yetty_yscene_terminal_reply_pending`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yscene_terminal_reply_pending", _t.yetty_ycore_uint32_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def terminal_reply_word(self, word_index: int) -> Any:
        """Call `yetty_yscene_terminal_reply_word`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yscene_terminal_reply_word", _t.yetty_ycore_uint64_result, [c_void_p, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, word_index))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def terminal_reply_consume(self, byte_count: int) -> None:
        """Call `yetty_yscene_terminal_reply_consume`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yscene_terminal_reply_consume", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, byte_count))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def input_event_head(self) -> Any:
        """Call `yetty_yscene_input_event_head`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yscene_input_event_head", _t.yetty_ycore_uint64_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def input_event_word(self, word_index: int) -> Any:
        """Call `yetty_yscene_input_event_word`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yscene_input_event_word", _t.yetty_ycore_uint64_result, [c_void_p, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, word_index))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def input_event_pop(self) -> None:
        """Call `yetty_yscene_input_event_pop`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yscene_input_event_pop", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def dispatch_key(self, input_class: int, bytes: _t.yetty_ycore_buffer) -> int:
        """Call `yetty_yscene_dispatch_key`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yscene_dispatch_key", _t.yetty_ycore_uint32_result, [c_void_p, c_uint32, _t.yetty_ycore_buffer])
        res = _rt.result_from_c(_fn(self._handle, input_class, _rt.as_buffer(bytes)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def note_key_intake(self, input_class: int, bytes: _t.yetty_ycore_buffer) -> None:
        """Call `yetty_yscene_note_key_intake`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yscene_note_key_intake", _t.yetty_ycore_void_result, [c_void_p, c_uint32, _t.yetty_ycore_buffer])
        res = _rt.result_from_c(_fn(self._handle, input_class, _rt.as_buffer(bytes)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def set_terminal_selection(self, start_row: int, start_col: int, end_row: int, end_col: int, active: int) -> None:
        """Call `yetty_yscene_set_terminal_selection`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yscene_set_terminal_selection", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_uint32, c_uint32, c_uint32, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, start_row, start_col, end_row, end_col, active))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def dispatch_pointer(self, local_x: int, local_y: int, kind: int, button: int, mods: int, pressed: int) -> Any:
        """Call `yetty_yscene_dispatch_pointer`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yscene_dispatch_pointer", _t.yetty_ycore_uint64_result, [c_void_p, c_uint32, c_uint32, c_uint32, c_uint32, c_uint32, c_uint32])
        res = _rt.result_from_c(_fn(self._handle, local_x, local_y, kind, button, mods, pressed))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def apply_content_transaction(self, rich: _t.yetty_ycore_buffer) -> None:
        """Call `yetty_yscene_apply_content_transaction`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yscene_apply_content_transaction", _t.yetty_ycore_void_result, [c_void_p, _t.yetty_ycore_buffer])
        res = _rt.result_from_c(_fn(self._handle, _rt.as_buffer(rich)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value

class Vtermgrid(_rt.YClass):
    """yclass yscene:vtermgrid"""
    __yclass_domain__: ClassVar[str] = 'yscene'
    __yclass_name__: ClassVar[str] = 'vtermgrid'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_yscene_vtermgrid_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_yscene_vtermgrid_make", _t.yetty_yclass_object_ptr_result, [])
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
    def create(cls, **kwargs: Any) -> 'Vtermgrid':
        return cls(**kwargs)
    def destroy(self) -> None:
        """Dispose via `yetty_yscene_vtermgrid_dispose`; idempotent."""
        if self._handle is None:
            return
        handle, self._handle = self._handle, None
        _fn = _rt.cfn("yetty_yscene_vtermgrid_dispose", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    close = destroy

def scene_terminal_grid_create(obj: Any, rows: int, cols: int, cell_width: float, cell_height: float) -> _rt.Result[None]:
    """Call `yetty_yscene_scene_terminal_grid_create`."""
    _fn = _rt.cfn("yetty_yscene_scene_terminal_grid_create", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_uint32, c_float, c_float])
    res = _fn(_rt.handle(obj), rows, cols, cell_width, cell_height)
    return _rt.result_from_c(res)

def scene_terminal_grid_write(obj: Any, bytes: Any, len: int) -> _rt.Result[None]:
    """Call `yetty_yscene_scene_terminal_grid_write`."""
    _fn = _rt.cfn("yetty_yscene_scene_terminal_grid_write", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_size_t])
    res = _fn(_rt.handle(obj), _rt.handle(bytes), len)
    return _rt.result_from_c(res)

def scene_terminal_write_content(obj: Any, vt_bytes: Any, vt_len: int, rich_words: Any, rich_word_count: int) -> _rt.Result[None]:
    """Call `yetty_yscene_scene_terminal_write_content`."""
    _fn = _rt.cfn("yetty_yscene_scene_terminal_write_content", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_size_t, c_void_p, c_size_t])
    res = _fn(_rt.handle(obj), _rt.handle(vt_bytes), vt_len, _rt.handle(rich_words), rich_word_count)
    return _rt.result_from_c(res)

def scene_terminal_grid_resize(obj: Any, rows: int, cols: int) -> _rt.Result[None]:
    """Call `yetty_yscene_scene_terminal_grid_resize`."""
    _fn = _rt.cfn("yetty_yscene_scene_terminal_grid_resize", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_uint32])
    res = _fn(_rt.handle(obj), rows, cols)
    return _rt.result_from_c(res)

def scene_terminal_grid(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_yscene_scene_terminal_grid`."""
    _fn = _rt.cfn("yetty_yscene_scene_terminal_grid", _t.yetty_yclass_object_ptr_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def create(rect: _t.yetty_ycore_rectangle, context: Any) -> _rt.Result[Any]:
    """Call `yetty_yscene_create`."""
    _fn = _rt.cfn("yetty_yscene_create", _t.yetty_yscene_scene_ptr_result, [_t.yetty_ycore_rectangle, c_void_p])
    res = _fn(rect, _rt.handle(context))
    return _rt.result_from_c(res)

def scene_rich_fault_arm(obj: Any, countdown: int) -> _rt.Result[None]:
    """Call `yetty_yscene_scene_rich_fault_arm`."""
    _fn = _rt.cfn("yetty_yscene_scene_rich_fault_arm", _t.yetty_ycore_void_result, [c_void_p, c_int])
    res = _fn(_rt.handle(obj), countdown)
    return _rt.result_from_c(res)

def scene_apply_content_transaction(obj: Any, rich_words: Any, rich_word_count: int) -> _rt.Result[None]:
    """Call `yetty_yscene_scene_apply_content_transaction`."""
    _fn = _rt.cfn("yetty_yscene_scene_apply_content_transaction", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_size_t])
    res = _fn(_rt.handle(obj), _rt.handle(rich_words), rich_word_count)
    return _rt.result_from_c(res)

def scene_key_event_serial(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_yscene_scene_key_event_serial`."""
    _fn = _rt.cfn("yetty_yscene_scene_key_event_serial", _t.yetty_ycore_uint64_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def scene_take_input_event(obj: Any, out_class: Any, out_bytes: Any, out_capacity: int) -> _rt.Result[int]:
    """Call `yetty_yscene_scene_take_input_event`."""
    _fn = _rt.cfn("yetty_yscene_scene_take_input_event", _t.yetty_ycore_int_result, [c_void_p, c_void_p, c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), _rt.handle(out_class), _rt.handle(out_bytes), out_capacity)
    return _rt.result_from_c(res)

def scene_key_focus(obj: Any) -> _rt.Result[int]:
    """Call `yetty_yscene_scene_key_focus`."""
    _fn = _rt.cfn("yetty_yscene_scene_key_focus", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def scene_rich_entry_count(obj: Any) -> _rt.Result[int]:
    """Call `yetty_yscene_scene_rich_entry_count`."""
    _fn = _rt.cfn("yetty_yscene_scene_rich_entry_count", _t.yetty_ycore_uint32_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def scene_set_default_font(obj: Any, font: Any) -> _rt.Result[None]:
    """Call `yetty_yscene_scene_set_default_font`."""
    _fn = _rt.cfn("yetty_yscene_scene_set_default_font", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(font))
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

def set_complex_factory(obj: Any, factory: Any) -> _rt.Result[None]:
    """Call `yetty_yscene_set_complex_factory`."""
    _fn = _rt.cfn("yetty_yscene_set_complex_factory", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
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

def scene_dispatch_pointer(obj: Any, local_x: int, local_y: int, kind: int, button: int, mods: int, pressed: int) -> _rt.Result[Any]:
    """Call `yetty_yscene_scene_dispatch_pointer`."""
    _fn = _rt.cfn("yetty_yscene_scene_dispatch_pointer", _t.yetty_ycore_uint64_result, [c_void_p, c_uint32, c_uint32, c_uint32, c_uint32, c_uint32, c_uint32])
    res = _fn(_rt.handle(obj), local_x, local_y, kind, button, mods, pressed)
    return _rt.result_from_c(res)

def scene_pointer_event_serial(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_yscene_scene_pointer_event_serial`."""
    _fn = _rt.cfn("yetty_yscene_scene_pointer_event_serial", _t.yetty_ycore_uint64_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def render_plan(obj: Any) -> _rt.Result[str | None]:
    """Call `yetty_yscene_render_plan`."""
    _fn = _rt.cfn("yetty_yscene_render_plan", _t.yetty_ycore_char_ptr_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res, _rt.take_owned_cstr)

def set_view_scale(obj: Any, view_scale: float) -> _rt.Result[None]:
    """Call `yetty_yscene_set_view_scale`."""
    _fn = _rt.cfn("yetty_yscene_set_view_scale", _t.yetty_ycore_void_result, [c_void_p, c_float])
    res = _fn(_rt.handle(obj), view_scale)
    return _rt.result_from_c(res)

def vtermgrid_make(rows: int, cols: int) -> _rt.Result[Any]:
    """Call `yetty_yscene_vtermgrid_make`."""
    _fn = _rt.cfn("yetty_yscene_vtermgrid_make", _t.yetty_yclass_object_ptr_result, [c_uint32, c_uint32])
    res = _fn(rows, cols)
    return _rt.result_from_c(res)

def vtermgrid_dispose(obj: Any) -> _rt.Result[None]:
    """Call `yetty_yscene_vtermgrid_dispose`."""
    _fn = _rt.cfn("yetty_yscene_vtermgrid_dispose", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    if hasattr(obj, '_handle'):
        obj._handle = None  # consumed: no dangling wrapper
    return _rt.result_from_c(res)

def vtermgrid_write(obj: Any, bytes: Any, len: int) -> _rt.Result[None]:
    """Call `yetty_yscene_vtermgrid_write`."""
    _fn = _rt.cfn("yetty_yscene_vtermgrid_write", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_size_t])
    res = _fn(_rt.handle(obj), _rt.handle(bytes), len)
    return _rt.result_from_c(res)

def vtermgrid_reset(obj: Any, rows: int, cols: int) -> _rt.Result[None]:
    """Call `yetty_yscene_vtermgrid_reset`."""
    _fn = _rt.cfn("yetty_yscene_vtermgrid_reset", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_uint32])
    res = _fn(_rt.handle(obj), rows, cols)
    return _rt.result_from_c(res)

def vtermgrid_resize(obj: Any, rows: int, cols: int) -> _rt.Result[None]:
    """Call `yetty_yscene_vtermgrid_resize`."""
    _fn = _rt.cfn("yetty_yscene_vtermgrid_resize", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_uint32])
    res = _fn(_rt.handle(obj), rows, cols)
    return _rt.result_from_c(res)

def vtermgrid_dims(obj: Any, rows: Any, cols: Any) -> _rt.Result[None]:
    """Call `yetty_yscene_vtermgrid_dims`."""
    _fn = _rt.cfn("yetty_yscene_vtermgrid_dims", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(rows), _rt.handle(cols))
    return _rt.result_from_c(res)

def vtermgrid_generation(obj: Any) -> _rt.Result[int]:
    """Call `yetty_yscene_vtermgrid_generation`."""
    _fn = _rt.cfn("yetty_yscene_vtermgrid_generation", _t.yetty_ycore_uint32_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def vtermgrid_cursor(obj: Any, row: Any, col: Any, visible: Any) -> _rt.Result[None]:
    """Call `yetty_yscene_vtermgrid_cursor`."""
    _fn = _rt.cfn("yetty_yscene_vtermgrid_cursor", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(row), _rt.handle(col), _rt.handle(visible))
    return _rt.result_from_c(res)

def vtermgrid_cell(obj: Any, row: int, col: int, out: Any) -> _rt.Result[None]:
    """Call `yetty_yscene_vtermgrid_cell`."""
    _fn = _rt.cfn("yetty_yscene_vtermgrid_cell", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_uint32, c_void_p])
    res = _fn(_rt.handle(obj), row, col, _rt.handle(out))
    return _rt.result_from_c(res)

def vtermgrid_gpu_setup(obj: Any, runtime: Any, font: Any) -> _rt.Result[None]:
    """Call `yetty_yscene_vtermgrid_gpu_setup`."""
    _fn = _rt.cfn("yetty_yscene_vtermgrid_gpu_setup", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(runtime), _rt.handle(font))
    return _rt.result_from_c(res)

def vtermgrid_gpu_set_fallback_font(obj: Any, fallback_font: Any) -> _rt.Result[None]:
    """Call `yetty_yscene_vtermgrid_gpu_set_fallback_font`."""
    _fn = _rt.cfn("yetty_yscene_vtermgrid_gpu_set_fallback_font", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(fallback_font))
    return _rt.result_from_c(res)

def vtermgrid_mode_flags(obj: Any) -> _rt.Result[int]:
    """Call `yetty_yscene_vtermgrid_mode_flags`."""
    _fn = _rt.cfn("yetty_yscene_vtermgrid_mode_flags", _t.yetty_ycore_uint32_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def vtermgrid_reply_pending(obj: Any) -> _rt.Result[int]:
    """Call `yetty_yscene_vtermgrid_reply_pending`."""
    _fn = _rt.cfn("yetty_yscene_vtermgrid_reply_pending", _t.yetty_ycore_uint32_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def vtermgrid_reply_word(obj: Any, word_index: int) -> _rt.Result[Any]:
    """Call `yetty_yscene_vtermgrid_reply_word`."""
    _fn = _rt.cfn("yetty_yscene_vtermgrid_reply_word", _t.yetty_ycore_uint64_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), word_index)
    return _rt.result_from_c(res)

def vtermgrid_reply_consume(obj: Any, byte_count: int) -> _rt.Result[None]:
    """Call `yetty_yscene_vtermgrid_reply_consume`."""
    _fn = _rt.cfn("yetty_yscene_vtermgrid_reply_consume", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), byte_count)
    return _rt.result_from_c(res)

def vtermgrid_take_replies(obj: Any, out: Any, out_capacity: int) -> _rt.Result[int]:
    """Call `yetty_yscene_vtermgrid_take_replies`."""
    _fn = _rt.cfn("yetty_yscene_vtermgrid_take_replies", _t.yetty_ycore_uint32_result, [c_void_p, c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), _rt.handle(out), out_capacity)
    return _rt.result_from_c(res)

def vtermgrid_on_alt_screen(obj: Any) -> _rt.Result[int]:
    """Call `yetty_yscene_vtermgrid_on_alt_screen`."""
    _fn = _rt.cfn("yetty_yscene_vtermgrid_on_alt_screen", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def vtermgrid_set_selection(obj: Any, start_row: int, start_col: int, end_row: int, end_col: int, active: int) -> _rt.Result[None]:
    """Call `yetty_yscene_vtermgrid_set_selection`."""
    _fn = _rt.cfn("yetty_yscene_vtermgrid_set_selection", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_uint32, c_uint32, c_uint32, c_int])
    res = _fn(_rt.handle(obj), start_row, start_col, end_row, end_col, active)
    return _rt.result_from_c(res)

def vtermgrid_render(obj: Any, target: Any, rect: _t.yetty_ycore_rectangle) -> _rt.Result[None]:
    """Call `yetty_yscene_vtermgrid_render`."""
    _fn = _rt.cfn("yetty_yscene_vtermgrid_render", _t.yetty_ycore_void_result, [c_void_p, c_void_p, _t.yetty_ycore_rectangle])
    res = _fn(_rt.handle(obj), _rt.handle(target), rect)
    return _rt.result_from_c(res)
