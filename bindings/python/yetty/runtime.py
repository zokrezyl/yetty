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
import ctypes.util
import glob
import os
from dataclasses import dataclass
from pathlib import Path
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


def as_buffer(value):
    """Coerce a python value into a by-value `yetty_ycore_buffer` argument:
    bytes/bytearray as-is; a list/tuple of numbers as a packed f32 array (the
    wire's sample format); an already-built buffer struct passes through."""
    import struct as _struct

    from .generated import _types
    if isinstance(value, _types.yetty_ycore_buffer):
        return value
    if isinstance(value, (list, tuple)):
        value = _struct.pack(f"<{len(value)}f", *[float(v) for v in value])
    if isinstance(value, (bytes, bytearray)):
        raw = bytes(value)
        backing = ctypes.create_string_buffer(raw, len(raw))
        buffer = _types.yetty_ycore_buffer()
        buffer.data = ctypes.cast(backing, ctypes.c_void_p)
        buffer.size = len(raw)
        buffer.capacity = len(raw)
        # keep the backing storage alive for the duration of the call
        buffer._backing = backing
        return buffer
    raise TypeError(f"cannot coerce {type(value).__name__} to a ycore buffer")


class YClass:
    """Base class for generated yclass object wrappers."""

    __slots__ = ("_handle", "_error", "_owned")

    def __init__(self, _handle, _error: Error | None = None):
        self._handle = _handle
        self._error = _error
        # Created-by-us wrappers set _owned (in the generated create path);
        # borrowed handles (from_handle, RPC proxies) stay unowned and are
        # never finalized here.
        self._owned = False

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

    def _apply_kwargs(self, kwargs) -> None:
        """Apply constructor keywords, generically (no per-class logic):

        - `set_<key>` method when the class has one. TUPLE values unpack
          into its arguments, one nesting level flattened
          (size=(640, 320) -> set_size(640, 320);
          view=((a, b), (c, d)) -> set_view(a, b, c, d));
          lists pass through whole (data arrays, e.g. values=[...]).
        - else a generated @property member (inherited ones included).
        - else, for a list/tuple of yclass objects, an `add_<key-singular>`
          method applied per element (functions=[...] -> add_function each).
        """
        for key, value in kwargs.items():
            setter = getattr(self, "set_" + key, None)
            if setter is not None:
                if isinstance(value, tuple):
                    flat = []
                    for element in value:
                        if isinstance(element, tuple):
                            flat.extend(element)
                        else:
                            flat.append(element)
                    setter(*flat)
                else:
                    setter(value)
                continue
            descriptor = getattr(type(self), key, None)
            if isinstance(descriptor, property):
                setattr(self, key, value)
                continue
            if isinstance(value, (list, tuple)) and key.endswith("s"):
                adder = getattr(self, "add_" + key[:-1], None)
                if adder is not None and all(isinstance(e, YClass) for e in value):
                    for element in value:
                        adder(element)
                    continue
            raise TypeError(f"{type(self).__name__}: unknown property {key!r}")

    def destroy(self) -> None:
        """Reclaim the underlying yclass object.

        Generic fallback used by classes that declare no class-specific
        `destroy` slot (nothing but the object allocation to free). A class
        with owned resources declares its own `destroy` slot, whose generated
        method shadows this one and frees those resources first.
        """
        if self._handle is None:
            return
        from .generated import _types
        free = cfn("yetty_yclass_object_free", _types.yetty_ycore_void_result, [ctypes.c_void_p])
        res = result_from_c(free(self._handle))
        self._handle = None
        if res.error is not None:
            raise YettyError(res.error.message)

    def __del__(self):
        # Reclaim owned native objects when the wrapper is collected, so
        # temporaries like dlist.add(Circle(...)) do not accumulate in
        # long-running hosts. destroy() is idempotent (class-specific
        # destroys invalidate the handle too), and interpreter-shutdown
        # teardown races are absorbed.
        if not getattr(self, "_owned", False):
            return
        if getattr(self, "_handle", None) is None:
            return
        try:
            self.destroy()
        except Exception:
            pass

    @classmethod
    def from_handle(cls, handle):
        return cls(_handle=handle)


