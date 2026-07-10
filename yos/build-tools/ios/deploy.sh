#!/usr/bin/env bash
# deploy.sh — run the cross-built yos host binary inside a booted
# iPhone Simulator and expose its wasm telnetd on localhost.
#
# Steps:
#   1. Make sure setup + build are current (cross-compiled yos exists).
#   2. Pick (or boot) a simulator device.
#   3. xcrun simctl spawn that device with the cross-built yos in
#      --server mode, runsvdir-supervising runtime/runit/.
#   4. Tell the user where to telnet.
#
# Filesystem note: the iOS simulator is a host-process namespace, not
# a VM — so the cross-built yos sees the same paths as we do. We don't
# need to copy anything into the simulator container; we just point
# yos at the existing nix `result/` umbrella.
#
# Network note: the simulator shares the host's loopback. A wasm
# telnetd binding 0.0.0.0:12323 inside yos is reachable from the host
# as `telnet localhost 12323`.

set -euo pipefail

cd "$(dirname "$0")/../.."
REPO="$PWD"

# ── arguments / defaults ──────────────────────────────────────────────
PORT="${YOS_IOS_TELNET_PORT:-12323}"
DEVICE_NAME="${YOS_IOS_DEVICE:-}"   # e.g. "iPhone 16", "" = pick first booted/available
LOG_DIR="${YOS_IOS_LOG_DIR:-$REPO/runtime/logs/ios}"

BIN="$REPO/build-ios-sim-x86_64/src/yos/yos"
ALL_LINK="$REPO/result"          # nix umbrella symlink (built by tools/yos.sh or `nix build .#all`)
SVCDIR="$REPO/runtime/runit"

# ── preflight ─────────────────────────────────────────────────────────
if [ ! -x "$BIN" ]; then
    echo "[deploy] $BIN not found. Run build-tools/ios/setup.sh + meson compile first." >&2
    echo "[deploy]   ./build-tools/ios/setup.sh" >&2
    echo "[deploy]   meson compile -C build-ios-sim-x86_64 src/yos/yos" >&2
    exit 1
fi
if [ ! -e "$ALL_LINK/libexec" ]; then
    echo "[deploy] $ALL_LINK/libexec not found. Build the umbrella first:" >&2
    echo "[deploy]   nix build .#all" >&2
    exit 1
fi
if [ ! -d "$SVCDIR" ]; then
    echo "[deploy] $SVCDIR missing — runtime/runit/ holds the runsv service tree." >&2
    exit 1
fi
mkdir -p "$LOG_DIR"

# Confirm the binary is actually for the simulator.
case "$(file -b "$BIN")" in
    *Mach-O*x86_64*) : ;;
    *) echo "[deploy] $BIN doesn't look like a Mach-O x86_64 binary." >&2; exit 1 ;;
esac

# ── pick a device ─────────────────────────────────────────────────────
pick_device() {
    # Prefer a currently-booted one; otherwise pick the named device or
    # the first available iPhone.
    local booted
    booted=$(xcrun simctl list devices booted -j 2>/dev/null \
             | /usr/bin/python3 -c 'import sys, json; d=json.load(sys.stdin)["devices"]; \
udids=[dev["udid"] for ds in d.values() for dev in ds if dev.get("state")=="Booted"]; \
print(udids[0] if udids else "")') || booted=""
    if [ -n "$booted" ]; then echo "$booted"; return 0; fi

    if [ -n "$DEVICE_NAME" ]; then
        xcrun simctl list devices available -j \
            | PICK_NAME="$DEVICE_NAME" /usr/bin/python3 -c '
import sys, json, os
d = json.load(sys.stdin)["devices"]
want = os.environ.get("PICK_NAME","")
for runtime, devices in d.items():
    for dev in devices:
        if dev.get("name") == want and dev.get("isAvailable", True):
            print(dev["udid"])
            sys.exit(0)
print("", end="")
' || true
        return 0
    fi

    # First available iPhone, any iOS runtime.
    xcrun simctl list devices available -j \
        | /usr/bin/python3 -c '
import sys, json
d = json.load(sys.stdin)["devices"]
for runtime, devices in d.items():
    if "iOS" not in runtime: continue
    for dev in devices:
        name = dev.get("name", "")
        if name.startswith("iPhone") and dev.get("isAvailable", True):
            print(dev["udid"])
            sys.exit(0)
print("", end="")
' || true
}

