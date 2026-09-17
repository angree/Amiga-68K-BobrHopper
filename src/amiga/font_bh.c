/* Drawing with the game's own font. See font_bh.h for the format and why it exists.
 *
 * Everything here is deliberately plain: the glyph tables are read where they lie (big-endian, so the 68k does
 * no conversion), and drawing is a masked copy of one byte per pixel. The font is a pixel font, so there is no
 * blending to do and no threshold to test - a non-zero byte means ink.
 */
#include "font_bh.h"

#include <exec/memory.h>
#include <exec/types.h>
#include <proto/exec.h>
#include <stdio.h>

#define BH_FONT_HEADER 8
#define BH_FACE_BYTES 16
#define BH_GLYPH_BYTES 16

static unsigned short be16(const unsigned char *p) { return (unsigned short)((p[0] << 8) | p[1]); }
static unsigned long be32(const unsigned char *p)
{
    return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) | ((unsigned long)p[2] << 8) |
           (unsigned long)p[3];
}

int bh_font_load(BHFont *f, const char *path)
{
    FILE *fp;
    long size;
    int i;

    f->data = 0;
    f->faceCount = 0;

    fp = fopen(path, "rb");
    if (!fp) {
        printf("font: cannot open %s\n", path);
        return 0;
    }
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (size <= BH_FONT_HEADER) {
        printf("font: %s is too small\n", path);
        fclose(fp);
        return 0;
    }
    /* Total free says nothing about whether ONE block this size exists. */
    if (AvailMem(MEMF_ANY | MEMF_LARGEST) < (unsigned long)size) {
        printf("font: need %ld bytes in one block\n", size);
        fclose(fp);
        return 0;
    }
    f->data = (unsigned char *)AllocVec((unsigned long)size, MEMF_ANY);
    if (!f->data) {
        printf("font: AllocVec(%ld) failed\n", size);
        fclose(fp);
        return 0;
    }
    if (fread(f->data, 1, (size_t)size, fp) != (size_t)size) {
        printf("font: short read on %s\n", path);
        fclose(fp);
        bh_font_free(f);
        return 0;
    }
    fclose(fp);
    f->bytes = (unsigned long)size;

    if (f->data[0] != 'B' || f->data[1] != 'H' || f->data[2] != 'F' || f->data[3] != 'N') {
        printf("font: %s is not a BHFN file\n", path);
        bh_font_free(f);
        return 0;
    }
    if (be16(f->data + 4) != 1) {
        printf("font: %s is version %d, expected 1\n", path, (int)be16(f->data + 4));
        bh_font_free(f);
        return 0;
    }
    f->faceCount = (int)be16(f->data + 6);
    if (f->faceCount < 1 || f->faceCount > (int)(sizeof(f->faces) / sizeof(f->faces[0]))) {
        printf("font: %s claims %d faces\n", path, f->faceCount);
        bh_font_free(f);
        return 0;
    }

    for (i = 0; i < f->faceCount; i++) {
        const unsigned char *e = f->data + BH_FONT_HEADER + i * BH_FACE_BYTES;
        BHFace *face = &f->faces[i];
        unsigned long off;
        face->pixelSize = be16(e);
        face->lineHeight = be16(e + 2);
        face->glyphCount = be16(e + 4);
        face->atlasW = be16(e + 6);
        face->atlasH = be16(e + 8);
        off = be32(e + 12);
        if (off + (unsigned long)face->glyphCount * BH_GLYPH_BYTES > f->bytes) {
            printf("font: face %d points outside the file\n", i);
            bh_font_free(f);
            return 0;
        }
        face->glyphs = (const BHGlyph *)(f->data + off);
        face->pixels = f->data + off + (unsigned long)face->glyphCount * BH_GLYPH_BYTES;
    }
    printf("font: %d faces, %lu bytes (%d px, %d px)\n", f->faceCount, f->bytes, (int)f->faces[0].pixelSize,
           f->faceCount > 1 ? (int)f->faces[1].pixelSize : 0);
    return 1;
}

void bh_font_free(BHFont *f)
{
    if (f->data) FreeVec(f->data);
    f->data = 0;
    f->bytes = 0;
    f->faceCount = 0;
}

/* Linear search. The faces carry 90 glyphs and a line of text is a dozen characters, so a lookup table would
 * cost more to build than it saves - and this runs while a menu is on screen, never inside the game loop's
 * hot path. */
static const BHGlyph *find_glyph(const BHFace *face, unsigned short cp)
{
    int i;
    for (i = 0; i < (int)face->glyphCount; i++) {
        const BHGlyph *g = &face->glyphs[i];
        if (be16((const unsigned char *)&g->codepoint) == cp) return g;
    }
    return 0;
}

static int face_ok(const BHFont *f, int face) { return f->data && face >= 0 && face < f->faceCount; }

