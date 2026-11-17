/*
 * n3ds_gpu.c -- the match's top screen drawn by the GPU (3DS only; docs/3DS_GPU.md).
 *
 * The CPU used to build both eyes' pictures pixel by pixel (compose, then write ~192,000
 * pixels in the screen's rotated layout): 25 ms of a frame on an original 3DS. The picture is
 * made of very few distinct things -- a pitch plane that is almost the same in both eyes and a
 * few hundred small pieces standing on it -- so here each eye is a list of textured rectangles:
 *
 *   1. the pitch plane (the game's own picture of the pitch, stands and boards, no players),
 *      as one texture, drawn as pieces (Stereo_GpuPlan's seg list), each shifted by its depth;
 *   2. the frame itself (players, ball, goals, shadows, text), as a second texture, drawn as
 *      strips (the run list), each moved by its depth, furthest first;
 *   3. the HUD sprites, 1:1, from a small atlas.
 *
 * The depth model is exactly stereo.c's -- the plan comes from it, from the same planes and the
 * same stereo.ini settings -- so the look and the tuning are unchanged. The CPU converts each
 * frame's palette pixels into the textures (once, not once per eye) and writes a few thousand
 * vertices; the GPU does the rest. Nothing here is match state.
 *
 * Any failure (memory, a plan that does not fit) returns 0 and the caller draws the frame the
 * old way, so this can only ever cost a frame, never the match.
 */
#ifdef __3DS__

#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "n3ds.h"
#include "stereo.h"
#include "display.h"
#include "crt_effects.h"

#define TOP_W   400
#define TOP_H   240
#define PAD_COLS 16                     /* the pitch plane's last column repeated, for shifts past the edge */
#define HUD_W   256
#define HUD_H   128

int32_t g_n3dsGpu = 1;                  /* R + SELECT in a match switches it off and on */

static int32_t s_state;                 /* 0 not tried, 1 ready, -1 failed */
static C3D_RenderTarget *s_tgt[2];
static C3D_Tex s_texBg[2], s_texFg[2], s_texHud[2];
static int32_t s_tw, s_th, s_fw, s_fh;  /* texture size, and the frame size they were made for */
static int32_t s_set;                   /* which of the two texture sets this frame writes */
static uint8_t s_zoff[64];              /* texel order inside an 8x8 tile (Z order) */
static uint16_t s_pal565[256], s_pal5551[256];
static uint8_t s_tileUsed[128 * 64];    /* fg tiles a run touches: [tile row][tile col] */

/* the log: where the time goes */
static uint64_t s_tConv, s_tBuild, s_tWait;
static uint32_t s_frames, s_quads, s_fails;
static uint64_t s_logT0;

static int32_t pow2_at_least(int32_t n)
{
    int32_t p = 8;
    while (p < n) p <<= 1;
    return p;
}

static void make_zoff(void)
{
    int32_t x, y;
    for (y = 0; y < 8; y++)
        for (x = 0; x < 8; x++)
            s_zoff[y * 8 + x] = (uint8_t)((x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) |
                                          ((x & 4) << 2) | ((y & 4) << 3));
}

static C3D_RenderTarget *make_target(gfx3dSide_t side)
{
    C3D_RenderTarget *t = C3D_RenderTargetCreate(240, 400, GPU_RB_RGB565, GPU_RB_DEPTH16);
    if (!t) return NULL;
    C3D_RenderTargetSetOutput(t, GFX_TOP, side,
        GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |
        GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGB565) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB565) |
        GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
    return t;
}

