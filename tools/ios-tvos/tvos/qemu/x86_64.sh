#!/bin/bash
#
# Deploy YettyQemu.app onto the tvOS x86_64 Simulator.
#
# Mirror of ios/qemu/x86_64.sh; the qemu binary is built upstream by
# build-tools/3rdparty/qemu/_build.sh (which patches qemu's meson.build
# to link build-tools/ios-tvos/tvos/qemu/tvos.m as its UIKit `main`)
# and published to GitHub at zokrezyl/yetty under tag `lib-qemu-<version>`.
# No prebuilt .app — we download the bare binary, assemble a hand-made
# YettyQemu.app/ locally, ad-hoc-sign, and install+launch on the same
# Apple TV simulator that tvos/yetty/x86_64.sh uses.
#
# Usage:
#   ./tools/ios-tvos/tvos/qemu/x86_64.sh            # Download + install + launch
#   ./tools/ios-tvos/tvos/qemu/x86_64.sh --rebuild  # Force re-assemble (skip cache)
#   ./tools/ios-tvos/tvos/qemu/x86_64.sh --help     # Show full help

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Script lives at tools/ios-tvos/tvos/qemu/, so the repo root is four dirs up.
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"

QEMU_VERSION="${QEMU_VERSION:-$(cat "$PROJECT_ROOT/build-tools/3rdparty/qemu/version")}"
GH_REPO="${GH_REPO:-zokrezyl/yetty}"
TAG="lib-qemu-${QEMU_VERSION}"
ASSET_GLOB="qemu-tvos-x86_64-*.tar.gz"
BUNDLE_ID="com.yetty.qemu"
APP_NAME="YettyQemu"

CACHE_DIR="$PROJECT_ROOT/build-tools/ios-tvos/tvos/qemu/.cache/${QEMU_VERSION}"
TARBALL="$CACHE_DIR/qemu-tvos-x86_64.tar.gz"
APP_BUNDLE="$CACHE_DIR/${APP_NAME}.app"

SRC_INFO_PLIST="$PROJECT_ROOT/build-tools/ios-tvos/tvos/qemu/Info.plist"

# Match tvos/yetty/x86_64.sh so YettyQemu lands on the same Apple TV sim
# and its slirp hostfwd 2423 → guest:23 is reachable from yetty.app.
DEVICE_TYPE_NAME="${DEVICE_TYPE_NAME:-Apple TV 4K (3rd generation) (at 1080p)}"
SIMULATOR_NAME="${SIMULATOR_NAME:-$DEVICE_TYPE_NAME}"
DEVICE_TYPE_FALLBACK="com.apple.CoreSimulator.SimDeviceType.Apple-TV-4K-3rd-generation-1080p"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; BLUE='\033[0;34m'; NC='\033[0m'
info()    { echo -e "${BLUE}[INFO]${NC} $*"; }
warn()    { echo -e "${YELLOW}[WARN]${NC} $*"; }
error()   { echo -e "${RED}[ERROR]${NC} $*"; }
success() { echo -e "${GREEN}[OK]${NC} $*"; }

check_deps() {
    if [[ "$(uname -s)" != "Darwin" ]]; then
        error "macOS only (uses xcrun simctl + codesign)"; exit 1
    fi
    for cmd in xcrun gh codesign tar plutil python3; do
        if ! command -v "$cmd" >/dev/null 2>&1; then
            error "$cmd not on PATH"; exit 1
        fi
    done
    if ! xcrun simctl help >/dev/null 2>&1; then
        error "simctl missing — install Xcode from the App Store"; exit 1
    fi
}

download_qemu() {
    if [ -f "$TARBALL" ]; then
        info "Tarball cached: $TARBALL"
        return
    fi
    info "Downloading $ASSET_GLOB from ${GH_REPO}@${TAG}"
    mkdir -p "$CACHE_DIR"
    local tmp_dir; tmp_dir="$(mktemp -d)"
    if ! gh release download "$TAG" --repo "$GH_REPO" \
            -p "$ASSET_GLOB" -D "$tmp_dir" >/dev/null; then
        error "gh release download failed — is the tag $TAG published on $GH_REPO?"
        rm -rf "$tmp_dir"; exit 1
    fi
    local downloaded; downloaded="$(ls "$tmp_dir"/qemu-tvos-x86_64-*.tar.gz | head -1)"
    if [ -z "$downloaded" ]; then
        error "no asset matched $ASSET_GLOB in $TAG"
        rm -rf "$tmp_dir"; exit 1
    fi
    mv "$downloaded" "$TARBALL"
    rm -rf "$tmp_dir"
    success "Cached → $TARBALL"
}

