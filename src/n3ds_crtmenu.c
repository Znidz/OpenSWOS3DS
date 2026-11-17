/*
 * n3ds_crtmenu.c -- the CRT filter's controls, on the bottom screen while a match is paused (3DS only).
 *
 * The filter is the ini's [video] crt_mask / crt_mask_strength / crt_vignette / crt_corners
 * (swos_settings.c, crt_effects.c). The GPU draws it over the match picture (n3ds_gpu.c); this is
 * where it is switched and tuned, live:
 *
 *   touch a mode button (OFF SCANLINES APERTURE SLOT) -- the switch
 *   touch or drag on a slider (STRENGTH, VIGNETTE, CORNERS); tap a name to switch that one off / on
 *   D-pad: up/down picks a row, left/right changes it
 *
 * Resuming (START) writes what changed back to SWOS.ini. Only matches drawn by the GPU carry the
 * filter (R + SELECT in a match switches the GPU drawing off).
 */
#ifdef __3DS__

#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include "n3ds.h"
#include "crt_effects.h"
#include "platform_sdl.h"
#include "swos_settings.h"

extern PlatformInitOptions *g_arcadeOpts;
extern const char *g_arcadeIniPath;

#define RGBC(r, g, b)  ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
#define BAR_X0  8
#define BAR_X1  312
#define MODE_Y0 42
#define MODE_Y1 68
#define SLD_Y0  86
#define SLD_STEP 38

static CrtEffectsConfig s_cfg;
static int32_t s_loaded, s_dirty, s_saveNeeded, s_sel;
static int32_t s_lastOn[3] = { 45, 30, 30 };     /* what a slider goes back to when its name is tapped on */

static const char *const kMode[4] = { "OFF", "SCANLINES", "APERTURE", "SLOT" };
static const CrtMaskKind kKind[4] = { CRT_MASK_OFF, CRT_MASK_SCANLINES, CRT_MASK_APERTURE, CRT_MASK_SLOT };
static const char *const kName[3] = { "STRENGTH", "VIGNETTE", "CORNERS" };

static int32_t mode_index(void)
{
    int32_t i;
    for (i = 0; i < 4; i++) if (kKind[i] == s_cfg.mask) return i;
    return 0;
}

static int32_t *slider(int32_t i)
{
    return i == 0 ? &s_cfg.mask_strength : (i == 1 ? &s_cfg.vignette : &s_cfg.corners);
}

static void changed(void)
{
    N3DSCrt_Set(&s_cfg);
    s_dirty = 1;
    s_saveNeeded = 1;
}

void N3DSCrtMenu_Sync(void)
{
    if (s_loaded || !g_arcadeOpts) return;
    s_loaded = 1;
    s_cfg.mask = (CrtMaskKind)g_arcadeOpts->crt_mask;
    s_cfg.mask_strength = g_arcadeOpts->crt_mask_strength;
    s_cfg.vignette = g_arcadeOpts->crt_vignette;
    s_cfg.corners = g_arcadeOpts->crt_corners;
    N3DSCrt_Set(&s_cfg);
    if (s_cfg.mask != CRT_MASK_OFF || s_cfg.vignette > 0 || s_cfg.corners > 0)
        fprintf(stderr, "[crt] from SWOS.ini: mask=%d strength=%d vignette=%d corners=%d\n",
                (int)s_cfg.mask, s_cfg.mask_strength, s_cfg.vignette, s_cfg.corners);
}

void N3DSCrtMenu_Enter(void)
{
    N3DSCrtMenu_Sync();
    s_dirty = 1;
}

void N3DSCrtMenu_Leave(void)
{
    if (!s_saveNeeded || !g_arcadeOpts || !g_arcadeIniPath) return;
    s_saveNeeded = 0;
    g_arcadeOpts->crt_mask = (int)s_cfg.mask;
    g_arcadeOpts->crt_mask_strength = s_cfg.mask_strength;
    g_arcadeOpts->crt_vignette = s_cfg.vignette;
    g_arcadeOpts->crt_corners = s_cfg.corners;
    Settings_Save(g_arcadeOpts, g_arcadeIniPath);
    fprintf(stderr, "[crt] saved to SWOS.ini: mask=%d strength=%d vignette=%d corners=%d\n",
            (int)s_cfg.mask, s_cfg.mask_strength, s_cfg.vignette, s_cfg.corners);
    fflush(stderr);
}

static void box(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t edge, uint16_t fill)
{
    N3DSScr_Fill(x0, y0, x1, y1, edge);
    N3DSScr_Fill(x0 + 1, y0 + 1, x1 - 1, y1 - 1, fill);
}

