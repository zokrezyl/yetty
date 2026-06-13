#!/bin/bash
#
# Run Yetty Android x86_64 build in an emulator
# Supports: Ubuntu and macOS
#
# Usage:
#   First build: make build-android_x86_64-ytrace-debug (or -release)
#   Then run:
#     ./tools/android_x86_64.sh              # Normal: emulator window with yetty
#     ./tools/android_x86_64.sh --vnc        # VNC: emulator window + yetty VNC server
#     ./tools/android_x86_64.sh --vnc-headless  # Headless: no window, yetty VNC only
#     ./tools/android_x86_64.sh --soft-gpu   # Use software GPU (SwiftShader)
#     ./tools/android_x86_64.sh --kill       # Kill running emulator
#     ./tools/android_x86_64.sh --help       # Show full help
#
# THREE MODES:
#
#   NORMAL (no flags):
#     - Emulator window opens with yetty running inside
#     - Use the emulator GUI directly
#     - WARNING: Ctrl keys don't work (Qt intercepts for pinch-zoom)
#
#   --vnc (VNC with window):
#     - Emulator window opens (can minimize it)
#     - Yetty's VNC server runs on port 5900
#     - Connect: vncviewer localhost:5900
#     - Ctrl keys work via VNC!
#
#   --vnc-headless (Headless VNC):
#     - NO emulator window at all
#     - Yetty's VNC server runs on port 5900
#     - Connect: vncviewer localhost:5900
#     - Best for servers/remote use
#
# =============================================================================
# TRANSPARENCY: What this script downloads and why
# =============================================================================
#
# This script may download the following official Google components:
#
# 1. ANDROID SDK COMMAND LINE TOOLS (if not installed)
#    - What: Official Android development tools from Google
#    - From: https://developer.android.com/studio#command-tools
#    - Why: Required to manage Android emulator and system images
#    - Size: ~150 MB
#
# 2. ANDROID SYSTEM IMAGE (if not installed)
#    - What: Android 14 (API 34) x86_64 emulator image with Google APIs
#    - From: Google's official SDK repository via sdkmanager
#    - Package: "system-images;android-34;google_apis;x86_64"
#    - Why: The actual Android OS that runs inside the emulator
#    - Size: ~1.2 GB
#
# 3. PLATFORM TOOLS & EMULATOR (if not installed)
#    - What: adb (Android Debug Bridge) and emulator binaries
#    - From: Google's official SDK repository via sdkmanager
#    - Why: adb installs/runs apps; emulator runs the virtual device
#    - Size: ~300 MB
#
# All downloads are from official Google servers via HTTPS.
# No third-party sources are used.
#
# On Ubuntu: Uses apt package 'android-sdk' from Ubuntu repositories
# On macOS: Uses Homebrew cask 'android-commandlinetools' from Google
#
# =============================================================================
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Auto-yes mode (skip prompts)
AUTO_YES=false
DEBUG_MODE=false
VNC_MODE=false
VNC_HEADLESS=false
VNC_PORT=5900
SOFT_GPU=false

# Configuration
AVD_NAME="yetty_x86_64"
SYSTEM_IMAGE="system-images;android-34;google_apis;x86_64"
ACTIVITY_NAME="android.app.NativeActivity"

# Directory holding the gradle flavor APK outputs. Each program is built as a
# separate gradle product flavor, so each has its own APK + applicationId:
#   yetty    → app/outputs/apk/yetty/release/app-yetty-release.apk    (com.yetty.terminal)
#   ygreeter → app/outputs/apk/ygreeter/release/app-ygreeter-release.apk (com.yetty.greeter)
APK_DIR="${APK_DIR:-$PROJECT_ROOT/build-android_x86_64-ytrace-release/app/outputs/apk}"
# Honour an explicit APK_PATH from the environment (overrides flavor lookup).
APK_PATH_OVERRIDE="${APK_PATH:-}"

# Which program to deploy. Each is a distinct app/APK; --program selects which
# one to install and launch.
PROGRAM="yetty"
# Resolved from PROGRAM by resolve_program (after argument parsing).
PACKAGE_NAME="com.yetty.terminal"
APK_PATH=""
LAUNCH_COMPONENT="$PACKAGE_NAME/$ACTIVITY_NAME"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

