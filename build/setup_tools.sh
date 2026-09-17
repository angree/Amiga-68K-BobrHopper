#!/bin/sh
# Recreates every toolchain from scratch. Run from Git Bash on Windows.
#   Windows (%LOCALAPPDATA%/BobrHopper/tools, local disk - zig cannot run from the NAS UNC path):
#     zig 0.14.1 (builds BOTH the PC exe and the R36S aarch64 binary), SDL2 2.30.12 mingw,
#     r36s-sdl2/ = SDL2 2.0.9 headers + libSDL2.so taken from the buster arm64 sysroot
#   WSL Ubuntu-22.04 (zig does NOT work under WSL1): Debian buster arm64 sysroot in
#     /opt/crossy-sysroot (glibc 2.28) for qemu-aarch64-static runs and readelf checks
set -e
ZIG_VER=0.14.1
SDL_VER=2.30.12
TOOLS=${CROSSY_TOOLS:-$(cygpath -m "$LOCALAPPDATA")/BobrHopper/tools}
mkdir -p "$TOOLS"
cd "$TOOLS"
if [ ! -x "zig-x86_64-windows-$ZIG_VER/zig.exe" ]; then
  curl -fsSL -o zig.zip "https://ziglang.org/download/$ZIG_VER/zig-x86_64-windows-$ZIG_VER.zip"
  unzip -q zig.zip && rm zig.zip
fi
if [ ! -d "SDL2-$SDL_VER" ]; then
  curl -fsSL -o sdl2.zip "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VER/SDL2-devel-$SDL_VER-mingw.zip"
  unzip -q sdl2.zip && rm sdl2.zip
fi
echo "windows tools OK in $TOOLS"

WSL_TOOLS=$(echo "$TOOLS" | sed 's|^\([A-Za-z]\):|/mnt/\L\1|')
MSYS_NO_PATHCONV=1 wsl.exe -d Ubuntu-22.04 -e sh -c "
set -e
export DEBIAN_FRONTEND=noninteractive
which qemu-aarch64-static >/dev/null || sudo -n apt-get install -y -qq qemu-user-static
if [ ! -f /opt/crossy-sysroot/usr/lib/aarch64-linux-gnu/libSDL2-2.0.so.0 ]; then
  which debootstrap >/dev/null || sudo -n apt-get install -y -qq debootstrap
  sudo -n rm -rf /opt/crossy-sysroot
  sudo -n debootstrap --foreign --arch=arm64 --variant=minbase --include=libsdl2-dev,libstdc++-8-dev \
    buster /opt/crossy-sysroot http://archive.debian.org/debian
  cd /opt/crossy-sysroot && for d in var/cache/apt/archives/*.deb; do sudo -n dpkg-deb -x \"\$d\" .; done
fi
T=$WSL_TOOLS/r36s-sdl2
if [ ! -f \$T/lib/libSDL2.so ]; then
  mkdir -p \$T/include \$T/lib
  cp -rL /opt/crossy-sysroot/usr/include/SDL2 \$T/include/
  cp -L /opt/crossy-sysroot/usr/lib/aarch64-linux-gnu/libSDL2-2.0.so.0.9.0 \$T/lib/libSDL2.so
fi
echo 'wsl tools OK'
"
