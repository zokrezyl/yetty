{ stdenv, lib, binaryen, toolchain, sysroot
, yos ? null
}:

# yos-tcpserver — listen+accept+fork+exec super-server.
# One file (yos-tcpserver.c), no upstream — we own this code.
# Builds to $out/libexec/yos-tcpserver. The fork loop needs asyncify
# so each accept can spawn an independent wasm process running the
# target program (typically /libexec/zsh, /libexec/telnetd, …).
stdenv.mkDerivation {
  pname   = "yos-tcpserver";
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
      yos-tcpserver.c \
      -lc -lyos_stubs \
      -o yos-tcpserver.raw.wasm

    # Asyncify required for fork(); without it the wasm guest's
    # call stack can't be snapshotted across the fork bridge and
    # the first accept silently does nothing.
    wasm-opt --asyncify -O2 \
      yos-tcpserver.raw.wasm -o yos-tcpserver.wasm
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    mkdir -p $out/bin $out/libexec
    cp yos-tcpserver.wasm $out/libexec/yos-tcpserver
    cat > $out/bin/yos-tcpserver <<RUNNER
    #!/usr/bin/env bash
    exec ${if yos != null then "${yos}/bin/yos" else "yos"} \\
        "$out/libexec/yos-tcpserver" "\$@"
    RUNNER
    chmod +x $out/bin/yos-tcpserver
    runHook postInstall
  '';

  meta = with lib; {
    description = "yos-native listen+accept+fork+exec super-server (multi-session entry for wasm tools)";
    license     = licenses.bsd2;  # we own the source; declare permissively
    platforms   = platforms.linux ++ platforms.darwin;
  };
}
