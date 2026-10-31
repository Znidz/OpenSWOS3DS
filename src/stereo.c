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
/* the playing field in pitch pixels (the radar uses the same box) */
#define STEREO_FIELD_X0  81
#define STEREO_FIELD_X1  590
#define STEREO_FIELD_Y0  129
#define STEREO_FIELD_Y1  769
#define STEREO_HUD_Z_MIN    8000          /* sprites.c HUD_LAYER_Z_MIN */

/* Screen-pinned HUD sprites (score panel, names, numbers) are not part of the 3D scene: they are
 * kept apart here and drawn 1:1 over the scaled picture, so their small text is not resampled. */
#define STEREO_MAX_HUD   64
#define STEREO_HUD_POOL  32768
static StereoHudRect s_hudRect[STEREO_MAX_HUD];
static uint8_t       s_hudPoolBuf[STEREO_HUD_POOL];
static int32_t       s_hudN, s_hudUsed, s_hudCur = -1;

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
     48,    /* standsRise: the stands 6 px out                    */
     40,    /* standsMargin: pitch pixels past the touchlines     */
     24,    /* goalRise: a goal's base 3 px out, its bar further  */
     14,    /* tierRows: a tier of seating is 14 pitch rows deep  */
      8,    /* tierStep: each tier 1 px further out than the last */
     40,    /* standsMax: the top tier 5 px beyond the first      */
     40,    /* hoardRise: the boards 5 px out of the pitch        */
     92,    /* hoardY0: the boards' top row (world), top end       */
    103,    /* hoardY1: their bottom row                          */
    796,    /* hoardY2: the same at the bottom end                */
    807,    /* hoardY3                                            */
};

/* Where sprites were drawn, per row: a few [x0,x1) intervals, merged as they come. Present
 * only looks at these (and at rows something untracked drew on), not at every pixel. */
#define STEREO_MAX_SPANS 6
typedef struct {
    uint16_t x0[STEREO_MAX_SPANS], x1[STEREO_MAX_SPANS];
    uint8_t  n;
} RowSpans;

typedef struct {
    const uint8_t *fb;
    uint8_t       *bg;
    uint8_t       *stamp;
    int8_t        *dep;
    RowSpans      *spans;
    uint8_t       *hudMask;      /* 1 where a screen-pinned HUD sprite drew (drawn 1:1 instead) */
    uint8_t       *rowHud;       /* per row: any HUD pixel on it */
    int32_t        hasPitch;
} StereoSlot;

static void span_add(RowSpans *r, int32_t x0, int32_t x1)
{
    int32_t i;
    for (i = 0; i < r->n; i++)                      /* touching or overlapping one: extend it */
        if (x0 <= r->x1[i] && x1 >= r->x0[i]) {
            if (x0 < r->x0[i]) r->x0[i] = (uint16_t)x0;
            if (x1 > r->x1[i]) r->x1[i] = (uint16_t)x1;
            return;
        }
    if (r->n < STEREO_MAX_SPANS) {
        r->x0[r->n] = (uint16_t)x0;
        r->x1[r->n] = (uint16_t)x1;
        r->n++;
        return;
    }
    for (i = 1; i < r->n; i++) {                    /* full: one span over them all */
        if (r->x0[i] < r->x0[0]) r->x0[0] = r->x0[i];
        if (r->x1[i] > r->x1[0]) r->x1[0] = r->x1[i];
    }
    r->n = 1;
    if (x0 < r->x0[0]) r->x0[0] = (uint16_t)x0;
    if (x1 > r->x1[0]) r->x1[0] = (uint16_t)x1;
}

static StereoSlot s_slot[STEREO_SLOTS];
static int32_t    s_planeBytes;

/* The sprite being drawn. */
enum { MODE_NONE, MODE_GROUND, MODE_UPRIGHT, MODE_FLAT, MODE_HUD };
static int32_t s_mode;
enum { CUR_OTHER, CUR_SHADOW, CUR_BALL };
static int32_t s_isGoal;
static int32_t s_cur, s_shadowCentre = 100;  /* what is being drawn; where the ball's shadow is */
static int32_t s_rowBottom = -1; /* lowest screen row the sprite being drawn covers */
static int32_t s_groundRow;     /* screen row of the sprite's feet */
static int32_t s_flatDepth;
static int32_t s_groundSaved;   /* mode to go back to after Stereo_BeginGround(0) */

