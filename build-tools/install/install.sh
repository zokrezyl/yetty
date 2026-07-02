#!/usr/bin/env bash
#
# yetty installer bootstrap for Linux and macOS.
#
# Downloads the self-contained `yinstall` installer for this machine's OS and
# architecture from the latest GitHub release, then runs it. `yinstall` carries
# the whole yetty product (the terminal, the companion tools, shaders, fonts,
# config and the RISC-V VM runtime) as an embedded payload and unpacks each
# piece to the right per-OS location on first run.
#
# Usage:
#   curl -fsSL https://yetty.dev/install.sh | bash
#   curl -fsSL https://yetty.dev/install.sh | bash -s -- --verbose
#
# Environment overrides:
#   YETTY_VERSION   release tag to install (e.g. yetty-0.2.46). Default: latest.
#   YETTY_REPO      owner/repo to download from. Default: zokrezyl/yetty.
#
# Any arguments after `--` are forwarded verbatim to yinstall (e.g. --verbose,
# --force). See `yinstall --help` for the full list.

set -euo pipefail

readonly repo="${YETTY_REPO:-zokrezyl/yetty}"
readonly version="${YETTY_VERSION:-latest}"

log() { printf 'yetty-install: %s\n' "$1" >&2; }
die() { printf 'yetty-install: error: %s\n' "$1" >&2; exit 1; }

# Scratch dir, cleaned up on exit. Declared at script scope (not inside main)
# so the EXIT trap — which fires after main returns — can still see it under
# `set -u`. Guarded with :- so an early exit before assignment is also safe.
workdir=""
cleanup() {
    if [ -n "${workdir:-}" ] && [ -d "$workdir" ]; then
        rm -rf "$workdir" 2>/dev/null || true
    fi
}
trap cleanup EXIT

# Resolve the release asset name for this OS + architecture. The desktop
# release ships one archive per (os, arch); each contains a single `yinstall`.
resolve_asset() {
    local kernel machine
    kernel="$(uname -s)"
    machine="$(uname -m)"

    case "$kernel" in
        Linux)
            case "$machine" in
                x86_64 | amd64) echo "yetty-linux.tar.gz" ;;
                aarch64 | arm64) echo "yetty-linux-aarch64.tar.gz" ;;
                *) die "unsupported Linux architecture '$machine' (have x86_64, aarch64)" ;;
            esac
            ;;
        Darwin)
            case "$machine" in
                arm64) echo "yetty-macos.tar.gz" ;;
                x86_64) echo "yetty-macos-x86_64.tar.gz" ;;
                *) die "unsupported macOS architecture '$machine' (have arm64, x86_64)" ;;
            esac
            ;;
        *)
            die "unsupported operating system '$kernel' — this script covers Linux and macOS. On Windows use install.ps1."
            ;;
    esac
}

# Build the download URL. "latest" uses the version-independent redirect so the
# script never needs updating per release; a pinned tag uses the direct path.
resolve_url() {
    local asset="$1"
    if [ "$version" = "latest" ]; then
        echo "https://github.com/${repo}/releases/latest/download/${asset}"
    else
        echo "https://github.com/${repo}/releases/download/${version}/${asset}"
    fi
}

# Pick a downloader that streams to a file and fails loudly on HTTP errors.
download() {
    local url="$1" out="$2"
    if command -v curl >/dev/null 2>&1; then
        curl -fL --progress-bar -o "$out" "$url"
    elif command -v wget >/dev/null 2>&1; then
        wget -q --show-progress -O "$out" "$url"
    else
        die "need curl or wget on PATH to download the installer"
    fi
}

main() {
    command -v tar >/dev/null 2>&1 || die "need tar on PATH to unpack the installer"

    local asset url archive installer
    asset="$(resolve_asset)"
    url="$(resolve_url "$asset")"

    workdir="$(mktemp -d "${TMPDIR:-/tmp}/yetty-install.XXXXXX")"
    archive="${workdir}/${asset}"

    log "downloading ${asset} (${version}) from ${repo}"
    download "$url" "$archive"

    log "unpacking installer"
    tar -xzf "$archive" -C "$workdir"

    installer="${workdir}/yinstall"
    [ -f "$installer" ] || die "installer 'yinstall' not found inside ${asset}"
    chmod +x "$installer"

    log "running yinstall"
    # Forward any pass-through args (e.g. --verbose, --force). yinstall prints
    # its own log, including where each component landed and any PATH advice.
    "$installer" "$@"
}

main "$@"