info() { echo -e "${BLUE}[INFO]${NC} $*"; }
warn() { echo -e "${YELLOW}[WARN]${NC} $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; }
success() { echo -e "${GREEN}[OK]${NC} $*"; }

# Validate $PROGRAM and resolve its APK, package and launch component. Each
# program is a separate gradle flavor / APK with its own applicationId.
resolve_program() {
    case "$PROGRAM" in
        yetty)
            PACKAGE_NAME="com.yetty.terminal"
            APK_PATH="${APK_PATH_OVERRIDE:-$APK_DIR/yetty/release/app-yetty-release.apk}"
            ;;
        ygreeter)
            PACKAGE_NAME="com.yetty.greeter"
            APK_PATH="${APK_PATH_OVERRIDE:-$APK_DIR/ygreeter/release/app-ygreeter-release.apk}"
            ;;
        *)
            error "Unknown --program '$PROGRAM' (expected: yetty or ygreeter)"
            exit 1
            ;;
    esac
    LAUNCH_COMPONENT="$PACKAGE_NAME/$ACTIVITY_NAME"
}

# Prompt user for confirmation (respects AUTO_YES)
confirm() {
    local prompt="$1"
    if [ "$AUTO_YES" = true ]; then
        info "Auto-confirming: $prompt"
        return 0
    fi
    read -p "$prompt [y/N] " -n 1 -r
    echo
    [[ $REPLY =~ ^[Yy]$ ]]
}

#-----------------------------------------------------------------------------
# Detect OS
#-----------------------------------------------------------------------------
detect_os() {
    case "$(uname -s)" in
        Linux*)  OS="linux" ;;
        Darwin*) OS="macos" ;;
        *)       error "Unsupported OS: $(uname -s)"; exit 1 ;;
    esac
    info "Detected OS: $OS"
}

#-----------------------------------------------------------------------------
# Find Android SDK
#-----------------------------------------------------------------------------
find_android_sdk() {
    # Check common locations
    local sdk_paths=(
        "$ANDROID_HOME"
        "$ANDROID_SDK_ROOT"
        "$HOME/android-sdk"
        "$HOME/Android/Sdk"
        "$HOME/Library/Android/sdk"
        "/usr/local/android-sdk"
    )

    for path in "${sdk_paths[@]}"; do
        if [ -d "$path" ] && [ -f "$path/platform-tools/adb" ]; then
            ANDROID_SDK="$path"
            export ANDROID_HOME="$path"
            export ANDROID_SDK_ROOT="$path"
            # AVD home may be in .config/.android on some systems
            if [ -d "$HOME/.config/.android/avd" ]; then
                export ANDROID_AVD_HOME="$HOME/.config/.android/avd"
            elif [ -d "$HOME/.android/avd" ]; then
                export ANDROID_AVD_HOME="$HOME/.android/avd"
            fi
            export PATH="$ANDROID_SDK/platform-tools:$ANDROID_SDK/emulator:$ANDROID_SDK/cmdline-tools/latest/bin:$PATH"
            success "Found Android SDK: $ANDROID_SDK"
            return 0
        fi
    done

    return 1
}

#-----------------------------------------------------------------------------
# Check/Install Android SDK (Ubuntu)
#-----------------------------------------------------------------------------
install_android_sdk_ubuntu() {
    info "Android SDK not found."
    echo ""
    echo "The Android SDK is required to run the emulator."
    echo ""
    echo "This will install from Ubuntu's official repositories:"
    echo "  Package: android-sdk"
    echo "  Source:  Ubuntu/Debian apt repository"
    echo "  Info:    https://packages.ubuntu.com/android-sdk"
    echo ""
    if confirm "Install android-sdk via apt?"; then
        # Downloads from Ubuntu's official apt repositories
        info "Installing Android SDK from Ubuntu repositories..."
        sudo apt-get update
        sudo apt-get install -y android-sdk google-android-emulator-installer

        # Set up paths
        if [ -d "/usr/lib/android-sdk" ]; then
            ANDROID_SDK="/usr/lib/android-sdk"
        else
            error "Installation failed or SDK not found"
            exit 1
        fi

        export ANDROID_HOME="$ANDROID_SDK"
        export ANDROID_SDK_ROOT="$ANDROID_SDK"
        export PATH="$ANDROID_SDK/platform-tools:$ANDROID_SDK/emulator:$ANDROID_SDK/cmdline-tools/latest/bin:$PATH"

        success "Android SDK installed"
    else
        echo ""
        echo "Manual installation instructions:"
        echo "  1. Download Command Line Tools from:"
        echo "     https://developer.android.com/studio#command-tools"
        echo "  2. Extract to ~/android-sdk/cmdline-tools/latest/"
        echo "  3. Run: sdkmanager --install 'platform-tools' 'emulator' '$SYSTEM_IMAGE'"
        echo "  4. Set ANDROID_HOME=~/android-sdk"
        exit 1
    fi
}

