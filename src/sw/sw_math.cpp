#include "sw_math.h"

#include "sw_tables.h"

namespace cr {
namespace sw {

Mat34 fromMat4(const Mat4 &m)
{
    Mat34 r;
    for (int row = 0; row < 3; row++)
        for (int c = 0; c < 4; c++) r.m[row * 4 + c] = m.e[c * 4 + row].v;
    return r;
}

Mat34 mul(const Mat34 &a, const Mat34 &b)
{
    Mat34 r;
    for (int row = 0; row < 3; row++) {
        const int32_t *ar = a.m + row * 4;
        for (int c = 0; c < 4; c++) {
            int64_t s = int64_t(ar[0]) * b.m[c] + int64_t(ar[1]) * b.m[4 + c] + int64_t(ar[2]) * b.m[8 + c];
            if (c == 3) s += int64_t(ar[3]) << 16;
            r.m[row * 4 + c] = int32_t((s + 32768) >> 16);
        }
    }
    return r;
}

Mat34 screenMatrix(const Mat4 &projection, const Mat4 &view, int width, int height)
{
    const Mat34 p = fromMat4(projection);
    Mat34 s;
    // x = (ndc.x + 1) * width / 2, y = (1 - ndc.y) * height / 2, z = -view z (orthographic: ndc = rows of p, w = 1)
    for (int c = 0; c < 4; c++) {
        s.m[c] = int32_t((int64_t(p.m[c]) * width) / 2);
        s.m[4 + c] = int32_t(-(int64_t(p.m[4 + c]) * height) / 2);
        s.m[8 + c] = c == 2 ? -65536 : 0;
    }
    s.m[3] += width << 15;
    s.m[7] += height << 15;
    return mul(s, fromMat4(view));
}

void transformVertices(const Mat34 &t, const int32_t *positions, int count, ScreenVertex *out)
{
    for (int i = 0; i < count; i++, positions += 3) out[i] = toScreen(transformQ32(t, positions[0], positions[1], positions[2]));
}

int32_t lambertQ16(int32_t ndotl)
{
    if (ndotl <= 0) return kLambertQ16[0];
    if (ndotl >= 65536) return kLambertQ16[kLambertTableSize - 1];
    const int32_t index = ndotl >> 8, frac = ndotl & 0xff;
    const int32_t a = kLambertQ16[index], b = kLambertQ16[index + 1];
    return a + (((b - a) * frac + 128) >> 8);
}

static uint32_t isqrt64(uint64_t n)
{
    uint64_t root = 0, bit = uint64_t(1) << 62;
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
    return uint32_t(root);
}

void axisBrightness(const Mat4 &world, const Vec3 &towardsLight, int32_t out[6])
{
    // O7.4: a column costs a 64-bit bit-by-bit square root and a 64-bit division (library loops on the SF2000's MIPS32)
    // for every draw, while the same few columns (unrotated or quarter-turned nodes of one scale) come back draw after
    // draw; the results are kept, keyed by the exact column and light
    struct Entry {
        int32_t cx, cy, cz, lx, ly, lz, d;
        bool used;
    };
    static Entry cache[64];
    for (int k = 0; k < 3; k++) {
        const int32_t cx = world.e[k * 4].v, cy = world.e[k * 4 + 1].v, cz = world.e[k * 4 + 2].v;
        Entry &e = cache[(uint32_t(cx) * 2654435761u ^ uint32_t(cy) * 40503u ^ uint32_t(cz) * 2246822519u) >> 26];
        int32_t d = 0;
        if (e.used && e.cx == cx && e.cy == cy && e.cz == cz && e.lx == towardsLight.x.v && e.ly == towardsLight.y.v &&
            e.lz == towardsLight.z.v) {
            d = e.d;
        } else {
            const uint64_t len2 = uint64_t(int64_t(cx) * cx + int64_t(cy) * cy + int64_t(cz) * cz); // Q32
            const int32_t len = int32_t(isqrt64(len2));                                                 // Q16
            if (len > 0) {
                const int64_t dotQ32 = int64_t(cx) * towardsLight.x.v + int64_t(cy) * towardsLight.y.v +
                                       int64_t(cz) * towardsLight.z.v;
                d = int32_t(dotQ32 / len);
            }
            e = {cx, cy, cz, towardsLight.x.v, towardsLight.y.v, towardsLight.z.v, d, true};
        }
        out[k * 2] = lambertQ16(d);
        out[k * 2 + 1] = lambertQ16(-d);
    }
}

} // namespace sw
} // namespace cr
