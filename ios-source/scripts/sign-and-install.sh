#!/usr/bin/env bash
# Sign the built app with your own development certificate and install it on a
# connected device. Most people should use make-ipa.sh + a sideloading tool
# instead; this is for developers with a provisioning profile.
#
# Required:
#   PROFILE   path to a .mobileprovision that covers your bundle ID and device
# Optional:
#   IDENTITY  codesigning identity (default: first "Apple Development" identity)
#   DEVICE    device identifier from `xcrun devicectl list devices` (default: first)
#   APP       path to the built .app
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
APP="${APP:-$HERE/../app/out/build/ios-arm64-release/mc360.app}"
: "${PROFILE:?Set PROFILE to your .mobileprovision}"
IDENTITY="${IDENTITY:-$(security find-identity -v -p codesigning | grep -o '"Apple Development:[^"]*"' | head -1 | tr -d '"')}"
[ -n "$IDENTITY" ] || { echo "No Apple Development signing identity found." >&2; exit 1; }
[ -d "$APP" ] || { echo "Built app not found at $APP - run the build first." >&2; exit 1; }

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# Entitlements come from the profile itself, so they always match it.
security cms -D -i "$PROFILE" > "$WORK/profile.plist"
/usr/libexec/PlistBuddy -x -c "Print :Entitlements" "$WORK/profile.plist" > "$WORK/entitlements.plist"

cp "$PROFILE" "$APP/embedded.mobileprovision"
for d in "$APP"/Frameworks/*.dylib; do codesign -f -s "$IDENTITY" "$d"; done
codesign -f -s "$IDENTITY" --entitlements "$WORK/entitlements.plist" "$APP"

if [ -z "${DEVICE:-}" ]; then
  DEVICE="$(xcrun devicectl list devices 2>/dev/null | awk 'NR>2 && $0 ~ /available|connected/ {for(i=1;i<=NF;i++) if ($i ~ /^[0-9A-F-]{25,}$/) {print $i; exit}}')"
fi
[ -n "$DEVICE" ] || { echo "No device found; set DEVICE." >&2; exit 1; }
xcrun devicectl device install app --device "$DEVICE" "$APP"