assemble_app() {
    if [ -d "$APP_BUNDLE" ]; then
        info "App bundle cached: $APP_BUNDLE"
        return
    fi
    info "Assembling $APP_BUNDLE"
    rm -rf "$APP_BUNDLE"; mkdir -p "$APP_BUNDLE"

    tar -xzf "$TARBALL" -C "$APP_BUNDLE" --strip-components=0 ./qemu-system-riscv64
    mv "$APP_BUNDLE/qemu-system-riscv64" "$APP_BUNDLE/${APP_NAME}"
    chmod +x "$APP_BUNDLE/${APP_NAME}"

    sed -e 's|\$(EXECUTABLE_NAME)|'"${APP_NAME}"'|g' \
        -e 's|\$(PRODUCT_BUNDLE_IDENTIFIER)|'"${BUNDLE_ID}"'|g' \
        -e 's|\$(PRODUCT_NAME)|'"${APP_NAME}"'|g' \
        "$SRC_INFO_PLIST" > "$APP_BUNDLE/Info.plist"
    plutil -convert binary1 "$APP_BUNDLE/Info.plist"

    codesign --force --sign - --timestamp=none "$APP_BUNDLE" 2>&1 \
        | grep -v 'replacing existing signature' || true
    success "App ready → $APP_BUNDLE"
}

find_or_create_simulator() {
    info "Looking for tvOS simulator: $SIMULATOR_NAME"
    SIMULATOR_UDID="$(xcrun simctl list devices -j | python3 -c "
import sys, json
d = json.load(sys.stdin)
target = '$SIMULATOR_NAME'
for runtime, devs in d['devices'].items():
    if 'tvOS' not in runtime: continue
    for dev in devs:
        if dev.get('name') == target:
            print(dev['udid']); sys.exit(0)
" 2>/dev/null)"
    if [ -n "$SIMULATOR_UDID" ]; then
        success "Found simulator: $SIMULATOR_UDID"
        return
    fi

    info "Creating new tvOS simulator..."
    local runtime
    runtime="$(xcrun simctl list runtimes -j | python3 -c "
import sys, json
rts = json.load(sys.stdin)['runtimes']
tv = [r for r in rts if 'tvOS' in r.get('name','') and r.get('isAvailable', False)]
print(tv[-1]['identifier'] if tv else '')
" 2>/dev/null)"
    if [ -z "$runtime" ]; then
        error "No tvOS runtime found. Install in Xcode > Settings > Platforms > tvOS."
        exit 1
    fi

    local device_type
    device_type="$(xcrun simctl list devicetypes -j | python3 -c "
import sys, json
target = '$DEVICE_TYPE_NAME'
for d in json.load(sys.stdin)['devicetypes']:
    if d.get('name') == target:
        print(d['identifier']); sys.exit(0)
" 2>/dev/null)"
    [ -z "$device_type" ] && device_type="$DEVICE_TYPE_FALLBACK"

    SIMULATOR_UDID="$(xcrun simctl create "$SIMULATOR_NAME" "$device_type" "$runtime")"
    success "Created simulator: $SIMULATOR_UDID"
}

# `simctl list devices` reports state=Booted as soon as the runtime
# process is up, long before SpringBoard + system services are actually
# ready to host apps. `simctl bootstatus -b $UDID` boots if needed
# (no-op when already booted) AND blocks until the device is fully usable.
boot_simulator() {
    info "Booting simulator (waiting for full boot via simctl bootstatus)..."
    open -a Simulator --args -CurrentDeviceUDID "$SIMULATOR_UDID" || true
    if ! xcrun simctl bootstatus "$SIMULATOR_UDID" -b; then
        error "Simulator boot timeout / failure"; exit 1
    fi
    success "Booted (system services ready)"
}

install_and_launch() {
    info "Installing $APP_BUNDLE"
    xcrun simctl install "$SIMULATOR_UDID" "$APP_BUNDLE"
    info "Launching $BUNDLE_ID"
    xcrun simctl launch "$SIMULATOR_UDID" "$BUNDLE_ID"
    success "YettyQemu launched on $SIMULATOR_NAME ($SIMULATOR_UDID)"
    cat <<EOF

Next steps:
  - YettyQemu is now running on the sim, listening on 127.0.0.1:2423 via
    slirp hostfwd (host:2423 → guest:23 inside the qemu VM).
  - Run yetty against the same sim:
      ./tools/ios-tvos/tvos/yetty/x86_64.sh

Logs (inside the sim's app container):
  - \$HOME/tmp/yq-status.txt  — main-thread tracer
  - \$HOME/tmp/yq-output.log  — qemu stdout/stderr
  Pull them back via simctl:
    xcrun simctl get_app_container booted $BUNDLE_ID data
EOF
}

usage() {
    cat <<EOF
Usage: $0 [--rebuild|--help]

  (no args)   Download (if needed), assemble, install, launch YettyQemu
              on tvOS Simulator "$SIMULATOR_NAME".
  --rebuild   Wipe the cached .app and re-assemble from the tarball.
              Use after editing build-tools/ios-tvos/tvos/qemu/Info.plist
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
    download_qemu
    assemble_app
    find_or_create_simulator
    boot_simulator
    install_and_launch
}

main "$@"