#-----------------------------------------------------------------------------
# Check/Install Android SDK (macOS)
#-----------------------------------------------------------------------------
install_android_sdk_macos() {
    info "Android SDK not found."
    echo ""
    echo "The Android SDK is required to run the emulator."
    echo ""

    # Check if Homebrew is available
    if command -v brew &> /dev/null; then
        echo "This will install from Homebrew (official Google package):"
        echo "  Cask:   android-commandlinetools"
        echo "  Source: https://developer.android.com/studio#command-tools"
        echo "  Info:   https://formulae.brew.sh/cask/android-commandlinetools"
        echo ""
        if confirm "Install android-commandlinetools via Homebrew?"; then
            # Downloads official Google Android tools via Homebrew
            info "Installing Android Command Line Tools via Homebrew..."
            brew install --cask android-commandlinetools

            # Homebrew installs to this location
            ANDROID_SDK="$HOME/Library/Android/sdk"
            mkdir -p "$ANDROID_SDK"

            export ANDROID_HOME="$ANDROID_SDK"
            export ANDROID_SDK_ROOT="$ANDROID_SDK"
            export PATH="$ANDROID_SDK/platform-tools:$ANDROID_SDK/emulator:$ANDROID_SDK/cmdline-tools/latest/bin:$PATH"

            # Accept licenses and install required components
            yes | sdkmanager --licenses 2>/dev/null || true
            sdkmanager --install "platform-tools" "emulator"

            success "Android SDK installed"
            return 0
        fi
    fi

    echo ""
    echo "Manual installation instructions:"
    echo "  1. Install Homebrew: /bin/bash -c \"\$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\""
    echo "  2. Run: brew install --cask android-commandlinetools"
    echo "  3. Or download from: https://developer.android.com/studio#command-tools"
    exit 1
}

#-----------------------------------------------------------------------------
# Check/Install system image
#-----------------------------------------------------------------------------
check_system_image() {
    info "Checking for system image: $SYSTEM_IMAGE"

    if ! command -v sdkmanager &> /dev/null; then
        # Try to find sdkmanager
        local sdkmanager_paths=(
            "$ANDROID_SDK/cmdline-tools/latest/bin/sdkmanager"
            "$ANDROID_SDK/cmdline-tools/bin/sdkmanager"
            "$ANDROID_SDK/tools/bin/sdkmanager"
        )
        for path in "${sdkmanager_paths[@]}"; do
            if [ -f "$path" ]; then
                alias sdkmanager="$path"
                break
            fi
        done
    fi

    # Check if image is installed
    local image_path="$ANDROID_SDK/system-images/android-34/google_apis/x86_64"
    if [ -d "$image_path" ]; then
        success "System image found"
        return 0
    fi

    warn "System image not found: $SYSTEM_IMAGE"
    echo ""
    echo "This will download from Google's official SDK repository:"
    echo "  Package: $SYSTEM_IMAGE"
    echo "  Source:  https://dl.google.com/android/repository/"
    echo "  What:    Android 14 (API 34) x86_64 emulator image"
    echo "  Size:    ~1.2 GB"
    echo ""
    if confirm "Download system image?"; then
        # Downloads from Google's official Android SDK repository via HTTPS
        info "Downloading system image from Google (this may take a while)..."
        yes | sdkmanager --licenses 2>/dev/null || true
        sdkmanager --install "$SYSTEM_IMAGE"
        success "System image installed"
    else
        error "System image required for emulator"
        exit 1
    fi
}

