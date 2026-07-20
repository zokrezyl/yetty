"""yetty.ynotebook bindings — GENERATED from model.yaml, do not edit."""
from __future__ import annotations
from ctypes import (c_bool, c_char, c_char_p, c_double, c_float, c_int,
    c_int8, c_int16, c_int32, c_int64, c_long, c_size_t, c_ssize_t,
    c_uint, c_uint8, c_uint16, c_uint32, c_uint64, c_ulong, c_void_p)
from typing import Any, ClassVar
from .. import runtime as _rt
from . import _types as _t

class MimeBundle(_rt.YClass):
    """yclass ynotebook:mime_bundle"""
    __yclass_domain__: ClassVar[str] = 'ynotebook'
    __yclass_name__: ClassVar[str] = 'mime_bundle'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ynotebook_mime_bundle_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ynotebook_mime_bundle_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if res.error is not None:
                raise _rt.YettyError(res.error.message)
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls, **kwargs: Any) -> 'MimeBundle':
        obj = cls()
        for _key, _value in kwargs.items():
            _setter = getattr(obj, "set_" + _key, None)
            if _setter is None:
                raise TypeError(f"MimeBundle.create: unknown property {_key!r}")
            _setter(*_value) if isinstance(_value, (tuple, list)) else _setter(_value)
        return obj
    def mime_bundle_from_json_text(self, data_json: str | bytes | None, metadata_json: str | bytes | None) -> None:
        """Call `yetty_ynotebook_mime_bundle_from_json_text`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ynotebook_mime_bundle_from_json_text", _t.yetty_ycore_void_result, [c_void_p, c_char_p, c_char_p])
        res = _rt.result_from_c(_fn(self._handle, _rt.cstr(data_json), _rt.cstr(metadata_json)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def mime_bundle_to_json_text(self) -> str | None:
        """Call `yetty_ynotebook_mime_bundle_to_json_text`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ynotebook_mime_bundle_to_json_text", _t.yetty_ycore_char_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle), _rt.decode_cstr)
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def mime_bundle_count(self) -> int:
        """Call `yetty_ynotebook_mime_bundle_count`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ynotebook_mime_bundle_count", _t.yetty_ycore_size_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def mime_bundle_mime_at(self, index: int) -> str | None:
        """Call `yetty_ynotebook_mime_bundle_mime_at`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ynotebook_mime_bundle_mime_at", _t.yetty_ycore_const_char_ptr_result, [c_void_p, c_size_t])
        res = _rt.result_from_c(_fn(self._handle, index), _rt.decode_cstr)
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def mime_bundle_kind_at(self, index: int) -> str | None:
        """Call `yetty_ynotebook_mime_bundle_kind_at`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ynotebook_mime_bundle_kind_at", _t.yetty_ycore_const_char_ptr_result, [c_void_p, c_size_t])
        res = _rt.result_from_c(_fn(self._handle, index), _rt.decode_cstr)
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def mime_bundle_bytes_at(self, index: int, out_bytes: Any, out_len: Any) -> None:
        """Call `yetty_ynotebook_mime_bundle_bytes_at`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ynotebook_mime_bundle_bytes_at", _t.yetty_ycore_void_result, [c_void_p, c_size_t, c_void_p, c_void_p])
        res = _rt.result_from_c(_fn(self._handle, index, _rt.handle(out_bytes), _rt.handle(out_len)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def mime_bundle_json_at(self, index: int) -> str | None:
        """Call `yetty_ynotebook_mime_bundle_json_at`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ynotebook_mime_bundle_json_at", _t.yetty_ycore_char_ptr_result, [c_void_p, c_size_t])
        res = _rt.result_from_c(_fn(self._handle, index), _rt.decode_cstr)
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def mime_bundle_destroy(self) -> None:
        """Call `yetty_ynotebook_mime_bundle_destroy`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ynotebook_mime_bundle_destroy", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value

class Output(_rt.YClass):
    """yclass ynotebook:output"""
    __yclass_domain__: ClassVar[str] = 'ynotebook'
    __yclass_name__: ClassVar[str] = 'output'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ynotebook_output_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ynotebook_output_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if res.error is not None:
                raise _rt.YettyError(res.error.message)
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Output':
        obj = cls()
        for _key, _value in kwargs.items():
            _setter = getattr(obj, "set_" + _key, None)
            if _setter is None:
                raise TypeError(f"Output.create: unknown property {_key!r}")
            _setter(*_value) if isinstance(_value, (tuple, list)) else _setter(_value)
        return obj
    def output_type(self) -> str | None:
        """Call `yetty_ynotebook_output_type`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ynotebook_output_type", _t.yetty_ycore_const_char_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle), _rt.decode_cstr)
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def output_stream_name(self) -> str | None:
        """Call `yetty_ynotebook_output_stream_name`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ynotebook_output_stream_name", _t.yetty_ycore_const_char_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle), _rt.decode_cstr)
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def output_text(self) -> str | None:
        """Call `yetty_ynotebook_output_text`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ynotebook_output_text", _t.yetty_ycore_char_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle), _rt.decode_cstr)
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def output_execution_count(self) -> int:
        """Call `yetty_ynotebook_output_execution_count`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ynotebook_output_execution_count", _t.yetty_ycore_int_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def output_error_name(self) -> str | None:
        """Call `yetty_ynotebook_output_error_name`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ynotebook_output_error_name", _t.yetty_ycore_const_char_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle), _rt.decode_cstr)
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def output_error_value(self) -> str | None:
        """Call `yetty_ynotebook_output_error_value`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ynotebook_output_error_value", _t.yetty_ycore_const_char_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle), _rt.decode_cstr)
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def output_bundle(self) -> Any:
        """Call `yetty_ynotebook_output_bundle`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ynotebook_output_bundle", _t.yetty_yclass_object_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def output_destroy(self) -> None:
        """Call `yetty_ynotebook_output_destroy`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ynotebook_output_destroy", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value

class Cell(_rt.YClass):
    """yclass ynotebook:cell"""
    __yclass_domain__: ClassVar[str] = 'ynotebook'
    __yclass_name__: ClassVar[str] = 'cell'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ynotebook_cell_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ynotebook_cell_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if res.error is not None:
                raise _rt.YettyError(res.error.message)
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Cell':
        obj = cls()
        for _key, _value in kwargs.items():
            _setter = getattr(obj, "set_" + _key, None)
            if _setter is None:
                raise TypeError(f"Cell.create: unknown property {_key!r}")
            _setter(*_value) if isinstance(_value, (tuple, list)) else _setter(_value)
        return obj
    def cell_type(self) -> str | None:
        """Call `yetty_ynotebook_cell_type`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ynotebook_cell_type", _t.yetty_ycore_const_char_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle), _rt.decode_cstr)
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def cell_id(self) -> str | None:
        """Call `yetty_ynotebook_cell_id`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ynotebook_cell_id", _t.yetty_ycore_const_char_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle), _rt.decode_cstr)
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def cell_source(self) -> str | None:
        """Call `yetty_ynotebook_cell_source`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ynotebook_cell_source", _t.yetty_ycore_char_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle), _rt.decode_cstr)
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def cell_set_source(self, text: str | bytes | None) -> None:
        """Call `yetty_ynotebook_cell_set_source`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ynotebook_cell_set_source", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
        res = _rt.result_from_c(_fn(self._handle, _rt.cstr(text)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def cell_execution_count(self) -> int:
        """Call `yetty_ynotebook_cell_execution_count`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ynotebook_cell_execution_count", _t.yetty_ycore_int_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def cell_output_count(self) -> int:
        """Call `yetty_ynotebook_cell_output_count`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ynotebook_cell_output_count", _t.yetty_ycore_size_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def cell_output_at(self, index: int) -> Any:
        """Call `yetty_ynotebook_cell_output_at`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ynotebook_cell_output_at", _t.yetty_yclass_object_ptr_result, [c_void_p, c_size_t])
        res = _rt.result_from_c(_fn(self._handle, index))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def cell_metadata_json(self) -> str | None:
        """Call `yetty_ynotebook_cell_metadata_json`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ynotebook_cell_metadata_json", _t.yetty_ycore_char_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle), _rt.decode_cstr)
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def cell_apply_message(self, msg_type: str | bytes | None, content_json: str | bytes | None) -> None:
        """Call `yetty_ynotebook_cell_apply_message`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ynotebook_cell_apply_message", _t.yetty_ycore_void_result, [c_void_p, c_char_p, c_char_p])
        res = _rt.result_from_c(_fn(self._handle, _rt.cstr(msg_type), _rt.cstr(content_json)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def cell_destroy(self) -> None:
        """Call `yetty_ynotebook_cell_destroy`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ynotebook_cell_destroy", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value

class Notebook(_rt.YClass):
    """yclass ynotebook:notebook"""
    __yclass_domain__: ClassVar[str] = 'ynotebook'
    __yclass_name__: ClassVar[str] = 'notebook'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ynotebook_notebook_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None) -> None:
        if _handle is None:
            _fn = _rt.cfn("yetty_ynotebook_notebook_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
            res = _rt.result_from_c(_fn(None))
            if res.error is not None:
                raise _rt.YettyError(res.error.message)
            _handle = res.value
        super().__init__(_handle)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Notebook':
        obj = cls()
        for _key, _value in kwargs.items():
            _setter = getattr(obj, "set_" + _key, None)
            if _setter is None:
                raise TypeError(f"Notebook.create: unknown property {_key!r}")
            _setter(*_value) if isinstance(_value, (tuple, list)) else _setter(_value)
        return obj
    def notebook_load_text(self, json: str | bytes | None) -> None:
        """Call `yetty_ynotebook_notebook_load_text`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ynotebook_notebook_load_text", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
        res = _rt.result_from_c(_fn(self._handle, _rt.cstr(json)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def notebook_load_file(self, path: str | bytes | None) -> None:
        """Call `yetty_ynotebook_notebook_load_file`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ynotebook_notebook_load_file", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
        res = _rt.result_from_c(_fn(self._handle, _rt.cstr(path)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def notebook_to_text(self) -> str | None:
        """Call `yetty_ynotebook_notebook_to_text`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ynotebook_notebook_to_text", _t.yetty_ycore_char_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle), _rt.decode_cstr)
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def notebook_save_file(self, path: str | bytes | None) -> None:
        """Call `yetty_ynotebook_notebook_save_file`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ynotebook_notebook_save_file", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
        res = _rt.result_from_c(_fn(self._handle, _rt.cstr(path)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def notebook_nbformat(self) -> int:
        """Call `yetty_ynotebook_notebook_nbformat`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ynotebook_notebook_nbformat", _t.yetty_ycore_int_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def notebook_nbformat_minor(self) -> int:
        """Call `yetty_ynotebook_notebook_nbformat_minor`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ynotebook_notebook_nbformat_minor", _t.yetty_ycore_int_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def notebook_cell_count(self) -> int:
        """Call `yetty_ynotebook_notebook_cell_count`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ynotebook_notebook_cell_count", _t.yetty_ycore_size_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def notebook_cell_at(self, index: int) -> Any:
        """Call `yetty_ynotebook_notebook_cell_at`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ynotebook_notebook_cell_at", _t.yetty_yclass_object_ptr_result, [c_void_p, c_size_t])
        res = _rt.result_from_c(_fn(self._handle, index))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def notebook_metadata_json(self) -> str | None:
        """Call `yetty_ynotebook_notebook_metadata_json`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ynotebook_notebook_metadata_json", _t.yetty_ycore_char_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle), _rt.decode_cstr)
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def notebook_destroy(self) -> None:
        """Call `yetty_ynotebook_notebook_destroy`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ynotebook_notebook_destroy", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value

