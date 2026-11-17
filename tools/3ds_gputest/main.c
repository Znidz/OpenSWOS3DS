/*
 * OpenSWOS 3D -- GPU stereo test (docs/3DS_GPU_TEST.md)
 *
 * A stand-in for the match, drawn the way a GPU-based stereo renderer would draw it:
 * every eye is a list of textured quads, and the only thing that differs between the eyes
 * is each quad's horizontal shift. The CPU never touches a screen pixel.
 *
 *   - a pitch of 16x16 tiles from one tile atlas (the stands and boards recede in bands)
 *   - 22 players, each cut into 4 strips that lean out towards the viewer, with a flat shadow
 *   - a bouncing ball
 *
 * It measures, it does not play: it shows how many quads it draws, how long the CPU spends
 * building the frame, how long the GPU takes, and the frame rate -- synced and not.
 *
 * Buttons:  A  3D shift on/off        Y  vsync on/off (off shows the headroom)
 *           X  test view: 0 scene, 1 solid rectangles, 2 the textures themselves
 *           UP/DOWN  players 22/44/66  LEFT/RIGHT  pitch tiles 16 px / 32 px (4x fewer quads)
 *           START  quit
 * A line is appended to sdmc:/3ds/OpenSWOS3D/gputest.log every 5 seconds.
 */
#include <3ds.h>
#include <citro2d.h>
#include <stdint.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define SCR_W   400
#define SCR_H   240
#define STRIPS  4          /* a player is drawn as this many strips, each at its own depth */
#define PL_W    16
#define PL_H    24
#define MAX_PL  66

#define RGBA(r, g, b, a) ((uint32_t)(((r) << 24) | ((g) << 16) | ((b) << 8) | (a)))

/* ---- textures ---------------------------------------------------------------- */

/* Uploads a top-down RGBA image (0xRRGGBBAA) as a GPU texture: 8x8 tiles, Z-order inside a
 * tile, rows top first (as citro2d's sub-texture coordinates expect). */
