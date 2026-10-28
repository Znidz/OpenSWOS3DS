/* n3ds.h -- the Nintendo 3DS platform glue (src/n3ds.c). Makefile.3ds only. */
#ifndef N3DS_H
#define N3DS_H

#ifdef __3DS__
#include <stdint.h>

/* Everything the player puts on the SD card, and everything the game writes. */
#define N3DS_USER_DIR "sdmc:/3ds/OpenSWOS3D"

void    N3DS_EarlyInit(void);                    /* screens, console, romfs */
void    N3DS_PrepareData(const char *assetsDir); /* first start: ADFs -> assets */
void    N3DS_VideoReady(void);                   /* the game is about to draw */
void    N3DS_Present(const uint8_t *fb, int32_t w, int32_t h, const uint8_t *rgba);
int32_t N3DS_PollInput(void);                    /* 0: the system asked us to quit */
void    N3DS_Exit(void);
#endif

#endif