#-----------------------------------------------------------------------------
# Check/Create AVD
#-----------------------------------------------------------------------------
check_avd() {
    info "Checking for AVD: $AVD_NAME"

    if ! command -v avdmanager &> /dev/null; then
        local avdmanager_paths=(
            "$ANDROID_SDK/cmdline-tools/latest/bin/avdmanager"
            "$ANDROID_SDK/cmdline-tools/bin/avdmanager"
            "$ANDROID_SDK/tools/bin/avdmanager"
        )
        for path in "${avdmanager_paths[@]}"; do
            if [ -f "$path" ]; then
                AVDMANAGER="$path"
                break
            fi
        done
    else
        AVDMANAGER="avdmanager"
    fi

    # Check if AVD exists
    if $AVDMANAGER list avd 2>/dev/null | grep -q "Name: $AVD_NAME"; then
        success "AVD found: $AVD_NAME"
        return 0
    fi

    info "Creating AVD: $AVD_NAME"
    echo "no" | $AVDMANAGER create avd \
        --name "$AVD_NAME" \
        --package "$SYSTEM_IMAGE" \
        --device "pixel_6" \
        --force

    # Configure AVD for better performance
    local avd_config="$HOME/.android/avd/${AVD_NAME}.avd/config.ini"
    if [ -f "$avd_config" ]; then
        # Enable GPU acceleration
        echo "hw.gpu.enabled=yes" >> "$avd_config"
        echo "hw.gpu.mode=auto" >> "$avd_config"
        # Set RAM
        echo "hw.ramSize=4096" >> "$avd_config"
        # Enable keyboard
        echo "hw.keyboard=yes" >> "$avd_config"
    fi

    success "AVD created: $AVD_NAME"
}

#-----------------------------------------------------------------------------
# Check KVM (Linux only)
#-----------------------------------------------------------------------------
check_kvm() {
    if [ "$OS" != "linux" ]; then
        return 0
    fi

    info "Checking KVM support..."

    if [ ! -e /dev/kvm ]; then
        warn "KVM not available. Emulator will be slow."
        echo ""
        echo "To enable KVM:"
        echo "  1. Ensure virtualization is enabled in BIOS"
        echo "  2. Install KVM: sudo apt-get install qemu-kvm"
        echo "  3. Add user to kvm group: sudo usermod -aG kvm \$USER"
        echo "  4. Log out and back in"
        echo ""
        if ! confirm "Continue without KVM (slow)?"; then
            exit 1
        fi
    elif [ ! -r /dev/kvm ] || [ ! -w /dev/kvm ]; then
        warn "KVM exists but not accessible"
        echo "Run: sudo usermod -aG kvm \$USER"
        echo "Then log out and back in"
    else
        success "KVM available"
    fi
}


#-----------------------------------------------------------------------------
# Start emulator
#-----------------------------------------------------------------------------
start_emulator() {
    info "Starting emulator..."

    # Check if already running
    if adb devices 2>/dev/null | grep -q "emulator"; then
        success "Emulator already running"
        return 0
    fi

    # Find emulator binary
    local emulator_bin="$ANDROID_SDK/emulator/emulator"
    if [ ! -f "$emulator_bin" ]; then
        emulator_bin="$(command -v emulator)"
    fi

    if [ -z "$emulator_bin" ] || [ ! -f "$emulator_bin" ]; then
        error "Emulator binary not found"
        exit 1
    fi

    # Determine GPU mode
    local gpu_mode="host"
    if [ "$SOFT_GPU" = true ]; then
        gpu_mode="swiftshader_indirect"
        info "Using software GPU (SwiftShader)"
    fi

    # Start emulator in background
    if [ "$VNC_HEADLESS" = true ]; then
        # VNC headless: no emulator window, use yetty's built-in VNC server
        # The emulator runs without display, yetty provides VNC access
        info "Launching emulator in HEADLESS mode (no window)..."
        info "  - No emulator window"
        info "  - GPU mode: $gpu_mode"
        info "  - Yetty's VNC server on port $VNC_PORT"
        "$emulator_bin" -avd "$AVD_NAME" \
            -no-window \
            -no-audio \
            -gpu "$gpu_mode" \
            -no-snapshot-load \
            -skin 1920x1080 \
            -netdelay none \
            -netspeed full &
    elif [ "$VNC_MODE" = true ]; then
        # VNC mode with window: emulator window + yetty's VNC server
        info "Launching emulator for VNC mode..."
        info "  - Emulator window opens (can ignore it)"
        info "  - GPU mode: $gpu_mode"
        info "  - Yetty's VNC server on port $VNC_PORT"
        "$emulator_bin" -avd "$AVD_NAME" \
            -gpu "$gpu_mode" \
            -no-snapshot-load \
            -skin 1920x1080 \
            -netdelay none \
            -netspeed full \
            -qemu -k en-us &
    else
        # Normal mode: emulator with window
        info "Launching emulator with window..."
        info "  - GPU mode: $gpu_mode"
        "$emulator_bin" -avd "$AVD_NAME" \
            -gpu "$gpu_mode" \
            -no-snapshot-load \
            -skin 1920x1080 \
            -netdelay none \
            -netspeed full \
            -qemu -k en-us &
    fi
    EMULATOR_PID=$!

    # Wait for emulator to boot
    info "Waiting for emulator to boot..."
    local timeout=120
    local count=0
    while [ $count -lt $timeout ]; do
        if adb shell getprop sys.boot_completed 2>/dev/null | grep -q "1"; then
            success "Emulator booted"
            return 0
        fi
        sleep 2
        count=$((count + 2))
        echo -n "."
    done
    echo ""

    error "Emulator boot timeout"
    exit 1
}

