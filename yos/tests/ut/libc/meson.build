# tests/ut/libc — wasm32 guest tests for the libc-by-import surface.
#
# Each test_*.c compiles to wasm32 with clang, then runs through yos.
# The runner script reads the test's `Expected: exit N, stdout contains
# "..."` header to know what counts as a pass.

# Pick the OS-appropriate wasm-clang wrapper. POSIX uses the bash
# sibling that filters nix-injected darwin flags; Windows uses
# wasm-clang.cmd which calls wasm-clang.py via the venv python.
if host_machine.system() == 'windows'
  _wasm_clang_basename = 'wasm-clang.cmd'
else
  _wasm_clang_basename = 'wasm-clang'
endif
wasm_cc  = find_program(meson.project_source_root() / 'build-tools' / _wasm_clang_basename,
                        required: true)
wasm_opt = find_program('wasm-opt', required: true)  # Binaryen — for asyncify

# Tests run after `yos` is built; meson.test() handles the dep.
yos_bin = yos_exe.full_path()

# Per-test wasm32 compile flags. -nostdlib because the test brings its
# own minimal types and only uses imports — no libc baggage. -Wl flags
# tell wasm-ld to skip _start auto-detection and export the symbol
# yos calls into.
sysroot_dir = meson.project_build_root() / 'sysroot'

wasm_cflags = [
  '-target', 'wasm32-unknown-unknown',
  '-nostdlib',
  '--sysroot=' + sysroot_dir,
  '-isystem', sysroot_dir / 'usr' / 'include',
  '-Wl,--no-entry',
  '-Wl,--export=_start',
  '-Wl,--export-all',         # function table exposed (pthread_create
                              # reads it to look up start_routine)
  '-Wl,--allow-undefined',    # unresolved fn refs become env.<name>
                              # imports automatically — that's exactly
                              # the yos contract.
  # Skip wasm function-table indices 0 and 1 so user functions land at
  # index ≥ 2. The FreeBSD ABI encodes SIG_DFL as 0 and SIG_IGN as 1;
  # if a wasm guest's sigaction handler landed at table[1] (which
  # clang's linker does for small modules with few address-taken
  # functions), yos couldn't distinguish "function at table[1]" from
  # "user passed SIG_IGN" at the bridge boundary. --table-base=2
  # leaves indices 0 and 1 unused at link time.
  '-Wl,--table-base=2',
  '-O2',
]

