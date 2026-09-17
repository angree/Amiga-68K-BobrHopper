#!/usr/bin/env python3
"""Turn a chunky buffer the Amiga dumped into a PNG I can actually look at.

    python tools/raw2png_amiga.py --raw C:/temp/amiga_bobr/work/frame.raw \
                                  --spr out/check/amiga/data/sprites.spr \
                                  --out out/check/amiga/frame.png [--size 320x240]

WHY THIS EXISTS: a host-side screenshot of the emulator can lie. WinUAE's DirectDraw surface comes
back black through PrintWindow often enough that the sibling GTA port stopped trusting it, and a
black picture of a perfectly healthy game is the most expensive kind of false alarm. The game
therefore writes out its OWN 8-bit chunky buffer - exactly the bytes that are about to go through
c2p - and this converts them with the same palette the game loaded. When the PNG and the emulator
window disagree, the PNG is the one telling the truth about our rendering.

The palette is read from the sprite container (sprites.spr), so the colours here are by construction
the colours the Amiga set on the screen; --pal takes a raw 768-byte palette instead if needed.
"""
import argparse
import struct
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("needs Pillow: python -m pip install Pillow")

HEADER_BYTES = 12
PALETTE_BYTES = 768


def palette_from_spr(path):
    with open(path, "rb") as f:
        head = f.read(HEADER_BYTES + PALETTE_BYTES)
    if len(head) < HEADER_BYTES + PALETTE_BYTES or head[:4] != b"BHSP":
        sys.exit("%s is not a BHSP sprite container" % path)
    version, count, entries, _ = struct.unpack(">HHHH", head[4:12])
    return head[HEADER_BYTES:HEADER_BYTES + PALETTE_BYTES], count, entries


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--raw", required=True, help="chunky buffer dumped by the game (one byte per pixel)")
    ap.add_argument("--spr", help="sprite container to take the palette from")
    ap.add_argument("--pal", help="raw 768-byte palette instead of --spr")
    ap.add_argument("--out", required=True)
    ap.add_argument("--size", default="320x240")
    args = ap.parse_args()

    w, h = (int(v) for v in args.size.lower().split("x"))

    with open(args.raw, "rb") as f:
        data = f.read()
    if len(data) < w * h:
        sys.exit("%s holds %d bytes, need %d for %dx%d" % (args.raw, len(data), w * h, w, h))
    if len(data) > w * h:
        print("note: %s is %d bytes, using the first %d" % (args.raw, len(data), w * h))

    if args.pal:
        with open(args.pal, "rb") as f:
            pal = f.read(PALETTE_BYTES)
    elif args.spr:
        pal, count, entries = palette_from_spr(args.spr)
        print("palette from %s (%d sprites, %d colours)" % (args.spr, count, entries))
    else:
        sys.exit("give --spr or --pal so the colours are the ones the game used")

    img = Image.frombytes("P", (w, h), data[:w * h])
    img.putpalette(pal)
    img.convert("RGB").save(args.out)

    used = sorted(set(data[:w * h]))
    print("wrote %s (%dx%d), %d distinct palette indices used, lowest %d highest %d"
          % (args.out, w, h, len(used), used[0], used[-1]))


if __name__ == "__main__":
    main()
