#!/usr/bin/env bash
# Verify release artifacts for a given tag
# Usage: ./verify-release.sh <tag>
#   e.g. ./verify-release.sh v0.1.50
#
# Downloads all platform archives from the GitHub release, extracts them,
# and checks that each contains the expected executables, CDB fonts,
# shaders, and demo files.

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
# Checks for desktop platforms: executables and demo
# Note: fonts and shaders are embedded via incbin, not included separately
# ---------------------------------------------------------------------------
check_desktop() {
    local dir="$1" platform="$2" exe_dir="$3" exe_ext="${4:-}"

    info "[$platform] Executables"
    check_file "$dir" "${exe_dir}/yetty${exe_ext}"  "yetty executable"
    check_file "$dir" "${exe_dir}/ycat${exe_ext}"   "ycat executable"
    check_file "$dir" "${exe_dir}/yecho${exe_ext}"  "yecho executable"

    # Note: TTF fonts and shaders are embedded via incbin into the executable
    # They are NOT included as separate files in desktop releases (see issue #171)

    info "[$platform] Demo files"
    check_dir  "$dir" "demo"                                "Demo directory"
    check_dir  "$dir" "demo/scripts"                        "Demo scripts"
    check_dir  "$dir" "demo/files"                          "Demo data files"
    check_dir  "$dir" "demo/assets"                         "Demo assets"
    check_file "$dir" "demo/files/logo.encoded.yetty"       "Demo logo"
    check_file "$dir" "demo/files/shader.encoded.yetty"     "Demo shader"
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
# Linux
# ---------------------------------------------------------------------------
if [ -f yetty-linux.tar.gz ]; then
    info "============ LINUX ============"
    mkdir -p linux && tar -xzf yetty-linux.tar.gz -C linux
    check_desktop linux "Linux" "build-desktop" ""
else
    info "============ LINUX: MISSING (yetty-linux.tar.gz not in release) ============"
    ((ERRORS++)); ((CHECKS++))
fi

# ---------------------------------------------------------------------------
# macOS
# ---------------------------------------------------------------------------
if [ -f yetty-macos.tar.gz ]; then
    info "============ macOS ============"
    mkdir -p macos && tar -xzf yetty-macos.tar.gz -C macos
    check_desktop macos "macOS" "build-macos" ""
else
    info "============ macOS: MISSING (yetty-macos.tar.gz not in release) ============"
    ((ERRORS++)); ((CHECKS++))
fi

# ---------------------------------------------------------------------------
# Windows
# ---------------------------------------------------------------------------
if [ -f yetty-windows.zip ]; then
    info "============ WINDOWS ============"
    mkdir -p windows && unzip -qo yetty-windows.zip -d windows
    # Windows zip nests under yetty-windows/ and ships exactly one artifact:
    # yinstall.exe, the self-contained installer. It carries yetty, the
    # companion tools, the DirectX DLLs, the demo tree and the VM runtime as
    # an embedded payload, so there are no loose executables or demo/ dir to
    # check here — just the installer itself.
    win_root="windows/yetty-windows"
    [ -d "$win_root" ] || win_root="windows"
    info "[Windows] Installer"
    check_file "$win_root" "yinstall.exe" "yinstall installer"
else
    info "============ WINDOWS: MISSING (yetty-windows.zip not in release) ============"
    ((ERRORS++)); ((CHECKS++))
fi

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
    check_file "$wasm_root" "yetty.data"    "yetty.data (preloaded assets)"

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