libc_tests = [
  'test_redecl_merge',
  'test_write_passthrough',
  'test_exit_code',
  'test_generated_imports',
  'test_fork_basic',
  'test_pthread_create_join',
  'test_freebsd_headers',
  'test_malloc_basic',
  # nvim-on-darwin trouble-area pins (tmp/nvim-runtime-issues.md).
  # Each isolates one fd-bridge corner that the embedded nvim server
  # depends on. Pass on Linux; designed to surface darwin regressions.
  'test_socketpair_flags',
  'test_fcntl_dupfd_cloexec',
  'test_dup2_pipe_pair',
  'test_closefrom_preserves_stdio',
  'test_socketpair_sotype_isatty',
  'test_kqueue_evfilt_read',
  'test_ioctl_fionbio',
  'test_open_oflags',
  # End-to-end reproducer for the nvim embedded-server failure on
  # darwin. Walks channel_from_stdio + uv_pipe_open + uv_read_start
  # in nvim's order; each step has a unique exit code so a failing
  # run pinpoints the broken bridge layer.
  'test_nvim_channel_from_stdio',
  # Same reproducer but across a fork — the actual nvim embedded
  # server is a forked child, and the fd_map handoff via
  # yos_fd_fork_dup is part of the suspect surface. Needs asyncify.
  'test_fork_channel_from_stdio',
  # uv_guess_handle algorithm reproducer. Probes isatty, fstat,
  # getsockname, getsockopt(SO_TYPE) on a socketpair end in libuv's
  # exact order. The getsockname check pins the BSD-host bug in
  # src/yos/impl/posix.c:96 (host_buf[0]|host_buf[1]<<8 reads sa_len
  # | sa_family<<8 on darwin/FreeBSD instead of the Linux uint16
  # sa_family at offset 0).
  'test_uv_guess_handle',
  # Probes whether the bridge writes st_mode to the same offset that
  # the FreeBSD sysroot's struct stat reads it from. A mismatch would
  # explain why uv_guess_handle on a SOCK fd silently returns the
  # wrong type without ever calling getsockname.
  'test_stat_layout_probe',
  # Asyncify-leak regression. The forked child's first stderr write
  # must be byte-clean — no 0xff prefix from the rewound stack frame.
  # The visible symptom of this leak in openssh was "Bad file
  # descriptor" rendered as 0xff×8 + "ad file descriptor". The yos
  # workaround in vfs.c::yos_write only catches pure-0xff payloads;
  # this test exercises a MIXED payload (single byte 'X') over a pipe
  # so the workaround is bypassed and the raw leak shows.
  'test_post_fork_stderr_clean',
  # Stdio (fprintf) write from a forked child must reach the host
  # stderr. zsh-wasm's "command not found" is emitted from a forked
  # child via fputc on stderr — under yos that output disappears
  # because the child exits without an stdio flush.
  'test_fork_fprintf_stderr',
  # FreeBSD libc strmode(3): mode_t → "drwxr-xr-x " column ls -l
  # prints. Pure / stateless; hand-bridged in impl/posix.c because
  # glibc has no equivalent and the codegen ENOSYS stub left every
  # ls -l line with a blank mode column.
  'test_strmode',
  # yos_lpathconf bridge errno contract — pins the fix for the fts
  # info-leak ls printed before yos_lpathconf called yos_errno_neg
  # ("./<file>: Undefined error: 0"). See impl/libc/posix.c::yos_lpathconf.
  'test_lpathconf_errno',
  # fopen → fileno → fstat round-trip. Pre-fix yos_fileno returned
  # the host fd, which couldn't be looked up in ctx->fd_map; every
  # subsequent fstat/read/fcntl EBADF'd. ssh's "fstat /Users/.../
  # .ssh/config: Bad file descriptor" pinned by this test.
  'test_fopen_fileno_fstat',
  # lstat on a symlink: AT_SYMLINK_NOFOLLOW must be translated to
  # the host's bit (FreeBSD 0x200 → darwin 0x20) so the call
  # doesn't collapse to stat(). And the result's mode bits must be
  # normalised to 0777 (FreeBSD contract) regardless of what the
  # host lstat returns. ls used to display symlinks as regular
  # files because both pieces were broken.
  'test_symlink_lstat_perms',
  # user_from_uid + group_from_gid must use SEPARATE wasm scratch
  # buffers. ls's `printf("%s %s", uname, gname)` evaluated both
  # before printf; pre-fix they wrote into one shared buffer and
  # ls displayed `staff staff` for every file.
  'test_pwd_user_group_distinct',
  # getline(): reads lines from a FILE* — used by ssh's config
  # parser. Was stubbed -ENOSYS; ssh got zero lines from
  # ~/.ssh/config and tried to DNS-resolve every Host alias.
  'test_getline_basic',
  # ssh libc-surface pins. The ytrace of `ssh nixem` shows 47 distinct
  # libc imports; the entries below cover the non-trivial ones (BSD
  # string ops, secret scrubbing, address resolution, host/services
  # DB, randomness, set-uid detection). The pure mem/str passthroughs
  # are covered by test_libc_coverage and test_write_passthrough.
  'test_ssh_explicit_bzero',
  'test_ssh_str_bsd',
  'test_ssh_arc4random_buf',
  'test_ssh_getaddrinfo_localhost',
  'test_ssh_gethostname',
  'test_ssh_issetugid',
  'test_ssh_getservbyname',
  'test_ssh_ppoll',
  # /dev/null pollability — darwin returns POLLNVAL where Linux
  # returns POLLIN. ssh dups stdin and polls it; when stdin is
  # /dev/null (cron, non-interactive shells, `ssh host cmd </dev/null`),
  # the POLLNVAL trips ssh's fatal "invalid rfd pollfd[N]" check.
  # yos_poll detects regular-file / /dev/null fds via fstat and
  # synthesises POLLIN|POLLOUT.
  'test_ssh_poll_dev_null',
  # dup/dup2/close cycle ssh runs against stdio when setting up its
  # session channel. A bug where close on the source kills the dup
  # poisons every subsequent write to the session.
  'test_ssh_dup_stdio',
  # socket → bind → listen → connect → accept → getsockname /
  # getpeername / setsockopt / getsockopt. The same dance ssh runs
  # to set up its TCP connection. Pins the darwin sockaddr family
  # byte position (sa_family @ byte 1, not @ uint16 little-endian
  # @0/1 like Linux).
  'test_ssh_socket_loopback',
  # sigaction + sigprocmask round-trip on the FreeBSD 16-byte
  # sigset_t. ssh wraps every channel-loop iteration in these.
  'test_ssh_sigaction_mask',
  # atoi / timingsafe_bcmp / memcmp / memmove / memchr / umask /
  # getpid / getuid / time / clock_gettime / setlocale.
  'test_ssh_misc_libc',
  # strlen/strcmp/strncmp/str*casecmp/strchr/strrchr/strcpy.
  'test_ssh_string_basic',
  # malloc/calloc/realloc/free contracts ssh leans on for channel
  # state and packet buffers.
  'test_ssh_alloc_basic',
  # readpassphrase(3) — BSD libc, not in glibc, missed by the darwin
  # host-API extractor. ssh / sshd / ssh-add prompt for key passphrases
  # through this and trap with "unresolved import env.readpassphrase"
  # without the hand bridge.
  'test_ssh_readpassphrase',
  # Issue #15 regression suite — each test pins one of the test ideas
  # the static review enumerated:
  #   cwd_after_chdir   — guest cwd resolution survives fopen / openat(AT_FDCWD)
  #   fdopen_lifetime   — fdopen+fclose+close(stale_fd) does not close a recycled host fd
  #   boundary_buffers  — read/write/getentropy boundary semantics (NULL+0 OK, NULL+n EFAULT)
  #   iovec_bounds      — writev rejects negative iovcnt, huge iovcnt, and bad per-iovec ranges
  #   mkdirat_dirfd     — mkdirat with a real (non-AT_FDCWD) dirfd places the new dir correctly
  'test_issue15_cwd_after_chdir',
  'test_issue15_fdopen_lifetime',
  'test_issue15_boundary_buffers',
  'test_issue15_iovec_bounds',
  'test_issue15_mkdirat_dirfd',
  # libarchive surface (env.archive_*, src/yos/impl/libc/libarchive.c):
  # build a ustar in linear memory and round-trip it through host
  # libarchive. Requires the yos build to have found libarchive
  # (with_libarchive=auto); the devShell + nix yos derivation provide
  # it, so a default build runs this for real.
  'test_libarchive_read_memory',
  # Issue #20 hardening suite — OS-boundary validation pins:
  #   unterminated_path     — path bridges reject a guest string that
  #                           runs to the end of linear memory with no
  #                           NUL (wstr_check, finding 1)
  #   mmap_fixed            — MAP_FIXED carve/zero, heap-overlap refusal,
  #                           free-region coalescing, repeated munmap
  #                           (mem.c, finding 4)
  #   libarchive_filename   — archive_read_open_filename routed through
  #                           yos_open + a yos-fd-backed read callback
  #                           (libarchive.c, finding 2)
  'test_issue20_unterminated_path',
  'test_issue20_mmap_fixed',
  #   munmap_heap_guard     — munmap() only releases live mmap regions;
  #                           a munmap landing on static/.bss or the brk
  #                           heap is a no-op (does not zero the bytes nor
  #                           donate them to the mmap free list) (mem.c)
  'test_munmap_heap_guard',
  'test_issue20_libarchive_filename',
  #   libarchive_kinds      — archive/entry handle-kind enforcement:
  #                           freeing an entry-as-archive is a no-op, and
  #                           entry handles are invalidated once their
  #                           owning archive is freed (libarchive.c,
  #                           finding 3)
  'test_issue20_libarchive_kinds',
  #   libarchive_leak_teardown — a filename-opened archive left unfreed
  #                           is reclaimed by the runtime teardown
  #                           (yos_libarchive_ctx_free is now actually
  #                           invoked) without crashing (finding 4)
  'test_issue20_libarchive_leak_teardown',
  #   libarchive_into_fd    — archive_read_data_into_fd streams the entry
  #                           through yos_write to a yos wasm fd (not a
  #                           translated host fd); payload exceeds the
  #                           64 KiB scratch so the read/write loop and
  #                           short-write handling are exercised (libarchive.c)
  'test_libarchive_into_fd',
]

