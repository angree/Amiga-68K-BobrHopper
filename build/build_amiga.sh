#!/bin/sh
# Build the Amiga 68k (AGA/RTG) target. RUN INSIDE WSL Ubuntu-22.04:
#
#   wsl.exe -d Ubuntu-22.04 -e sh -c 'sh $REPO/build/build_amiga.sh probe'
#
# Targets:
#   probe   - build/amiga/probe.c only: proves toolchain + boot + Work: + logging
#   game    - the real thing (added as src/amiga grows)
#
# Rules carried over from the author's three 68k ports; each cost real time to find:
#   -O1, never -O2            (-O2 breaks C++ exception unwinding)
#   -mcpu=68020 -msoft-float  (never -m68040: it silently picks the 68881 multilib)
#   -noixemul                 (libnix, not ixemul)
#   never -lpthread, never -lc (they pull newlib in beside libnix)
#   NEVER strip               (m68k-amigaos-strip yields a Hunk exe that HALTs, no Guru)
#   never sprintf             (broken on this libc - use snprintf/fprintf)
#   never std::ifstream/ofstream (libstdc++ close() never returns on this target)
# Sources are mirrored onto the Linux filesystem first: compiling with include
# paths on drvfs (the NAS) is several times slower, because the compiler stats and
# opens headers thousands of times.
set -e

TARGET="${1:-probe}"

REPO=$(cd "$(dirname "$0")/.." && pwd)
WORK="$HOME/build-bobr"
DEPLOY=/mnt/c/temp/amiga_bobr/work
export PATH=/opt/amiga/bin:/usr/local/bin:/usr/bin:/bin

:

CC=m68k-amigaos-gcc
CXX=m68k-amigaos-g++
ASM=vasmm68k_mot

CPU="-mcpu=68020 -msoft-float"
OPT="-O1"
COMMON="$CPU $OPT -noixemul -fomit-frame-pointer"
DEFS="-D__AMIGA__ -DCR_FIXED=1"
INCS="-I$WORK/src -I$WORK/src/amiga"
CFLAGS="$COMMON $DEFS $INCS"
CXXFLAGS="$COMMON -std=gnu++11 -fno-exceptions -fno-rtti $DEFS $INCS"

mkdir -p "$WORK/obj" "$DEPLOY"

# Mirror the sources we need onto the Linux filesystem.
mkdir -p "$WORK/src"
if [ -d "$REPO/src" ]; then cp -r "$REPO/src/." "$WORK/src/"; fi
mkdir -p "$WORK/amiga"
cp -r "$REPO/build/amiga/." "$WORK/amiga/"

echo "=== toolchain"
$CC --version | head -1

case "$TARGET" in
probe)
    echo "=== compiling probe"
    $CC $CFLAGS -c "$WORK/amiga/probe.c" -o "$WORK/obj/probe.o"
    # No strip, ever. -lamiga gives us the AmigaOS glue; no -lm (no floats here).
    $CC $COMMON -o "$WORK/bhprobe" "$WORK/obj/probe.o" -lamiga
    ls -la "$WORK/bhprobe"
    m68k-amigaos-nm "$WORK/bhprobe" | grep -c . | sed 's/^/symbols: /'
    cp "$WORK/bhprobe" "$DEPLOY/bhprobe"
    echo "=== deployed to $DEPLOY/bhprobe"
    ;;
sprite_test)
    # First picture on the Amiga: screen + palette + masked blits + c2p, plus a dump of the chunky buffer.
    AMIGA_INCS="-I$WORK/src/amiga -I$WORK/src/amiga/cgx-include"
    echo "=== compiling the platform layer"
    for f in amiga_gfx sprites blit; do
        $CC $COMMON $DEFS $AMIGA_INCS -c "$WORK/src/amiga/$f.c" -o "$WORK/obj/$f.o"
    done
    echo "=== assembling c2p (Kalms)"
    $ASM -Fhunk -m68020 -no-opt -I"$WORK/src/amiga" -I/opt/amiga/m68k-amigaos/ndk-include \
        -o "$WORK/obj/c2p_glue.o" "$WORK/src/amiga/c2p_glue.s"
    $ASM -Fhunk -m68020 -no-opt -I"$WORK/src/amiga" -I/opt/amiga/m68k-amigaos/ndk-include \
        -o "$WORK/obj/c2p_rect.o" "$WORK/src/amiga/c2p_rect.s"
    echo "=== compiling the test"
    $CC $COMMON $DEFS $AMIGA_INCS -c "$WORK/amiga/sprite_test.c" -o "$WORK/obj/sprite_test.o"
    # No strip, ever.
    $CC $COMMON -o "$WORK/bhsprite" "$WORK/obj/sprite_test.o" "$WORK/obj/amiga_gfx.o" "$WORK/obj/sprites.o" \
        "$WORK/obj/blit.o" "$WORK/obj/c2p_glue.o" "$WORK/obj/c2p_rect.o" -lamiga
    ls -la "$WORK/bhsprite"
    cp "$WORK/bhsprite" "$DEPLOY/bhsprite"
    echo "=== deployed to $DEPLOY/bhsprite"
    ;;
