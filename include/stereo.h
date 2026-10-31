/*
 * stereo.h -- stereoscopic depth for the match view (SDL only; the 3DS uses it).
 *
 * The game draws one 8bpp picture. While stereo is on, every pixel it draws
 * also records how near the viewer it is, in a depth plane beside the
 * frontbuffer, and the pitch is kept in a "background" plane of its own. At
 * present time Stereo_ComposeRow builds each eye's row from those: the
 * background shifted by the pitch's own depth, then everything drawn over it
 * shifted by its depth, nearest last. Where a player moves aside in one eye,
 * the pitch behind it shows through from the background plane.
 *
 * Depth is a disparity in 1/8 pixel at the RENDER resolution; positive is
 * nearer the viewer (out of the screen), negative further (into it).
 *
 *   pitch        tilted: the far (top) end recedes, the near end is at the screen
 *   players      stand up: each row's height above the player's feet adds depth
 *   ball         its height above the pitch (z)
 *   shadows      flat on the pitch
 *   HUD sprites  (score, names: z >= 8000) at a fixed depth in front
 *   anything else drawn over the match (menus, overlays) at its own fixed depth
 *
 * Menus: the backdrop just behind the screen, text and pictures in front of it,
 * the coloured button rectangles further out, the highlighted one furthest.
 *
 * Nothing here is match state: it is never read by the simulation, saved, or
 * sent anywhere. With stereo off (every target but the 3DS, unless the
 * SWOS_STEREO environment variable asks for the desktop test view) every hook
 * is one test of g_stereoOn.
 */
#ifndef STEREO_H
#define STEREO_H

#include <stdint.h>

#ifdef SWOS_SDL_BUILD

struct Sprite;
struct SpriteGraphics;

#define STEREO_DEPTH_PITCH  ((int8_t)-128)   /* depth plane marker: "the pitch's own" */

typedef struct StereoConfig {
    int16_t pitchFar;     /* pitch disparity at the top of the view, 1/8 px    */
    int16_t pitchNear;    /* ... and at the bottom                             */
    int16_t heightGain;   /* per pixel of height above the pitch, 1/64 px      */
    int16_t hud;          /* score panel, player name                          */
    int16_t overlay;      /* anything else drawn over a match                  */
    int16_t limit;        /* clamp, 1/8 px either way                          */
    int16_t menuBack;     /* menus: the backdrop                               */
    int16_t menuText;     /*        text and pictures drawn on it              */
    int16_t menuButton;   /*        the coloured rectangles                    */
    int16_t menuSelected; /*        the highlighted rectangle                  */
} StereoConfig;

extern int32_t       g_stereoOn;        /* depth is being recorded */
extern StereoConfig  g_stereoCfg;

/* Read `<dir>/stereo.ini` (key = value; missing keys keep the defaults). */
void Stereo_LoadConfig(const char *dir);
void Stereo_Enable(int32_t on);

/* The pitch was just drawn into `fb` (whole view). */
void Stereo_PitchDrawn(const uint8_t *fb);

/* Wrap the drawing of one sprite. Pass NULL to end. */
void Stereo_BeginSprite(const struct Sprite *s, const struct SpriteGraphics *g);
/* Draws between Stereo_BeginGround(1) and (0) are pitch (the animated crowd). */
/* The screen rows the sprite about to be marked covers (its lowest row = its feet). */
void Stereo_SetRows(int32_t top, int32_t bottom);
void Stereo_BeginGround(int32_t on);

/* The sprite blit, once per drawn row: the row's opaque pixels (src[px] != 0)
 * went to fb row `y` at columns dstX + px. Records their depth. Nothing is
 * recorded for a draw outside Stereo_BeginSprite/BeginGround, so whatever else
 * draws over a match (menus, text) is told apart at present time. */
void Stereo_MarkRow(const uint8_t *fb, int32_t y, int32_t dstX,
                    const uint8_t *srcRow, int32_t sprW);

/* Once per presented frame, before Stereo_ComposeRow: is `fb` a frame with depth?
 * A match (the pitch was drawn into it) or a menu (drawn over its backdrop, which
 * _frontbufferMaskBuffer still holds). Anything else is shown flat. */
int32_t Stereo_FrameHasDepth(const uint8_t *fb);
/* What that call found: a match, a menu, or neither. */
enum { STEREO_FRAME_FLAT = 0, STEREO_FRAME_MATCH = 1, STEREO_FRAME_MENU = 2 };
int32_t Stereo_FrameKind(void);

/* Build one eye's row `y` of `fb` into `out` (w palette indices).
 * eye: -1 left, +1 right. strength: 0..256 (the 3D slider). */
void Stereo_ComposeRow(const uint8_t *fb, int32_t w, int32_t y, int32_t eye,
                       int32_t strength, uint8_t *out);
/* Both eyes of row y in one pass (the same rows ComposeRow gives, faster). */
void Stereo_ComposeRowPair(const uint8_t *fb, int32_t w, int32_t y, int32_t strength,
                           uint8_t *outL, uint8_t *outR);

#endif /* SWOS_SDL_BUILD */
#endif /* STEREO_H */