static int32_t tex_make(C3D_Tex *t, int32_t w, int32_t h, GPU_TEXCOLOR fmt, int32_t linear)
{
    if (!C3D_TexInit(t, (u16)w, (u16)h, fmt))
        return 0;
    memset(t->data, 0, (size_t)w * (size_t)h * 2);
    C3D_TexSetFilter(t, linear ? GPU_LINEAR : GPU_NEAREST, linear ? GPU_LINEAR : GPU_NEAREST);
    C3D_TexSetWrap(t, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    return 1;
}

static void free_textures(void)
{
    int32_t i;
    for (i = 0; i < 2; i++) {
        if (s_texBg[i].data) C3D_TexDelete(&s_texBg[i]);
        if (s_texFg[i].data) C3D_TexDelete(&s_texFg[i]);
        if (s_texHud[i].data) C3D_TexDelete(&s_texHud[i]);
        memset(&s_texBg[i], 0, sizeof s_texBg[i]);
        memset(&s_texFg[i], 0, sizeof s_texFg[i]);
        memset(&s_texHud[i], 0, sizeof s_texHud[i]);
    }
}

static int32_t textures_for(int32_t w, int32_t h)
{
    int32_t i, tw = pow2_at_least(w + PAD_COLS), th = pow2_at_least(h);
    if (s_fw == w && s_fh == h && s_texBg[0].data)
        return 1;
    free_textures();
    if (tw > 1024 || th > 1024 || tw / 8 > 128 || th / 8 > 64)
        return 0;
    for (i = 0; i < 2; i++)
        /* The pitch is scaled down (454 x 272 -> 400 x 240): filtered, so a one-pixel line stays a
         * line (nearest sampling dropped columns and rows, breaking lines and turning the grass's
         * fine pattern into moire). What stands on it stays crisp. */
        if (!tex_make(&s_texBg[i], tw, th, GPU_RGB565, 1) || !tex_make(&s_texFg[i], tw, th, GPU_RGBA5551, 0) ||
            !tex_make(&s_texHud[i], HUD_W, HUD_H, GPU_RGBA5551, 0)) {
            free_textures();
            return 0;
        }
    s_tw = tw; s_th = th; s_fw = w; s_fh = h;
    return 1;
}

static int32_t init_once(void)
{
    if (s_state)
        return s_state > 0;
    s_state = -1;
    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) {
        fprintf(stderr, "[gpu] C3D_Init failed -- drawing the old way\n");
        return 0;
    }
    if (!C2D_Init(8192)) {
        fprintf(stderr, "[gpu] C2D_Init failed -- drawing the old way\n");
        C3D_Fini();
        return 0;
    }
    C2D_Prepare();
    s_tgt[0] = make_target(GFX_LEFT);
    s_tgt[1] = make_target(GFX_RIGHT);
    if (!s_tgt[0] || !s_tgt[1]) {
        fprintf(stderr, "[gpu] render targets failed -- drawing the old way\n");
        C2D_Fini(); C3D_Fini();
        return 0;
    }
    make_zoff();
    s_state = 1;
    fprintf(stderr, "[gpu] ready: the match's top screen is drawn by the GPU (R + SELECT switches it off)\n");
    return 1;
}

void N3DSGpu_Shutdown(void)
{
    if (s_state <= 0) return;
    s_state = -1;
    free_textures();
    C2D_Fini();
    C3D_Fini();
}

/* ---- the textures ---------------------------------------------------------------------- */

/* The pitch plane, as RGB565, in 8x8 tiles. The columns past the right edge repeat the last
 * one (a shift can read past it; the left edge is repeated by the clamp). */
