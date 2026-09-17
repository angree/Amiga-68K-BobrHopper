#!/usr/bin/env python3
"""Pack the baked sprites into one Amiga-ready container (tasks C2 + C3).

    python tools/pack_amiga_sprites.py [--in out/check/amiga/sprites] [--out data_amiga]
                                       [--header src/amiga/sprite_ids.h]

Input is what apps/sw_bake_amiga.cpp produced: one RGBA PNG per sprite plus sprites.txt
(name rot phase w h anchorX anchorY).

Output:
  <out>/sprites.spr    one file, BIG-ENDIAN, loaded with a single read into fast RAM
  <header>             generated C header: one enum constant per sprite, so the game never
                       looks anything up by string at runtime

Format of sprites.spr (all multi-byte fields big-endian, so the 68k does no swapping at all -
the same trick Amiga_GTA uses for its .til files):

    0  char[4]  'BHSP'
    4  u16      version (1)
    6  u16      sprite count
    8  u16      palette entries used (<= 256)
   10  u16      reserved (0)
   12  u8[768]  palette, RGB triples; entry 0 is the transparent key and is never drawn
  780  entries[count], 12 bytes each:
           u16 w, u16 h, i16 anchorX, i16 anchorY, u32 pixelOffset
  ...  pixel data, w*h bytes per sprite, one palette index per pixel, 0 = transparent

Palette: the art is flat-coloured (.fmesh carries one colour per triangle), so the exact set of
colours is small and is used as-is when it fits. If it ever does not fit, the rarest colours are
merged into their nearest neighbour - reported, never silent.
"""
import argparse
import os
import struct
import sys
from collections import Counter

try:
    from PIL import Image
except ImportError:
    sys.exit("needs Pillow: python -m pip install Pillow")

MAGIC = b"BHSP"
VERSION = 1
MAX_COLORS = 256  # index 0 is the transparent key and index 1 the sky, so 254 are available for art

# src/game/settings.h: sceneColor = 0x87C6FF. The sky appears in no sprite (sprites are cropped to their model),
# so unless it is put into the palette on purpose the game has no colour to clear the screen to - which is exactly
# why the first Amiga frame came out magenta, the transparent key showing through everywhere.
SKY = (0x87, 0xC6, 0xFF)
SKY_INDEX = 1

# The menus must LOOK like the console versions, so they use the console versions' exact colours rather than
# whatever the art happens to contain. src/ui/screens.cpp: the menu bars alternate 0x6A40EB and 0x6A8FEB, the
# selected label is the HomeScreen coin yellow 0xF8E84D and the others are white. The last entry is BLACK AND
# DRAWABLE: index 0 is black too, but it is the transparency key and is never written, so an outline or a text
# shadow needs a black of its own.
UI_COLOURS = [(0x6A, 0x40, 0xEB), (0x6A, 0x8F, 0xEB), (0xF8, 0xE8, 0x4D), (0xFF, 0xFF, 0xFF), (0x00, 0x00, 0x00),
              # and the game-over banners, which are their own three blues in src/ui/screens.cpp
              (0x36, 0x40, 0xEB), (0x36, 0x8F, 0xEB), (0x36, 0xD6, 0xEB)]
UI_NAMES = ["BH_UI_BAR_A", "BH_UI_BAR_B", "BH_UI_SELECTED", "BH_UI_TEXT", "BH_UI_OUTLINE",
            "BH_UI_GO_A", "BH_UI_GO_B", "BH_UI_GO_C"]
UI_FIRST = 2  # 0 transparent, 1 sky, then these

