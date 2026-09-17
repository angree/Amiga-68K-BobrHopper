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
