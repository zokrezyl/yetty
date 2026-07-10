{ buildRecipe, lua }:

buildRecipe {
  pname = "lua-mpack";
  version = "1.0.13";
  url = "https://github.com/libmpack/libmpack-lua/archive/refs/tags/1.0.13.tar.gz";
  sha256 = "436a6a3973207403d3f20082002c32e74c25d9149ff2516dc06b0b41514044bf";
  deps = [ lua ];
  extraTarballs = [{
    # lua-mpack pulls libmpack 1.0.5 in as the C msgpack codec; the
    # recipe expects this filename under $WORK.
    name = "libmpack-1.0.5.tar.gz";
    url = "https://github.com/libmpack/libmpack/archive/refs/tags/1.0.5.tar.gz";
    sha256 = "4ce91395d81ccea97d3ad4cb962f8540d166e59d3e2ddce8a22979b49f108956";
  }];
}
