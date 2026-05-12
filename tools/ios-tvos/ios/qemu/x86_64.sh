#!/bin/bash
#
# Deploy YettyQemu.app onto the iOS x86_64 Simulator.
#
# YettyQemu is the side-by-side qemu app: the qemu binary is built
# upstream by build-tools/3rdparty/qemu/_build.sh (which patches qemu's
# meson.build to link build-tools/ios-tvos/ios/qemu/ios-main.m as its
# UIKit `main`), then published to GitHub at zokrezyl/yetty under tag
# `lib-qemu-<version>` as a per-target tarball. There is NO prebuilt
# .app — this script downloads the bare binary and assembles a hand-made
# YettyQemu.app/ bundle locally, ad-hoc-signs it (sufficient for the
# simulator), and installs+launches it on the same `yetty-simulator`
# device that ios/yetty/x86_64.sh uses.
#
# Usage:
#   ./tools/ios-tvos/ios/qemu/x86_64.sh            # Download + install + launch
#   ./tools/ios-tvos/ios/qemu/x86_64.sh --rebuild  # Force re-assemble (skip cache)
#   ./tools/ios-tvos/ios/qemu/x86_64.sh --help     # Show full help
#
# Prerequisites:
#   - macOS + Xcode command-line tools (for xcrun simctl + codesign)
#   - gh on PATH (brew install gh) — for the release download
#   - nix on PATH (DeterminateSystems flake) — only if the release asset
#     is missing and we need to fall back to a local build via
#     build-tools/3rdparty/qemu/build.sh
#
# Optional env:
#   QEMU_VERSION    Override the version pinned in
#                   build-tools/3rdparty/qemu/version (rarely useful).
#   GH_REPO         Override the publisher repo (default zokrezyl/yetty).
#   SIMULATOR_NAME  iOS sim device name (default yetty-simulator, matches
#                   tools/ios-tvos/ios/yetty/x86_64.sh).
#   FORCE_LOCAL_BUILD=1  Skip the gh-download path entirely; always build
#                   the qemu tarball locally (handy when iterating on
#                   patches under build-tools/3rdparty/qemu/patches/).

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Script lives at tools/ios-tvos/ios/qemu/, so the repo root is four dirs up.
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"

# Pin the version to whatever the 3rdparty manifest says; that's the same
# source `yetty_3rdparty_fetch` uses for the desktop/android slices.
QEMU_VERSION="${QEMU_VERSION:-$(cat "$PROJECT_ROOT/build-tools/3rdparty/qemu/version")}"
GH_REPO="${GH_REPO:-zokrezyl/yetty}"
TAG="lib-qemu-${QEMU_VERSION}"
TARGET_PLATFORM="ios-x86_64"
ASSET_GLOB="qemu-${TARGET_PLATFORM}-*.tar.gz"
BUNDLE_ID="com.yetty.qemu"
APP_NAME="YettyQemu"

# Cache the assembled .app under the build-tools scaffold dir so it sits
# next to the project.yml + Info.plist that define its shape. Versioned
# so swapping QEMU_VERSION doesn't poison the cache.
CACHE_DIR="$PROJECT_ROOT/build-tools/ios-tvos/ios/qemu/.cache/${QEMU_VERSION}"
TARBALL="$CACHE_DIR/qemu-ios-x86_64.tar.gz"
APP_BUNDLE="$CACHE_DIR/${APP_NAME}.app"

# Source Info.plist with xcodebuild placeholders ($(EXECUTABLE_NAME),
# $(PRODUCT_BUNDLE_IDENTIFIER), $(PRODUCT_NAME)). Stamped to the .app at
# install time — that's how xcodebuild would resolve them too.
SRC_INFO_PLIST="$PROJECT_ROOT/build-tools/ios-tvos/ios/qemu/Info.plist"

# Match ios/yetty/x86_64.sh so both apps land on the same sim and
# YettyQemu's loopback hostfwd 2423 → guest:23 is reachable from yetty.
SIMULATOR_NAME="${SIMULATOR_NAME:-yetty-simulator}"
DEVICE_TYPE_FALLBACK="com.apple.CoreSimulator.SimDeviceType.iPhone-15"

