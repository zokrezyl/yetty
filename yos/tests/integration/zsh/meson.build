# tests/integration/zsh — drive the wasm-built zsh under yos.
#
# These tests assume `nix build .#zsh` has already been run (the
# discovery code looks up the result via `nix path-info .#zsh`).
# If that fails, tests print SKIP and exit 0 — non-fatal so meson
# can still build clean on a checkout that hasn't built zsh yet.
#
# Pass YOS_ZSH_WASM=/abs/path/to/zsh.wasm to override discovery
# (useful when iterating without going through nix).

py3 = import('python').find_installation('python3')

zsh_int_env = environment()
zsh_int_env.set('YOS_REPO_ROOT', meson.project_source_root())
if host_machine.system() == 'windows'
  zsh_build_basename = run_command(
    py, '-c', 'import sys, os; print(os.path.basename(sys.argv[1]))',
    meson.project_build_root(), check : true).stdout().strip()
else
  zsh_build_basename = run_command('basename', meson.project_build_root(),
                                   check : true).stdout().strip()
endif
zsh_int_env.set('YOS_BUILD_DIR', zsh_build_basename)

zsh_int_tests = {
  'script-mode'      : {'src' : files('test_script_mode.py'),      'xfail' : false},
  'grammar'          : {'src' : files('test_grammar.py'),          'xfail' : false},
  'env-inherit'      : {'src' : files('test_env_inherit.py'),      'xfail' : false},
  'external-missing' : {'src' : files('test_external_missing.py'), 'xfail' : false},
  # Pinned regression for the runhookdef indirect-call-type-mismatch
  # trap (root cause: invalidate_list / accept_last registered as
  # Hookfn with the wrong wasm signature). Patched in
  # build-tools/wasm-pkg/configs/zsh/build.sh — keep this test green
  # so the trampolines aren't accidentally dropped.
  'runhookdef-trap'  : {'src' : files('test_runhookdef_trap.py'),  'xfail' : false},
  # Tests that used to be xfail and were promoted as their underlying
  # bug got fixed. Kept as regular passing tests so the regression net
  # catches the next time someone touches the relevant code path.
  #   silent-builtins      → fixed: nixpkgs/sysroot/default.nix now
  #                           defines __stdinp/__stdoutp/__stderrp as
  #                           the small-int handles 1/2/3 the bridge
  #                           understands. Without those, every
  #                           fputs/fwrite/printf got a NULL host FILE*
  #                           and silently dropped.
  #   external-diagnostic  → fixed alongside silent-builtins (same root
  #                           cause: zsh's "command not found" text is
  #                           emitted via stderr stdio).
  #   env-passthrough      → fixed: build-tools/sysroot/skel.sh's crt1
  #                           now calls __yos_envc + __yos_envp_setup,
  #                           defines `char **environ` as a static
  #                           backing array of 256 entries, and
  #                           populates it before main(). Without this
  #                           zsh's createparamtable() walked a NULL
  #                           environ and never imported PATH/HOME.
  'silent-builtins'     : {'src' : files('test_silent_builtins.py'),     'xfail' : false},
  'external-diagnostic' : {'src' : files('test_external_diagnostic.py'), 'xfail' : false},
  'env-passthrough'     : {'src' : files('test_env_passthrough.py'),     'xfail' : false},
  # User-reported "backspace = space" bug. ZLE emits the right
  # `\b \b` sequence; the visible corruption was caused by a yos
  # fork-asyncify leak — a forked child wrote 8 bytes of 0xff to
  # the dup'd tty fd, modern UTF-8 terminals rendered those as
  # replacement glyphs, and the destructive-backspace's column
  # accounting fell apart. Workaround in vfs.c::yos_write +
  # file.c::yos_fwrite drops short pure-0xff writes going to a
  # tty (isatty(hfd)==1). The test asserts NO 0xff garbage in the
  # post-backspace byte stream. The deeper proc.c fork-snapshot
  # bug is still open; YOS_NO_FF_DROP=1 disables the workaround.
  'backspace'           : {'src' : files('test_backspace.py'),           'xfail' : false},
  # Bug pins still standing (xfail). Each flips to UNEXPECTEDPASS when
  # fixed — remove the xfail entry as part of the fix commit.
  #
  # pipe-hang / cmdsubst-hang were in the same xfail family as the
  # ssh stderr leak. Root cause: the forked child's host fd map was
  # populated by the CHILD THREAD's F_DUPFD loop, which ran AFTER the
  # parent's wasm code resumed and reused host fd numbers — pipe
  # write fds ended up duplicating host stdout, etc. Fixed by moving
  # the dup loop into yos_fork_pump (parent thread, BEFORE resume)
  # so the snapshot captures stable host fds. See proc.c diff.
  'pipe-hang'           : {'src' : files('test_pipe_hang.py'),           'xfail' : false},
  'cmdsubst-hang'       : {'src' : files('test_cmdsubst_hang.py'),       'xfail' : false},
  # ctrl-c-cancel is a separate signal/asyncify issue (Ctrl-C at the
  # prompt kills zsh instead of cancelling the line). Still xfail.
  'ctrl-c-cancel'       : {'src' : files('test_ctrl_c_cancel.py'),       'xfail' : true},
  # Catch-all "external command crashes zsh" regression. Each new
  # tool port adds a one-line entry to CASES in
  # test_external_tty_survival.py; the day yos's fork+exec fd
  # plumbing breaks the TTY, this test goes red instead of us
  # finding it by hand later. Self-handles xfail per case so the
  # meson outer xfail stays false (the whole test still PASSes
  # when nothing's regressed and the per-case xfails are correct).
  'external-tty-survival' : {'src' : files('test_external_tty_survival.py'),
                             'xfail' : false},
  # `echo ssh | yos zsh` should print ssh's usage banner. Today the
  # forked ssh's first stderr write surfaces with an asyncify-rewind
  # 0xff prefix and "Bad file descriptor" with its leading 'B' eaten
  # — same root cause as tests/ut/libc/test_post_fork_stderr_clean.c
  # (xfail there too). Flip both sides when the fork-stderr leak is
  # fixed.
  'ssh-no-args-help'    : {'src' : files('test_ssh_no_args_help.py'),
                           'xfail' : true},
  # Sister of ssh-no-args-help. Now an expected-pass: it asserts only
  # that the forked child's stderr is byte-clean (no 0xff prefix),
  # which is exactly what the proc.c fork-fd-snapshot fix repairs.
  # The end-to-end "ssh prints its banner" assertion (ssh-no-args-help)
  # stays xfail because ssh additionally exits silently when started
  # from a non-tty stdin — separate downstream issue.
  'libexec-usage-clean' : {'src' : files('test_libexec_usage_clean.py'),
                           'xfail' : false},
  # User-reported bug: INTERACTIVE `yos zsh` (PTY stdin,
  # PATH=$libexec, NO `-i` flag — matching what tools/yos.sh does)
  # typing `ssh<Enter>` used to produce "ad file descriptor" garbage
  # and kill zsh with "error on TTY read". Fixed by the proc.c
  # fork-fd-snapshot rework. external-tty-survival.py uses
  # PATH=/bin:/usr/bin so its `ssh` case never actually executes
  # the wasm ssh — false positive there. This test points PATH at
  # libexec so the bug actually fires if it regresses.
  'interactive-ssh-clean' : {'src' : files('test_interactive_ssh_clean.py'),
                             'xfail' : false},
  # End-to-end `zsh -fc 'cd <tmp>; ls -alrt'` against a scratch dir
  # we built ourselves. Asserts every long-format column (mode +
  # owner + group + size + -t ordering) on lines we know the exact
  # value for — catches strmode / user_from_uid / group_from_gid /
  # struct stat layout regressions before they look like "ls output
  # is mangled" to the user.
  #
  # The 'yos-tool: ./X: Undefined error: 0' fts info-leak that this
  # test caught earlier is FIXED — the regression net for that
  # specific bug now lives in tests/ut/libc/test_lpathconf_errno.c
  # (the bridge contract: yos_lpathconf must set wasm errno on
  # failure). Re-running the integration test directly passes.
  #
  # The test now stat()s each file individually for its expected
  # uid/gid (was using os.getgid() — wrong on macOS where /var/folders
  # tempdirs carry the sgid bit and inherit 'wheel' onto new files
  # rather than the process's primary group). xfail:false.
  'ls-output'           : {'src' : files('test_ls_output.py'),
                           'xfail' : false},
}

