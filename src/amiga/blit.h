/* Sprite blitting into the chunky 8bpp buffer.
 *
 * This is the whole of the Amiga renderer's inner loop, so it is deliberately dull: no scaling, no rotation, no
 * blending. The baked sprites are already at the game's exact scale (the camera is orthographic and never rotates,
 * so an object's screen size never changes), which turns drawing into a masked copy - the cheapest thing a 68020
 * can do with memory.
 *
 * Palette index 0 is transparent and is never written, exactly like the sibling GTA port's sprite blitter.
 */
#ifndef BH_BLIT_H
#define BH_BLIT_H

#include "sprites.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Destination surface: the chunky buffer, its row stride, and the drawable area. The stride is NOT always the width
 * (in Workbench window mode amiga_gfx keeps a buffer as wide as the whole screen), so it is always passed in. */
typedef struct {
    unsigned char *pixels;
    int pitch;
    int width, height;
} BHSurface;

/* Draw sprite `id` with its top-left corner at (x, y), clipped to the surface. Index 0 is left untouched.
 * The caller subtracts the sprite's anchor from the projected position, so placement stays the renderer's business
 * and this stays a copy. */
void bh_blit(const BHSurface *dst, const BHSprites *s, int id, int x, int y);

/* P3/K1: learn which sprites have edges that only ever move one way down the rows (the ground strips are sheared
 * parallelograms). For those bh_blit finds the rows that can touch the clipped columns by binary search instead of
 * visiting every row - a strip 684 wide clipped to 320 had ~50 empty rows each, ~1,150 a frame. Call after every
 * load of a sprite set; a set it was not prepared for is drawn the old way. */
void bh_blit_prepare(const BHSprites *s);

/* Draw sprite `id` so that the model's own origin lands on (x, y) - i.e. anchor-relative. This is what the scene
 * renderer uses: it projects a world position to a pixel and hands it straight over. */
void bh_blit_at_anchor(const BHSurface *dst, const BHSprites *s, int id, int x, int y);

/* MEASUREMENT (P2): what the blitter actually does, per layer (0 ground, 1 everything else - set by the caller).
 * calls, rows handed a span, rows with nothing to copy, pixels copied by the long-word path, pixels tested one by
 * one. The per-row counts (empty rows, pixels) cost a few instructions a row in every game, so they exist only when
 * built with BH_BLIT_STATS; calls and rows are counted per call. Read and cleared by the profile report. */
extern int bh_stat_layer;
extern unsigned long bh_stat_enter, bh_stat_k1; /* every call, and every call that searched for its rows */
extern unsigned long bh_stat_calls[2], bh_stat_rows[2], bh_stat_empty[2], bh_stat_solid[2], bh_stat_masked[2];

/* Copy n bytes, long words while they last. Measured (yardstick, 320x229 window): a misaligned source costs the
 * same as an aligned one here, 1.1-1.6 ms, while reading aligned and shifting the bytes into place cost 5-6 ms. */
void bh_copy(unsigned char *dst, const unsigned char *src, long n);

/* Fill the whole surface with one palette index (the sky, before anything else is drawn). */
void bh_clear(const BHSurface *dst, unsigned char index);

/* Fill one rectangle, clipped. Used by the status bar and by solid UI panels. */
void bh_fill_rect(const BHSurface *dst, int x, int y, int w, int h, unsigned char index);

#ifdef __cplusplus
}
#endif

#endif /* BH_BLIT_H */