# Colors
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; BLUE='\033[0;34m'; NC='\033[0m'
info()    { echo -e "${BLUE}[INFO]${NC} $*"; }
warn()    { echo -e "${YELLOW}[WARN]${NC} $*"; }
error()   { echo -e "${RED}[ERROR]${NC} $*"; }
success() { echo -e "${GREEN}[OK]${NC} $*"; }

#-----------------------------------------------------------------------------
# Dependencies
#-----------------------------------------------------------------------------
check_deps() {
    if [[ "$(uname -s)" != "Darwin" ]]; then
        error "macOS only (uses xcrun simctl + codesign)"; exit 1
    fi
    # `gh` is only needed for the release-download path; `nix` is only
    # needed for the local-build fallback. Check the rest unconditionally;
    # check those two on demand inside obtain_tarball() so users who pin
    # FORCE_LOCAL_BUILD=1 don't need gh, and vice versa.
    for cmd in xcrun codesign tar plutil python3; do
        if ! command -v "$cmd" >/dev/null 2>&1; then
            error "$cmd not on PATH"; exit 1
        fi
    done
    if ! xcrun simctl help >/dev/null 2>&1; then
        error "simctl missing — install Xcode from the App Store"; exit 1
    fi
}

#-----------------------------------------------------------------------------
# Obtain the qemu tarball. Two strategies, in order:
#   1. gh release download — fast path, what CI publishes per tag.
#   2. Local build via build-tools/3rdparty/qemu/build.sh — fallback when
#      the asset isn't published yet (e.g. iterating on patches), or when
#      the user pins FORCE_LOCAL_BUILD=1.
# Both end at $TARBALL. Idempotent: skips if the tarball is already cached.
#-----------------------------------------------------------------------------
download_from_github() {
    if ! command -v gh >/dev/null 2>&1; then
        warn "gh not on PATH — skipping release download (brew install gh)"
        return 1
    fi
    info "Downloading $ASSET_GLOB from ${GH_REPO}@${TAG}"
    mkdir -p "$CACHE_DIR"
    local tmp_dir; tmp_dir="$(mktemp -d)"
    # Run gh directly (no pipe — pipefail would eat the real exit code if
    # we wrapped it). gh writes its own error message to stderr on miss.
    local rc=0
    gh release download "$TAG" --repo "$GH_REPO" \
        -p "$ASSET_GLOB" -D "$tmp_dir" || rc=$?
    if [ $rc -ne 0 ]; then
        rm -rf "$tmp_dir"
        return 1
    fi
    local downloaded
    downloaded="$(ls "$tmp_dir"/qemu-${TARGET_PLATFORM}-*.tar.gz 2>/dev/null | head -1)"
    if [ -z "$downloaded" ]; then
        rm -rf "$tmp_dir"
        return 1
    fi
    mv "$downloaded" "$TARBALL"
    rm -rf "$tmp_dir"
    success "Downloaded → $TARBALL"
    return 0
}

build_locally() {
    if ! command -v nix >/dev/null 2>&1; then
        error "nix not on PATH — install DeterminateSystems nix-installer or fetch the release asset some other way."
        return 1
    fi
    info "Building qemu locally via build-tools/3rdparty/qemu/build.sh ($TARGET_PLATFORM)"
    mkdir -p "$CACHE_DIR"
    # build.sh re-execs into the right nix shell and writes the tarball
    # named qemu-<target>-<VERSION>.tar.gz into OUTPUT_DIR.
    local out_dir="$CACHE_DIR/local-build"
    mkdir -p "$out_dir"
    if ! TARGET_PLATFORM="$TARGET_PLATFORM" OUTPUT_DIR="$out_dir" \
            bash "$PROJECT_ROOT/build-tools/3rdparty/qemu/build.sh"; then
        error "local build failed — see output above"
        return 1
    fi
    local built; built="$(ls "$out_dir"/qemu-${TARGET_PLATFORM}-*.tar.gz 2>/dev/null | head -1)"
    if [ -z "$built" ]; then
        error "build.sh succeeded but no tarball found under $out_dir"
        return 1
    fi
    mv "$built" "$TARBALL"
    rm -rf "$out_dir"
    success "Built locally → $TARBALL"
    return 0
}

