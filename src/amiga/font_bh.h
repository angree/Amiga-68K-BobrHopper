/* The game's own font on the Amiga (task G4).
 *
 * The status bar was drawn with a 5x7 font hand-coded in hud_bh.c. That proved the bar renders; it is not what
 * ships. Every other port of this game draws with the baked retro font - the same glyphs, the same Polish
 * letters - and the user asked for that here too.
 *
 * The container is built by tools/make_amiga_font.py from the .fnt atlases, BIG endian so the 68k reads the
 * tables where they lie, with coverage collapsed to one byte per pixel carrying ink or nothing. There is no
 * anti-aliasing to preserve: it is a pixel font, so a byte that is non-zero means "draw the text colour here"
 * and the inner loop needs no threshold test.
 *
 * Plain C, no Amiga types in this header.
 */
#ifndef BH_FONT_H
#define BH_FONT_H

#include "blit.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned short codepoint;
    unsigned short x, y, w, h; /* the glyph's box inside the atlas */
    short xoff, yoff;          /* where that box sits relative to the pen, on the text's top line */
    unsigned short advance;    /* how far the pen moves afterwards */
} BHGlyph;

typedef struct {
    unsigned short pixelSize, lineHeight, glyphCount, atlasW, atlasH;
    const BHGlyph *glyphs;       /* glyphCount of them, big-endian as loaded */
    const unsigned char *pixels; /* atlasW * atlasH, 1 = ink */
} BHFace;

typedef struct {
    unsigned char *data; /* the whole file, one allocation in fast RAM */
    unsigned long bytes;
    int faceCount;
    BHFace faces[4];
} BHFont;

/* Load the container (e.g. "PROGDIR:data/font.bhf"). Returns 0 and says why on failure; the caller can then
 * fall back to whatever it had, rather than refusing to start. */
int bh_font_load(BHFont *f, const char *path);
void bh_font_free(BHFont *f);

/* Draw text at (x, y), where y is the TOP of the line. Returns the pen position after the last glyph, so
 * callers can chain a number after a label without measuring twice. Characters the font does not carry are
 * skipped with a space's worth of pen movement. */
int bh_font_draw(const BHSurface *dst, const BHFont *f, int face, const char *text, int x, int y,
                 unsigned char colour);

/* The same, for an integer - no sprintf anywhere near this machine, where it prints nonsense. */
int bh_font_draw_int(const BHSurface *dst, const BHFont *f, int face, int value, int x, int y,
                     unsigned char colour);

/* Width in pixels the text would occupy, for centring a menu line without drawing it first. */
int bh_font_width(const BHFont *f, int face, const char *text);
int bh_font_width_int(const BHFont *f, int face, int value);

/* Height of one line of this face. */
int bh_font_height(const BHFont *f, int face);

#ifdef __cplusplus
}
#endif

#endif /* BH_FONT_H */
