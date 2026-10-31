/*
 * n3ds_screens.c -- what the 3DS shows besides the game's own picture:
 *
 *   top screen, in menus     a 3D title scene: stands and a pitch receding into the
 *                            screen, the OpenSWOS logo floating out of it, a ball
 *                            bouncing, and the credits scrolling along the bottom
 *   bottom screen, menus     the menu itself, to be tapped (n3ds.c turns a tap into
 *                            a selection, menu_system.c MenuTapAt)
 *   bottom screen, matches   both teams, the score, the minute, and a radar
 *   bottom screen, otherwise the controls and the credits
 *
 * Everything is drawn straight into the RGB565 framebuffers. The screens are the
 * display turned on its side: pixel (x, y) of a 240-row screen is fb[x*240 + 239-y].
 * The font is our own 5x7.
 */
#ifdef __3DS__

#include <3ds.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "n3ds.h"
#include "game_types.h"
#include "display.h"
#include "menu_system.h"

#define TOP_W  400
#define BOT_W  320
#define SCR_H  240

#define RGB(r, g, b)  ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))

static inline void put(uint16_t *fb, int32_t x, int32_t y, uint16_t c)
{
    fb[x * SCR_H + (SCR_H - 1 - y)] = c;
}

/* ---- font ------------------------------------------------------------------ */

static const char    k_glyphChars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-:'./()+&,!";
static const uint8_t k_glyphs[][7] = {
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E},
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C},
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, {0x07,0x02,0x02,0x02,0x02,0x12,0x0C},
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, {0x11,0x11,0x19,0x15,0x13,0x11,0x11},
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
    {0x11,0x11,0x11,0x15,0x15,0x15,0x0A}, {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
    {0x11,0x11,0x11,0x0A,0x04,0x04,0x04}, {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
    {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00},
    {0x0C,0x04,0x08,0x00,0x00,0x00,0x00}, {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C},
    {0x01,0x01,0x02,0x04,0x08,0x10,0x10}, {0x02,0x04,0x08,0x08,0x08,0x04,0x02},
    {0x08,0x04,0x02,0x02,0x02,0x04,0x08}, {0x00,0x04,0x04,0x1F,0x04,0x04,0x00},
    {0x0C,0x12,0x14,0x08,0x15,0x12,0x0D}, {0x00,0x00,0x00,0x00,0x0C,0x04,0x08},
    {0x04,0x04,0x04,0x04,0x04,0x00,0x04},
};

static const uint8_t *glyph(char c)
{
    const char *p;
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    p = c ? strchr(k_glyphChars, c) : NULL;
    return p ? k_glyphs[p - k_glyphChars] : NULL;
}

static int32_t text_width(const char *s, int32_t scale)
{
    return (int32_t)strlen(s) * 6 * scale;
}

/* Draw `s` at (x, y) on a screen `w` wide. `shift` moves it for one eye. */
static void draw_text(uint16_t *fb, int32_t w, int32_t x, int32_t y, const char *s,
                      int32_t scale, uint16_t col)
{
    for (; *s; s++, x += 6 * scale) {
        const uint8_t *g = glyph(*s);
        int32_t r, b, sx, sy;
        if (!g) continue;
        for (r = 0; r < 7; r++)
            for (b = 0; b < 5; b++) {
                if (!(g[r] & (0x10 >> b))) continue;
                for (sy = 0; sy < scale; sy++)
                    for (sx = 0; sx < scale; sx++) {
                        int32_t px = x + b * scale + sx, py = y + r * scale + sy;
                        if (px >= 0 && px < w && py >= 0 && py < SCR_H)
                            put(fb, px, py, col);
                    }
            }
    }
}

static void fill_rect(uint16_t *fb, int32_t w, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)
{
    int32_t x, y;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > w) x1 = w;
    if (y1 > SCR_H) y1 = SCR_H;
    for (x = x0; x < x1; x++)
        for (y = y0; y < y1; y++)
            put(fb, x, y, c);
}

/* ---- credits ---------------------------------------------------------------- */

static const char k_creditsTicker[] =
    "SENSIBLE WORLD OF SOCCER BY SENSIBLE SOFTWARE  -  CHRIS CHAPMAN, JON HARE, "
    "STOO CAMBRIDGE, RICHARD JOSEPH AND THE SENSIBLE TEAM  -  "
    "OPENSWOS BY BEN BAKER (BENBAKER76) AND THE OPENSWOS CONTRIBUTORS  -  "
    "DOS REVERSE ENGINEERING BY ZLATKO KARAKAS  -  "
    "SWOS IS (C) SENSIBLE SOFTWARE AND ITS CURRENT RIGHTS HOLDERS  -  "
    "OPENSWOS 3D IS AN UNOFFICIAL FAN PORT  -  ";