obtain_tarball() {
    if [ -f "$TARBALL" ]; then
        info "Tarball cached: $TARBALL"
        return
    fi
    if [ "${FORCE_LOCAL_BUILD:-0}" = "1" ]; then
        info "FORCE_LOCAL_BUILD=1 — skipping gh download"
        build_locally && return
        exit 1
    fi
    if download_from_github; then return; fi
    warn "gh release download failed for tag $TAG — falling back to local build"
    if build_locally; then return; fi
    error "Could not obtain qemu tarball for $TARGET_PLATFORM"
    error "  - Asset $ASSET_GLOB not published on $GH_REPO@$TAG (or gh missing)"
    error "  - Local build failed or nix is unavailable"
    exit 1
}

#-----------------------------------------------------------------------------
# Assemble YettyQemu.app from the binary + stamped Info.plist + ad-hoc sign.
# The simulator accepts ad-hoc signatures (codesign -s -); real-device
# deploy needs a development identity + provisioning profile, handled
# elsewhere.
#-----------------------------------------------------------------------------
assemble_app() {
    if [ -d "$APP_BUNDLE" ]; then
        info "App bundle cached: $APP_BUNDLE"
        return
    fi
    info "Assembling $APP_BUNDLE"
    rm -rf "$APP_BUNDLE"; mkdir -p "$APP_BUNDLE"

    # The tarball ships ./qemu-system-riscv64 at root. Rename it on
    # extract to match CFBundleExecutable (YettyQemu); the Mach-O itself
    # doesn't care about its own filename.
    tar -xzf "$TARBALL" -C "$APP_BUNDLE" --strip-components=0 ./qemu-system-riscv64
    mv "$APP_BUNDLE/qemu-system-riscv64" "$APP_BUNDLE/${APP_NAME}"
    chmod +x "$APP_BUNDLE/${APP_NAME}"

    # Stamp the Info.plist — substitute the xcodebuild variables with
    # their literal values. Single source of truth stays at
    # build-tools/ios-tvos/ios/qemu/Info.plist for the provisioning stub.
    sed -e 's|\$(EXECUTABLE_NAME)|'"${APP_NAME}"'|g' \
        -e 's|\$(PRODUCT_BUNDLE_IDENTIFIER)|'"${BUNDLE_ID}"'|g' \
        -e 's|\$(PRODUCT_NAME)|'"${APP_NAME}"'|g' \
        "$SRC_INFO_PLIST" > "$APP_BUNDLE/Info.plist"
    # Convert to binary plist (what simctl/launchd actually consume on
    # iOS) and validate in one shot.
    plutil -convert binary1 "$APP_BUNDLE/Info.plist"

    # Intentionally NOT codesigning. The iOS Simulator is happy with
    # completely unsigned bundles (yetty.app, built via CMake's
    # add_executable(MACOSX_BUNDLE), ships with no _CodeSignature/ at
    # all and launches cleanly). An ad-hoc `codesign -s -` here was
    # empirically making SpringBoard crash on launch
    # (FBSOpenApplicationServiceErrorDomain code=5) on the hand-assembled
    # bundle. Real-device deploy needs a Development identity and a
    # provisioning profile — handled by a separate script, not here.
    success "App ready → $APP_BUNDLE"
}

