{ lib }:

# Filter a full-repo src derivation down to a small set of paths, so
# that derivations which only need files under build-tools/ (toolchain,
# sysroot, wasm-pkg recipes) aren't invalidated when totally unrelated
# files (src/yos/impl/sig.c, tests/, docs/) change.
#
# Without this filter, every `inherit src` derivation hashes the whole
# repo and a one-line edit to a yos host source file rebuilds the
# entire wasm-pkg world (zsh, nvim, openssh, openssl, lua, libuv, …),
# which the user (rightly) calls out as ~10 minutes wasted.
#
# Usage:
#   scopedSrc = scopeSrc {
#     inherit src;
#     name = "yos-toolchain-src";
#     keep = [
#       "build-tools/wasm-clang"
#     ];
#   };
#
# `keep` paths are repo-relative prefixes. Anything matching one of
# them (file or dir) is included, along with whatever parent dirs are
# needed for cleanSourceWith to keep the leaves reachable.

{ src, name, keep }:

let
  prefix = (toString src) + "/";

  # Walk every keep-path's parent chain to build the set of intermediate
  # directories cleanSourceWith must let through.
  parentDirsOf = p:
    let parts = lib.splitString "/" p;
        accum = lib.foldl' (acc: seg:
          let last = if acc == [] then "" else lib.last acc;
              next = if last == "" then seg else "${last}/${seg}";
          in acc ++ [ next ]
        ) [] parts;
    in lib.init accum;  # drop p itself; we only want its parents

  parents = lib.unique (lib.concatMap parentDirsOf keep);

in lib.cleanSourceWith {
  inherit name src;
  filter = path: _type:
    let rel = lib.removePrefix prefix (toString path);
    in
      rel == ""
      || lib.any (k: lib.hasPrefix k rel) keep
      || lib.elem rel parents;
}
