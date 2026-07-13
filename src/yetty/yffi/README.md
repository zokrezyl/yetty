# yffi — libyetty_ffi.so, the FFI aggregation for language bindings

`yffi` builds **`libyetty_ffi.so`**, the shared library the host-language
bindings under `bindings/<lang>/` load (Python via `ctypes.CDLL`, Lua via its
FFI) to call the generated `yetty_<module>_*` stubs of every FFI-exposed
[yclass](../yclass/README.md) module. It exists only in the dedicated PIC
"double build" tree (`YETTY_BUILD_FFI_SHARED=ON`,
`make build-desktop-ffi-release`); the static application build never links
it, so the fast non-PIC archives stay uncompromised.

## What the one source file provides

`yffi.c` is deliberately tiny — the library's real content is the linked
module archives:

```c
/* Version probe — bindings call this to confirm they loaded the
 * expected library. Bumped alongside the binding ABI. */
const char *yetty_ffi_version(void);

/* fd-backed platform_pty: write() blocks until flushed, read() yields
 * nothing. Lets a binding hand the ygui framework / yview a pty that is
 * just its own stdout (= the hosting yetty's PTY). The pty ops return
 * Result structs by value, which ctypes cannot synthesize from a Python
 * callback — hence this C shim. */
struct yetty_platform_pty *yetty_yffi_fd_pty_create(int fd);
void yetty_yffi_fd_pty_destroy(struct yetty_platform_pty *self);
```

See [`yplatform`](../yplatform/README.md) for the `platform_pty` contract.

## How the library is assembled

`CMakeLists.txt` does the heavy lifting:

- **Whole-archive force-include** of every FFI-exposed yclass module
  (`ychrome ycircuit yfigure yflame ygrid ygui yguiapp ymap ymgui ymusic
  yrdawn yrich yshadertoy yview yvterm yplatform_core yclass ywire`) so
  their stubs are exported even though nothing in this TU references them.
  Kept in sync with `bindings/python/yetty/generated/__init__.py`: if
  codegen emits a Python package for a module, the `.so` must export its C
  symbols.
- **Bootstrap sources compiled inline** (yframework, the desktop yplatform
  backends, ypty, yconfig, yrich app, …) because generated app classes such
  as `yguiapp`/`yrich` reference the concrete desktop platform — the shared
  object must resolve them the same way the yetty executable does.
- **`-Wl,--no-undefined`** so a missing transitive module fails the link at
  build time instead of surfacing as a dlopen error in the bindings.
- GLFW's static archives are linked as files and paired with dynamic
  X11/Wayland libs (the imported targets propagate non-PIC archives tuned
  for executables).

## The binding pipeline around it

1. `make codegen` — per-module `model.yaml` + C glue from the annotated
   sources ([yclass](../yclass/README.md)).
2. `make ffi` — `tools/ffi-codegen/{python,lua}/ffigen.py` turn the models
   into `bindings/python/yetty/generated/*.py` and the Lua equivalents. See
   [`docs/ffi-gen.md`](../../../docs/ffi-gen.md).
3. `make build-desktop-ffi-release` — builds this library.
4. Client code: `yetty.runtime.load("/path/to/libyetty_ffi.so")` (or the
   env-var default) and every generated class is callable.

## Layout of the module

| file | role |
|------|------|
| `yffi.c` | version probe + fd-backed `platform_pty` shim |
| `CMakeLists.txt` | the shared-object link recipe (whole-archive set, bootstrap sources, no-undefined) |

There is no `include/yetty/yffi/` header: the exported surface is the union
of the generated module headers plus the two helpers above, which bindings
resolve by symbol name.
