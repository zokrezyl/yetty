# tests/integration/pty — black-box tests that drive a tiny wasm
# echo program through a real pty, isolating the TTY-input path
# from nvim's full TUI complexity.
#
# These pin the bug the user reported: nvim runs but no text input
# arrives, and Ctrl-C exits the process. Both stem from the host-side
# kqueue/pty/signal plumbing, not from anything specific to nvim.

py3_pty = import('python').find_installation('python3')

if host_machine.system() == 'windows'
  _wasm_clang_basename_pty = 'wasm-clang.cmd'
else
  _wasm_clang_basename_pty = 'wasm-clang'
endif
wasm_cc_pty  = find_program(meson.project_source_root() / 'build-tools'
                                                       / _wasm_clang_basename_pty,
                            required: true)
sysroot_dir_pty = meson.project_build_root() / 'sysroot'

wasm_cflags_pty = [
  '-target', 'wasm32-unknown-unknown',
  '-nostdlib',
  '--sysroot=' + sysroot_dir_pty,
  '-isystem', sysroot_dir_pty / 'usr' / 'include',
  '-Wl,--no-entry',
  '-Wl,--export=_start',
  '-Wl,--export-all',
  '-Wl,--allow-undefined',
  '-O2',
]

pty_echo_wasm = custom_target(
  'pty_echo.wasm',
  input            : files('pty_echo.c'),
  output           : 'pty_echo.wasm',
  depends          : sysroot_skel,
  command          : [wasm_cc_pty, wasm_cflags_pty, '-o', '@OUTPUT@', '@INPUT0@'],
  build_by_default : true,
)

# fork() variant — needs Binaryen asyncify post-pass like
# tests/ut/libc/test_fork_basic so the host runtime can unwind/rewind
# the wasm call stack at the fork point.
wasm_opt_pty = find_program('wasm-opt', required: true)
pty_echo_fork_raw = custom_target(
  'pty_echo_fork-raw.wasm',
  input            : files('pty_echo_fork.c'),
  output           : 'pty_echo_fork-raw.wasm',
  depends          : sysroot_skel,
  command          : [wasm_cc_pty, wasm_cflags_pty, '-o', '@OUTPUT@', '@INPUT0@'],
  build_by_default : true,
)
pty_echo_fork_wasm = custom_target(
  'pty_echo_fork.wasm',
  input            : pty_echo_fork_raw,
  output           : 'pty_echo_fork.wasm',
  command          : [wasm_opt_pty, '--asyncify', '-o', '@OUTPUT@', '@INPUT@'],
  build_by_default : true,
)

pty_int_env = environment()
pty_int_env.set('YOS_REPO_ROOT', meson.project_source_root())
if host_machine.system() == 'windows'
  yos_build_basename_pty = run_command(
    py, '-c', 'import sys, os; print(os.path.basename(sys.argv[1]))',
    meson.project_build_root(), check : true).stdout().strip()
else
  yos_build_basename_pty = run_command('basename', meson.project_build_root(),
                                        check : true).stdout().strip()
endif
pty_int_env.set('YOS_BUILD_DIR', yos_build_basename_pty)

pty_int_tests = {
  'input-blocking'    : files('test_input_blocking.py'),
  'input-kqueue'      : files('test_input_kqueue.py'),
  'input-fork-kqueue' : files('test_input_fork_kqueue.py'),
  'input-cooked'      : files('test_input_cooked.py'),
  'input-kqueue-cooked' : files('test_input_kqueue_cooked.py'),
  'ctrlc'             : files('test_ctrlc.py'),
}

# Tests drive yos through Python's `pty` module (Unix-only). Windows
# port would need a ConPTY-based runner; skip until then.
if host_machine.system() == 'windows'
  pty_int_tests = {}
endif

foreach name, src : pty_int_tests
  test('pty-' + name,
       py3_pty,
       args    : src,
       suite   : 'integration-pty',
       env     : pty_int_env,
       depends : [yos_exe, pty_echo_wasm, pty_echo_fork_wasm],
       timeout : 30)
endforeach
