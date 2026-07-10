{ stdenv, lib, fetchurl, binaryen, toolchain, sysroot, freebsd-src }:

# fzy — C fuzzy finder (jhawthorn/fzy), wasm32 port.
#
# The umbrella's fuzzy finder: pipe-list-in / fuzzy-pick / print-
# selection in portable C99 on the native yos ABI — the interactive
# picker works on both runtimes.
#
# Port notes:
#   - Compiled from the src/*.c list directly (the upstream Makefile
#     only orchestrates the same compile). config.def.h → config.h.
#   - asyncify is REQUIRED: fzy blocks in read() on the tty for every
#     keystroke, and blocking under the cooperative engines needs the
#     unwind/rewind instrumentation (same as every interactive tool).
#   - yos_libc_init.o is linked in: fzy's scorer leans on tolower(3)
#     via the FreeBSD ctype tables, which stay zeroed unless the init
#     object runs (isalnum-returns-0 class of bug).
#   - getopt.c AND getopt_long.c are compiled in from the FreeBSD
#     source tree (the sysroot libc carries neither; freebsd-tools do
#     the same libcExtras dance per tool). BOTH are needed: getopt.c
#     defines the optind/optarg/opterr globals — without it those
#     resolve to wasm address 0 (--allow-undefined) and getopt_long
#     spins forever on an optind that never advances.
#   - The scoring worker count comes from sysconf(_SC_NPROCESSORS_ONLN);
#     pthreads exist on both runtimes (real on native, cooperative in
#     the browser engine), so no source patching needed.
stdenv.mkDerivation rec {
  pname   = "fzy";
  version = "1.0";

  src = fetchurl {
    url = "https://github.com/jhawthorn/fzy/archive/refs/tags/${version}.tar.gz";
    sha256 = "sha256-gCV/10V54TQ4sF7fUNzcjPDNsYcLSivFlnvR/b7R+s8=";
  };

  nativeBuildInputs = [ toolchain binaryen ];

  dontStrip     = true;
  dontPatchELF  = true;
  dontFixup     = true;
  dontConfigure = true;

  buildPhase = ''
    runHook preBuild

    # Sources include "../config.h" relative to src/ — the config lives
    # at the tarball root, seeded from the default (upstream Makefile
    # does the same copy). VERSION is a Makefile-provided macro.
    cp src/config.def.h config.h

    ${toolchain}/bin/wasm-clang \
      -target wasm32-unknown-unknown -nostdlib -nostdinc \
      --sysroot=${sysroot} \
      -isystem ${sysroot}/usr/include \
      -L ${sysroot}/usr/lib \
      -I . \
      -I ${freebsd-src}/usr/src/lib/libc/include \
      -D__i386__=1 -D__yos__=1 -DVERSION='"${version}"' \
      -O2 -std=c99 -fno-builtin \
      -Wl,--no-entry -Wl,--allow-undefined \
      -Wl,--export=_start -Wl,--export=main -Wl,--export-all \
      ${sysroot}/usr/lib/crt1.o \
      ${sysroot}/usr/lib/yos_libc_init.o \
      src/fzy.c src/match.c src/choices.c src/options.c \
      src/tty.c src/tty_interface.c \
      ${freebsd-src}/usr/src/lib/libc/stdlib/getopt.c \
      ${freebsd-src}/usr/src/lib/libc/stdlib/getopt_long.c \
      -lc -lyos_stubs \
      -o fzy.raw.wasm

    # Interactive tool: blocks on tty reads — must be asyncify-instrumented.
    wasm-opt --asyncify -O2 fzy.raw.wasm -o fzy.wasm

    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    mkdir -p $out/libexec
    cp fzy.wasm $out/libexec/fzy
    runHook postInstall
  '';

  meta = with lib; {
    description = "fuzzy text selector (C) for yos — interactive on both runtimes";
    homepage    = "https://github.com/jhawthorn/fzy";
    license     = licenses.mit;
    platforms   = platforms.linux ++ platforms.darwin;
  };
}
