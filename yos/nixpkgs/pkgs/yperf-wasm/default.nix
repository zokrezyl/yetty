{ stdenv, lib, binaryen, toolchain, sysroot }:

# yperf — per-app guest-side wrapper for the host's wasm-function
# profile recorder. Tiny wasm32 program: setenv(YPERF=yes) + setenv
# (YPERF_FILE=...), fork+exec the target, wait, setenv(YPERF=stop)
# which the host's env hook routes to yperf_dump_and_reset. See
# yperf.c for the full sequence.
#
# Asyncify required for fork(): yos's pseudo-fork snapshots and
# restores wasm linear memory + globals, which needs wasm-opt's
# asyncify rewrite. Same setup as yos-tcpserver.
stdenv.mkDerivation {
  pname   = "yperf";
  version = "0.1";

  src = ./.;

  nativeBuildInputs = [ toolchain binaryen ];

  dontStrip     = true;
  dontPatchELF  = true;
  dontFixup     = true;
  dontConfigure = true;

  buildPhase = ''
    runHook preBuild
    CC=${toolchain}/bin/wasm-clang
    $CC \
      -target wasm32-unknown-unknown -nostdlib \
      --sysroot=${sysroot} \
      -isystem ${sysroot}/usr/include \
      -L ${sysroot}/usr/lib \
      -D__i386__=1 -D__yos__=1 -D_GNU_SOURCE \
      -O2 \
      -Wl,--no-entry -Wl,--allow-undefined \
      -Wl,--export=_start -Wl,--export=main -Wl,--export-all \
      ${sysroot}/usr/lib/crt1.o \
      yperf.c \
      -lc -lyos_stubs \
      -o yperf.raw.wasm

    # Asyncify required for fork — see file comment.
    wasm-opt --asyncify -O2 \
      yperf.raw.wasm -o yperf.wasm
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    mkdir -p $out/libexec
    cp yperf.wasm $out/libexec/yperf
    runHook postInstall
  '';

  meta = with lib; {
    description = "guest-side per-app wasm-function profile recorder for yos";
    license     = licenses.bsd2;
    platforms   = platforms.linux ++ platforms.darwin;
  };
}
