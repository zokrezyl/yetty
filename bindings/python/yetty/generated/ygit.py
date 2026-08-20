"""yetty.ygit bindings — GENERATED from model.yaml, do not edit."""
from __future__ import annotations
from ctypes import (c_bool, c_char, c_char_p, c_double, c_float, c_int,
    c_int8, c_int16, c_int32, c_int64, c_long, c_size_t, c_ssize_t,
    c_uint, c_uint8, c_uint16, c_uint32, c_uint64, c_ulong, c_void_p)
from typing import Any, ClassVar
from .. import runtime as _rt
from . import _types as _t

class Repo(_rt.YClass):
    """yclass ygit:repo"""
    __yclass_domain__: ClassVar[str] = 'ygit'
    __yclass_name__: ClassVar[str] = 'repo'
    @classmethod
    def yclass(cls) -> _rt.Result[Any]:
        _fn = _rt.cfn("yetty_ygit_repo_class_get", _t.yetty_yclass_ptr_result, [])
        return _rt.result_from_c(_fn())
    def __init__(self, _handle: Any = None, **kwargs: Any) -> None:
        if _handle is not None:
            _rt.YClass.__init__(self, _handle)
            return
        _fn = _rt.cfn("yetty_ygit_repo_create", _t.yetty_yclass_object_ptr_result, [c_void_p])
        res = _rt.result_from_c(_fn(None))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        _rt.YClass.__init__(self, res.value)
        self._apply_kwargs(kwargs)
    @classmethod
    def create(cls, **kwargs: Any) -> 'Repo':
        return cls(**kwargs)
    def constructor(self) -> None:
        """Call `yetty_ygit_constructor`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ygit_constructor", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value
    def destructor(self) -> None:
        """Call `yetty_ygit_destructor`; raises _rt.YettyError on failure."""
        if self._handle is None:
            raise _rt.YettyError("uninitialized yclass handle")
        _fn = _rt.cfn("yetty_ygit_destructor", _t.yetty_ycore_void_result, [c_void_p])
        res = _rt.result_from_c(_fn(self._handle))
        if res.error is not None:
            raise _rt.YettyError(res.error.message)
        return res.value

def repo_open(obj: Any, path: str | bytes | None) -> _rt.Result[None]:
    """Call `yetty_ygit_repo_open`."""
    _fn = _rt.cfn("yetty_ygit_repo_open", _t.yetty_ycore_void_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(path))
    return _rt.result_from_c(res)

def repo_path(obj: Any) -> _rt.Result[str | None]:
    """Call `yetty_ygit_repo_path`."""
    _fn = _rt.cfn("yetty_ygit_repo_path", _t.yetty_ycore_const_char_ptr_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res, _rt.decode_cstr)

def repo_is_repo(obj: Any) -> _rt.Result[int]:
    """Call `yetty_ygit_repo_is_repo`."""
    _fn = _rt.cfn("yetty_ygit_repo_is_repo", _t.yetty_ycore_int_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def repo_log(obj: Any, revision: str | bytes | None, max_count: int) -> _rt.Result[Any]:
    """Call `yetty_ygit_repo_log`."""
    _fn = _rt.cfn("yetty_ygit_repo_log", _t.yetty_ygit_log_ptr_result, [c_void_p, c_char_p, c_int])
    res = _fn(_rt.handle(obj), _rt.cstr(revision), max_count)
    return _rt.result_from_c(res)

def repo_log_all(obj: Any, max_count: int) -> _rt.Result[Any]:
    """Call `yetty_ygit_repo_log_all`."""
    _fn = _rt.cfn("yetty_ygit_repo_log_all", _t.yetty_ygit_log_ptr_result, [c_void_p, c_int])
    res = _fn(_rt.handle(obj), max_count)
    return _rt.result_from_c(res)

def repo_status(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_ygit_repo_status`."""
    _fn = _rt.cfn("yetty_ygit_repo_status", _t.yetty_ygit_status_ptr_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def repo_branches(obj: Any) -> _rt.Result[Any]:
    """Call `yetty_ygit_repo_branches`."""
    _fn = _rt.cfn("yetty_ygit_repo_branches", _t.yetty_ygit_branches_ptr_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

def repo_show(obj: Any, revision: str | bytes | None) -> _rt.Result[Any]:
    """Call `yetty_ygit_repo_show`."""
    _fn = _rt.cfn("yetty_ygit_repo_show", _t.yetty_ygit_commit_detail_ptr_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(revision))
    return _rt.result_from_c(res)

def repo_read_blob(obj: Any, spec: str | bytes | None) -> _rt.Result[Any]:
    """Call `yetty_ygit_repo_read_blob`."""
    _fn = _rt.cfn("yetty_ygit_repo_read_blob", _t.yetty_ygit_blob_ptr_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(spec))
    return _rt.result_from_c(res)

def repo_diff(obj: Any, revision: str | bytes | None) -> _rt.Result[Any]:
    """Call `yetty_ygit_repo_diff`."""
    _fn = _rt.cfn("yetty_ygit_repo_diff", _t.yetty_ygit_diff_ptr_result, [c_void_p, c_char_p])
    res = _fn(_rt.handle(obj), _rt.cstr(revision))
    return _rt.result_from_c(res)

def repo_destroy(obj: Any) -> _rt.Result[None]:
    """Call `yetty_ygit_repo_destroy`."""
    _fn = _rt.cfn("yetty_ygit_repo_destroy", _t.yetty_ycore_void_result, [c_void_p])
    res = _fn(_rt.handle(obj))
    return _rt.result_from_c(res)

