#!/usr/bin/env bash
#
# yetty installer bootstrap for Linux and macOS.
#
# Downloads the self-contained installer for this machine's OS and
# architecture from the latest GitHub release, then runs it. The installer
# carries its payload embedded and unpacks each piece to the right per-OS
# location on first run. It comes in three sizes (see
# src/yetty/yinstall/README.md, "Variants"):
#   min      yetty alone with shaders, raw fonts and config; the MSDF font
#            atlases are built from the raw fonts on the first start
#   default  every yetty executable and tool, fonts + pre-generated atlases,
#            config, greeter and demo assets
#   max      default plus the RISC-V VM runtime and QEMU
#
# Usage:
#   curl -fsSL https://yetty.dev/install.sh | bash
#   curl -fsSL https://yetty.dev/install-min.sh | bash
#   curl -fsSL https://yetty.dev/install-max.sh | bash
#   curl -fsSL https://yetty.dev/install.sh | bash -s -- --variant max --verbose
#
# install-min.sh / install-max.sh are this script with `variant_default`
# pinned (build-tools/install/make-variants.sh derives them).
#
# Options (before any arguments meant for the installer):
#   --variant NAME  min | default | max
#   --min / --max   shorthand for --variant
#
# Environment overrides:
#   YETTY_VARIANT   min | default | max. Default: the script's pinned variant.
#   YETTY_VERSION   release tag to install (e.g. yetty-0.2.46). Default: latest.
#   YETTY_REPO      owner/repo to download from. Default: zokrezyl/yetty.
#
# Any other arguments are forwarded verbatim to the installer (e.g. --verbose,
# --force). See `yinstall --help` for the full list.

set -euo pipefail

# The variant this script installs unless YETTY_VARIANT or --variant says
# otherwise. make-variants.sh rewrites this one line for install-min.sh and
# install-max.sh — keep it on its own line, exactly this shape.
variant_default="default"

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

# Validate a variant name.
check_variant() {
    case "$1" in
        min | default | max) ;;
        *) die "unknown variant '$1' (have min, default, max)" ;;
    esac
}

# The release asset name for this OS + architecture + variant. The desktop
# release ships one archive per (os, arch, variant); each contains a single
# installer binary. The default variant is the plain archive name, min and
# max carry a suffix.
resolve_asset() {
    local variant="$1" kernel machine base
    kernel="$(uname -s)"
    machine="$(uname -m)"

    case "$kernel" in
        Linux)
            case "$machine" in
                x86_64 | amd64) base="yetty-linux" ;;
                aarch64 | arm64) base="yetty-linux-aarch64" ;;
                *) die "unsupported Linux architecture '$machine' (have x86_64, aarch64)" ;;
            esac
            ;;
        Darwin)
            case "$machine" in
                arm64) base="yetty-macos" ;;
                x86_64) base="yetty-macos-x86_64" ;;
                *) die "unsupported macOS architecture '$machine' (have arm64, x86_64)" ;;
            esac
            ;;
        *)
            die "unsupported operating system '$kernel' — this script covers Linux and macOS. On Windows use install.ps1."
            ;;
    esac

    if [ "$variant" = "default" ]; then
        echo "${base}.tar.gz"
    else
        echo "${base}-${variant}.tar.gz"
    fi
}

# The installer binary inside the archive: yinstall, yinstall-min, yinstall-max.
resolve_installer_name() {
    local variant="$1"
    if [ "$variant" = "default" ]; then
        echo "yinstall"
    else
        echo "yinstall-${variant}"
    fi
}

# Build the download URL. A resolved tag uses the direct release path; the
# literal "latest" falls back to GitHub's version-independent redirect.
resolve_url() {
    local asset="$1" release_tag="$2"
    if [ "$release_tag" = "latest" ]; then
        echo "https://github.com/${repo}/releases/latest/download/${asset}"
    else
        echo "https://github.com/${repo}/releases/download/${release_tag}/${asset}"
    fi
}

# Fetch a URL to stdout quietly. For small API metadata, not release payloads.
fetch_text() {
    local url="$1"
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL --max-time 30 "$url"
    elif command -v wget >/dev/null 2>&1; then
        wget -qO- --timeout=30 "$url"
    else
        return 1
    fi
}

# Resolve the newest desktop release tag (yetty-X.Y.Z). The repo publishes
# several release families (yetty-*, yos-web-*, yetty-rootfs-riscv-*) and
# GitHub's repo-wide "latest release" pointer belongs to whichever release
# published most recently — not necessarily a desktop one. So "latest" is
# resolved by listing releases and picking the highest yetty-X.Y.Z version.
# Fails (prints nothing) when the API is unreachable or rate-limited; the
# caller then falls back to the repo-wide redirect.
resolve_latest_tag() {
    local releases_json best_version
    releases_json="$(fetch_text "https://api.github.com/repos/${repo}/releases?per_page=100")" || return 1
    best_version="$(printf '%s\n' "$releases_json" \
        | grep -o '"tag_name"[[:space:]]*:[[:space:]]*"yetty-[0-9][0-9.]*"' \
        | sed 's/.*"yetty-\([0-9][0-9.]*\)".*/\1/' \
        | sort -t . -k 1,1n -k 2,2n -k 3,3n \
        | tail -n 1 || true)"
    [ -n "$best_version" ] || return 1
    echo "yetty-${best_version}"
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

    # Variant: the script's pinned default, overridden by YETTY_VARIANT, then
    # by a leading --variant / --min / --max. Anything else goes to yinstall.
    local variant="${YETTY_VARIANT:-$variant_default}"
    while [ $# -gt 0 ]; do
        case "$1" in
            --variant)
                [ $# -ge 2 ] || die "--variant needs a value (min, default, max)"
                variant="$2"
                shift 2
                ;;
            --variant=*)
                variant="${1#--variant=}"
                shift
                ;;
            --min) variant="min"; shift ;;
            --max) variant="max"; shift ;;
            --default) variant="default"; shift ;;
            *) break ;;
        esac
    done
    check_variant "$variant"

    local asset url archive installer installer_name release_tag
    asset="$(resolve_asset "$variant")"
    installer_name="$(resolve_installer_name "$variant")"

    release_tag="$version"
    if [ "$release_tag" = "latest" ]; then
        if release_tag="$(resolve_latest_tag)"; then
            log "latest desktop release is ${release_tag}"
        else
            release_tag="latest"
            log "cannot list releases via the GitHub API; falling back to the repo-wide latest-release redirect"
        fi
    fi

    url="$(resolve_url "$asset" "$release_tag")"

    workdir="$(mktemp -d "${TMPDIR:-/tmp}/yetty-install.XXXXXX")"
    archive="${workdir}/${asset}"

    log "downloading ${asset} (${release_tag}, ${variant} variant) from ${repo}"
    download "$url" "$archive"

    log "unpacking installer"
    tar -xzf "$archive" -C "$workdir"

    installer="${workdir}/${installer_name}"
    [ -f "$installer" ] || die "installer '${installer_name}' not found inside ${asset}"
    chmod +x "$installer"

    log "running ${installer_name}"
    # Forward any pass-through args (e.g. --verbose, --force). yinstall prints
    # its own log, including where each component landed and any PATH advice.
    "$installer" "$@"
}

main "$@"