static const char *const k_creditsLines[] = {
    "SENSIBLE WORLD OF SOCCER",
    "  BY SENSIBLE SOFTWARE",
    "  (C) SENSIBLE SOFTWARE AND",
    "  ITS CURRENT RIGHTS HOLDERS",
    "",
    "OPENSWOS",
    "  BEN BAKER (BENBAKER76) AND",
    "  THE OPENSWOS CONTRIBUTORS",
    "  DOS RE: ZLATKO KARAKAS",
    "",
    "OPENSWOS 3D: UNOFFICIAL FAN PORT",
    NULL
};

/* ---- the title scene (top screen, menus) ----------------------------------- */

#define TS_MARGIN   16
#define TS_BG_W     (TOP_W + 2 * TS_MARGIN)
#define TS_HORIZON  34            /* where the pitch's lines meet */
#define TS_STANDS   78            /* the stands end, the pitch begins */
#define TS_TICKER_Y 228

static uint16_t *s_titleBg;        /* TS_BG_W x SCR_H, row-major */
static uint16_t *s_logo;           /* w*h RGB565 */
static uint8_t  *s_logoA;          /* w*h alpha */
static int32_t   s_logoW, s_logoH;
static int32_t   s_titleT;

/* Disparity of the background at a row, in 1/16 pixel (positive: nearer). */
static int32_t ground_disp16(int32_t y)
{
    if (y < TS_STANDS)
        return -160;                                    /* the stands: 10 px in */
    return -128 + 128 * (y - TS_STANDS) / (SCR_H - TS_STANDS);   /* 8 px in .. the screen */
}

static uint32_t rng(uint32_t *s)
{
    *s = *s * 1664525u + 1013904223u;
    return *s >> 16;
}

static void build_title_bg(void)
{
    uint32_t seed = 12345, cseed = 0;
    static uint32_t blockSeed[TS_BG_W / 3 + 2];
    int32_t x, y;
    s_titleBg = (uint16_t *)malloc(sizeof(uint16_t) * TS_BG_W * SCR_H);
    if (!s_titleBg) return;
    for (y = 0; y < SCR_H; y++)
        for (x = 0; x < TS_BG_W; x++) {
            uint16_t c;
            int32_t sx = x - TS_MARGIN;            /* screen x */
            if (y < 8) {
                c = RGB(30, 34, 48);                 /* the roof */
            } else if (y < TS_STANDS - 12) {
                c = RGB(52, 56, 70);                 /* the crowd */
                if ((y % 3 == 0 || y == 8) && x % 3 == 0) blockSeed[x / 3] = rng(&seed);
                cseed = blockSeed[x / 3];
                if ((cseed & 7) < 3) {
                    static const uint16_t crowd[6] = {
                        RGB(220,60,60), RGB(240,240,240), RGB(60,90,220),
                        RGB(240,210,80), RGB(200,160,130), RGB(110,110,120) };
                    c = crowd[(cseed >> 4) % 6];
                }
            } else if (y < TS_STANDS - 2) {
                /* advertising boards: plain coloured panels */
                static const uint16_t boards[4] = {
                    RGB(200,40,40), RGB(240,240,240), RGB(40,80,200), RGB(250,200,40) };
                int32_t b = ((x + 7) / 46) & 3;
                c = boards[b];
                if ((x + 7) % 46 == 0) c = RGB(20, 20, 20);
            } else if (y < TS_STANDS) {
                c = RGB(20, 20, 20);
            } else {
                /* the pitch, in perspective toward (200, TS_HORIZON) */
                int32_t dy = y - TS_HORIZON;
                int32_t v = 6000 / dy;                      /* distance along the pitch */
                int32_t stripe = (v / 9) & 1;
                int32_t halfW = dy * 19 / 10;               /* touchline at this row */
                int32_t dx = sx - TOP_W / 2;
                int32_t onPitch = dx > -halfW && dx < halfW;
                c = onPitch ? (stripe ? RGB(46, 140, 54) : RGB(56, 156, 62))
                            : (stripe ? RGB(36, 112, 44) : RGB(42, 124, 50));
                /* touchlines */
                if (dx >= -halfW - 1 && dx <= -halfW + 1) c = RGB(235, 235, 235);
                if (dx >= halfW - 1 && dx <= halfW + 1)  c = RGB(235, 235, 235);
                /* the halfway line and the centre circle */
                if (v == 6000 / (164 - TS_HORIZON) && onPitch) c = RGB(235, 235, 235);
                {
                    int32_t cy = 164, ry = 26, rx = 92;
                    int32_t ex = dx * 100 / rx, ey = (y - cy) * 100 / ry;
                    int32_t r2 = ex * ex + ey * ey;
                    if (r2 > 8700 && r2 < 10400) c = RGB(235, 235, 235);
                    if (r2 < 40) c = RGB(235, 235, 235);
                }
            }
            s_titleBg[y * TS_BG_W + x] = c;
        }
}