static void convert_bg(C3D_Tex *tex, const uint8_t *bg, int32_t w, int32_t h)
{
    const int32_t tilesW = s_tw >> 3, tilesY = (h + 7) >> 3, colsUsed = (w + PAD_COLS + 7) >> 3;
    int32_t ty, tx, ry, rx;
    uint16_t *base = (uint16_t *)tex->data;
    for (ty = 0; ty < tilesY; ty++) {
        for (tx = 0; tx < colsUsed; tx++) {
            uint16_t *dst = base + (ty * tilesW + tx) * 64;
            const int32_t x0 = tx << 3;
            for (ry = 0; ry < 8; ry++) {
                int32_t y = (ty << 3) + ry;
                const uint8_t *src;
                const uint8_t *z = &s_zoff[ry << 3];
                if (y >= h) y = h - 1;
                src = bg + y * w;
                if (x0 + 8 <= w) {
                    dst[z[0]] = s_pal565[src[x0]];     dst[z[1]] = s_pal565[src[x0 + 1]];
                    dst[z[2]] = s_pal565[src[x0 + 2]]; dst[z[3]] = s_pal565[src[x0 + 3]];
                    dst[z[4]] = s_pal565[src[x0 + 4]]; dst[z[5]] = s_pal565[src[x0 + 5]];
                    dst[z[6]] = s_pal565[src[x0 + 6]]; dst[z[7]] = s_pal565[src[x0 + 7]];
                } else {
                    for (rx = 0; rx < 8; rx++) {
                        int32_t x = x0 + rx;
                        if (x >= w) x = w - 1;
                        dst[z[rx]] = s_pal565[src[x]];
                    }
                }
            }
        }
    }
    C3D_TexFlush(tex);
}

/* The frame's own picture, as RGBA5551 where something drew (a sprite, text, anything over the
 * pitch) and clear where the pitch shows: only the tiles a strip touches. */
static void convert_fg(C3D_Tex *tex, const uint8_t *fb, const StereoGpuPlan *pl, int32_t w, int32_t h)
{
    const int32_t tilesW = s_tw >> 3, tilesY = (h + 7) >> 3;
    int32_t i, ty, tx, ry, rx;
    uint16_t *base = (uint16_t *)tex->data;

    memset(s_tileUsed, 0, (size_t)(tilesY * 128));
    for (i = 0; i < pl->nRun; i++) {
        const StereoRun *r = &pl->run[i];
        const int32_t ta = r->y0 >> 3, tb = (r->y1 - 1) >> 3, ua = r->x0 >> 3, ub = (r->x1 - 1) >> 3;
        for (ty = ta; ty <= tb; ty++)
            for (tx = ua; tx <= ub; tx++)
                s_tileUsed[ty * 128 + tx] = 1;
    }
    for (ty = 0; ty < tilesY; ty++) {
        for (tx = 0; tx < tilesW; tx++) {
            uint16_t *dst;
            if (!s_tileUsed[ty * 128 + tx]) continue;
            dst = base + (ty * tilesW + tx) * 64;
            for (ry = 0; ry < 8; ry++) {
                const int32_t y = (ty << 3) + ry;
                const uint8_t *z = &s_zoff[ry << 3];
                const uint8_t *row, *st, *hm = NULL;
                const int8_t *dp;
                if (y >= h) {
                    for (rx = 0; rx < 8; rx++) dst[z[rx]] = 0;
                    continue;
                }
                row = fb + y * w; st = pl->stamp + y * w; dp = pl->dep + y * w;
                if (pl->rowHud[y]) hm = pl->hudMask + y * w;
                for (rx = 0; rx < 8; rx++) {
                    const int32_t x = (tx << 3) + rx;
                    uint8_t c;
                    if (x >= w) { dst[z[rx]] = 0; continue; }
                    c = (hm && hm[x]) ? st[x] : row[x];         /* the HUD is drawn separately */
                    dst[z[rx]] = (c != st[x] || dp[x] != STEREO_DEPTH_PITCH) ? s_pal5551[c] : 0;
                }
            }
        }
    }
    C3D_TexFlush(tex);
}

/* The HUD sprites packed into one small atlas (shelf by shelf); returns where each went. */
typedef struct { int16_t x, y; } AtlasPos;
static AtlasPos s_hudPos[64];
static int32_t  s_hudCount;

