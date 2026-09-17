// The five overlay calls and three text calls that src/ui/screens.cpp and src/ui/hud.cpp are built on, implemented
// with the sprite blitter and the baked font - see shim/engine/renderer.h for why. Also the one method of cr::Input
// this build needs.
//
// Everything is integer: the shared code passes 16.16 (`mreal`), logical 640x480 units; a pixel is value >> 17
// (the >> 16 of the fixed point and the halving of the layout in one shift).
#include "engine/input.h"
#include "engine/renderer.h"
#include "engine/text.h"
#include "ui/lang.h"

#include <string>
#include <vector>

extern "C" {
#include "ui_colours.h"
}

namespace cr {

namespace {

// logical 640x480 units (16.16) -> pixels: halved at 320x240, as they are at 640x480
inline int pxs(mreal v, int scale) { return int(v.v >> (scale > 1 ? 16 : 17)); }
inline int c8(mreal v)
{
    const long c = (long(v.v) * 255L) >> 16;
    return c < 0 ? 0 : c > 255 ? 255 : int(c);
}

// Every colour the shared screens ever ask for, and the palette register that holds it. The packer puts these
// colours at fixed indices (tools/pack_amiga_sprites.py: UI_COLOURS, SYS_SLOTS, SKY), so this is a lookup, never a
// search through the art's palette.
struct Known {
    unsigned char r, g, b, index;
};
const Known kKnown[] = {
    {0xFF, 0xFF, 0xFF, BH_UI_TEXT},     {0x00, 0x00, 0x00, BH_UI_OUTLINE}, {0xF8, 0xE8, 0x4D, BH_UI_SELECTED},
    {0x6A, 0x40, 0xEB, BH_UI_BAR_A},    {0x6A, 0x8F, 0xEB, BH_UI_BAR_B},   {0x36, 0x40, 0xEB, BH_UI_GO_A},
    {0x36, 0x8F, 0xEB, BH_UI_GO_B},     {0x36, 0xD6, 0xEB, BH_UI_GO_C},    {105, 201, 230, BH_SYS_SPARE_13},
    {0x87, 0xC6, 0xFF, 1 /* the sky */},
};

// Ordered 4x4 dither: an 8-bit screen cannot blend, and the screens use translucency twice - the 80% pause/settings
// backdrop and the restart fade to sky. A Bayer pattern at the same coverage is the period-correct answer and costs
// one table read per pixel, only while a menu or a fade is up.
const unsigned char kBayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

} // namespace

unsigned char Renderer::paletteIndex(mreal r, mreal g, mreal b)
{
    const int R = c8(r), G = c8(g), B = c8(b);
    long best = 0x7fffffffL;
    unsigned char index = BH_UI_TEXT;
    for (unsigned i = 0; i < sizeof(kKnown) / sizeof(kKnown[0]); i++) {
        const long dr = R - kKnown[i].r, dg = G - kKnown[i].g, db = B - kKnown[i].b;
        const long d = dr * dr + dg * dg + db * db;
        if (d < best) {
            best = d;
            index = kKnown[i].index;
        }
    }
    return index;
}

void Renderer::drawOverlayRect(mreal x, mreal y, mreal w, mreal h, mreal r, mreal g, mreal b, mreal a)
{
    if (!surface) return;
    int x0 = pxs(x, pixelScale), y0 = pxs(y, pixelScale), x1 = pxs(x + w, pixelScale), y1 = pxs(y + h, pixelScale);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > surface->width) x1 = surface->width;
    if (y1 > surface->height) y1 = surface->height;
    if (x1 <= x0 || y1 <= y0) return;
    const unsigned char index = paletteIndex(r, g, b);
    const int level = int((long(a.v) * 16L + 32768L) >> 16); // 0..16 of the Bayer matrix
    if (level <= 0) return;
    if (level >= 16) {
        bh_fill_rect(surface, x0, y0, x1 - x0, y1 - y0, index);
        return;
    }
    unsigned char *row = surface->pixels + (unsigned long)y0 * (unsigned long)surface->pitch;
    const int pitch = surface->pitch;
    for (int yy = y0; yy < y1; yy++, row += pitch) {
        const unsigned char *bayer = kBayer[yy & 3];
        for (int xx = x0; xx < x1; xx++)
            if (bayer[xx & 3] < level) row[xx] = index;
    }
}

// THE BUTTON PICTURES ARE LABELLED BOXES. The consoles draw a PLAY and a SETTINGS button under the game-over banners
// and a BACK arrow on the settings screen, each with a one-letter label above it (A, SELECT, B). This build has no
// RGBA pictures, so what the player saw was the two letters and nothing under them - "S A and nothing". Each box now
// says what it does and which key and stick movement does it, and the lone letters above are dropped (see
// forThisKeyboard). Upload order in Screens::load: 1 title, 2 play, 3 settings, 4 back.
static void labelledBox(Renderer &r, int x, int y, int w, int h, bool alignRight, const char *top, const char *bottom);

