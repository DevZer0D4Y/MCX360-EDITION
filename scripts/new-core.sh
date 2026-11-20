#!/usr/bin/env bash
# Sets up a new title update core in app/cores/<id>/, ready for its first
# build and test. See app/cores/README.md for what happens next.
#
#   scripts/new-core.sh <id> <title update> ["Name shown in the menu"]
#
#   <id>            folder name, e.g. tu14
#   <title update>  the title update package as downloaded (a CON/LIVE file),
#                   a folder extracted from it, or its default.xexp
#
# Needs your disc's default.xex in app/game/ and the host rexglue tool:
# REXGLUE_HOST_TOOL, or REXGLUE_SDK (uses $REXGLUE_SDK/out/mac-arm64/Release/rexglue).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
APP="$ROOT/app"

die() { echo "error: $*" >&2; exit 1; }

[ $# -ge 2 ] || die "usage: $0 <id> <title update package | folder | default.xexp> [\"Menu name\"]"
ID="$1"
SOURCE="$2"
NAME="${3:-}"
[[ "$ID" =~ ^[a-z0-9_]+$ ]] || die "id must be lowercase letters, digits or _ (e.g. tu14)."
CORE="$APP/cores/$ID"
[ ! -e "$CORE" ] || die "$CORE already exists."
[ -f "$APP/game/default.xex" ] || die "app/game/default.xex not found: put your disc's files there first."
[ -e "$SOURCE" ] || die "$SOURCE not found."

TOOL="${REXGLUE_HOST_TOOL:-${REXGLUE_SDK:+$REXGLUE_SDK/out/mac-arm64/Release/rexglue}}"
[ -n "$TOOL" ] && [ -x "$TOOL" ] || die "Set REXGLUE_HOST_TOOL (or REXGLUE_SDK) to find the host rexglue tool."

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# Find the title update's default.xexp.
if [ -d "$SOURCE" ]; then
  XEXP="$SOURCE/default.xexp"
elif [ "$(head -c 4 "$SOURCE")" = "CON " ] || [ "$(head -c 4 "$SOURCE")" = "LIVE" ] ||
     [ "$(head -c 4 "$SOURCE")" = "PIRS" ]; then
  echo "==> Unpacking the title update package"
  "$TOOL" dump "$SOURCE" -o "$WORK/tu" >/dev/null
  XEXP="$WORK/tu/default.xexp"
else
  XEXP="$SOURCE"
fi
[ -f "$XEXP" ] || die "No default.xexp found in $SOURCE."

mkdir -p "$CORE/game"
cp "$APP/game/default.xex" "$CORE/game/default.xex"
cp "$XEXP" "$CORE/game/default.xexp"

# Version, for the default menu name: major.minor.build.qfe from the header.
VERSION="$(python3 - "$CORE/game/default.xexp" <<'PY' || true
import struct, sys
d = open(sys.argv[1], 'rb').read(1 << 20)
n = struct.unpack_from('>I', d, 0x14)[0]
target = None
for i in range(min(n, 256)):
    key, value = struct.unpack_from('>II', d, 0x18 + i * 8)
    if key == 0x000005FF:
        target = struct.unpack_from('>I', d, value + 4)[0]
    elif key == 0x00040006 and target is None:
        target = struct.unpack_from('>I', d, value + 4)[0]
if target is not None:
    print(f"{target >> 28 & 15}.{target >> 24 & 15}.{target >> 8 & 0xFFFF}.{target & 255}")
PY
)"
[ -n "$NAME" ] || NAME="Title Update ${ID#tu}"

cat > "$CORE/core.cmake" <<EOF
# $NAME core${VERSION:+ (game version $VERSION)}. Read by cmake/mc360_cores.cmake.
# Folders on the phone: $ID/ (title update), userdata-$ID/ (saves), cache-$ID/.
set(CORE_NAME "$NAME")
EOF

cat > "$CORE/manifest.toml" <<'EOF'
# Codegen manifest for this core. Paths are relative to this file. Start from
# nothing TU-specific: every address in cores/tu9/manifest.toml is TU9's.

[project]
name = "mc360"
sdk_version = "0.10.0"
game_root = "game"

[entrypoint]
generate_exception_handlers = true
file_path = "game/default.xex"
out_directory_path = "generated"
includes = []

# TODO: the CRT's setjmp/longjmp in this executable (see cores/tu9/manifest.toml
# for why they matter). Without them the image loader's JPEG probe corrupts the
# stack.
# setjmp_address = 0x........
# longjmp_address = 0x........

[entrypoint.functions]
# Add an entry for every address the runtime traps on as an "invalid or
# unregistered function" while you test.
EOF

cat > "$CORE/hooks.cpp" <<EOF
// $NAME core: host replacements for guest functions.
//
// TODO: port TU9's two performance hooks (cores/tu9/hooks.cpp) to this title
// update's addresses: the PIX named-event function, and Direct3D's GPU
// spin-wait step. Without them it runs, but slower and hotter.

#include "mc360_init.h"
EOF

echo "==> Scanning for function pointers in data (vtables, callbacks)"
if (cd "$APP" && "$TOOL" codegen "$CORE/manifest.toml" --scan_code_pointers_to="$WORK/pointers.toml" >"$WORK/scan.log" 2>&1) &&
   [ -s "$WORK/pointers.toml" ]; then
  { echo; cat "$WORK/pointers.toml"; } >> "$CORE/manifest.toml"
  echo "    added $(grep -c '^0x' "$WORK/pointers.toml") entries to manifest.toml"
else
  echo "    codegen didn't finish the scan; see the log below and fix manifest.toml by hand." >&2
  tail -20 "$WORK/scan.log" >&2 || true
fi

cat <<EOF

Created $CORE${VERSION:+ for game version $VERSION}.
Next:
  1. Fill in setjmp/longjmp and port the hooks (TODOs in manifest.toml, hooks.cpp).
  2. scripts/build-ipa.sh
  3. Install, test, and add each address the game traps on to manifest.toml.
EOF