extern Sprite _ballSprite, _ballShadowSprite, _goal1TopSprite, _goal2BottomSprite;

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
    else if (!strcmp(k, "stands_rise"))   g_stereoCfg.standsRise   = (int16_t)v;
    else if (!strcmp(k, "stands_margin")) g_stereoCfg.standsMargin = (int16_t)v;
    else if (!strcmp(k, "goal_rise"))     g_stereoCfg.goalRise     = (int16_t)v;
    else if (!strcmp(k, "tier_rows"))     g_stereoCfg.tierRows     = (int16_t)v;
    else if (!strcmp(k, "tier_step"))     g_stereoCfg.tierStep     = (int16_t)v;
    else if (!strcmp(k, "stands_max"))    g_stereoCfg.standsMax    = (int16_t)v;
    else if (!strcmp(k, "hoard_rise"))    g_stereoCfg.hoardRise    = (int16_t)v;
    else if (!strcmp(k, "hoard_y0"))      g_stereoCfg.hoardY0      = (int16_t)v;
    else if (!strcmp(k, "hoard_y1"))      g_stereoCfg.hoardY1      = (int16_t)v;
    else if (!strcmp(k, "hoard_y2"))      g_stereoCfg.hoardY2      = (int16_t)v;
    else if (!strcmp(k, "hoard_y3"))      g_stereoCfg.hoardY3      = (int16_t)v;
    else fprintf(stderr, "[stereo] stereo.ini: unknown key '%s'\n", k);
}

static void write_cfg(FILE *f)
{
    fprintf(f, "; OpenSWOS 3D depth settings. Units: 1/8 pixel of disparity.\n"
               "; Positive comes out of the screen, negative goes into it.\n"
               "pitch_far = %d\npitch_near = %d\n"
               "; depth per pixel of height above the pitch, in 1/64 pixel\n"
               "height_gain = %d\nhud = %d\noverlay = %d\nlimit = %d\n"
               "; menus: the backdrop, text and pictures, buttons, the highlighted button\n"
               "menu_back = %d\nmenu_text = %d\nmenu_button = %d\nmenu_selected = %d\n"
               "; the stands round the pitch (how far out; where they start past the lines) and the goals\n"
               "stands_rise = %d\nstands_margin = %d\ngoal_rise = %d\n"
               "; tiers: rows per tier, depth per tier, the most; the advertising boards: depth, top and bottom world rows\n"
               "tier_rows = %d\ntier_step = %d\nstands_max = %d\nhoard_rise = %d\nhoard_y0 = %d\nhoard_y1 = %d\nhoard_y2 = %d\nhoard_y3 = %d\n",
            g_stereoCfg.pitchFar, g_stereoCfg.pitchNear, g_stereoCfg.heightGain,
            g_stereoCfg.hud, g_stereoCfg.overlay, g_stereoCfg.limit,
            g_stereoCfg.menuBack, g_stereoCfg.menuText, g_stereoCfg.menuButton,
            g_stereoCfg.menuSelected, g_stereoCfg.standsRise, g_stereoCfg.standsMargin,
            g_stereoCfg.goalRise, g_stereoCfg.tierRows, g_stereoCfg.tierStep,
            g_stereoCfg.standsMax, g_stereoCfg.hoardRise, g_stereoCfg.hoardY0,
            g_stereoCfg.hoardY1, g_stereoCfg.hoardY2, g_stereoCfg.hoardY3);

}

static char s_cfgDir[512];

