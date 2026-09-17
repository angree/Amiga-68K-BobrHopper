/* Joystick through lowlevel.library. See joy_bh.h for why this library and not gameport.device.
 *
 * ReadJoyPort returns a bitfield whose top nibble says what kind of controller answered. A joystick reports
 * JP_TYPE_JOYSTK and then the direction and fire bits below it. Anything else - a mouse, nothing plugged in,
 * a game pad this OS does not know - is ignored rather than guessed at.
 */
#include "joy_bh.h"

#include <exec/types.h>
#include <libraries/lowlevel.h>
#include <proto/exec.h>
#include <proto/lowlevel.h>
#include <stdio.h>

static struct Library *g_lowlevel;
static int g_prevDir; /* 0 none, 1 up, 2 down, 3 left, 4 right - for the release edge */
static int g_prevFire;

int bh_joy_open(void)
{
    g_prevDir = 0;
    g_prevFire = 0;
    g_lowlevel = OpenLibrary("lowlevel.library", 37);
    if (!g_lowlevel) {
        printf("joystick: no lowlevel.library - keyboard only\n");
        return 0;
    }
    printf("joystick: lowlevel.library open, reading the game port\n");
    return 1;
}

void bh_joy_close(void)
{
    if (g_lowlevel) {
        CloseLibrary(g_lowlevel);
        g_lowlevel = 0;
    }
}

void bh_joy_poll(BHJoyEdges *out)
{
    ULONG state;

    out->up = out->down = out->left = out->right = 0;
    out->release = 0;
    out->fire = 0;
    out->lastDir = 0;
    if (!g_lowlevel) return;

    /* Port 1 is where a joystick lives on an Amiga; port 0 is the mouse. */
    state = ReadJoyPort(1);
    bh_joy_decode((unsigned long)state, out);
}

/* What the port ACTUALLY reports, once, so a machine with nothing plugged in can be told apart from a machine
 * whose joystick this code fails to read. Without this the log said only "lowlevel.library open", which is true
 * of an empty port too. */
void bh_joy_report(void)
{
    ULONG state;
    if (!g_lowlevel) return;
    state = ReadJoyPort(1);
    /* The names are the header's, not a guess: libraries/lowlevel.h gives NOTAVAIL 0, GAMECTLR 1<<28,
     * MOUSE 2<<28, JOYSTK 3<<28, UNKNOWN 4<<28, mask 15<<28. This line first called $40000000 "nothing
     * attached" - it is UNKNOWN, and the two mean opposite things: one is an empty socket, the other is a
     * device the OS cannot identify. That mislabelling sent me looking for a bug in the type check that
     * was not there. */
    printf("joystick: port 1 raw $%08lx, type $%08lx (%s)\n", (unsigned long)state,
           (unsigned long)(state & JP_TYPE_MASK),
           (state & JP_TYPE_MASK) == JP_TYPE_JOYSTK     ? "a joystick"
           : (state & JP_TYPE_MASK) == JP_TYPE_GAMECTLR ? "a game controller (CD32 pad) - also read"
           : (state & JP_TYPE_MASK) == JP_TYPE_MOUSE    ? "a mouse - deliberately ignored"
           : (state & JP_TYPE_MASK) == JP_TYPE_UNKNOWN  ? "an UNKNOWN device - an idle stick can read like this"
                                                        : "nothing attached (empty port)");
    fflush(stdout);
}

/* WHAT IS HELD RIGHT NOW, as plain bits (BH_JOY_*). The edge decoder below answers "what just happened" and was
 * enough while the stick only hopped; the shared menus want a held button mask, exactly as the keyboard gives
 * them, and the platform turns changes of that mask into presses and releases for both devices the same way.
 * The same filters apply: a mouse and an impossible (floating) port read as nothing. */
unsigned bh_joy_held(void)
{
    ULONG state;
    unsigned type, held = 0;
    if (!g_lowlevel) return 0;
    state = ReadJoyPort(1);
    type = (unsigned)(state & JP_TYPE_MASK);
    if (type != JP_TYPE_JOYSTK && type != JP_TYPE_GAMECTLR && type != JP_TYPE_UNKNOWN) return 0;
    if (((state & JPF_JOY_UP) && (state & JPF_JOY_DOWN)) || ((state & JPF_JOY_LEFT) && (state & JPF_JOY_RIGHT)))
        return 0;
    if (state & JPF_JOY_UP) held |= BH_JOY_UP;
    else if (state & JPF_JOY_DOWN) held |= BH_JOY_DOWN;
    else if (state & JPF_JOY_LEFT) held |= BH_JOY_LEFT;
    else if (state & JPF_JOY_RIGHT) held |= BH_JOY_RIGHT;
    if (state & JPF_BUTTON_RED) held |= BH_JOY_FIRE;
    if (state & JPF_BUTTON_BLUE) held |= BH_JOY_FIRE2;
    /* the rest exist on a CD32 pad only, and only when the port says it is one */
    if (type == JP_TYPE_GAMECTLR) {
        if (state & JPF_BUTTON_PLAY) held |= BH_JOY_PLAY;
        if (state & JPF_BUTTON_GREEN) held |= BH_JOY_GREEN;
        if (state & JPF_BUTTON_YELLOW) held |= BH_JOY_YELLOW;
    }
    return held;
}

