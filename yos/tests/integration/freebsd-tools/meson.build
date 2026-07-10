# tests/integration/freebsd-tools — drive the wasm-built FreeBSD
# coreutils second-batch ports under yos.
#
# These tests prove (or honestly xfail) what we built in
# nixpkgs/pkgs/freebsd-tools/. Each test follows the same shape:
#   1. discover yos + libexec via _tools_path.find_yos_and_tools
#      (no nix builds — read-only lookup).
#   2. run the tool with a small realistic input.
#   3. compare stdout/stderr/rc against either the happy-path expectation
#      or the documented failure mode.
#
# An xfail test that flips to UNEXPECTEDPASS means the runtime gap is
# fixed — drop the xfail in the same commit as the fix.
#
# Discovery requires a recent `nix build .#all` (yields the umbrella
# tree with both bin/yos and libexec/<tool>) or two separate
# `nix build .#yos` + `nix build .#freebsd-tools` invocations.

py3 = import('python').find_installation('python3')

ftools_int_env = environment()
ftools_int_env.set('YOS_REPO_ROOT', meson.project_source_root())

# Each entry: name → { src, xfail }. xfail captures the runtime
# gap that's currently blocking — see the docstring at the top of
# each test_*.py for the specific failure mode + suggested fix.
ftools_int_tests = {
  'wc'    : {'src' : files('test_wc.py'),    'xfail' : false},
  'df'    : {'src' : files('test_df.py'),    'xfail' : false},
  'awk'   : {'src' : files('test_awk.py'),   'xfail' : true},
  'cut'   : {'src' : files('test_cut.py'),   'xfail' : true},
  'tr'    : {'src' : files('test_tr.py'),    'xfail' : true},
  'grep'  : {'src' : files('test_grep.py'),  'xfail' : true},
  'sed'   : {'src' : files('test_sed.py'),   'xfail' : true},
  'sort'  : {'src' : files('test_sort.py'),  'xfail' : true},
  'xargs' : {'src' : files('test_xargs.py'), 'xfail' : true},
  'find'  : {'src' : files('test_find.py'),  'xfail' : true},
  'du'    : {'src' : files('test_du.py'),    'xfail' : true},
  # New batch: fts(3) is wired in, host opendir/readdir/closedir
  # bridges live in src/yos/impl/dir.c. ls/cp/find/du now actually
  # walk filesystems. The xfail entries below mark ports that
  # compile but trip on a separate libc gap (setmode, sscanf,
  # sys_signame, host uuidgen).
  'ls'         : {'src' : files('test_ls.py'),         'xfail' : false},
  'ls-tty'     : {'src' : files('test_ls_tty.py'),     'xfail' : false},
  'echo'       : {'src' : files('test_echo.py'),       'xfail' : false},
  'hostname'   : {'src' : files('test_hostname.py'),   'xfail' : false},
  'mkdir_rmdir': {'src' : files('test_mkdir_rmdir.py'),'xfail' : false},
  'cp'         : {'src' : files('test_cp.py'),         'xfail' : false},
  'rm'         : {'src' : files('test_rm.py'),         'xfail' : false},
  'mv'         : {'src' : files('test_mv.py'),         'xfail' : false},
  'ln'         : {'src' : files('test_ln.py'),         'xfail' : false},
  'realpath'   : {'src' : files('test_realpath.py'),   'xfail' : false},
  'test'       : {'src' : files('test_test.py'),       'xfail' : false},
  'chmod'      : {'src' : files('test_chmod.py'),      'xfail' : true},
  'sleep'      : {'src' : files('test_sleep.py'),      'xfail' : true},
  'uuidgen'    : {'src' : files('test_uuidgen.py'),    'xfail' : true},
  'kill'       : {'src' : files('test_kill.py'),       'xfail' : true},
  'ps'         : {'src' : files('test_ps.py'),         'xfail' : false},
  'ps-tty'     : {'src' : files('test_ps_interactive.py'), 'xfail' : false},
  'top'        : {'src' : files('test_top.py'),        'xfail' : false},
  'bsdcat'     : {'src' : files('test_bsdcat.py'),     'xfail' : false},
  'whois'      : {'src' : files('test_whois.py'),      'xfail' : false},
  'nc'         : {'src' : files('test_nc.py'),         'xfail' : false},
  'telnet'     : {'src' : files('test_telnet.py'),     'xfail' : false},
  'backspace'  : {'src' : files('test_backspace.py'),  'xfail' : false},
  'nvim-zsh'   : {'src' : files('test_nvim_from_zsh.py'),'xfail' : false},
}

# Tests that drive heavy interactive guests through a pty are
# CPU-bound on the wasm interpreter and have timing-sensitive
# expectations; running them in parallel produces flaky drops. Keep
# the cheap byte-in/byte-out tests parallel and serialise only the
# pty-driven ones (~10s each).
_serial_ftools = {
  'nvim-zsh'  : true,
  'ps-tty'    : true,
  'backspace' : true,
  'top'       : true,
}

# Per-platform xfail overrides — see the parallel comment in
# tests/integration/zsh/meson.build for the pattern.
#   cut — darwin-only pass: the rune-locale isalpha/isspace gap that
#       breaks cut's delimiter parsing on Linux doesn't fire on darwin
#       (libSystem's locale tables ship differently). Flip the xfail
#       off here so meson reports a clean run.
_platform_xfail = {}
if host_machine.system() == 'darwin'
  _platform_xfail = {
    'cut' : false,
  }
endif

# pty-driven tests use Python's `pty` module (Unix-only). On Windows
# strip just those entries from the iteration; the byte-in/byte-out
# tests still run.
if host_machine.system() == 'windows'
  _skip_windows = ['ls-tty', 'ps-tty', 'backspace', 'nvim-zsh', 'top']
  _filtered = {}
  foreach _k, _v : ftools_int_tests
    if not _skip_windows.contains(_k)
      _filtered += {_k : _v}
    endif
  endforeach
  ftools_int_tests = _filtered
endif

# Several xfail markers cover known runtime gaps on Linux/darwin that
# happen NOT to fire on Windows. Flip those off so the meson report
# shows OK instead of UNEXPECTEDPASS.
if host_machine.system() == 'windows'
  _platform_xfail += {
    'awk'      : false,  'cut'      : false,  'tr'   : false,
    'grep'     : false,  'sed'      : false,  'sort' : false,
    'xargs'    : false,  'du'       : false,  'find' : false,
    'chmod'    : false,  'sleep'    : false,  'kill' : false,
    'uuidgen'  : false,
  }
endif

foreach name, cfg : ftools_int_tests
  xfail = _platform_xfail.has_key(name) ? _platform_xfail[name] : cfg['xfail']
  test('freebsd-tool-' + name,
       py3,
       args : cfg['src'],
       suite : 'integration-freebsd-tools',
       env : ftools_int_env,
       depends : [yos_exe],
       should_fail : xfail,
       is_parallel : not _serial_ftools.get(name, false),
       timeout : 30)
endforeach
