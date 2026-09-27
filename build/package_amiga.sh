#!/bin/sh
# Package the Amiga build into one drawer the player can copy anywhere (task F4). RUN INSIDE WSL:
#
#   wsl.exe -d Ubuntu-22.04 -e sh -c 'sh $REPO/build/package_amiga.sh 001'
#
# What goes in, and why it is exactly this:
#   bobrhopper            the executable, NEVER stripped (a stripped Hunk binary halts the machine)
#   data/sprites.spr      every baked sprite plus the palette, big-endian, read in one go into fast RAM
#   data/sounds.bhs       the effects, 8-bit signed with a precomputed Paula period each
#   data/music/*.wav      IMA ADPCM, STREAMED from disk - the biggest part of the package by far
#   data/manifest.txt     what models exist
#   data/meshes/*.fmesh   40-byte stubs carrying only AABBs: the shared logic measures collision widths from
#                         mesh bounds, and this is 1.5 KB instead of the megabyte of real meshes
#
# The game writes only into its own drawer (PROGDIR:), so the package is self-contained and never touches the
# boot volume.
set -e

REPO=$(cd "$(dirname "$0")/.." && pwd)
BIN="$HOME/build-bobr/bobrhopper"
STAGE="$REPO/out/amiga/BobrHopper"
OUT="$REPO/out/amiga"

# THE VERSION IS THE ONE THE GAME SHOWS (src/amiga/version_bh.h, on the screen bar). The user tests by version
# number, so a package whose name and whose title bar disagree is a trap. An explicit argument must match it.
HDR_VERSION=$(sed -n 's/^#define BH_VERSION "v\([0-9]*\)"/\1/p' "$REPO/src/amiga/version_bh.h")
[ -n "$HDR_VERSION" ] || { echo "cannot read BH_VERSION from src/amiga/version_bh.h"; exit 1; }
VERSION="${1:-$HDR_VERSION}"
VERSION="${VERSION#v}"
if [ "$VERSION" != "$HDR_VERSION" ]; then
    echo "package v$VERSION but the game says v$HDR_VERSION - bump src/amiga/version_bh.h and rebuild first"
    exit 1
fi
if [ -f "$OUT/BobrHopper-Amiga-v$VERSION.zip" ]; then
    echo "note: replacing the existing BobrHopper-Amiga-v$VERSION.zip"
fi

:

[ -f "$BIN" ] || { echo "no binary at $BIN - run build_amiga.sh game first"; exit 1; }
[ -f "$REPO/data_amiga/sprites.spr" ] || { echo "no data_amiga/sprites.spr - run tools/pack_amiga_sprites.py"; exit 1; }
# The font is not optional. It was missing from this list, so every package built so far would have run with the
# fallback instead of the game's own font - the exact defect the user reported about the first draft, shipped
# again in the box while the working directory looked fine.
[ -f "$REPO/data_amiga/font.bhf" ] || { echo "no data_amiga/font.bhf - run tools/make_amiga_font.py"; exit 1; }

# The binary must be NEWER than the sources it came from, or the package ships something stale. The SF2000
# packaging learned this the hard way ("binarka starsza niz libretro_core.cpp").
NEWEST=$(find "$REPO/src" -name '*.cpp' -o -name '*.c' -o -name '*.h' | xargs ls -t 2>/dev/null | head -1)
if [ -n "$NEWEST" ] && [ "$NEWEST" -nt "$BIN" ]; then
    echo "ERROR: $NEWEST is newer than the binary - rebuild before packaging"
    exit 1
fi

rm -rf "$STAGE"
mkdir -p "$STAGE/data/meshes" "$STAGE/data/music"

cp "$BIN" "$STAGE/bobrhopper"
# THE 68060 BUILD (build/build_amiga060.sh): the same game without a single 64-bit multiply or divide in its own
# code - instructions the 060 does not have and emulates by trap. Checked by build/check_060.sh; it must be as
# fresh as the ordinary one.
BIN060="$HOME/build-bobr060/bobrhopper060"
[ -f "$BIN060" ] || { echo "no bobrhopper060 - run build/build_amiga060.sh first"; exit 1; }
if [ "$BIN" -nt "$BIN060" ] && [ $(( $(stat -c %Y "$BIN") - $(stat -c %Y "$BIN060") )) -gt 3600 ]; then
    echo "ERROR: bobrhopper060 is over an hour older than bobrhopper - rebuild it"
    exit 1
