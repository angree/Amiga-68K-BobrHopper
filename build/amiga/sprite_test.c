/* First picture on the Amiga (task B2/D1): open a 320x240 8-bit screen, load the baked sprite container, set its
 * palette, blit a few sprites, and push the frame through chunky-to-planar.
 *
 * It exists to answer, in one boot, a list of questions that are otherwise answered by hope: does the container load
 * on the real machine, is the big-endian table usable where it lies, does the palette reach the screen, does the
 * masked blit place sprites where the anchors say, and does Kalms' c2p produce a picture at 320x240.
 *
 * It also writes PROGDIR:frame.raw - the chunky buffer, byte for byte, before c2p. That file is the evidence I
 * trust: a host screenshot of WinUAE's DirectDraw surface can come back black on a perfectly healthy game, so when
 * the screenshot and the dump disagree, the dump wins. tools/raw2png_amiga.py turns it into a PNG.
 *
 * Never sprintf (broken on this libc), never strip the binary (a stripped Hunk executable halts the machine).
 */
#include <proto/dos.h>
#include <stdio.h>

#include "amiga_gfx.h"
#include "blit.h"
#include "sprite_ids.h"
#include "sprites.h"

#define SCREEN_W 320 /* a multiple of 32: both c2p kernels work in 32-pixel columns */
#define SCREEN_H 240

static void dump_frame(const BHSurface *s, const char *path)
{
    FILE *f = fopen(path, "wb");
    int y;
    if (!f) {
        printf("sprite_test: cannot write %s\n", path);
        return;
    }
    /* Row by row, because the pitch is not always the width. */
    for (y = 0; y < s->height; y++)
        fwrite(s->pixels + (unsigned long)y * (unsigned long)s->pitch, 1, (size_t)s->width, f);
    fclose(f);
    printf("sprite_test: wrote %s (%d x %d)\n", path, s->width, s->height);
}

/* Which display path to exercise. A file rather than a command-line switch, because the guest is started by an
 * AmigaDOS script and the host drives the test by dropping files into the shared folder - the same trick the GTA
 * port uses for its backend override. */
static int wanted_backend(void)
{
    FILE *f = fopen("PROGDIR:rtg.txt", "r");
    if (f) {
        fclose(f);
        printf("sprite_test: rtg.txt present, asking for the RTG backend\n");
        return AMIGAGFX_BACKEND_RTG;
    }
    return AMIGAGFX_BACKEND_AGA;
}

int main(void)
{
    BHSprites sprites;
    BHSurface surface;
    int backend, asked;

    printf("sprite_test: start\n");

    if (!bh_sprites_load(&sprites, "PROGDIR:data/sprites.spr")) {
        printf("sprite_test: no sprites, giving up\n");
        return 20;
    }

    asked = wanted_backend();
    if (amigagfx_open(SCREEN_W, SCREEN_H, 0, asked) != 0) {
        printf("sprite_test: amigagfx_open failed\n");
        bh_sprites_free(&sprites);
        return 20;
    }
    backend = amigagfx_backend();
    printf("sprite_test: backend %d (0=AGA 1=RTG 2=EHB 3=WB), area %dx%d, pitch %d\n", backend,
           amigagfx_game_width(), amigagfx_game_height(), amigagfx_pitch());

    amigagfx_set_palette(sprites.palette, 0, 256);

    surface.pixels = amigagfx_chunky();
    surface.pitch = amigagfx_pitch();
    surface.width = amigagfx_game_width();
    surface.height = amigagfx_game_height();

    /* The sky. It appears in no sprite - sprites are cropped to their model - so the packer reserves a palette
     * entry for it; clearing to index 0 instead left the first frame magenta, the transparency key showing through
     * everywhere. */
    bh_clear(&surface, BH_SKY_INDEX);

    /* Three rows of ground, then things standing on them - drawn far to near, which is the order the whole renderer
     * will use. The positions are hand-picked for this test; the real ones come from the projection. */
    bh_blit_at_anchor(&surface, &sprites, SPR_GRASS_0_R0, 160, 70);
    bh_blit_at_anchor(&surface, &sprites, SPR_ROAD_0_R0, 160, 120);
    bh_blit_at_anchor(&surface, &sprites, SPR_GRASS_1_R0, 160, 170);

    bh_blit_at_anchor(&surface, &sprites, SPR_TREE_1_R0, 60, 70);
    bh_blit_at_anchor(&surface, &sprites, SPR_TREE_3_R0, 250, 70);
    bh_blit_at_anchor(&surface, &sprites, SPR_TAXI_R0, 120, 120);
    bh_blit_at_anchor(&surface, &sprites, SPR_POLICE_CAR_R2, 230, 120);
    bh_blit_at_anchor(&surface, &sprites, SPR_BEAVER_R0_S4, 160, 170);

    dump_frame(&surface, "PROGDIR:frame.raw");

    amigagfx_blit(0, 0, SCREEN_W, SCREEN_H);
    printf("sprite_test: frame pushed through c2p, holding for a few seconds\n");
    Delay(250); /* ~5 s, long enough for a screenshot from the host */

    amigagfx_close();
    bh_sprites_free(&sprites);
    printf("sprite_test: done\n");
    return 0;
}
