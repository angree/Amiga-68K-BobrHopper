#include "engine/text.h"

#include "engine/log.h"
#include "engine/strings.h"

namespace cr {

static const int kSizes[] = {12, 14, 16, 18, 32, 48};

// the glyph slot of the UTF-8 character at text[i] (i moves past it); -1 when the font has no glyph for it. A stray
// continuation byte counts as a character of its own.
static int nextGlyph(const std::string &text, size_t &i)
{
    const unsigned char lead = static_cast<unsigned char>(text[i++]);
    if (lead < 0x80) return lead;
    int more = lead >= 0xf0 ? 3 : lead >= 0xe0 ? 2 : lead >= 0xc0 ? 1 : 0;
    uint32_t cp = lead & (0x3f >> more);
    for (; more > 0 && i < text.size() && (static_cast<unsigned char>(text[i]) & 0xc0) == 0x80; more--)
        cp = (cp << 6) | (static_cast<unsigned char>(text[i++]) & 0x3f);
    return more ? -1 : glyphSlot(cp);
}

bool TextRenderer::load(Renderer &renderer, const std::string &dataDir)
{
    for (int size : kSizes) {
        Loaded font;
        std::string path = dataDir + "fonts/retro_" + toString(size / glyphScale) + ".fnt";
        if (!loadFont(path, font.data)) {
            logf("text: cannot load %s", path.c_str());
            return false;
        }
        font.texture = renderer.uploadAlphaTexture(font.data.atlasW, font.data.atlasH, font.data.coverage.data());
        fonts_[size] = std::move(font);
    }
    return true;
}

int TextRenderer::width(const std::string &text, int size) const
{
    auto it = fonts_.find(size);
    if (it == fonts_.end()) return 0;
    int w = 0;
    for (size_t i = 0; i < text.size();) {
        const int slot = nextGlyph(text, i);
        if (slot >= 0) w += it->second.data.glyphs[slot].advance * glyphScale;
    }
    return w;
}

int TextRenderer::lineHeight(int size) const
{
    auto it = fonts_.find(size);
    return it == fonts_.end() ? 0 : it->second.data.lineHeight * glyphScale;
}

void TextRenderer::draw(Renderer &renderer, const std::string &text, int x, int y, int size, Rgba color)
{
    auto it = fonts_.find(size);
    if (it == fonts_.end()) return;
    const FontData &f = it->second.data;
    verts_.clear();
    int pen = x;
    for (size_t i = 0; i < text.size();) {
        const int slot = nextGlyph(text, i);
        if (slot < 0) continue;
        const Glyph &g = f.glyphs[slot];
        if (g.w && g.h) {
            const mreal x0 = mreal(pen + g.xoff * glyphScale), y0 = mreal(y + g.yoff * glyphScale);
            const mreal x1 = x0 + mreal(g.w * glyphScale), y1 = y0 + mreal(g.h * glyphScale);
            const mreal u0 = mreal(int(g.x)) / mreal(f.atlasW), v0 = mreal(int(g.y)) / mreal(f.atlasH);
            const mreal u1 = mreal(g.x + g.w) / mreal(f.atlasW), v1 = mreal(g.y + g.h) / mreal(f.atlasH);
            const mreal quad[24] = {x0, y0, u0, v0, x1, y0, u1, v0, x1, y1, u1, v1,
                                    x0, y0, u0, v0, x1, y1, u1, v1, x0, y1, u0, v1};
            verts_.insert(verts_.end(), quad, quad + 24);
        }
        pen += g.advance * glyphScale;
    }
    renderer.drawOverlayTriangles(it->second.texture, verts_, color.r, color.g, color.b, color.a);
}

void TextRenderer::drawOutlined(Renderer &renderer, const std::string &text, int x, int y, int size, Rgba color,
                                int outlineWidth, Rgba outline)
{
    static const int offsets[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
#ifdef CR_FIXED
    // opaque text only: translucent passes blend with what is under them, a capture cannot hold that
    if (color.a == mreal(1) && outline.a == mreal(1)) {
        const auto same = [](const Rgba &p, const Rgba &q) { return p.r == q.r && p.g == q.g && p.b == q.b && p.a == q.a; };
        const Renderer::OverlayState state = renderer.overlayState();
        for (CachedText &c : cache_) {
            if (c.x == x && c.y == y && c.size == size && c.outlineWidth == outlineWidth && same(c.color, color) &&
                same(c.outline, outline) && c.state == state && c.text == text) {
                c.lastUse = ++useCount_;
                renderer.drawOverlayCapture(c.capture);
                return;
            }
        }
        Renderer::OverlayCapture capture;
        renderer.beginOverlayCapture();
        for (const auto &o : offsets) draw(renderer, text, x + o[0] * outlineWidth, y + o[1] * outlineWidth, size, outline);
        draw(renderer, text, x, y, size, color);
        renderer.endOverlayCapture(capture);
        if (capture.valid) {
            renderer.drawOverlayCapture(capture);
            CachedText *slot = nullptr;
            if (cache_.size() < 8) {
                cache_.emplace_back();
                slot = &cache_.back();
            } else {
                slot = &cache_[0];
                for (CachedText &c : cache_)
                    if (c.lastUse < slot->lastUse) slot = &c;
            }
            slot->text = text;
            slot->x = x;
            slot->y = y;
            slot->size = size;
            slot->outlineWidth = outlineWidth;
            slot->color = color;
            slot->outline = outline;
            slot->state = state;
            slot->capture = std::move(capture);
            slot->lastUse = ++useCount_;
            return;
        }
        // not exact after all (a translucent glyph pixel): drawn again straight onto the target below
    }
#endif
    for (const auto &o : offsets) draw(renderer, text, x + o[0] * outlineWidth, y + o[1] * outlineWidth, size, outline);
    draw(renderer, text, x, y, size, color);
}

} // namespace cr
