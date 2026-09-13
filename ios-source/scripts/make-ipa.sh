#!/usr/bin/env bash
# Package the built app as an unsigned .ipa for sideloading (AltStore, SideStore,
# Sideloadly, TrollStore). Those tools sign it with your own Apple ID.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
APP="${APP:-$HERE/../app/out/build/ios-arm64-release/mc360.app}"
OUT="${OUT:-$HERE/../MCX360Edition.ipa}"

[ -d "$APP" ] || { echo "Built app not found at $APP - run the build first." >&2; exit 1; }

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/Payload"
cp -R "$APP" "$STAGE/Payload/"
BUNDLE="$STAGE/Payload/$(basename "$APP")"

# Strip anything that identifies the builder: a provisioning profile lists the
# team, name and device UDIDs, and a signature names the certificate.
rm -f "$BUNDLE/embedded.mobileprovision"
for d in "$BUNDLE"/Frameworks/*.dylib; do codesign --remove-signature "$d" 2>/dev/null || true; done
codesign --remove-signature "$BUNDLE" 2>/dev/null || true
rm -rf "$BUNDLE/_CodeSignature"

rm -f "$OUT"
(cd "$STAGE" && zip -qry "$OUT" Payload)
echo "Wrote $OUT"
shasum -a 256 "$OUT"
