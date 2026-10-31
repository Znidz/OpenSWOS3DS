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
#include "menu_system.h"

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
     -8,    /* menuBack: the menu backdrop 1 px into the screen   */
     16,    /* menuText: titles, text and pictures, 2 px out      */
     32,    /* menuButton: the rectangles, 4 px out               */
     48,    /* menuSelected: the highlighted one, 6 px out        */
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
enum { CUR_OTHER, CUR_SHADOW, CUR_BALL };
static int32_t s_cur, s_shadowCentre = 100;  /* what is being drawn; where the ball's shadow is */
static int32_t s_rowBottom = -1; /* lowest screen row the sprite being drawn covers */
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
    else if (!strcmp(k, "menu_back"))     g_stereoCfg.menuBack     = (int16_t)v;
    else if (!strcmp(k, "menu_text"))     g_stereoCfg.menuText     = (int16_t)v;
    else if (!strcmp(k, "menu_button"))   g_stereoCfg.menuButton   = (int16_t)v;
    else if (!strcmp(k, "menu_selected")) g_stereoCfg.menuSelected = (int16_t)v;
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
                       "height_gain = %d\nhud = %d\noverlay = %d\nlimit = %d\n"
                       "; menus: the backdrop, text and pictures, buttons, the highlighted button\n"
                       "menu_back = %d\nmenu_text = %d\nmenu_button = %d\nmenu_selected = %d\n",
                    g_stereoCfg.pitchFar, g_stereoCfg.pitchNear, g_stereoCfg.heightGain,
                    g_stereoCfg.hud, g_stereoCfg.overlay, g_stereoCfg.limit,
                    g_stereoCfg.menuBack, g_stereoCfg.menuText, g_stereoCfg.menuButton,
                    g_stereoCfg.menuSelected);
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
    s_cur = CUR_OTHER;
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
    s_cur = CUR_OTHER;
    if (spr == &_ballShadowSprite) {
        s_cur = CUR_SHADOW;
        s_mode = MODE_FLAT;
        s_flatDepth = clamp_depth(ground_depth(s_groundRow));
    } else if (spr == &_ballSprite) {
        /* On its shadow the ball is on the ground, the plane furthest from the viewer.
         * How far it has risen is the gap on screen between it and its shadow, filled in
         * by Stereo_SetRows once both are placed. */
        s_cur = CUR_BALL;
        s_mode = MODE_FLAT;
        s_flatDepth = clamp_depth(ground_depth(s_shadowCentre) + g_stereoCfg.heightGain / 4);
    } else {
        s_mode = MODE_UPRIGHT;     /* players, keepers, referee, goals, flags */
    }
}

