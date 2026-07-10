{ buildRecipe, lua }:

buildRecipe {
  pname = "lpeg";
  version = "1.1.0";
  url = "http://www.inf.puc-rio.br/~roberto/lpeg/lpeg-1.1.0.tar.gz";
  sha256 = "4b155d67d2246c1ffa7ad7bc466c1ea899bbc40fef0257cc9c03cecbaed4352a";
  deps = [ lua ];
}
