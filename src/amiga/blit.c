/* The masked blit. See blit.h for why this is allowed to be so plain.
 *
 * Two rules carried over from the sibling ports' renderers, both about the inner loop:
 *   - CLIP BY MOVING THE START, not by testing inside the loop. A sprite hanging off the left edge has its source
 *     pointer and width adjusted once, before the rows begin.
 *   - NOTHING IN THE INNER LOOP MAY TOUCH A STRUCT through a pointer. gcc 6.5 at -O1 reloads struct fields on every
 *     iteration when it cannot prove they did not change, so pitch and width are copied into locals first. The GTA
 *     port measured this as a real cost, not a theoretical one.
 */
#include "blit.h"

void bh_blit(const BHSurface *dst, const BHSprites *s, int id, int x, int y)
{
    const BHSpriteEntry *e;
    const unsigned char *src;
    unsigned char *out;
    int sw, sh, pitch, dw, dh;
    int skipX = 0, skipY = 0, cols, rows, r, c;

    if (id < 0 || id >= s->count) return;
    e = &s->entries[id];
    sw = (int)e->w;
    sh = (int)e->h;
    pitch = dst->pitch;
    dw = dst->width;
    dh = dst->height;

    if (x >= dw || y >= dh) return;
    if (x + sw <= 0 || y + sh <= 0) return;

    if (x < 0) { skipX = -x; x = 0; }
    if (y < 0) { skipY = -y; y = 0; }
    cols = sw - skipX;
    rows = sh - skipY;
    if (x + cols > dw) cols = dw - x;
    if (y + rows > dh) rows = dh - y;
    if (cols <= 0 || rows <= 0) return;

    src = s->data + e->offset + (unsigned long)skipY * (unsigned long)sw + (unsigned long)skipX;
    out = dst->pixels + (unsigned long)y * (unsigned long)pitch + (unsigned long)x;

    /* ROW SPANS (see BHSpan): skip the transparent margins outright and copy the unbroken run. src/out point at
     * the first VISIBLE column, which is sprite column skipX, so the span is clipped to [skipX, skipX+cols). */
    {
        const BHSpan *span = s->spans + s->spanStart[id] + skipY;
        const int lo = skipX, hi = skipX + cols;
        for (r = 0; r < rows; r++, span++, src += sw, out += pitch) {
            int a = (int)span->first, b = (int)span->last;
            if (a < lo) a = lo;
            if (b > hi) b = hi;
            if (a >= b) continue;
            if (span->solid) {
                const unsigned char *sp = src + (a - lo);
                unsigned char *op = out + (a - lo);
                int n = b - a;
                /* Longs while they last. No alignment games: the 020+ reads unaligned longs correctly, and the
                 * runs are short enough that setup would cost more than it saves. */
                /* Sixteen bytes a turn: measured with the microsecond clock, the floors were 7 ms a frame and the
                 * loop's own bookkeeping - a compare, a branch, three additions per LONG - was most of it. */
                while (n >= 16) {
                    ((unsigned long *)op)[0] = ((const unsigned long *)sp)[0];
                    ((unsigned long *)op)[1] = ((const unsigned long *)sp)[1];
                    ((unsigned long *)op)[2] = ((const unsigned long *)sp)[2];
                    ((unsigned long *)op)[3] = ((const unsigned long *)sp)[3];
                    op += 16; sp += 16; n -= 16;
                }
                while (n >= 4) {
                    *(unsigned long *)op = *(const unsigned long *)sp;
                    op += 4; sp += 4; n -= 4;
                }
                while (n-- > 0) *op++ = *sp++;
            } else {
                for (c = a - lo; c < b - lo; c++) {
                    const unsigned char p = src[c];
                    if (p) out[c] = p; /* index 0 is the transparent key */
                }
            }
        }
    }
}

void bh_blit_at_anchor(const BHSurface *dst, const BHSprites *s, int id, int x, int y)
{
    const BHSpriteEntry *e;
    if (id < 0 || id >= s->count) return;
    e = &s->entries[id];
    bh_blit(dst, s, id, x - (int)e->anchorX, y - (int)e->anchorY);
}

void bh_clear(const BHSurface *dst, unsigned char index)
{
    unsigned char *out = dst->pixels;
    const int pitch = dst->pitch, w = dst->width, h = dst->height;
    int r, c;
    const unsigned long fill = (unsigned long)index * 0x01010101UL;
    for (r = 0; r < h; r++) {
        unsigned char *op = out;
        for (c = w; c >= 16; c -= 16, op += 16) {
            ((unsigned long *)op)[0] = fill;
            ((unsigned long *)op)[1] = fill;
            ((unsigned long *)op)[2] = fill;
            ((unsigned long *)op)[3] = fill;
        }
        for (; c >= 4; c -= 4, op += 4) *(unsigned long *)op = fill;
        while (c-- > 0) *op++ = index;
        out += pitch;
    }
}

void bh_fill_rect(const BHSurface *dst, int x, int y, int w, int h, unsigned char index)
{
    unsigned char *out;
    const int pitch = dst->pitch, dw = dst->width, dh = dst->height;
    int r, c;

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > dw) w = dw - x;
    if (y + h > dh) h = dh - y;
    if (w <= 0 || h <= 0) return;

    out = dst->pixels + (unsigned long)y * (unsigned long)pitch + (unsigned long)x;
    for (r = 0; r < h; r++) {
        for (c = 0; c < w; c++) out[c] = index;
        out += pitch;
    }
}
