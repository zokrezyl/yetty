#!/bin/bash
#
# Run Yetty tvOS x86_64 build in the Apple TV Simulator
# Supports: macOS only
#
# Usage:
#   First build: make build-tvos_x86_64-ytrace-release (or -debug)
#   Then run:
#     ./tools/ios-tvos/tvos/yetty/x86_64.sh             # Start simulator with yetty
#     ./tools/ios-tvos/tvos/yetty/x86_64.sh --kill      # Kill running simulator
#     ./tools/ios-tvos/tvos/yetty/x86_64.sh --list      # List available simulators
#     ./tools/ios-tvos/tvos/yetty/x86_64.sh --help      # Show full help

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Script lives at tools/ios-tvos/tvos/yetty/, so the repo root is four dirs up.
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"

# Configuration
# Apple TV 4K (3rd gen) at 4K is heavy on Intel Macs (SimRenderServer +
# SimMetalHost burn CPU). The 1080p variant is the same device family but
# at 1920x1080 — far more responsive on Intel hosts. Override with
# DEVICE_TYPE_NAME="Apple TV 4K (3rd generation)" if you really want 4K.
DEVICE_TYPE_NAME="${DEVICE_TYPE_NAME:-Apple TV 4K (3rd generation) (at 1080p)}"
SIMULATOR_NAME="${SIMULATOR_NAME:-$DEVICE_TYPE_NAME}"

# Try release build first, then debug
if [ -d "$PROJECT_ROOT/build-tvos_x86_64-ytrace-release/yetty.app" ]; then
    APP_BUNDLE="${APP_BUNDLE:-$PROJECT_ROOT/build-tvos_x86_64-ytrace-release/yetty.app}"
else
    APP_BUNDLE="${APP_BUNDLE:-$PROJECT_ROOT/build-tvos_x86_64-ytrace-debug/yetty.app}"
fi
BUNDLE_ID="com.yetty.terminal"

# Hardware-keyboard layout the tvOS Simulator should advertise to yetty
# (yetty's pressesBegan: receives UIKey.characters which is the post-layout
# value). Default Dvorak; export KEYBOARD_LAYOUT=QWERTY to override, or
# KEYBOARD_LAYOUT=skip to leave the sim's existing setting alone.
KEYBOARD_LAYOUT="${KEYBOARD_LAYOUT:-Dvorak}"

# Where to capture yetty's stderr (ytrace + crash dumps) for the post-launch
# health monitor. Override with LOG_FILE=… to put it elsewhere.
LOG_FILE="${LOG_FILE:-$PROJECT_ROOT/tmp/tvos_yetty.log}"

# Health-monitor cadence (seconds between reports) and max iterations.
# 0 iterations = monitor disabled, just launch and exit.
MONITOR_INTERVAL="${MONITOR_INTERVAL:-10}"
MONITOR_ITERATIONS="${MONITOR_ITERATIONS:-0}"   # 0 = forever (until yetty dies / Ctrl-C)

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

info()    { echo -e "${BLUE}[INFO]${NC} $*"; }
warn()    { echo -e "${YELLOW}[WARN]${NC} $*"; }
error()   { echo -e "${RED}[ERROR]${NC} $*"; }
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

    if ! xcrun simctl help &> /dev/null; then
        error "simctl not found. Install Xcode from the App Store."
        exit 1
    fi

    success "Xcode tools available"
}

#-----------------------------------------------------------------------------
# Find or create simulator
#
# Apple TV simulators are pre-shipped with Xcode under names like
# "Apple TV 4K (3rd generation) (at 1080p)" — we look for that name first
# and only create a new device if missing.
#-----------------------------------------------------------------------------
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
        return 0
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
    info "Using runtime: $runtime"

    local device_type
    device_type="$(xcrun simctl list devicetypes -j | python3 -c "
import sys, json
target = '$DEVICE_TYPE_NAME'
dts = json.load(sys.stdin)['devicetypes']
for d in dts:
    if d.get('name') == target:
        print(d['identifier']); sys.exit(0)
