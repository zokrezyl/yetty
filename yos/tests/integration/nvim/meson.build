# tests/integration/nvim — black-box tests that drive nvim through a pty
# and verify behaviour via captured terminal output.
#
# These run only when the nvim wasm package is present at
# build-linux/wasm-pkgs/nvim-0.10.4/out/bin/nvim.wasm. Build it via:
#   tools/wasm-pkg.sh nvim
# and the yos host binary via the standard meson build.
#
# Each test is a self-contained python3 script under this directory; it
# uses run_in_pty.py (a thin pty.fork + script-style wrapper) to spawn
# yos+nvim with a controlled pty size, drives it with timed keystrokes,
# and exits 0 on PASS / non-zero on FAIL with diagnostic output.
#
# YOS_REPO_ROOT is passed via env so tests can locate the yos binary
# and nvim.wasm without depending on the test runner's cwd.

py3 = import('python').find_installation('python3')
nvim_wasm = meson.project_build_root() / 'wasm-pkgs' / 'nvim-0.10.4' \
                                       / 'out' / 'bin' / 'nvim.wasm'

nvim_int_env = environment()
nvim_int_env.set('YOS_REPO_ROOT', meson.project_source_root())
# Tell tests where the host yos binary lives (build dir basename, e.g.
# build-linux on linux, build-darwin on darwin). POSIX hosts have a
# native `basename`; on Windows we shell out to Python instead.
if host_machine.system() == 'windows'
  yos_build_basename = run_command(
    py, '-c', 'import sys, os; print(os.path.basename(sys.argv[1]))',
    meson.project_build_root(), check : true).stdout().strip()
else
  yos_build_basename = run_command('basename', meson.project_build_root(),
                                   check : true).stdout().strip()
endif
nvim_int_env.set('YOS_BUILD_DIR', yos_build_basename)

nvim_int_tests = {
  'quit'         : {'src' : files('test_quit.py'),         'xfail' : false},
  'winsize'      : {'src' : files('test_winsize.py'),      'xfail' : false},
  'edit-session' : {'src' : files('test_edit_session.py'), 'xfail' : false},
  # Pins the user-reported "no text input" symptom. Drives nvim with
  # 'iHELLO<Esc>:q!<CR>' and verifies HELLO appears in the pty output.
  'input'        : {'src' : files('test_input.py'),        'xfail' : false},
  # :terminal round trip — forkpty, $SHELL host-path basename exec
  # resolution, iconv, embedded-shell I/O, SIGCHLD reap, clean :q!.
  # Generous timeout: the embedded shell is a SECOND interpreted guest
  # and its cold boot has been observed north of 2 minutes under load.
  'terminal'     : {'src' : files('test_terminal.py'),     'xfail' : false,
                    'timeout' : 480},
}

# The harness uses Python's `pty` module (Unix-only). Windows would
# need a ConPTY-based rewrite — skip until that exists.
if host_machine.system() == 'windows'
  nvim_int_tests = {}
endif

foreach name, cfg : nvim_int_tests
  # is_parallel: false — each test drives a full nvim TUI through a
  # pty with hard-coded timing windows (`(2.5, b"")` etc. — wait 2.5s
  # for nvim to settle before sending the next keystroke). Under
  # parallel test load every yos host is competing for CPU, the wasm
  # interpreter is much slower than expected, nvim hasn't drawn the
  # screen yet when `:q!` arrives, and the input is dropped. The test
  # then SIGKILLs after its 2s grace and reports status=9. Cheap to
  # serialise — each test runs in ~10s and there are only 4 of them.
  test('nvim-' + name,
       py3,
       args : cfg['src'],
       suite : 'integration-nvim',
       env : nvim_int_env,
       depends : [yos_exe],
       should_fail : cfg['xfail'],
       is_parallel : false,
       # :terminal boots a SECOND interpreted guest (the embedded zsh)
       # inside nvim, so its timed driver needs ~1 min of wall clock;
       # the darwin timing multiplier can triple that.
       timeout : cfg.get('timeout', 30))
endforeach