/* ONE CHARACTER, NOT ONE BYTE.
 *
 * The shared text (src/ui/lang.cpp) is UTF-8 and the Polish words in it need letters the baked font carries at
 * their Unicode codepoints: A-ogonek 260, E-ogonek 280, L-stroke 321, N-acute 323, S-acute 346, Z-acute 377,
 * Z-dot 379. Reading a byte at a time would look for glyph 0xC5, find nothing, and print DZWIEKI as two pieces
 * of rubbish per letter - and, worse, bh_font_width would count two characters where one is drawn, so every
 * centred line in the menus would sit off to one side.
 *
 * Two-byte sequences are all this needs (everything below U+0800 is covered, and the font has nothing above).
 * Anything malformed is returned as the raw byte, which is what the ASCII path did anyway. */
static unsigned short next_codepoint(const char **text)
{
    const unsigned char *p = (const unsigned char *)*text;
    const unsigned char c = *p;
    if (c >= 0xC0 && c <= 0xDF && (p[1] & 0xC0) == 0x80) {
        *text += 2;
        return (unsigned short)(((c & 0x1F) << 6) | (p[1] & 0x3F));
    }
    *text += 1;
    return (unsigned short)c;
}

int bh_font_height(const BHFont *f, int face)
{
    if (!face_ok(f, face)) return 0;
    return (int)f->faces[face].lineHeight;
}

static void draw_glyph(const BHSurface *dst, const BHFace *face, const BHGlyph *g, int penX, int penY,
                       unsigned char colour)
{
    const int gx = (int)be16((const unsigned char *)&g->x);
    const int gy = (int)be16((const unsigned char *)&g->y);
    const int gw = (int)be16((const unsigned char *)&g->w);
    const int gh = (int)be16((const unsigned char *)&g->h);
    const int xoff = (short)be16((const unsigned char *)&g->xoff);
    const int yoff = (short)be16((const unsigned char *)&g->yoff);
    const int atlasW = (int)face->atlasW;
    const int pitch = dst->pitch, dw = dst->width, dh = dst->height;
    int x0 = penX + xoff, y0 = penY + yoff;
    int skipX = 0, skipY = 0, cols = gw, rows = gh, r, c;
    const unsigned char *src;
    unsigned char *out;

    if (gw <= 0 || gh <= 0) return;
    if (x0 >= dw || y0 >= dh || x0 + gw <= 0 || y0 + gh <= 0) return;

    /* Clip by moving the start, never by testing inside the loop. */
    if (x0 < 0) { skipX = -x0; x0 = 0; }
    if (y0 < 0) { skipY = -y0; y0 = 0; }
    cols = gw - skipX;
    rows = gh - skipY;
    if (x0 + cols > dw) cols = dw - x0;
    if (y0 + rows > dh) rows = dh - y0;
    if (cols <= 0 || rows <= 0) return;

    src = face->pixels + (unsigned long)(gy + skipY) * (unsigned long)atlasW + (unsigned long)(gx + skipX);
    out = dst->pixels + (unsigned long)y0 * (unsigned long)pitch + (unsigned long)x0;

    for (r = 0; r < rows; r++) {
        for (c = 0; c < cols; c++)
            if (src[c]) out[c] = colour;
        src += atlasW;
        out += pitch;
    }
}

int bh_font_draw(const BHSurface *dst, const BHFont *f, int face, const char *text, int x, int y,
                 unsigned char colour)
{
    const BHFace *fc;
    if (!face_ok(f, face) || !text) return x;
    fc = &f->faces[face];
    while (*text) {
        const unsigned short ch = next_codepoint(&text);
        const BHGlyph *g = find_glyph(fc, ch);
        if (!g) {
            x += fc->pixelSize / 2; /* unknown character: a space's worth of pen movement */
            continue;
        }
        draw_glyph(dst, fc, g, x, y, colour);
        x += (int)be16((const unsigned char *)&g->advance);
    }
    return x;
}

int bh_font_draw_int(const BHSurface *dst, const BHFont *f, int face, int value, int x, int y,
                     unsigned char colour)
{
    char buf[12];
    int n = 0, i;
    if (value < 0) value = 0;
    do {
        buf[n++] = (char)('0' + (value % 10));
        value /= 10;
    } while (value && n < (int)sizeof(buf) - 1);
    for (i = n - 1; i >= 0; i--) {
        const char one[2] = {buf[i], 0};
        x = bh_font_draw(dst, f, face, one, x, y, colour);
    }
    return x;
}

int bh_font_width(const BHFont *f, int face, const char *text)
{
    const BHFace *fc;
    int w = 0;
    if (!face_ok(f, face) || !text) return 0;
    fc = &f->faces[face];
    while (*text) {
        /* The SAME decoder the drawing loop uses: if these two ever disagree about how many characters a string
         * has, the text is drawn correctly and centred wrongly, which is the harder bug to see. */
        const unsigned short ch = next_codepoint(&text);
        const BHGlyph *g = find_glyph(fc, ch);
        w += g ? (int)be16((const unsigned char *)&g->advance) : fc->pixelSize / 2;
    }
    return w;
}

int bh_font_width_int(const BHFont *f, int face, int value)
{
    char buf[12];
    int n = 0, i, w = 0;
    if (value < 0) value = 0;
    do {
        buf[n++] = (char)('0' + (value % 10));
        value /= 10;
    } while (value && n < (int)sizeof(buf) - 1);
    for (i = n - 1; i >= 0; i--) {
        const char one[2] = {buf[i], 0};
        w += bh_font_width(f, face, one);
    }
    return w;
}
