// Task 2.9: frame timing and renderer counters in the bottom-left corner (--overlay, conf fps_counter=1,
// Select+L at runtime). The font has letters and digits only, so the lines read "FPS 60 MS 16 MAX 18" and
// "DC 108 TRI 6338 SH 36" (draw calls, triangles and shadow casters of the 3D scene).
#pragma once

#include "engine/renderer.h"
#include "engine/text.h"

namespace cr {

class FrameTimer {
public:
    // once per presented frame, with the platform clock in seconds; figures refresh every 0.5 s
    void frame(double now);

    double fps() const { return fps_; }
    double avgMs() const { return avgMs_; }
    double worstMs() const { return worstMs_; }
    long frames() const { return total_; }
    // whole run: presented frames per second
    double overallFps() const;

private:
    double first_ = -1, last_ = -1, windowStart_ = -1, windowWorst_ = 0;
    int windowFrames_ = 0;
    long total_ = 0;
    double fps_ = 0, avgMs_ = 0, worstMs_ = 0;
};

struct OverlayCounters {
    int drawCalls = 0, triangles = 0, casters = 0;
};

void drawDebugOverlay(Renderer &renderer, TextRenderer &text, const FrameTimer &timer, const OverlayCounters &counters,
                      int screenW, int screenH);

} // namespace cr
