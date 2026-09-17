/* A microsecond clock, from timer.device's EClock.
 *
 * amigagfx_millis() is built on DateStamp and ticks once every 20 ms. That is fine for pacing a 60 Hz logic loop and
 * useless for finding out where a 7 ms logic step spends its time - every stage shorter than a tick reads as zero or
 * as twenty. The EClock runs at about 709 kHz on a PAL machine and costs one library call to read.
 *
 * The value only ever ACCUMULATES differences of the low 32 bits, so it needs no 64-bit division: the counter's low
 * word wraps after ~100 minutes, and the game reads this many times a second.
 */
#include "clock_bh.h"

#include <devices/timer.h>
#include <exec/io.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/timer.h>
#include <stdio.h>

struct Device *TimerBase = 0;

static struct MsgPort *g_port = 0;
static struct timerequest *g_req = 0;
static unsigned long g_lastLo = 0, g_freq = 0;
static unsigned long long g_micros = 0;

int bh_clock_open(void)
{
    struct EClockVal ev;
    g_port = CreateMsgPort();
    if (!g_port) return 0;
    g_req = (struct timerequest *)CreateIORequest(g_port, sizeof(struct timerequest));
    if (!g_req || OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_ECLOCK, (struct IORequest *)g_req, 0) != 0) {
        printf("clock: cannot open timer.device - profiles fall back to the 20 ms clock\n");
        bh_clock_close();
        return 0;
    }
    TimerBase = g_req->tr_node.io_Device;
    g_freq = ReadEClock(&ev);
    g_lastLo = ev.ev_lo;
    printf("clock: EClock at %lu Hz\n", g_freq);
    return 1;
}

void bh_clock_close(void)
{
    if (TimerBase && g_req) CloseDevice((struct IORequest *)g_req);
    TimerBase = 0;
    if (g_req) DeleteIORequest((struct IORequest *)g_req);
    g_req = 0;
    if (g_port) DeleteMsgPort(g_port);
    g_port = 0;
}

unsigned long long bh_micros(void)
{
    struct EClockVal ev;
    unsigned long delta;
    if (!TimerBase || g_freq < 1000UL) return 0;
    ReadEClock(&ev);
    delta = ev.ev_lo - g_lastLo; /* unsigned: correct across the wrap */
    g_lastLo = ev.ev_lo;
    /* delta * 1000 stays in 32 bits for anything under ~6 seconds between two reads */
    g_micros += (unsigned long long)((delta * 1000UL) / (g_freq / 1000UL));
    return g_micros;
}
