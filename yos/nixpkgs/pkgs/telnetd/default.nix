{ stdenv, lib, fetchFromGitHub, binaryen, toolchain, sysroot
, yos ? null   # optional — when null, the bin/ runner falls back to
              # `yos` on PATH instead of hardcoding a store path.
}:

# telnetd — BSD telnet server, wasm32 port for yos.
#
# Source: FreeBSD releng/13.4.0's libexec/telnetd. FreeBSD 14 removed
# base telnetd entirely — only the Heimdal/Kerberos copy survives in
# 14.x, and we want the no-auth-baggage version. libexec/telnetd in
# 13.4 is the original BSD code (UCB Regents) with FreeBSD's local
# fixes, BSD-3-Clause throughout (UCB retracted the advertising clause
# in 1999, making the "BSD-4-Clause" headers effectively 3-clause).
#
# Two source dirs from the FreeBSD tree are needed:
#   libexec/telnetd/   — the daemon itself (8 .c files, ~6000 LOC)
#   contrib/telnet/    — libtelnet/ (shared with the telnet client)
#                         provides genget / getent / misc helpers
#
# sparseCheckout limits the fetch to just those two subtrees so we
# don't drag in 500 MB of FreeBSD sources we already vendor elsewhere.
#
# What we DON'T link in:
#   * authenc.c       — Kerberos / S/Key authentication hooks
#   * libtelnet/auth.c, enc_des.c, kerberos*.c, encrypt.c
#                     — auth + encryption (we want plaintext telnet
#                       for test rig use only — never expose this
#                       binary on a public network).
# Built with -DNOAUTH so any remaining auth references compile out.
#
# Pairs with runit: drop a service dir under /tmp/run/telnetd/ with
#   #!/bin/sh
#   exec telnetd -p 2323 -l /libexec/zsh
# and `runsvdir /tmp/run` keeps it alive across crashes.

