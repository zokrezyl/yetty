"""yetty FFI runtime — HAND-WRITTEN (the generator never touches this file).

Provides the stable foundation the generated bindings build on:
  - locating + loading the yetty FFI shared library,
  - configuring + calling a C symbol,
  - decoding a yclass Result (raise on error), generically over any
    `*_result` struct (they all share the {ok, union{value, error}} shape).

Load the library once: `yetty.runtime.load("/path/to/libyetty_ffi.so")`, or set
the env var `YETTY_FFI_LIB` and it loads lazily on first call.
"""

from __future__ import annotations

import ctypes
import os

_lib: ctypes.CDLL | None = None


class YettyError(Exception):
    """Raised when a yclass method returns an error Result."""


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


def check(res) -> None:
    """Raise YettyError if a Result carries an error. Generic over every
    `*_result` struct: they expose `.ok` and (via the anonymous union)
    `.error`."""
    if getattr(res, "ok", 1):
        return
    error = res.error
    msg = error.msg.decode("utf-8", "replace") if error.msg else "yetty error"
    raise YettyError(msg)
