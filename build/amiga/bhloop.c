/* bhloop - the test machine's job runner (winuae/harness/bh_go.ps1). NOT part of the game or its package.
 *
 * Every WinUAE start steals the host's mouse, so the test machine is never restarted: a run is a file the host
 * drops into Work:. This program waits for Work:go, moves it to T:bhgo and runs it through System(), then waits
 * for the next one - forever.
 *
 * It replaced an AmigaDOS script loop (Lab / If EXISTS / Execute / Skip BACK), which ran the first job and then
 * never polled again, even with FailAt 1000. A program has no control flow for a nested script to disturb, and
 * System() hands back whatever the job returned without ending anything.
 *
 * Work:loop.log gets one line per job, so "is the runner alive?" has an answer on the host.
 */
#include <dos/dos.h>
#include <dos/dostags.h>
#include <exec/types.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include <stdio.h>

static int take_job(void)
{
    char buf[1024];
    LONG n;
    BPTR in, out;
    in = Open((CONST_STRPTR) "Work:go", MODE_OLDFILE);
    if (!in) return 0;
    n = Read(in, buf, sizeof buf);
    Close(in);
    if (n <= 0) return 0;
    out = Open((CONST_STRPTR) "T:bhgo", MODE_NEWFILE);
    if (!out) return 0;
    Write(out, buf, n);
    Close(out);
    DeleteFile((CONST_STRPTR) "Work:go");
    return 1;
}

static void note(const char *what, long value)
{
    FILE *f = fopen("Work:loop.log", "a");
    if (!f) return;
    fprintf(f, "%s %ld\n", what, value);
    fclose(f);
}

int main(void)
{
    long jobs = 0;
    note("bhloop: started", 0);
    for (;;) {
        if (take_job()) {
            BPTR nilIn = Open((CONST_STRPTR) "NIL:", MODE_OLDFILE);
            BPTR nilOut = Open((CONST_STRPTR) "NIL:", MODE_NEWFILE);
            LONG rc;
            jobs++;
            note("bhloop: job", jobs);
            rc = SystemTags((CONST_STRPTR) "Execute T:bhgo", SYS_Input, (ULONG)nilIn, SYS_Output, (ULONG)nilOut,
                            NP_StackSize, 1000000UL, TAG_END);
            /* synchronous System() leaves the handles to the caller */
            if (nilIn) Close(nilIn);
            if (nilOut) Close(nilOut);
            note("bhloop: job ended, rc", (long)rc);
        }
        Delay(50); /* one second */
    }
    return 0;
}
