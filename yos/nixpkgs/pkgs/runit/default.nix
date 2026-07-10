{ stdenv, lib, fetchurl, binaryen, python3, toolchain, sysroot
, yos ? null   # optional — when null, the bin/ runner falls back to
              # `yos` on PATH instead of hardcoding a store path.
}:

# runit — Gerrit Pape's process supervisor, wasm32 port for yos.
#
# Why runit and not systemd / busybox-init?
#   - Liberal license (BSD-3-Clause; everything we build under yos is
#     BSL-compatible — no GPL on the wasm side).
#   - ~3 000 lines of pure POSIX C. No procfs scraping, no cgroups,
#     no inotify, no D-Bus. Maps cleanly onto what yos's wasm guest
#     gets: fork / exec / signal / open / read / write / waitpid.
#   - Composable: runsvdir watches a dir of services; one `runsv` per
#     service does the supervision (restart on exit, signal forwarding,
#     log fd setup); `sv` is the control client. Perfect shape for the
#     "real multi-process test rig with telnet sessions" the user
#     described — drop a service dir under /tmp/run/<name>/run and the
#     supervisor takes care of the rest.
#
# What we build (and why a subset):
#   runsv      — per-service supervisor. Keep.
#   runsvdir   — scans a dir, spawns one runsv per entry. Keep.
#   sv         — control client (`sv up <svc>`, `sv status`, …). Keep.
#   svlogd     — rotating log daemon. Keep — usable as a service's `log/run`.
#   runsvchdir — atomic service-set switch helper. Keep — tiny.
#   chpst      — privilege/resource changer. SKIP for now: uses
#                setrlimit/chroot/setuid — most of which yos doesn't
#                bridge yet and which don't make sense in a single-uid
#                wasm guest anyway.
#   utmpset    — utmp/wtmp mutator. SKIP: yos has no utmp.
#   runit, runit-init — PID 1 init. SKIP: Apple platforms forbid
#                PID-1 replacement; we'd never run it.
#
# Build approach: runit's upstream uses a hand-rolled `package/install`
# script that compile-and-runs feature probes (hasflock, haswaitp,
# direntry…). Those probes can't run for a wasm cross-compile target,
# so we side-step the whole runit build harness and drive wasm-clang
# directly — same pattern as ../zlib/default.nix. The configure
# answers we know from POSIX + FreeBSD semantics get hardcoded into
# the per-feature headers below.

