/*
 * stereo.c -- per-pixel depth for the stereoscopic match view. See include/stereo.h.
 *
 * Three planes ride beside each frontbuffer (the game double-buffers, and the
 * presented frame is the one NOT being drawn, so each buffer needs its own):
 *
 *   bg     the pitch as DrawChunkyPitch left it, before anything was drawn over it
 *   stamp  the colour the last TRACKED draw wrote at each pixel
 *   dep    that draw's depth (STEREO_DEPTH_PITCH for the pitch itself)
 *
 * A pixel whose colour no longer matches its stamp was written by something
 * untracked -- a menu, a text overlay -- and gets the overlay depth. That is
 * what lets everything that is not a sprite or the pitch stay unmodified.
 */
#include "stereo.h"

#ifdef SWOS_SDL_BUILD

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game_types.h"
#include "display.h"

#define STEREO_SLOTS        3
#define STEREO_NO_TRACK     0x7FFF
#define STEREO_HUD_Z_MIN    8000          /* sprites.c HUD_LAYER_Z_MIN */

int32_t      g_stereoOn;
StereoConfig g_stereoCfg = {
    -48,    /* pitchFar:  the far touchline 6 px into the screen */
      0,    /* pitchNear: the near one on it                     */
     16,    /* heightGain: a 16 px player stands 4 px proud       */
     24,    /* hud: 3 px out                                     */
     16,    /* overlay: 2 px out                                 */
     96,    /* limit: 12 px either way                           */
};

typedef struct {
    const uint8_t *fb;
    uint8_t       *bg;
    uint8_t       *stamp;
    int8_t        *dep;
    int32_t        hasPitch;
} StereoSlot;

static StereoSlot s_slot[STEREO_SLOTS];
static int32_t    s_planeBytes;

/* The sprite being drawn. */
enum { MODE_NONE, MODE_GROUND, MODE_UPRIGHT, MODE_FLAT };
static int32_t s_mode;
static int32_t s_groundRow;     /* screen row of the sprite's feet */
static int32_t s_flatDepth;
static int32_t s_groundSaved;   /* mode to go back to after Stereo_BeginGround(0) */

extern Sprite _ballSprite, _ballShadowSprite;

/* ---- configuration ---------------------------------------------------- */

static void set_key(const char *k, int32_t v)
{
    if (!strcmp(k, "pitch_far"))        g_stereoCfg.pitchFar   = (int16_t)v;
    else if (!strcmp(k, "pitch_near"))  g_stereoCfg.pitchNear  = (int16_t)v;
    else if (!strcmp(k, "height_gain")) g_stereoCfg.heightGain = (int16_t)v;
    else if (!strcmp(k, "hud"))         g_stereoCfg.hud        = (int16_t)v;
    else if (!strcmp(k, "overlay"))     g_stereoCfg.overlay    = (int16_t)v;
    else if (!strcmp(k, "limit"))       g_stereoCfg.limit      = (int16_t)v;
    else fprintf(stderr, "[stereo] stereo.ini: unknown key '%s'\n", k);
}

void Stereo_LoadConfig(const char *dir)
{
    char path[1024], line[256];
    FILE *f;
    snprintf(path, sizeof path, "%s/stereo.ini", dir ? dir : ".");
    f = fopen(path, "r");
    if (!f) {
        /* Write the defaults so there is a file to edit. */
        f = fopen(path, "w");
        if (f) {
            fprintf(f, "; OpenSWOS 3D depth settings. Units: 1/8 pixel of disparity.\n"
                       "; Positive comes out of the screen, negative goes into it.\n"
                       "pitch_far = %d\npitch_near = %d\n"
                       "; depth per pixel of height above the pitch, in 1/64 pixel\n"
                       "height_gain = %d\nhud = %d\noverlay = %d\nlimit = %d\n",
                    g_stereoCfg.pitchFar, g_stereoCfg.pitchNear, g_stereoCfg.heightGain,
                    g_stereoCfg.hud, g_stereoCfg.overlay, g_stereoCfg.limit);
            fclose(f);
        }
        return;
    }
    while (fgets(line, sizeof line, f)) {
        char key[64];
        int32_t v;
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == ';' || *p == '#' || *p == '\n' || *p == '\r' || !*p) continue;
        if (sscanf(p, " %63[a-z_] = %d", key, &v) == 2)
            set_key(key, v);
    }
    fclose(f);
    fprintf(stderr, "[stereo] %s: far %d near %d gain %d hud %d overlay %d limit %d\n", path,
            g_stereoCfg.pitchFar, g_stereoCfg.pitchNear, g_stereoCfg.heightGain,
            g_stereoCfg.hud, g_stereoCfg.overlay, g_stereoCfg.limit);
}

