#!/usr/bin/env python3
"""Bake the game's logo into the Amiga sprite set.

    python tools/make_amiga_logo.py [--width 256] [--sprites out/check/amiga/sprites]

WHY: the Amiga title screen drew the game's name as TEXT in our own font on a black panel. The console versions
draw the real logo - the pixel-art "BOBR HOPPER" of tools/make_logo.py - over the live scene, and the user asked
for menus that look like those versions, not for an approximation of them.

The logo ships as data/images/title.tex, which is an RLE'd RGBA image (src/engine/assets.cpp: 'CRT1', little
endian w/h/channels/runs, then run-length pairs). It carries FIVE opaque colours, and the sprite palette has well
over a hundred free entries, so it goes in exactly - no quantisation, no dithering, no new format: this writes a
PNG into the sprite directory and adds one line to sprites.txt, after which the ordinary packer treats the logo
like any other sprite.

Scaling is NEAREST on purpose. The logo is pixel art with a hard black outline; a smooth filter would invent
dozens of intermediate colours, every one of which would then have to fit in the palette and be blitted through
the same masked copy.
"""
import argparse
import os
import struct
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("needs Pillow: python -m pip install Pillow")

LOGO_NAME = "logo"


def load_crt1(path):
    """The CRT1 reader of src/engine/assets.cpp, in Python."""
    with open(path, "rb") as f:
        d = f.read()
    if d[:4] != b"CRT1":
        sys.exit("%s is not a CRT1 texture" % path)
    w, h, channels, _zero, runs = struct.unpack("<HHBBI", d[4:14])
    px = bytearray()
    at = 14
    for _ in range(runs):
        n = struct.unpack("<H", d[at:at + 2])[0]
        at += 2
        colour = d[at:at + channels]
        at += channels
        px += colour * n
    want = w * h * channels
    if len(px) != want:
        sys.exit("%s unpacked to %d bytes, expected %d" % (path, len(px), want))
    return Image.frombytes("RGBA" if channels == 4 else "RGB", (w, h), bytes(px)).convert("RGBA")


def rewrite_index(path, entry_line):
    """Add (or replace) the logo's line in sprites.txt, leaving the baker's own lines alone.

    The baker rewrites this file every time it runs, so this is deliberately idempotent: run the baker, run this,
    run the packer - in that order - and the logo is there exactly once."""
    lines = []
    if os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            lines = [l.rstrip("\n") for l in f]
    lines = [l for l in lines if not l.startswith(LOGO_NAME + " ")]
    lines.append(entry_line)
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tex", default="data/images/title.tex")
    ap.add_argument("--sprites", default="out/check/amiga/sprites")
    # 172, NOT 256. The shared home screen fits the 800x464 title into a 512x200 box of its 640x480 layout:
    # scale = min(512/800, 200/464) = 0.431 -> 345x200, which is 172x100 on this 320-pixel screen. The default
    # used to be 256 and the right size was passed by hand - until the day it was not, and the user got a logo
    # half as big again as on the consoles.
    ap.add_argument("--width", type=int, default=172, help="target width in pixels (172 = the console layout)")
    args = ap.parse_args()

    img = load_crt1(args.tex)
    box = img.getbbox()  # the texture is mostly transparent margin
    if box:
        img = img.crop(box)
    scale = args.width / float(img.width)
    size = (args.width, max(1, int(round(img.height * scale))))
    img = img.resize(size, Image.NEAREST)

    # Anything half-transparent becomes fully transparent: the blitter has no blending, so a soft edge would
    # otherwise turn into a fringe of solid pixels around every letter.
    px = img.load()
    colours = set()
    for y in range(img.height):
        for x in range(img.width):
            r, g, b, a = px[x, y]
            if a < 128:
                px[x, y] = (0, 0, 0, 0)
            else:
                px[x, y] = (r, g, b, 255)
                colours.add((r, g, b))

    os.makedirs(args.sprites, exist_ok=True)
    out = os.path.join(args.sprites, "%s_r0.png" % LOGO_NAME)
    img.save(out)
    # anchor 0,0: the title screen positions the logo by its top-left corner, not by a model origin
    rewrite_index(os.path.join(args.sprites, "sprites.txt"),
                  "%s 0 -1 %d %d 0 0" % (LOGO_NAME, img.width, img.height))
    print("logo %dx%d, %d colours -> %s" % (img.width, img.height, len(colours), out))
    print("sprites.txt updated; now run tools/pack_amiga_sprites.py")


if __name__ == "__main__":
    main()
