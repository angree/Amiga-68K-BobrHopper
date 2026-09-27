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


def _lum(c):
    """Rec.601 luminance, the one the eye agrees with best for "is this the light face or the dark one"."""
    return (c[0] * 299 + c[1] * 587 + c[2] * 114) / 1000.0


def nearest(color, palette, first=None):
    if first is None:
        first = ART_FIRST
    best, bestd = first, None
    for i, p in enumerate(palette):
        if i < first:
            continue  # never map art onto the transparent key, the menus' colours or the system's registers
        d = (color[0] - p[0]) ** 2 + (color[1] - p[1]) ** 2 + (color[2] - p[2]) ** 2
        if bestd is None or d < bestd:
            best, bestd = i, d
    return best


# ---------------------------------------------------------------------------- OCS / Extra Half-Brite
#
# SIX bitplanes on a chipset that has no more: 64 pens, of which only the first 32 are ours to choose. Pen 32 + n
# always shows half of pen n, and the halving is the CHIPSET's, done on the four bits it actually stores - so the
# palette is computed in nibbles here and written out as nibble * 17. Computing it in eight bits and rounding at
# the end puts the halves in the wrong place: half of 0x8F is 0x47, but half of the nibble 8 is 4, which shows as
# 0x44.
#
# WHAT IS PINNED, AND WHY IT SURVIVES THE OPTIMISER. The mouse pointer is hardware sprite 0 and reads registers
# 17, 18 and 19 whatever we do; the screen bar's pens are 10..12 (src/amiga/amiga_gfx.c, g_screen_pens); the menus
# and the sky have their own fixed indices so the Amiga shows the console versions' colours. Those COLOURS are
# pinned. The art is free to draw WITH them - that is the difference from the 256-colour sets, where art was kept
# out of the reserved indices as a tidiness rule that cost two entries out of 256 and would cost twenty out of 64.
# Pinning the colour is what protects the pointer and the bar; forbidding the art to reference it protects nothing.
EHB_FREE_SLOTS = [14, 15] + list(range(20, 32))  # 14, 15: greys nothing draws with (see SYS_SLOTS)

# THREE COLOURS THE OPTIMISER IS NOT ALLOWED TO AVERAGE AWAY. A fit that minimises total error over 500,000
# pixels will happily spend nothing on a colour that covers a few thousand of them, and it did:
#
#   - the lily pads are 96% three greens (18A652, 088E42, 108A42) and every one of them landed on 116677, a
#     dark teal, because no green was worth a pen on its own. The pads came out sea-coloured;
#   - the beaver's brown (633C21) and the log's (734131) are close enough that the fit merged them into one
#     pen, so the animal was exactly the colour of the thing it stands on and vanished into it.
#
# Both are the author's own reports. A pinned colour costs one of the fourteen free base pens and buys its
# half-bright as well, which for the green is the darker shade the pads already use.
ART_PINS = [
    ("lily green", (0x11, 0x99, 0x44)),
    ("beaver brown", (0x66, 0x44, 0x22)),
    # TWO tones for the logs, and both deliberately LIGHTER than the beaver. In the artwork a log is 734131 and
    # the beaver 633C21 - the same brown to within a nibble - so no amount of "pick the nearest pen that is not
    # the beaver's" separates them; the log has to be moved on purpose. Light wood under a dark animal is also
    # the right way round: the beaver stands ON the log, so the log is the background and gives way.
    ("log light", (0xBB, 0x99, 0x77)),
    ("log dark", (0x88, 0x66, 0x55)),
    # A RED. The title logo (the beaver's cap, the cock's comb) is dithered onto these 64 pens, and without a
    # saturated red it came out brown; the red cars and the train are better for it too.
    ("red", (0xDD, 0x22, 0x22)),
]