_LIB_BASENAMES = ("libyetty_ffi.so", "libyetty_ffi.dylib", "yetty_ffi.dll")

# Load with LAZY symbol binding: the FFI shared object is large and links the
# whole app-bootstrap surface (GLFW/WebGPU/…), and eager (RTLD_NOW) binding can
# fault on an IFUNC/relocation before any binding call runs. Lazy defers symbol
# resolution to first use, which is what the generated stubs need.
_LOAD_MODE = ctypes.RTLD_LOCAL | getattr(os, "RTLD_LAZY", 1)


# The binding ABI this runtime speaks. MAJOR must match the library
# exactly; the library's MINOR must be >= ours (the surface only grows
# within a major). Symbol presence is a capability check, not an ABI
# check — a stale library could export a probe symbol over incompatible
# struct layouts, so the version gate runs at load, before any ctypes
# signature is configured.
REQUIRED_ABI = (0, 2)


def _check_abi(lib: ctypes.CDLL, path: str) -> None:
    try:
        probe = lib.yetty_ffi_version
    except AttributeError:
        raise RuntimeError(
            f"yetty FFI: {path} exports no yetty_ffi_version probe — the "
            "library predates the binding ABI and cannot be used; rebuild it")
    probe.restype = ctypes.c_char_p
    probe.argtypes = []
    text = (probe() or b"0.0.0").decode("ascii", "replace")
    try:
        major, minor = (int(part) for part in text.split(".")[:2])
    except ValueError:
        raise RuntimeError(f"yetty FFI: {path} reports unparsable version {text!r}")
    if major != REQUIRED_ABI[0] or minor < REQUIRED_ABI[1]:
        raise RuntimeError(
            f"yetty FFI: {path} speaks ABI {text}, bindings require "
            f"{REQUIRED_ABI[0]}.{REQUIRED_ABI[1]}+ within major "
            f"{REQUIRED_ABI[0]} — rebuild the library or the bindings")


def _open_library(path: str) -> ctypes.CDLL:
    lib = ctypes.CDLL(path, mode=_LOAD_MODE)
    _check_abi(lib, path)
    return lib


def _candidate_paths() -> list[str]:
    """Ordered library locations tried on first use. `import yetty` alone is
    enough — no explicit load() call and no env var are required in a normal
    dev checkout or an installed wheel. Order, strongest signal first:

      1. $YETTY_FFI_LIB          explicit override
      2. bundled in the package  (an installed wheel ships the .so here)
      3. build trees in the repo (dev checkout: build-desktop-ffi-release, …)
      4. the OS loader           (LD_LIBRARY_PATH / ldconfig / DYLD_*)

    An explicit override short-circuits the rest — probing the build tree is
    O(build-tree size) and pointless once the caller has named the file.
    """
    override = os.environ.get("YETTY_FFI_LIB")
    if override:
        return [override]

    package_dir = Path(__file__).resolve().parent
    repo_root = package_dir.parents[2]  # <repo>/bindings/python/yetty → <repo>
    candidates: list[str] = []

    for basename in _LIB_BASENAMES:
        candidates.append(str(package_dir / basename))

    # Dev checkout: the library builds at a fixed path under each build tree,
    # <repo>/build-desktop-*/src/yetty/yffi/<basename>. Glob only that exact
    # slot — a recursive `**` walk over the build tree scans tens of thousands
    # of object files and dominates startup. Prefer a `-release` tree, then
    # any `build-desktop-*`.
    yffi_subpath = Path("src") / "yetty" / "yffi"
    for basename in _LIB_BASENAMES:
        for build_glob in ("build-desktop-*-release", "build-desktop-*"):
            candidates.extend(sorted(glob.glob(
                str(repo_root / build_glob / yffi_subpath / basename))))

    system = ctypes.util.find_library("yetty_ffi")
    if system:
        candidates.append(system)
    candidates.extend(_LIB_BASENAMES)  # let the loader resolve a bare soname

    seen: set[str] = set()
    ordered: list[str] = []
    for candidate in candidates:
        if candidate not in seen:
            seen.add(candidate)
            ordered.append(candidate)
    return ordered


