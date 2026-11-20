#!/usr/bin/env bash
# One command from source to MCX360Edition.ipa, on a Mac.
#
#   REXGLUE_SDK=/path/to/patched/rexglue-sdk scripts/build-ipa.sh
#
# Builds every title update core in app/cores/ whose game files are present
# (see app/cores/README.md), the launcher, and the .ipa. Optional:
#   MC360_CORES      which cores, in menu order, e.g. "tu9;tu14" (default: all)
#   MC360_BUNDLE_ID  bundle identifier (default: com.example.mcx360)
#   OUT              where to write the .ipa (default: ./MCX360Edition.ipa)
#   MC360_SKIP_TESTS=1  skip the launcher tests run first
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
APP="$ROOT/app"
PRESET=ios-arm64-release

die() { echo "error: $*" >&2; exit 1; }

[ "$(uname -s)" = "Darwin" ] || die "iPhone apps can only be built on a Mac (Xcode's iOS SDK)."
command -v xcrun >/dev/null || die "Xcode command line tools not found. Install Xcode, then run: xcode-select --install"
command -v cmake >/dev/null || die "cmake not found (brew install cmake)."
command -v ninja >/dev/null || die "ninja not found (brew install ninja)."
: "${REXGLUE_SDK:?Set REXGLUE_SDK to your patched rexglue-sdk checkout (see sdk/rexglue-sdk-ios.patch).}"

# What app/CMakePresets.json expects from the SDK.
HOST_TOOL="${REXGLUE_HOST_TOOL:-$REXGLUE_SDK/out/mac-arm64/Release/rexglue}"
INSTALL="$REXGLUE_SDK/out/install/ios-arm64"
LIBS="$REXGLUE_SDK/out/ios-arm64/Release"
[ -x "$HOST_TOOL" ] || die "Host codegen tool not found at $HOST_TOOL. Build the SDK for macOS first."
[ -d "$INSTALL" ] || die "iOS SDK install not found at $INSTALL. Build and install the SDK for iOS first."
[ -f "$LIBS/librexruntime.dylib" ] || die "iOS runtime not found at $LIBS/librexruntime.dylib."
# The launcher needs ReXApp::SetImageInfo, added to the SDK patch with it.
if ! grep -q "SetImageInfo" "$INSTALL/include/rex/rex_app.h" 2>/dev/null; then
  die "The installed SDK predates the launcher (no ReXApp::SetImageInfo in $INSTALL/include/rex/rex_app.h).
  Apply the current sdk/rexglue-sdk-ios.patch to a clean checkout, rebuild and reinstall the SDK for iOS."
fi

# At least TU9's files, or another core's.
found=0
for core in "$APP"/cores/*/; do
  [ -f "$core/core.cmake" ] || continue
  xex="$(sed -n 's/^file_path *= *"\(.*\)"/\1/p' "$core/manifest.toml" | head -1)"
  [ -n "$xex" ] && [ -f "$core/$xex" ] && found=1
done
[ "$found" = 1 ] || die "No game files to build from. Put your disc's files in app/game/ and TU9's
  default.xexp next to them (see app/game/PUT_YOUR_GAME_FILES_HERE.md)."

EXTRA=()
[ -n "${MC360_CORES:-}" ] && EXTRA+=("-DMC360_CORES=$MC360_CORES")
[ -n "${MC360_BUNDLE_ID:-}" ] && EXTRA+=("-DMC360_BUNDLE_ID=$MC360_BUNDLE_ID")

# The launcher's own tests (seconds; synthetic data). MC360_SKIP_TESTS=1 skips.
if [ "${MC360_SKIP_TESTS:-0}" != 1 ]; then
  echo "==> Testing the launcher and importer"
  cmake -S "$APP/tests" -B "$APP/out/tests" -G Ninja -DREXGLUE_SDK="$REXGLUE_SDK" >/dev/null
  cmake --build "$APP/out/tests"
  ctest --test-dir "$APP/out/tests" --output-on-failure
fi

cd "$APP"
echo "==> Configuring (the first run recompiles each core's code; this takes a while)"
cmake --preset "$PRESET" ${EXTRA[@]+"${EXTRA[@]}"}
echo "==> Building"
cmake --build --preset "$PRESET"
echo "==> Packaging"
OUT="${OUT:-$ROOT/MCX360Edition.ipa}" bash "$HERE/make-ipa.sh"
