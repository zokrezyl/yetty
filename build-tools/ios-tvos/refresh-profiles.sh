#!/bin/bash
# Drive Xcode (via xcodebuild) to fetch & install Apple Development
# provisioning profiles for the stub apps under this directory.
#
# Profiles land in ~/Library/Developer/Xcode/UserData/Provisioning Profiles/
# and the codesign step for the real apps (yetty.app, YettyQemu.app) just
# splices the matching one in as embedded.mobileprovision.
#
# Stubs (one per <platform>/<app>/ directory containing a project.yml):
#   tvos/qemu/  →  com.yetty.qemu  (tvOS)
#   ios/qemu/   →  com.yetty.qemu  (iOS)
#
# yetty.app's own bundle id (com.yetty.terminal) already has a profile
# from earlier work; if it's missing or stale, drop a similar stub under
# tvos/yetty/ or ios/yetty/ and it'll be picked up automatically.
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

if ! command -v xcodegen >/dev/null 2>&1; then
    echo "error: xcodegen not on PATH (brew install xcodegen)" >&2
    exit 1
fi

# Discover stubs by globbing <platform>/<app>/project.yml. Each match's
# parent directory is one stub; the first path component is the platform,
# which selects the xcodebuild -destination.
shopt -s nullglob
for proj_yml in "$SCRIPT_DIR"/{ios,tvos}/*/project.yml; do
    stub_dir="$(dirname "$proj_yml")"
    rel="${stub_dir#$SCRIPT_DIR/}"            # e.g. tvos/qemu
    platform="${rel%%/*}"                      # ios | tvos
    stub_name="${rel//\//-}"                   # tvos-qemu  (for DERIVED path)

    case "$platform" in
        ios)  destination="generic/platform=iOS"  ;;
        tvos) destination="generic/platform=tvOS" ;;
        *)    echo "skip: unknown platform '$platform' for $rel" >&2; continue ;;
    esac

    echo "==> $rel ($platform)"
    (
        cd "$stub_dir"
        xcodegen generate --quiet

        PROJ="$(ls -d ./*.xcodeproj | head -1)"
        SCHEME="${PROJ%.xcodeproj}"
        SCHEME="${SCHEME#./}"

        # -allowProvisioningUpdates lets Xcode talk to the developer portal
        # to create / refresh / download the profile;
        # -allowProvisioningDeviceRegistration covers the first time a
        # paired device gets added to the team's device list.
        xcodebuild \
            -project "$PROJ" \
            -scheme "$SCHEME" \
            -destination "$destination" \
            -derivedDataPath "$DERIVED/$stub_name" \
            -allowProvisioningUpdates \
            -allowProvisioningDeviceRegistration \
            DEVELOPMENT_TEAM="$TEAM_ID" \
            build 2>&1 | tail -20
    )
done

echo ""
echo "Profiles now in ~/Library/Developer/Xcode/UserData/Provisioning Profiles/:"
ls -la ~/Library/Developer/Xcode/UserData/Provisioning\ Profiles/
