/* BobrHopperPrefs - the settings the game needs BEFORE it opens a screen.
 *
 *     BobrHopperPrefs                  open the window (on the Workbench screen)
 *     BobrHopperPrefs SHOW             print the settings and exit
 *     BobrHopperPrefs GFX=RTG BAR=OFF  set those and save, no window
 *
 * Modelled on the sibling GTA port's gtaprefs, for the same reasons: what it edits decides whether the game can
 * open a display at all, so it cannot live in a menu drawn BY that display; it is a GadTools window on the
 * Workbench screen, measured off the screen's font; and every gadget has a key, because a settings editor that
 * needs a working pointer is useless on exactly the machine that needs it.
 *
 * The file is PROGDIR:bobrhopper.prefs, one "key word" per line - words, not numbers, so it can be fixed with a
 * text editor:
 *     gfx    aga | rtg
 *     screen 320x240
 *     bar    on | off
 *
 * RESOLUTION: 320x240 on both, 640x480 on RTG only (its sprites are a second set baked at twice the size, and the
 * game loads only the set the chosen size needs). With AGA chosen the gadget is locked at 320x240.
 */
#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <libraries/gadtools.h>

#include <proto/exec.h>
#include <proto/gadtools.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

#include <stdio.h>
#include <string.h>

#include "prefs_bh.h" /* the ONE parser, shared with the game - see src/amiga/prefs_bh.c */

/* Opened BY HAND, as the GTA port's gtaprefs does. libnix auto-opens intuition and graphics, but leaving gadtools to
 * it produced a program that linked, ran, and never showed its window - the first GadTools call went through a
 * null base. */
struct Library *GadToolsBase = NULL;

enum { GID_GFX = 1, GID_SCREEN, GID_BAR, GID_SAVE, GID_CANCEL };

typedef BHPrefs Prefs;

static STRPTR kGfxLabels[] = {(STRPTR) "AGA (chipset)", (STRPTR) "RTG (graphics card)", NULL};
static STRPTR kScreenLabels[] = {(STRPTR) "320x240", (STRPTR) "640x480 (RTG only)", NULL};
static STRPTR kBarLabels[] = {(STRPTR) "Off", (STRPTR) "On", NULL};

static void prefs_load(Prefs *p) { bh_prefs_load(p); }
static int prefs_save(const Prefs *p) { return bh_prefs_save(p); }
#define word_eq bh_prefs_word_eq

static void prefs_show(const Prefs *p)
{
    printf("gfx    %s\nscreen %s\nbar    %s\n", (p->rtg ? "rtg" : "aga"), (p->rtg && p->hires) ? "640x480" : "320x240",
           p->bar ? "on" : "off");
}

static int text_w(struct Screen *scr, const char *s) { return (int)TextLength(&scr->RastPort, (STRPTR)s, strlen(s)); }

