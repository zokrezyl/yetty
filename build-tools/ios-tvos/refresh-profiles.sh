#!/bin/bash
# Drive Xcode (via xcodebuild) to fetch & install Apple Development
# provisioning profiles for the stub apps under this directory.
#
# Profiles land in ~/Library/Developer/Xcode/UserData/Provisioning Profiles/
# and the codesign step for the real apps (yetty.app, YettyQemu.app) just
# splices the matching one in as embedded.mobileprovision.
#
# Stubs:
#   yetty-qemu-stub/  →  com.yetty.qemu  (the side-by-side qemu app)
#
# yetty.app's own bundle id (com.yetty.terminal) already has a profile
# from earlier work; if it's missing or stale, drop a yetty-stub/ next
# to yetty-qemu-stub/ with the same shape and add it to STUBS below.
#
# Requires:
#   - Xcode + command-line tools
#   - xcodegen on PATH (brew install xcodegen)
#   - The user's Apple ID logged in to Xcode (Settings > Accounts)
#   - Device registered to the team (Window > Devices & Simulators).
#     -allowProvisioningDeviceRegistration handles first-time registration
#     for free / personal teams.

set -Eeuo pipefail
trap 'rc=$?; echo "FAILED: rc=$rc line=$LINENO" >&2' ERR

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEAM_ID="${TEAM_ID:-R2XJ745TS6}"
DERIVED="$SCRIPT_DIR/.build"
mkdir -p "$DERIVED"

STUBS=( yetty-qemu )

for stub in "${STUBS[@]}"; do
    echo "==> $stub"
    cd "$SCRIPT_DIR/$stub"

    if ! command -v xcodegen >/dev/null 2>&1; then
        echo "error: xcodegen not on PATH (brew install xcodegen)" >&2
        exit 1
    fi
    xcodegen generate --quiet

    PROJ="$(ls -d *.xcodeproj | head -1)"
    SCHEME="${PROJ%.xcodeproj}"

    # Build for a generic tvOS device. -allowProvisioningUpdates lets Xcode
    # talk to the developer portal to create / refresh / download the
    # profile; -allowProvisioningDeviceRegistration covers the first time
    # the paired Apple TV gets added to the team's device list.
    xcodebuild \
        -project "$PROJ" \
        -scheme "$SCHEME" \
        -destination "generic/platform=tvOS" \
        -derivedDataPath "$DERIVED/$stub" \
        -allowProvisioningUpdates \
        -allowProvisioningDeviceRegistration \
        DEVELOPMENT_TEAM="$TEAM_ID" \
        build 2>&1 | tail -20
done

echo ""
echo "Profiles now in ~/Library/Developer/Xcode/UserData/Provisioning Profiles/:"
ls -la ~/Library/Developer/Xcode/UserData/Provisioning\ Profiles/
