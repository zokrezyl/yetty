# tests/freebsd-libc — FreeBSD's own libc test cases compiled to wasm32
# and run through yos. Each test source is taken verbatim from the
# extracted FreeBSD tree at build-tools/freebsd/src/.../lib/libc/tests/;
# atf-c.h / run.c / gen_driver.py turn ATF macros into a plain C
# program that exits 0 on PASS, 1 on any FAIL.
#
# Adding a new test = adding the path to FREEBSD_TESTS below.
# Anything that compiles cleanly and doesn't ENOSYS becomes our
# regression net.

freebsd_src_root = meson.project_build_root() / 'build-tools' / 'freebsd' \
                                              / 'src' / 'usr' / 'src' \
                                              / 'lib' / 'libc' / 'tests'

# Each entry: short_name -> source path. Absolute paths point inside
# the extracted FreeBSD tree; bare basenames live next to this
# meson.build for yos-authored regressions.
freebsd_tests = {
  # ── FreeBSD upstream tests (verbatim from extracted tree) ─────────
  'errno_basic'        : freebsd_src_root / 'sys/errno_test.c',
  'clearenv'           : freebsd_src_root / 'stdlib/clearenv_test.c',
  'qsort'              : freebsd_src_root / 'stdlib/qsort_test.c',
  'arc4random'         : freebsd_src_root / 'gen/arc4random_test.c',
  'getentropy'         : freebsd_src_root / 'gen/getentropy_test.c',
  'fmtcheck'           : freebsd_src_root / 'gen/fmtcheck_test.c',
  'ffs'                : freebsd_src_root / 'string/ffs_test.c',
  'strxfrm'            : freebsd_src_root / 'string/strxfrm_test.c',
  # Skipped reasons (each one a separate follow-up):
  #   fls, heapsort, mergesort, qsort_r, qsort_r_compat,
  #   strverscmp, fmtcheck                  — guest_only (FreeBSD-specific
  #                                            libc fns absent from glibc;
  #                                            need Tier-2 source compile
  #                                            or yos-side stub impls).
  #   quick_exit                            — fork+pipe; investigate.
  #   wcsnlen                               — segfaults; investigate.
  #   btowc                                 — locale conversion.
  #   perror                                — needs __stderrp init.
  #   fpclassify2 / set_constraint_handler_s — toolchain features.
  # ── yos-authored regressions ──────────────────────────────────────
  'stat'               : meson.current_source_dir() / 'yos_stat_test.c',
  'snprintf'           : meson.current_source_dir() / 'yos_snprintf_test.c',
  'time'               : meson.current_source_dir() / 'yos_time_test.c',
  'localtime'          : meson.current_source_dir() / 'yos_localtime_test.c',
  'logpath'            : meson.current_source_dir() / 'yos_logpath_test.c',
}

wasm_cflags = [
  '-target', 'wasm32-unknown-unknown',
  '-nostdlib',
  '--sysroot=' + sysroot_dir,
  '-isystem', sysroot_dir / 'usr' / 'include',
  '-D__i386__=1', '-D__yos__=1',
  '-O2', '-fno-builtin', '-ffreestanding',
  # Match nvim's -fstack-protector-strong so canary smashes (the
  # exact failure mode nvim hits) actually fire here too. Without
  # this the test compiler would skip canary instrumentation and
  # the bug we're chasing in nvim would never reproduce in tests.
  '-fstack-protector-strong',
  '-Wno-unused-parameter',
  '-I' + meson.current_source_dir(),
]

wasm_ldflags = [
  '-Wl,--no-entry', '-Wl,--export=_start', '-Wl,--export-all',
  '-Wl,--allow-undefined', '-Wl,--stack-first', '-Wl,-z,stack-size=65536',
]

run_c        = files('run.c')
gen_driver   = files('gen_driver.py')

# wasm-opt --asyncify pass: required for tests that call fork() (e.g.
# arc4random_test forks to verify parent/child get different entropy).
# Without it yos's setjmp/fork host shims can't unwind the wasm stack.
wasm_opt = find_program('wasm-opt', required: true)

foreach short_name, src_path : freebsd_tests
  driver_c = custom_target(
    'fbsd-' + short_name + '-driver',
    output : 'fbsd_' + short_name + '_driver.c',
    # The FreeBSD-upstream test sources are read by path inside gen_driver;
    # `depends` forces the freebsd-src extract step to run first so the
    # path exists.
    depends : [freebsd_src],
    command : [
      py, gen_driver, src_path, '@OUTPUT@',
    ],
    build_by_default : false,
  )
  raw_wasm = custom_target(
    'fbsd-' + short_name + '-raw.wasm',
    input            : [driver_c],
    output           : 'fbsd_' + short_name + '_raw.wasm',
    depends          : sysroot_skel,
    command : [
      wasm_cc, wasm_cflags, wasm_ldflags,
      sysroot_dir / 'usr' / 'lib' / 'crt1.o',
      run_c,
      '@INPUT0@',                 # generated driver
      src_path,                   # the FreeBSD test
      '-o', '@OUTPUT@',
    ],
    build_by_default : false,
  )
  wasm = custom_target(
    'fbsd-' + short_name + '.wasm',
    input            : raw_wasm,
    output           : 'fbsd_' + short_name + '.wasm',
    command : [
      wasm_opt, '--asyncify', '-O2', '@INPUT@', '-o', '@OUTPUT@',
    ],
    build_by_default : true,
  )
  test('fbsd-libc-' + short_name,
       yos_exe,
       args : [wasm.full_path()],
       suite : 'freebsd-libc',
       depends : [wasm])
endforeach
