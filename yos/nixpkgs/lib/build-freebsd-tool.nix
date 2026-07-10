{ stdenv, lib, binaryen, toolchain, sysroot, freebsd-src
, yos ? null   # optional — when null, the runner script defers to
              # `yos` on PATH instead of hardcoding a store path.
              # Lets you build wasm tools before the host yos
              # derivation is finished, which is convenient during
              # the bring-up phase.
}:

# Per-tool builder for FreeBSD-base userland (`bin/`, `usr.bin/`,
# `sbin/`, `usr.sbin/`, etc.). Reads .c files straight from the
# freebsd-src derivation, compiles them against our wasm32 sysroot,
# links a single .wasm, and installs $out/bin/<pname>.wasm plus a
# self-contained runner.
#
# Why not just run FreeBSD's bsd.prog.mk?
#   - bsd.prog.mk wants bmake + the share/mk/*.mk tree at the right
#     relative paths and assumes -L/usr/lib + -lc against a real
#     libc.a. We resolve every libc symbol as a wasm `env.<name>`
#     import (sysroot/usr/lib/libc.a is empty), so most of bsd.mk's
#     work is irrelevant.
#   - Each per-tool Makefile only carries SRCS/CFLAGS/LIBADD; pulling
#     those out by hand is faster than wrapping bmake to be quiet.
#
# Usage:
#   buildFreebsdTool {
#     pname        = "cat";
#     srcDir       = "bin/cat";          # under usr/src/
#     srcs         = [ "cat.c" ];
#     extraCflags  = [ ];                # tool-specific -D / -I
#     extraSrcDirs = [ ];                # PATHs to add to source search
#     asyncify     = true;               # post-link wasm-opt --asyncify
#   }
#
# Output:
#   $out/bin/<pname>          shell runner — `exec yos $out/libexec/<pname> "$@"`
#                             (no .wasm suffix; this IS the executable)
#   $out/libexec/<pname>      the wasm module (no suffix; runner only)
#   $out/manifest.txt         versioning + recipe metadata

{ pname
, srcDir              # path under usr.src/, e.g. "bin/cat"
, srcs                # list of basenames, e.g. [ "cat.c" ]
, extraSrcDirs ? []   # extra paths under usr.src/ to search for srcs
, extraCflags ? []
, extraLibs ? []      # like FreeBSD's LIBADD — accepted but currently
                      # all libs link to empty stubs (every libc fn
                      # resolves as an import). Listed for diffability
                      # against the upstream Makefile.
, libcExtras ? []     # well-known FreeBSD libc helpers to compile in
                      # alongside the tool. Names map to source paths
                      # (see libcExtraTable below). yos's bridge
                      # surface doesn't include every libc fn (notably
                      # getopt's argv shape mismatch + global-state
                      # accessors), so the simplest portable answer
                      # is to compile the upstream source in directly.
, asyncify ? false
, version ? freebsd-src.version
, ...
}@args:

let
  # Well-known libc helpers — name → (subdir, file). When a tool lists
  # `libcExtras = [ "getopt" ];`, the entries below resolve to a
  # specific FreeBSD-libc .c that gets compiled and linked alongside
  # the tool. Each gets a uniquified object name (libc_<file>.o) so a
  # tool whose own source has the same basename (basename(1) →
  # basename.c) doesn't collide. The libc-private include dir
  # (lib/libc/include) is added for namespace.h / un-namespace.h.
  libcExtraTable = {
    getopt      = { sub = "lib/libc/stdlib"; file = "getopt.c"; };
    getopt_long = { sub = "lib/libc/stdlib"; file = "getopt_long.c"; };
    err         = { sub = "lib/libc/gen";    file = "err.c"; };
    basename    = { sub = "lib/libc/gen";    file = "basename.c"; };
    strsignal   = { sub = "lib/libc/string"; file = "strsignal.c"; };
  };
  libcExtraEntries = map (n:
    let e = libcExtraTable.${n}; in
    "${freebsd-src}/usr/src/${e.sub}/${e.file}"
  ) libcExtras;
  libcExtraCflags = lib.optionals (libcExtras != []) [
    "-I${freebsd-src}/usr/src/lib/libc/include"
  ];
in

