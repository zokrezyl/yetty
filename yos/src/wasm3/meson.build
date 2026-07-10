# wasm3 — vendored locally in source/.
#
# Source is yos-private's wasm3 fork (zokrezyl/wasm3 with the
# pthread-as-syscall + sibling-runtime extensions). Treated like
# locally-maintained code, not fetched. Copied wholesale once; future
# updates by re-running tools/sync-wasm3.sh (TBD).

wasm3_sources = files(
  'source/m3_api_libc.c',
  'source/m3_atomic.c',
  'source/m3_bind.c',
  'source/m3_code.c',
  'source/m3_compile.c',
  'source/m3_core.c',
  'source/m3_emit.c',
  'source/m3_env.c',
  'source/m3_exec.c',
  'source/m3_function.c',
  'source/m3_info.c',
  'source/m3_module.c',
  'source/m3_optimize.c',
  'source/m3_parse.c',
)

wasm3_extra_inc = []
if host_machine.system() == 'windows'
  # Windows SDK / msvcrt have no <sys/mman.h>; provide a tiny
  # VirtualAlloc-backed shim. wasm3's m3_core.c uses
  # MAP_ANONYMOUS|MAP_PRIVATE only, so the shim covers what's actually
  # called.
  wasm3_extra_inc += include_directories(
    '../yos/platform/windows/compat')
endif

_cc = meson.get_compiler('c')
_gnu_like = _cc.get_id() in ['gcc', 'clang', 'clang-cl']
_msvc_like = _cc.get_argument_syntax() == 'msvc'

wasm3_c_args = ['-Dd_m3RecordBacktraces=1']
if _gnu_like
  wasm3_c_args += [
    '-D_GNU_SOURCE',
    '-Wno-unused-parameter',
    '-Wno-unused-but-set-variable',
    '-Wno-unused-variable',
    '-Wno-implicit-fallthrough',
    '-Wno-unused-function',
    '-Wno-array-bounds',
    '-Wno-stringop-overread',
    '-Wno-stringop-overflow',
    '-Wno-maybe-uninitialized',
    '-Wno-format-overflow',
    '-Wno-tautological-compare',
    '-Wno-pedantic',
    '-fno-strict-aliasing',
  ]
endif
if _msvc_like
  # Force-include the __atomic_* / __ATOMIC_* shim so wasm3's m3_exec.h /
  # m3_atomic.c get GCC-style builtins mapped onto MSVC's _Interlocked*
  # intrinsics without touching the vendored source.
  wasm3_c_args += ['/FIm3_msvc_atomics.h']
  # /wd<n> = disable warning. The set below covers the same noise the
  # gcc -Wno-* flags above silence (unused, fallthrough, type-pun,
  # signed/unsigned mismatch on the wasm-cell reads).
  wasm3_c_args += [
    '/wd4100',  # unused parameter
    '/wd4101',  # unreferenced local variable
    '/wd4102',  # unreferenced label
    '/wd4189',  # local variable initialised but not referenced
    '/wd4244',  # conversion possible loss of data
    '/wd4267',  # conversion from size_t to smaller
    '/wd4310',  # cast truncates constant value
    '/wd4334',  # << of 32-bit value implicitly extended to 64-bit
    '/wd4456',  # declaration hides previous local
    '/wd4459',  # declaration hides global
    '/wd4477',  # printf format mismatch
    '/wd4701',  # potentially uninitialised
    '/wd4703',  # potentially uninitialised pointer
    '/wd4706',  # assignment within conditional
    '/wd4828',  # illegal character
    '/wd5045',  # spectre mitigation insertion
    '/Zc:preprocessor',     # conforming preprocessor (VA_ARGS, __VA_OPT__)
    '/experimental:c11atomics',
    '/D_CRT_SECURE_NO_WARNINGS',
    '/D_CRT_NONSTDC_NO_WARNINGS',
    '/DNOMINMAX',
    '/DWIN32_LEAN_AND_MEAN',
  ]
endif

wasm3_lib = static_library(
  'wasm3',
  wasm3_sources,
  # wasm3 is an interpreter that uses indirect-call dispatch from one
  # opcode handler to the next. At -O2/-O3 the compiler turns those
  # into sibling calls (no host-stack growth), but at -O0 every
  # dispatched opcode adds a frame — a guest function with thousands
  # of opcodes blows the host pthread stack and SIGSEGV's at the
  # call instruction (rsp on guard page). Pin -O3 here so the project
  # buildtype (debug, release, …) doesn't break the dispatch loop.
  override_options : ['optimization=3'],
  c_args : wasm3_c_args,
  include_directories : [include_directories('source')] + wasm3_extra_inc,
  install : false,
)

wasm3_dep = declare_dependency(
  link_with           : wasm3_lib,
  include_directories : [include_directories('source')] + wasm3_extra_inc,
)