void Renderer::drawOverlayImage(const GpuTexture &tex, mreal x, mreal y, mreal w, mreal h, mreal /*alpha*/)
{
    if (!surface) return;
    if (tex.id == 1) {
        // The title. It is a sprite of its own size (the box the screens compute is for the RGBA original), so it
        // is centred on the box and hung from its top - which keeps the slide-in the shared code animates.
        if (!sprites || logoSprite < 0) return;
        const int logoW = int(sprites->entries[logoSprite].w);
        bh_blit(surface, sprites, logoSprite, pxs(x, pixelScale) + (pxs(w, pixelScale) - logoW) / 2, pxs(y, pixelScale));
        return;
    }
    const bool polish = lang::current() == 1;
    const int bx = pxs(x, pixelScale), by = pxs(y, pixelScale), bw = pxs(w, pixelScale), bh = pxs(h, pixelScale);
    if (tex.id == 2) labelledBox(*this, bx, by, bw, bh, true, "A / FIRE", polish ? "GRAJ" : "PLAY");
    else if (tex.id == 3) labelledBox(*this, bx, by, bw, bh, false, "S / LEFT", lang::t(lang::MenuItem));
    else if (tex.id == 4) labelledBox(*this, bx, by, bw, bh, false, "B / FIRE 2", lang::t(lang::Back));
}

int TextRenderer::face(int size) const
{
    if (!font || font->faceCount <= 0) return -1;
    const int want = pixelScale > 1 ? size : size / 2;
    int best = 0, bestD = 1000;
    for (int i = 0; i < font->faceCount; i++) {
        int d = int(font->faces[i].pixelSize) - want;
        if (d < 0) d = -d;
        if (d < bestD) {
            bestD = d;
            best = i;
        }
    }
    return best;
}

// THE CONSOLES HAVE A "SELECT" BUTTON; THIS KEYBOARD HAS AN S KEY. The hint lines come from the shared lang.cpp and
// say "A START   SELECT SETTINGS" - true on a pad, a riddle here. The word is swapped for the key that does the job
// (S, or Tab), in the one place every string passes through, so the shared text stays shared.
static std::string forThisKeyboard(const std::string &text)
{
    // the lone button letters above the button pictures: the boxes carry their own labels now
    if (text == "SELECT" || text == "A" || text == "B") return std::string();
    const std::string::size_type at = text.find("SELECT ");
    if (at == std::string::npos) return text;
    if (at > 0 && text[at - 1] != ' ') return text; // part of a longer word
    if (text.compare(0, 2, "A ") == 0 && at == 2) return text; // "A SELECT": there SELECT is the verb, not the button
    return text.substr(0, at) + "S " + text.substr(at + 7);
}

int TextRenderer::width(const std::string &raw, int size) const
{
    const std::string text = forThisKeyboard(raw);
    const int f = face(size);
    return f < 0 ? 0 : (pixelScale > 1 ? 1 : 2) * bh_font_width(font, f, text.c_str());
}

int TextRenderer::lineHeight(int size) const
{
    const int f = face(size);
    return f < 0 ? 0 : (pixelScale > 1 ? 1 : 2) * bh_font_height(font, f);
}

void TextRenderer::draw(Renderer &renderer, const std::string &text, int x, int y, int size, Rgba color)
{
    const int f = face(size);
    if (f < 0 || !renderer.surface) return;
    const int d = pixelScale > 1 ? 1 : 2;
    bh_font_draw(renderer.surface, font, f, text.c_str(), x / d, y / d, Renderer::paletteIndex(color.r, color.g, color.b));
}

// DRAWN ONCE, COPIED AFTERWARDS. An outlined line is five passes over its glyphs, each a glyph lookup and a masked
// copy per character; the HUD and the menus redraw the same few lines every frame, and that was 12% of a frame on
// a 68040. The finished line is kept as a small 8-bit picture (0 = nothing) and blitted from then on - the same
// cure the SF2000 build needed (TextRenderer's overlay capture). A sliding banner moves its text, not its pixels,
// so it hits the cache too.
namespace {
struct CachedLine {
    std::string text;
    int face = -1, w = 0, h = 0;
    unsigned char ink = 0, edge = 0;
    unsigned long lastUse = 0;
    std::vector<unsigned char> pixels;
};
std::vector<CachedLine> gLines;
unsigned long gLineClock = 0;
const size_t kMaxLines = 16;
} // namespace