# INDICES 10..19 BELONG TO THE SYSTEM, NOT TO THE ART.
#
# On an 8-bitplane screen the Intuition title bar's pens and the MOUSE POINTER's colours are the same colour
# registers the game paints with - the pointer is hardware sprite 0, which on this chipset reads registers
# 17, 18 and 19. Loading art into those registers is why the bar and the arrow came out in whatever colour the
# sprites happened to put there. Reserving them costs ten entries out of 256 (the art uses about 150) and makes
# the bar and the pointer look like the system's, which is what they are.
SYS_FIRST = 10
# Where the artwork's own colours begin. Everything below this line is reserved and the packer guarantees no
# sprite pixel ever refers to it.
ART_FIRST = 20
SYS_SLOTS = [
    ("BH_SYS_BAR_TEXT", (0xFF, 0xFF, 0xFF)),  # 10  BARDETAILPEN - the title on the bar
    ("BH_SYS_BAR_FILL", (0x20, 0x5A, 0x8C)),  # 11  BARBLOCKPEN  - the bar itself, Workbench blue
    ("BH_SYS_BAR_EDGE", (0x00, 0x00, 0x00)),  # 12  the bar's trim line
    # 13: the pause/settings backdrop of src/ui/screens.cpp, rgba(105, 201, 230, 0.8) - drawn dithered by
    # src/amiga/ui_amiga.cpp. It sits among the system slots because those are the fixed indices left.
    ("BH_SYS_SPARE_13", (105, 201, 230)),
    ("BH_SYS_SPARE_14", (0x90, 0x90, 0x90)),  # 14
    ("BH_SYS_SPARE_15", (0xB0, 0xB0, 0xB0)),  # 15
    ("BH_SYS_SPARE_16", (0xD0, 0xD0, 0xD0)),  # 16
    ("BH_SYS_POINTER_1", (0xFF, 0xFF, 0xFF)),           # 17  pointer body
    ("BH_SYS_POINTER_2", (0x00, 0x00, 0x00)),           # 18  pointer outline
    ("BH_SYS_POINTER_3", (0xFF, 0x8A, 0x00)),           # 19  pointer highlight
]


def load_index(path):
    """sprites.txt -> list of dicts, in file order (that order becomes the sprite ids)."""
    entries = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split()
            if len(parts) != 7:
                sys.exit("bad line in %s: %s" % (path, line))
            name, rot, phase, w, h, ax, ay = parts
            entries.append({"name": name, "rot": int(rot), "phase": int(phase),
                            "w": int(w), "h": int(h), "ax": int(ax), "ay": int(ay)})
    return entries


def sprite_file(indir, e):
    if e["phase"] >= 0:
        return os.path.join(indir, "%s_r%d_s%d.png" % (e["name"], e["rot"], e["phase"]))
    return os.path.join(indir, "%s_r%d.png" % (e["name"], e["rot"]))


def sprite_symbol(e):
    base = "SPR_%s_R%d" % (e["name"].upper(), e["rot"])
    return base + ("_S%d" % e["phase"] if e["phase"] >= 0 else "")


def nearest(color, palette):
    best, bestd = ART_FIRST, None
    for i, p in enumerate(palette):
        if i < ART_FIRST:
            continue  # never map art onto the transparent key, the menus' colours or the system's registers
        d = (color[0] - p[0]) ** 2 + (color[1] - p[1]) ** 2 + (color[2] - p[2]) ** 2
        if bestd is None or d < bestd:
            best, bestd = i, d
    return best


FLOOR_NAMES = ("grass_0", "grass_1", "road_0", "road_1", "river", "railroad")


