{ stdenv, lib, fetchurl, perl, toolchain, sysroot, zlib }:

# openssl — libcrypto + libssl, wasm32 static. Adapted from upstream
# nixpkgs' openssl recipe: same fetchurl + patchShebangs + Configure
# shape, but with our own wasm32 target preset installed at build
# time. Everything declared as a regular stdenv.mkDerivation so the
# package owns its own build-time deps (perl) and is a normal nix
# derivation, not a buildRecipe + shell script.

stdenv.mkDerivation rec {
  pname   = "openssl";
  version = "3.3.2";

  src = fetchurl {
    url    = "https://www.openssl.org/source/openssl-${version}.tar.gz";
    sha256 = "2e8a40b01979afe8be0bbfb3de5dc1c6709fedb46d6c89c10da114ab5fc3d281";
  };

  nativeBuildInputs = [ perl toolchain ];
  buildInputs       = [ zlib ];

  dontStrip    = true;
  dontPatchELF = true;
  dontFixup    = true;

  # Drop a wasm32 target preset into Configurations/. openssl's
  # Configure auto-loads every *.conf under there. asm/threads/shared
  # all off — clang's wasm backend rejects all inline asm, yos's
  # pthread surface isn't 100% yet, and wasm-mvp has no dlopen.
  postPatch = ''
    patchShebangs Configure

    cat > Configurations/99-wasm32.conf <<'PERLEOF'
    ## yos: wasm32 target preset.
    my %targets = (
        "wasm32-yos" => {
            inherit_from    => [ "BASE_unix" ],
            cflags          => "-D__FreeBSD__=14 -DOPENSSL_NO_SECURE_MEMORY",
            cppflags        => "",
            bn_ops          => "THIRTY_TWO_BIT",
            thread_scheme   => "(unknown)",
            dso_scheme      => "",
            perlasm_scheme  => "void",
            asm_arch        => "",
            disable         => [ "asm", "threads", "shared", "dso",
                                 "engine", "dynamic-engine",
                                 "afalgeng",
                                 "tests", "apps",
                                 "ktls", "sctp",
                                 "secure-memory" ],
        },
    );
    PERLEOF
  '';

  CC     = "${toolchain}/bin/wasm-clang";
  AR     = "llvm-ar";
  RANLIB = "llvm-ranlib";

  # OpenSSL's Configure reads $CFLAGS as additional CFLAGS to embed
  # in the generated Makefile.
  CFLAGS = lib.concatStringsSep " " [
    "-target wasm32-unknown-unknown"
    "-nostdlib" "-nostdinc"
    "--sysroot=${sysroot}"
    "-isystem ${sysroot}/usr/include"
    "-D__i386__=1" "-D__yos__=1" "-D__FreeBSD__=14"
    # NOTE: NO -D_GNU_SOURCE for openssl. With _GNU_SOURCE openssl's
    # o_str.c picks the GNU-flavoured strerror_r (returns char *), but
    # our FreeBSD sysroot only declares the XSI form (returns int).
    # Use the POSIX-XSI branch by setting _POSIX_C_SOURCE and
    # leaving _GNU_SOURCE off.
    # 200112L is the lowest value that satisfies openssl's
    # XSI-strerror_r branch (`_POSIX_C_SOURCE >= 200112L`) while
    # keeping `gettimeofday` visible in FreeBSD's <sys/time.h>
    # (POSIX 2008 retired it).
    "-D_POSIX_C_SOURCE=200112L"
    "-D_XOPEN_SOURCE=700"
    # _POSIX_C_SOURCE alone hides every BSD-only declaration in
    # FreeBSD's sysroot (timegm, strlcpy, arc4random_*, …). Restore
    # __BSD_VISIBLE so those stay declared — openssl uses some of
    # them in date/time and string code paths.
    "-D__BSD_VISIBLE=1"
    "-O2" "-fno-builtin" "-ffreestanding"
  ];

  configurePhase = ''
    runHook preConfigure
    # Pass CC= to Configure (it embeds CC into the Makefile so make
    # doesn't fall back to its default `gcc`). Also CROSS_COMPILE=
    # to neutralise nix's stdenv cross-prefix.
    ./Configure wasm32-yos \
        CC=${toolchain}/bin/wasm-clang \
        AR=llvm-ar \
        RANLIB=llvm-ranlib \
        CROSS_COMPILE= \
        no-asm no-threads no-shared no-tests no-apps no-ktls \
        no-engine no-dso no-secure-memory \
        --prefix=$out \
        --openssldir=$out/etc/ssl \
        --with-zlib-include=${zlib}/include \
        --with-zlib-lib=${zlib}/lib \
        -static
    runHook postConfigure
  '';

  buildPhase = ''
    runHook preBuild
    # Pass CC/AR/RANLIB through make too — Makefile macros sometimes
    # invoke $(CC) but other rules use bare `cc`.
    make -j$NIX_BUILD_CORES build_libs \
        CC=${toolchain}/bin/wasm-clang \
        AR=llvm-ar \
        RANLIB=llvm-ranlib \
        CROSS_COMPILE=
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    mkdir -p $out/lib $out/include/openssl
    install -m 644 libcrypto.a libssl.a $out/lib/
    cp -a include/openssl/. $out/include/openssl/
    cat > $out/manifest.txt <<EOF
    name=openssl
    version=${version}
    prefix=$out
    libs=-lssl -lcrypto -lz
    cflags=-I$out/include
    deps=zlib
    EOF
    runHook postInstall
  '';

  meta = with lib; {
    description = "OpenSSL libcrypto + libssl, wasm32 static build for yos";
    homepage    = "https://www.openssl.org";
    license     = licenses.openssl;
    platforms   = platforms.linux ++ platforms.darwin;
  };
}
