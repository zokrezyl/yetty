{ stdenv, lib, toolchain, sysroot }:

# ytrace — guest-side strace front. Tiny wasm32 program that sets
# YTRACE_DEFAULT_ON=yes (and optionally YTRACE_FILE_PREFIX), then
# execvps the rest of argv. The host's yos_execve scans the new env
# at exec time and flips the trace points on. See ytrace.c for the
# full rationale.
stdenv.mkDerivation {
  pname   = "ytrace";
  version = "0.1";

  src = ./.;

  nativeBuildInputs = [ toolchain ];

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
      ytrace.c \
      -lc -lyos_stubs \
      -o ytrace.wasm
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    mkdir -p $out/libexec
    cp ytrace.wasm $out/libexec/ytrace
    runHook postInstall
  '';

  meta = with lib; {
    description = "guest-side strace-style libc-surface tracer for wasm under yos";
    license     = licenses.bsd2;
    platforms   = platforms.linux ++ platforms.darwin;
  };
}