static void load_logo(void)
{
    FILE *f = fopen("romfs:/title_logo.bin", "rb");
    uint16_t wh[2];
    if (!f) return;
    if (fread(wh, 2, 2, f) == 2) {
        s_logoW = wh[0];
        s_logoH = wh[1];
        s_logo  = (uint16_t *)malloc((size_t)s_logoW * s_logoH * 2);
        s_logoA = (uint8_t *)malloc((size_t)s_logoW * s_logoH);
        if (!s_logo || !s_logoA ||
            fread(s_logo, 2, (size_t)s_logoW * s_logoH, f) != (size_t)s_logoW * s_logoH ||
            fread(s_logoA, 1, (size_t)s_logoW * s_logoH, f) != (size_t)s_logoW * s_logoH) {
            free(s_logo); free(s_logoA);
            s_logo = NULL; s_logoA = NULL;
        }
    }
    fclose(f);
}

static inline uint16_t blend565(uint16_t a, uint16_t b, int32_t al)   /* a over b */
{
    int32_t r = (((a >> 11) & 31) * al + ((b >> 11) & 31) * (255 - al)) / 255;
    int32_t g = (((a >> 5) & 63) * al + ((b >> 5) & 63) * (255 - al)) / 255;
    int32_t bl = ((a & 31) * al + (b & 31) * (255 - al)) / 255;
    return (uint16_t)((r << 11) | (g << 5) | bl);
}

/* Half the disparity for one eye: d16 in 1/16 px, strength 0..256. */
static inline int32_t eye_px(int32_t d16, int32_t eye, int32_t strength)
{
    int32_t v = d16 * strength;            /* 1/(16*256) px of full disparity */
    int32_t h = v >= 0 ? (v + 4096) >> 13 : -((-v + 4096) >> 13);
    return eye * h;                        /* left eye (-1) moves near things right */
}

static void title_eye(uint16_t *fb, int32_t eye, int32_t strength)
{
    int32_t x, y, t = s_titleT;
    if (!s_titleBg) build_title_bg();

    /* background, each row by its own depth */
    for (y = 0; y < SCR_H; y++) {
        int32_t sh = -eye_px(ground_disp16(y), eye, strength);
        const uint16_t *src = s_titleBg ? s_titleBg + y * TS_BG_W + TS_MARGIN + sh : NULL;
        for (x = 0; x < TOP_W; x++)
            put(fb, x, y, src ? src[x] : RGB(40, 120, 50));
    }

    /* the ball: across the pitch, bouncing, with its shadow */
    {
        int32_t period = 260;
        int32_t p = t % period;
        int32_t gx = 60 + p * 280 / period;
        int32_t gy = 196;
        float   hgt = fabsf(sinf((float)t * 0.075f)) * 46.0f;
        int32_t by = gy - (int32_t)hgt;
        int32_t gd = ground_disp16(gy);
        int32_t sdx = eye_px(gd, -eye, strength);
        int32_t bdx = eye_px(gd + (int32_t)(hgt * 2.2f), -eye, strength);
        int32_t dx, dy;
        for (dy = -2; dy <= 2; dy++)
            for (dx = -6; dx <= 6; dx++)
                if (dx * dx * 4 + dy * dy * 36 <= 144) {
                    int32_t px = gx + dx + sdx, py = gy + dy;
                    if (px >= 0 && px < TOP_W) put(fb, px, py, RGB(24, 70, 30));
                }
        for (dy = -5; dy <= 5; dy++)
            for (dx = -5; dx <= 5; dx++) {
                int32_t r2 = dx * dx + dy * dy, px = gx + dx + bdx, py = by - 5 + dy;
                uint16_t c;
                if (r2 > 25) continue;
                c = r2 > 18 ? RGB(20, 20, 20)
                  : ((((dx + 8) / 3) + ((dy + 8) / 3) + t / 6) % 4 == 0) ? RGB(30, 30, 30)
                  : RGB(250, 250, 250);
                if (px >= 0 && px < TOP_W && py >= 0) put(fb, px, py, c);
            }
    }

    /* the logo, floating out of the screen and bobbing */
    if (s_logo) {
        float   bob = sinf((float)t * 0.04f);
        int32_t d16 = 144 + (int32_t)(bob * 24.0f);      /* ~9 px out */
        int32_t lx = (TOP_W - s_logoW) / 2 + eye_px(d16, -eye, strength);
        int32_t ly = 22 + (int32_t)(bob * 4.0f);
        for (y = 0; y < s_logoH; y++) {
            int32_t py = ly + y;
            if (py < 0 || py >= SCR_H) continue;
            for (x = 0; x < s_logoW; x++) {
                int32_t px = lx + x, a = s_logoA[y * s_logoW + x];
                uint16_t *dst;
                if (a < 8 || px < 0 || px >= TOP_W) continue;
                dst = &fb[px * SCR_H + (SCR_H - 1 - py)];
                *dst = a > 247 ? s_logo[y * s_logoW + x] : blend565(s_logo[y * s_logoW + x], *dst, a);
            }
        }
    }

    /* the credits, scrolling along a strip just in front of the screen */
    {
        int32_t len = text_width(k_creditsTicker, 1);
        int32_t off = eye_px(32, -eye, strength);
        int32_t x0 = TOP_W - (t % (len + TOP_W)) + off;
        for (y = TS_TICKER_Y - 3; y < SCR_H; y++)
            for (x = 0; x < TOP_W; x++)
                put(fb, x, y, RGB(12, 14, 22));
        draw_text(fb, TOP_W, x0, TS_TICKER_Y, k_creditsTicker, 1, RGB(250, 220, 90));
    }
}

