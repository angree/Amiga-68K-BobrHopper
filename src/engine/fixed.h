// 16.16 fixed point for the SF2000 build (CR_FIXED, see real.h): the console has no FPU, and soft-float made one logic
// step cost 3.85 ms and the scene graph update 34 ms (docs/PROGRESS.md), so nothing there may use float or double at
// run time. Range +-32768, step 1/65536.
//
// Construction from a double/float literal is allowed and folds at compile time; a double that survives to run time
// is a bug, and build/check_softfloat.sh fails the SF2000 build when any soft-float routine is called.
// Integers convert exactly. Products and quotients round through int64 (MIPS32 `mult` gives the 64-bit product).
// Values that come from files as IEEE floats go through fixedFromFloatBits(), which only shifts bits.
#pragma once

#include <cstdint>
#include <cstring>

#include "fixed_tables.h"

namespace cr {

// THE AMIGA BUILD COMPILES SOME FILES WITH -fno-inline (a compiler crash forces it, see build_amiga.sh), and without
// inlining every one of these one-line operators becomes a real function call - and, far worse, Fixed(double) stops
// folding at compile time, so each real(0.5) literal turns into a soft-float multiply AT RUN TIME. Measured on the
// Amiga: 2 fps with 28 logic steps per frame. always_inline is honoured even under -fno-inline. Every other build
// gets an empty macro and compiles exactly as before.
#if defined(CR_AMIGA)
#define CR_FIXED_INLINE __attribute__((always_inline)) inline
#else
#define CR_FIXED_INLINE
#endif

struct Fixed {
    int32_t v = 0; // raw value * 65536

    constexpr Fixed() = default;
    CR_FIXED_INLINE constexpr Fixed(int i) : v(int32_t(uint32_t(i) << 16)) {}
    CR_FIXED_INLINE constexpr Fixed(unsigned i) : v(int32_t(i << 16)) {}
    CR_FIXED_INLINE constexpr Fixed(long i) : v(int32_t(uint32_t(i) << 16)) {}
    CR_FIXED_INLINE constexpr Fixed(unsigned long i) : v(int32_t(uint32_t(i) << 16)) {}
    CR_FIXED_INLINE constexpr Fixed(long long i) : v(int32_t(uint32_t(i) << 16)) {}
    CR_FIXED_INLINE constexpr Fixed(unsigned long long i) : v(int32_t(uint32_t(i) << 16)) {}
    // literals only (see above)
    CR_FIXED_INLINE constexpr Fixed(double d) : v(int32_t(d >= 0 ? d * 65536.0 + 0.5 : d * 65536.0 - 0.5)) {}
    CR_FIXED_INLINE constexpr Fixed(float f) : Fixed(double(f)) {}

    CR_FIXED_INLINE static constexpr Fixed fromRaw(int32_t r)
    {
        Fixed f;
        f.v = r;
        return f;
    }

    // C++ int(double) semantics: truncation toward zero
    CR_FIXED_INLINE constexpr explicit operator int() const { return v >= 0 ? (v >> 16) : -((-v) >> 16); }
    CR_FIXED_INLINE constexpr explicit operator bool() const { return v != 0; }
    // for host-side tools and logs only (never in the core)
    CR_FIXED_INLINE constexpr double toDouble() const { return double(v) / 65536.0; }

    CR_FIXED_INLINE constexpr Fixed operator-() const { return fromRaw(-v); }
    CR_FIXED_INLINE constexpr Fixed operator+() const { return *this; }