#-----------------------------------------------------------------------------
# Find or create the same iOS simulator ios/yetty/x86_64.sh uses.
#-----------------------------------------------------------------------------
find_or_create_simulator() {
    info "Looking for iOS simulator: $SIMULATOR_NAME"
    SIMULATOR_UDID="$(xcrun simctl list devices -j | python3 -c "
import sys, json
d = json.load(sys.stdin)
target = '$SIMULATOR_NAME'
for runtime, devs in d['devices'].items():
    if 'iOS' not in runtime: continue
    for dev in devs:
        if dev.get('name') == target:
            print(dev['udid']); sys.exit(0)
" 2>/dev/null)"
    if [ -n "$SIMULATOR_UDID" ]; then
        success "Found simulator: $SIMULATOR_UDID"
        return
    fi

    info "Creating new iOS simulator..."
    local runtime
    runtime="$(xcrun simctl list runtimes -j | python3 -c "
import sys, json
rts = json.load(sys.stdin)['runtimes']
ios = [r for r in rts if 'iOS' in r.get('name','') and r.get('isAvailable', False)]
print(ios[-1]['identifier'] if ios else '')
" 2>/dev/null)"
    if [ -z "$runtime" ]; then
        error "No iOS runtime found. Install in Xcode > Settings > Platforms > iOS."
        exit 1
    fi
    SIMULATOR_UDID="$(xcrun simctl create "$SIMULATOR_NAME" "$DEVICE_TYPE_FALLBACK" "$runtime")"
    success "Created simulator: $SIMULATOR_UDID"
}

#-----------------------------------------------------------------------------
# Boot the sim if not already booted. The Simulator.app GUI is optional —
# YettyQemu's UI is mostly background-audio + log file anyway.
#-----------------------------------------------------------------------------
boot_simulator() {
    local state
    state="$(xcrun simctl list devices -j | python3 -c "
import sys, json
d = json.load(sys.stdin)
for runtime, devs in d['devices'].items():
    for dev in devs:
        if dev.get('udid') == '$SIMULATOR_UDID':
            print(dev.get('state', 'Unknown')); sys.exit(0)
" 2>/dev/null)"
    if [ "$state" = "Booted" ]; then
        info "Simulator already booted"; return
    fi
    info "Booting simulator (headless — Simulator.app GUI not auto-opened)..."
    xcrun simctl boot "$SIMULATOR_UDID" 2>/dev/null || true
    # Intentionally NOT calling `open -a Simulator` here: if SpringBoard
    # later crashes during launch, the visible UI freezes and looks like
    # the whole sim is dead. Headless boot is enough for simctl install
    # and for any app whose UI we don't need on screen. Open the GUI
    # manually when ready:  open -a Simulator --args -CurrentDeviceUDID "$SIMULATOR_UDID"
    local count=0
    while [ $count -lt 60 ]; do
        state="$(xcrun simctl list devices -j | python3 -c "
import sys, json
d = json.load(sys.stdin)
for runtime, devs in d['devices'].items():
    for dev in devs:
        if dev.get('udid') == '$SIMULATOR_UDID':
            print(dev.get('state', 'Unknown')); sys.exit(0)
" 2>/dev/null)"
        [ "$state" = "Booted" ] && { success "Booted"; return; }
        sleep 1; count=$((count + 1))
    done
    error "Simulator boot timeout"; exit 1
}

