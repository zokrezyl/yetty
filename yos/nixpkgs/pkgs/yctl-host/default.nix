{ stdenv, lib, pkg-config, msgpack-c, yctlSrc }:

# yctl host build — same source as the wasm yctl (yctlSrc points to the
# shared yctl.c in nixpkgs/pkgs/yctl/), linked against the host
# pkgs.msgpack-c. Lives in $out/bin/ so it sits on the user's PATH
# outside the yos sandbox.
stdenv.mkDerivation {
  pname   = "yctl-host";
  version = "0.1";

  src = yctlSrc;

  nativeBuildInputs = [ pkg-config ];
  buildInputs       = [ msgpack-c ];

  dontConfigure = true;

  buildPhase = ''
    runHook preBuild
    # Some msgpack-c installs expose `msgpack-c.pc`, older ones use
    # `msgpack.pc`. Try both.
    PC=$(pkg-config --exists msgpack-c && echo msgpack-c || echo msgpack)
    cc $(pkg-config --cflags "$PC") -O2 -Wall \
       yctl.c \
       $(pkg-config --libs "$PC") \
       -o yctl
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    install -Dm755 yctl $out/bin/yctl
    runHook postInstall
  '';

  meta = with lib; {
    description = "host client for the yos runtime introspection / control daemon";
    license     = licenses.bsd2;
    platforms   = platforms.linux ++ platforms.darwin;
  };
}
