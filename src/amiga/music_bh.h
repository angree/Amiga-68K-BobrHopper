/* Music, streamed from disk (task E2).
 *
 * The user asked for music that is NOT loaded into RAM, and this is the one part of the port where that is a
 * hard requirement rather than a preference: a track is megabytes, chip RAM is two, and the sprites already
 * live in fast RAM.
 *
 * The pieces already exist and are simply joined here:
 *   - amiga_adpcm.c (from the OpenTTD port) decodes IMA/DVI ADPCM WAV one 1024-byte block at a time through a
 *     staging buffer, so the file is read as it plays and never resident.
 *   - AmigaAudio_MusicStart/Service/Stop (amiga_audio.c) keeps two chip buffers queued on Paula channels 2 and
 *     3 - the same mono stream on a left and a right channel, so music is never stuck in one ear - and pulls a
 *     refill callback once a frame from the main loop, NEVER from an interrupt.
 *
 * Effects went the other way round (resident, 8-bit, 22050 Hz): they must start instantly and are short.
 *
 * Plain C, no Amiga types in this header.
 */
#ifndef BH_MUSIC_H
#define BH_MUSIC_H

#ifdef __cplusplus
extern "C" {
#endif

/* Start streaming <path> (e.g. "PROGDIR:data/music/track_1.wav"), looping when it ends. volume is 0..64 on
 * Paula's own scale. Returns 0 if the file is missing or audio is unavailable - the game then plays on in
 * silence rather than refusing to start. */
int bh_music_start(const char *path, int volume);

/* Once a frame from the main loop: hands the decoder whatever buffer Paula has finished with. Cheap when
 * there is nothing to refill, and never blocks. */
void bh_music_service(void);

void bh_music_stop(void);
void bh_music_volume(int volume);
int bh_music_active(void);

#ifdef __cplusplus
}
#endif

#endif /* BH_MUSIC_H */