# THE LOGS ARE DRAWN IN THOSE TWO PENS AND NOTHING ELSE. Banning the beaver's pen was not enough - the search
# just moved the log to 665522, one nibble of green away from it. Banning a whole radius was not enough either:
# what was left that was nearest to a dark brown was 777722, an olive, and the pinned wood tone still lost by a
# handful of squared units. A whitelist says what the log IS instead of what it may not be, and two tones is
# what a flat-shaded log needs: a lit face and a shaded one.
LOG_PINS = (2, 3)  # indices into ART_PINS
LOG_PREFIX = "log_"


def _nib(c):
    """RGB8 -> the nibbles an OCS colour register holds. int(), because the Lloyd step hands this averages."""
    return tuple(max(0, min(15, int((v + 8.5) // 17))) for v in c)


def _rgb(n):
    """nibbles -> the RGB8 those registers display."""
    return tuple(v * 17 for v in n)


def _half(n):
    """what the chipset shows for pen 32 + n."""
    return tuple(v >> 1 for v in n)


def _sqd(a, b):
    return (a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2


def build_ehb_palette(counts, reserved, rounds=24):
    """64 EHB pens: the reserved colours where they must be, the rest chosen to fit the art.

    `reserved` is the fixed part of the base half, index -> RGB8. Returns (64 RGB8 entries, pin index map).
    """
    base = [None] * 32
    for i, rgb in reserved.items():
        base[i] = _nib(rgb)
    free = [i for i in range(32) if base[i] is None]
    assert free == EHB_FREE_SLOTS, (free, EHB_FREE_SLOTS)

    # The pinned art colours take the first free slots and stay there; the fit moves only what is left.
    pin_index = {}
    for n, (name, rgb) in enumerate(ART_PINS):
        base[free[n]] = _nib(rgb)
        pin_index[name] = free[n]
    free = free[len(ART_PINS):]

    art = [(c, n) for c, n in counts.items() if n > 0]
    art.sort(key=lambda cn: -cn[1])

    def pens():
        out = []
        for i in range(32):
            out.append(_rgb(base[i]) if base[i] else None)
        for i in range(32):
            out.append(_rgb(_half(base[i])) if base[i] else None)
        return out

    # Start where the art is densest, in the colours the reserved pens and their halves leave worst covered.
    fixed = [_rgb(b) for b in base if b] + [_rgb(_half(b)) for b in base if b]
    for slot in free:
        worst, worstd = None, -1
        have = fixed + [_rgb(base[i]) for i in free if base[i]] + [_rgb(_half(base[i])) for i in free if base[i]]
        for c, n in art[:400]:
            d = min(_sqd(c, h) for h in have) * n
            if d > worstd:
                worstd, worst = d, c
        base[slot] = _nib(worst)

    # Lloyd, with the half-brights pulling on their own base. A base serves two pens: pixels that landed on
    # `b` want b where they are, pixels that landed on `b >> 1` want b at twice where they are, and the second
    # group pulls a quarter as hard because moving b by d moves its half by d/2.
    for _ in range(rounds):
        acc = {i: [0.0, 0.0, 0.0, 0.0] for i in free}  # r, g, b, weight
        for c, n in art:
            p = pens()
            bi, bd = None, None
            for i, rgb in enumerate(p):
                if rgb is None:
                    continue
                d = _sqd(c, rgb)
                if bd is None or d < bd:
                    bd, bi = d, i
            slot = bi & 31
            if slot not in acc:
                continue
            a = acc[slot]
            if bi < 32:
                a[0] += c[0] * n; a[1] += c[1] * n; a[2] += c[2] * n; a[3] += n
            else:
                a[0] += c[0] * n * 0.5; a[1] += c[1] * n * 0.5; a[2] += c[2] * n * 0.5; a[3] += n * 0.25
        moved = 0
        for slot in free:
            a = acc[slot]
            if a[3] <= 0:
                continue
            want = _nib((a[0] / a[3], a[1] / a[3], a[2] / a[3]))
            if want != base[slot]:
                base[slot] = want
                moved += 1
        if not moved:
            break

    out = [_rgb(b) for b in base] + [_rgb(_half(b)) for b in base]
    return out, pin_index


FLOOR_NAMES = ("grass_0", "grass_1", "road_0", "road_1", "river", "railroad")

# SHADOW TWINS (256-colour sets only). The Amiga draws the simple shadows by darkening what is already on screen
# through a table: pixel = shade[pixel]. For that to look right, every colour a shadow falls on - the floors, the
# logs, the lily pads - needs a darker copy of itself in the palette, at the same factor the console ports multiply
# their framebuffer by (src/game/scene_render.cpp shadowFactorForTopFaces: (1.8 / 2.632) ^ (1 / 2.2) = 0.841).
# The twins go AFTER the art and no art pixel ever uses them, so darkening a darkened pixel changes nothing - two
# shadows that overlap do not come out darker. The header's fourth word says where they start (0 = none).
# EHB sets need none: the chipset's half-brite IS a shadow table, pen N + 32 at half the brightness.
SHADE = 0.841

# THE TITLE LOGO (tools/make_amiga_logo.py) is a painted picture with thousands of colours, not flat-shaded art.
# It is kept out of the art's colour count, so the game keeps exactly the palette it had; the 256-colour sets then
# give it whatever entries are left (a median cut of its own pixels), and every logo pixel takes the nearest of the
# whole palette. EHB has no entries to spare, so there the logo is dithered (Floyd-Steinberg) onto the 64 pens.
LOGO_NAME = "logo"
SHADE_PREFIXES = FLOOR_NAMES + ("log_", "lily_pad")


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


def dither_to(img, palette, first):
    """Floyd-Steinberg onto palette[first:]; transparent stays 0. For the logo on a 64-pen EHB screen."""
    w, h = img.size
    px = img.load()
    err = [[0.0, 0.0, 0.0] for _ in range(w * h)]
    out = bytearray(w * h)
    cands = [(i, palette[i]) for i in range(first, len(palette))]
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a < 128:
                continue
            e = err[y * w + x]
            want = (min(255, max(0, r + e[0])), min(255, max(0, g + e[1])), min(255, max(0, b + e[2])))
            bi, bd = first, None
            for i, c in cands:
                d = (want[0] - c[0]) ** 2 + (want[1] - c[1]) ** 2 + (want[2] - c[2]) ** 2
                if bd is None or d < bd:
                    bi, bd = i, d
            out[y * w + x] = bi
            c = palette[bi]
            de = (want[0] - c[0], want[1] - c[1], want[2] - c[2])
            for dx, dy, f in ((1, 0, 7 / 16.0), (-1, 1, 3 / 16.0), (0, 1, 5 / 16.0), (1, 1, 1 / 16.0)):
                xx, yy = x + dx, y + dy
                if 0 <= xx < w and yy < h and px[xx, yy][3] >= 128:
                    t = err[yy * w + xx]
                    t[0] += de[0] * f
                    t[1] += de[1] * f
                    t[2] += de[2] * f
    return bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--in", dest="indir", default="out/check/amiga/sprites")
    ap.add_argument("--out", dest="outdir", default="data_amiga")
    ap.add_argument("--header", default="src/amiga/sprite_ids.h")
    # The 640x480 set: its own file name, its own seal width, and a header that must come out IDENTICAL to the
    # 320x240 one - the game has one compiled name table, so both sets must list the same sprites in the same order.
    ap.add_argument("--name", default="sprites.spr")
    ap.add_argument("--seal", type=int, default=1)
    ap.add_argument("--ehb", action="store_true",
                    help="OCS Extra Half-Brite: 64 pens, the upper 32 fixed at half the lower 32 (lores only)")
    args = ap.parse_args()

    entries = load_index(os.path.join(args.indir, "sprites.txt"))
    if not entries:
        sys.exit("no sprites in %s" % args.indir)

    # Pass 1: every opaque colour in every sprite, by how many pixels use it.
    images, counts = [], Counter()
    log_counts = Counter()  # the logs' own, so their two tones can be split where their own shading splits
    shade_counts = Counter()  # colours shadows fall on (SHADE_PREFIXES)
    for e in entries:
        path = sprite_file(args.indir, e)
        img = Image.open(path).convert("RGBA")
        if img.size != (e["w"], e["h"]):
            sys.exit("%s is %dx%d but sprites.txt says %dx%d" % (path, img.size[0], img.size[1], e["w"], e["h"]))
        if e["name"] in FLOOR_NAMES:
            img = seal_floor_edge(img, e, args.seal)
        px = img.load()
        images.append(img)
        if e["name"] == LOGO_NAME:
            continue  # its colours are chosen separately, after the art's (see "the title logo" below)
        for y in range(e["h"]):
            for x in range(e["w"]):
                r, g, b, a = px[x, y]
                if a >= 128:
                    counts[(r, g, b)] += 1
                    if e["name"].startswith(LOG_PREFIX):
                        log_counts[(r, g, b)] += 1
                    if e["name"].startswith(SHADE_PREFIXES):
                        shade_counts[(r, g, b)] += 1

    # Index 0 is the transparent key and is never drawn. Index 1 is the SKY: the game clears the screen to it
    # before anything else, and no sprite contains it (sprites are cropped to the model), so it would otherwise be
    # missing from a palette built only from the art - which is why the first Amiga frame came out magenta.
    # Index 0 is the transparency key AND the colour Intuition shows outside the game's own area, so it must be
    # BLACK. It was magenta, which put a bright pink border around the whole screen on the real machine - the
    # first thing the user said about the running game. Nothing is ever drawn in index 0, so its colour is free
    # to be anything; black is what a border should be.
    sys_cols = [rgb for _, rgb in SYS_SLOTS]
    if args.ehb:
        # EHB decides its own 64 entries: everything pinned stays where the hardware and the menus need it, the
        # fourteen slots nothing draws with are fitted to this set's own art, and 32..63 follow by halving.
        reserved = {0: (0, 0, 0), 1: SKY}
        for i, rgb in enumerate(UI_COLOURS):
            reserved[UI_FIRST + i] = rgb
        for i, (name, rgb) in enumerate(SYS_SLOTS):
            if SYS_FIRST + i not in EHB_FREE_SLOTS:
                reserved[SYS_FIRST + i] = rgb
        palette, pin_index = build_ehb_palette(counts, reserved)
        index_of = {}
        log_index_of = {}
        log_light = pin_index[ART_PINS[LOG_PINS[0]][0]]
        log_dark = pin_index[ART_PINS[LOG_PINS[1]][0]]
        # WHERE THE LOG'S OWN SHADING SPLITS, not where the palette happens to fall. Both pinned tones are
        # lighter than every colour in the artwork, so a nearest-colour search gave the whole log one pen and
        # the shading went flat. Splitting on the log's own luminance keeps the lit face and the shaded one -
        # the midpoint of the range its pixels actually cover, weighted by how many there are.
        log_lums = sorted(_lum(c) for c in log_counts)
        log_split = (log_lums[0] + log_lums[-1]) / 2.0 if log_lums else 0.0
        print("EHB palette: 64 pens, %d chosen for the art, %d pinned (%s), 32 half-brights"
              % (len(EHB_FREE_SLOTS) - len(ART_PINS), len(ART_PINS),
                 ", ".join("%s pen %d" % (n, pin_index[n]) for n, _ in ART_PINS)))
        print("  logs: %d colours split at luminance %.0f into pen %d (lit) and pen %d (shaded)"
              % (len(log_counts), log_split, log_light, log_dark))
        err = wsum = 0
        for c, n in counts.items():
            err += min((c[0] - p[0]) ** 2 + (c[1] - p[1]) ** 2 + (c[2] - p[2]) ** 2 for p in palette) * n
            wsum += n
        print("  fit over %d painted pixels: rms %.1f per channel" % (wsum, (err / wsum / 3.0) ** 0.5))
    else:
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
        log_index_of = {}
        log_light = log_dark = -1
        log_split = 0.0

        print("colours in the art: %d%s" % (len(ordered),
              "" if not merged else "  (%d rarest merged into their nearest neighbour)" % merged))

    # The shadow twins (computed here, placed after the logo's colours) and the title logo's own colours.
    twin_first = 0
    twins = []
    if not args.ehb:
        for c, _ in shade_counts.most_common():
            i = index_of.get(c)
            if i is None:
                continue  # merged away: its pixels use a neighbour's index, which gets its own twin
            t = tuple(int(round(v * SHADE)) for v in palette[i])
            if t not in twins:
                twins.append(t)
        logo = [(e, img) for e, img in zip(entries, images) if e["name"] == LOGO_NAME]
        room = MAX_COLORS - len(palette) - len(twins)
        if logo and room > 0:
            img = logo[0][1]
            opaque = [p[:3] for p in img.getdata() if p[3] >= 128]
            strip = Image.new("RGB", (len(opaque), 1))
            strip.putdata(opaque)
            q = strip.quantize(colors=room, method=Image.Quantize.MEDIANCUT)
            qp = q.getpalette()[:room * 3]
            used = sorted(set(q.getdata()))
            logo_cols = [tuple(qp[i * 3:i * 3 + 3]) for i in used]
            palette.extend(logo_cols)
            print("title logo: %d colours of its own (%d painted pixels)" % (len(logo_cols), len(opaque)))
        room = MAX_COLORS - len(palette)
        if twins and room > 0:
            twin_first = len(palette)
            palette.extend(twins[:room])
        print("shadow twins: %d floor colours, %d twins from index %d%s" % (
            len(shade_counts), min(len(twins), max(room, 0)), twin_first,
            "" if len(twins) <= room else "  (%d had no room - they shade to their nearest)" % (len(twins) - room)))

    # Pass 2: pixels.
    blobs = []
    for e, img in zip(entries, images):
        # The logs are painted in their own two pens and nothing else, so no log pixel can come out the colour
        # of the beaver standing on it. Only the logs: nothing else sits under the animal.
        if args.ehb and e["name"] == LOGO_NAME:
            blobs.append(dither_to(img, palette, 1))
            continue
        is_log = args.ehb and e["name"].startswith(LOG_PREFIX)
        cache = log_index_of if is_log else index_of
        px = img.load()
        data = bytearray(e["w"] * e["h"])
        for y in range(e["h"]):
            row = y * e["w"]
            for x in range(e["w"]):
                r, g, b, a = px[x, y]
                if a < 128:
                    continue  # stays 0 = transparent
                key = (r, g, b)
                i = cache.get(key)
                if i is None:
                    if is_log:
                        i = log_light if _lum(key) >= log_split else log_dark
                    else:
                        # EHB: every pen is fair game, including the pinned ones - their COLOUR is what is
                        # pinned, and drawing white with the pointer's register paints white, it does not
                        # recolour the pointer. Index 0 stays out either way: it is the transparency key.
                        i = nearest(key, palette, first=1 if args.ehb else ART_FIRST)
                    cache[key] = i
                data[row + x] = i
        blobs.append(bytes(data))

    os.makedirs(args.outdir, exist_ok=True)
    header_size = 12 + 768
    table_size = 12 * len(entries)
    offset = header_size + table_size

    out = bytearray()
    out += MAGIC
    out += struct.pack(">HHHH", VERSION, len(entries), len(palette), twin_first)
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
        # The palette SIZE is deliberately not here. build/bake_amiga.sh proves every set lists the same sprites in
        # the same order by comparing these headers byte for byte, and the EHB set has 64 entries where the
        # 256-colour sets have about 170 - a difference that says nothing about the sprite table and would break
        # the comparison that does matter. Nothing in the game ever read the define: the count comes from the .spr
        # the game actually loaded, and the packer prints it (out/check/amiga/<set>/pack.log).
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
