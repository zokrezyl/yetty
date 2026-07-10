{ stdenv, lib, fetchurl, cmake, gnumake, perl, gawk, gnused, gettext
, coreutils, gnutar, gzip, which, pkg-config, curl, gcc, python3
, toolchain, sysroot, src
, yos ? null   # see postInstall runner-conversion note below
}:

# Wraps a `build-tools/wasm-pkg/configs/<name>/build.sh` recipe in a
# Nix derivation. The recipe is run as-is — no rewrites — so the same
# script works under `tools/wasm-pkg.sh` and under Nix.
#
# Inputs:
#   pname / version / url / sha256  → identifies the upstream tarball
#   src                             → yos repo source (gives us
#                                     build-tools/wasm-pkg/configs/<recipeName>/build.sh)
#   recipeName ?= pname             → directory under
#                                     build-tools/wasm-pkg/configs/
#   deps        ?= []               → list of dep derivations; their
#                                     outpaths are joined with spaces
#                                     and fed to the recipe as
#                                     $DEP_PREFIXES (positional!).
#   extraTarballs                   → secondary tarballs the recipe
#                                     fetches (luv → lua-compat-5.3,
#                                     lua-mpack → libmpack, …).
#   preRecipe                       → shell snippet between env setup
#                                     and recipe invocation; useful for
#                                     ROOT-shadow tricks.
#   extraNativeBuildInputs          → bolt on extras (cmake-using deps
#                                     get cmake automatically; this is
#                                     for one-off needs).
#
# What the wrapper does:
#   - Pre-fetches the tarball via fetchurl (with the recipe's SHA256)
#     and places it at $WORK/<pname>-<version>.tar.gz so the recipe's
#     `[[ -f $TARBALL ]] || curl …` short-circuits. The recipe's own
#     `sha256sum -c` then verifies against the pre-fetched bytes.
#   - Sets ROOT/PREFIX/WORK/WASM_CC/WASM_SYSROOT/WASM_CFLAGS/
#     WASM_LDFLAGS/DEP_PREFIXES — same shape as tools/wasm-pkg.sh.
#   - Runs the recipe; the recipe writes to $PREFIX (= $out).

{ pname
, version
, url
, sha256
, recipeName ? pname
, deps ? []
, extraTarballs ? []
, preRecipe ? ""
, extraNativeBuildInputs ? []
, ...
}@args:

let
  tarball = fetchurl { inherit url sha256; name = "${pname}-${version}.tar.gz"; };

  fetchedExtras = map (e: {
    name = e.name;
    file = fetchurl { inherit (e) url sha256; name = e.name; };
  }) extraTarballs;

  # Scope the src derivation passes to ONLY the files this recipe
  # actually reads from $ROOT — its own configs/<recipeName>/ subtree.
  # Previously `inherit src` here meant every wasm-pkg derivation (zsh,
  # nvim, lua, libuv, openssl, openssh, …) took the entire yos repo as
  # input. Editing src/yos/impl/sig.c then invalidated the input hash
  # of every wasm-pkg, forcing a full umbrella rebuild even though the
  # wasm packages don't depend on host-side yos source at all.
  #
  # Filter src to build-tools/wasm-pkg/configs/<recipeName>/ + the
  # shared sha256sum shim under tools/wasm-pkg-shims/. After this,
  # changes outside those paths leave the cached wasm-pkg outputs
  # untouched. Each recipe lives in its own subdir, so per-recipe edits
  # still invalidate only that one wasm-pkg.
  scopedSrc = lib.cleanSourceWith {
    name = "yos-recipe-${recipeName}-src";
    inherit src;
    filter = path: _type:
      let
        rel = lib.removePrefix ((toString src) + "/") (toString path);
        keepDir = "build-tools/wasm-pkg/configs/${recipeName}";
        keepShim = "tools/wasm-pkg-shims";
      in
           lib.hasPrefix keepDir rel
        || lib.hasPrefix keepShim rel
        # Parent directories of the kept paths must pass too, otherwise
        # cleanSourceWith prunes them and the leaves become unreachable.
        || rel == "build-tools"
        || rel == "build-tools/wasm-pkg"
        || rel == "build-tools/wasm-pkg/configs"
        || rel == "tools";
  };

  # Mirror the COMMON_CFLAGS / COMMON_LDFLAGS arrays from
  # tools/wasm-pkg.sh. Recipes append to these via $WASM_CFLAGS rather
  # than setting their own from scratch — keeping the two drivers in
  # sync is a maintenance requirement.
  wasmCFlags = lib.concatStringsSep " " [
    "-target wasm32-unknown-unknown"
    "-nostdlib"
    "-nostdinc"
    "--sysroot=${sysroot}"
    "-isystem ${sysroot}/usr/include"
    # Resource dir is provided by the toolchain wrapper via
    # YOS_WASM_CLANG_RESOURCE_DIR; recipes that pass -nostdinc end up
    # with that resource dir injected as -idirafter by the wasm-clang
    # shim, so freestanding headers (stdarg.h, stddef.h, …) still
    # resolve from clang's resource tree.
    "-D__i386__=1"
    "-O2"
    "-fno-builtin"
    "-ffreestanding"
    "-D__yos__=1"
  ];
  wasmLDFlags = lib.concatStringsSep " " [
    "-Wl,--no-entry"
    "-Wl,--allow-undefined"
    "-Wl,--export-all"
  ];