void TextRenderer::drawOutlined(Renderer &renderer, const std::string &raw, int x, int y, int size, Rgba color,
                                int /*outlineWidth*/, Rgba outline)
{
    const std::string text = forThisKeyboard(raw);
    const int f = face(size);
    if (f < 0 || !renderer.surface || text.empty()) return;
    const BHSurface *dst = renderer.surface;
    const unsigned char ink = Renderer::paletteIndex(color.r, color.g, color.b);
    const unsigned char edge = Renderer::paletteIndex(outline.r, outline.g, outline.b);

    CachedLine *line = 0;
    for (size_t i = 0; i < gLines.size(); i++) {
        CachedLine &c = gLines[i];
        if (c.face == f && c.ink == ink && c.edge == edge && c.text == text) {
            line = &c;
            break;
        }
    }
    if (!line) {
        if (gLines.size() < kMaxLines) {
            gLines.push_back(CachedLine());
            line = &gLines.back();
        } else {
            line = &gLines[0];
            for (size_t i = 1; i < gLines.size(); i++)
                if (gLines[i].lastUse < line->lastUse) line = &gLines[i];
        }
        line->text = text;
        line->face = f;
        line->ink = ink;
        line->edge = edge;
        // one pixel of outline all round, and room below for the glyphs that hang under the line
        line->w = bh_font_width(font, f, text.c_str()) + 2;
        line->h = bh_font_height(font, f) + 6;
        line->pixels.assign(size_t(line->w) * size_t(line->h), 0);
        BHSurface tmp;
        tmp.pixels = &line->pixels[0];
        tmp.pitch = line->w;
        tmp.width = line->w;
        tmp.height = line->h;
        const char *s = text.c_str();
        // One pixel all round: the logical outline is 2 or 3, and half of either is one pixel here.
        bh_font_draw(&tmp, font, f, s, 0, 1, edge);
        bh_font_draw(&tmp, font, f, s, 2, 1, edge);
        bh_font_draw(&tmp, font, f, s, 1, 0, edge);
        bh_font_draw(&tmp, font, f, s, 1, 2, edge);
        bh_font_draw(&tmp, font, f, s, 1, 1, ink);
    }
    line->lastUse = ++gLineClock;

    // the masked copy, clipped by moving the start
    const int d = pixelScale > 1 ? 1 : 2;
    int dx = x / d - 1, dy = y / d - 1, sx = 0, sy = 0, w = line->w, h = line->h;
    if (dx < 0) { sx = -dx; w += dx; dx = 0; }
    if (dy < 0) { sy = -dy; h += dy; dy = 0; }
    if (dx + w > dst->width) w = dst->width - dx;
    if (dy + h > dst->height) h = dst->height - dy;
    if (w <= 0 || h <= 0) return;
    const unsigned char *src = &line->pixels[size_t(sy) * size_t(line->w) + size_t(sx)];
    unsigned char *out = dst->pixels + (unsigned long)dy * (unsigned long)dst->pitch + (unsigned long)dx;
    const int srcPitch = line->w, dstPitch = dst->pitch;
    for (int r = 0; r < h; r++, src += srcPitch, out += dstPitch)
        for (int c = 0; c < w; c++)
            if (src[c]) out[c] = src[c];
}

static void labelledBox(Renderer &r, int x, int y, int w, int h, bool alignRight, const char *top, const char *bottom)
{
    if (!r.font) return;
    TextRenderer text;
    text.font = r.font;
    text.pixelScale = r.pixelScale;
    const int size = 12;                        // the smallest face: 6 pixels at 320, 12 at 640
    const int d = r.pixelScale > 1 ? 1 : 2;     // logical units per pixel
    const int tw1 = text.width(top, size) / d, tw2 = text.width(bottom, size) / d, lh = text.lineHeight(size) / d;
    int bw = (tw1 > tw2 ? tw1 : tw2) + 10 * r.pixelScale;
    if (bw < w) bw = w;
    int bh = 2 * lh + 8 * r.pixelScale;
    if (bh < h) bh = h;
    const int bx = alignRight ? x + w - bw : x;
    const BHSurface *s = r.surface;
    bh_fill_rect(s, bx, y, bw, bh, BH_UI_OUTLINE);
    bh_fill_rect(s, bx + 1, y + 1, bw - 2, bh - 2, BH_UI_BAR_A);
    const int ty = y + (bh - 2 * lh - 2) / 2;
    const Rgba white{mreal(1), mreal(1), mreal(1), mreal(1)}, black{mreal(0), mreal(0), mreal(0), mreal(1)};
    const Rgba yellow{mreal(0xF8) / mreal(255), mreal(0xE8) / mreal(255), mreal(0x4D) / mreal(255), mreal(1)};
    text.drawOutlined(r, top, d * (bx + (bw - tw1) / 2), d * ty, size, yellow, 2, black);
    text.drawOutlined(r, bottom, d * (bx + (bw - tw2) / 2), d * (ty + lh + 2), size, white, 2, black);
}

// cr::Input, the one method this build uses. The class is the shared one (engine/input.h); its SDL half lives in
// engine/input.cpp, which is not compiled here. The platform hands over a button mask per logic step through
// setSynthetic() - the same road the SF2000 core and the bots use - and this latches it.
void Input::step()
{
    prev_ = cur_;
    cur_ = synthetic_;
}

} // namespace cr
