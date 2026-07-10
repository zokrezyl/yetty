{ stdenv, lib, fetchurl, fetchFromGitHub
, meson, ninja, pkg-config, python3, llvmPackages_18, binaryen
, libffi, libiconv, apple-sdk_13 ? null
, msgpack-c
, openssl
, lua5_1
, libarchive
, src
}:

# Layer 1: the yos host runtime binary.
#
# Drives the same meson build users invoke as `meson setup build-linux
# && ninja -C build-linux`, but with all network fetches pre-resolved
# via Nix's content-addressed fetchers. The two sources the meson
# build would otherwise pull at configure time:
#
#   build-tools/freebsd/  fetch.py  —  FreeBSD-RELEASE src.txz
#                                       (a 200+ MiB tarball).
#   build-tools/mimalloc/ fetch.py  —  mimalloc git checkout at a
#                                       pinned commit.
#
# We stage both into the build dir at the paths the meson scripts
# expect and create the .stamp / .extracted files those scripts write
# on success — meson's custom_targets see those as "already done" and
# skip the would-be-network step.
#
# WARNING: the freebsd/mimalloc pins below MUST stay in sync with
# meson_options.txt (FreeBSD) and build-tools/mimalloc/meson.build
# (mimalloc commit). If you bump one and not the other, this build
# fetches different bytes than `meson compile` does.

let
  freebsdVersion = "14.4";
  freebsdSrcUrl  = "https://download.freebsd.org/releases/amd64/${freebsdVersion}-RELEASE/src.txz";
  freebsdSha256  = "1b337069331dbf0791dc43129900377da256ba1ec0ef94e0d3f6e6232de84fe2";

  freebsdSrcTarball = fetchurl {
    url    = freebsdSrcUrl;
    sha256 = freebsdSha256;
    name   = "freebsd-src-${freebsdVersion}.txz";
  };

  mimallocCommit = "b1963961a5cdb1996c9ad2e356014089b7a94ea3";
  mimallocSrc = fetchFromGitHub {
    owner  = "microsoft";
    repo   = "mimalloc";
    rev    = mimallocCommit;
    sha256 = "sha256-sjjZhGlqJtT+Jg45qbCCrolQpmX8uRrqopZn4EefKT0=";
  };

  llvm = llvmPackages_18;

  # The codegen pipeline needs python3 with libclang + pyyaml. The
  # Makefile-driven flow uses `uv venv && uv sync` to build a project
  # venv with both; in the Nix sandbox we provide them up-front and
  # set the venv check (in the root meson.build) to find_program("python3").
  pythonForCodegen = python3.withPackages (ps: with ps; [
    pyyaml
    libclang
  ]);
