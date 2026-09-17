/* Sprite container for the Amiga port: one file, one read, straight into fast RAM.
 *
 * The file is produced by tools/pack_amiga_sprites.py from what apps/sw_bake_amiga.cpp rendered, and it is
 * BIG-ENDIAN on purpose: the 68k reads every field exactly as it lies on disk, so loading costs one read and no
 * conversion at all. The entry table is 12 bytes per sprite and naturally aligned, so the runtime overlays this
 * struct on the buffer instead of parsing it.
 *
 * Layout (see the packer for the authoritative version):
 *     0  char[4]  'BHSP'
 *     4  UWORD    version (1)
 *     6  UWORD    sprite count
 *     8  UWORD    palette entries used
 *    10  UWORD    reserved
 *    12  UBYTE[768] palette, RGB triples; entry 0 is the transparent key and is never drawn
 *   780  BHSpriteEntry[count]
 *   ...  pixels, w*h bytes per sprite, one palette index each, 0 = transparent
 *
 * This header is plain C and pulls in no Amiga proto headers: Amiga headers and the game's C++ must never meet (the
 * dos/intuition macros Insert, Remove and Allocate, and the type Point, collide head-on with game names - that
 * collision is what sank an earlier attempt in the sibling OpenTTD port).
 */
#ifndef BH_SPRITES_H
#define BH_SPRITES_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned short w, h;    /* pixels */
    short anchorX, anchorY; /* where the model's own origin sits inside the sprite */
    unsigned long offset;   /* byte offset of the pixels within the loaded file */
} BHSpriteEntry;

/* ONE ROW OF ONE SPRITE, described once at load time so the blitter never has to look at a transparent pixel.
 * Measured on an honest 68040 (no JIT, -80%): testing every pixel of every sprite cost 140 ms a frame. Voxel
 * sprites are convex blobs and the row strips are solid parallelograms, so nearly every row is "nothing, then one
 * unbroken run, then nothing" - and an unbroken run is a plain memory copy. */
typedef struct {
    unsigned short first; /* first non-transparent pixel in the row */
    unsigned short last;  /* one past the last non-transparent pixel; first == last means an empty row */
    unsigned short solid; /* non-zero: no transparent pixel between first and last - copy, do not test */
} BHSpan;

typedef struct {
    unsigned char *data;        /* the whole file, one allocation in fast RAM */
    BHSpan *spans;              /* one per sprite row, all sprites back to back */
    unsigned long *spanStart;   /* index of sprite i's first span */
    unsigned long bytes;
    int count;
    int paletteEntries;
    const unsigned char *palette;   /* 768 bytes, RGB triples */
    const BHSpriteEntry *entries;   /* count of them, big-endian as loaded */
} BHSprites;

/* Loads <path> (e.g. "PROGDIR:data/sprites.spr"). Returns 0 on failure and logs why.
 * Everything lives in ONE allocation: sprites are read far more often than they are freed, and a single block
 * cannot fragment fast RAM the way hundreds of small ones would. */
int bh_sprites_load(BHSprites *s, const char *path);
void bh_sprites_free(BHSprites *s);

/* Pixels of sprite `id` (one palette index per pixel, row-major, 0 = transparent). */
const unsigned char *bh_sprite_pixels(const BHSprites *s, int id);

#ifdef __cplusplus
}
#endif

#endif /* BH_SPRITES_H */