void N3DSScr_TitleTop(uint16_t *left, uint16_t *right, int32_t strength)
{
    s_titleT++;
    title_eye(left, -1, strength);
    if (right)
        title_eye(right, +1, strength);
}

/* ---- the bottom screen -------------------------------------------------------- */

static int32_t s_bottomMode = -1;
static int32_t s_bottomFrame;

static uint16_t *bottom_fb(void)
{
    return (uint16_t *)gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
}

int32_t N3DSScr_BottomMode(void)
{
    return s_bottomMode;
}

/* The menu, scaled to fit: the menu's own 336 x 272 to the screen's 320 x 240. */
static int32_t s_menuDirty = 1;

/* Where on the bottom screen the menu sits. Every menu's entries fit 320 x 240 of the 336 x 272
 * picture, so the screen shows that window pixel for pixel -- no scaling, nothing resampled.
 * A menu that does not fit falls back to the scaled picture. */
static int32_t s_winOx, s_winOy, s_winExact;
extern uint8_t  _currentMenu[];
extern MenuEntry _menuEntries[];

static void pick_menu_window(void)
{
    const Menu *menu = (const Menu *)_currentMenu;
    int32_t i, n = menu->numEntries, x0 = 9999, y0 = 9999, x1 = -1, y1 = -1;
    int32_t ox = 8, oy = 0, exact = 0;
    if (n > 0 && n < 256) {
        for (i = 0; i < n; i++) {
            const MenuEntry *e = (const MenuEntry *)((const uint8_t *)_menuEntries + i * MENUENTRY_STRIDE);
            if (e->isInvisible || !e->dwWidth || !e->dwHeight)
                continue;
            if (e->x < x0) x0 = e->x;
            if (e->y < y0) y0 = e->y;
            if (e->x + e->dwWidth > x1) x1 = e->x + e->dwWidth;
            if (e->y + e->dwHeight > y1) y1 = e->y + e->dwHeight;
        }
        if (x1 > 0 && x0 >= 0 && y0 >= 0 && x1 <= 336 && y1 <= 272) {
            /* as much of the top as the bottom entry allows */
            oy = y1 + 4 - SCR_H;
            if (oy < 0) oy = 0;
            if (oy > y0) oy = y0;
            ox = 8;
            if (x0 < ox) ox = x0;
            if (x1 > ox + BOT_W) ox = x1 - BOT_W;
            if (ox < 0) ox = 0;
            if (ox > 16) ox = 16;
            exact = (x0 >= ox && x1 <= ox + BOT_W && y0 >= oy && y1 <= oy + SCR_H);
        }
    }
    if (ox != s_winOx || oy != s_winOy || exact != s_winExact)
        s_menuDirty = 1;
    s_winOx = ox; s_winOy = oy; s_winExact = exact;
}
void N3DSScr_InvalidateMenu(void) { s_menuDirty = 1; }