#-----------------------------------------------------------------------------
# Configure emulator settings for terminal use
#-----------------------------------------------------------------------------
configure_emulator() {
    info "Configuring emulator settings..."

    # Disable pointer location overlay (annoying visual feedback)
    adb shell settings put system pointer_location 0

    # Disable show touches
    adb shell settings put system show_touches 0

    # Disable on-screen keyboard when hardware keyboard is connected
    adb shell settings put secure show_ime_with_hard_keyboard 0

    # Hide the navigation bar to maximize terminal space
    adb shell settings put global policy_control immersive.navigation=*

    success "Emulator configured (on-screen keyboard disabled)"
}

#-----------------------------------------------------------------------------
# Install and run APK
#-----------------------------------------------------------------------------
install_and_run() {
    if [ ! -f "$APK_PATH" ]; then
        error "APK not found: $APK_PATH"
        error "Run with --build first or run without arguments"
        exit 1
    fi

    # Configure emulator for terminal use
    configure_emulator

    info "Installing APK..."
    adb install -r "$APK_PATH"

    if [ "$VNC_HEADLESS" = true ] || [ "$VNC_MODE" = true ]; then
        # VNC mode: use yetty's built-in VNC server
        # Works with or without emulator window - yetty provides the VNC!
        local host_ip=$(hostname -I 2>/dev/null | awk '{print $1}')
        [ -z "$host_ip" ] && host_ip="<host-ip>"

        if [ "$VNC_HEADLESS" = true ]; then
            info "Setting up VNC HEADLESS mode (no emulator window)..."
        else
            info "Setting up VNC mode..."
        fi

        # Set yetty VNC properties BEFORE starting the app
        info "Setting VNC properties (debug.yetty.vnc.*)..."
        adb shell setprop debug.yetty.vnc.headless 1
        adb shell setprop debug.yetty.vnc.port $VNC_PORT
        success "VNC properties set"

        # Forward yetty's VNC port from emulator to host
        # Use tcp:0.0.0.0:PORT to bind to all interfaces for remote access
        info "Forwarding port $VNC_PORT (binding to 0.0.0.0 for remote access)..."
        adb forward tcp:$VNC_PORT tcp:$VNC_PORT

        # Also set up a listener on all interfaces using socat if available
        if command -v socat &>/dev/null; then
            # Kill any existing socat on this port
            pkill -f "socat.*:$VNC_PORT" 2>/dev/null || true
            # Forward from all interfaces to localhost
            socat TCP-LISTEN:$VNC_PORT,bind=0.0.0.0,fork,reuseaddr TCP:127.0.0.1:$VNC_PORT &
            success "Port $VNC_PORT bound to 0.0.0.0 via socat"
        else
            warn "Install socat for remote VNC access: sudo apt install socat"
            warn "Without socat, VNC only works on localhost"
        fi

        info "Starting $PROGRAM..."
        adb shell am start -n "$LAUNCH_COMPONENT"
        sleep 3

        # Verify the app is actually running
        if adb shell pidof "$PACKAGE_NAME" >/dev/null 2>&1; then
            success "$PROGRAM is running! (PID: $(adb shell pidof $PACKAGE_NAME))"
        else
            error "$PROGRAM failed to start!"
            info "Check logs: adb logcat -s yetty"
            return 1
        fi

        # Check if VNC port is listening
        if nc -z localhost $VNC_PORT 2>/dev/null; then
            success "Yetty VNC server is listening on port $VNC_PORT"
        else
            warn "VNC port $VNC_PORT not responding yet (app may need more time)"
        fi

        echo ""
        echo "=============================================="
        if [ "$VNC_HEADLESS" = true ]; then
            echo "  VNC HEADLESS MODE ACTIVE"
        else
            echo "  VNC MODE ACTIVE"
        fi
        echo "=============================================="
        echo ""
        echo "Connect from REMOTE host:"
        echo "  vncviewer $host_ip:$VNC_PORT"
        echo ""
        echo "Connect LOCALLY:"
        echo "  vncviewer localhost:$VNC_PORT"
        echo ""
        echo "Ctrl+C, Ctrl+D, etc. work via VNC!"
        if [ "$VNC_HEADLESS" = true ]; then
            echo "(No emulator window - truly headless)"
        fi
        echo ""

    else
        info "Starting $PROGRAM..."
        adb shell am start -n "$LAUNCH_COMPONENT"
        sleep 2

        # Verify the app is actually running
        if adb shell pidof "$PACKAGE_NAME" >/dev/null 2>&1; then
            success "$PROGRAM is running! (PID: $(adb shell pidof $PACKAGE_NAME))"
        else
            error "$PROGRAM failed to start!"
            info "Check logs: adb logcat -s yetty"
            return 1
        fi

        # Launch scrcpy for display
        if command -v scrcpy &>/dev/null; then
            info "Launching scrcpy for display..."
            scrcpy --window-title "Yetty Android" &
        else
            warn "scrcpy not installed. Install with: sudo apt install scrcpy"
            warn "Then run 'scrcpy' manually to view the emulator display"
        fi
    fi

    echo ""
    echo "Useful commands:"
    echo "  adb logcat -s yetty          # View Yetty logs"
    echo "  adb logcat | grep -i yetty   # View all related logs"
    echo "  adb shell am force-stop $PACKAGE_NAME  # Stop app"
    echo "  ./tools/android_x86_64.sh --kill       # Kill emulator"
}

