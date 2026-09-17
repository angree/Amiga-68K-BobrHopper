#include "ui/debug_overlay.h"

#include <cstdio>

namespace cr {

void FrameTimer::frame(double now)
{
    if (first_ < 0) first_ = now;
    if (last_ >= 0 && now - last_ > windowWorst_) windowWorst_ = now - last_;
    last_ = now;
    total_++;
    if (windowStart_ < 0) {
        windowStart_ = now;
        windowFrames_ = 0;
        windowWorst_ = 0;
        return;
    }
    windowFrames_++;
    double span = now - windowStart_;
    if (span >= 0.5) {
        fps_ = windowFrames_ / span;
        avgMs_ = 1000.0 * span / windowFrames_;
        worstMs_ = 1000.0 * windowWorst_;
        windowStart_ = now;
        windowFrames_ = 0;
        windowWorst_ = 0;
    }
}

double FrameTimer::overallFps() const
{
    return (total_ > 1 && last_ > first_) ? double(total_ - 1) / (last_ - first_) : 0;
}

void drawDebugOverlay(Renderer &renderer, TextRenderer &text, const FrameTimer &timer, const OverlayCounters &c,
                      int screenW, int screenH)
{
    char line1[64], line2[64];
    snprintf(line1, sizeof line1, "FPS %d MS %d MAX %d", int(timer.fps() + 0.5), int(timer.avgMs() + 0.5),
                  int(timer.worstMs() + 0.5));
    snprintf(line2, sizeof line2, "DC %d TRI %d SH %d", c.drawCalls, c.triangles, c.casters);
    const int size = 16, pad = 8, step = text.lineHeight(size) + 6;
    const Rgba white{1, 1, 1, 1}, black{0, 0, 0, 1};
    renderer.beginOverlay(screenW, screenH);
    text.drawOutlined(renderer, line1, pad, screenH - pad - 2 * step, size, white, 2, black);
    text.drawOutlined(renderer, line2, pad, screenH - pad - step, size, white, 2, black);
    renderer.endOverlay();
}

} // namespace cr
