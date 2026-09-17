/* Display settings shared by the game and BobrHopperPrefs - see prefs_bh.c. */
#ifndef BH_PREFS_H
#define BH_PREFS_H

#ifdef __cplusplus
extern "C" {
#endif

#define BH_PREFS_PATH "PROGDIR:bobrhopper.prefs"

typedef struct {
    int rtg; /* 0 AGA, 1 RTG */
    int bar; /* 1 the Intuition screen bar is shown, 0 hidden */
    int hires; /* 1 = 640x480 - RTG only; the game ignores it on AGA */
} BHPrefs;

void bh_prefs_defaults(BHPrefs *p);
int bh_prefs_load(BHPrefs *p); /* 0 when there is no file: *p then holds the defaults */
int bh_prefs_save(const BHPrefs *p);
int bh_prefs_word_eq(const char *a, const char *b); /* case-insensitive */

#ifdef __cplusplus
}
#endif

#endif