void bh_joy_decode(unsigned long state, BHJoyEdges *out)
{
    int dir = 0, fire = 0;

    out->up = out->down = out->left = out->right = 0;
    out->release = 0;
    out->fire = 0;
    out->lastDir = 0;
    /* A joystick OR a CD32 game controller: libraries/lowlevel.h says in so many words that the direction bits
     * are "valid for JP_TYPE_GAMECTLR and JP_TYPE_JOYSTK", and a CD32 pad is the commonest thing plugged into
     * these machines after a plain stick.
     *
     * A MOUSE is refused on purpose even though it sets bits in the same places: those bits are movement deltas,
     * and reading them as directions would hop the hero at random whenever the player moved the mouse. UNKNOWN is
     * refused for the same reason - a device the OS cannot name is a device whose bits mean nothing certain. */
    {
        const unsigned long type = state & JP_TYPE_MASK;
        /* UNKNOWN is accepted too, and that is a decision with a reason.
         *
         * Measured on the emulated machine: a port with a joystick configured on it reports JP_TYPE_UNKNOWN
         * while the stick sits at centre - electrically an idle digital stick is not distinguishable from a
         * device the OS cannot name. Refusing UNKNOWN therefore risks a game that silently ignores a real
         * player's stick forever, with a log that looks perfectly healthy. That is the worse failure.
         *
         * MOUSE stays refused: its bits are movement deltas, and reading them as directions would hop the hero
         * whenever the mouse moved. NOTAVAIL (an empty socket) stays refused as well. */
        if (type != JP_TYPE_JOYSTK && type != JP_TYPE_GAMECTLR && type != JP_TYPE_UNKNOWN) return;
    }
    /* ...but an unnamed port can also be an OPEN one, floating, and a floating port shows impossible
     * combinations. A stick cannot be pushed up and down at once, so when it reads that way the port is noise
     * and nothing here is real. */
    if (((state & JPF_JOY_UP) && (state & JPF_JOY_DOWN)) || ((state & JPF_JOY_LEFT) && (state & JPF_JOY_RIGHT)))
        return;

    /* One direction at a time, and the vertical axis wins a diagonal: the game is a grid of hops, so a
     * diagonal has to resolve to one of them rather than to nothing. */
    if (state & JPF_JOY_UP) dir = 1;
    else if (state & JPF_JOY_DOWN) dir = 2;
    else if (state & JPF_JOY_LEFT) dir = 3;
    else if (state & JPF_JOY_RIGHT) dir = 4;
    fire = (state & (JPF_BUTTON_RED | JPF_BUTTON_BLUE)) ? 1 : 0;

    if (dir && dir != g_prevDir) {
        switch (dir) {
        case 1: out->up = 1; break;
        case 2: out->down = 1; break;
        case 3: out->left = 1; break;
        case 4: out->right = 1; break;
        default: break;
        }
    }
    /* The hop is committed when the stick comes back, exactly as the keyboard commits it on key up. */
    if (!dir && g_prevDir) {
        out->release = 1;
        out->lastDir = g_prevDir;
    }
    if (fire && !g_prevFire) out->fire = 1;

    g_prevDir = dir;
    g_prevFire = fire;
}

void bh_joy_selftest(void)
{
    /* The sequence a player's hand makes: push up, hold it, let go, then press fire. What must come out is one
     * `up` edge on the push (not on the hold), one `release` carrying lastDir=1 when the stick centres, and one
     * `fire` edge on the button - the same contract the keyboard obeys. */
    static const struct {
        const char *what;
        ULONG state;
    } steps[] = {
        {"centre        ", JP_TYPE_JOYSTK},
        {"push up       ", JP_TYPE_JOYSTK | JPF_JOY_UP},
        {"still up      ", JP_TYPE_JOYSTK | JPF_JOY_UP},
        {"back to centre", JP_TYPE_JOYSTK},
        {"press fire    ", JP_TYPE_JOYSTK | JPF_BUTTON_RED},
        {"pad left      ", JP_TYPE_GAMECTLR | JPF_JOY_LEFT}, /* a CD32 pad carries the same direction bits */
        {"a mouse       ", JP_TYPE_MOUSE | JPF_JOY_UP},   /* must be ignored entirely: those are deltas */
        {"unnamed, up   ", JP_TYPE_UNKNOWN | JPF_JOY_UP}, /* accepted: an idle real stick reads as UNKNOWN */
        {"floating port ", JP_TYPE_UNKNOWN | JPF_JOY_UP | JPF_JOY_DOWN}, /* impossible - must be ignored */
    };
    const int savedDir = g_prevDir, savedFire = g_prevFire;
    unsigned int i;

    g_prevDir = 0;
    g_prevFire = 0;
    for (i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
        BHJoyEdges e;
        bh_joy_decode((unsigned long)steps[i].state, &e);
        printf("joystick selftest: %s -> up %d down %d left %d right %d release %d lastDir %d fire %d\n",
               steps[i].what, e.up, e.down, e.left, e.right, e.release, e.lastDir, e.fire);
    }
    fflush(stdout);
    g_prevDir = savedDir;
    g_prevFire = savedFire;
}