" 2>/dev/null)"

    if [ -z "$device_type" ]; then
        device_type="com.apple.CoreSimulator.SimDeviceType.Apple-TV-4K-3rd-generation-1080p"
    fi
    info "Using device type: $device_type"

    SIMULATOR_UDID="$(xcrun simctl create "$SIMULATOR_NAME" "$device_type" "$runtime")"
    success "Created simulator: $SIMULATOR_UDID"
}

#-----------------------------------------------------------------------------
# Boot simulator
#-----------------------------------------------------------------------------
boot_simulator() {
    local state
    state="$(xcrun simctl list devices -j | python3 -c "
import sys, json
d = json.load(sys.stdin)
udid = '$SIMULATOR_UDID'
for runtime, devs in d['devices'].items():
    for dev in devs:
        if dev['udid'] == udid:
            print(dev.get('state','Unknown')); sys.exit(0)
" 2>/dev/null)"

    if [ "$state" = "Booted" ]; then
        success "Simulator already booted"
        return 0
    fi

    info "Booting simulator..."
    xcrun simctl boot "$SIMULATOR_UDID" 2>/dev/null || true
    open -a Simulator --args -CurrentDeviceUDID "$SIMULATOR_UDID"

    info "Waiting for simulator to boot..."
    local count=0
    while [ $count -lt 60 ]; do
        state="$(xcrun simctl list devices -j | python3 -c "
import sys, json
d = json.load(sys.stdin)
udid = '$SIMULATOR_UDID'
for runtime, devs in d['devices'].items():
    for dev in devs:
        if dev['udid'] == udid:
            print(dev.get('state','Unknown')); sys.exit(0)
" 2>/dev/null)"
        if [ "$state" = "Booted" ]; then
            break
        fi
        sleep 1
        count=$((count + 1))
        echo -n "."
    done
    echo ""

    if [ "$state" != "Booted" ]; then
        error "Simulator boot timeout (state=$state)"
        exit 1
    fi

    # state=Booted only means launchd is up. simctl launch needs frontboardd
    # which needs the System App (TVHome on tvOS) — which can take another
    # 30–90s. bootstatus -b blocks until the System App reports ready;
    # without this, the next `simctl launch` fails with "system shell
    # probably crashed" on a cold boot.
    info "Waiting for System App (TVHome) — can take 30-90s on cold start"
    info "(bootstatus output below is live; lines like 'Elapsed=NN' are progress)"
    xcrun simctl bootstatus "$SIMULATOR_UDID" -b || true
    success "Simulator fully booted"
}

#-----------------------------------------------------------------------------
# Set Simulator hardware-keyboard layout via the shared helper. tvOS has
# no Settings.app keyboard pane, so this preference write is the only way
# to change it. yetty's pressesBegan: receives UIKey.characters (post-layout),
# so the sim's setting determines what character yetty sees per physical key.
#
# First run on a sim takes ~30s (boot → write prefs → shutdown → boot →
# wait for booted). Subsequent runs are ~instant — sim-keyboard.sh fast-paths
# when the prefs file already has the desired layout.
#-----------------------------------------------------------------------------
set_keyboard_layout() {
    if [ "$KEYBOARD_LAYOUT" = "skip" ]; then
        info "Keyboard layout: leaving sim's existing setting (KEYBOARD_LAYOUT=skip)"
        return 0
    fi
    info "Configuring keyboard layout '$KEYBOARD_LAYOUT' (may restart sim if not yet set)"
    "$PROJECT_ROOT/tools/sim-keyboard.sh" "$KEYBOARD_LAYOUT" "$SIMULATOR_UDID"
    success "Keyboard layout ready"
}

