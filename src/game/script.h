// Scripted input shared by apps/bobrhopper (automation) and apps/trace (comparison with the original),
// with the same per-frame semantics as tools/webref/trace.mjs:
//   wN   wait N frames          s    start playing (home screen: first hop)
//   u/d/l/r  hop: key down this frame, key up the next   a    restart after game over
//   shot:name  screenshot request (ignored by the trace)
#pragma once

#include <string>
#include <vector>

#include "game/game.h"

namespace cr {

class InputScript {
public:
    void parse(const std::string &text);
    // Applies this frame's input to the game, before Game::step(). Returns shot names requested this frame.
    std::vector<std::string> apply(Game &game);
    bool finished() const { return pos_ >= ops_.size() && !pendingRelease_ && wait_ == 0; }

private:
    struct Op {
        enum Kind { Wait, Start, Hop, Restart, Shot } kind;
        int value = 0;
        Swipe dir = Swipe::Up;
        std::string name;
    };
    std::vector<Op> ops_;
    size_t pos_ = 0;
    int wait_ = 0;
    bool pendingRelease_ = false;
    Swipe pendingDir_ = Swipe::Up;
};

} // namespace cr
