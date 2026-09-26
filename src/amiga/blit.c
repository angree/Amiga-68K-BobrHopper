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

#include <stdio.h>
#include <stdlib.h>

int bh_stat_layer;
unsigned long bh_stat_enter, bh_stat_k1;
unsigned long bh_stat_calls[2], bh_stat_rows[2], bh_stat_empty[2], bh_stat_solid[2], bh_stat_masked[2];

/* K1: the ground strips are long sheared bands - on screen a flat diamond. Going down the rows, the LEFT edge of
 * such a shape moves one way and then the other (first falls, then rises) and the RIGHT edge the opposite way. For
 * any shape like that ("convex by rows") the rows that meet a range of columns are one unbroken run, and each end of
 * it is found by binary search on the half of an edge where it is monotonic. Per sprite: whether it qualifies, its
 * run of non-empty rows [top, end), the row where `first` is smallest and the row where `last` is largest. */
static const BHSprites *bh_convFor;
static unsigned char *bh_conv;
static unsigned short *bh_convTop, *bh_convEnd, *bh_convMinF, *bh_convMaxL;

void bh_blit_prepare(const BHSprites *s)
{
    int id, count = 0;
    const size_t n = (size_t)s->count + 1;
    free(bh_conv);
    free(bh_convTop);
    free(bh_convEnd);
    free(bh_convMinF);
    free(bh_convMaxL);
    bh_convFor = 0;
    bh_conv = (unsigned char *)malloc(n);
    bh_convTop = (unsigned short *)malloc(n * sizeof(unsigned short));
    bh_convEnd = (unsigned short *)malloc(n * sizeof(unsigned short));
    bh_convMinF = (unsigned short *)malloc(n * sizeof(unsigned short));
    bh_convMaxL = (unsigned short *)malloc(n * sizeof(unsigned short));
    if (!bh_conv || !bh_convTop || !bh_convEnd || !bh_convMinF || !bh_convMaxL) return;
    for (id = 0; id < s->count; id++) {
        const BHSpan *sp = s->spans + s->spanStart[id];
        const int h = (int)s->entries[id].h;
        int r, top = -1, end = -1, gap = 0, ok, mf, ml;
        for (r = 0; r < h; r++) {
            if (sp[r].first >= sp[r].last) {
                if (top >= 0 && end < 0) end = r;
                continue;
            }
            if (end >= 0) gap = 1; /* a second run of rows */
            if (top < 0) top = r;
        }
        if (top < 0) top = end = 0;
        if (end < 0) end = h;
        mf = ml = top;
        for (r = top; r < end; r++) {
            if (sp[r].first < sp[mf].first) mf = r;
            if (sp[r].last > sp[ml].last) ml = r;
        }
        ok = !gap && end > top;
        for (r = top + 1; ok && r < end; r++) {
            if (r <= mf ? sp[r].first > sp[r - 1].first : sp[r].first < sp[r - 1].first) ok = 0;
            if (r <= ml ? sp[r].last < sp[r - 1].last : sp[r].last > sp[r - 1].last) ok = 0;
        }
        bh_conv[id] = (unsigned char)ok;
        bh_convTop[id] = (unsigned short)top;
        bh_convEnd[id] = (unsigned short)end;
        bh_convMinF[id] = (unsigned short)mf;
        bh_convMaxL[id] = (unsigned short)ml;
        if (ok) count++;
    }
    bh_convFor = s;
    printf("blit: %d of %d sprites are convex by rows (empty rows found by binary search)\n", count, s->count);
}

