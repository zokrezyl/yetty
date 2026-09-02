"""yetty.ymux bindings — GENERATED from model.yaml, do not edit."""
from __future__ import annotations
from ctypes import (c_bool, c_char, c_char_p, c_double, c_float, c_int,
    c_int8, c_int16, c_int32, c_int64, c_long, c_size_t, c_ssize_t,
    c_uint, c_uint8, c_uint16, c_uint32, c_uint64, c_ulong, c_void_p)
from typing import Any, ClassVar
from .. import runtime as _rt
from . import _types as _t

class Attachment(_rt.YClass):
    """yclass ymux:attachment"""
    __yclass_domain__: ClassVar[str] = 'ymux'
    __yclass_name__: ClassVar[str] = 'attachment'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ymux_attachment_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_ymux_attachment_make", _t.yetty_yclass_object_ptr_result, [])
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
    def create(cls, **kwargs: Any) -> 'Attachment':
        return cls(**kwargs)
    def destroy(self) -> None:
        """Dispose via `yetty_ymux_attachment_dispose`; idempotent."""
        if self._handle is None:
            return
        handle, self._handle = self._handle, None
        _fn = _rt.cfn("yetty_ymux_attachment_dispose", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    close = destroy

class Client(_rt.YClass):
    """yclass ymux:client"""
    __yclass_domain__: ClassVar[str] = 'ymux'
    __yclass_name__: ClassVar[str] = 'client'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ymux_client_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_ymux_client_make", _t.yetty_yclass_object_ptr_result, [])
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
    def create(cls, **kwargs: Any) -> 'Client':
        return cls(**kwargs)
    def destroy(self) -> None:
        """Dispose via `yetty_ymux_client_dispose`; idempotent."""
        if self._handle is None:
            return
        handle, self._handle = self._handle, None
        _fn = _rt.cfn("yetty_ymux_client_dispose", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    close = destroy

class Daemon(_rt.YClass):
    """yclass ymux:daemon"""
    __yclass_domain__: ClassVar[str] = 'ymux'
    __yclass_name__: ClassVar[str] = 'daemon'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ymux_daemon_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_ymux_daemon_make", _t.yetty_yclass_object_ptr_result, [])
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
    def create(cls, **kwargs: Any) -> 'Daemon':
        return cls(**kwargs)
    def destroy(self) -> None:
        """Dispose via `yetty_ymux_daemon_dispose`; idempotent."""
        if self._handle is None:
            return
        handle, self._handle = self._handle, None
        _fn = _rt.cfn("yetty_ymux_daemon_dispose", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    close = destroy

class Engine(_rt.YClass):
    """yclass ymux:engine"""
    __yclass_domain__: ClassVar[str] = 'ymux'
    __yclass_name__: ClassVar[str] = 'engine'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ymux_engine_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_ymux_engine_make", _t.yetty_yclass_object_ptr_result, [])
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
    def create(cls, **kwargs: Any) -> 'Engine':
        return cls(**kwargs)
    def destroy(self) -> None:
        """Dispose via `yetty_ymux_engine_dispose`; idempotent."""
        if self._handle is None:
            return
        handle, self._handle = self._handle, None
        _fn = _rt.cfn("yetty_ymux_engine_dispose", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    close = destroy

class History(_rt.YClass):
    """yclass ymux:history"""
    __yclass_domain__: ClassVar[str] = 'ymux'
    __yclass_name__: ClassVar[str] = 'history'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ymux_history_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_ymux_history_make", _t.yetty_yclass_object_ptr_result, [])
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
    def create(cls, **kwargs: Any) -> 'History':
        return cls(**kwargs)
    def destroy(self) -> None:
        """Dispose via `yetty_ymux_history_dispose`; idempotent."""
        if self._handle is None:
            return
        handle, self._handle = self._handle, None
        _fn = _rt.cfn("yetty_ymux_history_dispose", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    close = destroy

class Pane(_rt.YClass):
    """yclass ymux:pane"""
    __yclass_domain__: ClassVar[str] = 'ymux'
    __yclass_name__: ClassVar[str] = 'pane'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ymux_pane_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_ymux_pane_make", _t.yetty_yclass_object_ptr_result, [])
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
    def create(cls, **kwargs: Any) -> 'Pane':
        return cls(**kwargs)
    def destroy(self) -> None:
        """Dispose via `yetty_ymux_pane_dispose`; idempotent."""
        if self._handle is None:
            return
        handle, self._handle = self._handle, None
        _fn = _rt.cfn("yetty_ymux_pane_dispose", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    close = destroy

class Projector(_rt.YClass):
    """yclass ymux:projector"""
    __yclass_domain__: ClassVar[str] = 'ymux'
    __yclass_name__: ClassVar[str] = 'projector'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ymux_projector_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_ymux_projector_make", _t.yetty_yclass_object_ptr_result, [])
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
    def create(cls, **kwargs: Any) -> 'Projector':
        return cls(**kwargs)
    def destroy(self) -> None:
        """Dispose via `yetty_ymux_projector_dispose`; idempotent."""
        if self._handle is None:
            return
        handle, self._handle = self._handle, None
        _fn = _rt.cfn("yetty_ymux_projector_dispose", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    close = destroy

class Rich(_rt.YClass):
    """yclass ymux:rich"""
    __yclass_domain__: ClassVar[str] = 'ymux'
    __yclass_name__: ClassVar[str] = 'rich'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ymux_rich_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_ymux_rich_make", _t.yetty_yclass_object_ptr_result, [])
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
    def create(cls, **kwargs: Any) -> 'Rich':
        return cls(**kwargs)
    def destroy(self) -> None:
        """Dispose via `yetty_ymux_rich_dispose`; idempotent."""
        if self._handle is None:
            return
        handle, self._handle = self._handle, None
        _fn = _rt.cfn("yetty_ymux_rich_dispose", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    close = destroy

class Session(_rt.YClass):
    """yclass ymux:session"""
    __yclass_domain__: ClassVar[str] = 'ymux'
    __yclass_name__: ClassVar[str] = 'session'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ymux_session_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_ymux_session_make", _t.yetty_yclass_object_ptr_result, [])
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
    def create(cls, **kwargs: Any) -> 'Session':
        return cls(**kwargs)
    def destroy(self) -> None:
        """Dispose via `yetty_ymux_session_dispose`; idempotent."""
        if self._handle is None:
            return
        handle, self._handle = self._handle, None
        _fn = _rt.cfn("yetty_ymux_session_dispose", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
    close = destroy

class Vtsink(_rt.YClass):
    """yclass ymux:vtsink"""
    __yclass_domain__: ClassVar[str] = 'ymux'
    __yclass_name__: ClassVar[str] = 'vtsink'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ymux_vtsink_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_ymux_vtsink_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(None))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._apply_kwargs(kwargs)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Vtsink':
        return cls(**kwargs)
    def feed(self, generation: int, bytes: _t.yetty_ycore_buffer) -> None:
        """Call `yetty_ymux_feed`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ymux_feed", _t.yetty_ycore_void_result, [c_void_p, c_uint64, _t.yetty_ycore_buffer])
        res = _rt.result_from_c(_fn(self._handle, generation, _rt.as_buffer(bytes)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value

def attachment_make(pane: Any, view_rows: int, view_cols: int) -> _rt.Result[Any]:
    """Call `yetty_ymux_attachment_make`."""
    _fn = _rt.cfn("yetty_ymux_attachment_make", _t.yetty_yclass_object_ptr_result, [c_void_p, c_uint32, c_uint32])
    res = _fn(_rt.handle(pane), view_rows, view_cols)
    return _rt.result_from_c(res)

def attachment_dispose(obj: Any) -> _rt.Result[None]:
    """Call `yetty_ymux_attachment_dispose`."""
    _fn = _rt.cfn("yetty_ymux_attachment_dispose", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    if hasattr(obj, '_handle'):
        obj._handle = None  # consumed: no dangling wrapper
    return _rt.result_from_c(res)

def attachment_set_view_size(obj: Any, view_rows: int, view_cols: int) -> _rt.Result[None]:
    """Call `yetty_ymux_attachment_set_view_size`."""
    _fn = _rt.cfn("yetty_ymux_attachment_set_view_size", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_uint32])
    res = _fn(_rt.handle(obj), view_rows, view_cols)
    return _rt.result_from_c(res)

def attachment_follow(obj: Any) -> _rt.Result[None]:
    """Call `yetty_ymux_attachment_follow`."""
    _fn = _rt.cfn("yetty_ymux_attachment_follow", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def attachment_anchor(obj: Any, timeline_idx: int) -> _rt.Result[None]:
    """Call `yetty_ymux_attachment_anchor`."""
    _fn = _rt.cfn("yetty_ymux_attachment_anchor", _t.yetty_ycore_void_result, [c_void_p, c_uint64])
    res = _fn(_rt.handle(obj), timeline_idx)
    return _rt.result_from_c(res)

def attachment_view_top(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_ymux_attachment_view_top`."""
    _fn = _rt.cfn("yetty_ymux_attachment_view_top", _t.yetty_ycore_uint64_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def attachment_is_following(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ymux_attachment_is_following`."""
    _fn = _rt.cfn("yetty_ymux_attachment_is_following", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def attachment_set_selection(obj: Any, anchor_logical_id: int, anchor_offset: int, head_logical_id: int, head_offset: int) -> _rt.Result[None]:
    """Call `yetty_ymux_attachment_set_selection`."""
    _fn = _rt.cfn("yetty_ymux_attachment_set_selection", _t.yetty_ycore_void_result, [c_void_p, c_uint64, c_uint32, c_uint64, c_uint32])
    res = _fn(_rt.handle(obj), anchor_logical_id, anchor_offset, head_logical_id, head_offset)
    return _rt.result_from_c(res)

def attachment_selection(obj: Any, out_anchor_logical_id: Any, out_anchor_offset: Any, out_head_logical_id: Any, out_head_offset: Any) -> _rt.Result[None]:
    """Call `yetty_ymux_attachment_selection`."""
    _fn = _rt.cfn("yetty_ymux_attachment_selection", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(out_anchor_logical_id), _rt.handle(out_anchor_offset), _rt.handle(out_head_logical_id), _rt.handle(out_head_offset))
    return _rt.result_from_c(res)

def attachment_next_generation(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_ymux_attachment_next_generation`."""
    _fn = _rt.cfn("yetty_ymux_attachment_next_generation", _t.yetty_ycore_uint64_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def attachment_ack(obj: Any, generation: int) -> _rt.Result[None]:
    """Call `yetty_ymux_attachment_ack`."""
    _fn = _rt.cfn("yetty_ymux_attachment_ack", _t.yetty_ycore_void_result, [c_void_p, c_uint64])
    res = _fn(_rt.handle(obj), generation)
    return _rt.result_from_c(res)

def attachment_generations(obj: Any, out_published: Any, out_acked: Any) -> _rt.Result[None]:
    """Call `yetty_ymux_attachment_generations`."""
    _fn = _rt.cfn("yetty_ymux_attachment_generations", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(out_published), _rt.handle(out_acked))
    return _rt.result_from_c(res)

def attachment_view_size(obj: Any, out_rows: Any, out_cols: Any) -> _rt.Result[None]:
    """Call `yetty_ymux_attachment_view_size`."""
    _fn = _rt.cfn("yetty_ymux_attachment_view_size", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(out_rows), _rt.handle(out_cols))
    return _rt.result_from_c(res)

def client_make(socket_path: str | bytes | None) -> _rt.Result[Any]:
    """Call `yetty_ymux_client_make`."""
    _fn = _rt.cfn("yetty_ymux_client_make", _t.yetty_yclass_object_ptr_result, [c_char_p])
    res = _fn(_rt.cstr(socket_path))
    return _rt.result_from_c(res)

def client_dispose(obj: Any) -> _rt.Result[None]:
    """Call `yetty_ymux_client_dispose`."""
    _fn = _rt.cfn("yetty_ymux_client_dispose", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    if hasattr(obj, '_handle'):
        obj._handle = None  # consumed: no dangling wrapper
    return _rt.result_from_c(res)

def client_set_terminal(obj: Any, term_name: str | bytes | None, features: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ymux_client_set_terminal`."""
    _fn = _rt.cfn("yetty_ymux_client_set_terminal", _t.yetty_ycore_void_result, [c_void_p, c_char_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(term_name), _rt.cstr(features))
    return _rt.result_from_c(res)

def client_attach(obj: Any, session_name: str | bytes | None, pane_id: int, view_rows: int, view_cols: int, cell_pixel_height: int, capabilities: int, token: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ymux_client_attach`."""
    _fn = _rt.cfn("yetty_ymux_client_attach", _t.yetty_ycore_void_result, [c_void_p, c_char_p, c_uint32, c_uint32, c_uint32, c_uint32, c_uint32, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(session_name), pane_id, view_rows, view_cols, cell_pixel_height, capabilities, _rt.cstr(token))
    return _rt.result_from_c(res)

def client_session_new(obj: Any, name: str | bytes | None, rows: int, cols: int) -> _rt.Result[None]:
    """Call `yetty_ymux_client_session_new`."""
    _fn = _rt.cfn("yetty_ymux_client_session_new", _t.yetty_ycore_void_result, [c_void_p, c_char_p, c_uint32, c_uint32])
    res = _fn(_rt.handle(obj), _rt.cstr(name), rows, cols)
    return _rt.result_from_c(res)

def client_session_list(obj: Any) -> _rt.Result[None]:
    """Call `yetty_ymux_client_session_list`."""
    _fn = _rt.cfn("yetty_ymux_client_session_list", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def client_session_has(obj: Any, name: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ymux_client_session_has`."""
    _fn = _rt.cfn("yetty_ymux_client_session_has", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(name))
    return _rt.result_from_c(res)

def client_session_kill(obj: Any, name: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ymux_client_session_kill`."""
    _fn = _rt.cfn("yetty_ymux_client_session_kill", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(name))
    return _rt.result_from_c(res)

def client_session_detach(obj: Any, name: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ymux_client_session_detach`."""
    _fn = _rt.cfn("yetty_ymux_client_session_detach", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(name))
    return _rt.result_from_c(res)

def client_session_send_keys(obj: Any, name: str | bytes | None, pairs: Any, pair_count: int) -> _rt.Result[None]:
    """Call `yetty_ymux_client_session_send_keys`."""
    _fn = _rt.cfn("yetty_ymux_client_session_send_keys", _t.yetty_ycore_void_result, [c_void_p, c_char_p, c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), _rt.cstr(name), _rt.handle(pairs), pair_count)
    return _rt.result_from_c(res)

def client_session_rename(obj: Any, old_name: str | bytes | None, new_name: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ymux_client_session_rename`."""
    _fn = _rt.cfn("yetty_ymux_client_session_rename", _t.yetty_ycore_void_result, [c_void_p, c_char_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(old_name), _rt.cstr(new_name))
    return _rt.result_from_c(res)

def client_session_reply(obj: Any, out_text: Any, out_status: Any) -> _rt.Result[Any]:
    """Call `yetty_ymux_client_session_reply`."""
    _fn = _rt.cfn("yetty_ymux_client_session_reply", _t.yetty_ycore_uint64_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(out_text), _rt.handle(out_status))
    return _rt.result_from_c(res)

def client_detach(obj: Any) -> _rt.Result[None]:
    """Call `yetty_ymux_client_detach`."""
    _fn = _rt.cfn("yetty_ymux_client_detach", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def client_input_char(obj: Any, codepoint: int, mods: int) -> _rt.Result[None]:
    """Call `yetty_ymux_client_input_char`."""
    _fn = _rt.cfn("yetty_ymux_client_input_char", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_int])
    res = _fn(_rt.handle(obj), codepoint, mods)
    return _rt.result_from_c(res)

def client_input_key(obj: Any, key: int, mods: int) -> _rt.Result[None]:
    """Call `yetty_ymux_client_input_key`."""
    _fn = _rt.cfn("yetty_ymux_client_input_key", _t.yetty_ycore_void_result, [c_void_p, c_int, c_int])
    res = _fn(_rt.handle(obj), key, mods)
    return _rt.result_from_c(res)

def client_input_mouse_move(obj: Any, row: int, col: int, mods: int) -> _rt.Result[None]:
    """Call `yetty_ymux_client_input_mouse_move`."""
    _fn = _rt.cfn("yetty_ymux_client_input_mouse_move", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_uint32, c_int])
    res = _fn(_rt.handle(obj), row, col, mods)
    return _rt.result_from_c(res)

def client_input_mouse_button(obj: Any, button: int, pressed: int, mods: int) -> _rt.Result[None]:
    """Call `yetty_ymux_client_input_mouse_button`."""
    _fn = _rt.cfn("yetty_ymux_client_input_mouse_button", _t.yetty_ycore_void_result, [c_void_p, c_int, c_int, c_int])
    res = _fn(_rt.handle(obj), button, pressed, mods)
    return _rt.result_from_c(res)

def client_input_paste(obj: Any, text: str | bytes | None, len: int) -> _rt.Result[None]:
    """Call `yetty_ymux_client_input_paste`."""
    _fn = _rt.cfn("yetty_ymux_client_input_paste", _t.yetty_ycore_void_result, [c_void_p, c_char_p, c_size_t])
    res = _fn(_rt.handle(obj), _rt.cstr(text), len)
    return _rt.result_from_c(res)

def client_resize(obj: Any, rows: int, cols: int) -> _rt.Result[None]:
    """Call `yetty_ymux_client_resize`."""
    _fn = _rt.cfn("yetty_ymux_client_resize", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_uint32])
    res = _fn(_rt.handle(obj), rows, cols)
    return _rt.result_from_c(res)

def client_scroll(obj: Any, delta: int) -> _rt.Result[None]:
    """Call `yetty_ymux_client_scroll`."""
    _fn = _rt.cfn("yetty_ymux_client_scroll", _t.yetty_ycore_void_result, [c_void_p, c_int])
    res = _fn(_rt.handle(obj), delta)
    return _rt.result_from_c(res)

def client_takeover(obj: Any) -> _rt.Result[None]:
    """Call `yetty_ymux_client_takeover`."""
    _fn = _rt.cfn("yetty_ymux_client_takeover", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def client_resync(obj: Any) -> _rt.Result[None]:
    """Call `yetty_ymux_client_resync`."""
    _fn = _rt.cfn("yetty_ymux_client_resync", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def client_send_tty_response(obj: Any, bytes: Any, byte_count: int) -> _rt.Result[None]:
    """Call `yetty_ymux_client_send_tty_response`."""
    _fn = _rt.cfn("yetty_ymux_client_send_tty_response", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), _rt.handle(bytes), byte_count)
    return _rt.result_from_c(res)

def client_send_overlay_input(obj: Any, sequence: int, input_class: int, bytes: Any, byte_count: int) -> _rt.Result[None]:
    """Call `yetty_ymux_client_send_overlay_input`."""
    _fn = _rt.cfn("yetty_ymux_client_send_overlay_input", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_uint32, c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), sequence, input_class, _rt.handle(bytes), byte_count)
    return _rt.result_from_c(res)

def client_overlay_input_nacked(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ymux_client_overlay_input_nacked`."""
    _fn = _rt.cfn("yetty_ymux_client_overlay_input_nacked", _t.yetty_ycore_uint32_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def client_chrome_release_count(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ymux_client_chrome_release_count`."""
    _fn = _rt.cfn("yetty_ymux_client_chrome_release_count", _t.yetty_ycore_uint32_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def client_request_recover(obj: Any) -> _rt.Result[None]:
    """Call `yetty_ymux_client_request_recover`."""
    _fn = _rt.cfn("yetty_ymux_client_request_recover", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def client_request_paste(obj: Any) -> _rt.Result[None]:
    """Call `yetty_ymux_client_request_paste`."""
    _fn = _rt.cfn("yetty_ymux_client_request_paste", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def client_shutdown_server(obj: Any) -> _rt.Result[None]:
    """Call `yetty_ymux_client_shutdown_server`."""
    _fn = _rt.cfn("yetty_ymux_client_shutdown_server", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def client_step(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ymux_client_step`."""
    _fn = _rt.cfn("yetty_ymux_client_step", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def client_ingest(obj: Any, bytes: str | bytes | None, len: int) -> _rt.Result[int]:
    """Call `yetty_ymux_client_ingest`."""
    _fn = _rt.cfn("yetty_ymux_client_ingest", _t.yetty_ycore_int_result, [c_void_p, c_char_p, c_size_t])
    res = _fn(_rt.handle(obj), _rt.cstr(bytes), len)
    return _rt.result_from_c(res)

def client_fd(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ymux_client_fd`."""
    _fn = _rt.cfn("yetty_ymux_client_fd", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def client_attached(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ymux_client_attached`."""
    _fn = _rt.cfn("yetty_ymux_client_attached", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def client_attachment_id(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ymux_client_attachment_id`."""
    _fn = _rt.cfn("yetty_ymux_client_attachment_id", _t.yetty_ycore_uint32_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def client_pane_id(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ymux_client_pane_id`."""
    _fn = _rt.cfn("yetty_ymux_client_pane_id", _t.yetty_ycore_uint32_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def client_permissions(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ymux_client_permissions`."""
    _fn = _rt.cfn("yetty_ymux_client_permissions", _t.yetty_ycore_uint32_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def client_last_refuse(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ymux_client_last_refuse`."""
    _fn = _rt.cfn("yetty_ymux_client_last_refuse", _t.yetty_ycore_uint32_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def client_pane_exited(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ymux_client_pane_exited`."""
    _fn = _rt.cfn("yetty_ymux_client_pane_exited", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def client_rich_body(obj: Any, out_word_count: Any) -> _rt.Result[Any]:
    """Call `yetty_ymux_client_rich_body`."""
    _fn = _rt.cfn("yetty_ymux_client_rich_body", _t.yetty_ycore_const_uint32_ptr_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(out_word_count))
    return _rt.result_from_c(res)

def client_rich_generation(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_ymux_client_rich_generation`."""
    _fn = _rt.cfn("yetty_ymux_client_rich_generation", _t.yetty_ycore_uint64_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def client_bell_count(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_ymux_client_bell_count`."""
    _fn = _rt.cfn("yetty_ymux_client_bell_count", _t.yetty_ycore_uint64_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def client_title(obj: Any, out: Any, out_cap: int) -> _rt.Result[Any]:
    """Call `yetty_ymux_client_title`."""
    _fn = _rt.cfn("yetty_ymux_client_title", _t.yetty_ycore_uint64_result, [c_void_p, c_void_p, c_size_t])
    res = _fn(_rt.handle(obj), _rt.handle(out), out_cap)
    return _rt.result_from_c(res)

def client_clipboard(obj: Any, out_text: Any, out_len: Any, out_target: Any) -> _rt.Result[Any]:
    """Call `yetty_ymux_client_clipboard`."""
    _fn = _rt.cfn("yetty_ymux_client_clipboard", _t.yetty_ycore_uint64_result, [c_void_p, c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(out_text), _rt.handle(out_len), _rt.handle(out_target))
    return _rt.result_from_c(res)

def daemon_make(socket_path: str | bytes | None, default_rows: int, default_cols: int, host: Any) -> _rt.Result[Any]:
    """Call `yetty_ymux_daemon_make`."""
    _fn = _rt.cfn("yetty_ymux_daemon_make", _t.yetty_yclass_object_ptr_result, [c_char_p, c_uint32, c_uint32, c_void_p])
    res = _fn(_rt.cstr(socket_path), default_rows, default_cols, _rt.handle(host))
    return _rt.result_from_c(res)

def daemon_dispose(obj: Any) -> _rt.Result[None]:
    """Call `yetty_ymux_daemon_dispose`."""
    _fn = _rt.cfn("yetty_ymux_daemon_dispose", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    if hasattr(obj, '_handle'):
        obj._handle = None  # consumed: no dangling wrapper
    return _rt.result_from_c(res)

def daemon_paste_buffer(obj: Any, out_bytes: Any, out_capacity: int) -> _rt.Result[int]:
    """Call `yetty_ymux_daemon_paste_buffer`."""
    _fn = _rt.cfn("yetty_ymux_daemon_paste_buffer", _t.yetty_ycore_uint32_result, [c_void_p, c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), _rt.handle(out_bytes), out_capacity)
    return _rt.result_from_c(res)

def daemon_chrome_intake(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_ymux_daemon_chrome_intake`."""
    _fn = _rt.cfn("yetty_ymux_daemon_chrome_intake", _t.yetty_ycore_uint64_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def daemon_chrome_last_event(obj: Any, out_bytes: Any, out_capacity: int) -> _rt.Result[Any]:
    """Call `yetty_ymux_daemon_chrome_last_event`."""
    _fn = _rt.cfn("yetty_ymux_daemon_chrome_last_event", _t.yetty_ycore_uint64_result, [c_void_p, c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), _rt.handle(out_bytes), out_capacity)
    return _rt.result_from_c(res)

def daemon_force_recover(obj: Any) -> _rt.Result[None]:
    """Call `yetty_ymux_daemon_force_recover`."""
    _fn = _rt.cfn("yetty_ymux_daemon_force_recover", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def daemon_fail_next_vtsink_tx(obj: Any, count: int) -> _rt.Result[None]:
    """Call `yetty_ymux_daemon_fail_next_vtsink_tx`."""
    _fn = _rt.cfn("yetty_ymux_daemon_fail_next_vtsink_tx", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), count)
    return _rt.result_from_c(res)

def daemon_step(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ymux_daemon_step`."""
    _fn = _rt.cfn("yetty_ymux_daemon_step", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def daemon_socket_path(obj: Any, out: Any, out_cap: int) -> _rt.Result[None]:
    """Call `yetty_ymux_daemon_socket_path`."""
    _fn = _rt.cfn("yetty_ymux_daemon_socket_path", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_size_t])
    res = _fn(_rt.handle(obj), _rt.handle(out), out_cap)
    return _rt.result_from_c(res)

def daemon_session(obj: Any, name: str | bytes | None) -> _rt.Result[Any]:
    """Call `yetty_ymux_daemon_session`."""
    _fn = _rt.cfn("yetty_ymux_daemon_session", _t.yetty_yclass_object_ptr_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(name))
    return _rt.result_from_c(res)

def daemon_shutdown_requested(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ymux_daemon_shutdown_requested`."""
    _fn = _rt.cfn("yetty_ymux_daemon_shutdown_requested", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def engine_make(rows: int, cols: int, host: Any) -> _rt.Result[Any]:
    """Call `yetty_ymux_engine_make`."""
    _fn = _rt.cfn("yetty_ymux_engine_make", _t.yetty_yclass_object_ptr_result, [c_uint32, c_uint32, c_void_p])
    res = _fn(rows, cols, _rt.handle(host))
    return _rt.result_from_c(res)

def engine_dispose(obj: Any) -> _rt.Result[None]:
    """Call `yetty_ymux_engine_dispose`."""
    _fn = _rt.cfn("yetty_ymux_engine_dispose", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    if hasattr(obj, '_handle'):
        obj._handle = None  # consumed: no dangling wrapper
    return _rt.result_from_c(res)

def engine_feed(obj: Any, bytes: str | bytes | None, len: int) -> _rt.Result[None]:
    """Call `yetty_ymux_engine_feed`."""
    _fn = _rt.cfn("yetty_ymux_engine_feed", _t.yetty_ycore_void_result, [c_void_p, c_char_p, c_size_t])
    res = _fn(_rt.handle(obj), _rt.cstr(bytes), len)
    return _rt.result_from_c(res)

def engine_set_cell_height(obj: Any, cell_pixel_height: int) -> _rt.Result[None]:
    """Call `yetty_ymux_engine_set_cell_height`."""
    _fn = _rt.cfn("yetty_ymux_engine_set_cell_height", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), cell_pixel_height)
    return _rt.result_from_c(res)

def engine_reserve_rich_rows(obj: Any, figure_pixel_height: int) -> _rt.Result[int]:
    """Call `yetty_ymux_engine_reserve_rich_rows`."""
    _fn = _rt.cfn("yetty_ymux_engine_reserve_rich_rows", _t.yetty_ycore_uint32_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), figure_pixel_height)
    return _rt.result_from_c(res)

def engine_resize(obj: Any, rows: int, cols: int) -> _rt.Result[None]:
    """Call `yetty_ymux_engine_resize`."""
    _fn = _rt.cfn("yetty_ymux_engine_resize", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_uint32])
    res = _fn(_rt.handle(obj), rows, cols)
    return _rt.result_from_c(res)

def engine_input_char(obj: Any, codepoint: int, mods: int) -> _rt.Result[None]:
    """Call `yetty_ymux_engine_input_char`."""
    _fn = _rt.cfn("yetty_ymux_engine_input_char", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_int])
    res = _fn(_rt.handle(obj), codepoint, mods)
    return _rt.result_from_c(res)

def engine_input_key(obj: Any, key: int, mods: int) -> _rt.Result[None]:
    """Call `yetty_ymux_engine_input_key`."""
    _fn = _rt.cfn("yetty_ymux_engine_input_key", _t.yetty_ycore_void_result, [c_void_p, c_int, c_int])
    res = _fn(_rt.handle(obj), key, mods)
    return _rt.result_from_c(res)

def engine_input_mouse_move(obj: Any, row: int, col: int, mods: int) -> _rt.Result[None]:
    """Call `yetty_ymux_engine_input_mouse_move`."""
    _fn = _rt.cfn("yetty_ymux_engine_input_mouse_move", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_uint32, c_int])
    res = _fn(_rt.handle(obj), row, col, mods)
    return _rt.result_from_c(res)

def engine_input_mouse_button(obj: Any, button: int, pressed: int, mods: int) -> _rt.Result[None]:
    """Call `yetty_ymux_engine_input_mouse_button`."""
    _fn = _rt.cfn("yetty_ymux_engine_input_mouse_button", _t.yetty_ycore_void_result, [c_void_p, c_int, c_int, c_int])
    res = _fn(_rt.handle(obj), button, pressed, mods)
    return _rt.result_from_c(res)

def engine_input_paste(obj: Any, text: str | bytes | None, len: int) -> _rt.Result[None]:
    """Call `yetty_ymux_engine_input_paste`."""
    _fn = _rt.cfn("yetty_ymux_engine_input_paste", _t.yetty_ycore_void_result, [c_void_p, c_char_p, c_size_t])
    res = _fn(_rt.handle(obj), _rt.cstr(text), len)
    return _rt.result_from_c(res)

def engine_set_palette_color(obj: Any, index: int, rgb: int) -> _rt.Result[None]:
    """Call `yetty_ymux_engine_set_palette_color`."""
    _fn = _rt.cfn("yetty_ymux_engine_set_palette_color", _t.yetty_ycore_void_result, [c_void_p, c_int, c_uint32])
    res = _fn(_rt.handle(obj), index, rgb)
    return _rt.result_from_c(res)

def engine_default_colors(obj: Any, out_fg: Any, out_bg: Any) -> _rt.Result[None]:
    """Call `yetty_ymux_engine_default_colors`."""
    _fn = _rt.cfn("yetty_ymux_engine_default_colors", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(out_fg), _rt.handle(out_bg))
    return _rt.result_from_c(res)

def engine_palette_color(obj: Any, index: int) -> _rt.Result[int]:
    """Call `yetty_ymux_engine_palette_color`."""
    _fn = _rt.cfn("yetty_ymux_engine_palette_color", _t.yetty_ycore_uint32_result, [c_void_p, c_int])
    res = _fn(_rt.handle(obj), index)
    return _rt.result_from_c(res)

def engine_set_default_colors(obj: Any, fg_rgb: int, bg_rgb: int) -> _rt.Result[None]:
    """Call `yetty_ymux_engine_set_default_colors`."""
    _fn = _rt.cfn("yetty_ymux_engine_set_default_colors", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_uint32])
    res = _fn(_rt.handle(obj), fg_rgb, bg_rgb)
    return _rt.result_from_c(res)

def engine_dims(obj: Any, out_rows: Any, out_cols: Any) -> _rt.Result[None]:
    """Call `yetty_ymux_engine_dims`."""
    _fn = _rt.cfn("yetty_ymux_engine_dims", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(out_rows), _rt.handle(out_cols))
    return _rt.result_from_c(res)

def engine_alt_active(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ymux_engine_alt_active`."""
    _fn = _rt.cfn("yetty_ymux_engine_alt_active", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def engine_cursor(obj: Any, out_row: Any, out_col: Any, out_visible: Any) -> _rt.Result[None]:
    """Call `yetty_ymux_engine_cursor`."""
    _fn = _rt.cfn("yetty_ymux_engine_cursor", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(out_row), _rt.handle(out_col), _rt.handle(out_visible))
    return _rt.result_from_c(res)

def engine_cursor_style(obj: Any, out_shape: Any, out_blink: Any) -> _rt.Result[None]:
    """Call `yetty_ymux_engine_cursor_style`."""
    _fn = _rt.cfn("yetty_ymux_engine_cursor_style", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(out_shape), _rt.handle(out_blink))
    return _rt.result_from_c(res)

def engine_exotic_colour(obj: Any, ref: int, out_text: Any, out_capacity: int) -> _rt.Result[int]:
    """Call `yetty_ymux_engine_exotic_colour`."""
    _fn = _rt.cfn("yetty_ymux_engine_exotic_colour", _t.yetty_ycore_uint32_result, [c_void_p, c_uint32, c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), ref, _rt.handle(out_text), out_capacity)
    return _rt.result_from_c(res)

def engine_exotic_link(obj: Any, ref: int, out_text: Any, out_capacity: int) -> _rt.Result[int]:
    """Call `yetty_ymux_engine_exotic_link`."""
    _fn = _rt.cfn("yetty_ymux_engine_exotic_link", _t.yetty_ycore_uint32_result, [c_void_p, c_uint32, c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), ref, _rt.handle(out_text), out_capacity)
    return _rt.result_from_c(res)

def engine_row_cells(obj: Any, row: int) -> _rt.Result[Any]:
    """Call `yetty_ymux_engine_row_cells`."""
    _fn = _rt.cfn("yetty_ymux_engine_row_cells", _t.yetty_ymux_cell_const_ptr_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), row)
    return _rt.result_from_c(res)

def engine_row_identity(obj: Any, row: int, out_logical_line_id: Any, out_logical_cell_start: Any, out_continuation: Any) -> _rt.Result[None]:
    """Call `yetty_ymux_engine_row_identity`."""
    _fn = _rt.cfn("yetty_ymux_engine_row_identity", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), row, _rt.handle(out_logical_line_id), _rt.handle(out_logical_cell_start), _rt.handle(out_continuation))
    return _rt.result_from_c(res)

def engine_snapshot(obj: Any, out: Any) -> _rt.Result[None]:
    """Call `yetty_ymux_engine_snapshot`."""
    _fn = _rt.cfn("yetty_ymux_engine_snapshot", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(out))
    return _rt.result_from_c(res)

def history_make(hot_rows: int, total_row_cap: int) -> _rt.Result[Any]:
    """Call `yetty_ymux_history_make`."""
    _fn = _rt.cfn("yetty_ymux_history_make", _t.yetty_yclass_object_ptr_result, [c_uint32, c_uint64])
    res = _fn(hot_rows, total_row_cap)
    return _rt.result_from_c(res)

def history_dispose(obj: Any) -> _rt.Result[None]:
    """Call `yetty_ymux_history_dispose`."""
    _fn = _rt.cfn("yetty_ymux_history_dispose", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    if hasattr(obj, '_handle'):
        obj._handle = None  # consumed: no dangling wrapper
    return _rt.result_from_c(res)

def history_set_budgets(obj: Any, warm_bytes: int, file_max_bytes: int) -> _rt.Result[None]:
    """Call `yetty_ymux_history_set_budgets`."""
    _fn = _rt.cfn("yetty_ymux_history_set_budgets", _t.yetty_ycore_void_result, [c_void_p, c_uint64, c_uint64])
    res = _fn(_rt.handle(obj), warm_bytes, file_max_bytes)
    return _rt.result_from_c(res)

def history_push(obj: Any, cells: Any, cols: int, logical_line_id: int, logical_cell_start: int, continuation: int) -> _rt.Result[None]:
    """Call `yetty_ymux_history_push`."""
    _fn = _rt.cfn("yetty_ymux_history_push", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_uint32, c_uint64, c_uint32, c_int])
    res = _fn(_rt.handle(obj), _rt.handle(cells), cols, logical_line_id, logical_cell_start, continuation)
    return _rt.result_from_c(res)

def history_pushed_rows(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_ymux_history_pushed_rows`."""
    _fn = _rt.cfn("yetty_ymux_history_pushed_rows", _t.yetty_ycore_uint64_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def history_floor(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_ymux_history_floor`."""
    _fn = _rt.cfn("yetty_ymux_history_floor", _t.yetty_ycore_uint64_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def history_resolve(obj: Any, timeline_idx: int) -> _rt.Result[Any]:
    """Call `yetty_ymux_history_resolve`."""
    _fn = _rt.cfn("yetty_ymux_history_resolve", _t.yetty_ymux_history_row_result, [c_void_p, c_uint64])
    res = _fn(_rt.handle(obj), timeline_idx)
    return _rt.result_from_c(res)

def pane_make(rows: int, cols: int, hot_rows: int, total_row_cap: int, host: Any) -> _rt.Result[Any]:
    """Call `yetty_ymux_pane_make`."""
    _fn = _rt.cfn("yetty_ymux_pane_make", _t.yetty_yclass_object_ptr_result, [c_uint32, c_uint32, c_uint32, c_uint64, c_void_p])
    res = _fn(rows, cols, hot_rows, total_row_cap, _rt.handle(host))
    return _rt.result_from_c(res)

def pane_dispose(obj: Any) -> _rt.Result[None]:
    """Call `yetty_ymux_pane_dispose`."""
    _fn = _rt.cfn("yetty_ymux_pane_dispose", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    if hasattr(obj, '_handle'):
        obj._handle = None  # consumed: no dangling wrapper
    return _rt.result_from_c(res)

def pane_feed(obj: Any, bytes: str | bytes | None, len: int) -> _rt.Result[None]:
    """Call `yetty_ymux_pane_feed`."""
    _fn = _rt.cfn("yetty_ymux_pane_feed", _t.yetty_ycore_void_result, [c_void_p, c_char_p, c_size_t])
    res = _fn(_rt.handle(obj), _rt.cstr(bytes), len)
    return _rt.result_from_c(res)

def pane_resize(obj: Any, rows: int, cols: int) -> _rt.Result[None]:
    """Call `yetty_ymux_pane_resize`."""
    _fn = _rt.cfn("yetty_ymux_pane_resize", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_uint32])
    res = _fn(_rt.handle(obj), rows, cols)
    return _rt.result_from_c(res)

def pane_engine(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_ymux_pane_engine`."""
    _fn = _rt.cfn("yetty_ymux_pane_engine", _t.yetty_yclass_object_ptr_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def pane_history(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_ymux_pane_history`."""
    _fn = _rt.cfn("yetty_ymux_pane_history", _t.yetty_yclass_object_ptr_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def pane_rich_store(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_ymux_pane_rich_store`."""
    _fn = _rt.cfn("yetty_ymux_pane_rich_store", _t.yetty_yclass_object_ptr_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def pane_timeline(obj: Any, out_floor: Any, out_live_top: Any) -> _rt.Result[None]:
    """Call `yetty_ymux_pane_timeline`."""
    _fn = _rt.cfn("yetty_ymux_pane_timeline", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(out_floor), _rt.handle(out_live_top))
    return _rt.result_from_c(res)

def pane_resolve_row(obj: Any, timeline_idx: int) -> _rt.Result[Any]:
    """Call `yetty_ymux_pane_resolve_row`."""
    _fn = _rt.cfn("yetty_ymux_pane_resolve_row", _t.yetty_ymux_history_row_result, [c_void_p, c_uint64])
    res = _fn(_rt.handle(obj), timeline_idx)
    return _rt.result_from_c(res)

def projector_make(pane: Any, attachment: Any) -> _rt.Result[Any]:
    """Call `yetty_ymux_projector_make`."""
    _fn = _rt.cfn("yetty_ymux_projector_make", _t.yetty_yclass_object_ptr_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(pane), _rt.handle(attachment))
    return _rt.result_from_c(res)

def projector_consume_tty_response(obj: Any, bytes: Any, byte_count: int) -> _rt.Result[None]:
    """Call `yetty_ymux_projector_consume_tty_response`."""
    _fn = _rt.cfn("yetty_ymux_projector_consume_tty_response", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), _rt.handle(bytes), byte_count)
    return _rt.result_from_c(res)

def projector_response_cap(obj: Any, cap_index: int, out_text: Any, out_capacity: int) -> _rt.Result[int]:
    """Call `yetty_ymux_projector_response_cap`."""
    _fn = _rt.cfn("yetty_ymux_projector_response_cap", _t.yetty_ycore_uint32_result, [c_void_p, c_uint32, c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), cap_index, _rt.handle(out_text), out_capacity)
    return _rt.result_from_c(res)

def projector_response_state(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_ymux_projector_response_state`."""
    _fn = _rt.cfn("yetty_ymux_projector_response_state", _t.yetty_ycore_uint64_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def projector_set_capabilities(obj: Any, capabilities: int) -> _rt.Result[None]:
    """Call `yetty_ymux_projector_set_capabilities`."""
    _fn = _rt.cfn("yetty_ymux_projector_set_capabilities", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), capabilities)
    return _rt.result_from_c(res)

def projector_set_terminal(obj: Any, term_name: str | bytes | None, features: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ymux_projector_set_terminal`."""
    _fn = _rt.cfn("yetty_ymux_projector_set_terminal", _t.yetty_ycore_void_result, [c_void_p, c_char_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(term_name), _rt.cstr(features))
    return _rt.result_from_c(res)

def projector_capabilities(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ymux_projector_capabilities`."""
    _fn = _rt.cfn("yetty_ymux_projector_capabilities", _t.yetty_ycore_uint32_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def projector_dispose(obj: Any) -> _rt.Result[None]:
    """Call `yetty_ymux_projector_dispose`."""
    _fn = _rt.cfn("yetty_ymux_projector_dispose", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    if hasattr(obj, '_handle'):
        obj._handle = None  # consumed: no dangling wrapper
    return _rt.result_from_c(res)

def projector_invalidate(obj: Any) -> _rt.Result[None]:
    """Call `yetty_ymux_projector_invalidate`."""
    _fn = _rt.cfn("yetty_ymux_projector_invalidate", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def projector_project_rich(obj: Any, out: Any) -> _rt.Result[int]:
    """Call `yetty_ymux_projector_project_rich`."""
    _fn = _rt.cfn("yetty_ymux_projector_project_rich", _t.yetty_ycore_int_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(out))
    return _rt.result_from_c(res)

def projector_project_vt(obj: Any, out: Any) -> _rt.Result[None]:
    """Call `yetty_ymux_projector_project_vt`."""
    _fn = _rt.cfn("yetty_ymux_projector_project_vt", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(out))
    return _rt.result_from_c(res)

def rich_make() -> _rt.Result[Any]:
    """Call `yetty_ymux_rich_make`."""
    _fn = _rt.cfn("yetty_ymux_rich_make", _t.yetty_yclass_object_ptr_result, [])
    res = _fn()
    return _rt.result_from_c(res)

def rich_dispose(obj: Any) -> _rt.Result[None]:
    """Call `yetty_ymux_rich_dispose`."""
    _fn = _rt.cfn("yetty_ymux_rich_dispose", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    if hasattr(obj, '_handle'):
        obj._handle = None  # consumed: no dangling wrapper
    return _rt.result_from_c(res)

def rich_mint(obj: Any, creation_words: Any, word_count: int, anchor_kind: int, anchor_a: int, anchor_b: int, span_rows: int) -> _rt.Result[Any]:
    """Call `yetty_ymux_rich_mint`."""
    _fn = _rt.cfn("yetty_ymux_rich_mint", _t.yetty_ycore_uint64_result, [c_void_p, c_void_p, c_uint32, c_int, c_uint64, c_uint32, c_uint32])
    res = _fn(_rt.handle(obj), _rt.handle(creation_words), word_count, anchor_kind, anchor_a, anchor_b, span_rows)
    return _rt.result_from_c(res)

def rich_anchor(obj: Any, rich_id: int, out_kind: Any, out_anchor_a: Any, out_anchor_b: Any, out_span_rows: Any) -> _rt.Result[None]:
    """Call `yetty_ymux_rich_anchor`."""
    _fn = _rt.cfn("yetty_ymux_rich_anchor", _t.yetty_ycore_void_result, [c_void_p, c_uint64, c_void_p, c_void_p, c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), rich_id, _rt.handle(out_kind), _rt.handle(out_anchor_a), _rt.handle(out_anchor_b), _rt.handle(out_span_rows))
    return _rt.result_from_c(res)

def rich_journal_append(obj: Any, rich_id: int, words: Any, word_count: int) -> _rt.Result[None]:
    """Call `yetty_ymux_rich_journal_append`."""
    _fn = _rt.cfn("yetty_ymux_rich_journal_append", _t.yetty_ycore_void_result, [c_void_p, c_uint64, c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), rich_id, _rt.handle(words), word_count)
    return _rt.result_from_c(res)

def rich_journal_count(obj: Any, rich_id: int) -> _rt.Result[int]:
    """Call `yetty_ymux_rich_journal_count`."""
    _fn = _rt.cfn("yetty_ymux_rich_journal_count", _t.yetty_ycore_uint32_result, [c_void_p, c_uint64])
    res = _fn(_rt.handle(obj), rich_id)
    return _rt.result_from_c(res)

def rich_creation(obj: Any, rich_id: int, out_word_count: Any) -> _rt.Result[Any]:
    """Call `yetty_ymux_rich_creation`."""
    _fn = _rt.cfn("yetty_ymux_rich_creation", _t.yetty_ycore_const_uint32_ptr_result, [c_void_p, c_uint64, c_void_p])
    res = _fn(_rt.handle(obj), rich_id, _rt.handle(out_word_count))
    return _rt.result_from_c(res)

def rich_creation_hash(obj: Any, rich_id: int) -> _rt.Result[Any]:
    """Call `yetty_ymux_rich_creation_hash`."""
    _fn = _rt.cfn("yetty_ymux_rich_creation_hash", _t.yetty_ycore_uint64_result, [c_void_p, c_uint64])
    res = _fn(_rt.handle(obj), rich_id)
    return _rt.result_from_c(res)

def rich_distinct_resource_count(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ymux_rich_distinct_resource_count`."""
    _fn = _rt.cfn("yetty_ymux_rich_distinct_resource_count", _t.yetty_ycore_uint32_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def rich_journal_entry(obj: Any, rich_id: int, entry_index: int, out_word_count: Any) -> _rt.Result[Any]:
    """Call `yetty_ymux_rich_journal_entry`."""
    _fn = _rt.cfn("yetty_ymux_rich_journal_entry", _t.yetty_ycore_const_uint32_ptr_result, [c_void_p, c_uint64, c_uint32, c_void_p])
    res = _fn(_rt.handle(obj), rich_id, entry_index, _rt.handle(out_word_count))
    return _rt.result_from_c(res)

def rich_tombstone(obj: Any, rich_id: int) -> _rt.Result[None]:
    """Call `yetty_ymux_rich_tombstone`."""
    _fn = _rt.cfn("yetty_ymux_rich_tombstone", _t.yetty_ycore_void_result, [c_void_p, c_uint64])
    res = _fn(_rt.handle(obj), rich_id)
    return _rt.result_from_c(res)

def rich_is_tombstoned(obj: Any, rich_id: int) -> _rt.Result[int]:
    """Call `yetty_ymux_rich_is_tombstoned`."""
    _fn = _rt.cfn("yetty_ymux_rich_is_tombstoned", _t.yetty_ycore_int_result, [c_void_p, c_uint64])
    res = _fn(_rt.handle(obj), rich_id)
    return _rt.result_from_c(res)

def rich_count(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ymux_rich_count`."""
    _fn = _rt.cfn("yetty_ymux_rich_count", _t.yetty_ycore_uint32_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def rich_id_at(obj: Any, index: int) -> _rt.Result[Any]:
    """Call `yetty_ymux_rich_id_at`."""
    _fn = _rt.cfn("yetty_ymux_rich_id_at", _t.yetty_ycore_uint64_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), index)
    return _rt.result_from_c(res)

def rich_revision(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_ymux_rich_revision`."""
    _fn = _rt.cfn("yetty_ymux_rich_revision", _t.yetty_ycore_uint64_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def rich_clear_all(obj: Any) -> _rt.Result[None]:
    """Call `yetty_ymux_rich_clear_all`."""
    _fn = _rt.cfn("yetty_ymux_rich_clear_all", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def rich_compact_tombstoned(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ymux_rich_compact_tombstoned`."""
    _fn = _rt.cfn("yetty_ymux_rich_compact_tombstoned", _t.yetty_ycore_uint32_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def rich_snapshot(obj: Any, out: Any) -> _rt.Result[None]:
    """Call `yetty_ymux_rich_snapshot`."""
    _fn = _rt.cfn("yetty_ymux_rich_snapshot", _t.yetty_ycore_void_result, [c_void_p, c_void_p])
    res = _fn(_rt.handle(obj), _rt.handle(out))
    return _rt.result_from_c(res)

def rich_restore(obj: Any, words: Any, word_count: int) -> _rt.Result[None]:
    """Call `yetty_ymux_rich_restore`."""
    _fn = _rt.cfn("yetty_ymux_rich_restore", _t.yetty_ycore_void_result, [c_void_p, c_void_p, c_size_t])
    res = _fn(_rt.handle(obj), _rt.handle(words), word_count)
    return _rt.result_from_c(res)

def rich_map_bind(obj: Any, ordinal: int, rich_id: int) -> _rt.Result[None]:
    """Call `yetty_ymux_rich_map_bind`."""
    _fn = _rt.cfn("yetty_ymux_rich_map_bind", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_uint64])
    res = _fn(_rt.handle(obj), ordinal, rich_id)
    return _rt.result_from_c(res)

def rich_map_resolve(obj: Any, ordinal: int) -> _rt.Result[Any]:
    """Call `yetty_ymux_rich_map_resolve`."""
    _fn = _rt.cfn("yetty_ymux_rich_map_resolve", _t.yetty_ycore_uint64_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), ordinal)
    return _rt.result_from_c(res)

def rich_map_delete(obj: Any, ordinal: int) -> _rt.Result[None]:
    """Call `yetty_ymux_rich_map_delete`."""
    _fn = _rt.cfn("yetty_ymux_rich_map_delete", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), ordinal)
    return _rt.result_from_c(res)

def rich_map_close(obj: Any) -> _rt.Result[None]:
    """Call `yetty_ymux_rich_map_close`."""
    _fn = _rt.cfn("yetty_ymux_rich_map_close", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def session_make() -> _rt.Result[Any]:
    """Call `yetty_ymux_session_make`."""
    _fn = _rt.cfn("yetty_ymux_session_make", _t.yetty_yclass_object_ptr_result, [])
    res = _fn()
    return _rt.result_from_c(res)

def session_dispose(obj: Any) -> _rt.Result[None]:
    """Call `yetty_ymux_session_dispose`."""
    _fn = _rt.cfn("yetty_ymux_session_dispose", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    if hasattr(obj, '_handle'):
        obj._handle = None  # consumed: no dangling wrapper
    return _rt.result_from_c(res)

def session_pane_create(obj: Any, rows: int, cols: int, hot_rows: int, total_row_cap: int, host: Any) -> _rt.Result[int]:
    """Call `yetty_ymux_session_pane_create`."""
    _fn = _rt.cfn("yetty_ymux_session_pane_create", _t.yetty_ycore_uint32_result, [c_void_p, c_uint32, c_uint32, c_uint32, c_uint64, c_void_p])
    res = _fn(_rt.handle(obj), rows, cols, hot_rows, total_row_cap, _rt.handle(host))
    return _rt.result_from_c(res)

def session_pane_close(obj: Any, pane_id: int) -> _rt.Result[None]:
    """Call `yetty_ymux_session_pane_close`."""
    _fn = _rt.cfn("yetty_ymux_session_pane_close", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), pane_id)
    return _rt.result_from_c(res)

def session_pane(obj: Any, pane_id: int) -> _rt.Result[Any]:
    """Call `yetty_ymux_session_pane`."""
    _fn = _rt.cfn("yetty_ymux_session_pane", _t.yetty_yclass_object_ptr_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), pane_id)
    return _rt.result_from_c(res)

def session_attach(obj: Any, pane_id: int, view_rows: int, view_cols: int, token: str | bytes | None) -> _rt.Result[int]:
    """Call `yetty_ymux_session_attach`."""
    _fn = _rt.cfn("yetty_ymux_session_attach", _t.yetty_ycore_uint32_result, [c_void_p, c_uint32, c_uint32, c_uint32, c_char_p])
    res = _fn(_rt.handle(obj), pane_id, view_rows, view_cols, _rt.cstr(token))
    return _rt.result_from_c(res)

def session_detach(obj: Any, attachment_id: int) -> _rt.Result[None]:
    """Call `yetty_ymux_session_detach`."""
    _fn = _rt.cfn("yetty_ymux_session_detach", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), attachment_id)
    return _rt.result_from_c(res)

def session_takeover(obj: Any, attachment_id: int) -> _rt.Result[None]:
    """Call `yetty_ymux_session_takeover`."""
    _fn = _rt.cfn("yetty_ymux_session_takeover", _t.yetty_ycore_void_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), attachment_id)
    return _rt.result_from_c(res)

def session_resize(obj: Any, attachment_id: int, rows: int, cols: int) -> _rt.Result[None]:
    """Call `yetty_ymux_session_resize`."""
    _fn = _rt.cfn("yetty_ymux_session_resize", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_uint32, c_uint32])
    res = _fn(_rt.handle(obj), attachment_id, rows, cols)
    return _rt.result_from_c(res)

def session_controller(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ymux_session_controller`."""
    _fn = _rt.cfn("yetty_ymux_session_controller", _t.yetty_ycore_uint32_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def session_active_pane(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ymux_session_active_pane`."""
    _fn = _rt.cfn("yetty_ymux_session_active_pane", _t.yetty_ycore_uint32_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def session_permissions(obj: Any, attachment_id: int) -> _rt.Result[int]:
    """Call `yetty_ymux_session_permissions`."""
    _fn = _rt.cfn("yetty_ymux_session_permissions", _t.yetty_ycore_uint32_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), attachment_id)
    return _rt.result_from_c(res)

def session_set_permissions(obj: Any, attachment_id: int, permissions: int) -> _rt.Result[None]:
    """Call `yetty_ymux_session_set_permissions`."""
    _fn = _rt.cfn("yetty_ymux_session_set_permissions", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_uint32])
    res = _fn(_rt.handle(obj), attachment_id, permissions)
    return _rt.result_from_c(res)

def session_input_char(obj: Any, attachment_id: int, codepoint: int, mods: int) -> _rt.Result[None]:
    """Call `yetty_ymux_session_input_char`."""
    _fn = _rt.cfn("yetty_ymux_session_input_char", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_uint32, c_int])
    res = _fn(_rt.handle(obj), attachment_id, codepoint, mods)
    return _rt.result_from_c(res)

def session_input_key(obj: Any, attachment_id: int, key: int, mods: int) -> _rt.Result[None]:
    """Call `yetty_ymux_session_input_key`."""
    _fn = _rt.cfn("yetty_ymux_session_input_key", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_int, c_int])
    res = _fn(_rt.handle(obj), attachment_id, key, mods)
    return _rt.result_from_c(res)

def session_input_mouse_move(obj: Any, attachment_id: int, row: int, col: int, mods: int) -> _rt.Result[None]:
    """Call `yetty_ymux_session_input_mouse_move`."""
    _fn = _rt.cfn("yetty_ymux_session_input_mouse_move", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_uint32, c_uint32, c_int])
    res = _fn(_rt.handle(obj), attachment_id, row, col, mods)
    return _rt.result_from_c(res)

def session_input_mouse_button(obj: Any, attachment_id: int, button: int, pressed: int, mods: int) -> _rt.Result[None]:
    """Call `yetty_ymux_session_input_mouse_button`."""
    _fn = _rt.cfn("yetty_ymux_session_input_mouse_button", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_int, c_int, c_int])
    res = _fn(_rt.handle(obj), attachment_id, button, pressed, mods)
    return _rt.result_from_c(res)

def session_input_paste(obj: Any, attachment_id: int, text: str | bytes | None, len: int) -> _rt.Result[None]:
    """Call `yetty_ymux_session_input_paste`."""
    _fn = _rt.cfn("yetty_ymux_session_input_paste", _t.yetty_ycore_void_result, [c_void_p, c_uint32, c_char_p, c_size_t])
    res = _fn(_rt.handle(obj), attachment_id, _rt.cstr(text), len)
    return _rt.result_from_c(res)

def session_projector(obj: Any, attachment_id: int) -> _rt.Result[Any]:
    """Call `yetty_ymux_session_projector`."""
    _fn = _rt.cfn("yetty_ymux_session_projector", _t.yetty_yclass_object_ptr_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), attachment_id)
    return _rt.result_from_c(res)

def session_attachment(obj: Any, attachment_id: int) -> _rt.Result[Any]:
    """Call `yetty_ymux_session_attachment`."""
    _fn = _rt.cfn("yetty_ymux_session_attachment", _t.yetty_yclass_object_ptr_result, [c_void_p, c_uint32])
    res = _fn(_rt.handle(obj), attachment_id)
    return _rt.result_from_c(res)
