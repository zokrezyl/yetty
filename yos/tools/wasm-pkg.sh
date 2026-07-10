#!/usr/bin/env bash
# Nix-lite driver for wasm32 packages building against yos's libc surface.
#
# Each package has a recipe at build-tools/wasm-pkg/configs/<name>/build.sh
# responsible for fetching, building, and installing into $PREFIX. This
# driver:
#   - sets the standard env (WASM_CC, WASM_SYSROOT, ROOT, PREFIX, WORK,
#     YOS_IMPORTS_INCLUDE);
#   - resolves inter-package deps via $DEPS so a downstream recipe sees
#     its deps' installs in $DEP_PREFIXES (space-separated).
#
# Output layout:
#   build-linux/wasm-pkgs/<name>-<version>/
#     ├── src/        upstream extracted source
#     ├── work/       (recipe may use)
#     ├── out/        installed artefacts ($PREFIX) — lib/, include/, manifest.txt
#     └── *.tar.*     fetched archives
#
# Usage:
#   tools/wasm-pkg.sh <name>            # build single package
#   tools/wasm-pkg.sh --all <list>      # build a list, deps must already be built
#   tools/wasm-pkg.sh --clean <name>    # wipe the package's work + out dirs
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SYSROOT="$ROOT/build-linux/sysroot"
CC="${WASM_CC:-clang}"
PKGS_DIR="$ROOT/build-linux/wasm-pkgs"
CONFIGS_DIR="$ROOT/build-tools/wasm-pkg/configs"

if [[ ! -d "$SYSROOT/usr/include" ]]; then
    echo "wasm-pkg: sysroot missing — run 'meson compile -C build-linux sysroot-skel' first" >&2
    exit 1
fi

# Common cflags every recipe inherits via $WASM_CFLAGS. Recipes may
# extend, not override.
COMMON_CFLAGS=(
    -target wasm32-unknown-unknown
    -nostdlib
    # `-nostdinc` keeps clang's resource-dir <stdint.h> / <inttypes.h>
    # / <stddef.h> out of the search path so FreeBSD's
    # <sys/stdint.h> wins (clang's typedefs use _least types, FreeBSD
    # uses _fast types — redefining each other = compile error).
    # `-isystem $clang_resource_dir/include` then re-adds clang's
    # intrinsics-only headers (immintrin.h, stdarg.h, …) that are
    # actually compiler-private.
    -nostdinc
    --sysroot="$SYSROOT"
    -isystem "$SYSROOT/usr/include"
    -isystem "$($CC -print-resource-dir)/include"
    # Convince FreeBSD's headers that we're i386 so the i386-gated
    # typedefs (mcontext_t, struct __mcontext, sigset_t shape, …)
    # are visible. Our extracted FreeBSD tree IS the i386 view; the
    # ABI matches wasm32 closely enough (32-bit pointers, same
    # alignment classes for the typedefs that cross the wasm/host
    # boundary). Real-asm-using bits are gated behind __GNUC__-asm
    # macros that we already neutralised in sys/cdefs.h.
    "-D__i386__=1"
    -O2
    -fno-builtin
    -ffreestanding
    "-D__yos__=1"
)
COMMON_LDFLAGS=(
    -Wl,--no-entry
    -Wl,--allow-undefined
    -Wl,--export-all
)

resolve_version () {
    awk -F= '/^VERSION=/ { gsub(/"/, "", $2); print $2; exit }' "$1"
}

build_one () {
    local name="$1"
    local recipe="$CONFIGS_DIR/$name/build.sh"
    if [[ ! -f "$recipe" ]]; then
        echo "wasm-pkg: no recipe at $recipe" >&2
        exit 1
    fi
    local version
    version="$(resolve_version "$recipe")"
    if [[ -z "$version" ]]; then
        echo "wasm-pkg: $recipe has no VERSION= line" >&2
        exit 1
    fi

    local outdir="$PKGS_DIR/${name}-${version}"
    local prefix="$outdir/out"

    if [[ -f "$prefix/manifest.txt" && -z "${REBUILD:-}" ]]; then
        echo "[$name] cached at $prefix (REBUILD=1 to force)"
        echo "$prefix"
        return 0
    fi

    mkdir -p "$prefix"

    local recipe_deps
    recipe_deps="$(awk -F= '/^DEPS=/ { gsub(/"/, "", $2); print $2; exit }' "$recipe")"
    : "${recipe_deps:=${DEPS:-}}"
    local dep_prefixes=()
    for dep in $recipe_deps; do
        local dep_recipe="$CONFIGS_DIR/$dep/build.sh"
        local dep_version
        dep_version="$(resolve_version "$dep_recipe")"
        local dep_prefix="$PKGS_DIR/${dep}-${dep_version}/out"
        if [[ ! -f "$dep_prefix/manifest.txt" ]]; then
            echo "wasm-pkg: $name needs '$dep' — build it first." >&2
            exit 1
        fi
        dep_prefixes+=("$dep_prefix")
    done

    ROOT="$ROOT" \
    PREFIX="$prefix" \
    WORK="$outdir" \
    WASM_CC="$CC" \
    WASM_SYSROOT="$SYSROOT" \
    WASM_CFLAGS="${COMMON_CFLAGS[*]}" \
    WASM_LDFLAGS="${COMMON_LDFLAGS[*]}" \
    DEP_PREFIXES="${dep_prefixes[*]:-}" \
    bash "$recipe"

    echo "$prefix"
}

case "${1:-}" in
    --all)
        shift
        for pkg in "$@"; do build_one "$pkg"; done
        ;;
    --clean)
        shift
        for pkg in "$@"; do
            recipe="$CONFIGS_DIR/$pkg/build.sh"
            v="$(resolve_version "$recipe")"
            rm -rf "$PKGS_DIR/${pkg}-${v}"
            echo "[wasm-pkg] cleaned $pkg-$v"
        done
        ;;
    "")
        echo "Usage: $0 <name> | $0 --all <name1> ... | $0 --clean <name1> ..." >&2
        exit 2
        ;;
    *)
        build_one "$1"
        ;;
esac
