/*
 * n3ds.c -- the Nintendo 3DS: stereoscopic top screen, buttons, and the first-run
 * data setup. Built only by Makefile.3ds (devkitARM + libctru + SDL2 for 3DS).
 *
 * The game is the ordinary SDL build. SDL still does the sound, the timer and the
 * threads; the picture skips SDL and goes straight to the top screen's two
 * framebuffers (one per eye), and the buttons go straight from the HID service
 * into the input layer's on-screen-controls hook (input_sdl.c), the same one the
 * web page's touch controls use.
 *
 * The render is the widescreen picture at the 3DS's shape: 454 x 272 (the height is
 * fixed by the game), scaled to the 400 x 240 screen. Each eye's row is composed
 * at the render resolution by Stereo_ComposeRow (stereo.c) and then sampled.
 *
 * Files, all under sdmc:/3ds/OpenSWOS3D/:
 *   *.adf        your SWOS 96/97 Amiga disks -- converted once, on first start
 *   assets/      what that conversion made (the game's graphics, sound, teams)
 *   save/        careers, tactics, replays
 *   SWOS.ini     the game's settings;  stereo.ini  the 3D depth settings
 *   SWOS.log     the log
 * The project's own small files (data_json/, setup/, ...) are inside the app (romfs).
 */
#ifdef __3DS__

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "n3ds.h"
#include "stereo.h"
#include "display.h"
#include "swosdata.h"

/* The game uses the stack like a desktop program does. */
u32 __stacksize__ = 1024 * 1024;

#define TOP_W   400
#define TOP_H   240

static int32_t  s_videoUp;
static int32_t  s_3dOn = -1;
static uint16_t s_pal565[256];
static uint16_t s_xmap[TOP_W];
static int32_t  s_xmapFor;
static uint8_t  s_lineL[FRONTBUFFER_MAX_STRIDE_BYTES];
static uint8_t  s_lineR[FRONTBUFFER_MAX_STRIDE_BYTES];
static PrintConsole s_console;

/* ---- start-up -------------------------------------------------------------- */

void N3DS_EarlyInit(void)
{
    osSetSpeedupEnable(true);           /* New 3DS: 804 MHz and the L2 cache */
    gfxInitDefault();
    gfxSetScreenFormat(GFX_TOP, GSP_RGB565_OES);
    gfxSetDoubleBuffering(GFX_TOP, true);
    consoleInit(GFX_BOTTOM, &s_console);
    romfsInit();
    s_videoUp = 1;

    printf("\x1b[1;1H\x1b[32mOpenSWOS 3D\x1b[0m\n"
           "Sensible World of Soccer, stereoscopic\n\n");
    mkdir("sdmc:/3ds", 0777);
    mkdir(N3DS_USER_DIR, 0777);
}

void N3DS_Exit(void)
{
    if (!s_videoUp) return;
    gfxExit();          /* romfs is left to the C runtime's own teardown (SDL's main may own it) */
    s_videoUp = 0;
}

static void print_controls(void)
{
    consoleClear();
    printf("\x1b[1;1H\x1b[32mOpenSWOS 3D\x1b[0m\n\n"
           " Circle Pad / D-Pad  move\n"
           " A                   fire\n"
           " B                   fire, skip\n"
           " START               pause\n"
           " SELECT              statistics\n"
           " Y                   instant replay\n"
           " X                   replay after match\n"
           " L + R               quit the match\n\n"
           " 3D slider           depth\n\n"
           " Depth settings:\n"
           "  sdmc:/3ds/OpenSWOS3D/stereo.ini\n"
           " Log:\n"
           "  sdmc:/3ds/OpenSWOS3D/SWOS.log\n");
}

/* ---- first-run data -------------------------------------------------------- */

static void on_setup_log(void *user, const char *line)
{
    (void)user;
    printf("%.39s\n", line);
    gfxFlushBuffers();
    gfxSwapBuffers();
}

static void on_setup_progress(void *user, int32_t done, int32_t total)
{
    (void)user;
    if (total > 0)
        printf("\x1b[29;1H %3d%%  (%d of %d)        ", (int)(done * 100 / total), (int)done, (int)total);
    gfxFlushBuffers();
    gfxSwapBuffers();
}

static void wait_and_quit(void)
{
    printf("\nPress START to exit.\n");
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
    N3DS_Exit();
    exit(0);
}

static int32_t file_exists(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0;
}

