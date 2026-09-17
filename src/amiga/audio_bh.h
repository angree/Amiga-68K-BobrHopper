/* Sound effects on Paula (task E2).
 *
 * The container is produced by tools/make_amiga_audio.py: 8-bit signed samples with a precomputed Paula period,
 * big-endian, so the 68k reads the table where it lies. See that tool for the format.
 *
 * How this plays sound, and why it is this shape:
 *   - The samples live in FAST RAM (one allocation for the whole file). Only the bytes Paula is actually fetching
 *     have to be in CHIP RAM, and chip is the scarce, slow memory on this machine.
 *   - Two chip buffers, one per SFX channel, each big enough for the longest sound; playing copies into one and
 *     starts a DMA write. Channels 2 and 3 are left for streamed music.
 *   - When both channels are busy the OLDEST is taken over. The OpenTTD port drops the sound instead, which is
 *     audible as a missing hop; a hopping game needs its most recent sound more than its oldest.
 *
 * Plain C, no Amiga types in this header: Amiga headers and the game's C++ must never meet.
 */
#ifndef BH_AUDIO_H
#define BH_AUDIO_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned long offset;      /* byte offset of the samples within the loaded file */
    unsigned long bytes;       /* 8-bit signed samples, always an even count (Paula counts words) */
    unsigned short period;     /* PAL Paula period: 3546895 / sample rate */
    unsigned short nameOffset; /* offset of the NUL-terminated name within the file */
} BHSoundEntry;

typedef struct {
    unsigned char *data; /* the whole file, one allocation in fast RAM */
    unsigned long bytes;
    int count;
    const BHSoundEntry *entries;
} BHSounds;

/* Load the container. Returns 0 and logs why on failure; the game then runs silent rather than not at all. */
int bh_sounds_load(BHSounds *s, const char *path);
void bh_sounds_free(BHSounds *s);

/* Index of a sound by name, or -1. Linear: meant for startup, never for a frame. */
int bh_sounds_find(const BHSounds *s, const char *name);

/* Open audio.device and take the channels. Returns 0 if sound is unavailable - every call below is then a safe
 * no-op, so a machine with Paula already claimed still plays the game. */
int bh_audio_open(const BHSounds *s);
void bh_audio_close(void);

/* Start `id` on a free SFX channel, or take over the oldest. volume is 0..64 (Paula's own scale). */
void bh_audio_play(const BHSounds *s, int id, int volume);

/* Call once a frame: reaps finished channels so they can be reused. Never blocks. */
void bh_audio_service(void);

#ifdef __cplusplus
}
#endif

#endif /* BH_AUDIO_H */