fi
cp "$BIN060" "$STAGE/bobrhopper060"
cp "$REPO/data_amiga/BobrHopper.info" "$STAGE/bobrhopper060.info"
# The settings editor (AGA/RTG, screen bar), the classic four-pen icons for both programs, and the title's .tex
# files - the shared home screen lays the logo out from the title's size. None of these were in the first packages.
[ -f "$HOME/build-bobr/BobrHopperPrefs" ] || { echo "no BobrHopperPrefs - run build_amiga.sh game first"; exit 1; }
cp "$HOME/build-bobr/BobrHopperPrefs" "$STAGE/BobrHopperPrefs"
[ -f "$REPO/data_amiga/BobrHopper.info" ] || { echo "no icon - run tools/make_amiga_icon.py"; exit 1; }
cp "$REPO/data_amiga/BobrHopper.info" "$STAGE/bobrhopper.info"
cp "$REPO/data_amiga/BobrHopper.info" "$STAGE/BobrHopperPrefs.info"
mkdir -p "$STAGE/data/images"
for t in title button_long_play button_settings button_back; do cp "$REPO/data_amiga/images/$t.tex" "$STAGE/data/images/"; done
cp "$REPO/data_amiga/sprites.spr" "$STAGE/data/"
cp "$REPO/data_amiga/font.bhf" "$STAGE/data/"
# The 640x480 set (RTG only): a second sprite container and a full-size font. The game loads ONE of the two sets,
# the one the chosen resolution needs.
[ -f "$REPO/data_amiga/sprites640.spr" ] || { echo "no data_amiga/sprites640.spr - bake and pack the 640 set"; exit 1; }
[ -f "$REPO/data_amiga/font640.bhf" ] || { echo "no data_amiga/font640.bhf - run tools/make_amiga_font.py --data data --sizes 12,14,18,32 --name font640.bhf"; exit 1; }
cp "$REPO/data_amiga/sprites640.spr" "$STAGE/data/"
cp "$REPO/data_amiga/font640.bhf" "$STAGE/data/"
# O23: and the WIDE sets, one per resolution. Wide is what two players need (and what the View setting picks); the
# game still holds exactly ONE container at a time and swaps it on the title screen. Both are SMALLER than their
# normal counterparts - a wider view means smaller sprites - so nothing here raises the memory floor.
for w in spriteswide.spr sprites640wide.spr; do
    [ -f "$REPO/data_amiga/$w" ] || { echo "no data_amiga/$w - run sh build/bake_amiga.sh"; exit 1; }
    cp "$REPO/data_amiga/$w" "$STAGE/data/"
done
# O25: and the OCS pair, for a machine with no AGA. Same pictures in 64 Extra Half-Brite pens instead of 256
# colours, so they are the same SIZE as the lores sets they are made from - an OCS machine is not asked to find
# any more memory than an AGA one, it just gets a palette its chipset can show.
for w in spritesocs.spr spritesocswide.spr; do
    [ -f "$REPO/data_amiga/$w" ] || { echo "no data_amiga/$w - run sh build/bake_amiga.sh ocs ocswide"; exit 1; }
    cp "$REPO/data_amiga/$w" "$STAGE/data/"
done
# The SCREEN shapes (the game's settings: NARROW, PHONE): the scene in a narrower column, zoomed out, each with its
# own sets - normal and wide, 320x240 and 640x480. Loaded one at a time, like every other set.
for w in spritesn256.spr spritesn256wide.spr spritesn160.spr spritesn160wide.spr \
         sprites640n512.spr sprites640n512wide.spr sprites640n320.spr sprites640n320wide.spr; do
    [ -f "$REPO/data_amiga/$w" ] || { echo "no data_amiga/$w - run sh build/bake_amiga.sh"; exit 1; }
    cp "$REPO/data_amiga/$w" "$STAGE/data/"
done
[ -f "$REPO/data_amiga/sounds.bhs" ] && cp "$REPO/data_amiga/sounds.bhs" "$STAGE/data/"
cp "$REPO/data_amiga/manifest.txt" "$STAGE/data/"
cp "$REPO/data_amiga/meshes/"*.fmesh "$STAGE/data/meshes/"
if [ -d "$REPO/data_amiga/music" ]; then cp "$REPO/data_amiga/music/"*.wav "$STAGE/data/music/" 2>/dev/null || true; fi

cat > "$STAGE/README.txt" <<'TXT'
Bobr Hopper - Amiga 68k
=======================

A hopping game: cross the roads, the rivers and the railway without being run over.
Classic (endless) and Progression (levels, a finish line, ranks), English and Polish.
One or TWO players on one screen.

REQUIREMENTS
  - 68020 or better, no FPU needed (the whole game is 16.16 fixed point)
  - AGA, an RTG board (Picasso96 / CyberGraphX), or PLAIN OCS/ECS - the OCS mode
    draws in Extra Half-Brite, 64 colours, 320x240, with its own sprite set
  - Kickstart 3.0+, a few hundred KB of chip RAM free and 4 MB of fast RAM
    (10 MB for the 640x480 RTG mode). Fast RAM is not optional: the sprites and
    the sounds are about 2 MB and they do not fit in chip.
  - a hard disk: the music is STREAMED from disk while you play