static void convert_hud(C3D_Tex *tex)
{
    static uint16_t lin[HUD_W * HUD_H];
    const StereoHudRect *hr;
    int32_t n = Stereo_HudRects(&hr), i, x = 0, y = 0, shelf = 0, usedH = 0, px, py, ty, tx, ry, rx;
    uint16_t *base = (uint16_t *)tex->data;

    s_hudCount = 0;
    if (n > 64) n = 64;
    for (i = 0; i < n; i++) {
        const int32_t rw = hr[i].w, rh = hr[i].h;
        if (rw > HUD_W || rh > HUD_H) { s_hudPos[i].x = -1; s_hudCount = i + 1; continue; }
        if (x + rw > HUD_W) { x = 0; y += shelf; shelf = 0; }
        if (y + rh > HUD_H) { s_hudPos[i].x = -1; s_hudCount = i + 1; continue; }
        s_hudPos[i].x = (int16_t)x; s_hudPos[i].y = (int16_t)y;
        for (py = 0; py < rh; py++)
            for (px = 0; px < rw; px++) {
                const uint8_t c = hr[i].px[py * rw + px];
                lin[(y + py) * HUD_W + x + px] = c ? s_pal5551[c] : 0;
            }
        x += rw;
        if (rh > shelf) shelf = rh;
        if (y + rh > usedH) usedH = y + rh;
        s_hudCount = i + 1;
    }
    for (ty = 0; ty * 8 < usedH; ty++)
        for (tx = 0; tx < HUD_W / 8; tx++) {
            uint16_t *dst = base + (ty * (HUD_W / 8) + tx) * 64;
            for (ry = 0; ry < 8; ry++)
                for (rx = 0; rx < 8; rx++)
                    dst[s_zoff[ry * 8 + rx]] = lin[((ty << 3) + ry) * HUD_W + (tx << 3) + rx];
        }
    C3D_TexFlush(tex);
}

/* ---- drawing --------------------------------------------------------------------------- */

/* The output row that source row y starts at: the first row whose centre is at or below it. */
static inline int32_t out_row(int32_t y, int32_t h)
{
    return (480 * y + h - 1) / (2 * h);
}

/* A rectangle of `tex` ([u0,u1) x [v0,v1) texels, v0 at the top) onto the screen. */
static inline void quad(C3D_Tex *tex, int32_t tw, int32_t th, float x, float y, float qw, float qh,
                        float u0, float v0, float u1, float v1)
{
    Tex3DS_SubTexture sub;
    C2D_Image im;
    C2D_DrawParams p;
    sub.width = (u16)(qw > 0 ? qw : 1);
    sub.height = (u16)(qh > 0 ? qh : 1);
    sub.left = u0 / (float)tw;
    sub.right = u1 / (float)tw;
    sub.top = 1.0f - v0 / (float)th;
    sub.bottom = 1.0f - v1 / (float)th;
    im.tex = tex;
    im.subtex = &sub;
    p.pos.x = x; p.pos.y = y; p.pos.w = qw; p.pos.h = qh;
    p.center.x = 0.0f; p.center.y = 0.0f;
    p.depth = 0.0f; p.angle = 0.0f;
    C2D_DrawImage(im, &p, NULL);
    s_quads++;
}

static void crt_draw(void);