stdenv.mkDerivation rec {
  pname   = "telnetd";
  version = "13.4.0-freebsd";

  src = fetchFromGitHub {
    owner = "freebsd";
    repo  = "freebsd-src";
    rev   = "release/13.4.0";
    # FreeBSD's build layout: libexec/telnetd/ has only the Makefile +
    # Makefile.depend; the actual .c sources live under contrib/telnet/
    # (telnetd/ + libtelnet/), shared with the client. The Makefile's
    # .PATH: clause points at ${TELNETDIR}/telnetd. So sparse-checkout
    # contrib/telnet — that's where the code is.
    sha256 = "sha256-ku64nH0+G9Y/Dy13Zvpba9aJeKM6dJf9s3QSbmZ45iA=";
    sparseCheckout = [ "contrib/telnet" ];
  };

  nativeBuildInputs = [ toolchain binaryen ];

  dontStrip     = true;
  dontPatchELF  = true;
  dontFixup     = true;
  dontConfigure = true;

  CFLAGS = lib.concatStringsSep " " [
    "-target wasm32-unknown-unknown"
    "-nostdlib"
    "--sysroot=${sysroot}"
    "-isystem ${sysroot}/usr/include"
    "-Icontrib/telnet"
    "-Icontrib/telnet/telnet"
    "-D__i386__=1" "-D__yos__=1"
    "-D_GNU_SOURCE"
    # Defines from FreeBSD's libexec/telnetd/Makefile. NOTE: no
    # AUTHENTICATION / ENCRYPTION / KRB5 — those compile in the
    # auth/encryption hooks (authenc.c, libtelnet/auth.c, …) which we
    # explicitly leave out for the test-rig use case.
    "-DLINEMODE" "-DUSE_TERMIO" "-DDIAGNOSTICS"
    "-DOLD_ENVIRON" "-DENV_HACK" "-DSTREAMSPTY"
    "-O2"
    "-fno-builtin" "-ffreestanding"
    "-Wno-unused-parameter"
    "-Wno-unused-but-set-variable"
    "-Wno-unused-variable"
    "-Wno-unused-function"
    "-Wno-implicit-function-declaration"
    "-Wno-pointer-sign"
    "-Wno-deprecated-non-prototype"
  ];

  # The actual sources live in contrib/telnet/ — libexec/telnetd/ in
  # the FreeBSD tree is just a Makefile that .PATH-s in this dir.
  TELNETD_SRCS = lib.concatStringsSep " " [
    "contrib/telnet/telnetd/telnetd.c"
    "contrib/telnet/telnetd/state.c"
    "contrib/telnet/telnetd/sys_term.c"
    "contrib/telnet/telnetd/utility.c"
    "contrib/telnet/telnetd/global.c"
    "contrib/telnet/telnetd/slc.c"
    "contrib/telnet/telnetd/termstat.c"
  ];
  # libtelnet helpers (skip auth.c, encrypt.c, kerberos*.c, pk.c,
  # sra.c, krb4encpwd.c, rsaencpwd.c, read_password.c — all
  # auth/encryption — and enc_des.c).
  LIBTELNET_SRCS = lib.concatStringsSep " " [
    "contrib/telnet/libtelnet/genget.c"
    "contrib/telnet/libtelnet/getent.c"
    "contrib/telnet/libtelnet/misc.c"
  ];

  buildPhase = ''
    runHook preBuild
    CC=${toolchain}/bin/wasm-clang

    # getopt-compat shim: telnetd's wasm has NO body for the four
    # getopt(3) globals (optarg/optind/optopt/opterr). Our yos
    # sysroot has an empty libc.a (.empty.o only — every symbol
    # comes through env.* imports). Without a definition,
    # wasm-ld with --allow-undefined silently resolves `extern
    # char *optarg` to address 0 of linear memory — which yos
    # uses to store the wasm32 thread-self pointer (= 0x100 after
    # exec). Telnetd's `case 'p': altlogin = optarg;` then loads
    # 0x100, ships that as the execv() path, and the child errors
    # out with "telnetd: : Operation not permitted." because
    # execve resolves to a nonsense path inside the runit cwd.
    #
    # Provide REAL .bss storage here, plus a getopt() wrapper that
    # calls into yos's env.getopt then updates the locals from the
    # per-ctx accessors. The wrapper hides env.getopt — telnetd's
    # call to getopt() resolves to this body, not the bare import.
    cat > getopt_compat.c <<'GOPT'
    /* Provide real storage + an updating wrapper around env.getopt. */
    char *optarg  = (void *)0;
    int   optind  = 1;
    int   optopt  = 0;
    int   opterr  = 1;

    /* env.getopt is yos's per-ctx getopt; rename via attribute. */
    extern int __real_getopt(int argc, char *const argv[], const char *opts)
        __attribute__((import_module("env"), import_name("getopt")));

    /* Per-ctx accessors yos provides for the four global slots. */
    extern unsigned __yos_getopt_optarg_get(void)
        __attribute__((import_module("env"), import_name("__yos_getopt_optarg_get")));
    extern int      __yos_getopt_optind_get(void)
        __attribute__((import_module("env"), import_name("__yos_getopt_optind_get")));
    extern int      __yos_getopt_optopt_get(void)
        __attribute__((import_module("env"), import_name("__yos_getopt_optopt_get")));

    int getopt(int argc, char *const argv[], const char *opts)
    {
        int r = __real_getopt(argc, argv, opts);
        /* yos returns optarg as a wasm-memory offset (uint32). The
         * guest's view of char* is a 32-bit offset too, so just store. */
        optarg = (char *)(unsigned long)__yos_getopt_optarg_get();
        optind = __yos_getopt_optind_get();
        optopt = __yos_getopt_optopt_get();
        return r;
    }
GOPT
    $CC $CFLAGS -c getopt_compat.c -o getopt_compat.o

    OBJS=( getopt_compat.o )
    for s in $TELNETD_SRCS $LIBTELNET_SRCS; do
      if [ ! -f "$s" ]; then
        echo "telnetd: missing src $s" >&2
        ls libexec/telnetd contrib/telnet/libtelnet 2>&1 >&2
        exit 1
      fi
      obj="''${s##*/}"; obj="''${obj%.c}.o"
      $CC $CFLAGS -c "$s" -o "$obj"
      OBJS+=("$obj")
    done
    $CC $CFLAGS \
      -L"${sysroot}/usr/lib" \
      -Wl,--no-entry \
      -Wl,--allow-undefined \
      -Wl,--export=_start \
      -Wl,--export=main \
      -Wl,--export-all \
      "${sysroot}/usr/lib/crt1.o" \
      "''${OBJS[@]}" \
      -lc -lyos_stubs \
      -o telnetd.raw.wasm

    # Asyncify pass — telnetd forks AT LEAST twice per connection
    # (once for inetd-style handoff, once more for the PTY slave
    # session). Without --asyncify yos's fork bridge silently
    # no-ops, leaks stale errno into the wasm guest, and telnetd
    # bails with 'fork: Inappropriate ioctl for device' (whatever
    # the prior tcgetattr/ioctl left in errno).
    wasm-opt --asyncify -O2 telnetd.raw.wasm -o telnetd.wasm
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    mkdir -p $out/bin $out/libexec
    cp telnetd.wasm $out/libexec/telnetd
    cat > $out/bin/telnetd <<RUNNER
    #!/usr/bin/env bash
    exec ${if yos != null then "${yos}/bin/yos" else "yos"} \\
        "$out/libexec/telnetd" "\$@"
    RUNNER
    chmod +x $out/bin/telnetd
    runHook postInstall
  '';

  meta = with lib; {
    description = "BSD telnetd from FreeBSD 13.4 libexec/, wasm32 port for yos (NO auth/encryption — test-rig only)";
    homepage    = "https://cgit.freebsd.org/src/tree/libexec/telnetd?h=release/13.4.0";
    license     = licenses.bsd3;
    platforms   = platforms.linux ++ platforms.darwin;
  };
}