/* 1 = save, 0 = cancelled, -1 = the window would not open */
static int edit(Prefs *p)
{
    struct Screen *scr;
    APTR vi;
    struct Gadget *glist = NULL, *gad, *g_gfx, *g_bar, *g_screen;
    struct Window *win;
    struct NewGadget ng;
    static const char kKeys[] = "Keys: G graphics, R resolution, B bar, S save, Esc cancel";
    int cw, fh, gh, lm, gap, labw, gadw, innerw, innerh, leftb, topb, y, btnw, result = -1, done = 0;

    scr = LockPubScreen(NULL);
    if (!scr) return -1;
    vi = GetVisualInfo(scr, TAG_END);
    if (!vi) {
        UnlockPubScreen(NULL, scr);
        return -1;
    }

    cw = scr->RastPort.TxWidth;
    if (cw < 6) cw = 6;
    fh = scr->RastPort.TxHeight;
    if (fh < 8) fh = 8;
    gh = fh + 6;
    lm = cw * 2;
    gap = fh / 2;
    if (gap < 4) gap = 4;
    labw = text_w(scr, "Resolution:") + cw;
    gadw = text_w(scr, "640x480 (RTG only)") + cw * 2 + 24;
    if (text_w(scr, "RTG (graphics card)") + cw * 2 + 24 > gadw) gadw = text_w(scr, "RTG (graphics card)") + cw * 2 + 24; /* the longest label plus the cycle arrow box */
    innerw = lm + labw + gadw + lm;
    if (lm + text_w(scr, kKeys) + lm > innerw) innerw = lm + text_w(scr, kKeys) + lm;
    leftb = scr->WBorLeft;
    topb = scr->WBorTop + scr->Font->ta_YSize + 1;

    if (!CreateContext(&glist)) {
        FreeVisualInfo(vi);
        UnlockPubScreen(NULL, scr);
        return -1;
    }
    memset(&ng, 0, sizeof ng);
    ng.ng_TextAttr = scr->Font;
    ng.ng_VisualInfo = vi;
    ng.ng_Flags = PLACETEXT_LEFT;
    ng.ng_LeftEdge = leftb + lm + labw;
    ng.ng_Width = gadw;
    ng.ng_Height = gh;

    y = gap;
    ng.ng_TopEdge = topb + y;
    ng.ng_GadgetText = (STRPTR) "_Graphics:";
    ng.ng_GadgetID = GID_GFX;
    gad = g_gfx = CreateGadget(CYCLE_KIND, glist, &ng, GTCY_Labels, (ULONG)kGfxLabels, GTCY_Active, (ULONG)p->rtg,
                               GT_Underscore, (ULONG)'_', TAG_END);
    y += gh + gap;

    ng.ng_TopEdge = topb + y;
    ng.ng_GadgetText = (STRPTR) "_Resolution:";
    ng.ng_GadgetID = GID_SCREEN;
    /* 640x480 exists for RTG only (its sprite set is baked for it); on AGA the gadget is locked at 320x240 */
    gad = g_screen = CreateGadget(CYCLE_KIND, gad, &ng, GTCY_Labels, (ULONG)kScreenLabels, GTCY_Active,
                                  (ULONG)(p->rtg ? p->hires : 0), GA_Disabled, (ULONG)(p->rtg ? FALSE : TRUE),
                                  GT_Underscore, (ULONG)'_', TAG_END);
    y += gh + gap;

    ng.ng_TopEdge = topb + y;
    ng.ng_GadgetText = (STRPTR) "Screen _bar:";
    ng.ng_GadgetID = GID_BAR;
    gad = g_bar = CreateGadget(CYCLE_KIND, gad, &ng, GTCY_Labels, (ULONG)kBarLabels, GTCY_Active, (ULONG)p->bar,
                               GT_Underscore, (ULONG)'_', TAG_END);
    y += gh + gap;

    ng.ng_Flags = PLACETEXT_IN;
    ng.ng_LeftEdge = leftb + lm;
    ng.ng_TopEdge = topb + y;
    ng.ng_Width = innerw - lm * 2;
    ng.ng_Height = fh + 2;
    ng.ng_GadgetText = NULL;
    ng.ng_GadgetID = 0;
    gad = CreateGadget(TEXT_KIND, gad, &ng, GTTX_Text, (ULONG)kKeys, TAG_END);
    y += fh + 2 + gap;

    btnw = text_w(scr, "Cancel") + cw * 4;
    ng.ng_Height = gh;
    ng.ng_Width = btnw;
    ng.ng_TopEdge = topb + y;
    ng.ng_LeftEdge = leftb + lm;
    ng.ng_GadgetText = (STRPTR) "_Save";
    ng.ng_GadgetID = GID_SAVE;
    gad = CreateGadget(BUTTON_KIND, gad, &ng, GT_Underscore, (ULONG)'_', TAG_END);
    ng.ng_LeftEdge = leftb + innerw - lm - btnw;
    ng.ng_GadgetText = (STRPTR) "Cancel";
    ng.ng_GadgetID = GID_CANCEL;
    gad = CreateGadget(BUTTON_KIND, gad, &ng, TAG_END);
    y += gh + gap;
    innerh = y;

    if (!gad) { /* any CreateGadget failing leaves NULL from there on */
        FreeGadgets(glist);
        FreeVisualInfo(vi);
        UnlockPubScreen(NULL, scr);
        return -1;
    }

    win = OpenWindowTags(NULL, WA_Title, (ULONG) "Bobr Hopper Settings", WA_InnerWidth, (ULONG)innerw, WA_InnerHeight,
                         (ULONG)innerh, WA_Left, (ULONG)(scr->Width > innerw ? (scr->Width - innerw) / 2 : 0), WA_Top,
                         (ULONG)(scr->Height > innerh ? (scr->Height - innerh) / 3 : 0), WA_DragBar, TRUE,
                         WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE, WA_SmartRefresh, TRUE,
                         WA_PubScreen, (ULONG)scr, WA_Gadgets, (ULONG)glist, WA_IDCMP,
                         IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY, TAG_END);
    if (!win) {
        FreeGadgets(glist);
        FreeVisualInfo(vi);
        UnlockPubScreen(NULL, scr);
        return -1;
    }
    GT_RefreshWindow(win, NULL);

    while (!done) {
        struct IntuiMessage *msg;
        WaitPort(win->UserPort);
        while ((msg = GT_GetIMsg(win->UserPort)) != NULL) {
            const ULONG cls = msg->Class;
            const UWORD code = msg->Code;
            struct Gadget *src = (struct Gadget *)msg->IAddress;
            GT_ReplyIMsg(msg);
            switch (cls) {
            case IDCMP_CLOSEWINDOW: result = 0; done = 1; break;
            case IDCMP_REFRESHWINDOW:
                GT_BeginRefresh(win);
                GT_EndRefresh(win, TRUE);
                break;
            case IDCMP_GADGETUP:
                switch (src->GadgetID) {
                case GID_GFX:
                    p->rtg = (int)code;
                    /* AGA has one resolution: lock the gadget there, and unlock it again for RTG */
                    GT_SetGadgetAttrs(g_screen, win, NULL, GA_Disabled, (ULONG)(p->rtg ? FALSE : TRUE), GTCY_Active,
                                      (ULONG)(p->rtg ? p->hires : 0), TAG_END);
                    break;
                case GID_SCREEN: p->hires = (int)code; break;
                case GID_BAR: p->bar = (int)code; break;
                case GID_SAVE: result = 1; done = 1; break;
                case GID_CANCEL: result = 0; done = 1; break;
                default: break;
                }
                break;
            case IDCMP_VANILLAKEY:
                switch (code) {
                case 'g': case 'G':
                    p->rtg = 1 - p->rtg;
                    GT_SetGadgetAttrs(g_gfx, win, NULL, GTCY_Active, (ULONG)p->rtg, TAG_END);
                    GT_SetGadgetAttrs(g_screen, win, NULL, GA_Disabled, (ULONG)(p->rtg ? FALSE : TRUE), GTCY_Active,
                                      (ULONG)(p->rtg ? p->hires : 0), TAG_END);
                    break;
                case 'r': case 'R':
                    if (p->rtg) {
                        p->hires = 1 - p->hires;
                        GT_SetGadgetAttrs(g_screen, win, NULL, GTCY_Active, (ULONG)p->hires, TAG_END);
                    }
                    break;
                case 'b': case 'B':
                    p->bar = 1 - p->bar;
                    GT_SetGadgetAttrs(g_bar, win, NULL, GTCY_Active, (ULONG)p->bar, TAG_END);
                    break;
                case 's': case 'S': case 13: result = 1; done = 1; break;
                case 27: result = 0; done = 1; break;
                default: break;
                }
                break;
            default: break;
            }
        }
    }

    CloseWindow(win);
    FreeGadgets(glist);
    FreeVisualInfo(vi);
    UnlockPubScreen(NULL, scr);
    return result;
}