# Pty-driven tests with hard-coded timing windows; run serially so
# CPU contention from parallel meson tests doesn't cause :q!/Enter
# to be sent before the wasm guest has finished drawing the screen.
_serial_zsh = {
  'backspace'             : true,
  'interactive-ssh-clean' : true,
  'external-tty-survival' : true,
  'ssh-no-args-help'      : true,
  'runhookdef-trap'       : true,
  'ctrl-c-cancel'         : true,
}

# Per-platform xfail overrides. Entries set true to xfail; entries set
# false flip the table-level xfail off (because the bug fixed itself
# on this platform).
#   ssh-no-args-help — darwin-only pass: the fork-stderr 0xff prefix
#       this test pins doesn't surface on darwin (different asyncify
#       rewind path under apple-clang's wasm output), so the test
#       passes here even though Linux still hits it. Flip off the
#       xfail so meson reports a clean run.
_platform_xfail = {}
if host_machine.system() == 'darwin'
  _platform_xfail = {
    'ssh-no-args-help'      : false,
  }
endif
if host_machine.system() == 'windows'
  _platform_xfail = {
    'ssh-no-args-help'      : false,
  }
endif

# Strip pty-using tests on Windows (Python's pty is Unix-only). The
# non-pty ones (script-mode, grammar, env-inherit, …) keep running.
if host_machine.system() == 'windows'
  _zsh_skip_windows = ['backspace', 'external-tty-survival',
                       'interactive-ssh-clean', 'ctrl-c-cancel',
                       'runhookdef-trap']
  _zsh_filtered = {}
  foreach _k, _v : zsh_int_tests
    if not _zsh_skip_windows.contains(_k)
      _zsh_filtered += {_k : _v}
    endif
  endforeach
  zsh_int_tests = _zsh_filtered
endif

foreach name, cfg : zsh_int_tests
  xfail = _platform_xfail.has_key(name) ? _platform_xfail[name] : cfg['xfail']
  test('zsh-' + name,
       py3,
       args : cfg['src'],
       suite : 'integration-zsh',
       env : zsh_int_env,
       depends : [yos_exe],
       should_fail : xfail,
       is_parallel : not _serial_zsh.get(name, false),
       timeout : 30)
endforeach

# tvOS-only round-trip test: drives a wasm zsh on a paired Apple TV
# over telnet. Skipped automatically (exit 0 with "SKIP: ...") when
# YOS_TVOS_LAN_IP isn't set or the device isn't reachable, so it's
# safe to keep in the default suite — it's a no-op on CI / hosts
# without a tvOS device. Set YOS_TVOS_LAN_IP=192.168.1.9 (or use
# build-tools/tvos/deploy.sh which gives the IP in its banner) to
# enable the live-device check.
test('zsh-tvos-telnet',
     py3,
     args : files('test_tvos_telnet.py'),
     suite : 'integration-zsh',
     env : zsh_int_env,
     should_fail : false,
     is_parallel : false,
     timeout : 60)
