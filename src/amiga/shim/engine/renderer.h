// THE AMIGA'S "engine/renderer.h".
//
// src/ui/screens.cpp and src/ui/hud.cpp are the game's real screens - the animated banners, the pause menu, the
// settings, the career, the ranks - and the first two Amiga drafts did not use them: I wrote a static imitation
// (screens_bh.c) on the grounds that the shared file "needs the textured overlay API". It needs FIVE calls from it.
// This header gives those five calls the same names on top of the sprite blitter, and the build puts this
// directory ahead of src/ on the include path, so the shared files compile UNCHANGED and the Amiga shows the same
// screens, with the same timings, as the R36S and the SF2000.
//
// The screens lay themselves out in 640x480 logical pixels on every platform; like the SF2000 this draws them at
// half size, so beginOverlay() is given twice the drawable height and every coordinate is halved here.
//
// Plain integers only. The shared code hands over `mreal` (16.16 here); it is turned into pixels by a shift.
#pragma once

#include <cstdint>

#include "engine/assets.h"
#include "engine/real.h"

extern "C" {
#include "blit.h"
#include "font_bh.h"
#include "sprites.h"
}

namespace cr {

struct GpuTexture {
    int id = 0;
    int width = 0, height = 0;
};

class Renderer {
public:
    // set once by the platform
    const BHSurface *surface = nullptr;
    const BHSprites *sprites = nullptr;
    int logoSprite = -1; // the title picture is baked in with the rest of the art (tools/make_amiga_logo.py)
    const BHFont *font = nullptr; // the button pictures are drawn as labelled boxes - see drawOverlayImage
    // The screens lay out in 640x480 logical pixels: at 320x240 every coordinate is halved (1), at 640x480 it is
    // the screen itself (2).
    int pixelScale = 1;

    // The pixels are not kept: an 8-bit screen cannot show an RGBA picture, so images are sprites baked offline.
    // The size is kept because the screens lay the title out from it. The first upload is the title.
    GpuTexture uploadTexture(const TextureData &tex)
    {
        GpuTexture t;
        t.id = ++uploads_;
        t.width = tex.width;
        t.height = tex.height;
        return t;
    }

    void beginOverlay(int /*screenW*/, int /*screenH*/) {}
    void endOverlay() {}

    void drawOverlayImage(const GpuTexture &tex, mreal x, mreal y, mreal w, mreal h, mreal alpha = mreal(1));
    void drawOverlayRect(mreal x, mreal y, mreal w, mreal h, mreal r, mreal g, mreal b, mreal a);

    // what the text renderer needs: a logical colour as a palette index
    static unsigned char paletteIndex(mreal r, mreal g, mreal b);

private:
    int uploads_ = 0;
};

} // namespace cr
