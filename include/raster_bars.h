/*
 * raster_bars.h -- where the Amiga's match frame spends its time, drawn on the screen.
 *
 * Build with  make -f Makefile.amiga EXTRA_CFLAGS=-DRASTER_BARS  (never in a release).
 * Each phase of the match frame sets COLOR00 as it starts, and the beam paints the
 * lines it is drawing in that colour: the border strip right of the picture becomes
 * stacked bands, one per phase, as tall as that phase took. The band that is still
 * coloured at the bottom of the screen, or a frame whose bands run past it, is a frame
 * over budget -- the game then shows every other frame and runs slow.
 *
 * Only the border: this build shifts the picture's colours to the other half of the
 * palette (BPLCON4, display.c's RB_BANK), so the grass is no longer COLOR00. The copper
 * reloads the palette at the top of every frame and leaves BPLCON3 on bank 0, so a CPU
 * write to COLOR00 lands in the border's colour for the rest of that frame.
 */
#ifndef RASTER_BARS_H
#define RASTER_BARS_H

#include <stdint.h>

#if defined(RASTER_BARS) && !defined(SWOS_SDL_BUILD)
#include "display.h"
/* Never in the first lines of a frame: the copper is loading the palette there, a bank at
 * a time, and a write between its bank selects lands in THAT bank's colour 0 -- the grass,
 * since this build swaps the halves. The load is done by line 8 (still vertical blank). */
#define RASTER(rgb12)   do { \
        while (((*(volatile uint32_t *)&_custom->vposr >> 8) & 0x1FFu) < 8u) {} \
        _custom->color[0] = (uint16_t)(rgb12); } while (0)
#elif defined(__3DS__)
/* The 3DS has no beam to paint: the same markers time the phases instead (n3ds.c, the [frame] log). */
extern void FrameProf_Phase(unsigned rgb12);
#define RASTER(rgb12)   FrameProf_Phase(rgb12)
#else
#define RASTER(rgb12)   ((void)0)
#endif

/* A side timer, for work that sits inside a phase: FPROF_T(t) starts it, FPROF_ADD(slot, t) ends it.
 * Slots: 0 the match recorder, 1 the 3D plane copy, 2 the audio pump. Nothing off the 3DS. */
#ifdef __3DS__
extern uint32_t FrameProf_Now(void);
extern void     FrameProf_Add(int slot, uint32_t t0);
#define FPROF_T(v)          uint32_t v = FrameProf_Now()
#define FPROF_ADD(slot, v)  FrameProf_Add((slot), (v))
#else
#define FPROF_T(v)          ((void)0)
#define FPROF_ADD(slot, v)  ((void)0)
#endif

/* The phases, 12-bit 0x0RGB. */
#define RASTER_UNDRAW   0x0F00  /* red: undraw last frame's bobs, panel        */
#define RASTER_SCROLL   0x0FF0  /* yellow: camera + pitch scroll (tile blits)  */
#define RASTER_SIM      0x00F0  /* green: the match -- ball, players, AI       */
#define RASTER_CROWD    0x0F0F  /* magenta: animated crowd tiles               */
#define RASTER_SPRITES  0x000F  /* blue: drawing the bobs                       */
#define RASTER_HUD      0x00FF  /* cyan: bench, stats, result                  */
#define RASTER_IDLE     0x0000  /* black: done, waiting for the vertical blank */

#endif /* RASTER_BARS_H */
