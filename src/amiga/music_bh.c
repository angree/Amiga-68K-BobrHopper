/* Streaming music. See music_bh.h for why it is streamed and where the pieces come from. */
#include "music_bh.h"

#include <stdio.h>

#include "amiga_adpcm.h"
#include "amiga_audio.h"

#define PAL_CLOCK 3546895
/* 4096 samples per buffer is about a third of a second at 11 kHz: far more headroom than a frame needs even
 * at the 21 fps this machine actually manages, and only a few kilobytes of chip RAM per buffer. */
#define BH_MUSIC_CHUNK 4096

static AdpcmStream *g_stream;
static int g_active;

/* Called by AmigaAudio_MusicService when a buffer has drained. Returning 0 would end the stream, so the track
 * is rewound and decoding continues - the game's music loops for as long as it is playing. */
static int refill(void *ud, signed char *dst, int max)
{
    int got;
    (void)ud;
    if (!g_stream) return 0;
    got = Adpcm_Decode(g_stream, dst, max);
    if (got <= 0) {
        Adpcm_Rewind(g_stream);
        got = Adpcm_Decode(g_stream, dst, max);
    }
    return got;
}

int bh_music_start(const char *path, int volume)
{
    int rate, period;

    bh_music_stop();

    g_stream = Adpcm_Open(path);
    if (!g_stream) {
        printf("music: cannot open %s - playing without music\n", path);
        return 0;
    }
    rate = Adpcm_Rate(g_stream);
    if (rate <= 0) rate = 11025;
    period = PAL_CLOCK / rate;
    if (period < 124) period = 124; /* the hardware cannot fetch faster than this */

    if (!AmigaAudio_MusicStart(period, BH_MUSIC_CHUNK, refill, 0)) {
        printf("music: Paula would not start the stream\n");
        Adpcm_Close(g_stream);
        g_stream = 0;
        return 0;
    }
    AmigaAudio_MusicSetVolume(volume);
    g_active = 1;
    printf("music: streaming %s at %d Hz (period %d), %d-sample buffers\n", path, rate, period, BH_MUSIC_CHUNK);
    return 1;
}

void bh_music_service(void)
{
    if (!g_active) return;
    AmigaAudio_MusicService();
}

void bh_music_stop(void)
{
    if (g_active) {
        AmigaAudio_MusicStop();
        g_active = 0;
    }
    if (g_stream) {
        Adpcm_Close(g_stream);
        g_stream = 0;
    }
}

void bh_music_volume(int volume)
{
    if (g_active) AmigaAudio_MusicSetVolume(volume);
}

int bh_music_active(void) { return g_active; }
