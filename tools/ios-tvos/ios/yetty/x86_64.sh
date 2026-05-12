#!/bin/bash
#
# Run Yetty iOS x86_64 build in the iOS Simulator
# Supports: macOS only
#
# Usage:
#   First build: make build-ios_x86_64-ytrace-release (or -debug)
#   Then run:
#     ./tools/ios-tvos/ios/yetty/x86_64.sh              # Start simulator with yetty
#     ./tools/ios-tvos/ios/yetty/x86_64.sh --kill       # Kill running simulator
#     ./tools/ios-tvos/ios/yetty/x86_64.sh --list       # List available simulators
#     ./tools/ios-tvos/ios/yetty/x86_64.sh --help       # Show full help

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Script lives at tools/ios-tvos/ios/yetty/, so the repo root is four dirs up.
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"

# Configuration
SIMULATOR_NAME="yetty-simulator"
DEVICE_TYPE="iPhone 15 Pro"
RUNTIME="iOS-17-2"
# Try release build first, then debug
if [ -d "$PROJECT_ROOT/build-ios_x86_64-ytrace-release/yetty.app" ]; then
    APP_BUNDLE="${APP_BUNDLE:-$PROJECT_ROOT/build-ios_x86_64-ytrace-release/yetty.app}"
else
    APP_BUNDLE="${APP_BUNDLE:-$PROJECT_ROOT/build-ios_x86_64-ytrace-debug/yetty.app}"
fi
BUNDLE_ID="com.yetty.terminal"

# Hardware-keyboard layout the iOS Simulator should advertise to yetty
# (yetty's pressesBegan: receives UIKey.characters which is the post-layout
# value). Default Dvorak; export KEYBOARD_LAYOUT=QWERTY to override, or
# KEYBOARD_LAYOUT=skip to leave the sim's existing setting alone.
KEYBOARD_LAYOUT="${KEYBOARD_LAYOUT:-Dvorak}"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