UDID="$(pick_device)"
if [ -z "$UDID" ]; then
    echo "[deploy] no usable iOS simulator device found." >&2
    echo "[deploy] open Xcode → Window → Devices and Simulators to add one." >&2
    exit 1
fi

device_field() {
    local field="$1"
    xcrun simctl list devices -j | U="$UDID" F="$field" /usr/bin/python3 -c '
import sys, json, os
d = json.load(sys.stdin)["devices"]
udid = os.environ["U"]
field = os.environ["F"]
for ds in d.values():
    for dev in ds:
        if dev["udid"] == udid:
            print(dev.get(field, ""))
            sys.exit(0)
'
}
STATE=$(device_field state)
NAME=$(device_field name)

echo "[deploy] device : $NAME ($UDID) — state=$STATE"

if [ "$STATE" != "Booted" ]; then
    echo "[deploy] booting…"
    xcrun simctl boot "$UDID"
fi

# Open Simulator.app pointing at our device so the user gets the GUI
# window. `simctl boot` only starts the device's services (launchd_sim
# etc.) — it does NOT open the app. Without this the device runs but
# you only ever see it via `simctl list`. Idempotent: if Simulator.app
# is already up, `open -a` just brings it forward.
if ! pgrep -x Simulator >/dev/null 2>&1; then
    echo "[deploy] opening Simulator.app GUI…"
    open -a Simulator --args -CurrentDeviceUDID "$UDID" || true
fi

# ── spawn yos ─────────────────────────────────────────────────────────
echo "[deploy] umbrella libexec : $ALL_LINK/libexec"
echo "[deploy] runit svcdir     : $SVCDIR"
echo "[deploy] log dir          : $LOG_DIR"
echo "[deploy] telnet port      : $PORT"
echo
echo "[deploy] spawning yos --server inside the simulator…"
echo "[deploy] (Ctrl-C here = kill the spawned process)"
echo

# Resolve the symlink — simctl spawn doesn't follow per-process symlinks
# reliably across SDK versions, and the wasm tools live under the
# nix store path that result/ points at.
ALL="$(readlink -f "$ALL_LINK" 2>/dev/null || /usr/bin/python3 -c "import os, sys; print(os.path.realpath(sys.argv[1]))" "$ALL_LINK")"

# simctl spawn runs an absolute-path simulator-target binary as a
# child of the simulator's launchd. Env propagation has two quirks
# that bit us in earlier revisions:
#
#   1. `simctl spawn` ignores most calling-shell env vars. The
#      documented escape hatch is to set them with the SIMCTL_CHILD_
#      prefix; simctl strips the prefix and forwards the value.
#
#   2. PATH is special: simctl OVERWRITES PATH inside the spawned
#      process with its own simulator-RuntimeRoot value, even when
#      SIMCTL_CHILD_PATH is set. So we tunnel the real path through
#      a different name (YOS_PATH) and yos's main.c does
#      setenv("PATH", $YOS_PATH, 1) before initializing — see the
#      early-env-override block at the top of main().
env \
    SIMCTL_CHILD_YOS_PATH="$ALL/libexec:/usr/bin:/bin" \
    SIMCTL_CHILD_YOS_LIBEXEC="$ALL/libexec" \
    SIMCTL_CHILD_LOG_DIR="$LOG_DIR" \
    SIMCTL_CHILD_YTRACE_DEFAULT_ON=no \
    xcrun simctl spawn "$UDID" \
        "$BIN" \
            --server \
            --log-dir "$LOG_DIR" \
            "$ALL/libexec/runsvdir" -P "$SVCDIR" &

SPAWN_PID=$!
# Give yos a couple of seconds to bind the listener before we tell the
# user to connect; nothing fancier than that.
/bin/sleep 2

cat <<EOF

[deploy] yos running (host pid $SPAWN_PID). Connect with:

    telnet localhost $PORT

Stop the daemon with: kill $SPAWN_PID   (or Ctrl-C in this shell)

Logs land under: $LOG_DIR
EOF

wait "$SPAWN_PID"