static void draw_eye(int32_t eye, int32_t w, int32_t h, const StereoGpuPlan *pl, int32_t strength)
{
    /* eye: -1 left, +1 right (the plan's e is the LEFT eye's move: right eye is the opposite) */
    const float sx = (float)TOP_W / (float)w, sy = (float)h / (float)TOP_H;
    const int32_t sgn = eye < 0 ? 1 : -1;
    C3D_Tex *bg = &s_texBg[s_set], *fg = &s_texFg[s_set], *hud = &s_texHud[s_set];
    int32_t i;

    C2D_TargetClear(s_tgt[eye < 0 ? 0 : 1], C2D_Color32(0, 0, 0, 255));
    C2D_SceneBegin(s_tgt[eye < 0 ? 0 : 1]);

    for (i = 0; i < pl->nSeg; i++) {                        /* the pitch plane */
        const StereoSeg *g = &pl->seg[i];
        const int32_t a = out_row(g->y0, h), b = out_row(g->y1, h);
        const float e = (float)(g->e * sgn);
        if (b <= a) continue;
        quad(bg, s_tw, s_th, (float)g->x0 * sx, (float)a, (float)(g->x1 - g->x0) * sx, (float)(b - a),
             (float)g->x0 - e, (float)a * sy, (float)g->x1 - e, (float)b * sy);
    }
    for (i = 0; i < pl->nRun; i++) {                        /* what stands on it, furthest first */
        const StereoRun *r = &pl->run[i];
        const int32_t a = out_row(r->y0, h), b = out_row(r->y1, h);
        const float e = (float)(r->e * sgn);
        if (b <= a) continue;
        quad(fg, s_tw, s_th, ((float)r->x0 + e) * sx, (float)a, (float)(r->x1 - r->x0) * sx, (float)(b - a),
             (float)r->x0, (float)a * sy, (float)r->x1, (float)b * sy);
    }
    {                                                       /* the HUD, one pixel to a pixel */
        const StereoHudRect *hr;
        const int32_t n = Stereo_HudRects(&hr);
        const int32_t shift = strength > 0 ? Stereo_EyeShiftFor(Stereo_HudDepth(), strength) : 0;
        for (i = 0; i < n && i < s_hudCount; i++) {
            const int32_t ax = hr[i].x * TOP_W / w, ay = hr[i].y * TOP_H / h;
            if (s_hudPos[i].x < 0) continue;
            quad(hud, HUD_W, HUD_H, (float)(ax + shift * sgn), (float)ay, (float)hr[i].w, (float)hr[i].h,
                 (float)s_hudPos[i].x, (float)s_hudPos[i].y,
                 (float)(s_hudPos[i].x + hr[i].w), (float)(s_hudPos[i].y + hr[i].h));
        }
    }
    crt_draw();
}


/* ---- the CRT filter (the ini's crt_* settings; the pause screen changes them live) ---------- */

static CrtEffectsConfig s_crt;
static int32_t s_crtVer, s_crtBuilt = -1;
static C3D_Tex s_texCrt;
static int32_t s_crtTex;

void N3DSCrt_Set(const CrtEffectsConfig *c) { s_crt = *c; s_crtVer++; }
void N3DSCrt_Get(CrtEffectsConfig *c)       { *c = s_crt; }

static int32_t crt_active(void)
{
    return s_crt.mask != CRT_MASK_OFF || s_crt.vignette > 0 || s_crt.corners > 0;
}

/* The mask is built at the screen's own size (400 x 240) so the grille lands on whole pixels, and
 * multiplied over the finished picture with one rectangle per eye. Rebuilt only when a setting changes. */
static void crt_build(void)
{
    static uint32_t *buf;
    int32_t tx, ty, rx, ry;
    uint16_t *base;
    if (!buf) buf = (uint32_t *)malloc((size_t)TOP_W * TOP_H * 4);
    if (!buf) { s_crtBuilt = s_crtVer; return; }
    if (!s_crtTex) {
        if (!tex_make(&s_texCrt, 512, 256, GPU_RGB565, 0)) { s_crtBuilt = s_crtVer; return; }
        s_crtTex = 1;
    }
    CrtEffects_Fill(&s_crt, TOP_W, TOP_H, buf, TOP_W * 4);
    base = (uint16_t *)s_texCrt.data;
    for (ty = 0; ty < TOP_H / 8; ty++)
        for (tx = 0; tx < TOP_W / 8; tx++) {
            uint16_t *dst = base + (ty * (512 / 8) + tx) * 64;
            for (ry = 0; ry < 8; ry++)
                for (rx = 0; rx < 8; rx++) {
                    const uint32_t c = buf[((ty << 3) + ry) * TOP_W + (tx << 3) + rx];
                    dst[s_zoff[ry * 8 + rx]] = (uint16_t)((((c >> 16) & 0xFF) >> 3 << 11) | (((c >> 8) & 0xFF) >> 2 << 5) | ((c & 0xFF) >> 3));
                }
        }
    C3D_TexFlush(&s_texCrt);
    s_crtBuilt = s_crtVer;
}