# Path to the generator's output dir, exposed by src/yos/codegen.
# api_bridge_outputs[2] is yos_imports.h — used to derive the include path.
yos_imports_dir = meson.project_build_root() / 'src' / 'yos' / 'codegen'

# Combined performance + stress test. Different from the others: links
# against crt1.o so main() gets argv (needed because the test execve's
# itself, passing argv[0] as the wasm path). Built standalone, not
# inside the libc_tests foreach.
perf_stress_raw = custom_target(
  'test_perf_stress-raw.wasm',
  input            : files('test_perf_stress.c'),
  output           : 'test_perf_stress-raw.wasm',
  depends          : sysroot_skel,
  command          : [wasm_cc,
                      '-target', 'wasm32-unknown-unknown',
                      '-nostdlib',
                      '--sysroot=' + sysroot_dir,
                      '-isystem', sysroot_dir / 'usr' / 'include',
                      '-L', sysroot_dir / 'usr' / 'lib',
                      '-Wl,--no-entry',
                      '-Wl,--allow-undefined',
                      '-Wl,--export=_start',
                      '-Wl,--export=main',
                      # pthread_create looks up start_routine by index
                      # into the wasm function table; without
                      # --export-all the table is empty and every
                      # thread traps with "fn_idx N out of table".
                      '-Wl,--export-all',
                      '-O2',
                      sysroot_dir / 'usr' / 'lib' / 'crt1.o',
                      '-o', '@OUTPUT@', '@INPUT0@'],
  build_by_default : true,
)
perf_stress_wasm = custom_target(
  'test_perf_stress.wasm',
  input            : perf_stress_raw,
  output           : 'test_perf_stress.wasm',
  # -O2 is REQUIRED here. Without it, wasm-opt's asyncify pass emits
  # a `_start` whose locals overflow wasm3's compile-time function
  # stack-height limit, and the host bails out with "compiling
  # function overran its stack height limit" before even calling
  # main. -O2 shrinks the asyncify-instrumented frame enough that
  # _start fits. The other (smaller) libc tests get away without
  # -O2 because their _start frame is already compact.
  command          : [wasm_opt, '--asyncify', '-O2', '-o', '@OUTPUT@', '@INPUT@'],
  build_by_default : true,
)

