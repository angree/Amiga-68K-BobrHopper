// mulberry32, bit-identical to the JavaScript used by the instrumented reference build
// (tools/webref/make_reference_build.py). The game uses two independent streams like the patched
// original: `map` for everything that generates the level (rows, cars, logs, trees, model picks)
// and `fx` for cosmetic randomness (particles, foam, sounds, death spins), so frame timing never
// changes the level.
#pragma once

#include <cstdint>

#include "engine/real.h"

namespace cr {

// a 32-bit random integer as a number in [0, 1): Math.random() on PC/R36S, its top 16 bits in 16.16 on the SF2000
inline real realFraction32(uint32_t bits)
{
#ifdef CR_FIXED
    return real::fromRaw(int32_t(bits >> 16));
#else
    return double(bits) / 4294967296.0;
#endif
}

class Rng {
public:
    explicit Rng(uint32_t seed = 1) : s_(seed) {}
    void seed(uint32_t seed) { s_ = seed; }

    // Math.random()
    real next()
    {
        s_ += 0x6D2B79F5u;
        uint32_t t = s_;
        t = (t ^ (t >> 15)) * (t | 1u);
        t ^= t + (t ^ (t >> 7)) * (t | 61u);
        return realFraction32(t ^ (t >> 14));
    }

    uint32_t state() const { return s_; }

private:
    uint32_t s_;
};

struct GameRng {
    Rng map;
    Rng fx;
    void seed(uint32_t s)
    {
        map.seed(s);
        fx.seed(s ^ 0x9E3779B9u);
    }
};

} // namespace cr