void Stereo_Enable(int32_t on)
{
    g_stereoOn = on != 0;
    if (g_stereoOn && !s_planeBytes)
        s_planeBytes = FRONTBUFFER_MAX_STRIDE_BYTES * (SCREEN_HEIGHT + 1);
}

/* ---- slots -------------------------------------------------------------- */

static StereoSlot *slot_for(const uint8_t *fb, int32_t create)
{
    int32_t i;
    for (i = 0; i < STEREO_SLOTS; i++)
        if (s_slot[i].fb == fb)
            return &s_slot[i];
    if (!create || !g_stereoOn)
        return NULL;
    for (i = 0; i < STEREO_SLOTS; i++) {
        StereoSlot *s = &s_slot[i];
        if (s->fb)
            continue;
        s->bg    = (uint8_t *)calloc(1, (size_t)s_planeBytes);
        s->stamp = (uint8_t *)calloc(1, (size_t)s_planeBytes);
        s->dep   = (int8_t *)calloc(1, (size_t)s_planeBytes);
        if (!s->bg || !s->stamp || !s->dep) {
            free(s->bg); free(s->stamp); free(s->dep);
            memset(s, 0, sizeof *s);
            fprintf(stderr, "[stereo] out of memory for depth planes\n");
            return NULL;
        }
        s->fb = fb;
        return s;
    }
    return NULL;   /* a fourth buffer: not a frontbuffer (the mask buffer) */
}

/* ---- depth model -------------------------------------------------------- */

static int32_t clamp_depth(int32_t d)
{
    int32_t lim = g_stereoCfg.limit;
    if (lim > 126) lim = 126;
    if (d > lim)  d = lim;
    if (d < -lim) d = -lim;
    return d;
}

/* The pitch's depth at screen row y: a plane tilted away from the viewer. */
static int32_t ground_depth(int32_t y)
{
    int32_t h = _screen_height > 1 ? _screen_height : 1;
    if (y < 0) y = 0;
    if (y > h) y = h;
    return g_stereoCfg.pitchFar + (g_stereoCfg.pitchNear - g_stereoCfg.pitchFar) * y / h;
}

void Stereo_PitchDrawn(const uint8_t *fb)
{
    StereoSlot *s;
    int32_t n;
    if (!g_stereoOn || !fb)
        return;
    s = slot_for(fb, 1);
    if (!s)
        return;
    n = FRONTBUFFER_ROW_STRIDE_BYTES * _screen_height;
    if (n > s_planeBytes) n = s_planeBytes;
    memcpy(s->bg, fb, (size_t)n);
    memcpy(s->stamp, fb, (size_t)n);
    memset(s->dep, (uint8_t)STEREO_DEPTH_PITCH, (size_t)n);
    s->hasPitch = 1;
}

void Stereo_BeginSprite(const Sprite *spr, const SpriteGraphics *g)
{
    (void)g;
    if (!g_stereoOn || !spr) {
        s_mode = MODE_NONE;
        return;
    }
    if (spr->z.s.i >= STEREO_HUD_Z_MIN) {
        s_mode = MODE_FLAT;
        s_flatDepth = clamp_depth(g_stereoCfg.hud);
        return;
    }
    s_groundRow = (int32_t)spr->y.s.i - (int32_t)Render_CameraY();
    if (spr == &_ballShadowSprite) {
        s_mode = MODE_FLAT;
        s_flatDepth = clamp_depth(ground_depth(s_groundRow));
    } else if (spr == &_ballSprite) {
        int32_t z = spr->z.s.i > 0 ? spr->z.s.i : 0;
        s_mode = MODE_FLAT;
        s_flatDepth = clamp_depth(ground_depth(s_groundRow) + g_stereoCfg.heightGain * z / 8);
    } else {
        s_mode = MODE_UPRIGHT;     /* players, keepers, referee, goals, flags */
    }
}

void Stereo_BeginGround(int32_t on)
{
    if (on) {
        s_groundSaved = s_mode;
        s_mode = MODE_GROUND;
    } else {
        s_mode = s_groundSaved;
    }
}