# Shared-memory (-pthread) build of the SAME source. The host-threaded
# wasm above exports its own non-shared memory (native yos / wasm3 sets
# the per-thread stack pointer); the browser cannot do that, so its
# real-thread engine (mt_engine: Web Workers + SharedArrayBuffer) needs a
# wasm that IMPORTS a shared memory and uses wasm-native atomics. Same .c,
# different ABI: -matomics -mbulk-memory + --import-memory --shared-memory
# + --export-table. The browser libc suite (src/yos/platform/web) runs THIS
# variant for perf_stress, where it passes with real threads; native keeps
# running the host-threaded one.
perf_stress_mt_raw = custom_target(
  'test_perf_stress-mt-raw.wasm',
  input            : files('test_perf_stress.c'),
  output           : 'test_perf_stress-mt-raw.wasm',
  depends          : sysroot_skel,
  command          : [wasm_cc,
                      '-target', 'wasm32-unknown-unknown', '-nostdlib', '-O2', '-fno-builtin',
                      '--sysroot=' + sysroot_dir,
                      '-isystem', sysroot_dir / 'usr' / 'include',
                      '-L', sysroot_dir / 'usr' / 'lib',
                      '-matomics', '-mbulk-memory',
                      '-Wl,--no-entry', '-Wl,--allow-undefined',
                      '-Wl,--import-memory', '-Wl,--shared-memory', '-Wl,--max-memory=67108864',
                      '-Wl,--export-table', '-Wl,--export-all', '-Wl,--table-base=2',
                      sysroot_dir / 'usr' / 'lib' / 'crt1.o',
                      '-o', '@OUTPUT@', '@INPUT0@'],
  build_by_default : true,
)
perf_stress_mt_wasm = custom_target(
  'test_perf_stress-mt.wasm',
  input            : perf_stress_mt_raw,
  output           : 'test_perf_stress-mt.wasm',
  command          : [wasm_opt, '--asyncify', '-O2', '-o', '@OUTPUT@', '@INPUT@'],
  build_by_default : true,
)
test('test_perf_stress',
  py,
  args : [
    files('run_libc_test.py'),
    '--yos',     yos_bin,
    '--src',     files('test_perf_stress.c'),
    '--wasm',    perf_stress_wasm,
    '--timeout', '90',
  ],
  suite   : 'libc',
  # Stress test spawns many forks + threads + execve'd processes; the
  # default 30 s per-test timeout is tight when the host is under
  # load. 120 s is plenty even on a slow VM.
  timeout : 120,
)

