// Fixed-point transform and lighting maths of the SF2000 software renderer (task R1.2). No float or double: the scene
// graph's 16.16 Mat4 (CR_FIXED) becomes one 3x4 model-to-screen matrix per node, vertices go through int64 products,
// and the three.js Lambert term comes from a table.
//
// Precision (tests/test_sw_math.cpp): screen positions within 1/256 pixel of the same matrices evaluated in double,
// before the 28.4 subpixel rounding. Limits: |world translation relative to the camera| x pixels per unit must stay
// below 32767 pixels (the scene renderer draws nodes relative to the camera position, so this is ~40 units at the
// largest zoom); depth covers 64 world units around the camera plane.
#pragma once

#ifndef CR_FIXED
#error "src/sw is the SF2000 renderer: build it with CR_FIXED"
#endif

#include <cstdint>

#include "engine/math.h"

namespace cr {
namespace sw {

constexpr int kSubpixelBits = 4;
constexpr int32_t kSubpixelOne = 1 << kSubpixelBits;
// depth buffer value = (distance in front of the camera plane + kDepthOffset) << kDepthUnitBits, clamped to 16 bits:
// steps of 1/1024 unit over -32 .. +32 (the GLES build spreads its -30 .. 30 near/far over 24 bits)
constexpr int kDepthUnitBits = 10;
constexpr int32_t kDepthOffset = 32;
constexpr int32_t kDepthMax = 65535;

// rows x, y, z; columns a, b, c, t: out_row = a * x + b * y + c * z + t, every element Q16
struct Mat34 {
    int32_t m[12];
};

Mat34 fromMat4(const Mat4 &m);
Mat34 mul(const Mat34 &a, const Mat34 &b);

// view * model space -> row x: pixels from the left edge, row y: pixels from the top edge, row z: distance in front of
// the camera plane in world units. projection must be orthographic (CrossyCamera); width/height in pixels.
Mat34 screenMatrix(const Mat4 &projection, const Mat4 &view, int width, int height);
inline Mat34 modelToScreen(const Mat34 &screen, const Mat4 &world) { return mul(screen, fromMat4(world)); }

struct ScreenVertex {
    int32_t x, y; // 28.4 pixels; the centre of pixel (i, j) is (i * 16 + 8, j * 16 + 8)
    int32_t z;    // depth buffer units 0 .. kDepthMax, smaller is nearer
};

// the exact product in Q32 (pixels, pixels, world units)
struct PointQ32 {
    int64_t x, y, z;
};

inline PointQ32 transformQ32(const Mat34 &t, int32_t px, int32_t py, int32_t pz)
{
    const int32_t *m = t.m;
    return {int64_t(m[0]) * px + int64_t(m[1]) * py + int64_t(m[2]) * pz + (int64_t(m[3]) << 16),
            int64_t(m[4]) * px + int64_t(m[5]) * py + int64_t(m[6]) * pz + (int64_t(m[7]) << 16),
            int64_t(m[8]) * px + int64_t(m[9]) * py + int64_t(m[10]) * pz + (int64_t(m[11]) << 16)};
}

inline ScreenVertex toScreen(const PointQ32 &p)
{
    const int shift = 32 - kSubpixelBits;
    const int64_t half = int64_t(1) << (shift - 1);
    ScreenVertex v;
    v.x = int32_t((p.x + half) >> shift);
    v.y = int32_t((p.y + half) >> shift);
    const int64_t d = (p.z + (int64_t(kDepthOffset) << 32)) >> (32 - kDepthUnitBits);
    v.z = d < 0 ? 0 : d > kDepthMax ? kDepthMax : int32_t(d);
    return v;
}

// positions: x y z per vertex in 16.16 (FlatMeshData::positions)
void transformVertices(const Mat34 &t, const int32_t *positions, int count, ScreenVertex *out);

// ((1.8 + max(n.l, 0)) / PI)^(1/2.2) in Q16 for n.l in Q16 (the GLES shader's per-vertex light)
int32_t lambertQ16(int32_t ndotl);

// the Lambert light of the six model-space axis normals (FlatMeshData::Triangle::axis) after `world`, Q16.
// towardsLight must be normalized. The normal of axis k is taken along the world matrix's column k, which is exact for
// compose(position, rotation, scale) chains without shear (every node in this game).
void axisBrightness(const Mat4 &world, const Vec3 &towardsLight, int32_t out[6]);

// a colour channel lit like gl_FragColor = colour * light, stored as 8 bits
inline uint8_t shade8(uint8_t c, int32_t lightQ16) { return uint8_t((int32_t(c) * lightQ16 + 32768) >> 16); }
inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return uint16_t((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3));
}

} // namespace sw
} // namespace cr