#-----------------------------------------------------------------------------
# Install and run app
#-----------------------------------------------------------------------------
install_and_run() {
    if [ ! -d "$APP_BUNDLE" ]; then
        error "App bundle not found: $APP_BUNDLE"
        echo ""
        echo "Build first with:"
        echo "  make build-tvos_x86_64-ytrace-release"
        echo ""
        exit 1
    fi

    info "Reinstalling app: $APP_BUNDLE"
    xcrun simctl uninstall "$SIMULATOR_UDID" "$BUNDLE_ID" 2>/dev/null || true
    xcrun simctl install "$SIMULATOR_UDID" "$APP_BUNDLE"

    # Stderr (carries ytrace) goes to a file so the monitor can read it.
    mkdir -p "$(dirname "$LOG_FILE")"
    : > "$LOG_FILE"   # truncate so monitor counts start at 0
    info "Launching app (YTRACE on, stderr → $LOG_FILE)"

    # `simctl launch` can fail with "system shell probably crashed" when
    # tvOS's TVHome process is dead even though `simctl list` reports the
    # device as Booted. State=Booted only means launchd is up; TVHome /
    # frontboardd need to be alive too for app launch to succeed. If the
    # first try fails that way, force a clean shutdown + bootstatus wait
    # (which blocks until the System App is up) and retry once.
    local launch_err
    launch_err="$(SIMCTL_CHILD_YTRACE_DEFAULT_ON=yes \
        xcrun simctl launch --terminate-running-process \
                            --stderr="$LOG_FILE" \
                            "$SIMULATOR_UDID" "$BUNDLE_ID" 2>&1)" || true

    if echo "$launch_err" | grep -q "system shell probably crashed\|Simulator device failed to launch"; then
        warn "TVHome appears to have crashed — restarting sim with a full boot wait..."
        xcrun simctl shutdown "$SIMULATOR_UDID" 2>/dev/null || true
        xcrun simctl boot "$SIMULATOR_UDID" 2>/dev/null || true
        info "waiting for System App (TVHome) to come up — this can take 30–90s..."
        # bootstatus -b blocks until the System App reports ready. We need
        # this here (not in the keyboard helper) because launch *requires*
        # frontboardd which the System App brings up.
        xcrun simctl bootstatus "$SIMULATOR_UDID" -b 2>/dev/null || true
        info "retrying launch..."
        SIMCTL_CHILD_YTRACE_DEFAULT_ON=yes \
            xcrun simctl launch --terminate-running-process \
                                --stderr="$LOG_FILE" \
                                "$SIMULATOR_UDID" "$BUNDLE_ID"
    else
        echo "$launch_err"
    fi

    success "Yetty launched on $SIMULATOR_NAME — focus the Apple TV Simulator window."
    success "Hardware keyboard pass-through: I/O > Keyboard > Connect Hardware Keyboard (⌘K)."
}

