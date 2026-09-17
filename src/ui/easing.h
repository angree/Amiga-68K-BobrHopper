// React Native's Animated easing, 1:1 with react-native-web (vendor/react-native/Animated/Easing.js and bezier.js),
// for the screens' animations: Animated.timing defaults to inOut(ease) over 500 ms. Checked against values
// computed by the real JavaScript (tools/webref/gen_easing_vectors.mjs -> tests/test_easing.cpp).
// Numbers are `real` (engine/real.h): double on PC/R36S, 16.16 on the SF2000.
#pragma once

#include <cmath>

#include "engine/real.h"

namespace cr {
namespace easing {

namespace detail {
constexpr int kNewtonIterations = 4;
constexpr real kNewtonMinSlope = 0.001;
constexpr real kSubdivisionPrecision = 0.0000001;
constexpr int kSubdivisionMaxIterations = 10;
constexpr int kSplineTableSize = 11;
constexpr real kSampleStepSize = 1.0 / (kSplineTableSize - 1.0);

inline real A(real a1, real a2) { return real(1.0) - real(3.0) * a2 + real(3.0) * a1; }
inline real B(real a1, real a2) { return real(3.0) * a2 - real(6.0) * a1; }
inline real C(real a1) { return real(3.0) * a1; }
inline real calcBezier(real t, real a1, real a2) { return ((A(a1, a2) * t + B(a1, a2)) * t + C(a1)) * t; }
inline real getSlope(real t, real a1, real a2)
{
    return real(3.0) * A(a1, a2) * t * t + real(2.0) * B(a1, a2) * t + C(a1);
}
} // namespace detail

// Easing.bezier(x1, y1, x2, y2) evaluated at x
inline real bezier(real mX1, real mY1, real mX2, real mY2, real x)
{
    using namespace detail;
    if (mX1 == mY1 && mX2 == mY2) return x; // linear
    if (x == real(0) || x == real(1)) return x;
    real samples[kSplineTableSize];
    for (int i = 0; i < kSplineTableSize; i++) samples[i] = calcBezier(real(i) * kSampleStepSize, mX1, mX2);

    real intervalStart = 0.0;
    int current = 1;
    const int last = kSplineTableSize - 1;
    for (; current != last && samples[current] <= x; ++current) intervalStart += kSampleStepSize;
    --current;
    real dist = (x - samples[current]) / (samples[current + 1] - samples[current]);
    real guessForT = intervalStart + dist * kSampleStepSize;
    real initialSlope = getSlope(guessForT, mX1, mX2);
    real t;
    if (initialSlope >= kNewtonMinSlope) {
        t = guessForT;
        for (int i = 0; i < kNewtonIterations; ++i) {
            real slope = getSlope(t, mX1, mX2);
            if (slope == real(0.0)) break;
            t -= (calcBezier(t, mX1, mX2) - x) / slope;
        }
    } else if (initialSlope == real(0.0)) {
        t = guessForT;
    } else {
        real a = intervalStart, b = intervalStart + kSampleStepSize, currentX, currentT;
        int i = 0;
        do {
            currentT = a + (b - a) / real(2.0);
            currentX = calcBezier(currentT, mX1, mX2) - x;
            if (currentX > real(0.0)) b = currentT;
            else a = currentT;
        } while (rabs(currentX) > kSubdivisionPrecision && ++i < kSubdivisionMaxIterations);
        t = currentT;
    }
    return calcBezier(t, mY1, mY2);
}

// Easing.ease
inline real ease(real t) { return bezier(0.42, 0, 1, 1, t); }

// Easing.inOut(Easing.ease): the default of Animated.timing
inline real inOutEase(real t)
{
    if (t < real(0.5)) return ease(t * real(2)) / real(2);
    return real(1) - ease((real(1) - t) * real(2)) / real(2);
}

// Easing.elastic(bounciness)
inline real elastic(real t, real bounciness = 1)
{
    const real p = bounciness * real(3.141592653589793);
    const real c = rcos(t * real(3.141592653589793) / real(2));
#ifdef CR_FIXED
    return real(1) - c * c * c * rcos(t * p);
#else
    return 1 - std::pow(c, 3) * std::cos(t * p);
#endif
}

} // namespace easing
} // namespace cr
