{ buildRecipe }:

# Z shell, autoconf-driven port. Cross-compiled to wasm32 with most
# of zsh's "fancy" features disabled (zle/line editing, multibyte,
# dynamic modules, gdbm, pcre, capabilities) — what's left is a
# parsing+executing zsh that runs scripts and pipelines.
buildRecipe {
  pname = "zsh";
  version = "5.9";
  url = "https://www.zsh.org/pub/zsh-5.9.tar.xz";
  sha256 = "9b8d1ecedd5b5e81fbf1918e876752a7dd948e05c1a0dba10ab863842d45acd5";
}
