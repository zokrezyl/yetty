{ stdenv, lib, binaryen, toolchain, sysroot, yos }:

# Generic builder for single-file yos wasm packages (think
# `mkYosPackage { sourceFiles = [ "main.c" ]; }`). Smaller cousin of
# `buildRecipe`, which drives a full build.sh script.
#
# Usage:
#   mkYosPackage {
#     pname = "hello";
#     version = "1.0";
#     src = ./.;
#     sourceFiles = [ "hello.c" ];
#     asyncify = true;   # enables wasm-opt --asyncify post-link
#   }
#
# What it does:
#   - Sets YOS_SYSROOT so the wasm-clang shim finds includes + crt1
#   - Default buildPhase calls `wasm-clang -target wasm32-unknown-unknown
#     -nostdlib --sysroot=$YOS_SYSROOT -isystem $YOS_SYSROOT/usr/include
#     -O2 -o $pname.wasm $sourceFiles`
#   - If asyncify=true, runs `wasm-opt --asyncify -O2` over the result
#   - Installs the wasm to $out/libexec/$pname (no suffix) and a
#     runner script to $out/bin/$pname that execs the Nix-built yos
#     with that wasm module. The runner is fully self-contained — yos
#     does not need to be on PATH at runtime.

{ pname
, version ? "0"
, src
, sourceFiles ? null
, buildPhase ? null
, installPhase ? null
, asyncify ? true
, extraBuildInputs ? []
, ...
}@args:

let
  defaultBuildPhase = ''
    runHook preBuild

    wasm-clang \
      -target wasm32-unknown-unknown -nostdlib -nostdinc \
      --sysroot="$YOS_SYSROOT" \
      -isystem "$YOS_SYSROOT/usr/include" \
      -D__i386__=1 -O2 -fno-builtin -ffreestanding \
      -Wl,--no-entry -Wl,--allow-undefined -Wl,--export-all \
      "$YOS_SYSROOT/usr/lib/crt1.o" \
      ${lib.concatStringsSep " " sourceFiles} \
      -o ${pname}.wasm

    ${lib.optionalString asyncify ''
      wasm-opt --asyncify -O2 ${pname}.wasm -o ${pname}.asyncify.wasm
      mv ${pname}.asyncify.wasm ${pname}.wasm
    ''}

    runHook postBuild
  '';

  defaultInstallPhase = ''
    runHook preInstall

    mkdir -p $out/bin
    cp ${pname}.wasm $out/bin/${pname}.wasm

    cat > $out/bin/${pname} <<EOF
    #!/usr/bin/env bash
    # Self-contained runner: hardcodes the Nix-built yos store path
    # so this script works without any PATH setup. Forwards all argv
    # to the wasm module.
    exec ${yos}/bin/yos $out/bin/${pname}.wasm "\$@"
    EOF
    chmod +x $out/bin/${pname}

    runHook postInstall
  '';
in

stdenv.mkDerivation (lib.recursiveUpdate {
  inherit pname version src;

  nativeBuildInputs = [ toolchain binaryen ] ++ extraBuildInputs;

  YOS_SYSROOT = "${sysroot}";

  dontConfigure = args.dontConfigure or true;

  buildPhase = if buildPhase != null
    then buildPhase
    else if sourceFiles != null
    then defaultBuildPhase
    else throw "mkYosPackage(${pname}): supply either `sourceFiles` or `buildPhase`";

  installPhase = if installPhase != null
    then installPhase
    else defaultInstallPhase;

  dontStrip = true;
  dontPatchELF = true;
  dontFixup = true;
} (removeAttrs args [
  "pname" "version" "src" "sourceFiles" "buildPhase"
  "installPhase" "asyncify" "extraBuildInputs"
]))