static void crt_draw(void)
{
    if (!crt_active() || !s_crtTex) return;
    C2D_Flush();
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_DST_COLOR, GPU_ZERO, GPU_ZERO, GPU_ONE);   /* dst * mask */
    quad(&s_texCrt, 512, 256, 0.0f, 0.0f, (float)TOP_W, (float)TOP_H, 0.0f, 0.0f, (float)TOP_W, (float)TOP_H);
    C2D_Flush();
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
                   GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);                                    /* citro2d's own */
}

static void log_window(void)
{
    const uint64_t now = osGetTime();
    if (!s_logT0) s_logT0 = now;
    if (now - s_logT0 < 5000 || !s_frames) return;
    {
        const uint32_t n = s_frames;
        #define MS10(t) ((unsigned)((uint64_t)(t) * 10 / 268111 / n))
        fprintf(stderr, "[gpu] %u frames in %u ms: convert %u.%u ms, build+submit %u.%u ms, wait %u.%u ms, "
                        "%u quads/frame (both eyes), gpu %d.%d ms, %u fallbacks\n",
                n, (unsigned)(now - s_logT0),
                MS10(s_tConv) / 10, MS10(s_tConv) % 10, MS10(s_tBuild) / 10, MS10(s_tBuild) % 10,
                MS10(s_tWait) / 10, MS10(s_tWait) % 10, (unsigned)(s_quads / n),
                (int)C3D_GetProcessingTime(), (int)(C3D_GetProcessingTime() * 10.0f) % 10, (unsigned)s_fails);
        #undef MS10
    }
    fflush(stderr);
    s_tConv = s_tBuild = s_tWait = 0; s_frames = s_quads = s_fails = 0; s_logT0 = now;
}

/* Draws the match frame. 1: drawn and shown; 0: nothing was drawn, the caller draws it. */
int32_t N3DSGpu_Present(const uint8_t *fb, int32_t w, int32_t h, const uint8_t *rgba, int32_t strength)
{
    StereoGpuPlan pl;
    uint64_t t0, t1, t2;
    int32_t x, eyes;

    if (!g_n3dsGpu || !init_once())
        return 0;
    if (!Stereo_GpuPlan(fb, w, h, strength, &pl)) { s_fails++; return 0; }
    if (!textures_for(w, h)) { s_fails++; return 0; }

    t0 = svcGetSystemTick();
    for (x = 0; x < 256; x++) {
        const uint8_t *c = rgba + x * 4;
        s_pal565[x] = (uint16_t)(((c[0] >> 3) << 11) | ((c[1] >> 2) << 5) | (c[2] >> 3));
        s_pal5551[x] = (uint16_t)(((c[0] >> 3) << 11) | ((c[1] >> 3) << 6) | ((c[2] >> 3) << 1) | 1);
    }
    s_set ^= 1;
    convert_bg(&s_texBg[s_set], pl.bg, w, h);
    convert_fg(&s_texFg[s_set], fb, &pl, w, h);
    convert_hud(&s_texHud[s_set]);
    t1 = svcGetSystemTick();

    if (crt_active() && s_crtBuilt != s_crtVer)
        crt_build();
    C3D_FrameBegin(0);
    t2 = svcGetSystemTick();
    eyes = strength > 0 ? 2 : 1;
    draw_eye(-1, w, h, &pl, strength);
    if (eyes == 2)
        draw_eye(+1, w, h, &pl, strength);
    C3D_FrameEnd(0);

    s_tConv += t1 - t0;
    s_tWait += t2 - t1;
    s_tBuild += svcGetSystemTick() - t2;
    s_frames++;
    log_window();
    return 1;
}

#endif /* __3DS__ */
