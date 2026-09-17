// The game's mix of its sound requests (Game::takeSounds names): the user found the train's pass sound and its warning
// too loud against everything else (after SF2000 v014), so both play at half volume. Both ports use it.
#pragma once

#include <string>

#include "engine/real.h"

namespace cr {

inline mreal soundVolume(const std::string &name)
{
    // O15: the crossing bell rings before every train and drowned everything else out (user report) - a third of
    // full volume is enough to warn
    if (name == "train_alarm") return mreal(0.35f);
    // O15: the hop sound is the one heard most often, so it sits under everything else at half volume
    if (name.compare(0, 12, "chicken_move") == 0 || name.compare(0, 11, "beaver_move") == 0) return mreal(0.5f);
    // O11.7: the fanfares of a finished Progression level are as loud as the train (O14: there are three of them)
    if (name == "train_move_0" || name.compare(0, 7, "fanfare") == 0) return mreal(0.5f);
    return mreal(1.0f);
}

} // namespace cr
