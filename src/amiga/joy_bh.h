/* Joystick in the game port (task G8).
 *
 * The platform layer this port inherited has no joystick support at all - it was written for a game played
 * with a mouse - so this is ours. It reads the port through lowlevel.library, which is the cheapest correct
 * way on Kickstart 3.x: no gameport.device to open, no input handler to install, no interrupt to service, and
 * it coexists with Intuition still owning the keyboard.
 *
 * The game already takes its input as "a direction was pressed" and "the press was released, commit the hop"
 * - the same contract the keyboard uses - so this reports edges rather than levels and the game loop does not
 * care which device a hop came from.
 *
 * Plain C, no Amiga types in this header.
 */
#ifndef BH_JOY_H
#define BH_JOY_H

#ifdef __cplusplus
extern "C" {
#endif

/* Directions and fire, as edges. */
typedef struct {
    int up, down, left, right; /* 1 on the frame the direction was pushed */
    int release;               /* 1 on the frame the stick returned to centre after a direction */
    int fire;                  /* 1 on the frame fire went down: menu "A" */
    int lastDir;               /* which direction the release belongs to: 0 none, 1 up, 2 down, 3 left, 4 right */
} BHJoyEdges;

/* Opens lowlevel.library. Returns 0 if it is unavailable - the game then runs on the keyboard alone rather
 * than refusing to start, which is how every optional device in this port behaves. */
int bh_joy_open(void);
void bh_joy_close(void);

/* Read once per frame. Fills `out` with edges since the previous call. Safe (and silent) when not open. */
void bh_joy_poll(BHJoyEdges *out);

#define BH_JOY_UP 0x01
#define BH_JOY_DOWN 0x02
#define BH_JOY_LEFT 0x04
#define BH_JOY_RIGHT 0x08
#define BH_JOY_FIRE 0x10   /* red, the only button every stick has */
#define BH_JOY_FIRE2 0x20  /* blue: second button of a 2-button stick, B on a CD32 pad */
#define BH_JOY_PLAY 0x40   /* CD32 pad */
#define BH_JOY_GREEN 0x80  /* CD32 pad */
#define BH_JOY_YELLOW 0x100 /* CD32 pad */
/* What the joystick port holds right now; 0 without lowlevel.library, for a mouse, or for a floating port. */
unsigned bh_joy_held(void);

/* O23 (two players): the same, for a named port. 1 is the joystick socket (what bh_joy_held reads), 0 is the one
 * the mouse normally lives in - a second player's stick can go there, so it is readable, but ONLY when a player
 * has actually chosen it in the settings: a mouse in that socket reports movement in the direction bits and would
 * hop a hero about at random. */
unsigned bh_joy_held_port(int port);

/* The DECODING on its own, with no hardware behind it: turn one raw ReadJoyPort() bitfield into edges.
 *
 * It is split out because it is the only part of this file that can be tested without a person holding a
 * stick. bh_joy_selftest() feeds it a known sequence - push up, centre, press fire - and logs what came back,
 * which proves the contract the game relies on (a direction BEGINS a hop, the return to centre COMMITS it)
 * even on a machine with nothing plugged into the port. What it cannot prove is the wire from the real port;
 * that stays honestly unverified until someone moves a stick. */
void bh_joy_decode(unsigned long state, BHJoyEdges *out);

/* Runs that sequence and prints one line per step. Call once at startup; costs nothing afterwards. */
void bh_joy_selftest(void);

/* Log what the port reports right now - a joystick, a mouse, or nothing attached. "lowlevel.library open" is
 * equally true of an empty port, which is why that line alone never proved anything. */
void bh_joy_report(void);

#ifdef __cplusplus
}
#endif

#endif /* BH_JOY_H */