in stdenv.mkDerivation {
  pname = "yos";
  version = "0.1";

  inherit src;

  nativeBuildInputs = [
    meson ninja pkg-config
    pythonForCodegen
    # codegen's snapshot.py invokes `clang -E -v /dev/null` to enumerate
    # the host's libc include search path. We need the WRAPPED clang
    # (not clang-unwrapped) so the wrapper exports
    # NIX_CC_WRAPPER_TARGET_HOST_x86_64-unknown-linux-gnu, which makes
    # clang report glibc-dev under /nix/store. The unwrapped clang
    # instead reports baked-in defaults (/usr/include, /usr/local/include)
    # — none of which exist in the Nix sandbox, so snapshot.py finds
    # nothing valid, errors out, and the whole build aborts at
    # `Generating build-tools/host-libc/snapshot`.
    llvm.clang
    llvm.libllvm             # llvm-ar/ranlib for the wasm sysroot stubs
    binaryen                 # wasm-opt (used by tests and codegen for the Tier-2 sidecar)
  ];

  buildInputs = [
    libffi
    # Host msgpack-c — yctl daemon's RPC encoder/decoder
    # (src/yos/yctl/yctl.c). pkg-config picks the `msgpack-c` (or older
    # `msgpack`) module up from this input.
    msgpack-c
    # Host openssl — libssl + libcrypto linked into the yos binary so
    # wasm guests can call env.SSL_*, env.EVP_*, env.RAND_*, env.ERR_*
    # via the bridges in src/yos/impl/libc/openssl.c. pkg-config
    # exposes the `openssl` module; meson's `with_openssl=auto` picks
    # it up. Constrained Apple SDKs that don't ship libssl (iOS sim
    # in some configurations) can disable via -Dwith_openssl=disabled.
    openssl
    # Host liblua-5.1 — linked into yos so wasm guests can call
    # env.lua_* / env.luaL_* via src/yos/impl/libc/liblua.c. pkg-config
    # name is `lua-5.1` (nixpkgs); meson's `with_liblua=auto` picks
    # it up. Disable with -Dwith_liblua=disabled if a target SDK
    # doesn't have lua-5.1.
    lua5_1
    # Host libarchive — linked into yos so wasm guests (tar/cpio/unzip
    # class) can call env.archive_* via src/yos/impl/libc/libarchive.c.
    # pkg-config name is `libarchive`; meson's `with_libarchive=auto`
    # picks it up. Disable with -Dwith_libarchive=disabled.
    libarchive
  ] ++ lib.optionals stdenv.isDarwin [
    libiconv
    # Without this, nix's stdenv on darwin gates libSystem at the 10.12
    # SDK; preadv / pwritev / mknodat (POSIX-2017, shipped in macOS 11+)
    # don't appear in the header surface and the build fails with
    # "call to undeclared function". The dev shell in flake.nix does
    # the same override via shellHook — keep them in sync.
    apple-sdk_13
  ];

  # MACOSX_DEPLOYMENT_TARGET / SDKROOT must be set BEFORE meson runs so
  # the configure-time feature probes see the modern symbol set. nix's
  # stdenv setup-hook pins SDKROOT to its default 10.12 SDK; override
  # in preBuild (which fires after the hook).
  preBuild = lib.optionalString stdenv.isDarwin ''
    export MACOSX_DEPLOYMENT_TARGET=13.0
    export SDKROOT=${apple-sdk_13}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
    export NIX_CFLAGS_COMPILE="$NIX_CFLAGS_COMPILE -isysroot $SDKROOT"
    export NIX_LDFLAGS="$NIX_LDFLAGS -L$SDKROOT/usr/lib"
  '';

  # No standard configurePhase — we run meson manually below to
  # control where the build directory lives (Nix store paths are
  # read-only; meson wants to write back into source unless we point
  # it elsewhere).
  dontConfigure = true;

  buildPhase = ''
    runHook preBuild

    # The meson build runs from ./build-<host_os>; mirror that so any
    # path-sensitive code (recipes, generated-file references) keeps
    # working without surgery.
    BUILD_DIR="$TMPDIR/build-${if stdenv.isDarwin then "darwin" else "linux"}"

    # ── Stage FreeBSD source where build-tools/freebsd/meson.build expects ──
    FB_OUT="$BUILD_DIR/build-tools/freebsd"
    mkdir -p "$FB_OUT"
    cp ${freebsdSrcTarball} "$FB_OUT/src.txz"
    mkdir -p "$FB_OUT/src"
    tar -xJf ${freebsdSrcTarball} -C "$FB_OUT/src" --strip-components=0
    touch "$FB_OUT/src/.extracted"

    # ── Stage mimalloc git tree where build-tools/mimalloc expects ──
    MI_OUT="$BUILD_DIR/build-tools/mimalloc"
    mkdir -p "$MI_OUT/mimalloc"
    cp -r ${mimallocSrc}/* "$MI_OUT/mimalloc/"
    chmod -R u+w "$MI_OUT/mimalloc"
    echo "${mimallocCommit}" > "$MI_OUT/mimalloc.stamp"

    # ── meson setup + ninja ──
    # No project venv inside the sandbox; the meson root falls back to
    # `find_program('python3')` which we provide as `pythonForCodegen`
    # above (PATH-injected via nativeBuildInputs).
    meson setup "$BUILD_DIR" "$src" \
        --prefix=$out \
        -Dfreebsd_version=${freebsdVersion} \
        -Dfreebsd_src_sha256=${freebsdSha256}

    # `ninja yos` doesn't work because meson's target naming for
    # executable('yos', ..., install: false) is `src/yos/yos`. Asking
    # for the path directly works.
    ninja -C "$BUILD_DIR" src/yos/yos

    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall

    BUILD_DIR="$TMPDIR/build-${if stdenv.isDarwin then "darwin" else "linux"}"
    install -Dm755 "$BUILD_DIR/src/yos/yos" $out/bin/yos

    runHook postInstall
  '';

  meta = with lib; {
    description = "yos — wasm32 FreeBSD-libc emulator host runtime";
    platforms   = platforms.linux ++ platforms.darwin;
  };
}
