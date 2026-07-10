#!/usr/bin/env bash
# deploy.sh — assemble + sign + install + launch the yos tvOS bundle
# on a paired Apple TV.
#
#   ./build-tools/tvos/deploy.sh
#
# Pipeline:
#   1. (If needed) run setup.sh + meson compile to make libyos.a.
#   2. Link launcher.m + libyos.a → Mach-O executable.
#   3. Assemble <build>/yos.app/ with the executable + Info.plist +
#      bundled libexec/*.wasm + runit/ service tree.
#   4. codesign the bundle against the user's Apple Development cert.
#   5. xcrun devicectl device install app → launch.
#   6. Print the device hostname + telnet command.
#
# Provisioning profile: this script attempts to codesign without one
# (--entitlements only). For real-device install Apple typically wants
# an embedded.mobileprovision listing the device UDID. If devicectl
# rejects the install, the script prints the exact fix steps.

set -euo pipefail

cd "$(dirname "$0")/../.."
REPO="$PWD"

DEVICE_NAME="${YOS_TVOS_DEVICE:-Entertainment Room}"
DEVICE_UDID="${YOS_TVOS_UDID:-}"
BUNDLE_ID="local.yos.tvos"

BUILD_DIR="$REPO/build-tvos-arm64"
LIBYOS="$BUILD_DIR/src/yos/libyos.a"
BUNDLE_DIR="$BUILD_DIR/bundle/yos.app"
ALL_LINK="$REPO/result"

LIBYOS_BRIDGE="$BUILD_DIR/src/yos/codegen/libyos_api_bridge.a"
LIBWASM3="$BUILD_DIR/src/wasm3/libwasm3.a"

# ── preflight ─────────────────────────────────────────────────────────
if [ ! -f "$LIBYOS" ] || [ ! -f "$LIBYOS_BRIDGE" ] || [ ! -f "$LIBWASM3" ]; then
    echo "[tvos] static libs missing — running setup + compile…"
    "$REPO/build-tools/tvos/setup.sh"
    # shellcheck disable=SC1091
    [ -f "$BUILD_DIR/.yos_env" ] && source "$BUILD_DIR/.yos_env"
    meson compile -C "$BUILD_DIR"
fi
# shellcheck disable=SC1091
[ -f "$BUILD_DIR/.yos_env" ] && source "$BUILD_DIR/.yos_env"

if [ ! -e "$ALL_LINK/libexec" ]; then
    echo "[tvos] $ALL_LINK/libexec not found. Build the umbrella first:" >&2
    echo "[tvos]   nix build .#all" >&2
    exit 1
fi
ALL="$(readlink -f "$ALL_LINK" 2>/dev/null || /usr/bin/python3 -c "import os, sys; print(os.path.realpath(sys.argv[1]))" "$ALL_LINK")"

