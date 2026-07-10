{ buildRecipe, lua, libuv }:

buildRecipe {
  pname = "luv";
  version = "1.48.0-2";
  url = "https://github.com/luvit/luv/archive/refs/tags/1.48.0-2.tar.gz";
  sha256 = "e64cd8a0197449288b37df6ca058120e8d2308fc305f543162b5bf3e92273a05";
  deps = [ lua libuv ];
  extraTarballs = [{
    # luv vendors lua-compat-5.3 to bridge Lua 5.1 ↔ Lua 5.3 API gaps;
    # the recipe expects this exact filename under $WORK.
    name = "lua-compat-5.3-0.13.tar.gz";
    url = "https://github.com/keplerproject/lua-compat-5.3/archive/refs/tags/v0.13.tar.gz";
    sha256 = "f5dc30e7b1fda856ee4d392be457642c1f0c259264a9b9bfbcb680302ce88fc2";
  }];
}