INSTALL
  Copy the whole BobrHopper drawer wherever you like and double-click "bobrhopper".
  The game writes only inside its own drawer.

68060: START "bobrhopper060" INSTEAD
  The same game, built for the 68060. The 060 has no 32x32->64 multiply and no
  64/32 divide in silicon - 68060.library emulates them by trapping, every time -
  and the ordinary build uses them for all of its arithmetic. bobrhopper060 uses
  none, and plays out bit for bit the same. On a 68020/030/040 use "bobrhopper".

SETTINGS BEFORE THE GAME STARTS - BobrHopperPrefs
  Graphics     AGA, RTG, or OCS/ECS (Extra Half-Brite)
  Resolution   320x240 (all three) or 640x480 (RTG only)
  Screen bar   the Workbench title bar above the game: on or off
  From a Shell: BobrHopperPrefs GFX=RTG SCREEN=640x480 BAR=OFF   (SHOW prints them)
                BobrHopperPrefs GFX=OCS                          for a machine without AGA
  Everything else (sound, music, language, character, two players, ...) is in the
  game's own settings screen: SETTINGS on the title screen, or press S. The list
  scrolls - a small triangle appears when there is more above or below.

SCREEN SHAPE (in the game's settings)
  FULL     the whole screen, as before
  NARROW   the middle 4/5 of the screen, zoomed out a little - the author's pick:
           less to convert to bitplanes, the same game
  PHONE    a tall column half the width, zoomed out twice - you see twice as many
           rows ahead. A different way to play
  Each shape has its own graphics, loaded on the title screen (LOADING for a
  moment) - only one set is ever in memory. AGA and RTG, 320x240 and 640x480;
  not in the OCS/ECS mode.

TWO PLAYERS
  Settings -> Players -> 2 PLAYERS, then give each player a device of its own:
    ARROWS   cursor keys, Space or Return to hop forward
    WSAD     W A S D, left Shift or Ctrl to hop forward
    JOY 2    the joystick port (the lower socket - where a stick normally lives)
    JOY 1    the mouse port; only read when a player has chosen it, because a
             mouse in that socket would hop a hero about at random
  The two share one screen, so the view widens by itself and the game loads a
  second set of graphics for it (one set at a time - it says LOADING for a moment).
  Every row then has at least TWO ways through, so neither player has to queue.
  Land on the other player and you stand on its head; it walks out from under you
  the moment it hops away.
    CLASSIC is a DUEL - fall too far behind and you are out of the frame, and the
      other one plays on. The higher score wins.
    PROGRESSION is CO-OPERATIVE - nobody is left behind: the one in front is
      pulled back onto the other's head, and a player who dies comes back on its
      partner's head a couple of seconds later.

CONTROLS
                 keyboard              joystick / CD32 pad
  hop            cursor keys           stick (the hop happens when you let go)
  A  (choose)    A, Space, Return      fire (red)
  B  (back)      B, Backspace, Esc     2nd button (blue) - in menus
  pause          P, Esc                2nd button during play, PLAY on a CD32 pad
  settings (S)   S, Tab                green
  Esc always goes back one step: out of a menu, out of the settings, from the game
  to the pause menu - and from the title screen out of the game.
  quit           Esc on the title screen, or Exit in the pause menu

  After a game:  A / fire = play again,  S / stick left = back to the menu.

CREDITS AND LICENCE
  Bobr Hopper, Amiga 68k port: (c) 2026 G. Korycki, MIT licence.
  Game logic ported from EvanBacon/expo-crossy-road (MIT). Not affiliated with
  Hipster Whale, Yodo1, or the "Crossy Road" game or trademark.
  Chunky-to-planar routines by Mikael Kalms.
  Every sound and music track was made for this port; no audio from the original
  project is included.
TXT

cd "$OUT"
ZIP="BobrHopper-Amiga-v$VERSION.zip"
rm -f "$ZIP"
if command -v zip >/dev/null 2>&1; then
    zip -rq "$ZIP" BobrHopper
else
    echo "note: no zip available, writing a tar.gz instead"
    ZIP="BobrHopper-Amiga-v$VERSION.tar.gz"
    tar czf "$ZIP" BobrHopper
fi

echo "=== package: $OUT/$ZIP"
ls -la "$OUT/$ZIP"
echo "=== contents:"
find "$STAGE" -type f | sed "s|$STAGE|  BobrHopper|" | sort | head -20
echo "  ... $(find "$STAGE" -type f | wc -l) files, $(du -sh "$STAGE" | cut -f1) unpacked"
