// The number type of the shared game code. PC and R36S: `double`, exactly like the JavaScript original (their traces
// and digests stay bit-identical, which is how every step of the fixed-point conversion is checked). SF2000
// (CR_FIXED): 16.16 `Fixed` - an approximate port without any float or double at run time (user decision after the
// v001 device bench, docs/PROGRESS.md).
//
// Game code uses `real` for its numbers and the r* functions below instead of std:: maths; `mreal` is the element
// type of Mat4 (float for the GLES renderer, Fixed on the SF2000).
#pragma once

#include <cmath>
#include <cstdint>

#ifdef CR_FIXED
#include "fixed.h"
#endif

namespace cr {

#ifdef CR_FIXED

using real = Fixed;
using mreal = Fixed;

inline constexpr real rfloor(real a) { return fixedFloor(a); }
inline constexpr real rceil(real a) { return fixedCeil(a); }
inline constexpr real rabs(real a) { return fixedAbs(a); }
inline real rsin(real a) { return fixedSin(a); }
inline real rcos(real a) { return fixedCos(a); }
inline real rsqrt(real a) { return fixedSqrt(a); }
inline constexpr real rhalf(real a) { return fixedHalf(a); } // == a / 2
inline constexpr bool risnan(real) { return false; }
// Math.atan2(Math.sin(a), Math.cos(a))
inline real rnormalizeAngle(real a) { return fixedNormalizeAngle(a); }
inline real realFromFloat(float f) { return fixedFromFloat(f); }
// the fractional part of a non-negative value (std::fmod(a, 1.0))
inline constexpr real rfrac(real a) { return a - fixedFloor(a); }
inline constexpr int rint(real a) { return int(a); }
// host-side printing only
inline constexpr double rd(real a) { return a.toDouble(); }
// "a huge duration" (GSAP's 999999999999 for infinite repeats)
constexpr real kRealHuge = kFixedMax;
// the smallest positive step (GSAP's _tiny = 1e-8 is below 16.16 resolution). A larger slack (16 raw) was tried
// against fast_hops_s5 and changed nothing: that scenario's 1-tick shift comes from tick times (1092.27 raw per tick)
// rounding against summed durations in plain <=/== comparisons (docs/PROGRESS.md, F6)
constexpr real kRealTiny = kFixedEpsilon;

#else

using real = double;
using mreal = float;

inline real rfloor(real a) { return std::floor(a); }
inline real rceil(real a) { return std::ceil(a); }
inline real rabs(real a) { return std::fabs(a); }
inline real rsin(real a) { return std::sin(a); }
inline real rcos(real a) { return std::cos(a); }
inline real rsqrt(real a) { return std::sqrt(a); }
inline constexpr real rhalf(real a) { return a / 2; }
inline bool risnan(real a) { return std::isnan(a); }
inline real rnormalizeAngle(real a) { return std::atan2(std::sin(a), std::cos(a)); }
inline real realFromFloat(float f) { return f; }
inline real rfrac(real a) { return std::fmod(a, 1.0); }
inline constexpr int rint(real a) { return int(a); }
inline constexpr double rd(real a) { return a; }
constexpr real kRealHuge = 999999999999.0;
constexpr real kRealTiny = 0.00000001;

#endif

// JavaScript Math.round (halves go up, also for negatives)
inline real jsRound(real v) { return rfloor(v + real(0.5)); }

} // namespace cr