int main(int argc, char **argv)
{
    Prefs p;
    int i, changed = 0;
    prefs_load(&p);

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (word_eq(a, "SHOW")) {
            prefs_show(&p);
            return 0;
        } else if (word_eq(a, "?")) {
            printf("BobrHopperPrefs [SHOW] [GFX=AGA|RTG] [SCREEN=320x240|640x480] [BAR=ON|OFF]\n");
            return 0;
        } else if (word_eq(a, "GFX=AGA")) { p.rtg = 0; changed = 1; }
        else if (word_eq(a, "GFX=RTG")) { p.rtg = 1; changed = 1; }
        else if (word_eq(a, "BAR=ON")) { p.bar = 1; changed = 1; }
        else if (word_eq(a, "SCREEN=320X240")) { p.hires = 0; changed = 1; }
        else if (word_eq(a, "SCREEN=640X480")) { p.hires = 1; changed = 1; }
        else if (word_eq(a, "BAR=OFF")) { p.bar = 0; changed = 1; }
        else {
            printf("BobrHopperPrefs: unknown argument %s (try ?)\n", a);
            return 10;
        }
    }
    if (changed) {
        if (!prefs_save(&p)) {
            printf("BobrHopperPrefs: cannot write %s\n", BH_PREFS_PATH);
            return 10;
        }
        prefs_show(&p);
        return 0;
    }

    GadToolsBase = OpenLibrary((CONST_STRPTR) "gadtools.library", 37L);
    if (GadToolsBase == NULL) {
        printf("BobrHopperPrefs: no gadtools.library v37 - use the command line: BobrHopperPrefs GFX=RTG BAR=OFF\n");
        return 10;
    }
    i = edit(&p);
    CloseLibrary(GadToolsBase);
    GadToolsBase = NULL;
    if (i < 0) {
        printf("BobrHopperPrefs: cannot open a window - use the command line: BobrHopperPrefs GFX=RTG BAR=OFF\n");
        return 10;
    }
    if (i == 1 && !prefs_save(&p)) {
        printf("BobrHopperPrefs: cannot write %s\n", BH_PREFS_PATH);
        return 10;
    }
    return 0;
}
