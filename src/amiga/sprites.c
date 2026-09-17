/* Loading the sprite container. See sprites.h for the format and why it is big-endian.
 *
 * Rules this file obeys, each inherited from a sibling 68k port where breaking it cost real time:
 *   - fast RAM, not chip: only bitplanes and Paula's sample buffers have to be chip, and chip is the scarce,
 *     slow memory (the GTA port measured chip-vs-fast at 40x for the same work).
 *   - check AvailMem(MEMF_LARGEST) before a big allocation, not just the total: a machine with 16.3 MB free but a
 *     largest block one kilobyte short of 8 MB failed an 8 MB malloc and took the game down with it.
 *   - stdio (fopen/fread), never C++ streams: libstdc++'s close() never returns on this target.
 *   - snprintf/printf, never sprintf: sprintf produces nonsense on this libc and lies inside your own diagnostics.
 */
#include "sprites.h"

#include <exec/memory.h>
#include <exec/types.h>
#include <proto/exec.h>
#include <stdio.h>

#define BH_HEADER_BYTES 12
#define BH_PALETTE_BYTES 768
#define BH_TABLE_OFFSET (BH_HEADER_BYTES + BH_PALETTE_BYTES)

static unsigned short be16(const unsigned char *p) { return (unsigned short)((p[0] << 8) | p[1]); }

int bh_sprites_load(BHSprites *s, const char *path)
{
    FILE *f;
    long size;
    unsigned long need;

    s->data = 0;
    s->count = 0;
    s->spans = 0;
    s->spanStart = 0;

    f = fopen(path, "rb");
    if (!f) {
        printf("sprites: cannot open %s\n", path);
        return 0;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= BH_TABLE_OFFSET) {
        printf("sprites: %s is too small (%ld bytes)\n", path, size);
        fclose(f);
        return 0;
    }

    /* Total free says nothing about whether ONE block of this size exists. */
    need = (unsigned long)size;
    if (AvailMem(MEMF_ANY | MEMF_LARGEST) < need) {
        printf("sprites: need %lu bytes in one block, largest is %lu\n", need,
               (unsigned long)AvailMem(MEMF_ANY | MEMF_LARGEST));
        fclose(f);
        return 0;
    }
    s->data = (unsigned char *)AllocVec(need, MEMF_ANY);
    if (!s->data) {
        printf("sprites: AllocVec(%lu) failed\n", need);
        fclose(f);
        return 0;
    }
    if (fread(s->data, 1, (size_t)size, f) != (size_t)size) {
        printf("sprites: short read on %s\n", path);
        fclose(f);
        bh_sprites_free(s);
        return 0;
    }
    fclose(f);
    s->bytes = need;

    if (s->data[0] != 'B' || s->data[1] != 'H' || s->data[2] != 'S' || s->data[3] != 'P') {
        printf("sprites: %s is not a BHSP file\n", path);
        bh_sprites_free(s);
        return 0;
    }
    if (be16(s->data + 4) != 1) {
        printf("sprites: %s is version %d, expected 1\n", path, (int)be16(s->data + 4));
        bh_sprites_free(s);
        return 0;
    }
    s->count = (int)be16(s->data + 6);
    s->paletteEntries = (int)be16(s->data + 8);
    s->palette = s->data + BH_HEADER_BYTES;
    /* The table is 12-byte aligned and big-endian, so it is used where it lies - no parsing pass. */
    s->entries = (const BHSpriteEntry *)(s->data + BH_TABLE_OFFSET);

    if ((unsigned long)(BH_TABLE_OFFSET + s->count * 12) > s->bytes) {
        printf("sprites: %s claims %d sprites but is only %lu bytes\n", path, s->count, s->bytes);
        bh_sprites_free(s);
        return 0;
    }
    printf("sprites: %d sprites, %d palette entries, %lu bytes\n", s->count, s->paletteEntries, s->bytes);

    /* The span table - see BHSpan. One pass over every pixel, once, at load. */
    {
        unsigned long rowsTotal = 0, solidRows = 0, at = 0;
        int i;
        for (i = 0; i < s->count; i++) rowsTotal += s->entries[i].h;
        s->spans = (BHSpan *)AllocVec(rowsTotal * sizeof(BHSpan), MEMF_ANY);
        s->spanStart = (unsigned long *)AllocVec((unsigned long)s->count * sizeof(unsigned long), MEMF_ANY);
        if (!s->spans || !s->spanStart) {
            printf("sprites: no memory for %lu row spans\n", rowsTotal);
            bh_sprites_free(s);
            return 0;
        }
        for (i = 0; i < s->count; i++) {
            const int w = (int)s->entries[i].w, h = (int)s->entries[i].h;
            const unsigned char *row = s->data + s->entries[i].offset;
            int r;
            s->spanStart[i] = at;
            for (r = 0; r < h; r++, row += w) {
                int a = 0, b = w, c, holes = 0;
                while (a < w && !row[a]) a++;
                while (b > a && !row[b - 1]) b--;
                for (c = a; c < b; c++)
                    if (!row[c]) { holes = 1; break; }
                s->spans[at].first = (unsigned short)a;
                s->spans[at].last = (unsigned short)b;
                s->spans[at].solid = (unsigned short)!holes;
                if (!holes) solidRows++;
                at++;
            }
        }
        printf("sprites: %lu rows, %lu of them one unbroken run (copied, not tested)\n", rowsTotal, solidRows);
    }
    return 1;
}

void bh_sprites_free(BHSprites *s)
{
    if (s->data) FreeVec(s->data);
    if (s->spans) FreeVec(s->spans);
    if (s->spanStart) FreeVec(s->spanStart);
    s->spans = 0;
    s->spanStart = 0;
    s->data = 0;
    s->count = 0;
    s->entries = 0;
    s->palette = 0;
    s->bytes = 0;
}

const unsigned char *bh_sprite_pixels(const BHSprites *s, int id)
{
    if (id < 0 || id >= s->count) return 0;
    return s->data + s->entries[id].offset;
}
