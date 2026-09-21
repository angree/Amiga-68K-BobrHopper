/* Display settings shared by the game and BobrHopperPrefs - see prefs_bh.c. */
#ifndef BH_PREFS_H
#define BH_PREFS_H

#ifdef __cplusplus
extern "C" {
#endif

#define BH_PREFS_PATH "PROGDIR:bobrhopper.prefs"

/* Which screen the game opens. The cycle gadget's order, so BH_GFX_* IS the gadget index. */
#define BH_GFX_AGA 0
#define BH_GFX_RTG 1
#define BH_GFX_OCS 2 /* Extra Half-Brite: six bitplanes, 320x240 only - what a machine without AGA can show */

typedef struct {
    int gfx; /* BH_GFX_AGA | BH_GFX_RTG | BH_GFX_OCS  (was a plain rtg flag until OCS made it three ways) */
    int bar; /* 1 the Intuition screen bar is shown, 0 hidden */
    int hires; /* 1 = 640x480 - RTG only; the game ignores it on AGA and OCS */
} BHPrefs;

void bh_prefs_defaults(BHPrefs *p);
int bh_prefs_load(BHPrefs *p); /* 0 when there is no file: *p then holds the defaults */
int bh_prefs_save(const BHPrefs *p);
int bh_prefs_word_eq(const char *a, const char *b); /* case-insensitive */

#ifdef __cplusplus
}
#endif

#endif
