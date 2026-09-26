#!/usr/bin/env bash
# Package the built app as an .ipa for sideloading (AltStore, SideStore,
# Sideloadly, TrollStore). Those tools sign it with your own Apple ID.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
APP="${APP:-$HERE/../app/out/build/ios-arm64-release/mc360.app}"
OUT="${OUT:-$HERE/../MCX360Edition.ipa}"
ENTITLEMENTS="${ENTITLEMENTS:-$HERE/../app/platform/ios/mc360.entitlements}"

[ -d "$APP" ] || { echo "Built app not found at $APP - run the build first." >&2; exit 1; }
[ -f "$ENTITLEMENTS" ] || { echo "Entitlements not found at $ENTITLEMENTS." >&2; exit 1; }

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/Payload"
cp -R "$APP" "$STAGE/Payload/"
BUNDLE="$STAGE/Payload/$(basename "$APP")"

# Strip anything that identifies the builder: a provisioning profile lists the
# team, name and device UDIDs, and a signature names the certificate.
rm -f "$BUNDLE/embedded.mobileprovision"
rm -rf "$BUNDLE/_CodeSignature"

# Re-sign ad hoc. An ad hoc signature names no certificate or team, but it does
# carry the entitlements the app asks for, and sideloading tools carry those
# over into their own signature. That is how the app requests extended virtual
# addressing, which 3 GB devices need: TrollStore, jailbroken installs and paid
# developer accounts grant it, free Apple IDs drop it.
for d in "$BUNDLE"/Frameworks/*.dylib; do codesign -f -s - "$d"; done
codesign -f -s - --entitlements "$ENTITLEMENTS" "$BUNDLE"

rm -f "$OUT"
(cd "$STAGE" && zip -qry "$OUT" Payload)
echo "Wrote $OUT"
shasum -a 256 "$OUT"