    CR_FIXED_INLINE friend constexpr Fixed operator+(Fixed a, Fixed b) { return fromRaw(a.v + b.v); }
    CR_FIXED_INLINE friend constexpr Fixed operator-(Fixed a, Fixed b) { return fromRaw(a.v - b.v); }
    CR_FIXED_INLINE friend constexpr Fixed operator*(Fixed a, Fixed b)
    {
        return fromRaw(int32_t((int64_t(a.v) * int64_t(b.v) + 32768) >> 16));
    }
    // division by zero gives 0 (JavaScript would give Infinity/NaN; the game guards those cases anyway)
    CR_FIXED_INLINE friend Fixed operator/(Fixed a, Fixed b)
    {
        if (b.v == 0) return fromRaw(0);
#if defined(CR_AMIGA)
        // THE 68020 DIVIDES 64 BY 32 IN ONE INSTRUCTION, and gcc never uses it: a C int64 division becomes a call to
        // libgcc's __divdi3, a generic 64/64 routine of several hundred instructions. Measured on a 68040: the
        // tween engine, which divides once or twice per live tween per step, was 80% of the whole logic step.
        // Same result as the portable code below, bit for bit: magnitudes, half the divisor added, truncation.
        {
            const uint32_t ua = a.v < 0 ? uint32_t(-a.v) : uint32_t(a.v), ub = b.v < 0 ? uint32_t(-b.v) : uint32_t(b.v);
            uint32_t hi = ua >> 16, lo = ua << 16;
            const uint32_t half = ub >> 1;
            lo += half;
            if (lo < half) hi++;
            if (hi >= ub) return fromRaw((a.v ^ b.v) < 0 ? int32_t(0x80000001) : int32_t(0x7fffffff)); // would not fit
            register uint32_t q __asm__("d0") = lo;
            register uint32_t r __asm__("d1") = hi;
            register uint32_t d __asm__("d2") = ub;
            __asm__("divul %2,%1:%0" : "+d"(q), "+d"(r) : "d"(d));
            (void)r;
            return fromRaw((a.v ^ b.v) < 0 ? -int32_t(q) : int32_t(q));
        }
#endif
        // C division truncates toward zero, so half the divisor is added away from zero: round half away from zero
        const int64_t n = int64_t(a.v) * 65536;
        const int64_t half = (b.v > 0 ? int64_t(b.v) : -int64_t(b.v)) / 2;
        return fromRaw(int32_t((n >= 0 ? n + half : n - half) / b.v));
    }

    CR_FIXED_INLINE Fixed &operator+=(Fixed o) { v += o.v; return *this; }
    CR_FIXED_INLINE Fixed &operator-=(Fixed o) { v -= o.v; return *this; }
    CR_FIXED_INLINE Fixed &operator*=(Fixed o) { return *this = *this * o; }
    CR_FIXED_INLINE Fixed &operator/=(Fixed o) { return *this = *this / o; }

