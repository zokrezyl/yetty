#!/usr/bin/env bash
# Verify release artifacts for a given tag
# Usage: ./verify-release.sh <tag>
#   e.g. ./verify-release.sh v0.1.50
#
# Downloads all platform archives from the GitHub release, extracts them,
# and checks that each desktop archive holds its installer binary (one
# archive per platform and installer variant: default / min / max) and that
# the webasm / mobile archives hold their expected files.

set -euo pipefail

TAG="${1:-}"
if [ -z "$TAG" ]; then
    echo "Usage: $0 <tag>"
    echo "  e.g. $0 v0.1.50"
    exit 1
fi

REPO="zokrezyl/yetty"
WORKDIR=$(mktemp -d)
ERRORS=0
CHECKS=0

cleanup() { rm -rf "$WORKDIR"; }
trap cleanup EXIT

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

pass() { ((CHECKS++)); echo -e "  ${GREEN}OK${NC}  $1"; }
fail() { ((CHECKS++)); ((ERRORS++)); echo -e "  ${RED}FAIL${NC}  $1"; }
info() { echo -e "\n${YELLOW}>>> $1${NC}"; }

check_file() {
    local dir="$1" path="$2" desc="$3"
    if [ -f "$dir/$path" ] && [ -s "$dir/$path" ]; then
        local size
        size=$(stat -c%s "$dir/$path" 2>/dev/null || stat -f%z "$dir/$path" 2>/dev/null)
        pass "$desc  ($path, ${size} bytes)"
    elif [ -f "$dir/$path" ]; then
        fail "$desc  ($path exists but is EMPTY)"
    else
        fail "$desc  ($path NOT FOUND)"
    fi
}

check_dir() {
    local dir="$1" path="$2" desc="$3"
    if [ -d "$dir/$path" ]; then
        pass "$desc  ($path/)"
    else
        fail "$desc  ($path/ NOT FOUND)"
    fi
}

# ---------------------------------------------------------------------------
# Checks for desktop platforms: the self-contained installers.
# Each desktop release archive holds exactly one installer binary; there is
# one archive per (platform, variant). Fonts, shaders, tools and demos are
# inside the installer's payload, not loose in the archive.
# ---------------------------------------------------------------------------

# check_installer <archive-base> <platform label> <exe extension>
# e.g. check_installer yetty-linux "Linux x86_64" ""
check_installer() {
    local base="$1" platform="$2" exe_ext="${3:-}"
    local variant archive dir root name
    for variant in default min max; do
        if [ "$variant" = "default" ]; then
            name="yinstall${exe_ext}"
            archive="${base}"
        else
            name="yinstall-${variant}${exe_ext}"
            archive="${base}-${variant}"
        fi
        if [ -n "$exe_ext" ]; then
            archive="${archive}.zip"
        else
            archive="${archive}.tar.gz"
        fi
        info "[$platform] ${variant} installer (${archive})"
        if [ ! -f "$archive" ]; then
            fail "${archive} not in release"
            continue
        fi
        dir="extract-${archive%%.*}-${variant}"
        mkdir -p "$dir"
        if [ -n "$exe_ext" ]; then
            unzip -qo "$archive" -d "$dir"
        else
            tar -xzf "$archive" -C "$dir"
        fi
        # The binary may sit at the archive root or one level down.
        root="$dir"
        [ -f "$root/$name" ] || root="$(dirname "$(find "$dir" -name "$name" -type f | head -n 1 || true)")"
        check_file "$root" "$name" "${name} installer"
        if [ -z "$exe_ext" ] && [ -f "$root/$name" ] && [ ! -x "$root/$name" ]; then
            fail "${name} is not executable"
        fi
    done
}

# ---------------------------------------------------------------------------
# Download release assets
# ---------------------------------------------------------------------------
info "Downloading release $TAG from $REPO"
cd "$WORKDIR"
gh release download "$TAG" --repo "$REPO" \
    --pattern '*.tar.gz' --pattern '*.zip' --pattern '*.apk' 2>/dev/null || true

echo ""
echo "  Downloaded:"
ls -lh "$WORKDIR"/ 2>/dev/null | grep -v ^total | sed 's/^/    /'

# ---------------------------------------------------------------------------
# Desktop: Linux (x86_64, aarch64), macOS (arm64, x86_64), Windows (x64)
# ---------------------------------------------------------------------------
info "============ LINUX ============"
check_installer yetty-linux          "Linux x86_64"  ""
check_installer yetty-linux-aarch64  "Linux aarch64" ""

info "============ macOS ============"
check_installer yetty-macos          "macOS arm64"   ""
check_installer yetty-macos-x86_64   "macOS x86_64"  ""

info "============ WINDOWS ============"
check_installer yetty-windows        "Windows x64"   ".exe"

# ---------------------------------------------------------------------------
# WebAssembly
# ---------------------------------------------------------------------------
if [ -f yetty-webasm.zip ]; then
    info "============ WEBASM ============"
    mkdir -p webasm && unzip -qo yetty-webasm.zip -d webasm
    # May be nested under yetty-webasm/
    wasm_root="webasm/yetty-webasm"
    [ -d "$wasm_root" ] || wasm_root="webasm"

    info "[WebAssembly] Core files"
    check_file "$wasm_root" "index.html"    "index.html"
    check_file "$wasm_root" "yetty.js"      "yetty.js loader"
    check_file "$wasm_root" "yetty.wasm"    "yetty.wasm binary"
    # No yetty.data: /demo and /src ship as lazy yfs subtrees (docs/yfs.md).
    check_file "$wasm_root" "yfs/current.json" "yfs version pointer"

    info "[WebAssembly] vfsync Alpine filesystem"
    check_dir  "$wasm_root" "vfsync"                          "vfsync directory"
    check_dir  "$wasm_root" "vfsync/u/os/yetty-alpine"        "Alpine rootfs"
    check_file "$wasm_root" "vfsync/u/os/yetty-alpine/head"   "Alpine rootfs head"
else
    info "============ WEBASM: MISSING (yetty-webasm.zip not in release) ============"
    ((ERRORS++)); ((CHECKS++))
fi

# ---------------------------------------------------------------------------
# Android
# ---------------------------------------------------------------------------
apk_file=$(ls ./*.apk 2>/dev/null | head -1 || true)
if [ -n "$apk_file" ]; then
    info "============ ANDROID ============"
    check_file "." "$apk_file" "Android APK"
else
    info "============ ANDROID: MISSING (no .apk in release) ============"
    ((ERRORS++)); ((CHECKS++))
fi

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
echo ""
echo "==========================================="
if [ "$ERRORS" -eq 0 ]; then
    echo -e "  ${GREEN}ALL $CHECKS CHECKS PASSED${NC} for $TAG"
else
    echo -e "  ${RED}$ERRORS FAILURES${NC} out of $CHECKS checks for $TAG"
fi
echo "==========================================="
exit "$ERRORS"