#-----------------------------------------------------------------------------
# Health monitor — polls yetty's launchctl status + tails the stderr log
# every MONITOR_INTERVAL seconds and prints a one-line summary so you can
# see whether the instance is still running and making progress.
#
# Reports per tick:
#   alive (PID), Δlog lines, total frames rendered, total PTY reads from
#   the VM, recent error count.
#
# Exits when:
#   - yetty's PID disappears (process died), OR
#   - MONITOR_ITERATIONS ticks completed (0 = forever), OR
#   - SIGINT (Ctrl-C).
#-----------------------------------------------------------------------------
monitor_yetty() {
    if [ "$MONITOR_ITERATIONS" = "0" ] && ! [ -t 1 ]; then
        info "stdout not a tty — skipping monitor (export MONITOR_ITERATIONS=N to force)"
        return 0
    fi

    info "Health monitor: every ${MONITOR_INTERVAL}s (Ctrl-C to stop, MONITOR_ITERATIONS=$MONITOR_ITERATIONS)"
    echo

    # Give yetty a moment to register with launchd before the first poll.
    sleep 2

    local last_lines=0
    local tick=0
    while :; do
        tick=$((tick + 1))

        local pid
        pid="$(xcrun simctl spawn "$SIMULATOR_UDID" launchctl list 2>/dev/null \
            | awk -v bid="$BUNDLE_ID" '$0 ~ "UIKitApplication:"bid {print $1; exit}')"

        local total_lines=0
        if [ -f "$LOG_FILE" ]; then
            total_lines="$(wc -l < "$LOG_FILE" | tr -d ' ')"
        fi
        local delta=$((total_lines - last_lines))
        last_lines=$total_lines

        local frames pty_reads errors
        frames="$(grep -c 'frame_render:' "$LOG_FILE" 2>/dev/null || echo 0)"
        pty_reads="$(grep -c 'on_pty_pipe_read:' "$LOG_FILE" 2>/dev/null || echo 0)"
        errors="$(grep -cE '\[error\]|FATAL|abort|sigsegv|sigabrt' "$LOG_FILE" 2>/dev/null || echo 0)"

        local stamp
        stamp="$(date +%H:%M:%S)"

        if [ -z "$pid" ]; then
            error "[$stamp] tick=$tick yetty NOT running (PID gone) — last log lines:"
            tail -10 "$LOG_FILE" 2>/dev/null | sed 's/^/    /'
            return 1
        fi

        printf "${BLUE}[%s]${NC} tick=%d ${GREEN}alive${NC} pid=%s log=%d (+%d) frames=%s pty_reads=%s errors=%s\n" \
            "$stamp" "$tick" "$pid" "$total_lines" "$delta" "$frames" "$pty_reads" "$errors"

        # Highlight the two health signals that matter most:
        #   - frames stuck at 1 = renderer wedged after splash
        #   - pty_reads stuck at 1 = VM stalled after OpenSBI banner
        if [ "$tick" -ge 2 ] && [ "$frames" = "1" ]; then
            warn "    renderer is idle past first frame — sim window probably stuck on splash"
        fi
        if [ "$tick" -ge 2 ] && [ "$pty_reads" = "1" ]; then
            warn "    VM produced only OpenSBI banner — TinyEMU stalled before kernel boot"
        fi

        if [ "$MONITOR_ITERATIONS" != "0" ] && [ "$tick" -ge "$MONITOR_ITERATIONS" ]; then
            success "Monitor done after $tick ticks."
            return 0
        fi

        sleep "$MONITOR_INTERVAL"
    done
}

kill_simulator() {
    info "Shutting down simulators..."
    xcrun simctl shutdown all 2>/dev/null || true
    killall "Simulator" 2>/dev/null || true
    success "Simulators shut down"
}

list_simulators() {
    echo "Available tvOS Simulators:"
    echo ""
    xcrun simctl list devices available | sed -n '/-- tvOS/,/-- /p'
}

main() {
    echo "========================================"
    echo "  Yetty tvOS x86_64 Simulator"
    echo "========================================"
    echo ""

    case "${1:-}" in
        --kill|-k)  kill_simulator; exit 0 ;;
        --list|-l)  list_simulators; exit 0 ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "PREREQUISITE:"
            echo "  make build-tvos_x86_64-ytrace-release"
            echo ""
            echo "OPTIONS:"
            echo "  (no options)    Start simulator and install/launch the app"
            echo "  --kill, -k      Kill all running simulators"
            echo "  --list, -l      List available tvOS simulators"
            echo "  --help, -h      Show this help"
            echo ""
            echo "ENV:"
            echo "  KEYBOARD_LAYOUT     Hardware keyboard layout for the sim."
            echo "                      Default: Dvorak. Valid: Dvorak,"
            echo "                      DvorakLeft, DvorakRight, QWERTY, Colemak,"
            echo "                      or 'skip' to leave the sim's setting."
            echo "  DEVICE_TYPE_NAME    Override sim device (default: '$DEVICE_TYPE_NAME')."
            echo "  SIMULATOR_NAME      Override sim instance name."
            echo "  APP_BUNDLE          Override the .app path."
            echo "  LOG_FILE            Where to capture yetty stderr/ytrace."
            echo "                      Default: \$PROJECT_ROOT/tmp/tvos_yetty.log"
            echo "  MONITOR_INTERVAL    Seconds between health-check ticks (default 10)."
            echo "  MONITOR_ITERATIONS  Number of ticks before the monitor exits."
            echo "                      Default: 0 = forever (until yetty dies / Ctrl-C)."
            exit 0
            ;;
    esac

    check_macos
    check_xcode
    find_or_create_simulator
    boot_simulator
    set_keyboard_layout
    install_and_run
    monitor_yetty
}

main "$@"