info() { echo -e "${BLUE}[INFO]${NC} $*"; }
warn() { echo -e "${YELLOW}[WARN]${NC} $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; }
success() { echo -e "${GREEN}[OK]${NC} $*"; }

#-----------------------------------------------------------------------------
# Check macOS
#-----------------------------------------------------------------------------
check_macos() {
    if [[ "$(uname -s)" != "Darwin" ]]; then
        error "This script only works on macOS"
        exit 1
    fi
    info "Running on macOS"
}

#-----------------------------------------------------------------------------
# Check Xcode tools
#-----------------------------------------------------------------------------
check_xcode() {
    if ! command -v xcrun &> /dev/null; then
        error "Xcode command line tools not found"
        echo "Install with: xcode-select --install"
        exit 1
    fi

    if ! command -v simctl &> /dev/null && ! xcrun simctl help &> /dev/null; then
        error "simctl not found. Install Xcode from the App Store."
        exit 1
    fi

    success "Xcode tools available"
}

#-----------------------------------------------------------------------------
# Find or create simulator
#-----------------------------------------------------------------------------
find_or_create_simulator() {
    info "Looking for simulator: $SIMULATOR_NAME"

    # Check if our simulator exists
    local sim_udid=$(xcrun simctl list devices -j | \
        python3 -c "import sys,json; d=json.load(sys.stdin); print(next((dev['udid'] for devs in d['devices'].values() for dev in devs if dev['name']=='$SIMULATOR_NAME'), ''))" 2>/dev/null)

    if [ -n "$sim_udid" ]; then
        SIMULATOR_UDID="$sim_udid"
        success "Found simulator: $SIMULATOR_UDID"
        return 0
    fi

    # Find available runtime
    info "Creating new simulator..."
    local runtime=$(xcrun simctl list runtimes -j | \
        python3 -c "import sys,json; rts=json.load(sys.stdin)['runtimes']; ios=[r for r in rts if 'iOS' in r.get('name','') and r.get('isAvailable',False)]; print(ios[-1]['identifier'] if ios else '')" 2>/dev/null)

    if [ -z "$runtime" ]; then
        error "No iOS runtime found. Install iOS Simulator runtime in Xcode."
        echo "Xcode -> Settings -> Platforms -> iOS -> Download"
        exit 1
    fi

    info "Using runtime: $runtime"

    # Find device type - prefer iPhone 15 or newer
    local device_type=$(xcrun simctl list devicetypes -j | \
        python3 -c "import sys,json; dts=json.load(sys.stdin)['devicetypes']; iphones=[d for d in dts if 'iPhone 15' in d.get('name','') or 'iPhone 16' in d.get('name','')]; print(iphones[0]['identifier'] if iphones else '')" 2>/dev/null)

    if [ -z "$device_type" ]; then
        device_type="com.apple.CoreSimulator.SimDeviceType.iPhone-15"
    fi

    info "Using device type: $device_type"

    # Create simulator
    SIMULATOR_UDID=$(xcrun simctl create "$SIMULATOR_NAME" "$device_type" "$runtime")
    success "Created simulator: $SIMULATOR_UDID"
}

#-----------------------------------------------------------------------------
# Boot simulator
#-----------------------------------------------------------------------------
boot_simulator() {
    local state=$(xcrun simctl list devices -j | \
        python3 -c "import sys,json; d=json.load(sys.stdin); print(next((dev['state'] for devs in d['devices'].values() for dev in devs if dev['udid']=='$SIMULATOR_UDID'), 'Unknown'))" 2>/dev/null)

    if [ "$state" = "Booted" ]; then
        success "Simulator already booted"
        return 0
    fi

    info "Booting simulator..."
    xcrun simctl boot "$SIMULATOR_UDID" 2>/dev/null || true

    # Open Simulator.app
    open -a Simulator --args -CurrentDeviceUDID "$SIMULATOR_UDID"

    # Wait for boot
    info "Waiting for simulator to boot..."
    local timeout=60
    local count=0
    while [ $count -lt $timeout ]; do
        state=$(xcrun simctl list devices -j | \
            python3 -c "import sys,json; d=json.load(sys.stdin); print(next((dev['state'] for devs in d['devices'].values() for dev in devs if dev['udid']=='$SIMULATOR_UDID'), 'Unknown'))" 2>/dev/null)
        if [ "$state" = "Booted" ]; then
            success "Simulator booted"
            return 0
        fi
        sleep 1
        count=$((count + 1))
        echo -n "."
    done
    echo ""

    error "Simulator boot timeout"
    exit 1
}

#-----------------------------------------------------------------------------
# Set Simulator hardware-keyboard layout via the shared helper. See
# tools/sim-keyboard.sh for why and how — yetty's pressesBegan: receives
# UIKey.characters which is post-layout, so the sim's setting determines
# what character yetty sees for each physical key.
#-----------------------------------------------------------------------------
set_keyboard_layout() {
    if [ "$KEYBOARD_LAYOUT" = "skip" ]; then
        info "Keyboard layout: leaving sim's existing setting (KEYBOARD_LAYOUT=skip)"
        return 0
    fi
    "$PROJECT_ROOT/tools/sim-keyboard.sh" "$KEYBOARD_LAYOUT" "$SIMULATOR_UDID"
}

#-----------------------------------------------------------------------------
# Install and run app
#-----------------------------------------------------------------------------
install_and_run() {
    if [ ! -d "$APP_BUNDLE" ]; then
        error "App bundle not found: $APP_BUNDLE"
        echo ""
        echo "Build first with:"
        echo "  make build-ios_x86_64-ytrace-release"
        echo ""
        exit 1
    fi

    info "Installing app: $APP_BUNDLE"
    xcrun simctl install "$SIMULATOR_UDID" "$APP_BUNDLE"

    info "Launching app..."
    xcrun simctl launch --console "$SIMULATOR_UDID" "$BUNDLE_ID" &

    success "Yetty launched! Check Simulator window."
}

#-----------------------------------------------------------------------------
# Kill simulator
#-----------------------------------------------------------------------------
kill_simulator() {
    info "Shutting down simulators..."
    xcrun simctl shutdown all 2>/dev/null || true
    killall "Simulator" 2>/dev/null || true
    success "Simulators shut down"
}

#-----------------------------------------------------------------------------
# List simulators
#-----------------------------------------------------------------------------
list_simulators() {
    echo "Available iOS Simulators:"
    echo ""
    xcrun simctl list devices available
}

#-----------------------------------------------------------------------------
# Show library info
#-----------------------------------------------------------------------------
show_library_info() {
    local lib_path=""
    local build_dir=""

    if [ -f "$PROJECT_ROOT/build-ios_x86_64-ytrace-release/libyetty.a" ]; then
        build_dir="$PROJECT_ROOT/build-ios_x86_64-ytrace-release"
        lib_path="$build_dir/libyetty.a"
    elif [ -f "$PROJECT_ROOT/build-ios_x86_64-ytrace-debug/libyetty.a" ]; then
        build_dir="$PROJECT_ROOT/build-ios_x86_64-ytrace-debug"
        lib_path="$build_dir/libyetty.a"
    fi

    if [ -n "$lib_path" ]; then
        echo ""
        echo "Built library:"
        echo "  Path: $lib_path"
        echo "  Size: $(du -h "$lib_path" | cut -f1)"
        echo "  Arch: $(lipo -info "$lib_path" 2>/dev/null | sed 's/.*: //')"
        echo ""
        echo "Assets directory:"
        echo "  $build_dir/ios-assets/"
        echo ""
    else
        warn "Library not built yet"
        echo "Run: make build-ios_x86_64-ytrace-release"
    fi
}

#-----------------------------------------------------------------------------
# Main
#-----------------------------------------------------------------------------
main() {
    echo "========================================"
    echo "  Yetty iOS x86_64 Simulator"
    echo "========================================"
    echo ""

    case "${1:-}" in
        --kill|-k)
            kill_simulator
            exit 0
            ;;
        --list|-l)
            list_simulators
            exit 0
            ;;
        --info|-i)
            show_library_info
            exit 0
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "PREREQUISITE: Build the library first!"
            echo "  make build-ios_x86_64-ytrace-release"
            echo "  make build-ios_x86_64-ytrace-debug"
            echo ""
            echo "OPTIONS:"
            echo "  (no options)    Start simulator (and install app if available)"
            echo "  --kill, -k      Kill all running simulators"
            echo "  --list, -l      List available simulators"
            echo "  --info, -i      Show built library info"
            echo "  --help, -h      Show this help"
            echo ""
            echo "ENV:"
            echo "  KEYBOARD_LAYOUT  Hardware keyboard layout for the sim."
            echo "                   Default: Dvorak. Valid: Dvorak,"
            echo "                   DvorakLeft, DvorakRight, QWERTY, Colemak,"
            echo "                   or 'skip' to leave the sim's setting."
            echo "  APP_BUNDLE       Override the .app path."
            echo ""
            echo "NOTE:"
            echo "  The iOS build produces libyetty.a (static library)."
            echo "  To run in the simulator, you need an iOS app project"
            echo "  that links this library (TODO: add sample Xcode project)."
            echo ""
            echo "EXAMPLES:"
            echo "  # Build the library:"
            echo "  make build-ios_x86_64-ytrace-release"
            echo ""
            echo "  # Start simulator:"
            echo "  $0"
            echo ""
            echo "  # Check build output:"
            echo "  $0 --info"
            exit 0
            ;;
    esac

    check_macos
    check_xcode
    find_or_create_simulator
    boot_simulator
    set_keyboard_layout
    show_library_info
    install_and_run
}

main "$@"
