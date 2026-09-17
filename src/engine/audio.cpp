#include "audio.h"

#include <algorithm>
#include <cstring>

#include "log.h"
#ifndef CR_FIXED
#include "stb_vorbis.h"
#endif

namespace cr {

Audio::~Audio()
{
    shutdown();
    release(music_);
    release(next_);
}

bool Audio::loadBank(const Manifest &manifest, const std::string &dataDir)
{
    int loaded = 0;
    for (const std::string &name : manifest.sounds) {
        SoundData sd;
        if (loadSound(dataDir + "sounds/" + name + ".snd", sd) && sd.sampleRate == kSoundRate && !sd.samples.empty()) {
            sounds_[name] = std::move(sd);
            loaded++;
        } else {
            logf("audio: cannot load sound %s", name.c_str());
        }
    }
    logf("audio: %d/%zu sounds loaded", loaded, manifest.sounds.size());
    return loaded == int(manifest.sounds.size());
}

void Audio::play(const std::string &name, mreal volume)
{
    auto it = sounds_.find(name);
    if (it == sounds_.end()) {
        // once per name: a missing sound on the device shows up in the log instead of as silence
        if (unknown_.insert(name).second) logf("audio: unknown sound %s", name.c_str());
        return;
    }
    lockDevice();
    int slot = 0;
    for (int i = 0; i < kVoices; i++) {
        if (!voices_[i].sound) {
            slot = i;
            break;
        }
        if (voices_[i].startedAt < voices_[slot].startedAt) slot = i;
    }
    Voice &v = voices_[slot];
    v.sound = &it->second;
    v.pos = 0;
    v.volume = int(volume * mreal(256) + mreal(0.5f));
    v.startedAt = ++counter_;
    unlockDevice();
}

void Audio::stopAll()
{
    lockDevice();
    for (Voice &v : voices_) v.sound = nullptr;
    unlockDevice();
}

void Audio::setMasterVolume(mreal v)
{
    master_ = int((v < mreal(0) ? mreal(0) : v > mreal(1) ? mreal(1) : v) * mreal(256) + mreal(0.5f));
}

void Audio::setMusicVolume(mreal v)
{
    musicVolume_ = int((v < mreal(0) ? mreal(0) : v > mreal(1) ? mreal(1) : v) * mreal(256) + mreal(0.5f));
}

int Audio::playingCount() const
{
    int n = 0;
    for (const Voice &v : voices_) n += v.sound ? 1 : 0;
    return n;
}

#ifdef CR_FIXED

bool Audio::isOpen(const Music &m) { return adpcmIsOpen(m.wav); }

void Audio::release(Music &m) { adpcmClose(m.wav); }

bool Audio::preloadMusic(const std::string &path)
{
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    // read to the end in chunks (no SEEK_END: the firmware's file layer is not trusted with it)
    auto data = std::make_shared<std::vector<uint8_t>>();
    const size_t kChunk = 256 * 1024;
    size_t got = 0;
    do {
        const size_t at = data->size();
        data->resize(at + kChunk);
        got = std::fread(data->data() + at, 1, kChunk, f);
        data->resize(at + got);
    } while (got == kChunk);
    std::fclose(f);
    if (data->empty()) return false;
    musicFiles_[path] = data;
    return true;
}

bool Audio::playMusic(const std::string &path)
{
    if (path == wantedMusic_) return true;
    Music incoming;
    auto loaded = musicFiles_.find(path);
    const bool opened =
        loaded != musicFiles_.end() ? adpcmOpenMemory(loaded->second, incoming.wav) : adpcmOpen(path, incoming.wav);
    if (!opened) {
        logf("audio: cannot open music %s (mono MS ADPCM .wav)", path.c_str());
        return false;
    }
    if (incoming.wav.sampleRate != kRate)
        logf("audio: %s is %d Hz, played as %d Hz", path.c_str(), incoming.wav.sampleRate, kRate);

    Music old;
    lockDevice();
    std::swap(old, next_);
    std::swap(next_, incoming);
    switching_ = true;
    unlockDevice();
    release(old);
    wantedMusic_ = path;
    return true;
}

#else

bool Audio::isOpen(const Music &m) { return m.decoder != nullptr; }

bool Audio::preloadMusic(const std::string &) { return true; } // the .ogg is read whole when it starts

void Audio::release(Music &m)
{
    if (m.decoder) stb_vorbis_close(m.decoder);
    m.decoder = nullptr;
    m.data.clear();
    m.data.shrink_to_fit();
}

bool Audio::playMusic(const std::string &path)
{
    if (path == wantedMusic_) return true;
    Music incoming;
    if (!readFile(path, incoming.data) || incoming.data.empty()) {
        logf("audio: cannot read music %s", path.c_str());
        return false;
    }
    int error = 0;
    incoming.decoder = stb_vorbis_open_memory(incoming.data.data(), int(incoming.data.size()), &error, nullptr);
    if (!incoming.decoder) {
        logf("audio: cannot decode music %s (stb_vorbis error %d)", path.c_str(), error);
        return false;
    }
    const stb_vorbis_info info = stb_vorbis_get_info(incoming.decoder);
    if (info.sample_rate != unsigned(kRate))
        logf("audio: %s is %u Hz, played as %d Hz", path.c_str(), info.sample_rate, kRate);

    Music old;
    lockDevice();
    std::swap(old, next_); // a track that never got swapped in, or the one the audio thread swapped out
    std::swap(next_, incoming);
    switching_ = true;
    unlockDevice();
    release(old);
    wantedMusic_ = path;
    return true;
}

#endif

void Audio::stopMusic()
{
    Music old;
    lockDevice();
    std::swap(old, next_);
    switching_ = true; // fades out and swaps in the empty track
    unlockDevice();
    release(old);
    wantedMusic_.clear();
}

void Audio::mix(int16_t *out, int frames)
{
    acc_.assign(size_t(frames), 0);

    // sound effects: 16.16 positions stepping through 22050 Hz samples, linear interpolation
    for (Voice &v : voices_) {
        if (!v.sound) continue;
        const std::vector<int16_t> &s = v.sound->samples;
        const uint64_t step = (uint64_t(v.sound->sampleRate) << 16) / uint64_t(kRate);
        const int gain = v.volume * master_; // 16.16
        for (int i = 0; i < frames; i++) {
            const size_t index = size_t(v.pos >> 16);
            if (index >= s.size()) break;
            const int32_t a = s[index], b = index + 1 < s.size() ? s[index + 1] : a;
            const int32_t frac = int32_t(v.pos & 0xffff);
            const int32_t sample = a + int32_t((int64_t(b - a) * frac) >> 16);
            acc_[size_t(i)] += int32_t((int64_t(sample) * gain) >> 16);
            v.pos += step;
        }
        if ((v.pos >> 16) >= s.size()) v.sound = nullptr;
    }

    // music volume 0 (settings MUSIC) pauses the track: nothing is decoded or read from the card and the stream stays
    // where it was; a pending switch happens at once (there is nothing to fade out), and the track fades back in
    // once the volume is raised again
    if (musicVolume_ == 0) {
        if (switching_) {
            std::swap(music_, next_); // the main thread frees the old track on its next call
            switching_ = false;
        }
        fade_ = 0;
    }
    // music: decode this buffer, looping at the end of the track
    else if (isOpen(music_) || switching_) {
        musicBuf_.assign(size_t(frames), 0);
#ifdef CR_FIXED
        if (isOpen(music_)) adpcmRead(music_.wav, musicBuf_.data(), frames);
#else
        int filled = 0, restarts = 0;
        while (music_.decoder && filled < frames && restarts < 2) {
            int n = stb_vorbis_get_samples_short_interleaved(music_.decoder, 1, musicBuf_.data() + filled, frames - filled);
            if (n <= 0) {
                stb_vorbis_seek_start(music_.decoder);
                restarts++;
                continue;
            }
            restarts = 0;
            filled += n;
        }
#endif
        bool swapped = false;
        for (int i = 0; i < frames; i++) {
            if (switching_) {
                if (fade_ > 0) {
                    fade_--;
                } else {
                    std::swap(music_, next_); // the main thread frees the old track on its next call
                    switching_ = false;
                    swapped = true;
                }
            } else if (fade_ < kFadeFrames) {
                fade_++;
            }
            if (swapped || !isOpen(music_)) continue; // the new track starts with the next buffer
            const int64_t gain = int64_t(musicVolume_) * fade_ / kFadeFrames; // 0..256
            acc_[size_t(i)] += int32_t((int64_t(musicBuf_[size_t(i)]) * gain) >> 8);
        }
    }

    for (int i = 0; i < frames; i++) {
        int32_t x = acc_[size_t(i)];
        out[i] = int16_t(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
    }
}

} // namespace cr
