#!/usr/bin/env python3
"""Convert the music to what the Amiga streams from disk (task E2).

    python tools/make_amiga_music.py [--src data/music] [--out data_amiga/music] [--rate 11025]

The user asked for music STREAMED FROM DISK rather than loaded into RAM, and the Amiga side already has a
decoder for exactly one format: amiga_adpcm.c (from the OpenTTD port) reads IMA/DVI ADPCM in a WAV
container, mono, 4 bits per sample, in 1024-byte blocks, and decodes one block at a time through a small
staging buffer - a whole track is never resident.

So this produces precisely that, with ffmpeg:

    RIFF/WAVE, wFormatTag 0x0011 (IMA ADPCM), 1 channel, nBlockAlign 1024
    -> 2041 samples per block, which is what the decoder's tables assume

Why 11025 Hz by default: music is the least demanding thing on screen and the most expensive thing on the
disk. At 11025 Hz a minute costs about 330 KB instead of 660 KB, and Paula's period (3546895/11025 = 322)
is nowhere near the hardware limit. Pass --rate 22050 for the better-sounding, twice-as-large version.

The effects went the other way (22050 Hz, 8-bit signed, resident in RAM) because they must start instantly
and are short; see tools/make_amiga_audio.py.
"""
import argparse
import os
import subprocess
import sys

PAL_CLOCK = 3546895
BLOCK_SIZE = 1024


def convert(src, dst, rate):
    # -map_metadata -1: no tags. The decoder skips unknown chunks, but a smaller file is a faster seek.
    cmd = ["ffmpeg", "-v", "error", "-y", "-i", src, "-ac", "1", "-ar", str(rate),
           "-c:a", "adpcm_ima_wav", "-block_size", str(BLOCK_SIZE), "-map_metadata", "-1", dst]
    subprocess.run(cmd, check=True)
    return os.path.getsize(dst)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default="data/music", help="source tracks (ogg/wav/mp3)")
    ap.add_argument("--out", default="data_amiga/music")
    ap.add_argument("--rate", type=int, default=11025)
    args = ap.parse_args()

    if not os.path.isdir(args.src):
        sys.exit("no music in %s" % args.src)
    names = sorted(f for f in os.listdir(args.src) if f.lower().endswith((".ogg", ".wav", ".mp3")))
    if not names:
        sys.exit("no tracks in %s" % args.src)

    os.makedirs(args.out, exist_ok=True)
    period = int(round(PAL_CLOCK / float(args.rate)))
    total = 0
    for name in names:
        stem = os.path.splitext(name)[0]
        dst = os.path.join(args.out, stem + ".wav")
        try:
            size = convert(os.path.join(args.src, name), dst, args.rate)
        except subprocess.CalledProcessError as e:
            sys.exit("ffmpeg failed on %s: %s" % (name, e))
        total += size
        print("%-24s -> %-28s %8.1f KB" % (name, stem + ".wav", size / 1024.0))

    print("%d tracks, %.1f MB total, %d Hz (Paula period %d), IMA ADPCM in %d-byte blocks"
          % (len(names), total / (1024.0 * 1024.0), args.rate, period, BLOCK_SIZE))


if __name__ == "__main__":
    main()
