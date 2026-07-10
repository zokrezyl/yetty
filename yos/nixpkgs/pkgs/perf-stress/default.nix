{ stdenv, lib, binaryen, toolchain, sysroot
, yos ? null   # optional — when null, the bin/ runner falls back to
              # `yos` on PATH instead of hardcoding a store path.
              # Same convention as build-freebsd-tool.nix uses.
, src
}:

# yos-perf-stress — combined fork / pthread / locking / procfs /
# execve / I/O stress test, packaged as a runnable wasm so it lands
# in the umbrella's `$out/libexec/perf-stress` and runs from inside
# `./tools/yos.sh` like any other wasm tool:
#
#     ./tools/yos.sh                  # opens wasm-zsh prompt
#     nixem% perf-stress &            # background-run the stress
#     nixem% wait                     # or observe stdout/stderr live
#
# Sources: tests/ut/libc/test_perf_stress.c (a single file). Compiled
# with the same wasm32 toolchain + sysroot that everything else uses,
# linked against the project's crt1 (so main() gets argv — the execve
# phase re-execs argv[0] and re-enters main with argv[1]="child" as
# its self-detect sentinel), and post-processed with `wasm-opt
# --asyncify` because every fork() under yos needs the asyncify
# stack-unwind machinery to snapshot+restore the wasm linear memory.

stdenv.mkDerivation {
  pname   = "yos-perf-stress";
  version = "0.1";

  inherit src;

  dontConfigure = true;
  dontPatch     = true;
  dontStrip     = true;
  dontPatchELF  = true;
  dontFixup     = true;

  nativeBuildInputs = [ toolchain binaryen ];

  buildPhase = ''
    runHook preBuild

    wasm-clang \
      -target wasm32-unknown-unknown -nostdlib \
      --sysroot="${sysroot}" \
      -isystem "${sysroot}/usr/include" \
      -L "${sysroot}/usr/lib" \
      -D__i386__=1 -D__yos__=1 \
      -O2 \
      -Wl,--no-entry \
      -Wl,--allow-undefined \
      -Wl,--export=_start \
      -Wl,--export=main \
      -Wl,--export-all \
      "${sysroot}/usr/lib/crt1.o" \
      tests/ut/libc/test_perf_stress.c \
      -lc -lyos_stubs \
      -o perf-stress.raw.wasm

    # Asyncify pass — yos's fork()/setjmp()/longjmp() bridges drive
    # the wasm guest's call stack via Binaryen's asyncify rewrite.
    # The test forks (recursively) and uses setjmp-equivalent control
    # flow inside libc, so the binary MUST be asyncify-instrumented or
    # the very first fork traps. -O2 after the rewrite shrinks the
    # added bookkeeping back down without un-doing the instrumentation.
    wasm-opt --asyncify -O2 perf-stress.raw.wasm -o perf-stress.wasm

    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall

    mkdir -p $out/bin $out/libexec
    cp perf-stress.wasm $out/libexec/perf-stress

    # Host-side runner — when this package is consumed via the
    # umbrella `all` (which puts $out/libexec on the wasm-side PATH),
    # the runner in bin/ is what host shells see if they `nix shell
    # .#perf-stress`. From inside the wasm sandbox the wasm module
    # itself is execve-able via its libexec/ path, no shim needed.
    cat > $out/bin/perf-stress <<RUNNER
    #!/usr/bin/env bash
    exec ${if yos != null then "${yos}/bin/yos" else "yos"} \\
        "$out/libexec/perf-stress" "\$@"
    RUNNER
    chmod +x $out/bin/perf-stress

    runHook postInstall
  '';

  meta = with lib; {
    description = "yos fork/pthread/locking/procfs/execve/IO combined stress test (~111 procs + mutex+condvar+rwlock churn). Lands as $out/libexec/perf-stress so `./tools/yos.sh` can run it inline.";
    platforms   = platforms.linux ++ platforms.darwin;
  };
}