static bool tex_upload(C3D_Tex *t, int w, int h, const uint32_t *img, bool swapRB)
{
    if (!C3D_TexInit(t, (u16)w, (u16)h, GPU_RGBA8))
        return false;
    uint32_t *dst = (uint32_t *)t->data;
    for (int y = 0; y < h; y++) {
        int ty = y;      /* first memory row is the picture's TOP row (checked on hardware, gputest-4: bottom-first came out upside down) */
        for (int x = 0; x < w; x++) {
            int tile = (ty >> 3) * (w >> 3) + (x >> 3);
            int z = (x & 1) | ((ty & 1) << 1) | ((x & 2) << 1) | ((ty & 2) << 2) |
                    ((x & 4) << 2) | ((ty & 4) << 3);
            uint32_t px = img[y * w + x];
            if (swapRB) px = ((px & 0xFF) << 24) | ((px & 0xFF00) << 8) | ((px >> 8) & 0xFF00) | (px >> 24);  /* AABBGGRR */
            dst[tile * 64 + z] = px;
        }
    }
    C3D_TexFlush(t);
    C3D_TexSetFilter(t, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetWrap(t, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    return true;
}

static uint32_t s_seed = 12345;
static int rnd(int n) { s_seed = s_seed * 1103515245u + 12345u; return (int)((s_seed >> 16) % (uint32_t)n); }

static C3D_Tex s_tileTex, s_spriteTex, s_swapTex;
static Tex3DS_SubTexture s_swapSub, s_atlasSub, s_spriteAllSub;
static C2D_Image s_swapImg, s_atlasImg, s_spriteImg;
static Tex3DS_SubTexture s_tileSub[8], s_stripSub[2][STRIPS], s_ballSub, s_shadowSub;
static C2D_Image s_tile[8], s_strip[2][STRIPS], s_ball, s_shadow;

static Tex3DS_SubTexture make_sub(int texW, int texH, int x, int y, int w, int h)
{
    Tex3DS_SubTexture s;
    s.width = (u16)w; s.height = (u16)h;
    s.left = (float)x / texW;           s.right  = (float)(x + w) / texW;
    s.top  = 1.0f - (float)y / texH;    s.bottom = 1.0f - (float)(y + h) / texH;
    return s;
}

static void fill(uint32_t *img, int stride, int x0, int y0, int w, int h, uint32_t c)
{
    for (int y = y0; y < y0 + h; y++)
        for (int x = x0; x < x0 + w; x++)
            img[y * stride + x] = c;
}

static void paint_tiles(uint32_t *img)      /* 64x64: 4x4 atlas of 16x16 tiles */
{
    const int S = 64;
    for (int t = 0; t < 8; t++) {
        int ox = (t & 3) * 16, oy = (t >> 2) * 16;
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 16; x++) {
                int n = rnd(14) - 7, r, g, b;
                switch (t) {
                case 0:  r = 40 + n;  g = 150 + n; b = 40 + n;  break;   /* light grass */
                case 1:  r = 30 + n;  g = 130 + n; b = 35 + n;  break;   /* dark grass */
                case 2:  r = 40 + n;  g = 150 + n; b = 40 + n;           /* goal line, horizontal */
                         if (y >= 7 && y <= 8) { r = g = b = 235; }      break;
                case 3:  r = 40 + n;  g = 150 + n; b = 40 + n;           /* centre line, vertical */
                         if (x >= 7 && x <= 8) { r = g = b = 235; }      break;
                case 4:  r = 120 + n; g = 110 + n; b = 105 + n;          /* stands: seats */
                         if (((x >> 2) + (y >> 2)) & 1) { r = 170; g = 60; b = 60; }  break;
                default: r = 30 + n;  g = 60 + n;  b = 170 + n;          /* boards */
                         if (y >= 6 && y <= 9) { r = g = b = 230; }      break;
                }
                img[(oy + y) * S + ox + x] = RGBA(r, g, b, 255);
            }
    }
}

static void paint_player(uint32_t *img, int stride, int ox, int oy, int kit)
{
    uint32_t shirt = kit ? RGBA(40, 70, 220, 255) : RGBA(220, 40, 40, 255);
    uint32_t stripe = RGBA(240, 240, 240, 255), skin = RGBA(235, 190, 150, 255);
    fill(img, stride, ox, oy, PL_W, PL_H, 0);
    fill(img, stride, ox + 5, oy + 1, 6, 6, skin);                       /* head */
    fill(img, stride, ox + 3, oy + 7, 10, 9, shirt);                     /* shirt */
    for (int y = oy + 7; y < oy + 16; y++)
        for (int x = ox + 4; x < ox + 12; x += 3) img[y * stride + x] = stripe;
    fill(img, stride, ox + 1, oy + 8, 2, 6, skin);                       /* arms */
    fill(img, stride, ox + 13, oy + 8, 2, 6, skin);
    fill(img, stride, ox + 4, oy + 16, 8, 4, RGBA(250, 250, 250, 255));  /* shorts */
    fill(img, stride, ox + 5, oy + 20, 2, 4, skin);                      /* legs */
    fill(img, stride, ox + 9, oy + 20, 2, 4, skin);
}

static bool make_textures(void)
{
    static uint32_t tiles[64 * 64], sprites[64 * 64];
    paint_tiles(tiles);
    memset(sprites, 0, sizeof sprites);
    paint_player(sprites, 64, 0, 0, 0);
    paint_player(sprites, 64, 16, 0, 1);
    for (int y = 0; y < 8; y++)                                          /* ball 8x8 at (32,0) */
        for (int x = 0; x < 8; x++) {
            int dx = x * 2 - 7, dy = y * 2 - 7;
            if (dx * dx + dy * dy <= 49)
                sprites[y * 64 + 32 + x] = (x == 3 || x == 4) && y == 3 ? RGBA(20, 20, 20, 255) : RGBA(250, 250, 250, 255);
        }
    for (int y = 0; y < 6; y++)                                          /* shadow 16x6 at (32,8) */
        for (int x = 0; x < 16; x++) {
            int dx = x * 2 - 15, dy = y * 2 - 5;
            if (dx * dx * 4 + dy * dy * 49 <= 900) sprites[(8 + y) * 64 + 32 + x] = RGBA(0, 0, 0, 120);
        }
    if (!tex_upload(&s_tileTex, 64, 64, tiles, false) || !tex_upload(&s_spriteTex, 64, 64, sprites, false) ||
        !tex_upload(&s_swapTex, 64, 64, tiles, true))
        return false;
    s_swapSub = make_sub(64, 64, 0, 0, 64, 64);
    s_swapImg.tex = &s_swapTex; s_swapImg.subtex = &s_swapSub;
    for (int t = 0; t < 8; t++) {
        s_tileSub[t] = make_sub(64, 64, (t & 3) * 16, (t >> 2) * 16, 16, 16);
        s_tile[t].tex = &s_tileTex; s_tile[t].subtex = &s_tileSub[t];
    }
    for (int k = 0; k < 2; k++)
        for (int s = 0; s < STRIPS; s++) {
            s_stripSub[k][s] = make_sub(64, 64, k * 16, s * (PL_H / STRIPS), PL_W, PL_H / STRIPS);
            s_strip[k][s].tex = &s_spriteTex; s_strip[k][s].subtex = &s_stripSub[k][s];
        }
    s_atlasSub = make_sub(64, 64, 0, 0, 64, 64);     s_atlasImg.tex = &s_tileTex;   s_atlasImg.subtex = &s_atlasSub;
    s_spriteAllSub = make_sub(64, 64, 0, 0, 64, 64); s_spriteImg.tex = &s_spriteTex; s_spriteImg.subtex = &s_spriteAllSub;
    s_ballSub = make_sub(64, 64, 32, 0, 8, 8);   s_ball.tex = &s_spriteTex;   s_ball.subtex = &s_ballSub;
    s_shadowSub = make_sub(64, 64, 32, 8, 16, 6); s_shadow.tex = &s_spriteTex; s_shadow.subtex = &s_shadowSub;
    return true;
}

/* ---- the scene --------------------------------------------------------------- */

typedef struct { float x, y; int kit; } Player;
static Player s_pl[MAX_PL];
static float  s_ballX, s_ballY, s_ballZ;
static int    s_order[MAX_PL];
static int    s_nPlayers = 22;
static int    s_tilePx   = 16;          /* 16 or 32: the pitch's quad size */
static bool   s_shift    = true;
static bool   s_vsync    = true;
static int    s_quads;                  /* quads drawn this frame, both eyes */
static int    s_mode;                   /* 0 scene, 1 solid rectangles, 2 the two textures */

/* Depths are in pixels of disparity: negative is behind the screen. The pitch sits 8 px in. */
#define PITCH_D   (-8.0f)

static float row_depth(float y)         /* the pitch's rows; the stands recede in steps */
{
    if (y < 32.0f)  return PITCH_D - 6.0f + (y < 16.0f ? 0.0f : 3.0f);
    if (y < 48.0f)  return PITCH_D - 1.0f;                 /* the boards */
    return PITCH_D;
}

static void sim(double t)               /* positions as a function of time: nothing to carry over */
{
    for (int i = 0; i < MAX_PL; i++) {
        float a = (float)i * 0.61f, w = 0.35f + 0.05f * (float)(i % 7);
        s_pl[i].x    = 200.0f + (60.0f + 8.0f * (float)(i % 11)) * cosf((float)t * w + a) * (i & 1 ? 1.4f : -1.4f);
        s_pl[i].y    = 140.0f + (45.0f + 3.0f * (float)(i % 5)) * sinf((float)t * w * 0.8f + a * 1.7f);
        s_pl[i].kit  = i & 1;
        s_order[i]   = i;
    }
    s_ballX = 200.0f + 90.0f * sinf((float)t * 0.9f);
    s_ballY = 150.0f + 40.0f * sinf((float)t * 1.3f);
    s_ballZ = 22.0f * fabsf(sinf((float)t * 3.1f));
}

static int by_y(const void *a, const void *b)
{
    float ya = s_pl[*(const int *)a].y, yb = s_pl[*(const int *)b].y;
    return ya < yb ? -1 : ya > yb;
}

static inline float eye_shift(int eye, float depth, float k)    /* eye: -1 left, +1 right */
{
    return s_shift ? -(float)eye * depth * k * 0.5f : 0.0f;
}

static void draw_border(int eye, float k)       /* magenta frame: proves the top screen shows anything */
{
    float sh = eye_shift(eye, -4.0f, k);
    u32 m = C2D_Color32(255, 0, 255, 255);
    C2D_DrawRectSolid(0.0f + sh, 0.0f, 0.0f, SCR_W, 4.0f, m);
    C2D_DrawRectSolid(0.0f + sh, SCR_H - 4.0f, 0.0f, SCR_W, 4.0f, m);
    C2D_DrawRectSolid(0.0f + sh, 0.0f, 0.0f, 4.0f, SCR_H, m);
    C2D_DrawRectSolid(SCR_W - 4.0f + sh, 0.0f, 0.0f, 4.0f, SCR_H, m);
}

static void draw_scene(C3D_RenderTarget *target, int eye, float k)
{
    C2D_TargetClear(target, s_mode == 0 ? C2D_Color32(0, 0, 0, 255) : C2D_Color32(20, 20, 60, 255));
    C2D_SceneBegin(target);

    if (s_mode == 1) {                          /* solid rectangles at three depths: no textures at all */
        C2D_DrawRectSolid(40.0f + eye_shift(eye, -8.0f, k), 40.0f, 0.0f, 80, 60, C2D_Color32(220, 40, 40, 255));
        C2D_DrawRectSolid(160.0f + eye_shift(eye, 0.0f, k), 40.0f, 0.0f, 80, 60, C2D_Color32(40, 200, 40, 255));
        C2D_DrawRectSolid(280.0f + eye_shift(eye, 8.0f, k), 40.0f, 0.0f, 80, 60, C2D_Color32(60, 80, 240, 255));
        draw_border(eye, k);
        s_quads += 7;
        return;
    }
    if (s_mode == 2) {                          /* the textures themselves, 3x: atlas, sprites, channel-swapped atlas */
        C2D_DrawImageAt(s_atlasImg,  8.0f,   40.0f, 0.0f, NULL, 3.0f, 3.0f);
        C2D_DrawImageAt(s_spriteImg, 208.0f, 40.0f, 0.0f, NULL, 3.0f, 3.0f);
        C2D_DrawImageAt(s_swapImg,   8.0f,   -140.0f + 240.0f, 0.0f, NULL, 0.5f, 0.5f);
        draw_border(eye, k);
        s_quads += 7;
        return;
    }

    const int tp = s_tilePx;
    const float sc = (float)tp / 16.0f;
    const int cols = (SCR_W + 32) / tp + 1, rows = (SCR_H + tp - 1) / tp;
    for (int r = 0; r < rows; r++) {
        float yc = (float)(r * tp) + tp * 0.5f;
        float sh = eye_shift(eye, row_depth(yc), k);
        for (int c = 0; c < cols; c++) {
            int t;
            if (yc < 32.0f)       t = 4;
            else if (yc < 48.0f)  t = 5;
            else if (yc < 64.0f)  t = 2;
            else if (c * tp - 16 <= 200 && 200 < c * tp - 16 + tp) t = 3;      /* the centre line */
            else                  t = (r + c) & 1;
            C2D_DrawImageAt(s_tile[t], (float)(c * tp - 16) + sh, (float)(r * tp), 0.0f, NULL, sc, sc);
            s_quads++;
        }
    }
    for (int j = 0; j < s_nPlayers; j++) {                            /* shadows: flat on the grass */
        const Player *p = &s_pl[s_order[j]];
        C2D_DrawImageAt(s_shadow, p->x - 8.0f + eye_shift(eye, PITCH_D, k), p->y - 3.0f, 0.0f, NULL, 1.0f, 1.0f);
        s_quads++;
    }
    C2D_DrawImageAt(s_shadow, s_ballX - 8.0f + eye_shift(eye, PITCH_D, k), s_ballY - 3.0f, 0.0f, NULL, 1.0f, 1.0f);
    s_quads++;
    for (int j = 0; j < s_nPlayers; j++) {                            /* players, back to front */
        const Player *p = &s_pl[s_order[j]];
        for (int s = 0; s < STRIPS; s++) {
            float h = (float)(PL_H - (s * (PL_H / STRIPS) + (PL_H / STRIPS) / 2));   /* height of the strip's middle */
            float d = PITCH_D + 1.0f + 3.0f * h / (float)PL_H;                      /* feet 1 px out, head 4 px */
            C2D_DrawImageAt(s_strip[p->kit][s], p->x - PL_W / 2 + eye_shift(eye, d, k),
                            p->y - PL_H + (float)(s * (PL_H / STRIPS)), 0.0f, NULL, 1.0f, 1.0f);
            s_quads++;
        }
    }
    {
        float d = PITCH_D + 1.0f + 3.0f * s_ballZ / (float)PL_H;
        C2D_DrawImageAt(s_ball, s_ballX - 4.0f + eye_shift(eye, d, k), s_ballY - 8.0f - s_ballZ, 0.0f, NULL, 1.0f, 1.0f);
        s_quads++;
    }
    draw_border(eye, k);
    s_quads += 4;
}

/* ---- main -------------------------------------------------------------------- */

static double now_ms(void) { return (double)svcGetSystemTick() / (SYSCLOCK_ARM11 / 1000.0); }

int main(void)
{
    gfxInitDefault();
    gfxSet3D(true);
    bool c3dOk = C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    bool c2dOk = C2D_Init(8192);
    C2D_Prepare();
    consoleInit(GFX_BOTTOM, NULL);

    C3D_RenderTarget *left  = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    C3D_RenderTarget *right = C2D_CreateScreenTarget(GFX_TOP, GFX_RIGHT);
    printf("C3D_Init %d  C2D_Init %d  targets %d %d\n", c3dOk, c2dOk, left != NULL, right != NULL);
    int beginOk = 0, beginTotal = 0;

    if (!make_textures()) {
        printf("Could not create the textures.\nPress START.\n");
        while (aptMainLoop()) { hidScanInput(); if (hidKeysDown() & KEY_START) break; gfxFlushBuffers(); gspWaitForVBlank(); }
        C2D_Fini(); C3D_Fini(); gfxExit();
        return 1;
    }
    mkdir("sdmc:/3ds/OpenSWOS3D", 0777);

    double t0 = now_ms(), winStart = t0, buildSum = 0.0, waitSum = 0.0, winMax = 0.0;
    int frames = 0, quadsLast = 0, logWin = 0;
    float cpuMs = 0, waitMs = 0, maxMs = 0, fps = 0;

    while (aptMainLoop()) {
        hidScanInput();
        u32 kd = hidKeysDown();
        if (kd & KEY_START) break;
        if (kd & KEY_A) s_shift = !s_shift;
        if (kd & KEY_Y) s_vsync = !s_vsync;
        if (kd & KEY_X) s_mode = (s_mode + 1) % 3;
        if (kd & KEY_UP)    s_nPlayers = s_nPlayers < MAX_PL ? s_nPlayers + 22 : MAX_PL;
        if (kd & KEY_DOWN)  s_nPlayers = s_nPlayers > 22 ? s_nPlayers - 22 : 22;
        if (kd & (KEY_LEFT | KEY_RIGHT)) s_tilePx = s_tilePx == 16 ? 32 : 16;

        float k = osGet3DSliderState();
        double tf0 = now_ms();
        sim((tf0 - t0) / 1000.0);
        qsort(s_order, (size_t)s_nPlayers, sizeof s_order[0], by_y);

        s_quads = 0;
        double tb = now_ms();
        bool began = C3D_FrameBegin(s_vsync ? C3D_FRAME_SYNCDRAW : 0);
        beginTotal++; if (began) beginOk++;
        double ta = now_ms();
        draw_scene(left, -1, k);
        if (k > 0.0f) draw_scene(right, +1, k);
        C3D_FrameEnd(0);
        double te = now_ms();

        double build = te - ta, wait = ta - tb;
        buildSum += build; waitSum += wait; frames++;
        if (te - tf0 > winMax) winMax = te - tf0;
        quadsLast = s_quads;

        if (te - winStart >= 1000.0) {
            fps = (float)frames * 1000.0f / (float)(te - winStart);
            cpuMs = (float)(buildSum / frames); waitMs = (float)(waitSum / frames); maxMs = (float)winMax;
            consoleClear();
            printf("OpenSWOS 3D  GPU stereo test\n\n");
            printf("3D slider    %.2f %s\n", k, k <= 0.0f ? "(OFF: one eye only)" : "");
            printf("shift        %s   (A)\n", s_shift ? "on" : "off");
            printf("vsync        %s   (Y)\n", s_vsync ? "on" : "off");
            printf("players      %d   (UP/DOWN)\n", s_nPlayers);
            printf("pitch tiles  %d px   (LEFT/RIGHT)\n\n", s_tilePx);
            printf("quads/frame  %d\n", quadsLast);
            printf("frames/s     %.1f\n", fps);
            printf("CPU build    %.2f ms\n", cpuMs);
            printf("waiting      %.2f ms\n", waitMs);
            printf("slowest      %.1f ms\n", maxMs);
            printf("GPU          %.2f ms\n", C3D_GetProcessingTime());
            printf("GPU drawing  %.2f ms\n", C3D_GetDrawingTime());
            printf("cmd buffer   %.0f %%\n\n", C3D_GetCmdBufUsage() * 100.0f);
            printf("test view   %d   (X: 0 scene, 1 rects, 2 textures)\n", s_mode);
            printf("FrameBegin  %d/%d ok\n", beginOk, beginTotal);
            printf("START quits\n");
            if (++logWin >= 5) {
                logWin = 0;
                FILE *f = fopen("sdmc:/3ds/OpenSWOS3D/gputest.log", "a");
                if (f) {
                    fprintf(f, "[gputest] slider %.2f shift %d vsync %d players %d tile %d quads %d fps %.1f cpu %.2f wait %.2f slowest %.1f gpu %.2f drawing %.2f cmdbuf %.0f%%\n",
                            k, s_shift, s_vsync, s_nPlayers, s_tilePx, quadsLast, fps, cpuMs, waitMs, maxMs,
                            C3D_GetProcessingTime(), C3D_GetDrawingTime(), C3D_GetCmdBufUsage() * 100.0f);
                    fclose(f);
                }
            }
            frames = 0; buildSum = waitSum = winMax = 0.0; winStart = te;
        }
    }

    C3D_TexDelete(&s_tileTex);
    C3D_TexDelete(&s_spriteTex);
    C3D_TexDelete(&s_swapTex);
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