void N3DS_PrepareData(const char *assetsDir)
{
    static SdJob job;
    char ini[1100], err[256];

    snprintf(ini, sizeof ini, "%s/ASSETS.ini", assetsDir);
    if (file_exists(ini))
        return;                                     /* made already */

    printf("First start: making the game data\n"
           "from your Amiga disks.\n\n");
    memset(&job, 0, sizeof job);
    SdSources_Scan(&job.src, N3DS_USER_DIR);
    if (!SdSources_HaveAmiga(&job.src)) {
        printf("\x1b[31mNo SWOS 96/97 disks found.\x1b[0m\n\n"
               "Copy your two Amiga disk images\n"
               "(.adf, or a .zip/.lha/.hdf of the\n"
               "game) into:\n\n"
               "  sdmc:/3ds/OpenSWOS3D/\n\n"
               "then start OpenSWOS 3D again.\n");
        if (SdSources_AmigaProblem()[0])
            printf("\nNote: %.200s\n", SdSources_AmigaProblem());
        wait_and_quit();
    }
    {
        const char *season = SdSources_AmigaSeason(&job.src);
        printf("Season: %s\n\n", season ? season : "?");
    }
    snprintf(job.outDir, sizeof job.outDir, "%s", assetsDir);
    snprintf(job.resDir, sizeof job.resDir, "romfs:");
    job.log = on_setup_log;
    job.progress = on_setup_progress;
    if (SwosData_Run(&job, err) != 0) {
        printf("\n\x1b[31mThe data setup failed:\x1b[0m\n%.300s\n", err);
        wait_and_quit();
    }
    printf("\nDone.\n");
}

/* ---- picture --------------------------------------------------------------- */

void N3DS_VideoReady(void)
{
    print_controls();
}

static void set_3d(int32_t on)
{
    if (on == s_3dOn) return;
    s_3dOn = on;
    gfxSet3D(on ? true : false);
}

void N3DS_Present(const uint8_t *fb, int32_t w, int32_t h, const uint8_t *rgba)
{
    uint16_t *outL, *outR;
    int32_t x, y, strength, depth;

    if (!s_videoUp || !fb || w <= 0 || h <= 0 || w > FRONTBUFFER_MAX_STRIDE_BYTES)
        return;

    for (x = 0; x < 256; x++) {
        const uint8_t *c = rgba + x * 4;
        s_pal565[x] = (uint16_t)(((c[0] >> 3) << 11) | ((c[1] >> 2) << 5) | (c[2] >> 3));
    }
    if (s_xmapFor != w) {
        for (x = 0; x < TOP_W; x++)
            s_xmap[x] = (uint16_t)(x * w / TOP_W);
        s_xmapFor = w;
    }

    strength = (int32_t)(osGet3DSliderState() * 256.0f + 0.5f);
    depth = strength > 0 && Stereo_FrameHasDepth(fb);
    set_3d(strength > 0);

    outL = (uint16_t *)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
    outR = s_3dOn ? (uint16_t *)gfxGetFramebuffer(GFX_TOP, GFX_RIGHT, NULL, NULL) : NULL;

    for (y = 0; y < TOP_H; y++) {
        const int32_t sy = y * h / TOP_H;
        const uint8_t *l, *r;
        /* The framebuffer is the screen turned on its side: column x is 240
         * pixels, bottom to top. */
        uint16_t *pl = outL + (TOP_H - 1 - y);
        if (depth) {
            Stereo_ComposeRow(fb, w, sy, -1, strength, s_lineL);
            Stereo_ComposeRow(fb, w, sy, +1, strength, s_lineR);
            l = s_lineL;
            r = s_lineR;
        } else {
            l = r = fb + sy * w;    /* menus: flat, on the screen */
        }
        for (x = 0; x < TOP_W; x++)
            pl[x * TOP_H] = s_pal565[l[s_xmap[x]]];
        if (outR) {
            uint16_t *pr = outR + (TOP_H - 1 - y);
            for (x = 0; x < TOP_W; x++)
                pr[x * TOP_H] = s_pal565[r[s_xmap[x]]];
        }
    }
    gfxFlushBuffers();
    gfxSwapBuffers();
}

/* ---- buttons --------------------------------------------------------------- */

extern void InputSDL_TouchControls(int32_t dirs, int32_t fire);
extern void InputSDL_TouchKey(int32_t akey);
extern void InputSDL_TouchSkip(int32_t on);

int32_t N3DS_PollInput(void)
{
    u32 held, down;
    int32_t dirs = 0;

    if (!aptMainLoop())
        return 0;                       /* HOME menu close / power: quit */

    hidScanInput();
    held = hidKeysHeld();
    down = hidKeysDown();

    if (held & KEY_UP)    dirs |= 1;    /* KEY_UP is the D-pad or the Circle Pad */
    if (held & KEY_DOWN)  dirs |= 2;
    if (held & KEY_LEFT)  dirs |= 4;
    if (held & KEY_RIGHT) dirs |= 8;
    InputSDL_TouchControls(dirs, (held & (KEY_A | KEY_B)) != 0);
    InputSDL_TouchSkip((held & KEY_B) != 0);

    /* Raw Amiga scancodes, as the pad's buttons send them (input_sdl.c k_padKeys). */
    if (down & KEY_START)  InputSDL_TouchKey(0x19);     /* P   pause                */
    if (down & KEY_SELECT) InputSDL_TouchKey(0x21);     /* S   statistics           */
    if (down & KEY_Y)      InputSDL_TouchKey(0x13);     /* R   instant replay       */
    if (down & KEY_X)      InputSDL_TouchKey(0x25);     /* H   replay after match   */
    if ((down & (KEY_L | KEY_R)) && (held & KEY_L) && (held & KEY_R))
        InputSDL_TouchKey(0x45);                        /* ESC quit the match       */
    return 1;
}

#endif /* __3DS__ */
