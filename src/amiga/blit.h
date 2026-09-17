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

/* Draw sprite `id` so that the model's own origin lands on (x, y) - i.e. anchor-relative. This is what the scene
 * renderer uses: it projects a world position to a pixel and hands it straight over. */
void bh_blit_at_anchor(const BHSurface *dst, const BHSprites *s, int id, int x, int y);

/* Fill the whole surface with one palette index (the sky, before anything else is drawn). */
void bh_clear(const BHSurface *dst, unsigned char index);

/* Fill one rectangle, clipped. Used by the status bar and by solid UI panels. */
void bh_fill_rect(const BHSurface *dst, int x, int y, int w, int h, unsigned char index);

#ifdef __cplusplus
}
#endif

#endif /* BH_BLIT_H */
