/* PROGDIR:bobrhopper.prefs - read and written by the game AND by BobrHopperPrefs, through this one file.
 *
 * WHY ONE PARSER. Both programs had their own, and both read the file as a stream of word PAIRS
 * (fscanf "%s %s"). The editor writes a comment on the first line - eleven words - so every pair after it was
 * shifted by one: "gfx" was read as a value and "rtg" as a key, the setting was never found, and both programs
 * fell back to AGA. The user chose RTG, saved, and got AGA every time. Lines, not words; '#' starts a comment.
 *
 * Plain C, stdio only, no Amiga headers: the game's C++ includes prefs_bh.h.
 */
#include "prefs_bh.h"

#include <stdio.h>
#include <string.h>

static int lc(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

int bh_prefs_word_eq(const char *a, const char *b)
{
    while (*a && *b) {
        if (lc((unsigned char)*a) != lc((unsigned char)*b)) return 0;
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

void bh_prefs_defaults(BHPrefs *p)
{
    p->rtg = 0;
    p->bar = 1;
    p->hires = 0;
}

/* Copies the next blank-separated word of *s into out (at most cap-1 characters) and advances *s. */
static int next_word(const char **s, char *out, int cap)
{
    const char *p = *s;
    int n = 0;
    while (*p == ' ' || *p == '\t') p++;
    while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
        if (n < cap - 1) out[n++] = *p;
        p++;
    }
    out[n] = 0;
    *s = p;
    return n > 0;
}

int bh_prefs_load(BHPrefs *p)
{
    char line[160], key[32], word[32];
    FILE *f;
    bh_prefs_defaults(p);
    f = fopen(BH_PREFS_PATH, "r");
    if (!f) return 0;
    while (fgets(line, sizeof line, f)) {
        const char *s = line;
        char *hash = strchr(line, '#');
        if (hash) *hash = 0;
        if (!next_word(&s, key, sizeof key) || !next_word(&s, word, sizeof word)) continue;
        if (bh_prefs_word_eq(key, "gfx")) p->rtg = bh_prefs_word_eq(word, "rtg");
        else if (bh_prefs_word_eq(key, "bar")) p->bar = !bh_prefs_word_eq(word, "off");
        else if (bh_prefs_word_eq(key, "screen")) p->hires = bh_prefs_word_eq(word, "640x480");
    }
    fclose(f);
    return 1;
}

int bh_prefs_save(const BHPrefs *p)
{
    FILE *f = fopen(BH_PREFS_PATH, "w");
    if (!f) return 0;
    fprintf(f, "# Bobr Hopper settings - edit with BobrHopperPrefs or by hand\n");
    fprintf(f, "gfx %s\n", p->rtg ? "rtg" : "aga");
    fprintf(f, "screen %s\n", (p->hires && p->rtg) ? "640x480" : "320x240");
    fprintf(f, "bar %s\n", p->bar ? "on" : "off");
    fclose(f);
    return 1;
}