# Comprehensive libc-coverage smoke test. One wasm program calls
# every libc function we can probe safely (touches only /tmp,
# 127.0.0.1 loopback, in-process pthread/mutex). Each call emits
# `FN:<name>:PASS|FAIL|SKIP`. The Python runner parses the output,
# cross-checks against the bridged-function list extracted from the
# generated yos_bridge.h, and writes a markdown coverage table to
# tmp/libc-coverage.md. Per-function failures show up in the table
# but DO NOT fail this test — the goal is one-shot visibility into
# which bridges work and which don't, so we don't have to fix them
# one-at-a-time as nvim/zsh/ssh tripping over each. The test only
# fails if the wasm aborts before printing COVERAGE-DONE.
libc_coverage_raw = custom_target(
  'test_libc_coverage-raw.wasm',
  input            : files('test_libc_coverage.c'),
  output           : 'test_libc_coverage-raw.wasm',
  depends          : sysroot_skel,
  command          : [wasm_cc,
                      '-target', 'wasm32-unknown-unknown',
                      '-nostdlib',
                      '--sysroot=' + sysroot_dir,
                      '-isystem', sysroot_dir / 'usr' / 'include',
                      '-L', sysroot_dir / 'usr' / 'lib',
                      '-Wl,--no-entry',
                      '-Wl,--allow-undefined',
                      '-Wl,--export=_start',
                      '-Wl,--export=main',
                      '-Wl,--export-all',
                      '-O2',
                      sysroot_dir / 'usr' / 'lib' / 'crt1.o',
                      '-o', '@OUTPUT@', '@INPUT0@'],
  build_by_default : true,
)
test('test_libc_coverage',
  py,
  args : [
    files('run_libc_coverage.py'),
    '--yos',       yos_bin,
    '--src',       files('test_libc_coverage.c'),
    '--wasm',      libc_coverage_raw,
    '--repo',      meson.project_source_root(),
    '--build-dir', meson.project_build_root().split('/')[-1],
    '--timeout',   '60',
  ],
  suite   : 'libc',
  timeout : 90,
)

