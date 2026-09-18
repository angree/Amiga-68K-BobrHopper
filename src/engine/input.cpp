#include "input.h"

#include <cstdio>

#include "log.h"

namespace cr {

uint16_t Input::live() const { return uint16_t(keys_ | pad_ | stick_ | hat_ | rawButtons_ | synthetic_); }

void Input::step()
{
    prev_ = cur_;
    if (replaying_) {
        cur_ = replayPos_ < replay_.size() ? replay_[replayPos_] : 0;
        replayPos_++;
    } else {
        cur_ = live();
    }
    if (!recordPath_.empty()) recording_.push_back(cur_);
    // O23: the per-device masks are latched the same way, so devicePressed/Released see the same step boundaries.
    // A replay only carries the whole mask, so a replayed session drives device 0 - which is what one player uses.
    devCur_[0] = cur_;
    devPrev_[0] = prev_;
    for (int d = 1; d < kInputDevices; d++) {
        devPrev_[d] = devCur_[d];
        // A DEVICE IS ONLY ITSELF. The synthetic mask (a bot, a script) is the WHOLE input state and belongs to
        // device 0 alone: folding it in here handed every key to every player, so with two players the arrows drove
        // both of them (user report). A platform that wants a bot to reach a particular player calls setDevice for
        // that player's device - which is what the Amiga and the automated PC runs do.
        devCur_[d] = replaying_ ? uint16_t(0) : devSet_[d];
    }
}

bool Input::startRecording(const std::string &path)
{
    recordPath_ = path;
    recording_.clear();
    return true;
}

void Input::flushRecording()
{
    if (recordPath_.empty()) return;
    FILE *f = std::fopen(recordPath_.c_str(), "wb");
    if (!f) return;
    for (uint16_t v : recording_) {
        unsigned char b[2] = {uint8_t(v), uint8_t(v >> 8)};
        std::fwrite(b, 1, 2, f);
    }
    std::fclose(f);
    logf("input: recorded %zu steps to %s", recording_.size(), recordPath_.c_str());
}

bool Input::loadReplay(const std::string &path)
{
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    replay_.clear();
    unsigned char b[2];
    while (std::fread(b, 1, 2, f) == 2) replay_.push_back(uint16_t(b[0] | b[1] << 8));
    std::fclose(f);
    replaying_ = true;
    replayPos_ = 0;
    logf("input: replay %s, %zu steps", path.c_str(), replay_.size());
    return true;
}

} // namespace cr