# ── identify device ───────────────────────────────────────────────────
if [ -z "$DEVICE_UDID" ]; then
    DEVICE_UDID=$(xcrun devicectl list devices -j /tmp/.yos_tvos_devices.json >/dev/null 2>&1; \
        DEVICE_NAME="$DEVICE_NAME" /usr/bin/python3 -c '
import json, os, sys
want = os.environ.get("DEVICE_NAME","")
data = json.load(open("/tmp/.yos_tvos_devices.json"))
for d in data.get("result", {}).get("devices", []):
    name = d.get("deviceProperties", {}).get("name", "")
    if name == want:
        print(d["identifier"]); sys.exit(0)
print("", end="")
')
fi
if [ -z "$DEVICE_UDID" ]; then
    echo "[tvos] no Apple TV named '$DEVICE_NAME' found. Available:" >&2
    xcrun devicectl list devices 2>&1 | head -10 >&2
    echo "[tvos] override with YOS_TVOS_UDID=<udid> or YOS_TVOS_DEVICE='<name>'" >&2
    exit 1
fi
echo "[tvos] device : $DEVICE_NAME ($DEVICE_UDID)"

# CoreDevice's UDID (45F5A68-...) is NOT the value provisioning profiles
# encode under ProvisionedDevices. That field uses the device's classic
# hardware UDID, which on modern Apple TVs is the 40-char SHA prefix of
# the .coredevice.local hostname (matches what xcodebuild reports as
# device id). Pull it out of the hostname now so the profile-matcher
# below can compare apples to apples.
DEVICE_HW_UDID=$(xcrun devicectl list devices -j /tmp/.yos_tvos_devices.json >/dev/null 2>&1; \
    DEVICE_UDID="$DEVICE_UDID" /usr/bin/python3 -c '
import json, os, sys, re
udid = os.environ["DEVICE_UDID"]
data = json.load(open("/tmp/.yos_tvos_devices.json"))
for d in data.get("result", {}).get("devices", []):
    if d["identifier"] != udid: continue
    for hn in d.get("connectionProperties", {}).get("potentialHostnames", []):
        m = re.match(r"^([0-9a-f]{40})\.coredevice\.local$", hn)
        if m: print(m.group(1)); sys.exit(0)
print("", end="")
')
if [ -z "$DEVICE_HW_UDID" ]; then
    echo "[tvos] couldn't derive hardware UDID from hostname; falling back" >&2
    DEVICE_HW_UDID=$(echo "$DEVICE_UDID" | tr -d '-' | tr 'A-Z' 'a-z')
fi
echo "[tvos] hw-udid: $DEVICE_HW_UDID"

# ── identify signing cert + team ──────────────────────────────────────
SIGNING_LINE=$(security find-identity -v -p codesigning 2>/dev/null \
                | awk '/Apple Development/ {print; exit}')
if [ -z "$SIGNING_LINE" ]; then
    echo "[tvos] no Apple Development codesigning identity found." >&2
    echo "[tvos] Open Xcode → Settings → Accounts → add your Apple ID." >&2
    exit 1
fi
SIGNING_ID=$(echo "$SIGNING_LINE" | awk -F'"' '{print $2}')
SIGNING_SHA=$(echo "$SIGNING_LINE" | awk '{print $2}')
# The parenthetical in the cert's CN is the cert serial/display id, NOT
# the team id — Personal Teams in particular show different values.
# The actual team id lives in the cert's OU field; read it directly.
TEAM_ID=$(security find-certificate -c "$SIGNING_ID" -p 2>/dev/null \
          | openssl x509 -noout -subject -nameopt sep_multiline,utf8 2>/dev/null \
          | awk -F= '/^[[:space:]]*OU=/ {print $2; exit}' | tr -d ' ')
if [ -z "$TEAM_ID" ]; then
    echo "[tvos] couldn't extract team id from cert OU: $SIGNING_ID" >&2
    exit 1
fi
echo "[tvos] cert   : $SIGNING_ID"
echo "[tvos] team   : $TEAM_ID"

# ── link the bundle executable ────────────────────────────────────────
SDK="$(xcrun --sdk appletvos --show-sdk-path)"
CLANG="$(xcrun --sdk appletvos --find clang)"

# Wipe any previous bundle. cp -RL from /nix/store brings in read-only
# files; a second deploy run would fail with EACCES on overwrite.
chmod -R u+w "$BUNDLE_DIR" 2>/dev/null || true
rm -rf "$BUNDLE_DIR"
mkdir -p "$BUNDLE_DIR"
EXEC="$BUNDLE_DIR/yos"

echo "[tvos] linking ${EXEC}…"
"$CLANG" \
    -arch arm64 \
    -target arm64-apple-tvos16.0 \
    -isysroot "$SDK" \
    -mtvos-version-min=16.0 \
    -ObjC \
    -fobjc-arc \
    -framework Foundation \
    -framework UIKit \
    -framework AVFoundation \
    "$REPO/build-tools/tvos/launcher.m" \
    -Wl,-force_load,"$LIBYOS" \
    "$LIBWASM3" \
    -liconv \
    -o "$EXEC"

# ── assemble bundle resources ─────────────────────────────────────────
INFO_PLIST="$BUNDLE_DIR/Info.plist"
cp "$REPO/build-tools/tvos/Info.plist" "$INFO_PLIST"

# libexec/*.wasm — the wasm tools the wasm runit pipeline invokes.
# Copy from the umbrella so they're shipped INSIDE the bundle and the
# device doesn't need any /nix path mapping.
mkdir -p "$BUNDLE_DIR/libexec"
cp -RL "$ALL/libexec/." "$BUNDLE_DIR/libexec/"

# runit/ service tree.
mkdir -p "$BUNDLE_DIR/runit"
cp -RL "$REPO/runtime/runit/." "$BUNDLE_DIR/runit/"
# Strip the gitignored supervise dirs (left over from --server runs).
find "$BUNDLE_DIR/runit" -type d -name supervise -exec rm -rf {} + 2>/dev/null || true

# ── embed a matching provisioning profile ─────────────────────────────
# devicectl install will reject the bundle without an
# embedded.mobileprovision that (a) was signed by this Apple Developer
# team, (b) lists the device UDID under ProvisionedDevices, and (c)
# has an application-identifier that matches our bundle ID (literal
# match or via a wildcard like TEAMID.*).
#
# Search ~/Library/MobileDevice/Provisioning Profiles/ for the best
# match. Bundle ID is encoded in entitlement application-identifier
# as <TEAM>.<bundle.id>. Wildcard profiles end in `.*`.
echo "[tvos] searching for matching provisioning profile…"
PROFILE_PATH=$(BUNDLE_ID="$BUNDLE_ID" TEAM_ID="$TEAM_ID" \
               DEVICE_UDID_LOWER="$DEVICE_HW_UDID" \
               /usr/bin/python3 - <<'PY'
import os, glob, subprocess, plistlib, sys
team   = os.environ["TEAM_ID"]
bid    = os.environ["BUNDLE_ID"]
udid_l = os.environ["DEVICE_UDID_LOWER"]
want_app_id_exact = f"{team}.{bid}"
want_app_id_wild  = f"{team}.*"
candidates = (
    glob.glob(os.path.expanduser(
        "~/Library/MobileDevice/Provisioning Profiles/*.mobileprovision"))
    + glob.glob(os.path.expanduser(
        "~/Library/Developer/Xcode/UserData/Provisioning Profiles/*.mobileprovision")))
best = None
for p in candidates:
    try:
        raw = subprocess.check_output(["security","cms","-D","-i",p],
                                      stderr=subprocess.DEVNULL)
        plist = plistlib.loads(raw)
    except Exception:
        continue
    if plist.get("TeamIdentifier", [""])[0] != team:
        continue
    devs = [d.lower() for d in plist.get("ProvisionedDevices", [])]
    if udid_l not in devs:
        continue
    app_id = plist.get("Entitlements", {}).get("application-identifier","")
    if app_id == want_app_id_exact:
        # Exact bundle-id match wins immediately.
        print(p); sys.exit(0)
    if app_id == want_app_id_wild and best is None:
        best = p
if best:
    print(best)
PY
)
if [ -z "$PROFILE_PATH" ]; then
    echo "[tvos] no matching provisioning profile found — running bootstrap…"
    if ! command -v xcodegen >/dev/null 2>&1; then
        echo "[tvos] xcodegen not on PATH. Install with: brew install xcodegen" >&2
        exit 1
    fi
    (
        cd "$REPO/build-tools/tvos/project"
        xcodegen generate --quiet
    )
    # `generic/platform=tvOS` is enough — xcodebuild's preflight reaches
    # the developer portal, creates/refreshes the profile, registers any
    # missing paired devices (via -allowProvisioningDeviceRegistration),
    # and drops the .mobileprovision into Xcode's profile dir. The
    # subsequent link/codesign that the stub build then attempts is
    # allowed to fail (e.g. when this script runs in a non-Aqua shell):
    # the profile is already on disk by that point. We ignore the
    # exit code and re-scan for the new profile right after.
    xcodebuild \
        -project "$REPO/build-tools/tvos/project/yos-tvos.xcodeproj" \
        -scheme yos \
        -destination "generic/platform=tvOS" \
        -allowProvisioningUpdates \
        -allowProvisioningDeviceRegistration \
        -derivedDataPath "$BUILD_DIR/bootstrap-derived" \
        DEVELOPMENT_TEAM="$TEAM_ID" \
        build 2>&1 | tail -25 || true
    # Re-scan for the freshly-downloaded profile.
    PROFILE_PATH=$(BUNDLE_ID="$BUNDLE_ID" TEAM_ID="$TEAM_ID" \
                   DEVICE_UDID_LOWER="$DEVICE_HW_UDID" \
                   /usr/bin/python3 - <<'PY'
import os, glob, subprocess, plistlib, sys
team   = os.environ["TEAM_ID"]
bid    = os.environ["BUNDLE_ID"]
udid_l = os.environ["DEVICE_UDID_LOWER"]
want_app_id_exact = f"{team}.{bid}"
want_app_id_wild  = f"{team}.*"
candidates = (
    glob.glob(os.path.expanduser(
        "~/Library/MobileDevice/Provisioning Profiles/*.mobileprovision"))
    + glob.glob(os.path.expanduser(
        "~/Library/Developer/Xcode/UserData/Provisioning Profiles/*.mobileprovision")))
best = None
for p in candidates:
    try:
        raw = subprocess.check_output(["security","cms","-D","-i",p],
                                      stderr=subprocess.DEVNULL)
        plist = plistlib.loads(raw)
    except Exception:
        continue
    if plist.get("TeamIdentifier", [""])[0] != team: continue
    devs = [d.lower() for d in plist.get("ProvisionedDevices", [])]
    if udid_l not in devs: continue
    app_id = plist.get("Entitlements", {}).get("application-identifier","")
    if app_id == want_app_id_exact:
        print(p); sys.exit(0)
    if app_id == want_app_id_wild and best is None: best = p
if best: print(best)
PY
)
    if [ -z "$PROFILE_PATH" ]; then
        echo "[tvos] bootstrap finished but still no matching profile." >&2
        exit 1
    fi
fi
echo "[tvos] profile : $PROFILE_PATH"
cp "$PROFILE_PATH" "$BUNDLE_DIR/embedded.mobileprovision"

# ── render entitlements with the user's team id ───────────────────────
# Pull application-identifier and team-identifier from the SAME profile
# so entitlements stay consistent — the device installer cross-checks
# them, and a mismatch produces yet another 0xe8008015.
ENT="$BUILD_DIR/bundle/entitlements.plist"
PROFILE_ENT_PLIST=$(security cms -D -i "$PROFILE_PATH" 2>/dev/null)
PROFILE_APP_ID=$(echo "$PROFILE_ENT_PLIST" \
    | /usr/libexec/PlistBuddy -c "Print :Entitlements:application-identifier" /dev/stdin)
PROFILE_TEAM=$(echo "$PROFILE_ENT_PLIST" \
    | /usr/libexec/PlistBuddy -c "Print :Entitlements:com.apple.developer.team-identifier" /dev/stdin)
# Wildcard profiles (TEAMID.*) need the literal bundle id substituted.
if [[ "$PROFILE_APP_ID" == *.\* ]]; then
    PROFILE_APP_ID="${TEAM_ID}.${BUNDLE_ID}"
fi
cat > "$ENT" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>application-identifier</key>
    <string>$PROFILE_APP_ID</string>
    <key>com.apple.developer.team-identifier</key>
    <string>$PROFILE_TEAM</string>
    <key>get-task-allow</key>
    <true/>
</dict>
</plist>
EOF

# ── code sign ─────────────────────────────────────────────────────────
echo "[tvos] codesigning…"
codesign --force --timestamp=none \
    --sign "$SIGNING_ID" \
    --entitlements "$ENT" \
    "$EXEC"
codesign --force --timestamp=none \
    --sign "$SIGNING_ID" \
    --entitlements "$ENT" \
    "$BUNDLE_DIR"

# ── install + launch ──────────────────────────────────────────────────
echo "[tvos] installing $BUNDLE_DIR onto ${DEVICE_NAME}…"
INSTALL_OUT=$(xcrun devicectl device install app \
                  --device "$DEVICE_UDID" "$BUNDLE_DIR" 2>&1) || INSTALL_RC=$?
INSTALL_RC=${INSTALL_RC:-0}
echo "$INSTALL_OUT"
if [ $INSTALL_RC -ne 0 ]; then
    echo
    echo "[tvos] install failed (rc=$INSTALL_RC). Most common cause:" >&2
    echo "[tvos]   missing provisioning profile for $BUNDLE_ID + device UDID." >&2
    echo "[tvos]   The simplest fix is to open Xcode ONCE:" >&2
    echo "[tvos]     1. File → New → Project → tvOS App, set bundle ID to $BUNDLE_ID" >&2
    echo "[tvos]     2. Signing & Capabilities → Team = $TEAM_ID, Automatic" >&2
    echo "[tvos]     3. Add the Apple TV under Window → Devices and Simulators" >&2
    echo "[tvos]     4. Build once. Xcode drops the profile in" >&2
    echo "[tvos]        ~/Library/MobileDevice/Provisioning Profiles/" >&2
    echo "[tvos]     5. Re-run this script." >&2
    exit $INSTALL_RC
fi

echo
echo "[tvos] launching with ytrace on…"
echo "[tvos] streaming console (NSLog + redirected stderr) from device"
echo "[tvos] — Ctrl-C this terminal to kill the launch."
echo
xcrun devicectl device process launch \
    --device "$DEVICE_UDID" \
    --console \
    --environment-variables '{"YTRACE_DEFAULT_ON":"yes"}' \
    "$BUNDLE_ID" &
LAUNCH_PID=$!

# ── tell the user where to telnet ─────────────────────────────────────
sleep 3
HOSTNAME=$(xcrun devicectl list devices -j /tmp/.yos_tvos_devices.json >/dev/null 2>&1; \
    DEVICE_UDID="$DEVICE_UDID" /usr/bin/python3 -c '
import json, os, sys
udid = os.environ["DEVICE_UDID"]
data = json.load(open("/tmp/.yos_tvos_devices.json"))
for d in data.get("result", {}).get("devices", []):
    if d["identifier"] == udid:
        hns = d.get("connectionProperties", {}).get("potentialHostnames", [])
        if hns: print(hns[0]); sys.exit(0)
print("", end="")
')

cat <<EOF

[tvos] yos launched on $DEVICE_NAME. Connect with:

    telnet ${HOSTNAME:-<device-hostname>} 12323

(Apple TV is on the local network; resolve $HOSTNAME via mDNS.
 If telnet hangs, the bundle's sandbox is probably blocking the
 listen socket — check the device console:
 xcrun devicectl device process view --device $DEVICE_UDID console)

Ctrl-C this terminal to stop streaming the device console.
EOF

wait "$LAUNCH_PID" 2>/dev/null || true