stdenv.mkDerivation (lib.recursiveUpdate {
  inherit pname version;

  # No `src` — sources come from freebsd-src store path.
  dontUnpack = true;
  dontConfigure = true;
  dontPatch = true;
  dontStrip = true;
  dontPatchELF = true;
  dontFixup = true;

  nativeBuildInputs = [ toolchain binaryen ];

  buildPhase = ''
    runHook preBuild

    SRC_BASE="${freebsd-src}/usr/src/${srcDir}"
    OBJS=()
    SEARCH_DIRS=( "$SRC_BASE" \
        ${lib.concatMapStringsSep " " (d: ''"${freebsd-src}/usr/src/${d}"'') extraSrcDirs} )

    compile_c () {
      local found="$1" obj="$2"
      wasm-clang \
        -target wasm32-unknown-unknown -nostdlib -nostdinc \
        --sysroot="${sysroot}" \
        -isystem "${sysroot}/usr/include" \
        -D__i386__=1 -D__yos__=1 \
        -O2 -fno-builtin -ffreestanding \
        -Wno-unused-parameter -Wno-unused-but-set-variable \
        ${lib.concatStringsSep " " (extraCflags ++ libcExtraCflags)} \
        -c "$found" -o "$obj"
    }

    # 1) Tool's own srcs — search SEARCH_DIRS, first hit wins
    # (FreeBSD .PATH semantics).
    for s in ${lib.concatStringsSep " " srcs}; do
      found=""
      for d in "''${SEARCH_DIRS[@]}"; do
        if [ -f "$d/$s" ]; then found="$d/$s"; break; fi
      done
      if [ -z "$found" ]; then
        echo "build-freebsd-tool($pname): source '$s' not found under any of:" >&2
        for d in "''${SEARCH_DIRS[@]}"; do echo "  $d" >&2; done
        exit 1
      fi
      obj="$TMPDIR/tool_$(basename "$s" .c).o"
      compile_c "$found" "$obj"
      OBJS+=("$obj")
    done

    # 2) libcExtras — compiled from absolute paths in freebsd-src
    # under uniquified obj names so basename(1)/basename(3) etc.
    # coexist without colliding.
    ${lib.concatMapStringsSep "\n    " (p:
      let base = builtins.baseNameOf p; in
      ''compile_c "${p}" "$TMPDIR/libc_${base}.o" && OBJS+=("$TMPDIR/libc_${base}.o")''
    ) libcExtraEntries}

    # Link with crt1.o + every .o we built. -lc and friends point at
    # empty stubs — actual libc fns resolve at wasm load time as
    # `env.<name>` imports against yos. -lyos_stubs carries the
    # FreeBSD-specific helper shims (capsicum no-ops etc.) that we
    # don't bridge but every consumer wants linkable.
    wasm-clang \
      -target wasm32-unknown-unknown -nostdlib \
      --sysroot="${sysroot}" \
      -L"${sysroot}/usr/lib" \
      -Wl,--no-entry -Wl,--allow-undefined -Wl,--export=_start \
      -Wl,--export=main \
      -o ${pname}.wasm \
      "${sysroot}/usr/lib/crt1.o" \
      "''${OBJS[@]}" \
      -lc -lyos_stubs ${lib.concatMapStringsSep " " (l: "-l${l}") extraLibs}

    ${lib.optionalString asyncify ''
      wasm-opt --asyncify -O2 ${pname}.wasm -o ${pname}.asyncify.wasm
      mv ${pname}.asyncify.wasm ${pname}.wasm
    ''}

    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall

    mkdir -p $out/bin $out/libexec
    # Wasm module lives under libexec/ so $out/bin/<pname> can be
    # the runner script *without* having to disambiguate from a
    # sibling *.wasm. Users (and PATH) only see the runner.
    cp ${pname}.wasm $out/libexec/${pname}

    # Runner script. When the yos derivation is supplied we hardcode
    # its store path (fully self-contained); otherwise the script
    # looks up `yos` on PATH so the tool is buildable independently
    # of the host runtime drv.
    cat > $out/bin/${pname} <<'EOF'
    #!/usr/bin/env bash
    EOF
    ${if yos != null
      then ''echo "exec ${yos}/bin/yos $out/libexec/${pname} \"\$@\"" >> $out/bin/${pname}''
      else ''echo "exec yos $out/libexec/${pname} \"\$@\"" >> $out/bin/${pname}''}
    chmod +x $out/bin/${pname}

    cat > $out/manifest.txt <<EOF
    name=${pname}
    version=${version}
    src_dir=usr/src/${srcDir}
    srcs=${lib.concatStringsSep " " srcs}
    EOF

    runHook postInstall
  '';

  meta = with lib; {
    description = "FreeBSD-base ${pname} ported to wasm32 (yos)";
    license     = licenses.bsd2;
    platforms   = platforms.linux ++ platforms.darwin;
  };
} (removeAttrs args [
  "pname" "srcDir" "srcs" "extraSrcDirs" "extraCflags" "extraLibs"
  "libcExtras" "asyncify" "version"
]))
