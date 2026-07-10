{ buildRecipe }:

# Drives build-tools/wasm-pkg/configs/lua/build.sh — produces both
# wasm32 liblua.a and a HOST lua binary (used by neovim's build-time
# codegen).
buildRecipe {
  pname = "lua";
  version = "5.1.5";
  url = "https://www.lua.org/ftp/lua-5.1.5.tar.gz";
  sha256 = "2640fc56a795f29d28ef15e13c34a47e223960b0240e8cb0a82d9b0738695333";
}
