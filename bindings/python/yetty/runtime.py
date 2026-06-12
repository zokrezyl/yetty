"""yetty FFI runtime — HAND-WRITTEN (the generator never touches this file).

Provides the stable foundation the generated bindings build on:
  - locating + loading the yetty FFI shared library,
  - configuring + calling a C symbol,
  - translating yclass C Result structs into Python Result values, generically
    over any `*_result` struct (they all share {ok, union{value, error}}).

Load the library once: `yetty.runtime.load("/path/to/libyetty_ffi.so")`, or set
the env var `YETTY_FFI_LIB` and it loads lazily on first call.
"""

from __future__ import annotations

import ctypes
import os
from dataclasses import dataclass
from typing import Callable, Generic, TypeVar

_lib: ctypes.CDLL | None = None
T = TypeVar("T")


@dataclass(frozen=True)
class Error:
    """Python copy of a yetty_ycore_error, including its cause chain."""

    message: str
    file: str | None = None
    func: str | None = None
    line: int = 0
    cause: "Error | None" = None


@dataclass(frozen=True)
class Result(Generic[T]):
    """Python result value returned by generated yclass methods."""

    value: T | None = None
    error: Error | None = None

    @property
    def ok(self) -> bool:
        return self.error is None

    def __bool__(self) -> bool:
        return self.ok


class YettyError(Exception):
    """Compatibility exception for low-level handwritten wrappers only."""


class YClass:
    """Base class for generated yclass object wrappers."""

    __slots__ = ("_handle", "_error")

    def __init__(self, _handle, _error: Error | None = None):
        self._handle = _handle
        self._error = _error

    @property
    def handle(self):
        """Raw `struct yetty_yclass_object *` handle for low-level interop."""
        return self._handle

    @property
    def init_result(self):
        """Result for direct Class() construction."""
        if self._error is not None:
            return Result(error=self._error)
        return Result(value=self)

    def _invalid_result(self):
        return Result(error=self._error or Error("uninitialized yclass handle"))

    @classmethod
    def from_handle(cls, handle):
        return cls(_handle=handle)


def load(path: str | None = None) -> ctypes.CDLL:
    """Load (or reload) the yetty FFI shared library. Idempotent-ish: the last
    call wins."""
    global _lib
    resolved = path or os.environ.get("YETTY_FFI_LIB")
    if not resolved:
        raise RuntimeError(
            "yetty FFI: no library path — call yetty.runtime.load(path) "
            "or set YETTY_FFI_LIB to the shared library.")
    _lib = ctypes.CDLL(resolved)
    return _lib


def _require() -> ctypes.CDLL:
    if _lib is None:
        env = os.environ.get("YETTY_FFI_LIB")
        if env:
            return load(env)
        raise RuntimeError(
            "yetty FFI: library not loaded — call yetty.runtime.load(path) "
            "or set YETTY_FFI_LIB.")
    return _lib


def cfn(name: str, restype, argtypes: list):
    """Look up a C symbol and pin its restype/argtypes (so by-value struct
    returns use the correct ABI). Cheap to repeat."""
    fn = getattr(_require(), name)
    fn.restype = restype
    fn.argtypes = argtypes
    return fn


def cstr(value):
    """Coerce a Python str to UTF-8 bytes for a `const char *` argument
    (bytes and None pass through)."""
    if value is None or isinstance(value, bytes):
        return value
    return value.encode("utf-8")


def handle(value):
    """Return the raw C handle for generated wrapper objects."""
    if value is None:
        return None
    return getattr(value, "handle", value)


class _CError(ctypes.Structure):
    pass


_CErrorPtr = ctypes.POINTER(_CError)
_CError._fields_ = [
    ("msg", ctypes.c_char_p),
    ("file", ctypes.c_char_p),
    ("func", ctypes.c_char_p),
    ("line", ctypes.c_int),
    ("cause", _CErrorPtr),
]


def decode_cstr(value) -> str | None:
    if not value:
        return None
    return value.decode("utf-8", "replace")


def error_from_c(err, _seen: set[int] | None = None) -> Error:
    """Copy a yetty_ycore_error, including linked causes, into Python data."""
    _seen = _seen or set()
    cause = None
    cause_ptr = getattr(err, "cause", None)
    if cause_ptr:
        if isinstance(cause_ptr, int):
            addr = cause_ptr
            cptr = ctypes.c_void_p(cause_ptr)
        else:
            addr = ctypes.addressof(cause_ptr.contents)
            cptr = cause_ptr
        if addr and addr not in _seen:
            _seen.add(addr)
            cause = error_from_c(ctypes.cast(cptr, _CErrorPtr).contents, _seen)
    return Error(
        message=decode_cstr(getattr(err, "msg", None)) or "yetty error",
        file=decode_cstr(getattr(err, "file", None)),
        func=decode_cstr(getattr(err, "func", None)),
        line=int(getattr(err, "line", 0) or 0),
        cause=cause,
    )


def result_from_c(res, convert: Callable[[object], T] | None = None) -> Result[T]:
    """Translate a C Result struct into a Python Result value.

    Generated bindings use this instead of exceptions: ok results carry a
    Python value, error results carry an Error with the C cause chain copied.
    """
    if not getattr(res, "ok", 1):
        return Result(error=error_from_c(res.error))
    if type(res).__name__.endswith("void_result"):
        return Result(value=None)
    value = res.value
    if convert is not None:
        value = convert(value)
    return Result(value=value)


def check(res) -> None:
    """Compatibility helper for handwritten low-level wrappers.

    Generated yclass bindings do not call this; they return Result values.
    """
    pyres = result_from_c(res)
    if pyres.error is not None:
        raise YettyError(pyres.error.message)
