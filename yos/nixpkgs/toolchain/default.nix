{ stdenv, lib, llvmPackages_18, binaryen, makeWrapper, src }:

# yos wasm toolchain. Bundles, on PATH:
#   - wasm-clang  (the shim from build-tools/wasm-clang, which filters
#                  the darwin-only -arch / -mmacos-version-min flags
#                  before forwarding to the unwrapped clang)
#   - clang       (clang-unwrapped — bare; the wrapped nix clang would
#                  inject host-target flags wasm-ld doesn't grok)
#   - wasm-ld     (lld with the wasm32 target enabled)
#   - wasm-opt    (binaryen, needed for the asyncify pass)
#   - llvm-ar / llvm-ranlib
#
# The wasm-clang shim discovers the unwrapped clang via two env vars
# we set in the wrapper:
#   YOS_WASM_CLANG          → unwrapped clang binary
#   YOS_WASM_CLANG_RESOURCE_DIR → clang resource dir (for stdarg.h /
#                              stddef.h / immintrin.h that
#                              clang-unwrapped doesn't ship by itself)

let
  llvm = llvmPackages_18;
  clangBin = llvm.clang-unwrapped;
  # `clang` (the wrapped one) carries the resource dir; the unwrapped
  # one doesn't. The shim needs the dir path so it can pass
  # -resource-dir + -idirafter <dir>/include for freestanding headers.
  resourceDir = "${llvm.clang}/resource-root";
in stdenv.mkDerivation {
  pname = "yos-toolchain";
  version = "0.1";

  inherit src;

  nativeBuildInputs = [ makeWrapper ];

  dontConfigure = true;
  dontBuild = true;

  installPhase = ''
    runHook preInstall

    mkdir -p $out/bin

    # Wrap build-tools/wasm-clang so YOS_WASM_CLANG and
    # YOS_WASM_CLANG_RESOURCE_DIR are injected before the script runs.
    install -Dm755 $src/build-tools/wasm-clang $out/share/yos/wasm-clang
    makeWrapper $out/share/yos/wasm-clang $out/bin/wasm-clang \
      --set YOS_WASM_CLANG ${clangBin}/bin/clang \
      --set YOS_WASM_CLANG_RESOURCE_DIR ${resourceDir} \
      --prefix PATH : ${lib.makeBinPath [ clangBin llvm.lld llvm.libllvm binaryen ]}

    # Re-export the upstream binaries so devShell users / Makefile
    # invocations can call them directly without going through wasm-clang.
    ln -sf ${clangBin}/bin/clang        $out/bin/clang
    ln -sf ${llvm.lld}/bin/wasm-ld      $out/bin/wasm-ld
    ln -sf ${llvm.libllvm}/bin/llvm-ar  $out/bin/llvm-ar
    ln -sf ${llvm.libllvm}/bin/llvm-ranlib $out/bin/llvm-ranlib
    ln -sf ${binaryen}/bin/wasm-opt     $out/bin/wasm-opt

    runHook postInstall
  '';

  meta = with lib; {
    description = "yos wasm32 toolchain (clang + wasm-ld + wasm-opt + wasm-clang shim)";
    platforms = platforms.linux ++ platforms.darwin;
  };
}
