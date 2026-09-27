#!/bin/sh
# Bake and pack an Amiga sprite set. Until O23 both existing sets were made by hand, one command at a time, and the
# numbers behind them lived only in a chat log - which is exactly the sort of thing that cannot be rebuilt a month
# later. There are SIX sets now, so they get a script.
#
#   sh build/bake_amiga.sh                 # all six
#   sh build/bake_amiga.sh lores           # one of: lores, loreswide, hires, hireswide, ocs, ocswide
#
# The six sets and why their numbers are what they are:
#
#   set        screen    view scale   canvas       file                 when it is loaded
#   lores      320x240   6            1024x384     sprites.spr          one player, normal view
#   loreswide  320x240   7            1024x384     spriteswide.spr      two players, or the wide view
#   hires      640x480   3            2048x768     sprites640.spr       one player, normal view (RTG)
#   hireswide  640x480   3.5          2048x768     sprites640wide.spr   two players, or the wide view (RTG)
#   ocs        320x240   6            1024x384     spritesocs.spr       one player, normal view (OCS/EHB)
#   ocswide    320x240   7            1024x384     spritesocswide.spr   two players, or wide (OCS/EHB)
#
# The view scale is the game's own (src/game/settings.h: 3.0 normal, 3.5 wide on the consoles); the Amiga's 320
# screen is half as wide, so its scales are double. Wide therefore shows 7/6 more world, which is what makes room
# for two players. The canvas must hold the widest sprite - a row floor is 25 world units, 833 px at scale 6 - and
# is rendered in 1024-pixel tiles because the software rasteriser draws nothing past x = 1024.
#
# The generated header (src/amiga/sprite_ids.h) must come out IDENTICAL for all six: the game has one compiled
# name table, so every set must list the same sprites in the same order. The script checks that.
set -e
cd "$(dirname "$0")/.." || exit 1

WHICH="${1:-all}"
BAKER=out/pc/sw_bake_amiga.exe
[ -x "$BAKER" ] || sh build/build_pc.sh sw_bake_amiga >/dev/null

bake_one() {
    name="$1"; scale="$2"; cw="$3"; ch="$4"; file="$5"; seal="$6"; logow="$7"; ehb="$8"
    dir="out/check/amiga/$name"
    echo "=== $name: view scale $scale, canvas ${cw}x${ch} -> data_amiga/$file"
    mkdir -p "$dir" out/check/amiga/ids
    rm -f out/check/amiga/"$name"/*.png out/check/amiga/"$name"/sprites.txt
    "$BAKER" --data data_sf2000 --out "$dir" --view-scale "$scale" --canvas "$cw" "$ch" > "$dir/bake.log" 2>&1 ||
        { echo "bake failed - see $dir/bake.log"; tail -5 "$dir/bake.log"; exit 1; }
    # The title logo is not a model, so the baker knows nothing about it: tools/make_amiga_logo.py drops it into
    # the same directory and adds its line to sprites.txt, and the packer then treats it like any other sprite.
    # Leaving this out is how the first run of this script produced a set with no logo on the title screen.
    # the author's own logo picture, fitted into 256x88 (512x176 at 640x480): the home screen's logo box with three bars
    python tools/make_amiga_logo.py --sprites "$dir" --png assets_extra/bobr_logo_amiga.png \
        --width $((logow * 256 / 172)) --height $((logow * 88 / 172)) >> "$dir/bake.log" 2>&1 ||
        { echo "logo failed - see $dir/bake.log"; tail -5 "$dir/bake.log"; exit 1; }
    python tools/pack_amiga_sprites.py --in "$dir" --out data_amiga --name "$file" --seal "$seal" ${ehb:+--ehb} \
        --header out/check/amiga/ids/"$name".h > "$dir/pack.log" 2>&1 ||
        { echo "pack failed - see $dir/pack.log"; tail -5 "$dir/pack.log"; exit 1; }
    ls -l "data_amiga/$file" | awk '{print "    " $5 " bytes  " $9}'
    grep -h "EHB palette" -A 1 "$dir/pack.log" | sed 's/^/    /' || true
}

case "$WHICH" in
lores|all)      bake_one lores     6   1024 384 sprites.spr        1 172 ;;
esac
case "$WHICH" in
loreswide|all)  bake_one loreswide 7   1024 384 spriteswide.spr    1 172 ;;
esac
case "$WHICH" in
hires|all)      bake_one hires     3   2048 768 sprites640.spr     2 344 ;;
esac
case "$WHICH" in
hireswide|all)  bake_one hireswide 3.5 2048 768 sprites640wide.spr 2 344 ;;
esac
# OCS, Extra Half-Brite: the SAME two lores bakes, packed into 64 pens instead of 256 colours. Nothing about the
# drawing changes - same canvas, same view scales, same sprite list - only which pens the pixels land in, so a
# machine without AGA gets the same game rather than a cut-down one. Lores only: EHB is a lores mode.
case "$WHICH" in
ocs|all)        bake_one ocs       6   1024 384 spritesocs.spr     1 172 1 ;;
esac
case "$WHICH" in
ocswide|all)    bake_one ocswide   7   1024 384 spritesocswide.spr 1 172 1 ;;
esac

# every set must name the same sprites in the same order, or one compiled table cannot serve them all
# NARROW VIEWS (VIEW=NARROW|PHONE in BobrHopperPrefs): the scene in a 256- or 160-pixel column of the 320 screen,
# zoomed out so the whole width of the level still fits - 320/256 and 320/160 times the normal scale. Fewer
# columns go through c2p and the sprites are smaller; the phone view shows twice as many rows. 8-bit sets only.
case "$WHICH" in
narrow|all)     bake_one narrow     7.5  1024 384 spritesn256.spr     1 172 ;;
esac
case "$WHICH" in
narrowwide|all) bake_one narrowwide 8.75 1024 384 spritesn256wide.spr 1 172 ;;
esac
case "$WHICH" in
phone|all)      bake_one phone      12   1024 384 spritesn160.spr     1 172 ;;
esac
case "$WHICH" in
phonewide|all)  bake_one phonewide  14   1024 384 spritesn160wide.spr 1 172 ;;
esac
# ...and the same two views at 640x480 (RTG): 512 and 320 columns of the 640 screen.
case "$WHICH" in
hnarrow|all)     bake_one hnarrow     3.75  2048 768 sprites640n512.spr     2 344 ;;
esac
case "$WHICH" in
hnarrowwide|all) bake_one hnarrowwide 4.375 2048 768 sprites640n512wide.spr 2 344 ;;
esac
case "$WHICH" in
hphone|all)      bake_one hphone      6     2048 768 sprites640n320.spr     2 344 ;;
esac
case "$WHICH" in
hphonewide|all)  bake_one hphonewide  7     2048 768 sprites640n320wide.spr 2 344 ;;
esac
first=""
for h in out/check/amiga/ids/*.h; do
    case "$h" in *ui_colours.h) continue ;; esac
    [ -f "$h" ] || continue
    if [ -z "$first" ]; then first="$h"; continue; fi
    cmp -s "$first" "$h" || { echo "MISMATCH: $h differs from $first - the sets do not list the same sprites"; exit 1; }
done
[ -n "$first" ] && cp "$first" src/amiga/sprite_ids.h

# The EHB sets are the ones an optimiser could quietly ruin: it chooses 14 of the 32 base pens, and the pens it
# must NOT touch are the mouse pointer's three registers, the screen bar's and the menu colours. Checked on the
# packed files, not on the tables that made them.
if [ -f data_amiga/spritesocs.spr ]; then
    python build/ehb_palette_check.py || exit 1
fi
echo "bake_amiga: OK"
