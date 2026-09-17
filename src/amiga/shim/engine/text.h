// THE AMIGA'S "engine/text.h" - see shim/engine/renderer.h for why this exists.
//
// Same class name and the same three calls the shared screens use. Sizes are the logical 640x480 sizes (12, 14, 18,
// 32). At 320x240 each is drawn with the face baked at HALF that size (6, 7, 9, 16 - the SF2000's own fonts, Polish
// letters included) and every metric is reported doubled, exactly as TextRenderer::glyphScale = 2 does on the
// SF2000; at 640x480 (font640.bhf) the faces are the full sizes and nothing is scaled.
#pragma once

#include <string>

#include "engine/renderer.h"

extern "C" {
#include "font_bh.h"
}

namespace cr {

struct Rgba {
    mreal r, g, b, a;
};

class TextRenderer {
public:
    const BHFont *font = nullptr; // set once by the platform
    int pixelScale = 1;           // as Renderer::pixelScale: 1 draws the half-size faces, 2 the full-size ones

    int width(const std::string &text, int size) const;
    int lineHeight(int size) const;
    void draw(Renderer &renderer, const std::string &text, int x, int y, int size, Rgba color);
    void drawOutlined(Renderer &renderer, const std::string &text, int x, int y, int size, Rgba color, int outlineWidth,
                      Rgba outline);

private:
    int face(int size) const;
};

} // namespace cr
