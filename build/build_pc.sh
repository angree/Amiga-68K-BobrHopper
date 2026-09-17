#!/bin/sh
# Windows build (Git Bash): zig c++ -> x86_64-windows-gnu, SDL2 mingw.
# Toolchain lives on the LOCAL disk (zig cannot re-spawn itself from a network (UNC) path).
# Usage: sh build/build_pc.sh <target>   targets: probe, test_math
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd -W)
TOOLS=${CROSSY_TOOLS:-$(cygpath -m "$LOCALAPPDATA")/BobrHopper/tools}
ZIG="$TOOLS/zig-x86_64-windows-0.14.1/zig.exe"
SDL="$TOOLS/SDL2-2.30.12/x86_64-w64-mingw32"
OUT="$ROOT/out/pc"
TARGET=${1:-probe}
[ -x "$ZIG" ] || { echo "missing zig in $TOOLS: run sh build/setup_tools.sh"; exit 1; }
mkdir -p "$OUT"

. "$ROOT/build/sources.sh"
SOURCES=$(sources_for "$TARGET") || { echo "unknown target $TARGET"; exit 1; }

SRC_ABS=""
for s in $SOURCES; do SRC_ABS="$SRC_ABS $ROOT/$s"; done

DEFS=""
case "$TARGET" in
  test_sw_*|sw_*) DEFS="-DCR_FIXED -DCR_PLATFORM_SF2000" ;; # the SF2000 software renderer is 16.16 only
esac

"$ZIG" c++ -target x86_64-windows-gnu -std=c++17 -O2 -g0 -Wall -ffp-contract=off $DEFS \
  -DSDL_MAIN_HANDLED -I"$SDL/include/SDL2" -I"$ROOT/src" \
  $SRC_ABS "$SDL/lib/libSDL2.dll.a" -o "$OUT/$TARGET.exe"
cp -f "$SDL/bin/SDL2.dll" "$OUT/"
echo "built $OUT/$TARGET.exe"
