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
void    N3DS_Loading(const char *what, int32_t percent);   /* start-up progress screen */
/* A tap on the menu (bottom screen), in the menu's own coordinates. 1 when there was one. */
int32_t N3DS_TakeMenuTap(int32_t *x, int32_t *y);

/* src/n3ds_screens.c */
enum { N3DS_BOTTOM_INFO = 0, N3DS_BOTTOM_MENU = 1, N3DS_BOTTOM_MATCH = 2 };
void    N3DSScr_Init(void);
void    N3DSScr_TitleTop(uint16_t *left, uint16_t *right, int32_t strength);
void    N3DSScr_BottomMenu(const uint8_t *fb, int32_t w, const uint16_t *pal, int32_t mainMenu);
/* the SEASON button on the main menu, and the helpers the season list draws with */
#define N3DS_TAB_X0 248
#define N3DS_TAB_Y0 220
#define N3DS_TAB_X1 318
#define N3DS_TAB_Y1 238
void      N3DSScr_SeasonTab(void);
uint16_t *N3DSScr_BottomFb(void);
void      N3DSScr_Fill(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c);
void      N3DSScr_Text(int32_t x, int32_t y, const char *t, int32_t scale, uint16_t c);
int32_t   N3DSScr_TextWidth(const char *t, int32_t scale);
void    N3DSScr_FlushBottom(void);       /* copy the touch screen's drawing to the display */
void    N3DSScr_InvalidateMenu(void);   /* redraw the menu mirror next frame */
void    N3DSScr_BottomMatch(const uint16_t *pal);
void    N3DSScr_BottomInfo(void);
int32_t N3DSScr_BottomMode(void);
void    N3DSScr_TouchToMenu(int32_t tx, int32_t ty, int32_t *mx, int32_t *my);
#endif

void N3DS_QuitWatchdog(void);
#endif
