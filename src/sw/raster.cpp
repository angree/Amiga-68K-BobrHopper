#include "raster.h"

namespace cr {
namespace sw {

void clearTarget(const RasterTarget &t, uint16_t color, uint16_t depth)
{
    const int stride = t.rowStride();
    for (int y = 0; y < t.height; y++) {
        uint16_t *c = t.color + y * stride;
        for (int x = 0; x < t.width; x++) c[x] = color;
        if (t.depth) {
            uint16_t *d = t.depth + y * stride;
            for (int x = 0; x < t.width; x++) d[x] = depth;
        }
    }
}

namespace {

struct ColorSpan {
    const RasterTarget &t;
    uint16_t color;
    void operator()(int y, int x0, int x1, int32_t, int32_t)
    {
        if (t.counters) t.counters->spans++, t.counters->pixels += x1 - x0;
        // O7.5: four stores per loop turn (most of a frame's pixels are these fills)
        uint16_t *p = t.color + y * t.rowStride() + x0;
        int n = x1 - x0;
        const uint16_t c = color;
        for (; n >= 4; n -= 4, p += 4) {
            p[0] = c;
            p[1] = c;
            p[2] = c;
            p[3] = c;
        }
        for (; n > 0; n--) *p++ = c;
    }
};

struct DepthSpan {
    const RasterTarget &t;
    uint16_t color;
    bool write;
    void operator()(int y, int x0, int x1, int32_t z, int32_t dz)
    {
        if (t.counters) t.counters->spans++, t.counters->pixels += x1 - x0;
        uint16_t *p = t.color + y * t.rowStride();
        uint16_t *d = t.depth + y * t.rowStride();
        for (int x = x0; x < x1; x++, z += dz) {
            const int32_t zi = z >> 8;
            if (zi <= int32_t(d[x])) {
                p[x] = color;
                if (write) d[x] = uint16_t(zi < 0 ? 0 : zi);
            }
        }
    }
};

} // namespace

void drawTriangle(const RasterTarget &t, const ScreenVertex &a, const ScreenVertex &b, const ScreenVertex &c,
                  uint16_t color, int flags, const DepthGradient *gradient)
{
    const bool cull = (flags & kCullBack) != 0;
    if ((flags & kDepthTest) && t.depth) {
        DepthSpan span{t, color, (flags & kDepthWrite) != 0};
        rasterTriangle(t, a, b, c, cull, span, gradient);
    } else {
        ColorSpan span{t, color};
        rasterTriangle(t, a, b, c, cull, span, gradient); // a gradient lets small triangles take rasterSmall
    }
}

} // namespace sw
} // namespace cr
