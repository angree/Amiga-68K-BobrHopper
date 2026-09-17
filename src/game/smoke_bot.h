// Bot for --smoke: plays like an impatient human, from its own mulberry32 stream.
// The game's RNG streams are not touched, so the same seed plus the same bot gives the same session.
#pragma once

#include <cstdint>

#include "engine/input.h"
#include "game/game.h"
#include "game/rng.h"

namespace cr {

struct SmokeBot {
    uint32_t state;
    uint16_t mask = 0;
    int holdLeft = 0, gap = 0;
    explicit SmokeBot(uint32_t seed) : state(seed ^ 0x5bd1e995u) {}

    real rnd()
    {
        state += 0x6d2b79f5u;
        uint32_t t = state;
        t = (t ^ (t >> 15)) * (t | 1u);
        t ^= t + (t ^ (t >> 7)) * (t | 61u);
        return realFraction32(t ^ (t >> 14));
    }

    // held buttons for this step: press, hold 1-3 steps, release, wait
    uint16_t next(GameState s)
    {
        if (holdLeft > 0) {
            if (--holdLeft > 0) return mask;
            mask = 0;
            gap = s == GameState::Playing ? 2 + int(rnd() * 24) : 20 + int(rnd() * 60);
            return 0;
        }
        if (gap > 0) {
            gap--;
            return 0;
        }
        if (s == GameState::Playing) {
            real r = rnd();
            mask = r < real(0.55) ? ActUp : r < real(0.7) ? ActLeft : r < real(0.85) ? ActRight : ActDown;
            holdLeft = 1 + int(rnd() * 3);
        } else {
            // A both starts a game and restarts after a game over: since O11.2 the home screen is a mode menu, where
            // Up and Down move the cursor and only A starts the game the cursor is on (Classic by default)
            mask = ActA;
            holdLeft = 1;
        }
        return mask;
    }
};

} // namespace cr
