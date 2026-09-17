// SDL output device for the mixer in audio.cpp (PC and R36S).
#include "audio.h"

#include <SDL.h>

#include "log.h"

namespace cr {

bool Audio::init(bool openDevice)
{
    if (!openDevice) return true;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        logf("audio: SDL_INIT_AUDIO failed: %s (silent)", SDL_GetError());
        return false;
    }
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = kRate;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 1024;
    want.callback = &Audio::callback;
    want.userdata = this;
    // allowed_changes = 0: SDL converts to whatever the ALSA device really wants
    device_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (!device_) {
        logf("audio: open failed: %s (silent)", SDL_GetError());
        return false;
    }
    logf("audio: driver %s, %d Hz, %d ch, buffer %d", SDL_GetCurrentAudioDriver(), have.freq, have.channels,
         have.samples);
    SDL_PauseAudioDevice(device_, 0);
    return true;
}

void Audio::shutdown()
{
    if (device_) SDL_CloseAudioDevice(device_);
    device_ = 0;
}

void Audio::lockDevice()
{
    if (device_) SDL_LockAudioDevice(device_);
}

void Audio::unlockDevice()
{
    if (device_) SDL_UnlockAudioDevice(device_);
}

void Audio::callback(void *user, uint8_t *stream, int len)
{
    static_cast<Audio *>(user)->mix(reinterpret_cast<int16_t *>(stream), len / 2);
}

} // namespace cr
