/* Sound effects on Paula. See audio_bh.h for the design and the container format.
 *
 * The hard-won rule this file inherits: audio.device requests go out with BeginIO() only, never SendIO()/DoIO() -
 * those clear io_Flags and wipe ADIOF_PERVOL, leaving the channel at volume 0 while every write "succeeds". That
 * cost the OpenTTD port a day of silence. All of it is hidden behind AmigaAudio_* (amiga_audio.c); this file only
 * decides WHAT to play and WHERE the bytes live.
 */
#include "audio_bh.h"

#include <exec/memory.h>
#include <exec/types.h>
#include <proto/exec.h>
#include <stdio.h>
#include <string.h>

#include "amiga_audio.h"

#define BH_SFX_CHANNELS 2 /* 0 and 1; 2 and 3 are reserved for streamed music */
#define BH_HEADER_BYTES 8

static unsigned short be16(const unsigned char *p) { return (unsigned short)((p[0] << 8) | p[1]); }

static struct {
    int open;
    void *chip[BH_SFX_CHANNELS];   /* one buffer per channel, in chip RAM */
    unsigned long chipBytes;
    unsigned long startedAt[BH_SFX_CHANNELS]; /* a counter, so "oldest" is well defined */
    unsigned long counter;
} g;

int bh_sounds_load(BHSounds *s, const char *path)
{
    FILE *f;
    long size;

    s->data = 0;
    s->count = 0;
    s->entries = 0;

    f = fopen(path, "rb");
    if (!f) {
        printf("audio: cannot open %s\n", path);
        return 0;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= BH_HEADER_BYTES) {
        printf("audio: %s is too small\n", path);
        fclose(f);
        return 0;
    }
    /* Total free says nothing about whether ONE block this size exists. */
    if (AvailMem(MEMF_ANY | MEMF_LARGEST) < (unsigned long)size) {
        printf("audio: need %ld bytes in one block\n", size);
        fclose(f);
        return 0;
    }
    s->data = (unsigned char *)AllocVec((unsigned long)size, MEMF_ANY);
    if (!s->data) {
        printf("audio: AllocVec(%ld) failed\n", size);
        fclose(f);
        return 0;
    }
    if (fread(s->data, 1, (size_t)size, f) != (size_t)size) {
        printf("audio: short read on %s\n", path);
        fclose(f);
        bh_sounds_free(s);
        return 0;
    }
    fclose(f);
    s->bytes = (unsigned long)size;

    if (s->data[0] != 'B' || s->data[1] != 'H' || s->data[2] != 'S' || s->data[3] != 'F') {
        printf("audio: %s is not a BHSF file\n", path);
        bh_sounds_free(s);
        return 0;
    }
    if (be16(s->data + 4) != 1) {
        printf("audio: %s is version %d, expected 1\n", path, (int)be16(s->data + 4));
        bh_sounds_free(s);
        return 0;
    }
    s->count = (int)be16(s->data + 6);
    s->entries = (const BHSoundEntry *)(s->data + BH_HEADER_BYTES);
    if ((unsigned long)(BH_HEADER_BYTES + s->count * 12) > s->bytes) {
        printf("audio: %s claims %d sounds but is only %lu bytes\n", path, s->count, s->bytes);
        bh_sounds_free(s);
        return 0;
    }
    printf("audio: %d sounds, %lu bytes\n", s->count, s->bytes);
    return 1;
}

void bh_sounds_free(BHSounds *s)
{
    if (s->data) FreeVec(s->data);
    s->data = 0;
    s->count = 0;
    s->entries = 0;
    s->bytes = 0;
}

int bh_sounds_find(const BHSounds *s, const char *name)
{
    int i;
    if (!s->data) return -1;
    for (i = 0; i < s->count; i++) {
        const char *n = (const char *)(s->data + s->entries[i].nameOffset);
        if (strcmp(n, name) == 0) return i;
    }
    return -1;
}

int bh_audio_open(const BHSounds *s)
{
    int i;
    unsigned long longest = 0;

    memset(&g, 0, sizeof(g));
    if (!s->data || s->count <= 0) return 0;
    if (!AmigaAudio_Open()) {
        printf("audio: Paula unavailable - the game runs silent\n");
        return 0;
    }

    for (i = 0; i < s->count; i++)
        if (s->entries[i].bytes > longest) longest = s->entries[i].bytes;
    if (longest & 1) longest++;

    for (i = 0; i < BH_SFX_CHANNELS; i++) {
        g.chip[i] = AmigaAudio_AllocSample(longest);
        if (!g.chip[i]) {
            printf("audio: only %lu bytes of chip RAM per channel available, wanted %lu\n", (unsigned long)0, longest);
            bh_audio_close();
            return 0;
        }
    }
    g.chipBytes = longest;
    g.open = 1;
    printf("audio: open, %d channels, %lu bytes of chip per channel\n", BH_SFX_CHANNELS, longest);
    return 1;
}

void bh_audio_close(void)
{
    int i;
    for (i = 0; i < BH_SFX_CHANNELS; i++) {
        if (g.chip[i]) AmigaAudio_FreeSample(g.chip[i]);
        g.chip[i] = 0;
    }
    AmigaAudio_Close();
    g.open = 0;
}

void bh_audio_service(void)
{
    int i;
    if (!g.open) return;
    for (i = 0; i < BH_SFX_CHANNELS; i++) AmigaAudio_ChannelIdle(i); /* reaps finished requests */
}

void bh_audio_play(const BHSounds *s, int id, int volume)
{
    int ch, pick = -1;
    unsigned long oldest = 0;
    const BHSoundEntry *e;

    if (!g.open || id < 0 || id >= s->count) return;
    e = &s->entries[id];
    if (!e->bytes || e->bytes > g.chipBytes) return;

    for (ch = 0; ch < BH_SFX_CHANNELS; ch++) {
        if (AmigaAudio_ChannelIdle(ch)) {
            pick = ch;
            break;
        }
    }
    if (pick < 0) {
        /* All busy: take over the oldest. A hopping game needs its newest sound more than its oldest. */
        pick = 0;
        oldest = g.startedAt[0];
        for (ch = 1; ch < BH_SFX_CHANNELS; ch++)
            if (g.startedAt[ch] < oldest) {
                oldest = g.startedAt[ch];
                pick = ch;
            }
    }

    memcpy(g.chip[pick], s->data + e->offset, (size_t)e->bytes);
    g.startedAt[pick] = ++g.counter;
    AmigaAudio_Play(pick, g.chip[pick], e->bytes, (int)e->period, volume);
}
