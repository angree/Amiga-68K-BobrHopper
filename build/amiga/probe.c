/* Ten-line-class probe, built with EXACTLY the flags the game will use.
 *
 * Its job is to answer, in one boot, the questions that otherwise get answered by
 * guessing: does the toolchain produce a Hunk binary this Kickstart will run, does
 * our shared folder reach the guest, does writing a log from PROGDIR: work, what
 * does the machine actually offer us (CPU flags, chip/fast RAM, RTG present or
 * not, screen modes) - and it does it before a single line of game code exists.
 * The OpenXcom port's lesson: a probe built like the game settles
 * "toolchain or game?" faster than any amount of reading.
 *
 * NEVER use sprintf here (broken on this libc), never std::ifstream, never strip.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <graphics/displayinfo.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <stdarg.h>
#include <stdio.h>

extern struct ExecBase *SysBase;

static FILE *lg;

static void say(const char *fmt, ...)
{
    /* Two destinations on purpose: the file survives a crash that eats stdout,
     * and stdout is what the run script redirects. */
    va_list ap;
    va_start(ap, fmt);
    if (lg) { vfprintf(lg, fmt, ap); fputc('\n', lg); fflush(lg); }
    va_end(ap);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    putchar('\n');
    fflush(stdout);
    va_end(ap);
}

int main(void)
{
    struct Library *cgx;
    ULONG chip, fast, chipLargest, fastLargest;
    UWORD attn;

    lg = fopen("PROGDIR:bhprobe.log", "w");

    say("bhprobe: alive");
    say("exec version %d, revision %d", (int)SysBase->LibNode.lib_Version,
        (int)SysBase->LibNode.lib_Revision);

    attn = SysBase->AttnFlags;
    say("AttnFlags 0x%04x: 68010=%d 68020=%d 68030=%d 68040=%d 68881=%d 68882=%d FPU40=%d",
        (unsigned)attn,
        (attn & AFF_68010) ? 1 : 0, (attn & AFF_68020) ? 1 : 0,
        (attn & AFF_68030) ? 1 : 0, (attn & AFF_68040) ? 1 : 0,
        (attn & AFF_68881) ? 1 : 0, (attn & AFF_68882) ? 1 : 0,
        (attn & AFF_FPU40) ? 1 : 0);

    chip = AvailMem(MEMF_CHIP);
    fast = AvailMem(MEMF_FAST);
    chipLargest = AvailMem(MEMF_CHIP | MEMF_LARGEST);
    fastLargest = AvailMem(MEMF_FAST | MEMF_LARGEST);
    say("chip free %lu (largest block %lu)", (unsigned long)chip, (unsigned long)chipLargest);
    say("fast free %lu (largest block %lu)", (unsigned long)fast, (unsigned long)fastLargest);

    /* Sprites and the chunky buffer go in FAST ram; only the bitplanes the c2p
     * writes into and the Paula sample buffers have to be CHIP. If fast is 0 we
     * are on a machine where that split does not exist and everything competes
     * for chip bandwidth - worth knowing before measuring anything. */
    say("fast ram present: %s", fast ? "yes" : "NO (chip only machine)");

    cgx = OpenLibrary("cybergraphics.library", 0);
    say("cybergraphics.library: %s", cgx ? "present (RTG path available)" : "absent (AGA only)");
    if (cgx) CloseLibrary(cgx);

    if (GfxBase) {
        say("graphics.library version %d", (int)GfxBase->LibNode.lib_Version);
        /* Can we get the plain lores PAL mode our 320x240 screen wants? */
        say("ModeNotAvailable(PAL_MONITOR_ID|LORES_KEY) = %ld",
            (long)ModeNotAvailable(PAL_MONITOR_ID | LORES_KEY));
        say("ModeNotAvailable(NTSC_MONITOR_ID|LORES_KEY) = %ld",
            (long)ModeNotAvailable(NTSC_MONITOR_ID | LORES_KEY));
    } else {
        say("GfxBase is NULL - graphics.library did not open");
    }

    say("bhprobe: done");
    if (lg) fclose(lg);
    return 0;
}
