# Vendored mimalloc — pinned commit fetched into <build>/build-tools/mimalloc/mimalloc/.
# Compiled as a static lib for the *host* (yos itself, not the wasm guest).
# Per-process arenas are bound to each ctx's wasm linear memory at runtime
# via `mi_manage_os_memory`, so mi_malloc returns host pointers that lie
# inside the guest's linear-memory blob — yos converts them to wasm
# offsets at the bridge boundary.

mimalloc_commit = 'b1963961a5cdb1996c9ad2e356014089b7a94ea3'  # main as of 2026-04

mimalloc_src = custom_target(
  'mimalloc-src',
  output           : 'mimalloc.stamp',
  command          : [
    py, files('fetch.py'),
    '--commit', mimalloc_commit,
    '--out',    '@OUTDIR@',
  ],
  build_by_default : false,
  console          : true,
)

mimalloc_shim = custom_target(
  'mimalloc-shim-c',
  input            : mimalloc_src,
  output           : 'mimalloc_unity.c',
  command          : [
    py, files('emit_unity.py'),
    '--source-dir', '@OUTDIR@/mimalloc/src',
    '--output',     '@OUTPUT@',
  ],
  build_by_default : false,
)

mimalloc_include_dir = meson.current_build_dir() / 'mimalloc' / 'include'

mimalloc_lib = static_library(
  'mimalloc',
  mimalloc_shim,
  c_args : [
    '-D_GNU_SOURCE',
    '-DMI_MALLOC_OVERRIDE=0',  # don't intercept host malloc
    '-DMI_STATIC_LIB',
    '-Wno-unused-parameter',
    '-Wno-unused-but-set-variable',
    '-Wno-unused-variable',
    '-Wno-unused-function',
    '-Wno-implicit-fallthrough',
    '-Wno-array-bounds',
    '-Wno-stringop-overread',
    '-Wno-stringop-overflow',
    '-Wno-maybe-uninitialized',
    '-Wno-format-overflow',
    '-Wno-tautological-compare',
    '-Wno-pedantic',
    # mimalloc's static.c includes the per-source files — give them the
    # mimalloc internal include path.
    '-I' + meson.current_build_dir() / 'mimalloc' / 'src',
    # Public mimalloc.h lives under mimalloc/include/. The internal
    # sources #include "mimalloc.h" without the include/ prefix.
    '-I' + meson.current_build_dir() / 'mimalloc' / 'include',
  ],
  include_directories : include_directories('.'),
  install : false,
)

mimalloc_dep = declare_dependency(
  link_with           : mimalloc_lib,
  include_directories : include_directories('.'),
  # Public header lives at <build>/build-tools/mimalloc/mimalloc/include/mimalloc.h.
  # Consumers do `#include "mimalloc.h"`.
  compile_args        : ['-I' + mimalloc_include_dir],
)
