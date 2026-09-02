"""yetty.yjupyter bindings — GENERATED from model.yaml, do not edit."""
from __future__ import annotations
from ctypes import (c_bool, c_char, c_char_p, c_double, c_float, c_int,
    c_int8, c_int16, c_int32, c_int64, c_long, c_size_t, c_ssize_t,
    c_uint, c_uint8, c_uint16, c_uint32, c_uint64, c_ulong, c_void_p)
from typing import Any, ClassVar
from .. import runtime as _rt
from . import _types as _t

class Client(_rt.YClass):
    """yclass yjupyter:client"""
    __yclass_domain__: ClassVar[str] = 'yjupyter'
    __yclass_name__: ClassVar[str] = 'client'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_yjupyter_client_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_yjupyter_client_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(None))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._apply_kwargs(kwargs)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Client':
        return cls(**kwargs)
    def client_open(self, base_url: str | bytes | None, token: str | bytes | None) -> None:
        """Call `yetty_yjupyter_client_open`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yjupyter_client_open", _t.yetty_ycore_void_result, [c_void_p, c_char_p, c_char_p])
        res = _rt.result_from_c(_fn(self._handle, _rt.cstr(base_url), _rt.cstr(token)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def client_execute(self, code: str | bytes | None, tag: str | bytes | None) -> str | None:
        """Call `yetty_yjupyter_client_execute`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yjupyter_client_execute", _t.yetty_ycore_char_ptr_result, [c_void_p, c_char_p, c_char_p])
        res = _rt.result_from_c(_fn(self._handle, _rt.cstr(code), _rt.cstr(tag)), _rt.take_owned_cstr)
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def client_poll(self, timeout_ms: int) -> Any:
        """Call `yetty_yjupyter_client_poll`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yjupyter_client_poll", _t.yetty_yclass_object_ptr_result, [c_void_p, c_int])
        res = _rt.result_from_c(_fn(self._handle, timeout_ms))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def client_kernel_state(self) -> str | None:
        """Call `yetty_yjupyter_client_kernel_state`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yjupyter_client_kernel_state", _t.yetty_ycore_const_char_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle), _rt.decode_cstr)
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def client_tag_for(self, parent_msg_id: str | bytes | None) -> str | None:
        """Call `yetty_yjupyter_client_tag_for`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yjupyter_client_tag_for", _t.yetty_ycore_const_char_ptr_result, [c_void_p, c_char_p])
        res = _rt.result_from_c(_fn(self._handle, _rt.cstr(parent_msg_id)), _rt.decode_cstr)
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def client_close(self) -> None:
        """Call `yetty_yjupyter_client_close`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yjupyter_client_close", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def client_destroy(self) -> None:
        """Call `yetty_yjupyter_client_destroy`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yjupyter_client_destroy", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value

class Message(_rt.YClass):
    """yclass yjupyter:message"""
    __yclass_domain__: ClassVar[str] = 'yjupyter'
    __yclass_name__: ClassVar[str] = 'message'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_yjupyter_message_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_yjupyter_message_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(None))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._apply_kwargs(kwargs)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Message':
        return cls(**kwargs)
    def message_build(self, msg_type: str | bytes | None, channel: str | bytes | None, session_id: str | bytes | None, msg_id: str | bytes | None, parent_msg_id: str | bytes | None, content_json: str | bytes | None) -> None:
        """Call `yetty_yjupyter_message_build`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yjupyter_message_build", _t.yetty_ycore_void_result, [c_void_p, c_char_p, c_char_p, c_char_p, c_char_p, c_char_p, c_char_p])
        res = _rt.result_from_c(_fn(self._handle, _rt.cstr(msg_type), _rt.cstr(channel), _rt.cstr(session_id), _rt.cstr(msg_id), _rt.cstr(parent_msg_id), _rt.cstr(content_json)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def message_from_wire(self, json: str | bytes | None) -> None:
        """Call `yetty_yjupyter_message_from_wire`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yjupyter_message_from_wire", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
        res = _rt.result_from_c(_fn(self._handle, _rt.cstr(json)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def message_to_wire(self) -> str | None:
        """Call `yetty_yjupyter_message_to_wire`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yjupyter_message_to_wire", _t.yetty_ycore_char_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle), _rt.take_owned_cstr)
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def message_msg_type(self) -> str | None:
        """Call `yetty_yjupyter_message_msg_type`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yjupyter_message_msg_type", _t.yetty_ycore_const_char_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle), _rt.decode_cstr)
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def message_msg_id(self) -> str | None:
        """Call `yetty_yjupyter_message_msg_id`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yjupyter_message_msg_id", _t.yetty_ycore_const_char_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle), _rt.decode_cstr)
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def message_parent_msg_id(self) -> str | None:
        """Call `yetty_yjupyter_message_parent_msg_id`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yjupyter_message_parent_msg_id", _t.yetty_ycore_const_char_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle), _rt.decode_cstr)
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def message_channel(self) -> str | None:
        """Call `yetty_yjupyter_message_channel`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yjupyter_message_channel", _t.yetty_ycore_const_char_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle), _rt.decode_cstr)
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def message_session(self) -> str | None:
        """Call `yetty_yjupyter_message_session`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yjupyter_message_session", _t.yetty_ycore_const_char_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle), _rt.decode_cstr)
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def message_content_json(self) -> str | None:
        """Call `yetty_yjupyter_message_content_json`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yjupyter_message_content_json", _t.yetty_ycore_char_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle), _rt.take_owned_cstr)
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def message_content_string(self, key: str | bytes | None) -> str | None:
        """Call `yetty_yjupyter_message_content_string`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yjupyter_message_content_string", _t.yetty_ycore_char_ptr_result, [c_void_p, c_char_p])
        res = _rt.result_from_c(_fn(self._handle, _rt.cstr(key)), _rt.take_owned_cstr)
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def message_content_int(self, key: str | bytes | None) -> int:
        """Call `yetty_yjupyter_message_content_int`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yjupyter_message_content_int", _t.yetty_ycore_int_result, [c_void_p, c_char_p])
        res = _rt.result_from_c(_fn(self._handle, _rt.cstr(key)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def message_destroy(self) -> None:
        """Call `yetty_yjupyter_message_destroy`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yjupyter_message_destroy", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value

class Session(_rt.YClass):
    """yclass yjupyter:session"""
    __yclass_domain__: ClassVar[str] = 'yjupyter'
    __yclass_name__: ClassVar[str] = 'session'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_yjupyter_session_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_yjupyter_session_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(None))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._apply_kwargs(kwargs)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Session':
        return cls(**kwargs)
    def session_init(self, session_id: str | bytes | None) -> None:
        """Call `yetty_yjupyter_session_init`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yjupyter_session_init", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
        res = _rt.result_from_c(_fn(self._handle, _rt.cstr(session_id)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def session_id(self) -> str | None:
        """Call `yetty_yjupyter_session_id`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yjupyter_session_id", _t.yetty_ycore_const_char_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle), _rt.decode_cstr)
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def session_kernel_state(self) -> str | None:
        """Call `yetty_yjupyter_session_kernel_state`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yjupyter_session_kernel_state", _t.yetty_ycore_const_char_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle), _rt.decode_cstr)
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def session_new_request(self, msg_type: str | bytes | None, channel: str | bytes | None, content_json: str | bytes | None, tag: str | bytes | None) -> Any:
        """Call `yetty_yjupyter_session_new_request`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yjupyter_session_new_request", _t.yetty_yclass_object_ptr_result, [c_void_p, c_char_p, c_char_p, c_char_p, c_char_p])
        res = _rt.result_from_c(_fn(self._handle, _rt.cstr(msg_type), _rt.cstr(channel), _rt.cstr(content_json), _rt.cstr(tag)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def session_handle_wire(self, json: str | bytes | None) -> Any:
        """Call `yetty_yjupyter_session_handle_wire`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yjupyter_session_handle_wire", _t.yetty_yclass_object_ptr_result, [c_void_p, c_char_p])
        res = _rt.result_from_c(_fn(self._handle, _rt.cstr(json)))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def session_tag_for(self, parent_msg_id: str | bytes | None) -> str | None:
        """Call `yetty_yjupyter_session_tag_for`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yjupyter_session_tag_for", _t.yetty_ycore_const_char_ptr_result, [c_void_p, c_char_p])
        res = _rt.result_from_c(_fn(self._handle, _rt.cstr(parent_msg_id)), _rt.decode_cstr)
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def session_destroy(self) -> None:
        """Call `yetty_yjupyter_session_destroy`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_yjupyter_session_destroy", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
