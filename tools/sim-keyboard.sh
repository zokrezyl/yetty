#!/usr/bin/env bash
# Set the iOS / tvOS Simulator's hardware-keyboard layout.
#
# Why: the Simulator's "Hardware Keyboard" defaults to U.S. QWERTY regardless
# of the host's keyboard layout. iOS reads HID scan codes from the host and
# translates them through ITS layout — so a Dvorak-on-host user pressing
# the physical T key gets `t` in yetty (US-QWERTY translation), not `k`
# (Dvorak translation).
#
# Yetty's pressesBegan: reads UIKey.characters, which is post-layout — once
# the Simulator's layout is right, yetty does the right thing.
#
# IMPORTANT: This script REQUIRES a fully-booted sim (PineBoard / TVHome up)
# and writes the keyboard prefs via `simctl spawn defaults write`. We tried
# the alternative of shutdown + edit .GlobalPreferences.plist + reboot — it
# is FASTER but corrupts the sim's data store on tvOS: the next data
# migration crashes PineBoard with status -6, and the sim becomes unusable
# (only black screen, simctl launch returns "system shell probably crashed").
# So we accept the slower-but-safe post-boot route here.
#
# Usage:
#   tools/sim-keyboard.sh [layout] [device]
#     layout  one of: Dvorak | DvorakLeft | DvorakRight | QWERTY (default Dvorak)
#     device  Simulator device name or UDID (default: "yetty-simulator")
#
#   Examples:
#     tools/sim-keyboard.sh                            # Dvorak on yetty-simulator
#     tools/sim-keyboard.sh QWERTY                    # back to US QWERTY
#     tools/sim-keyboard.sh Dvorak "iPhone 16"
#     tools/sim-keyboard.sh Dvorak "Apple TV 4K (3rd generation) (at 1080p)"

set -euo pipefail

LAYOUT="${1:-Dvorak}"
DEVICE="${2:-yetty-simulator}"

case "$LAYOUT" in
    Dvorak|DvorakLeft|DvorakRight|QWERTY) ;;
    *)
        echo "error: unknown layout '$LAYOUT' (use Dvorak, DvorakLeft, DvorakRight, or QWERTY)" >&2
        exit 2
        ;;
esac

UDID="$(xcrun simctl list devices -j 2>/dev/null \
    | python3 -c "
import sys, json
d = json.load(sys.stdin)
target = '$DEVICE'
for runtime, devs in d['devices'].items():
    for dev in devs:
        if dev.get('name') == target or dev.get('udid') == target:
            print(dev['udid']); sys.exit(0)
sys.exit(1)
" )" || { echo "error: device '$DEVICE' not found" >&2; exit 1; }

LAYOUT_ID="en_US@hw=$LAYOUT;sw=$LAYOUT"
echo "device: $DEVICE ($UDID)"
echo "layout: $LAYOUT_ID"

# Fast path: query the live pref via simctl spawn — if the sim already has
# what we want we skip the writes entirely.
if CURRENT="$(xcrun simctl spawn "$UDID" defaults read -g KeyboardLastUsedKeyboard 2>/dev/null)" \
        && [ "$CURRENT" = "$LAYOUT_ID" ]; then
    echo "already set — nothing to do"
    exit 0
fi

# Make sure the sim is actually ready for `defaults write` — needs launchd
# in the sim to spawn user processes. If state isn't Booted, we can't write.
state="$(xcrun simctl list devices -j 2>/dev/null | python3 -c "
import sys, json
d = json.load(sys.stdin)
for runtime, devs in d['devices'].items():
    for dev in devs:
        if dev['udid'] == '$UDID':
            print(dev.get('state','Unknown')); sys.exit(0)
" 2>/dev/null)"
if [ "$state" != "Booted" ]; then
    echo "error: sim not booted (state=$state) — boot it first (and let System App come up) before setting keyboard layout" >&2
    exit 1
fi

# Wrap each spawn in a 30s timeout: if launchd in the sim is wedged we want
# to fail loud, not hang forever.
spawn_with_timeout() {
    ( xcrun simctl spawn "$UDID" "$@" 2>&1 &
      P=$!
      for i in $(seq 1 30); do sleep 1; kill -0 $P 2>/dev/null || { wait $P; return $?; }; done
      kill -9 $P 2>/dev/null
      echo "timeout waiting for: $*" >&2
      return 124
    )
}

echo "writing keyboard prefs via defaults write..."
spawn_with_timeout defaults write -g AppleKeyboards -array "$LAYOUT_ID" >/dev/null
spawn_with_timeout defaults write -g KeyboardLastUsedKeyboards -array "$LAYOUT_ID" >/dev/null
spawn_with_timeout defaults write -g KeyboardLastUsedKeyboard "$LAYOUT_ID" >/dev/null
# Do NOT write AppleKeyboardsExpanded here. We previously wrote it as
# `-dict "$LAYOUT_ID" 1`, which iOS's TIPreferencesController later read
# back and called `boolValue` on, crashing the keyboard subsystem with
# NSInvalidArgumentException (`-[__NSSingleEntryDictionaryI boolValue]:
# unrecognized selector`). The three writes above are sufficient to set
# the active keyboard layout — yetty doesn't open the system picker.
# Defensively scrub a stale corrupt value if it's still in the sim's
# global plist from earlier runs.
spawn_with_timeout defaults delete -g AppleKeyboardsExpanded >/dev/null 2>&1 || true

# Verify
CURRENT="$(xcrun simctl spawn "$UDID" defaults read -g KeyboardLastUsedKeyboard 2>/dev/null || true)"
if [ "$CURRENT" = "$LAYOUT_ID" ]; then
    echo "ok: KeyboardLastUsedKeyboard = $CURRENT"
else
    echo "warn: post-write read returned '$CURRENT' (expected '$LAYOUT_ID')" >&2
fi

echo "done. Connect Hardware Keyboard must be ON in the simulator menubar"
echo "(I/O > Keyboard > Connect Hardware Keyboard, or ⌘K)."