/* Two packed RGB565 pixels blended, weight 0..32 of b. */
static inline uint32_t spread565(uint16_t c) { return (c | ((uint32_t)c << 16)) & 0x07E0F81Fu; }
static inline uint32_t mix565(uint32_t a, uint32_t b, uint32_t w)
{
    return ((a * (32 - w) + b * w) >> 5) & 0x07E0F81Fu;
}

/* The menu is 336x272 and the screen 320x240: a non-whole scale, so picking the nearest
 * pixel drops rows and columns unevenly and the text breaks up. Blend the four around each
 * point instead, and only when the picture or palette changed (a menu is mostly still,
 * and rewriting the single-buffered screen every frame tears). */
void N3DSScr_BottomMenu(const uint8_t *fb, int32_t w, const uint16_t *pal, int32_t mainMenu)
{
    static uint16_t xi[BOT_W], yi[SCR_H];
    static uint8_t  xw[BOT_W], yw[SCR_H];
    static uint8_t  prev[336 * 272];
    static uint16_t prevPal[256];
    static int32_t  mapFor = -1;
    uint16_t line[BOT_W];
    uint16_t *out = bottom_fb();
    int32_t x, y, margin = (w - 336) / 2, changed = s_menuDirty;
    int32_t dstLo = 0, dstHi = SCR_H;
    if (margin < 0) margin = 0;
    s_bottomMode = N3DS_BOTTOM_MENU;
    if (!out || w < 336)
        return;
    if (mapFor != w) {
        for (x = 0; x < BOT_W; x++) {
            int32_t v = x * 336 * 32 / BOT_W;        /* 5 fractional bits */
            int32_t i = v >> 5;
            xi[x] = (uint16_t)(margin + (i > 334 ? 334 : i));
            xw[x] = (uint8_t)(i > 334 ? 0 : (v & 31));
        }
        for (y = 0; y < SCR_H; y++) {
            int32_t v = y * 272 * 32 / SCR_H;
            int32_t i = v >> 5;
            yi[y] = (uint16_t)(i > 270 ? 270 : i);
            yw[y] = (uint8_t)(i > 270 ? 0 : (v & 31));
        }
        mapFor = w;
        changed = 1;
    }
    if (memcmp(prevPal, pal, sizeof prevPal)) {
        memcpy(prevPal, pal, sizeof prevPal);
        changed = 1;
    }
    {
        int32_t lo = 272, hi = -1;
        for (y = 0; y < 272; y++) {
            const uint8_t *row = fb + y * w + margin;
            if (memcmp(prev + y * 336, row, 336)) {
                memcpy(prev + y * 336, row, 336);
                if (y < lo) lo = y;
                hi = y;
            }
        }
        if (hi >= 0 && !changed) {            /* only the screen rows that read a changed row */
            for (y = 0; y < SCR_H && yi[y] + 1 < lo; y++) ;
            dstLo = y;
            for (dstHi = SCR_H; dstHi > dstLo && yi[dstHi - 1] > hi; dstHi--) ;
        }
        if (hi >= 0) changed = 1;
    }
    pick_menu_window();
    changed |= s_menuDirty;
    if (!changed)
        return;
    s_menuDirty = 0;
    if (s_winExact) {
        for (x = 0; x < BOT_W; x++) {
            uint16_t *col = out + x * SCR_H;
            const uint8_t *src = prev + s_winOy * 336 + s_winOx + x;
            for (y = 0; y < SCR_H; y++)
                col[SCR_H - 1 - y] = pal[src[y * 336]];
        }
        if (mainMenu)
            N3DSScr_SeasonTab();
        return;
    }
    for (y = dstLo; y < dstHi; y++) {
        const uint8_t *r0 = prev + yi[y] * 336;
        const uint8_t *r1 = r0 + 336;
        const uint32_t wy = yw[y];
        for (x = 0; x < BOT_W; x++) {
            const int32_t i = xi[x] - margin;
            uint32_t a = spread565(pal[r0[i]]), b = spread565(pal[r0[i + 1]]);
            uint32_t c = spread565(pal[r1[i]]), d = spread565(pal[r1[i + 1]]);
            uint32_t t = mix565(mix565(a, b, xw[x]), mix565(c, d, xw[x]), wy);
            line[x] = (uint16_t)(t | (t >> 16));
        }
        for (x = 0; x < BOT_W; x++)
            out[x * SCR_H + (SCR_H - 1 - y)] = line[x];
    }
    if (mainMenu)
        N3DSScr_SeasonTab();
}