stdenv.mkDerivation rec {
  pname   = "runit";
  version = "2.1.2";

  src = fetchurl {
    url    = "http://smarden.org/runit/runit-${version}.tar.gz";
    sha256 = "sha256-b9AWDLDPEgfeTmZ1S205dQz/FLsKpmq0lJCZLAxHuhg=";
  };

  nativeBuildInputs = [ toolchain binaryen python3 ];

  dontStrip    = true;
  dontPatchELF = true;
  dontFixup    = true;
  dontConfigure = true;

  postUnpack = ''
    # Upstream extracts into admin/runit-<ver>/. Flatten so the
    # buildPhase doesn't need to track the path.
    sourceRoot=$(echo admin/runit-${version})
    echo "sourceRoot=$sourceRoot"
  '';

  # Per-feature headers normally produced by runit's `choose` probe.
  # Upstream ships every conditional header in two variants:
  #   <name>.h1  — "feature absent" (the fallback / older API path)
  #   <name>.h2  — "feature present" (defines HAS<FOO>, uses the
  #                modern POSIX API)
  # The runit configure step compile-and-runs a tiny probe and copies
  # the matching variant to <name>.h. For a wasm32 cross-compile the
  # probes can't run, but the relevant features are all in FreeBSD-
  # shape POSIX-2008 (waitpid / sigaction / sigprocmask / flock /
  # poll / mkfifo / sys/select.h / dirent), so .h2 is the right pick.
  # Two exceptions:
  #   uint64 — .h1 (unsigned long long = 8 B everywhere; .h2 uses
  #            plain `unsigned long` which would be 4 B on i386).
  #   uw_tmp / reboot_system — skip entirely (utmpset and runit-init
  #            aren't in RUNIT_TOOLS).
  postPatch = ''
    cd src
    for h2 in *.h2; do
      base="''${h2%.h2}"
      case "$base" in
        uint64)                  cp "$base.h1" "$base.h" ;;
        uw_tmp|reboot_system)    continue ;;
        *)                       cp "$h2"      "$base.h" ;;
      esac
    done
    # systype — auxiliary info string. runit's `system` tag.
    echo 'yos-wasm32-freebsd' > systype

    # yos-specific patch: runsvdir spawns `runsv <basename>` and
    # relies on the child inheriting the parent's cwd (svdir) so
    # runsv's `chdir(basename)` resolves correctly. Under yos every
    # forked process is a pthread of the same host process, which
    # means the HOST cwd is shared — the parent's `fchdir(curdir)`
    # right after spawn races the child's `chdir(basename)` and
    # the latter ends up at REPO_ROOT/basename instead of
    # svdir/basename, fatally exiting with "unable to change to
    # directory: file does not exist".
    #
    # Workaround: hand runsv an absolute path so its `chdir` is
    # cwd-independent. Patch the spawn site to prepend `svdir + "/"`
    # to the basename before exec'ing runsv. Keeps the rest of
    # runit's machinery untouched.
    python3 - runsvdir.c <<'PY'
    import sys, pathlib, re
    p = pathlib.Path(sys.argv[1])
    text = p.read_text()
    marker = "/* yos: pass absolute service path */"
    if marker in text: sys.exit(0)
    old = '    prog[0] ="runsv";\n    prog[1] =name;\n'
    new = ('    char absbuf[1024];\n'
           '    int n1 = 0, n2 = 0;\n'
           '    while (svdir[n1]) absbuf[n1] = svdir[n1], n1++;\n'
           '    if (n1 > 0 && absbuf[n1-1] != \'/\') absbuf[n1++] = \'/\';\n'
           '    while (name[n2] && n1 + n2 < (int)sizeof(absbuf) - 1)\n'
           '      absbuf[n1+n2] = name[n2], n2++;\n'
           '    absbuf[n1+n2] = 0;\n'
           '    ' + marker + '\n'
           '    prog[0] ="runsv";\n'
           '    prog[1] =absbuf;\n')
    if old not in text:
      sys.stderr.write("yos patch: spawn site already moved — re-check\n")
      sys.exit(1)
    p.write_text(text.replace(old, new))
    PY

    cd ..
  '';

  CFLAGS = lib.concatStringsSep " " [
    "-target wasm32-unknown-unknown"
    "-nostdlib"
    "--sysroot=${sysroot}"
    "-isystem ${sysroot}/usr/include"
    "-D__i386__=1" "-D__yos__=1"
    "-D_GNU_SOURCE"
    "-O2" "-fno-builtin" "-ffreestanding"
    "-Wno-unused-parameter"
    "-Wno-unused-but-set-variable"
    "-Wno-unused-variable"
    "-Wno-pointer-sign"        # runit uses unsigned char * for byte ops
    "-Wno-implicit-int"
    "-Wno-implicit-function-declaration"  # runit predates C99 some places
  ];

  # Subset of upstream `src/` to compile into the per-runit static
  # library. Listed explicitly so a missing source surfaces here
  # instead of inside a Makefile rule. Names checked against the
  # 2.1.2 source listing — note the bizarre stralloc / str family
  # naming: stralloc_eady.c (= ready), _opyb.c (= copyb), _opys.c
  # (= copys), _pend.c (= append).
  RUNIT_LIB_SRCS = lib.concatStringsSep " " [
    # alloc / strings / fmt
    "alloc.c" "alloc_re.c"
    "stralloc_cat.c" "stralloc_catb.c" "stralloc_cats.c"
    "stralloc_eady.c" "stralloc_opyb.c" "stralloc_opys.c" "stralloc_pend.c"
    "byte_chr.c" "byte_copy.c" "byte_cr.c" "byte_diff.c" "byte_rchr.c"
    "str_chr.c" "str_diff.c" "str_len.c" "str_start.c"
    "fmt_ptime.c" "fmt_uint.c" "fmt_uint0.c" "fmt_ulong.c"
    "scan_ulong.c"
    # error wrappers (strerr_die uses by every tool's `usage()`)
    "strerr_die.c" "strerr_sys.c"
    # I/O buffers
    "buffer.c" "buffer_0.c" "buffer_1.c" "buffer_2.c"
    "buffer_get.c" "buffer_put.c"
    "buffer_read.c" "buffer_write.c"
    # open / fd helpers
    "open_append.c" "open_read.c" "open_trunc.c" "open_write.c"
    "openreadclose.c" "readclose.c"
    "fd_copy.c" "fd_move.c" "coe.c"
    "ndelay_off.c" "ndelay_on.c" "lock_ex.c" "lock_exnb.c"
    "fifo.c"
    "seek_set.c"
    # env / error / prog
    "env.c" "error.c" "error_str.c"
    "pathexec_env.c" "pathexec_run.c"
    # uid/gid helpers (chpst, uses these)
    "uidgid.c" "prot.c"
    # pattern matching (svlogd's processor stanzas)
    "pmatch.c"
    # getopt
    "sgetopt.c" "subgetopt.c"
    # signals + wait
    "sig.c" "sig_block.c" "sig_catch.c" "sig_pause.c"
    "wait_nohang.c" "wait_pid.c"
    # poll/select wrapper
    "iopause.c"
    # TAI time
    "tai_now.c" "tai_pack.c" "tai_sub.c" "tai_unpack.c"
    "taia_add.c" "taia_approx.c" "taia_frac.c" "taia_now.c"
    "taia_pack.c" "taia_sub.c" "taia_uint.c" "taia_less.c"
  ];

  # The mains we package. Each is a separate .wasm. runsvctrl /
  # runsvstat are alternative-name links of sv that runit installs
  # via symlinks in upstream; for us each becomes a standalone
  # binary because the symlink mechanism + libexec/ doesn't translate
  # well. They share sv.c's argv[0]-driven dispatch logic so the
  # built binary already does the right thing.
  RUNIT_TOOLS = lib.concatStringsSep " " [
    "runsv" "runsvdir" "sv" "svlogd" "runsvchdir"
    "runsvctrl" "runsvstat"
  ];

  buildPhase = ''
    runHook preBuild
    cd src

    CC=${toolchain}/bin/wasm-clang

    # 1) Library .o files.
    for s in $RUNIT_LIB_SRCS; do
      if [ ! -f "$s" ]; then
        echo "runit($pname): missing src $s" >&2
        ls *.c | head -40 >&2
        exit 1
      fi
      $CC $CFLAGS -c "$s" -o "''${s%.c}.o"
    done
    LIB_OBJS=$(echo $RUNIT_LIB_SRCS | sed 's/\.c/.o/g')

    # 2) Link each main against the library set.
    mkdir -p ../out/libexec ../out/bin
    for t in $RUNIT_TOOLS; do
      if [ ! -f "$t.c" ]; then
        echo "runit($pname): missing tool $t.c" >&2
        exit 1
      fi
      $CC $CFLAGS -c "$t.c" -o "$t.o"
      $CC $CFLAGS \
        -L"${sysroot}/usr/lib" \
        -Wl,--no-entry \
        -Wl,--allow-undefined \
        -Wl,--export=_start \
        -Wl,--export=main \
        -Wl,--export-all \
        "${sysroot}/usr/lib/crt1.o" \
        "$t.o" $LIB_OBJS \
        -lc -lyos_stubs \
        -o "../out/libexec/$t.raw"
    done

    # Asyncify pass — runsv, runsvdir and sv ALL fork/exec under
    # yos. Without --asyncify the wasm guest's call stack can't be
    # snapshotted/restored across fork, and the first fork bridge
    # logs "asyncify not available - WASM must be compiled with
    # asyncify" and silently no-ops. runsvdir then sits there
    # without ever spawning a runsv per service dir.
    #
    # We pass --asyncify across every tool — the control clients
    # (sv / runsvctrl / runsvstat / runsvchdir) don't fork but the
    # extra instrumentation is harmless, and uniform builds make
    # the recipe simpler.
    for t in $RUNIT_TOOLS; do
      wasm-opt --asyncify -O2 \
        "../out/libexec/$t.raw" \
        -o "../out/libexec/$t"
      rm "../out/libexec/$t.raw"
    done

    cd ..
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall

    mkdir -p $out/bin $out/libexec
    cp out/libexec/* $out/libexec/

    # Per-tool host runner — same shape freebsd-tools uses. From
    # inside the yos shell the wasm modules are exec'd directly via
    # libexec/, so the host-side runner is just for `nix shell .#runit`
    # debugging convenience.
    for t in $RUNIT_TOOLS; do
      cat > $out/bin/$t <<RUNNER
    #!/usr/bin/env bash
    exec ${if yos != null then "${yos}/bin/yos" else "yos"} \\
        "$out/libexec/$t" "\$@"
    RUNNER
      chmod +x $out/bin/$t
    done

    runHook postInstall
  '';

  meta = with lib; {
    description = "runit — process supervisor (runsv/runsvdir/sv/svlogd/runsvchdir), wasm32 port for yos";
    homepage    = "http://smarden.org/runit/";
    license     = licenses.bsd3;
    platforms   = platforms.linux ++ platforms.darwin;
  };
}
