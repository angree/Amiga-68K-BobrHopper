/* Microsecond clock for profiling - see clock_bh.c. Plain C, no Amiga headers: the game's C++ includes this. */
#ifndef BH_CLOCK_H
#define BH_CLOCK_H

#ifdef __cplusplus
extern "C" {
#endif

int bh_clock_open(void);  /* 0 when timer.device will not open; bh_micros() then returns 0 */
void bh_clock_close(void);
unsigned long long bh_micros(void);

#ifdef __cplusplus
}
#endif

#endif