#-----------------------------------------------------------------------------
# Seed YettyQemu's app sandbox with the riscv64 firmware/kernel/rootfs that
# ios-main.m looks for at $HOME/Library/Caches/yetty/yemu/. Same files
# yetty.app's 3rdparty fetch already downloaded into the iOS build dir;
# YettyQemu has its own sandbox (different bundle ID = different container)
# so they have to be copied in explicitly. Idempotent: skips files already
# present and same size.
#-----------------------------------------------------------------------------
seed_assets() {
    local data_container
    data_container="$(xcrun simctl get_app_container "$SIMULATOR_UDID" "$BUNDLE_ID" data 2>/dev/null)"
    if [ -z "$data_container" ] || [ ! -d "$data_container" ]; then
        warn "YettyQemu data container not found yet — launch once, then re-run to seed assets"
        return 0
    fi

    local dst="$data_container/Library/Caches/yetty/yemu"
    mkdir -p "$dst"

    # Prefer the iOS build (matches the target). Fall back to desktop +
    # tvos builds in that order; the asset files are platform-agnostic
    # blobs (riscv64 firmware/kernel/disk), so any successful yetty build
    # has them.
    local src_root=""
    for candidate in \
            "$PROJECT_ROOT/build-ios_x86_64-ytrace-release" \
            "$PROJECT_ROOT/build-ios_x86_64-ytrace-debug" \
            "$PROJECT_ROOT/build-desktop-ytrace-release" \
            "$PROJECT_ROOT/build-desktop-ytrace-debug" \
            "$PROJECT_ROOT/build-tvos_x86_64-ytrace-release"; do
        if [ -f "$candidate/3rdparty/opensbi/opensbi-fw_dynamic.bin" ]; then
            src_root="$candidate"; break
        fi
    done
    if [ -z "$src_root" ]; then
        warn "No yetty build dir with 3rdparty/opensbi found — run 'make build-ios_x86_64-ytrace-release' first if you want the riscv64 guest to boot."
        return 0
    fi

    # alpine-extended-disk has runit + a telnetd service under
    # /etc/service/telnetd — required for yetty's --telnet 127.0.0.1:2423
    # path. The plain alpine-disk image just drops to /bin/sh on
    # virtio-console and has no listener on guest:23, so slirp's hostfwd
    # NAT closes the connection immediately. Renamed on copy because
    # ios-main.m hardcodes the destination filename as alpine-rootfs.img.
    local pairs=(
        "3rdparty/opensbi/opensbi-fw_dynamic.bin:opensbi-fw_dynamic.bin"
        "3rdparty/linux/kernel-riscv64.bin:kernel-riscv64.bin"
        "3rdparty/alpine-extended-disk/alpine-extended-rootfs.img:alpine-rootfs.img"
    )
    for pair in "${pairs[@]}"; do
        local rel="${pair%%:*}" name="${pair##*:}"
        local src="$src_root/$rel"
        local dst_file="$dst/$name"
        if [ ! -f "$src" ]; then
            warn "  missing $rel under $src_root"
            continue
        fi
        if [ -f "$dst_file" ] && [ "$(stat -f%z "$dst_file")" = "$(stat -f%z "$src")" ]; then
            info "  already seeded: $name"
            continue
        fi
        cp "$src" "$dst_file"
        info "  seeded: $name ($(stat -f%z "$dst_file") bytes)"
    done
    success "Assets seeded → $dst"
}

install_and_launch() {
    info "Installing $APP_BUNDLE"
    xcrun simctl install "$SIMULATOR_UDID" "$APP_BUNDLE"
    seed_assets
    success "YettyQemu installed on $SIMULATOR_NAME ($SIMULATOR_UDID)"
    cat <<EOF

Install OK. The launch step is intentionally NOT done here:
previously a bad bundle + simctl launch crashed SpringBoard and froze
the sim UI. Trigger it manually once you're ready:

  xcrun simctl launch --console-pty $SIMULATOR_UDID $BUNDLE_ID

That gives you live stdout. If SpringBoard crashes, kill the sim with:

  xcrun simctl shutdown $SIMULATOR_UDID

Logs inside the sim sandbox (after a successful launch):
  - \$HOME/tmp/yq-status.txt  — main-thread tracer
  - \$HOME/tmp/yq-output.log  — qemu stdout/stderr
Pull back with:
  xcrun simctl get_app_container $SIMULATOR_UDID $BUNDLE_ID data
EOF
}

usage() {
    cat <<EOF
Usage: $0 [--rebuild|--help]

  (no args)   Download (if needed), assemble, install, launch YettyQemu
              on iOS Simulator $SIMULATOR_NAME.
  --rebuild   Wipe the cached .app and re-assemble from the tarball.
              Use after editing build-tools/ios-tvos/ios/qemu/Info.plist
              or after pulling a new QEMU_VERSION manifest.
  --help      This message.

Cache: $CACHE_DIR
Target tag: $TAG on $GH_REPO
EOF
}

main() {
    case "${1:-}" in
        --help|-h) usage; exit 0 ;;
        --rebuild) rm -rf "$APP_BUNDLE"; shift ;;
        "") ;;
        *) error "unknown arg: $1"; usage; exit 2 ;;
    esac

    check_deps
    obtain_tarball
    assemble_app
    find_or_create_simulator
    boot_simulator
    install_and_launch
}

main "$@"