/* For n3ds.c's season list, drawn over the menu. */
uint16_t *N3DSScr_BottomFb(void) { return bottom_fb(); }
void N3DSScr_Fill(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)
{
    uint16_t *fb = bottom_fb();
    if (fb) fill_rect(fb, BOT_W, x0, y0, x1, y1, c);
}
void N3DSScr_Text(int32_t x, int32_t y, const char *t, int32_t scale, uint16_t c)
{
    uint16_t *fb = bottom_fb();
    if (fb) draw_text(fb, BOT_W, x, y, t, scale, c);
}
int32_t N3DSScr_TextWidth(const char *t, int32_t scale) { return text_width(t, scale); }

/* The SEASON button, over the main menu's bottom-right corner. */
void N3DSScr_SeasonTab(void)
{
    uint16_t *fb = bottom_fb();
    if (!fb) return;
    fill_rect(fb, BOT_W, N3DS_TAB_X0, N3DS_TAB_Y0, N3DS_TAB_X1, N3DS_TAB_Y1, RGB(20, 24, 40));
    fill_rect(fb, BOT_W, N3DS_TAB_X0 + 1, N3DS_TAB_Y0 + 1, N3DS_TAB_X1 - 1, N3DS_TAB_Y1 - 1, RGB(250, 200, 40));
    draw_text(fb, BOT_W, N3DS_TAB_X0 + (N3DS_TAB_X1 - N3DS_TAB_X0 - text_width("SEASON", 1)) / 2,
              N3DS_TAB_Y0 + 5, "SEASON", 1, RGB(20, 24, 40));
}

/* Map a touch on the bottom screen to the menu's own coordinates. */
void N3DSScr_TouchToMenu(int32_t tx, int32_t ty, int32_t *mx, int32_t *my)
{
    if (s_winExact) {
        *mx = s_winOx + tx;
        *my = s_winOy + ty;
        return;
    }
    *mx = tx * 336 / BOT_W;
    *my = ty * 272 / SCR_H;
}

/* ---- the match: score, minute, radar ------------------------------------------ */

extern TeamGeneralInfo _topTeamData, _bottomTeamData;
extern uint16_t _team1TotalGoals, _team2TotalGoals;
extern uint16_t _team1PenaltyGoals, _team2PenaltyGoals, _playingPenalties;
extern uint32_t _gameTime;
extern uint16_t _kTeamColorsTable[10];
extern Sprite   _ballSprite;

#define PITCH_X0  81
#define PITCH_X1  590
#define PITCH_Y0  129
#define PITCH_Y1  769

#define RADAR_Y0  58
#define RADAR_H   (SCR_H - RADAR_Y0 - 4)
#define RADAR_W   (RADAR_H * (PITCH_X1 - PITCH_X0) / (PITCH_Y1 - PITCH_Y0))
#define RADAR_X0  ((BOT_W - RADAR_W) / 2)

static int32_t rx(int32_t wx) { return RADAR_X0 + (wx - PITCH_X0) * RADAR_W / (PITCH_X1 - PITCH_X0); }
static int32_t ry(int32_t wy) { return RADAR_Y0 + (wy - PITCH_Y0) * RADAR_H / (PITCH_Y1 - PITCH_Y0); }

static TeamGeneralInfo *team_numbered(int32_t n)
{
    if (_topTeamData.teamNumber == n)    return &_topTeamData;
    if (_bottomTeamData.teamNumber == n) return &_bottomTeamData;
    return n == 1 ? &_topTeamData : &_bottomTeamData;
}

static uint16_t team_colour(const TeamGeneralInfo *t, const uint16_t *pal, uint16_t fallback)
{
    const TeamGame *g = t->inGameTeamPtr;
    if (!g || g->prShirtCol < 0 || g->prShirtCol >= 10)
        return fallback;
    return pal[_kTeamColorsTable[g->prShirtCol] & 0xFF];
}

static void team_name(const TeamGeneralInfo *t, char *out, int32_t cap)
{
    const TeamGame *g = t->inGameTeamPtr;
    int32_t i;
    out[0] = 0;
    if (!g) return;
    for (i = 0; i < cap - 1 && i < 16 && g->teamName[i]; i++)
        out[i] = (char)g->teamName[i];
    out[i] = 0;
}

