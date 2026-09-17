// Digital action input from keyboard (PC) and SDL game controllers / raw joysticks (R36S).
// Sampled once per fixed logic step into a bitmask, which is also what --record / --replay store,
// so a recorded session replays bit-exactly with the seeded RNG.
// Devices are platform code: input_sdl.cpp (init, shutdown, handleEvents); input.cpp is shared.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

union SDL_Event;

namespace cr {

enum Action : uint16_t {
    ActUp = 1 << 0,
    ActDown = 1 << 1,
    ActLeft = 1 << 2,
    ActRight = 1 << 3,
    ActA = 1 << 4,      // confirm / hop forward
    ActB = 1 << 5,      // back
    ActStart = 1 << 6,  // pause
    ActSelect = 1 << 7, // with Start: quit
    ActL = 1 << 8,
    ActR = 1 << 9,
};

class Input {
public:
    void init();
    void shutdown();

    // Feed this frame's SDL events (from Platform::events()).
    void handleEvents(const std::vector<SDL_Event> &events);

    // Latch the live state for one logic step; with a replay loaded, the recorded state is used.
    void step();

    uint16_t held() const { return cur_; }
    bool down(Action a) const { return (cur_ & a) != 0; }
    bool pressed(Action a) const { return (cur_ & a) && !(prev_ & a); }
    bool released(Action a) const { return !(cur_ & a) && (prev_ & a); }

    // state from a bot (--smoke), ORed into the live devices; recorded like real input
    void setSynthetic(uint16_t mask) { synthetic_ = mask; }

    bool startRecording(const std::string &path);
    bool loadReplay(const std::string &path);
    bool replayFinished() const { return replaying_ && replayPos_ >= replay_.size(); }
    void flushRecording();

    // log every raw button once with its name, for the device report
    bool logRawButtons = true;

private:
    void openController(int index);
    uint16_t live() const;

    uint16_t keys_ = 0, pad_ = 0, stick_ = 0, hat_ = 0, rawButtons_ = 0, synthetic_ = 0;
    uint16_t cur_ = 0, prev_ = 0;
    std::vector<void *> controllers_; // SDL_GameController *
    std::vector<void *> joysticks_;   // SDL_Joystick *

    std::vector<uint16_t> replay_;
    size_t replayPos_ = 0;
    bool replaying_ = false;
    std::vector<uint16_t> recording_;
    std::string recordPath_;
};

} // namespace cr
