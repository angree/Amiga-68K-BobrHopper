// Minimal mixer: 8 sound effect voices plus one streamed music track, mono s16. Sound effects are baked at 22050 Hz
// and resampled with linear interpolation; music is looped and crossfaded (0.25 s out, 0.25 s in) when the track
// changes. When every voice is busy the oldest one is replaced. If the audio device cannot be opened the game keeps
// running silently.
//   PC and R36S: 44100 Hz, music Ogg Vorbis decoded in the audio callback (stb_vorbis); audio_sdl.cpp opens an SDL
//   device whose callback calls mix().
//   SF2000 (CR_FIXED): 22050 Hz, music MS-ADPCM streamed from the card (engine/adpcm.h); the core calls mix() from
//   retro_run (sf2000/audio_retro.cpp), volumes are 16.16.
#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "assets.h"
#include "real.h"

#ifdef CR_FIXED
#include "adpcm.h"
#else
struct stb_vorbis;
#endif

namespace cr {

class Audio {
public:
    static const int kVoices = 8;
#ifdef CR_FIXED
    static const int kRate = 22050;      // device and music
#else
    static const int kRate = 44100;      // device and music
#endif
    static const int kSoundRate = 22050; // baked sound effects

    ~Audio();

    // openDevice=false: no SDL audio at all (headless tests); mix() can still be driven manually.
    bool init(bool openDevice);
    void shutdown();

    bool loadBank(const Manifest &manifest, const std::string &dataDir);
    bool has(const std::string &name) const { return sounds_.count(name) != 0; }

    void play(const std::string &name, mreal volume = 1.0f);
    void stopAll();
    // sound effects volume (settings SOUNDS)
    void setMasterVolume(mreal v);
    mreal masterVolume() const { return mreal(master_) / mreal(256.0f); }
    int playingCount() const;
    bool deviceOpen() const { return device_ != 0; }

    // Starts looping a track (.ogg, on the SF2000 an MS-ADPCM .wav), crossfading from the current one; the same path
    // again keeps it playing.
    bool playMusic(const std::string &path);
    // SF2000: reads a track into memory, so playMusic takes it from there instead of opening and streaming the file
    // (opening a file on the card froze the game when the track changed at death and at the start; user report).
    // Elsewhere a no-op returning true.
    bool preloadMusic(const std::string &path);
    void stopMusic();
    // music volume (settings MUSIC), independent of the sound effects volume; 0 pauses the track (no decoding), a
    // volume above 0 resumes it where it stopped, with a fade-in
    void setMusicVolume(mreal v);
    mreal musicVolume() const { return mreal(musicVolume_) / mreal(256.0f); }
    const std::string &musicPath() const { return wantedMusic_; }

    // Mixes `frames` samples into out (also what the SDL callback uses).
    void mix(int16_t *out, int frames);

private:
    struct Voice {
        const SoundData *sound = nullptr;
        uint64_t pos = 0; // 16.16 position in the sound's samples
        int volume = 256;
        uint32_t startedAt = 0;
    };
    struct Music {
#ifdef CR_FIXED
        AdpcmWav wav;
#else
        std::vector<uint8_t> data; // the whole .ogg; the decoder reads from it
        stb_vorbis *decoder = nullptr;
#endif
    };
    static void callback(void *user, uint8_t *stream, int len);
    static void release(Music &m);
    static bool isOpen(const Music &m);
    // keep the device's mixing thread out while voices or the music track change (no-op without a thread)
    void lockDevice();
    void unlockDevice();

    std::map<std::string, SoundData> sounds_;
    std::set<std::string> unknown_; // names already reported by play()
    Voice voices_[kVoices];
    uint32_t device_ = 0; // SDL_AudioDeviceID
    int master_ = 256;
    uint32_t counter_ = 0;
    std::vector<int32_t> acc_;

    // music: the audio thread swaps `next_` in once the fade-out reached 0; the main thread frees what it left
    Music music_, next_;
    bool switching_ = false;
    int fade_ = 0; // 0..kFadeFrames
    static const int kFadeFrames = kRate / 4;
    int musicVolume_ = 56; // 22%
    std::string wantedMusic_;
    std::vector<int16_t> musicBuf_;
#ifdef CR_FIXED
    std::map<std::string, std::shared_ptr<const std::vector<uint8_t>>> musicFiles_; // preloadMusic
#endif
};

} // namespace cr
