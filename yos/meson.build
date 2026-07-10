# yos build graph — meson root
#
# Build dir convention (enforced by `meson setup build-<host>`):
#   build-linux    on Linux hosts
#   build-darwin   on macOS hosts
#   build-freebsd  on FreeBSD hosts
#
# Each platform-specific build dir holds the entire derived tree —
# fetched source archives, extracted FreeBSD headers, abi.yaml,
# generated bridge code, wasm32 sysroot, host yos binary, guest .wasm
# packages. The repo source tree never gets written to.

project('yos', 'c',
  version       : '0.1',
  meson_version : '>=1.3',
  default_options : [
    'c_std=c11',
    'warning_level=2',
  ],
)

# Sanity: refuse in-source builds. With the build-<platform> convention
# we want users to learn the right pattern from the start.
if meson.current_source_dir() == meson.current_build_dir()
  error('In-source builds are not allowed. Use `meson setup build-linux` (or build-darwin / build-freebsd) instead.')
endif

# Recommended build-dir name based on host OS. Doesn't enforce — meson
# can't easily reject a build dir's name — but documents the convention.
host_os = host_machine.system()
recommended_build = 'build-' + host_os
message('Building for host OS: @0@ (recommended dir: @1@)'.format(host_os, recommended_build))

# Use the project's uv-managed venv for python helpers — it has the
# `yaml` and `clang.cindex` modules pre-installed (see pyproject.toml).
# `python3` from a bare nix profile won't have them.
fs = import('fs')
project_venv_py_posix = meson.current_source_dir() / '.venv/bin/python3'
project_venv_py_win   = meson.current_source_dir() / '.venv/Scripts/python.exe'
if fs.exists(project_venv_py_posix)
  py = find_program(project_venv_py_posix, required: true)
elif fs.exists(project_venv_py_win)
  py = find_program(project_venv_py_win, required: true)
else
  py = import('python').find_installation('python3')
  msg = 'No .venv/ found at @0@ or @1@. Falling back to system python3 — helper scripts will fail if pyyaml / libclang aren\'t available. Run `uv venv && uv sync` (or `python -m venv .venv && .venv/Scripts/pip install pyyaml clang libclang`) in the source root.'
  warning(msg.format(project_venv_py_posix, project_venv_py_win))
endif

subdir('build-tools')
subdir('src')
# Tests run yos as a host binary against the wasm sysroot, so they
# only make sense when we're building the executable. Skip when
# cross-building the static lib (tvOS / iOS app bundles).
if not get_option('build_static_lib')
  subdir('tests')
endif