def seal_floor_edge(img, e, grow=1):
    """THE DASHED DARK LINES BETWEEN ROWS, which the user reported seven times before I looked at the sprite.

    Measured on grass_0: 116 of 400 columns carry, as their TOPMOST pixel, a darker green (132,170,49) instead of
    the surface colour (148,190,66) - a sliver of the slab's far edge caught by the bake. Rows are painted far to
    near, so the near row's top edge always lands on top, and along the tilted edge those slivers read as a dashed
    dark line at every join.

    Two things, per column: the topmost pixel takes the colour of the surface beneath it, and the strip grows ONE
    pixel upwards (about 4% of the 24-pixel surface), so it overlaps the row behind and no rounding of two
    independently placed sprites can open a seam. The anchor moves with it, so nothing on the row shifts.

    `grow` is that pixel count: 1 for the 320x240 set, 2 for the 640x480 set, whose sprites are twice the size."""
    w, h = img.size
    out = Image.new("RGBA", (w, h + grow), (0, 0, 0, 0))
    out.paste(img, (0, grow))
    px = out.load()
    H = h + grow
    for x in range(w):
        top = next((y for y in range(grow, H) if px[x, y][3]), None)
        if top is None or top + 2 * grow >= H:
            continue
        surface = px[x, top + 2 * grow] if px[x, top + 2 * grow][3] else px[x, top]
        for y in range(top, top + 2 * grow):
            if px[x, y][3]:
                px[x, y] = surface
        for y in range(top - grow, top):
            px[x, y] = surface
    e["h"] += grow
    e["ay"] += grow
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--in", dest="indir", default="out/check/amiga/sprites")
    ap.add_argument("--out", dest="outdir", default="data_amiga")
    ap.add_argument("--header", default="src/amiga/sprite_ids.h")
    # The 640x480 set: its own file name, its own seal width, and a header that must come out IDENTICAL to the
    # 320x240 one - the game has one compiled name table, so both sets must list the same sprites in the same order.
    ap.add_argument("--name", default="sprites.spr")
    ap.add_argument("--seal", type=int, default=1)
    args = ap.parse_args()

    entries = load_index(os.path.join(args.indir, "sprites.txt"))
    if not entries:
        sys.exit("no sprites in %s" % args.indir)

    # Pass 1: every opaque colour in every sprite, by how many pixels use it.
    images, counts = [], Counter()
    for e in entries:
        path = sprite_file(args.indir, e)
        img = Image.open(path).convert("RGBA")
        if img.size != (e["w"], e["h"]):
            sys.exit("%s is %dx%d but sprites.txt says %dx%d" % (path, img.size[0], img.size[1], e["w"], e["h"]))
        if e["name"] in FLOOR_NAMES:
            img = seal_floor_edge(img, e, args.seal)
        px = img.load()
        images.append(img)
        for y in range(e["h"]):
            for x in range(e["w"]):
                r, g, b, a = px[x, y]
                if a >= 128:
                    counts[(r, g, b)] += 1

    # Index 0 is the transparent key and is never drawn. Index 1 is the SKY: the game clears the screen to it
    # before anything else, and no sprite contains it (sprites are cropped to the model), so it would otherwise be
    # missing from a palette built only from the art - which is why the first Amiga frame came out magenta.
    # Index 0 is the transparency key AND the colour Intuition shows outside the game's own area, so it must be
    # BLACK. It was magenta, which put a bright pink border around the whole screen on the real machine - the
    # first thing the user said about the running game. Nothing is ever drawn in index 0, so its colour is free
    # to be anything; black is what a border should be.
    sys_cols = [rgb for _, rgb in SYS_SLOTS]
    # 0 transparent, 1 sky, 2..9 the menus' colours, 10..19 the system's (bar pens and the mouse pointer's
    # hardware registers). Art starts at ART_FIRST and may never land below it.
    #
    # The art keeps its OWN copies of white and black even though the reserved slots hold those colours too.
    # Excluding them looked tidier and was wrong: with no art slot of their own, every white pixel in the
    # artwork mapped onto the reserved white - which is register 17, the mouse pointer's - and the reservation
    # existed in name only. Two duplicated entries out of 256 buy a pointer and a title bar that the game can
    # never repaint by accident.
    ordered = [c for c, _ in counts.most_common() if c != SKY]
    palette = [(0, 0, 0), SKY] + UI_COLOURS + sys_cols
    merged = 0
    room = MAX_COLORS - len(palette)
    if len(ordered) <= room:
        palette.extend(ordered)
    else:
        palette.extend(ordered[:room])
        merged = len(ordered) - room
    # ONLY the art range: a sprite pixel must never be given a reserved index, even when its colour happens to
    # match one exactly (white and black do).
    index_of = {c: i for i, c in enumerate(palette) if i >= ART_FIRST}

    print("colours in the art: %d%s" % (len(ordered),
          "" if not merged else "  (%d rarest merged into their nearest neighbour)" % merged))

    # Pass 2: pixels.
    blobs = []
    for e, img in zip(entries, images):
        px = img.load()
        data = bytearray(e["w"] * e["h"])
        for y in range(e["h"]):
            row = y * e["w"]
            for x in range(e["w"]):
                r, g, b, a = px[x, y]
                if a < 128:
                    continue  # stays 0 = transparent
                key = (r, g, b)
                i = index_of.get(key)
                if i is None:
                    i = nearest(key, palette)
                    index_of[key] = i
                data[row + x] = i
        blobs.append(bytes(data))

    os.makedirs(args.outdir, exist_ok=True)
    header_size = 12 + 768
    table_size = 12 * len(entries)
    offset = header_size + table_size

    out = bytearray()
    out += MAGIC
    out += struct.pack(">HHHH", VERSION, len(entries), len(palette), 0)
    pal = bytearray(768)
    for i, (r, g, b) in enumerate(palette):
        pal[i * 3:i * 3 + 3] = bytes((r, g, b))
    out += pal
    for e, blob in zip(entries, blobs):
        out += struct.pack(">HHhhI", e["w"], e["h"], e["ax"], e["ay"], offset)
        offset += len(blob)
    for blob in blobs:
        out += blob

    spr_path = os.path.join(args.outdir, args.name)
    with open(spr_path, "wb") as f:
        f.write(out)

    os.makedirs(os.path.dirname(args.header), exist_ok=True)
    with open(args.header, "w", encoding="utf-8") as f:
        f.write("// Generated by tools/pack_amiga_sprites.py - do not edit.\n")
        f.write("// One constant per baked sprite, so the Amiga never looks a sprite up by name at runtime.\n")
        f.write("#ifndef BH_SPRITE_IDS_H\n#define BH_SPRITE_IDS_H\n\n")
        f.write("#define BH_SPRITE_COUNT %d\n" % len(entries))
        f.write("#define BH_PALETTE_ENTRIES %d\n" % len(palette))
        f.write("// Palette index 0 is the transparent key and is never drawn; index 1 is the sky (0x87C6FF from\n")
        f.write("// src/game/settings.h), which appears in no sprite and must therefore be reserved on purpose.\n")
        f.write("#define BH_SKY_INDEX %d\n\n" % SKY_INDEX)
        for i, e in enumerate(entries):
            f.write("#define %-40s %d\n" % (sprite_symbol(e), i))

        # A runtime table as well as the constants. The scene bridge walks the game's scene graph, where a node
        # knows its Model and a Model knows only its NAME - so the Amiga needs to turn "police_car" plus a rotation
        # (and, for the hero, a squash phase) into a sprite id without doing any string work per frame. The table is
        # walked once at startup to build the lookups; the ids above stay for anything referenced directly.
        f.write("\ntypedef struct {\n")
        f.write("    const char *name;\n")
        f.write("    short rot;   /* 0..BH_ROT_MAX-1, in equal steps about Y; the count is PER MODEL */\n")
        f.write("    short phase; /* squash phase for the hero, -1 for everything else */\n")
        f.write("    short id;\n")
        f.write("} BHSpriteName;\n\n")
        f.write("/* Include this header from exactly ONE translation unit if you want the table. */\n")
        f.write("static const BHSpriteName bh_sprite_names[] = {\n")
        for i, e in enumerate(entries):
            f.write('    {"%s", %d, %d, %d},\n' % (e["name"], e["rot"], e["phase"], i))
        f.write("};\n")
        f.write("#define BH_SPRITE_NAME_COUNT %d\n" % len(entries))
        f.write("#define BH_HERO_PHASE_COUNT %d\n" % max(1, 1 + max(e["phase"] for e in entries)))
        # How many baked directions the most-rotated model has. The hero has twelve (its facing is a tween, so
        # four could only snap); everything else has four or, for row strips, one. The renderer sizes its tables
        # with this and learns each model's OWN count from the table above.
        f.write("#define BH_ROT_MAX %d\n" % max(1, 1 + max(e["rot"] for e in entries)))
        f.write("\n#endif\n")

    # The UI colours in their own tiny header. They could have gone into sprite_ids.h, but that carries a static
    # table of every sprite name, and screens_bh.c wants four colours - not 178 strings it never reads.
    ui_path = os.path.join(os.path.dirname(args.header), "ui_colours.h")
    with open(ui_path, "w", encoding="utf-8") as f:
        f.write("// Generated by tools/pack_amiga_sprites.py - do not edit.\n")
        f.write("// Fixed palette slots for the menus, so the Amiga shows the SAME colours as the console ports\n")
        f.write("// (src/ui/screens.cpp) instead of picking the lightest colour it can find in the art.\n")
        f.write("#ifndef BH_UI_COLOURS_H\n#define BH_UI_COLOURS_H\n\n")
        for i, (name, rgb) in enumerate(zip(UI_NAMES, UI_COLOURS)):
            f.write("#define %-16s %-3d /* 0x%02X%02X%02X */\n" % (name, UI_FIRST + i, rgb[0], rgb[1], rgb[2]))
        f.write("\n// The SYSTEM's own registers, never written by the art. The Intuition bar draws in the pens\n")
        f.write("// below, and the mouse pointer is hardware sprite 0, which reads registers 17, 18 and 19 -\n")
        f.write("// so those three must hold pointer colours, not whatever the sprites happened to need.\n")
        for i, (name, rgb) in enumerate(SYS_SLOTS):
            f.write("#define %-18s %-3d /* 0x%02X%02X%02X */\n" % (name, SYS_FIRST + i, rgb[0], rgb[1], rgb[2]))
        f.write("\n#endif\n")

    print("packed %d sprites, %d bytes -> %s" % (len(entries), len(out), spr_path))
    print("ids -> %s, ui colours -> %s" % (args.header, ui_path))


if __name__ == "__main__":
    main()