void Stereo_MarkRow(const uint8_t *fb, int32_t y, int32_t dstX,
                    const uint8_t *srcRow, int32_t sprW)
{
    StereoSlot *s;
    int32_t w, d, px, base;
    if (s_mode == MODE_NONE || y < 0 || y >= _screen_height)
        return;
    s = slot_for(fb, 0);
    if (!s)
        return;
    w = FRONTBUFFER_ROW_STRIDE_BYTES;
    base = y * w;

    if (s_mode == MODE_GROUND) {
        for (px = 0; px < sprW; px++) {
            int32_t x = dstX + px;
            uint8_t c = srcRow[px];
            if (!c || x < 0 || x >= w) continue;
            s->bg[base + x] = c;
            s->stamp[base + x] = c;
            s->dep[base + x] = STEREO_DEPTH_PITCH;
        }
        return;
    }

    if (s_mode == MODE_FLAT) {
        d = s_flatDepth;
    } else {
        int32_t h = s_groundRow - y;          /* height of this row above the feet */
        if (h < 0) h = 0;
        d = clamp_depth(ground_depth(s_groundRow) + g_stereoCfg.heightGain * h / 8);
    }
    for (px = 0; px < sprW; px++) {
        int32_t x = dstX + px;
        uint8_t c = srcRow[px];
        if (!c || x < 0 || x >= w) continue;
        s->stamp[base + x] = c;
        s->dep[base + x] = (int8_t)d;
    }
}

/* ---- present ------------------------------------------------------------ */

int32_t Stereo_FrameHasDepth(const uint8_t *fb)
{
    extern volatile uint16_t _playGame, _playHighlightFlag;
    extern int32_t Pitch_DrawnThisMatch(void);
    StereoSlot *s;
    if (!g_stereoOn || !fb)
        return 0;
    if (!_playGame && !_playHighlightFlag)
        return 0;
    if (!Pitch_DrawnThisMatch())
        return 0;
    s = slot_for(fb, 0);
    return s && s->hasPitch;
}

/* Half the disparity, in whole pixels, for one eye: d in 1/8 px, strength /256. */
static inline int32_t eye_shift(int32_t d, int32_t strength)
{
    int32_t v = d * strength;               /* 1/(8*256) px of full disparity */
    /* half of it, rounded to the nearest pixel: /4096 */
    return v >= 0 ? (v + 2048) >> 12 : -((-v + 2048) >> 12);
}

void Stereo_ComposeRow(const uint8_t *fb, int32_t w, int32_t y, int32_t eye,
                       int32_t strength, uint8_t *out)
{
    static int16_t zrow[FRONTBUFFER_MAX_STRIDE_BYTES];
    StereoSlot *s = slot_for(fb, 0);
    const uint8_t *row = fb + y * w;
    const uint8_t *bg, *st;
    const int8_t  *dp;
    int32_t pd, sh, x;

    if (!s || w > FRONTBUFFER_MAX_STRIDE_BYTES) {
        memcpy(out, row, (size_t)w);
        return;
    }
    bg = s->bg + y * w;
    st = s->stamp + y * w;
    dp = s->dep + y * w;

    /* 1. The pitch, shifted by its own depth at this row. A pixel at source
     *    column x appears at x - eye * shift, so the output reads x + eye * shift. */
    pd = ground_depth(y);
    sh = eye * eye_shift(pd, strength);
    for (x = 0; x < w; x++) {
        int32_t sx = x + sh;
        if (sx < 0) sx = 0;
        if (sx >= w) sx = w - 1;
        out[x] = bg[sx];
        zrow[x] = (int16_t)pd;
    }

    /* 2. Everything over it, nearest wins. */
    for (x = 0; x < w; x++) {
        uint8_t c = row[x];
        int32_t d, tx;
        if (c == st[x]) {
            if (dp[x] == STEREO_DEPTH_PITCH)
                continue;                       /* the pitch: already drawn */
            d = dp[x];
        } else {
            d = clamp_depth(g_stereoCfg.overlay);   /* drawn by something untracked */
        }
        tx = x - eye * eye_shift(d, strength);
        if (tx < 0 || tx >= w || d < zrow[tx])
            continue;
        out[tx] = c;
        zrow[tx] = (int16_t)d;
    }
}

#endif /* SWOS_SDL_BUILD */
