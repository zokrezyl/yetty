{ buildRecipe, fetchurl, writeShellScriptBin, llvmPackages_18, sysroot
, lua, libuv, msgpack-c, unibilium, libvterm
, tree-sitter, lpeg, lua-mpack, luv }:

# nvim 0.10.4 — neovim built for wasm32 against the yos sysroot and
# the deps we already staged. The recipe in
# build-tools/wasm-pkg/configs/nvim/build.sh:
#   - patches the cmake/source tree for cross-compile to wasm32
#   - builds a tiny host `lua-codegen` helper with luaopen_nlua0 baked
#     into package.preload (replaces the dlopen'd nlua0 module)
#   - configures the wasm32 build with a FindLibuv stub
#   - wasm-opt --asyncify the result
#
# DEP_PREFIXES is positional: lua libuv msgpack-c unibilium libvterm
# tree-sitter lpeg lua-mpack luv. We pass deps in this order so the
# recipe's `read LUA_P LIBUV_P MSGPACK_P …` line resolves correctly.
#
# preRecipe job:
#
# 1. The recipe's host-helper build reads .c files from
#    $ROOT/build-linux/wasm-pkgs/lua-5.1.5/src/src and
#    $ROOT/build-linux/wasm-pkgs/lpeg-1.1.0/src directly. ROOT in the
#    Nix sandbox is the read-only repo store path, so we make a
#    writable shadow of ROOT and extract the lua + lpeg tarballs into
#    the expected build-linux/wasm-pkgs subdirs.
#
# 2. nvim's recipe builds its own link line — explicit crt1.o + -lc —
#    and expects WASM_CC to be a *plain* clang, not the wasm-clang
#    shim. The shim's flag-filtering would interfere with the
#    cross-link Detection in cmake. Override WASM_CC to a
#    clangWithResources wrapper that just adds `-resource-dir` so
#    freestanding headers (stdarg.h, stddef.h) still resolve.

let
  llvm = llvmPackages_18;

  # Plain clang with -resource-dir baked in. nvim's recipe wants a
  # *bare* clang as WASM_CC; clang-unwrapped is bare but lacks its own
  # resource dir (stdarg.h, stddef.h, immintrin.h all live in
  # llvmPackages_18.clang-unwrapped.lib/lib/clang/18). Wrap so those
  # are findable.
  #
  # nvim's recipe passes -nostdinc, which strips clang's builtin
  # include path even when -resource-dir is set. Re-add the resource
  # include via -idirafter so freestanding headers (stdarg.h,
  # stddef.h) still resolve, and the user's -isystem sysroot wins on
  # any header that exists in both places. Same trick the wasm-clang
  # shim plays for -nostdinc invocations.
  resourceRoot = "${llvm.clang-unwrapped.lib}/lib/clang/18";
  clangWithResources = writeShellScriptBin "clang" ''
    exec ${llvm.clang-unwrapped}/bin/clang \
      -resource-dir=${resourceRoot} \
      -idirafter ${resourceRoot}/include \
      "$@"
  '';

  luaTarball = fetchurl {
    url = "https://www.lua.org/ftp/lua-5.1.5.tar.gz";
    sha256 = "2640fc56a795f29d28ef15e13c34a47e223960b0240e8cb0a82d9b0738695333";
  };
  lpegTarball = fetchurl {
    url = "http://www.inf.puc-rio.br/~roberto/lpeg/lpeg-1.1.0.tar.gz";
    sha256 = "4b155d67d2246c1ffa7ad7bc466c1ea899bbc40fef0257cc9c03cecbaed4352a";
  };
in buildRecipe {
  pname = "nvim";
  version = "0.10.4";
  url = "https://github.com/neovim/neovim/archive/refs/tags/v0.10.4.tar.gz";
  sha256 = "10413265a915133f8a853dc757571334ada6e4f0aa15f4c4cc8cc48341186ca2";
  deps = [
    lua libuv msgpack-c unibilium libvterm
    tree-sitter lpeg lua-mpack luv
  ];

  preRecipe = ''
    # ── Patch 1: writable ROOT shadow with lua/lpeg sources at the
    # paths the recipe's host-helper build expects.
    SHADOW="$TMPDIR/yos-shadow"
    mkdir -p "$SHADOW"
    for entry in "$ROOT"/*; do
      ln -s "$entry" "$SHADOW/$(basename "$entry")"
    done
    # The shadow needs to be a real dir, not a symlink, for the
    # build-linux subtree we're going to populate.
    rm -f "$SHADOW/build-linux"
    mkdir -p "$SHADOW/build-linux/wasm-pkgs/lua-5.1.5/src" \
             "$SHADOW/build-linux/wasm-pkgs/lpeg-1.1.0/src"
    tar -xzf ${luaTarball} -C "$SHADOW/build-linux/wasm-pkgs/lua-5.1.5/src" \
        --strip-components=1
    tar -xzf ${lpegTarball} -C "$SHADOW/build-linux/wasm-pkgs/lpeg-1.1.0/src" \
        --strip-components=1
    export ROOT="$SHADOW"

    # ── Patch 2: nvim's recipe builds its own link line (explicit
    # crt1.o + -lc), so WASM_CC needs to be a *plain* clang rather
    # than the wasm-clang shim. clangWithResources is clang-unwrapped
    # with -resource-dir baked in so stdarg.h / stddef.h resolve.
    export WASM_CC=${clangWithResources}/bin/clang
  '';
}
