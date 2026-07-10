{ stdenv, lib, toolchain, sysroot, msgpack-c }:

# yctl — client for the yos introspection + control daemon. Single .c
# (yctl.c here) builds twice from the same source:
#   - this derivation (wasm32, linked against msgpack-c built for wasm32
#     via the buildRecipe; installed as $out/libexec/yctl)
#   - nixpkgs/pkgs/yctl-host/  (host binary, linked against the host
#     pkgs.msgpack-c; installed as $out/bin/yctl)
#
# AF_UNIX inside the wasm guest just passes the sockaddr_un.sun_path
# straight through yos's connect bridge to host connect(2) — same
# filesystem path the host yctl uses.
stdenv.mkDerivation {
  pname   = "yctl";
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
      -isystem ${msgpack-c}/include \
      -L ${sysroot}/usr/lib \
      -L ${msgpack-c}/lib \
      -D__i386__=1 -D__yos__=1 -D_GNU_SOURCE \
      -O2 \
      -Wl,--no-entry -Wl,--allow-undefined \
      -Wl,--export=_start -Wl,--export=main -Wl,--export-all \
      ${sysroot}/usr/lib/crt1.o \
      yctl.c \
      -lmsgpack-c -lc -lyos_stubs \
      -o yctl.wasm
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    mkdir -p $out/libexec
    cp yctl.wasm $out/libexec/yctl
    runHook postInstall
  '';

  meta = with lib; {
    description = "client for the yos runtime introspection / control daemon (wasm guest build)";
    license     = licenses.bsd2;
    platforms   = platforms.linux ++ platforms.darwin;
  };
}