    CR_FIXED_INLINE friend constexpr bool operator==(Fixed a, Fixed b) { return a.v == b.v; }
    CR_FIXED_INLINE friend constexpr bool operator!=(Fixed a, Fixed b) { return a.v != b.v; }
    CR_FIXED_INLINE friend constexpr bool operator<(Fixed a, Fixed b) { return a.v < b.v; }
    CR_FIXED_INLINE friend constexpr bool operator<=(Fixed a, Fixed b) { return a.v <= b.v; }
    CR_FIXED_INLINE friend constexpr bool operator>(Fixed a, Fixed b) { return a.v > b.v; }
    CR_FIXED_INLINE friend constexpr bool operator>=(Fixed a, Fixed b) { return a.v >= b.v; }
};

constexpr Fixed kFixedMax = Fixed::fromRaw(0x7fffffff);
constexpr Fixed kFixedEpsilon = Fixed::fromRaw(1);
constexpr Fixed kFixedPi = Fixed::fromRaw(205887);    // 3.14159 * 65536
constexpr Fixed kFixedTwoPi = Fixed::fromRaw(411775); // 6.28318 * 65536

inline constexpr Fixed fixedFloor(Fixed a) { return Fixed::fromRaw(int32_t(uint32_t(a.v) & 0xffff0000u)); }
inline constexpr Fixed fixedCeil(Fixed a) { return Fixed::fromRaw(int32_t(uint32_t(a.v + 0xffff) & 0xffff0000u)); }
// JavaScript Math.round: floor(x + 0.5)
inline constexpr Fixed fixedRound(Fixed a) { return Fixed::fromRaw(int32_t(uint32_t(a.v + 0x8000) & 0xffff0000u)); }
inline constexpr Fixed fixedAbs(Fixed a) { return a.v < 0 ? -a : a; }
// a / Fixed(2) without the 64-bit division, bit for bit: that operator rounds half away from zero
inline constexpr Fixed fixedHalf(Fixed a)
{
    return Fixed::fromRaw(a.v >= 0 ? int32_t((int64_t(a.v) + 1) >> 1) : int32_t(-((-int64_t(a.v) + 1) >> 1)));
}

// sine and cosine of an angle in radians: the angle becomes a 32-bit fraction of a full turn (wraps for free), the
// top 10 bits index kSinQ16 and the next 16 bits interpolate
inline Fixed fixedSinTurn(uint32_t turn)
{
    const uint32_t index = turn >> (32 - kSinTableBits);
    const int32_t frac = int32_t((turn >> (32 - kSinTableBits - 16)) & 0xffff);
    const int32_t a = kSinQ16[index], b = kSinQ16[index + 1];
    return Fixed::fromRaw(a + int32_t((int64_t(b - a) * frac) >> 16));
}
inline uint32_t fixedTurn(Fixed radians)
{
    // 2^32 / (2 pi) in 16.16 = 683565275.6
    return uint32_t((int64_t(radians.v) * 683565276LL) >> 16);
}
inline Fixed fixedSin(Fixed radians) { return fixedSinTurn(fixedTurn(radians)); }
inline Fixed fixedCos(Fixed radians) { return fixedSinTurn(fixedTurn(radians) + 0x40000000u); }

// Math.atan2(Math.sin(a), Math.cos(a)): the angle wrapped into (-pi, pi]
inline Fixed fixedNormalizeAngle(Fixed a)
{
    int32_t r = a.v % kFixedTwoPi.v;
    if (r > kFixedPi.v) r -= kFixedTwoPi.v;
    else if (r <= -kFixedPi.v) r += kFixedTwoPi.v;
    return Fixed::fromRaw(r);
}

inline Fixed fixedSqrt(Fixed a)
{
    if (a.v <= 0) return Fixed::fromRaw(0);
    // raw result = sqrt(raw << 16), integer square root bit by bit
    uint64_t n = uint64_t(uint32_t(a.v)) << 16;
    uint64_t root = 0, bit = uint64_t(1) << 46;
    while (bit > n) bit >>= 2;
    while (bit) {
        if (n >= root + bit) {
            n -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    if (n > root) root++; // round to nearest
    return Fixed::fromRaw(int32_t(root));
}

// IEEE-754 single (as stored in the .mesh files) to 16.16 with shifts only; saturates, NaN/Inf/denormal -> 0
inline Fixed fixedFromFloatBits(uint32_t bits)
{
    const int exponent = int((bits >> 23) & 0xff);
    if (exponent == 0 || exponent == 0xff) return Fixed::fromRaw(0);
    const int64_t mantissa = int64_t((bits & 0xffffff) | 0x800000); // value = mantissa * 2^(exponent - 150)
    const int shift = exponent - 150 + 16;
    int64_t raw;
    if (shift >= 0) raw = shift > 31 ? int64_t(0x7fffffff) : (mantissa << shift);
    else if (shift < -40) raw = 0;
    else raw = (mantissa + (int64_t(1) << (-shift - 1))) >> -shift;
    if (raw > 0x7fffffff) raw = 0x7fffffff;
    return Fixed::fromRaw(int32_t((bits & 0x80000000u) ? -raw : raw));
}

inline Fixed fixedFromFloat(float f)
{
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof bits); // a copy of the bits, not float arithmetic
    return fixedFromFloatBits(bits);
}

} // namespace cr