game)
    # The real thing: shared game logic (CR_FIXED, 16.16, no floats) plus our Amiga layer.
    # src/amiga/shim COMES FIRST: it holds the Amiga's own "engine/renderer.h" and "engine/text.h", so the SHARED
    # screens (src/ui/screens.cpp, hud.cpp) compile unchanged against the sprite blitter instead of the 3D renderer.
    AMIGA_INCS="-I$WORK/src/amiga/shim -I$WORK/src -I$WORK/src/amiga -I$WORK/src/amiga/cgx-include"
    CXX_COMMON="$COMMON -std=gnu++14 -fno-exceptions -fno-rtti -fpermissive $DEFS -DCR_AMIGA=1 $AMIGA_INCS"

    # THE ICE LADDER. gcc 6.5 on this target segfaults on some template-heavy files - measured so far in
    # game/rows.cpp (bits/stl_heap.h) and game/player.cpp (bits/vector.tcc). Chasing them one at a time with
    # hand-written exceptions is a losing game, so every C++ file is compiled through the same ladder: the normal
    # flags, then -fno-inline, then -O0. A file that needs a step down is REPORTED and listed, never silently
    # demoted - the sibling OpenTTD port learned that a quietly -O0'd file can hide a miscompile for weeks.
    # A real compile error (not an ICE) is printed and stops the build immediately.
    : > "$WORK/ice.list"
    cxx_ice_ladder() {
        src="$1"; out="$2"; name=$(basename "$src")
        for extra in "" "-fno-inline" "-O0 -fno-inline"; do
            if $CXX $CXX_COMMON $extra -c "$src" -o "$out" 2>"$WORK/ice.err"; then
                if [ -n "$extra" ]; then
                    echo "    note: $name needed $extra"
                    echo "$name: $extra" >> "$WORK/ice.list"
                fi
                return 0
            fi
            if ! grep -q "internal compiler error" "$WORK/ice.err"; then
                echo "ERROR compiling $name:" >&2
                cat "$WORK/ice.err" >&2
                return 1
            fi
        done
        echo "ERROR: $name still crashes the compiler at -O0 -fno-inline" >&2
        cat "$WORK/ice.err" >&2
        return 1
    }

    # The object list is BUILT BY THE LOOPS, never typed out a second time below. Writing it twice is how the
    # first attempt broke: game_map.cpp compiles to game_game_map.o, and the hand-written link line said
    # game_map.o. The compiler cannot catch that - only the linker does, at the very end of a long build.
    OBJS=""

    echo "=== compiling the shared game logic"
    for f in scene models context player game_map game script rows; do
        cxx_ice_ladder "$WORK/src/game/$f.cpp" "$WORK/obj/game_$f.o" || exit 1
        OBJS="$OBJS $WORK/obj/game_$f.o"
    done

    echo "=== compiling the shared engine subset"
    for f in math assets log gsap; do
        cxx_ice_ladder "$WORK/src/engine/$f.cpp" "$WORK/obj/engine_$f.o" || exit 1
        OBJS="$OBJS $WORK/obj/engine_$f.o"
    done

    # The SHARED text and ranks. src/ui was skipped wholesale because screens.cpp needs the textured overlay API,
    # but these two files need nothing of the sort: lang.cpp is a table of {English, Polish} strings behind
    # lang::t(), and ranks.cpp is the generated rank list. Using them rather than retyping the words is what keeps
    # the Amiga's menus word-for-word identical to the SF2000 and R36S ones, Polish letters included.
    echo "=== compiling the shared text (lang, ranks)"
    for f in lang ranks screens hud; do
        cxx_ice_ladder "$WORK/src/ui/$f.cpp" "$WORK/obj/ui_$f.o" || exit 1
        OBJS="$OBJS $WORK/obj/ui_$f.o"
    done

    echo "=== compiling the Amiga layer"
    # amiga_audio.c is the Paula OS layer from the OpenTTD port; audio_bh.c is ours on top of it.
    # amiga_gfx.c and amiga_audio.c are the OpenTTD port's OS layers, amiga_adpcm.c its streaming decoder;
    # sprites/blit/audio_bh/music_bh are ours on top of them.
    for f in amiga_gfx amiga_audio amiga_adpcm sprites blit font_bh audio_bh music_bh joy_bh clock_bh prefs_bh; do
        $CC $COMMON $DEFS $AMIGA_INCS -c "$WORK/src/amiga/$f.c" -o "$WORK/obj/$f.o" || exit 1
        OBJS="$OBJS $WORK/obj/$f.o"
    done
    for f in platform_paths_amiga ui_amiga; do
        cxx_ice_ladder "$WORK/src/amiga/$f.cpp" "$WORK/obj/$f.o" || exit 1
        OBJS="$OBJS $WORK/obj/$f.o"
    done

    # game_amiga.cpp gets -fno-inline ON PURPOSE. At -O1 this compiler inlines the shared vector helpers into this
    # file and generates WRONG CODE for them: normalize() came back with its input unchanged (which it only does
    # for a zero-length vector) and cross() came back zero, collapsing the camera basis and piling the whole scene
    # at screen centre - while the very same helpers, called a few lines earlier in the same file, were correct to
    # a few raw 16.16 units. Measured, not guessed: docs/PROGRESS_AMIGA.md has the dumps.
    $CXX $CXX_COMMON  -c "$WORK/src/amiga/game_amiga.cpp" -o "$WORK/obj/game_amiga.o" || exit 1
    OBJS="$OBJS $WORK/obj/game_amiga.o"

    echo "=== assembling c2p (Kalms)"
    for s in c2p_glue c2p_rect; do
        $ASM -Fhunk -m68020 -no-opt -I"$WORK/src/amiga" -I/opt/amiga/m68k-amigaos/ndk-include \
            -o "$WORK/obj/$s.o" "$WORK/src/amiga/$s.s" || exit 1
        OBJS="$OBJS $WORK/obj/$s.o"
    done

    echo "=== linking (never stripped)"
    $CXX $COMMON -o "$WORK/bobrhopper" $OBJS -lamiga || exit 1
    ls -la "$WORK/bobrhopper"
    cp "$WORK/bobrhopper" "$DEPLOY/bobrhopper"
    echo "=== deployed to $DEPLOY/bobrhopper"

    # The settings editor: a separate program, because what it edits decides how the game's screen is opened.
    echo "=== compiling BobrHopperPrefs"
    $CC $COMMON -I"$WORK/src/amiga" -c "$WORK/amiga/bhprefs.c" -o "$WORK/obj/bhprefs.o" || exit 1
    $CC $COMMON -o "$WORK/BobrHopperPrefs" "$WORK/obj/bhprefs.o" "$WORK/obj/prefs_bh.o" -lamiga || exit 1
    cp "$WORK/BobrHopperPrefs" "$DEPLOY/BobrHopperPrefs"
    # The test machine's job runner (winuae/harness/bh_go.ps1) - deployed for testing, never packaged.
    $CC $COMMON -c "$WORK/amiga/bhloop.c" -o "$WORK/obj/bhloop.o" || exit 1
    $CC $COMMON -o "$WORK/bhloop" "$WORK/obj/bhloop.o" -lamiga || exit 1
    cp "$WORK/bhloop" "$DEPLOY/bhloop"
    # Both get the classic four-pen icon (tools/make_amiga_icon.py).
    if [ -f "$REPO/data_amiga/BobrHopper.info" ]; then
        cp "$REPO/data_amiga/BobrHopper.info" "$DEPLOY/bobrhopper.info"
        cp "$REPO/data_amiga/BobrHopper.info" "$DEPLOY/BobrHopperPrefs.info"
    fi
    ;;
*)
    echo "ERROR: unknown target '$TARGET' (probe|game)"
    exit 2
    ;;
esac

echo "=== build_amiga.sh: done ($TARGET)"