# Tests in this set are known-failing pins for open bugs. The test
# still RUNS — when the underlying bug is fixed, the suite reports
# UNEXPECTEDPASS and the entry should be removed.
#
# `test_post_fork_stderr_clean` was in this list. Fixed by moving
# the per-fd F_DUPFD loop from the forked child thread into
# yos_fork_pump (parent thread, before resume) so the snapshot
# captures stable host fds rather than racing fd-table slots
# against the parent's post-fork close/reuse traffic. See
# src/yos/impl/proc.c diff.
libc_tests_xfail = [
]

# Windows-specific expected failures: tests that depend on host
# primitives Windows doesn't provide and can't reasonably emulate.
if host_machine.system() == 'windows'
  libc_tests_xfail += [
    # kqueue is BSD/macOS-only — Windows IOCP is the rough equivalent
    # but the wasm guest's wire format is FreeBSD kqueue, and a full
    # IOCP-shim is out of scope. These tests all assert kqueue() != -1.
    'test_kqueue_evfilt_read',
    'test_nvim_channel_from_stdio',
    'test_fork_channel_from_stdio',
    # symlink(2) on Windows requires either Administrator or Developer
    # Mode (Win10 1703+). On a stock unprivileged shell CreateSymbolicLink
    # fails with ERROR_PRIVILEGE_NOT_HELD; the test then exits 1.
    'test_symlink_lstat_perms',
  ]
endif

foreach t : libc_tests
  src = files(t + '.c')

  # Tests that include the auto-generated yos_imports.h need the
  # codegen output dir on their include path AND a dep on the bridge
  # custom_target so meson regenerates the header before compiling.
  extra_inputs = []
  extra_cflags = []
  if t in ['test_generated_imports', 'test_fork_basic',
           'test_pthread_create_join']
    extra_inputs += [api_bridge_outputs]
    extra_cflags += ['-I' + yos_imports_dir]
  endif

  raw_wasm = custom_target(
    t + '-raw.wasm',
    input            : [src] + extra_inputs,
    output           : t + '-raw.wasm',
    # Depend on the sysroot stamp so wasm tests rebuild when FreeBSD
    # headers change. sysroot_skel is a custom_target.
    depends          : sysroot_skel,
    command          : [wasm_cc, wasm_cflags, extra_cflags,
                        '-o', '@OUTPUT@', '@INPUT0@'],
    build_by_default : true,
  )

  # Tests that exercise fork / setjmp / longjmp need the Binaryen
  # asyncify pass applied so the host runtime can unwind/rewind the
  # wasm call stack. Plain stateless tests skip the pass.
  needs_asyncify = t in ['test_fork_basic',
                         'test_fork_channel_from_stdio',
                         'test_fork_fprintf_stderr',
                         'test_post_fork_stderr_clean']

  if needs_asyncify
    wasm = custom_target(
      t + '.wasm',
      input            : raw_wasm,
      output           : t + '.wasm',
      command          : [wasm_opt, '--asyncify', '-o', '@OUTPUT@', '@INPUT@'],
      build_by_default : true,
    )
  else
    wasm = raw_wasm
  endif

  test(t,
    py,
    args : [
      files('run_libc_test.py'),
      '--yos',  yos_bin,
      '--src',  src,
      '--wasm', wasm,
    ],
    suite       : 'libc',
    should_fail : t in libc_tests_xfail,
  )
endforeach

# ─── parametrised fork-child matrix ─────────────────────────────────
#
# gen_fork_matrix.py reads fork_matrix.yaml and emits one
# test_fork_child_<name>.c per row into the build tree. Every row
# forks, so every generated test goes through wasm-opt --asyncify.
#
# Reconfigure note: meson runs the --list-names step at CONFIGURE
# time, so adding/renaming/removing rows in fork_matrix.yaml requires
# `meson setup --reconfigure build-<host>` (or wiping the build dir).
# Editing only the BODY of an existing row is fine — that's a
# build-time recompile via the custom_target's input dependency.
gen_fork_script = files('gen_fork_matrix.py')
fork_matrix_yaml = files('fork_matrix.yaml')