def load(path: str | None = None) -> ctypes.CDLL:
    """Load the yetty FFI shared library.

    Normally you never call this — the library auto-loads on the first FFI
    call (see `_require`). Call it explicitly only to force a specific build:
    `yetty.runtime.load("/path/to/libyetty_ffi.so")`. The last call wins.
    """
    global _lib
    if path is not None:
        _lib = _open_library(path)
        return _lib

    attempted: list[str] = []
    for candidate in _candidate_paths():
        is_path = os.sep in candidate or (os.altsep and os.altsep in candidate)
        if is_path and not os.path.exists(candidate):
            continue
        try:
            _lib = _open_library(candidate)
            return _lib
        except OSError as error:
            attempted.append(f"{candidate} ({error})")
            continue
        except RuntimeError as error:
            # ABI-incompatible candidate: skip it (a dev checkout may hold
            # several build trees) but keep the diagnosis for the report.
            attempted.append(f"{candidate} ({error})")
            continue

    raise RuntimeError(
        "yetty FFI: could not locate a compatible libyetty_ffi.so. Build it "
        "with `make build-desktop-ffi-release`, or set YETTY_FFI_LIB to its "
        "path.\n"
        f"Tried: {attempted or _candidate_paths()}")


def _require() -> ctypes.CDLL:
    """Return the loaded library, auto-loading it on first use."""
    if _lib is None:
        return load()
    return _lib


def cfn(name: str, restype, argtypes: list):
    """Look up a C symbol and pin its restype/argtypes (so by-value struct
    returns use the correct ABI). Cheap to repeat."""
    fn = getattr(_require(), name)
    fn.restype = restype
    fn.argtypes = argtypes
    return fn


def has_symbol(name: str) -> bool:
    """Whether the loaded FFI library exports `name`. Used to gate
    feature-dependent classes (e.g. Video) on the actual build."""
    try:
        getattr(_require(), name)
        return True
    except (AttributeError, OSError, RuntimeError):
        return False


def cstr(value):
    """Coerce a Python str (or os.PathLike) to UTF-8 bytes for a
    `const char *` argument (bytes and None pass through)."""
    if value is None or isinstance(value, bytes):
        return value
    if isinstance(value, os.PathLike):
        value = os.fspath(value)
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


def take_owned_cstr(address) -> str | None:
    """Adopt an OWNED heap C string result (`char *` per the Result
    contract): copy the bytes out, then release the allocation through the
    library's own allocator-compatible free (NEVER the process libc free
    picked blindly). `address` is the raw pointer kept by a c_void_p
    result-value field."""
    if not address:
        return None
    text = ctypes.string_at(address).decode("utf-8", "replace")
    try:
        release = _require().yetty_ffi_string_release
        release.restype = None
        release.argtypes = [ctypes.c_void_p]
        release(address)
    except (AttributeError, OSError):
        pass  # library predates the export — degrade to the old leak
    return text


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
        error = error_from_c(res.error)
        # Result contract: the receiver owns the heap-linked cause chain and
        # must destroy it. The chain is now copied into Python — release the
        # native side, or every failed call leaks one allocation per cause.
        try:
            destroy = _require().yetty_ycore_error_destroy
            destroy.restype = None
            destroy.argtypes = [type(res.error)]
            destroy(res.error)
        except (AttributeError, OSError):
            pass  # library predates the export — degrade to the old leak
        return Result(error=error)
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
