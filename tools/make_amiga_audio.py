#!/usr/bin/env python3
"""Convert our baked sounds into what Paula can play directly (task E1).

    python tools/make_amiga_audio.py [--data data_sf2000] [--out data_amiga] [--rate 22050]
                                     [--header src/amiga/sound_ids.h]

Paula plays 8-BIT SIGNED samples straight out of memory by DMA, at a rate set by a period register.
There is no mixer and no resampler on this machine and no CPU to spare for one, so the conversion
happens here, once, on the host:

  * 16-bit -> 8-bit signed (the only format the hardware takes),
  * the playback PERIOD is computed per sound and stored with it, so the runtime never divides,
  * lengths are rounded up to an even number of bytes, because Paula's length register counts WORDS.

Period: the PAL Paula clock is 3546895 Hz, so period = clock / sample_rate. A period below about 124
is faster than DMA can feed, which is why nothing here is baked above ~28 kHz; our sounds are 22050 Hz
(period 161), comfortably inside that.

Input .snd (tools/bake_audio.py), little-endian:
    char[4] 'CRS1'; u32 sample_rate; u32 frame_count; frame_count x s16

Output data_amiga/sounds.bhs, BIG-endian so the 68k reads it where it lies:
     0  char[4] 'BHSF'
     4  u16 version (1), u16 count
     8  entries[count]: u32 offset, u32 bytes, u16 period, u16 nameOffset
    ...  name blob (NUL-terminated names, referenced by nameOffset)
    ...  sample data, 8-bit signed, each sound starting at an even offset

The runtime keeps this in FAST RAM and copies a sound into a small chip-RAM buffer to play it: only
the bytes Paula is actually fetching need to be in chip, and chip is the scarce, slow memory.
"""
import argparse
import os
import struct
import sys

PAL_CLOCK = 3546895
MIN_PERIOD = 124  # below this the hardware cannot fetch fast enough


def read_snd(path):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 12 or data[:4] != b"CRS1":
        return None, None
    rate, frames = struct.unpack("<II", data[4:12])
    pcm = data[12:12 + frames * 2]
    if len(pcm) < frames * 2:
        frames = len(pcm) // 2
    return rate, struct.unpack("<%dh" % frames, pcm[:frames * 2])


def resample(samples, src_rate, dst_rate):
    if dst_rate == src_rate or not samples:
        return samples
    out = []
    step = float(src_rate) / float(dst_rate)
    pos = 0.0
    n = len(samples)
    while pos < n - 1:
        i = int(pos)
        frac = pos - i
        a, b = samples[i], samples[i + 1]
        out.append(int(a + (b - a) * frac))
        pos += step
    return out


def to_signed8(samples):
    out = bytearray(len(samples))
    for i, s in enumerate(samples):
        v = (s + 128) >> 8          # round to nearest, then take the top 8 bits
        if v < -128:
            v = -128
        elif v > 127:
            v = 127
        out[i] = v & 0xFF
    return bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", default="data_sf2000")
    ap.add_argument("--out", default="data_amiga")
    ap.add_argument("--rate", type=int, default=22050, help="playback rate; lower halves the memory")
    ap.add_argument("--header", default="src/amiga/sound_ids.h")
    args = ap.parse_args()

    period = int(round(PAL_CLOCK / float(args.rate)))
    if period < MIN_PERIOD:
        sys.exit("rate %d needs period %d, below the hardware minimum %d" % (args.rate, period, MIN_PERIOD))

    src_dir = os.path.join(args.data, "sounds")
    if not os.path.isdir(src_dir):
        sys.exit("no sounds in %s" % src_dir)
    names = sorted(f[:-4] for f in os.listdir(src_dir) if f.endswith(".snd"))
    if not names:
        sys.exit("no .snd files in %s" % src_dir)

    blobs, periods, kept = [], [], []
    for name in names:
        rate, samples = read_snd(os.path.join(src_dir, name + ".snd"))
        if rate is None:
            print("skipping %s: not a CRS1 file" % name)
            continue
        samples = resample(samples, rate, args.rate)
        pcm = to_signed8(samples)
        if len(pcm) % 2:
            pcm += b"\x00"  # Paula counts words
        blobs.append(pcm)
        periods.append(period)
        kept.append(name)

    name_blob = bytearray()
    name_offsets = []
    for name in kept:
        name_offsets.append(len(name_blob))
        name_blob += name.encode("ascii") + b"\x00"
    if len(name_blob) % 2:
        name_blob += b"\x00"

    header_bytes = 8
    table_bytes = 12 * len(kept)
    data_start = header_bytes + table_bytes + len(name_blob)

    out = bytearray()
    out += b"BHSF"
    out += struct.pack(">HH", 1, len(kept))
    offset = data_start
    for pcm, per, noff in zip(blobs, periods, name_offsets):
        out += struct.pack(">IIHH", offset, len(pcm), per, header_bytes + table_bytes + noff)
        offset += len(pcm)
    out += name_blob
    for pcm in blobs:
        out += pcm

    os.makedirs(args.out, exist_ok=True)
    path = os.path.join(args.out, "sounds.bhs")
    with open(path, "wb") as f:
        f.write(out)

    os.makedirs(os.path.dirname(args.header), exist_ok=True)
    with open(args.header, "w", encoding="utf-8") as f:
        f.write("// Generated by tools/make_amiga_audio.py - do not edit.\n")
        f.write("// Sound ids, so the game never looks a sound up by name while it is running.\n")
        f.write("#ifndef BH_SOUND_IDS_H\n#define BH_SOUND_IDS_H\n\n")
        f.write("#define BH_SOUND_COUNT %d\n" % len(kept))
        f.write("#define BH_SOUND_PERIOD %d  /* PAL Paula: %d Hz */\n\n" % (period, args.rate))
        for i, name in enumerate(kept):
            f.write("#define %-34s %d\n" % ("SND_" + name.upper(), i))
        f.write("\ntypedef struct { const char *name; short id; } BHSoundName;\n")
        f.write("/* Include from exactly ONE translation unit. */\n")
        f.write("static const BHSoundName bh_sound_names[] = {\n")
        for i, name in enumerate(kept):
            f.write('    {"%s", %d},\n' % (name, i))
        f.write("};\n#define BH_SOUND_NAME_COUNT %d\n" % len(kept))
        f.write("\n#endif\n")

    total = sum(len(b) for b in blobs)
    print("%d sounds, %d bytes of samples at %d Hz (period %d) -> %s"
          % (len(kept), total, args.rate, period, path))
    print("ids -> %s" % args.header)


if __name__ == "__main__":
    main()