void Stereo_SetRows(int32_t top, int32_t bottom)
{
    s_rowBottom = bottom;
    if (s_cur == CUR_SHADOW) {
        s_shadowCentre = (top + bottom) / 2;
        s_flatDepth = clamp_depth(ground_depth(s_shadowCentre));
    } else if (s_cur == CUR_BALL) {
        int32_t lift = s_shadowCentre - (top + bottom) / 2;   /* rows above its shadow */
        if (lift < 0) lift = 0;
        /* An object with height like a player: a card standing at its contact point (the
         * shadow), as proud of the ground as a player is, then raised by its flight. */
        s_flatDepth = clamp_depth(ground_depth(s_shadowCentre) + g_stereoCfg.heightGain / 4
                                  + g_stereoCfg.heightGain * lift / 8);
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
        /* A standing figure is one flat card at its feet: a depth that grew row by row
         * sheared players, and measured from the sprite's anchor it flattened the legs. */
        int32_t feet = s_rowBottom >= 0 ? s_rowBottom : s_groundRow;
        d = clamp_depth(ground_depth(feet) + g_stereoCfg.heightGain / 4);
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

/* ---- menus -----------------------------------------------------------------
 *
 * A menu is drawn over a clean copy of its backdrop: DrawMenu starts with
 * BlitterCopyPanelToScreen2, which copies _frontbufferMaskBuffer over the screen,
 * and an item is erased by copying that buffer back. So the backdrop plane is
 * already there, and a pixel that differs from it is something the menu drew.
 * The menu's own entries say where the rectangles are; those stand out as slabs,
 * the highlighted one furthest. Nothing here touches the menu code. */

enum { KIND_FLAT, KIND_MATCH, KIND_MENU };
#define STEREO_MAX_SLABS  96
#define STEREO_NO_SLAB    (-32768)

typedef struct { int16_t x0, y0, x1, y1, d; } StereoSlab;

static int32_t    s_kind;
static StereoSlab s_slab[STEREO_MAX_SLABS];
static int32_t    s_numSlabs;

extern uint8_t  _frontbufferMaskBuffer[];
extern uint8_t  _currentMenu[];
extern MenuEntry _menuEntries[];
extern int      _menu_draw_x_offset;

/* Is the backdrop buffer this menu's backdrop? Other screens borrow it as scratch
 * space (image loading), and a menu-less screen has nothing to compare with. */
static int32_t menu_backdrop_matches(const uint8_t *fb)
{
    int32_t n = FRONTBUFFER_ROW_STRIDE_BYTES * _screen_height, i, same = 0, total = 0;
    for (i = 0; i < n; i += 37, total++)
        if (fb[i] == _frontbufferMaskBuffer[i])
            same++;
    return total > 0 && same * 100 >= total * 35;   /* most of the screen is backdrop */
}

static void menu_slabs(void)
{
    const Menu *menu = (const Menu *)_currentMenu;
    int32_t i, count = menu->numEntries;
    s_numSlabs = 0;
    if (count > 255) return;
    for (i = 0; i < count && s_numSlabs < STEREO_MAX_SLABS; i++) {
        const MenuEntry *e = (const MenuEntry *)((const uint8_t *)_menuEntries + i * MENUENTRY_STRIDE);
        StereoSlab *sl;
        if (e->isInvisible || !e->alreadyDrawn || !e->dwWidth || !e->dwHeight)
            continue;
        if (e->background != ENTRY_FRAME_AND_BACK_COLOR || (e->bgColor & 0xF) == 0)
            continue;                    /* transparent: its text floats, no slab */
        sl = &s_slab[s_numSlabs++];
        sl->x0 = (int16_t)(e->x + _menu_draw_x_offset);
        sl->y0 = (int16_t)e->y;
        sl->x1 = (int16_t)(sl->x0 + e->dwWidth);
        sl->y1 = (int16_t)(e->y + e->dwHeight);
        sl->d  = (int16_t)clamp_depth(e == menu->selectedEntry ? g_stereoCfg.menuSelected
                                                                : g_stereoCfg.menuButton);
    }
}

int32_t Stereo_FrameKind(void)
{
    return s_kind;
}

/* Called once per presented frame, before its rows are composed. */
int32_t Stereo_FrameHasDepth(const uint8_t *fb)
{
    extern volatile uint16_t _playGame, _playHighlightFlag;
    extern int32_t Pitch_DrawnThisMatch(void);
    StereoSlot *s;
    s_kind = KIND_FLAT;
    if (!g_stereoOn || !fb)
        return 0;
    if (_playGame || _playHighlightFlag) {
        if (!Pitch_DrawnThisMatch())
            return 0;
        s = slot_for(fb, 0);
        if (s && s->hasPitch)
            s_kind = KIND_MATCH;
        return s_kind != KIND_FLAT;
    }
    if (fb != _frontbufferMaskBuffer && menu_backdrop_matches(fb)) {
        menu_slabs();
        s_kind = KIND_MENU;
    }
    return s_kind != KIND_FLAT;
}

/* Half the disparity, in whole pixels, for one eye: d in 1/8 px, strength /256. */
static inline int32_t eye_shift(int32_t d, int32_t strength)
{
    int32_t v = d * strength;               /* 1/(8*256) px of full disparity */
    /* half of it, rounded to the nearest pixel: /4096 */
    return v >= 0 ? (v + 2048) >> 12 : -((-v + 2048) >> 12);
}

static void compose_menu_row(const uint8_t *fb, int32_t w, int32_t y, int32_t eye,
                             int32_t strength, uint8_t *out, int16_t *zrow)
{
    static int16_t slabRow[FRONTBUFFER_MAX_STRIDE_BYTES];
    const uint8_t *row = fb + y * w;
    const uint8_t *bg  = _frontbufferMaskBuffer + y * w;
    int32_t back = clamp_depth(g_stereoCfg.menuBack);
    int32_t text = clamp_depth(g_stereoCfg.menuText);
    int32_t sh = eye * eye_shift(back, strength);
    int32_t x, i;

    for (x = 0; x < w; x++) {
        int32_t sx = x + sh;
        if (sx < 0) sx = 0;
        if (sx >= w) sx = w - 1;
        out[x] = bg[sx];
        zrow[x] = (int16_t)back;
        slabRow[x] = STEREO_NO_SLAB;
    }
    for (i = 0; i < s_numSlabs; i++) {
        const StereoSlab *sl = &s_slab[i];
        int32_t x0 = sl->x0 < 0 ? 0 : sl->x0, x1 = sl->x1 > w ? w : sl->x1;
        if (y < sl->y0 || y >= sl->y1)
            continue;
        for (x = x0; x < x1; x++)
            if (sl->d > slabRow[x]) slabRow[x] = sl->d;
    }
    for (x = 0; x < w; x++) {
        int32_t d = slabRow[x], tx;
        if (d == STEREO_NO_SLAB) {
            if (row[x] == bg[x])
                continue;               /* backdrop: already drawn */
            d = text;
        }
        tx = x - eye * eye_shift(d, strength);
        if (tx < 0 || tx >= w || d < zrow[tx])
            continue;
        out[tx] = row[x];
        zrow[tx] = (int16_t)d;
    }
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

    if (s_kind == KIND_MENU && w <= FRONTBUFFER_MAX_STRIDE_BYTES) {
        compose_menu_row(fb, w, y, eye, strength, out, zrow);
        return;
    }
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

/* Both eyes of one row at once. The pitch is a shifted copy (memcpy, not a clamped pixel
 * loop), and the pass over what stands on it -- the part that costs -- reads each pixel once
 * and writes it to both eyes through a table of shifts. Same result as two ComposeRow calls. */
static void shifted_copy(uint8_t *out, const uint8_t *bg, int32_t w, int32_t sh)
{
    if (sh >= w || -sh >= w) { memset(out, bg[sh > 0 ? w - 1 : 0], (size_t)w); return; }
    if (sh > 0) {
        memcpy(out, bg + sh, (size_t)(w - sh));
        memset(out + w - sh, bg[w - 1], (size_t)sh);
    } else if (sh < 0) {
        memset(out, bg[0], (size_t)-sh);
        memcpy(out - sh, bg, (size_t)(w + sh));
    } else {
        memcpy(out, bg, (size_t)w);
    }
}

void Stereo_ComposeRowPair(const uint8_t *fb, int32_t w, int32_t y, int32_t strength,
                           uint8_t *outL, uint8_t *outR)
{
    static int8_t  zL[FRONTBUFFER_MAX_STRIDE_BYTES], zR[FRONTBUFFER_MAX_STRIDE_BYTES];
    static int8_t  lut[256];
    static int32_t lutFor = -1;
    StereoSlot *s = slot_for(fb, 0);
    const uint8_t *row = fb + y * w, *bg, *st;
    const int8_t *dp;
    int32_t pd, e, x;

    if (s_kind == KIND_MENU || !s || w > FRONTBUFFER_MAX_STRIDE_BYTES) {
        Stereo_ComposeRow(fb, w, y, -1, strength, outL);
        Stereo_ComposeRow(fb, w, y, +1, strength, outR);
        return;
    }
    if (lutFor != strength) {
        int32_t d;
        for (d = -128; d < 128; d++) lut[d + 128] = (int8_t)eye_shift(d, strength);
        lutFor = strength;
    }
    bg = s->bg + y * w;
    st = s->stamp + y * w;
    dp = s->dep + y * w;

    pd = ground_depth(y);
    e = eye_shift(pd, strength);
    shifted_copy(outL, bg, w, -e);
    shifted_copy(outR, bg, w, e);
    if (pd > 127) pd = 127;
    if (pd < -128) pd = -128;
    memset(zL, pd, (size_t)w);
    memset(zR, pd, (size_t)w);

    for (x = 0; x < w; x++) {
        uint8_t c = row[x];
        int32_t d, sh, t;
        if (c == st[x]) {
            if (dp[x] == STEREO_DEPTH_PITCH)
                continue;
            d = dp[x];
        } else {
            d = clamp_depth(g_stereoCfg.overlay);
        }
        sh = lut[d + 128];
        t = x + sh;                               /* left eye */
        if (t >= 0 && t < w && d >= zL[t]) { outL[t] = c; zL[t] = (int8_t)d; }
        t = x - sh;                               /* right eye */
        if (t >= 0 && t < w && d >= zR[t]) { outR[t] = c; zR[t] = (int8_t)d; }
    }
}

#endif /* SWOS_SDL_BUILD */