void bh_blit(const BHSurface *dst, const BHSprites *s, int id, int x, int y)
{
    const BHSpriteEntry *e;
    const unsigned char *src;
    unsigned char *out;
    int sw, sh, pitch, dw, dh;
    int skipX = 0, skipY = 0, cols, rows, r, c;

    bh_stat_enter++;
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

    if (s == bh_convFor && bh_conv[id]) {
        /* K1: only the rows whose span meets [skipX, skipX + cols) - see bh_blit_prepare. The searches run inside
         * the rows the vertical clip left, so a sliver of a strip costs a few steps, not a search of 183 rows. */
        const BHSpan *sp = s->spans + s->spanStart[id];
        const int lo = skipX, hi = skipX + cols;
        const int mf = (int)bh_convMinF[id], ml = (int)bh_convMaxL[id];
        int r0 = skipY, r1 = skipY + rows, a, b, m;
        if (r0 < (int)bh_convTop[id]) r0 = (int)bh_convTop[id];
        if (r1 > (int)bh_convEnd[id]) r1 = (int)bh_convEnd[id];
        if (r1 <= r0) return;
        bh_stat_k1++;
        /* first < hi: false then true above mf (first falls), true then false below it (first rises) */
        if (r0 < mf) {
            a = r0; b = mf < r1 ? mf : r1;
            while (a < b) { m = (a + b) >> 1; if ((int)sp[m].first < hi) b = m; else a = m + 1; }
            r0 = a;
        }
        a = r0 > mf ? r0 : mf; b = r1;
        while (a < b) { m = (a + b) >> 1; if ((int)sp[m].first >= hi) b = m; else a = m + 1; }
        if (r1 > a) r1 = a;
        /* last > lo: the same about ml, the other way round */
        if (r0 < ml) {
            a = r0; b = ml < r1 ? ml : r1;
            while (a < b) { m = (a + b) >> 1; if ((int)sp[m].last > lo) b = m; else a = m + 1; }
            r0 = a;
        }
        a = r0 > ml ? r0 : ml; b = r1;
        while (a < b) { m = (a + b) >> 1; if ((int)sp[m].last <= lo) b = m; else a = m + 1; }
        if (r1 > a) r1 = a;
        if (r1 <= r0) return;
        y += r0 - skipY;
        rows = r1 - r0;
        skipY = r0;
    }

    bh_stat_calls[bh_stat_layer]++;
    bh_stat_rows[bh_stat_layer] += (unsigned long)rows;
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
            if (a >= b) {
#ifdef BH_BLIT_STATS
                bh_stat_empty[bh_stat_layer]++;
#endif
                continue;
            }
            if (span->solid) {
#ifdef BH_BLIT_STATS
                bh_stat_solid[bh_stat_layer] += (unsigned long)(b - a);
#endif
                const unsigned char *sp = src + (a - lo);
                unsigned char *op = out + (a - lo);
                const int n = b - a;
                /* P3: DUFF'S DEVICE. Longs while they last (the 020+ reads and writes misaligned longs by itself),
                 * entered part-way through an unrolled loop of eight, then the last 0-3 bytes by a jump. The old
                 * copy was three loops of sixteen bytes, four and one, and setting each of them up cost more than a
                 * row of a car's worth of copying: about 35 instructions of bookkeeping a row. */
                int longs = n >> 2;
                if (longs > 0) {
                    const unsigned long *ls = (const unsigned long *)sp;
                    unsigned long *ld = (unsigned long *)op;
                    int turns = (longs + 7) >> 3;
                    switch (longs & 7) {
                    case 0: do { *ld++ = *ls++;
                    case 7:      *ld++ = *ls++;
                    case 6:      *ld++ = *ls++;
                    case 5:      *ld++ = *ls++;
                    case 4:      *ld++ = *ls++;
                    case 3:      *ld++ = *ls++;
                    case 2:      *ld++ = *ls++;
                    case 1:      *ld++ = *ls++;
                            } while (--turns > 0);
                    }
                    sp = (const unsigned char *)ls;
                    op = (unsigned char *)ld;
                }
                switch (n & 3) {
                case 3: *op++ = *sp++; /* fall through */
                case 2: *op++ = *sp++; /* fall through */
                case 1: *op = *sp;
                }
            } else {
#ifdef BH_BLIT_STATS
                bh_stat_masked[bh_stat_layer] += (unsigned long)(b - a);
#endif
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

void bh_copy(unsigned char *dst, const unsigned char *src, long n)
{
    while (n >= 16) {
        ((unsigned long *)dst)[0] = ((const unsigned long *)src)[0];
        ((unsigned long *)dst)[1] = ((const unsigned long *)src)[1];
        ((unsigned long *)dst)[2] = ((const unsigned long *)src)[2];
        ((unsigned long *)dst)[3] = ((const unsigned long *)src)[3];
        dst += 16; src += 16; n -= 16;
    }
    while (n >= 4) {
        *(unsigned long *)dst = *(const unsigned long *)src;
        dst += 4; src += 4; n -= 4;
    }
    while (n-- > 0) *dst++ = *src++;
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