# Configure-time probe: ask the generator for the row name list so
# the foreach loop below can declare one test() per row.
fork_names_cmd = run_command(
  py, gen_fork_script,
  '--yaml',       fork_matrix_yaml[0],
  '--list-names',
  check : true,
)
fork_row_names = fork_names_cmd.stdout().strip().split('\n')

# Same probe, but only rows tagged `xfail: true` — these are tests that
# pin known yos bugs. They still run and still go through asyncify so a
# fix elsewhere will flip them to UNEXPECTEDPASS and force a YAML edit.
fork_xfail_cmd = run_command(
  py, gen_fork_script,
  '--yaml',       fork_matrix_yaml[0],
  '--list-xfail',
  check : true,
)
fork_xfail_stdout = fork_xfail_cmd.stdout().strip()
if fork_xfail_stdout == ''
  fork_xfail_names = []
else
  fork_xfail_names = fork_xfail_stdout.split('\n')
endif

# Pre-compute the output filename list so custom_target knows what
# to expect. Order MUST match the order gen_fork_matrix.py emits
# (which is the row order in the YAML — both sides walk the same
# list in the same order, so indexing below is stable).
fork_gen_outputs = []
foreach n : fork_row_names
  fork_gen_outputs += 'test_fork_child_' + n + '.c'
endforeach

fork_gen = custom_target(
  'fork-matrix-gen',
  input            : [fork_matrix_yaml, gen_fork_script],
  output           : fork_gen_outputs,
  command          : [py, gen_fork_script,
                      '--yaml',    '@INPUT0@',
                      '--out-dir', '@OUTDIR@'],
  build_by_default : true,
)

# Per-row wasm build + meson test entry. Each row goes through the
# same wasm-clang + wasm-opt --asyncify path the hand-written fork
# tests use (test_fork_basic etc.), so a row that misbehaves trips
# the same diagnostics path as the manual tests.
fork_row_index = 0
foreach n : fork_row_names
  src = fork_gen[fork_row_index]
  fork_row_index += 1

  raw_wasm = custom_target(
    'test_fork_child_' + n + '-raw.wasm',
    input            : src,
    output           : 'test_fork_child_' + n + '-raw.wasm',
    depends          : sysroot_skel,
    command          : [wasm_cc, wasm_cflags, '-o', '@OUTPUT@', '@INPUT0@'],
    build_by_default : true,
  )

  wasm = custom_target(
    'test_fork_child_' + n + '.wasm',
    input            : raw_wasm,
    output           : 'test_fork_child_' + n + '.wasm',
    command          : [wasm_opt, '--asyncify', '-o', '@OUTPUT@', '@INPUT@'],
    build_by_default : true,
  )

  test('test_fork_child_' + n,
    py,
    args : [
      files('run_libc_test.py'),
      '--yos',  yos_bin,
      '--src',  src,
      '--wasm', wasm,
      # Inner runner default is 30 s — the stress rows (100 sequential
      # fork+exit cycles, 363-process tree) routinely brush past it on
      # Windows where each fork pays a Binaryen-asyncify rewind cost.
      '--timeout', '90',
    ],
    suite       : 'libc',
    # Each row forks + waits; on a loaded CI box that can creep past
    # the default per-test timeout. The outer meson timeout must stay
    # above the inner runner timeout so the runner gets a chance to
    # produce a report rather than being SIGKILL'd by meson first.
    timeout     : 120,
    # `xfail: true` in the YAML row pins a known yos bug — the test
    # PASSES the suite by failing the assertion, and UNEXPECTEDPASSes
    # (suite-failing) the day the underlying bridge is fixed.
    should_fail : n in fork_xfail_names,
  )
endforeach