void Stereo_LoadConfig(const char *dir)
{
    snprintf(s_cfgDir, sizeof s_cfgDir, "%s", dir ? dir : ".");
    char path[1024], line[256];
    FILE *f;
    snprintf(path, sizeof path, "%s/stereo.ini", dir ? dir : ".");
    f = fopen(path, "r");
    if (!f) {
        /* Write the defaults so there is a file to edit. */
        f = fopen(path, "w");
        if (f) {
            write_cfg(f);
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
        s->spans = (RowSpans *)calloc((size_t)(SCREEN_HEIGHT + 1), sizeof(RowSpans));
        s->hudMask = (uint8_t *)calloc(1, (size_t)s_planeBytes);
        s->rowHud = (uint8_t *)calloc((size_t)(SCREEN_HEIGHT + 1), 1);
        if (!s->bg || !s->stamp || !s->dep || !s->spans || !s->hudMask || !s->rowHud) {
            free(s->bg); free(s->stamp); free(s->dep); free(s->spans); free(s->hudMask); free(s->rowHud);
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

/* How far the terrain at distance `out` pitch pixels past the stands' start stands out of
 * the pitch: the first tier at standsRise, then a step per tier, up to standsMax more. */
static int32_t tier_rise(int32_t out)
{
    int32_t rows = g_stereoCfg.tierRows > 0 ? g_stereoCfg.tierRows : 1;
    int32_t up;
    if (out < 0) out = 0;
    up = (out / rows) * g_stereoCfg.tierStep;
    if (up > g_stereoCfg.standsMax) up = g_stereoCfg.standsMax;
    return g_stereoCfg.standsRise + up;
}

/* The terrain's rise at world row wy above or below the pitch (0 on and near the pitch). */
static int32_t terrain_rise_y(int32_t wy)
{
    const int32_t m = g_stereoCfg.standsMargin;
    if (g_stereoCfg.standsRise == 0) return 0;
    if (wy < STEREO_FIELD_Y0 - m) return tier_rise((STEREO_FIELD_Y0 - m) - wy);
    if (wy > STEREO_FIELD_Y1 + m) return tier_rise(wy - (STEREO_FIELD_Y1 + m));
    return 0;
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
    memset(s->spans, 0, (size_t)(SCREEN_HEIGHT + 1) * sizeof(RowSpans));
    memset(s->hudMask, 0, (size_t)n);
    memset(s->rowHud, 0, (size_t)(SCREEN_HEIGHT + 1));
    s_hudN = 0;
    s_hudUsed = 0;
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
    s_hudCur = -1;
    if (spr->z.s.i >= STEREO_HUD_Z_MIN) {
        s_mode = MODE_HUD;          /* kept apart: drawn 1:1 over the scaled picture */
        return;
    }
    s_groundRow = (int32_t)spr->y.s.i - (int32_t)Render_CameraY();
    s_cur = CUR_OTHER;
    s_isGoal = 0;
    s_isGoal = (spr == &_goal1TopSprite || spr == &_goal2BottomSprite);
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

    if (s_mode == MODE_HUD) {
        StereoHudRect *r;
        if (s_hudCur < 0) {
            if (s_hudN >= STEREO_MAX_HUD) return;
            r = &s_hudRect[s_hudN];
            r->x = (int16_t)dstX; r->y = (int16_t)y; r->w = (int16_t)sprW; r->h = 0;
            r->off = s_hudUsed;
            r->px = s_hudPoolBuf + s_hudUsed;
            s_hudCur = s_hudN++;
        }
        r = &s_hudRect[s_hudCur];
        if (y != r->y + r->h || s_hudUsed + sprW > STEREO_HUD_POOL)
            return;
        memcpy(s_hudPoolBuf + s_hudUsed, srcRow, (size_t)sprW);
        s_hudUsed += sprW;
        r->h++;
        for (px = 0; px < sprW; px++) {
            int32_t x = dstX + px;
            if (srcRow[px] && x >= 0 && x < w)
                s->hudMask[base + x] = 1;
        }
        s->rowHud[y] = 1;
        return;
    }

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
        /* a goal's height is measured from the goal line it stands on (where its shadow falls),
         * not from the bottom of its picture, which includes the net behind the line */
        int32_t feet = s_isGoal ? s_groundRow : (s_rowBottom >= 0 ? s_rowBottom : s_groundRow);
        int32_t h = feet - y;                      /* rows above the feet */
        if (h < 0) h = 0;
        int32_t base = terrain_rise_y(feet + (int32_t)Render_CameraY());  /* up on the terraces */
        if (s_isGoal)       /* a goal: its base out, the bar further, the net in between */
            d = clamp_depth(ground_depth(feet) + base + g_stereoCfg.goalRise + h * 3);
        else                /* a player leans out: feet near the ground, the head ~2 px out */
            d = clamp_depth(ground_depth(feet) + base + g_stereoCfg.heightGain / 2 + h * g_stereoCfg.heightGain / 16);
    }
    {
        int32_t lo = dstX < 0 ? 0 : dstX, hi = dstX + sprW > w ? w : dstX + sprW;
        if (hi > lo && y <= SCREEN_HEIGHT)
            span_add(&s->spans[y], lo, hi);
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

int32_t Stereo_HudRects(const StereoHudRect **list)
{
    *list = s_hudRect;
    return s_hudN;
}

int32_t Stereo_EyeShiftFor(int32_t depth, int32_t strength)
{
    return eye_shift(clamp_depth(depth), strength);
}

int32_t Stereo_HudDepth(void)
{
    return g_stereoCfg.hud;
}

/* Row y of fb with the HUD sprites' pixels taken back out (what was under them, from the
 * stamp plane), so the scaled picture has no HUD and it can be drawn 1:1 instead. */
const uint8_t *Stereo_PlainRow(const uint8_t *fb, int32_t w, int32_t y, uint8_t *tmp)
{
    StereoSlot *s = slot_for(fb, 0);
    const uint8_t *row = fb + y * w;
    int32_t x;
    if (!s || s_kind != KIND_MATCH || y > SCREEN_HEIGHT || !s->rowHud[y])
        return row;
    {
        const uint8_t *st = s->stamp + y * w, *m = s->hudMask + y * w;
        for (x = 0; x < w; x++)
            tmp[x] = m[x] ? st[x] : row[x];
    }
    return tmp;
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
    static uint8_t plain[FRONTBUFFER_MAX_STRIDE_BYTES];
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
    row = Stereo_PlainRow(fb, w, y, plain);

    pd = ground_depth(y);
    e = eye_shift(pd, strength);
    shifted_copy(outL, bg, w, -e);
    shifted_copy(outR, bg, w, e);
    if (pd > 127) pd = 127;
    if (pd < -128) pd = -128;
    memset(zL, pd, (size_t)w);
    memset(zR, pd, (size_t)w);

    /* The stands: the world outside the touchlines (plus a margin) stands out from the pitch.
     * Whole rows above and below the field, and the strips to its left and right. */
    if (g_stereoCfg.standsRise != 0) {
        const int32_t m = g_stereoCfg.standsMargin;
        const int32_t wy = y + (int32_t)Render_CameraY();
        const int32_t left = (STEREO_FIELD_X0 - m) - (int32_t)Render_CameraX();
        const int32_t right = (STEREO_FIELD_X1 + m) - (int32_t)Render_CameraX();
        int32_t seg[2][2] = { { 0, 0 }, { 0, 0 } }, k;
        int32_t rowRise = terrain_rise_y(wy);
        if (wy < STEREO_FIELD_Y0 - m || wy > STEREO_FIELD_Y1 + m) {
            seg[0][1] = w;
        } else {
            if (left > 0)  { seg[0][1] = left > w ? w : left; }
            if (right < w) { seg[1][0] = right < 0 ? 0 : right; seg[1][1] = w; }
        }
        for (k = 0; k < 2; k++) {
            int32_t a = seg[k][0], b = seg[k][1];
            int32_t lastRise = -9999, sd = 0, se = 0;
            for (x = a; x < b; x++) {
                int32_t sl, sr, rise = rowRise;
                if (rise == 0 && k == 0) rise = tier_rise((STEREO_FIELD_X0 - m) - ((int32_t)Render_CameraX() + x));
                else if (rise == 0)      rise = tier_rise(((int32_t)Render_CameraX() + x) - (STEREO_FIELD_X1 + m));
                if (rise != lastRise) {
                    lastRise = rise;
                    sd = clamp_depth(ground_depth(y) + rise);
                    se = eye_shift(sd, strength);
                }
                sl = x - se; sr = x + se;
                if (sl < 0) sl = 0; else if (sl >= w) sl = w - 1;
                if (sr < 0) sr = 0; else if (sr >= w) sr = w - 1;
                outL[x] = bg[sl];
                outR[x] = bg[sr];
                zL[x] = zR[x] = (int8_t)sd;
            }
        }
    }

    /* The advertising boards: a wall standing out of the pitch, its top further out. */
    if (g_stereoCfg.hoardRise != 0) {
        const int32_t wy = y + (int32_t)Render_CameraY();
        const int32_t top = wy >= g_stereoCfg.hoardY0 && wy <= g_stereoCfg.hoardY1;
        const int32_t bot = wy >= g_stereoCfg.hoardY2 && wy <= g_stereoCfg.hoardY3;
        if (top || bot) {
            /* a wall: its top (the row furthest up the screen) is the furthest out */
            int32_t hd = clamp_depth(ground_depth(y) + g_stereoCfg.hoardRise
                                     + (top ? g_stereoCfg.hoardY1 - wy : g_stereoCfg.hoardY3 - wy) * 2);
            int32_t he = eye_shift(hd, strength);
            for (x = 0; x < w; x++) {
                int32_t sl = x - he, sr = x + he;
                if (sl < 0) sl = 0; else if (sl >= w) sl = w - 1;
                if (sr < 0) sr = 0; else if (sr >= w) sr = w - 1;
                outL[x] = bg[sl];
                outR[x] = bg[sr];
                zL[x] = zR[x] = (int8_t)hd;
            }
        }
    }

#define PAIR_PX(X)                                                          \
    do {                                                                    \
        uint8_t c = row[X];                                                 \
        int32_t d, sh, t;                                                   \
        if (c == st[X]) {                                                   \
            if (dp[X] == STEREO_DEPTH_PITCH) continue;                      \
            d = dp[X];                                                      \
        } else {                                                            \
            d = clamp_depth(g_stereoCfg.overlay);                           \
        }                                                                   \
        sh = lut[d + 128];                                                  \
        t = (X) + sh;                               /* left eye */          \
        if (t >= 0 && t < w && d >= zL[t]) { outL[t] = c; zL[t] = (int8_t)d; } \
        t = (X) - sh;                               /* right eye */         \
        if (t >= 0 && t < w && d >= zR[t]) { outR[t] = c; zR[t] = (int8_t)d; } \
    } while (0)

    if (memcmp(row, st, (size_t)w) != 0) {
        /* something untracked drew on this row (text, an overlay): look at every pixel */
        for (x = 0; x < w; x++)
            PAIR_PX(x);
    } else if (y <= SCREEN_HEIGHT) {
        /* only sprites: look at where they were drawn */
        const RowSpans *rs = &s->spans[y];
        int32_t k;
        for (k = 0; k < rs->n; k++) {
            int32_t x1 = rs->x1[k];
            for (x = rs->x0[k]; x < x1; x++)
                PAIR_PX(x);
        }
    }
#undef PAIR_PX
}

#endif /* SWOS_SDL_BUILD */

/* ---- the checker's hooks (3DS debug screen) ----------------------------------- */

int32_t Stereo_SaveConfig(void)
{
    char path[1100];
    FILE *f;
    snprintf(path, sizeof path, "%s/stereo.ini", s_cfgDir[0] ? s_cfgDir : ".");
    f = fopen(path, "w");
    if (!f) return 0;
    write_cfg(f);
    fclose(f);
    return 1;
}

static const StereoParam k_params[] = {
    { "PITCH FAR",     &g_stereoCfg.pitchFar,     4 },
    { "PITCH NEAR",    &g_stereoCfg.pitchNear,    4 },
    { "HEIGHT GAIN",   &g_stereoCfg.heightGain,   2 },
    { "HUD",           &g_stereoCfg.hud,          2 },
    { "OVERLAY",       &g_stereoCfg.overlay,      2 },
    { "STANDS RISE",   &g_stereoCfg.standsRise,   4 },
    { "STANDS MARGIN", &g_stereoCfg.standsMargin, 2 },
    { "TIER ROWS",     &g_stereoCfg.tierRows,     1 },
    { "TIER STEP",     &g_stereoCfg.tierStep,     1 },
    { "STANDS MAX",    &g_stereoCfg.standsMax,    4 },
    { "HOARD RISE",    &g_stereoCfg.hoardRise,    4 },
    { "HOARD TOP",     &g_stereoCfg.hoardY0,      1 },
    { "HOARD BOTTOM",  &g_stereoCfg.hoardY1,      1 },
    { "HOARD 2 TOP",   &g_stereoCfg.hoardY2,      1 },
    { "HOARD 2 BOT",   &g_stereoCfg.hoardY3,      1 },
    { "GOAL RISE",     &g_stereoCfg.goalRise,     4 },
    { "LIMIT",         &g_stereoCfg.limit,        8 },
};

const StereoParam *Stereo_Params(int32_t *n)
{
    *n = (int32_t)(sizeof k_params / sizeof k_params[0]);
    return k_params;
}

/* What is at source pixel (x, y) of the presented frame: which part of the model drew it and
 * the depth it was given (1/8 px). */
int32_t Stereo_Probe(const uint8_t *fb, int32_t w, int32_t x, int32_t y, StereoProbe *p)
{
    StereoSlot *s = slot_for(fb, 0);
    int32_t base, wy, wx;
    memset(p, 0, sizeof *p);
    if (!s || x < 0 || x >= w || y < 0 || y >= _screen_height)
        return 0;
    base = y * w;
    wx = x + (int32_t)Render_CameraX();
    wy = y + (int32_t)Render_CameraY();
    p->wx = wx;
    p->wy = wy;
    p->idx = fb[base + x];
    p->bgIdx = s->bg[base + x];
    if (s->hudMask[base + x]) {
        p->kind = STEREO_PROBE_HUD;
        p->depth = Stereo_HudDepth();
        return 1;
    }
    if (fb[base + x] == s->stamp[base + x] && s->dep[base + x] != STEREO_DEPTH_PITCH) {
        p->kind = STEREO_PROBE_SPRITE;
        p->depth = s->dep[base + x];
        return 1;
    }
    if (fb[base + x] != s->stamp[base + x]) {
        p->kind = STEREO_PROBE_OVERLAY;
        p->depth = clamp_depth(g_stereoCfg.overlay);
        return 1;
    }
    {
        const int32_t m = g_stereoCfg.standsMargin;
        int32_t rise = 0;
        p->kind = STEREO_PROBE_PITCH;
        if (g_stereoCfg.hoardRise != 0 &&
            ((wy >= g_stereoCfg.hoardY0 && wy <= g_stereoCfg.hoardY1) ||
             (wy >= g_stereoCfg.hoardY2 && wy <= g_stereoCfg.hoardY3))) {
            const int32_t top = wy >= g_stereoCfg.hoardY0 && wy <= g_stereoCfg.hoardY1;
            p->kind = STEREO_PROBE_HOARDING;
            p->depth = clamp_depth(ground_depth(y) + g_stereoCfg.hoardRise
                                   + (top ? g_stereoCfg.hoardY1 - wy : g_stereoCfg.hoardY3 - wy) * 2);
            return 1;
        }
        if (g_stereoCfg.standsRise != 0) {
            rise = terrain_rise_y(wy);
            if (rise == 0 && (wx < STEREO_FIELD_X0 - m))        rise = tier_rise((STEREO_FIELD_X0 - m) - wx);
            else if (rise == 0 && (wx > STEREO_FIELD_X1 + m))   rise = tier_rise(wx - (STEREO_FIELD_X1 + m));
            if (rise) p->kind = STEREO_PROBE_STANDS;
        }
        p->depth = clamp_depth(ground_depth(y) + rise);
    }
    return 1;
}
