#!/usr/bin/env python3
"""A CLASSIC Workbench icon for the game: 2 bitplanes, the four standard pens, nothing else.

    python tools/make_amiga_icon.py [--out data_amiga/BobrHopper.info] [--preview out/check/amiga/icon.png]

The user asked for the traditional four-colour kind on purpose: of all the sibling ports only OpenTTD's icon comes
out right, and the part of it that every Workbench draws is exactly this - an OS 1.x/3.1 planar image in the four
reserved pens (0 grey, 1 black, 2 white, 3 blue). No NewIcons tooltypes, no colour-icon chunks, nothing a plain
Workbench 3.1 has to have a patch for. The DiskObject/Gadget/Image layout below is byte for byte the one
Amiga_OpenTTD/build/make-info.py writes (78 + 20 bytes, big-endian), which is the one proven on the target.

The picture is drawn by this script for those four pens (see draw_beaver) - not converted from the game's art.
do_StackSize is 1 MB: the game recurses through its scene graph and the default 4 KB hangs a Workbench launch.
"""
import argparse
import os
import struct
import sys

from PIL import Image

WB_PALETTE = [(0x95, 0x95, 0x95), (0x00, 0x00, 0x00), (0xFF, 0xFF, 0xFF), (0x3B, 0x67, 0xA2)]
WBTOOL = 3
STACK_SIZE = 1000000
NO_ICON_POSITION = -0x80000000


def be(fmt, *vals):
    return struct.pack(">" + fmt, *vals)


def draw_beaver():
    """The icon is DRAWN for the four pens, not converted. The first attempt quantised the baked beaver sprite by
    brightness and produced a black blob: the model is flat brown, so there are no three tones in it to find.
    Pixel art at this size is a handful of shapes - a head, two ears, eyes, a nose, the two teeth that say
    "beaver" - each in a pen chosen by hand. Workbench pixels are about twice as tall as wide on a PAL screen in
    640x256, so the head is drawn wide on purpose: it comes out round on the real thing."""
    from PIL import ImageDraw
    w, h = 46, 30
    img = Image.new("P", (w, h), 0)
    d = ImageDraw.Draw(img)
    GREY, BLACK, WHITE, BLUE = 0, 1, 2, 3
    # ears first, the head overlaps them
    for ex in (7, 31):
        d.ellipse((ex, 0, ex + 8, 7), fill=BLUE, outline=BLACK)
        d.ellipse((ex + 3, 2, ex + 5, 4), fill=BLACK)
    d.ellipse((3, 3, 42, 25), fill=BLUE, outline=BLACK)          # head
    d.ellipse((11, 9, 17, 14), fill=WHITE, outline=BLACK)        # eyes
    d.ellipse((28, 9, 34, 14), fill=WHITE, outline=BLACK)
    d.rectangle((14, 11, 15, 12), fill=BLACK)
    d.rectangle((31, 11, 32, 12), fill=BLACK)
    d.ellipse((14, 14, 31, 23), fill=WHITE, outline=BLACK)       # muzzle
    d.ellipse((19, 14, 26, 17), fill=BLACK)                      # nose
    d.rectangle((18, 22, 22, 28), fill=WHITE, outline=BLACK)     # the teeth
    d.rectangle((23, 22, 27, 28), fill=WHITE, outline=BLACK)
    px = img.load()
    return [[px[x, y] for x in range(w)] for y in range(h)], w, h


def pack_bitplanes(grid, w, h, depth):
    bpr = ((w + 15) // 16) * 2
    out = bytearray()
    for plane in range(depth):
        for y in range(h):
            row = bytearray(bpr)
            for x in range(w):
                if (grid[y][x] >> plane) & 1:
                    row[x >> 3] |= 0x80 >> (x & 7)
            out += row
    return bytes(out)


def build(grid, w, h):
    g = be("I", 0) + be("hh", 0, 0) + be("hh", w, h)
    g += be("H", 0x0004 | 0x0000)  # GFLG_GADGIMAGE | GFLG_GADGHCOMP: one image, highlighted by complement
    g += be("H", 0x0001 | 0x0002)  # GACT_RELVERIFY | GACT_GADGIMMEDIATE
    g += be("H", 0x0001)           # GTYP_BOOLGADGET
    g += be("I", 1) + be("I", 0) + be("I", 0) + be("i", 0) + be("I", 0) + be("H", 0) + be("I", 0)
    assert len(g) == 44
    do = be("H", 0xE310) + be("H", 1) + g + be("B", WBTOOL) + be("B", 0)
    do += be("I", 0)  # do_DefaultTool
    do += be("I", 0)  # do_ToolTypes: none
    do += be("i", NO_ICON_POSITION) + be("i", NO_ICON_POSITION) + be("I", 0) + be("I", 0) + be("i", STACK_SIZE)
    assert len(do) == 78
    image = be("hh", 0, 0) + be("hh", w, h) + be("h", 2) + be("I", 1) + be("B", 0x03) + be("B", 0x00) + be("I", 0)
    assert len(image) == 20
    return do + image + pack_bitplanes(grid, w, h, 2)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="data_amiga/BobrHopper.info")
    ap.add_argument("--preview", default="out/check/amiga/icon.png")
    args = ap.parse_args()
    grid, w, h = draw_beaver()
    data = build(grid, w, h)
    with open(args.out, "wb") as f:
        f.write(data)
    prev = Image.new("RGB", (w, h))
    for y in range(h):
        for x in range(w):
            prev.putpixel((x, y), WB_PALETTE[grid[y][x]])
    prev.resize((w * 4, h * 8), Image.NEAREST).save(args.preview)  # as a 640x256 Workbench shows it
    print("icon %dx%d, 2 planes, %d bytes -> %s (preview %s)" % (w, h, len(data), args.out, args.preview))


if __name__ == "__main__":
    main()