in stdenv.mkDerivation (lib.recursiveUpdate {
  inherit pname version;
  src = scopedSrc;

  nativeBuildInputs = [
    toolchain
    cmake gnumake perl gawk gnused gettext coreutils gnutar gzip
    which pkg-config curl python3
  ] ++ extraNativeBuildInputs;

  # Static archives we produce don't need stripping/patching.
  dontStrip = true;
  dontPatchELF = true;
  dontFixup = true;

  # Recipes are self-configuring + self-installing. We just point them.
  dontConfigure = true;

  buildPhase = ''
    runHook preBuild

    export ROOT="$src"
    export WORK="$TMPDIR/work"
    export PREFIX="$out"
    # The `wasm-clang` shim under toolchain/bin filters darwin-only
    # flags and forwards to clang-unwrapped via YOS_WASM_CLANG (set up
    # by the toolchain wrapper). Recipes call $WASM_CC; we point at
    # the shim.
    export WASM_CC="${toolchain}/bin/wasm-clang"
    export WASM_SYSROOT="${sysroot}"
    export WASM_CFLAGS="${wasmCFlags}"
    export WASM_LDFLAGS="${wasmLDFlags}"
    export DEP_PREFIXES="${lib.concatMapStringsSep " " (d: "${d}") deps}"

    mkdir -p "$WORK"
    # Pre-place the verified tarball; recipe's curl call is skipped
    # when the file exists, and its `sha256sum -c` then runs against
    # the bytes Nix supplied.
    cp ${tarball} "$WORK/${pname}-${version}.tar.gz"

    # Same pre-place treatment for any secondary tarballs the recipe
    # fetches (lua-compat-5.3, libmpack, …).
    ${lib.concatMapStringsSep "\n    " (e: ''cp ${e.file} "$WORK/${e.name}"'') fetchedExtras}

    # Per-recipe escape hatch (see preRecipe doc above).
    ${preRecipe}

    # Recipes are read-only Nix store paths. Some packages need to
    # patch the recipe text itself (nvim's FindLibuv.cmake stub, for
    # one — see nvim/default.nix). Copy the recipe to a writable
    # location before invoking it; preRecipe may have patched it.
    RECIPE="$TMPDIR/build.sh"
    if [ ! -f "$RECIPE" ]; then
      cp "$src/build-tools/wasm-pkg/configs/${recipeName}/build.sh" "$RECIPE"
      chmod +w "$RECIPE"
    fi
    bash "$RECIPE"

    runHook postBuild
  '';

  # Recipe already installed into $out via $PREFIX. Skip installation
  # but still fire postInstall so the runner-script conversion below
  # gets a chance to run (the default installPhase invokes runHook
  # postInstall; ours has to do it explicitly because we override the
  # whole phase).
  installPhase = ''
    runHook preInstall
    runHook postInstall
  '';

  # ── Runner-script conversion ─────────────────────────────────────────
  # Recipes install their wasm executables as `$out/bin/<name>.wasm` —
  # honest about what the file is, but ugly at the user's PATH and
  # confusing inside symlinkJoin trees (`zsh.wasm`, `nvim.wasm`). Convert
  # to the same shape FreeBSD-tools use:
  #   $out/libexec/<name>     the wasm module (no suffix)
  #   $out/bin/<name>         shell runner (`exec yos $out/libexec/<name> "$@"`)
  # so PATH lookup yields a normal-looking `zsh` / `nvim`.
  #
  # Library-only packages (lua, libuv, msgpack-c, …) ship .a archives
  # under $out/lib and have no $out/bin at all — the loop below short-
  # circuits when the dir is empty.
  postInstall = ''
    if [ -d "$out/bin" ]; then
      mkdir -p "$out/libexec"
      shopt -s nullglob
      for w in "$out"/bin/*.wasm; do
        name="$(basename "$w" .wasm)"
        mv "$w" "$out/libexec/$name"
        cat > "$out/bin/$name" <<RUNNER_EOF
    #!/usr/bin/env bash
    exec ${if yos != null then "${yos}/bin/yos" else "yos"} "$out/libexec/$name" "\$@"
    RUNNER_EOF
        chmod +x "$out/bin/$name"
      done
    fi
  '';
} (removeAttrs args [
  "pname" "version" "url" "sha256" "recipeName" "deps"
  "extraTarballs" "preRecipe" "extraNativeBuildInputs"
]))