void N3DSCrtMenu_Draw(void)
{
    const uint16_t bg = RGBC(14, 18, 30), white = RGBC(235, 235, 235), yellow = RGBC(255, 230, 90);
    const uint16_t dim = RGBC(120, 128, 150), on = RGBC(250, 200, 40), off = RGBC(30, 36, 58);
    char buf[24];
    int32_t i, mi = mode_index();
    if (!s_dirty) return;
    s_dirty = 0;

    N3DSScr_Fill(0, 0, 320, 240, bg);
    N3DSScr_Text(8, 6, "PAUSED", 2, yellow);
    N3DSScr_Text(8, 28, "SCREEN FILTER - CHANGES SHOW AT ONCE", 1, dim);

    for (i = 0; i < 4; i++) {
        const int32_t x0 = BAR_X0 + i * 76, x1 = x0 + 72;
        const uint16_t edge = (s_sel == 0 && i == mi) ? white : RGBC(60, 70, 100);
        box(x0, MODE_Y0, x1, MODE_Y1, edge, i == mi ? on : off);
        N3DSScr_Text(x0 + (72 - N3DSScr_TextWidth(kMode[i], 1)) / 2, MODE_Y0 + 10, kMode[i], 1,
                     i == mi ? RGBC(20, 24, 40) : white);
    }

    for (i = 0; i < 3; i++) {
        const int32_t y = SLD_Y0 + i * SLD_STEP, v = *slider(i), fill = BAR_X0 + (BAR_X1 - BAR_X0) * v / 100;
        const int32_t usable = !(i == 0 && s_cfg.mask == CRT_MASK_OFF);     /* the grille's strength means nothing with it off */
        const uint16_t c = usable ? white : dim;
        N3DSScr_Text(BAR_X0, y, kName[i], 1, s_sel == i + 1 ? yellow : c);
        if (v > 0 || (i == 0 && usable)) snprintf(buf, sizeof buf, "%d", (int)v);
        else snprintf(buf, sizeof buf, "OFF");
        N3DSScr_Text(BAR_X1 - N3DSScr_TextWidth(buf, 1), y, buf, 1, c);
        box(BAR_X0, y + 12, BAR_X1, y + 26, s_sel == i + 1 ? white : RGBC(60, 70, 100), off);
        if (fill > BAR_X0 + 1)
            N3DSScr_Fill(BAR_X0 + 1, y + 13, fill, y + 25, usable ? on : RGBC(90, 80, 40));
        N3DSScr_Fill(fill - 1, y + 10, fill + 2, y + 28, white);              /* the knob */
    }
    N3DSScr_Text(8, 206, "TOUCH OR D-PAD.  START: RESUME", 1, dim);
    N3DSScr_Text(8, 220, "SAVED TO SWOS.INI WHEN YOU RESUME", 1, dim);
}

static int32_t clamp100(int32_t v) { return v < 0 ? 0 : (v > 100 ? 100 : v); }

void N3DSCrtMenu_Input(uint32_t held, uint32_t down, int32_t tx, int32_t ty)
{
    int32_t i;
    N3DSCrtMenu_Sync();
    if (held & KEY_TOUCH) {
        if (ty >= MODE_Y0 && ty <= MODE_Y1 && (down & KEY_TOUCH)) {
            i = (tx - BAR_X0) / 76;
            if (i >= 0 && i < 4 && kKind[i] != s_cfg.mask) { s_cfg.mask = kKind[i]; s_sel = 0; changed(); }
        }
        for (i = 0; i < 3; i++) {
            const int32_t y = SLD_Y0 + i * SLD_STEP;
            int32_t *v = slider(i);
            if (ty < y - 2 || ty > y + 30) continue;
            if ((down & KEY_TOUCH) && ty < y + 11 && tx < BAR_X0 + 90) {        /* its name: switch it off / back on */
                if (*v > 0) { s_lastOn[i] = *v; *v = 0; } else *v = s_lastOn[i];
                s_sel = i + 1; changed();
            } else if (ty >= y + 8 && tx >= BAR_X0 - 4) {                           /* the bar: set it where it is touched */
                const int32_t nv = clamp100((tx - BAR_X0) * 100 / (BAR_X1 - BAR_X0));
                if (nv != *v) { *v = nv; if (nv > 0) s_lastOn[i] = nv; s_sel = i + 1; changed(); }
            }
        }
    }
    if (down & KEY_UP)   { s_sel = (s_sel + 3) % 4; s_dirty = 1; }
    if (down & KEY_DOWN) { s_sel = (s_sel + 1) % 4; s_dirty = 1; }
    if (down & (KEY_LEFT | KEY_RIGHT)) {
        const int32_t d = (down & KEY_RIGHT) ? 1 : -1;
        if (s_sel == 0) {
            s_cfg.mask = kKind[(mode_index() + 4 + d) % 4];
            changed();
        } else {
            int32_t *v = slider(s_sel - 1);
            const int32_t nv = clamp100(*v + d * 5);
            if (nv != *v) { *v = nv; if (nv > 0) s_lastOn[s_sel - 1] = nv; changed(); }
        }
    }
}

#endif /* __3DS__ */