static void line_h(uint16_t *fb, int32_t x0, int32_t x1, int32_t y, uint16_t c)
{
    for (; x0 <= x1; x0++) if (x0 >= 0 && x0 < BOT_W && y >= 0 && y < SCR_H) put(fb, x0, y, c);
}
static void line_v(uint16_t *fb, int32_t x, int32_t y0, int32_t y1, uint16_t c)
{
    for (; y0 <= y1; y0++) if (x >= 0 && x < BOT_W && y0 >= 0 && y0 < SCR_H) put(fb, x, y0, c);
}
static void rect_outline(uint16_t *fb, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)
{
    line_h(fb, x0, x1, y0, c); line_h(fb, x0, x1, y1, c);
    line_v(fb, x0, y0, y1, c); line_v(fb, x1, y0, y1, c);
}

static void dot(uint16_t *fb, int32_t x, int32_t y, uint16_t c, int32_t ring)
{
    int32_t dx, dy;
    for (dy = -2; dy <= 2; dy++)
        for (dx = -2; dx <= 2; dx++) {
            int32_t px = x + dx, py = y + dy, edge = dx == -2 || dx == 2 || dy == -2 || dy == 2;
            if ((dx == -2 || dx == 2) && (dy == -2 || dy == 2)) continue;
            if (px < 0 || px >= BOT_W || py < 0 || py >= SCR_H) continue;
            put(fb, px, py, edge ? (ring ? RGB(255, 255, 255) : RGB(0, 0, 0)) : c);
        }
}

void N3DSScr_BottomMatch(const uint16_t *pal)
{
    uint16_t *fb = bottom_fb();
    const uint16_t white = RGB(235, 235, 235), grass = RGB(40, 130, 50), grass2 = RGB(48, 142, 56);
    TeamGeneralInfo *t1 = team_numbered(1), *t2 = team_numbered(2);
    uint16_t c1 = team_colour(t1, pal, RGB(220, 50, 50)), c2 = team_colour(t2, pal, RGB(50, 90, 230));
    char name[20], buf[40];
    int32_t i, x, y, minute;
    const uint8_t *gt = (const uint8_t *)&_gameTime;

    s_bottomMode = N3DS_BOTTOM_MATCH;
    if ((s_bottomFrame++ & 1) || !fb)
        return;

    /* the score panel */
    fill_rect(fb, BOT_W, 0, 0, BOT_W, RADAR_Y0 - 2, RGB(14, 18, 30));
    team_name(t1, name, sizeof name);
    fill_rect(fb, BOT_W, 4, 6, 10, 14, c1);
    draw_text(fb, BOT_W, 14, 7, name, 1, white);
    team_name(t2, name, sizeof name);
    fill_rect(fb, BOT_W, BOT_W - 10, 6, BOT_W - 4, 14, c2);
    draw_text(fb, BOT_W, BOT_W - 14 - text_width(name, 1), 7, name, 1, white);

    snprintf(buf, sizeof buf, "%u - %u", (unsigned)_team1TotalGoals, (unsigned)_team2TotalGoals);
    draw_text(fb, BOT_W, (BOT_W - text_width(buf, 3)) / 2, 18, buf, 3, RGB(255, 230, 90));

    minute = (gt[1] & 0x0F) * 100 + (gt[2] & 0x0F) * 10 + (gt[3] & 0x0F);
    if (_playingPenalties)
        snprintf(buf, sizeof buf, "PENS %u - %u", (unsigned)_team1PenaltyGoals, (unsigned)_team2PenaltyGoals);
    else
        snprintf(buf, sizeof buf, "%d'", (int)minute);
    draw_text(fb, BOT_W, (BOT_W - text_width(buf, 1)) / 2, 44, buf, 1, white);

    /* the radar: the pitch */
    fill_rect(fb, BOT_W, 0, RADAR_Y0 - 2, BOT_W, SCR_H, RGB(14, 18, 30));
    for (y = RADAR_Y0; y < RADAR_Y0 + RADAR_H; y++)
        for (x = RADAR_X0; x < RADAR_X0 + RADAR_W; x++)
            put(fb, x, y, (((y - RADAR_Y0) / 12) & 1) ? grass : grass2);
    rect_outline(fb, rx(PITCH_X0), ry(PITCH_Y0), rx(PITCH_X1), ry(PITCH_Y1), white);
    line_h(fb, rx(PITCH_X0), rx(PITCH_X1), ry(449), white);
    for (i = 0; i < 64; i++) {                       /* the centre circle */
        float a = (float)i * 6.2832f / 64.0f;
        int32_t px = rx(336) + (int32_t)(cosf(a) * 13.0f), py = ry(449) + (int32_t)(sinf(a) * 13.0f);
        if (px >= 0 && px < BOT_W) put(fb, px, py, white);
    }
    /* the boxes and goals, at both ends */
    rect_outline(fb, rx(336 - 132), ry(PITCH_Y0), rx(336 + 132), ry(PITCH_Y0 + 92), white);
    rect_outline(fb, rx(336 - 132), ry(PITCH_Y1 - 92), rx(336 + 132), ry(PITCH_Y1), white);
    rect_outline(fb, rx(336 - 58), ry(PITCH_Y0), rx(336 + 58), ry(PITCH_Y0 + 32), white);
    rect_outline(fb, rx(336 - 58), ry(PITCH_Y1 - 32), rx(336 + 58), ry(PITCH_Y1), white);
    fill_rect(fb, BOT_W, rx(336 - 32), ry(PITCH_Y0) - 3, rx(336 + 32) + 1, ry(PITCH_Y0), white);
    fill_rect(fb, BOT_W, rx(336 - 32), ry(PITCH_Y1) + 1, rx(336 + 32) + 1, ry(PITCH_Y1) + 4, white);

    /* the players: kit colours, a white ring on the man under control */
    for (i = 0; i < 2; i++) {
        TeamGeneralInfo *t = i ? t2 : t1;
        uint16_t c = i ? c2 : c1;
        Sprite **sp = t->spritesTable;
        int32_t k;
        if (!sp) continue;
        for (k = 0; k < 11; k++) {
            Sprite *s = sp[k];
            if (!s || s->sentAway) continue;
            dot(fb, rx(s->x.s.i), ry(s->y.s.i), c, s == t->controlledPlayer && t->playerNumber != 0);
        }
    }
    /* the ball, bigger the higher it is */
    {
        int32_t bx = rx(_ballSprite.x.s.i), by = ry(_ballSprite.y.s.i);
        int32_t r = 1 + (_ballSprite.z.s.i > 0 ? _ballSprite.z.s.i / 24 : 0), dx, dy;
        if (r > 3) r = 3;
        for (dy = -r - 1; dy <= r + 1; dy++)
            for (dx = -r - 1; dx <= r + 1; dx++) {
                int32_t d2 = dx * dx + dy * dy, px = bx + dx, py = by + dy;
                if (d2 > (r + 1) * (r + 1) || px < 0 || px >= BOT_W || py < 0 || py >= SCR_H) continue;
                put(fb, px, py, d2 > r * r ? RGB(0, 0, 0) : RGB(255, 255, 255));
            }
    }
}

