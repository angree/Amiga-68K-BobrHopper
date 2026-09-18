#!/bin/sh
# Bake and pack an Amiga sprite set. Until O23 both existing sets were made by hand, one command at a time, and the
# numbers behind them lived only in a chat log - which is exactly the sort of thing that cannot be rebuilt a month
# later. There are FOUR sets now, so they get a script.
#
#   sh build/bake_amiga.sh                 # all four
#   sh build/bake_amiga.sh lores           # one of: lores, loreswide, hires, hireswide
#
# The four sets and why their numbers are what they are:
#
#   set        screen    view scale   canvas       file                 when it is loaded
#   lores      320x240   6            1024x384     sprites.spr          one player, normal view
#   loreswide  320x240   7            1024x384     spriteswide.spr      two players, or the wide view
#   hires      640x480   3            2048x768     sprites640.spr       one player, normal view (RTG)
#   hireswide  640x480   3.5          2048x768     sprites640wide.spr   two players, or the wide view (RTG)
#
# The view scale is the game's own (src/game/settings.h: 3.0 normal, 3.5 wide on the consoles); the Amiga's 320
# screen is half as wide, so its scales are double. Wide therefore shows 7/6 more world, which is what makes room
# for two players. The canvas must hold the widest sprite - a row floor is 25 world units, 833 px at scale 6 - and
# is rendered in 1024-pixel tiles because the software rasteriser draws nothing past x = 1024.
#
# The generated header (src/amiga/sprite_ids.h) must come out IDENTICAL for all four: the game has one compiled
# name table, so every set must list the same sprites in the same order. The script checks that.
set -e
cd "$(dirname "$0")/.." || exit 1

WHICH="${1:-all}"
BAKER=out/pc/sw_bake_amiga.exe
[ -x "$BAKER" ] || sh build/build_pc.sh sw_bake_amiga >/dev/null

bake_one() {
    name="$1"; scale="$2"; cw="$3"; ch="$4"; file="$5"; seal="$6"; logow="$7"
    dir="out/check/amiga/$name"
    echo "=== $name: view scale $scale, canvas ${cw}x${ch} -> data_amiga/$file"
    mkdir -p "$dir" out/check/amiga/ids
    rm -f out/check/amiga/"$name"/*.png out/check/amiga/"$name"/sprites.txt
    "$BAKER" --data data_sf2000 --out "$dir" --view-scale "$scale" --canvas "$cw" "$ch" > "$dir/bake.log" 2>&1 ||
        { echo "bake failed - see $dir/bake.log"; tail -5 "$dir/bake.log"; exit 1; }
    # The title logo is not a model, so the baker knows nothing about it: tools/make_amiga_logo.py drops it into
    # the same directory and adds its line to sprites.txt, and the packer then treats it like any other sprite.
    # Leaving this out is how the first run of this script produced a set with no logo on the title screen.
    python tools/make_amiga_logo.py --sprites "$dir" --width "$logow" >> "$dir/bake.log" 2>&1 ||
        { echo "logo failed - see $dir/bake.log"; tail -5 "$dir/bake.log"; exit 1; }
    python tools/pack_amiga_sprites.py --in "$dir" --out data_amiga --name "$file" --seal "$seal" \
        --header out/check/amiga/ids/"$name".h > "$dir/pack.log" 2>&1 ||
        { echo "pack failed - see $dir/pack.log"; tail -5 "$dir/pack.log"; exit 1; }
    ls -l "data_amiga/$file" | awk '{print "    " $5 " bytes  " $9}'
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

# every set must name the same sprites in the same order, or one compiled table cannot serve them all
first=""
for h in out/check/amiga/ids/*.h; do
    case "$h" in *ui_colours.h) continue ;; esac
    [ -f "$h" ] || continue
    if [ -z "$first" ]; then first="$h"; continue; fi
    cmp -s "$first" "$h" || { echo "MISMATCH: $h differs from $first - the sets do not list the same sprites"; exit 1; }
done
[ -n "$first" ] && cp "$first" src/amiga/sprite_ids.h
echo "bake_amiga: OK"