#-----------------------------------------------------------------------------
# Kill emulator
#-----------------------------------------------------------------------------
kill_emulator() {
    info "Killing emulator..."
    adb -s emulator-5554 emu kill 2>/dev/null || true
    pkill -f "qemu-system-x86_64.*$AVD_NAME" 2>/dev/null || true
    pkill -f "emulator.*$AVD_NAME" 2>/dev/null || true
    success "Emulator killed"
}

#-----------------------------------------------------------------------------
# Main
#-----------------------------------------------------------------------------
main() {
    # Parse global flags first (can be combined with other flags)
    local args=()
    local skip_next=false
    for i in $(seq 1 $#); do
        if [ "$skip_next" = true ]; then
            skip_next=false
            continue
        fi
        arg="${!i}"
        next_i=$((i + 1))
        next_arg="${!next_i:-}"
        case "$arg" in
            --yes|-y)
                AUTO_YES=true
                ;;
            --debug|-d)
                DEBUG_MODE=true
                ;;
            --soft-gpu)
                SOFT_GPU=true
                ;;
            --vnc-port)
                if [ -n "$next_arg" ] && [[ "$next_arg" =~ ^[0-9]+$ ]]; then
                    VNC_PORT="$next_arg"
                    skip_next=true
                else
                    error "--vnc-port requires a port number"
                    exit 1
                fi
                ;;
            --program)
                if [ -n "$next_arg" ]; then
                    PROGRAM="$next_arg"
                    skip_next=true
                else
                    error "--program requires a name (yetty or ygreeter)"
                    exit 1
                fi
                ;;
            *)
                args+=("$arg")
                ;;
        esac
    done
    set -- "${args[@]}"

    # Enable debug tracing if requested
    if [ "$DEBUG_MODE" = true ]; then
        set -x
    fi

    # Validate --program and resolve the launch component.
    resolve_program

    echo "========================================"
    echo "  Yetty Android x86_64 Emulator"
    echo "========================================"
    echo "  Program: $PROGRAM ($LAUNCH_COMPONENT)"
    if [ "$AUTO_YES" = true ]; then
        echo "  (auto-yes mode enabled)"
    fi
    echo ""

    detect_os

    # Parse action arguments
    case "${1:-}" in
        --run)
            find_android_sdk || { error "Android SDK not found"; exit 1; }
            check_system_image
            check_avd
            check_kvm
            if [ ! -f "$APK_PATH" ]; then
                error "APK not found: $APK_PATH"
                echo "Build first: make build-android_x86_64-ytrace-debug"
                exit 1
            fi
            start_emulator
            install_and_run
            exit 0
            ;;
        --vnc)
            VNC_MODE=true
            find_android_sdk || { error "Android SDK not found"; exit 1; }
            check_system_image
            check_avd
            check_kvm
            if [ ! -f "$APK_PATH" ]; then
                error "APK not found: $APK_PATH"
                echo "Build first: make build-android_x86_64-ytrace-debug"
                exit 1
            fi
            start_emulator
            install_and_run
            exit 0
            ;;
        --vnc-headless)
            VNC_HEADLESS=true
            find_android_sdk || { error "Android SDK not found"; exit 1; }
            check_system_image
            check_avd
            check_kvm
            if [ ! -f "$APK_PATH" ]; then
                error "APK not found: $APK_PATH"
                echo "Build first: make build-android_x86_64-ytrace-debug"
                exit 1
            fi
            start_emulator
            install_and_run
            exit 0
            ;;
        --kill)
            kill_emulator
            exit 0
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "PREREQUISITE: Build the APK first!"
            echo "  make build-android_x86_64-ytrace-debug"
            echo "  make build-android_x86_64-ytrace-release"
            echo ""
            echo "MODES:"
            echo ""
            echo "  (no options)    NORMAL MODE - Emulator window with yetty inside"
            echo "                  Use if you want the Android emulator GUI."
            echo "                  WARNING: Ctrl keys don't work (Qt intercepts them)"
            echo ""
            echo "  --vnc           VNC MODE - Emulator window + yetty VNC server"
            echo "                  Emulator window opens but you can minimize it."
            echo "                  Connect via VNC to yetty for working Ctrl keys."
            echo ""
            echo "  --vnc-headless  HEADLESS MODE - No emulator window, yetty VNC only"
            echo "                  Best for remote/server use. No GUI at all."
            echo "                  Connect via VNC to yetty on port 5900."
            echo ""
            echo "OTHER OPTIONS:"
            echo "  --run           Same as no options (run emulator)"
            echo "  --program NAME  Which app to install + launch (default: yetty)"
            echo "                  yetty    → the terminal        (com.yetty.terminal)"
            echo "                  ygreeter → the ygui showcase    (com.yetty.greeter)"
            echo "                  (each is a separate gradle-flavor APK)"
            echo "  --soft-gpu      Use software GPU (SwiftShader) instead of host GPU"
            echo "  --vnc-port N    Set VNC port (default: 5900)"
            echo "  --kill          Kill running emulator"
            echo "  --yes, -y       Auto-confirm all prompts"
            echo "  --debug, -d     Enable bash debug tracing"
            echo "  --help          Show this help"
            echo ""
            echo "EXAMPLES:"
            echo ""
            echo "  # First build the APK (only once, or after code changes):"
            echo "  make build-android_x86_64-ytrace-debug"
            echo ""
            echo "  # Then run headless (server/remote use):"
            echo "  $0 --vnc-headless"
            echo "  vncviewer localhost:5900"
            echo ""
            echo "  # Or with emulator window + VNC for input:"
            echo "  $0 --vnc"
            echo "  vncviewer localhost:5900"
            echo ""
            echo "  # Or normal mode (Ctrl keys won't work):"
            echo "  $0"
            echo ""
            echo "  # Launch the ygreeter showcase instead of the terminal:"
            echo "  $0 --program ygreeter"
            echo ""
            echo "WHY VNC?"
            echo "  The Android emulator's Qt GUI intercepts Ctrl key for pinch-zoom,"
            echo "  making Ctrl+C, Ctrl+D, etc. unusable in terminal apps."
            echo "  VNC mode bypasses this by using yetty's built-in VNC server."
            exit 0
            ;;
    esac

    # Default: setup and run (NO BUILD - use 'make build-android_x86_64-ytrace-*' first!)
    if ! find_android_sdk; then
        case "$OS" in
            linux) install_android_sdk_ubuntu ;;
            macos) install_android_sdk_macos ;;
        esac
    fi

    check_system_image
    check_avd
    check_kvm

    # Check APK exists - DO NOT BUILD, user must build separately
    if [ ! -f "$APK_PATH" ]; then
        error "APK not found: $APK_PATH"
        echo ""
        echo "Build the APK first with:"
        echo "  make build-android_x86_64-ytrace-debug"
        echo "  make build-android_x86_64-ytrace-release"
        echo ""
        exit 1
    fi

    start_emulator
    install_and_run
}

main "$@"