/* ---- everything else: controls and credits -------------------------------------- */

void N3DSScr_BottomInfo(void)
{
    static const char *const controls[] = {
        "CIRCLE PAD / D-PAD   MOVE",
        "A   FIRE        B   FIRE, SKIP",
        "START  PAUSE    SELECT  STATS",
        "Y  REPLAY       X  HIGHLIGHTS",
        "L + R   QUIT THE MATCH",
        "MENUS: TAP A BUTTON BELOW",
        NULL
    };
    uint16_t *fb = bottom_fb();
    int32_t i, y;
    if (s_bottomMode == N3DS_BOTTOM_INFO || !fb)
        return;                                 /* static: drawn once */
    s_bottomMode = N3DS_BOTTOM_INFO;
    fill_rect(fb, BOT_W, 0, 0, BOT_W, SCR_H, RGB(14, 18, 30));
    draw_text(fb, BOT_W, (BOT_W - text_width("OPENSWOS 3D", 2)) / 2, 6, "OPENSWOS 3D", 2, RGB(255, 230, 90));
    for (i = 0, y = 28; controls[i]; i++, y += 11)
        draw_text(fb, BOT_W, 10, y, controls[i], 1, RGB(235, 235, 235));
    y += 4;
    line_h(fb, 10, BOT_W - 10, y, RGB(80, 90, 120));
    y += 6;
    for (i = 0; k_creditsLines[i]; i++, y += 10)
        draw_text(fb, BOT_W, 10, y, k_creditsLines[i], 1,
                  k_creditsLines[i][0] == ' ' ? RGB(200, 200, 210) : RGB(140, 220, 140));
}

void N3DSScr_Init(void)
{
    gfxSetScreenFormat(GFX_BOTTOM, GSP_RGB565_OES);
    gfxSetDoubleBuffering(GFX_BOTTOM, false);
    load_logo();
    build_title_bg();
    s_bottomMode = -1;
    N3DSScr_BottomInfo();
}

#endif /* __3DS__ */
